#pragma once

#include <cstdint>
#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>
#include <memory>

#include <rex/ppc.h>
#include <rex/runtime.h>

namespace re_wor::mods {

// Configuración del cargador VFS y expansión de memoria
struct ModLoaderConfig {
  std::filesystem::path baked_data_path = "mods/baked/data";
  std::filesystem::path raw_data_path = "mods/raw/data";
  std::filesystem::path song_mods_path = "mods/song";
  std::filesystem::path base_mods_path = "mods";
  std::filesystem::path assets_data_path = "assets/data";
  std::filesystem::path assets_path = "assets";

  // Expansión de Heaps de Neversoft a arquitectura PC de 64 bits
  bool enable_heap_expansion = true;
  size_t song_heap_mb = 2048;      // 2 GB para soportar miles de canciones personalizadas
  size_t topdown_heap_mb = 1024;   // 1 GB para texturas, iconos y modelos
  size_t bottomup_heap_mb = 1024;  // 1 GB para scripts QB y animaciones
  size_t total_memory_cap_mb = 4096; // 4 GB RAM total
};

// Inicializa el cargador VFS, monta los dispositivos prioritarios y expande la memoria
bool InitializeVFSModLoader(rex::Runtime* runtime, const ModLoaderConfig& config = {});

// Resuelve la ruta física de cualquier archivo siguiendo la jerarquía de prioridad:
// 1. mods/baked/data/...
// 2. mods/raw/data/...
// 3. mods/song/<SongName>/Content/... (y mods/song/<SongName>/...)
// 4. mods/...
// 5. assets/data/...
// 6. assets/...
std::filesystem::path ResolveModFilePath(const std::string& requested_path);

// Carga e inyecta dinámicamente custom_registry.qb.xen en la memoria del motor Neversoft
bool LoadCustomRegistry(rex::Runtime* runtime);

// Expande los límites de los Heaps de Neversoft en memoria (BottomUp, TopDown, SongHeap, PakHeap)
void ExpandNeversoftHeaps(uint8_t* guest_base);

// Notifica al cargador que qb.pak.xen ha terminado de inicializarse para inyectar scripts custom
void OnDefaultScriptsReady(rex::Runtime* runtime);

}  // namespace re_wor::mods

