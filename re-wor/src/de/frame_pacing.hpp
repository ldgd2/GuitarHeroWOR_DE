// Ritmo de frames de la Definitive Edition: FPS altos sin cambiar la velocidad del juego.
//
// Como mide el tiempo el motor (WoR, Neversoft):
//   - Cada frame, sub_823A0420 promedia los ultimos 4 tiempos de frame reales (timebase de la CPU) y deja el
//     resultado, en segundos, en 0x8285684C. Es lo que devuelve GetDeltaTime y lo que IncrementSongTimer suma
//     al reloj de la cancion (0x82993A58).
//   - La global game_lock_framerate = 1 (la de fabrica) fija ese tiempo a 1/60 s por frame: a 60 FPS da igual,
//     pero a 165 FPS el juego avanza 165 x 1/60 = 2,75 s por segundo ("modo imposible").
//   - render_lock_framerate = 1 hace que el dibujo espere al VBlank (60 Hz, como la consola).
//
// Lo que hace este modulo, desde el gancho del frame del motor (sub_8242AD60):
//   - Frame Rate > 60: quita los dos candados, de modo que el motor mide tiempo real, y limita el bucle a la
//     frecuencia elegida (sin limite iria a 700+ FPS). El VBlank del SDK se queda en 60 Hz.
//   - Mide cada 2 s la velocidad del motor: segundos de juego (suma de los 0x8285684C) por segundo real.
//     Debe ser 1,00. Si con FPS altos se sale de 0,9-1,1 dos veces seguidas, vuelve solo a 60 FPS con candados
//     (seguro) y lo apunta en re_wor_trace.log. Cambiar el Frame Rate en el menu lo vuelve a intentar.
//   - El reloj de la cancion (0x82993A58) NO usa ese tiempo: el motor (sub_823D1490) y el script
//     (IncrementSongTimer) lo avanzan por frames. Con FPS altos, AdjustNativeSongClock y AdjustSongTimer lo
//     avanzan con el reloj real x slomo, como el audio.
//   - Mide tambien la velocidad de la cancion (reloj de la cancion por segundo real; el modo practica la baja a
//     proposito, por eso no se usa para la proteccion).
#pragma once
#include <cstdint>

namespace re_wor::de::frame_pacing {

struct Stats {
  int target_hz = 60;          // Frame Rate elegido
  bool unlocked = false;       // candados del motor quitados (FPS > 60)
  bool fallback = false;       // se volvio a 60 porque la velocidad no era 1,00
  double engine_speed = 0.0;   // segundos de juego por segundo real (1,00 = correcto; 0 = sin medir aun)
  double song_speed = 0.0;     // reloj de la cancion por segundo real (0 = no hay cancion)
};

void BeforeEngineFrame();               // limitador (antes del frame del motor)
// Reloj de la cancion con FPS altos: cada vez que el juego lo avanza (IncrementSongTimer o el reloj nativo
// sub_823D1490) se sustituye lo sumado por tiempo real x slomo (0x82856844, GetSlomo), igual que el audio.
// No depende de cuantos frames haya. Pausas, saltos y la cuenta atras se dejan como los hace el juego.
void AdjustSongTimer(uint8_t* base, double song_time_before);
void AdjustNativeSongClock(uint8_t* base, double song_time_before);
void NoteEngineWait(double ms);           // tiempo dentro de sub_8242AD60 (espera al hilo de render)
void AfterEngineFrame(uint8_t* base);   // candados y medidas (despues)
Stats Get();

}  // namespace re_wor::de::frame_pacing
