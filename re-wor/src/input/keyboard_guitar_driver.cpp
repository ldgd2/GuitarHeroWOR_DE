#include "input/keyboard_guitar_driver.hpp"

#include <rex/input/input.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
#include <rex/ui/ui_event.h>
#include <rex/ui/window.h>
#include <rex/ui/windowed_app_context.h>

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <string>

#include "core/crash_handler.hpp"
#include "input/devices.hpp"
#include "input/key_names.hpp"

namespace re_wor::input {

using namespace rex::input;
using re_wor::de::GuitarAction;

namespace {

constexpr DeviceId kKeyboardGuitarDevice = static_cast<DeviceId>(0x47554954);  // 'GUIT'
constexpr size_t kMaxQueuedKeystrokes = 64;

std::atomic<KeyboardGuitarDriver*> g_instance{nullptr};
std::atomic<bool> g_suppressed{false};

// Boton XInput de cada accion (0 = no es boton).
constexpr uint16_t kActionButtons[] = {
    X_INPUT_GAMEPAD_A,              // verde
    X_INPUT_GAMEPAD_B,              // rojo
    X_INPUT_GAMEPAD_Y,              // amarillo
    X_INPUT_GAMEPAD_X,              // azul
    X_INPUT_GAMEPAD_LEFT_SHOULDER,  // naranja
    X_INPUT_GAMEPAD_DPAD_UP,        // rasgueo arriba
    X_INPUT_GAMEPAD_DPAD_DOWN,      // rasgueo abajo
    X_INPUT_GAMEPAD_START,          // start
    X_INPUT_GAMEPAD_BACK,           // star power
    0,                              // whammy (eje)
    0,                              // tilt (eje)
    X_INPUT_GAMEPAD_DPAD_LEFT,      // menu izquierda
    X_INPUT_GAMEPAD_DPAD_RIGHT,     // menu derecha
};

// VK_PAD_* equivalente para XInputGetKeystroke (menus del sistema).
constexpr uint16_t kActionPadKeys[] = {
    0x5800, 0x5801, 0x5803, 0x5802, 0x5805,  // A B Y X LB
    0x5810, 0x5811, 0x5814, 0x5815,          // dpad up/down, start, back
    0x5832, 0x5830,                          // rstick right / up
    0x5812, 0x5813,                          // dpad left/right
};

static_assert(sizeof(kActionButtons) / sizeof(kActionButtons[0]) ==
              static_cast<size_t>(GuitarAction::kCount));
static_assert(sizeof(kActionPadKeys) / sizeof(kActionPadKeys[0]) ==
              static_cast<size_t>(GuitarAction::kCount));

// Diagnostico: REWOR_AUTOPRESS="start@30,green@36,down@40" pulsa botones solos
// (150 ms) a los N segundos de arrancar, para llegar a escenas 3D sin manos.
struct AutoPress {
  uint16_t buttons;
  double at;
};
std::vector<AutoPress> LoadAutoPress() {
  std::vector<AutoPress> out;
  const char* env = std::getenv("REWOR_AUTOPRESS");
  if (!env) return out;
  std::string spec = env;
  size_t pos = 0;
  while (pos < spec.size()) {
    size_t comma = spec.find(',', pos);
    std::string item = spec.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
    size_t at = item.find('@');
    if (at != std::string::npos) {
      std::string name = item.substr(0, at);
      double t = std::atof(item.c_str() + at + 1);
      uint16_t b = 0;
      if (name == "green" || name == "a") b = X_INPUT_GAMEPAD_A;
      else if (name == "red" || name == "b") b = X_INPUT_GAMEPAD_B;
      else if (name == "yellow" || name == "y") b = X_INPUT_GAMEPAD_Y;
      else if (name == "blue" || name == "x") b = X_INPUT_GAMEPAD_X;
      else if (name == "orange") b = X_INPUT_GAMEPAD_LEFT_SHOULDER;
      else if (name == "start") b = X_INPUT_GAMEPAD_START;
      else if (name == "back") b = X_INPUT_GAMEPAD_BACK;
      else if (name == "up") b = X_INPUT_GAMEPAD_DPAD_UP;
      else if (name == "down") b = X_INPUT_GAMEPAD_DPAD_DOWN;
      else if (name == "left") b = X_INPUT_GAMEPAD_DPAD_LEFT;
      else if (name == "right") b = X_INPUT_GAMEPAD_DPAD_RIGHT;
      if (b) out.push_back({b, t});
    }
    if (comma == std::string::npos) break;
    pos = comma + 1;
  }
  return out;
}

uint16_t AutoPressButtons() {
  static const std::vector<AutoPress> presses = LoadAutoPress();
  static const auto start = std::chrono::steady_clock::now();
  if (presses.empty()) return 0;
  double t = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
  uint16_t b = 0;
  for (auto& p : presses) {
    if (t >= p.at && t < p.at + 0.15) b |= p.buttons;
  }
  return b;
}

// El SDK traduce las teclas por su posicion fisica (scancode de SDL): el pad numerico llega siempre
// como Numpad0..9 / Decimal, este o no activo Bloq Num. Windows (y el lanzador) en cambio lo ven
// como Fin, Flecha abajo, etc. cuando Bloq Num esta apagado. Cada tecla del pad cuenta tambien como
// su equivalente de navegacion, asi vale lo que se haya guardado. (Solo en ese sentido: las flechas
// de verdad no activan acciones asignadas al pad.)
uint16_t NumpadNavAlias(uint16_t vk) {
  switch (vk) {
    case 0x60: return 0x2D;  // Numpad0 -> Insert
    case 0x61: return 0x23;  // Numpad1 -> End
    case 0x62: return 0x28;  // Numpad2 -> Down
    case 0x63: return 0x22;  // Numpad3 -> PageDown
    case 0x64: return 0x25;  // Numpad4 -> Left
    case 0x65: return 0x0C;  // Numpad5 -> Clear
    case 0x66: return 0x27;  // Numpad6 -> Right
    case 0x67: return 0x24;  // Numpad7 -> Home
    case 0x68: return 0x26;  // Numpad8 -> Up
    case 0x69: return 0x21;  // Numpad9 -> PageUp
    case 0x6E: return 0x2E;  // Decimal -> Delete
    default: return 0;
  }
}

// El SDK junta Shift/Ctrl/Alt izquierdo y derecho en uno solo; Windows sabe cual es.
bool SideDown(int vk) {
#ifdef _WIN32
  return (GetAsyncKeyState(vk) & 0x8000) != 0;
#else
  (void)vk;
  return false;
#endif
}

uint16_t ResolveSide(uint16_t vk) {
  switch (vk) {
    case 0x10: return SideDown(0xA1) ? 0xA1 : 0xA0;  // Shift -> LShift / RShift
    case 0x11: return SideDown(0xA3) ? 0xA3 : 0xA2;  // Control -> LControl / RControl
    case 0x12: return SideDown(0xA5) ? 0xA5 : 0xA4;  // Alt -> LAlt / RAlt
    default: return vk;
  }
}

// Modo mando: boton XInput y VK_PAD de cada accion (orden de PadAction)
constexpr uint16_t kPadButtons[] = {
    X_INPUT_GAMEPAD_A,         X_INPUT_GAMEPAD_B,          X_INPUT_GAMEPAD_X,         X_INPUT_GAMEPAD_Y,
    X_INPUT_GAMEPAD_START,     X_INPUT_GAMEPAD_BACK,       X_INPUT_GAMEPAD_DPAD_UP,   X_INPUT_GAMEPAD_DPAD_DOWN,
    X_INPUT_GAMEPAD_DPAD_LEFT, X_INPUT_GAMEPAD_DPAD_RIGHT, X_INPUT_GAMEPAD_LEFT_SHOULDER,
    X_INPUT_GAMEPAD_RIGHT_SHOULDER,
};
constexpr uint16_t kPadPadKeys[] = {0x5800, 0x5801, 0x5802, 0x5803, 0x5814, 0x5815,
                                    0x5810, 0x5811, 0x5812, 0x5813, 0x5805, 0x5804};
static_assert(sizeof(kPadButtons) / sizeof(kPadButtons[0]) == static_cast<size_t>(re_wor::de::PadAction::kCount));
static_assert(sizeof(kPadPadKeys) / sizeof(kPadPadKeys[0]) == static_cast<size_t>(re_wor::de::PadAction::kCount));

// Bateria de Guitar Hero (Xbox 360): verde A, rojo B, amarillo Y, azul X, naranja RB, pedal LB (orden de DrumAction)
constexpr uint16_t kDrumButtons[] = {
    X_INPUT_GAMEPAD_B,         X_INPUT_GAMEPAD_Y,         X_INPUT_GAMEPAD_X,          X_INPUT_GAMEPAD_RIGHT_SHOULDER,
    X_INPUT_GAMEPAD_A,         X_INPUT_GAMEPAD_LEFT_SHOULDER, X_INPUT_GAMEPAD_START,  X_INPUT_GAMEPAD_BACK,
    X_INPUT_GAMEPAD_DPAD_UP,   X_INPUT_GAMEPAD_DPAD_DOWN, X_INPUT_GAMEPAD_DPAD_LEFT,  X_INPUT_GAMEPAD_DPAD_RIGHT,
};
constexpr uint16_t kDrumPadKeys[] = {0x5801, 0x5803, 0x5802, 0x5804, 0x5800, 0x5805,
                                     0x5814, 0x5815, 0x5810, 0x5811, 0x5812, 0x5813};
static_assert(sizeof(kDrumButtons) / sizeof(kDrumButtons[0]) == static_cast<size_t>(re_wor::de::DrumAction::kCount));
static_assert(sizeof(kDrumPadKeys) / sizeof(kDrumPadKeys[0]) == static_cast<size_t>(re_wor::de::DrumAction::kCount));

constexpr int kPlayers = re_wor::de::kMaxPlayers;

DeviceId PlayerDevice(int p) { return static_cast<DeviceId>(static_cast<uint64_t>(kKeyboardGuitarDevice) + p); }
int DevicePlayer(DeviceId id) {
  const uint64_t d = static_cast<uint64_t>(id) - static_cast<uint64_t>(kKeyboardGuitarDevice);
  return d < static_cast<uint64_t>(kPlayers) ? static_cast<int>(d) : -1;
}

}  // namespace

KeyboardGuitarDriver::KeyboardGuitarDriver(rex::ui::Window* window, size_t window_z_order)
    : InputDriver(window, window_z_order) {
  ReloadBindings();
  g_instance.store(this);
}

KeyboardGuitarDriver::~KeyboardGuitarDriver() {
  KeyboardGuitarDriver* self = this;
  g_instance.compare_exchange_strong(self, nullptr);
  Detach();
}

KeyboardGuitarDriver* KeyboardGuitarDriver::Instance() {
  return g_instance.load();
}

void KeyboardGuitarDriver::SetSuppressed(bool suppressed) {
  g_suppressed.store(suppressed);
  if (suppressed) {
    if (auto* self = Instance()) {
      std::lock_guard lock(self->mutex_);
      self->keys_by_kb_.clear();
      self->RefreshKeystrokesLocked();
    }
  }
}

X_STATUS KeyboardGuitarDriver::Setup() {
  TRACE("[KeyboardGuitar] Driver de teclados (hasta 4 jugadores) listo");
  return X_STATUS_SUCCESS;
}

void KeyboardGuitarDriver::ReloadBindings() {
  using re_wor::de::Instrument;
  re_wor::de::PlayerSlotConfig players[kPlayers];
  int guitar_subtype = 6;
  {
    std::lock_guard cfg_lock(re_wor::de::Mutex());
    auto& cfg = re_wor::de::Get();
    for (int i = 0; i < kPlayers; i++) players[i] = cfg.input.players[i];
    guitar_subtype = cfg.input.keyboard_guitar_subtype;
  }
  std::lock_guard lock(mutex_);
  uint8_t old_subtype[kPlayers];
  bool old_connected[kPlayers];
  for (int i = 0; i < kPlayers; i++) {
    old_subtype[i] = players_[i].subtype;
    old_connected[i] = !players_[i].keyboard.empty();
  }
  for (int i = 0; i < kPlayers; i++) {
    Player& pl = players_[i];
    const auto& c = players[i];
    pl.keyboard = c.keyboard;
    pl.has_mic = !c.mic.empty();
    switch (re_wor::de::InstrumentFromId(c.instrument)) {
      case Instrument::kBass: pl.mode = Mode::kGuitar; pl.subtype = 11; break;   // GUITAR_BASS
      case Instrument::kDrums: pl.mode = Mode::kDrums; pl.subtype = 8; break;    // DRUM_KIT
      case Instrument::kVocals: pl.mode = Mode::kPad; pl.subtype = 1; break;     // GAMEPAD (+ microfono)
      default: pl.mode = Mode::kGuitar; pl.subtype = static_cast<uint8_t>(guitar_subtype); break;
    }
    for (int a = 0; a < static_cast<int>(GuitarAction::kCount); a++) pl.guitar[a] = ParseKeyList(c.guitar_keys[a]);
    for (int a = 0; a < static_cast<int>(re_wor::de::DrumAction::kCount); a++) pl.drums[a] = ParseKeyList(c.drum_keys[a]);
    for (int a = 0; a < static_cast<int>(re_wor::de::PadAction::kCount); a++) pl.pad[a] = ParseKeyList(c.pad_keys[a]);
  }
  if (menu_fallback_ && players_[0].keyboard.empty()) {
    Player& pl = players_[0];
    pl.keyboard = "**";   // cualquier teclado, aunque sea de otro jugador
    pl.mode = Mode::kPad;
    pl.subtype = 1;       // GAMEPAD
    for (int a = 0; a < static_cast<int>(re_wor::de::PadAction::kCount); a++)
      pl.pad[a] = ParseKeyList(re_wor::de::DefaultPadKeys(static_cast<re_wor::de::PadAction>(a)));
  }
  for (int i = 0; i < kPlayers; i++) {
    Player& pl = players_[i];
    if (old_connected[i] && !pl.keyboard.empty() && old_subtype[i] != pl.subtype) {
      pl.unplugged_until = std::chrono::steady_clock::now() + std::chrono::milliseconds(600);
      TRACE("[Input] jugador " << (i + 1) << ": su mando de teclado cambia de tipo (" << int(old_subtype[i]) << " -> "
                                << int(pl.subtype) << "), se reconecta");
    }
  }
  RefreshKeystrokesLocked();
}

void KeyboardGuitarDriver::SetMenuFallback(bool on) {
  if (menu_fallback_.exchange(on) == on) return;
  TRACE("[Input] jugador 1 sin control: teclado como mando " << (on ? "activado" : "desactivado"));
  ReloadBindings();
}

void KeyboardGuitarDriver::OnWindowAvailable(rex::ui::Window* window) {
  if (!window) return;
  {
    std::lock_guard lock(mutex_);
    attached_window_ = window;
  }
  window->AddInputListener(this, window_z_order());
  window->AddListener(this);
}

void KeyboardGuitarDriver::Detach() {
  rex::ui::Window* window = attached_window_;
  if (!window) return;
  window->app_context().CallInUIThreadSynchronous([this, window] {
    {
      std::lock_guard lock(mutex_);
      attached_window_ = nullptr;
    }
    window->RemoveInputListener(this);
    window->RemoveListener(this);
  });
}

void KeyboardGuitarDriver::OnClosing(rex::ui::UIEvent&) {
  Detach();
}

void KeyboardGuitarDriver::OnLostFocus(rex::ui::UISetupEvent&) {
  has_focus_ = false;
  std::lock_guard lock(mutex_);
  keys_by_kb_.clear();
  RefreshKeystrokesLocked();
}

void KeyboardGuitarDriver::OnGotFocus(rex::ui::UISetupEvent&) {
  has_focus_ = true;
}

void KeyboardGuitarDriver::BeginKeyCapture() {
  captured_vk_ = 0;
  capturing_ = true;
}

void KeyboardGuitarDriver::CancelKeyCapture() {
  capturing_ = false;
  captured_vk_ = 0;
}

uint16_t KeyboardGuitarDriver::PollCapturedKey(std::string* keyboard_path) {
  uint16_t vk = captured_vk_.exchange(0);
  if (vk) {
    capturing_ = false;
    if (keyboard_path) {
      std::lock_guard lock(mutex_);
      *keyboard_path = captured_path_;
    }
  }
  return vk;
}

void KeyboardGuitarDriver::BeginKeyboardDetect() {
  std::lock_guard lock(mutex_);
  detected_path_.clear();
  detecting_ = true;
}

void KeyboardGuitarDriver::CancelKeyboardDetect() {
  std::lock_guard lock(mutex_);
  detecting_ = false;
  detected_path_.clear();
}

std::string KeyboardGuitarDriver::PollDetectedKeyboard() {
  std::lock_guard lock(mutex_);
  std::string p = std::move(detected_path_);
  detected_path_.clear();
  return p;
}

void KeyboardGuitarDriver::ApplyKeyLocked(KeyState& keys, uint16_t vk, bool down) {
  keys[vk] = down;
  if (uint16_t nav = NumpadNavAlias(vk)) keys[nav] = down;
  if (vk == 0x10 || vk == 0x11 || vk == 0x12) {
    // Generico + cada lado segun el estado real de Windows (al soltar uno puede seguir el otro).
    const int l = vk == 0x10 ? 0xA0 : vk == 0x11 ? 0xA2 : 0xA4;
    keys[l] = SideDown(l);
    keys[l + 1] = SideDown(l + 1);
    keys[vk] = keys[l] || keys[l + 1] || down;
    if (!down && !keys[l] && !keys[l + 1]) keys[vk] = false;
  }
}

// Un teclado sirve a un jugador si es el suyo, o si el jugador acepta "cualquiera" (el primero con "*") y
// ningun otro jugador lo tiene asignado.
bool KeyboardGuitarDriver::PlayerUsesKeyboardLocked(int player, uint32_t kb) {
  const std::string& rule = players_[player].keyboard;
  if (rule.empty()) return false;
  if (rule == "**") return true;
  auto it = path_by_kb_.find(kb);
  const std::string path = it != path_by_kb_.end() ? it->second : std::string();
  if (rule != "*") return !path.empty() && rule == path;
  for (int i = 0; i < kPlayers; i++) {
    if (i != player && !path.empty() && players_[i].keyboard == path) return false;
  }
  for (int i = 0; i < player; i++) {
    if (players_[i].keyboard == "*") return false;
  }
  return true;
}

bool KeyboardGuitarDriver::KeyDownLocked(int player, uint16_t vk) {
  if (vk >= 256) return false;
  for (auto& [kb, keys] : keys_by_kb_) {
    if (keys[vk] && PlayerUsesKeyboardLocked(player, kb)) return true;
  }
  return false;
}

uint32_t KeyboardGuitarDriver::PressedMaskLocked(int player) {
  Player& pl = players_[player];
  const std::vector<uint16_t>* lists = pl.guitar;
  int count = static_cast<int>(GuitarAction::kCount);
  if (pl.mode == Mode::kDrums) {
    lists = pl.drums;
    count = static_cast<int>(re_wor::de::DrumAction::kCount);
  } else if (pl.mode == Mode::kPad) {
    lists = pl.pad;
    count = static_cast<int>(re_wor::de::PadAction::kCount);
  }
  uint32_t pressed = 0;
  for (int i = 0; i < count; i++) {
    for (uint16_t vk : lists[i]) {
      if (KeyDownLocked(player, vk)) {
        pressed |= 1u << i;
        break;
      }
    }
  }
  return pressed;
}

void KeyboardGuitarDriver::OnKeyDown(rex::ui::KeyEvent& e) {
  const uint16_t vk = static_cast<uint16_t>(e.virtual_key());
  const uint32_t kb = e.keyboard_id();
  if (detecting_) {
    std::string path = KeyboardPathForId(kb);
    if (!path.empty()) {
      std::lock_guard lock(mutex_);
      detected_path_ = path;
      detecting_ = false;
      e.set_handled(true);
      return;
    }
  }
  if (capturing_) {
    if (vk != 0 && vk < 256) {
      {
        std::lock_guard lock(mutex_);
        captured_path_ = KeyboardPathForId(kb);
      }
      captured_vk_ = ResolveSide(vk);
      e.set_handled(true);
    }
    return;
  }
  if (!has_focus_ || vk >= 256) return;   // suprimido: se sigue leyendo (visor de Controles) pero no llega al juego
  const std::string path = KeyboardPathForId(kb);
  std::lock_guard lock(mutex_);
  path_by_kb_[kb] = path;
  ApplyKeyLocked(keys_by_kb_[kb], vk, true);
  RefreshKeystrokesLocked();
}

void KeyboardGuitarDriver::OnKeyUp(rex::ui::KeyEvent& e) {
  const uint16_t vk = static_cast<uint16_t>(e.virtual_key());
  if (vk >= 256) return;
  std::lock_guard lock(mutex_);
  auto it = keys_by_kb_.find(e.keyboard_id());
  if (it != keys_by_kb_.end()) ApplyKeyLocked(it->second, vk, false);
  RefreshKeystrokesLocked();
}

bool KeyboardGuitarDriver::IsActionDown(GuitarAction a) {
  std::lock_guard lock(mutex_);
  for (int p = 0; p < kPlayers; p++) {
    if (players_[p].keyboard.empty() || players_[p].mode != Mode::kGuitar) continue;
    for (uint16_t vk : players_[p].guitar[static_cast<int>(a)]) {
      if (KeyDownLocked(p, vk)) return true;
    }
    return false;
  }
  return false;
}

uint32_t KeyboardGuitarDriver::PlayerPressedMask(int player) {
  if (player < 0 || player >= kPlayers) return 0;
  std::lock_guard lock(mutex_);
  return players_[player].keyboard.empty() ? 0 : PressedMaskLocked(player);
}

uint16_t KeyboardGuitarDriver::PlayerButtons(int player) {
  if (player < 0 || player >= kPlayers) return 0;
  std::lock_guard lock(mutex_);
  if (players_[player].keyboard.empty()) return 0;
  int16_t w = 0, t = 0;
  return ComputeButtonsLocked(player, &w, &t);
}

uint16_t KeyboardGuitarDriver::ComputeButtonsLocked(int player, int16_t* whammy, int16_t* tilt) {
  uint16_t buttons = 0;
  const uint32_t pressed = PressedMaskLocked(player);
  *whammy = int16_t(-32768);
  *tilt = 0;
  if (players_[player].mode == Mode::kPad) {
    for (int i = 0; i < static_cast<int>(re_wor::de::PadAction::kCount); i++) {
      if (pressed & (1u << i)) buttons |= kPadButtons[i];
    }
    *whammy = 0;
    return buttons;
  }
  if (players_[player].mode == Mode::kDrums) {
    for (int i = 0; i < static_cast<int>(re_wor::de::DrumAction::kCount); i++) {
      if (pressed & (1u << i)) buttons |= kDrumButtons[i];
    }
    *whammy = 0;
    return buttons;
  }
  for (int i = 0; i < static_cast<int>(GuitarAction::kCount); i++) {
    if (!(pressed & (1u << i))) continue;
    if (i == static_cast<int>(GuitarAction::kWhammy)) {
      *whammy = int16_t(32767);   // guitarra X360: palanca = stick derecho X (reposo -32768)
    } else if (i == static_cast<int>(GuitarAction::kTilt)) {
      *tilt = int16_t(32767);     // inclinacion = stick derecho Y (vertical = +32767)
    } else {
      buttons |= kActionButtons[i];
    }
  }
  return buttons;
}

void KeyboardGuitarDriver::RefreshKeystrokesLocked() {
  for (int p = 0; p < kPlayers; p++) {
    Player& pl = players_[p];
    const uint32_t pressed = (pl.keyboard.empty() || g_suppressed) ? 0 : PressedMaskLocked(p);
    const uint32_t changed = pressed ^ pl.pressed;
    pl.pressed = pressed;
    const uint16_t* pad_keys = pl.mode == Mode::kPad ? kPadPadKeys : pl.mode == Mode::kDrums ? kDrumPadKeys : kActionPadKeys;
    const int count = pl.mode == Mode::kPad     ? static_cast<int>(re_wor::de::PadAction::kCount)
                      : pl.mode == Mode::kDrums ? static_cast<int>(re_wor::de::DrumAction::kCount)
                                                : static_cast<int>(GuitarAction::kCount);
    for (int i = 0; i < count; i++) {
      if (changed & (1u << i)) PushKeystroke(pl, pad_keys[i], (pressed & (1u << i)) != 0);
    }
  }
}

void KeyboardGuitarDriver::PushKeystroke(Player& pl, uint16_t vk_pad, bool down) {
  X_INPUT_KEYSTROKE ks = {};
  ks.virtual_key = vk_pad;
  ks.unicode = 0;
  ks.flags = down ? X_INPUT_KEYSTROKE_KEYDOWN : X_INPUT_KEYSTROKE_KEYUP;
  ks.user_index = 0;
  ks.hid_code = 0;
  if (pl.keystrokes.size() >= kMaxQueuedKeystrokes) pl.keystrokes.pop();
  pl.keystrokes.push(ks);
}

void KeyboardGuitarDriver::EnumerateDevices(std::vector<DeviceInfo>& out) {
  std::lock_guard lock(mutex_);
  for (int p = 0; p < kPlayers; p++) {
    if (players_[p].keyboard.empty()) continue;
    DeviceInfo info;
    info.id = PlayerDevice(p);
    const char* what = players_[p].mode == Mode::kPad     ? "Mando de teclado"
                       : players_[p].mode == Mode::kDrums ? "Bateria de teclado"
                       : players_[p].subtype == 11        ? "Bajo de teclado"
                                                          : "Guitarra de teclado";
    info.name = std::string(what) + " (jugador " + std::to_string(p + 1) + ")";
    // re-wor-kbd-p<N>: PlayerAssignment (input_factory.cpp) lo manda al mando N del juego
    info.guid = "re-wor-kbd-p" + std::to_string(p);
    info.subtype = players_[p].subtype;
    info.synthetic = true;
    out.push_back(info);
  }
}

X_RESULT KeyboardGuitarDriver::GetDeviceCapabilities(DeviceId id, uint32_t, X_INPUT_CAPABILITIES* out_caps) {
  const int p = DevicePlayer(id);
  std::lock_guard lock(mutex_);
  if (p < 0 || players_[p].keyboard.empty() || UnpluggedLocked(p)) return X_ERROR_DEVICE_NOT_CONNECTED;
  if (out_caps) {
    std::memset(out_caps, 0, sizeof(*out_caps));
    out_caps->type = XINPUT_DEVTYPE_GAMEPAD;
    out_caps->sub_type = players_[p].subtype;
    out_caps->flags = players_[p].has_mic ? 0x0004 : 0;   // XINPUT_CAPS_VOICE_SUPPORTED: tiene "auriculares"
    out_caps->gamepad.buttons = 0xFFFF;
    out_caps->gamepad.left_trigger = 0xFF;
    out_caps->gamepad.right_trigger = 0xFF;
    out_caps->gamepad.thumb_lx = static_cast<int16_t>(0x7FFF);
    out_caps->gamepad.thumb_ly = static_cast<int16_t>(0x7FFF);
    out_caps->gamepad.thumb_rx = static_cast<int16_t>(0x7FFF);
    out_caps->gamepad.thumb_ry = static_cast<int16_t>(0x7FFF);
  }
  return X_ERROR_SUCCESS;
}

X_RESULT KeyboardGuitarDriver::GetDeviceState(DeviceId id, X_INPUT_STATE* out_state) {
  const int p = DevicePlayer(id);
  if (p < 0) return X_ERROR_DEVICE_NOT_CONNECTED;
  int16_t whammy = -32768;
  int16_t tilt = 0;
  uint16_t buttons = 0;
  {
    std::lock_guard lock(mutex_);
    if (players_[p].keyboard.empty() || UnpluggedLocked(p)) return X_ERROR_DEVICE_NOT_CONNECTED;
    if (!out_state) return X_ERROR_SUCCESS;
    std::memset(out_state, 0, sizeof(*out_state));
    if (has_focus_ && !g_suppressed && is_active()) {
      buttons = ComputeButtonsLocked(p, &whammy, &tilt);
    } else if (players_[p].mode != Mode::kGuitar) {
      whammy = 0;
    }
    out_state->packet_number = ++players_[p].packet_number;
  }
  if (p == 0) buttons |= AutoPressButtons();
  out_state->gamepad.buttons = buttons;
  out_state->gamepad.thumb_rx = whammy;
  out_state->gamepad.thumb_ry = tilt;
  return X_ERROR_SUCCESS;
}

X_RESULT KeyboardGuitarDriver::SetDeviceVibration(DeviceId id, X_INPUT_VIBRATION*) {
  return DevicePlayer(id) < 0 ? X_ERROR_DEVICE_NOT_CONNECTED : X_ERROR_SUCCESS;
}

X_RESULT KeyboardGuitarDriver::GetDeviceKeystroke(DeviceId id, uint32_t, X_INPUT_KEYSTROKE* out_keystroke) {
  const int p = DevicePlayer(id);
  if (p < 0) return X_ERROR_DEVICE_NOT_CONNECTED;
  std::lock_guard lock(mutex_);
  if (players_[p].keyboard.empty()) return X_ERROR_DEVICE_NOT_CONNECTED;
  auto& q = players_[p].keystrokes;
  if (q.empty()) return X_ERROR_EMPTY;
  if (out_keystroke) *out_keystroke = q.front();
  q.pop();
  return X_ERROR_SUCCESS;
}

}  // namespace re_wor::input

