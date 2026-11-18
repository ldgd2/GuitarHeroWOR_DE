#pragma once
// Referencia de xenos2hlsl_check: copia del interprete de shaders del SDK
// (src/graphics/pipeline/shader/interpreter.cpp) con estos cambios, todos para que haga lo mismo que el traductor
// DXBC del SDK, que es lo que el juego ejecuta hoy:
//   - memoria: un bufer propio en vez de memory::Memory;
//   - texturas: tfetch devuelve un color fijo por constante de fetch (texture_colors); el check usa texturas de
//     1 texel de ese color, asi el resultado no depende de coordenadas ni filtrado;
//   - kill: marca el pixel como descartado (el original no implementa kill);
//   - bucles: aL = inicio + paso por iteracion y pila de un nivel por bucle (el original sube la pila dos veces y
//     lee aL del nivel equivocado); repeat conserva el aL anterior; cuenta 0 salta el bucle;
//   - vfetch: los formatos empaquetados sin signo se desplazan por su offset (el original no desplaza),
//     FLOAT16 es half IEEE (f16tof32, como DXBC) y no el half de la 360 sin infinitos, y no se limita la lectura
//     al tamano de la constante de fetch (DXBC lee de la memoria compartida sin ese limite).
//     El indice pasa a entero como ftoi (satura, NaN = 0) y la direccion se calcula en bytes con 32 bits;
//   - abs de constantes (bit abs_constants): el original no lo aplica nunca, el analizador y DXBC si (tambien al
//     registro de las formas escalares c0 / c1).
/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 *
 * @modified    Tom Clay, 2026 - Adapted for ReXGlue runtime
 */

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

#include <rex/assert.h>
#include <rex/graphics/format/ucode.h>
#include <rex/graphics/pipeline/shader/shader.h>
#include <rex/graphics/register_file.h>
#include <rex/graphics/xenos.h>
#include <rex/memory.h>

namespace rex::graphics::refcheck {

class RefInterpreter {
 public:
  RefInterpreter(const RegisterFile& register_file, const uint8_t* membase, uint32_t memsize)
      : register_file_(register_file), membase_(membase), memsize_(memsize) {}

  // color (x, y, z, w) que devuelve un tfetch de cada constante de fetch de textura
  std::array<std::array<float, 4>, 32> texture_colors{};
  bool killed() const { return killed_; }
  // getTexGradients / getTexLOD / getTexWeights / getBCF: sin referencia (el check no compara ese shader)
  bool used_unsupported_fetch() const { return unsupported_fetch_; }

  class ExportSink {
   public:
    virtual ~ExportSink() = default;
    virtual void AllocExport(ucode::AllocType /*type*/, uint32_t /*size*/) {}
    virtual void Export(ucode::ExportRegister /*export_register*/, const float* /*value*/,
                        uint32_t /*value_mask*/) {}
  };

  ExportSink* GetExportSink() const { return export_sink_; }
  void SetExportSink(ExportSink* new_export_sink) { export_sink_ = new_export_sink; }

  const float* temp_registers() const { return &temp_registers_[0][0]; }
  float* temp_registers() { return &temp_registers_[0][0]; }

  static bool CanInterpretShader(const Shader& shader) {
    assert_true(shader.is_ucode_analyzed());
    // Texture instructions are not very common in vertex shaders (and not used
    // in Direct3D 9's internal rectangles such as clears) and are extremely
    // complex, not implemented.
    (void)shader;
    return true;
  }
  void SetShader(xenos::ShaderType shader_type, const uint32_t* ucode) {
    shader_type_ = shader_type;
    ucode_ = ucode;
  }
  void SetShader(const Shader& shader) {
    assert_true(CanInterpretShader(shader));
    SetShader(shader.type(), shader.ucode_dwords());
  }

  void Execute();

 private:
  struct State {
    ucode::VertexFetchInstruction vfetch_full_last;
    uint32_t vfetch_address_dwords;
    float previous_scalar;
    uint32_t call_stack_depth;
    uint32_t call_return_addresses[4];
    uint32_t loop_stack_depth;
    xenos::LoopConstant loop_constants[4];
    uint32_t loop_iterators[4];
    int32_t loop_al[4];
    int32_t address_register;
    bool predicate;

    void Reset() { std::memset(this, 0, sizeof(*this)); }

    int32_t GetLoopAddress() const { return loop_stack_depth ? loop_al[loop_stack_depth - 1] : 0; }
  };

  static float FlushDenormal(float value) {
    uint32_t bits = *reinterpret_cast<const uint32_t*>(&value);
    bits &= (bits & UINT32_C(0x7F800000)) ? ~UINT32_C(0) : (UINT32_C(1) << 31);
    return *reinterpret_cast<const float*>(&bits);
  }

  uint32_t GetTempRegisterIndex(uint32_t address, bool is_relative) const {
    return (int32_t(address) + (is_relative ? state_.GetLoopAddress() : 0)) &
           ((UINT32_C(1) << xenos::kMaxShaderTempRegistersLog2) - 1);
  }
  const float* GetTempRegister(uint32_t address, bool is_relative) const {
    return temp_registers_[GetTempRegisterIndex(address, is_relative)];
  }
  float* GetTempRegister(uint32_t address, bool is_relative) {
    return temp_registers_[GetTempRegisterIndex(address, is_relative)];
  }
  const std::array<float, 4> GetFloatConstant(uint32_t address, bool is_relative,
                                              bool relative_address_is_a0) const;

  void ExecuteAluInstruction(ucode::AluInstruction instr);
  void StoreFetchResult(uint32_t dest, bool is_dest_relative, uint32_t swizzle, const float* value);
  void ExecuteVertexFetchInstruction(ucode::VertexFetchInstruction instr);

  const RegisterFile& register_file_;
  const uint8_t* membase_;
  uint32_t memsize_;
  bool killed_ = false;
  bool unsupported_fetch_ = false;

  ExportSink* export_sink_ = nullptr;

  xenos::ShaderType shader_type_ = xenos::ShaderType::kVertex;
  const uint32_t* ucode_ = nullptr;

  // For both inputs and locals.
  float temp_registers_[xenos::kMaxShaderTempRegisters][4];

  State state_;
};

}  // namespace rex::graphics::refcheck
