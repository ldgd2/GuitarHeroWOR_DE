// Opciones graficas "de PC": se traducen a cvars del SDK (ver gfx_options.hpp).
#include "de/gfx_options.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <mutex>
#include <cmath>
#include <string>

#include <rex/cvar.h>
#include <rex/graphics/draw_hook.h>
#include <rex/ui/imgui_dialog.h>

#include "core/crash_handler.hpp"
#include "de/controls_menu.hpp"
#include "de/de_hooks.hpp"
#include "de/edfx.hpp"
#include "de/frame_pacing.hpp"
#include "de/qb_bridge.hpp"
#include "de/render_sandbox.hpp"

#ifdef _WIN32
#include <windows.h>
#endif

namespace re_wor::de {

namespace {
void Set(const char* name, const std::string& v) {
  if (!rex::cvar::SetFlagByName(name, v)) TRACE("[Graficos] el SDK no acepto " << name << " = " << v);
}
GraphicsConfig g_boot;
std::atomic<int> g_frame_cap{60};
}  // namespace

int FrameCapHz() {
  if (re_wor::de::Get().dev.uncapped_fps) return 1000;
  return g_frame_cap.load(std::memory_order_relaxed);
}

const GraphicsConfig& BootGraphics() { return g_boot; }

void ApplyDisplayOptions(const GraphicsConfig& g) {
  g_boot = g;
  // API grafica (plugin de GPU): se elige antes de cargarlo; si Vulkan no puede presentar, el SDK vuelve a D3D12
  Set("gpu_backend", g.backend == "vulkan" ? "vulkan" : "d3d12");
  Set("resolution", g.resolution);
  Set("fullscreen", g.window_mode > 0 ? "true" : "false");
  Set("fullscreen_exclusive", g.window_mode == 2 ? "true" : "false");
  // VBlank del juego: siempre 60 Hz, como la consola. Parte del juego (el tiempo de la cancion, entre
  // otros) cuenta VBlanks como 1/60 s: con un VBlank mas rapido el juego entero iba acelerado. Los FPS
  // altos salen de quitar los candados del motor y del limitador de re-wor (FrameCapHz), no del VBlank.
  Set("video_mode_refresh_rate", "60");
  const std::string up = (g.upscale == "cas" || g.upscale == "fsr") ? g.upscale : "bilinear";
  Set("present_effect", up);
  Set("present_cas_additional_sharpness", std::to_string(std::clamp(g.sharpness, 0.f, 1.f)));
  // FSR: la nitidez se expresa como "reduccion" en pasos (0 = maxima, 2 = minima)
  Set("present_fsr_sharpness_reduction", std::to_string(2.f * (1.f - std::clamp(g.sharpness, 0.f, 1.f))));
  // Las cvars del plugin de GPU aun no existen aqui: el SDK las guarda y las aplica al cargarlo
  Set("native_2x_msaa", g.msaa2x ? "true" : "false");
  Set("anisotropic_override", std::to_string(std::clamp(g.aniso, 0, 4) + 1));   // 1 = 1x ... 5 = 16x
  ApplyLiveDisplayOptions(g);
  TRACE("[Graficos] api=" << g.backend << " preset=" << g.preset << " resolucion='" << g.resolution
        << "' modo=" << g.window_mode << " " << g.refresh_hz << " Hz salida=" << up << " nitidez=" << g.sharpness
        << " msaa2x=" << g.msaa2x << " fxaa=" << g.fxaa << " aniso=" << (1 << std::clamp(g.aniso, 0, 4))
        << "x postfx=" << g.post_fx << " cache=" << g.texture_cache_mb << " MB");
}

void ApplyLiveDisplayOptions(const GraphicsConfig& g) {
  Set("present_letterbox", g.letterbox ? "true" : "false");
  Set("present_vsync", g.vsync ? "true" : "false");
  Set("swap_post_effect", g.fxaa ? "fxaa" : "none");   // el SDK lo relee en cada swap
  // Frame Rate: limite de re-wor sobre el bucle del motor (el VBlank sigue a 60, ver ApplyDisplayOptions)
  g_frame_cap = std::clamp(g.refresh_hz, 30, 240);
}

// ---------------------------------------------------------------------------- resolucion con confirmacion
namespace {
struct DisplayConfirm {
  bool active = false;
  std::chrono::steady_clock::time_point deadline{};
  std::string prev_res, new_res;
  int prev_mode = 0, new_mode = 0;
};
std::mutex g_cmx;
DisplayConfirm g_confirm;
constexpr int kConfirmSeconds = 10;

void ApplyWindow(const std::string& res, int mode) {
  Set("fullscreen_exclusive", mode == 2 ? "true" : "false");
  Set("resolution", res);   // el SDK cambia el tamano de la ventana / el modo exclusivo al momento
  Set("fullscreen", mode > 0 ? "true" : "false");
}

const char* ResLabel(const std::string& r) {
  if (r == "720p") return "1280 x 720";
  if (r == "1080p") return "1920 x 1080";
  if (r == "1440p") return "2560 x 1440";
  if (r == "4k") return "3840 x 2160";
  return "la del escritorio";
}
const char* ModeLabel(int m) { return m == 2 ? "pantalla completa" : m == 1 ? "sin bordes" : "ventana"; }

// Termina la confirmacion: keep = mantener lo nuevo; si no, se vuelve a lo anterior
void FinishConfirm(bool keep) {
  DisplayConfirm c;
  {
    std::lock_guard lk(g_cmx);
    if (!g_confirm.active) return;
    c = g_confirm;
    g_confirm.active = false;
  }
  if (!keep) ApplyWindow(c.prev_res, c.prev_mode);
  {
    std::lock_guard lock(Mutex());
    Get().graphics.resolution = keep ? c.new_res : c.prev_res;
    Get().graphics.window_mode = keep ? c.new_mode : c.prev_mode;
  }
  Save();
  TRACE("[Graficos] " << (keep ? "resolucion confirmada: " : "sin confirmar, se vuelve a: ")
                      << ResLabel(keep ? c.new_res : c.prev_res) << ", "
                      << ModeLabel(keep ? c.new_mode : c.prev_mode));
}

bool OurWindowFocused() {
#ifdef _WIN32
  DWORD pid = 0;
  GetWindowThreadProcessId(GetForegroundWindow(), &pid);
  return pid == GetCurrentProcessId();
#else
  return true;
#endif
}
}  // namespace

void RequestDisplayChange(const std::string& prev_res, int prev_mode, const std::string& res, int mode) {
  {
    std::lock_guard lk(g_cmx);
    if (!g_confirm.active) {   // con una prueba en curso, "lo anterior" sigue siendo lo ultimo confirmado
      g_confirm.prev_res = prev_res;
      g_confirm.prev_mode = prev_mode;
    }
    g_confirm.new_res = res;
    g_confirm.new_mode = mode;
    g_confirm.deadline = std::chrono::steady_clock::now() + std::chrono::seconds(kConfirmSeconds);
    g_confirm.active = !(res == g_confirm.prev_res && mode == g_confirm.prev_mode);
  }
  ApplyWindow(res, mode);
  TRACE("[Graficos] probando " << ResLabel(res) << ", " << ModeLabel(mode) << " (" << kConfirmSeconds
                               << " s para confirmar)");
}

bool DisplayConfirmPending() {
  std::lock_guard lk(g_cmx);
  return g_confirm.active;
}

// Cada frame logico: Enter / A mantiene, Esc / B vuelve, y al acabar el tiempo se vuelve solo
void TickDisplayConfirm(uint8_t* base) {
  int menu = 0;   // 1 mantener / 2 volver, desde el menu del juego
  if (base && qb_bridge::ReadIntGlobal("re_gfx_confirm", base, &menu) && menu) {
    qb_bridge::WriteIntGlobal("re_gfx_confirm", base, 0);
  }
  bool active, expired;
  {
    std::lock_guard lk(g_cmx);
    active = g_confirm.active;
    expired = std::chrono::steady_clock::now() >= g_confirm.deadline;
  }
  static bool s_wait = false;
  if (base && s_wait != active) {
    qb_bridge::WriteIntGlobal("re_gfx_confirm_wait", base, active ? 1 : 0);
    s_wait = active;
  }
  if (!active) return;
#ifdef _WIN32
  const bool focused = OurWindowFocused();
  if (focused && (GetAsyncKeyState(VK_RETURN) & 0x8000)) menu = 1;
  if (focused && (GetAsyncKeyState(VK_ESCAPE) & 0x8000)) menu = 2;
#endif
  if (menu == 1) {
    FinishConfirm(true);
  } else if (menu == 2 || expired) {
    FinishConfirm(false);
  }
  if (menu || expired) {
    if (base) {
      qb_bridge::WriteIntGlobal("re_gfx_confirm_wait", base, 0);
      qb_bridge::WriteIntGlobal("re_gfx_redraw", base, 1);
    }
    s_wait = false;
  }
}

// ---------------------------------------------------------------------------- menu del juego
namespace {
// re_gfx_v0 .. re_gfx_v20, mismo orden que qb/options_graphics.q
enum Opt {
  kPreset, kApi, kResolution, kDisplayMode, kFrameRate, kVSync, kShowFps, kRenderScale, kAntiAliasing,
  kTextureFilter, kPostFx, kBloom, kGlow, kBlur, kBrightness, kContrast, kSaturation, kVignette, kGrain,
  kCrowd, kOptions
};
const char* const kResolutions[] = {"", "720p", "1080p", "1440p", "4k"};
const int kRates[] = {60, 75, 120, 144, 165, 240};

int Step10(float v) { return std::clamp(static_cast<int>(std::lround(v * 10)), 0, 10); }      // 0..1 -> 0..10
int Signed10(float v) { return std::clamp(static_cast<int>(std::lround(v * 5)) + 5, 0, 10); }  // -1..1 -> 0..10

void ToMenu(const GraphicsConfig& g, int* m) {
  m[kPreset] = std::clamp(g.preset, 0, 4);
  m[kApi] = g.backend == "vulkan" ? 1 : 0;
  m[kResolution] = 0;
  for (int i = 0; i < 5; ++i) if (g.resolution == kResolutions[i]) m[kResolution] = i;
  m[kDisplayMode] = std::clamp(g.window_mode, 0, 2);
  m[kFrameRate] = 0;
  for (int i = 0; i < 6; ++i) if (g.refresh_hz == kRates[i]) m[kFrameRate] = i;
  m[kVSync] = g.vsync ? 1 : 0;
  m[kShowFps] = g.show_fps ? (g.perf_detail ? 2 : 1) : 0;
  m[kRenderScale] = std::clamp(g.resolution_scale, 1, 4) - 1;
  m[kAntiAliasing] = (g.msaa2x ? 2 : 0) + (g.fxaa ? 1 : 0);   // Off, FXAA, MSAA 2x, MSAA 2x + FXAA
  m[kTextureFilter] = std::clamp(g.aniso, 0, 4);
  m[kPostFx] = g.post_fx ? 1 : 0;
  m[kBloom] = Step10(g.bloom);
  m[kGlow] = Step10(g.glow);
  m[kBlur] = Step10(g.blur);
  m[kBrightness] = Signed10(g.brightness);
  m[kContrast] = Signed10(g.contrast);
  m[kSaturation] = Signed10(g.saturation);
  m[kVignette] = Step10(g.vignette);
  m[kGrain] = Step10(g.grain);
  m[kCrowd] = std::clamp(g.crowd, 0, 3);
}

void FromMenu(int i, int v, GraphicsConfig& g) {
  auto in = [&](int n) { return std::clamp(v, 0, n - 1); };
  switch (i) {
    case kPreset: g.ApplyPreset(in(5)); return;   // 4 = Custom: no cambia nada
    case kApi: g.backend = in(2) ? "vulkan" : "d3d12"; break;
    case kResolution: g.resolution = kResolutions[in(5)]; break;
    case kDisplayMode: g.window_mode = in(3); break;
    case kFrameRate: g.refresh_hz = kRates[in(6)]; break;
    case kVSync: g.vsync = v != 0; break;
    case kShowFps: g.show_fps = in(3) > 0; g.perf_detail = in(3) == 2; break;
    case kRenderScale: g.resolution_scale = in(4) + 1; break;
    case kAntiAliasing: g.msaa2x = in(4) >= 2; g.fxaa = in(4) & 1; break;
    case kTextureFilter: g.aniso = in(5); break;
    case kPostFx: g.post_fx = v != 0; break;
    case kBloom: g.bloom = in(11) / 10.f; break;
    case kGlow: g.glow = in(11) / 10.f; break;
    case kBlur: g.blur = in(11) / 10.f; break;
    case kBrightness: g.brightness = (in(11) - 5) / 5.f; break;
    case kContrast: g.contrast = (in(11) - 5) / 5.f; break;
    case kSaturation: g.saturation = (in(11) - 5) / 5.f; break;
    case kVignette: g.vignette = in(11) / 10.f; break;
    case kGrain: g.grain = in(11) / 10.f; break;
    case kCrowd: g.crowd = in(4); break;
  }
  g.preset = g.MatchPreset();   // tocar a mano una opcion del preset lo deja en Custom
}

std::string Var(int i) { return "re_gfx_v" + std::to_string(i); }
}  // namespace

// Publico (qb/crowd.q): se pasa el nivel al script y se aplica al cambiarlo y, durante una cancion, cada 2 s
// (el escenario crea el publico al cargar, despues de que el nivel ya estuviera puesto).
namespace {
void SyncLanguage(uint8_t* base);
void SyncCrowd(uint8_t* base, int level) {
  static int s_tick = 0;
  int cur = -1;
  if (!qb_bridge::ReadIntGlobal("re_crowd_level", base, &cur)) return;   // qb/crowd.q no esta cargado
  const bool changed = cur != level;
  if (changed) {
    qb_bridge::WriteIntGlobal("re_crowd_level", base, level);
    TRACE("[Graficos] publico: " << (level == 0 ? "ninguno" : level == 1 ? "bajo" : level == 2 ? "medio" : "alto"));
  }
  // solo con el escenario cargado (durante una cancion): en los menus no hay publico que tocar
  if (!edfx::SongPlaying()) return;
  if (changed || (++s_tick % 20) == 0) qb_bridge::ExecuteScript("re_crowd_apply");
}
}  // namespace

// Lo que el SDK solo puede crear al arrancar. Resolucion, modo de pantalla y frecuencia van al momento.
bool NeedsRestart(const GraphicsConfig& g) {
  const GraphicsConfig& b = g_boot;
  return g.backend != b.backend || g.resolution_scale != b.resolution_scale || g.msaa2x != b.msaa2x ||
         g.aniso != b.aniso || g.texture_cache_mb != b.texture_cache_mb;
}


void SyncGameMenu(uint8_t* base) {
  static bool ready = false;
  static int last[kOptions];
  static int tick = 0;
  if (!base || !qb_bridge::ScriptsLoaded()) return;
  controls::Tick(base);   // Options > Controls (qb/controls.q): cada frame, para que el menu responda al momento
  if (++tick % 6) return;   // el resto, 10 veces por segundo basta

  GraphicsConfig cur;
  {
    std::lock_guard lock(Mutex());
    cur = Get().graphics;
  }
  // mientras se confirma una resolucion, el menu muestra la que se esta probando
  auto with_pending = [](int* m) {
    std::lock_guard lk(g_cmx);
    if (!g_confirm.active) return;
    GraphicsConfig t;
    t.resolution = g_confirm.new_res;
    t.window_mode = g_confirm.new_mode;
    int tm[kOptions];
    ToMenu(t, tm);
    m[kResolution] = tm[kResolution];
    m[kDisplayMode] = tm[kDisplayMode];
  };
  int want[kOptions];
  ToMenu(cur, want);
  with_pending(want);
  if (!ready) {   // primera vez: el menu arranca con lo que hay en re_wor_de.toml
    for (int i = 0; i < kOptions; ++i) {
      if (!qb_bridge::WriteIntGlobal(Var(i), base, want[i])) return;   // el archivo de scripts no esta
      last[i] = want[i];
    }
    qb_bridge::WriteIntGlobal("re_gfx_pending", base, NeedsRestart(cur) ? 1 : 0);
    ready = true;
    TRACE("[Graficos] menu Options > Graphics del juego enlazado");
    SyncLanguage(base);
    return;
  }

  bool changed = false;
  int reset = 0;
  if (qb_bridge::ReadIntGlobal("re_gfx_reset", base, &reset) && reset) {
    const std::string res = cur.resolution;   // resolucion y modo no cambian sin confirmar
    const int mode = cur.window_mode;
    cur = GraphicsConfig{};   // valores por defecto (preset High)
    cur.resolution = res;
    cur.window_mode = mode;
    changed = true;
  } else {
    for (int i = 0; i < kOptions; ++i) {
      int v = last[i];
      if (!qb_bridge::ReadIntGlobal(Var(i), base, &v)) return;
      if (v == last[i]) continue;
      if (i == kResolution || i == kDisplayMode) {
        // se aplica al momento y se pide confirmacion; el .toml no cambia hasta confirmar
        GraphicsConfig t = cur;
        {
          std::lock_guard lk(g_cmx);
          if (g_confirm.active) {
            t.resolution = g_confirm.new_res;
            t.window_mode = g_confirm.new_mode;
          }
        }
        FromMenu(i, v, t);
        RequestDisplayChange(cur.resolution, cur.window_mode, t.resolution, t.window_mode);
        last[i] = v;
        continue;
      }
      FromMenu(i, v, cur);   // lo cambio el menu del juego
      changed = true;
    }
  }
  if (changed) {
    {
      std::lock_guard lock(Mutex());
      Get().graphics = cur;
    }
    Save();
    ApplyLiveDisplayOptions(cur);
    TRACE("[Graficos] cambiado desde el menu del juego (preset " << cur.preset << ")");
  }
  // escribir de vuelta: normaliza el valor del menu, refleja el preset y recoge cambios del panel F8
  {
    std::lock_guard lock(Mutex());
    const GraphicsConfig& g = Get().graphics;   // la confirmacion pudo cambiar resolucion / modo
    cur.resolution = g.resolution;
    cur.window_mode = g.window_mode;
  }
  ToMenu(cur, want);
  with_pending(want);
  bool redraw = false;
  for (int i = 0; i < kOptions; ++i) {
    if (want[i] != last[i] || changed) {
      qb_bridge::WriteIntGlobal(Var(i), base, want[i]);
      redraw = redraw || want[i] != last[i];
    }
    last[i] = want[i];
  }
  if (reset) qb_bridge::WriteIntGlobal("re_gfx_reset", base, 0);
  if (redraw && !reset) qb_bridge::WriteIntGlobal("re_gfx_redraw", base, 1);   // el menu repinta sus filas
  qb_bridge::WriteIntGlobal("re_gfx_pending", base, NeedsRestart(cur) ? 1 : 0);
  SyncCrowd(base, cur.crowd);
  SyncLanguage(base);
}

// Language (menu de opciones del juego): re_lang <-> [game] language. Se aplica al reiniciar (language.cpp).
namespace {
const char* const kLangCodes[] = {"en", "es", "fr", "de", "it"};
int LangIndex(const std::string& code) {
  for (int i = 0; i < 5; ++i)
    if (code == kLangCodes[i]) return i;
  return 0;
}
void SyncLanguage(uint8_t* base) {
  static int boot = -1;
  static int last = -1;
  std::string code;
  {
    std::lock_guard lock(Mutex());
    code = Get().game.language;
  }
  const int want = LangIndex(code);
  if (boot < 0) {   // el idioma de este arranque es el del .toml al empezar
    boot = want;
    qb_bridge::WriteIntGlobal("re_lang_boot", base, boot);
    qb_bridge::WriteIntGlobal("re_lang", base, want);
    last = want;
    return;
  }
  int v = last;
  if (!qb_bridge::ReadIntGlobal("re_lang", base, &v)) return;
  if (v != last && v >= 0 && v < 5) {   // lo cambio el menu del juego
    {
      std::lock_guard lock(Mutex());
      Get().game.language = kLangCodes[v];
    }
    Save();
    last = v;
    TRACE("[Idioma] elegido " << kLangCodes[v] << " (al reiniciar)");
  } else if (want != last) {            // lo cambio el panel F8
    qb_bridge::WriteIntGlobal("re_lang", base, want);
    last = want;
  }
}
}  // namespace

// ---------------------------------------------------------------------------- rendimiento
namespace {
struct PerfView {
  uint32_t seq = 0;
  rex::graphics::GpuStats s;
  std::chrono::steady_clock::time_point last_log{};
} g_perf;
}  // namespace

void PerfTick() {
  const rex::graphics::GpuStats* st = sandbox::GpuStats();
  if (!st || st->seq == g_perf.seq) return;
  g_perf.seq = st->seq;
  g_perf.s = *st;
  const auto& s = g_perf.s;
  if (!s.frames) return;
  // Tiron: un frame que dura mas de 2,5 veces la media de la ventana (y mas de 40 ms)
  const float avg = s.window_ms / s.frames;
  const auto now = std::chrono::steady_clock::now();
  if (s.worst_frame_ms > 40.f && s.worst_frame_ms > 2.5f * avg && now - g_perf.last_log > std::chrono::seconds(5)) {
    g_perf.last_log = now;
    TRACE("[Perf] tiron: frame de " << s.worst_frame_ms << " ms (media " << avg << " ms, " << s.frames
                                    << " fps)  esperas a la GPU: " << s.stalls << " (" << s.stall_ms
                                    << " ms, la peor " << s.worst_stall_ms << " ms)  draws/frame "
                                    << s.draws / s.frames << "  resolves/frame " << s.resolves / s.frames);
  }
}

// ---------------------------------------------------------------------------- contador de FPS
namespace {
class FpsCounter final : public rex::ui::ImGuiDialog {
 public:
  explicit FpsCounter(rex::ui::ImGuiDrawer* d) : ImGuiDialog(d) {}

