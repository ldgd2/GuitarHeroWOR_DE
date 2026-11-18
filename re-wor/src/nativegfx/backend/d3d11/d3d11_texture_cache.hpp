// nativegfx/backend/d3d11/d3d11_texture_cache.hpp
// Cache y creador de texturas y SRVs en Direct3D 11 a partir de descriptores Xenos.
#pragma once

#ifdef _WIN32
#include <d3d11.h>
#include <wrl/client.h>

#include <cstdint>
#include <map>
#include "nativegfx/state/texture_desc.hpp"

namespace re_wor::nativegfx {

struct TextureResource {
  Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
  Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv;
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t format = 0;
};

class D3D11TextureCache {
 public:
  D3D11TextureCache() = default;
  ~D3D11TextureCache() = default;

  bool Initialize(ID3D11Device* device, ID3D11DeviceContext* context);
  void Shutdown();

  ID3D11ShaderResourceView* GetOrCreateTexture(const TextureDesc& desc, const uint8_t* guest_memory);
  ID3D11ShaderResourceView* RegisterResolveTarget(uint32_t address, ID3D11Texture2D* texture, ID3D11ShaderResourceView* srv);

  ID3D11ShaderResourceView* GetDummy2D() const { return dummy2d_.Get(); }
  ID3D11ShaderResourceView* GetDummy3D() const { return dummy3d_.Get(); }
  ID3D11ShaderResourceView* GetDummyCube() const { return dummycube_.Get(); }
  ID3D11ShaderResourceView* GetDummySRV(TextureDimension dim) const;

 private:
  ID3D11Device* device_ = nullptr;
  ID3D11DeviceContext* context_ = nullptr;

  Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> dummy2d_;
  Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> dummy3d_;
  Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> dummycube_;

  std::map<uint64_t, TextureResource> memory_texture_cache_;
  std::map<uint32_t, TextureResource> resolve_texture_cache_;
};

}  // namespace re_wor::nativegfx
#endif
