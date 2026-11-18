// nativegfx/state/rasterizer_state.hpp
// Estado tipado del rasterizador y viewport extraido de PA_SU_SC_MODE_CNTL, PA_SC_WINDOW_OFFSET, etc.
#pragma once

#include <cstdint>
#include "nativegfx/common/types.hpp"

namespace re_wor::nativegfx {

// Estado completo del rasterizador
struct RasterizerState {
  FillMode fill_mode              = FillMode::Solid;
  CullMode cull_mode              = CullMode::Back;
  bool front_counter_clockwise    = false;   // true si CW o CCW segun sm.face
  float depth_bias                = 0.0f;
  float slope_scaled_depth_bias   = 0.0f;

  bool scissor_enable             = true;
  Rect2D scissor_rect             = {0, 0, 1280, 720};

  Viewport viewport               = {0.0f, 0.0f, 1280.0f, 720.0f, 0.0f, 1.0f};
  Vec2 window_offset              = {0.0f, 0.0f};
  bool half_pixel_offset          = true;    // 0.5f offset si PixelCenter::kD3DZero
  bool vtx_window_offset_enable   = true;
};

// Parametros del sistema para transformacion NDC hacia la API nativa
struct SystemNdcConstants {
  float ndc_scale[4]  = {1.0f, 1.0f, 1.0f, 1.0f};
  float ndc_offset[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  uint32_t flags      = 0;   // 1=w0_fmt, 2=xy_fmt, 4=z_fmt, 8=polygonal
  uint32_t alpha_func = 7;   // Always si no esta habilitado alpha test
  float alpha_ref     = 0.0f;
  uint32_t param_gen  = 0xFFFF;
  float res_scale[2]  = {1.0f, 1.0f};   // Escala de resolucion (1.0 = nativo 720p)
};

}  // namespace re_wor::nativegfx
