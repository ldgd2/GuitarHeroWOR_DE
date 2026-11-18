// Traductor de shaders Xenos (Xbox 360) a HLSL para el render nativo de re-wor (docs/RENDER_NATIVO.md, fase 1).
//
// Usa el analizador de microcodigo del SDK (ShaderTranslator recorre el control de flujo y entrega cada
// instruccion ya decodificada) y escribe HLSL legible con una interfaz fija, independiente del emulador:
//
//   b0  XeFloatConsts   float4 xe_c[256]            constantes float del tipo de shader (VS o PS)
//   b1  XeBoolLoop      uint4 xe_b[2]; uint4 xe_l[8] 256 constantes bool (bits) y 32 constantes de bucle
//   b2  XeFetch         uint4 xe_fc[48]             las 192 palabras de constantes de fetch (vertices / texturas)
//   b3  XeSystem        ver xe_common.hlsli         NDC, test alfa, PsParamGen, escala de resolucion
//   t0  ByteAddressBuffer xe_mem                    memoria fisica del juego (los vfetch leen de aqui, como la GPU)
//   t1+n   Texture2D   xe_t2d_n    (fetch constant n; 1D y 2D)
//   t33+n  Texture3D   xe_t3d_n
//   t65+n  TextureCube xe_tcube_n
//   s0+n   SamplerState xe_s_n
//
// Vertice: entra SV_VertexID (r0.x = indice, como en la 360); sale SV_Position y 16 interpoladores TEXCOORD0..15.
// Pixel: entran los 16 interpoladores, SV_Position y SV_IsFrontFace; salen SV_Target0..3 y SV_Depth si se escribe.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <rex/graphics/pipeline/shader/translator.h>

namespace re_wor::xenos2hlsl {

class HlslTranslator : public rex::graphics::ShaderTranslator {
 public:
  // Lo que el render necesita saber del shader traducido (va al manifiesto).
  struct Report {
    bool uses_switch = false;     // control de flujo con saltos o bucles (maquina de estados)
    bool uses_loops = false;
    bool uses_calls = false;      // call / return (no soportado aun: se marca como error)
    bool uses_memexport = false;  // escrituras a memoria desde el shader (no soportado aun)
    bool uses_kill = false;
    bool uses_param_gen_slot = false;
    std::vector<std::string> notes;   // avisos no fatales
  };
  const Report& report() const { return report_; }

  // Cabecera comun (cbuffers, recursos y funciones de ayuda) que incluyen todos los shaders.
  static std::string CommonHeader();

 protected:
  void Reset() override;
  void StartTranslation() override;
  std::vector<uint8_t> CompleteTranslation() override;

  void ProcessLabel(uint32_t cf_index) override;
  void ProcessExecInstructionBegin(const rex::graphics::ParsedExecInstruction& instr) override;
  void ProcessExecInstructionEnd(const rex::graphics::ParsedExecInstruction& instr) override;
  void ProcessLoopStartInstruction(const rex::graphics::ParsedLoopStartInstruction& instr) override;
  void ProcessLoopEndInstruction(const rex::graphics::ParsedLoopEndInstruction& instr) override;
  void ProcessCallInstruction(const rex::graphics::ParsedCallInstruction& instr) override;
  void ProcessReturnInstruction(const rex::graphics::ParsedReturnInstruction& instr) override;
  void ProcessJumpInstruction(const rex::graphics::ParsedJumpInstruction& instr) override;
  void ProcessAllocInstruction(const rex::graphics::ParsedAllocInstruction& instr, uint8_t export_eM) override;
  void ProcessVertexFetchInstruction(const rex::graphics::ParsedVertexFetchInstruction& instr) override;
  void ProcessTextureFetchInstruction(const rex::graphics::ParsedTextureFetchInstruction& instr) override;
  void ProcessAluInstruction(const rex::graphics::ParsedAluInstruction& instr,
                             uint8_t memexport_eM_potentially_written_before) override;

 private:
  void Line(const std::string& s);
  void Open(const std::string& s);   // "s {" y sube la sangria
  void Close();                      // "}" y baja la sangria
  std::string Reg(uint32_t i) const;   // registro temporal i (variable suelta o elemento de r[])
  static const char* RelIndex(rex::graphics::InstructionStorageAddressingMode mode);   // "xe_a0" o "xe_al.x"
  std::string Operand(const rex::graphics::InstructionOperand& op) const;   // float4 ya con swizzle, abs y signo
  // Guarda value (expresion float4) segun el resultado: destino, mascara, swizzle, 0/1 y saturacion.
  void Store(const rex::graphics::InstructionResult& result, const std::string& value);
  // Predicacion de la instruccion
  int OpenPredication(bool predicated, bool condition);
  void ClosePredication(int n);
  void Jump(uint32_t cf_target);

  std::string body_;
  int indent_ = 0;
  bool use_switch_ = false;
  // exec en curso: condicion por constante bool (bloque abierto) y predicacion a nivel de exec
  bool exec_cond_open_ = false;   // if abierto del exec (condicion por constante bool o predicado)
  Report report_;
};

}  // namespace re_wor::xenos2hlsl
