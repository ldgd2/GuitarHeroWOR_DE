#pragma once

#include "input/input_types.hpp"
#include <rex/runtime.h>

namespace re_wor::input {

struct GamepadConfig {
  bool swap_strum_buttons = false;
  float stick_deadzone = 0.15f;
  bool enable_rumble = true;
};

class GamepadDevice {
 public:
  explicit GamepadDevice(ControllerSlot slot);
  ~GamepadDevice() = default;

  void Configure(const GamepadConfig& config);
  void Update();

  bool IsConnected() const;
  uint32_t GetButtons() const;
  void SetRumble(float low_freq, float high_freq);

 private:
  ControllerSlot slot_;
  GamepadConfig config_;
  InputDeviceState state_;
};

void initialize_gamepad_subsystem(rex::Runtime* runtime, const GamepadConfig& config);

}  // namespace re_wor::input

