#include "mods/mod_vfs.hpp"
#include "mods/vfs_mod_loader.hpp"
#include "core/path_resolver.hpp"
#include "core/crash_handler.hpp"
#include "de/mod_manager.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <iostream>
#include <fstream>
#include <vector>
#include <rex/filesystem/device.h>
#include <rex/filesystem/devices/host_path_device.h>
#include <rex/filesystem/devices/null_device.h>
#include <rex/filesystem/vfs.h>
#include <rex/system/xio.h>
#include <rex/logging.h>

namespace re_wor::mods {

static std::filesystem::path g_mods_root = "mods";
static std::filesystem::path g_assets_root = "assets";
static std::filesystem::path g_pak_mods_root = "assets/data/pak/mods";
static std::filesystem::path g_data_mods_root = "assets/data/mods";

class ModOverlayDevice : public rex::filesystem::Device {
 public:
  ModOverlayDevice(const std::string_view mount_path,
                   const std::filesystem::path& mods_path,
                   const std::filesystem::path& pak_mods_path,
                   const std::filesystem::path& data_mods_path,
                   const std::filesystem::path& base_path)
      : rex::filesystem::Device(mount_path),
        name_("ModOverlayVFS"),
        mods_path_(mods_path),
        pak_mods_path_(pak_mods_path),
        data_mods_path_(data_mods_path),
        base_path_(base_path),
        data_path_(base_path / "data"),
        pak_path_(base_path / "data" / "pak"),
        zones_path_(base_path / "data" / "zones"),
        songs_path_(base_path / "data" / "songs"),
        ui_path_(base_path / "data" / "ui_images"),
        music_path_(base_path / "data" / "music"),
        movies_path_(base_path / "data" / "moviestreams"),
        scripts_path_(base_path / "data" / "scripts"),
        fx_path_(base_path / "data" / "fxfiles"),
        anims_path_(base_path / "data" / "anims"),
        compressed_pak_path_(base_path / "data" / "compressed" / "PAK"),
        compressed_zones_path_(base_path / "data" / "compressed" / "ZONES"),
        compressed_songs_path_(base_path / "data" / "compressed" / "SONGS"),
        compressed_ui_path_(base_path / "data" / "compressed" / "UI_IMAGES"),
        compressed_jams_path_(base_path / "data" / "compressed" / "JAMS"),
        compressed_fx_path_(base_path / "data" / "compressed" / "FXFILES") {
    mods_device_ = std::make_unique<rex::filesystem::HostPathDevice>(
        mount_path, mods_path, /*read_only=*/false, /*allow_share_delete=*/true);
    pak_mods_device_ = std::make_unique<rex::filesystem::HostPathDevice>(
        mount_path, pak_mods_path, /*read_only=*/false, /*allow_share_delete=*/true);
    data_mods_device_ = std::make_unique<rex::filesystem::HostPathDevice>(
        mount_path, data_mods_path, /*read_only=*/false, /*allow_share_delete=*/true);
    base_device_ = std::make_unique<rex::filesystem::HostPathDevice>(
        mount_path, base_path, /*read_only=*/true, /*allow_share_delete=*/false);
    data_device_ = std::make_unique<rex::filesystem::HostPathDevice>(
        mount_path, data_path_, /*read_only=*/true, /*allow_share_delete=*/false);
    pak_device_ = std::make_unique<rex::filesystem::HostPathDevice>(
        mount_path, pak_path_, /*read_only=*/true, /*allow_share_delete=*/false);
    zones_device_ = std::make_unique<rex::filesystem::HostPathDevice>(
        mount_path, zones_path_, /*read_only=*/true, /*allow_share_delete=*/false);
    songs_device_ = std::make_unique<rex::filesystem::HostPathDevice>(
        mount_path, songs_path_, /*read_only=*/true, /*allow_share_delete=*/false);
    ui_device_ = std::make_unique<rex::filesystem::HostPathDevice>(
        mount_path, ui_path_, /*read_only=*/true, /*allow_share_delete=*/false);
    music_device_ = std::make_unique<rex::filesystem::HostPathDevice>(
        mount_path, music_path_, /*read_only=*/true, /*allow_share_delete=*/false);
    movies_device_ = std::make_unique<rex::filesystem::HostPathDevice>(
        mount_path, movies_path_, /*read_only=*/true, /*allow_share_delete=*/false);
    scripts_device_ = std::make_unique<rex::filesystem::HostPathDevice>(
        mount_path, scripts_path_, /*read_only=*/true, /*allow_share_delete=*/false);
    fx_device_ = std::make_unique<rex::filesystem::HostPathDevice>(
        mount_path, fx_path_, /*read_only=*/true, /*allow_share_delete=*/false);
    anims_device_ = std::make_unique<rex::filesystem::HostPathDevice>(
        mount_path, anims_path_, /*read_only=*/true, /*allow_share_delete=*/false);
    compressed_pak_device_ = std::make_unique<rex::filesystem::HostPathDevice>(
        mount_path, compressed_pak_path_, /*read_only=*/true, /*allow_share_delete=*/false);
    compressed_zones_device_ = std::make_unique<rex::filesystem::HostPathDevice>(
        mount_path, compressed_zones_path_, /*read_only=*/true, /*allow_share_delete=*/false);
    compressed_songs_device_ = std::make_unique<rex::filesystem::HostPathDevice>(
        mount_path, compressed_songs_path_, /*read_only=*/true, /*allow_share_delete=*/false);
    compressed_ui_device_ = std::make_unique<rex::filesystem::HostPathDevice>(
        mount_path, compressed_ui_path_, /*read_only=*/true, /*allow_share_delete=*/false);
    compressed_jams_device_ = std::make_unique<rex::filesystem::HostPathDevice>(
        mount_path, compressed_jams_path_, /*read_only=*/true, /*allow_share_delete=*/false);
    compressed_fx_device_ = std::make_unique<rex::filesystem::HostPathDevice>(
        mount_path, compressed_fx_path_, /*read_only=*/true, /*allow_share_delete=*/false);
  }

