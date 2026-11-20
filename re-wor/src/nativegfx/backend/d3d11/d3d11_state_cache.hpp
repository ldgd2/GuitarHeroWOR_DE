// nativegfx/backend/d3d11/d3d11_state_cache.hpp
// Cache y creador de estados de DirectX 11 (Blend, DepthStencil, Rasterizer, Sampler).
#pragma once

#ifdef _WIN32
#include <d3d11.h>
#include <wrl/client.h>

#include <unordered_map>
#include "nativegfx/state/blend_state.hpp"
#include "nativegfx/state/depth_stencil_state.hpp"
#include "nativegfx/state/rasterizer_state.hpp"
#include "nativegfx/state/texture_desc.hpp"

namespace re_wor::nativegfx {

struct D3D11BlendKey {
  uint64_t b0 = 0;
  uint64_t b1 = 0;
  bool operator==(const D3D11BlendKey& o) const noexcept { return b0 == o.b0 && b1 == o.b1; }
};

struct D3D11BlendKeyHash {
  size_t operator()(const D3D11BlendKey& k) const noexcept {
    return static_cast<size_t>(k.b0 ^ (k.b1 * 1099511628211ull));
  }
};

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
  uint32_t anisotropy_ = 16;

  std::unordered_map<D3D11BlendKey, Microsoft::WRL::ComPtr<ID3D11BlendState>, D3D11BlendKeyHash> blend_cache_;
  std::unordered_map<uint64_t, Microsoft::WRL::ComPtr<ID3D11DepthStencilState>> depth_cache_;
  std::unordered_map<uint64_t, Microsoft::WRL::ComPtr<ID3D11RasterizerState>> raster_cache_;
  std::unordered_map<uint32_t, Microsoft::WRL::ComPtr<ID3D11SamplerState>> sampler_cache_;

  D3D11BlendKey last_blend_key_ = {};
  ID3D11BlendState* last_blend_state_ = nullptr;

  uint64_t last_depth_key_ = 0;
  ID3D11DepthStencilState* last_depth_state_ = nullptr;

  uint64_t last_raster_key_ = 0;
  ID3D11RasterizerState* last_raster_state_ = nullptr;

  uint32_t last_sampler_key_ = 0;
  ID3D11SamplerState* last_sampler_state_ = nullptr;
};

}  // namespace re_wor::nativegfx
#endif
