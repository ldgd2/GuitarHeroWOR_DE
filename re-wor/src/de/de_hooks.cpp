// =============================================================================
// de_hooks.cpp - Hooks de la Definitive Edition (solo observan, no alteran).
// Cada hook llama a la funcion ORIGINAL con los mismos argumentos y devuelve
// el mismo resultado; solo registra informacion para el panel DE (F8 por defecto).
// (Si se compila con REWOR_BOOT_PROBES, las sondas definen estos mismos
//  simbolos y estos hooks se omiten.)
// =============================================================================

#include "de/de_hooks.hpp"

#include <atomic>
#include <chrono>
#include <cstring>
#include <string>

#include "core/crash_handler.hpp"
#include "de/de_config.hpp"
#include "de/edfx.hpp"
#include "de/mod_manager.hpp"
#include "generated/re_wor_pch.h"

namespace re_wor::de {

namespace {
std::atomic<uint64_t> g_frames{0};
std::atomic<double> g_fps{0.0};
std::atomic<bool> g_log_opens{false};
std::atomic<bool> g_trace_probes{false};
std::atomic<double> g_audio_rate{0.0};
std::atomic<double> g_logic_fps{0.0};
std::atomic<double> g_audio_ms{0.0};
}  // namespace

uint64_t GameFrameCount() {
  return g_frames.load();
}

double GameFps() {
  return g_fps.load();
}

double LogicFps() {
  return g_logic_fps.load();
}

double AudioMixRate() {
  return g_audio_rate.load();
}

double AudioMixMs() {
  return g_audio_ms.load();
}

void SetLogFileOpens(bool enabled) {
  g_log_opens = enabled;
}

void SetTraceProbes(bool enabled) {
  g_trace_probes = enabled;
}

}  // namespace re_wor::de

#ifndef REWOR_BOOT_PROBES

extern "C" void __imp__sub_82422E58(PPCContext& __restrict ctx, uint8_t* base);
namespace re_wor::de::nativefx {
void Tick(PPCContext& ctx, uint8_t* base);
}
namespace re_wor::de {
void NoteBootFile(const std::string& name);
void OnLogicFrameExtras(PPCContext& ctx, uint8_t* base);
}  // namespace re_wor::de
extern "C" void __imp__sub_822E5E10(PPCContext& __restrict ctx, uint8_t* base);

// FileStream::Open(nombre, flags, ...) -> stream* (0 = no encontrado)
extern "C" REX_FUNC(sub_82422E58) {
  const uint32_t name_addr = ctx.r3.u32;
  const uint32_t flags = ctx.r4.u32;
  std::string name = name_addr ? reinterpret_cast<const char*>(base + name_addr) : "(null)";
  __imp__sub_82422E58(ctx, base);
  const bool ok = ctx.r3.u32 != 0;
  re_wor::de::ModManager::Instance().RecordOpen(name, flags, ok);
  re_wor::de::NoteBootFile(name);
  if (ok) re_wor::de::edfx::OnFileOpen(name.c_str());
  if (re_wor::de::g_log_opens.load(std::memory_order_relaxed) || !ok) {
    TRACE("[FileOpen] " << (ok ? "OK   " : "FALLA") << " '" << name << "' flags=0x" << std::hex
          << flags << std::dec);
  }
}

// Bucle de frame del motor: cuenta frames y calcula los FPS reales del juego.
extern "C" REX_FUNC(sub_822E5E10) {
  using clock = std::chrono::steady_clock;
  static clock::time_point s_window_start = clock::now();
  static uint64_t s_window_frames = 0;

  re_wor::de::g_frames.fetch_add(1, std::memory_order_relaxed);
  if (++s_window_frames >= 30) {
    auto now = clock::now();
    double secs = std::chrono::duration<double>(now - s_window_start).count();
    if (secs > 0) re_wor::de::g_fps.store(s_window_frames / secs);
    static clock::time_point s_last_log = now;
    if (re_wor::de::g_trace_probes.load(std::memory_order_relaxed) &&
        std::chrono::duration<double>(now - s_last_log).count() >= 2.0) {
      s_last_log = now;
      TRACE("[GameFPS] motor=" << re_wor::de::g_fps.load() << " fps (frames reales del juego)");
    }
    s_window_start = now;
    s_window_frames = 0;
  }
  __imp__sub_822E5E10(ctx, base);
}

