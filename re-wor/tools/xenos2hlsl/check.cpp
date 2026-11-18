// xenos2hlsl_check: comprueba que el HLSL de xenos2hlsl calcula lo mismo que la 360 (docs/RENDER_NATIVO.md, fase 1).
//
// Por cada shader: estado aleatorio reproducible (constantes, bools, bucles, memoria de vertices, interpoladores,
// un color por textura) -> se ejecuta en la referencia (ref_interpreter: interprete del SDK alineado con el
// traductor DXBC) y en una GPU D3D11 (WARP por defecto) con el HLSL compilado -> se comparan las salidas:
//   vertice: posicion e interpoladores (capturados con stream output), 64 vertices;
//   pixel: colores 0..3, profundidad y descarte, 64 pixeles (puntos de 1 pixel con los interpoladores fijos).
//
// Uso: xenos2hlsl_check <entrada.xsh | carpeta> [--hw | --vk] [--seed N] [--cases N] [--only HASH] [--json salida.json]
//      --vk: Vulkan 1.2 con SPIR-V de dxc (dxcompiler.dll de --dxc, de DXC_SPIRV o de cache/tools/dxc-*/bin/x64)
//      depuracion: --dump (valores de cada caso), --hlsl archivo (con --only), --fast (sin IEEE estricto, D3D11)
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include <fmt/format.h>

#include <rex/graphics/flags.h>
#include <rex/graphics/format/ucode.h>
#include <rex/graphics/pipeline/shader/shader.h>
#include <rex/graphics/register_file.h>
#include <rex/graphics/registers.h>
#include <rex/string/buffer.h>

#include "check_vk.h"
#include "hlsl_translator.h"
#include "ref_interpreter.h"
#include "xsh.h"

REXCVAR_DEFINE_STRING(dump_shaders, "", "GPU", "Path to dump shaders to");

using Microsoft::WRL::ComPtr;
using namespace rex::graphics;
namespace x2h = re_wor::xenos2hlsl;

