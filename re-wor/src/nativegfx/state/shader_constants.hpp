// nativegfx/state/shader_constants.hpp
// Buffers de constantes de shaders extraidos de la estructura del dispositivo.
// Mapea exactamente a las ranuras b0, b1, b2, b3 de los shaders de xenos2hlsl.
#pragma once

#include <cstdint>
#include <cstring>
#include "nativegfx/common/types.hpp"
#include "nativegfx/state/rasterizer_state.hpp"

namespace re_wor::nativegfx {

struct ShaderConstants {
  // b0: Constantes float de Vertex Shader (256 x float4 = 1024 floats)
  alignas(16) float vs_float[256][4] = {};

  // b0: Constantes float de Pixel Shader (256 x float4 = 1024 floats)
  alignas(16) float ps_float[256][4] = {};

  // b1: Bools (8 palabras uint32 = 256 bits) y Loops (32 palabras uint32)
  alignas(16) uint32_t bool_bits[8]   = {};
  alignas(16) uint32_t loop_consts[32] = {};

  // b2: Constantes de Fetch (192 palabras uint32 = 32 texturas x 6 palabras)
  alignas(16) uint32_t fetch_consts[192] = {};

  // b3: Constantes del sistema (NDC, alpha test, resolucion)
  alignas(16) SystemNdcConstants system = {};

  // Versionado monótono por bloque para omisión ultra-rápida de hash y memcpy (cero overhead en draw)
  uint32_t vs_version = 0;
  uint32_t ps_version = 0;
  uint32_t bool_version = 0;
  uint32_t fetch_version = 0;
  uint32_t system_version = 0;

  // Flags de sucio para subida selectiva
  bool dirty_vs_float = true;
  bool dirty_ps_float = true;
  bool dirty_bool_loop = true;
  bool dirty_fetch = true;
  bool dirty_system = true;

  void ClearDirty() {
    dirty_vs_float = false;
    dirty_ps_float = false;
    dirty_bool_loop = false;
    dirty_fetch = false;
    dirty_system = false;
  }
};

}  // namespace re_wor::nativegfx
