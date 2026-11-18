// Ritmo de frames de la Definitive Edition (ver frame_pacing.hpp).
#include "de/frame_pacing.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <mutex>
#include <thread>
#ifdef _WIN32
#include <windows.h>
#endif

#include "generated/re_wor_pch.h"   // REX_LOAD_U32

#include "core/crash_handler.hpp"
#include "de/de_hooks.hpp"
#include "de/gfx_options.hpp"
#include "de/qb_bridge.hpp"
#include "de/render_sandbox.hpp"
#include <rex/graphics/draw_hook.h>
#include <string>

namespace re_wor::de::frame_pacing {

namespace {
using Clock = std::chrono::steady_clock;

constexpr uint32_t kFrameLenAddr = 0x8285684C;   // float: segundos de juego de este frame (GetDeltaTime)
constexpr uint32_t kSongTimeAddr = 0x82993A58;   // double: reloj de la cancion (IncrementSongTimer)
constexpr uint32_t kSlomoAddr = 0x82856844;      // float: camara lenta del motor (GetSlomo; 1,0 normal)
constexpr double kWindowSec = 2.0;

std::mutex g_mx;
Stats g_stats;

std::atomic<bool> g_fallback{false};
std::atomic<bool> g_unlocked_now{false};   // los candados estan quitados en el juego ahora mismo
int g_fallback_for_hz = 0;   // el Frame Rate con el que se activo la proteccion
int g_bad_windows = 0;

float LoadF32(uint8_t* base, uint32_t a) {
  const uint32_t v = REX_LOAD_U32(a);
  float f;
  std::memcpy(&f, &v, 4);
  return f;
}
double LoadF64(uint8_t* base, uint32_t a) {
  const uint64_t v = (uint64_t(REX_LOAD_U32(a)) << 32) | REX_LOAD_U32(a + 4);
  double d;
  std::memcpy(&d, &v, 8);
  return d;
}

// Frame Rate efectivo: el elegido, salvo que la proteccion haya vuelto a 60 para ese mismo valor
int EffectiveHz() {
  const int hz = FrameCapHz();
  if (g_fallback.load(std::memory_order_relaxed)) {
    if (hz == g_fallback_for_hz) return 60;
    g_fallback = false;   // el jugador eligio otro Frame Rate: se vuelve a intentar
    g_bad_windows = 0;
    TRACE("[FPS] Frame Rate cambiado a " << hz << ": se vuelve a probar sin candados");
  }
  return hz;
}

// Candados del motor (globales del juego; existen desde que arranca). Solo se escriben si no coinciden.
void SyncLocks(uint8_t* base, bool unlocked) {
  const int want = unlocked ? 0 : 1;
  g_unlocked_now = unlocked;
  for (const char* name : {"game_lock_framerate", "render_lock_framerate"}) {
    int v = -1;
    if (qb_bridge::ReadIntGlobal(name, base, &v) && v != want) {
      qb_bridge::WriteIntGlobal(name, base, want);
      TRACE("[FPS] " << name << " = " << want);
    }
  }
}
}  // namespace

namespace {
std::atomic<double> g_sleep_ms{0.0}, g_wait_ms{0.0};   // acumulados de la ventana actual
}
void NoteEngineWait(double ms) { g_wait_ms = g_wait_ms.load(std::memory_order_relaxed) + ms; }

void BeforeEngineFrame() {
  static Clock::time_point s_next{};
  const auto t_in = Clock::now();
  struct Acc { Clock::time_point t; ~Acc() { g_sleep_ms = g_sleep_ms.load(std::memory_order_relaxed) +
      std::chrono::duration<double, std::milli>(Clock::now() - t).count(); } } acc{t_in};
  const int hz = EffectiveHz();
  if (hz <= 60 || hz > 1000) {   // a 60 manda el VBlank, como en la consola
    s_next = {};
    return;
  }
  const auto period = std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(1.0 / hz));
  auto now = Clock::now();
  if (s_next > now) {
    const auto left = s_next - now;
    if (left > std::chrono::microseconds(1500)) std::this_thread::sleep_for(left - std::chrono::microseconds(1000));
    while (Clock::now() < s_next) {
#ifdef _WIN32
      YieldProcessor();
#else
      std::this_thread::yield();
#endif
    }
    now = Clock::now();
  }
  s_next = (now - s_next > period) ? now + period : s_next + period;   // si va atrasado no acumula deuda
}

