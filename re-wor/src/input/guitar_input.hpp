#pragma once

#include "input/input_types.hpp"
#include <rex/runtime.h>

namespace re_wor::input {

struct GuitarConfig {
  bool enable_santroller_detection = true;
  bool enable_tap_slider = true;
  float whammy_deadzone = 0.05f;
  float tilt_sensitivity = 0.75f;
};

class GuitarDevice {
 public:
  explicit GuitarDevice(ControllerSlot slot);
  ~GuitarDevice() = default;

  void Configure(const GuitarConfig& config);
  void Update();

  bool IsConnected() const;
  InstrumentType GetGuitarType() const;
  uint16_t GetFretsPressed() const;
  bool IsStrummingUp() const;
  bool IsStrummingDown() const;
  float GetWhammyValue() const;
  bool IsTiltActive() const;

 private:
  ControllerSlot slot_;
  GuitarConfig config_;
  InputDeviceState state_;
};

void initialize_guitar_subsystem(rex::Runtime* runtime, const GuitarConfig& config);

}  // namespace re_wor::input

