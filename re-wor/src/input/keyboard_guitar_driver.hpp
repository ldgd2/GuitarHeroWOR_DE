#pragma once
// =============================================================================
// KeyboardGuitarDriver - Teclados del PC como mandos de Xbox 360, uno por jugador.
//
// Cada jugador de [[input.players]] puede tener un teclado: uno concreto (por su ruta de Windows), "*" (los
// que no use otro jugador) o ninguno. En modo "guitar" el juego lo ve como una guitarra de Guitar Hero
// (trastes, rasgueo, palanca, tilt), bajo, bateria (pads y pedal) o, para quien canta, un mando (A, B,
// cruceta...) con microfono. Cada jugador tiene sus propias teclas, y cada tecla viene marcada con su teclado
// (raw input, KeyEvent::keyboard_id): dos teclados con las mismas teclas no se pisan.
// =============================================================================

#include <rex/input/input_driver.h>
#include <rex/ui/window_listener.h>

#include <array>
#include <atomic>
#include <map>
#include <mutex>
#include <queue>
#include <string>
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
  // Sin ningun control para el jugador 1 (ni mando ni teclado suyo): cualquier teclado hace de mando de
  // Xbox para el jugador 1 (flechas, Enter = A, Esc/Retroceso = B, Espacio = Start) y asi se puede llegar
  // a Opciones > Controls. Se quita sola en cuanto el jugador 1 tiene un control.
  void SetMenuFallback(bool on);
  bool MenuFallback() const { return menu_fallback_; }

  // Captura de tecla para reasignar desde la UI: la siguiente tecla pulsada
  // se guarda y NO se envia al juego. Devuelve 0 mientras no haya tecla.
  void BeginKeyCapture();
  void CancelKeyCapture();
  uint16_t PollCapturedKey(std::string* keyboard_path = nullptr);

  // Estado actual de cada accion (para el visor de entrada del panel), del primer jugador con guitarra.
  bool IsActionDown(re_wor::de::GuitarAction a);

  // Deteccion de teclado para la pantalla Controles: la siguiente tecla dice de que teclado viene (y no llega
  // al juego). PollDetectedKeyboard devuelve la ruta ("" mientras no haya).
  void BeginKeyboardDetect();
  void CancelKeyboardDetect();
  std::string PollDetectedKeyboard();

  // Botones de un jugador ahora mismo (para el visor de la pantalla Controles); 0 si no tiene teclado
  uint16_t PlayerButtons(int player);
  // Acciones pulsadas de un jugador (bit = indice de GuitarAction, DrumAction o PadAction segun su
  // instrumento), aunque el teclado este suprimido: para el visor en vivo de la pantalla Controles.
  uint32_t PlayerPressedMask(int player);

  // Mientras el panel DE (F8 por defecto) esta abierto el teclado no llega al juego.
  static void SetSuppressed(bool suppressed);

 private:
  void RefreshKeystrokesLocked();
  void Detach();

  using KeyState = std::array<bool, 256>;
  enum class Mode { kGuitar, kDrums, kPad };
  struct Player {
    std::string keyboard;          // "" ninguno, "*" cualquiera libre, o ruta
    Mode mode = Mode::kGuitar;
    uint8_t subtype = 6;           // subtipo XInput que ve el juego
    bool has_mic = false;
    std::vector<uint16_t> guitar[static_cast<int>(re_wor::de::GuitarAction::kCount)];
    std::vector<uint16_t> drums[static_cast<int>(re_wor::de::DrumAction::kCount)];
    std::vector<uint16_t> pad[static_cast<int>(re_wor::de::PadAction::kCount)];
    uint32_t pressed = 0;          // bitmask por accion (para keystrokes)
    std::queue<rex::input::X_INPUT_KEYSTROKE> keystrokes;
    uint32_t packet_number = 0;
  };

  rex::ui::Window* attached_window_ = nullptr;
  std::mutex mutex_;
  std::atomic<bool> menu_fallback_{false};
  // Teclas por teclado (id de raw input; 0 = desconocido)
  std::map<uint32_t, KeyState> keys_by_kb_;
  std::map<uint32_t, std::string> path_by_kb_;
  Player players_[re_wor::de::kMaxPlayers];
  // Marca la tecla (y sus equivalentes: pad numerico <-> navegacion, Shift/Ctrl/Alt por lado).
  void ApplyKeyLocked(KeyState& keys, uint16_t vk, bool down);
  bool PlayerUsesKeyboardLocked(int player, uint32_t kb);
  bool KeyDownLocked(int player, uint16_t vk);
  uint32_t PressedMaskLocked(int player);
  uint16_t ComputeButtonsLocked(int player, int16_t* whammy, int16_t* tilt);
  void PushKeystroke(Player& pl, uint16_t vk_pad, bool down);
  std::atomic<bool> detecting_{false};
  std::string detected_path_;
  std::atomic<bool> has_focus_{true};
  std::atomic<bool> capturing_{false};
  std::atomic<uint16_t> captured_vk_{0};
  std::string captured_path_;
};

}  // namespace re_wor::input