void AfterEngineFrame(uint8_t* base) {
  if (!base) return;
  const int hz = EffectiveHz();
  const bool unlocked = hz > 60;

  static int s_tick = 0;
  if (BootDone() && (++s_tick % 30) == 0) SyncLocks(base, unlocked);

  // ---- medidas: ventana de 2 s
  static Clock::time_point s_start = Clock::now();
  static double s_engine = 0, s_song = 0, s_song_real = 0, s_prev_song = -1;
  static Clock::time_point s_prev = Clock::now();
  static uint64_t s_frames = 0;
  const auto now = Clock::now();
  const double real_dt = std::chrono::duration<double>(now - s_prev).count();
  s_prev = now;

  const float len = LoadF32(base, kFrameLenAddr);
  if (std::isfinite(len) && len > 0.f && len < 1.f) s_engine += len;
  ++s_frames;

  const double song = LoadF64(base, kSongTimeAddr);
  static Clock::time_point s_last_adv{};
  if (s_prev_song >= 0 && std::isfinite(song)) {
    const double d = song - s_prev_song;
    if (d > 0 && d < 0.5) s_last_adv = now;
    // cuenta todos los frames mientras la cancion esta en marcha (avanzo hace menos de 0,1 s), avance o no en este
    if (d >= 0 && d < 0.5 && real_dt < 0.5 && now - s_last_adv < std::chrono::milliseconds(100)) {
      s_song += d;
      s_song_real += real_dt;
    }
  }
  s_prev_song = std::isfinite(song) ? song : -1;

  const double win = std::chrono::duration<double>(now - s_start).count();
  if (win < kWindowSec) return;
  const double fps = s_frames / win;
  const double engine_speed = s_engine / win;
  const double song_speed = s_song_real > 0.5 ? s_song / s_song_real : 0.0;
  static int s_song_logs = 0;
  if (song_speed > 0 && s_song_logs < 600) {
    ++s_song_logs;
    int gl = -1, rl = -1;
    qb_bridge::ReadIntGlobal("game_lock_framerate", base, &gl);
    qb_bridge::ReadIntGlobal("render_lock_framerate", base, &rl);
    std::string gpu;
    if (const auto* g = sandbox::GpuStats(); g && g->window_ms > 0)
      gpu = " gpu: swaps=" + std::to_string(g->frames) + " esperas=" + std::to_string(g->stalls) + " (" +
            std::to_string(int(g->stall_ms)) + " ms) peor_frame=" + std::to_string(int(g->worst_frame_ms)) + " ms";
    const double per = s_frames ? 1.0 / double(s_frames) : 0.0;
    const double frame_ms = 1000.0 / std::max(fps, 1.0);
    const double sleep_ms = g_sleep_ms.load() * per, wait_ms = g_wait_ms.load() * per;
    TRACE("[FPS] en cancion: " << fps << " FPS (objetivo " << hz << "), juego " << engine_speed << "x, cancion "
                               << song_speed << "x, slomo " << LoadF32(base, kSlomoAddr) << ", candados " << gl << "/"
                               << rl << " | por frame " << frame_ms << " ms: limitador " << sleep_ms
                               << ", espera al hilo de render " << wait_ms << ", resto del motor "
                               << (frame_ms - sleep_ms - wait_ms) << gpu);
  }
  g_sleep_ms = 0.0;
  g_wait_ms = 0.0;
  s_start = now;
  s_engine = s_song = s_song_real = 0;
  s_frames = 0;

  // ---- proteccion: con FPS altos el juego tiene que ir a 1,00x
  // Solo cuenta la cancion (lo que se ve y se oye): la velocidad del motor baja sola en cargas y tirones (el motor
  // recorta los frames largos a proposito) y eso no es un fallo. Tienen que salir mal 3 ventanas seguidas (6 s).
  const float slomo_now = LoadF32(base, kSlomoAddr);
  const double want_song = (slomo_now > 0.f && slomo_now <= 4.f) ? double(slomo_now) : 1.0;
  if (unlocked && fps > 40 && song_speed > 0) {
    const bool bad = std::fabs(song_speed - want_song) > 0.1 * want_song;
    g_bad_windows = bad ? g_bad_windows + 1 : 0;
    if (bad) {
      TRACE("[FPS] cancion a " << song_speed << "x (debe ser " << want_song << "x) a " << fps << " FPS");
    }
    if (g_bad_windows >= 3) {
      g_fallback_for_hz = FrameCapHz();
      g_fallback = true;
      g_bad_windows = 0;
      SyncLocks(base, false);
      TRACE("[FPS] la velocidad no era 1,00x con " << g_fallback_for_hz
                                                   << " FPS: se vuelve a 60 FPS con los candados del motor");
    }
  } else {
    g_bad_windows = 0;
  }

  static int s_logged_hz = -1;
  if (unlocked && s_logged_hz != hz && fps > 40) {
    s_logged_hz = hz;
    TRACE("[FPS] " << hz << " FPS sin candados: " << fps << " FPS reales, velocidad del motor " << engine_speed
                   << "x" << (song_speed > 0 ? ", cancion " : "") << (song_speed > 0 ? std::to_string(song_speed) : ""));
  }

  std::lock_guard lk(g_mx);
  g_stats.target_hz = FrameCapHz();
  g_stats.unlocked = unlocked;
  g_stats.fallback = g_fallback.load();
  g_stats.engine_speed = engine_speed;
  g_stats.song_speed = song_speed;
}

