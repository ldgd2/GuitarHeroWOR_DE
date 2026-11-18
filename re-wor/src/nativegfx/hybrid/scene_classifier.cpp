// nativegfx/hybrid/scene_classifier.cpp
// Implementacion del clasificador de fases de dibujo.
#include "nativegfx/hybrid/scene_classifier.hpp"

namespace re_wor::nativegfx {

void SceneClassifier::OnBeginFrame() {
  current_phase_ = RenderPhase::Stage3D;
  scene_resolved_ = false;
  stage_3d_draws_ = 0;
  highway_hud_draws_ = 0;
}

RenderPhase SceneClassifier::ClassifyDraw(const DrawCommand& cmd) {
  if (!scene_resolved_) {
    current_phase_ = RenderPhase::Stage3D;
    ++stage_3d_draws_;
  } else {
    current_phase_ = RenderPhase::HighwayHUD;
    ++highway_hud_draws_;
  }
  return current_phase_;
}

void SceneClassifier::OnResolve(const ResolveCommand& cmd) {
  // El primer resolve grande de la superficie es el volcado del escenario 3D
  if (!scene_resolved_ && cmd.width >= 1280 && cmd.height >= 720) {
    scene_resolved_ = true;
    current_phase_ = RenderPhase::SceneResolve;
  }
}

}  // namespace re_wor::nativegfx
