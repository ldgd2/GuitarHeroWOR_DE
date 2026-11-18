// xfcplay: reproductor de capturas de frame del render nativo (docs/RENDER_NATIVO.md, fase 3b).
//
// Lee una captura (cache/framecap/frame_<n>.xfc, ver src/de/framecap.cpp) y vuelve a dibujar el frame en D3D11 con
// los shaders traducidos por xenos2hlsl, a la escala que se pida (--scale 2 = 2560x1440 para un frame de 720p).
// No emula la GPU de la 360: interpreta el flujo de ordenes (PM4) que escribio el juego y lo convierte en estado y
// draws de D3D11.
//
//   registros   copia del dispositivo al empezar el frame + escrituras de registro del PM4
//   EDRAM       un render target por (base, pitch, formato); los resolves copian de ahi a texturas por direccion
//   texturas    las de memoria se destilan y decodifican en la CPU (con el swizzle de la constante de fetch);
//               las que salen de un resolve se leen del render target copiado, a la escala del render
//   vertices    los vfetch del shader leen la memoria fisica capturada (ByteAddressBuffer de 512 MB)
//   rectangulos lista de rectangulos de la 360 con un geometry shader (cuarta esquina)
//
// Salida (carpeta --out): PNG de cada resolve grande en el momento en que ocurre (resolve_NNN_<destino>.png),
// PNG de los render targets grandes al final (rt_<base>_<pitch>.png) y un resumen.
//
// Uso: xfcplay <frame.xfc> [--scale N] [--out carpeta] [--warp]
#include <d3d11_1.h>
#include <d3dcompiler.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <fmt/format.h>

#include <rex/graphics/flags.h>
#include <rex/graphics/pipeline/shader/shader.h>
#include <rex/graphics/pipeline/texture/util.h>
#include <rex/graphics/registers.h>
#include <rex/graphics/xenos.h>
#include <rex/string/buffer.h>

#include "hlsl_translator.h"

REXCVAR_DEFINE_STRING(dump_shaders, "", "GPU", "Path to dump shaders to");

using Microsoft::WRL::ComPtr;
using namespace rex::graphics;
namespace x2h = re_wor::xenos2hlsl;
namespace fs = std::filesystem;

namespace {

constexpr uint32_t kMemBytes = 512u << 20;   // memoria fisica de la 360
constexpr uint32_t kEdramHeight = 1024;      // alto de los render targets (pixeles de la 360)
constexpr uint32_t kShadowOffset = 1152, kShadowBytes = 9712;

// bloques de registros copiados en el dispositivo (tools/d3d_device_map.py, src/de/nativegfx_device_map.h)
struct Block {
  uint32_t reg, offset, count;
};
constexpr Block kBlocks[] = {{0x4800, 1152, 192}, {0x4000, 1920, 1024}, {0x4400, 6016, 1024}, {0x4900, 10112, 8},
                             {0x4908, 10144, 32}, {0x2388, 10272, 24},  {0x2000, 10368, 16},  {0x2080, 10432, 3},
                             {0x2100, 10444, 21}, {0x2180, 10528, 5},   {0x2200, 10548, 12},  {0x2280, 10596, 21},
                             {0x2300, 10680, 38}, {0x2380, 10832, 8}};

uint32_t Be(const uint8_t* p) {
  uint32_t v;
  std::memcpy(&v, p, 4);
  return _byteswap_ulong(v);
}
uint32_t Swap(uint32_t v, uint32_t e) {   // xenos::GpuSwap
  if (e == 1 || e == 2) v = ((v & 0x00FF00FFu) << 8) | ((v >> 8) & 0x00FF00FFu);
  if (e == 2 || e == 3) v = (v << 16) | (v >> 16);
  return v;
}
float AsFloat(uint32_t v) {
  float f;
  std::memcpy(&f, &v, 4);
  return f;
}
float Half(uint16_t h) {
  const uint32_t s = (h >> 15) & 1, e = (h >> 10) & 0x1F, m = h & 0x3FF;
  float v;
  if (e == 0) v = std::ldexp(float(m), -24);
  else if (e == 31) v = m ? NAN : INFINITY;
  else v = std::ldexp(float(m | 0x400), int(e) - 25);
  return s ? -v : v;
}
float Float20e4(uint32_t f24) {   // profundidad en coma flotante de la 360 (D24FS8)
  f24 &= 0xFFFFFF;
  const uint32_t m = f24 & 0xFFFFF, e = f24 >> 20;
  if (!e) return std::ldexp(float(m), -20 - 14);
  return std::ldexp(1.0f + float(m) / float(1 << 20), int(e) - 15);
}
uint64_t Fnv(const void* p, size_t n, uint64_t h = 1469598103934665603ull) {
  const uint8_t* b = static_cast<const uint8_t*>(p);
  for (size_t i = 0; i < n; ++i) h = (h ^ b[i]) * 1099511628211ull;
  return h;
}

// ------------------------------------------------------------------------------------------- D3D11
struct Gpu {
  ComPtr<ID3D11Device> dev;
  ComPtr<ID3D11DeviceContext> ctx;
  ComPtr<ID3D11Buffer> mem;
  ComPtr<ID3D11ShaderResourceView> mem_srv;
  ComPtr<ID3D11Buffer> cb_vs_c, cb_ps_c, cb_bl, cb_fc, cb_sys, cb_pass;
  ComPtr<ID3D11Buffer> ib;
  uint32_t ib_size = 0;
  ComPtr<ID3D11GeometryShader> rect_gs;
  ComPtr<ID3D11VertexShader> pass_vs;
  ComPtr<ID3D11PixelShader> copy_ps, swz_ps, clear_ps;
  ComPtr<ID3D11ShaderResourceView> dummy2d, dummy3d, dummycube;
  std::map<std::string, ComPtr<ID3D11BlendState>> blend;
  std::map<std::string, ComPtr<ID3D11DepthStencilState>> depth;
  std::map<std::string, ComPtr<ID3D11RasterizerState>> raster;
  std::map<std::string, ComPtr<ID3D11SamplerState>> samplers;
};

std::string g_common;
class Include : public ID3DInclude {
 public:
  HRESULT __stdcall Open(D3D_INCLUDE_TYPE, LPCSTR, LPCVOID, LPCVOID* data, UINT* bytes) override {
    *data = g_common.data();
    *bytes = UINT(g_common.size());
    return S_OK;
  }
  HRESULT __stdcall Close(LPCVOID) override { return S_OK; }
};

bool Compile(const std::string& src, const char* entry, const char* profile, ComPtr<ID3DBlob>& code,
             std::string& err) {
  Include inc;
  ComPtr<ID3DBlob> e;
  // IEEE estricto: sin el se pierden los NaN de la 360 (fase 1)
  HRESULT hr = D3DCompile(src.data(), src.size(), "shader.hlsl", nullptr, &inc, entry, profile,
                          D3DCOMPILE_OPTIMIZATION_LEVEL1 | D3DCOMPILE_IEEE_STRICTNESS, 0, &code, &e);
  if (FAILED(hr)) {
    e.Reset();
    hr = D3DCompile(src.data(), src.size(), "shader.hlsl", nullptr, &inc, entry, profile,
                    D3DCOMPILE_SKIP_OPTIMIZATION | D3DCOMPILE_IEEE_STRICTNESS, 0, &code, &e);
  }
  if (FAILED(hr)) err = e ? std::string(static_cast<const char*>(e->GetBufferPointer()), e->GetBufferSize()) : "?";
  return SUCCEEDED(hr);
}

ComPtr<ID3D11Buffer> MakeCb(ID3D11Device* dev, UINT size) {
  D3D11_BUFFER_DESC d = {(size + 15) & ~15u, D3D11_USAGE_DEFAULT, D3D11_BIND_CONSTANT_BUFFER, 0, 0, 0};
  ComPtr<ID3D11Buffer> b;
  dev->CreateBuffer(&d, nullptr, &b);
  return b;
}

const char* kPassHlsl = R"(
Texture2D<float4> src : register(t0);
cbuffer XePass : register(b0) {
  float4 k_color;
  uint4 k_swz;
  int2 k_off;
  float k_scale;
  uint k_flags;   // 1 rojo<->azul, 2 profundidad, 4 degamma, 8 gamma
  float k_depth;
  float3 k_pad;
};
float4 vs(uint id : SV_VertexID) : SV_Position {
  float2 uv = float2((id << 1) & 2, id & 2);
  return float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
}
float3 degamma(float3 c) { return c <= 0.04045 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4); }
float3 gamma(float3 c) { c = saturate(c); return c <= 0.0031308 ? c * 12.92 : 1.055 * pow(c, 1.0 / 2.4) - 0.055; }
float4 load(float4 p) {
  float4 v = src.Load(int3(int2(p.xy) + k_off, 0));
  if (k_flags & 2u) v = float4(v.rrr, 1.0);
  if (k_flags & 1u) v = v.bgra;
  return v;
}
float4 copy(float4 p : SV_Position) : SV_Target {
  float4 v = load(p) * k_scale;
  if (k_flags & 8u) v.rgb = gamma(v.rgb);
  return v;
}
float4 swz(float4 p : SV_Position) : SV_Target {
  float4 v = load(p), o;
  [unroll] for (uint i = 0; i < 4; ++i) {
    uint s = k_swz[i];
    o[i] = s < 4u ? v[s] : (s == 4u ? 0.0 : 1.0);
  }
  if (k_flags & 4u) o.rgb = degamma(o.rgb);
  return o;
}
struct ClearOut { float4 c : SV_Target; float d : SV_Depth; };
ClearOut clr(float4 p : SV_Position) { ClearOut o; o.c = k_color; o.d = k_depth; return o; }
)";

// lista de rectangulos: tres esquinas -> la cuarta es la suma de las dos de la hipotenusa menos la del angulo recto
const char* kRectGs = R"(
struct XeVaryings { float4 pos : SV_Position; float4 o[16] : TEXCOORD0; };
[maxvertexcount(4)]
void main(triangle XeVaryings v[3], inout TriangleStream<XeVaryings> s) {
  XeVaryings w[3] = {v[0], v[1], v[2]};
  float2 p0 = w[0].pos.xy / w[0].pos.w, p1 = w[1].pos.xy / w[1].pos.w, p2 = w[2].pos.xy / w[2].pos.w;
  float d01 = dot(p1 - p0, p1 - p0), d12 = dot(p2 - p1, p2 - p1), d20 = dot(p0 - p2, p0 - p2);
  uint c = 0u, a = 1u, b = 2u;
  if (d01 >= d12 && d01 >= d20) { c = 2u; a = 0u; b = 1u; }
  else if (d20 >= d01 && d20 >= d12) { c = 1u; a = 2u; b = 0u; }
  XeVaryings d;
  d.pos = w[a].pos + w[b].pos - w[c].pos;
  [unroll] for (uint i = 0u; i < 16u; ++i) d.o[i] = w[a].o[i] + w[b].o[i] - w[c].o[i];
  s.Append(w[c]); s.Append(w[a]); s.Append(w[b]); s.Append(d);
}
)";