namespace {

constexpr uint32_t kMemSize = 4u << 20;      // memoria de vertices de prueba
constexpr uint32_t kBindingBytes = 128u << 10;
constexpr float kSentinel = -7777.0f;       // color de fondo: pixel descartado

// ------------------------------------------------------------------------------------------- estado aleatorio
struct State {
  float c[256][4];
  uint32_t b[8];
  uint32_t l[32];
  uint32_t fc[192];
  std::vector<uint8_t> mem;
};

float RandValue(std::mt19937& rng) {
  std::uniform_int_distribution<int> pick(0, 99);
  const int p = pick(rng);
  if (p < 8) return 0.0f;
  if (p < 12) return 1.0f;
  if (p < 15) return -1.0f;
  if (p < 18) return 0.5f;
  return std::uniform_real_distribution<float>(-3.0f, 3.0f)(rng);
}

uint32_t Swap(uint32_t v, uint32_t e) {   // igual que xe_swap / xenos::GpuSwap
  if (e == 1 || e == 2) v = ((v & 0x00FF00FFu) << 8) | ((v >> 8) & 0x00FF00FFu);
  if (e == 2 || e == 3) v = (v << 16) | (v >> 16);
  return v;
}

void MakeState(State& s, const Shader& sh, std::mt19937& rng) {
  for (auto& v : s.c)
    for (float& f : v) f = RandValue(rng);
  for (auto& w : s.b) w = rng();
  for (auto& w : s.l) {
    const uint32_t count = rng() % 4, start = rng() % 4, step = rng() % 3;
    w = count | (start << 8) | (step << 16);
  }
  std::memset(s.fc, 0, sizeof(s.fc));
  s.mem.assign(kMemSize, 0);
  // cada constante de fetch de vertices usada: su zona de memoria, con un orden de bytes al azar y palabras que,
  // ya en orden, son floats razonables (los formatos empaquetados ven bits al azar igual)
  uint32_t zone = 0;
  for (const auto& vb : sh.vertex_bindings()) {
    const uint32_t k = vb.fetch_constant;
    const uint32_t base = (zone++ % (kMemSize / kBindingBytes)) * kBindingBytes;
    const uint32_t endian = rng() % 4;
    s.fc[k * 2 + 0] = 3u | base;                           // tipo 3 (vertices), direccion en bytes
    s.fc[k * 2 + 1] = endian | ((kBindingBytes / 4) << 2);  // orden de bytes, tamano en palabras
    for (uint32_t o = 0; o < kBindingBytes; o += 4) {
      float f = RandValue(rng);
      uint32_t bits;
      std::memcpy(&bits, &f, 4);
      if (rng() % 4 == 0) bits = rng();   // tambien bits sueltos (enteros, empaquetados)
      bits = Swap(bits, endian);
      std::memcpy(&s.mem[base + o], &bits, 4);
    }
  }
}

void FillRegisters(RegisterFile& rf, const State& s, bool pixel) {
  std::memset(rf.values, 0, sizeof(rf.values));
  const uint32_t cbase = pixel ? 256 : 0;
  std::memcpy(&rf.values[XE_GPU_REG_SHADER_CONSTANT_000_X + 4 * cbase], s.c, sizeof(s.c));
  rf.values[pixel ? XE_GPU_REG_SQ_PS_CONST : XE_GPU_REG_SQ_VS_CONST] = cbase | (255u << 12);
  std::memcpy(&rf.values[XE_GPU_REG_SHADER_CONSTANT_BOOL_000_031], s.b, sizeof(s.b));
  std::memcpy(&rf.values[XE_GPU_REG_SHADER_CONSTANT_LOOP_00], s.l, sizeof(s.l));
  std::memcpy(&rf.values[XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0], s.fc, sizeof(s.fc));
}

// ------------------------------------------------------------------------------------------- referencia
struct Outputs {
  float v[18][4];      // vertice: 0..15 interpoladores, 16 posicion | pixel: 0..3 colores, 4 profundidad (x)
  uint8_t mask[18];
  bool killed;
};

class Sink : public refcheck::RefInterpreter::ExportSink {
 public:
  Outputs* out = nullptr;
  bool pixel = false;
  void Export(ucode::ExportRegister reg, const float* value, uint32_t mask) override {
    const uint32_t r = uint32_t(reg);
    int slot = -1;
    if (!pixel) {
      if (r < 16) slot = int(r);
      else if (reg == ucode::ExportRegister::kVSPosition) slot = 16;
    } else {
      if (r < 4) slot = int(r);
      else if (reg == ucode::ExportRegister::kPSDepth) slot = 4, mask &= 1;
    }
    if (slot < 0) return;
    for (uint32_t i = 0; i < 4; ++i)
      if (mask & (1u << i)) out->v[slot][i] = value[i];
    out->mask[slot] |= uint8_t(mask);
  }
};

// ------------------------------------------------------------------------------------------- GPU
struct Gpu {
  ComPtr<ID3D11Device> dev;
  ComPtr<ID3D11DeviceContext> ctx;
  ComPtr<ID3D11Buffer> cb[4], mem, so, so_staging, ps_in;
  ComPtr<ID3D11ShaderResourceView> mem_srv, ps_in_srv;
  ComPtr<ID3D11ShaderResourceView> tex2d[32], tex3d[32], texcube[32];
  ComPtr<ID3D11SamplerState> samp;
  ComPtr<ID3D11Texture2D> rt[4], rt_staging, ds, ds_staging;
  ComPtr<ID3D11RenderTargetView> rtv[4];
  ComPtr<ID3D11DepthStencilView> dsv;
  ComPtr<ID3D11DepthStencilState> dss;
  ComPtr<ID3D11VertexShader> passthrough_vs;
  float tex_colors[32][4];
  uint32_t cases = 64;
};

std::string g_common;
// IEEE estricto por defecto: sin el, fxc y los drivers cambian max / comparaciones de forma que pierden los NaN
// y en una GPU real salen ~5 % de shaders distintos de la 360; --fast lo quita para comparar
UINT g_compile_flags = D3DCOMPILE_IEEE_STRICTNESS;

class Include : public ID3DInclude {
 public:
  HRESULT __stdcall Open(D3D_INCLUDE_TYPE, LPCSTR, LPCVOID, LPCVOID* data, UINT* bytes) override {
    *data = g_common.data();
    *bytes = UINT(g_common.size());
    return S_OK;
  }
  HRESULT __stdcall Close(LPCVOID) override { return S_OK; }
};

bool Compile(const std::string& src, const char* profile, ComPtr<ID3DBlob>& code, std::string& err) {
  Include inc;
  ComPtr<ID3DBlob> e;
  HRESULT hr = D3DCompile(src.data(), src.size(), "shader.hlsl", nullptr, &inc, "main", profile,
                          D3DCOMPILE_OPTIMIZATION_LEVEL1 | g_compile_flags, 0, &code, &e);
  if (FAILED(hr)) {   // como validate.py: el optimizador de fxc se rompe con algunas maquinas de estados
    e.Reset();
    hr = D3DCompile(src.data(), src.size(), "shader.hlsl", nullptr, &inc, "main", profile,
                    D3DCOMPILE_SKIP_OPTIMIZATION | g_compile_flags, 0, &code, &e);
  }
  if (FAILED(hr)) err = e ? std::string(static_cast<const char*>(e->GetBufferPointer()), e->GetBufferSize()) : "?";
  return SUCCEEDED(hr);
}

ComPtr<ID3D11Buffer> MakeBuffer(ID3D11Device* dev, UINT size, UINT bind, D3D11_USAGE usage = D3D11_USAGE_DEFAULT,
                                UINT cpu = 0, UINT misc = 0, UINT stride = 0) {
  D3D11_BUFFER_DESC d = {size, usage, bind, cpu, misc, stride};
  ComPtr<ID3D11Buffer> b;
  dev->CreateBuffer(&d, nullptr, &b);
  return b;
}

// texturas de un texel: el tfetch da el color de su constante de fetch sea cual sea la coordenada o el filtro
void MakeTexColors(Gpu& g) {
  std::mt19937 rng(12345);
  for (auto& c : g.tex_colors)
    for (float& f : c) f = std::uniform_real_distribution<float>(0.0f, 1.0f)(rng);
}

// constantes del sistema (b3) para la prueba: sin escala de NDC, sin prueba alfa, sin PsParamGen
struct SysConstants {
  float ndc_scale[4], ndc_offset[4];
  uint32_t flags, alpha_func;
  float alpha_ref;
  uint32_t param_gen;
  float res_scale[2], pad[2];
};
const SysConstants kSys = {{1, 1, 1, 1}, {0, 0, 0, 0}, 0, 7, 0.0f, 0xFFFF, {1, 1}, {0, 0}};

// dxcompiler.dll con SPIR-V: --dxc, la de DXC_SPIRV (ruta de dxc.exe o de la dll), o la de cache/tools/dxc-*
std::string FindDxcDll(const std::string& given) {
  namespace fs = std::filesystem;
  auto as_dll = [](fs::path p) { return p.extension() == ".exe" ? p.parent_path() / "dxcompiler.dll" : p; };
  if (!given.empty()) return as_dll(given).string();
  if (const char* env = std::getenv("DXC_SPIRV"); env && *env) return as_dll(env).string();
  std::vector<fs::path> found;
  std::error_code ec;
  for (const auto& d : fs::directory_iterator("cache/tools", ec))
    if (d.path().filename().string().rfind("dxc-", 0) == 0 && fs::exists(d.path() / "bin/x64/dxcompiler.dll"))
      found.push_back(d.path() / "bin/x64/dxcompiler.dll");
  std::sort(found.begin(), found.end());
  return found.empty() ? std::string() : found.back().string();
}

// el HLSL incluye xe_common.hlsli: dxc lo recibe ya sustituido
std::string ResolveInclude(const std::string& src) {
  const std::string inc = "#include \"xe_common.hlsli\"";
  const size_t at = src.find(inc);
  return at == std::string::npos ? src : src.substr(0, at) + g_common + src.substr(at + inc.size());
}

bool InitGpu(Gpu& g, bool hw) {
  const D3D_FEATURE_LEVEL fl = D3D_FEATURE_LEVEL_11_0;
  if (FAILED(D3D11CreateDevice(nullptr, hw ? D3D_DRIVER_TYPE_HARDWARE : D3D_DRIVER_TYPE_WARP, nullptr, 0, &fl, 1,
                               D3D11_SDK_VERSION, &g.dev, nullptr, &g.ctx)))
    return false;
  const UINT cb_sizes[4] = {4096, 160, 768, 64};
  for (int i = 0; i < 4; ++i) g.cb[i] = MakeBuffer(g.dev.Get(), cb_sizes[i], D3D11_BIND_CONSTANT_BUFFER);
  g.mem = MakeBuffer(g.dev.Get(), kMemSize, D3D11_BIND_SHADER_RESOURCE, D3D11_USAGE_DEFAULT, 0,
                     D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS);
  D3D11_SHADER_RESOURCE_VIEW_DESC sv = {};
  sv.Format = DXGI_FORMAT_R32_TYPELESS;
  sv.ViewDimension = D3D11_SRV_DIMENSION_BUFFEREX;
  sv.BufferEx.NumElements = kMemSize / 4;
  sv.BufferEx.Flags = D3D11_BUFFEREX_SRV_FLAG_RAW;
  g.dev->CreateShaderResourceView(g.mem.Get(), &sv, &g.mem_srv);
  const UINT so_stride = 17 * 16;
  g.so = MakeBuffer(g.dev.Get(), so_stride * g.cases, D3D11_BIND_STREAM_OUTPUT);
  g.so_staging = MakeBuffer(g.dev.Get(), so_stride * g.cases, 0, D3D11_USAGE_STAGING, D3D11_CPU_ACCESS_READ);
  g.ps_in = MakeBuffer(g.dev.Get(), 16 * 16 * g.cases, D3D11_BIND_SHADER_RESOURCE, D3D11_USAGE_DEFAULT, 0,
                       D3D11_RESOURCE_MISC_BUFFER_STRUCTURED, 16);
  D3D11_SHADER_RESOURCE_VIEW_DESC pv = {};
  pv.Format = DXGI_FORMAT_UNKNOWN;
  pv.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
  pv.Buffer.NumElements = 16 * g.cases;
  g.dev->CreateShaderResourceView(g.ps_in.Get(), &pv, &g.ps_in_srv);

  for (uint32_t n = 0; n < 32; ++n) {
    D3D11_SUBRESOURCE_DATA init[6];
    for (auto& i : init) i = {g.tex_colors[n], 16, 16};
    D3D11_TEXTURE2D_DESC t2 = {1, 1, 1, 1, DXGI_FORMAT_R32G32B32A32_FLOAT, {1, 0}, D3D11_USAGE_IMMUTABLE,
                               D3D11_BIND_SHADER_RESOURCE, 0, 0};
    ComPtr<ID3D11Texture2D> t;
    g.dev->CreateTexture2D(&t2, init, &t);
    g.dev->CreateShaderResourceView(t.Get(), nullptr, &g.tex2d[n]);
    t2.ArraySize = 6;
    t2.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE;
    ComPtr<ID3D11Texture2D> tc;
    g.dev->CreateTexture2D(&t2, init, &tc);
    g.dev->CreateShaderResourceView(tc.Get(), nullptr, &g.texcube[n]);
    D3D11_TEXTURE3D_DESC t3 = {1, 1, 1, 1, DXGI_FORMAT_R32G32B32A32_FLOAT, D3D11_USAGE_IMMUTABLE,
                               D3D11_BIND_SHADER_RESOURCE, 0, 0};
    ComPtr<ID3D11Texture3D> t3d;
    g.dev->CreateTexture3D(&t3, init, &t3d);
    g.dev->CreateShaderResourceView(t3d.Get(), nullptr, &g.tex3d[n]);
  }
  D3D11_SAMPLER_DESC sd = {};
  sd.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
  sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
  sd.MaxLOD = D3D11_FLOAT32_MAX;
  g.dev->CreateSamplerState(&sd, &g.samp);

  D3D11_TEXTURE2D_DESC rd = {g.cases, 1, 1, 1, DXGI_FORMAT_R32G32B32A32_FLOAT, {1, 0}, D3D11_USAGE_DEFAULT,
                             D3D11_BIND_RENDER_TARGET, 0, 0};
  for (int i = 0; i < 4; ++i) {
    g.dev->CreateTexture2D(&rd, nullptr, &g.rt[i]);
    g.dev->CreateRenderTargetView(g.rt[i].Get(), nullptr, &g.rtv[i]);
  }
  rd.Usage = D3D11_USAGE_STAGING;
  rd.BindFlags = 0;
  rd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  g.dev->CreateTexture2D(&rd, nullptr, &g.rt_staging);
  D3D11_TEXTURE2D_DESC dd = {g.cases, 1, 1, 1, DXGI_FORMAT_D32_FLOAT, {1, 0}, D3D11_USAGE_DEFAULT,
                             D3D11_BIND_DEPTH_STENCIL, 0, 0};
  g.dev->CreateTexture2D(&dd, nullptr, &g.ds);
  g.dev->CreateDepthStencilView(g.ds.Get(), nullptr, &g.dsv);
  dd.Usage = D3D11_USAGE_STAGING;
  dd.BindFlags = 0;
  dd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  g.dev->CreateTexture2D(&dd, nullptr, &g.ds_staging);
  D3D11_DEPTH_STENCIL_DESC dsd = {};
  dsd.DepthEnable = TRUE;
  dsd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
  dsd.DepthFunc = D3D11_COMPARISON_ALWAYS;
  g.dev->CreateDepthStencilState(&dsd, &g.dss);

  // vertice de paso para probar pixeles: un punto por pixel con sus 16 interpoladores
  const std::string pass = R"(
struct XeVaryings { float4 pos : SV_Position; float4 o[16] : TEXCOORD0; };
StructuredBuffer<float4> xe_in : register(t0);
XeVaryings main(uint id : SV_VertexID) {
  XeVaryings v;
  v.pos = float4((float(id) + 0.5) / )" + std::to_string(g.cases) + R"(.0 * 2.0 - 1.0, 0.0, 0.5, 1.0);
  [unroll] for (uint i = 0u; i < 16u; ++i) v.o[i] = xe_in[id * 16u + i];
  return v;
})";
  ComPtr<ID3DBlob> code;
  std::string err;
  if (!Compile(pass, "vs_5_0", code, err)) {
    fmt::print(stderr, "vertice de paso: {}\n", err);
    return false;
  }
  g.dev->CreateVertexShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &g.passthrough_vs);
  return true;
}

