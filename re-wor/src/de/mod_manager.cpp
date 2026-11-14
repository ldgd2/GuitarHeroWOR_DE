#include "de/mod_manager.hpp"

#include <rex/filesystem.h>
#include <rex/filesystem/devices/host_path_device.h>
#include <rex/filesystem/devices/host_path_entry.h>
#include <rex/filesystem/entry.h>
#include <rex/system/xio.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>
#include <map>
#include <set>

#include <toml++/toml.hpp>

#include "core/crash_handler.hpp"
#include "core/path_resolver.hpp"
#include "de/de_config.hpp"

namespace re_wor::de {

namespace {

constexpr size_t kMaxRecentOpens = 400;

// Carpetas de mods/ que NO son mods individuales (las usa el cargador antiguo).
const std::set<std::string> kReservedFolders = {"data", "baked", "raw", "song", "_templates", "_cache"};

std::string ToLower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return s;
}

// Acceso legal a Entry::children_ (miembro protegido) mediante puntero a miembro.
struct EntryChildrenAccess : rex::filesystem::Entry {
  static std::vector<std::unique_ptr<rex::filesystem::Entry>>& Of(rex::filesystem::Entry* e) {
    return e->*(&EntryChildrenAccess::children_);
  }
};

std::string GuestKey(const std::filesystem::path& rel) {
  std::string s = rel.generic_string();
  std::replace(s.begin(), s.end(), '/', '\\');
  return s;
}


uint32_t QbKey(const std::string& text) {
  uint32_t crc = 0xFFFFFFFFu;
  for (unsigned char c : text) {
    unsigned char ch = static_cast<unsigned char>(std::tolower(c == '/' ? '\\' : c));
    crc ^= ch;
    for (int k = 0; k < 8; ++k) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
  }
  return crc;  // QBKey de Neversoft = ~crc32 estandar = crc sin la inversion final
}

uint32_t Be32(const uint8_t* p) {
  return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
}
void PutBe32(uint8_t* p, uint32_t v) {
  p[0] = uint8_t(v >> 24); p[1] = uint8_t(v >> 16); p[2] = uint8_t(v >> 8); p[3] = uint8_t(v);
}

// Datos de un archivo CHNK para la tabla compress.toc: tamano descomprimido,
// numero de bloques y tamano del primer bloque. false si no es CHNK.
bool ScanChnk(const std::filesystem::path& host, uint32_t* dec_total, uint32_t* chunks,
              uint32_t* first_size, uint32_t* file_size) {
  std::ifstream f(host, std::ios::binary | std::ios::ate);
  if (!f) return false;
  const auto size = static_cast<uint64_t>(f.tellg());
  *file_size = static_cast<uint32_t>(size);
  uint64_t off = 0;
  *dec_total = 0;
  *chunks = 0;
  *first_size = 0;
  uint8_t h[32];
  while (off + 32 <= size) {
    f.seekg(static_cast<std::streamoff>(off));
    f.read(reinterpret_cast<char*>(h), 32);
    if (!f || std::memcmp(h, "CHNK", 4) != 0) break;
    const uint32_t comp = Be32(h + 8), next = Be32(h + 12), dec = Be32(h + 20);
    *dec_total += dec;
    if (*chunks == 0) *first_size = (next == 0xFFFFFFFFu || next == 0) ? ((0x80 + comp + 0x7FF) & ~0x7FFu) : next;
    ++*chunks;
    if (next == 0xFFFFFFFFu || next == 0) break;
    off += next;
  }
  return *chunks > 0;
}
}  // namespace

ModManager& ModManager::Instance() {
  static ModManager instance;
  return instance;
}

void ModManager::Initialize(const std::filesystem::path& mods_root) {
  root_ = mods_root;
  std::error_code ec;
  std::filesystem::create_directories(root_, ec);
  std::unique_lock lock(mutex_);
  RescanLocked();
}

void ModManager::Rescan() {
  std::unique_lock lock(mutex_);
  RescanLocked();
}

