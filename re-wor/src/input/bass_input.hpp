#pragma once

#include "input/input_types.hpp"
#include <rex/runtime.h>

namespace re_wor::input {

struct BassConfig {
  bool enable_open_notes = true;      // Soporte de cuerdas al aire (Línea morada continua)
  bool enable_touch_strip = true;     // Soporte de slider strip para tap notes
  float open_strum_threshold = 0.5f;
};

class BassDevice {
 public:
  explicit BassDevice(ControllerSlot slot);
  ~BassDevice() = default;

  void Configure(const BassConfig& config);
  void Update();

  bool IsConnected() const;
  bool IsOpenNotePressed() const;
  uint16_t GetFretsPressed() const;
  float GetTouchStripPosition() const;

 private:
  ControllerSlot slot_;
  BassConfig config_;
  InputDeviceState state_;
};

void initialize_bass_subsystem(rex::Runtime* runtime, const BassConfig& config);

}  // namespace re_wor::input