void Upload(Gpu& g, const State& s, bool pixel) {
  g.ctx->UpdateSubresource(g.cb[0].Get(), 0, nullptr, s.c, 0, 0);
  uint32_t bl[40] = {};
  std::memcpy(bl, s.b, sizeof(s.b));
  std::memcpy(bl + 8, s.l, sizeof(s.l));
  g.ctx->UpdateSubresource(g.cb[1].Get(), 0, nullptr, bl, 0, 0);
  g.ctx->UpdateSubresource(g.cb[2].Get(), 0, nullptr, s.fc, 0, 0);
  g.ctx->UpdateSubresource(g.cb[3].Get(), 0, nullptr, &kSys, 0, 0);
  g.ctx->UpdateSubresource(g.mem.Get(), 0, nullptr, s.mem.data(), 0, 0);
  (void)pixel;
}

template <typename SetSrv, typename SetSamp>
void BindTextures(Gpu& g, const Shader& sh, SetSrv set_srv, SetSamp set_samp) {
  for (const auto& t : sh.texture_bindings()) {
    const uint32_t n = t.fetch_constant & 31;
    ID3D11ShaderResourceView* v;
    UINT slot;
    switch (t.fetch_instr.dimension) {
      case xenos::FetchOpDimension::k3DOrStacked: v = g.tex3d[n].Get(); slot = 33 + n; break;
      case xenos::FetchOpDimension::kCube: v = g.texcube[n].Get(); slot = 65 + n; break;
      default: v = g.tex2d[n].Get(); slot = 1 + n; break;
    }
    set_srv(slot, v);
    if (n < D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT) {
      ID3D11SamplerState* s = g.samp.Get();
      set_samp(n, s);
    }
  }
}

