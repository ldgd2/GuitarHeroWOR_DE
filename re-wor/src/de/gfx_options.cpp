// Opciones graficas "de PC": se traducen a cvars del SDK (ver gfx_options.hpp).
#include "de/gfx_options.hpp"

#include <algorithm>
#include <string>

#include <cmath>

#include <rex/cvar.h>
#include <rex/ui/imgui_dialog.h>

#include "core/crash_handler.hpp"
#include "de/de_hooks.hpp"
#include "de/qb_bridge.hpp"

namespace re_wor::de {

namespace {
void Set(const char* name, const std::string& v) {
  if (!rex::cvar::SetFlagByName(name, v)) TRACE("[Graficos] el SDK no acepto " << name << " = " << v);
}
GraphicsConfig g_boot;
}  // namespace

const GraphicsConfig& BootGraphics() { return g_boot; }

void ApplyDisplayOptions(const GraphicsConfig& g) {
  g_boot = g;
  Set("resolution", g.resolution);
  Set("fullscreen", g.window_mode > 0 ? "true" : "false");
  Set("fullscreen_exclusive", g.window_mode == 2 ? "true" : "false");
  // VBlank del juego: 60 es lo de la consola; mas alto deja al motor dibujar mas veces por segundo
  Set("video_mode_refresh_rate", std::to_string(std::clamp(g.refresh_hz, 30, 240)));
  const std::string up = (g.upscale == "cas" || g.upscale == "fsr") ? g.upscale : "bilinear";
  Set("present_effect", up);
  Set("present_cas_additional_sharpness", std::to_string(std::clamp(g.sharpness, 0.f, 1.f)));
  // FSR: la nitidez se expresa como "reduccion" en pasos (0 = maxima, 2 = minima)
  Set("present_fsr_sharpness_reduction", std::to_string(2.f * (1.f - std::clamp(g.sharpness, 0.f, 1.f))));
  Set("native_2x_msaa", g.msaa2x ? "true" : "false");
  ApplyLiveDisplayOptions(g);
  TRACE("[Graficos] resolucion='" << g.resolution << "' modo=" << g.window_mode << " " << g.refresh_hz
        << " Hz salida=" << up << " nitidez=" << g.sharpness << " msaa2x=" << g.msaa2x);
}

void ApplyLiveDisplayOptions(const GraphicsConfig& g) {
  Set("present_letterbox", g.letterbox ? "true" : "false");
  Set("present_vsync", g.vsync ? "true" : "false");
}

// ---------------------------------------------------------------------------- menu del juego
namespace {
constexpr int kOptions = 17;   // re_gfx_v0 .. re_gfx_v16 (mismo orden que qb/options_graphics.q)
const char* const kResolutions[] = {"", "720p", "1080p", "1440p", "4k"};
const int kRates[] = {60, 75, 120, 144, 165, 240};
const char* const kUpscale[] = {"bilinear", "cas", "fsr"};

int Step10(float v) { return std::clamp(static_cast<int>(std::lround(v * 10)), 0, 10); }      // 0..1 -> 0..10
int Signed10(float v) { return std::clamp(static_cast<int>(std::lround(v * 5)) + 5, 0, 10); }  // -1..1 -> 0..10

void ToMenu(const GraphicsConfig& g, int* m) {
  m[0] = 0;
  for (int i = 0; i < 5; ++i) if (g.resolution == kResolutions[i]) m[0] = i;
  m[1] = std::clamp(g.window_mode, 0, 2);
  m[2] = 0;
  for (int i = 0; i < 6; ++i) if (g.refresh_hz == kRates[i]) m[2] = i;
  m[3] = g.vsync ? 1 : 0;
  m[4] = g.show_fps ? 1 : 0;
  m[5] = std::clamp(g.resolution_scale, 1, 4) - 1;
  m[6] = g.msaa2x ? 1 : 0;
  m[7] = 0;
  for (int i = 0; i < 3; ++i) if (g.upscale == kUpscale[i]) m[7] = i;
  m[8] = Step10(g.sharpness);
  m[9] = Step10(g.bloom);
  m[10] = Step10(g.glow);
  m[11] = Step10(g.blur);
  m[12] = Signed10(g.brightness);
  m[13] = Signed10(g.contrast);
  m[14] = Signed10(g.saturation);
  m[15] = Step10(g.vignette);
  m[16] = Step10(g.grain);
}

void FromMenu(int i, int v, GraphicsConfig& g) {
  auto in = [&](int n) { return std::clamp(v, 0, n - 1); };
  switch (i) {
    case 0: g.resolution = kResolutions[in(5)]; break;
    case 1: g.window_mode = in(3); break;
    case 2: g.refresh_hz = kRates[in(6)]; break;
    case 3: g.vsync = v != 0; break;
    case 4: g.show_fps = v != 0; break;
    case 5: g.resolution_scale = in(4) + 1; break;
    case 6: g.msaa2x = v != 0; break;
    case 7: g.upscale = kUpscale[in(3)]; break;
    case 8: g.sharpness = in(11) / 10.f; break;
    case 9: g.bloom = in(11) / 10.f; break;
    case 10: g.glow = in(11) / 10.f; break;
    case 11: g.blur = in(11) / 10.f; break;
    case 12: g.brightness = (in(11) - 5) / 5.f; break;
    case 13: g.contrast = (in(11) - 5) / 5.f; break;
    case 14: g.saturation = (in(11) - 5) / 5.f; break;
    case 15: g.vignette = in(11) / 10.f; break;
    case 16: g.grain = in(11) / 10.f; break;
  }
}

bool NeedsRestart(const GraphicsConfig& g) {
  const GraphicsConfig& b = g_boot;
  return g.resolution != b.resolution || g.window_mode != b.window_mode || g.refresh_hz != b.refresh_hz ||
         g.resolution_scale != b.resolution_scale || g.msaa2x != b.msaa2x || g.upscale != b.upscale ||
         g.sharpness != b.sharpness;
}

std::string Var(int i) { return "re_gfx_v" + std::to_string(i); }
}  // namespace

void SyncGameMenu(uint8_t* base) {
  static bool ready = false;
  static int last[kOptions];
  static int tick = 0;
  if (!base || !qb_bridge::ScriptsLoaded() || (++tick % 6)) return;   // 10 veces por segundo basta

  GraphicsConfig cur;
  {
    std::lock_guard lock(Mutex());
    cur = Get().graphics;
  }
  int want[kOptions];
  ToMenu(cur, want);
  if (!ready) {   // primera vez: el menu arranca con lo que hay en re_wor_de.toml
    for (int i = 0; i < kOptions; ++i) {
      if (!qb_bridge::WriteIntGlobal(Var(i), base, want[i])) return;   // el archivo de scripts no esta
      last[i] = want[i];
    }
    qb_bridge::WriteIntGlobal("re_gfx_pending", base, NeedsRestart(cur) ? 1 : 0);
    ready = true;
    TRACE("[Graficos] menu Options > Graphics del juego enlazado");
    return;
  }

  bool changed = false;
  int reset = 0;
  if (qb_bridge::ReadIntGlobal("re_gfx_reset", base, &reset) && reset) {
    const GraphicsConfig d;
    GraphicsConfig& g = cur;
    g.resolution = d.resolution;
    g.window_mode = d.window_mode;
    g.refresh_hz = d.refresh_hz;
    g.vsync = d.vsync;
    g.show_fps = d.show_fps;
    g.resolution_scale = d.resolution_scale;
    g.msaa2x = d.msaa2x;
    g.upscale = d.upscale;
    g.sharpness = d.sharpness;
    g.bloom = g.glow = g.blur = g.brightness = g.contrast = g.saturation = g.vignette = g.grain = 0;
    changed = true;
  } else {
    for (int i = 0; i < kOptions; ++i) {
      int v = last[i];
      if (!qb_bridge::ReadIntGlobal(Var(i), base, &v)) return;
      if (v != last[i]) {        // lo cambio el menu del juego
        FromMenu(i, v, cur);
        changed = true;
      }
    }
  }
  if (changed) {
    {
      std::lock_guard lock(Mutex());
      Get().graphics = cur;
    }
    Save();
    ApplyLiveDisplayOptions(cur);
    TRACE("[Graficos] cambiado desde el menu del juego");
  }
  // escribir de vuelta: normaliza el valor del menu y recoge cambios hechos desde el panel F8
  ToMenu(cur, want);
  for (int i = 0; i < kOptions; ++i) {
    if (want[i] != last[i] || changed) qb_bridge::WriteIntGlobal(Var(i), base, want[i]);
    last[i] = want[i];
  }
  if (reset) qb_bridge::WriteIntGlobal("re_gfx_reset", base, 0);
  qb_bridge::WriteIntGlobal("re_gfx_pending", base, NeedsRestart(cur) ? 1 : 0);
}

// ---------------------------------------------------------------------------- contador de FPS
namespace {
class FpsCounter final : public rex::ui::ImGuiDialog {
 public:
  explicit FpsCounter(rex::ui::ImGuiDrawer* d) : ImGuiDialog(d) {}

 protected:
  void OnDraw(ImGuiIO& io) override {
    if (!Get().graphics.show_fps) return;
    const ImGuiWindowFlags f = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                               ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                               ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoMove;
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - 12, 12), ImGuiCond_Always, ImVec2(1, 0));
    ImGui::SetNextWindowBgAlpha(0.45f);
    if (ImGui::Begin("##re_wor_fps", nullptr, f)) {
      const double fps = GameFps();
      ImGui::TextColored(fps >= 55 ? ImVec4(0.55f, 0.95f, 0.55f, 1) : ImVec4(1, 0.75f, 0.3f, 1), "%.0f FPS", fps);
    }
    ImGui::End();
  }
};
}  // namespace

void CreateFpsCounter(rex::ui::ImGuiDrawer* drawer) {
  static FpsCounter* s_counter = nullptr;   // vive lo que viva el drawer
  if (!s_counter && drawer) s_counter = new FpsCounter(drawer);
}

}  // namespace re_wor::de
