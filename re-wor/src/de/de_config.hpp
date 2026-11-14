#pragma once
// =============================================================================
// de_config - Configuracion persistente de la "Definitive Edition".
// Archivo: <base>/re_wor_de.toml (se crea con valores por defecto).
// =============================================================================

#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

namespace re_wor::de {

// Acciones de una guitarra de Guitar Hero (mapeo Xbox 360).
enum class GuitarAction : int {
  kGreen = 0,   // A
  kRed,         // B
  kYellow,      // Y
  kBlue,        // X
  kOrange,      // LB
  kStrumUp,     // D-pad arriba
  kStrumDown,   // D-pad abajo
  kStart,       // Start
  kStarPower,   // Back (Select) = activar Star Power
  kWhammy,      // Stick derecho X (palanca)
  kTilt,        // Stick derecho Y (inclinacion = Star Power)
  kNavLeft,     // D-pad izquierda (menus)
  kNavRight,    // D-pad derecha (menus)
  kCount
};

const char* GuitarActionId(GuitarAction a);     // clave en el TOML
const char* GuitarActionLabel(GuitarAction a);  // texto para la UI

struct InputConfig {
  // "sdl"    -> SDL3: XInput + DirectInput + HID (Santroller, guitarras PS3/Wii, etc.)
  // "xinput" -> solo XInput nativo de Windows
  std::string backend = "sdl";
  bool keyboard_guitar = true;   // teclado como guitarra
  bool keyboard_gamepad = false; // teclado como mando (driver MnK del SDK)
  // Subtipo XInput reportado por la guitarra de teclado:
  // 6 = GUITAR, 7 = GUITAR_ALTERNATE (GH), 11 = GUITAR_BASS
  int keyboard_guitar_subtype = 6;
  // Teclas por accion: lista separada por comas (ej. "A,1").
  std::string keys[static_cast<int>(GuitarAction::kCount)];
};

struct ModEntryConfig {
  std::string folder;  // nombre de la carpeta dentro de mods/
  bool enabled = true;
};

struct DevConfig {
  bool log_file_opens = false;  // registrar cada archivo abierto (re_wor_trace.log)
  bool show_fps = false;
};

// Rendimiento (seccion [performance]).
struct PerfConfig {
  bool trace_probes = false;   // escribir FPS/audio/reloj en el trace cada 2 s (diagnostico)
  bool above_normal_priority = true;  // prioridad "por encima de lo normal" para el proceso
};

// Atajos de teclado del juego (seccion [hotkeys]). Nombres de tecla del SDK: "F8", "Backtick",
// "Insert", "P"... Una tecla vacia, desconocida o reservada (F1-F5: trastes) usa la de por defecto.
struct HotkeyConfig {
  std::string panel = "F8";          // panel de la Definitive Edition
  std::string fullscreen = "F11";    // pantalla completa
  std::string perf_overlay = "F9";   // overlay de rendimiento (SDK; de fabrica F3)
  std::string settings = "F10";      // ajustes avanzados / cvars (SDK; de fabrica F4)
  std::string console = "Backtick";  // consola (SDK)
  std::string achievements = "F7";   // logros (SDK)
  std::string skip_intro = "F";      // saltar los videos del arranque
};

struct Config {
  InputConfig input;
  PerfConfig perf;
  HotkeyConfig hotkeys;
  std::vector<ModEntryConfig> mods;  // orden = prioridad (primero gana)
  DevConfig dev;
};

// Acceso global (thread-safe para lectura/escritura con el mutex).
Config& Get();
void ResetKeysToDefaults(InputConfig& input);
std::mutex& Mutex();

// Tecla configurada si el SDK la reconoce; si no (vacia o mal escrita), la de por defecto.
std::string HotkeyOr(const std::string& key, const char* fallback);

std::filesystem::path ConfigPath();
void Load();
bool Save();

}  // namespace re_wor::de
