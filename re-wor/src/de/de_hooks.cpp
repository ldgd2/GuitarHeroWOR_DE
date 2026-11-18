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
#include <thread>
#include <cstring>
#include <string>

#ifdef _WIN32
#include <windows.h>
#endif

#include "core/crash_handler.hpp"
#include "de/de_config.hpp"
#include "de/edfx.hpp"
#include "de/ui3d.hpp"
#include "de/render_sandbox.hpp"
#include "de/cam_watch.hpp"
#include "de/d3d_tap.hpp"
#include "de/mod_manager.hpp"
#include "de/qb_bridge.hpp"
#include "de/frame_pacing.hpp"
#include "de/gfx_options.hpp"
#include "nativegfx/device/device_tracker.hpp"
#include "generated/re_wor_pch.h"

namespace re_wor::de {

namespace {
std::atomic<uint64_t> g_frames{0};
std::atomic<double> g_fps{0.0};
std::atomic<bool> g_log_opens{false};
std::atomic<bool> g_trace_probes{false};
std::atomic<double> g_audio_rate{0.0};
std::atomic<double> g_logic_fps{0.0};
std::atomic<double> g_dt_rate{0.0};   // segundos de juego por segundo real (1 = velocidad normal)
std::atomic<double> g_dt_avg{0.0};    // dt medio que usa el motor
std::atomic<double> g_audio_ms{0.0};
}  // namespace

uint64_t GameFrameCount() {
  return g_frames.load();
}

double GameFps() {
  return g_fps.load();
}

double EngineTimeRate() { return g_dt_rate.load(); }
double EngineDtAvg() { return g_dt_avg.load(); }

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
  re_wor::de::d3dtap::FlushPending(base);   // la ultima llamada del D3D del frame
  re_wor::de::framecap::OnSwap(base);   // captura de frames (diagnostico, REWOR_FRAMECAP=1)
  re_wor::nativegfx::DeviceTracker::Instance().OnSwap();
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

// Bucle principal del motor (sub_823F2A88): 8242AD60 (tiempo/espera al render) -> ... -> 823F2710 (tareas: scripts,
// camaras...) -> ... -> 82434D40 (cierre del frame).
extern "C" void __imp__sub_823F2710(PPCContext& __restrict ctx, uint8_t* base);
extern "C" REX_FUNC(sub_823F2710) {
  __imp__sub_823F2710(ctx, base);
  re_wor::de::edfx::ReapplyCamera(base, 0);
}

// Reloj de las canciones: el script suma 1/60 s por frame con IncrementSongTimer (0x823D0BC8; medido: a 144 FPS
// sin candados la cancion iba a 2,41x). frame_pacing lo pasa a tiempo real cuando los FPS van por encima de 60.
extern "C" void __imp__sub_823D0BC8(PPCContext& __restrict ctx, uint8_t* base);
extern "C" REX_FUNC(sub_823D0BC8) {
  const uint64_t bits = (uint64_t(REX_LOAD_U32(0x82993A58)) << 32) | REX_LOAD_U32(0x82993A5C);
  double before;
  std::memcpy(&before, &bits, 8);
  __imp__sub_823D0BC8(ctx, base);
  re_wor::de::frame_pacing::AdjustSongTimer(base, before);
}
// Reloj nativo de la cancion: sub_823D1490 (lo llama sub_821E51A0 cada frame) suma frames x 1/60 s al reloj
// 0x82993A58. Es el que de verdad mueve la cancion (a 144 FPS iba a 2,41x).
extern "C" void __imp__sub_823D1490(PPCContext& __restrict ctx, uint8_t* base);
extern "C" REX_FUNC(sub_823D1490) {
  const uint64_t bits = (uint64_t(REX_LOAD_U32(0x82993A58)) << 32) | REX_LOAD_U32(0x82993A5C);
  double before;
  std::memcpy(&before, &bits, 8);
  __imp__sub_823D1490(ctx, base);
  re_wor::de::frame_pacing::AdjustNativeSongClock(base, before);
}

// =============================================================================
// Parche en Memoria de Limites de Motor Xbox 360 (In-Memory Engine Patch)
// =============================================================================
// Neversoft diseno el motor para los 512 MB de la Xbox 360, recortando
// distancias de dibujado (default_far_plane_distance = 300), buffers de nodos,
// oclusores (cull_max_occluder = 256) y luces. En planos generales de estadios
// estos buffers se desbordan, provocando caidas severas de FPS y desaparicion de objetos.
//
// Para no modificar ni corromper los archivos de fabrica (.qb.xen) y que el proyecto
// funcione limpiamente para cualquiera, interceptamos LoadFile (sub_824DA068).
// Cuando el juego carga 'scripts/engine/engine_params.qb.xen', sobreescribimos
// en caliente en RAM los 22 parametros con los valores ampliados de PC de 64-bit.
// =============================================================================

namespace {
void PatchEngineParamsInMemory(uint8_t* data, size_t size) {
  if (!data || size < 1500) return;

  struct PatchInt { uint32_t key; uint32_t val; };
  static const PatchInt kIntPatches[] = {
      {0x4b642ed1, 1500},      // default_far_plane_distance
      {0x877c2aaf, 1500},      // cull_max_instances
      {0xc1331ede, 3500},      // cull_max_geom
      {0x8712ac22, 512},       // cull_max_occluder
      {0x64f3df41, 32},        // max_streams
      {0x5896fb30, 3072},      // max_sound_entries
      {0x4f90c3e4, 128},       // num_sound_instances
      {0xe50baccf, 2048},      // pool_text_instances
      {0xae3cd7c4, 25165824},  // fmod_memory_size
      {0x80f1090c, 40000},     // cpu_skinning_buffer_size
      {0x9ffd6a92, 15000},     // cpu_skin_wrap_buffer_size
      {0xd71f4e67, 600},       // node_instances_buff
      {0xc595b0e7, 500},       // node_statics_buff
      {0xc7c8e585, 512},       // node_sort_buff
      {0xdd014c76, 4000},      // node_misc_buff
      {0x862259a1, 600},       // node_2d_buff
      {0x3a77b43a, 2500},      // node_particle_buff
      {0xca2f1e1c, 512},       // node_particle_node
      {0xef2aeaf0, 300},       // node_light_buff
      // target_ntsc_framerate y gpu_vsync_time_interval NO se tocan: son la temporizacion del motor y los FPS
      // altos los gestiona frame_pacing sin cambiar la velocidad del juego.
  };

  int applied = 0;
  for (const auto& p : kIntPatches) {
    uint8_t key_bytes[4] = {
        uint8_t(p.key >> 24), uint8_t(p.key >> 16),
        uint8_t(p.key >> 8),  uint8_t(p.key)
    };
    for (size_t i = 0; i + 8 <= size; i += 4) {
      if (std::memcmp(data + i, key_bytes, 4) == 0) {
        data[i + 4] = uint8_t(p.val >> 24);
        data[i + 5] = uint8_t(p.val >> 16);
        data[i + 6] = uint8_t(p.val >> 8);
        data[i + 7] = uint8_t(p.val);
        applied++;
        break;
      }
    }
  }

  // cull_smallobj (float 0.000001f en BE IEEE-754: 0x358637bd)
  const uint32_t smallobj_key = 0x5ca62f8f;
  const uint32_t smallobj_val = 0x358637bd;
  uint8_t so_key[4] = {
      uint8_t(smallobj_key >> 24), uint8_t(smallobj_key >> 16),
      uint8_t(smallobj_key >> 8),  uint8_t(smallobj_key)
  };
  for (size_t i = 0; i + 8 <= size; i += 4) {
    if (std::memcmp(data + i, so_key, 4) == 0) {
      data[i + 4] = uint8_t(smallobj_val >> 24);
      data[i + 5] = uint8_t(smallobj_val >> 16);
      data[i + 6] = uint8_t(smallobj_val >> 8);
      data[i + 7] = uint8_t(smallobj_val);
      applied++;
      break;
    }
  }

  // Draw distance floats [600.0, 1000.0] -> [1800.0, 3500.0]
  uint8_t old_draw[8] = {0x44, 0x16, 0x00, 0x00, 0x44, 0x7a, 0x00, 0x00};
  uint8_t new_draw[8] = {0x44, 0xe1, 0x00, 0x00, 0x45, 0x5a, 0xc0, 0x00};
  for (size_t i = 0; i + 8 <= size; i += 4) {
    if (std::memcmp(data + i, old_draw, 8) == 0) {
      std::memcpy(data + i, new_draw, 8);
      applied++;
      break;
    }
  }

  TRACE("[DE::Hook] In-memory patch para engine_params.qb.xen aplicado: " << applied << "/20 limites mejorados en RAM!");
}
}  // namespace

extern "C" void __imp__sub_824DA068(PPCContext& __restrict ctx, uint8_t* base);
extern "C" REX_FUNC(sub_824DA068) {
  const uint32_t name_addr = ctx.r3.u32;
  const char* rel_path = name_addr ? reinterpret_cast<const char*>(base + name_addr) : "";
  const uint32_t out_size_ptr = ctx.r4.u32;

  __imp__sub_824DA068(ctx, base);

  const uint32_t buf = ctx.r3.u32;
  if (buf != 0 && rel_path && std::strstr(rel_path, "engine_params") != nullptr) {
    uint32_t file_size = out_size_ptr ? REX_LOAD_U32(out_size_ptr) : 23524;
    PatchEngineParamsInMemory(base + buf, file_size);
  }
}
extern "C" void __imp__sub_82434D40(PPCContext& __restrict ctx, uint8_t* base);
extern "C" REX_FUNC(sub_82434D40) {
  re_wor::de::edfx::ReapplyCamera(base, 1);
  __imp__sub_82434D40(ctx, base);
}

// Funciones del motor que escriben los campos de camara (pos +112, lado +128, up +144, at +160, fov +192) sobre su
// r3: se encontraron buscando en el codigo recompilado las que guardan floats en esos desplazamientos. Al terminar,
// si lo escrito era la camara del escenario y hay toma del editor, se vuelve a poner la nuestra.
#define REWOR_CAM_WRITER(addr, idx)                                         extern "C" void __imp__sub_##addr(PPCContext& __restrict ctx, uint8_t* base);   extern "C" REX_FUNC(sub_##addr) {                                           const uint32_t obj = ctx.r3.u32;                                          __imp__sub_##addr(ctx, base);                                             re_wor::de::edfx::OnEngineCamWrite(base, obj, idx);                     }
REWOR_CAM_WRITER(8222F888, 0)
REWOR_CAM_WRITER(8222F938, 1)
REWOR_CAM_WRITER(8222FEA0, 2)
REWOR_CAM_WRITER(82246538, 3)
REWOR_CAM_WRITER(822468B0, 4)
REWOR_CAM_WRITER(82248320, 5)
REWOR_CAM_WRITER(8245C728, 6)
REWOR_CAM_WRITER(8246A278, 7)
REWOR_CAM_WRITER(825E53A0, 8)
REWOR_CAM_WRITER(826F49E8, 9)
#undef REWOR_CAM_WRITER

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

