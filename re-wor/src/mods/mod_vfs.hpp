#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <rex/runtime.h>

namespace re_wor {

// Forward-compat alias y funciones en namespace raíz y re_wor::mods
bool SetupModVFS(rex::Runtime* runtime);
std::filesystem::path ResolveAssetPath(const std::string& original_path);

}  // namespace re_wor

namespace re_wor::mods {

bool initialize_mod_vfs(rex::Runtime* runtime);
std::filesystem::path resolve_asset_path(const std::string& original_path);

}  // namespace re_wor::mods

