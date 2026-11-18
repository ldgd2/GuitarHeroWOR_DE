// nativegfx/hybrid/hybrid_pipeline.hpp
// Pipeline de Render Hibrido (Fase 3: docs/RENDER_NATIVO.md).
// Orquesta el render nativo en alta resolucion del escenario 3D combinandolo
// con el pipeline emulado para la autopista y el HUD.
#pragma once

#include <memory>
#include "nativegfx/backend/backend_factory.hpp"
#include "nativegfx/backend/native_renderer.hpp"
#include "nativegfx/device/device_tracker.hpp"
#include "nativegfx/hybrid/scene_classifier.hpp"

namespace re_wor::nativegfx {

class HybridPipeline : public IDeviceEventListener {
 public:
  static HybridPipeline& Instance();

  bool Initialize(void* window_handle, GraphicsBackendType backend_type = GraphicsBackendType::D3D11,
                  uint32_t target_width = 1920, uint32_t target_height = 1080);
  void Shutdown();

  void SetEnabled(bool enabled) { enabled_ = enabled; }
  bool IsEnabled() const { return enabled_; }

  void SetGuestMemoryBase(const uint8_t* base) { guest_memory_base_ = base; }

  // Callbacks de IDeviceEventListener
  void OnDraw(const DrawCommand& cmd) override;
  void OnResolve(const ResolveCommand& cmd) override;
  void OnSwap() override;

  INativeRenderer* GetRenderer() const { return renderer_.get(); }
  const SceneClassifier& GetClassifier() const { return classifier_; }

 private:
  HybridPipeline() = default;

  bool enabled_ = false;
  const uint8_t* guest_memory_base_ = nullptr;

  std::unique_ptr<INativeRenderer> renderer_;
  SceneClassifier classifier_;

  uint64_t total_hybrid_draws_ = 0;
};

}  // namespace re_wor::nativegfx
