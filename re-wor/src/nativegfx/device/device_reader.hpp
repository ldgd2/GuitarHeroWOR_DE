// nativegfx/device/device_reader.hpp
// Lector de la estructura de dispositivo D3D de Xbox 360 en memoria (Fase 2).
// Extrae el estado grafico completo (registros sombra, constantes, texturas, shaders)
// sin requerir la interpretacion de paquetes PM4 por parte de la GPU emulada.
#pragma once

#include <cstdint>
#include <vector>
#include "nativegfx/common/register_map.hpp"
#include "nativegfx/common/types.hpp"
#include "nativegfx/device/draw_command.hpp"
#include "nativegfx/device/resolve_command.hpp"
#include "nativegfx/state/pipeline_state.hpp"

namespace re_wor::nativegfx {

class DeviceReader {
 public:
  DeviceReader();
  ~DeviceReader() = default;

  // Actualiza los registros internos leyendo la memoria del dispositivo huésped.
  // Usa las máscaras de sucio (dev + 0..47) para actualizar solo lo que cambió.
  void UpdateFromDevice(const uint8_t* memory_base, uint32_t device_guest_addr, bool force_all = false);

  // Notifica la carga o vinculacion de un shader (IM_LOAD / SetShader)
  void SetActiveShader(uint32_t type, uint32_t guest_addr, uint32_t dword_count, uint64_t hash);

  // Extrae la instantánea tipada del pipeline gráfico actual
  bool ExtractPipelineState(PipelineState& out_state, float resolution_scale = 1.0f) const;

  // Extrae y construye un comando de dibujo a partir de una llamada D3D interceptada
  bool ExtractDrawCommand(uint32_t func_addr, const uint32_t args[8], const uint8_t* memory_base,
                          DrawCommand& out_cmd, float resolution_scale = 1.0f) const;

  // Extrae y construye un comando de resolve a partir de sub_822E4538
  bool ExtractResolveCommand(uint32_t func_addr, const uint32_t args[8], ResolveCommand& out_cmd) const;

  // Acceso directo a los registros leídos (para compatibilidad / inspección)
  uint32_t GetRegister(uint32_t reg_index) const {
    return (reg_index < kMaxRegisters) ? shadow_regs_[reg_index] : 0;
  }

  uint32_t GetDeviceGuestAddress() const { return current_device_addr_; }

 private:
  static constexpr uint32_t kMaxRegisters = 0x5000;

  uint32_t current_device_addr_ = 0;
  uint32_t shadow_regs_[kMaxRegisters] = {};
  uint8_t previous_dirty_masks_[kDeviceDirtyBytes] = {};

  BoundShaderInfo active_vs_;
  BoundShaderInfo active_ps_;

  // Decodifica la topología de la primitiva a partir del tipo nativo de Xenos
  PrimitiveTopology DecodeTopology(uint32_t prim_type) const;

  // Calcula el rectángulo de recorte (Scissor)
  bool CalculateScissor(Rect2D& out_rect, float scale) const;
};

}  // namespace re_wor::nativegfx
