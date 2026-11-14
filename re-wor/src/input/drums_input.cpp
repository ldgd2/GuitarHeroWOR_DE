#include "input/drums_input.hpp"
#include "core/crash_handler.hpp"

namespace re_wor::input {

DrumsDevice::DrumsDevice(ControllerSlot slot)
    : slot_(slot) {
  state_.instrument = InstrumentType::Drums5Lane;
  state_.device_name = "Drum Kit (Slot " + std::to_string(static_cast<int>(slot)) + ")";
}

void DrumsDevice::Configure(const DrumsConfig& config) {
  config_ = config;
  state_.instrument = config.enable_5lane_mode ? InstrumentType::Drums5Lane : InstrumentType::Drums4Lane;
}

void DrumsDevice::Update() {
  // Enlace con polling de MIDI / USB Drum Kits (Alesis, Roland, GHWT, GH5, Rock Band)
}

bool DrumsDevice::IsConnected() const {
  return state_.is_connected;
}

uint16_t DrumsDevice::GetPadsHit() const {
  return static_cast<uint16_t>(state_.buttons_pressed);
}

uint8_t DrumsDevice::GetPadVelocity(DrumFlags pad) const {
  int idx = 0;
  switch (pad) {
    case Drum_RedPad:    idx = 0; break;
    case Drum_YellowPad: idx = 1; break;
    case Drum_BluePad:   idx = 2; break;
    case Drum_GreenPad:  idx = 3; break;
    case Drum_OrangePad: idx = 4; break;
    case Drum_Kick1:     idx = 5; break;
    case Drum_Kick2:     idx = 6; break;
    default: return 127;
  }
  return state_.drum_velocities[idx];
}

bool DrumsDevice::IsKickPedalPressed() const {
  return (state_.buttons_pressed & Drum_Kick1) != 0;
}

bool DrumsDevice::IsDoubleKickActive() const {
  return (state_.buttons_pressed & Drum_Kick2) != 0;
}

void initialize_drums_subsystem(rex::Runtime* runtime, const DrumsConfig& config) {
  (void)runtime;
  TRACE("[Input::Drums] Subsystem initialized. 5-Lane Mode: "
        << (config.enable_5lane_mode ? "ACTIVE (GH Layout)" : "4-Lane (RB Layout)")
        << ", Velocity Sensing: " << (config.enable_velocity_sensing ? "ENABLED" : "DISABLED"));
}

}  // namespace re_wor::input

