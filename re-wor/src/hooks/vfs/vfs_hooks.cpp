#include "hooks/vfs/vfs_hooks.hpp"
#include "core/crash_handler.hpp"
#include "mods/mod_vfs.hpp"
#include "generated/re_wor_pch.h"

#include <filesystem>
#include <fstream>
#include <cstring>
#include <string_view>

extern "C" void sub_820D0010(PPCContext& __restrict ctx, uint8_t* base);
extern "C" void sub_8256F400(PPCContext& __restrict ctx, uint8_t* base);
extern "C" void __imp__sub_82422E58(PPCContext& __restrict ctx, uint8_t* base);
extern "C" void __imp__sub_82421418(PPCContext& __restrict ctx, uint8_t* base);
extern "C" void __imp__sub_82424680(PPCContext& __restrict ctx, uint8_t* base);
extern "C" void __imp__sub_824DA068(PPCContext& __restrict ctx, uint8_t* base);
extern "C" void __imp__sub_8256E3A8(PPCContext& __restrict ctx, uint8_t* base);
extern "C" void __imp__sub_826C2B10(PPCContext& __restrict ctx, uint8_t* base);
extern "C" void __imp__sub_826C2BC8(PPCContext& __restrict ctx, uint8_t* base);

// ---------------------------------------------------------------------------
// Hook: sub_82422E58 — FileStream::Open
// Abre streams de archivos y resuelve buffers del pool
// ---------------------------------------------------------------------------
extern "C" REX_FUNC(sub_82422E58) {
  const char* filename = (ctx.r3.u32 != 0) ? reinterpret_cast<const char*>(base + ctx.r3.u32) : "NULL";
  TRACE("[Hooks::VFS::FileStream::Open] sub_82422E58 requested: '" << filename << "'");

  // Fix: Si filename es solo ".xen", redirigir a "pak/qb.pak.xen"
  if (filename && (strcmp(filename, ".xen") == 0 || strcmp(filename, "") == 0)) {
    TRACE("[Hooks::VFS::FileStream::Open] Redirecting invalid '" << filename << "' -> 'pak/qb.pak.xen'");
    static const char s_fallback_qb_pak[] = "pak/qb.pak.xen";
    ctx.r3.u64 = reinterpret_cast<uint64_t>(s_fallback_qb_pak);
    filename = s_fallback_qb_pak;
  }

  // Si el pool de streams no ha sido inicializado en 0x82A9DA44, inicializarlo perezosamente
  uint32_t pool_ptr = REX_LOAD_U32(0x82A9DA44);
  if (pool_ptr == 0) {
    TRACE("[Hooks::VFS::FileStream::Open] Stream pool at 0x82A9DA44 is NULL - calling sub_8256F400");
    PPCContext sctx = ctx;
    sctx.r3.u64 = 0;
    sub_8256F400(sctx, base);
    TRACE("[Hooks::VFS::FileStream::Open] Initialized stream pool at 0x82A9DA44 -> 0x" 
          << std::hex << REX_LOAD_U32(0x82A9DA44));
  }

  __imp__sub_82422E58(ctx, base);
  TRACE("[Hooks::VFS::FileStream::Open] sub_82422E58 result r3=0x" << std::hex << ctx.r3.u32);
}

// ---------------------------------------------------------------------------
// Hook: sub_8256E3A8 — Normalizador de rutas de streams (elimina Compressed\)
// ---------------------------------------------------------------------------
extern "C" REX_FUNC(sub_8256E3A8) {
  if (ctx.r4.u32 != 0) {
    char* path_buf = reinterpret_cast<char*>(base + ctx.r4.u32);
    std::string path_str(path_buf);
    if (path_str.find("compress.toc") == std::string::npos) {
      size_t comp_pos = std::string::npos;
      if ((comp_pos = path_str.find("compressed\\")) != std::string::npos ||
          (comp_pos = path_str.find("Compressed\\")) != std::string::npos ||
          (comp_pos = path_str.find("compressed/")) != std::string::npos ||
          (comp_pos = path_str.find("Compressed/")) != std::string::npos) {
        std::string uncomp_path = path_str.substr(0, comp_pos) + path_str.substr(comp_pos + 11);
        strncpy_s(path_buf, path_str.size() + 1, uncomp_path.c_str(), _TRUNCATE);
        TRACE("[Hooks::VFS::sub_8256E3A8] Redirected: '" << path_str << "' -> '" << path_buf << "'");
      }
    }
    TRACE("[Hooks::VFS::sub_8256E3A8] Opening stream path: '" << path_buf << "' (flags=0x" 
          << std::hex << ctx.r5.u32 << ")");
  }
  __imp__sub_8256E3A8(ctx, base);
  TRACE("[Hooks::VFS::sub_8256E3A8] Result r3=0x" << std::hex << ctx.r3.u32);
}