void ModManager::RescanLocked() {
  std::vector<ModInfo> found;
  std::error_code ec;
  for (auto it = std::filesystem::directory_iterator(root_, ec);
       !ec && it != std::filesystem::directory_iterator(); it.increment(ec)) {
    if (!it->is_directory(ec)) continue;
    std::string folder = it->path().filename().string();
    if (folder.empty() || kReservedFolders.count(ToLower(folder))) continue;

    ModInfo info;
    info.folder = folder;
    info.name = folder;
    info.path = it->path();

    auto manifest = it->path() / "mod.toml";
    if (std::filesystem::exists(manifest, ec)) {
      try {
        auto tbl = toml::parse_file(manifest.string());
        info.has_manifest = true;
        info.name = tbl["name"].value_or(folder);
        info.author = tbl["author"].value_or(std::string());
        info.version = tbl["version"].value_or(std::string());
        info.description = tbl["description"].value_or(std::string());
        info.type = ToLower(tbl["type"].value_or(std::string("other")));
      } catch (const toml::parse_error& e) {
        info.description = std::string("mod.toml invalido: ") + std::string(e.description());
      }
    }

    auto data_dir = it->path() / "data";
    if (!std::filesystem::is_directory(data_dir, ec)) {
      // Sin carpeta data/ no hay nada que cargar; se lista igualmente para avisar.
      if (!info.has_manifest) continue;
    } else {
      for (auto f = std::filesystem::recursive_directory_iterator(data_dir, ec);
           !ec && f != std::filesystem::recursive_directory_iterator(); f.increment(ec)) {
        if (f->is_regular_file(ec)) {
          info.file_count++;
          info.total_bytes += f->file_size(ec);
        }
      }
    }
    found.push_back(std::move(info));
  }

  // Orden y estado guardados en re_wor_de.toml; los mods nuevos van al final activados.
  std::vector<ModEntryConfig> saved;
  {
    std::lock_guard cfg_lock(Mutex());
    saved = Get().mods;
  }
  std::vector<Slot> ordered;
  for (const auto& s : saved) {
    auto it = std::find_if(found.begin(), found.end(),
                           [&](const ModInfo& m) { return m.folder == s.folder; });
    if (it == found.end()) continue;
    it->enabled = s.enabled;
    ordered.push_back(Slot{std::move(*it), nullptr});
    found.erase(it);
  }
  std::sort(found.begin(), found.end(),
            [](const ModInfo& a, const ModInfo& b) { return a.folder < b.folder; });
  for (auto& m : found) ordered.push_back(Slot{std::move(m), nullptr});

  // Conservar contadores de la sesion.
  for (auto& slot : ordered) {
    for (auto& old : slots_) {
      if (old.info.folder == slot.info.folder) {
        slot.info.hits = old.info.hits;
        slot.info.overrides = old.info.overrides;
        slot.info.added = old.info.added;
        slot.device = old.device;
      }
    }
  }
  slots_ = std::move(ordered);
  TRACE("[ModManager] " << slots_.size() << " mod(s) encontrados en " << root_.string());
}

std::vector<ModInfo> ModManager::List() {
  std::shared_lock lock(mutex_);
  std::vector<ModInfo> out;
  out.reserve(slots_.size());
  for (auto& s : slots_) out.push_back(s.info);
  return out;
}

void ModManager::SetEnabled(const std::string& folder, bool enabled) {
  std::unique_lock lock(mutex_);
  for (auto& s : slots_) {
    if (s.info.folder == folder) s.info.enabled = enabled;
  }
}

void ModManager::Move(const std::string& folder, int delta) {
  std::unique_lock lock(mutex_);
  for (size_t i = 0; i < slots_.size(); i++) {
    if (slots_[i].info.folder != folder) continue;
    long j = static_cast<long>(i) + delta;
    if (j < 0 || j >= static_cast<long>(slots_.size())) return;
    std::swap(slots_[i], slots_[static_cast<size_t>(j)]);
    return;
  }
}

void ModManager::Persist() {
  {
    std::shared_lock lock(mutex_);
    std::lock_guard cfg_lock(Mutex());
    auto& mods = Get().mods;
    mods.clear();
    for (auto& s : slots_) mods.push_back(ModEntryConfig{s.info.folder, s.info.enabled});
  }
  Save();
}

