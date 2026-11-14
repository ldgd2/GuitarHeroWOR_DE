#include "hooks/init/crt_init.hpp"
#include "core/crash_handler.hpp"
#include "generated/re_wor_pch.h"

#ifdef _WIN32
#include <windows.h>
#endif

#ifdef REWOR_DEV
#include "diagnostics/stub_tracker.hpp"
#include <unordered_set>
#endif

#ifndef PPC_LOAD_U32
#define PPC_LOAD_U32(addr) REX_LOAD_U32(addr)
#endif
#ifndef PPC_LOOKUP_FUNC
#define PPC_LOOKUP_FUNC(base, addr) REX_LOOKUP_FUNC(base, addr)
#endif
#ifndef PPC_FUNC
#define PPC_FUNC(name) REX_FUNC(name)
#endif
#ifndef PPC_CODE_BASE
#define PPC_CODE_BASE REX_CODE_BASE
#define PPC_CODE_SIZE REX_CODE_SIZE
#define PPC_IMAGE_BASE REX_IMAGE_BASE
#define PPC_IMAGE_SIZE REX_IMAGE_SIZE
#endif

static void safe_call_ctor(PPCContext& ctx, uint8_t* base, uint32_t ppc_addr,
                           const char* source, int& called, int& skipped, int& crashed) {
  auto* fn = PPC_LOOKUP_FUNC(base, ppc_addr);
  if (!fn) {
    skipped++;
    return;
  }
#ifdef _WIN32
  __try {
    fn(ctx, base);
    called++;
  } __except(EXCEPTION_EXECUTE_HANDLER) {
    TRACE("[Hooks::Init::ctor] CRASH in 0x" << std::hex << ppc_addr << " (" << source 
          << ") exception=0x" << GetExceptionCode() << " - skipping");
    crashed++;
  }
#else
  fn(ctx, base);
  called++;
#endif
}

extern "C" PPC_FUNC(sub_822CCB60) {
  TRACE("[Hooks::Init::sub_822CCB60] Safe CRT init started");
  int called = 0, skipped = 0, crashed = 0;

  // Stage 1: Single callback at 0x82064074
  uint32_t fn_addr = 0;
  bool stage1_readable = false;
#ifdef _WIN32
  __try {
    fn_addr = PPC_LOAD_U32(0x82064074);
    stage1_readable = true;
  } __except(EXCEPTION_EXECUTE_HANDLER) {
    TRACE("[Hooks::Init::sub_822CCB60] Cannot read stage1 addr 0x82064074 - skipping");
  }
#else
  fn_addr = PPC_LOAD_U32(0x82064074);
  stage1_readable = true;
#endif
  if (stage1_readable && fn_addr != 0) {
    safe_call_ctor(ctx, base, fn_addr, "stage1", called, skipped, crashed);
  }

  // Stage 2: Table 1 (0x827D2720 to 0x827D272C)
  TRACE("[Hooks::Init::sub_822CCB60] Stage 2: table1 (0x827D2720-0x827D272C)");
  for (uint32_t ea = 0x827D2720; ea < 0x827D272C; ea += 4) {
    fn_addr = PPC_LOAD_U32(ea);
    if (fn_addr != 0) {
      safe_call_ctor(ctx, base, fn_addr, "table1", called, skipped, crashed);
    }
  }

  // Stage 3: Table 2 (0x827D0414 to 0x827D271C)  
  TRACE("[Hooks::Init::sub_822CCB60] Stage 3: table2 (0x827D0414-0x827D271C)");
  for (uint32_t ea = 0x827D0414; ea < 0x827D271C; ea += 4) {
    fn_addr = PPC_LOAD_U32(ea);
    if (fn_addr != 0 && fn_addr != 0xFFFFFFFF) {
      safe_call_ctor(ctx, base, fn_addr, "table2", called, skipped, crashed);
    }
  }

  TRACE("[Hooks::Init::sub_822CCB60] Done: called=" << std::dec << called 
        << " skipped=" << skipped << " crashed=" << crashed);
}

#ifdef REWOR_DEV
static std::unordered_set<uint32_t> g_logged_unmapped;
#endif

static PPC_FUNC(unmapped_func_stub) {
  uint32_t target = ctx.ctr.u32;
  uint32_t caller = static_cast<uint32_t>(ctx.lr);
#ifdef REWOR_DEV
  if (g_logged_unmapped.find(target) == g_logged_unmapped.end()) {
    g_logged_unmapped.insert(target);
    TRACE("[UNMAPPED] PPC call to 0x" << std::hex << target 
          << " from 0x" << caller << " - stubbed (total: " 
          << std::dec << g_logged_unmapped.size() << ")");
  }
  re_wor::diagnostics::StubTracker::instance().record_unmapped(target, caller);
#endif
  ctx.r3.u64 = 0;
}

namespace re_wor::hooks::init {

void fill_dispatch_table(uint8_t* base) {
  TRACE("[Hooks::Init] Filling null entries in function dispatch table...");
  auto* table_base = reinterpret_cast<PPCFunc**>(
      base + PPC_IMAGE_BASE + PPC_IMAGE_SIZE);
  size_t num_entries = PPC_CODE_SIZE / 4;
  size_t filled = 0;
  for (size_t i = 0; i < num_entries; i++) {
    if (table_base[i] == nullptr) {
      table_base[i] = &unmapped_func_stub;
      filled++;
    }
  }
  TRACE("[Hooks::Init] Filled " << std::dec << filled << " null entries out of " 
        << num_entries << " total (" << (num_entries - filled) << " mapped)");
}

}  // namespace re_wor::hooks::init
