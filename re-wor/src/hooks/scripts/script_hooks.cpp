#include "hooks/scripts/script_hooks.hpp"
#include "core/crash_handler.hpp"
#include "mods/vfs_mod_loader.hpp"
#include "de/edfx.hpp"
#include "generated/re_wor_pch.h"
#include <cstring>

#ifdef _WIN32
#include <windows.h>
#endif

extern "C" void sub_820D0010(PPCContext& __restrict ctx, uint8_t* base);
extern "C" void __imp__sub_82401A28(PPCContext& __restrict ctx, uint8_t* base);
extern "C" void __imp__sub_823DB618(PPCContext& __restrict ctx, uint8_t* base);
extern "C" void __imp__sub_823956F8(PPCContext& __restrict ctx, uint8_t* base);
extern "C" void __imp__sub_825AB5B0(PPCContext& __restrict ctx, uint8_t* base);
extern "C" void __imp__sub_8215DD88(PPCContext& __restrict ctx, uint8_t* base);
extern "C" void __imp__sub_82424CD8(PPCContext& __restrict ctx, uint8_t* base);
extern "C" void __imp__sub_82424E50(PPCContext& __restrict ctx, uint8_t* base);
extern "C" void __imp__sub_8239E2A0(PPCContext& __restrict ctx, uint8_t* base);
extern "C" void __imp__sub_825E4AD0(PPCContext& __restrict ctx, uint8_t* base);

