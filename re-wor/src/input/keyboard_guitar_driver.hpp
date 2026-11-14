#pragma once
// =============================================================================
// KeyboardGuitarDriver - Emula una guitarra de Guitar Hero (Xbox 360) con el
// teclado. El juego la ve como un dispositivo XInput de subtipo GUITAR, asi
// que funciona igual que una guitarra real (trastes, rasgueo, palanca, tilt).
// Las teclas se leen de re_wor_de.toml y se pueden reasignar en vivo (F8 por defecto, configurable).
// =============================================================================

#include <rex/input/input_driver.h>
#include <rex/ui/window_listener.h>

#include <atomic>
#include <mutex>
#include <queue>
#include <vector>

#include "de/de_config.hpp"

namespace re_wor::input {

using rex::X_RESULT;
using rex::X_STATUS;

class KeyboardGuitarDriver final : public rex::input::InputDriver,
                                   public rex::ui::WindowInputListener,
                                   public rex::ui::WindowListener {
 public:
  KeyboardGuitarDriver(rex::ui::Window* window, size_t window_z_order);
  ~KeyboardGuitarDriver() override;

  // Instancia activa (para el panel DE (F8 por defecto)). Puede ser nullptr.
  static KeyboardGuitarDriver* Instance();

  X_STATUS Setup() override;
  void EnumerateDevices(std::vector<rex::input::DeviceInfo>& out) override;
  X_RESULT GetDeviceState(rex::input::DeviceId id, rex::input::X_INPUT_STATE* out_state) override;
  X_RESULT GetDeviceCapabilities(rex::input::DeviceId id, uint32_t flags,
                                 rex::input::X_INPUT_CAPABILITIES* out_caps) override;
  X_RESULT SetDeviceVibration(rex::input::DeviceId id,
                              rex::input::X_INPUT_VIBRATION* vibration) override;
  X_RESULT GetDeviceKeystroke(rex::input::DeviceId id, uint32_t flags,
                              rex::input::X_INPUT_KEYSTROKE* out_keystroke) override;
  void OnWindowAvailable(rex::ui::Window* window) override;

  // WindowInputListener
  void OnKeyDown(rex::ui::KeyEvent& e) override;
  void OnKeyUp(rex::ui::KeyEvent& e) override;

  // WindowListener
  void OnClosing(rex::ui::UIEvent& e) override;
  void OnLostFocus(rex::ui::UISetupEvent& e) override;
  void OnGotFocus(rex::ui::UISetupEvent& e) override;

  // Vuelve a leer las teclas desde la configuracion.
  void ReloadBindings();

  // Captura de tecla para reasignar desde la UI: la siguiente tecla pulsada
  // se guarda y NO se envia al juego. Devuelve 0 mientras no haya tecla.
  void BeginKeyCapture();
  void CancelKeyCapture();
  uint16_t PollCapturedKey();

  // Estado actual de cada accion (para el visor de entrada del panel).
  bool IsActionDown(re_wor::de::GuitarAction a);

  // Mientras el panel DE (F8 por defecto) esta abierto el teclado no llega al juego.
  static void SetSuppressed(bool suppressed);

 private:
  uint16_t ComputeButtonsLocked(int16_t* whammy, int16_t* tilt);
  void RefreshKeystrokesLocked();
  void PushKeystroke(uint16_t vk_pad, bool down);
  void Detach();

  rex::ui::Window* attached_window_ = nullptr;
  std::mutex mutex_;
  bool key_down_[256] = {};
  // Marca la tecla (y sus equivalentes: pad numerico <-> navegacion, Shift/Ctrl/Alt por lado).
  void ApplyKeyLocked(uint16_t vk, bool down);
  std::vector<uint16_t> bindings_[static_cast<int>(re_wor::de::GuitarAction::kCount)];
  uint32_t pressed_actions_ = 0;  // bitmask por accion (para keystrokes)
  std::queue<rex::input::X_INPUT_KEYSTROKE> keystrokes_;
  uint32_t packet_number_ = 0;
  std::atomic<bool> has_focus_{true};
  std::atomic<bool> capturing_{false};
  std::atomic<uint16_t> captured_vk_{0};
};

}  // namespace re_wor::input
