#include "hooks/materials/dummy_material.hpp"
#include "core/crash_handler.hpp"
#include "generated/re_wor_pch.h"
#include <cstring>
#include <unordered_map>

extern "C" void sub_820D0010(PPCContext& __restrict ctx, uint8_t* base);

namespace re_wor::hooks::materials {

static std::unordered_map<uint32_t, uint32_t> s_dummy_materials;
static uint32_t s_shared_pass_array = 0;
static uint32_t s_shared_shader_desc = 0;
static uint32_t s_default_pass_name_ptr = 0;

uint32_t GetOrCreateDummyMaterial(PPCContext& ctx, uint8_t* base, uint32_t identifier) {
  auto it = s_dummy_materials.find(identifier);
  if (it != s_dummy_materials.end() && it->second != 0) {
    return it->second;
  }

  // 1. Cadena de nombre de pase por defecto en memoria guest ("default\0")
  if (s_default_pass_name_ptr == 0) {
    PPCContext actx = ctx;
    actx.r3.u64 = 16;
    sub_820D0010(actx, base);
    s_default_pass_name_ptr = actx.r3.u32;
    if (s_default_pass_name_ptr != 0) {
      memset(base + s_default_pass_name_ptr, 0, 16);
      memcpy(base + s_default_pass_name_ptr, "default", 8);
    }
  }

  // 2. Descriptor de shader compartido (128 bytes, 0 samplers y 0 constantes)
  if (s_shared_shader_desc == 0) {
    PPCContext actx = ctx;
    actx.r3.u64 = 128;
    sub_820D0010(actx, base);
    s_shared_shader_desc = actx.r3.u32;
    if (s_shared_shader_desc != 0) {
      memset(base + s_shared_shader_desc, 0, 128);
    }
  }

  // 3. Arreglo de pases compartido (8 pases x 88 bytes = 704 bytes)
  if (s_shared_pass_array == 0) {
    PPCContext actx = ctx;
    actx.r3.u64 = 704;
    sub_820D0010(actx, base);
    s_shared_pass_array = actx.r3.u32;
    if (s_shared_pass_array != 0) {
      memset(base + s_shared_pass_array, 0, 704);
      for (int i = 0; i < 8; i++) {
        uint32_t pass_addr = s_shared_pass_array + (i * 88);
        // pass + 0: puntero al nombre del pase (cadena "default")
        if (s_default_pass_name_ptr != 0) {
          REX_STORE_U32(pass_addr + 0, s_default_pass_name_ptr);
        }
        // pass + 84: puntero al descriptor de shaders
        if (s_shared_shader_desc != 0) {
          REX_STORE_U32(pass_addr + 84, s_shared_shader_desc);
        }
      }
    }
  }

  // 4. Alojar la cabecera del material (256 bytes)
  PPCContext mctx = ctx;
  mctx.r3.u64 = 256;
  sub_820D0010(mctx, base);
  uint32_t mat_addr = mctx.r3.u32;

  if (mat_addr != 0) {
    memset(base + mat_addr, 0, 256);
    // Escribir en formato Big-Endian nativo de Xbox 360 usando REX_STORE_U32
    REX_STORE_U32(mat_addr + 4, identifier);               // Checksum / Name
    REX_STORE_U32(mat_addr + 8, 0);                        // Group ID
    REX_STORE_U32(mat_addr + 12, 0);                       // Type
    REX_STORE_U32(mat_addr + 24, 1);                       // num_passes = 1
    if (s_shared_pass_array != 0) {
      REX_STORE_U32(mat_addr + 28, s_shared_pass_array);   // pass array
    }

    s_dummy_materials[identifier] = mat_addr;
    TRACE("[Hooks::Materials] Allocated safe dummy material for 0x" << std::hex << identifier 
          << " -> mat=0x" << mat_addr << " passes=0x" << s_shared_pass_array);
    return mat_addr;
  }

  return 0;
}

}  // namespace re_wor::hooks::materials
