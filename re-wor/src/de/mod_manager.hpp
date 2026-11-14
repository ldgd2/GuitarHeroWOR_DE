#pragma once
// =============================================================================
// ModManager - Mods por carpeta, estilo GHWT:DE.
//
//   mods/
//     MiSkinDeHighway/
//       mod.toml          <- nombre, autor, version, tipo (skin/music/song/other)
//       data/...          <- misma ruta que dentro de assets/data del juego
//     MiCancion/
//       mod.toml
//       data/music/again_1.fsb.xen ...
//
// Al arrancar, los archivos de cada mod activo se INYECTAN en el arbol del VFS
// del juego base (reemplazan la entrada original), porque el VFS del SDK busca
// los archivos como hijos de la carpeta padre ya resuelta y nunca pasaria por
// un overlay. Mayor prioridad = mas arriba en la lista.
// Los cambios de activar/orden se aplican al reiniciar el juego.
// =============================================================================

#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <vector>

namespace rex::filesystem {
class Entry;
class HostPathDevice;
}  // namespace rex::filesystem

namespace re_wor::de {

struct ModInfo {
  std::string folder;          // nombre de la carpeta en mods/
  std::string name;            // mod.toml: name (o la carpeta)
  std::string author;
  std::string version;
  std::string description;
  std::string type = "other";  // skin | music | song | highway | character | ui | other
  bool enabled = true;
  bool has_manifest = false;
  size_t file_count = 0;
  uint64_t total_bytes = 0;
  size_t overrides = 0;        // archivos originales reemplazados (al arrancar)
  size_t added = 0;            // archivos que no existen en el juego base
  uint64_t hits = 0;           // archivos nuevos servidos en esta sesion
  std::filesystem::path path;
};

struct InjectedFile {
  std::string guest_path;
  std::string mod;
  std::string host_path;
  bool replaced = false;  // false = archivo nuevo
};

struct FileOpenRecord {
  std::string path;
  uint32_t flags = 0;
  bool ok = false;
};

class ModManager {
 public:
  using ResolveBaseFn = std::function<rex::filesystem::Entry*(std::string_view)>;

  static ModManager& Instance();

  // Busca mods en mods/ y aplica el orden/estado de la configuracion.
  void Initialize(const std::filesystem::path& mods_root);
  void set_mount_path(const std::string& mount_path);
  void Rescan();

  // Inyecta los archivos de los mods activos en el arbol del juego base.
  // Devuelve cuantos archivos originales se reemplazaron.
  size_t ApplyOverrides(const ResolveBaseFn& resolve_base);
  std::vector<InjectedFile> Injected();

  // Archivos de mods que no existen en el juego base (carpetas nuevas).
  rex::filesystem::Entry* ResolveNew(std::string_view path, std::string* out_mod);

  // Copia del estado actual (para la UI).
  std::vector<ModInfo> List();

  void SetEnabled(const std::string& folder, bool enabled);
  void Move(const std::string& folder, int delta);  // -1 sube prioridad, +1 baja
  // Guarda el orden/estado en re_wor_de.toml.
  void Persist();

  // Crea mods/<folder>/ con mod.toml y carpetas de ejemplo para el tipo.
  bool CreateTemplate(const std::string& folder, const std::string& type, std::string* error);

  // Registro de aperturas de archivos del motor (panel de desarrollo).
  void RecordOpen(std::string_view path, uint32_t flags, bool ok);
  std::vector<FileOpenRecord> RecentOpens();
  void ClearRecentOpens();
  uint64_t total_opens() const { return total_opens_; }
  uint64_t failed_opens() const { return failed_opens_; }

  const std::filesystem::path& root() const { return root_; }

 private:
  struct Slot {
    ModInfo info;
    // Los dispositivos nunca se destruyen (puede haber archivos abiertos).
    std::shared_ptr<rex::filesystem::HostPathDevice> device;
  };

  void RescanLocked();

  std::filesystem::path root_;
  std::string mount_path_ = "\\Device\\Harddisk0\\Partition1";
  std::shared_mutex mutex_;
  std::vector<Slot> slots_;
  std::vector<InjectedFile> injected_;
  std::vector<std::unique_ptr<rex::filesystem::Entry>> retired_entries_;

  std::mutex opens_mutex_;
  std::deque<FileOpenRecord> recent_opens_;
  uint64_t total_opens_ = 0;
  uint64_t failed_opens_ = 0;
};

}  // namespace re_wor::de
