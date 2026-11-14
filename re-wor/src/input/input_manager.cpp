#include "input/input_manager.hpp"
#include "core/crash_handler.hpp"

namespace re_wor::input {

InputManager& InputManager::Instance() {
  static InputManager instance;
  return instance;
}

InputManager::InputManager() {
  for (uint8_t i = 0; i < static_cast<uint8_t>(ControllerSlot::Count); ++i) {
    auto slot = static_cast<ControllerSlot>(i);
    guitars_.push_back(std::make_unique<GuitarDevice>(slot));
    drums_.push_back(std::make_unique<DrumsDevice>(slot));
    basses_.push_back(std::make_unique<BassDevice>(slot));
    mics_.push_back(std::make_unique<MicrophoneDevice>(slot));
    gamepads_.push_back(std::make_unique<GamepadDevice>(slot));
  }
}

void InputManager::Initialize(rex::Runtime* runtime, const InputManagerConfig& config) {
  runtime_ = runtime;
  config_ = config;

  TRACE("[InputManager] Initializing Unified Instrument & Controller Architecture...");
  TRACE("[InputManager]   Framerate Decoupling: " << (config_.unlock_framerate ? "ENABLED" : "LOCKED 60Hz"));
  TRACE("[InputManager]   Target Refresh Rate:  " << config_.target_refresh_rate << " Hz");
  TRACE("[InputManager]   Target Resolution:    " << config_.resolution_width << "x" << config_.resolution_height);

  initialize_guitar_subsystem(runtime, config_.guitar_config);
  initialize_drums_subsystem(runtime, config_.drums_config);
  initialize_bass_subsystem(runtime, config_.bass_config);
  initialize_microphone_subsystem(runtime, config_.mic_config);
  initialize_gamepad_subsystem(runtime, config_.gamepad_config);

  for (auto& g : guitars_) g->Configure(config_.guitar_config);
  for (auto& d : drums_) d->Configure(config_.drums_config);
  for (auto& b : basses_) b->Configure(config_.bass_config);
  for (auto& m : mics_) m->Configure(config_.mic_config);
  for (auto& p : gamepads_) p->Configure(config_.gamepad_config);

  TRACE("[InputManager] All 4 instrument slots configured successfully");
}

void InputManager::PollEvents() {
  for (auto& g : guitars_) g->Update();
  for (auto& d : drums_) d->Update();
  for (auto& b : basses_) b->Update();
  for (auto& m : mics_) m->Update();
  for (auto& p : gamepads_) p->Update();
}

float InputManager::GetTargetRefreshRate() const {
  return config_.unlock_framerate ? config_.target_refresh_rate : 60.0f;
}

bool InputManager::IsHighRefreshRateEnabled() const {
  return config_.unlock_framerate && (config_.target_refresh_rate > 60.0f);
}

GuitarDevice& InputManager::GetGuitar(ControllerSlot slot) {
  return *guitars_[static_cast<size_t>(slot)];
}

DrumsDevice& InputManager::GetDrums(ControllerSlot slot) {
  return *drums_[static_cast<size_t>(slot)];
}

BassDevice& InputManager::GetBass(ControllerSlot slot) {
  return *basses_[static_cast<size_t>(slot)];
}

MicrophoneDevice& InputManager::GetMicrophone(ControllerSlot slot) {
  return *mics_[static_cast<size_t>(slot)];
}

GamepadDevice& InputManager::GetGamepad(ControllerSlot slot) {
  return *gamepads_[static_cast<size_t>(slot)];
}

}  // namespace re_wor::input

