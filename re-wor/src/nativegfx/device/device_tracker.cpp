// nativegfx/device/device_tracker.cpp
// Implementacion del coordinador de seguimiento del dispositivo D3D.
#include "nativegfx/device/device_tracker.hpp"

#include <algorithm>

namespace re_wor::nativegfx {

DeviceTracker& DeviceTracker::Instance() {
  static DeviceTracker s_instance;
  return s_instance;
}

void DeviceTracker::AddListener(IDeviceEventListener* listener) {
  if (!listener) return;
  auto it = std::find(listeners_.begin(), listeners_.end(), listener);
  if (it == listeners_.end()) {
    listeners_.push_back(listener);
  }
}

void DeviceTracker::RemoveListener(IDeviceEventListener* listener) {
  auto it = std::find(listeners_.begin(), listeners_.end(), listener);
  if (it != listeners_.end()) {
    listeners_.erase(it);
  }
}

void DeviceTracker::OnD3DCall(const uint8_t* memory_base, uint32_t func, uint32_t dev_addr, const uint32_t args[8]) {
  if (!enabled_ || !memory_base || dev_addr < 0x00010000) return;

  // Actualizar los registros del lector desde la memoria del dispositivo huésped
  reader_.UpdateFromDevice(memory_base, dev_addr);

  if (func == 0x822E4538) {
    // Resolve (copia de superficie a textura)
    ++total_resolves_;
    if (!listeners_.empty()) {
      if (reader_.ExtractResolveCommand(func, args, cached_resolve_cmd_)) {
        for (auto* listener : listeners_) {
          listener->OnResolve(cached_resolve_cmd_);
        }
      }
    }
  } else {
    // Llamada de dibujo (Draw)
    ++total_draws_;
    if (!listeners_.empty()) {
      if (reader_.ExtractDrawCommand(func, args, memory_base, cached_cmd_, resolution_scale_)) {
        for (auto* listener : listeners_) {
          listener->OnDraw(cached_cmd_);
        }
      }
    }
  }
}

void DeviceTracker::OnShaderLoaded(uint32_t type, uint32_t guest_addr, uint32_t dword_count, uint64_t hash) {
  reader_.SetActiveShader(type, guest_addr, dword_count, hash);
}

void DeviceTracker::OnSwap() {
  if (!enabled_) return;
  ++total_frames_;
  for (auto* listener : listeners_) {
    listener->OnSwap();
  }
}

}  // namespace re_wor::nativegfx
