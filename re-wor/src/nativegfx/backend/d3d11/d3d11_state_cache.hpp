// nativegfx/backend/d3d11/d3d11_state_cache.hpp
// Cache y creador de estados de DirectX 11 (Blend, DepthStencil, Rasterizer, Sampler).
#pragma once

#ifdef _WIN32
#include <d3d11.h>
#include <wrl/client.h>

#include <map>
#include <string>
#include "nativegfx/state/blend_state.hpp"
#include "nativegfx/state/depth_stencil_state.hpp"
#include "nativegfx/state/rasterizer_state.hpp"
#include "nativegfx/state/texture_desc.hpp"

namespace re_wor::nativegfx {

class D3D11StateCache {
 public:
  D3D11StateCache() = default;
  ~D3D11StateCache() = default;

  void Initialize(ID3D11Device* device);
  void Clear();

  ID3D11BlendState* GetOrCreateBlendState(const BlendState& blend);
  ID3D11DepthStencilState* GetOrCreateDepthStencilState(const DepthStencilState& depth);
  ID3D11RasterizerState* GetOrCreateRasterizerState(const RasterizerState& rasterizer);
  ID3D11SamplerState* GetOrCreateSamplerState(const TextureDesc& tex_desc);

  void SetAnisotropy(uint32_t level);
  uint32_t GetAnisotropy() const { return anisotropy_; }

 private:
  ID3D11Device* device_ = nullptr;
  uint32_t anisotropy_ = 1;

  std::map<std::string, Microsoft::WRL::ComPtr<ID3D11BlendState>> blend_cache_;
  std::map<std::string, Microsoft::WRL::ComPtr<ID3D11DepthStencilState>> depth_cache_;
  std::map<std::string, Microsoft::WRL::ComPtr<ID3D11RasterizerState>> raster_cache_;
  std::map<std::string, Microsoft::WRL::ComPtr<ID3D11SamplerState>> sampler_cache_;
};

}  // namespace re_wor::nativegfx
#endif