bool ModManager::CreateTemplate(const std::string& folder, const std::string& type,
                                std::string* error) {
  if (folder.empty() || folder.find_first_of("\\/:*?\"<>|") != std::string::npos ||
      kReservedFolders.count(ToLower(folder))) {
    if (error) *error = "Nombre de carpeta invalido";
    return false;
  }
  auto dir = root_ / folder;
  std::error_code ec;
  if (std::filesystem::exists(dir, ec)) {
    if (error) *error = "Ya existe una carpeta con ese nombre";
    return false;
  }

  std::vector<std::string> subdirs;
  std::string hint;
  if (type == "music" || type == "song") {
    subdirs = {"data/music", "data/songs"};
    hint =
        "# Reemplazo de cancion: copia aqui los archivos con el MISMO nombre que el original.\n"
        "#   data/music/<cancion>_1.fsb.xen ... _3.fsb.xen, <cancion>_preview.fsb.xen (audio)\n"
        "#   data/songs/<Cancion>_song.pak.xen (chart/notas)\n"
        "# Puedes generarlos con herramientas de la comunidad (Onyx, GHWT:DE toolkit).\n";
  } else if (type == "skin" || type == "highway" || type == "character" || type == "ui") {
    subdirs = {"data/pak", "data/zones", "data/ui_images"};
    hint =
        "# Skin: reemplaza los .pak.xen / .img.xen originales manteniendo nombre y ruta.\n"
        "#   data/pak/...        (paks globales: highways, gemas, HUD)\n"
        "#   data/zones/...      (escenarios)\n"
        "#   data/ui_images/...  (pantallas de carga, menus)\n";
  } else {
    subdirs = {"data"};
    hint = "# Coloca los archivos con la misma ruta relativa que dentro de assets/.\n";
  }
  for (auto& s : subdirs) std::filesystem::create_directories(dir / s, ec);

  std::ofstream manifest(dir / "mod.toml");
  if (!manifest.is_open()) {
    if (error) *error = "No se pudo crear mod.toml";
    return false;
  }
  manifest << "# Mod para Guitar Hero: Warriors of Rock - Definitive Edition (re-wor)\n"
           << hint << "#\n"
           << "# IMPORTANTE: si el archivo original esta en assets/data/compressed/ (o figura\n"
           << "# en compress.toc), el reemplazo debe estar comprimido igual (formato CHNK).\n\n"
           << "name = \"" << folder << "\"\n"
           << "author = \"\"\n"
           << "version = \"1.0\"\n"
           << "type = \"" << type << "\"\n"
           << "description = \"\"\n";
  manifest.close();

  std::unique_lock lock(mutex_);
  RescanLocked();
  return true;
}

