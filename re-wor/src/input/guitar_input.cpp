#include "input/guitar_input.hpp"
#include "core/crash_handler.hpp"

#include <iostream>

namespace re_wor::input {

GuitarDevice::GuitarDevice(ControllerSlot slot)
    : slot_(slot) {
  state_.instrument = InstrumentType::Guitar5Fret;
  state_.device_name = "Guitar Controller (Slot " + std::to_string(static_cast<int>(slot)) + ")";
}

void GuitarDevice::Configure(const GuitarConfig& config) {
  config_ = config;
}

void GuitarDevice::Update() {
  // Enlace con polling del sistema HID / XInput / SDL de ReXGlue
}

bool GuitarDevice::IsConnected() const {
  return state_.is_connected;
}

InstrumentType GuitarDevice::GetGuitarType() const {
  return state_.instrument;
}

uint16_t GuitarDevice::GetFretsPressed() const {
  return static_cast<uint16_t>(state_.buttons_pressed & 0x1F);
}

bool GuitarDevice::IsStrummingUp() const {
  return (state_.buttons_pressed & (1 << 5)) != 0;
}

bool GuitarDevice::IsStrummingDown() const {
  return (state_.buttons_pressed & (1 << 6)) != 0;
}

float GuitarDevice::GetWhammyValue() const {
  return state_.whammy_axis;
}

bool GuitarDevice::IsTiltActive() const {
  return state_.tilt_axis >= config_.tilt_sensitivity;
}

void initialize_guitar_subsystem(rex::Runtime* runtime, const GuitarConfig& config) {
  (void)runtime;
  TRACE("[Input::Guitar] Subsystem initialized. Santroller detection: "
        << (config.enable_santroller_detection ? "ENABLED" : "DISABLED")
        << ", Tilt sensitivity: " << config.tilt_sensitivity);
}

}  // namespace re_wor::input

