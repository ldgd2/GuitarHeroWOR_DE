// nativegfx/backend/d3d11/d3d11_shader_cache.cpp
// Implementacion del cache y compilador de shaders nativos en D3D11.
#ifdef _WIN32
#include "nativegfx/backend/d3d11/d3d11_shader_cache.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

namespace re_wor::nativegfx {

namespace {

static const std::filesystem::path& GetNativeShadersDir() {
  static std::filesystem::path s_dir = []() {
    const std::filesystem::path candidates[] = {
        std::filesystem::current_path() / "cache" / "native_shaders",
        std::filesystem::current_path() / "re-wor" / "cache" / "native_shaders",
        std::filesystem::path("cache/native_shaders"),
        std::filesystem::path("re-wor/cache/native_shaders")
    };
    for (const auto& c : candidates) {
      if (std::filesystem::is_directory(c)) {
        return c;
      }
    }
    return std::filesystem::path("cache/native_shaders");
  }();
  return s_dir;
}

class NativeShaderInclude : public ID3DInclude {
 public:
  STDMETHOD(Open)(D3D_INCLUDE_TYPE, LPCSTR pFileName, LPCVOID, LPCVOID* ppData, UINT* pBytes) override {
    if (!pFileName) return E_FAIL;
    std::filesystem::path p = GetNativeShadersDir() / pFileName;
    std::ifstream f(p, std::ios::binary);
    if (f.is_open()) {
      std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
      char* buf = new char[content.size()];
      std::memcpy(buf, content.data(), content.size());
      *ppData = buf;
      *pBytes = static_cast<UINT>(content.size());
      return S_OK;
    }
    return E_FAIL;
  }
  STDMETHOD(Close)(LPCVOID pData) override {
    delete[] static_cast<const char*>(pData);
    return S_OK;
  }
};

static std::string LoadShaderSourceFromDisk(uint64_t hash, const char* stage) {
  char filename[64];
  std::snprintf(filename, sizeof(filename), "%016llX.%s.hlsl", static_cast<unsigned long long>(hash), stage);
  std::filesystem::path p = GetNativeShadersDir() / filename;
  std::ifstream f(p, std::ios::binary);
  if (f.is_open()) {
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  }
  return "";
}

static std::vector<uint8_t> LoadShaderBytecodeFromDisk(uint64_t hash, const char* stage) {
  char filename[64];
  std::snprintf(filename, sizeof(filename), "%016llX.%s.cso", static_cast<unsigned long long>(hash), stage);
  std::filesystem::path p = GetNativeShadersDir() / filename;
  std::ifstream f(p, std::ios::binary);
  if (f.is_open()) {
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  }
  return {};
}

static void SaveShaderBytecodeToDisk(uint64_t hash, const char* stage, const void* data, size_t size) {
  if (!data || size == 0) return;
  char filename[64];
  std::snprintf(filename, sizeof(filename), "%016llX.%s.cso", static_cast<unsigned long long>(hash), stage);
  std::filesystem::path p = GetNativeShadersDir() / filename;
  std::ofstream f(p, std::ios::binary);
  if (f.is_open()) {
    f.write(static_cast<const char*>(data), size);
  }
}

// Geometry shader que expande listas de rectángulos de la 360 (3 esquinas -> 4 vértices para 2 triángulos)
const char* kRectGeometryShaderSource = R"(
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

// Vertex shader de pantalla completa para pases de blit y post-procesado
const char* kPassShaderSource = R"(
Texture2D<float4> src_tex : register(t0);
SamplerState src_smp : register(s0);

float4 vs_main(uint id : SV_VertexID) : SV_Position {
  float2 uv = float2((id << 1) & 2, id & 2);
  return float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
}

float4 ps_copy(float4 p : SV_Position) : SV_Target {
  return src_tex.Load(int3(int2(p.xy), 0));
}
)";

}  // namespace

using Microsoft::WRL::ComPtr;

bool D3D11ShaderCache::Initialize(ID3D11Device* device) {
  device_ = device;
  if (!device_) return false;

  std::string err;
  ComPtr<ID3DBlob> blob;

  // 1. Compilar Geometry Shader de rectángulos
  if (CompileShader(kRectGeometryShaderSource, "main", "gs_5_0", blob, err)) {
    device_->CreateGeometryShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &rect_gs_);
  }

  // 2. Compilar VS de pantalla completa
  if (CompileShader(kPassShaderSource, "vs_main", "vs_5_0", blob, err)) {
    device_->CreateVertexShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &pass_vs_);
  }

  // 3. Compilar PS de copia
  if (CompileShader(kPassShaderSource, "ps_copy", "ps_5_0", blob, err)) {
    device_->CreatePixelShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &copy_ps_);
  }

  return true;
}