bool RunVertex(Gpu& g, const ComPtr<ID3DBlob>& code, const Shader& sh, std::vector<Outputs>& out, std::string& err) {
  ComPtr<ID3D11VertexShader> vs;
  ComPtr<ID3D11GeometryShader> gs;
  D3D11_SO_DECLARATION_ENTRY decl[17];
  decl[0] = {0, "SV_POSITION", 0, 0, 4, 0};
  for (UINT i = 0; i < 16; ++i) decl[1 + i] = {0, "TEXCOORD", i, 0, 4, 0};
  const UINT stride = 17 * 16;
  if (FAILED(g.dev->CreateVertexShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &vs)) ||
      FAILED(g.dev->CreateGeometryShaderWithStreamOutput(code->GetBufferPointer(), code->GetBufferSize(), decl, 17,
                                                         &stride, 1, D3D11_SO_NO_RASTERIZED_STREAM, nullptr, &gs))) {
    err = "no se pudo crear el shader de vertices o el stream output";
    return false;
  }
  g.ctx->ClearState();
  g.ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);
  g.ctx->VSSetShader(vs.Get(), nullptr, 0);
  g.ctx->GSSetShader(gs.Get(), nullptr, 0);
  ID3D11Buffer* cbs[4] = {g.cb[0].Get(), g.cb[1].Get(), g.cb[2].Get(), g.cb[3].Get()};
  g.ctx->VSSetConstantBuffers(0, 4, cbs);
  ID3D11ShaderResourceView* m = g.mem_srv.Get();
  g.ctx->VSSetShaderResources(0, 1, &m);
  BindTextures(g, sh, [&](UINT s, ID3D11ShaderResourceView* v) { g.ctx->VSSetShaderResources(s, 1, &v); },
               [&](UINT s, ID3D11SamplerState* p) { g.ctx->VSSetSamplers(s, 1, &p); });
  ID3D11Buffer* so = g.so.Get();
  UINT off = 0;
  g.ctx->SOSetTargets(1, &so, &off);
  g.ctx->Draw(g.cases, 0);
  g.ctx->SOSetTargets(0, nullptr, nullptr);
  g.ctx->CopyResource(g.so_staging.Get(), g.so.Get());
  D3D11_MAPPED_SUBRESOURCE map;
  if (FAILED(g.ctx->Map(g.so_staging.Get(), 0, D3D11_MAP_READ, 0, &map))) return false;
  const float* p = static_cast<const float*>(map.pData);
  out.assign(g.cases, Outputs{});
  for (uint32_t i = 0; i < g.cases; ++i) {
    std::memcpy(out[i].v[16], p + i * 68, 16);
    for (int j = 0; j < 16; ++j) std::memcpy(out[i].v[j], p + i * 68 + 4 + j * 4, 16);
  }
  g.ctx->Unmap(g.so_staging.Get(), 0);
  return true;
}

