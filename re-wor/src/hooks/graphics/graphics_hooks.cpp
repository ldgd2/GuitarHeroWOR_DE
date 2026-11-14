#include "hooks/graphics/graphics_hooks.hpp"
#include "core/crash_handler.hpp"
#include "generated/re_wor_pch.h"

#ifdef _WIN32
#include <windows.h>
#endif

extern "C" void __imp__sub_824C7788(PPCContext& __restrict ctx, uint8_t* base);
extern "C" void __imp__sub_824D7B50(PPCContext& __restrict ctx, uint8_t* base);
extern "C" void __imp__sub_82392380(PPCContext& __restrict ctx, uint8_t* base);
extern "C" void __imp__sub_8253F2F8(PPCContext& __restrict ctx, uint8_t* base);
extern "C" void __imp__sub_82391D00(PPCContext& __restrict ctx, uint8_t* base);
extern "C" void __imp__sub_82391C88(PPCContext& __restrict ctx, uint8_t* base);
extern "C" void __imp__sub_823D6978(PPCContext& __restrict ctx, uint8_t* base);
extern "C" void __imp__sub_82620388(PPCContext& __restrict ctx, uint8_t* base);
extern "C" void __imp__sub_825F1DA8(PPCContext& __restrict ctx, uint8_t* base);
extern "C" void __imp__sub_822E5E10(PPCContext& __restrict ctx, uint8_t* base);