// ---------------------------------------------------------------------------
// Hooks de NtCreateFile y eliminacion
// ---------------------------------------------------------------------------
extern "C" REX_FUNC(sub_826C2B10) {
  const char* path = (ctx.r3.u32 != 0) ? reinterpret_cast<const char*>(base + ctx.r3.u32) : "NULL";
  TRACE("[Hooks::VFS::sub_826C2B10] DeleteFile for: '" << (path ? path : "NULL") << "'");
  __imp__sub_826C2B10(ctx, base);
  TRACE("[Hooks::VFS::sub_826C2B10] result r3=" << ctx.r3.u32);
}

extern "C" REX_FUNC(sub_826C2BC8) {
  const char* path = (ctx.r3.u32 != 0) ? reinterpret_cast<const char*>(base + ctx.r3.u32) : "NULL";
  TRACE("[Hooks::VFS::sub_826C2BC8] NtCreateFile path: '" << (path ? path : "NULL") 
        << "' (access=0x" << std::hex << ctx.r4.u32 << ")");
  __imp__sub_826C2BC8(ctx, base);
  TRACE("[Hooks::VFS::sub_826C2BC8] result handle=0x" << std::hex << ctx.r3.u32);
}

// ---------------------------------------------------------------------------
// Hooks con NULL guards para GetSize y Read
// ---------------------------------------------------------------------------
extern "C" REX_FUNC(sub_82421418) {
  if (ctx.r3.u32 == 0) {
    TRACE("[Hooks::VFS::CRASH_PREVENT] sub_82421418 called with NULL r3!");
    ctx.r3.s64 = -1;
    return;
  }
  __imp__sub_82421418(ctx, base);
}

extern "C" REX_FUNC(sub_82424680) {
  if (ctx.r3.u32 == 0) {
    TRACE("[Hooks::VFS::CRASH_PREVENT] sub_82424680 called with NULL r3!");
    ctx.r3.u64 = 0;
    return;
  }
  __imp__sub_82424680(ctx, base);
}

// ---------------------------------------------------------------------------
// Hook: sub_824DA068 — LoadFile resolución multi-root VFS
// ---------------------------------------------------------------------------
extern "C" REX_FUNC(sub_824DA068) {
  const char* rel_path = (ctx.r3.u32 != 0) ? reinterpret_cast<const char*>(base + ctx.r3.u32) : "";
  uint32_t out_size_ptr = ctx.r4.u32;
  uint32_t dest_buf = ctx.r5.u32;

  TRACE("[Hooks::VFS::LoadFile] Requesting: '" << rel_path << "' (dest=0x" << std::hex << dest_buf << ")");

  std::filesystem::path resolved = re_wor::ResolveAssetPath(rel_path);

  if (std::filesystem::exists(resolved) && !std::filesystem::is_directory(resolved)) {
    std::ifstream file(resolved, std::ios::binary | std::ios::ate);
    if (file.is_open()) {
      std::streamsize file_size = file.tellg();
      file.seekg(0, std::ios::beg);

      uint32_t target_buf = dest_buf;
      if (target_buf == 0) {
        PPCContext alloc_ctx = ctx;
        alloc_ctx.r3.u64 = static_cast<uint64_t>(file_size + 64);
        sub_820D0010(alloc_ctx, base);
        target_buf = alloc_ctx.r3.u32;
      }

      if (target_buf != 0) {
        file.read(reinterpret_cast<char*>(base + target_buf), file_size);
        if (out_size_ptr != 0) {
          REX_STORE_U32(out_size_ptr, static_cast<uint32_t>(file_size));
        }
        TRACE("[Hooks::VFS::LoadFile] SUCCESS: Loaded '" << resolved.string() 
              << "' (" << std::dec << file_size << " bytes) -> 0x" << std::hex << target_buf);
        ctx.r3.u64 = target_buf;
        return;
      }
    }
  }

  __imp__sub_824DA068(ctx, base);
  TRACE("[Hooks::VFS::LoadFile] Fallback for '" << rel_path << "' returned: 0x" << std::hex << ctx.r3.u32);
}

namespace re_wor::hooks::vfs {

void register_vfs_hooks() {
}

}  // namespace re_wor::hooks::vfs

