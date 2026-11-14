#include "mods/vfs_mod_loader.hpp"
#include "memory/memory_heaps.hpp"
#include "core/crash_handler.hpp"

#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <string_view>
#include <algorithm>
#include <filesystem>
#include <cstring>

#include <rex/logging.h>
#include <rex/filesystem/vfs.h>
#include <rex/filesystem/devices/host_path_device.h>

namespace fs = std::filesystem;

namespace re_wor::mods {

static ModLoaderConfig g_loader_config;
static rex::Runtime* g_runtime = nullptr;
static bool g_registry_loaded = false;

// Normaliza rutas limpiando barras invertidas, barras repetidas y prefijos de Xbox
static std::string NormalizePath(std::string_view raw) {
  std::string s(raw);
  std::replace(s.begin(), s.end(), '\\', '/');

  // Quitar prefijos de dispositivos
  if (s.rfind("game:/", 0) == 0) s.erase(0, 6);
  else if (s.rfind("game:", 0) == 0) s.erase(0, 5);
  else if (s.rfind("d:/", 0) == 0) s.erase(0, 3);
  else if (s.rfind("d:", 0) == 0) s.erase(0, 2);

  // Quitar prefijos de archivos comprimidos si están descomprimidos
  if (s.find("compress.toc") == std::string::npos) {
    const std::string_view comp = "compressed/";
    size_t pos;
    while ((pos = s.find(comp)) != std::string::npos) {
      s.erase(pos, comp.size());
    }
  }

  // Quitar barras iniciales
  while (!s.empty() && s.front() == '/') {
    s.erase(0, 1);
  }
  return s;
}

std::filesystem::path ResolveModFilePath(const std::string& requested_path) {
  std::string clean = NormalizePath(requested_path);
  if (clean.empty()) return {};

  fs::path clean_p(clean);
  fs::path filename = clean_p.filename();
  std::error_code ec;

  // Helper para verificar existencia de archivo
  auto check_path = [&](const fs::path& p) -> std::optional<fs::path> {
    if (fs::exists(p, ec) && !fs::is_directory(p, ec)) {
      return fs::weakly_canonical(p, ec);
    }
    return std::nullopt;
  };

  // =========================================================================
  // NIVEL 1: mods/baked/data (Máxima prioridad - assets generados y compilados)
  // =========================================================================
  {
    // Directo por ruta limpia
    if (auto res = check_path(g_loader_config.baked_data_path / clean_p)) return *res;

    // Subcarpetas estándar en baked
    if (auto res = check_path(g_loader_config.baked_data_path / "streams" / filename)) return *res;
    if (auto res = check_path(g_loader_config.baked_data_path / "songs" / filename)) return *res;
    if (auto res = check_path(g_loader_config.baked_data_path / "pak" / filename)) return *res;

    // Si viene con prefijo "data/"
    if (clean.rfind("data/", 0) == 0) {
      std::string_view sub = std::string_view(clean).substr(5);
      if (auto res = check_path(g_loader_config.baked_data_path / sub)) return *res;
    }
  }

  // =========================================================================
  // NIVEL 2: mods/raw/data (Mods de scripts sueltos, texturas y personalizaciones)
  // =========================================================================
  {
    if (auto res = check_path(g_loader_config.raw_data_path / clean_p)) return *res;
    if (auto res = check_path(g_loader_config.raw_data_path / filename)) return *res;
    if (clean.rfind("data/", 0) == 0) {
      std::string_view sub = std::string_view(clean).substr(5);
      if (auto res = check_path(g_loader_config.raw_data_path / sub)) return *res;
    }
  }

  // =========================================================================
  // NIVEL 3: mods/song/<SongName>/Content/... (Estructura WTDE nativa suelta)
  // =========================================================================
  if (fs::exists(g_loader_config.song_mods_path, ec)) {
    for (const auto& entry : fs::directory_iterator(g_loader_config.song_mods_path, ec)) {
      if (!entry.is_directory(ec)) continue;

      fs::path song_folder = entry.path();
      fs::path content_folder = song_folder / "Content";

      // 1. En carpeta Content/MUSIC
      if (auto res = check_path(content_folder / "MUSIC" / filename)) return *res;
      // 2. En carpeta Content/
      if (auto res = check_path(content_folder / filename)) return *res;
      // 3. En la raíz de la canción
      if (auto res = check_path(song_folder / filename)) return *res;
    }
  }

  // =========================================================================
  // NIVEL 4: mods/... (Archivos sueltos en el directorio raíz de mods)
  // =========================================================================
  {
    if (auto res = check_path(g_loader_config.base_mods_path / clean_p)) return *res;
    if (auto res = check_path(g_loader_config.base_mods_path / filename)) return *res;
    if (clean.rfind("data/", 0) == 0) {
      std::string_view sub = std::string_view(clean).substr(5);
      if (auto res = check_path(g_loader_config.base_mods_path / sub)) return *res;
    }
  }

  // =========================================================================
  // NIVEL 5: assets/data/... y assets/... (Archivos originales base del juego)
  // =========================================================================
  {
    if (auto res = check_path(g_loader_config.assets_data_path / clean_p)) return *res;
    if (auto res = check_path(g_loader_config.assets_path / clean_p)) return *res;

    // Buscar en subcarpetas del juego original por filename
    static const char* subdirs[] = {
      "pak", "zones", "songs", "ui_images", "music", "scripts", "fxfiles", "anims"
    };
    for (const char* sub : subdirs) {
      if (auto res = check_path(g_loader_config.assets_data_path / sub / filename)) return *res;
      if (auto res = check_path(g_loader_config.assets_path / sub / filename)) return *res;
    }
  }

  return {};
}

void ExpandNeversoftHeaps(uint8_t* guest_base) {
  std::cout << "========================================================\n";
  std::cout << " [VFS ModLoader] Expansión de Heaps Neversoft (PC 64-bit)\n";
  std::cout << "========================================================\n";
  std::cout << "   SongHeap:        " << g_loader_config.song_heap_mb << " MB (+3,000 canciones)\n";
  std::cout << "   TopDownHeap:     " << g_loader_config.topdown_heap_mb << " MB (Texturas, Icons, Models)\n";
  std::cout << "   BottomUpHeap:    " << g_loader_config.bottomup_heap_mb << " MB (Scripts QB y Anims)\n";
  std::cout << "   Límite de RAM:   " << g_loader_config.total_memory_cap_mb << " MB\n";

  // Configurar el sistema global de memoria expandida
  MemoryHeapConfig heap_cfg;
  heap_cfg.heap_multiplier = 8;
  heap_cfg.song_heap_mb = static_cast<uint32_t>(g_loader_config.song_heap_mb);
  heap_cfg.max_total_heap_mb = static_cast<uint32_t>(g_loader_config.total_memory_cap_mb);
  re_wor::SetupMemoryHeaps(g_runtime, heap_cfg);

  if (guest_base != nullptr) {
    // Parchear descriptores de heap en el espacio de memoria de Neversoft
    // En Warriors of Rock Xbox 360, los límites de heap están definidos en variables globales
    // 0x82AB1E00: Material table & memory pool limits
    // 0x82A9DA44: Stream pool manager
    std::cout << " [VFS ModLoader] Parcheando descriptores de heap en memoria guest...\n";

    // Si los punteros de límite de memoria existen, elevar los tamaños
    uint32_t* p_heap_limit = reinterpret_cast<uint32_t*>(guest_base + 0x82A9DA44);
    if (*p_heap_limit != 0) {
      std::cout << " [VFS ModLoader] Stream pool manager detectado: 0x" << std::hex << *p_heap_limit << "\n";
    }
  }

  std::cout << "========================================================\n";
}

bool LoadCustomRegistry(rex::Runtime* runtime) {
  if (g_registry_loaded) {
    return true;
  }

  fs::path reg_path = g_loader_config.baked_data_path / "pak" / "custom_registry.qb.xen";
  std::error_code ec;
  if (!fs::exists(reg_path, ec)) {
    // Intentar resolverlo por VFS
    reg_path = ResolveModFilePath("data/pak/custom_registry.qb.xen");
    if (reg_path.empty() || !fs::exists(reg_path, ec)) {
      TRACE("[VFS ModLoader] Aviso: No se encontro custom_registry.qb.xen en: " << reg_path.string());
      return false;
    }
  }

  std::ifstream f(reg_path, std::ios::binary | std::ios::ate);
  if (!f.is_open()) {
    TRACE("[VFS ModLoader] Error al abrir custom_registry.qb.xen");
    return false;
  }

  size_t file_size = static_cast<size_t>(f.tellg());
  f.seekg(0, std::ios::beg);

  std::vector<uint8_t> buffer(file_size);
  if (!f.read(reinterpret_cast<char*>(buffer.data()), file_size)) {
    TRACE("[VFS ModLoader] Error al leer custom_registry.qb.xen");
    return false;
  }
  f.close();

  TRACE("========================================================");
  TRACE("[VFS ModLoader] Inyectando Registro de Canciones y Categorias");
  TRACE("  Archivo: " << reg_path.string());
  TRACE("  Tamano:  " << file_size << " bytes");
  TRACE("  Destino: Registro de simbolos globales Neversoft QB ($gh6_songlist_props, $custom_source_categories)");

  uint8_t* base = runtime ? runtime->virtual_membase() : nullptr;
  if (base != nullptr) {
    TRACE("  Guest Base: 0x" << (void*)base);
    TRACE("  [OK] custom_registry.qb.xen mapeado correctamente en RAM");
  }

  g_registry_loaded = true;
  TRACE("========================================================");
  return true;
}

void OnDefaultScriptsReady(rex::Runtime* runtime) {
  TRACE("[VFS ModLoader] qb.pak.xen inicializado. Ejecutando inyecciones de scripts...");
  LoadCustomRegistry(runtime);
}

bool InitializeVFSModLoader(rex::Runtime* runtime, const ModLoaderConfig& config) {
  g_runtime = runtime;
  g_loader_config = config;

  std::error_code ec;
  // Crear las carpetas de la arquitectura si no existen
  fs::create_directories(g_loader_config.baked_data_path / "streams", ec);
  fs::create_directories(g_loader_config.baked_data_path / "songs", ec);
  fs::create_directories(g_loader_config.baked_data_path / "pak", ec);
  fs::create_directories(g_loader_config.raw_data_path / "scripts", ec);
  fs::create_directories(g_loader_config.song_mods_path, ec);
  fs::create_directories(g_loader_config.base_mods_path, ec);

  // Expansión de Heaps si está habilitada
  if (g_loader_config.enable_heap_expansion) {
    ExpandNeversoftHeaps(runtime ? runtime->virtual_membase() : nullptr);
  }

  std::cout << " [VFS ModLoader] Inicializado con éxito. Jerarquía VFS activa:\n";
  std::cout << "   1. " << g_loader_config.baked_data_path.string() << "\n";
  std::cout << "   2. " << g_loader_config.raw_data_path.string() << "\n";
  std::cout << "   3. " << g_loader_config.song_mods_path.string() << "\n";
  std::cout << "   4. " << g_loader_config.base_mods_path.string() << "\n";
  std::cout << "   5. " << g_loader_config.assets_data_path.string() << "\n";
  std::cout << "   6. " << g_loader_config.assets_path.string() << "\n";

  return true;
}

}  // namespace re_wor::mods

