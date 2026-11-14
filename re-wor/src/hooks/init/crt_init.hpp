#pragma once

#include <cstdint>
#include <rex/ppc.h>

namespace re_wor::hooks::init {

// Rellena las entradas nulas en la tabla de despacho con el stub seguro
void fill_dispatch_table(uint8_t* base);

}  // namespace re_wor::hooks::init

