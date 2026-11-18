// nativegfx/hybrid/hybrid_pipeline.cpp
// Implementacion del pipeline de renderizado hibrido (Fase 3).
#include "nativegfx/hybrid/hybrid_pipeline.hpp"
#include "nativegfx/backend/backend_factory.hpp"

namespace re_wor::nativegfx {

HybridPipeline& HybridPipeline::Instance() {
  static HybridPipeline s_instance;
  return s_instance;
}

bool HybridPipeline::Initialize(void* window_handle, GraphicsBackendType backend_type, uint32_t target_width, uint32_t target_height) {
  renderer_ = BackendFactory::CreateRenderer(backend_type);
  if (!renderer_ || !renderer_->Initialize(window_handle, target_width, target_height)) {
    renderer_.reset();
    return false;
  }

  DeviceTracker::Instance().AddListener(this);
  classifier_.OnBeginFrame();
  enabled_ = true;
  return true;
}

void HybridPipeline::Shutdown() {
  DeviceTracker::Instance().RemoveListener(this);
  if (renderer_) {
    renderer_->Shutdown();
    renderer_.reset();
  }
  enabled_ = false;
}

void HybridPipeline::OnDraw(const DrawCommand& cmd) {
  if (!enabled_ || !renderer_) return;

  RenderPhase phase = classifier_.ClassifyDraw(cmd);

  // En la Fase 3, el render nativo toma el control de los draws del escenario 3D
  if (phase == RenderPhase::Stage3D) {
    ++total_hybrid_draws_;
    renderer_->RenderDraw(cmd, guest_memory_base_);
  }
}

void HybridPipeline::OnResolve(const ResolveCommand& cmd) {
  if (!enabled_ || !renderer_) return;

  classifier_.OnResolve(cmd);
  renderer_->RenderResolve(cmd);
}

void HybridPipeline::OnSwap() {
  if (!enabled_ || !renderer_) return;

  if (total_hybrid_draws_ > 0) {
    renderer_->EndFrame();
    renderer_->Present();
  }

  classifier_.OnBeginFrame();
  renderer_->BeginFrame();
}

}  // namespace re_wor::nativegfx
