// nativegfx/full/surface_manager.hpp
// Administrador de superficies y texturas resueltas sin EDRAM (Fase 4: Completo).
// En la Xbox 360, el renderizado se efectua en una memoria local de 10 MB (EDRAM)
// y luego se 'resuelve' (copia) a texturas en memoria principal.
// En la Fase 4 nativa, NO se emula la EDRAM: las superficies de dibujo se mantienen
// como Render Targets nativos en la GPU, y los resolves se tratan como alias
// o copias directas GPU-a-GPU sin sobrecarga de des-tiling ni CPU.
#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>
#include <string>

#include "nativegfx/common/types.hpp"
#include "nativegfx/device/resolve_command.hpp"
#include "nativegfx/state/render_target_state.hpp"

namespace re_wor::nativegfx {

enum class SurfaceFormat : uint32_t {
  k_8_8_8_8           = 0,
  k_8_8_8_8_GAMMA     = 1,
  k_2_10_10_10        = 2,
  k_16_16_FLOAT       = 3,
  k_16_16_16_16_FLOAT = 4,
  k_32_FLOAT          = 5,
  k_D24S8             = 6,
  k_D24FS8            = 7
};

struct NativeSurface {
  uint32_t id = 0;
  uint32_t guest_address = 0;        // Direccion de memoria huesped o base EDRAM
  uint32_t width = 0;
  uint32_t height = 0;
  SurfaceFormat format = SurfaceFormat::k_8_8_8_8;
  bool is_depth = false;
  bool is_swap_chain_target = false;  // Resolucion final para presentacion en pantalla
  uint64_t last_frame_used = 0;
  void* native_resource_handle = nullptr; // Puntero opaco al recurso de la API (D3D11/D3D12/VK)
};

class SurfaceManager {
 public:
  static SurfaceManager& Instance();

  void Initialize(uint32_t native_width, uint32_t native_height);
  void Shutdown();

  void BeginFrame(uint64_t frame_index);
  void EndFrame();

  // Asigna o recupera la superficie nativa correspondiente a un render target
  NativeSurface* GetOrCreateRenderTarget(uint32_t surface_base, uint32_t width, uint32_t height,
                                         SurfaceFormat format, bool is_depth = false);

  // Procesa un resolve sin pasar por EDRAM
  // Si dest_texture_address coincide con el frame final de salida, se marca como swap target.
  // Si es una textura intermedia (shadow map, bloom, reflejo), se indexa para muestreo nativo.
  bool ProcessResolve(const ResolveCommand& cmd, uint64_t current_frame);

  // Comprueba si una direccion de memoria huesped tiene una superficie nativa resuelta en GPU
  bool HasResolvedTexture(uint32_t guest_address) const;
  const NativeSurface* FindResolvedTexture(uint32_t guest_address) const;

  // Estadisticas de optimizacion
  uint32_t GetActiveSurfaceCount() const { return static_cast<uint32_t>(surfaces_.size()); }
  uint64_t GetTotalResolvesBypassed() const { return total_resolves_bypassed_; }
  uint64_t GetEdramBytesSaved() const { return edram_bytes_saved_; }

 private:
  SurfaceManager() = default;

  uint32_t native_width_  = 1920;
  uint32_t native_height_ = 1080;
  uint64_t current_frame_ = 0;

  // Superficies activas indexadas por su direccion base / ID
  std::unordered_map<uint32_t, NativeSurface> surfaces_;

  // Texturas resueltas indexadas por su direccion fisica de memoria huesped
  std::unordered_map<uint32_t, uint32_t> guest_addr_to_surface_id_;

  uint64_t total_resolves_bypassed_ = 0;
  uint64_t edram_bytes_saved_       = 0;
  uint32_t next_surface_id_         = 1;
};

}  // namespace re_wor::nativegfx
