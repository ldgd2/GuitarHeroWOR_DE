// nativegfx/backend/d3d11/d3d11_state_cache.cpp
// Implementacion del cache de estados de pipeline de D3D11.
#ifdef _WIN32
#include "nativegfx/backend/d3d11/d3d11_state_cache.hpp"

namespace re_wor::nativegfx {

namespace {

D3D11_BLEND ToD3DBlend(BlendFactor factor) {
  switch (factor) {
    case BlendFactor::Zero:           return D3D11_BLEND_ZERO;
    case BlendFactor::One:            return D3D11_BLEND_ONE;
    case BlendFactor::SrcColor:       return D3D11_BLEND_SRC_COLOR;
    case BlendFactor::InvSrcColor:    return D3D11_BLEND_INV_SRC_COLOR;
    case BlendFactor::SrcAlpha:       return D3D11_BLEND_SRC_ALPHA;
    case BlendFactor::InvSrcAlpha:    return D3D11_BLEND_INV_SRC_ALPHA;
    case BlendFactor::DestAlpha:      return D3D11_BLEND_DEST_ALPHA;
    case BlendFactor::InvDestAlpha:   return D3D11_BLEND_INV_DEST_ALPHA;
    case BlendFactor::DestColor:      return D3D11_BLEND_DEST_COLOR;
    case BlendFactor::InvDestColor:   return D3D11_BLEND_INV_DEST_COLOR;
    case BlendFactor::SrcAlphaSat:    return D3D11_BLEND_SRC_ALPHA_SAT;
    case BlendFactor::BlendFactor:    return D3D11_BLEND_BLEND_FACTOR;
    case BlendFactor::InvBlendFactor: return D3D11_BLEND_INV_BLEND_FACTOR;
    default:                          return D3D11_BLEND_ONE;
  }
}

D3D11_BLEND_OP ToD3DBlendOp(BlendOp op) {
  switch (op) {
    case BlendOp::Add:         return D3D11_BLEND_OP_ADD;
    case BlendOp::Subtract:    return D3D11_BLEND_OP_SUBTRACT;
    case BlendOp::RevSubtract: return D3D11_BLEND_OP_REV_SUBTRACT;
    case BlendOp::Min:         return D3D11_BLEND_OP_MIN;
    case BlendOp::Max:         return D3D11_BLEND_OP_MAX;
    default:                   return D3D11_BLEND_OP_ADD;
  }
}

D3D11_COMPARISON_FUNC ToD3DComparison(ComparisonFunc func) {
  switch (func) {
    case ComparisonFunc::Never:        return D3D11_COMPARISON_NEVER;
    case ComparisonFunc::Less:         return D3D11_COMPARISON_LESS;
    case ComparisonFunc::Equal:        return D3D11_COMPARISON_EQUAL;
    case ComparisonFunc::LessEqual:    return D3D11_COMPARISON_LESS_EQUAL;
    case ComparisonFunc::Greater:      return D3D11_COMPARISON_GREATER;
    case ComparisonFunc::NotEqual:     return D3D11_COMPARISON_NOT_EQUAL;
    case ComparisonFunc::GreaterEqual: return D3D11_COMPARISON_GREATER_EQUAL;
    case ComparisonFunc::Always:       return D3D11_COMPARISON_ALWAYS;
    default:                           return D3D11_COMPARISON_ALWAYS;
  }
}

D3D11_STENCIL_OP ToD3DStencilOp(StencilOp op) {
  switch (op) {
    case StencilOp::Keep:    return D3D11_STENCIL_OP_KEEP;
    case StencilOp::Zero:    return D3D11_STENCIL_OP_ZERO;
    case StencilOp::Replace: return D3D11_STENCIL_OP_REPLACE;
    case StencilOp::IncrSat: return D3D11_STENCIL_OP_INCR_SAT;
    case StencilOp::DecrSat: return D3D11_STENCIL_OP_DECR_SAT;
    case StencilOp::Invert:  return D3D11_STENCIL_OP_INVERT;
    case StencilOp::Incr:    return D3D11_STENCIL_OP_INCR;
    case StencilOp::Decr:    return D3D11_STENCIL_OP_DECR;
    default:                 return D3D11_STENCIL_OP_KEEP;
  }
}

D3D11_TEXTURE_ADDRESS_MODE ToD3DAddressMode(TextureAddressMode mode) {
  switch (mode) {
    case TextureAddressMode::Repeat:             return D3D11_TEXTURE_ADDRESS_WRAP;
    case TextureAddressMode::MirroredRepeat:     return D3D11_TEXTURE_ADDRESS_MIRROR;
    case TextureAddressMode::ClampToEdge:        return D3D11_TEXTURE_ADDRESS_CLAMP;
    case TextureAddressMode::MirrorClampToEdge:  return D3D11_TEXTURE_ADDRESS_MIRROR_ONCE;
    case TextureAddressMode::ClampToBorder:      return D3D11_TEXTURE_ADDRESS_BORDER;
    case TextureAddressMode::MirrorClampToBorder:return D3D11_TEXTURE_ADDRESS_BORDER;
    default:                                     return D3D11_TEXTURE_ADDRESS_WRAP;
  }
}

}  // namespace

void D3D11StateCache::Initialize(ID3D11Device* device) {
  device_ = device;
  Clear();
}

void D3D11StateCache::Clear() {
  blend_cache_.clear();
  depth_cache_.clear();
  raster_cache_.clear();
  sampler_cache_.clear();
  last_blend_key_ = {};
  last_blend_state_ = nullptr;
  last_depth_key_ = 0;
  last_depth_state_ = nullptr;
  last_raster_key_ = 0;
  last_raster_state_ = nullptr;
  last_sampler_key_ = 0;
  last_sampler_state_ = nullptr;
}

ID3D11BlendState* D3D11StateCache::GetOrCreateBlendState(const BlendState& blend) {
  if (!device_) return nullptr;

  D3D11BlendKey key = {};
  uint64_t b0 = (blend.independent_blend_enable ? 1ULL : 0ULL) |
                (blend.targets[0].blend_enable ? 2ULL : 0ULL) |
                ((static_cast<uint64_t>(blend.targets[0].src_blend) & 0x1FULL) << 2) |
                ((static_cast<uint64_t>(blend.targets[0].dest_blend) & 0x1FULL) << 7) |
                ((static_cast<uint64_t>(blend.targets[0].blend_op) & 0x7ULL) << 12) |
                ((static_cast<uint64_t>(blend.targets[0].src_blend_alpha) & 0x1FULL) << 15) |
                ((static_cast<uint64_t>(blend.targets[0].dest_blend_alpha) & 0x1FULL) << 20) |
                ((static_cast<uint64_t>(blend.targets[0].blend_op_alpha) & 0x7ULL) << 25) |
                ((static_cast<uint64_t>(blend.targets[0].write_mask) & 0xFULL) << 28);
  key.b0 = b0;

  if (blend.independent_blend_enable) {
    uint64_t b1 = 0;
    for (int i = 1; i < 4; ++i) {
      uint64_t sub = (blend.targets[i].blend_enable ? 1ULL : 0ULL) |
                     ((static_cast<uint64_t>(blend.targets[i].src_blend) & 0x1FULL) << 1) |
                     ((static_cast<uint64_t>(blend.targets[i].dest_blend) & 0x1FULL) << 6) |
                     ((static_cast<uint64_t>(blend.targets[i].blend_op) & 0x7ULL) << 11) |
                     ((static_cast<uint64_t>(blend.targets[i].src_blend_alpha) & 0x1FULL) << 14) |
                     ((static_cast<uint64_t>(blend.targets[i].dest_blend_alpha) & 0x1FULL) << 19) |
                     ((static_cast<uint64_t>(blend.targets[i].blend_op_alpha) & 0x7ULL) << 24) |
                     ((static_cast<uint64_t>(blend.targets[i].write_mask) & 0xFULL) << 27);
      b1 |= (sub << ((i - 1) * 21));
    }
    key.b1 = b1;
  }

  if (last_blend_state_ && key == last_blend_key_) {
    return last_blend_state_;
  }

  auto it = blend_cache_.find(key);
  if (it != blend_cache_.end()) {
    last_blend_key_ = key;
    last_blend_state_ = it->second.Get();
    return last_blend_state_;
  }

  D3D11_BLEND_DESC desc = {};
  desc.AlphaToCoverageEnable = FALSE;
  desc.IndependentBlendEnable = blend.independent_blend_enable ? TRUE : FALSE;

  for (int i = 0; i < 4; ++i) {
    const auto& src = blend.targets[i];
    auto& dst = desc.RenderTarget[i];
    dst.BlendEnable           = src.blend_enable ? TRUE : FALSE;
    dst.SrcBlend              = ToD3DBlend(src.src_blend);
    dst.DestBlend             = ToD3DBlend(src.dest_blend);
    dst.BlendOp               = ToD3DBlendOp(src.blend_op);
    dst.SrcBlendAlpha         = ToD3DBlend(src.src_blend_alpha);
    dst.DestBlendAlpha        = ToD3DBlend(src.dest_blend_alpha);
    dst.BlendOpAlpha          = ToD3DBlendOp(src.blend_op_alpha);
    dst.RenderTargetWriteMask = src.write_mask;
  }

  Microsoft::WRL::ComPtr<ID3D11BlendState> state;
  if (SUCCEEDED(device_->CreateBlendState(&desc, &state))) {
    last_blend_key_ = key;
    last_blend_state_ = state.Get();
    blend_cache_[key] = state;
    return last_blend_state_;
  }
  return nullptr;
}

ID3D11DepthStencilState* D3D11StateCache::GetOrCreateDepthStencilState(const DepthStencilState& depth) {
  if (!device_) return nullptr;

  uint64_t key = (depth.depth_enable ? 1ULL : 0ULL) |
                 (depth.depth_write_enable ? 2ULL : 0ULL) |
                 ((static_cast<uint64_t>(depth.depth_func) & 0x7ULL) << 2) |
                 ((depth.stencil_enable ? 1ULL : 0ULL) << 5) |
                 ((static_cast<uint64_t>(depth.stencil_read_mask) & 0xFFULL) << 6) |
                 ((static_cast<uint64_t>(depth.stencil_write_mask) & 0xFFULL) << 14) |
                 ((depth.backface_enable ? 1ULL : 0ULL) << 22) |
                 ((static_cast<uint64_t>(depth.front_face.fail_op) & 0xFULL) << 23) |
                 ((static_cast<uint64_t>(depth.front_face.depth_fail_op) & 0xFULL) << 27) |
                 ((static_cast<uint64_t>(depth.front_face.pass_op) & 0xFULL) << 31) |
                 ((static_cast<uint64_t>(depth.front_face.compare_func) & 0x7ULL) << 35) |
                 ((static_cast<uint64_t>(depth.back_face.fail_op) & 0xFULL) << 38) |
                 ((static_cast<uint64_t>(depth.back_face.depth_fail_op) & 0xFULL) << 42) |
                 ((static_cast<uint64_t>(depth.back_face.pass_op) & 0xFULL) << 46) |
                 ((static_cast<uint64_t>(depth.back_face.compare_func) & 0x7ULL) << 50);

  if (last_depth_state_ && key == last_depth_key_) {
    return last_depth_state_;
  }

  auto it = depth_cache_.find(key);
  if (it != depth_cache_.end()) {
    last_depth_key_ = key;
    last_depth_state_ = it->second.Get();
    return last_depth_state_;
  }

  D3D11_DEPTH_STENCIL_DESC desc = {};
  desc.DepthEnable    = depth.depth_enable ? TRUE : FALSE;
  desc.DepthWriteMask = depth.depth_write_enable ? D3D11_DEPTH_WRITE_MASK_ALL : D3D11_DEPTH_WRITE_MASK_ZERO;
  desc.DepthFunc      = ToD3DComparison(depth.depth_func);

  desc.StencilEnable    = depth.stencil_enable ? TRUE : FALSE;
  desc.StencilReadMask  = depth.stencil_read_mask;
  desc.StencilWriteMask = depth.stencil_write_mask;

  desc.FrontFace.StencilFailOp      = ToD3DStencilOp(depth.front_face.fail_op);
  desc.FrontFace.StencilDepthFailOp = ToD3DStencilOp(depth.front_face.depth_fail_op);
  desc.FrontFace.StencilPassOp      = ToD3DStencilOp(depth.front_face.pass_op);
  desc.FrontFace.StencilFunc        = ToD3DComparison(depth.front_face.compare_func);

  if (depth.backface_enable) {
    desc.BackFace.StencilFailOp      = ToD3DStencilOp(depth.back_face.fail_op);
    desc.BackFace.StencilDepthFailOp = ToD3DStencilOp(depth.back_face.depth_fail_op);
    desc.BackFace.StencilPassOp      = ToD3DStencilOp(depth.back_face.pass_op);
    desc.BackFace.StencilFunc        = ToD3DComparison(depth.back_face.compare_func);
  } else {
    desc.BackFace = desc.FrontFace;
  }

  Microsoft::WRL::ComPtr<ID3D11DepthStencilState> state;
  if (SUCCEEDED(device_->CreateDepthStencilState(&desc, &state))) {
    last_depth_key_ = key;
    last_depth_state_ = state.Get();
    depth_cache_[key] = state;
    return last_depth_state_;
  }
  return nullptr;
}

ID3D11RasterizerState* D3D11StateCache::GetOrCreateRasterizerState(const RasterizerState& rasterizer) {
  if (!device_) return nullptr;

  uint32_t slope_bits = 0;
  std::memcpy(&slope_bits, &rasterizer.slope_scaled_depth_bias, 4);
  uint64_t key = (static_cast<uint64_t>(rasterizer.fill_mode) & 0x1ULL) |
                 ((static_cast<uint64_t>(rasterizer.cull_mode) & 0x3ULL) << 1) |
                 ((rasterizer.front_counter_clockwise ? 1ULL : 0ULL) << 3) |
                 ((static_cast<uint64_t>(static_cast<uint32_t>(static_cast<int32_t>(rasterizer.depth_bias)))) << 4) ^
                 (static_cast<uint64_t>(slope_bits) << 32) |
                 (rasterizer.scissor_enable ? (1ULL << 31) : 0ULL);

  if (last_raster_state_ && key == last_raster_key_) {
    return last_raster_state_;
  }

  auto it = raster_cache_.find(key);
  if (it != raster_cache_.end()) {
    last_raster_key_ = key;
    last_raster_state_ = it->second.Get();
    return last_raster_state_;
  }

  D3D11_RASTERIZER_DESC desc = {};
  desc.FillMode              = (rasterizer.fill_mode == FillMode::Wireframe) ? D3D11_FILL_WIREFRAME : D3D11_FILL_SOLID;
  desc.CullMode              = (rasterizer.cull_mode == CullMode::Front) ? D3D11_CULL_FRONT
                             : (rasterizer.cull_mode == CullMode::Back) ? D3D11_CULL_BACK : D3D11_CULL_NONE;
  desc.FrontCounterClockwise = rasterizer.front_counter_clockwise ? TRUE : FALSE;
  desc.DepthBias             = int(rasterizer.depth_bias);
  desc.SlopeScaledDepthBias  = rasterizer.slope_scaled_depth_bias;
  desc.DepthBiasClamp        = 0.0f;
  desc.DepthClipEnable       = TRUE;
  desc.ScissorEnable         = rasterizer.scissor_enable ? TRUE : FALSE;
  desc.MultisampleEnable     = TRUE;
  desc.AntialiasedLineEnable = TRUE;

  Microsoft::WRL::ComPtr<ID3D11RasterizerState> state;
  if (SUCCEEDED(device_->CreateRasterizerState(&desc, &state))) {
    last_raster_key_ = key;
    last_raster_state_ = state.Get();
    raster_cache_[key] = state;
    return last_raster_state_;
  }
  return nullptr;
}

void D3D11StateCache::SetAnisotropy(uint32_t level) {
  uint32_t clamped = (level >= 1u && level <= 16u) ? level : 1u;
  if (anisotropy_ != clamped) {
    anisotropy_ = clamped;
    sampler_cache_.clear();
    last_sampler_key_ = 0;
    last_sampler_state_ = nullptr;
  }
}

ID3D11SamplerState* D3D11StateCache::GetOrCreateSamplerState(const TextureDesc& tex_desc) {
  if (!device_) return nullptr;

  uint32_t key = (anisotropy_ & 0x1Fu) |
                 ((static_cast<uint32_t>(tex_desc.address_u) & 0x7u) << 5) |
                 ((static_cast<uint32_t>(tex_desc.address_v) & 0x7u) << 8) |
                 ((static_cast<uint32_t>(tex_desc.address_w) & 0x7u) << 11);

  if (last_sampler_state_ && key == last_sampler_key_) {
    return last_sampler_state_;
  }

  auto it = sampler_cache_.find(key);
  if (it != sampler_cache_.end()) {
    last_sampler_key_ = key;
    last_sampler_state_ = it->second.Get();
    return last_sampler_state_;
  }

  D3D11_SAMPLER_DESC desc = {};
  if (anisotropy_ > 1) {
    desc.Filter = D3D11_FILTER_ANISOTROPIC;
    desc.MaxAnisotropy = anisotropy_;
  } else {
    desc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    desc.MaxAnisotropy = 1;
  }
  desc.AddressU       = ToD3DAddressMode(tex_desc.address_u);
  desc.AddressV       = ToD3DAddressMode(tex_desc.address_v);
  desc.AddressW       = ToD3DAddressMode(tex_desc.address_w);
  desc.MipLODBias     = 0.0f;
  desc.ComparisonFunc = D3D11_COMPARISON_ALWAYS;
  desc.MinLOD         = 0.0f;
  desc.MaxLOD         = D3D11_FLOAT32_MAX;

  Microsoft::WRL::ComPtr<ID3D11SamplerState> state;
  if (SUCCEEDED(device_->CreateSamplerState(&desc, &state))) {
    last_sampler_key_ = key;
    last_sampler_state_ = state.Get();
    sampler_cache_[key] = state;
    return last_sampler_state_;
  }
  return nullptr;
}

}  // namespace re_wor::nativegfx
#endif
