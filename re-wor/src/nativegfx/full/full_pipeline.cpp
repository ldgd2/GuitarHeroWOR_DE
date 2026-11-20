// nativegfx/full/full_pipeline.cpp
// Implementacion del renderizador nativo completo (Fase 4).
#include "nativegfx/full/full_pipeline.hpp"
#include "nativegfx/backend/backend_factory.hpp"

namespace re_wor::nativegfx {

FullPipeline& FullPipeline::Instance() {
  static FullPipeline s_instance;
  return s_instance;
}

bool FullPipeline::Initialize(void* window_handle, GraphicsBackendType backend_type,
                              uint32_t target_width, uint32_t target_height) {
  renderer_ = BackendFactory::CreateRenderer(backend_type);
  if (!renderer_ || !renderer_->Initialize(window_handle, target_width, target_height)) {
    renderer_.reset();
    return false;
  }

  SurfaceManager::Instance().Initialize(target_width, target_height);
  VBlankGenerator::Instance().Initialize(60);
  GpuDecoupler::Instance().SetMode(PipelineMode::FullNative);

  float scale = float(target_height) / 720.0f;
  DeviceTracker::Instance().SetResolutionScale(scale > 0.0f ? scale : 1.0f);

  DeviceTracker::Instance().AddListener(this);
  renderer_->BeginFrame();

  enabled_ = true;
  return true;
}

void FullPipeline::Shutdown() {
  DeviceTracker::Instance().RemoveListener(this);

  if (renderer_) {
    renderer_->Shutdown();
    renderer_.reset();
  }

  SurfaceManager::Instance().Shutdown();
  VBlankGenerator::Instance().Shutdown();

  enabled_ = false;
}

void FullPipeline::SetEnabled(bool enabled) {
  enabled_ = enabled;
  if (enabled) {
    GpuDecoupler::Instance().SetMode(PipelineMode::FullNative);
  } else {
    GpuDecoupler::Instance().SetMode(PipelineMode::Emulated);
  }
}

void FullPipeline::OnDraw(const DrawCommand& cmd) {
  if (!enabled_ || !renderer_) return;

  ++total_native_draws_;

  // En la Fase 4: TODO se procesa por el backend nativo (Escenario 3D, Autopista de notas y HUD 2D)
  renderer_->RenderDraw(cmd, guest_memory_base_);
}

void FullPipeline::OnResolve(const ResolveCommand& cmd) {
  if (!enabled_ || !renderer_) return;

  ++total_native_resolves_;

  // En la Fase 4: resolve sin EDRAM. Se registra el destino en GPU directamente
  SurfaceManager::Instance().ProcessResolve(cmd, total_frames_rendered_);

  renderer_->RenderResolve(cmd);
}

void FullPipeline::OnSwap() {
  if (!enabled_ || !renderer_) return;

  ++total_frames_rendered_;

  // Enviar trabajo a la GPU (la presentacion a la ventana se realiza de forma unificada sin colisiones)
  if (total_native_draws_ > 0) {
    renderer_->EndFrame();
    renderer_->Present();
  }

  // Notificar al generador autonomo de VBlank
  VBlankGenerator::Instance().OnSwap();

  // Iniciar el siguiente cuadro en el dispositivo nativo
  SurfaceManager::Instance().BeginFrame(total_frames_rendered_);
  renderer_->BeginFrame();
}

}  // namespace re_wor::nativegfx
