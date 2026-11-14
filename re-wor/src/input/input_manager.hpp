#pragma once

#include "input/input_types.hpp"
#include "input/guitar_input.hpp"
#include "input/drums_input.hpp"
#include "input/bass_input.hpp"
#include "input/mic_input.hpp"
#include "input/controller_input.hpp"

#include <memory>
#include <vector>
#include <rex/runtime.h>

namespace re_wor::input {

struct InputManagerConfig {
  bool unlock_framerate = true;
  float target_refresh_rate = 144.0f; // 60, 120, 144, 240, 360 Hz
  uint32_t resolution_width = 1920;
  uint32_t resolution_height = 1080;
  GuitarConfig guitar_config;
  DrumsConfig drums_config;
  BassConfig bass_config;
  MicrophoneConfig mic_config;
  GamepadConfig gamepad_config;
};

class InputManager {
 public:
  static InputManager& Instance();

  void Initialize(rex::Runtime* runtime, const InputManagerConfig& config = {});
  void PollEvents();

  float GetTargetRefreshRate() const;
  bool IsHighRefreshRateEnabled() const;

  GuitarDevice& GetGuitar(ControllerSlot slot = ControllerSlot::Player1);
  DrumsDevice& GetDrums(ControllerSlot slot = ControllerSlot::Player1);
  BassDevice& GetBass(ControllerSlot slot = ControllerSlot::Player1);
  MicrophoneDevice& GetMicrophone(ControllerSlot slot = ControllerSlot::Player1);
  GamepadDevice& GetGamepad(ControllerSlot slot = ControllerSlot::Player1);

 private:
  InputManager();
  ~InputManager() = default;

  InputManagerConfig config_;
  rex::Runtime* runtime_ = nullptr;

  std::vector<std::unique_ptr<GuitarDevice>> guitars_;
  std::vector<std::unique_ptr<DrumsDevice>> drums_;
  std::vector<std::unique_ptr<BassDevice>> basses_;
  std::vector<std::unique_ptr<MicrophoneDevice>> mics_;
  std::vector<std::unique_ptr<GamepadDevice>> gamepads_;
};

}  // namespace re_wor::input

