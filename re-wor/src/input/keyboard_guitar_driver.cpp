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
      std::memset(self->key_down_, 0, sizeof(self->key_down_));
      self->RefreshKeystrokesLocked();
    }
  }
}

X_STATUS KeyboardGuitarDriver::Setup() {
  TRACE("[KeyboardGuitar] Driver de guitarra por teclado listo");
  return X_STATUS_SUCCESS;
}

void KeyboardGuitarDriver::ReloadBindings() {
  std::string keys[static_cast<int>(GuitarAction::kCount)];
  {
    std::lock_guard cfg_lock(re_wor::de::Mutex());
    auto& cfg = re_wor::de::Get();
    for (int i = 0; i < static_cast<int>(GuitarAction::kCount); i++) keys[i] = cfg.input.keys[i];
  }
  std::lock_guard lock(mutex_);
  for (int i = 0; i < static_cast<int>(GuitarAction::kCount); i++) {
    bindings_[i] = ParseKeyList(keys[i]);
  }
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
  std::memset(key_down_, 0, sizeof(key_down_));
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

uint16_t KeyboardGuitarDriver::PollCapturedKey() {
  uint16_t vk = captured_vk_.exchange(0);
  if (vk) capturing_ = false;
  return vk;
}

namespace {

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

}  // namespace

void KeyboardGuitarDriver::ApplyKeyLocked(uint16_t vk, bool down) {
  key_down_[vk] = down;
  if (uint16_t nav = NumpadNavAlias(vk)) key_down_[nav] = down;
  if (vk == 0x10 || vk == 0x11 || vk == 0x12) {
    // Generico + cada lado segun el estado real de Windows (al soltar uno puede seguir el otro).
    const int l = vk == 0x10 ? 0xA0 : vk == 0x11 ? 0xA2 : 0xA4;
    key_down_[l] = SideDown(l);
    key_down_[l + 1] = SideDown(l + 1);
    key_down_[vk] = key_down_[l] || key_down_[l + 1] || down;
    if (!down && !key_down_[l] && !key_down_[l + 1]) key_down_[vk] = false;
  }
}

void KeyboardGuitarDriver::OnKeyDown(rex::ui::KeyEvent& e) {
  const uint16_t vk = static_cast<uint16_t>(e.virtual_key());
  if (capturing_) {
    if (vk != 0 && vk < 256) {
      captured_vk_ = ResolveSide(vk);
      e.set_handled(true);
    }
    return;
  }
  if (g_suppressed || !has_focus_ || vk >= 256) return;
  std::lock_guard lock(mutex_);
  ApplyKeyLocked(vk, true);
  RefreshKeystrokesLocked();
}

void KeyboardGuitarDriver::OnKeyUp(rex::ui::KeyEvent& e) {
  const uint16_t vk = static_cast<uint16_t>(e.virtual_key());
  if (vk >= 256) return;
  std::lock_guard lock(mutex_);
  ApplyKeyLocked(vk, false);
  RefreshKeystrokesLocked();
}

bool KeyboardGuitarDriver::IsActionDown(GuitarAction a) {
  std::lock_guard lock(mutex_);
  for (uint16_t vk : bindings_[static_cast<int>(a)]) {
    if (vk < 256 && key_down_[vk]) return true;
  }
  return false;
}

uint16_t KeyboardGuitarDriver::ComputeButtonsLocked(int16_t* whammy, int16_t* tilt) {
  uint16_t buttons = 0;
  bool whammy_down = false;
  bool tilt_down = false;
  for (int i = 0; i < static_cast<int>(GuitarAction::kCount); i++) {
    bool down = false;
    for (uint16_t vk : bindings_[i]) {
      if (vk < 256 && key_down_[vk]) {
        down = true;
        break;
      }
    }
    if (!down) continue;
    if (i == static_cast<int>(GuitarAction::kWhammy)) {
      whammy_down = true;
    } else if (i == static_cast<int>(GuitarAction::kTilt)) {
      tilt_down = true;
    } else {
      buttons |= kActionButtons[i];
    }
  }
  // Guitarra X360: palanca = stick derecho X (reposo -32768),
  // inclinacion = stick derecho Y (vertical = +32767).
  *whammy = whammy_down ? int16_t(32767) : int16_t(-32768);
  *tilt = tilt_down ? int16_t(32767) : int16_t(0);
  return buttons;
}

void KeyboardGuitarDriver::RefreshKeystrokesLocked() {
  uint32_t pressed = 0;
  for (int i = 0; i < static_cast<int>(GuitarAction::kCount); i++) {
    for (uint16_t vk : bindings_[i]) {
      if (vk < 256 && key_down_[vk]) {
        pressed |= 1u << i;
        break;
      }
    }
  }
  const uint32_t changed = pressed ^ pressed_actions_;
  pressed_actions_ = pressed;
  for (int i = 0; i < static_cast<int>(GuitarAction::kCount); i++) {
    if (changed & (1u << i)) PushKeystroke(kActionPadKeys[i], (pressed & (1u << i)) != 0);
  }
}

void KeyboardGuitarDriver::PushKeystroke(uint16_t vk_pad, bool down) {
  X_INPUT_KEYSTROKE ks = {};
  ks.virtual_key = vk_pad;
  ks.unicode = 0;
  ks.flags = down ? X_INPUT_KEYSTROKE_KEYDOWN : X_INPUT_KEYSTROKE_KEYUP;
  ks.user_index = 0;
  ks.hid_code = 0;
  if (keystrokes_.size() >= kMaxQueuedKeystrokes) keystrokes_.pop();
  keystrokes_.push(ks);
}

bool KeyboardGuitarDriverEnabled();

void KeyboardGuitarDriver::EnumerateDevices(std::vector<DeviceInfo>& out) {
  if (!KeyboardGuitarDriverEnabled()) return;
  DeviceInfo info;
  info.id = kKeyboardGuitarDevice;
  info.name = "Guitarra de teclado (re-wor DE)";
  info.guid = "re-wor-keyboard-guitar";
  {
    std::lock_guard cfg_lock(re_wor::de::Mutex());
    info.subtype = static_cast<uint8_t>(re_wor::de::Get().input.keyboard_guitar_subtype);
  }
  info.synthetic = true;
  out.push_back(info);
}

X_RESULT KeyboardGuitarDriver::GetDeviceCapabilities(DeviceId id, uint32_t,
                                                     X_INPUT_CAPABILITIES* out_caps) {
  if (id != kKeyboardGuitarDevice || !KeyboardGuitarDriverEnabled()) {
    return X_ERROR_DEVICE_NOT_CONNECTED;
  }
  if (out_caps) {
    std::memset(out_caps, 0, sizeof(*out_caps));
    out_caps->type = XINPUT_DEVTYPE_GAMEPAD;
    {
      std::lock_guard cfg_lock(re_wor::de::Mutex());
      out_caps->sub_type = static_cast<uint8_t>(re_wor::de::Get().input.keyboard_guitar_subtype);
    }
    out_caps->flags = 0;
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
  if (id != kKeyboardGuitarDevice || !KeyboardGuitarDriverEnabled()) {
    return X_ERROR_DEVICE_NOT_CONNECTED;
  }
  if (!out_state) return X_ERROR_SUCCESS;
  std::memset(out_state, 0, sizeof(*out_state));
  int16_t whammy = -32768;
  int16_t tilt = 0;
  uint16_t buttons = 0;
  {
    std::lock_guard lock(mutex_);
    if (has_focus_ && !g_suppressed && is_active()) {
      buttons = ComputeButtonsLocked(&whammy, &tilt);
    }
    packet_number_++;
    out_state->packet_number = packet_number_;
  }
  buttons |= AutoPressButtons();
  out_state->gamepad.buttons = buttons;
  out_state->gamepad.thumb_rx = whammy;
  out_state->gamepad.thumb_ry = tilt;
  return X_ERROR_SUCCESS;
}

X_RESULT KeyboardGuitarDriver::SetDeviceVibration(DeviceId id, X_INPUT_VIBRATION*) {
  if (id != kKeyboardGuitarDevice || !KeyboardGuitarDriverEnabled()) {
    return X_ERROR_DEVICE_NOT_CONNECTED;
  }
  return X_ERROR_SUCCESS;
}

X_RESULT KeyboardGuitarDriver::GetDeviceKeystroke(DeviceId id, uint32_t,
                                                  X_INPUT_KEYSTROKE* out_keystroke) {
  if (id != kKeyboardGuitarDevice || !KeyboardGuitarDriverEnabled()) {
    return X_ERROR_DEVICE_NOT_CONNECTED;
  }
  std::lock_guard lock(mutex_);
  if (keystrokes_.empty()) return X_ERROR_EMPTY;
  if (out_keystroke) *out_keystroke = keystrokes_.front();
  keystrokes_.pop();
  return X_ERROR_SUCCESS;
}

bool KeyboardGuitarDriverEnabled() {
  std::lock_guard cfg_lock(re_wor::de::Mutex());
  return re_wor::de::Get().input.keyboard_guitar;
}

}  // namespace re_wor::input