  re_wor::de::frame_pacing::BeforeEngineFrame();   // FPS altos: limitador (ver frame_pacing.hpp)
  auto t0 = clock::now();
  double wall_ms = std::chrono::duration<double, std::milli>(t0 - s_prev).count();
  s_prev = t0;
  re_wor::de::edfx::ReapplyCamera(base, 2);   // antes de la espera al hilo de render
  const auto t_wait = clock::now();
  __imp__sub_8242AD60(ctx, base);
  re_wor::de::frame_pacing::NoteEngineWait(std::chrono::duration<double, std::milli>(clock::now() - t_wait).count());
  re_wor::de::frame_pacing::AfterEngineFrame(base);   // candados del motor y velocidad 1,00x
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
    re_wor::de::g_dt_rate.store(s_dt_sum / secs);
    re_wor::de::g_dt_avg.store(s_dt_sum / s_frames);
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

// Hook defensivo de memcpy (sub_826AC5F0):
// Protege contra desreferencias nulas si una rutina del motor pasa dest=0 o src=0.
// Evita el crash fatal (Exception 0xc0000005 WRITE a 0x100000000).
extern "C" void __imp__sub_826AC5F0(PPCContext& __restrict ctx, uint8_t* base);
extern "C" REX_FUNC(sub_826AC5F0) {
  if (ctx.r3.u32 == 0 || ctx.r4.u32 == 0 || ctx.r5.u32 == 0) {
    return;
  }
#ifdef _WIN32
  __try {
    __imp__sub_826AC5F0(ctx, base);
  } __except (EXCEPTION_EXECUTE_HANDLER) {
  }
#else
  __imp__sub_826AC5F0(ctx, base);
#endif
}

// Hook de sub_82400540 (recorre una lista de eventos de la cancion hasta el centinela 0x2CB3EF3B y devuelve
// cuantos proceso). sub_824013B0 le pasa como lista 16(objeto), que es NULL si la cancion se arranca sin la
// preparacion normal (p. ej. desde el menu de depuracion de Neversoft): leia la direccion 0 del juego
// (0x100000000 en el PC) y el juego se cerraba. Sin lista no hay eventos: se devuelve 0.
extern "C" void __imp__sub_82400540(PPCContext& __restrict ctx, uint8_t* base);
extern "C" REX_FUNC(sub_82400540) {
  if (ctx.r4.u32 == 0) {
    static std::atomic<int> s_logged{0};
    if (s_logged++ < 3) TRACE("[Guard] sub_82400540 con lista de eventos nula: ignorada");
    ctx.r3.u64 = 0;
    return;
  }
  __imp__sub_82400540(ctx, base);
}

// Hook de sub_823C6F20 (SetScreenElementProps / LegacyDoScreenElementMorph):
// En el codigo original de Neversoft, si sub_823CE160 no encuentra el elemento en pantalla,
// devuelve NULL (0), pero sub_823C6F20 intenta leer la vtable de r3 sin comprobar NULL (0x100000000).
// Esta proteccion captura la excepcion y evita que el juego caiga ante elementos no encontrados.
extern "C" void __imp__sub_823C6F20(PPCContext& __restrict ctx, uint8_t* base);
extern "C" REX_FUNC(sub_823C6F20) {
#ifdef _WIN32
  __try {
    __imp__sub_823C6F20(ctx, base);
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    ctx.r3.s64 = 0;
  }
#else
  __imp__sub_823C6F20(ctx, base);
#endif
}

namespace re_wor::de {
bool BootDone() { return g_boot_done.load(); }

// Vigia: si la logica del juego deja de avanzar 3 s (bucle infinito en un script, bloqueo...), lo apunta en el
// trace una vez, con re_dbg_mark (los scripts propios pueden dejar ahi una marca de por donde iban).
namespace {
std::atomic<uint64_t> g_logic_frames{0};
void StartLogicWatchdog(uint8_t* base) {
  static std::atomic<bool> started{false};
  if (started.exchange(true)) return;
  static uint8_t* s_base = base;
  core::set_crash_note([]() -> std::string {
    int mark = 0;
    qb_bridge::ReadIntGlobal("re_dbg_mark", s_base, &mark);
    return "marca re_dbg_mark = " + std::to_string(mark);
  });
  std::thread([base] {
    uint64_t last = 0;
    int stuck = 0;
    bool reported = false;
    for (;;) {
      std::this_thread::sleep_for(std::chrono::milliseconds(500));
      const uint64_t now = g_logic_frames.load();
      if (now != last) { last = now; stuck = 0; reported = false; continue; }
      if (++stuck == 6 && !reported) {
        reported = true;
        int mark = 0;
        qb_bridge::ReadIntGlobal("re_dbg_mark", base, &mark);
        TRACE("[Vigia] la logica del juego lleva 3 s sin avanzar (frame " << now << ", marca re_dbg_mark = " << mark << ")");
      }
    }
  }).detach();
}
}  // namespace

// desde el frame logico (hilo del juego): parar los videos pendientes y leer el reloj de la cancion
void OnLogicFrameExtras(PPCContext& ctx, uint8_t* base) {
  g_logic_frames.fetch_add(1, std::memory_order_relaxed);
  if (g_boot_done) StartLogicWatchdog(base);
  if (Get().dev.auto_skip_intro && !g_boot_done && !g_skip_armed && !g_movie_seen) {
    RequestSkipIntro();
  }
  const int64_t kill_at = g_kill_at.load();
  if (kill_at && NowMs() >= kill_at) {
    g_kill_at = 0;
    g_skip_until = NowMs() + 3000;
    PPCContext c = ctx;
    sub_825809A0(c, base);
    TRACE("[SkipIntro] videos parados");
  }
  edfx::OnLogicFrame(base);
  ui3d::OnLogicFrame(base);   // interfaces 3D de DE Studio (ordenes de los scripts, recarga)
  nativefx::Tick(ctx, base);
  sandbox::OnLogicFrame();
  camwatch::Tick();   // vigia de la camara (diagnostico, una vez)
  qb_bridge::OnLogicFrame(ctx, base);
  SyncGameMenu(base);
  TickDisplayConfirm(qb_bridge::ScriptsLoaded() ? base : nullptr);
  PerfTick();   // Options > Graphics del juego <-> [graphics]
}
}  // namespace re_wor::de
#endif