bool RunPixel(Gpu& g, const ComPtr<ID3DBlob>& code, const Shader& sh, const std::vector<float>& inputs,
              std::vector<Outputs>& out, std::string& err) {
  ComPtr<ID3D11PixelShader> ps;
  if (FAILED(g.dev->CreatePixelShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &ps))) {
    err = "no se pudo crear el shader de pixeles";
    return false;
  }
  g.ctx->ClearState();
  g.ctx->UpdateSubresource(g.ps_in.Get(), 0, nullptr, inputs.data(), 0, 0);
  const float clear[4] = {kSentinel, kSentinel, kSentinel, kSentinel};
  for (auto& r : g.rtv) g.ctx->ClearRenderTargetView(r.Get(), clear);
  g.ctx->ClearDepthStencilView(g.dsv.Get(), D3D11_CLEAR_DEPTH, 0.25f, 0);
  g.ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);
  g.ctx->VSSetShader(g.passthrough_vs.Get(), nullptr, 0);
  ID3D11ShaderResourceView* in = g.ps_in_srv.Get();
  g.ctx->VSSetShaderResources(0, 1, &in);
  g.ctx->PSSetShader(ps.Get(), nullptr, 0);
  ID3D11Buffer* cbs[4] = {g.cb[0].Get(), g.cb[1].Get(), g.cb[2].Get(), g.cb[3].Get()};
  g.ctx->PSSetConstantBuffers(0, 4, cbs);
  ID3D11ShaderResourceView* m = g.mem_srv.Get();
  g.ctx->PSSetShaderResources(0, 1, &m);
  BindTextures(g, sh, [&](UINT s, ID3D11ShaderResourceView* v) { g.ctx->PSSetShaderResources(s, 1, &v); },
               [&](UINT s, ID3D11SamplerState* p) { g.ctx->PSSetSamplers(s, 1, &p); });
  ID3D11RenderTargetView* rtvs[4] = {g.rtv[0].Get(), g.rtv[1].Get(), g.rtv[2].Get(), g.rtv[3].Get()};
  g.ctx->OMSetRenderTargets(4, rtvs, g.dsv.Get());
  g.ctx->OMSetDepthStencilState(g.dss.Get(), 0);
  D3D11_VIEWPORT vp = {0, 0, float(g.cases), 1, 0, 1};
  g.ctx->RSSetViewports(1, &vp);
  g.ctx->Draw(g.cases, 0);
  out.assign(g.cases, Outputs{});
  for (int t = 0; t < 4; ++t) {
    g.ctx->CopyResource(g.rt_staging.Get(), g.rt[t].Get());
    D3D11_MAPPED_SUBRESOURCE map;
    if (FAILED(g.ctx->Map(g.rt_staging.Get(), 0, D3D11_MAP_READ, 0, &map))) return false;
    for (uint32_t i = 0; i < g.cases; ++i) std::memcpy(out[i].v[t], static_cast<const float*>(map.pData) + i * 4, 16);
    g.ctx->Unmap(g.rt_staging.Get(), 0);
  }
  g.ctx->CopyResource(g.ds_staging.Get(), g.ds.Get());
  D3D11_MAPPED_SUBRESOURCE map;
  if (FAILED(g.ctx->Map(g.ds_staging.Get(), 0, D3D11_MAP_READ, 0, &map))) return false;
  for (uint32_t i = 0; i < g.cases; ++i) {
    out[i].v[4][0] = static_cast<const float*>(map.pData)[i];
    out[i].killed = out[i].v[4][0] == 0.25f;   // un pixel no descartado escribe profundidad (0.5 o SV_Depth)
  }
  g.ctx->Unmap(g.ds_staging.Get(), 0);
  (void)sh;
  return true;
}