  bool Initialize() override {
    std::error_code ec;
    auto ensure_dir = [&](const std::filesystem::path& p) {
      if (!std::filesystem::exists(p, ec)) {
        std::filesystem::create_directories(p, ec);
      }
    };

    ensure_dir(mods_path_);
    ensure_dir(pak_mods_path_);
    ensure_dir(data_mods_path_);

    bool mods_ok = mods_device_->Initialize();
    bool pak_mods_ok = pak_mods_device_->Initialize();
    bool data_mods_ok = data_mods_device_->Initialize();
    bool base_ok = base_device_->Initialize();
    bool data_ok = data_device_->Initialize();
    pak_device_->Initialize();
    zones_device_->Initialize();
    songs_device_->Initialize();
    ui_device_->Initialize();
    music_device_->Initialize();
    movies_device_->Initialize();
    scripts_device_->Initialize();
    fx_device_->Initialize();
    anims_device_->Initialize();
    compressed_pak_device_->Initialize();
    compressed_zones_device_->Initialize();
    compressed_songs_device_->Initialize();
    compressed_ui_device_->Initialize();
    compressed_jams_device_->Initialize();
    compressed_fx_device_->Initialize();

    std::cout << "[ModVFS] Initialized multi-root overlay:\n";
    std::cout << "[ModVFS]   1. Root mods:          '" << mods_path_.string() << "' (ok=" << mods_ok << ")\n";
    std::cout << "[ModVFS]   2. PAK mods:           '" << pak_mods_path_.string() << "' (ok=" << pak_mods_ok << ")\n";
    std::cout << "[ModVFS]   3. Data mods:          '" << data_mods_path_.string() << "' (ok=" << data_mods_ok << ")\n";
    std::cout << "[ModVFS]   4. Base assets:        '" << base_path_.string() << "' (ok=" << base_ok << ")\n";
    std::cout << "[ModVFS]   5. Data dir:           '" << data_path_.string() << "' (ok=" << data_ok << ")\n";
    std::cout << "[ModVFS]   6. PAK dir:            '" << pak_path_.string() << "'\n";
    std::cout << "[ModVFS]   7. Zones dir:          '" << zones_path_.string() << "'\n";
    std::cout << "[ModVFS]   8. UI dir:             '" << ui_path_.string() << "'\n";

    return base_ok || data_ok || mods_ok;
  }