 protected:
  void OnDraw(ImGuiIO& io) override {
    DrawConfirm(io);
    const GraphicsConfig& g = Get().graphics;
    if (!g.show_fps) return;
    const ImGuiWindowFlags f = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                               ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                               ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoMove;
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - 12, 12), ImGuiCond_Always, ImVec2(1, 0));
    ImGui::SetNextWindowBgAlpha(0.45f);
    if (ImGui::Begin("##re_wor_fps", nullptr, f)) {
      const double fps = GameFps();
      const double target = std::max(30, BootGraphics().refresh_hz);
      const ImVec4 good(0.55f, 0.95f, 0.55f, 1), warn(1, 0.75f, 0.3f, 1);
      ImGui::TextColored(fps >= target * 0.92 ? good : warn, "%.0f FPS", fps);
      const auto& s = g_perf.s;
      if (g.perf_detail && s.frames) {
        const float avg = s.window_ms / s.frames;
        ImGui::TextDisabled("frame  %.1f ms  (peor %.1f)", avg, s.worst_frame_ms);
        ImGui::TextDisabled("GPU espera  %u  /  %.1f ms", s.stalls, s.stall_ms);
        ImGui::TextDisabled("draws %u  resolves %u", s.draws / s.frames, s.resolves / s.frames);
        const auto fp = frame_pacing::Get();
        if (fp.engine_speed > 0)
          ImGui::TextDisabled("velocidad del juego  %.2fx%s", fp.engine_speed, fp.fallback ? "  (protegido: 60)" : "");
        if (fp.song_speed > 0) ImGui::TextDisabled("cancion  %.2fx", fp.song_speed);
        ImGui::TextDisabled("%s", BootGraphics().backend == "vulkan" ? "Vulkan" : "Direct3D 12");
      }
    }
    ImGui::End();
  }