bool Close(float a, float b) {
  if (std::isnan(a) || std::isnan(b)) return std::isnan(a) && std::isnan(b);
  if (std::isinf(a) || std::isinf(b)) return a == b || (std::fabs(a) > 1e30f && std::fabs(b) > 1e30f && (a > 0) == (b > 0));
  const float d = std::fabs(a - b);
  return d <= 1e-4f || d <= 1e-4f * std::max(std::fabs(a), std::fabs(b));
}

std::string JsonEscape(const std::string& s) {
  std::string o;
  for (char c : s) {
    if (c == '"' || c == '\\') { o += '\\'; o += c; }
    else if (c == '\n') o += "\\n";
    else if (uint8_t(c) >= 0x20) o += c;
  }
  return o;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    fmt::print(stderr, "uso: xenos2hlsl_check <entrada.xsh | carpeta> [--hw] [--only HASH] [--cases N] [--json f]\n");
    return 2;
  }
  bool hw = false, vk = false;
  std::string only, json_path, hlsl_override, dxc_arg;
  bool dump = false;
  uint32_t seed = 0;
  Gpu g;
  for (int i = 2; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--hw") hw = true;
    else if (a == "--vk") vk = true;
    else if (a == "--dxc" && i + 1 < argc) dxc_arg = argv[++i];
    else if (a == "--only" && i + 1 < argc) only = argv[++i];
    else if (a == "--cases" && i + 1 < argc) g.cases = std::clamp(uint32_t(std::stoul(argv[++i])), 1u, 1024u);
    else if (a == "--json" && i + 1 < argc) json_path = argv[++i];
    else if (a == "--hlsl" && i + 1 < argc) hlsl_override = argv[++i];   // con --only: HLSL editado a mano
    else if (a == "--dump") dump = true;
    else if (a == "--fast") g_compile_flags &= ~UINT(D3DCOMPILE_IEEE_STRICTNESS);
    else if (a == "--seed" && i + 1 < argc) seed = uint32_t(std::stoul(argv[++i]));   // otro estado aleatorio                                  // valores de cada caso
  }
  g_common = x2h::HlslTranslator::CommonHeader();
  MakeTexColors(g);
  std::unique_ptr<x2h::vkcheck::Backend> vkb;
  std::string where = hw ? "GPU" : "WARP";
  if (vk) {
    const std::string dll = FindDxcDll(dxc_arg);
    if (dll.empty()) {
      fmt::print(stderr, "falta una dxc con SPIR-V: --dxc, DXC_SPIRV o cache/tools/dxc-*/bin/x64\n");
      return 2;
    }
    std::string err;
    vkb = x2h::vkcheck::Backend::Create(g.cases, kMemSize, g.tex_colors, dll, err);
    if (!vkb) {
      fmt::print(stderr, "Vulkan: {}\n", err);
      return 2;
    }
    where = "Vulkan, " + vkb->device_name();
  } else if (!InitGpu(g, hw)) {
    fmt::print(stderr, "no se pudo crear el dispositivo D3D11 ({})\n", where);
    return 2;
  }
  const auto entries = x2h::ReadXshInput(argv[1]);
  x2h::HlslTranslator translator;
  rex::string::StringBuffer disasm;
  auto rf = std::make_unique<RegisterFile>();
  uint32_t pass = 0, fail = 0, skip = 0, approx = 0;
  std::string json = "{\n \"shaders\": [\n";
  bool first = true;
  for (const auto& e : entries) {
    const std::string hash = fmt::format("{:016X}", e.hash);
    if (!only.empty() && hash != only) continue;
    const std::string name = hash + (e.pixel ? ".ps" : ".vs");
    Shader sh(e.pixel ? xenos::ShaderType::kPixel : xenos::ShaderType::kVertex, e.hash, e.dwords.data(),
              e.dwords.size());
    sh.AnalyzeUcode(disasm);
    Shader::Translation* t = sh.GetOrCreateTranslation(0);
    std::string status, detail;
    if (!translator.TranslateAnalyzedShader(*t) || !t->is_valid()) {
      status = "skip", detail = "no se traduce";
    } else if (translator.report().uses_memexport) {
      status = "skip", detail = "memexport";
    }
    std::string src(t->translated_binary().begin(), t->translated_binary().end());
    if (!hlsl_override.empty()) {
      if (FILE* f = std::fopen(hlsl_override.c_str(), "rb")) {
        src.clear();
        char buf[4096];
        size_t n;
        while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) src.append(buf, n);
        std::fclose(f);
      }
    }
    ComPtr<ID3DBlob> code;
    std::vector<uint32_t> spirv;
    if (status.empty()) {
      const bool built = vkb ? vkb->Compile(ResolveInclude(src), e.pixel, spirv, detail)
                             : Compile(src, e.pixel ? "ps_5_0" : "vs_5_0", code, detail);
      if (!built) status = "fail";
    }

    // estado y entradas
    std::mt19937 rng(uint32_t(e.hash) ^ uint32_t(e.hash >> 32) ^ (seed * 0x9E3779B9u));
    State s;
    MakeState(s, sh, rng);
    FillRegisters(*rf, s, e.pixel);
    uint32_t nregs = sh.register_static_address_bound();
    if (sh.uses_register_dynamic_addressing()) nregs = std::max(nregs, xenos::kMaxShaderTempRegisters);
    const uint32_t ninterp = std::min(nregs, 16u);
    std::vector<float> inputs(size_t(g.cases) * 64, 0.0f);
    for (float& f : inputs) f = RandValue(rng);

    std::vector<Outputs> ref(g.cases), gpu;
    if (status.empty()) {
      refcheck::RefInterpreter interp(*rf, s.mem.data(), kMemSize);
      for (uint32_t n = 0; n < 32; ++n) std::memcpy(interp.texture_colors[n].data(), g.tex_colors[n], 16);
      Sink sink;
      sink.pixel = e.pixel;
      interp.SetExportSink(&sink);
      interp.SetShader(sh.type(), sh.ucode_dwords());
      for (uint32_t i = 0; i < g.cases && status.empty(); ++i) {
        std::memset(interp.temp_registers(), 0, sizeof(float) * 4 * xenos::kMaxShaderTempRegisters);
        if (e.pixel) {
          std::memcpy(interp.temp_registers(), &inputs[size_t(i) * 64], sizeof(float) * 4 * ninterp);
        } else {
          interp.temp_registers()[0] = float(i);
        }
        ref[i] = Outputs{};
        sink.out = &ref[i];
        interp.Execute();
        ref[i].killed = interp.killed();
        if (interp.used_unsupported_fetch()) status = "skip", detail = "getTex* sin referencia";
      }
    }
    if (status.empty() && vkb) {
      uint32_t bl[40] = {};
      std::memcpy(bl, s.b, sizeof(s.b));
      std::memcpy(bl + 8, s.l, sizeof(s.l));
      const x2h::vkcheck::Frame fr = {s.c, bl, s.fc, &kSys, s.mem.data(), inputs.data()};
      std::vector<float> a, depth;
      const bool ran = e.pixel ? vkb->RunPixel(spirv, fr, a, depth, detail) : vkb->RunVertex(spirv, fr, a, detail);
      if (!ran) {
        status = "fail";
      } else {
        gpu.assign(g.cases, Outputs{});
        for (uint32_t i = 0; i < g.cases; ++i) {
          if (e.pixel) {
            std::memcpy(gpu[i].v, &a[size_t(i) * 16], 64);
            gpu[i].v[4][0] = depth[i];
            gpu[i].killed = depth[i] == 0.25f;
          } else {   // posicion primero, luego o0..o15
            std::memcpy(gpu[i].v[16], &a[size_t(i) * 68], 16);
            std::memcpy(gpu[i].v[0], &a[size_t(i) * 68 + 4], 256);
          }
        }
      }
    } else if (status.empty()) {
      Upload(g, s, e.pixel);
      const bool ran = e.pixel ? RunPixel(g, code, sh, inputs, gpu, detail) : RunVertex(g, code, sh, gpu, detail);
      if (!ran) status = "fail";
    }
    if (status.empty()) {
      // comparar
      uint32_t bad = 0;
      float worst = 0.0f;   // peor error relativo de las diferencias
      const char* names_vs[17] = {"o0", "o1", "o2", "o3", "o4", "o5", "o6", "o7", "o8", "o9", "o10", "o11", "o12",
                                  "o13", "o14", "o15", "oPos"};
      const char* names_ps[5] = {"oC0", "oC1", "oC2", "oC3", "oDepth"};
      for (uint32_t i = 0; i < g.cases; ++i) {
        if (dump) {
          for (int sl = 0; sl < (e.pixel ? 5 : 17); ++sl)
            if (ref[i].mask[sl])
              fmt::print("caso {} {}: ref ({:.6g}, {:.6g}, {:.6g}, {:.6g}) gpu ({:.6g}, {:.6g}, {:.6g}, {:.6g}){}\n", i,
                         e.pixel ? names_ps[sl] : names_vs[sl], ref[i].v[sl][0], ref[i].v[sl][1], ref[i].v[sl][2],
                         ref[i].v[sl][3], gpu[i].v[sl][0], gpu[i].v[sl][1], gpu[i].v[sl][2], gpu[i].v[sl][3],
                         ref[i].killed ? " ref descartado" : "");
        }
        if (e.pixel && ref[i].killed != gpu[i].killed) {
          if (!bad++) detail = fmt::format("caso {}: descarte ref={} gpu={}", i, ref[i].killed, gpu[i].killed);
          continue;
        }
        if (e.pixel && ref[i].killed) continue;
        const int slots = e.pixel ? 5 : 17;
        for (int sl = 0; sl < slots; ++sl) {
          for (int c = 0; c < 4; ++c) {
            if (!(ref[i].mask[sl] & (1 << c))) continue;
            float want = ref[i].v[sl][c];
            if (e.pixel && sl == 4) want = std::isnan(want) ? want : std::clamp(want, 0.0f, 1.0f);
            const float got = gpu[i].v[sl][c];
            if (!Close(want, got)) {
              const float rel = (std::isfinite(want) && std::isfinite(got))
                                    ? std::fabs(want - got) / std::max({std::fabs(want), std::fabs(got), 1e-3f})
                                    : 1.0f;
              worst = std::max(worst, rel);
              if (!bad++)
                detail = fmt::format("caso {}: {}.{} ref={:.9g} gpu={:.9g}", i, e.pixel ? names_ps[sl] : names_vs[sl],
                                     "xyzw"[c], want, got);
            }
          }
        }
      }
      // sin, cos, exp2, log2, rsqrt...: la GPU solo garantiza unas pocas cifras (D3D11: sin / cos con error
      // absoluto de hasta 8e-4); una diferencia pequena en un shader que las usa es precision, no traduccion
      const bool transcendental = src.find("sin(") != std::string::npos || src.find("cos(") != std::string::npos ||
                                  src.find("exp2(") != std::string::npos || src.find("log2(") != std::string::npos ||
                                  src.find("rsqrt(") != std::string::npos;
      if (bad && transcendental && worst < 1e-2f) {
        status = "approx";
        detail += fmt::format(" ({} diferencias, error relativo maximo {:.1e})", bad, worst);
      } else if (bad) {
        status = "fail";
        detail += fmt::format(" ({} diferencias)", bad);
      } else {
        status = "ok";
      }
    }
    if (status == "ok") ++pass;
    else if (status == "approx") ++approx;
    else if (status == "fail") ++fail;
    else ++skip;
    if (status != "ok")
      fmt::print("{} {}: {}\n", status == "fail" ? "FALLA" : status == "approx" ? "aprox" : "omite", name, detail);
    json += fmt::format("{}  {{\"shader\": \"{}\", \"status\": \"{}\", \"detail\": \"{}\"}}", first ? "" : ",\n", name,
                        status, JsonEscape(detail));
    first = false;
  }
  json += fmt::format("\n ],\n \"ok\": {}, \"approx\": {}, \"failed\": {}, \"skipped\": {}\n}}\n", pass,
                      approx, fail, skip);
  if (!json_path.empty()) {
    if (FILE* f = std::fopen(json_path.c_str(), "wb")) {
      std::fwrite(json.data(), 1, json.size(), f);
      std::fclose(f);
    }
  }
  fmt::print("comprobacion ({}, {} casos por shader, semilla {}): {} iguales, {} iguales salvo precision de sin/cos/exp, "
             "{} distintos, {} omitidos\n",
             where, g.cases, seed, pass, approx, fail, skip);
  return fail ? 1 : 0;
}
