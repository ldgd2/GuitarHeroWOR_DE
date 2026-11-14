#include "hooks/materials/material_hooks.hpp"
#include "hooks/materials/dummy_material.hpp"
#include "core/crash_handler.hpp"
#include "mods/mod_vfs.hpp"
#include "generated/re_wor_pch.h"

#ifdef _WIN32
#include <windows.h>
#endif

#include <filesystem>
#include <fstream>
#include <cstring>
#include <vector>

extern "C" void sub_820D0010(PPCContext& __restrict ctx, uint8_t* base);
extern "C" void __imp__sub_82424B68(PPCContext& __restrict ctx, uint8_t* base);
extern "C" void __imp__sub_825A3840(PPCContext& __restrict ctx, uint8_t* base);
extern "C" void __imp__sub_825A33D8(PPCContext& __restrict ctx, uint8_t* base);
extern "C" void __imp__sub_825A34A0(PPCContext& __restrict ctx, uint8_t* base);
extern "C" void __imp__sub_8254CDF8(PPCContext& __restrict ctx, uint8_t* base);
extern "C" void __imp__sub_8254A9D8(PPCContext& __restrict ctx, uint8_t* base);
extern "C" void __imp__sub_8254A6B0(PPCContext& __restrict ctx, uint8_t* base);
extern "C" void __imp__sub_8254A968(PPCContext& __restrict ctx, uint8_t* base);
extern "C" void __imp__sub_82550A50(PPCContext& __restrict ctx, uint8_t* base);