struct PassConsts {
  float color[4];
  uint32_t swz[4];
  int32_t off[2];
  float scale;
  uint32_t flags;
  float depth;
  float pad[3];
};

bool InitGpu(Gpu& g, bool warp) {
  const D3D_FEATURE_LEVEL fl = D3D_FEATURE_LEVEL_11_0;
  if (FAILED(D3D11CreateDevice(nullptr, warp ? D3D_DRIVER_TYPE_WARP : D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, &fl, 1,
                               D3D11_SDK_VERSION, &g.dev, nullptr, &g.ctx)))
    return false;
  D3D11_BUFFER_DESC md = {kMemBytes, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0,
                          D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS, 0};
  if (FAILED(g.dev->CreateBuffer(&md, nullptr, &g.mem))) {
    fmt::print(stderr, "no se pudo crear el buffer de memoria de 512 MB\n");
    return false;
  }
  D3D11_SHADER_RESOURCE_VIEW_DESC sd = {};
  sd.Format = DXGI_FORMAT_R32_TYPELESS;
  sd.ViewDimension = D3D11_SRV_DIMENSION_BUFFEREX;
  sd.BufferEx.NumElements = kMemBytes / 4;
  sd.BufferEx.Flags = D3D11_BUFFEREX_SRV_FLAG_RAW;
  g.dev->CreateShaderResourceView(g.mem.Get(), &sd, &g.mem_srv);
  g.cb_vs_c = MakeCb(g.dev.Get(), 4096);
  g.cb_ps_c = MakeCb(g.dev.Get(), 4096);
  g.cb_bl = MakeCb(g.dev.Get(), 160);
  g.cb_fc = MakeCb(g.dev.Get(), 768);
  g.cb_sys = MakeCb(g.dev.Get(), 64);
  g.cb_pass = MakeCb(g.dev.Get(), sizeof(PassConsts));
  std::string err;
  ComPtr<ID3DBlob> code;
  const std::string pass = kPassHlsl;
  if (!Compile(kRectGs, "main", "gs_5_0", code, err)) return fmt::print(stderr, "GS: {}\n", err), false;
  g.dev->CreateGeometryShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &g.rect_gs);
  if (!Compile(pass, "vs", "vs_5_0", code, err)) return fmt::print(stderr, "pass vs: {}\n", err), false;
  g.dev->CreateVertexShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &g.pass_vs);
  if (!Compile(pass, "copy", "ps_5_0", code, err)) return fmt::print(stderr, "copy: {}\n", err), false;
  g.dev->CreatePixelShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &g.copy_ps);
  if (!Compile(pass, "swz", "ps_5_0", code, err)) return fmt::print(stderr, "swz: {}\n", err), false;
  g.dev->CreatePixelShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &g.swz_ps);
  if (!Compile(pass, "clr", "ps_5_0", code, err)) return fmt::print(stderr, "clr: {}\n", err), false;
  g.dev->CreatePixelShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &g.clear_ps);
  // texturas de relleno para lo que aun no se decodifica (3D, cubos, formatos raros): blanco
  const uint32_t white[6] = {0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu};
  D3D11_SUBRESOURCE_DATA init[6];
  for (auto& i : init) i = {white, 4, 4};
  D3D11_TEXTURE2D_DESC td = {1, 1, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, {1, 0}, D3D11_USAGE_DEFAULT,
                             D3D11_BIND_SHADER_RESOURCE, 0, 0};
  ComPtr<ID3D11Texture2D> t2;
  g.dev->CreateTexture2D(&td, init, &t2);
  g.dev->CreateShaderResourceView(t2.Get(), nullptr, &g.dummy2d);
  td.ArraySize = 6;
  td.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE;
  ComPtr<ID3D11Texture2D> tc;
  g.dev->CreateTexture2D(&td, init, &tc);
  g.dev->CreateShaderResourceView(tc.Get(), nullptr, &g.dummycube);
  D3D11_TEXTURE3D_DESC t3d = {1, 1, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_USAGE_DEFAULT,
                              D3D11_BIND_SHADER_RESOURCE, 0, 0};
  ComPtr<ID3D11Texture3D> t3;
  g.dev->CreateTexture3D(&t3d, init, &t3);
  g.dev->CreateShaderResourceView(t3.Get(), nullptr, &g.dummy3d);
  return true;
}

// ------------------------------------------------------------------------------------------- reproductor
struct Rt {   // un render target (o profundidad) del EDRAM
  ComPtr<ID3D11Texture2D> tex;
  ComPtr<ID3D11RenderTargetView> rtv;
  ComPtr<ID3D11DepthStencilView> dsv;
  ComPtr<ID3D11ShaderResourceView> srv;
  uint32_t base = 0, pitch = 0, fmt = 0;
  bool depth = false;
};

struct Resolved {   // destino de un resolve: textura a la escala del render
  ComPtr<ID3D11Texture2D> tex;
  ComPtr<ID3D11RenderTargetView> rtv;
  ComPtr<ID3D11ShaderResourceView> srv;
  uint32_t w = 0, h = 0, version = 0;
  bool depth = false;
};

struct Prog {
  std::unique_ptr<Shader> shader;
  ComPtr<ID3D11VertexShader> vs;
  ComPtr<ID3D11PixelShader> ps;
  bool ok = false;
};

struct TexView {
  ComPtr<ID3D11ShaderResourceView> srv;
  bool dim3d = false, cube = false;
};

class Player {
 public:
  Player(Gpu& g, uint32_t scale, fs::path out) : g_(g), s_(scale), out_(std::move(out)) {
    mem_.assign(kMemBytes, 0);
    regs_.assign(0x5003, 0);
  }
  bool Run(const std::vector<uint8_t>& b);
  void Summary();

 private:
  template <typename T>
  T R() const {
    T t;
    t.value = regs_[uint32_t(T::register_index)];
    return t;
  }
  uint32_t& Reg(uint32_t r) { return regs_[r]; }

  void ExecSpan(const uint32_t* dw, uint32_t n);
  void Packet3(uint32_t op, const uint32_t* d, uint32_t n);
  void Draw(uint32_t init, const uint32_t* imm, uint32_t imm_words, bool has_dma, uint32_t dma_base,
            uint32_t dma_size);
  void Resolve();
  void Upload();
  Prog* GetProg(uint32_t type);
  Rt* GetRt(uint32_t base, uint32_t pitch, uint32_t fmt, bool depth);
  TexView GetTexture(uint32_t fc_index);
  ID3D11SamplerState* GetSampler(uint32_t fc_index);
  bool Scissor(D3D11_RECT& r);
  void Pass(ID3D11PixelShader* ps, ID3D11RenderTargetView* rtv, ID3D11DepthStencilView* dsv, uint32_t w, uint32_t h,
            const D3D11_RECT& rect, ID3D11ShaderResourceView* src, const PassConsts& pc,
            ID3D11DepthStencilState* dss = nullptr, UINT stencil_ref = 0);
  void Export(ID3D11ShaderResourceView* srv, uint32_t w, uint32_t h, bool depth, const fs::path& path);
  void Count(const std::string& what) { ++notes_[what]; }

  Gpu& g_;
  uint32_t s_;
  fs::path out_;
  std::vector<uint8_t> mem_;
  std::vector<std::pair<uint32_t, uint32_t>> dirty_;
  std::vector<uint32_t> regs_;
  bool regs_init_ = false;
  std::map<std::pair<uint32_t, uint32_t>, std::vector<uint32_t>> ucode_;   // (tipo, direccion) -> microcodigo BE
  uint32_t active_[2] = {};
  uint32_t active_ndw_[2] = {};
  std::map<uint64_t, Prog> progs_;
  std::map<uint64_t, Rt> rts_;
  std::map<uint32_t, Resolved> resolved_;
  std::map<uint64_t, TexView> tex_cache_;
  std::map<std::string, uint64_t> notes_;
  rex::string::StringBuffer disasm_;
  x2h::HlslTranslator translator_;
  uint32_t draws_ = 0, resolves_ = 0, calls_ = 0, exported_ = 0;
  std::vector<std::string> log_;
};

void Player::Upload() {
  for (const auto& d : dirty_) {
    D3D11_BOX box = {d.first, 0, 0, d.first + d.second, 1, 1};
    g_.ctx->UpdateSubresource(g_.mem.Get(), 0, &box, mem_.data() + d.first, 0, 0);
  }
  dirty_.clear();
}

Prog* Player::GetProg(uint32_t type) {
  const auto it = ucode_.find({type, active_[type]});
  if (!active_[type] || it == ucode_.end()) return nullptr;
  const std::vector<uint32_t>& uc = it->second;
  const uint64_t key = Fnv(uc.data(), uc.size() * 4, type);
  auto pit = progs_.find(key);
  if (pit != progs_.end()) return pit->second.ok ? &pit->second : nullptr;
  Prog& p = progs_[key];
  p.shader = std::make_unique<Shader>(type ? xenos::ShaderType::kPixel : xenos::ShaderType::kVertex, key, uc.data(),
                                      uc.size());
  p.shader->AnalyzeUcode(disasm_);
  Shader::Translation* t = p.shader->GetOrCreateTranslation(0);
  if (!translator_.TranslateAnalyzedShader(*t) || !t->is_valid()) {
    Count("shader que no se traduce");
    return nullptr;
  }
  const std::string src(t->translated_binary().begin(), t->translated_binary().end());
  ComPtr<ID3DBlob> code;
  std::string err;
  if (!Compile(src, "main", type ? "ps_5_0" : "vs_5_0", code, err)) {
    Count("shader que no compila");
    log_.push_back(fmt::format("shader {:016X}: {}", key, err.substr(0, 300)));
    return nullptr;
  }
  if (type) g_.dev->CreatePixelShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &p.ps);
  else g_.dev->CreateVertexShader(code->GetBufferPointer(), code->GetBufferSize(), nullptr, &p.vs);
  p.ok = true;
  return &p;
}

