// =============================================================================
// timer_resolution.cpp - Resolucion del temporizador de Windows a 1 ms.
//
// Los hilos del juego (FMOD/streams de audio, carga, etc.) duermen 1-5 ms con
// KeDelayExecutionThread, que el SDK traduce a SleepEx(ms). Con la resolucion
// por defecto de Windows (15.6 ms) cada espera dura ~16 ms: los contextos XMA
// de las pistas de la cancion se alimentan tarde y de forma desigual, el audio
// de cada pista se desincroniza y el reloj de la cancion (que sigue al audio)
// avanza en camara lenta aunque el render vaya a 60 FPS.
// =============================================================================

#include "core/timer_resolution.hpp"

#include "core/crash_handler.hpp"

#ifdef _WIN32
#include <windows.h>
#include <timeapi.h>
#endif

namespace re_wor::core {

#ifdef _WIN32
namespace {
bool g_active = false;

using NtQueryTimerResolutionFn = LONG(NTAPI*)(PULONG, PULONG, PULONG);

double CurrentResolutionMs() {
  auto fn = reinterpret_cast<NtQueryTimerResolutionFn>(
      GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtQueryTimerResolution"));
  if (!fn) return -1.0;
  ULONG min_res = 0, max_res = 0, cur_res = 0;
  if (fn(&min_res, &max_res, &cur_res) != 0) return -1.0;
  return cur_res / 10000.0;  // unidades de 100 ns
}
}  // namespace
#endif

void enable_high_resolution_timer() {
#ifdef _WIN32
  if (g_active) return;

  // Windows 11: sin esto la peticion se ignora cuando la ventana no esta en
  // primer plano o esta tapada (EcoQoS / power throttling).
  PROCESS_POWER_THROTTLING_STATE state = {};
  state.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
  state.ControlMask = PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION |
                      PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
  state.StateMask = 0;  // 0 = NO aplicar throttling
  SetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &state, sizeof(state));

  double before = CurrentResolutionMs();
  g_active = timeBeginPeriod(1) == TIMERR_NOERROR;
  TRACE("[Timer] Resolucion del temporizador: " << before << " ms -> " << CurrentResolutionMs()
        << " ms (timeBeginPeriod(1) " << (g_active ? "OK" : "FALLO") << ")");
#endif
}

void disable_high_resolution_timer() {
#ifdef _WIN32
  if (g_active) {
    timeEndPeriod(1);
    g_active = false;
  }
#endif
}

}  // namespace re_wor::core