size_t ModManager::ApplyOverrides(const ResolveBaseFn& resolve_base) {
  std::unique_lock lock(mutex_);
  injected_.clear();

  struct Source {
    size_t slot;  // indice en slots_ o SIZE_MAX para fuentes "legacy"
    std::string label;
    std::filesystem::path host;
  };
  // clave (minusculas) -> primera fuente (mayor prioridad gana)
  std::map<std::string, std::pair<std::string, Source>> files;

  auto add_tree = [&](size_t slot, const std::string& label, const std::filesystem::path& mod_root,
                      const std::filesystem::path& data_dir) {
    std::error_code ec;
    if (!std::filesystem::is_directory(data_dir, ec)) return;
    for (auto f = std::filesystem::recursive_directory_iterator(data_dir, ec);
         !ec && f != std::filesystem::recursive_directory_iterator(); f.increment(ec)) {
      if (!f->is_regular_file(ec)) continue;
      std::string key = GuestKey(std::filesystem::relative(f->path(), mod_root, ec));
      std::string lower = ToLower(key);
      if (!files.count(lower)) files.emplace(lower, std::make_pair(key, Source{slot, label, f->path()}));
    }
  };

  for (size_t i = 0; i < slots_.size(); i++) {
    auto& info = slots_[i].info;
    info.overrides = 0;
    info.added = 0;
    if (!info.enabled) continue;
    add_tree(i, info.folder, info.path, info.path / "data");
  }
  // Compatibilidad: archivos sueltos en mods/data/... (menor prioridad).
  add_tree(SIZE_MAX, "mods/data (suelto)", root_, root_ / "data");
  // Compatibilidad GHWT:DE: assets/data/pak/mods/<nombre>.pak.xen -> data\pak\<nombre>.
  {
    std::error_code ec;
    auto pak_mods = re_wor::core::get_assets_directory() / "data" / "pak" / "mods";
    for (auto it = std::filesystem::directory_iterator(pak_mods, ec);
         !ec && it != std::filesystem::directory_iterator(); it.increment(ec)) {
      if (!it->is_regular_file(ec)) continue;
      std::string fname = it->path().filename().string();
      std::string lf = ToLower(fname);
      if (lf.size() < 8 || (lf.rfind(".pak.xen") != lf.size() - 8 && lf.rfind(".pab.xen") != lf.size() - 8)) {
        continue;
      }
      std::string key = "data\\pak\\" + fname;
      if (!files.count(ToLower(key))) {
        files.emplace(ToLower(key), std::make_pair(key, Source{SIZE_MAX, "assets/data/pak/mods", it->path()}));
      }
    }
  }

  size_t applied = 0;
  for (auto& [lower, kv] : files) {
    const std::string& key = kv.first;
    const Source& src = kv.second;

    // Candidatos de ruta en el juego base: tal cual, sin "compressed\" y con "compressed\".
    std::vector<std::string> candidates{key};
    std::string lk = ToLower(key);
    size_t pos = lk.find("\\compressed\\");
    if (pos != std::string::npos) {
      candidates.push_back(key.substr(0, pos) + key.substr(pos + strlen("\\compressed")));
    } else if (lk.rfind("data\\", 0) == 0) {
      candidates.push_back("data\\compressed\\" + key.substr(5));
    }

    rex::filesystem::Entry* base_entry = nullptr;
    for (auto& c : candidates) {
      base_entry = resolve_base(c);
      if (base_entry && !(base_entry->attributes() & rex::system::X_FILE_ATTRIBUTE_DIRECTORY)) break;
      base_entry = nullptr;
    }

    InjectedFile record;
    record.guest_path = key;
    record.mod = src.label;
    record.host_path = src.host.string();

    if (!base_entry || !base_entry->parent()) {
      // Archivo nuevo (p. ej. una cancion custom): si su carpeta existe en el juego base se
      // inserta ahi como entrada nueva; si no, lo resuelve ModOverlayDevice.
      record.replaced = false;
      const size_t slash = key.find_last_of('\\');
      if (slash != std::string::npos) {
        rex::filesystem::Entry* dir = resolve_base(key.substr(0, slash));
        rex::filesystem::FileInfo info;
        if (dir && (dir->attributes() & rex::system::X_FILE_ATTRIBUTE_DIRECTORY) &&
            rex::filesystem::GetInfo(src.host, &info)) {
          info.name = rex::to_path(key.substr(slash + 1));
          if (auto* added = rex::filesystem::HostPathEntry::Create(dir->device(), dir, src.host, info)) {
            EntryChildrenAccess::Of(dir).emplace_back(added);
            TRACE("[ModManager]   nuevo: " << key);
          }
        }
      }
      if (src.slot != SIZE_MAX) slots_[src.slot].info.added++;
      injected_.push_back(std::move(record));
      continue;
    }

    rex::filesystem::FileInfo info;
    if (!rex::filesystem::GetInfo(src.host, &info)) continue;
    info.name = rex::to_path(base_entry->name());  // mismo nombre exacto que el original

    auto* parent = base_entry->parent();
    auto& children = EntryChildrenAccess::Of(parent);
    auto it = std::find_if(children.begin(), children.end(),
                           [&](const auto& c) { return c.get() == base_entry; });
    if (it == children.end()) continue;

    auto* replacement =
        rex::filesystem::HostPathEntry::Create(parent->device(), parent, src.host, info);
    if (!replacement) continue;

    // El original se guarda (nunca se destruye) por si hay archivos abiertos.
    retired_entries_.push_back(std::move(*it));
    it->reset(replacement);

    record.replaced = true;
    if (src.slot != SIZE_MAX) slots_[src.slot].info.overrides++;
    injected_.push_back(std::move(record));
    applied++;
  }

  // compress.toc: el juego lee de cada archivo comprimido exactamente el tamano que dice
  // esta tabla. Si un mod reemplaza un archivo CHNK con otro tamano, se genera una copia
  // corregida de la tabla (en mods/_cache) y se inyecta como reemplazo.
  {
    std::error_code ec;
    const auto toc_path = re_wor::core::get_assets_directory() / "data" / "compressed" / "compress.toc.xen";
    std::ifstream tf(toc_path, std::ios::binary);
    std::vector<uint8_t> toc((std::istreambuf_iterator<char>(tf)), std::istreambuf_iterator<char>());
    if (toc.size() >= 0x20 && std::memcmp(toc.data(), "TOC1", 4) == 0) {
      const uint32_t count = Be32(&toc[4]);
      const uint32_t hdr = Be32(&toc[8]);
      int patched = 0;
      for (auto& r : injected_) {
        if (!r.replaced) continue;
        std::string rel = ToLower(r.guest_path);
        if (rel.rfind("data\\", 0) == 0) rel = rel.substr(5);
        if (rel.rfind("compressed\\", 0) == 0) rel = rel.substr(11);
        const uint32_t key = QbKey(rel);
        for (uint32_t i = 0; i < count && hdr + (i + 1) * 0x14 <= toc.size(); ++i) {
          uint8_t* e = &toc[hdr + i * 0x14];
          if (Be32(e) != key) continue;
          uint32_t dec = 0, chunks = 0, first = 0, fsize = 0;
          if (ScanChnk(r.host_path, &dec, &chunks, &first, &fsize)) {
            PutBe32(e + 4, dec);
            PutBe32(e + 8, fsize);
            PutBe32(e + 12, (Be32(e + 12) & 0xFFFF0000u) | (chunks & 0xFFFFu));
            PutBe32(e + 16, first);
            ++patched;
            TRACE("[ModManager]   compress.toc: " << rel << " dec=" << dec << " comp=" << fsize
                  << " bloques=" << chunks);
          }
          break;
        }
      }
      if (patched) {
        auto cache = root_ / "_cache";
        std::filesystem::create_directories(cache, ec);
        auto out = cache / "compress.toc.xen";
        {
          std::ofstream of(out, std::ios::binary | std::ios::trunc);
          of.write(reinterpret_cast<const char*>(toc.data()), static_cast<std::streamsize>(toc.size()));
        }
        rex::filesystem::Entry* base_entry = resolve_base("data\\compressed\\compress.toc.xen");
        rex::filesystem::FileInfo info;
        if (base_entry && base_entry->parent() && rex::filesystem::GetInfo(out, &info)) {
          info.name = rex::to_path(base_entry->name());
          auto* parent = base_entry->parent();
          auto& children = EntryChildrenAccess::Of(parent);
          auto it = std::find_if(children.begin(), children.end(),
                                 [&](const auto& c) { return c.get() == base_entry; });
          if (it != children.end()) {
            if (auto* rep = rex::filesystem::HostPathEntry::Create(parent->device(), parent, out, info)) {
              retired_entries_.push_back(std::move(*it));
              it->reset(rep);
              TRACE("[ModManager] compress.toc corregido para " << patched << " archivo(s)");
            }
          }
        }
      }
    }
  }

  TRACE("[ModManager] " << applied << " archivo(s) reemplazados por mods, "
        << (injected_.size() - applied) << " archivo(s) nuevos");
  for (auto& r : injected_) {
    TRACE("[ModManager]   " << (r.replaced ? "REEMPLAZA " : "NUEVO     ") << r.guest_path
          << "  <-  [" << r.mod << "] " << r.host_path);
  }
  return applied;
}