DXGI_FORMAT ColorFormat(uint32_t f, DXGI_FORMAT& rtv, DXGI_FORMAT& srv) {
  switch (f) {
    case 0: rtv = srv = DXGI_FORMAT_R8G8B8A8_UNORM; return DXGI_FORMAT_R8G8B8A8_TYPELESS;
    case 1: rtv = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, srv = DXGI_FORMAT_R8G8B8A8_UNORM; return DXGI_FORMAT_R8G8B8A8_TYPELESS;
    case 2: case 10: rtv = srv = DXGI_FORMAT_R10G10B10A2_UNORM; return rtv;
    case 4: case 6: rtv = srv = DXGI_FORMAT_R16G16_FLOAT; return rtv;
    case 14: rtv = srv = DXGI_FORMAT_R32_FLOAT; return rtv;
    case 15: rtv = srv = DXGI_FORMAT_R32G32_FLOAT; return rtv;
    default: rtv = srv = DXGI_FORMAT_R16G16B16A16_FLOAT; return rtv;   // 3, 5, 7, 12
  }
}

Rt* Player::GetRt(uint32_t base, uint32_t pitch, uint32_t fmt, bool depth) {
  if (!pitch) return nullptr;
  const uint64_t key = uint64_t(base) | uint64_t(pitch) << 12 | uint64_t(fmt) << 28 | uint64_t(depth) << 34;
  auto it = rts_.find(key);
  if (it != rts_.end()) return &it->second;
  Rt& rt = rts_[key];
  rt.base = base, rt.pitch = pitch, rt.fmt = fmt, rt.depth = depth;
  D3D11_TEXTURE2D_DESC td = {pitch * s_, kEdramHeight * s_, 1, 1, DXGI_FORMAT_UNKNOWN, {1, 0},
                             D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, 0};
  D3D11_SHADER_RESOURCE_VIEW_DESC sd = {};
  sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
  sd.Texture2D.MipLevels = 1;
  if (depth) {
    td.Format = fmt ? DXGI_FORMAT_R32G8X24_TYPELESS : DXGI_FORMAT_R24G8_TYPELESS;
    td.BindFlags |= D3D11_BIND_DEPTH_STENCIL;
    g_.dev->CreateTexture2D(&td, nullptr, &rt.tex);
    D3D11_DEPTH_STENCIL_VIEW_DESC dd = {};
    dd.Format = fmt ? DXGI_FORMAT_D32_FLOAT_S8X24_UINT : DXGI_FORMAT_D24_UNORM_S8_UINT;
    dd.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
    g_.dev->CreateDepthStencilView(rt.tex.Get(), &dd, &rt.dsv);
    sd.Format = fmt ? DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS : DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
    g_.dev->CreateShaderResourceView(rt.tex.Get(), &sd, &rt.srv);
    g_.ctx->ClearDepthStencilView(rt.dsv.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
  } else {
    DXGI_FORMAT rf, sf;
    td.Format = ColorFormat(fmt, rf, sf);
    td.BindFlags |= D3D11_BIND_RENDER_TARGET;
    g_.dev->CreateTexture2D(&td, nullptr, &rt.tex);
    D3D11_RENDER_TARGET_VIEW_DESC rd = {};
    rd.Format = rf;
    rd.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
    g_.dev->CreateRenderTargetView(rt.tex.Get(), &rd, &rt.rtv);
    sd.Format = sf;
    g_.dev->CreateShaderResourceView(rt.tex.Get(), &sd, &rt.srv);
    const float z[4] = {};
    g_.ctx->ClearRenderTargetView(rt.rtv.Get(), z);
  }
  return &rt;
}

bool Player::Scissor(D3D11_RECT& r) {
  const auto wtl = R<reg::PA_SC_WINDOW_SCISSOR_TL>();
  const auto wbr = R<reg::PA_SC_WINDOW_SCISSOR_BR>();
  int32_t tlx = wtl.tl_x, tly = wtl.tl_y, brx = wbr.br_x, bry = wbr.br_y;
  if (!wtl.window_offset_disable) {
    const auto wo = R<reg::PA_SC_WINDOW_OFFSET>();
    tlx += wo.window_x_offset, tly += wo.window_y_offset, brx += wo.window_x_offset, bry += wo.window_y_offset;
  }
  const auto stl = R<reg::PA_SC_SCREEN_SCISSOR_TL>();
  const auto sbr = R<reg::PA_SC_SCREEN_SCISSOR_BR>();
  tlx = std::max(tlx, int32_t(stl.tl_x)), tly = std::max(tly, int32_t(stl.tl_y));
  brx = std::min(brx, int32_t(sbr.br_x)), bry = std::min(bry, int32_t(sbr.br_y));
  tlx = std::max(tlx, 0), tly = std::max(tly, 0);
  if (brx <= tlx || bry <= tly) return false;
  r = {LONG(tlx * s_), LONG(tly * s_), LONG(brx * s_), LONG(bry * s_)};
  return true;
}

// ---- texturas de memoria: destilado y decodificacion en la CPU
struct Fmt {
  uint32_t bpb, bw, bh;
};
Fmt FormatBlock(uint32_t f) {
  switch (f) {
    case 2: case 8: case 9: return {1, 1, 1};
    case 3: case 4: case 5: case 10: case 15: case 24: case 27: case 30: return {2, 1, 1};
    case 18: case 58: case 59: case 60: case 51: return {8, 4, 4};
    case 19: case 20: case 49: case 52: case 53: return {16, 4, 4};
    case 26: case 29: case 32: case 34: case 37: case 50: case 54: case 55: case 56: return {8, 1, 1};
    case 35: case 38: return {16, 1, 1};
    default: return {4, 1, 1};
  }
}

void Rgb565(uint16_t c, float* o) {
  o[0] = float((c >> 11) & 31) / 31.0f, o[1] = float((c >> 5) & 63) / 63.0f, o[2] = float(c & 31) / 31.0f;
}
// bloque de color DXT (8 bytes) -> 16 texeles rgba (alfa = 1 o 0 en el modo de 3 colores de DXT1)
void DxtColor(const uint8_t* b, bool dxt1, float out[16][4]) {
  const uint16_t c0 = uint16_t(b[0] | b[1] << 8), c1 = uint16_t(b[2] | b[3] << 8);
  const uint32_t idx = uint32_t(b[4] | b[5] << 8 | b[6] << 16 | uint32_t(b[7]) << 24);
  float p[4][4];
  Rgb565(c0, p[0]);
  Rgb565(c1, p[1]);
  p[0][3] = p[1][3] = p[2][3] = p[3][3] = 1.0f;
  for (int i = 0; i < 3; ++i) {
    if (!dxt1 || c0 > c1) {
      p[2][i] = (2 * p[0][i] + p[1][i]) / 3, p[3][i] = (p[0][i] + 2 * p[1][i]) / 3;
    } else {
      p[2][i] = (p[0][i] + p[1][i]) / 2, p[3][i] = 0;
    }
  }
  if (dxt1 && c0 <= c1) p[3][3] = 0;
  for (int t = 0; t < 16; ++t)
    for (int i = 0; i < 4; ++i) out[t][i] = p[(idx >> (2 * t)) & 3][i];
}
void DxtAlpha5(const uint8_t* b, float out[16]) {   // bloque de alfa interpolado (DXT5, DXN, DXT5A)
  const float a0 = b[0] / 255.0f, a1 = b[1] / 255.0f;
  float p[8] = {a0, a1};
  if (b[0] > b[1]) {
    for (int i = 1; i < 7; ++i) p[i + 1] = ((7 - i) * a0 + i * a1) / 7;
  } else {
    for (int i = 1; i < 5; ++i) p[i + 1] = ((5 - i) * a0 + i * a1) / 5;
    p[6] = 0, p[7] = 1;
  }
  uint64_t bits = 0;
  for (int i = 0; i < 6; ++i) bits |= uint64_t(b[2 + i]) << (8 * i);
  for (int t = 0; t < 16; ++t) out[t] = p[(bits >> (3 * t)) & 7];
}

TexView Player::GetTexture(uint32_t n) {
  uint32_t fc[6];
  for (uint32_t i = 0; i < 6; ++i) fc[i] = regs_[0x4800 + 6 * n + i];
  TexView view;
  const uint32_t dim = (fc[5] >> 9) & 3;
  view.dim3d = dim == 2, view.cube = dim == 3;
  if ((fc[0] & 3) != 2) {
    Count("constante de textura vacia");
    return view;
  }
  if (dim == 2 || dim == 3) {
    Count(dim == 2 ? "textura 3D (sin decodificar)" : "cubo (sin decodificar)");
    return view;
  }
  const uint32_t base = fc[1] & 0xFFFFF000, fmtid = fc[1] & 0x3F, endian = (fc[1] >> 6) & 3;
  const uint32_t w = (fc[2] & 0x1FFF) + 1, h = dim == 0 ? 1 : ((fc[2] >> 13) & 0x1FFF) + 1;
  const uint32_t swz = (fc[3] >> 1) & 0xFFF;
  uint32_t sign[4];
  for (uint32_t i = 0; i < 4; ++i) sign[i] = (fc[0] >> (2 + 2 * i)) & 3;
  const bool gamma = sign[0] == 3;

  // destino de un resolve: se lee del render, con el swizzle y el tamano de la textura
  auto rit = resolved_.find(base);
  if (rit != resolved_.end()) {
    Resolved& r = rit->second;
    const uint64_t key = Fnv(fc, sizeof(fc), 0x52455300ull + r.version) ^ uint64_t(base) << 1;
    auto it = tex_cache_.find(key);
    if (it != tex_cache_.end()) return it->second;
    D3D11_TEXTURE2D_DESC td = {w * s_, h * s_, 1, 1, DXGI_FORMAT_R16G16B16A16_FLOAT, {1, 0}, D3D11_USAGE_DEFAULT,
                               D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET, 0, 0};
    ComPtr<ID3D11Texture2D> t;
    ComPtr<ID3D11RenderTargetView> rtv;
    g_.dev->CreateTexture2D(&td, nullptr, &t);
    g_.dev->CreateRenderTargetView(t.Get(), nullptr, &rtv);
    g_.dev->CreateShaderResourceView(t.Get(), nullptr, &view.srv);
    PassConsts pc = {};
    for (uint32_t i = 0; i < 4; ++i) pc.swz[i] = (swz >> (3 * i)) & 7;
    pc.scale = 1.0f;
    pc.flags = gamma ? 4u : 0u;
    const D3D11_RECT rect = {0, 0, LONG(w * s_), LONG(h * s_)};
    Pass(g_.swz_ps.Get(), rtv.Get(), nullptr, w * s_, h * s_, rect, r.srv.Get(), pc);
    Count("textura leida de un resolve");
    return tex_cache_[key] = view;
  }

  const Fmt f = FormatBlock(fmtid);
  const uint32_t pitch_tex = ((fc[0] >> 22) & 0x1FF) * 32;
  const bool tiled = (fc[0] >> 31) != 0;
  const uint32_t wb = (w + f.bw - 1) / f.bw, hb = (h + f.bh - 1) / f.bh;
  const uint32_t pitch_b = std::max((pitch_tex ? pitch_tex : w) / f.bw, wb);
  const uint64_t key = Fnv(fc, sizeof(fc));
  auto it = tex_cache_.find(key);
  if (it != tex_cache_.end()) return it->second;
  uint32_t ox = 0, oy = 0, oz = 0;
  if ((fc[5] >> 11) & 1)   // mips empaquetados: el nivel 0 de una textura pequena esta desplazado dentro del tile
    texture_util::GetPackedMipOffset(w, h, 1, xenos::TextureFormat(fmtid), 0, ox, oy, oz);
  uint32_t bpb_log2 = 0;
  while ((1u << bpb_log2) < f.bpb) ++bpb_log2;
  const uint32_t row_bytes = (pitch_b * f.bpb + 255) & ~255u;
  auto offset = [&](uint32_t x, uint32_t y) -> uint32_t {
    x += ox, y += oy;
    return tiled ? uint32_t(texture_util::GetTiledOffset2D(int32_t(x), int32_t(y), pitch_b, bpb_log2))
                 : y * row_bytes + x * f.bpb;
  };
  // palabras con el orden de bytes de la constante, ya en el orden de la CPU
  auto block = [&](uint32_t x, uint32_t y, uint8_t* dst) {
    const uint32_t o = (base + offset(x, y)) & (kMemBytes - 1);
    const uint32_t aligned = o & ~3u;
    uint8_t tmp[24];
    for (uint32_t i = 0; i < (f.bpb + 7) / 4 + 1 && aligned + 4 * i + 4 <= kMemBytes; ++i) {
      uint32_t v;
      std::memcpy(&v, mem_.data() + aligned + 4 * i, 4);
      v = Swap(v, endian);
      std::memcpy(tmp + 4 * i, &v, 4);
    }
    std::memcpy(dst, tmp + (o - aligned), f.bpb);
  };

  std::vector<float> px(size_t(w) * h * 4, 0.0f);
  bool ok = true;
  auto put = [&](uint32_t x, uint32_t y, const float* c) {
    if (x < w && y < h) std::memcpy(&px[(size_t(y) * w + x) * 4], c, 16);
  };
  uint8_t b[16];
  for (uint32_t by = 0; by < hb && ok; ++by) {
    for (uint32_t bx = 0; bx < wb && ok; ++bx) {
      block(bx, by, b);
      uint32_t v32;
      std::memcpy(&v32, b, 4);
      const uint16_t v16 = uint16_t(b[0] | b[1] << 8);
      float c[4] = {0, 0, 0, 1};
      auto un = [](uint32_t v, uint32_t off, uint32_t bits) {
        return float((v >> off) & ((1u << bits) - 1)) / float((1u << bits) - 1);
      };
      switch (fmtid) {
        case 2: case 8: case 9: c[0] = b[0] / 255.0f; break;
        case 6: case 14: case 50:
          for (int i = 0; i < 4; ++i) c[i] = un(v32, 8 * i, 8);
          break;
        case 7: case 54:
          c[0] = un(v32, 0, 10), c[1] = un(v32, 10, 10), c[2] = un(v32, 20, 10), c[3] = un(v32, 30, 2);
          break;
        case 10: c[0] = un(v16, 0, 8), c[1] = un(v16, 8, 8); break;
        case 4: c[0] = un(v16, 0, 5), c[1] = un(v16, 5, 6), c[2] = un(v16, 11, 5); break;
        case 5: c[0] = un(v16, 0, 5), c[1] = un(v16, 5, 5), c[2] = un(v16, 10, 6); break;
        case 3: c[0] = un(v16, 0, 5), c[1] = un(v16, 5, 5), c[2] = un(v16, 10, 5), c[3] = un(v16, 15, 1); break;
        case 15: for (int i = 0; i < 4; ++i) c[i] = un(v16, 4 * i, 4); break;
        case 16: case 55: c[0] = un(v32, 0, 11), c[1] = un(v32, 11, 11), c[2] = un(v32, 22, 10); break;
        case 17: case 56: c[0] = un(v32, 0, 10), c[1] = un(v32, 10, 11), c[2] = un(v32, 21, 11); break;
        case 22: c[0] = float(v32 >> 8) / 16777215.0f; break;
        case 23: c[0] = Float20e4(v32 >> 8); break;
        case 24: case 27: c[0] = v16 / 65535.0f; break;
        case 25: case 28: c[0] = un(v32, 0, 16), c[1] = un(v32, 16, 16); break;
        case 26: case 29:
          for (int i = 0; i < 4; ++i) c[i] = uint16_t(b[2 * i] | b[2 * i + 1] << 8) / 65535.0f;
          break;
        case 30: c[0] = Half(v16); break;
        case 31: c[0] = Half(uint16_t(v32)), c[1] = Half(uint16_t(v32 >> 16)); break;
        case 32:
          for (int i = 0; i < 4; ++i) c[i] = Half(uint16_t(b[2 * i] | b[2 * i + 1] << 8));
          break;
        case 33: c[0] = float(v32) / 4294967295.0f; break;
        case 36: c[0] = AsFloat(v32); break;
        case 37: case 38:
          for (int i = 0; i < (fmtid == 37 ? 2 : 4); ++i) {
            uint32_t u;
            std::memcpy(&u, b + 4 * i, 4);
            c[i] = AsFloat(u);
          }
          break;
        case 18: case 51: case 19: case 52: case 20: case 53: case 49: case 59: case 58: {
          float t[16][4];
          float a[16];
          if (fmtid == 18 || fmtid == 51) {
            DxtColor(b, true, t);
          } else if (fmtid == 19 || fmtid == 52) {
            DxtColor(b + 8, false, t);
            for (int i = 0; i < 16; ++i) t[i][3] = float((b[i / 2] >> (4 * (i & 1))) & 15) / 15.0f;
          } else if (fmtid == 20 || fmtid == 53) {
            DxtColor(b + 8, false, t);
            DxtAlpha5(b, a);
            for (int i = 0; i < 16; ++i) t[i][3] = a[i];
          } else if (fmtid == 49) {   // DXN: dos bloques de alfa -> x, y
            DxtAlpha5(b, a);
            for (int i = 0; i < 16; ++i) t[i][0] = a[i], t[i][2] = 0, t[i][3] = 1;
            DxtAlpha5(b + 8, a);
            for (int i = 0; i < 16; ++i) t[i][1] = a[i];
          } else if (fmtid == 59) {   // DXT5A
            DxtAlpha5(b, a);
            for (int i = 0; i < 16; ++i) t[i][0] = a[i], t[i][1] = t[i][2] = 0, t[i][3] = 1;
          } else {   // DXT3A
            for (int i = 0; i < 16; ++i)
              t[i][0] = float((b[i / 2] >> (4 * (i & 1))) & 15) / 15.0f, t[i][1] = t[i][2] = 0, t[i][3] = 1;
          }
          for (int i = 0; i < 16; ++i) put(bx * 4 + (i & 3), by * 4 + (i >> 2), t[i]);
          continue;
        }
        default:
          ok = false;
          Count(fmt::format("formato de textura {} (sin decodificar)", fmtid));
          continue;
      }
      put(bx, by, c);
    }
  }
  if (!ok) return tex_cache_[key] = view;

  // signo y swizzle de la constante de fetch
  bool fits8 = true;
  for (size_t i = 0; i < size_t(w) * h; ++i) {
    float* c = &px[i * 4];
    float o[4];
    for (int k = 0; k < 4; ++k) {
      float v = c[k];
      if (sign[k] == 1) v = v * 255.0f / 127.0f > 1.0f ? (v * 255.0f - 256.0f) / 127.0f : v * 255.0f / 127.0f;
      else if (sign[k] == 2) v = v * 2.0f - 1.0f;
      c[k] = v;
    }
    for (int k = 0; k < 4; ++k) {
      const uint32_t sel = (swz >> (3 * k)) & 7;
      o[k] = sel < 4 ? c[sel] : (sel == 4 ? 0.0f : 1.0f);
      if (o[k] < 0.0f || o[k] > 1.0f) fits8 = false;
    }
    std::memcpy(c, o, 16);
  }
  const bool narrow = f.bpb * 8 / std::max(1u, f.bw * f.bh) <= 32 && fmtid != 36 && fmtid != 30 && fmtid != 31 &&
                      fmtid != 23 && fmtid != 22;
  D3D11_TEXTURE2D_DESC td = {w, h, 1, 1, DXGI_FORMAT_R32G32B32A32_FLOAT, {1, 0}, D3D11_USAGE_IMMUTABLE,
                             D3D11_BIND_SHADER_RESOURCE, 0, 0};
  std::vector<uint8_t> p8;
  D3D11_SUBRESOURCE_DATA init = {px.data(), w * 16, 0};
  if (narrow && fits8) {
    td.Format = gamma ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM;
    p8.resize(size_t(w) * h * 4);
    for (size_t i = 0; i < p8.size(); ++i) p8[i] = uint8_t(std::lround(px[i] * 255.0f));
    init = {p8.data(), w * 4, 0};
  } else if (gamma) {
    for (size_t i = 0; i < size_t(w) * h; ++i)
      for (int k = 0; k < 3; ++k) {
        float& v = px[i * 4 + k];
        v = v <= 0.04045f ? v / 12.92f : std::pow((v + 0.055f) / 1.055f, 2.4f);
      }
  }
  ComPtr<ID3D11Texture2D> t;
  if (FAILED(g_.dev->CreateTexture2D(&td, &init, &t))) {
    Count("textura que no se crea");
    return tex_cache_[key] = view;
  }
  g_.dev->CreateShaderResourceView(t.Get(), nullptr, &view.srv);
  Count("textura de memoria");
  return tex_cache_[key] = view;
}

ID3D11SamplerState* Player::GetSampler(uint32_t n) {
  uint32_t fc[6];
  for (uint32_t i = 0; i < 6; ++i) fc[i] = regs_[0x4800 + 6 * n + i];
  static const D3D11_TEXTURE_ADDRESS_MODE kAddr[8] = {
      D3D11_TEXTURE_ADDRESS_WRAP,        D3D11_TEXTURE_ADDRESS_MIRROR, D3D11_TEXTURE_ADDRESS_CLAMP,
      D3D11_TEXTURE_ADDRESS_MIRROR_ONCE, D3D11_TEXTURE_ADDRESS_CLAMP,  D3D11_TEXTURE_ADDRESS_MIRROR_ONCE,
      D3D11_TEXTURE_ADDRESS_BORDER,      D3D11_TEXTURE_ADDRESS_BORDER};
  D3D11_SAMPLER_DESC d = {};
  d.AddressU = kAddr[(fc[0] >> 10) & 7];
  d.AddressV = kAddr[(fc[0] >> 13) & 7];
  d.AddressW = kAddr[(fc[0] >> 16) & 7];
  const uint32_t mag = (fc[3] >> 19) & 3, min = (fc[3] >> 21) & 3, mip = (fc[3] >> 23) & 3, aniso = (fc[3] >> 25) & 7;
  const bool lmag = mag == 1, lmin = min == 1, lmip = mip == 1;
  if (aniso > 1) {
    d.Filter = D3D11_FILTER_ANISOTROPIC;
    d.MaxAnisotropy = std::min(16u, 1u << (aniso - 1));
  } else {
    d.Filter = D3D11_FILTER(D3D11_FILTER_MIN_MAG_MIP_POINT | (lmin ? 0x10 : 0) | (lmag ? 0x4 : 0) | (lmip ? 0x1 : 0));
  }
  const float bc = (fc[5] & 3) == 1 ? 1.0f : 0.0f;
  d.BorderColor[0] = d.BorderColor[1] = d.BorderColor[2] = d.BorderColor[3] = bc;
  d.MaxLOD = D3D11_FLOAT32_MAX;
  d.ComparisonFunc = D3D11_COMPARISON_NEVER;
  const std::string k(reinterpret_cast<const char*>(&d), sizeof(d));
  auto& s = g_.samplers[k];
  if (!s) g_.dev->CreateSamplerState(&d, &s);
  return s.Get();
}

void Player::Pass(ID3D11PixelShader* ps, ID3D11RenderTargetView* rtv, ID3D11DepthStencilView* dsv, uint32_t w,
                  uint32_t h, const D3D11_RECT& rect, ID3D11ShaderResourceView* src, const PassConsts& pc,
                  ID3D11DepthStencilState* dss, UINT stencil_ref) {
  auto* c = g_.ctx.Get();
  c->ClearState();
  c->UpdateSubresource(g_.cb_pass.Get(), 0, nullptr, &pc, 0, 0);
  c->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  c->VSSetShader(g_.pass_vs.Get(), nullptr, 0);
  c->PSSetShader(ps, nullptr, 0);
  c->PSSetConstantBuffers(0, 1, g_.cb_pass.GetAddressOf());
  if (src) c->PSSetShaderResources(0, 1, &src);
  D3D11_RASTERIZER_DESC rd = {D3D11_FILL_SOLID, D3D11_CULL_NONE, FALSE, 0, 0, 0, TRUE, TRUE, FALSE, FALSE};
  auto& rs = g_.raster["pass"];
  if (!rs) g_.dev->CreateRasterizerState(&rd, &rs);
  c->RSSetState(rs.Get());
  const D3D11_VIEWPORT vp = {0, 0, float(w), float(h), 0, 1};
  c->RSSetViewports(1, &vp);
  c->RSSetScissorRects(1, &rect);
  c->OMSetRenderTargets(rtv ? 1 : 0, rtv ? &rtv : nullptr, dsv);
  if (dss) c->OMSetDepthStencilState(dss, stencil_ref);
  c->Draw(3, 0);
  c->ClearState();
}

void Player::Export(ID3D11ShaderResourceView* srv, uint32_t w, uint32_t h, bool depth, const fs::path& path) {
  D3D11_TEXTURE2D_DESC td = {w, h, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, {1, 0}, D3D11_USAGE_DEFAULT,
                             D3D11_BIND_RENDER_TARGET, 0, 0};
  ComPtr<ID3D11Texture2D> t, st;
  ComPtr<ID3D11RenderTargetView> rtv;
  g_.dev->CreateTexture2D(&td, nullptr, &t);
  g_.dev->CreateRenderTargetView(t.Get(), nullptr, &rtv);
  PassConsts pc = {};
  pc.scale = 1.0f;
  pc.flags = depth ? 2u : 0u;
  Pass(g_.copy_ps.Get(), rtv.Get(), nullptr, w, h, {0, 0, LONG(w), LONG(h)}, srv, pc);
  td.Usage = D3D11_USAGE_STAGING;
  td.BindFlags = 0;
  td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  g_.dev->CreateTexture2D(&td, nullptr, &st);
  g_.ctx->CopyResource(st.Get(), t.Get());
  D3D11_MAPPED_SUBRESOURCE m;
  if (FAILED(g_.ctx->Map(st.Get(), 0, D3D11_MAP_READ, 0, &m))) return;
  std::vector<uint8_t> px(size_t(w) * h * 4);
  for (uint32_t y = 0; y < h; ++y) {
    std::memcpy(&px[size_t(y) * w * 4], static_cast<uint8_t*>(m.pData) + size_t(y) * m.RowPitch, size_t(w) * 4);
    for (uint32_t x = 0; x < w; ++x) px[(size_t(y) * w + x) * 4 + 3] = 255;   // sin alfa: se ve el color
  }
  g_.ctx->Unmap(st.Get(), 0);
  ComPtr<IWICImagingFactory> wic;
  if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic)))) return;
  ComPtr<IWICStream> stream;
  ComPtr<IWICBitmapEncoder> enc;
  ComPtr<IWICBitmapFrameEncode> frame;
  wic->CreateStream(&stream);
  if (FAILED(stream->InitializeFromFilename(path.wstring().c_str(), GENERIC_WRITE))) return;
  wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc);
  enc->Initialize(stream.Get(), WICBitmapEncoderNoCache);
  enc->CreateNewFrame(&frame, nullptr);
  frame->Initialize(nullptr);
  frame->SetSize(w, h);
  WICPixelFormatGUID pf = GUID_WICPixelFormat32bppRGBA;
  frame->SetPixelFormat(&pf);
  frame->WritePixels(h, w * 4, UINT(px.size()), px.data());
  frame->Commit();
  enc->Commit();
  ++exported_;
}

