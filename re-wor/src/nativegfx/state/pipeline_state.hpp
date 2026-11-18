// nativegfx/state/pipeline_state.hpp
// Instantanea completa del estado del pipeline grafico en el momento de un Draw o Resolve.
#pragma once

#include <cstdint>
#include "nativegfx/state/blend_state.hpp"
#include "nativegfx/state/depth_stencil_state.hpp"
#include "nativegfx/state/rasterizer_state.hpp"
#include "nativegfx/state/render_target_state.hpp"
#include "nativegfx/state/shader_constants.hpp"
#include "nativegfx/state/texture_desc.hpp"

namespace re_wor::nativegfx {

// Identificador y metadatos de un shader activo
struct BoundShaderInfo {
  uint32_t guest_address = 0;   // Direccion fisica del microcodigo
  uint32_t dword_count   = 0;   // Cantidad de palabras uint32 del shader
  uint64_t hash          = 0;   // Hash XXH64 / FNV para busqueda en cache offline
  bool is_valid          = false;
};

// Snapshot desacoplado del estado del pipeline
struct PipelineState {
  RenderTargetState render_targets;
  BlendState blend;
  DepthStencilState depth_stencil;
  RasterizerState rasterizer;
  ShaderConstants constants;

  BoundShaderInfo vertex_shader;
  BoundShaderInfo pixel_shader;

  TextureDesc bound_textures[32];   // 32 constantes de fetch decodificadas

  // Verifica si el estado actual es ejecutable en un backend nativo
  bool IsValidForDraw() const {
    return vertex_shader.is_valid && render_targets.HasActiveTargets();
  }
};

}  // namespace re_wor::nativegfx
