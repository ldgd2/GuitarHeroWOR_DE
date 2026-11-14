#include "core/path_resolver.hpp"

#include <vector>
#include <iostream>

#ifdef _WIN32
#include <windows.h>
#endif

namespace re_wor::core {

std::filesystem::path get_executable_directory() {
#ifdef _WIN32
  char buffer[MAX_PATH];
  DWORD len = GetModuleFileNameA(NULL, buffer, MAX_PATH);
  if (len > 0 && len < MAX_PATH) {
    std::error_code ec;
    return std::filesystem::path(buffer).parent_path();
  }
#endif
  std::error_code ec;
  return std::filesystem::current_path(ec);
}

std::filesystem::path get_base_directory() {
  static std::filesystem::path s_base_dir;
  if (!s_base_dir.empty()) {
    return s_base_dir;
  }

  auto exe_dir = get_executable_directory();
  std::error_code ec;
  auto cwd = std::filesystem::current_path(ec);

  // Lista prioritaria de candidatos para localizar la raíz de assets
  std::vector<std::filesystem::path> candidates = {
    exe_dir,
    exe_dir / ".." / ".." / "..",       // out/build/win-amd64-relwithdebinfo -> re-wor
    exe_dir / ".." / ".." / ".." / "..", // out/build/... -> root
    exe_dir / "..",
    exe_dir / ".." / "..",
    cwd,
    cwd / "re-wor",
    cwd / ".."
  };

  for (const auto& cand : candidates) {
    if (cand.empty()) continue;
    std::error_code check_ec;
    auto p_assets = cand / "assets";
    auto p_assets_data = cand / "assets" / "data";
    auto p_data = cand / "data";
    if (std::filesystem::is_directory(p_assets_data, check_ec) ||
        std::filesystem::is_directory(p_assets, check_ec) ||
        std::filesystem::is_directory(p_data, check_ec)) {
      s_base_dir = std::filesystem::weakly_canonical(cand, check_ec);
      return s_base_dir;
    }
  }

  // Fallback seguro: directorio del ejecutable
  s_base_dir = exe_dir;
  return s_base_dir;
}

std::filesystem::path get_assets_directory() {
  auto base = get_base_directory();
  std::error_code ec;
  if (std::filesystem::is_directory(base / "assets", ec)) {
    return base / "assets";
  }
  if (std::filesystem::is_directory(base / "data", ec)) {
    return base;
  }
  return base / "assets";
}

std::filesystem::path get_mods_directory() {
  return get_base_directory() / "mods";
}

std::filesystem::path get_user_directory() {
  return get_base_directory() / "user";
}

std::filesystem::path get_cache_directory() {
  return get_base_directory() / "cache";
}

}  // namespace re_wor::core

