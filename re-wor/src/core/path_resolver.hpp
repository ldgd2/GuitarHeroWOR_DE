#pragma once

#include <filesystem>
#include <string>

namespace re_wor::core {

// Devuelve el directorio donde se ubica el ejecutable del proceso actual.
std::filesystem::path get_executable_directory();

// Resuelve dinámicamente la carpeta raíz del proyecto (donde residen assets/ y mods/)
// analizando la ubicación del binario y sus carpetas contenedoras.
// NUNCA hardcodea rutas absolutas.
std::filesystem::path get_base_directory();

// Rutas derivadas dinámicamente
std::filesystem::path get_assets_directory();
std::filesystem::path get_mods_directory();
std::filesystem::path get_user_directory();
std::filesystem::path get_cache_directory();

}  // namespace re_wor::core

