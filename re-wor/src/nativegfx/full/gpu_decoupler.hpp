// nativegfx/full/gpu_decoupler.hpp
// Desacoplador y conmutador del emulador de GPU (Fase 4: Completo).
// Administra el modo de ejecucion grafica:
//   - Emulated:   Ejecucion 100% tradicional en el emulador Xenos (PM4/EDRAM).
//   - Hybrid:     Fase 3 (render nativo solo del escenario 3D, autopista/HUD emulados).
//   - FullNative: Fase 4 (render nativo del 100% de draws y resolves, sin EDRAM, emulador apagado).
#pragma once

#include <atomic>
#include <cstdint>
#include <string>

namespace re_wor::nativegfx {

enum class PipelineMode : uint32_t {
  Emulated   = 0,
  Hybrid     = 1,
  FullNative = 2
};

const char* PipelineModeToString(PipelineMode mode);
PipelineMode PipelineModeFromString(const std::string& str);

class GpuDecoupler {
 public:
  static GpuDecoupler& Instance();

  void SetMode(PipelineMode mode);
  PipelineMode GetMode() const { return mode_.load(std::memory_order_relaxed); }

  bool IsFullNative() const { return GetMode() == PipelineMode::FullNative; }
  bool IsHybrid() const { return GetMode() == PipelineMode::Hybrid; }
  bool IsEmulated() const { return GetMode() == PipelineMode::Emulated; }

  // Determina si los paquetes PM4 y el microcodigo del Xenos deben ser omitidos en CPU
  bool ShouldBypassPm4Processing() const {
    return IsFullNative();
  }

  // Notificacion de llamada omitida del emulador
  void RecordBypassedPacket(uint32_t opcode, uint32_t dword_count);

  uint64_t GetTotalPacketsBypassed() const { return packets_bypassed_.load(std::memory_order_relaxed); }
  uint64_t GetTotalDwordsBypassed() const { return dwords_bypassed_.load(std::memory_order_relaxed); }

  void ResetStats();

 private:
  GpuDecoupler() = default;

  std::atomic<PipelineMode> mode_{PipelineMode::FullNative};
  std::atomic<uint64_t> packets_bypassed_{0};
  std::atomic<uint64_t> dwords_bypassed_{0};
};

}  // namespace re_wor::nativegfx