// Mezclador de audio del juego (envia cada bloque de 256 muestras al driver
// con XAudioSubmitRenderDriverFrame). Debe ejecutarse 187.5 veces/s
// (48000 Hz / 256). Si va por debajo, el audio -y el reloj de la cancion,
// que sigue al audio- avanza en camara lenta.
extern "C" void __imp__sub_8272E6F0(PPCContext& __restrict ctx, uint8_t* base);
extern "C" REX_FUNC(sub_8272E6F0) {
  using clock = std::chrono::steady_clock;
  static clock::time_point s_window_start = clock::now();
  static uint64_t s_calls = 0;
  static double s_busy = 0.0, s_max = 0.0;

  auto t0 = clock::now();
  __imp__sub_8272E6F0(ctx, base);
  auto t1 = clock::now();
  double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
  s_busy += ms;
  if (ms > s_max) s_max = ms;
  s_calls++;
  double secs = std::chrono::duration<double>(t1 - s_window_start).count();
  if (secs >= 2.0) {
    double rate = s_calls / secs;
    re_wor::de::g_audio_rate.store(rate);
    re_wor::de::g_audio_ms.store(s_busy / s_calls);
    if (re_wor::de::g_trace_probes.load(std::memory_order_relaxed))
      TRACE("[AudioProbe] mezclas/s=" << rate << " (ideal 187.5)  media=" << (s_busy / s_calls)
          << " ms  max=" << s_max << " ms  ocupacion=" << (100.0 * s_busy / (secs * 1000.0)) << "%");
    s_window_start = t1;
    s_calls = 0;
    s_busy = s_max = 0.0;
  }
}

// Frame LOGICO del motor (espera al hilo de render, mide dt con
// KeQueryPerformanceCounter y lo guarda en 0x829BB2E0 como double).
extern "C" void __imp__sub_8242AD60(PPCContext& __restrict ctx, uint8_t* base);
extern "C" REX_FUNC(sub_8242AD60) {
  using clock = std::chrono::steady_clock;
  static clock::time_point s_start = clock::now();
  static uint64_t s_frames = 0;
  static double s_dt_sum = 0.0, s_dt_max = 0.0, s_wall_max = 0.0;
  static clock::time_point s_prev = clock::now();
  static uint64_t s_tb_start = REX_QUERY_TIMEBASE();

  auto t0 = clock::now();
  double wall_ms = std::chrono::duration<double, std::milli>(t0 - s_prev).count();
  s_prev = t0;
  __imp__sub_8242AD60(ctx, base);
  re_wor::de::OnLogicFrameExtras(ctx, base);

  uint64_t bits = (uint64_t(REX_LOAD_U32(0x829BB2E0)) << 32) | REX_LOAD_U32(0x829BB2E4);
  double dt;
  std::memcpy(&dt, &bits, sizeof(dt));
  s_dt_sum += dt;
  if (dt > s_dt_max) s_dt_max = dt;
  if (wall_ms > s_wall_max) s_wall_max = wall_ms;
  s_frames++;
  double secs = std::chrono::duration<double>(clock::now() - s_start).count();
  if (secs >= 2.0) {
    re_wor::de::g_logic_fps.store(s_frames / secs);
    uint64_t tb_now = REX_QUERY_TIMEBASE();
    double guest_mhz = (tb_now - s_tb_start) / secs / 1e6;
    s_tb_start = tb_now;
    auto load_f64 = [&](uint32_t a) {
      uint64_t b = (uint64_t(REX_LOAD_U32(a)) << 32) | REX_LOAD_U32(a + 4);
      double v;
      std::memcpy(&v, &b, 8);
      return v;
    };
    uint64_t freq = (uint64_t(REX_LOAD_U32(0x8298BFC8)) << 32) | REX_LOAD_U32(0x8298BFCC);
    if (re_wor::de::g_trace_probes.load(std::memory_order_relaxed)) {
    TRACE("[GuestClock] mftb=" << guest_mhz << " MHz  freq_motor=" << freq << "  k=" << load_f64(0x82021B18)
          << "  factor_0x8298C0B0=" << load_f64(0x8298C0B0));
    TRACE("[LogicFrame] logica=" << (s_frames / secs) << " fps  render=" << re_wor::de::g_fps.load()
          << " fps  dt_motor_medio=" << (s_dt_sum / s_frames) << "  dt_max=" << s_dt_max
          << "  frame_real_max=" << s_wall_max << " ms  suma_dt=" << s_dt_sum << " en " << secs << " s");
    }
    s_start = clock::now();
    s_frames = 0;
    s_dt_sum = s_dt_max = s_wall_max = 0.0;
  }
}

#endif  // REWOR_BOOT_PROBES