// ---- resolve: copia de EDRAM a textura (y borrado)
void Player::Resolve() {
  ++resolves_;
  const auto cc = R<reg::RB_COPY_CONTROL>();
  const auto si = R<reg::RB_SURFACE_INFO>();
  const auto di = R<reg::RB_COPY_DEST_INFO>();
  const auto dp = R<reg::RB_COPY_DEST_PITCH>();
  // rectangulo: los tres vertices de la constante de fetch 0 (float2), como en GetResolveInfo del SDK
  const uint32_t f0 = regs_[0x4800], f1 = regs_[0x4801];
  const uint32_t va = (f0 & ~3u) & (kMemBytes - 1), ve = f1 & 3;
  const float half = R<reg::PA_SU_VTX_CNTL>().pix_center == xenos::PixelCenter::kD3DZero ? 0.5f : 0.0f;
  float v[6];
  for (int i = 0; i < 6; ++i) {
    uint32_t u;
    std::memcpy(&u, mem_.data() + va + 4 * i, 4);
    v[i] = AsFloat(Swap(u, ve)) + half;
  }
  int32_t x0 = int32_t(std::floor(std::min({v[0], v[2], v[4]}) + 0.5f));
  int32_t y0 = int32_t(std::floor(std::min({v[1], v[3], v[5]}) + 0.5f));
  int32_t x1 = int32_t(std::floor(std::max({v[0], v[2], v[4]}) + 0.5f));
  int32_t y1 = int32_t(std::floor(std::max({v[1], v[3], v[5]}) + 0.5f));
  if (R<reg::PA_SU_SC_MODE_CNTL>().vtx_window_offset_enable) {
    const auto wo = R<reg::PA_SC_WINDOW_OFFSET>();
    x0 += wo.window_x_offset, x1 += wo.window_x_offset, y0 += wo.window_y_offset, y1 += wo.window_y_offset;
  }
  if (resolves_ <= 3)
    log_.push_back(fmt::format("  rect: fetch0 {:08X} {:08X}, v = {} {} {} {} {} {}", f0, f1, v[0], v[1], v[2], v[3],
                               v[4], v[5]));
  D3D11_RECT sc;
  if (!Scissor(sc)) return Count("resolve sin area");
  x0 = std::clamp(x0, int32_t(sc.left / s_), int32_t(sc.right / s_));
  x1 = std::clamp(x1, int32_t(sc.left / s_), int32_t(sc.right / s_));
  y0 = std::clamp(y0, int32_t(sc.top / s_), int32_t(sc.bottom / s_));
  y1 = std::clamp(y1, int32_t(sc.top / s_), int32_t(sc.bottom / s_));
  x0 &= ~7, y0 &= ~7, x1 = (x1 + 7) & ~7, y1 = (y1 + 7) & ~7;
  if (x0 >= x1 || y0 >= y1) return Count("resolve sin area");
  const D3D11_RECT rect = {LONG(x0 * s_), LONG(y0 * s_), LONG(x1 * s_), LONG(y1 * s_)};

  const bool is_depth = cc.copy_src_select >= 4;
  Rt* src = nullptr;
  if (is_depth) {
    const auto d = R<reg::RB_DEPTH_INFO>();
    src = GetRt(d.depth_base, si.surface_pitch, uint32_t(d.depth_format), true);
  } else {
    static const uint32_t kInfo[4] = {0x2001, 0x2003, 0x2004, 0x2005};
    reg::RB_COLOR_INFO ci;
    ci.value = regs_[kInfo[cc.copy_src_select & 3]];
    src = GetRt(ci.color_base, si.surface_pitch, uint32_t(ci.color_format), false);
  }
  const uint32_t dest = regs_[0x2319];
  if (src && (cc.copy_command == xenos::CopyCommand::kRaw || cc.copy_command == xenos::CopyCommand::kConvert)) {
    const uint32_t dw = std::max<uint32_t>(dp.copy_dest_pitch, 1), dh = std::max<uint32_t>(dp.copy_dest_height, 1);
    Resolved& r = resolved_[dest];
    if (!r.tex || r.w != dw || r.h != dh) {
      r = Resolved();
      r.w = dw, r.h = dh;
      D3D11_TEXTURE2D_DESC td = {dw * s_, dh * s_, 1, 1, DXGI_FORMAT_R16G16B16A16_FLOAT, {1, 0},
                                 D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET, 0, 0};
      g_.dev->CreateTexture2D(&td, nullptr, &r.tex);
      g_.dev->CreateRenderTargetView(r.tex.Get(), nullptr, &r.rtv);
      g_.dev->CreateShaderResourceView(r.tex.Get(), nullptr, &r.srv);
    }
    ++r.version;
    r.depth = is_depth;
    PassConsts pc = {};
    pc.scale = is_depth ? 1.0f : std::ldexp(1.0f, di.copy_dest_exp_bias);
    pc.flags = (is_depth ? 2u : 0u) | (di.copy_dest_swap ? 1u : 0u);
    Pass(g_.copy_ps.Get(), r.rtv.Get(), nullptr, dw * s_, dh * s_, rect, src->srv.Get(), pc);
    if (dw * dh >= 200 * 200) {
      const fs::path p = out_ / fmt::format("resolve_{:03}_{:08X}_{}x{}.png", resolves_, dest, dw, dh);
      Export(r.srv.Get(), dw * s_, dh * s_, false, p);
    }
    log_.push_back(fmt::format("resolve {:3} tras {:4} draws: {} {} {}x{} @{} -> {:08X} ({}x{})", resolves_, draws_,
                               is_depth ? "profundidad" : "color", src->base, x1 - x0, y1 - y0, x0, dest, dw, dh));
  }
  // borrados
  if (cc.color_clear_enable && !is_depth && src) {
    const uint32_t cv = regs_[0x231E];
    PassConsts pc = {};
    for (int i = 0; i < 4; ++i) pc.color[i] = float((cv >> (8 * i)) & 0xFF) / 255.0f;
    if (src->fmt != 0 && src->fmt != 1)   // formatos de 16/32 bits: el valor es de otro tipo, se aproxima a 0
      std::memset(pc.color, 0, sizeof(pc.color));
    Pass(g_.clear_ps.Get(), src->rtv.Get(), nullptr, src->pitch * s_, kEdramHeight * s_, rect, nullptr, pc);
  }
  if (cc.depth_clear_enable) {
    const auto d = R<reg::RB_DEPTH_INFO>();
    Rt* ds = GetRt(d.depth_base, si.surface_pitch, uint32_t(d.depth_format), true);
    const uint32_t dv = regs_[0x231D];
    PassConsts pc = {};
    pc.depth = d.depth_format == xenos::DepthRenderTargetFormat::kD24FS8 ? Float20e4(dv >> 8)
                                                                          : float(dv >> 8) / 16777215.0f;
    D3D11_DEPTH_STENCIL_DESC dd = {};
    dd.DepthEnable = TRUE;
    dd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    dd.DepthFunc = D3D11_COMPARISON_ALWAYS;
    dd.StencilEnable = TRUE;
    dd.StencilReadMask = dd.StencilWriteMask = 0xFF;
    dd.FrontFace = {D3D11_STENCIL_OP_REPLACE, D3D11_STENCIL_OP_REPLACE, D3D11_STENCIL_OP_REPLACE,
                    D3D11_COMPARISON_ALWAYS};
    dd.BackFace = dd.FrontFace;
    auto& dss = g_.depth["clear"];
    if (!dss) g_.dev->CreateDepthStencilState(&dd, &dss);
    Pass(g_.clear_ps.Get(), nullptr, ds->dsv.Get(), ds->pitch * s_, kEdramHeight * s_, rect, nullptr, pc, dss.Get(),
         dv & 0xFF);
  }
}

