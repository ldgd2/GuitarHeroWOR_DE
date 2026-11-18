// nativegfx/full/vblank_generator.cpp
// Implementacion del generador autonomo de VBlank (Fase 4: Completo).
#include "nativegfx/full/vblank_generator.hpp"

#include <thread>
#ifdef _WIN32
#include <windows.h>
#endif

namespace re_wor::nativegfx {

VBlankGenerator& VBlankGenerator::Instance() {
  static VBlankGenerator s_instance;
  return s_instance;
}

void VBlankGenerator::Initialize(uint32_t target_refresh_rate_hz) {
  SetTargetRefreshRate(target_refresh_rate_hz);
  vblank_count_.store(0);
  last_swap_time_ = std::chrono::steady_clock::now();
  is_running_.store(true);
}

void VBlankGenerator::Shutdown() {
  is_running_.store(false);
  callback_ = nullptr;
}

void VBlankGenerator::SetTargetRefreshRate(uint32_t target_hz) {
  target_hz_ = (target_hz >= 30 && target_hz <= 360) ? target_hz : 60;
  frame_interval_ns_ = std::chrono::nanoseconds(1000000000ull / target_hz_);
}

void VBlankGenerator::OnSwap() {
  if (!is_running_.load(std::memory_order_relaxed)) return;

  auto now = std::chrono::steady_clock::now();
  auto elapsed_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(now - last_swap_time_).count();
  last_swap_time_ = now;

  double frame_ms = elapsed_ns / 1000000.0;
  if (frame_ms > 0.0 && frame_ms < 500.0) {
    avg_frame_time_ms_.store(frame_ms, std::memory_order_relaxed);
    calculated_fps_.store(1000.0 / frame_ms, std::memory_order_relaxed);
  }

  uint64_t count = vblank_count_.fetch_add(1, std::memory_order_relaxed) + 1;

  if (callback_) {
    callback_(count);
  }
}

void VBlankGenerator::WaitForVBlank() {
  if (!is_running_.load(std::memory_order_relaxed)) return;

  auto now = std::chrono::steady_clock::now();
  auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(now - last_swap_time_);

  if (elapsed < frame_interval_ns_) {
    auto remaining = frame_interval_ns_ - elapsed;
    // Dormir si queda mas de 1 ms para liberar la CPU
    if (remaining > std::chrono::milliseconds(1)) {
      std::this_thread::sleep_for(remaining - std::chrono::milliseconds(1));
    }
    while (std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - last_swap_time_) < frame_interval_ns_) {
#ifdef _WIN32
      YieldProcessor();  // Instruccion hardware PAUSE: reduce consumo termico del procesador
#else
      std::this_thread::yield();
#endif
    }
  }
}

}  // namespace re_wor::nativegfx
