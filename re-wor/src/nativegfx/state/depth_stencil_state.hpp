// nativegfx/state/depth_stencil_state.hpp
// Estado tipado de profundidad y plantilla extraido de RB_DEPTHCONTROL y RB_STENCILREFMASK.
#pragma once

#include <cstdint>
#include "nativegfx/common/types.hpp"

namespace re_wor::nativegfx {

// Operaciones de stencil para una cara (Front o Back)
struct StencilFaceOp {
  StencilOp fail_op            = StencilOp::Keep;
  StencilOp depth_fail_op      = StencilOp::Keep;
  StencilOp pass_op            = StencilOp::Keep;
  ComparisonFunc compare_func  = ComparisonFunc::Always;
};

// Estado completo de Depth-Stencil
struct DepthStencilState {
  bool depth_enable            = true;
  bool depth_write_enable      = true;
  ComparisonFunc depth_func    = ComparisonFunc::LessEqual;

  bool stencil_enable          = false;
  uint8_t stencil_read_mask    = 0xFF;
  uint8_t stencil_write_mask   = 0xFF;
  uint8_t stencil_ref          = 0;

  bool backface_enable         = false;
  StencilFaceOp front_face;
  StencilFaceOp back_face;
};

inline ComparisonFunc DecodeComparisonFunc(uint32_t val) {
  // En Xenos: 0=Never, 1=Less, 2=Equal, 3=LEqual, 4=Greater, 5=NEqual, 6=GEqual, 7=Always
  switch (val & 7) {
    case 0: return ComparisonFunc::Never;
    case 1: return ComparisonFunc::Less;
    case 2: return ComparisonFunc::Equal;
    case 3: return ComparisonFunc::LessEqual;
    case 4: return ComparisonFunc::Greater;
    case 5: return ComparisonFunc::NotEqual;
    case 6: return ComparisonFunc::GreaterEqual;
    case 7: return ComparisonFunc::Always;
    default: return ComparisonFunc::Always;
  }
}

inline StencilOp DecodeStencilOp(uint32_t val) {
  // En Xenos: 0=Keep, 1=Zero, 2=Replace, 3=IncrSat, 4=DecrSat, 5=Invert, 6=Incr, 7=Decr
  switch (val & 7) {
    case 0: return StencilOp::Keep;
    case 1: return StencilOp::Zero;
    case 2: return StencilOp::Replace;
    case 3: return StencilOp::IncrSat;
    case 4: return StencilOp::DecrSat;
    case 5: return StencilOp::Invert;
    case 6: return StencilOp::Incr;
    case 7: return StencilOp::Decr;
    default: return StencilOp::Keep;
  }
}

}  // namespace re_wor::nativegfx
