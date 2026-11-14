#pragma once

#include "input/input_types.hpp"
#include <rex/runtime.h>

namespace re_wor::input {

struct DrumsConfig {
  bool enable_5lane_mode = true;       // True: GH 5-lane, False: RB 4-lane
  bool enable_velocity_sensing = true; // Dinámica de golpeo suave/fuerte
  float cymbal_threshold = 0.15f;
  float kick_debounce_ms = 40.0f;      // Anti-rebote para pedales de doble bombo
};

class DrumsDevice {
 public:
  explicit DrumsDevice(ControllerSlot slot);
  ~DrumsDevice() = default;

  void Configure(const DrumsConfig& config);
  void Update();

  bool IsConnected() const;
  uint16_t GetPadsHit() const;
  uint8_t GetPadVelocity(DrumFlags pad) const;
  bool IsKickPedalPressed() const;
  bool IsDoubleKickActive() const;

 private:
  ControllerSlot slot_;
  DrumsConfig config_;
  InputDeviceState state_;
};

void initialize_drums_subsystem(rex::Runtime* runtime, const DrumsConfig& config);

}  // namespace re_wor::input

