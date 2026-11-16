#pragma once
// =============================================================================
// mic_capture - Microfono del PC para la voz de WoR.
//
// Captura con WASAPI (modo compartido) del dispositivo elegido en cada jugador ([[input.players]] mic) en
// el formato que tenga (float o PCM de 16/24/32 bits, cualquier numero de canales y frecuencia), lo mezcla a
// mono y lo pasa a 16 kHz y 16 bits, que es lo que da el microfono USB de Guitar Hero en Xbox 360.
// El juego lo recibe por la API de voz de Xbox (XamVoice*, voice.cpp).
// =============================================================================

#include <cstddef>
#include <cstdint>
#include <string>

namespace re_wor::input {

constexpr int kMicRate = 16000;

// Abre / cierra los microfonos segun la configuracion de jugadores. Se puede llamar en cualquier momento.
void ApplyMicConfig();

// Jugador (0..3) con microfono abierto
bool MicActive(int player);
// Muestras de 16 kHz disponibles / leerlas (int16 mono). Si faltan, devuelve las que haya.
size_t MicAvailable(int player);
size_t MicRead(int player, int16_t* out, size_t count);
// Nivel de entrada 0..1 (pico de los ultimos ~100 ms), para el medidor de la pantalla Controles
float MicLevel(int player);
// Estado legible ("", "abierto: <nombre>", "error: ...")
std::string MicStatus(int player);

}  // namespace re_wor::input
