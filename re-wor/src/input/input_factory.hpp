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

}  // namespace re_wor::input