#ifdef _WIN32
static bool safe_call_imp_sub_82424B68(PPCContext& ctx, uint8_t* base) {
  __try {
    __imp__sub_82424B68(ctx, base);
    return true;
  } __except(EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}

static bool safe_call_imp_sub_825A3840(PPCContext& ctx, uint8_t* base) {
  __try {
    __imp__sub_825A3840(ctx, base);
    return true;
  } __except(EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}

static bool safe_call_imp_sub_8254CDF8(PPCContext& ctx, uint8_t* base) {
  __try {
    __imp__sub_8254CDF8(ctx, base);
    return true;
  } __except(EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}

static bool safe_call_imp_sub_8254A968(PPCContext& ctx, uint8_t* base) {
  __try {
    __imp__sub_8254A968(ctx, base);
    return true;
  } __except(EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}

static bool safe_call_imp_sub_82550A50(PPCContext& ctx, uint8_t* base) {
  __try {
    __imp__sub_82550A50(ctx, base);
    return true;
  } __except(EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}
#endif

// ---------------------------------------------------------------------------
// Descompresor de contenedor CHNK (las librerias en assets ya estan descomprimidas)
// ---------------------------------------------------------------------------
static bool DecompressCHNK(const uint8_t* in_data, size_t in_size, std::vector<uint8_t>& out_data) {
  (void)in_data;
  (void)in_size;
  (void)out_data;
  return false;
}

// ---------------------------------------------------------------------------
// Hook: sub_82424B68 — Cargador síncrono de chunks/archivos
// FIX #2: Descomprime contenedores CHNK a buffers MATL limpios
// ---------------------------------------------------------------------------
extern "C" REX_FUNC(sub_82424B68) {
  if (ctx.r3.u32 == 0 || ctx.r4.u32 == 0) {
    TRACE("[Hooks::Materials::sub_82424B68] Called with NULL pointer (r3=0x" 
          << std::hex << ctx.r3.u32 << ", r4=0x" << ctx.r4.u32 << ")!");
    if (ctx.r4.u32 != 0) {
      REX_STORE_U32(ctx.r4.u32, 0);
    }
    ctx.r3.u64 = 0;
    return;
  }

  const char* filename = reinterpret_cast<const char*>(base + ctx.r3.u32);
  uint32_t out_buf_ptr = ctx.r4.u32;
  TRACE("[Hooks::Materials::sub_82424B68] Loading chunk/archive: '" << (filename ? filename : "NULL") 
        << "' out_ptr=0x" << std::hex << out_buf_ptr);

  if (filename && filename[0] != '\0' && strcmp(filename, "NULL") != 0) {
    std::filesystem::path resolved = re_wor::ResolveAssetPath(filename);
    if (std::filesystem::exists(resolved) && !std::filesystem::is_directory(resolved)) {
      std::ifstream file(resolved, std::ios::binary | std::ios::ate);
      if (file.is_open()) {
        std::streamsize file_size = file.tellg();
        file.seekg(0, std::ios::beg);

        std::vector<uint8_t> raw_file(static_cast<size_t>(file_size));
        file.read(reinterpret_cast<char*>(raw_file.data()), file_size);

        // Descomprimir CHNK si aplica, o usar directo si ya es MATL/payload limpio
        std::vector<uint8_t> decomp_buffer;
        const uint8_t* final_payload = raw_file.data();
        size_t final_size = raw_file.size();

        if (file_size >= 4 && raw_file[0] == 'C' && raw_file[1] == 'H' && raw_file[2] == 'N' && raw_file[3] == 'K') {
          TRACE("[Hooks::Materials::sub_82424B68] Detected CHNK container for '" << filename << "' - decompressing payload...");
          if (DecompressCHNK(raw_file.data(), raw_file.size(), decomp_buffer)) {
            final_payload = decomp_buffer.data();
            final_size = decomp_buffer.size();
            TRACE("[Hooks::Materials::sub_82424B68] Successfully decompressed CHNK -> " 
                  << std::dec << final_size << " bytes");
          } else {
            TRACE("[Hooks::Materials::sub_82424B68] CHNK decompression failed - using raw buffer");
          }
        }

        // Alojar buffer en el heap de Neversoft con alineación de 128 bytes
        PPCContext alloc_ctx = ctx;
        alloc_ctx.r3.u64 = static_cast<uint64_t>(final_size + 128);
        sub_820D0010(alloc_ctx, base);
        uint32_t target_buf = alloc_ctx.r3.u32;

        if (target_buf != 0) {
          memcpy(base + target_buf, final_payload, final_size);

          if (out_buf_ptr != 0) {
            REX_STORE_U32(out_buf_ptr, target_buf);
          }
          TRACE("[Hooks::Materials::sub_82424B68] SUCCESS: Loaded '" << resolved.string() 
                << "' (" << std::dec << final_size << " bytes payload) -> 0x" << std::hex << target_buf);
          ctx.r3.u64 = 0;
          return;
        }
      }
    }
  }

#ifdef _WIN32
  if (!safe_call_imp_sub_82424B68(ctx, base)) {
    TRACE("[Hooks::Materials::sub_82424B68] Exception caught for '" << filename << "' - graceful fail");
    if (out_buf_ptr != 0) {
      REX_STORE_U32(out_buf_ptr, 0);
    }
    ctx.r3.u64 = 0;
    return;
  }
#else
  __imp__sub_82424B68(ctx, base);
#endif
}

// ---------------------------------------------------------------------------
// Hook: sub_825A3840 — Procesador de MaterialLibrary
// ---------------------------------------------------------------------------
extern "C" REX_FUNC(sub_825A3840) {
  if (ctx.r3.u32 == 0) {
    TRACE("[Hooks::Materials::sub_825A3840] Called with NULL r3 pointer!");
    ctx.r3.u64 = 0;
    return;
  }

  const char* mat_lib_path = reinterpret_cast<const char*>(base + ctx.r3.u32);
  TRACE("[Hooks::Materials::sub_825A3840] Entering MaterialLibrary processor for: '" 
        << (mat_lib_path ? mat_lib_path : "NULL") << "'");
  uint32_t prev_table = *reinterpret_cast<uint32_t*>(base + 0x82AB1E00);
  if (prev_table == 0) {
    *reinterpret_cast<uint32_t*>(base + 0x82AB1E04) = 0;
  }

#ifdef _WIN32
  if (!safe_call_imp_sub_825A3840(ctx, base)) {
    TRACE("[Hooks::Materials::sub_825A3840] Exception caught in __imp__sub_825A3840 - continuing");
    ctx.r3.u64 = 0;
    return;
  }
#else
  __imp__sub_825A3840(ctx, base);
#endif
  TRACE("[Hooks::Materials::sub_825A3840] Finished MaterialLibrary processing. Result r3=" << ctx.r3.u64);
}

// ---------------------------------------------------------------------------
// Hook: sub_825A33D8 — Búsqueda de material por nombre de string
// ---------------------------------------------------------------------------
extern "C" REX_FUNC(sub_825A33D8) {
  uint32_t name_ptr = ctx.r3.u32;
  const char* name = (name_ptr != 0) ? reinterpret_cast<const char*>(base + name_ptr) : "NULL";
  __imp__sub_825A33D8(ctx, base);

  if (ctx.r3.u32 == 0) {
    uint32_t dummy = re_wor::hooks::materials::GetOrCreateDummyMaterial(ctx, base, name_ptr);
    if (dummy != 0) {
      ctx.r3.u64 = dummy;
      TRACE("[Hooks::Materials::sub_825A33D8] Fallback dummy material 0x" << std::hex << dummy 
            << " used for '" << name << "'");
    }
  }
}

// ---------------------------------------------------------------------------
// Hook: sub_825A34A0 — Búsqueda de material por Checksum CRC32 (FIX #3)
// ---------------------------------------------------------------------------
extern "C" REX_FUNC(sub_825A34A0) {
  uint32_t checksum = ctx.r3.u32;
  __imp__sub_825A34A0(ctx, base);

  if (ctx.r3.u32 == 0) {
    uint32_t dummy = re_wor::hooks::materials::GetOrCreateDummyMaterial(ctx, base, checksum);
    if (dummy != 0) {
      ctx.r3.u64 = dummy;
      TRACE("[Hooks::Materials::sub_825A34A0] Fallback dummy material 0x" << std::hex << dummy 
            << " used for checksum 0x" << checksum);
    }
  }
}

// ---------------------------------------------------------------------------
// Hook: sub_8254CDF8 — Parser de structs de materiales
// ---------------------------------------------------------------------------
extern "C" REX_FUNC(sub_8254CDF8) {
  if (ctx.r3.u32 == 0 || ctx.r3.u32 == ctx.r4.u32) {
    ctx.r3.u64 = 0;
    return;
  }
#ifdef _WIN32
  if (!safe_call_imp_sub_8254CDF8(ctx, base)) {
    TRACE("[Hooks::Materials::sub_8254CDF8] Exception caught - graceful continue");
  }
#else
  __imp__sub_8254CDF8(ctx, base);
#endif
}

extern "C" REX_FUNC(sub_8254A968) {
  if (ctx.r3.u32 == 0) {
    ctx.r3.s64 = -1;
    return;
  }
  uint32_t num_passes = REX_LOAD_U32(ctx.r3.u32 + 24);
  if (num_passes == 1) {
    ctx.r3.u64 = 0;
    return;
  }
#ifdef _WIN32
  if (!safe_call_imp_sub_8254A968(ctx, base)) {
    ctx.r3.u64 = 0;
    return;
  }
#else
  __imp__sub_8254A968(ctx, base);
#endif
}

extern "C" REX_FUNC(sub_82550A50) {
  if (ctx.r3.u32 == 0) {
    ctx.r3.u64 = 0;
    return;
  }
#ifdef _WIN32
  if (!safe_call_imp_sub_82550A50(ctx, base)) {
    ctx.r3.u64 = 0;
    return;
  }
#else
  __imp__sub_82550A50(ctx, base);
#endif
}

extern "C" REX_FUNC(sub_8254A9D8) {
  if (ctx.r3.u32 == 0) {
    ctx.r3.u64 = 0;
    return;
  }
  __imp__sub_8254A9D8(ctx, base);
}

extern "C" REX_FUNC(sub_8254A6B0) {
  if (ctx.r3.u32 == 0) {
    ctx.r3.u64 = 0;
    return;
  }
  __imp__sub_8254A6B0(ctx, base);
}

namespace re_wor::hooks::materials {

void register_material_hooks() {
}

}  // namespace re_wor::hooks::materials