// ---- draw
D3D11_BLEND Factor(xenos::BlendFactor f, bool alpha) {
  switch (uint32_t(f)) {
    case 0: return D3D11_BLEND_ZERO;
    case 1: return D3D11_BLEND_ONE;
    case 4: return alpha ? D3D11_BLEND_SRC_ALPHA : D3D11_BLEND_SRC_COLOR;
    case 5: return alpha ? D3D11_BLEND_INV_SRC_ALPHA : D3D11_BLEND_INV_SRC_COLOR;
    case 6: return D3D11_BLEND_SRC_ALPHA;
    case 7: return D3D11_BLEND_INV_SRC_ALPHA;
    case 8: return alpha ? D3D11_BLEND_DEST_ALPHA : D3D11_BLEND_DEST_COLOR;
    case 9: return alpha ? D3D11_BLEND_INV_DEST_ALPHA : D3D11_BLEND_INV_DEST_COLOR;
    case 10: return D3D11_BLEND_DEST_ALPHA;
    case 11: return D3D11_BLEND_INV_DEST_ALPHA;
    case 12: case 14: return D3D11_BLEND_BLEND_FACTOR;
    case 13: case 15: return D3D11_BLEND_INV_BLEND_FACTOR;
    case 16: return alpha ? D3D11_BLEND_ONE : D3D11_BLEND_SRC_ALPHA_SAT;
    default: return D3D11_BLEND_ONE;
  }
}
D3D11_BLEND_OP Op(xenos::BlendOp o) {
  static const D3D11_BLEND_OP k[5] = {D3D11_BLEND_OP_ADD, D3D11_BLEND_OP_SUBTRACT, D3D11_BLEND_OP_MIN,
                                      D3D11_BLEND_OP_MAX, D3D11_BLEND_OP_REV_SUBTRACT};
  return uint32_t(o) < 5 ? k[uint32_t(o)] : D3D11_BLEND_OP_ADD;
}
D3D11_STENCIL_OP SOp(xenos::StencilOp o) {
  static const D3D11_STENCIL_OP k[8] = {D3D11_STENCIL_OP_KEEP,     D3D11_STENCIL_OP_ZERO,     D3D11_STENCIL_OP_REPLACE,
                                        D3D11_STENCIL_OP_INCR_SAT, D3D11_STENCIL_OP_DECR_SAT, D3D11_STENCIL_OP_INVERT,
                                        D3D11_STENCIL_OP_INCR,     D3D11_STENCIL_OP_DECR};
  return k[uint32_t(o) & 7];
}

