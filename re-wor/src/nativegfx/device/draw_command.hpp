// nativegfx/device/draw_command.hpp
// Comando de dibujo desacoplado listo para ser ejecutado por un backend nativo.
#pragma once

#include <cstdint>
#include <vector>
#include "nativegfx/common/types.hpp"
#include "nativegfx/state/pipeline_state.hpp"

namespace re_wor::nativegfx {

struct DrawCommand {
  uint32_t source_function        = 0;   // Direccion de la llamada D3D original (ej. 0x822D18A0)
  PrimitiveTopology topology      = PrimitiveTopology::TriangleList;
  bool is_indexed                 = false;

  uint32_t primitive_count        = 0;
  uint32_t vertex_count           = 0;
  uint32_t index_count            = 0;
  uint32_t base_vertex            = 0;

  uint32_t index_buffer_address   = 0;   // Direccion fisica en la memoria de la consola
  IndexFormat index_format        = IndexFormat::Index16;

  // Si el draw usa indices inmediatos (DRAW_INDX_2, rectangulos, clear)
  std::vector<uint32_t> immediate_indices;

  // Snapshot completo del estado del pipeline para este draw
  PipelineState state;
};

}  // namespace re_wor::nativegfx