#ifdef _WIN32
static bool safe_call_imp_sub_824C7788(PPCContext& ctx, uint8_t* base) {
  __try {
    __imp__sub_824C7788(ctx, base);
    return true;
  } __except(EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}

static bool safe_call_imp_sub_824D7B50(PPCContext& ctx, uint8_t* base) {
  __try {
    __imp__sub_824D7B50(ctx, base);
    return true;
  } __except(EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}

static bool safe_call_imp_sub_82392380(PPCContext& ctx, uint8_t* base) {
  __try {
    __imp__sub_82392380(ctx, base);
    return true;
  } __except(EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}

static bool safe_call_imp_sub_8253F2F8(PPCContext& ctx, uint8_t* base) {
  __try {
    __imp__sub_8253F2F8(ctx, base);
    return true;
  } __except(EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}

static bool safe_call_imp_sub_82391D00(PPCContext& ctx, uint8_t* base) {
  __try {
    __imp__sub_82391D00(ctx, base);
    return true;
  } __except(EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}

static bool safe_call_imp_sub_82391C88(PPCContext& ctx, uint8_t* base) {
  __try {
    __imp__sub_82391C88(ctx, base);
    return true;
  } __except(EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}

static bool safe_call_imp_sub_823D6978(PPCContext& ctx, uint8_t* base) {
  __try {
    __imp__sub_823D6978(ctx, base);
    return true;
  } __except(EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}

static bool safe_call_imp_sub_82620388(PPCContext& ctx, uint8_t* base) {
  __try {
    __imp__sub_82620388(ctx, base);
    return true;
  } __except(EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}

static bool safe_call_imp_sub_825F1DA8(PPCContext& ctx, uint8_t* base) {
  __try {
    __imp__sub_825F1DA8(ctx, base);
    return true;
  } __except(EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}
#endif

extern "C" REX_FUNC(sub_824C7788) {
  TRACE("[Hooks::Graphics::sub_824C7788] 2D Viewport setup");
#ifdef _WIN32
  if (!safe_call_imp_sub_824C7788(ctx, base)) {
    TRACE("[Hooks::Graphics::sub_824C7788] Exception caught in __imp__sub_824C7788 - graceful continue");
    return;
  }
#else
  __imp__sub_824C7788(ctx, base);
#endif
  TRACE("[Hooks::Graphics::sub_824C7788] Done");
}

extern "C" REX_FUNC(sub_824D7B50) {
  TRACE("[Hooks::Graphics::sub_824D7B50] Font/Text system init");
#ifdef _WIN32
  if (!safe_call_imp_sub_824D7B50(ctx, base)) {
    TRACE("[Hooks::Graphics::sub_824D7B50] Exception caught in __imp__sub_824D7B50 - graceful continue");
    return;
  }
#else
  __imp__sub_824D7B50(ctx, base);
#endif
  TRACE("[Hooks::Graphics::sub_824D7B50] Font/Text system done");
}

extern "C" REX_FUNC(sub_82392380) {
#ifdef _WIN32
  if (!safe_call_imp_sub_82392380(ctx, base)) {
    TRACE("[Hooks::Graphics::sub_82392380] Exception caught - returning 0");
    ctx.r3.u64 = 0;
    return;
  }
#else
  __imp__sub_82392380(ctx, base);
#endif
}

extern "C" REX_FUNC(sub_8253F2F8) {
  TRACE("[Hooks::Graphics::sub_8253F2F8] Loading image display setup");
#ifdef _WIN32
  if (!safe_call_imp_sub_8253F2F8(ctx, base)) {
    TRACE("[Hooks::Graphics::sub_8253F2F8] Exception caught - continuing");
    return;
  }
#else
  __imp__sub_8253F2F8(ctx, base);
#endif
  TRACE("[Hooks::Graphics::sub_8253F2F8] Loading image display setup done");
}

extern "C" REX_FUNC(sub_82391D00) {
#ifdef _WIN32
  if (!safe_call_imp_sub_82391D00(ctx, base)) {
    TRACE("[Hooks::Graphics::sub_82391D00] Exception caught - returning 0");
    ctx.r3.u64 = 0;
    return;
  }
#else
  __imp__sub_82391D00(ctx, base);
#endif
}

extern "C" REX_FUNC(sub_82391C88) {
#ifdef _WIN32
  if (!safe_call_imp_sub_82391C88(ctx, base)) {
    TRACE("[Hooks::Graphics::sub_82391C88] Exception caught - continuing");
    return;
  }
#else
  __imp__sub_82391C88(ctx, base);
#endif
}

extern "C" REX_FUNC(sub_823D6978) {
#ifdef _WIN32
  if (!safe_call_imp_sub_823D6978(ctx, base)) {
    TRACE("[Hooks::Graphics::sub_823D6978] Exception caught - continuing");
    return;
  }
#else
  __imp__sub_823D6978(ctx, base);
#endif
}

extern "C" REX_FUNC(sub_825F1DA8) {
  TRACE("[Hooks::Graphics::sub_825F1DA8] Entering GPU/Shader pipeline setup");
#ifdef _WIN32
  if (!safe_call_imp_sub_825F1DA8(ctx, base)) {
    TRACE("[Hooks::Graphics::sub_825F1DA8] Exception caught in __imp__sub_825F1DA8 - continuing");
    return;
  }
#else
  __imp__sub_825F1DA8(ctx, base);
#endif
  TRACE("[Hooks::Graphics::sub_825F1DA8] Finished GPU/Shader pipeline setup cleanly");
}

extern "C" REX_FUNC(sub_82620388) {
  if (ctx.r3.u32 == 0) {
    ctx.r3.u64 = 0;
    return;
  }
  PPCContext saved_ctx = ctx;
#ifdef _WIN32
  if (!safe_call_imp_sub_82620388(ctx, base)) {
    TRACE("[Hooks::Graphics::sub_82620388] Exception caught in view render - graceful continue");
    ctx = saved_ctx;
    ctx.r3.u64 = 0;
    return;
  }
#else
  __imp__sub_82620388(ctx, base);
#endif
}

extern "C" REX_FUNC(sub_822E5E10) {
  static uint64_t s_frame_count = 0;
  s_frame_count++;
  if (s_frame_count % 60 == 1) {
    TRACE("[Hooks::Graphics::sub_822E5E10] Frame " << s_frame_count << " rendered (engine active)");
  }
  __imp__sub_822E5E10(ctx, base);
}

namespace re_wor::hooks::graphics {

void register_graphics_hooks() {
}

}  // namespace re_wor::hooks::graphics
