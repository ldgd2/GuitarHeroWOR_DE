#include "core/path_resolver.hpp"

#include <vector>
#include <iostream>
#include <fstream>
#include <sstream>

#ifdef _WIN32
#include <windows.h>
#include <shobjidl.h>
#include <shlobj.h>
#endif

namespace re_wor::core {

namespace {

static std::filesystem::path s_custom_game_path;

static bool CheckValidDirectory(const std::filesystem::path& dir, std::filesystem::path& out_base) {
  if (dir.empty()) return false;
  std::error_code ec;
  if (!std::filesystem::is_directory(dir, ec)) return false;

  // Si contiene subcarpeta "assets"
  if (std::filesystem::is_directory(dir / "assets", ec)) {
    out_base = std::filesystem::weakly_canonical(dir, ec);
    return true;
  }
  // Si contiene subcarpeta "data" directamente (o es la carpeta assets en si misma)
  if (std::filesystem::is_directory(dir / "data", ec)) {
    if (dir.filename() == "assets") {
      out_base = std::filesystem::weakly_canonical(dir.parent_path(), ec);
    } else {
      out_base = std::filesystem::weakly_canonical(dir, ec);
    }
    return true;
  }
  // Si contiene "assets/data"
  if (std::filesystem::is_directory(dir / "assets" / "data", ec)) {
    out_base = std::filesystem::weakly_canonical(dir, ec);
    return true;
  }
  return false;
}

static std::filesystem::path ReadConfiguredPath(const std::filesystem::path& cfg_path) {
  std::error_code ec;
  if (!std::filesystem::exists(cfg_path, ec)) return {};
  try {
    std::ifstream file(cfg_path);
    if (!file.is_open()) return {};
    std::string line;
    bool in_game = false;
    while (std::getline(file, line)) {
      size_t first = line.find_first_not_of(" \t\r\n");
      if (first == std::string::npos || line[first] == '#') continue;
      if (line[first] == '[') {
        in_game = (line.find("[game]") != std::string::npos);
        continue;
      }
      if (in_game) {
        size_t eq = line.find('=');
        if (eq != std::string::npos) {
          std::string key = line.substr(first, eq - first);
          while (!key.empty() && (key.back() == ' ' || key.back() == '\t')) key.pop_back();
          if (key == "game_path") {
            std::string val = line.substr(eq + 1);
            size_t q1 = val.find_first_of("'\"");
            size_t q2 = val.find_last_of("'\"");
            if (q1 != std::string::npos && q2 != std::string::npos && q2 > q1) {
              return std::filesystem::path(val.substr(q1 + 1, q2 - q1 - 1));
            }
          }
        }
      }
    }
  } catch (...) {}
  return {};
}

static void SaveConfiguredPath(const std::filesystem::path& cfg_path, const std::filesystem::path& target_path) {
  try {
    std::error_code ec;
    std::string content;
    bool updated = false;
    std::string path_str = target_path.string();
    std::string new_entry = "game_path = '" + path_str + "'";

    if (std::filesystem::exists(cfg_path, ec)) {
      std::ifstream in(cfg_path);
      std::stringstream ss;
      ss << in.rdbuf();
      content = ss.str();
      size_t pos = content.find("game_path");
      if (pos != std::string::npos) {
        size_t end_line = content.find('\n', pos);
        content.replace(pos, (end_line == std::string::npos ? content.size() - pos : end_line - pos), new_entry);
        updated = true;
      } else {
        size_t gpos = content.find("[game]");
        if (gpos != std::string::npos) {
          size_t end_g = content.find('\n', gpos);
          if (end_g != std::string::npos) {
            content.insert(end_g + 1, new_entry + "\n");
            updated = true;
          }
        }
      }
    }

    if (!updated) {
      if (content.empty()) {
        content = "[game]\n" + new_entry + "\n";
      } else {
        content += "\n[game]\n" + new_entry + "\n";
      }
    }

    std::ofstream out(cfg_path, std::ios::out | std::ios::trunc);
    if (out.is_open()) {
      out << content;
    }
  } catch (...) {}
}

#ifdef _WIN32
static std::filesystem::path ShowDirectorySelectionDialog() {
  MessageBoxW(
      nullptr,
      L"No se encontró la carpeta 'assets' en la ubicación del juego.\n\n"
      L"Por favor, seleccione la carpeta donde están ubicados los datos del juego.",
      L"Guitar Hero: Warriors of Rock",
      MB_OK | MB_ICONINFORMATION);

  std::filesystem::path selected;
  HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
  bool co_inited = SUCCEEDED(hr);

  IFileDialog* pfd = nullptr;
  hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pfd));
  if (SUCCEEDED(hr)) {
    DWORD dwOptions = 0;
    if (SUCCEEDED(pfd->GetOptions(&dwOptions))) {
      pfd->SetOptions(dwOptions | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
    }
    pfd->SetTitle(L"Seleccione la carpeta del juego");

    if (SUCCEEDED(pfd->Show(nullptr))) {
      IShellItem* psi = nullptr;
      if (SUCCEEDED(pfd->GetResult(&psi))) {
        PWSTR pszPath = nullptr;
        if (SUCCEEDED(psi->GetDisplayName(SIGDN_FILESYSPATH, &pszPath))) {
          selected = pszPath;
          CoTaskMemFree(pszPath);
        }
        psi->Release();
      }
    }
    pfd->Release();
  } else {
    // Fallback clasico con SHBrowseForFolderW
    BROWSEINFOW bi = {};
    bi.lpszTitle = L"Seleccione la carpeta del juego";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&bi);
    if (pidl) {
      wchar_t buffer[MAX_PATH];
      if (SHGetPathFromIDListW(pidl, buffer)) {
        selected = buffer;
      }
      CoTaskMemFree(pidl);
    }
  }

