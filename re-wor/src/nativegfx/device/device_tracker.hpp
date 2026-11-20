// nativegfx/device/device_tracker.hpp
// Coordinador global de seguimiento del dispositivo Direct3D (Fase 2).
// Notifica eventos de dibujo y resolución a oyentes registrados (backend nativo o depurador).
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>
#include "nativegfx/device/device_reader.hpp"
#include "nativegfx/device/draw_command.hpp"
#include "nativegfx/device/resolve_command.hpp"

namespace re_wor::nativegfx {

// Interfaz para consumidores de eventos de la GPU nativa
class IDeviceEventListener {
 public:
  virtual ~IDeviceEventListener() = default;
  virtual void OnDraw(const DrawCommand& cmd) {}
  virtual void OnResolve(const ResolveCommand& cmd) {}
  virtual void OnSwap() {}
};

class DeviceTracker {
 public:
  static DeviceTracker& Instance();

  // Activa o desactiva la captura y extraccion de estados
  void SetEnabled(bool enabled) { enabled_ = enabled; }
  bool IsEnabled() const { return enabled_; }

  void SetResolutionScale(float scale) { resolution_scale_ = (scale > 0.0f ? scale : 1.0f); }
  float GetResolutionScale() const { return resolution_scale_; }

  // Registro de oyentes (listeners)
  void AddListener(IDeviceEventListener* listener);
  void RemoveListener(IDeviceEventListener* listener);

  // Metodos llamados desde los ganchos de ejecucion (d3d_tap / de_hooks)
  void OnD3DCall(const uint8_t* memory_base, uint32_t func, uint32_t dev_addr, const uint32_t args[8]);
  void OnShaderLoaded(uint32_t type, uint32_t guest_addr, uint32_t dword_count, uint64_t hash);
  void OnSwap();

  DeviceReader& GetReader() { return reader_; }
  const DeviceReader& GetReader() const { return reader_; }

  // Estadisticas acumuladas
  uint64_t GetDrawCount() const { return total_draws_; }
  uint64_t GetResolveCount() const { return total_resolves_; }
  uint64_t GetFrameCount() const { return total_frames_; }

 private:
  DeviceTracker() = default;

  bool enabled_ = true;
  float resolution_scale_ = 1.0f;
  DeviceReader reader_;
  std::vector<IDeviceEventListener*> listeners_;

  DrawCommand cached_cmd_;
  ResolveCommand cached_resolve_cmd_;

  uint64_t total_draws_    = 0;
  uint64_t total_resolves_ = 0;
  uint64_t total_frames_   = 0;
};

}  // namespace re_wor::nativegfx
