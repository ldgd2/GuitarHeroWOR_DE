#include "input/controller_input.hpp"
#include "core/crash_handler.hpp"

namespace re_wor::input {

GamepadDevice::GamepadDevice(ControllerSlot slot)
    : slot_(slot) {
  state_.instrument = InstrumentType::Gamepad;
  state_.device_name = "Standard Controller (Slot " + std::to_string(static_cast<int>(slot)) + ")";
}

void GamepadDevice::Configure(const GamepadConfig& config) {
  config_ = config;
}

void GamepadDevice::Update() {
  // Enlace con XInput / SDL Gamepad
}

bool GamepadDevice::IsConnected() const {
  return state_.is_connected;
}

uint32_t GamepadDevice::GetButtons() const {
  return state_.buttons_pressed;
}

void GamepadDevice::SetRumble(float low_freq, float high_freq) {
  (void)low_freq;
  (void)high_freq;
}

void initialize_gamepad_subsystem(rex::Runtime* runtime, const GamepadConfig& config) {
  (void)runtime;
  TRACE("[Input::Gamepad] Subsystem initialized. Rumble: "
        << (config.enable_rumble ? "ENABLED" : "DISABLED")
        << ", Stick deadzone: " << config.stick_deadzone);
}

}  // namespace re_wor::input

