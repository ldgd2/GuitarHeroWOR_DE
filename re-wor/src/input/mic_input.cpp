#include "input/mic_input.hpp"
#include "core/crash_handler.hpp"

namespace re_wor::input {

MicrophoneDevice::MicrophoneDevice(ControllerSlot slot)
    : slot_(slot) {
  state_.instrument = InstrumentType::Microphone;
  state_.device_name = "USB Microphone (Slot " + std::to_string(static_cast<int>(slot)) + ")";
}

void MicrophoneDevice::Configure(const MicrophoneConfig& config) {
  config_ = config;
}

void MicrophoneDevice::Update() {
  // Enlace con captura de audio de SDL / WASAPI / micrófono USB
}

bool MicrophoneDevice::IsConnected() const {
  return state_.is_connected;
}

float MicrophoneDevice::GetDetectedPitchHz() const {
  return state_.mic_pitch;
}

float MicrophoneDevice::GetInputVolume() const {
  return state_.mic_amplitude;
}

bool MicrophoneDevice::IsPercussionTapDetected() const {
  return config_.enable_percussion_tap && (state_.mic_amplitude > config_.noise_gate_threshold * 4.0f);
}

void initialize_microphone_subsystem(rex::Runtime* runtime, const MicrophoneConfig& config) {
  (void)runtime;
  TRACE("[Input::Microphone] Subsystem initialized. Pitch detection: "
        << (config.enable_pitch_detection ? "ENABLED" : "DISABLED")
        << ", Percussion tap: " << (config.enable_percussion_tap ? "ENABLED" : "DISABLED"));
}

}  // namespace re_wor::input

