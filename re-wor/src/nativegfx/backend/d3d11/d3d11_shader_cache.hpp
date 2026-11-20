// nativegfx/backend/d3d11/d3d11_shader_cache.hpp
// Cache y gestor de shaders nativos D3D11 traducidos por xenos2hlsl y utilidades de render.
#pragma once

#ifdef _WIN32
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <map>
#include <string>

namespace re_wor::nativegfx {

class D3D11ShaderCache {
 public:
  D3D11ShaderCache() = default;
  ~D3D11ShaderCache() = default;

  bool Initialize(ID3D11Device* device);
  void Shutdown();

  ID3D11VertexShader* GetOrCreateVertexShader(uint64_t hash, const std::string& hlsl_source);
  ID3D11PixelShader* GetOrCreatePixelShader(uint64_t hash, const std::string& hlsl_source);

  ID3D11GeometryShader* GetRectangleGeometryShader() const { return rect_gs_.Get(); }
  ID3D11VertexShader* GetPassVertexShader() const { return pass_vs_.Get(); }
  ID3D11PixelShader* GetCopyPixelShader() const { return copy_ps_.Get(); }

 private:
  bool CompileShader(const std::string& source, const char* entry, const char* profile,
                     Microsoft::WRL::ComPtr<ID3DBlob>& out_blob, std::string& out_err);

  ID3D11Device* device_ = nullptr;

  Microsoft::WRL::ComPtr<ID3D11GeometryShader> rect_gs_;
  Microsoft::WRL::ComPtr<ID3D11VertexShader> pass_vs_;
  Microsoft::WRL::ComPtr<ID3D11PixelShader> copy_ps_;

  std::map<uint64_t, Microsoft::WRL::ComPtr<ID3D11VertexShader>> vs_cache_;
  std::map<uint64_t, Microsoft::WRL::ComPtr<ID3D11PixelShader>> ps_cache_;

  uint64_t last_vs_hash_ = 0;
  ID3D11VertexShader* last_vs_ = nullptr;

  uint64_t last_ps_hash_ = 0;
  ID3D11PixelShader* last_ps_ = nullptr;
};

}  // namespace re_wor::nativegfx
#endif
