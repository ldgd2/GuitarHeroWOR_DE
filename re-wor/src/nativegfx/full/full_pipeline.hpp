// nativegfx/full/full_pipeline.hpp
// Orquestador del Renderizado Nativo Completo (Fase 4: docs/RENDER_NATIVO.md).
// Reemplaza por completo el emulador de GPU Xenos para todas las llamadas
// del juego: Escenario 3D, Autopista de notas, HUD, marcadores y UI 2D.
// Las operaciones de Resolve se efectuan sin EDRAM via SurfaceManager.
#pragma once

#include <memory>
#include "nativegfx/backend/backend_factory.hpp"
#include "nativegfx/backend/native_renderer.hpp"
#include "nativegfx/device/device_tracker.hpp"
#include "nativegfx/full/gpu_decoupler.hpp"
#include "nativegfx/full/surface_manager.hpp"
#include "nativegfx/full/vblank_generator.hpp"

namespace re_wor::nativegfx {

class FullPipeline : public IDeviceEventListener {
 public:
  static FullPipeline& Instance();

  bool Initialize(void* window_handle, GraphicsBackendType backend_type = GraphicsBackendType::D3D12,
                  uint32_t target_width = 1920, uint32_t target_height = 1080);
  void Shutdown();

  void SetEnabled(bool enabled);
  bool IsEnabled() const { return enabled_; }

  void SetGuestMemoryBase(const uint8_t* base) { guest_memory_base_ = base; }

  // Callbacks de IDeviceEventListener (recibe 100% de draws y resolves)
  void OnDraw(const DrawCommand& cmd) override;
  void OnResolve(const ResolveCommand& cmd) override;
  void OnSwap() override;

  INativeRenderer* GetRenderer() const { return renderer_.get(); }
  SurfaceManager& GetSurfaceManager() { return SurfaceManager::Instance(); }
  VBlankGenerator& GetVBlankGenerator() { return VBlankGenerator::Instance(); }

  // Metricas de ejecucion de la Fase 4
  uint64_t GetTotalNativeDraws() const { return total_native_draws_; }
  uint64_t GetTotalNativeResolves() const { return total_native_resolves_; }
  uint64_t GetTotalFramesRendered() const { return total_frames_rendered_; }

 private:
  FullPipeline() = default;

  bool enabled_ = false;
  const uint8_t* guest_memory_base_ = nullptr;

  std::unique_ptr<INativeRenderer> renderer_;

  uint64_t total_native_draws_    = 0;
  uint64_t total_native_resolves_ = 0;
  uint64_t total_frames_rendered_ = 0;
};

}  // namespace re_wor::nativegfx
