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

// Acciones del teclado en modo mando (un jugador que solo navega menus o canta: A, B, cruceta...).
enum class PadAction : int {
  kA = 0, kB, kX, kY, kStart, kBack, kUp, kDown, kLeft, kRight, kLB, kRB,
  kCount
};
const char* PadActionId(PadAction a);
const char* PadActionLabel(PadAction a);

// Bateria de teclado (el juego la ve como una bateria de Guitar Hero: pads, pedal y cruceta para menus).
enum class DrumAction : int {
  kRed = 0, kYellow, kBlue, kOrange, kGreen, kKick, kStart, kStarPower, kUp, kDown, kLeft, kRight,
  kCount
};
const char* DrumActionId(DrumAction a);
const char* DrumActionLabel(DrumAction a);

// Instrumento de un jugador: que ve el juego en su mando (guitarra, bajo, bateria o mando + microfono).
enum class Instrument : int { kGuitar = 0, kBass, kDrums, kVocals, kCount };
const char* InstrumentId(Instrument i);              // "guitar", "bass", "drums", "vocals"
Instrument InstrumentFromId(const std::string& id);

// Un jugador (1..4 = mando 0..3 del juego). [[input.players]] en re_wor_de.toml.
struct PlayerSlotConfig {
  // Teclado fisico: "" ninguno, "*" cualquiera que no use otro jugador, o la ruta del dispositivo
  // de Windows, que no cambia entre arranques. keyboard_name solo es para mostrarlo.
  std::string keyboard;
  std::string keyboard_name;
  std::string keyboard_mode = "guitar";   // obsoleto (se deduce del instrumento); se lee de configs antiguas
  std::string instrument = "guitar";      // "guitar", "bass", "drums" o "vocals"
  // Teclas propias del jugador (cada teclado las suyas: dos teclados iguales no se pisan). Lista por accion
  // separada por comas, como [input.keys]. Guitarra y bajo usan guitar_keys; bateria drum_keys; voz pad_keys.
  std::string guitar_keys[static_cast<int>(GuitarAction::kCount)];
  std::string drum_keys[static_cast<int>(DrumAction::kCount)];
  std::string pad_keys[static_cast<int>(PadAction::kCount)];
  // Mando concreto: "" automatico (por orden de conexion), o el GUID/nombre del mando
  std::string pad;
  std::string pad_name;
  // Microfono: "" ninguno, "default" el predeterminado de Windows, o el id del dispositivo (WASAPI)
  std::string mic;
  std::string mic_name;
  float mic_gain = 1.0f;
};
constexpr int kMaxPlayers = 4;

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
  // Teclas del modo mando (teclado de un jugador en modo "pad")
  std::string pad_keys[static_cast<int>(PadAction::kCount)];
  PlayerSlotConfig players[kMaxPlayers];
};

struct ModEntryConfig {
  std::string folder;  // nombre de la carpeta dentro de mods/
  bool enabled = true;
};

struct DevConfig {
  bool log_file_opens = false;  // registrar cada archivo abierto (re_wor_trace.log)
  bool show_fps = false;
  bool debug_build = false;     // Forzar DebugBuild() = 1 en scripts Neversoft
  bool auto_skip_intro = true;  // Saltar legal/intro automaticamente sin pulsar F
  bool uncapped_fps = false;    // Desbloquear limite de 60 FPS
  int target_refresh_rate = 0;  // 0 = auto/vsync, 60, 120, 144, 240
  int bridge_port = 8770;       // puente de desarrollo por TCP en 127.0.0.1 (0 = apagado); clave en dev_bridge.json
};

// Rendimiento (seccion [performance]).
struct PerfConfig {
  bool trace_probes = false;   // escribir FPS/audio/reloj en el trace cada 2 s (diagnostico)
  bool above_normal_priority = true;  // prioridad "por encima de lo normal" para el proceso
};

