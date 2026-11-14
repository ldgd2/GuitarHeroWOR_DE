#include "de/de_config.hpp"

#include "core/crash_handler.hpp"
#include "core/path_resolver.hpp"

#include <fstream>
#include <sstream>

#include <rex/ui/keybinds.h>
#include <toml++/toml.hpp>

namespace re_wor::de {

namespace {

struct ActionInfo {
  const char* id;
  const char* label;
  const char* default_keys;
};

// Por defecto: estilo Clone Hero / GHWT:DE.
constexpr ActionInfo kActions[] = {
    // F1-F5 tambien son trastes (clasico de Guitar Hero en PC): por eso ningun atajo las usa.
    {"green", "Traste verde", "A,1,F1"},
    {"red", "Traste rojo", "S,2,F2,Escape"},
    {"yellow", "Traste amarillo", "J,3,F3"},
    {"blue", "Traste azul", "K,4,F4"},
    {"orange", "Traste naranja", "L,5,F5"},
    {"strum_up", "Rasgueo arriba", "Up"},
    {"strum_down", "Rasgueo abajo", "Down"},
    {"start", "Start / Pausa", "Return"},
    {"star_power", "Star Power (Select)", "Space,RShift"},
    {"whammy", "Palanca (whammy)", "RControl,Semicolon"},
    {"tilt", "Inclinacion (tilt)", "Backspace"},
    {"nav_left", "Menu izquierda", "Left"},
    {"nav_right", "Menu derecha", "Right"},
};
static_assert(sizeof(kActions) / sizeof(kActions[0]) == static_cast<int>(GuitarAction::kCount));

Config g_config;
std::mutex g_mutex;
bool g_defaults_applied = false;

}  // namespace

void ResetKeysToDefaults(InputConfig& input) {
  for (int i = 0; i < static_cast<int>(GuitarAction::kCount); i++) {
    input.keys[i] = kActions[i].default_keys;
  }
}

const char* GuitarActionId(GuitarAction a) {
  return kActions[static_cast<int>(a)].id;
}

const char* GuitarActionLabel(GuitarAction a) {
  return kActions[static_cast<int>(a)].label;
}

Config& Get() {
  if (!g_defaults_applied) {
    g_defaults_applied = true;
    ResetKeysToDefaults(g_config.input);
  }
  return g_config;
}

std::mutex& Mutex() {
  return g_mutex;
}

std::filesystem::path ConfigPath() {
  return re_wor::core::get_base_directory() / "re_wor_de.toml";
}

void Load() {
  std::lock_guard lock(g_mutex);
  Config& c = Get();
  auto path = ConfigPath();
  std::error_code ec;
  if (!std::filesystem::exists(path, ec)) {
    TRACE("[DE::Config] No existe " << path.string() << ", se creara con valores por defecto");
    return;
  }
  try {
    toml::table tbl = toml::parse_file(path.string());

    if (auto in = tbl["input"].as_table()) {
      c.input.backend = (*in)["backend"].value_or(c.input.backend);
      c.input.keyboard_guitar = (*in)["keyboard_guitar"].value_or(c.input.keyboard_guitar);
      c.input.keyboard_gamepad = (*in)["keyboard_gamepad"].value_or(c.input.keyboard_gamepad);
      c.input.keyboard_guitar_subtype =
          (*in)["keyboard_guitar_subtype"].value_or(c.input.keyboard_guitar_subtype);
      if (auto keys = (*in)["keys"].as_table()) {
        for (int i = 0; i < static_cast<int>(GuitarAction::kCount); i++) {
          if (auto v = (*keys)[kActions[i].id].value<std::string>()) {
            c.input.keys[i] = *v;
          }
        }
      }
    }

    if (auto mods = tbl["mods"].as_array()) {
      c.mods.clear();
      for (auto& node : *mods) {
        if (auto t = node.as_table()) {
          ModEntryConfig m;
          m.folder = (*t)["folder"].value_or(std::string());
          m.enabled = (*t)["enabled"].value_or(true);
          if (!m.folder.empty()) c.mods.push_back(m);
        }
      }
    }

    if (auto dev = tbl["dev"].as_table()) {
      c.dev.log_file_opens = (*dev)["log_file_opens"].value_or(c.dev.log_file_opens);
      c.dev.show_fps = (*dev)["show_fps"].value_or(c.dev.show_fps);
    }
    if (auto hk = tbl["hotkeys"].as_table()) {
      auto rd = [&](const char* k, std::string& v) { v = (*hk)[k].value_or(v); };
      rd("panel", c.hotkeys.panel);
      rd("fullscreen", c.hotkeys.fullscreen);
      rd("perf_overlay", c.hotkeys.perf_overlay);
      rd("settings", c.hotkeys.settings);
      rd("console", c.hotkeys.console);
      rd("achievements", c.hotkeys.achievements);
      rd("skip_intro", c.hotkeys.skip_intro);
    }
    if (auto perf = tbl["performance"].as_table()) {
      c.perf.trace_probes = (*perf)["trace_probes"].value_or(c.perf.trace_probes);
      c.perf.above_normal_priority =
          (*perf)["above_normal_priority"].value_or(c.perf.above_normal_priority);
    }
    TRACE("[DE::Config] Cargado " << path.string());
  } catch (const toml::parse_error& e) {
    TRACE("[DE::Config] Error al leer " << path.string() << ": " << e.description()
          << " (se usan valores por defecto)");
  }
}

std::string HotkeyOr(const std::string& key, const char* fallback) {
  const auto vk = key.empty() ? rex::ui::VirtualKey::kNone : rex::ui::ParseVirtualKey(key);
  // F1-F5 son de los trastes: un atajo ahi se comeria la tecla en mitad de la cancion.
  const bool reserved = vk == rex::ui::VirtualKey::kF1 || vk == rex::ui::VirtualKey::kF2 ||
                        vk == rex::ui::VirtualKey::kF3 || vk == rex::ui::VirtualKey::kF4 ||
                        vk == rex::ui::VirtualKey::kF5;
  if (vk != rex::ui::VirtualKey::kNone && !reserved) return key;
  return fallback;
}

bool Save() {
  std::lock_guard lock(g_mutex);
  Config& c = Get();

  toml::table keys;
  for (int i = 0; i < static_cast<int>(GuitarAction::kCount); i++) {
    keys.insert(kActions[i].id, c.input.keys[i]);
  }
  toml::table input{
      {"backend", c.input.backend},
      {"keyboard_guitar", c.input.keyboard_guitar},
      {"keyboard_gamepad", c.input.keyboard_gamepad},
      {"keyboard_guitar_subtype", c.input.keyboard_guitar_subtype},
  };
  input.insert("keys", std::move(keys));

  toml::array mods;
  for (auto& m : c.mods) {
    mods.push_back(toml::table{{"folder", m.folder}, {"enabled", m.enabled}});
  }

  toml::table dev{{"log_file_opens", c.dev.log_file_opens}, {"show_fps", c.dev.show_fps}};

  toml::table perf{{"trace_probes", c.perf.trace_probes},
                    {"above_normal_priority", c.perf.above_normal_priority}};

  toml::table hotkeys{{"panel", c.hotkeys.panel},
                      {"fullscreen", c.hotkeys.fullscreen},
                      {"perf_overlay", c.hotkeys.perf_overlay},
                      {"settings", c.hotkeys.settings},
                      {"console", c.hotkeys.console},
                      {"achievements", c.hotkeys.achievements},
                      {"skip_intro", c.hotkeys.skip_intro}};

  toml::table root;
  root.insert("performance", std::move(perf));
  root.insert("hotkeys", std::move(hotkeys));
  root.insert("input", std::move(input));
  root.insert("mods", std::move(mods));
  root.insert("dev", std::move(dev));

  std::ofstream out(ConfigPath(), std::ios::out | std::ios::trunc);
  if (!out.is_open()) {
    TRACE("[DE::Config] No se pudo escribir " << ConfigPath().string());
    return false;
  }
  out << "# Guitar Hero: Warriors of Rock - Definitive Edition (re-wor)\n"
      << "# Configuracion editable a mano, desde el lanzador o desde el panel del juego.\n"
      << "# hotkeys: teclas del juego (\"F8\", \"F11\", \"Backtick\", \"Insert\", \"P\"...).\n"
      << "# input.backend: \"sdl\" (XInput + DirectInput + HID) o \"xinput\". Requiere reiniciar.\n"
      << "# mods: el orden es la prioridad (el primero gana si dos mods tienen el mismo archivo).\n\n";
  out << root << "\n";
  return true;
}

}  // namespace re_wor::de
