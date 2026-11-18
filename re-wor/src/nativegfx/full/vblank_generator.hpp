// nativegfx/full/vblank_generator.hpp
// Generador autonomo de senales VBlank e interrupciones de cuadro (Fase 4: Completo).
// En el emulador tradicional, el plugin de GPU (rexgpu-xenos.dll) genera los VBlanks
// cuando procesa los paquetes de swap PM4.
// Al desacoplar y apagar la emulacion de la GPU de Xbox, el motor del juego aun
// necesita recibir senales VBlank regulares (a 60 Hz o la tasa elegida) para no
// quedarse congelado esperando el barrido vertical.
#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>

namespace re_wor::nativegfx {

class VBlankGenerator {
 public:
  using VBlankCallback = std::function<void(uint64_t vblank_count)>;

  static VBlankGenerator& Instance();

  void Initialize(uint32_t target_refresh_rate_hz = 60);
  void Shutdown();

  void SetTargetRefreshRate(uint32_t target_hz);
  uint32_t GetTargetRefreshRate() const { return target_hz_; }

  // Se llama en cada swap/presentacion nativa del frame
  void OnSwap();

  // Espera al proximo intervalo de VBlank si es necesario para mantener el ritmo
  void WaitForVBlank();

  // Registro de callbacks para notificar al motor del juego o runtime
  void SetVBlankCallback(VBlankCallback callback) { callback_ = std::move(callback); }

  uint64_t GetTotalVBlanks() const { return vblank_count_.load(std::memory_order_relaxed); }
  double GetAverageFrameTimeMs() const { return avg_frame_time_ms_.load(std::memory_order_relaxed); }
  double GetCalculatedFps() const { return calculated_fps_.load(std::memory_order_relaxed); }

 private:
  VBlankGenerator() = default;

  std::atomic<bool> is_running_{false};
  uint32_t target_hz_ = 60;
  std::chrono::nanoseconds frame_interval_ns_{16666667};

  std::atomic<uint64_t> vblank_count_{0};
  std::chrono::steady_clock::time_point last_swap_time_;

  std::atomic<double> avg_frame_time_ms_{16.666};
  std::atomic<double> calculated_fps_{60.0};

  VBlankCallback callback_;
};

}  // namespace re_wor::nativegfx
