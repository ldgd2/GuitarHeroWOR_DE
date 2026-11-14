#include "input/bass_input.hpp"
#include "core/crash_handler.hpp"

namespace re_wor::input {

BassDevice::BassDevice(ControllerSlot slot)
    : slot_(slot) {
  state_.instrument = InstrumentType::Bass;
  state_.device_name = "Bass Guitar (Slot " + std::to_string(static_cast<int>(slot)) + ")";
}

void BassDevice::Configure(const BassConfig& config) {
  config_ = config;
}

void BassDevice::Update() {
  // Enlace con polling del bajo
}

bool BassDevice::IsConnected() const {
  return state_.is_connected;
}

bool BassDevice::IsOpenNotePressed() const {
  return (state_.buttons_pressed & Fret_Open) != 0;
}

uint16_t BassDevice::GetFretsPressed() const {
  return static_cast<uint16_t>(state_.buttons_pressed & 0x1F);
}

float BassDevice::GetTouchStripPosition() const {
  return state_.slider_position;
}

void initialize_bass_subsystem(rex::Runtime* runtime, const BassConfig& config) {
  (void)runtime;
  TRACE("[Input::Bass] Subsystem initialized. Open notes: "
        << (config.enable_open_notes ? "ENABLED" : "DISABLED")
        << ", Touch strip: " << (config.enable_touch_strip ? "ENABLED" : "DISABLED"));
}

}  // namespace re_wor::input