struct SysConsts {
  float ndc_scale[4], ndc_offset[4];
  uint32_t flags, alpha_func;
  float alpha_ref;
  uint32_t param_gen;
  float res_scale[2], pad[2];
};

void Player::Draw(uint32_t init_v, const uint32_t* imm, uint32_t imm_words, bool has_dma, uint32_t dma_base,
                  uint32_t dma_size) {
  reg::VGT_DRAW_INITIATOR init;
  init.value = init_v;
  const auto mode = R<reg::RB_MODECONTROL>().edram_mode;
  if (mode == xenos::EdramMode::kCopy) return Resolve();
  if (mode != xenos::EdramMode::kColorDepth && mode != xenos::EdramMode::kDepthOnly) return Count("draw sin EDRAM");
  ++draws_;
  const uint32_t prim = uint32_t(init.prim_type), n = init.num_indices;
  if (!n) return Count("draw sin indices");

  // indices
  std::vector<uint32_t> idx(n);
  const bool i32 = init.index_size == xenos::IndexFormat::kInt32;
  if (init.source_select == xenos::SourceSelect::kAutoIndex) {
    for (uint32_t i = 0; i < n; ++i) idx[i] = i;
  } else if (init.source_select == xenos::SourceSelect::kImmediate) {
    for (uint32_t i = 0; i < n; ++i) {
      const uint32_t w = i32 ? i : i / 2;
      const uint32_t v = w < imm_words ? imm[w] : 0;
      idx[i] = i32 ? v : (i & 1) ? v >> 16 : v & 0xFFFF;
    }
  } else {
    if (!has_dma) {
      dma_base = regs_[0x21FA];
      dma_size = regs_[0x21FB];
    }
    const uint32_t e = dma_size >> 30, base = dma_base & (kMemBytes - 1);
    for (uint32_t i = 0; i < n; ++i) {
      if (i32) {
        uint32_t v;
        std::memcpy(&v, mem_.data() + ((base + 4 * i) & (kMemBytes - 1)), 4);
        idx[i] = Swap(v, e);
      } else {
        uint16_t v;
        std::memcpy(&v, mem_.data() + ((base + 2 * i) & (kMemBytes - 1)), 2);
        idx[i] = (e == 1) ? uint16_t(_byteswap_ushort(v)) : v;
      }
    }
  }
  const uint32_t ioff = regs_[0x2102];
  for (uint32_t& i : idx) i += ioff;

  D3D11_PRIMITIVE_TOPOLOGY topo;
  bool rect = false, polygonal = true;
  switch (prim) {
    case 1: topo = D3D11_PRIMITIVE_TOPOLOGY_POINTLIST, polygonal = false; break;
    case 2: topo = D3D11_PRIMITIVE_TOPOLOGY_LINELIST, polygonal = false; break;
    case 3: topo = D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP, polygonal = false; break;
    case 4: topo = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST; break;
    case 6: topo = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP; break;
    case 5: {   // abanico -> lista
      std::vector<uint32_t> l;
      for (uint32_t i = 2; i < n; ++i) l.insert(l.end(), {idx[0], idx[i - 1], idx[i]});
      idx.swap(l);
      topo = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    } break;
    case 13: {   // quads -> lista
      std::vector<uint32_t> l;
      for (uint32_t i = 0; i + 3 < n; i += 4) l.insert(l.end(), {idx[i], idx[i + 1], idx[i + 2], idx[i], idx[i + 2], idx[i + 3]});
      idx.swap(l);
      topo = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    } break;
    case 8: topo = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST, rect = true; break;
    default: return Count(fmt::format("primitiva {} (sin soporte)", prim));
  }
  if (idx.empty()) return;

  Prog* vs = GetProg(0);
  Prog* ps = mode == xenos::EdramMode::kColorDepth ? GetProg(1) : nullptr;
  if (!vs || (mode == xenos::EdramMode::kColorDepth && !ps)) return Count("draw sin shader");

  // render targets
  const auto si = R<reg::RB_SURFACE_INFO>();
  const uint32_t mask = regs_[0x2104];
  static const uint32_t kInfo[4] = {0x2001, 0x2003, 0x2004, 0x2005};
  ID3D11RenderTargetView* rtv[4] = {};
  uint32_t nrt = 0;
  if (ps) {
    for (uint32_t i = 0; i < 4; ++i) {
      const bool writes = ps->shader->writes_color_target(i) || (i == 0 && !ps->shader->writes_color_targets());
      if (!writes || !((mask >> (4 * i)) & 0xF)) continue;
      reg::RB_COLOR_INFO ci;
      ci.value = regs_[kInfo[i]];
      Rt* rt = GetRt(ci.color_base, si.surface_pitch, uint32_t(ci.color_format), false);
      if (rt) rtv[i] = rt->rtv.Get(), nrt = i + 1;
    }
  }
  const auto dc = R<reg::RB_DEPTHCONTROL>();
  ID3D11DepthStencilView* dsv = nullptr;
  if (dc.z_enable || dc.stencil_enable) {
    const auto d = R<reg::RB_DEPTH_INFO>();
    Rt* ds = GetRt(d.depth_base, si.surface_pitch, uint32_t(d.depth_format), true);
    if (ds) dsv = ds->dsv.Get();
  }
  if (!nrt && !dsv) return Count("draw sin destino");
  D3D11_RECT sc;
  if (!Scissor(sc)) return Count("draw fuera del scissor");

  // estado de mezcla
  D3D11_BLEND_DESC bd = {};
  bd.IndependentBlendEnable = TRUE;
  static const uint32_t kBlend[4] = {0x2201, 0x2209, 0x220A, 0x220B};
  for (uint32_t i = 0; i < 4; ++i) {
    reg::RB_BLENDCONTROL b;
    b.value = regs_[kBlend[i]];
    auto& t = bd.RenderTarget[i];
    t.RenderTargetWriteMask = UINT8((mask >> (4 * i)) & 0xF);
    t.SrcBlend = Factor(b.color_srcblend, false);
    t.DestBlend = Factor(b.color_destblend, false);
    t.BlendOp = Op(b.color_comb_fcn);
    t.SrcBlendAlpha = Factor(b.alpha_srcblend, true);
    t.DestBlendAlpha = Factor(b.alpha_destblend, true);
    t.BlendOpAlpha = Op(b.alpha_comb_fcn);
    t.BlendEnable = !(t.SrcBlend == D3D11_BLEND_ONE && t.DestBlend == D3D11_BLEND_ZERO &&
                      t.BlendOp == D3D11_BLEND_OP_ADD && t.SrcBlendAlpha == D3D11_BLEND_ONE &&
                      t.DestBlendAlpha == D3D11_BLEND_ZERO && t.BlendOpAlpha == D3D11_BLEND_OP_ADD);
  }
  auto& bs = g_.blend[std::string(reinterpret_cast<const char*>(&bd), sizeof(bd))];
  if (!bs) g_.dev->CreateBlendState(&bd, &bs);
  const float bf[4] = {AsFloat(regs_[0x2105]), AsFloat(regs_[0x2106]), AsFloat(regs_[0x2107]), AsFloat(regs_[0x2108])};

  // profundidad y stencil
  D3D11_DEPTH_STENCIL_DESC dd = {};
  dd.DepthEnable = dc.z_enable;
  dd.DepthWriteMask = dc.z_write_enable ? D3D11_DEPTH_WRITE_MASK_ALL : D3D11_DEPTH_WRITE_MASK_ZERO;
  dd.DepthFunc = D3D11_COMPARISON_FUNC(uint32_t(dc.zfunc) + 1);
  dd.StencilEnable = dc.stencil_enable;
  const auto sr = R<reg::RB_STENCILREFMASK>();
  dd.StencilReadMask = UINT8(sr.stencilmask);
  dd.StencilWriteMask = UINT8(sr.stencilwritemask);
  dd.FrontFace = {SOp(dc.stencilfail), SOp(dc.stencilzfail), SOp(dc.stencilzpass),
                  D3D11_COMPARISON_FUNC(uint32_t(dc.stencilfunc) + 1)};
  dd.BackFace = dc.backface_enable ? D3D11_DEPTH_STENCILOP_DESC{SOp(dc.stencilfail_bf), SOp(dc.stencilzfail_bf),
                                                                SOp(dc.stencilzpass_bf),
                                                                D3D11_COMPARISON_FUNC(uint32_t(dc.stencilfunc_bf) + 1)}
                                   : dd.FrontFace;
  auto& dss = g_.depth[std::string(reinterpret_cast<const char*>(&dd), sizeof(dd))];
  if (!dss) g_.dev->CreateDepthStencilState(&dd, &dss);

  // rasterizado
  const auto sm = R<reg::PA_SU_SC_MODE_CNTL>();
  if (polygonal && !rect && sm.cull_front && sm.cull_back) return Count("draw con las dos caras eliminadas");
  D3D11_RASTERIZER_DESC rd = {D3D11_FILL_SOLID, D3D11_CULL_NONE, sm.face ? FALSE : TRUE, 0, 0, 0, TRUE, TRUE,
                              FALSE, FALSE};
  if (polygonal && !rect) rd.CullMode = sm.cull_front ? D3D11_CULL_FRONT : sm.cull_back ? D3D11_CULL_BACK : D3D11_CULL_NONE;
  auto& rs = g_.raster[std::string(reinterpret_cast<const char*>(&rd), sizeof(rd))];
  if (!rs) g_.dev->CreateRasterizerState(&rd, &rs);

  // constantes del sistema: de las coordenadas de pantalla de la 360 al NDC de D3D11 sobre el render target entero
  const auto vte = R<reg::PA_CL_VTE_CNTL>();
  const float W = float(si.surface_pitch), H = float(kEdramHeight);
  float xs = vte.vport_x_scale_ena ? AsFloat(regs_[0x210F]) : 1.0f, xo = vte.vport_x_offset_ena ? AsFloat(regs_[0x2110]) : 0.0f;
  float ys = vte.vport_y_scale_ena ? AsFloat(regs_[0x2111]) : 1.0f, yo = vte.vport_y_offset_ena ? AsFloat(regs_[0x2112]) : 0.0f;
  float zs = vte.vport_z_scale_ena ? AsFloat(regs_[0x2113]) : 1.0f, zo = vte.vport_z_offset_ena ? AsFloat(regs_[0x2114]) : 0.0f;
  if (sm.vtx_window_offset_enable) {
    const auto wo = R<reg::PA_SC_WINDOW_OFFSET>();
    xo += float(wo.window_x_offset), yo += float(wo.window_y_offset);
  }
  if (R<reg::PA_SU_VTX_CNTL>().pix_center == xenos::PixelCenter::kD3DZero) xo += 0.5f, yo += 0.5f;
  SysConsts sys = {};
  sys.ndc_scale[0] = 2.0f * xs / W, sys.ndc_offset[0] = 2.0f * xo / W - 1.0f;
  sys.ndc_scale[1] = -2.0f * ys / H, sys.ndc_offset[1] = 1.0f - 2.0f * yo / H;
  sys.ndc_scale[2] = zs, sys.ndc_offset[2] = zo;
  sys.ndc_scale[3] = 1.0f;
  sys.flags = (vte.vtx_w0_fmt ? 1u : 0u) | (vte.vtx_xy_fmt ? 2u : 0u) | (vte.vtx_z_fmt ? 4u : 0u) | (polygonal ? 8u : 0u);
  const auto cctl = R<reg::RB_COLORCONTROL>();
  sys.alpha_func = cctl.alpha_test_enable ? uint32_t(cctl.alpha_func) : 7u;
  sys.alpha_ref = AsFloat(regs_[0x210E]);
  const auto pc = R<reg::SQ_PROGRAM_CNTL>();
  sys.param_gen = pc.param_gen ? (((regs_[0x2181] >> 8) & 0xFF) | (prim == 1 ? 0x100u : 0u)) : 0xFFFFu;
  sys.res_scale[0] = sys.res_scale[1] = float(s_);

  Upload();
  auto* c = g_.ctx.Get();
  c->ClearState();
  c->UpdateSubresource(g_.cb_vs_c.Get(), 0, nullptr, &regs_[0x4000], 0, 0);
  c->UpdateSubresource(g_.cb_ps_c.Get(), 0, nullptr, &regs_[0x4400], 0, 0);
  c->UpdateSubresource(g_.cb_bl.Get(), 0, nullptr, &regs_[0x4900], 0, 0);
  c->UpdateSubresource(g_.cb_fc.Get(), 0, nullptr, &regs_[0x4800], 0, 0);
  c->UpdateSubresource(g_.cb_sys.Get(), 0, nullptr, &sys, 0, 0);
  ID3D11Buffer* vcb[4] = {g_.cb_vs_c.Get(), g_.cb_bl.Get(), g_.cb_fc.Get(), g_.cb_sys.Get()};
  ID3D11Buffer* pcb[4] = {g_.cb_ps_c.Get(), g_.cb_bl.Get(), g_.cb_fc.Get(), g_.cb_sys.Get()};
  c->VSSetConstantBuffers(0, 4, vcb);
  c->PSSetConstantBuffers(0, 4, pcb);
  c->VSSetShaderResources(0, 1, g_.mem_srv.GetAddressOf());
  c->PSSetShaderResources(0, 1, g_.mem_srv.GetAddressOf());
  // texturas y samplers que usa cada shader
  auto bind = [&](Prog* p, bool pixel) {
    if (!p) return;
    for (const auto& tb : p->shader->texture_bindings()) {
      const uint32_t n = tb.fetch_constant;
      TexView tv = GetTexture(n);
      ID3D11ShaderResourceView* srv = tv.srv.Get();
      UINT slot = 1 + n;
      if (tv.dim3d) slot = 33 + n, srv = srv ? srv : g_.dummy3d.Get();
      else if (tv.cube) slot = 65 + n, srv = srv ? srv : g_.dummycube.Get();
      else srv = srv ? srv : g_.dummy2d.Get();
      ID3D11SamplerState* smp = GetSampler(n);
      if (pixel) c->PSSetShaderResources(slot, 1, &srv), c->PSSetSamplers(n, 1, &smp);
      else c->VSSetShaderResources(slot, 1, &srv), c->VSSetSamplers(n, 1, &smp);
    }
  };
  bind(vs, false);
  bind(ps, true);
  // indices
  const uint32_t ibytes = uint32_t(idx.size() * 4);
  if (ibytes > g_.ib_size) {
    g_.ib_size = std::max(ibytes, 1u << 20);
    D3D11_BUFFER_DESC d = {g_.ib_size, D3D11_USAGE_DEFAULT, D3D11_BIND_INDEX_BUFFER, 0, 0, 0};
    g_.ib.Reset();
    g_.dev->CreateBuffer(&d, nullptr, &g_.ib);
  }
  D3D11_BOX box = {0, 0, 0, ibytes, 1, 1};
  c->UpdateSubresource(g_.ib.Get(), 0, &box, idx.data(), 0, 0);
  c->IASetIndexBuffer(g_.ib.Get(), DXGI_FORMAT_R32_UINT, 0);
  c->IASetPrimitiveTopology(topo);
  c->VSSetShader(vs->vs.Get(), nullptr, 0);
  c->GSSetShader(rect ? g_.rect_gs.Get() : nullptr, nullptr, 0);
  c->PSSetShader(ps ? ps->ps.Get() : nullptr, nullptr, 0);
  c->RSSetState(rs.Get());
  const D3D11_VIEWPORT vp = {0, 0, W * s_, H * s_, 0, 1};
  c->RSSetViewports(1, &vp);
  c->RSSetScissorRects(1, &sc);
  c->OMSetRenderTargets(nrt, rtv, dsv);
  c->OMSetBlendState(bs.Get(), bf, 0xFFFFFFFF);
  c->OMSetDepthStencilState(dss.Get(), sr.stencilref);
  c->DrawIndexed(UINT(idx.size()), 0, 0);
}

