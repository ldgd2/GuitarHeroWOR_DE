// SANDBOX DE RENDER (re-wor). Lo que se ve en EditorSong se dibuja igual en el juego con una pasada propia en la
// GPU del PC, sin depender de los pocos filtros ScreenFX del motor de GH.
//
// Como funciona:
//  1. El SDK (rexglue, D3D12) avisa de cada draw/resolve/swap (rex/graphics/draw_hook.h).
//  2. ENGANCHE: el Xenos dibuja en EDRAM y copia (resolve) la imagen a memoria. El escenario terminado se resuelve a
//     pantalla completa ANTES de dibujar la autopista y el HUD, que van en el resolve final del frame.
//  3. Justo antes de ese primer resolve, el SDK ejecuta re-wor/sandbox/sandbox.hlsl sobre esa imagen con los
//     parametros que calcula este archivo (port de EditorSong web/src/viewport/fx.js: la cadena de filtros CSS se
//     compone en una matriz de color con las formulas del W3C).
//  4. El .hlsl se recarga en caliente: se puede meter cualquier efecto sin recompilar el juego.
// Si algo falla (sin enganche, sin pasadas), todo vuelve solo al camino anterior (native_fx + capa ImGui).
#include "de/render_sandbox.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include <rex/graphics/draw_hook.h>

#include <windows.h>

#include "core/crash_handler.hpp"
#include "de/de_config.hpp"
#include "de/edfx.hpp"