std::vector<InjectedFile> ModManager::Injected() {
  std::shared_lock lock(mutex_);
  return injected_;
}

rex::filesystem::Entry* ModManager::ResolveNew(std::string_view path, std::string* out_mod) {
  std::unique_lock lock(mutex_);
  for (auto& s : slots_) {
    if (!s.info.enabled) continue;
    if (!s.device) {
      s.device = std::make_shared<rex::filesystem::HostPathDevice>(
          mount_path_, s.info.path, /*read_only=*/true, /*allow_share_delete=*/true);
      if (!s.device->Initialize()) {
        s.device.reset();
        continue;
      }
    }
    if (auto* e = s.device->ResolvePath(path)) {
      if (!(e->attributes() & rex::system::X_FILE_ATTRIBUTE_DIRECTORY)) {
        s.info.hits++;
        if (out_mod) *out_mod = s.info.folder;
        return e;
      }
    }
  }
  return nullptr;
}

void ModManager::set_mount_path(const std::string& mount_path) {
  mount_path_ = mount_path;
}

void ModManager::RecordOpen(std::string_view path, uint32_t flags, bool ok) {
  std::lock_guard lock(opens_mutex_);
  total_opens_++;
  if (!ok) failed_opens_++;
  recent_opens_.push_back(FileOpenRecord{std::string(path), flags, ok});
  if (recent_opens_.size() > kMaxRecentOpens) recent_opens_.pop_front();
}

std::vector<FileOpenRecord> ModManager::RecentOpens() {
  std::lock_guard lock(opens_mutex_);
  return {recent_opens_.begin(), recent_opens_.end()};
}

void ModManager::ClearRecentOpens() {
  std::lock_guard lock(opens_mutex_);
  recent_opens_.clear();
}

}  // namespace re_wor::de
