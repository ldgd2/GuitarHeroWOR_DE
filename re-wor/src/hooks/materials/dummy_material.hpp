#pragma once

#include <cstdint>
#include <rex/ppc.h>

namespace re_wor::hooks::materials {

// Construye o devuelve un material dummy seguro con estructura completa
// (material -> pass_array -> shader_descriptors con conteos en cero)
// para evitar Null Pointer Dereference en viewport 2D y pipelines de render.
uint32_t GetOrCreateDummyMaterial(PPCContext& ctx, uint8_t* base, uint32_t identifier);

}  // namespace re_wor::hooks::materials