  if (co_inited) {
    CoUninitialize();
  }

  if (selected.empty()) {
    MessageBoxW(
        nullptr,
        L"No se seleccionó ninguna carpeta.\nEl juego no puede continuar sin los archivos requeridos.",
        L"Guitar Hero: Warriors of Rock",
        MB_OK | MB_ICONWARNING);
  }

  return selected;
}
#endif

}  // namespace

void set_custom_base_directory(const std::filesystem::path& path) {
  s_custom_game_path = path;
}

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

  // 1. Si se configuró programáticamente una ruta personalizada
  if (!s_custom_game_path.empty()) {
    std::filesystem::path resolved;
    if (CheckValidDirectory(s_custom_game_path, resolved)) {
      s_base_dir = resolved;
      return s_base_dir;
    }
  }

  auto exe_dir = get_executable_directory();
  std::error_code ec;
  auto cwd = std::filesystem::current_path(ec);

  // 2. Comprobar si existe una ruta guardada en re_wor_de.toml
  const std::filesystem::path config_candidates[] = {
    exe_dir / "re_wor_de.toml",
    cwd / "re_wor_de.toml",
    cwd / "re-wor" / "re_wor_de.toml"
  };
  for (const auto& cfg : config_candidates) {
    auto saved = ReadConfiguredPath(cfg);
    if (!saved.empty()) {
      std::filesystem::path resolved;
      if (CheckValidDirectory(saved, resolved)) {
        s_base_dir = resolved;
        return s_base_dir;
      }
    }
  }

  // 3. Comprobar ubicaciones candidatas estándar
  std::vector<std::filesystem::path> candidates = {
    exe_dir,
    exe_dir / ".." / ".." / "..",        // out/build/win-amd64-relwithdebinfo -> re-wor
    exe_dir / ".." / ".." / ".." / "..",  // out/build/... -> root
    exe_dir / "..",
    exe_dir / ".." / "..",
    cwd,
    cwd / "re-wor",
    cwd / ".."
  };

  for (const auto& cand : candidates) {
    std::filesystem::path resolved;
    if (CheckValidDirectory(cand, resolved)) {
      s_base_dir = resolved;
      return s_base_dir;
    }
  }

  // 4. Si no se detectó assets/ en ninguna de las ubicaciones:
  // Mostrar la ventana para que el usuario seleccione la carpeta donde están los datos
#ifdef _WIN32
  auto user_choice = ShowDirectorySelectionDialog();
  if (!user_choice.empty()) {
    std::filesystem::path resolved;
    if (CheckValidDirectory(user_choice, resolved)) {
      s_base_dir = resolved;
      SaveConfiguredPath(exe_dir / "re_wor_de.toml", s_base_dir);
      return s_base_dir;
    } else {
      MessageBoxW(
          nullptr,
          L"La carpeta seleccionada no parece contener los datos del juego ('assets' o 'data').\n"
          L"Se intentará iniciar con la ruta especificada.",
          L"Guitar Hero: Warriors of Rock",
          MB_OK | MB_ICONWARNING);
      s_base_dir = user_choice;
      SaveConfiguredPath(exe_dir / "re_wor_de.toml", s_base_dir);
      return s_base_dir;
    }
  } else {
    // Si se canceló la selección, salir de forma controlada
    ExitProcess(0);
  }
#endif

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
  std::error_code ec;
  auto exe_mods = get_executable_directory() / "mods";
  if (std::filesystem::is_directory(exe_mods, ec)) {
    return exe_mods;
  }
  return get_base_directory() / "mods";
}

std::filesystem::path get_user_directory() {
  return get_base_directory() / "user";
}

std::filesystem::path get_cache_directory() {
  return get_base_directory() / "cache";
}

}  // namespace re_wor::core
