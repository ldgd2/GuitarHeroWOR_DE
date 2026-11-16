#pragma once
// =============================================================================
// controls_menu - Options > Controls con el menu del juego (qb/controls.q).
//
// Vistas: lobby con las tarjetas de los 4 jugadores -> instrumento -> configuracion (imagen del instrumento
// a la izquierda; a la derecha, deteccion del dispositivo (lista o escucha) y el mapeo; voz: microfono,
// control y mapeo) con Prueba (cancion al azar) y Listo. Cada jugador es una plaza de [[input.players]] con
// su propio instrumento (se pueden repetir) y cada teclado es independiente. Todo con los controles.
// =============================================================================
#include <cstdint>

namespace re_wor::de::controls {

// Cada frame logico: atiende las pulsaciones de la pantalla y rellena sus filas.
void Tick(uint8_t* base);

// Abre la pantalla (p. ej. desde el panel F8). Solo tiene efecto en los menus.
void RequestOpen();

}  // namespace re_wor::de::controls