// Reloj de la cancion a tiempo real (ver frame_pacing.hpp). Los dos ganchos comparten el instante anterior:
// si en un frame lo avanzan los dos, entre ambos suman exactamente el tiempo real transcurrido.
namespace {
void RealTimeSongClock(uint8_t* base, double before) {
  // Tiempo real acumulado desde la ultima vez que el juego avanzo el reloj. Hay frames en los que el juego no lo
  // avanza (suma 0): ese tiempo no se pierde, se suma en el siguiente avance. Si lleva mas de 0,1 s sin avanzar
  // (pausa, carga) se descarta, para no adelantar las notas al volver.
  static Clock::time_point s_prev{};
  static bool s_has_prev = false;
  static double s_pending = 0.0;
  const auto now = Clock::now();
  const double real_dt = s_has_prev ? std::chrono::duration<double>(now - s_prev).count() : -1.0;
  s_prev = now;
  s_has_prev = true;
  if (real_dt > 0.0 && real_dt < 0.25) s_pending += real_dt; else s_pending = 0.0;
  if (s_pending > 0.1) s_pending = 0.0;   // no avanza desde hace rato: pausa
  if (!base || !g_unlocked_now.load(std::memory_order_relaxed)) { s_pending = 0.0; return; }   // 60: como la consola
  const double after = LoadF64(base, kSongTimeAddr);
  const double step = after - before;
  if (step == 0.0) return;                          // este frame el juego no lo avanzo: el tiempo queda pendiente
  if (!(step > 0.0 && step < 0.25)) { s_pending = 0.0; return; }   // salto, cuenta atras, puesta a cero
  float slomo = LoadF32(base, kSlomoAddr);          // 1,0 normal; menos en camara lenta / modo practica
  if (!(slomo > 0.f && slomo <= 4.f)) slomo = 1.f;
  const double fixed = before + s_pending * double(slomo);
  s_pending = 0.0;
  uint64_t bits;
  std::memcpy(&bits, &fixed, 8);
  REX_STORE_U32(kSongTimeAddr, uint32_t(bits >> 32));
  REX_STORE_U32(kSongTimeAddr + 4, uint32_t(bits));
}
}  // namespace

void AdjustSongTimer(uint8_t* base, double before) { RealTimeSongClock(base, before); }
void AdjustNativeSongClock(uint8_t* base, double before) { RealTimeSongClock(base, before); }

Stats Get() {
  std::lock_guard lk(g_mx);
  return g_stats;
}

}  // namespace re_wor::de::frame_pacing