bool D3D11ShaderCache::CompileShader(const std::string& source, const char* entry, const char* profile,
                                     ComPtr<ID3DBlob>& out_blob, std::string& out_err) {
  ComPtr<ID3DBlob> errors;
  // Compilar con IEEE_STRICTNESS para preservar semántica de NaNs y coma flotante de Xbox 360
  UINT flags = D3DCOMPILE_OPTIMIZATION_LEVEL1 | D3DCOMPILE_IEEE_STRICTNESS;

  NativeShaderInclude include_handler;
  HRESULT hr = D3DCompile(
      source.data(), source.size(), "native_shader.hlsl", nullptr, &include_handler,
      entry, profile, flags, 0, &out_blob, &errors);

  if (FAILED(hr)) {
    if (errors) {
      out_err = static_cast<const char*>(errors->GetBufferPointer());
    }
    // Reintentar sin optimizaciones si el compilador falló
    hr = D3DCompile(
        source.data(), source.size(), "native_shader.hlsl", nullptr, &include_handler,
        entry, profile, D3DCOMPILE_SKIP_OPTIMIZATION | D3DCOMPILE_IEEE_STRICTNESS, 0, &out_blob, nullptr);
  }

  return SUCCEEDED(hr);
}

ID3D11VertexShader* D3D11ShaderCache::GetOrCreateVertexShader(uint64_t hash, const std::string& hlsl_source) {
  if (!device_ || hash == 0) return nullptr;

  auto it = vs_cache_.find(hash);
  if (it != vs_cache_.end()) return it->second.Get();

  // 1. Comprobar si ya existe el bytecode compilado (.cso) en disco para carga instantanea
  std::vector<uint8_t> cso = LoadShaderBytecodeFromDisk(hash, "vs");
  if (!cso.empty()) {
    ComPtr<ID3D11VertexShader> vs;
    if (SUCCEEDED(device_->CreateVertexShader(cso.data(), cso.size(), nullptr, &vs))) {
      vs_cache_[hash] = vs;
      return vs.Get();
    }
  }

  // 2. Si no hay .cso, cargar fuente HLSL
  std::string src = hlsl_source;
  if (src.empty()) {
    src = LoadShaderSourceFromDisk(hash, "vs");
  }
  if (src.empty()) {
    vs_cache_[hash] = nullptr;   // Cache negativo: no reintentar busquedas en disco
    return nullptr;
  }

  // 3. Compilar codigo fuente HLSL
  ComPtr<ID3DBlob> blob;
  std::string err;
  if (CompileShader(src, "main", "vs_5_0", blob, err)) {
    ComPtr<ID3D11VertexShader> vs;
    HRESULT hr = device_->CreateVertexShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &vs);
    if (SUCCEEDED(hr)) {
      vs_cache_[hash] = vs;
      SaveShaderBytecodeToDisk(hash, "vs", blob->GetBufferPointer(), blob->GetBufferSize());
      return vs.Get();
    }
  }

  vs_cache_[hash] = nullptr;   // Cache negativo ante fallo de compilacion
  return nullptr;
}

ID3D11PixelShader* D3D11ShaderCache::GetOrCreatePixelShader(uint64_t hash, const std::string& hlsl_source) {
  if (!device_ || hash == 0) return nullptr;

  auto it = ps_cache_.find(hash);
  if (it != ps_cache_.end()) return it->second.Get();

  // 1. Comprobar si ya existe el bytecode compilado (.cso) en disco para carga instantanea
  std::vector<uint8_t> cso = LoadShaderBytecodeFromDisk(hash, "ps");
  if (!cso.empty()) {
    ComPtr<ID3D11PixelShader> ps;
    if (SUCCEEDED(device_->CreatePixelShader(cso.data(), cso.size(), nullptr, &ps))) {
      ps_cache_[hash] = ps;
      return ps.Get();
    }
  }

  // 2. Si no hay .cso, cargar fuente HLSL
  std::string src = hlsl_source;
  if (src.empty()) {
    src = LoadShaderSourceFromDisk(hash, "ps");
  }
  if (src.empty()) {
    ps_cache_[hash] = nullptr;   // Cache negativo: no reintentar busquedas en disco
    return nullptr;
  }

  // 3. Compilar codigo fuente HLSL
  ComPtr<ID3DBlob> blob;
  std::string err;
  if (CompileShader(src, "main", "ps_5_0", blob, err)) {
    ComPtr<ID3D11PixelShader> ps;
    HRESULT hr = device_->CreatePixelShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &ps);
    if (SUCCEEDED(hr)) {
      ps_cache_[hash] = ps;
      SaveShaderBytecodeToDisk(hash, "ps", blob->GetBufferPointer(), blob->GetBufferSize());
      return ps.Get();
    }
  }

  ps_cache_[hash] = nullptr;   // Cache negativo ante fallo de compilacion
  return nullptr;
}

void D3D11ShaderCache::Shutdown() {
  vs_cache_.clear();
  ps_cache_.clear();
  rect_gs_.Reset();
  pass_vs_.Reset();
  copy_ps_.Reset();
  device_ = nullptr;
}

}  // namespace re_wor::nativegfx
#endif
