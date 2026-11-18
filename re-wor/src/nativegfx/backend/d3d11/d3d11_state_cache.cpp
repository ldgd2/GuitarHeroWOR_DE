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
}

ID3D11BlendState* D3D11StateCache::GetOrCreateBlendState(const BlendState& blend) {
  if (!device_) return nullptr;

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

  std::string key(reinterpret_cast<const char*>(&desc), sizeof(desc));
  auto& state = blend_cache_[key];
  if (!state) {
    device_->CreateBlendState(&desc, &state);
  }
  return state.Get();
}

ID3D11DepthStencilState* D3D11StateCache::GetOrCreateDepthStencilState(const DepthStencilState& depth) {
  if (!device_) return nullptr;

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

  std::string key(reinterpret_cast<const char*>(&desc), sizeof(desc));
  auto& state = depth_cache_[key];
  if (!state) {
    device_->CreateDepthStencilState(&desc, &state);
  }
  return state.Get();
}

ID3D11RasterizerState* D3D11StateCache::GetOrCreateRasterizerState(const RasterizerState& rasterizer) {
  if (!device_) return nullptr;

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
  desc.MultisampleEnable     = FALSE;
  desc.AntialiasedLineEnable = FALSE;

  std::string key(reinterpret_cast<const char*>(&desc), sizeof(desc));
  auto& state = raster_cache_[key];
  if (!state) {
    device_->CreateRasterizerState(&desc, &state);
  }
  return state.Get();
}

void D3D11StateCache::SetAnisotropy(uint32_t level) {
  uint32_t clamped = (level >= 1u && level <= 16u) ? level : 1u;
  if (anisotropy_ != clamped) {
    anisotropy_ = clamped;
    sampler_cache_.clear();
  }
}

ID3D11SamplerState* D3D11StateCache::GetOrCreateSamplerState(const TextureDesc& tex_desc) {
  if (!device_) return nullptr;

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

  std::string key(reinterpret_cast<const char*>(&desc), sizeof(desc));
  auto& state = sampler_cache_[key];
  if (!state) {
    device_->CreateSamplerState(&desc, &state);
  }
  return state.Get();
}

}  // namespace re_wor::nativegfx
#endif