  void Dump(rex::string::StringBuffer* string_buffer) override {
    if (mods_device_) mods_device_->Dump(string_buffer);
    if (pak_mods_device_) pak_mods_device_->Dump(string_buffer);
    if (data_mods_device_) data_mods_device_->Dump(string_buffer);
    if (base_device_) base_device_->Dump(string_buffer);
    if (data_device_) data_device_->Dump(string_buffer);
  }

  // Resolucion del overlay de mods.
  // IMPORTANTE: los mods solo tienen prioridad cuando existe un archivo con la
  // MISMA ruta (o, para .pak/.pab, el mismo nombre en assets/data/pak/mods).
  // En cualquier otro caso se delega al dispositivo base con la ruta ORIGINAL,
  // exactamente igual que el HostPathDevice que monta el runtime por defecto
  // (sin quitar "Compressed\" ni buscar por nombre en otras carpetas), para no
  // cambiar la logica de carga del motor.
  rex::filesystem::Entry* ResolvePath(const std::string_view path) override {
    return ResolvePathImpl(path);
  }
  rex::filesystem::Entry* ResolvePathImpl(const std::string_view path) {
    std::string_view clean_path = path;
    while (!clean_path.empty() && (clean_path.front() == '\\' || clean_path.front() == '/')) {
      clean_path.remove_prefix(1);
    }

    std::string_view filename = clean_path;
    auto last_slash = filename.find_last_of("\\/");
    if (last_slash != std::string_view::npos) {
      filename = filename.substr(last_slash + 1);
    }

    if (!clean_path.empty()) {
      // 1. Mods por carpeta (mods/<Mod>/data/...): solo archivos NUEVOS, es
      //    decir, que no existen en el juego base. Los reemplazos de archivos
      //    existentes se inyectan en el arbol base al arrancar (ModManager).
      {
        std::string mod_name;
        if (auto* entry = re_wor::de::ModManager::Instance().ResolveNew(clean_path, &mod_name)) {
          re_wor::core::log_trace_message("[ModVFS] MOD nuevo: " + std::string(path) + " -> [" +
                                          mod_name + "]");
          return entry;
        }
      }
      // 1b. Compatibilidad: mods/<ruta exacta> (archivos sueltos).
      if (mods_device_) {
        if (auto* entry = mods_device_->ResolvePath(clean_path)) {
          if (!(entry->attributes() & rex::system::X_FILE_ATTRIBUTE_DIRECTORY)) {
            re_wor::core::log_trace_message("[ModVFS] MOD override: " + std::string(path) + " -> " + entry->absolute_path());
            return entry;
          }
        }
      }

      // 2. assets/data/pak/mods/<nombre>.pak.xen|.pab.xen (estilo GHWT:DE)
      auto ends_with = [](std::string_view s, std::string_view suf) {
        if (s.size() < suf.size()) return false;
        for (size_t i = 0; i < suf.size(); i++) {
          if (std::tolower(static_cast<unsigned char>(s[s.size() - suf.size() + i])) != suf[i]) return false;
        }
        return true;
      };
      if (pak_mods_device_ && (ends_with(filename, ".pak.xen") || ends_with(filename, ".pab.xen"))) {
        if (auto* entry = pak_mods_device_->ResolvePath(filename)) {
          re_wor::core::log_trace_message("[ModVFS] PAK mod override: " + std::string(path) + " -> " + entry->absolute_path());
          return entry;
        }
      }
    }

    // 3. Juego base, ruta original sin modificar (con compatibilidad de layout).
    return ResolveBase(path);
  }

