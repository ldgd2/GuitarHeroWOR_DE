// nativegfx/full/gpu_decoupler.cpp
// Implementacion del desacoplador del emulador de GPU (Fase 4: Completo).
#include "nativegfx/full/gpu_decoupler.hpp"

#include <algorithm>

namespace re_wor::nativegfx {

const char* PipelineModeToString(PipelineMode mode) {
  switch (mode) {
    case PipelineMode::Emulated:   return "emulated";
    case PipelineMode::Hybrid:     return "hybrid";
    case PipelineMode::FullNative: return "full";
    default:                       return "full";
  }
}

PipelineMode PipelineModeFromString(const std::string& str) {
  if (str == "emulated") return PipelineMode::Emulated;
  if (str == "hybrid")   return PipelineMode::Hybrid;
  return PipelineMode::FullNative;
}

GpuDecoupler& GpuDecoupler::Instance() {
  static GpuDecoupler s_instance;
  return s_instance;
}

void GpuDecoupler::SetMode(PipelineMode mode) {
  mode_.store(mode, std::memory_order_relaxed);
}

void GpuDecoupler::RecordBypassedPacket(uint32_t opcode, uint32_t dword_count) {
  (void)opcode;
  packets_bypassed_.fetch_add(1, std::memory_order_relaxed);
  dwords_bypassed_.fetch_add(dword_count, std::memory_order_relaxed);
}

void GpuDecoupler::ResetStats() {
  packets_bypassed_.store(0, std::memory_order_relaxed);
  dwords_bypassed_.store(0, std::memory_order_relaxed);
}

}  // namespace re_wor::nativegfx
