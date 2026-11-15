// re-wor: PASADA SANDBOX. Dibuja con un HLSL que da el juego (compilado en tiempo de ejecucion con
// d3dcompiler_47.dll, que trae Windows) encima del color RT 0 del ultimo draw que el juego marco
// (rex/graphics/draw_hook.h). Usa las mismas herramientas que las copias/limpiezas internas de este cache
// (SetExternalPipeline, SetExternalGraphicsRootSignature, RTV de dibujo) y deja el estado invalidado para que el
// siguiente draw del juego lo vuelva a poner todo.
#include <algorithm>
#include <cstdarg>
#include <climits>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include <d3dcommon.h>

#include <rex/graphics/d3d12/command_processor.h>
#include <rex/graphics/d3d12/render_target_cache.h>
#include <rex/graphics/draw_hook.h>
#include <rex/ui/d3d12/d3d12_util.h>

namespace rex::graphics::d3d12 {

namespace {
using D3DCompileFn = HRESULT(WINAPI*)(LPCVOID, SIZE_T, LPCSTR, const D3D_SHADER_MACRO*, ID3DInclude*, LPCSTR,
                                      LPCSTR, UINT, UINT, ID3DBlob**, ID3DBlob**);
constexpr uint32_t kCbSlotBytes = 1024;   // 256 floats
constexpr uint32_t kCbSlots = 512;        // anillo: de sobra para los frames en vuelo

void Log(const char* fmt, ...) {
  auto log = GetDrawHooks().log;
  if (!log) return;
  char buf[2048];
  va_list ap;
  va_start(ap, fmt);
  std::vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  log(buf);
}
}  // namespace

struct D3D12RenderTargetCache::SandboxState {
  D3DCompileFn compile = nullptr;
  bool compile_tried = false;
  uint32_t version = 0xFFFFFFFFu;
  std::vector<uint8_t> vs, ps, vs_part, ps_part;
  ID3D12RootSignature* root = nullptr;
  struct Psos { ID3D12PipelineState* full = nullptr; ID3D12PipelineState* part = nullptr; };
  std::map<DXGI_FORMAT, Psos> psos;
  std::vector<ID3D12PipelineState*> graveyard;
  std::vector<ID3D12Resource*> old_copies;   // los PSO viejos pueden estar en listas en vuelo: no se liberan
  ID3D12Resource* copy = nullptr;
  D3D12_RESOURCE_DESC copy_desc = {};
  D3D12_RESOURCE_STATES copy_state = D3D12_RESOURCE_STATE_COPY_DEST;
  ID3D12Resource* cb = nullptr;
  uint8_t* cb_map = nullptr;
  uint32_t cb_slot = 0;
  int errors = 0;
  bool warned_path = false, warned_msaa = false;
  uint64_t passes = 0;
};

namespace {
bool Compile(D3DCompileFn fn, const std::string& src, const char* entry, const char* target, std::vector<uint8_t>& out) {
  ID3DBlob* code = nullptr;
  ID3DBlob* err = nullptr;
  HRESULT hr = fn(src.data(), src.size(), "sandbox.hlsl", nullptr, nullptr, entry, target,
                  (1 << 15) /*D3DCOMPILE_OPTIMIZATION_LEVEL3*/, 0, &code, &err);
  if (err) {
    Log("[Sandbox/GPU] %s (%s): %s", entry, SUCCEEDED(hr) ? "avisos" : "ERROR",
        static_cast<const char*>(err->GetBufferPointer()));
    err->Release();
  }
  if (FAILED(hr) || !code) return false;
  out.assign(static_cast<const uint8_t*>(code->GetBufferPointer()),
             static_cast<const uint8_t*>(code->GetBufferPointer()) + code->GetBufferSize());
  code->Release();
  return true;
}

ID3D12PipelineState* MakePso(ID3D12Device* device, ID3D12RootSignature* root, const std::vector<uint8_t>& vs,
                             const std::vector<uint8_t>& ps, DXGI_FORMAT format, bool blend) {
  D3D12_GRAPHICS_PIPELINE_STATE_DESC d = {};
  d.pRootSignature = root;
  d.VS = {vs.data(), vs.size()};
  d.PS = {ps.data(), ps.size()};
  auto& rt = d.BlendState.RenderTarget[0];
  rt.BlendEnable = blend;
  rt.SrcBlend = D3D12_BLEND_SRC_ALPHA;
  rt.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
  rt.BlendOp = D3D12_BLEND_OP_ADD;
  rt.SrcBlendAlpha = D3D12_BLEND_ZERO;
  rt.DestBlendAlpha = D3D12_BLEND_ONE;
  rt.BlendOpAlpha = D3D12_BLEND_OP_ADD;
  rt.LogicOp = D3D12_LOGIC_OP_NOOP;
  rt.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_RED | D3D12_COLOR_WRITE_ENABLE_GREEN |
                             D3D12_COLOR_WRITE_ENABLE_BLUE;   // el alfa del juego no se toca
  d.SampleMask = UINT_MAX;
  d.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
  d.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
  d.RasterizerState.DepthClipEnable = TRUE;
  d.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  d.NumRenderTargets = 1;
  d.RTVFormats[0] = format;
  d.SampleDesc.Count = 1;
  ID3D12PipelineState* pso = nullptr;
  if (FAILED(device->CreateGraphicsPipelineState(&d, IID_PPV_ARGS(&pso)))) {
    Log("[Sandbox/GPU] no se pudo crear el pipeline (formato DXGI %u)", unsigned(format));
    return nullptr;
  }
  return pso;
}
}  // namespace

void D3D12RenderTargetCache::SandboxPass(uint32_t scissor_br, uint32_t rt_index) {
  const DrawHooks& hooks = GetDrawHooks();
  if (!hooks.hlsl || !hooks.constants) return;
  if (!sandbox_) sandbox_ = new SandboxState();
  SandboxState& S = *sandbox_;
  if (S.errors > 20) return;   // algo va muy mal: no insistir cada frame

  if (GetPath() != Path::kHostRenderTargets) {
    if (!S.warned_path) {
      S.warned_path = true;
      Log("[Sandbox/GPU] el render va por ROV (GPU Intel?): la pasada sandbox necesita render_target_path_d3d12 = "
          "\"rtv\" o ejecutar el juego en la GPU dedicada");
    }
    return;
  }
  RenderTarget* const* rts = last_update_accumulated_render_targets();
  if (rt_index > 3 || !rts || !rts[1 + rt_index]) return;
  auto& rt = *static_cast<D3D12RenderTarget*>(rts[1 + rt_index]);
  if (rt.key().msaa_samples != xenos::MsaaSamples::k1X) {
    if (!S.warned_msaa) { S.warned_msaa = true; Log("[Sandbox/GPU] el render target marcado tiene MSAA: no soportado aun"); }
    return;
  }
  const ui::d3d12::D3D12Provider& provider = command_processor_.GetD3D12Provider();
  ID3D12Device* device = provider.GetDevice();

  // ---- compilar (y recompilar en caliente) ----
  if (!S.compile_tried) {
    S.compile_tried = true;
    if (HMODULE m = LoadLibraryW(L"d3dcompiler_47.dll"))
      S.compile = reinterpret_cast<D3DCompileFn>(GetProcAddress(m, "D3DCompile"));
    if (!S.compile) Log("[Sandbox/GPU] falta d3dcompiler_47.dll: sin pasada sandbox");
  }
  if (!S.compile) { S.errors = 1000; return; }
  if (S.version != hooks.hlsl_version) {
    S.version = hooks.hlsl_version;
    const std::string src = hooks.hlsl;
    std::vector<uint8_t> vs, ps, vsp, psp;
    const bool has_part = src.find("VSPart") != std::string::npos;
    bool ok = Compile(S.compile, src, "VSMain", "vs_5_0", vs) && Compile(S.compile, src, "PSMain", "ps_5_0", ps);
    if (ok && has_part) ok = Compile(S.compile, src, "VSPart", "vs_5_0", vsp) && Compile(S.compile, src, "PSPart", "ps_5_0", psp);
    if (ok) {
      S.vs.swap(vs); S.ps.swap(ps); S.vs_part.swap(vsp); S.ps_part.swap(psp);
      for (auto& [f, p] : S.psos) { if (p.full) S.graveyard.push_back(p.full); if (p.part) S.graveyard.push_back(p.part); }
      S.psos.clear();
      Log("[Sandbox/GPU] shader compilado (version %u%s)", unsigned(S.version), has_part ? ", con particulas" : "");
    } else {
      Log("[Sandbox/GPU] el shader no compila: se sigue usando el anterior");
    }
  }
  if (S.vs.empty()) return;

  if (!S.root) {
    D3D12_DESCRIPTOR_RANGE range = {};
    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    range.NumDescriptors = 1;
    D3D12_ROOT_PARAMETER params[2] = {};
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[1].DescriptorTable.NumDescriptorRanges = 1;
    params[1].DescriptorTable.pDescriptorRanges = &range;
    params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_STATIC_SAMPLER_DESC samplers[2] = {};
    for (uint32_t i = 0; i < 2; ++i) {
      samplers[i].Filter = i == 0 ? D3D12_FILTER_MIN_MAG_MIP_LINEAR : D3D12_FILTER_MIN_MAG_MIP_POINT;
      samplers[i].AddressU = samplers[i].AddressV = samplers[i].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
      samplers[i].MaxLOD = D3D12_FLOAT32_MAX;
      samplers[i].ShaderRegister = i;
      samplers[i].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    }
    D3D12_ROOT_SIGNATURE_DESC desc = {};
    desc.NumParameters = 2;
    desc.pParameters = params;
    desc.NumStaticSamplers = 2;
    desc.pStaticSamplers = samplers;
    S.root = ui::d3d12::util::CreateRootSignature(provider, desc);
    if (!S.root) { Log("[Sandbox/GPU] no se pudo crear la firma raiz"); S.errors = 1000; return; }
  }
  if (!S.cb) {
    D3D12_RESOURCE_DESC bd = {};
    bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bd.Width = uint64_t(kCbSlotBytes) * kCbSlots;
    bd.Height = bd.DepthOrArraySize = bd.MipLevels = 1;
    bd.SampleDesc.Count = 1;
    bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if (FAILED(device->CreateCommittedResource(&ui::d3d12::util::kHeapPropertiesUpload, D3D12_HEAP_FLAG_NONE, &bd,
                                               D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&S.cb))) ||
        FAILED(S.cb->Map(0, nullptr, reinterpret_cast<void**>(&S.cb_map)))) {
      Log("[Sandbox/GPU] no se pudo crear el buffer de constantes");
      S.errors = 1000;
      return;
    }
  }