// ---- PM4
void Player::Packet3(uint32_t op, const uint32_t* d, uint32_t n) {
  switch (op) {
    case 0x2D:   // SET_CONSTANT
      if (n >= 2) {
        const uint32_t type = (d[0] >> 16) & 0xFF, i = d[0] & 0x7FF;
        const uint32_t first = type == 0 ? 0x4000 : type == 1 ? 0x4800 : type == 2 ? 0x4900 : type == 3 ? 0x4908 : 0x2000;
        for (uint32_t k = 1; k < n; ++k)
          if (first + i + k - 1 < regs_.size()) regs_[first + i + k - 1] = d[k];
      }
      break;
    case 0x2F:   // LOAD_ALU_CONSTANT: palabras de memoria a registros
      if (n >= 3) {
        const uint32_t a = d[0] & 0x1FFFFFFF, type = (d[1] >> 16) & 0xFF, i = d[1] & 0x7FF, sz = d[2] & 0xFFF;
        const uint32_t first = type == 0 ? 0x4000 : type == 1 ? 0x4800 : type == 2 ? 0x4900 : type == 3 ? 0x4908 : 0x2000;
        for (uint32_t k = 0; k < sz && first + i + k < regs_.size(); ++k)
          regs_[first + i + k] = Be(mem_.data() + ((a + 4 * k) & (kMemBytes - 1)));
      }
      break;
    case 0x27:   // IM_LOAD
      if (n >= 2) {
        const uint32_t type = d[0] & 3;
        if (type < 2) active_[type] = d[0] & ~3u, active_ndw_[type] = d[1] & 0xFFFF;
      }
      break;
    case 0x2B:   // IM_LOAD_IMMEDIATE: microcodigo dentro del paquete
      if (n >= 2) {
        const uint32_t type = d[0] & 3, sz = std::min(d[1] & 0xFFFF, n - 2);
        if (type < 2) {
          std::vector<uint32_t> uc(sz);
          for (uint32_t k = 0; k < sz; ++k) uc[k] = _byteswap_ulong(d[2 + k]);
          const uint32_t fake = 0x80000000u | uint32_t(Fnv(uc.data(), uc.size() * 4) & 0x7FFFFFF0u);
          ucode_[{type, fake}] = std::move(uc);
          active_[type] = fake, active_ndw_[type] = sz;
        }
      }
      break;
    case 0x22:   // DRAW_INDX: viz, iniciador, [base, tamano]
      if (n >= 2) Draw(d[1], nullptr, 0, n >= 4, n >= 4 ? d[2] : 0, n >= 4 ? d[3] : 0);
      break;
    case 0x36:   // DRAW_INDX_2: iniciador e indices dentro
      if (n >= 1) Draw(d[0], d + 1, n - 1, false, 0, 0);
      break;
    case 0x3F: case 0x37: Count(fmt::format("paquete {:02X} (buffer indirecto, sin seguir)", op)); break;
    default: break;
  }
}