  // Resuelve SOLO en el juego base (assets/). Tambien la usa ModManager para
  // localizar el archivo original que un mod reemplaza.
  rex::filesystem::Entry* ResolveBase(const std::string_view path) {
    if (!base_device_) return nullptr;
    if (auto* entry = base_device_->ResolvePath(path)) {
      return entry;
    }

    // Compatibilidad: si los archivos de data/compressed/ se movieron a data/
    // (layout "suelto"), reintentar quitando el segmento "compressed".
    // Solo se usa cuando la ruta original NO existe, asi que con un layout de
    // disco original el comportamiento es identico al runtime por defecto.
    std::string stripped(path);
    std::string lower(stripped);
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    for (const char* seg : {"compressed\\", "compressed/"}) {
      size_t pos = lower.find(seg);
      if (pos != std::string::npos && lower.find("compress.toc") == std::string::npos) {
        stripped.erase(pos, strlen(seg));
        // El motor mezcla separadores ("Compressed\pak/qb.pak.xen").
        std::replace(stripped.begin(), stripped.end(), '/', '\\');
        if (auto* entry = base_device_->ResolvePath(stripped)) {
          return entry;
        }
        break;
      }
    }
    return nullptr;
  }

  bool is_read_only() const override { return false; }
  const std::string& name() const override { return name_; }
  uint32_t attributes() const override { return base_device_ ? base_device_->attributes() : 0; }
  uint32_t component_name_max_length() const override { return 255; }
  uint32_t total_allocation_units() const override { return 1024 * 1024; }
  uint32_t available_allocation_units() const override { return 1024 * 1024; }
  uint32_t sectors_per_allocation_unit() const override { return 1; }
  uint32_t bytes_per_sector() const override { return 0x200; }

