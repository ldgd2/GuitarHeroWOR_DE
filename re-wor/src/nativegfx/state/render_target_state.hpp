// nativegfx/state/render_target_state.hpp
// Estado tipado de Render Targets y Depth Buffer extraido de los registros RB_SURFACE_INFO y RB_COLOR_INFO.
#pragma once

#include <cstdint>
#include "nativegfx/common/types.hpp"

namespace re_wor::nativegfx {

// Descripcion de un Render Target de Color
struct ColorTargetDesc {
  uint32_t base_address = 0;   // Direccion base o EDRAM tile
  uint32_t format       = 0;   // Formato nativo de la 360 (ColorFormat)
  uint8_t write_mask    = 0;   // Mascara de escritura RGBA (0x1 | 0x2 | 0x4 | 0x8)
  bool enabled          = false;
};

// Descripcion del Buffer de Profundidad / Plantilla
struct DepthTargetDesc {
  uint32_t base_address = 0;   // Direccion base o EDRAM tile de profundidad
  uint32_t format       = 0;   // Formato nativo (DepthFormat: D24S8, D24FS8, etc.)
  bool enabled          = false;
};

// Estado completo de superficies de destino
struct RenderTargetState {
  uint32_t surface_pitch  = 1280;   // Ancho del pitch en pixeles (RB_SURFACE_INFO)
  uint32_t surface_height = 720;    // Alto inferido o fijado
  uint32_t msaa_samples   = 1;      // 1x, 2x, 4x (RB_SURFACE_INFO)

  ColorTargetDesc color_targets[4];
  DepthTargetDesc depth_target;

  bool HasActiveTargets() const {
    if (depth_target.enabled) return true;
    for (int i = 0; i < 4; ++i) {
      if (color_targets[i].enabled) return true;
    }
    return false;
  }
};

}  // namespace re_wor::nativegfx
