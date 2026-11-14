#pragma once

#include "input/input_types.hpp"
#include <rex/runtime.h>

namespace re_wor::input {

struct MicrophoneConfig {
  bool enable_pitch_detection = true;
  float gain_boost = 1.0f;
  float noise_gate_threshold = 0.05f;
  bool enable_percussion_tap = true; // Golpear el micrófono para partes de pandereta / palmas
};

class MicrophoneDevice {
 public:
  explicit MicrophoneDevice(ControllerSlot slot);
  ~MicrophoneDevice() = default;

  void Configure(const MicrophoneConfig& config);
  void Update();

  bool IsConnected() const;
  float GetDetectedPitchHz() const;
  float GetInputVolume() const;
  bool IsPercussionTapDetected() const;

 private:
  ControllerSlot slot_;
  MicrophoneConfig config_;
  InputDeviceState state_;
};

void initialize_microphone_subsystem(rex::Runtime* runtime, const MicrophoneConfig& config);

}  // namespace re_wor::input