 private:
  std::string name_;
  std::filesystem::path mods_path_;
  std::filesystem::path pak_mods_path_;
  std::filesystem::path data_mods_path_;
  std::filesystem::path base_path_;
  std::filesystem::path data_path_;
  std::filesystem::path pak_path_;
  std::filesystem::path zones_path_;
  std::filesystem::path songs_path_;
  std::filesystem::path ui_path_;
  std::filesystem::path music_path_;
  std::filesystem::path movies_path_;
  std::filesystem::path scripts_path_;
  std::filesystem::path fx_path_;
  std::filesystem::path anims_path_;
  std::filesystem::path compressed_pak_path_;
  std::filesystem::path compressed_zones_path_;
  std::filesystem::path compressed_songs_path_;
  std::filesystem::path compressed_ui_path_;
  std::filesystem::path compressed_jams_path_;
  std::filesystem::path compressed_fx_path_;
  std::unique_ptr<rex::filesystem::HostPathDevice> mods_device_;
  std::unique_ptr<rex::filesystem::HostPathDevice> pak_mods_device_;
  std::unique_ptr<rex::filesystem::HostPathDevice> data_mods_device_;
  std::unique_ptr<rex::filesystem::HostPathDevice> base_device_;
  std::unique_ptr<rex::filesystem::HostPathDevice> data_device_;
  std::unique_ptr<rex::filesystem::HostPathDevice> pak_device_;
  std::unique_ptr<rex::filesystem::HostPathDevice> zones_device_;
  std::unique_ptr<rex::filesystem::HostPathDevice> songs_device_;
  std::unique_ptr<rex::filesystem::HostPathDevice> ui_device_;
  std::unique_ptr<rex::filesystem::HostPathDevice> music_device_;
  std::unique_ptr<rex::filesystem::HostPathDevice> movies_device_;
  std::unique_ptr<rex::filesystem::HostPathDevice> scripts_device_;
  std::unique_ptr<rex::filesystem::HostPathDevice> fx_device_;
  std::unique_ptr<rex::filesystem::HostPathDevice> anims_device_;
  std::unique_ptr<rex::filesystem::HostPathDevice> compressed_pak_device_;
  std::unique_ptr<rex::filesystem::HostPathDevice> compressed_zones_device_;
  std::unique_ptr<rex::filesystem::HostPathDevice> compressed_songs_device_;
  std::unique_ptr<rex::filesystem::HostPathDevice> compressed_ui_device_;
  std::unique_ptr<rex::filesystem::HostPathDevice> compressed_jams_device_;
  std::unique_ptr<rex::filesystem::HostPathDevice> compressed_fx_device_;
};

bool initialize_mod_vfs(rex::Runtime* runtime) {
  if (!runtime || !runtime->file_system()) {
    std::cerr << "[ModVFS] Error: runtime or file_system is null\n";
    return false;
  }

  auto* fs = runtime->file_system();
  g_assets_root = re_wor::core::get_assets_directory();
  g_mods_root = re_wor::core::get_mods_directory();
  g_pak_mods_root = g_assets_root / "data" / "pak" / "mods";
  g_data_mods_root = g_assets_root / "data" / "mods";

  std::error_code ec;
  auto create_readme = [&](const std::filesystem::path& dir, const std::string& title) {
    if (!std::filesystem::exists(dir, ec)) {
      std::filesystem::create_directories(dir, ec);
    }
    std::ofstream readme(dir / "README.txt");
    if (readme.is_open()) {
      readme << title << "\n";
      readme << "====================================================\n\n";
      readme << "Drop your modified .pak.xen, .qb.xen, .fsb.xen, or loose song/script files here.\n";
      readme << "Files in this directory take priority over original assets.\n";
      readme.close();
    }
  };

  create_readme(g_mods_root, "Guitar Hero: Warriors of Rock (re-wor) - Root Mods Directory");
  create_readme(g_pak_mods_root, "Guitar Hero: Warriors of Rock (re-wor) - PAK/MODS Directory (GHWT:DE Compatible)");
  create_readme(g_data_mods_root, "Guitar Hero: Warriors of Rock (re-wor) - DATA/MODS Directory");

  const std::string mount_path = "\\Device\\Harddisk0\\Partition1";
  const std::string null_mount = "\\Device\\Harddisk0";

  // Unregister Partition1 and NullDevice so Partition1 can be registered FIRST.
  fs->UnregisterDevice(mount_path);
  fs->UnregisterDevice(null_mount);

  auto overlay_device = std::make_unique<ModOverlayDevice>(
      mount_path, g_mods_root, g_pak_mods_root, g_data_mods_root, g_assets_root);
  if (!overlay_device->Initialize()) {
    std::cerr << "[ModVFS] Failed to initialize ModOverlayDevice\n";
    return false;
  }

  // Mods por carpeta: escanear e inyectar los reemplazos en el arbol base.
  {
    auto& mm = re_wor::de::ModManager::Instance();
    mm.set_mount_path(mount_path);
    mm.Initialize(g_mods_root);
    auto* overlay = overlay_device.get();
    mm.ApplyOverrides([overlay](std::string_view p) { return overlay->ResolveBase(p); });
  }

  if (!fs->RegisterDevice(std::move(overlay_device))) {
    std::cerr << "[ModVFS] Failed to register ModOverlayDevice\n";
    return false;
  }

  // Re-register NullDevice AFTER Partition1 so Partition1 has precedence
  auto null_device = std::make_unique<rex::filesystem::NullDevice>(
      null_mount, std::initializer_list<std::string>{"\\Partition0", "\\Cache0", "\\Cache1"});
  if (null_device->Initialize()) {
    fs->RegisterDevice(std::move(null_device));
  }

  // Ensure symlinks point to our overlay mount
  fs->RegisterSymbolicLink("game:", mount_path);
  fs->RegisterSymbolicLink("d:", mount_path);

  std::cout << "[ModVFS] Successfully mounted Multi-Root Mod Overlay at " << mount_path << "\n";
  return true;
}

std::filesystem::path resolve_asset_path(const std::string& original_path) {
  // 0. Use 5-tier VFS architecture resolver first
  auto mod_res = re_wor::mods::ResolveModFilePath(original_path);
  if (!mod_res.empty() && std::filesystem::exists(mod_res)) {
    return mod_res;
  }

  // Handle (null) in filenames due to missing language / localized string pointers
  std::string path_str = original_path;
  if (path_str.find("(null)") != std::string::npos) {
    if (path_str.find("loading") != std::string::npos || path_str.find("LOADING") != std::string::npos) {
      std::filesystem::path c_spa = g_assets_root / "data" / "ui_images" / "LOADING" / "ingame_loading_s.img.xen";
      if (std::filesystem::exists(c_spa)) return c_spa;
      std::filesystem::path c_def = g_assets_root / "data" / "ui_images" / "LOADING" / "ingame_loading.img.xen";
      if (std::filesystem::exists(c_def)) return c_def;
    }
  }

  std::filesystem::path rel(path_str);
  std::filesystem::path filename = rel.filename();

  // 1. Root mods (full relative path)
  std::filesystem::path p_mod = g_mods_root / rel;
  if (std::filesystem::exists(p_mod) && !std::filesystem::is_directory(p_mod)) return p_mod;

  // 2. Pak mods (filename in assets/data/pak/mods/)
  std::filesystem::path p_pak_mod = g_pak_mods_root / filename;
  if (std::filesystem::exists(p_pak_mod) && !std::filesystem::is_directory(p_pak_mod)) return p_pak_mod;

  // 3. Data mods (filename in assets/data/mods/)
  std::filesystem::path p_data_mod = g_data_mods_root / filename;
  if (std::filesystem::exists(p_data_mod) && !std::filesystem::is_directory(p_data_mod)) return p_data_mod;

  // 4. Base assets (exact relative path)
  std::filesystem::path p_base = g_assets_root / rel;
  if (std::filesystem::exists(p_base) && !std::filesystem::is_directory(p_base)) return p_base;

  // 5. Data relative path (assets/data/...)
  std::filesystem::path p_data = g_assets_root / "data" / rel;
  if (std::filesystem::exists(p_data) && !std::filesystem::is_directory(p_data)) return p_data;

  // 6. Direct lookup by filename in subfolders
  std::filesystem::path p_pak = g_assets_root / "data" / "pak" / filename;
  if (std::filesystem::exists(p_pak) && !std::filesystem::is_directory(p_pak)) return p_pak;

  std::filesystem::path p_fx = g_assets_root / "data" / "fxfiles" / filename;
  if (std::filesystem::exists(p_fx) && !std::filesystem::is_directory(p_fx)) return p_fx;

  std::filesystem::path p_zones = g_assets_root / "data" / "zones" / filename;
  if (std::filesystem::exists(p_zones) && !std::filesystem::is_directory(p_zones)) return p_zones;

  std::filesystem::path p_songs = g_assets_root / "data" / "songs" / filename;
  if (std::filesystem::exists(p_songs) && !std::filesystem::is_directory(p_songs)) return p_songs;

  std::filesystem::path p_ui = g_assets_root / "data" / "ui_images" / filename;
  if (std::filesystem::exists(p_ui) && !std::filesystem::is_directory(p_ui)) return p_ui;

  std::filesystem::path p_ui_load = g_assets_root / "data" / "ui_images" / "LOADING" / filename;
  if (std::filesystem::exists(p_ui_load) && !std::filesystem::is_directory(p_ui_load)) return p_ui_load;

  std::filesystem::path p_music = g_assets_root / "data" / "music" / filename;
  if (std::filesystem::exists(p_music) && !std::filesystem::is_directory(p_music)) return p_music;

  std::filesystem::path p_scripts = g_assets_root / "data" / "scripts" / filename;
  if (std::filesystem::exists(p_scripts) && !std::filesystem::is_directory(p_scripts)) return p_scripts;

  std::filesystem::path p_anims = g_assets_root / "data" / "anims" / filename;
  if (std::filesystem::exists(p_anims) && !std::filesystem::is_directory(p_anims)) return p_anims;

  std::filesystem::path p_mov = g_assets_root / "data" / "moviestreams" / filename;
  if (std::filesystem::exists(p_mov) && !std::filesystem::is_directory(p_mov)) return p_mov;

  return p_base;
}

}  // namespace re_wor::mods

namespace re_wor {

bool SetupModVFS(rex::Runtime* runtime) {
  return re_wor::mods::initialize_mod_vfs(runtime);
}

std::filesystem::path ResolveAssetPath(const std::string& original_path) {
  return re_wor::mods::resolve_asset_path(original_path);
}

}  // namespace re_wor