  // Aviso centrado mientras se prueba una resolucion / modo de pantalla
  void DrawConfirm(ImGuiIO& io) {
    DisplayConfirm c;
    {
      std::lock_guard lk(g_cmx);
      if (!g_confirm.active) return;
      c = g_confirm;
    }
    const double left =
        std::max(0.0, std::chrono::duration<double>(c.deadline - std::chrono::steady_clock::now()).count());
    const ImGuiWindowFlags f = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                               ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                               ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoMove;
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.30f), ImGuiCond_Always,
                            ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowBgAlpha(0.88f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(28, 20));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.f);
    if (ImGui::Begin("##re_wor_res_confirm", nullptr, f)) {
      ImGui::SetWindowFontScale(1.25f);
      ImGui::TextColored(ImVec4(1, 0.82f, 0.35f, 1), "Nueva resolucion: %s, %s", ResLabel(c.new_res),
                         ModeLabel(c.new_mode));
      ImGui::SetWindowFontScale(1.0f);
      ImGui::Spacing();
      ImGui::Text("Pulsa Enter o A para mantenerla.  Esc o B para volver a la anterior.");
      ImGui::Spacing();
      ImGui::ProgressBar(float(left / kConfirmSeconds), ImVec2(-1, 6), "");
      ImGui::TextDisabled("Si no confirmas, en %d s se vuelve a %s, %s.", int(std::ceil(left)),
                          ResLabel(c.prev_res), ModeLabel(c.prev_mode));
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
  }
};
}  // namespace

void CreateFpsCounter(rex::ui::ImGuiDrawer* drawer) {
  static FpsCounter* s_counter = nullptr;   // vive lo que viva el drawer
  if (!s_counter && drawer) s_counter = new FpsCounter(drawer);
}

}  // namespace re_wor::de
