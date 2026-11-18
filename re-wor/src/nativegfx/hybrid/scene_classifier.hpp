// nativegfx/hybrid/scene_classifier.hpp
// Clasificador de fases de renderizado (Fase 3: Hibrido).
// Discrimina entre el Escenario 3D (venue/personajes) y la Autopista/HUD.
#pragma once

#include <cstdint>
#include "nativegfx/device/draw_command.hpp"
#include "nativegfx/device/resolve_command.hpp"

namespace re_wor::nativegfx {

enum class RenderPhase : uint32_t {
  Unknown = 0,
  Stage3D,      // Escenario 3D, iluminacion, publico y personajes (antes del resolve)
  SceneResolve, // Resolve a pantalla completa del escenario
  HighwayHUD,   // Autopista de notas, trastes, marcadores y UI 2D
};

class SceneClassifier {
 public:
  SceneClassifier() = default;

  void OnBeginFrame();
  RenderPhase ClassifyDraw(const DrawCommand& cmd);
  void OnResolve(const ResolveCommand& cmd);

  RenderPhase GetCurrentPhase() const { return current_phase_; }
  bool IsInStage3D() const { return current_phase_ == RenderPhase::Stage3D; }

  uint32_t GetStage3DDrawCount() const { return stage_3d_draws_; }
  uint32_t GetHighwayHUDDrawCount() const { return highway_hud_draws_; }

 private:
  RenderPhase current_phase_ = RenderPhase::Stage3D;
  bool scene_resolved_ = false;

  uint32_t stage_3d_draws_    = 0;
  uint32_t highway_hud_draws_ = 0;
};

}  // namespace re_wor::nativegfx