// -----------------------------------------------------------------------------
// Saltar la intro (tecla F) y reloj de EDFX.
// Los videos se controlan con CFuncs del motor (tabla de CFuncs del ejecutable: nombre -> funcion):
//   PlayMovie 8251DE38, IsMoviePlaying 8251D7E8, WaitMovie 82515308, HasMovieStarted 825152E8,
//   WaitForAllMoviesToFinish 8251E210, WaitForMovieLoop 8251D678;
//   sub_825809A0 = parar todos los videos (lo que hace KillAllMovies por dentro, sin argumentos).
// Al pulsar F: NUNCA se bloquea PlayMovie (si el video no llega a empezar, el script de arranque espera para
// siempre a que empiece: asi se colgaba). El video arranca normal y medio segundo despues se paran todos los
// videos, igual que cuando terminan solos; durante 3 s mas los scripts que pregunten ven "ya termino".
// Si F se pulsa antes del video (pantalla legal), queda armada y se aplica cuando el video empieza.
// Tras llegar al menu (pak/band_lobby) la tecla ya no hace nada: los menus usan videos de fondo.
// (Antes aqui habia un hook de sub_8239E2A0 para EDFX, pero esa funcion busca ARRAYS (tipo 12), no
//  scripts: nunca veia los eventos. EDFX usa ahora el reloj de la cancion del motor.)
// -----------------------------------------------------------------------------
#if !defined(REWOR_BOOT_PROBES)

namespace {
using SkipClock = std::chrono::steady_clock;
std::atomic<bool> g_boot_done{false};
std::atomic<bool> g_movie_seen{false};
std::atomic<bool> g_skip_armed{false};
std::atomic<int64_t> g_kill_at{0};      // ms (steady_clock) en que hay que parar los videos; 0 = nada
std::atomic<int64_t> g_skip_until{0};   // hasta cuando los scripts ven "ningun video sonando"

int64_t NowMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(SkipClock::now().time_since_epoch()).count();
}
bool Skipping() { return NowMs() < g_skip_until.load(std::memory_order_relaxed); }
}  // namespace

namespace re_wor::de {
void RequestSkipIntro() {
  if (g_boot_done) return;
  if (!g_movie_seen) {
    g_skip_armed = true;
    TRACE("[SkipIntro] F pulsada antes del video: se saltara en cuanto empiece");
  } else {
    g_kill_at = NowMs();
    TRACE("[SkipIntro] F pulsada: parando videos de la intro");
  }
}
void NoteBootFile(const std::string& name) {
  if (!g_boot_done && name.find("band_lobby") != std::string::npos) {
    g_boot_done = true;
    g_skip_armed = false;
  }
}
}  // namespace re_wor::de

extern "C" void sub_825809A0(PPCContext& __restrict ctx, uint8_t* base);
#define REWOR_MOVIE_HOOK(addr, when_skipping)                                  \
  extern "C" void __imp__sub_##addr(PPCContext& __restrict ctx, uint8_t* base); \
  extern "C" REX_FUNC(sub_##addr) {                                            \
    if (Skipping()) {                                                          \
      ctx.r3.u64 = (when_skipping);                                            \
      return;                                                                  \
    }                                                                          \
    __imp__sub_##addr(ctx, base);                                              \
  }

// PlayMovie: siempre se reproduce; si F estaba armada, se programa el corte
extern "C" void __imp__sub_8251DE38(PPCContext& __restrict ctx, uint8_t* base);
extern "C" REX_FUNC(sub_8251DE38) {
  __imp__sub_8251DE38(ctx, base);
  if (!g_boot_done) {
    g_movie_seen = true;
    if (g_skip_armed.exchange(false)) {
      g_kill_at = NowMs() + 500;
      TRACE("[SkipIntro] video iniciado; se corta en 0.5 s");
    }
  }
}
REWOR_MOVIE_HOOK(8251D7E8, 0)  // IsMoviePlaying -> no
REWOR_MOVIE_HOOK(82515308, 1)  // WaitMovie -> no espera
REWOR_MOVIE_HOOK(8251E210, 1)  // WaitForAllMoviesToFinish -> no espera
REWOR_MOVIE_HOOK(8251D678, 1)  // WaitForMovieLoop -> no espera
#undef REWOR_MOVIE_HOOK

namespace re_wor::de {
// desde el frame logico (hilo del juego): parar los videos pendientes y leer el reloj de la cancion
void OnLogicFrameExtras(PPCContext& ctx, uint8_t* base) {
  const int64_t kill_at = g_kill_at.load();
  if (kill_at && NowMs() >= kill_at) {
    g_kill_at = 0;
    g_skip_until = NowMs() + 3000;
    PPCContext c = ctx;
    sub_825809A0(c, base);
    TRACE("[SkipIntro] videos parados");
  }
  edfx::OnLogicFrame(base);
  nativefx::Tick(ctx, base);   // filtros del editor con el ScreenFX del motor (solo el escenario)
}
}  // namespace re_wor::de
#endif