namespace re_wor::de::sandbox {
namespace {

using Clock = std::chrono::steady_clock;
namespace fs = std::filesystem;
constexpr int kConsts = 72;   // 18 float4 (ver cbuffer Fx en sandbox.hlsl)
constexpr uint32_t kParticles = 640;

rex::graphics::DrawHooks* g_hooks = nullptr;
bool g_installed = false;
int g_tries = 0;

// ---------------------------------------------------------------- estado compartido (hilo logico <-> GPU)
std::mutex g_mx;
std::array<float, kConsts> g_logic_consts{};   // ultimo calculo del hilo logico
bool g_logic_any = false;                       // hay algo que dibujar
std::string g_src_pending;                      // HLSL nuevo leido del disco
bool g_src_dirty = false;

std::atomic<int64_t> g_last_pass_ms{0};         // ultima pasada hecha (ms de Clock)
std::atomic<bool> g_failed{false};              // no hay forma: se vuelve al camino anterior en esta sesion
std::atomic<int> g_song{0};                     // 1 si hay cancion en marcha (escenario)
std::atomic<bool> g_anchor_ok{false};           // el frame tiene un resolve del escenario antes del final

int64_t NowMs() { return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count(); }

fs::path ExeDir() {
  wchar_t b[MAX_PATH];
  GetModuleFileNameW(nullptr, b, MAX_PATH);
  return fs::path(b).parent_path();
}

// ---------------------------------------------------------------- hilo de la GPU
std::array<float, kConsts> g_gpu_consts{};
std::string g_src_live;
uint32_t g_src_version = 0;

// ENGANCHE: el Xenos dibuja en su EDRAM y "resuelve" (copia) a memoria. El escenario terminado se resuelve a pantalla
// completa y DESPUES el juego dibuja la autopista y el HUD y resuelve el frame final. La pasada va justo antes del
// PRIMER resolve de color a pantalla completa del frame, siempre que haya otro despues (el del frame final con HUD):
// asi lo copiado ya lleva el efecto y el HUD queda intacto. No hay calibracion: se deduce cada frame.
struct Res { uint32_t dest, w, h, sel, fmt, draws; };
std::vector<Res> g_frame_res;          // resolves de este frame
uint32_t g_full_w = 0, g_full_h = 0;   // tamano del frame final (frame anterior)
int g_full_prev = 0;                   // resolves a pantalla completa del frame anterior
int g_full_now = 0;                    // ... de este frame hasta ahora
int g_census_logs = 0;
int g_passes_frame = 0;

void AdoptLogicState() {
  std::lock_guard<std::mutex> lk(g_mx);
  g_gpu_consts = g_logic_consts;
  if (g_src_dirty) {
    g_src_dirty = false;
    g_src_live = g_src_pending;
    ++g_src_version;
  }
}

uint32_t OnDraw(const rex::graphics::DrawHookInfo&) { return 0; }

void BeforePass(const rex::graphics::SandboxPassInfo& info) {
  g_gpu_consts[0] = static_cast<float>(info.width);
  g_gpu_consts[1] = static_cast<float>(info.height);
  g_gpu_consts[68] = info.tex_width ? static_cast<float>(info.width) / info.tex_width : 1.f;
  g_gpu_consts[69] = info.tex_height ? static_cast<float>(info.height) / info.tex_height : 1.f;
  g_last_pass_ms = NowMs();
}

bool IsFull(uint32_t w, uint32_t h) {
  return g_full_w && w * 10 >= g_full_w * 9 && h * 10 >= g_full_h * 9;
}

uint32_t OnResolve(const rex::graphics::ResolveHookInfo& r) {
  if (g_frame_res.size() < 256)
    g_frame_res.push_back({r.dest_base, r.dest_pitch, r.dest_height, r.src_select, r.dest_format, r.draws_before});
  if (r.src_select >= 4 || !IsFull(r.dest_pitch, r.dest_height)) return 0;
  const int nth = g_full_now++;
  if (nth != 0 || g_full_prev < 2 || !g_song.load() || g_failed.load()) return 0;
  bool any;
  { std::lock_guard<std::mutex> lk(g_mx); any = g_logic_any; }
  if (!any) return 0;
  AdoptLogicState();
  if (g_src_live.empty()) return 0;
  g_hooks->hlsl = g_src_live.c_str();
  g_hooks->hlsl_version = g_src_version;
  g_hooks->constants = g_gpu_consts.data();
  g_hooks->constant_count = kConsts;
  g_hooks->particle_instances = kParticles;
  ++g_passes_frame;
  return rex::graphics::kDrawHookPassAfter;
}

void OnSwap() {
  // el resolve mas grande de color del frame es el frame final
  uint32_t mw = 0, mh = 0;
  for (const Res& r : g_frame_res)
    if (r.sel < 4 && uint64_t(r.w) * r.h > uint64_t(mw) * mh) { mw = r.w; mh = r.h; }
  if (mw) { g_full_w = mw; g_full_h = mh; }
  int full = 0;
  for (const Res& r : g_frame_res) if (r.sel < 4 && IsFull(r.w, r.h)) ++full;
  g_full_prev = full;
  const bool ok = full >= 2;
  if (ok != g_anchor_ok.load() && g_song.load()) {
    TRACE("[Sandbox] " << (ok ? "enganche listo: la pasada va antes del 1er resolve a pantalla completa (de "
                              : "sin enganche seguro (el frame no resuelve el escenario aparte del HUD): ")
                       << full << (ok ? ")" : " resolve(s) completos; efectos por el camino anterior"));
  }
  g_anchor_ok = ok;
  if (g_song.load() && g_census_logs < 2 && !g_frame_res.empty()) {
    static int wait = 0;
    if (++wait == 300 || wait == 3000) {
      ++g_census_logs;
      std::ostringstream o;
      o << "[Sandbox] resolves de un frame de la cancion (frame final " << g_full_w << "x" << g_full_h << ", pasadas "
        << g_passes_frame << "):";
      for (size_t k = 0; k < g_frame_res.size(); ++k) {
        const Res& r = g_frame_res[k];
        char b[128];
        std::snprintf(b, sizeof(b), "\n  %zu: %s %ux%u -> %08x fmt %u (tras %u draws)%s", k,
                      r.sel >= 4 ? "PROF " : "color", r.w, r.h, r.dest, r.fmt, r.draws,
                      r.sel < 4 && IsFull(r.w, r.h) ? " <- completo" : "");
        o << b;
      }
      TRACE(o.str());
    }
  }
  g_frame_res.clear();
  g_full_now = 0;
  g_passes_frame = 0;
}

void OnLog(const char* text) { TRACE(text); }

// ---------------------------------------------------------------- composicion de filtros CSS (hilo logico)
struct Aff {   // x' = M x + o
  float m[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
  float o[3] = {0, 0, 0};
  void Then(const float n[3][3], const float* no = nullptr) {   // aplicar n DESPUES de lo que ya hay
    Aff r;
    for (int i = 0; i < 3; ++i) {
      for (int j = 0; j < 3; ++j) { float v = 0; for (int k = 0; k < 3; ++k) v += n[i][k] * m[k][j]; r.m[i][j] = v; }
      float v = no ? no[i] : 0;
      for (int k = 0; k < 3; ++k) v += n[i][k] * o[k];
      r.o[i] = v;
    }
    *this = r;
  }
  void Gray(float a) {
    const float s = 1 - std::clamp(a, 0.f, 1.f);
    const float n[3][3] = {{0.2126f + 0.7874f * s, 0.7152f - 0.7152f * s, 0.0722f - 0.0722f * s},
                           {0.2126f - 0.2126f * s, 0.7152f + 0.2848f * s, 0.0722f - 0.0722f * s},
                           {0.2126f - 0.2126f * s, 0.7152f - 0.7152f * s, 0.0722f + 0.9278f * s}};
    Then(n);
  }
  void Sepia(float a) {
    const float s = 1 - std::clamp(a, 0.f, 1.f);
    const float n[3][3] = {{0.393f + 0.607f * s, 0.769f - 0.769f * s, 0.189f - 0.189f * s},
                           {0.349f - 0.349f * s, 0.686f + 0.314f * s, 0.168f - 0.168f * s},
                           {0.272f - 0.272f * s, 0.534f - 0.534f * s, 0.131f + 0.869f * s}};
    Then(n);
  }
  void Sat(float s) {
    s = std::max(0.f, s);
    const float n[3][3] = {{0.213f + 0.787f * s, 0.715f - 0.715f * s, 0.072f - 0.072f * s},
                           {0.213f - 0.213f * s, 0.715f + 0.285f * s, 0.072f - 0.072f * s},
                           {0.213f - 0.213f * s, 0.715f - 0.715f * s, 0.072f + 0.928f * s}};
    Then(n);
  }
  void Hue(float deg) {
    const float a = deg * 0.0174532925f, c = std::cos(a), s = std::sin(a);
    const float n[3][3] = {{0.213f + c * 0.787f - s * 0.213f, 0.715f - c * 0.715f - s * 0.715f, 0.072f - c * 0.072f + s * 0.928f},
                           {0.213f - c * 0.213f + s * 0.143f, 0.715f + c * 0.285f + s * 0.140f, 0.072f - c * 0.072f - s * 0.283f},
                           {0.213f - c * 0.213f - s * 0.787f, 0.715f - c * 0.715f + s * 0.715f, 0.072f + c * 0.928f + s * 0.072f}};
    Then(n);
  }
  void Bright(float b) {
    b = std::max(0.f, b);
    const float n[3][3] = {{b, 0, 0}, {0, b, 0}, {0, 0, b}};
    Then(n);
  }
  void Contrast(float c) {
    c = std::max(0.f, c);
    const float n[3][3] = {{c, 0, 0}, {0, c, 0}, {0, 0, c}};
    const float no[3] = {0.5f - 0.5f * c, 0.5f - 0.5f * c, 0.5f - 0.5f * c};
    Then(n, no);
  }
};

// LOOK_CSS de fx.js (mismo orden). Operaciones: G gris, S sepia, H tono (grados) -> valor a*k;
// T saturacion, C contraste, B brillo -> valor 1 + a*k.
struct LookOp { char op; float a; };
struct Look { const char* name; std::vector<LookOp> ops; };
const std::vector<Look>& Looks() {
  static const std::vector<Look> L = {
      {"teal", {{'S', .2f}, {'H', -15}, {'T', .25f}, {'C', .12f}}},
      {"blockbuster", {{'H', -10}, {'T', -.15f}, {'C', .2f}, {'B', -.03f}}},
      {"bleach", {{'T', -.55f}, {'C', .4f}, {'B', .05f}}},
      {"noir", {{'G', 1}, {'C', .55f}, {'B', -.05f}}},
      {"dramatic", {{'T', -.2f}, {'C', .45f}, {'B', -.05f}}},
      {"film", {{'S', .12f}, {'C', .1f}, {'T', .1f}}},
      {"kodak", {{'S', .15f}, {'T', .25f}, {'C', .12f}}},
      {"polaroid", {{'S', .1f}, {'T', -.1f}, {'B', .06f}, {'C', -.05f}}},
      {"retro70", {{'S', .3f}, {'T', -.25f}, {'C', -.12f}, {'B', .04f}}},
      {"technicolor", {{'T', .5f}, {'C', .15f}}},
      {"lomo", {{'T', .4f}, {'C', .3f}}},
      {"vaporwave", {{'H', -25}, {'T', .25f}, {'S', .12f}, {'B', .05f}}},
      {"miami", {{'H', -20}, {'T', .35f}, {'B', .04f}}},
      {"cyberpunk", {{'H', -35}, {'T', .3f}, {'C', .2f}}},
      {"sunset", {{'S', .35f}, {'T', .15f}, {'B', .04f}}},
      {"golden", {{'S', .4f}, {'T', .05f}, {'B', .08f}}},
      {"desert", {{'S', .55f}, {'T', .1f}, {'C', .1f}}},
      {"ice", {{'H', 10}, {'T', -.3f}, {'B', .08f}, {'C', .1f}}},
      {"toxic", {{'S', .3f}, {'H', 45}, {'T', .3f}, {'C', .2f}}},
      {"blood", {{'S', .4f}, {'H', -25}, {'T', .6f}, {'C', .3f}, {'B', -.05f}}},
      {"matrix", {{'S', .3f}, {'H', 65}, {'T', .3f}, {'C', .15f}}},
      {"night", {{'G', 1}, {'S', 1}, {'H', 60}, {'T', 2}, {'B', .25f}}},
      {"infrared", {{'H', 150}, {'T', .2f}}},
      {"pastel", {{'T', -.4f}, {'B', .12f}, {'C', -.15f}}},
      {"dream", {{'B', .12f}, {'C', -.2f}, {'T', -.2f}}},
      {"sepia", {{'S', .9f}}},
  };
  return L;
}

float Smooth(float k) { k = std::clamp(k, 0.f, 1.f); return k * k * (3 - 2 * k); }
float Srgb(float l) {   // los colores del editor llegan en lineal (THREE.Color)
  l = std::clamp(l, 0.f, 1.f);
  return l <= 0.0031308f ? 12.92f * l : 1.055f * std::pow(l, 1 / 2.4f) - 0.055f;
}

// Calcula las constantes de sandbox.hlsl a partir de los efectos activos. Devuelve si hay algo que dibujar.
bool Compose(const std::vector<edfx::FxState>& list, float T, std::array<float, kConsts>& c) {
  std::map<std::string, const edfx::FxState*> fx;
  for (const auto& f : list) fx[f.type] = &f;
  auto has = [&](const char* n) { return fx.count(n) != 0; };
  auto g = [&](const char* n) { auto it = fx.find(n); return it == fx.end() ? 0.f : it->second->k; };
  auto P = [&](const char* n, const char* p, float d) { auto it = fx.find(n); return it == fx.end() ? d : it->second->P(p, d); };
  auto el = [&](const char* n) { return fx.at(n)->el; };
  auto left = [&](const char* n) { return fx.at(n)->left; };
  c.fill(0);
  bool any = false;
  auto mark = [&](float v) { if (v != 0) any = true; return v; };

  // ---- look del jugador (pestana Graficos): va primero, los efectos del editor se aplican encima ----
  const GraphicsConfig& U = Get().graphics;
  if (U.LookActive()) any = true;

  // ---- cadena de filtros (mismo orden que `filt` en fx.js) ----
  Aff A;
  float blur2 = 0;
  if (U.brightness != 0) A.Bright(std::pow(2.f, .7f * U.brightness));
  if (U.contrast != 0) A.Contrast(1 + .5f * U.contrast);
  if (U.saturation != 0) A.Sat(1 + U.saturation);
  if (U.blur > 0) { const float b = 1.5f * U.blur; blur2 += b * b; }
  if (has("bw")) { A.Gray(g("bw")); A.Contrast(1 + P("bw", "contrast", .15f) * g("bw")); any = true; }
  if (has("exposure")) { A.Bright(std::pow(2.f, P("exposure", "stops", .6f) * g("exposure"))); any = true; }
  if (has("saturate")) { A.Sat(1 + P("saturate", "amount", .8f) * g("saturate")); any = true; }
  if (has("contrast")) { A.Contrast(1 + P("contrast", "amount", .6f) * g("contrast")); any = true; }
  if (has("blur")) { const float b = 2 * g("blur"); blur2 += b * b; }
  if (has("glow") && P("glow", "bright", 0) > 0) { A.Bright(1 + .25f * g("glow") * P("glow", "bright", 0)); any = true; }
  { const float b = .8f * std::max({g("motionblur"), g("tiltshift"), g("rackfocus")}); blur2 += b * b; }
  if (has("hueshift")) { A.Hue(P("hueshift", "deg", 120) * g("hueshift")); any = true; }
  if (has("rainbow")) { A.Hue(std::fmod(T * 72 * P("rainbow", "speed", 1), 360.f) * g("rainbow")); any = true; }
  if (has("flicker")) {
    const float s = P("flicker", "speed", 1);
    A.Bright(1 - .35f * g("flicker") * (.5f + .5f * std::sin(T * 47 * s) * std::sin(T * 13.3f * s)));
    any = true;
  }
  if (has("godrays")) { A.Bright(1 + .12f * g("godrays")); any = true; }
  for (const auto& L : Looks()) {
    if (!has(L.name)) continue;
    const float k = g(L.name);
    any = true;
    for (const auto& op : L.ops) {
      switch (op.op) {
        case 'G': A.Gray(op.a * k); break;
        case 'S': A.Sepia(op.a * k); break;
        case 'H': A.Hue(op.a * k); break;
        case 'T': A.Sat(1 + op.a * k); break;
        case 'C': A.Contrast(1 + op.a * k); break;
        case 'B': A.Bright(1 + op.a * k); break;
      }
    }
  }
  if (has("grade")) {
    const float k = g("grade");
    A.Bright(std::pow(2.f, P("grade", "exposure", 0) * k));
    A.Contrast(1 + (P("grade", "contrast", 1) - 1) * k);
    A.Sat(1 + (P("grade", "saturation", 1) - 1) * k);
    A.Hue(P("grade", "hue", 0) * k + 12 * P("grade", "tint", 0) * k);
    A.Sepia(std::max(0.f, .3f * P("grade", "temperature", 0) * k));
    any = true;
  }
  if (has("focus")) { const float b = 1.2f * g("focus") * P("focus", "strength", 1); blur2 += b * b; }
  for (int i = 0; i < 3; ++i) { for (int j = 0; j < 3; ++j) c[4 + 4 * i + j] = A.m[i][j]; c[4 + 4 * i + 3] = A.o[i]; }
  c[16] = mark(std::sqrt(blur2));
  c[17] = mark(g("chroma"));
  // resplandor
  const float gl = std::max({has("glow") ? g("glow") * P("glow", "intensity", 1) : 0.f,
                             has("bloom") ? g("bloom") * P("bloom", "intensity", 1) * .7f : 0.f,
                             .6f * std::max({g("vaporwave"), g("cyberpunk"), g("miami"), g("golden"), g("dream")})});
  c[18] = mark(gl);
  c[19] = has("glow") ? P("glow", "threshold", .75f) : has("bloom") ? P("bloom", "threshold", .75f) : .75f;
  {   // bloom (solo luces fuertes) y glow (umbral bajo, mas suave) del jugador
    const float ub = .8f * U.bloom, ug = .6f * U.glow, uk = std::max(ub, ug);
    if (uk > c[18]) { c[18] = uk; c[19] = (ub * .78f + ug * .45f) / (ub + ug); }
  }
  // vineta
  const float vig = std::max({g("vignette"), g("night"), .5f * g("dream"), P("contrast", "vig", .6f) * g("contrast"),
                              .7f * g("bleach"), .85f * g("noir"), .95f * g("lomo"), .8f * g("blood"), .7f * g("dramatic")});
  const float vsize = has("vignette") ? P("vignette", "size", .5f) : .5f, vsoft = has("vignette") ? P("vignette", "soft", .5f) : .5f;
  const float dg = g("danger") * (.6f + .4f * std::sin(T * 6));
  const float inner = std::max(5.f, 70 - 60 * vsize), outer = std::min(160.f, inner + 20 + 60 * vsoft);
  c[20] = mark(std::max({vig, dg, .8f * U.vignette})); c[21] = inner; c[22] = outer; c[23] = dg > 0 ? 1.f : 0.f;
  if (has("vignette")) { c[24] = Srgb(P("vignette", "colr", 0)); c[25] = Srgb(P("vignette", "colg", 0)); c[26] = Srgb(P("vignette", "colb", 0)); }
  // franjas de cine
  if (has("letterbox")) {
    const float ratio = P("letterbox", "ratio", 2.39f);
    c[27] = mark(std::min(.45f, std::max(0.f, (1 - (16.f / 9.f) / ratio) / 2)) * g("letterbox") *
                 Smooth(std::min({1.f, el("letterbox") / .4f, left("letterbox") / .4f})));
  }
  // tinte
  if (has("tint")) {
    c[28] = Srgb(P("tint", "colr", 1)); c[29] = Srgb(P("tint", "colg", .54f)); c[30] = Srgb(P("tint", "colb", .19f));
    c[31] = mark(.45f * g("tint") * P("tint", "mix", .5f));
  } else if (has("cold")) {
    c[28] = 40 / 255.f; c[29] = 110 / 255.f; c[30] = 1; c[31] = mark(.16f * g("cold") * P("cold", "amount", 1));
  } else if (has("warm")) {
    c[28] = 1; c[29] = 130 / 255.f; c[30] = 30 / 255.f; c[31] = mark(.15f * g("warm") * P("warm", "amount", 1));
  }
  // destellos
  float flash = 0, strobe = 0, white = 0, red = 0;
  if (has("flash")) flash = std::min(1.f, g("flash") * P("flash", "power", 2.5f) / 2.5f) * std::max(0.f, 1 - el("flash") / std::max(.05f, P("flash", "len", .25f)));
  if (has("strobe")) strobe = g("strobe") * ((static_cast<int>(std::floor(el("strobe") * 2 * P("strobe", "rate", 8))) % 2) ? 0.f : .85f);
  if (has("lightning")) { const float ph = std::fmod(el("lightning") / std::max(.2f, P("lightning", "every", 1.6f)), 1.f); if (ph < .12f) flash = std::max(flash, g("lightning") * (.12f - ph) / .12f); }
  if (has("fadewhite")) white = g("fadewhite") * Smooth(el("fadewhite") / std::max(.05f, el("fadewhite") + left("fadewhite")));
  if (has("redflash")) red = g("redflash") * std::max(0.f, 1 - el("redflash") / .6f);
  const float fl = std::max({flash, strobe, white, red});
  if (red > std::max({flash, strobe, white})) { c[32] = 1; c[33] = 24 / 255.f; c[34] = 48 / 255.f; }
  else { c[32] = c[33] = c[34] = 1; }
  c[35] = mark(fl);
  // fundidos
  float fade = g("blackout");
  if (has("fadein")) fade = std::max(fade, g("fadein") * (1 - std::min(1.f, el("fadein") / .8f)));
  if (has("fadeout")) fade = std::max(fade, g("fadeout") * (1 - std::min(1.f, left("fadeout") / .8f)));
  c[36] = mark(fade);
  c[37] = mark(g("rainbow")); c[38] = mark(g("scanlines")); c[39] = mark(std::max(g("grain"), .6f * U.grain));
  c[40] = mark(g("vhs")); c[41] = mark(g("heartbeat")); c[42] = mark(g("spotlight")); c[43] = mark(g("iris"));
  if (has("iris")) c[44] = Smooth(std::min({1.f, el("iris") / .6f, left("iris") / .6f}));
  c[45] = mark(g("lightning")); c[46] = has("lightning") ? el("lightning") : 0;
  // transformacion 2D de la imagen (en el editor es CSS sobre el monitor)
  float rot = 0, sc = 1, tx = 0, ty = 0;
  if (has("dutch")) rot += P("dutch", "angle", 15) * g("dutch") * Smooth(std::min({1.f, el("dutch") / .5f, left("dutch") / .5f}));
  if (has("handheld")) {
    const float s = P("handheld", "speed", 1), k = g("handheld");
    rot += .7f * k * std::sin(T * s * .9f + 6); tx += .6f * k * std::sin(T * s * 1.3f + 4); ty += .5f * k * std::sin(T * s * 1.1f + 5);
  }
  if (has("zoompunch")) {
    const float e = el("zoompunch") < .06f ? el("zoompunch") / .06f : std::max(0.f, 1 - (el("zoompunch") - .06f) / std::max(.05f, P("zoompunch", "len", .6f)));
    sc *= 1 + P("zoompunch", "zoom", .3f) * g("zoompunch") * e;
  }
  if (has("vertigo")) sc *= 1 - .55f * P("vertigo", "amount", .45f) * g("vertigo") * std::min(1.f, el("vertigo") / std::max(.3f, el("vertigo") + left("vertigo")));
  c[48] = mark(rot); c[49] = sc; c[50] = mark(tx); c[51] = mark(ty);
  if (sc != 1) any = true;
  // particulas
  const char* pk[6] = {"smoke", "rain", "snow", "confetti", "sparks", "lasers"};
  for (int i = 0; i < 6; ++i) {
    c[52 + i] = mark(g(pk[i]));
    if (i < 4 && has(pk[i])) c[60 + i] = el(pk[i]);
  }
  if (has("sparks")) c[64] = el("sparks");
  c[2] = T;
  c[3] = static_cast<float>(kParticles);
  return any;
}

// ---------------------------------------------------------------- shader en disco (recarga en caliente)
fs::path g_src_path;
fs::file_time_type g_src_time{};
Clock::time_point g_next_src_check{};

void CheckShaderFile() {
  if (Clock::now() < g_next_src_check) return;
  g_next_src_check = Clock::now() + std::chrono::seconds(1);
  if (g_src_path.empty()) {
    const fs::path exe = ExeDir();
    for (const fs::path& p : {exe / "sandbox" / "sandbox.hlsl", exe / ".." / ".." / ".." / "sandbox" / "sandbox.hlsl"}) {
      std::error_code ec;
      if (fs::exists(p, ec)) { g_src_path = fs::weakly_canonical(p, ec); break; }
    }
    if (g_src_path.empty()) {
      static bool warned = false;
      if (!warned) { warned = true; TRACE("[Sandbox] no se encuentra sandbox/sandbox.hlsl: sin pasada sandbox"); }
      return;
    }
    TRACE("[Sandbox] shader: " << g_src_path.string() << " (se recarga al guardarlo)");
  }
  std::error_code ec;
  const auto t = fs::last_write_time(g_src_path, ec);
  if (ec || t == g_src_time) return;
  g_src_time = t;
  std::ifstream f(g_src_path, std::ios::binary);
  std::stringstream ss;
  ss << f.rdbuf();
  std::lock_guard<std::mutex> lk(g_mx);
  g_src_pending = ss.str();
  g_src_dirty = true;
}

}  // namespace

// La GPU es un plugin (rexgpu-xenos*.dll) que el runtime carga en tiempo de ejecucion: los ganchos se piden a la DLL.
void Install() {
  if (g_installed || g_tries > 600) return;
  ++g_tries;
  using GetHooksFn = rex::graphics::DrawHooks* (*)();
  for (const char* dll : {"rexgpu-xenosrd.dll", "rexgpu-xenos.dll", "rexgpu-xenosd.dll"}) {
    HMODULE m = GetModuleHandleA(dll);
    if (!m) continue;
    auto fn = reinterpret_cast<GetHooksFn>(GetProcAddress(m, "rex_gpu_draw_hooks"));
    if (!fn) {
      TRACE("[Sandbox] " << dll << " no tiene rex_gpu_draw_hooks: es una DLL de GPU vieja (recompila con build.bat)");
      g_tries = 1000;
      return;
    }
    g_hooks = fn();
    g_hooks->draw = &OnDraw;
    g_hooks->resolve = &OnResolve;
    g_hooks->swap = &OnSwap;
    g_hooks->log = &OnLog;
    g_hooks->before_pass = &BeforePass;
    g_installed = true;
    std::error_code ec;
    fs::remove(ExeDir() / "sandbox_marker.txt", ec);   // del metodo viejo (marcador): ya no se usa
    TRACE("[Sandbox] gancho de render instalado en " << dll << " (enganche: resolve del escenario antes del HUD)");
    return;
  }
}

void OnLogicFrame() {
  if (!g_installed) { Install(); if (!g_installed) return; }
  CheckShaderFile();
  const double ms = edfx::SongMsNow();
  const auto fx = edfx::ActiveFxNow();
  const bool song = edfx::SongPlaying() && ms > 1000;
  g_song = song ? 1 : 0;
  std::array<float, kConsts> c;
  const bool any = song && Compose(fx, static_cast<float>(ms / 1000.0), c);
  std::lock_guard<std::mutex> lk(g_mx);
  if (any) g_logic_consts = c;
  g_logic_any = any;
}

NativeMode Mode() {
  if (!g_installed || g_failed.load() || !g_anchor_ok.load()) return NativeMode::kNormal;
  return NativeMode::kSandbox;
}

bool DrawsImage() {
  if (!g_installed || g_failed.load() || !g_anchor_ok.load()) return false;
  bool any;
  { std::lock_guard<std::mutex> lk(g_mx); any = g_logic_any; }
  // con efectos, solo si las pasadas estan llegando de verdad (si no, vuelve la capa vieja)
  return !any || NowMs() - g_last_pass_ms.load() < 1500;
}

}  // namespace re_wor::de::sandbox
