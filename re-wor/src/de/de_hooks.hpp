#pragma once
#include <cstdint>

namespace re_wor::de {

// Frames del bucle principal del motor (no del presentador de la GPU).
uint64_t GameFrameCount();
double GameFps();
// Frames LOGICOS por segundo (bucle principal del juego).
double LogicFps();
// Mezclas de audio por segundo (ideal 187.5) y coste medio en ms.
double AudioMixRate();
double AudioMixMs();
// Registrar cada FileStream::Open en re_wor_trace.log (los fallos siempre).
void SetLogFileOpens(bool enabled);
void SetTraceProbes(bool enabled);  // FPS/audio/reloj al trace cada 2 s

// Tecla "saltar intro" ([hotkeys].skip_intro, F por defecto): corta los videos del arranque
// (logo_intro.bik...) y hace que los scripts de arranque los den por terminados. Solo antes del menu.
void RequestSkipIntro();

// true cuando el juego ya llego al menu principal (se cargo band_lobby)
bool BootDone();

}  // namespace re_wor::de