#ifdef _WIN32
static bool safe_call_imp_sub_82401A28(PPCContext& ctx, uint8_t* base) {
  __try {
    __imp__sub_82401A28(ctx, base);
    return true;
  } __except(EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}

static bool safe_call_imp_sub_823DB618(PPCContext& ctx, uint8_t* base) {
  __try {
    __imp__sub_823DB618(ctx, base);
    return true;
  } __except(EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}

static bool safe_call_imp_sub_823956F8(PPCContext& ctx, uint8_t* base) {
  __try {
    __imp__sub_823956F8(ctx, base);
    return true;
  } __except(EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}

static bool safe_call_imp_sub_825AB5B0(PPCContext& ctx, uint8_t* base) {
  __try {
    __imp__sub_825AB5B0(ctx, base);
    return true;
  } __except(EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}

static bool safe_call_imp_sub_8215DD88(PPCContext& ctx, uint8_t* base) {
  __try {
    __imp__sub_8215DD88(ctx, base);
    return true;
  } __except(EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}

static bool safe_call_imp_sub_82424CD8(PPCContext& ctx, uint8_t* base) {
  __try {
    __imp__sub_82424CD8(ctx, base);
    return true;
  } __except(EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}
#endif

// ---------------------------------------------------------------------------
// Hook: sub_82401A28 — LoadPak (Cargador de paquetes .pak / .pab)
// FIX #4: Sincroniza la inyección de scripts para ocurrir DESPUÉS de que
// qb.pak.xen y qb.pab.xen se hayan cargado completamente en la tabla de símbolos.
// ---------------------------------------------------------------------------
extern "C" REX_FUNC(sub_82401A28) {
  uint32_t struct_ptr = ctx.r3.u32;
  TRACE("[Hooks::Scripts::sub_82401A28] LoadPak called with struct at 0x" << std::hex << struct_ptr);

  PPCContext saved_ctx = ctx;
#ifdef _WIN32
  if (!safe_call_imp_sub_82401A28(ctx, base)) {
    TRACE("[Hooks::Scripts::sub_82401A28] Exception caught in LoadPak - graceful continue");
    ctx = saved_ctx;
    ctx.r3.u64 = 0;
    return;
  }
#else
  __imp__sub_82401A28(ctx, base);
#endif
  TRACE("[Hooks::Scripts::sub_82401A28] LoadPak done, result r3=0x" << std::hex << ctx.r3.u32);

  // FIX #4: Notificar al VFS Mod Loader una vez que la carga de pak/pab esté completada
  static bool s_scripts_ready_notified = false;
  if (!s_scripts_ready_notified) {
    s_scripts_ready_notified = true;
    TRACE("[Hooks::Scripts::ScriptInjection] Base scripts (pak/pab) fully loaded. Notifying VFS Mod Loader.");
    re_wor::mods::OnDefaultScriptsReady(nullptr);
  }
}

extern "C" REX_FUNC(sub_823DB618) {
  TRACE("[Hooks::Scripts::sub_823DB618] Entering sub_823DB618");
  PPCContext saved_ctx = ctx;
#ifdef _WIN32
  if (!safe_call_imp_sub_823DB618(ctx, base)) {
    TRACE("[Hooks::Scripts::sub_823DB618] Exception caught - restoring ctx and continuing");
    ctx = saved_ctx;
    ctx.r3.u64 = 0;
    return;
  }
#else
  __imp__sub_823DB618(ctx, base);
#endif
  TRACE("[Hooks::Scripts::sub_823DB618] Finished cleanly");
}

extern "C" REX_FUNC(sub_823956F8) {
  if (ctx.r3.u32 == 0) {
    ctx.r3.u64 = 0;
    return;
  }
  PPCContext saved_ctx = ctx;
#ifdef _WIN32
  if (!safe_call_imp_sub_823956F8(ctx, base)) {
    TRACE("[Hooks::Scripts::sub_823956F8] Exception caught in CStruct property lookup - returning 0");
    ctx = saved_ctx;
    ctx.r3.u64 = 0;
    return;
  }
#else
  __imp__sub_823956F8(ctx, base);
#endif
}

extern "C" REX_FUNC(sub_825AB5B0) {
  if (ctx.r3.u32 < 0x10000) {
    TRACE("[Hooks::Scripts::sub_825AB5B0] Invalid r3 (0x" << std::hex << ctx.r3.u32 << ")! Skipping parser.");
    ctx.r3.u64 = 0;
    return;
  }
  PPCContext saved_ctx = ctx;
#ifdef _WIN32
  if (!safe_call_imp_sub_825AB5B0(ctx, base)) {
    TRACE("[Hooks::Scripts::sub_825AB5B0] Exception caught - graceful continue");
    ctx = saved_ctx;
    ctx.r3.u64 = 0;
    return;
  }
#else
  __imp__sub_825AB5B0(ctx, base);
#endif
}

extern "C" REX_FUNC(sub_8215DD88) {
  PPCContext saved_ctx = ctx;
#ifdef _WIN32
  if (!safe_call_imp_sub_8215DD88(ctx, base)) {
    TRACE("[Hooks::Scripts::sub_8215DD88] Exception caught - graceful continue");
    ctx = saved_ctx;
    ctx.r3.u64 = 0;
    return;
  }
#else
  __imp__sub_8215DD88(ctx, base);
#endif
}

extern "C" REX_FUNC(sub_82424CD8) {
  PPCContext saved_ctx = ctx;
#ifdef _WIN32
  if (!safe_call_imp_sub_82424CD8(ctx, base)) {
    TRACE("[Hooks::Scripts::sub_82424CD8] Exception caught - graceful continue");
    ctx = saved_ctx;
    ctx.r3.u64 = 0;
    return;
  }
#else
  __imp__sub_82424CD8(ctx, base);
#endif
}

extern "C" REX_FUNC(sub_82424E50) {
  sub_82424CD8(ctx, base);
}

extern "C" REX_FUNC(sub_825E4AD0) {
  if (ctx.r3.u32 < 0x10000 || ctx.r4.u32 < 0x10000) {
    TRACE("[Hooks::Scripts::sub_825E4AD0] Skipping invalid pointers r3=0x" 
          << std::hex << ctx.r3.u32 << " r4=0x" << ctx.r4.u32);
    ctx.r3.u64 = 0;
    return;
  }
  __imp__sub_825E4AD0(ctx, base);
}

extern "C" REX_FUNC(sub_8239E2A0) {
  uint32_t checksum = ctx.r3.u32;
  // Eventos de sincronia del editor de canciones (EDFX): se anotan y se resuelven al script vacio del
  // juego (nullscript, o EmptyScript), asi el evento no hace nada en el juego y la capa sabe por donde va.
  if (re_wor::de::edfx::OnSymbolLookup(checksum)) {
    for (uint32_t empty : {0x0F7E902Cu /* nullscript */, 0x9DE81477u /* EmptyScript */}) {
      PPCContext e = ctx;
      e.r3.u64 = empty;
      __imp__sub_8239E2A0(e, base);
      if (e.r3.u32 != 0) { ctx = e; return; }
    }
  }
  __imp__sub_8239E2A0(ctx, base);
  if (ctx.r3.u32 == 0) {
    // Asignar un nodo dummy seguro independiente para evitar colisión de checksums
    PPCContext actx = ctx;
    actx.r3.u64 = 32;
    sub_820D0010(actx, base);
    uint32_t node = actx.r3.u32;
    if (node != 0) {
      memset(base + node, 0, 32);
      *reinterpret_cast<uint8_t*>(base + node + 2) = 1; // Tipo 1: entero seguro (valor = 0)
      *reinterpret_cast<uint32_t*>(base + node + 4) = checksum;
      *reinterpret_cast<uint32_t*>(base + node + 8) = 0;
      ctx.r3.u64 = node;
      TRACE("[Hooks::Scripts::QBSymbol] Resolved missing symbol 0x" << std::hex << checksum 
            << " -> safe dummy node 0x" << node);
    }
  }
}

namespace re_wor::hooks::scripts {

void register_script_hooks() {
}

}  // namespace re_wor::hooks::scripts
