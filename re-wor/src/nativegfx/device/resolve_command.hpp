// nativegfx/device/resolve_command.hpp
// Comando de resolve (copia de Render Target a Textura) desacoplado.
#pragma once

#include <cstdint>
#include "nativegfx/common/types.hpp"

namespace re_wor::nativegfx {

struct ResolveCommand {
  uint32_t source_function        = 0;   // Tipicamente sub_822E4538
  uint32_t source_surface_base    = 0;   // Base en EDRAM / Render Target origen
  uint32_t dest_texture_address   = 0;   // Direccion destino en memoria fisica
  uint32_t dest_pitch             = 0;   // Pitch de destino
  uint32_t format                 = 0;   // Formato de textura destino

  uint32_t width                  = 1280;
  uint32_t height                 = 720;

  bool is_depth                   = false;
  bool apply_gamma                = false;
  bool apply_degamma              = false;
  bool swap_red_blue              = false;
};

}  // namespace re_wor::nativegfx