void Player::ExecSpan(const uint32_t* dw, uint32_t n) {
  uint32_t i = 0;
  while (i < n) {
    const uint32_t h = dw[i++];
    switch (h >> 30) {
      case 0: {
        const uint32_t c = ((h >> 16) & 0x3FFF) + 1, r = h & 0x7FFF;
        const bool one = (h & 0x8000) != 0;
        for (uint32_t k = 0; k < c && i < n; ++k, ++i) {
          const uint32_t reg = one ? r : r + k;
          if (reg < regs_.size()) regs_[reg] = dw[i];
        }
      } break;
      case 1:
        if (i + 1 < n) regs_[h & 0x7FF] = dw[i], regs_[(h >> 11) & 0x7FF] = dw[i + 1];
        i += 2;
        break;
      case 2:
        break;
      default: {
        const uint32_t c = ((h >> 16) & 0x3FFF) + 1, op = (h >> 8) & 0x7F;
        if (i + c <= n) Packet3(op, dw + i, c);
        i += c;
      } break;
    }
  }
}

bool Player::Run(const std::vector<uint8_t>& b) {
  if (b.size() < 16 || std::memcmp(b.data(), "XFC1", 4) != 0) return false;
  size_t pos = 16;
  bool first_shaders = true;
  while (pos + 8 <= b.size()) {
    char tag[5] = {};
    std::memcpy(tag, &b[pos], 4);
    uint32_t size;
    std::memcpy(&size, &b[pos + 4], 4);
    const uint8_t* body = &b[pos + 8];
    pos += 8 + size;
    if (pos > b.size()) break;
    const std::string t(tag);
    if (t == "SHDR") {
      uint32_t type, addr, ndw;
      std::memcpy(&type, body, 4), std::memcpy(&addr, body + 4, 4), std::memcpy(&ndw, body + 8, 4);
      std::vector<uint32_t> uc(ndw);
      std::memcpy(uc.data(), body + 12, size_t(ndw) * 4);
      ucode_[{type, addr}] = std::move(uc);
      if (first_shaders && type < 2 && !active_[type]) active_[type] = addr, active_ndw_[type] = ndw;
    } else if (t == "MEM ") {
      uint32_t addr, bytes;
      std::memcpy(&addr, body, 4), std::memcpy(&bytes, body + 4, 4);
      addr &= kMemBytes - 1;
      bytes = std::min(bytes, kMemBytes - addr);
      std::memcpy(mem_.data() + addr, body + 8, bytes);
      dirty_.push_back({addr, bytes});
      for (auto it = tex_cache_.begin(); it != tex_cache_.end();) it = tex_cache_.erase(it);   // memoria nueva
    } else if (t == "CALL") {
      first_shaders = false;
      ++calls_;
      uint32_t nspans;
      std::memcpy(&nspans, body + 40, 4);
      if (!regs_init_) {   // estado de la GPU al empezar: la copia del dispositivo
        const uint8_t* shadow = body + 44;
        for (const Block& bl : kBlocks)
          for (uint32_t i = 0; i < bl.count; ++i) regs_[bl.reg + i] = Be(shadow + bl.offset - kShadowOffset + 4 * i);
        regs_init_ = true;
      }
      size_t p = 44 + kShadowBytes + 48;
      for (uint32_t s = 0; s < nspans && p + 8 <= size; ++s) {
        uint32_t addr, n;
        std::memcpy(&addr, body + p, 4), std::memcpy(&n, body + p + 4, 4);
        {   // el buffer de ordenes tambien es memoria: el D3D deja ahi datos (vertices de resolve, constantes)
          const uint32_t pa = addr & (kMemBytes - 1), bytes = std::min<uint32_t>(n * 4, kMemBytes - pa);
          std::memcpy(mem_.data() + pa, body + p + 8, bytes);
          dirty_.push_back({pa, bytes});
        }
        std::vector<uint32_t> dw(n);
        for (uint32_t k = 0; k < n; ++k) dw[k] = Be(body + p + 8 + 4 * k);
        p += 8 + size_t(n) * 4;
        ExecSpan(dw.data(), n);
      }
    } else if (t == "SWAP") {
      break;
    }
  }
  g_.ctx->Flush();
  // render targets grandes al final
  for (auto& [k, rt] : rts_) {
    if (rt.pitch < 640) continue;
    const fs::path p = out_ / fmt::format("rt_{}_{}_{}{}.png", rt.base, rt.pitch, rt.fmt, rt.depth ? "_prof" : "");
    Export(rt.srv.Get(), rt.pitch * s_, 720 * s_, rt.depth, p);
  }
  return true;
}

void Player::Summary() {
  std::string s = fmt::format("{} llamadas, {} draws, {} resolves, {} shaders, {} render targets, {} PNG\n", calls_,
                              draws_, resolves_, progs_.size(), rts_.size(), exported_);
  for (const auto& [k, v] : notes_) s += fmt::format("  {}: {}\n", k, v);
  fmt::print("{}", s);
  std::ofstream f(out_ / "resumen.txt");
  f << s;
  for (const auto& l : log_) f << l << "\n";
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    fmt::print(stderr, "uso: xfcplay <frame.xfc> [--scale N] [--out carpeta] [--warp]\n");
    return 2;
  }
  uint32_t scale = 2;
  bool warp = false;
  fs::path in = argv[1], out;
  for (int i = 2; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--scale" && i + 1 < argc) scale = std::clamp(uint32_t(std::stoul(argv[++i])), 1u, 4u);
    else if (a == "--out" && i + 1 < argc) out = argv[++i];
    else if (a == "--warp") warp = true;
  }
  if (out.empty()) out = in.parent_path() / fmt::format("{}_x{}", in.stem().string(), scale);
  fs::create_directories(out);
  std::ifstream f(in, std::ios::binary);
  if (!f) {
    fmt::print(stderr, "no se puede leer {}\n", in.string());
    return 2;
  }
  const std::vector<uint8_t> b((std::istreambuf_iterator<char>(f)), {});
  CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  g_common = x2h::HlslTranslator::CommonHeader();
  Gpu g;
  if (!InitGpu(g, warp)) {
    fmt::print(stderr, "no se pudo iniciar D3D11\n");
    return 2;
  }
  Player p(g, scale, out);
  const auto t0 = std::chrono::steady_clock::now();
  if (!p.Run(b)) {
    fmt::print(stderr, "{}: no es una captura XFC1\n", in.string());
    return 2;
  }
  const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
  p.Summary();
  fmt::print("salida en {} ({:.0f} ms, incluye traducir y compilar shaders)\n", out.string(), ms);
  return 0;
}