// Configuracion grafica y traduccion (seccion [graphics]).
struct GraphicsConfig {
  std::string backend = "d3d12";            // API grafica: "d3d12" o "vulkan" (si Vulkan falla, vuelve a D3D12)
  std::string render_target_path = "rtv";   // "rtv" (Hardware ROP - rapido para Nvidia/AMD/Intel y patatas), "rov"
  int resolution_scale = 1;                 // 1 = 720p (ligero/patatas), 2 = 1440p/4K (high-end)
  int texture_cache_mb = 1024;              // limite de textura (MB) para evitar tirones
  bool async_pipeline_compile = true;       // compilar shaders en hilos paralelos
  // Pantalla (cvars del SDK; se aplican al arrancar)
  std::string resolution;                   // "" = la del escritorio/ventana, "720p", "1080p", "1440p", "4k"
  int window_mode = 1;                      // 0 ventana, 1 pantalla completa sin bordes, 2 pantalla completa exclusiva
  int refresh_hz = 60;                      // VBlank del juego (video_mode_refresh_rate): 60 = como en la consola
  std::string upscale = "bilinear";         // salida: "bilinear", "cas" (nitidez AMD) o "fsr" (AMD FSR 1)
  float sharpness = 0.0f;                   // 0..1 nitidez extra de CAS/FSR
  bool msaa2x = true;                       // MSAA 2x nativo del Xenos
  bool fxaa = false;                        // FXAA en la imagen final (al momento)
  int aniso = 3;                            // filtrado anisotropico: 0 1x, 1 2x, 2 4x, 3 8x, 4 16x
  bool post_fx = true;                      // post-proceso de imagen (bloom, glow...): apagado ahorra una pasada
  int preset = 2;                           // calidad: 0 Low, 1 Medium, 2 High, 3 Ultra, 4 Custom
  bool letterbox = true;                    // franjas si la ventana no es 16:9
  bool vsync = false;                       // VSync del monitor (present_vsync; al momento)
  bool show_fps = false;                    // contador de FPS en una esquina (al momento)
  bool perf_detail = false;                 // el contador muestra tiempos de frame y esperas a la GPU
  // Imagen: post-proceso propio en la pasada sandbox, solo el escenario (la autopista y el HUD no se tocan)
  float bloom = 0.0f;                       // 0..1 resplandor de las luces fuertes
  float glow = 0.0f;                        // 0..1 resplandor suave (umbral bajo)
  float brightness = 0.0f;                  // -1..1
  float contrast = 0.0f;                    // -1..1
  float saturation = 0.0f;                  // -1..1
  float vignette = 0.0f;                    // 0..1
  float grain = 0.0f;                       // 0..1 grano de pelicula
  float blur = 0.0f;                        // 0..1 desenfoque suave del escenario
  // Publico (qb/crowd.q): 0 ninguno, 1 bajo (filas 3D de delante), 2 medio (publico 2D del fondo), 3 alto (original)
  int crowd = 3;
  // Presets de calidad. No tocan la resolucion ni la pantalla: solo AA, filtrado, post-proceso y cache.
  void ApplyPreset(int p) {
    static const struct { bool msaa, fxaa; int aniso; bool post; int cache; } k[4] = {
        {false, false, 0, false, 512},   // Low
        {false, true, 2, true, 1024},    // Medium
        {true, false, 3, true, 1024},    // High
        {true, true, 4, true, 2048},     // Ultra
    };
    if (p < 0 || p > 3) { preset = 4; return; }
    msaa2x = k[p].msaa; fxaa = k[p].fxaa; aniso = k[p].aniso; post_fx = k[p].post; texture_cache_mb = k[p].cache;
    preset = p;
  }
  int MatchPreset() const {
    for (int p = 0; p < 4; ++p) {
      GraphicsConfig t = *this;
      t.ApplyPreset(p);
      if (t.msaa2x == msaa2x && t.fxaa == fxaa && t.aniso == aniso && t.post_fx == post_fx &&
          t.texture_cache_mb == texture_cache_mb)
        return p;
    }
    return 4;
  }
  bool LookActive() const {
    return bloom > 0.001f || glow > 0.001f || vignette > 0.001f || grain > 0.001f || blur > 0.001f ||
           brightness * brightness > 1e-6f ||
           contrast * contrast > 1e-6f || saturation * saturation > 1e-6f;
  }
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
  std::string debug_menu = "F6";     // menu debug original Neversoft
};

// Seccion [game]. language: idioma de los textos del juego. WoR trae los textos en ingles (pak/qs), frances
// (qs_f), aleman (qs_g), italiano (qs_i) y espanol (qs_s); el juego carga el que le dice el sistema (XGetLanguage,
// cvar user_language del SDK). Requiere reiniciar.
struct GameConfig {
  std::string language = "en";   // en, es, fr, de, it
};

struct Config {
  GameConfig game;
  InputConfig input;
  PerfConfig perf;
  GraphicsConfig graphics;
  HotkeyConfig hotkeys;
  std::vector<ModEntryConfig> mods;  // orden = prioridad (primero gana)
  DevConfig dev;
};

// Acceso global (thread-safe para lectura/escritura con el mutex).
Config& Get();
void ResetKeysToDefaults(InputConfig& input);
// Teclas de fabrica de un jugador para su instrumento (solo la tabla de ese instrumento).
void ResetPlayerKeys(PlayerSlotConfig& p, Instrument which);
const char* DefaultGuitarKeys(GuitarAction a);
const char* DefaultDrumKeys(DrumAction a);
const char* DefaultPadKeys(PadAction a);
std::mutex& Mutex();

// Tecla configurada si el SDK la reconoce; si no (vacia o mal escrita), la de por defecto.
std::string HotkeyOr(const std::string& key, const char* fallback);

std::filesystem::path ConfigPath();
void Load();
bool Save();

}  // namespace re_wor::de