  ID3D12Resource* res = rt.resource();
  const D3D12_RESOURCE_DESC rdesc = res->GetDesc();
  const DXGI_FORMAT draw_format = GetColorDrawDXGIFormat(rt.key().GetColorFormat());
  auto& psos = S.psos[draw_format];
  if (!psos.full) {
    psos.full = MakePso(device, S.root, S.vs, S.ps, draw_format, false);
    if (!S.vs_part.empty()) psos.part = MakePso(device, S.root, S.vs_part, S.ps_part, draw_format, true);
    if (!psos.full) { ++S.errors; return; }
  }

  // ---- copia de la imagen (la pasada lee de la copia y escribe en el RT) ----
  if (!S.copy || S.copy_desc.Width != rdesc.Width || S.copy_desc.Height != rdesc.Height ||
      S.copy_desc.Format != rdesc.Format) {
    if (S.copy) S.old_copies.push_back(S.copy);   // puede estar en uso por la GPU: no se libera
    D3D12_RESOURCE_DESC cd = rdesc;
    cd.Flags = D3D12_RESOURCE_FLAG_NONE;
    S.copy = nullptr;
    if (FAILED(device->CreateCommittedResource(&ui::d3d12::util::kHeapPropertiesDefault, D3D12_HEAP_FLAG_NONE, &cd,
                                               D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&S.copy)))) {
      Log("[Sandbox/GPU] no se pudo crear la copia de la imagen");
      ++S.errors;
      return;
    }
    S.copy_desc = cd;
    S.copy_state = D3D12_RESOURCE_STATE_COPY_DEST;
  }
  DeferredCommandList& cl = command_processor_.GetDeferredCommandList();
  command_processor_.PushTransitionBarrier(res, rt.SetResourceState(D3D12_RESOURCE_STATE_COPY_SOURCE),
                                           D3D12_RESOURCE_STATE_COPY_SOURCE);
  command_processor_.PushTransitionBarrier(S.copy, S.copy_state, D3D12_RESOURCE_STATE_COPY_DEST);
  command_processor_.SubmitBarriers();
  cl.D3DCopyResource(S.copy, res);
  command_processor_.PushTransitionBarrier(res, rt.SetResourceState(D3D12_RESOURCE_STATE_RENDER_TARGET),
                                           D3D12_RESOURCE_STATE_RENDER_TARGET);
  command_processor_.PushTransitionBarrier(S.copy, D3D12_RESOURCE_STATE_COPY_DEST,
                                           D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
  S.copy_state = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
  command_processor_.SubmitBarriers();

  ui::d3d12::util::DescriptorCpuGpuHandlePair srv;
  if (!command_processor_.RequestOneUseSingleViewDescriptors(1, &srv)) return;
  D3D12_SHADER_RESOURCE_VIEW_DESC sd = {};
  sd.Format = draw_format;
  sd.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
  sd.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  sd.Texture2D.MipLevels = 1;
  device->CreateShaderResourceView(S.copy, &sd, srv.first);

  // ---- zona dibujada y constantes ----
  SandboxPassInfo info;
  info.scale_x = draw_resolution_scale_x();
  info.scale_y = draw_resolution_scale_y();
  uint32_t w = (scissor_br & 0x7FFF) * info.scale_x, h = ((scissor_br >> 16) & 0x7FFF) * info.scale_y;
  if (!w || w > rdesc.Width) w = uint32_t(rdesc.Width);
  if (!h || h > rdesc.Height) h = rdesc.Height;
  info.width = w;
  info.height = h;
  info.dxgi_format = uint32_t(draw_format);
  info.tex_width = uint32_t(rdesc.Width);
  info.tex_height = rdesc.Height;
  if (hooks.before_pass) hooks.before_pass(info);
  const uint32_t slot = S.cb_slot++ % kCbSlots;
  std::memset(S.cb_map + slot * kCbSlotBytes, 0, kCbSlotBytes);
  std::memcpy(S.cb_map + slot * kCbSlotBytes, hooks.constants, std::min<uint32_t>(hooks.constant_count, 256) * 4);
  const D3D12_GPU_VIRTUAL_ADDRESS cb_addr = S.cb->GetGPUVirtualAddress() + uint64_t(slot) * kCbSlotBytes;

  // ---- dibujar ----
  D3D12_CPU_DESCRIPTOR_HANDLE rtv = rt.descriptor_draw().GetHandle();
  cl.D3DOMSetRenderTargets(1, &rtv, FALSE, nullptr);
  are_current_command_list_render_targets_valid_ = false;   // el siguiente draw del juego vuelve a poner los suyos
  command_processor_.SetExternalGraphicsRootSignature(S.root);
  command_processor_.SetExternalPipeline(psos.full);
  command_processor_.SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  D3D12_VIEWPORT vp = {0.f, 0.f, float(w), float(h), 0.f, 1.f};
  command_processor_.SetViewport(vp);
  D3D12_RECT sc = {0, 0, LONG(w), LONG(h)};
  command_processor_.SetScissorRect(sc);
  cl.D3DSetGraphicsRootConstantBufferView(0, cb_addr);
  cl.D3DSetGraphicsRootDescriptorTable(1, srv.second);
  cl.D3DDrawInstanced(3, 1, 0, 0);
  if (psos.part && hooks.particle_instances) {
    command_processor_.SetExternalPipeline(psos.part);
    cl.D3DDrawInstanced(6, std::min<uint32_t>(hooks.particle_instances, 65536), 0, 0);
  }
  if (S.passes++ == 0)
    Log("[Sandbox/GPU] primera pasada: %ux%u, formato DXGI %u, escala %ux%u", w, h, unsigned(draw_format),
        info.scale_x, info.scale_y);
}

}  // namespace rex::graphics::d3d12
