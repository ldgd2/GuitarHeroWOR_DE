// nativegfx/state/blend_state.hpp
// Estado tipado de mezcla de color (Blending) extraido de RB_BLENDCONTROL y RB_COLORCONTROL.
#pragma once

#include <cstdint>
#include "nativegfx/common/types.hpp"

namespace re_wor::nativegfx {

// Configuracion de mezcla para un Render Target individual
struct RenderTargetBlendDesc {
  bool blend_enable            = false;
  BlendFactor src_blend        = BlendFactor::One;
  BlendFactor dest_blend       = BlendFactor::Zero;
  BlendOp blend_op             = BlendOp::Add;
  BlendFactor src_blend_alpha  = BlendFactor::One;
  BlendFactor dest_blend_alpha = BlendFactor::Zero;
  BlendOp blend_op_alpha       = BlendOp::Add;
  uint8_t write_mask           = ColorWriteMask::All;
};

// Estado global de mezcla
struct BlendState {
  bool independent_blend_enable = true;
  RenderTargetBlendDesc targets[4];
  Vec4 blend_constant = {1.0f, 1.0f, 1.0f, 1.0f};   // RB_BLEND_RED..ALPHA
};

// Conversiones de factores y operaciones de mezcla de Xbox 360 a enums nativos
inline BlendFactor DecodeBlendFactor(uint32_t val, bool alpha) {
  // Mapeo segun constantes de GPU Xenos
  switch (val) {
    case 0: return BlendFactor::Zero;
    case 1: return BlendFactor::One;
    case 4: return BlendFactor::SrcColor;
    case 5: return BlendFactor::InvSrcColor;
    case 6: return BlendFactor::SrcAlpha;
    case 7: return BlendFactor::InvSrcAlpha;
    case 8: return BlendFactor::DestAlpha;
    case 9: return BlendFactor::InvDestAlpha;
    case 10: return BlendFactor::DestColor;
    case 11: return BlendFactor::InvDestColor;
    case 12: return BlendFactor::SrcAlphaSat;
    case 14: return BlendFactor::BlendFactor;
    case 15: return BlendFactor::InvBlendFactor;
    default: return BlendFactor::One;
  }
}

inline BlendOp DecodeBlendOp(uint32_t val) {
  switch (val) {
    case 0: return BlendOp::Add;
    case 1: return BlendOp::Subtract;
    case 2: return BlendOp::Min;
    case 3: return BlendOp::Max;
    case 4: return BlendOp::RevSubtract;
    default: return BlendOp::Add;
  }
}

}  // namespace re_wor::nativegfx
