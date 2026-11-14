#pragma once

namespace re_wor::core {

// Pide a Windows una resolucion de temporizador de 1 ms (y desactiva el power
// throttling del proceso). Imprescindible para que el audio vaya a tiempo real.
void enable_high_resolution_timer();
void disable_high_resolution_timer();

}  // namespace re_wor::core
