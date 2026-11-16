#pragma once
// Construye el sistema de entrada de la Definitive Edition:
//   1. Mandos/guitarras reales: SDL3 (XInput + DirectInput + HID) o XInput nativo
//   2. Guitarra por teclado (re_wor_de.toml -> [input.keys])
//   3. Teclado como mando (driver MnK del SDK, opcional)
//   4. NOP (relleno)
// Los dispositivos reales se conectan primero, asi que una guitarra fisica
// es el jugador 1 y el teclado queda como jugador 2 (o 1 si no hay ninguna).

#include <memory>

#include <rex/runtime.h>

namespace re_wor::input {

std::unique_ptr<rex::system::IInputSystem> CreateDefinitiveInputSystem(bool tool_mode);

// Los menus del juego solo obedecen al mando 1 (el unico con perfil iniciado). Si el jugador 1 no tiene un
// control propio (su teclado enchufado o un mando fisico suyo), cualquier teclado hace de su mando de Xbox
// (KeyboardGuitarDriver) y devuelve true. *nobody = ningun jugador tiene ningun control (para el aviso).
// apply = false solo calcula: el mando del teclado se pone o se quita al arrancar y al salir de Controls,
// nunca en mitad del juego (que aparezca un mando mientras el juego monta el menu principal lo tumbaba).
bool UpdatePlayerOneFallback(bool* nobody, bool apply);

}  // namespace re_wor::input
