// =============================================================================
// boot_probe.cpp - Sondas de diagnostico del arranque (solo lectura).
// Cada hook llama a la funcion ORIGINAL sin modificar argumentos ni resultado,
// y solo registra en re_wor_trace.log el estado relevante.
// =============================================================================

#include "core/crash_handler.hpp"
#include "generated/re_wor_pch.h"
#include <string>

extern "C" void __imp__sub_8256F400(PPCContext& __restrict ctx, uint8_t* base);
extern "C" void __imp__sub_82393178(PPCContext& __restrict ctx, uint8_t* base);
extern "C" void __imp__sub_82424E50(PPCContext& __restrict ctx, uint8_t* base);

static constexpr uint32_t kMainHeapPtr = 0x8297F0DC;   // heap global del motor
static constexpr uint32_t kStreamPoolPtr = 0x82A9DA44;  // gestor de streams de archivos

// Crea el heap principal del motor.
extern "C" REX_FUNC(sub_82393178) {
  TRACE("[BootProbe] sub_82393178 (crear heap) antes: heap=0x" << std::hex << REX_LOAD_U32(kMainHeapPtr));
  __imp__sub_82393178(ctx, base);
  TRACE("[BootProbe] sub_82393178 despues: heap=0x" << std::hex << REX_LOAD_U32(kMainHeapPtr));
}

// Inicializacion del sistema de archivos/streams (llamada desde el main).
extern "C" REX_FUNC(sub_82424E50) {
  TRACE("[BootProbe] sub_82424E50 (init streams) heap=0x" << std::hex << REX_LOAD_U32(kMainHeapPtr)
        << " pool=0x" << REX_LOAD_U32(kStreamPoolPtr));
  __imp__sub_82424E50(ctx, base);
  TRACE("[BootProbe] sub_82424E50 despues: pool=0x" << std::hex << REX_LOAD_U32(kStreamPoolPtr));
}

// Crea el pool de streams usando el heap principal.
extern "C" REX_FUNC(sub_8256F400) {
  TRACE("[BootProbe] sub_8256F400 (crear pool) heap=0x" << std::hex << REX_LOAD_U32(kMainHeapPtr)
        << " pool=0x" << REX_LOAD_U32(kStreamPoolPtr));
  __imp__sub_8256F400(ctx, base);
  TRACE("[BootProbe] sub_8256F400 despues: pool=0x" << std::hex << REX_LOAD_U32(kStreamPoolPtr));
}

extern "C" void __imp__sub_82422E58(PPCContext& __restrict ctx, uint8_t* base);
extern "C" void __imp__sub_8256E3A8(PPCContext& __restrict ctx, uint8_t* base);

static const char* guest_str(uint8_t* base, uint32_t addr) {
  return addr ? reinterpret_cast<const char*>(base + addr) : "(null)";
}

// FileStream::Open(nombre, flags, ...)
extern "C" REX_FUNC(sub_82422E58) {
  std::string name = guest_str(base, ctx.r3.u32);
  uint32_t flags = ctx.r4.u32;
  __imp__sub_82422E58(ctx, base);
  TRACE("[BootProbe] FileStream::Open('" << name << "', flags=0x" << std::hex << flags
        << ") -> 0x" << ctx.r3.u32);
}

// StreamManager::Open(pool, ruta_final, flags, ...)
extern "C" REX_FUNC(sub_8256E3A8) {
  std::string path = guest_str(base, ctx.r4.u32);
  uint32_t flags = ctx.r5.u32;
  __imp__sub_8256E3A8(ctx, base);
  TRACE("[BootProbe]   StreamManager::Open('" << path << "', flags=0x" << std::hex << flags
        << ") -> 0x" << ctx.r3.u32);
}

// Bucle de frame del motor: cuenta frames para confirmar que el juego renderiza.
extern "C" void __imp__sub_822E5E10(PPCContext& __restrict ctx, uint8_t* base);
extern "C" REX_FUNC(sub_822E5E10) {
  static uint64_t s_frames = 0;
  if (++s_frames % 120 == 1) {
    TRACE("[BootProbe] Frame " << std::dec << s_frames);
  }
  __imp__sub_822E5E10(ctx, base);
}

// Callback de interrupcion grafica del juego (source 0 = VBlank, 1 = CP).
extern "C" void __imp__sub_822DB470(PPCContext& __restrict ctx, uint8_t* base);
extern "C" void __imp__sub_822E5668(PPCContext& __restrict ctx, uint8_t* base);
static uint64_t s_irq_vblank = 0, s_irq_other = 0, s_vblank_handler = 0;

static uint64_t load_u64(uint8_t* base, uint32_t addr) {
  return (uint64_t(REX_LOAD_U32(addr)) << 32) | REX_LOAD_U32(addr + 4);
}

extern "C" REX_FUNC(sub_822DB470) {
  uint32_t source = ctx.r3.u32;
  (source == 0 ? s_irq_vblank : s_irq_other)++;
  if (source == 0 && s_irq_vblank % 120 == 1) {
    TRACE("[BootProbe] IRQ vblank#" << std::dec << s_irq_vblank << " other=" << s_irq_other
          << " vblank_handler=" << s_vblank_handler
          << " cnt_C1E0=" << load_u64(base, 0x8298C1E0) << " cnt_C1F0=" << load_u64(base, 0x8298C1F0));
  }
  __imp__sub_822DB470(ctx, base);
}

extern "C" REX_FUNC(sub_822E5668) {
  s_vblank_handler++;
  __imp__sub_822E5668(ctx, base);
}
