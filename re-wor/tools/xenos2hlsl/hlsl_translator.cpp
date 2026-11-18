// Traductor Xenos -> HLSL (ver hlsl_translator.h). La semantica de cada instruccion es la del interprete del SDK
// (src/graphics/pipeline/shader/interpreter.cpp) y la de los fetch la del traductor DXBC
// (dxbc_translator_fetch.cpp): si una cuenta no cuadra con el juego, se compara contra esos dos.
#include "hlsl_translator.h"

#include <cmath>
#include <cstring>

#include <fmt/format.h>

#include <rex/graphics/format/ucode.h>
#include <rex/graphics/xenos.h>

namespace re_wor::xenos2hlsl {

using namespace rex::graphics;
using ucode::AluScalarOpcode;
using ucode::AluVectorOpcode;
using ucode::FetchOpcode;

namespace {
constexpr uint32_t kPcEnd = 0xFFFF;
const char* const kComp = "xyzw";

std::string F(float v) {
  if (std::isinf(v)) return v > 0 ? "3.402823466e+38" : "-3.402823466e+38";
  std::string s = fmt::format("{:.9g}", v);
  if (s.find_first_of(".eE") == std::string::npos) s += ".0";
  return s;
}
}  // namespace

// ------------------------------------------------------------------------------------------- cabecera comun
std::string HlslTranslator::CommonHeader() {
  return R"(// xe_common.hlsli - interfaz comun de los shaders traducidos de la 360 (generado por xenos2hlsl).
#ifndef XE_COMMON_HLSLI
#define XE_COMMON_HLSLI

cbuffer XeFloatConsts : register(b0) { float4 xe_c[256]; };
cbuffer XeBoolLoop : register(b1) { uint4 xe_b[2]; uint4 xe_l[8]; };
cbuffer XeFetch : register(b2) { uint4 xe_fc[48]; };
cbuffer XeSystem : register(b3) {
  float4 xe_ndc_scale;    // posicion: xyz * scale + offset * w
  float4 xe_ndc_offset;
  uint xe_flags;          // XE_FLAG_*
  uint xe_alpha_func;     // test alfa (CompareFunction de la 360: 0 nunca ... 7 siempre)
  float xe_alpha_ref;
  uint xe_param_gen;      // PsParamGen: registro destino (0xFFFF = no) | 0x100 si es un punto
  float2 xe_res_scale;    // escala de resolucion del render (para PsParamGen)
  float2 xe_pad0;
};
#define XE_FLAG_W0_IS_RCP 1u        // vtx_w0_fmt: la W exportada es 1/W
#define XE_FLAG_XY_DIVIDED 2u       // vtx_xy_fmt: XY ya divididas por W
#define XE_FLAG_Z_DIVIDED 4u        // vtx_z_fmt: Z ya dividida por W
#define XE_FLAG_POLYGONAL 8u        // la primitiva tiene cara (para PsParamGen)

ByteAddressBuffer xe_mem : register(t0);

struct XeVaryings {
  float4 pos : SV_Position;
  float4 o[16] : TEXCOORD0;
};

bool xe_bool(uint i) { return ((xe_b[i >> 7u][(i >> 5u) & 3u] >> (i & 31u)) & 1u) != 0u; }
uint xe_loop(uint i) { return xe_l[i >> 2u][i & 3u]; }
float4 xe_cf(int i) { return uint(i) < 256u ? xe_c[i] : float4(0.0, 0.0, 0.0, 0.0); }   // fuera de rango: 0
uint2 xe_vfc(uint i) { uint4 v = xe_fc[i >> 1u]; return (i & 1u) ? v.zw : v.xy; }

// Direct3D 9: 0 (o desnormal) por cualquier cosa = +0
float xe_mul(float a, float b) { return (a == 0.0 || b == 0.0) ? 0.0 : a * b; }
float4 xe_mul(float4 a, float4 b) { return float4(xe_mul(a.x, b.x), xe_mul(a.y, b.y), xe_mul(a.z, b.z), xe_mul(a.w, b.w)); }
float4 xe_mad(float4 a, float4 b, float4 c) { return xe_mul(a, b) + c; }
float xe_dp4(float4 a, float4 b) { float4 m = xe_mul(a, b); return m.x + m.y + m.z + m.w; }
float xe_dp3(float4 a, float4 b) { float4 m = xe_mul(a, b); return m.x + m.y + m.z; }
float xe_dp2add(float4 a, float4 b, float4 c) { float4 m = xe_mul(a, b); return m.x + m.y + c.x; }
// Shader Model 3: a >= b ? a : b (no fmax), con NaN igual que la 360
float xe_max(float a, float b) { return a >= b ? a : b; }
float xe_min(float a, float b) { return a < b ? a : b; }
float4 xe_max(float4 a, float4 b) { return float4(xe_max(a.x, b.x), xe_max(a.y, b.y), xe_max(a.z, b.z), xe_max(a.w, b.w)); }
float4 xe_min(float4 a, float4 b) { return float4(xe_min(a.x, b.x), xe_min(a.y, b.y), xe_min(a.z, b.z), xe_min(a.w, b.w)); }
float4 xe_sel(bool4 m, float4 t, float4 f) { return float4(m.x ? t.x : f.x, m.y ? t.y : f.y, m.z ? t.z : f.z, m.w ? t.w : f.w); }

// Comparaciones con NaN / infinitos como en la 360 aunque el driver suponga que no los hay. NVIDIA en Vulkan
// convierte abs(x) >= 0 en "siempre cierto" pese a SignedZeroInfNanPreserve, asi que NaN e infinito se miran con
// enteros, que ningun compilador puede dar por supuestos. Ordenadas (==, >, >=): falsas con NaN; != cierta.
bool xe_nan(float x) { return (asuint(x) & 0x7FFFFFFFu) > 0x7F800000u; }
bool xe_inf(float x) { return (asuint(x) & 0x7FFFFFFFu) == 0x7F800000u; }
bool xe_eq(float a, float b) { return !xe_nan(a) && !xe_nan(b) && a == b; }
bool xe_gt(float a, float b) { return !xe_nan(a) && !xe_nan(b) && a > b; }
bool xe_ge(float a, float b) { return !xe_nan(a) && !xe_nan(b) && a >= b; }
bool xe_ne(float a, float b) { return !xe_eq(a, b); }
bool4 xe_eq(float4 a, float4 b) { return bool4(xe_eq(a.x, b.x), xe_eq(a.y, b.y), xe_eq(a.z, b.z), xe_eq(a.w, b.w)); }
bool4 xe_gt(float4 a, float4 b) { return bool4(xe_gt(a.x, b.x), xe_gt(a.y, b.y), xe_gt(a.z, b.z), xe_gt(a.w, b.w)); }
bool4 xe_ge(float4 a, float4 b) { return bool4(xe_ge(a.x, b.x), xe_ge(a.y, b.y), xe_ge(a.z, b.z), xe_ge(a.w, b.w)); }
bool4 xe_ne(float4 a, float4 b) { return bool4(xe_ne(a.x, b.x), xe_ne(a.y, b.y), xe_ne(a.z, b.z), xe_ne(a.w, b.w)); }
float4 xe_cube(float4 a) {   // operando .z_xy; resultado: T, S, 2 * eje mayor, cara
  float x = a.z, y = a.w, z = a.x;
  float ax = abs(x), ay = abs(y), az = abs(z);
  float4 r;
  if (az >= ax && az >= ay) { bool n = z < 0.0; r = float4(-y, n ? -x : x, z, n ? 5.0 : 4.0); }
  else if (ay >= ax) { bool n = y < 0.0; r = float4(n ? -z : z, x, y, n ? 3.0 : 2.0); }
  else { bool n = x < 0.0; r = float4(-y, n ? z : -z, x, n ? 1.0 : 0.0); }
  r.z *= 2.0;
  return r;
}
float xe_max4(float4 a) { return xe_max(xe_max(a.x, a.y), xe_max(a.z, a.w)); }
int xe_addr(float v, bool round_half) { return int(floor(clamp(v, -256.0, 255.0) + (round_half ? 0.5 : 0.0))); }
float xe_clampmax(float v) { return xe_inf(v) ? (v > 0.0 ? 3.402823466e+38 : -3.402823466e+38) : v; }
float xe_clampzero(float v) { return xe_inf(v) ? (v > 0.0 ? 0.0 : -0.0) : v; }

// vfetch: palabras de la memoria del juego con el orden de bytes de la constante de fetch
uint xe_swap(uint v, uint e) {
  if (e == 1u || e == 2u) v = ((v & 0x00FF00FFu) << 8u) | ((v >> 8u) & 0x00FF00FFu);
  if (e == 2u || e == 3u) v = (v << 16u) | (v >> 16u);
  return v;
}
uint4 xe_vload(uint addr, uint e) {
  uint4 w = xe_mem.Load4(addr);
  return uint4(xe_swap(w.x, e), xe_swap(w.y, e), xe_swap(w.z, e), xe_swap(w.w, e));
}
float xe_ubits(uint v, uint off, uint width) { return float((v >> off) & ((1u << width) - 1u)); }
float xe_sbits(uint v, uint off, uint width) { return float(int(v << (32u - off - width)) >> int(32u - width)); }

// cubemaps: de SC/TC + 1 y cara (lo que deja la instruccion cube) a una direccion
float3 xe_cube_dir(float4 tc) {
  float2 sc = tc.xy * 2.0 - 3.0;
  uint face = min(uint(max(tc.z, 0.0)), 5u);
  uint axis = face >> 1u;
  bool n = (face & 1u) != 0u;
  if (axis == 0u) return float3(n ? -1.0 : 1.0, -sc.y, n ? sc.x : -sc.x);
  if (axis == 1u) return float3(sc.x, n ? -1.0 : 1.0, n ? -sc.y : sc.y);
  return float3(n ? -sc.x : sc.x, -sc.y, n ? -1.0 : 1.0);
}

float4 xe_finish_position(float4 p) {
  if (xe_flags & XE_FLAG_W0_IS_RCP) p.w = rcp(p.w);
  if (xe_flags & XE_FLAG_XY_DIVIDED) p.xy *= p.w;
  if (xe_flags & XE_FLAG_Z_DIVIDED) p.z *= p.w;
  p.xyz = p.xyz * xe_ndc_scale.xyz + xe_mul(xe_ndc_offset, p.wwww).xyz;   // 0 * w infinita = 0, no NaN
  return p;
}

void xe_alpha_test(float a) {
  uint f = xe_alpha_func;
  if (f == 7u) return;
  bool ok = (f == 1u && a < xe_alpha_ref) || (f == 2u && a == xe_alpha_ref) || (f == 3u && a <= xe_alpha_ref) ||
              (f == 4u && a > xe_alpha_ref) || (f == 5u && a != xe_alpha_ref) || (f == 6u && a >= xe_alpha_ref);
  if (!ok) discard;
}

#endif
)";
}

// ------------------------------------------------------------------------------------------- utilidades
void HlslTranslator::Line(const std::string& s) {
  body_.append(size_t(indent_ * 2 + 2), ' ');
  body_ += s;
  body_ += '\n';
}
void HlslTranslator::Open(const std::string& s) {
  Line(s.empty() ? "{" : s + " {");
  ++indent_;
}
void HlslTranslator::Close() {
  --indent_;
  Line("}");
}

void HlslTranslator::Reset() {
  ShaderTranslator::Reset();
  body_.clear();
  indent_ = 0;
  use_switch_ = false;
  exec_cond_open_ = false;
  report_ = Report{};
}

void HlslTranslator::StartTranslation() {
  // Saltos o bucles: maquina de estados sobre la direccion de control de flujo (una cadena de if con continue y no
  // un switch: fxc da "internal error: l-value expected" al asignar xe_pc dentro de un if en un case); si no hay
  // etiquetas, codigo lineal.
  use_switch_ = !current_shader().label_addresses().empty();
  report_.uses_switch = use_switch_;
  indent_ = 1;
  if (use_switch_) {
    Line("uint xe_pc = 0u;");
    Open("[loop] while (xe_pc != 0xFFFFu)");
    Open("if (xe_pc == 0u)");
  } else {
    Open("do");   // un solo paso; [loop] aqui tumba a fxc
  }
}

std::string HlslTranslator::Reg(uint32_t i) const {
  return current_shader().uses_register_dynamic_addressing() ? fmt::format("r[{}]", i) : fmt::format("r{}", i);
}

const char* HlslTranslator::RelIndex(InstructionStorageAddressingMode mode) {
  return mode == InstructionStorageAddressingMode::kAddressRegisterRelative ? "xe_a0" : "xe_al.x";
}

std::string HlslTranslator::Operand(const InstructionOperand& op) const {
  std::string base;
  const uint32_t n = std::max(register_count(), 1u);
  switch (op.storage_source) {
    case InstructionStorageSource::kRegister:
      base = op.storage_addressing_mode == InstructionStorageAddressingMode::kAbsolute
                 ? Reg(op.storage_index)
                 : fmt::format("r[min(uint({} + {}), {}u)]", op.storage_index, RelIndex(op.storage_addressing_mode), n - 1);
      break;
    case InstructionStorageSource::kConstantFloat:
      switch (op.storage_addressing_mode) {
        case InstructionStorageAddressingMode::kAbsolute: base = fmt::format("xe_c[{}]", op.storage_index); break;
        case InstructionStorageAddressingMode::kAddressRegisterRelative:
          base = fmt::format("xe_cf({} + xe_a0)", op.storage_index);
          break;
        case InstructionStorageAddressingMode::kLoopRelative:
          base = fmt::format("xe_cf({} + xe_al.x)", op.storage_index);
          break;
      }
      break;
    default:
      base = "float4(0.0, 0.0, 0.0, 0.0)";
      break;
  }
  std::string swz = ".";
  for (uint32_t i = 0; i < 4; ++i) swz += GetCharForSwizzle(op.GetComponent(i));
  std::string e = base + (swz == ".xyzw" ? "" : swz);
  if (op.is_absolute_value) e = "abs(" + e + ")";
  if (op.is_negated) e = "-" + e;
  return e;
}

void HlslTranslator::Store(const InstructionResult& result, const std::string& value) {
  const uint32_t mask = result.GetUsedWriteMask();
  if (!mask) return;
  std::string target;
  switch (result.storage_target) {
    case InstructionStorageTarget::kRegister:
      target = result.storage_addressing_mode == InstructionStorageAddressingMode::kAbsolute
                   ? Reg(result.storage_index)
                   : fmt::format("r[min(uint({} + {}), {}u)]", result.storage_index,
                                 RelIndex(result.storage_addressing_mode), std::max(register_count(), 1u) - 1);
      break;
    // variables sueltas y no arrays: fxc pierde la pista de la inicializacion de arrays escritos por partes
    case InstructionStorageTarget::kInterpolator: target = fmt::format("xe_o{}", result.storage_index & 15); break;
    case InstructionStorageTarget::kPosition: target = "xe_pos"; break;
    case InstructionStorageTarget::kPointSizeEdgeFlagKillVertex: target = "xe_psize"; break;
    case InstructionStorageTarget::kColor: target = fmt::format("xe_oc{}", result.storage_index & 3); break;
    case InstructionStorageTarget::kDepth: target = "xe_depth"; break;
    case InstructionStorageTarget::kExportAddress:
    case InstructionStorageTarget::kExportData:
      report_.uses_memexport = true;
      return;
    default: return;
  }
  std::string v = result.is_clamped ? "saturate(" + value + ")" : value;
  std::string dst, src;
  for (uint32_t i = 0; i < 4; ++i) {
    if (!(mask & (1u << i))) continue;
    const SwizzleSource c = result.components[i];
    if (c == SwizzleSource::k0 || c == SwizzleSource::k1) {
      Line(fmt::format("{}.{} = {};", target, kComp[i], c == SwizzleSource::k1 ? "1.0" : "0.0"));
    } else {
      dst += kComp[i];
      src += GetCharForSwizzle(c);
    }
  }
  if (!dst.empty()) Line(fmt::format("{}.{} = ({}).{};", target, dst, v, src));
}

int HlslTranslator::OpenPredication(bool predicated, bool condition) {
  int n = 0;
  if (predicated) {
    Open(fmt::format("if (xe_p0 == {})", condition ? "true" : "false"));
    ++n;
  }
  return n;
}
void HlslTranslator::ClosePredication(int n) {
  while (n-- > 0) Close();
}

void HlslTranslator::Jump(uint32_t cf_target) {
  if (use_switch_) {
    Line(fmt::format("xe_pc = {}u;", cf_target));
    Line("continue;");
  } else {
    Line("break;");
  }
}

// ------------------------------------------------------------------------------------------- control de flujo
void HlslTranslator::ProcessLabel(uint32_t cf_index) {
  if (cf_index == 0 || !use_switch_) return;
  if (exec_cond_open_) {
    Close();
    exec_cond_open_ = false;
  }
  Jump(cf_index);   // caer a la etiqueta siguiente
  Close();          // cierra el bloque anterior
  Open(fmt::format("if (xe_pc == {}u)", cf_index));
}

void HlslTranslator::ProcessExecInstructionBegin(const ParsedExecInstruction& instr) {
  switch (instr.type) {
    case ParsedExecInstruction::Type::kConditional:
      Open(fmt::format("if (xe_bool({}u) == {})", instr.bool_constant_index, instr.condition ? "true" : "false"));
      exec_cond_open_ = true;
      break;
    case ParsedExecInstruction::Type::kPredicated:
      // una sola vez al entrar: un setp dentro del exec no cambia si el exec se ejecuta
      Open(fmt::format("if (xe_p0 == {})", instr.condition ? "true" : "false"));
      exec_cond_open_ = true;
      break;
    default:
      break;
  }
}

void HlslTranslator::ProcessExecInstructionEnd(const ParsedExecInstruction& instr) {
  if (instr.is_end) Jump(kPcEnd);
  if (exec_cond_open_) {
    Close();
    exec_cond_open_ = false;
  }
}

void HlslTranslator::ProcessLoopStartInstruction(const ParsedLoopStartInstruction& instr) {
  report_.uses_loops = true;
  Open("");
  Line(fmt::format("uint xe_n = xe_loop({}u) & 0xFFu;", instr.loop_constant_index));
  Open("if (xe_n == 0u)");
  Jump(instr.loop_skip_address);
  Close();
  Line("xe_lc = uint4(xe_n, xe_lc.xyz);");
  Line(instr.is_repeat ? "xe_al = int4(xe_al.x, xe_al.xyz);"
                       : fmt::format("xe_al = int4(int((xe_loop({}u) >> 8u) & 0xFFu), xe_al.xyz);",
                                     instr.loop_constant_index));
  Close();
}

void HlslTranslator::ProcessLoopEndInstruction(const ParsedLoopEndInstruction& instr) {
  Line("xe_lc.x -= 1u;");
  std::string brk = "xe_lc.x == 0u";
  if (instr.is_predicated_break) brk += fmt::format(" || xe_p0 == {}", instr.predicate_condition ? "true" : "false");
  Open(fmt::format("if ({})", brk));
  Line("xe_lc = uint4(xe_lc.yzw, 0u);");
  Line("xe_al = int4(xe_al.yzw, 0);");
  Close();
  Open("else");
  Line(fmt::format("xe_al.x += int(xe_loop({}u) << 8u) >> 24;", instr.loop_constant_index));
  Jump(instr.loop_body_address);
  Close();
}

void HlslTranslator::ProcessCallInstruction(const ParsedCallInstruction&) {
  report_.uses_calls = true;
  EmitTranslationError("call de control de flujo: no soportado aun", true);
}
void HlslTranslator::ProcessReturnInstruction(const ParsedReturnInstruction&) {
  report_.uses_calls = true;
  EmitTranslationError("return de control de flujo: no soportado aun", true);
}

void HlslTranslator::ProcessJumpInstruction(const ParsedJumpInstruction& instr) {
  switch (instr.type) {
    case ParsedJumpInstruction::Type::kConditional:
      Open(fmt::format("if (xe_bool({}u) == {})", instr.bool_constant_index, instr.condition ? "true" : "false"));
      Jump(instr.target_address);
      Close();
      break;
    case ParsedJumpInstruction::Type::kPredicated:
      Open(fmt::format("if (xe_p0 == {})", instr.condition ? "true" : "false"));
      Jump(instr.target_address);
      Close();
      break;
    default:
      Jump(instr.target_address);
      break;
  }
}

void HlslTranslator::ProcessAllocInstruction(const ParsedAllocInstruction& instr, uint8_t export_eM) {
  if (instr.type == ucode::AllocType::kMemory || export_eM) report_.uses_memexport = true;
}

// ------------------------------------------------------------------------------------------- fetch de vertices
void HlslTranslator::ProcessVertexFetchInstruction(const ParsedVertexFetchInstruction& instr) {
  const int pred = OpenPredication(instr.is_predicated, instr.predicate_condition);
  Open("");
  const auto& at = instr.attributes;
  if (!instr.is_mini_fetch) {
    // vfetch_full: direccion base + indice * zancada (se guarda para los vfetch_mini que la sigan)
    Line(fmt::format("uint2 xe_k = xe_vfc({}u);", instr.operands[1].storage_index));
    if (at.stride) {
      Line(fmt::format("float xe_i = ({}).x;", Operand(instr.operands[0])));
      Line(fmt::format("xe_vaddr = (xe_k.x & 0xFFFFFFFCu) + uint(int(floor(xe_i{}))) * {}u;",
                       at.is_index_rounded ? " + 0.5" : "", at.stride * 4));
    } else {
      Line("xe_vaddr = xe_k.x & 0xFFFFFFFCu;");
    }
    Line("xe_vend = xe_k.y & 3u;");
  }
  const uint32_t used = instr.result.GetUsedResultComponents();
  const uint32_t fmt_comps = xenos::GetVertexFormatComponentCount(at.data_format);
  if (!used || !fmt_comps) {
    Store(instr.result, "float4(0.0, 0.0, 0.0, 0.0)");
    Close();
    ClosePredication(pred);
    return;
  }
  Line(fmt::format("uint4 xe_w = xe_vload(uint(int(xe_vaddr) + {}), xe_vend);", at.offset * 4));
  // desempaquetado (anchos y desplazamientos del traductor DXBC)
  uint32_t widths[4] = {}, offs[4] = {}, word[4] = {};
  bool packed = true;
  switch (at.data_format) {
    case xenos::VertexFormat::k_8_8_8_8: widths[0] = widths[1] = widths[2] = widths[3] = 8; offs[1] = 8; offs[2] = 16; offs[3] = 24; break;
    case xenos::VertexFormat::k_2_10_10_10: widths[0] = widths[1] = widths[2] = 10; widths[3] = 2; offs[1] = 10; offs[2] = 20; offs[3] = 30; break;
    case xenos::VertexFormat::k_10_11_11: widths[0] = widths[1] = 11; widths[2] = 10; offs[1] = 11; offs[2] = 22; break;
    case xenos::VertexFormat::k_11_11_10: widths[0] = 10; widths[1] = widths[2] = 11; offs[1] = 10; offs[2] = 21; break;
    case xenos::VertexFormat::k_16_16: widths[0] = widths[1] = 16; offs[1] = 16; break;
    case xenos::VertexFormat::k_16_16_16_16: widths[0] = widths[1] = widths[2] = widths[3] = 16; offs[1] = offs[3] = 16; word[2] = word[3] = 1; break;
    default: packed = false; break;
  }
  std::string c[4] = {"0.0", "0.0", "0.0", "0.0"};
  if (packed) {
    for (uint32_t i = 0; i < fmt_comps; ++i) {
      const std::string w = fmt::format("xe_w.{}", kComp[word[i]]);
      std::string v = fmt::format("{}({}, {}u, {}u)", at.is_signed ? "xe_sbits" : "xe_ubits", w, offs[i], widths[i]);
      if (!at.is_integer) {
        if (at.is_signed) {
          if (at.signed_rf_mode == xenos::SignedRepeatingFractionMode::kNoZero) {
            const float zero = 1.0f / float((1u << widths[i]) - 1);
            v = fmt::format("({} * {} + {})", v, F(2.0f * zero), F(zero));
          } else if (widths[i] > 2) {
            v = fmt::format("max({} * {}, -1.0)", v, F(1.0f / float((1u << (widths[i] - 1)) - 1)));
          } else {
            v = fmt::format("max({}, -1.0)", v);
          }
        } else if (widths[i] > 1) {
          v = fmt::format("({} * {})", v, F(1.0f / float((1u << widths[i]) - 1)));
        }
      }
      c[i] = v;
    }
  } else {
    switch (at.data_format) {
      case xenos::VertexFormat::k_16_16_FLOAT:
      case xenos::VertexFormat::k_16_16_16_16_FLOAT:
        for (uint32_t i = 0; i < fmt_comps; ++i)
          c[i] = fmt::format("f16tof32(xe_w.{} >> {}u)", kComp[i >> 1], (i & 1) * 16);
        break;
      case xenos::VertexFormat::k_32:
      case xenos::VertexFormat::k_32_32:
      case xenos::VertexFormat::k_32_32_32_32:
        for (uint32_t i = 0; i < fmt_comps; ++i) {
          std::string v = at.is_signed ? fmt::format("float(int(xe_w.{}))", kComp[i]) : fmt::format("float(xe_w.{})", kComp[i]);
          if (!at.is_integer) {
            if (!at.is_signed) v = fmt::format("({} * {})", v, F(1.0f / 4294967295.0f));
            else if (at.signed_rf_mode == xenos::SignedRepeatingFractionMode::kNoZero)
              v = fmt::format("({} * {} + {})", v, F(1.0f / 2147483647.5f), F(0.5f / 2147483647.5f));
            else v = fmt::format("({} * {})", v, F(1.0f / 2147483647.0f));
          }
          c[i] = v;
        }
        break;
      case xenos::VertexFormat::k_32_FLOAT:
      case xenos::VertexFormat::k_32_32_FLOAT:
      case xenos::VertexFormat::k_32_32_32_FLOAT:
      case xenos::VertexFormat::k_32_32_32_32_FLOAT:
        for (uint32_t i = 0; i < fmt_comps; ++i) c[i] = fmt::format("asfloat(xe_w.{})", kComp[i]);
        break;
      default:
        report_.notes.push_back(fmt::format("formato de vertice desconocido {}", uint32_t(at.data_format)));
        break;
    }
  }
  std::string val = fmt::format("float4({}, {}, {}, {})", c[0], c[1], c[2], c[3]);
  if (at.exp_adjust) val = fmt::format("({} * {})", val, F(std::ldexp(1.0f, at.exp_adjust)));
  Line("float4 xe_f = " + val + ";");
  Store(instr.result, "xe_f");
  Close();
  ClosePredication(pred);
}

// ------------------------------------------------------------------------------------------- fetch de texturas
void HlslTranslator::ProcessTextureFetchInstruction(const ParsedTextureFetchInstruction& instr) {
  const bool ps = is_pixel_shader();
  const int pred = OpenPredication(instr.is_predicated, instr.predicate_condition);
  Open("");
  const auto& at = instr.attributes;
  const uint32_t fc = instr.operands[1].storage_index;
  const std::string tc = Operand(instr.operands[0]);
  switch (instr.opcode) {
    case FetchOpcode::kSetTextureLod: Line(fmt::format("xe_lod = ({}).x;", tc)); break;
    case FetchOpcode::kSetTextureGradientsHorz: Line(fmt::format("xe_gh = ({}).xyz;", tc)); break;
    case FetchOpcode::kSetTextureGradientsVert: Line(fmt::format("xe_gv = ({}).xyz;", tc)); break;
    case FetchOpcode::kGetTextureGradients:
      if (ps) {
        Line(fmt::format("float4 xe_g = {};", tc));
        Store(instr.result, "float4(ddx_coarse(xe_g.x), ddy_coarse(xe_g.x), ddx_coarse(xe_g.y), ddy_coarse(xe_g.y))");
      } else {
        Store(instr.result, "float4(0.0, 0.0, 0.0, 0.0)");
      }
      break;
    case FetchOpcode::kGetTextureBorderColorFrac: Store(instr.result, "float4(0.0, 0.0, 0.0, 0.0)"); break;
    case FetchOpcode::kTextureFetch:
    case FetchOpcode::kGetTextureComputedLod:
    case FetchOpcode::kGetTextureWeights: {
      std::string tex, crd, size_decl;
      Line(fmt::format("float4 xe_t = {};", tc));
      switch (instr.dimension) {
        case xenos::FetchOpDimension::k1D:
          tex = fmt::format("xe_t2d_{}", fc);
          Line(fmt::format("float2 xe_sz; {}.GetDimensions(xe_sz.x, xe_sz.y);", tex));
          crd = "float2(xe_t.x, 0.5)";
          if (at.unnormalized_coordinates) crd = "float2(xe_t.x / xe_sz.x, 0.5)";
          if (at.offset_x) crd = fmt::format("({} + float2({} / xe_sz.x, 0.0))", crd, F(at.offset_x));
          break;
        case xenos::FetchOpDimension::k2D:
          tex = fmt::format("xe_t2d_{}", fc);
          Line(fmt::format("float2 xe_sz; {}.GetDimensions(xe_sz.x, xe_sz.y);", tex));
          crd = at.unnormalized_coordinates ? "(xe_t.xy / xe_sz)" : "xe_t.xy";
          if (at.offset_x || at.offset_y)
            crd = fmt::format("({} + float2({}, {}) / xe_sz)", crd, F(at.offset_x), F(at.offset_y));
          break;
        case xenos::FetchOpDimension::k3DOrStacked:
          tex = fmt::format("xe_t3d_{}", fc);
          Line(fmt::format("float3 xe_sz; {}.GetDimensions(xe_sz.x, xe_sz.y, xe_sz.z);", tex));
          crd = at.unnormalized_coordinates ? "(xe_t.xyz / xe_sz)" : "xe_t.xyz";
          if (at.offset_x || at.offset_y || at.offset_z)
            crd = fmt::format("({} + float3({}, {}, {}) / xe_sz)", crd, F(at.offset_x), F(at.offset_y), F(at.offset_z));
          break;
        case xenos::FetchOpDimension::kCube:
          tex = fmt::format("xe_tcube_{}", fc);
          Line(fmt::format("float2 xe_sz; {}.GetDimensions(xe_sz.x, xe_sz.y);", tex));
          crd = "xe_cube_dir(xe_t)";
          break;
      }
      const std::string smp = fmt::format("xe_s_{}", fc);
      if (instr.opcode == FetchOpcode::kGetTextureComputedLod) {
        if (ps && instr.dimension != xenos::FetchOpDimension::kCube)
          Store(instr.result, fmt::format("float4({}.CalculateLevelOfDetail({}, {}), 0.0, 0.0, 0.0)", tex, smp, crd));
        else
          Store(instr.result, "float4(0.0, 0.0, 0.0, 0.0)");
        break;
      }
      if (instr.opcode == FetchOpcode::kGetTextureWeights) {
        if (instr.dimension == xenos::FetchOpDimension::k3DOrStacked)
          Store(instr.result, fmt::format("float4(frac({} * xe_sz - 0.5), 0.0)", crd));
        else if (instr.dimension == xenos::FetchOpDimension::kCube)
          Store(instr.result, "float4(0.0, 0.0, 0.0, 0.0)");
        else
          Store(instr.result, fmt::format("float4(frac({} * xe_sz - 0.5), 0.0, 0.0)", crd));
        break;
      }
      const std::string bias = F(at.lod_bias);
      std::string call;
      if (at.use_register_gradients && ps) {
        const char* g = instr.dimension == xenos::FetchOpDimension::k2D || instr.dimension == xenos::FetchOpDimension::k1D
                            ? ".xy" : "";
        call = fmt::format("{}.SampleGrad({}, {}, xe_gh{}, xe_gv{})", tex, smp, crd, g, g);
      } else if (at.use_register_lod) {
        call = fmt::format("{}.SampleLevel({}, {}, xe_lod + {})", tex, smp, crd, bias);
      } else if (!ps || !at.use_computed_lod || use_switch_) {
        // en el vertice no hay derivadas; en la maquina de estados tampoco (el compilador no las permite
        // dentro de un bucle de iteraciones variables): nivel 0 + sesgo
        if (ps && at.use_computed_lod && use_switch_) report_.notes.push_back("tfetch sin LOD automatico (bucle)");
        call = fmt::format("{}.SampleLevel({}, {}, {})", tex, smp, crd, bias);
      } else if (at.lod_bias != 0.0f) {
        call = fmt::format("{}.SampleBias({}, {}, {})", tex, smp, crd, bias);
      } else {
        call = fmt::format("{}.Sample({}, {})", tex, smp, crd);
      }
      Store(instr.result, call);
    } break;
    default:
      report_.notes.push_back(fmt::format("fetch desconocido {}", uint32_t(instr.opcode)));
      break;
  }
  Close();
  ClosePredication(pred);
}

// ------------------------------------------------------------------------------------------- ALU
void HlslTranslator::ProcessAluInstruction(const ParsedAluInstruction& instr, uint8_t) {
  if (instr.IsNop()) return;
  const int pred = OpenPredication(instr.is_predicated, instr.predicate_condition);
  Open("");
  // operandos del vector
  std::string a, b, c;
  if (instr.vector_operand_count > 0) a = Operand(instr.vector_operands[0]);
  if (instr.vector_operand_count > 1) b = Operand(instr.vector_operands[1]);
  if (instr.vector_operand_count > 2) c = Operand(instr.vector_operands[2]);
  const ucode::AluVectorOpcodeInfo& vinfo = ucode::GetAluVectorOpcodeInfo(instr.vector_opcode);
  const bool vec_needed = instr.vector_and_constant_result.GetUsedResultComponents() || vinfo.changed_state;
  std::string vexpr, vpred, vkill, va0;
  if (vec_needed) {
    switch (instr.vector_opcode) {
      case AluVectorOpcode::kAdd: vexpr = fmt::format("{} + {}", a, b); break;
      case AluVectorOpcode::kMul: vexpr = fmt::format("xe_mul({}, {})", a, b); break;
      case AluVectorOpcode::kMax: vexpr = fmt::format("xe_max({}, {})", a, b); break;
      case AluVectorOpcode::kMin: vexpr = fmt::format("xe_min({}, {})", a, b); break;
      case AluVectorOpcode::kSeq: vexpr = fmt::format("float4(xe_eq({}, {}))", a, b); break;
      case AluVectorOpcode::kSgt: vexpr = fmt::format("float4(xe_gt({}, {}))", a, b); break;
      case AluVectorOpcode::kSge: vexpr = fmt::format("float4(xe_ge({}, {}))", a, b); break;
      case AluVectorOpcode::kSne: vexpr = fmt::format("float4(xe_ne({}, {}))", a, b); break;
      case AluVectorOpcode::kFrc: vexpr = fmt::format("frac({})", a); break;
      case AluVectorOpcode::kTrunc: vexpr = fmt::format("trunc({})", a); break;
      case AluVectorOpcode::kFloor: vexpr = fmt::format("floor({})", a); break;
      case AluVectorOpcode::kMad: vexpr = fmt::format("xe_mad({}, {}, {})", a, b, c); break;
      case AluVectorOpcode::kCndEq: vexpr = fmt::format("xe_sel(xe_eq({}, 0.0), {}, {})", a, b, c); break;
      case AluVectorOpcode::kCndGe: vexpr = fmt::format("xe_sel(xe_ge({}, 0.0), {}, {})", a, b, c); break;
      case AluVectorOpcode::kCndGt: vexpr = fmt::format("xe_sel(xe_gt({}, 0.0), {}, {})", a, b, c); break;
      case AluVectorOpcode::kDp4: vexpr = fmt::format("xe_dp4({}, {}).xxxx", a, b); break;
      case AluVectorOpcode::kDp3: vexpr = fmt::format("xe_dp3({}, {}).xxxx", a, b); break;
      case AluVectorOpcode::kDp2Add: vexpr = fmt::format("xe_dp2add({}, {}, {}).xxxx", a, b, c); break;
      case AluVectorOpcode::kCube: vexpr = fmt::format("xe_cube({})", a); break;
      case AluVectorOpcode::kMax4: vexpr = fmt::format("xe_max4({}).xxxx", a); break;
      case AluVectorOpcode::kSetpEqPush:
      case AluVectorOpcode::kSetpNePush:
      case AluVectorOpcode::kSetpGtPush:
      case AluVectorOpcode::kSetpGePush: {
        const char* cmp = instr.vector_opcode == AluVectorOpcode::kSetpEqPush ? "xe_eq"
                          : instr.vector_opcode == AluVectorOpcode::kSetpNePush ? "xe_ne"
                          : instr.vector_opcode == AluVectorOpcode::kSetpGtPush ? "xe_gt" : "xe_ge";
        Line(fmt::format("float4 xe_va = {}, xe_vb = {};", a, b));
        vpred = fmt::format("xe_eq(xe_va.w, 0.0) && {}(xe_vb.w, 0.0)", cmp);
        vexpr = fmt::format("((xe_eq(xe_va.x, 0.0) && {}(xe_vb.x, 0.0)) ? 0.0 : xe_va.x + 1.0).xxxx", cmp);
      } break;
      case AluVectorOpcode::kKillEq:
      case AluVectorOpcode::kKillGt:
      case AluVectorOpcode::kKillGe:
      case AluVectorOpcode::kKillNe: {
        const char* op = instr.vector_opcode == AluVectorOpcode::kKillEq ? "xe_eq"
                         : instr.vector_opcode == AluVectorOpcode::kKillGt ? "xe_gt"
                         : instr.vector_opcode == AluVectorOpcode::kKillGe ? "xe_ge" : "xe_ne";
        vexpr = fmt::format("float(any({}({}, {}))).xxxx", op, a, b);
        vkill = "xe_v.x != 0.0";
        report_.uses_kill = true;
      } break;
      case AluVectorOpcode::kDst:
        Line(fmt::format("float4 xe_va = {}, xe_vb = {};", a, b));
        vexpr = "float4(1.0, xe_mul(xe_va.y, xe_vb.y), xe_va.z, xe_vb.w)";
        break;
      case AluVectorOpcode::kMaxA:
        Line(fmt::format("float4 xe_va = {}, xe_vb = {};", a, b));
        vexpr = "xe_max(xe_va, xe_vb)";
        va0 = "xe_addr(xe_va.w, true)";
        break;
      default:
        report_.notes.push_back(fmt::format("opcode vectorial desconocido {}", uint32_t(instr.vector_opcode)));
        vexpr = "float4(0.0, 0.0, 0.0, 0.0)";
        break;
    }
    Line("float4 xe_v = " + vexpr + ";");
  }

  // escalar: a = operando 0 componente 0, b = operando 0 componente 1 (o el operando 1 en las formas c0 / c1)
  std::string sa, sb, sexpr, spred, skill, sa0;
  const ucode::AluScalarOpcodeInfo& sinfo = ucode::GetAluScalarOpcodeInfo(instr.scalar_opcode);
  if (instr.scalar_operand_count > 0) {
    Line(fmt::format("float4 xe_s0 = {};", Operand(instr.scalar_operands[0])));
    sa = "xe_s0.x";
    sb = "xe_s0.y";
  }
  if (instr.scalar_operand_count > 1) {
    Line(fmt::format("float4 xe_s1 = {};", Operand(instr.scalar_operands[1])));
    sb = "xe_s1.x";
  }
  (void)sinfo;
  switch (instr.scalar_opcode) {
    case AluScalarOpcode::kAdds: case AluScalarOpcode::kAddsc0: case AluScalarOpcode::kAddsc1:
      sexpr = fmt::format("{} + {}", sa, sb); break;
    case AluScalarOpcode::kAddsPrev: sexpr = fmt::format("{} + xe_ps", sa); break;
    case AluScalarOpcode::kMuls: case AluScalarOpcode::kMulsc0: case AluScalarOpcode::kMulsc1:
      sexpr = fmt::format("xe_mul({}, {})", sa, sb); break;
    case AluScalarOpcode::kMulsPrev: sexpr = fmt::format("xe_mul({}, xe_ps)", sa); break;
    case AluScalarOpcode::kMulsPrev2:
      sexpr = fmt::format("(xe_ps == -3.402823466e+38 || xe_inf(xe_ps) || xe_nan(xe_ps) || xe_inf({1}) || xe_nan({1}) || {1} <= 0.0) "
                          "? -3.402823466e+38 : xe_mul({0}, xe_ps)", sa, sb);
      break;
    case AluScalarOpcode::kMaxs: sexpr = fmt::format("xe_max({}, {})", sa, sb); break;
    case AluScalarOpcode::kMins: sexpr = fmt::format("xe_min({}, {})", sa, sb); break;
    case AluScalarOpcode::kSeqs: sexpr = fmt::format("float(xe_eq({}, 0.0))", sa); break;
    case AluScalarOpcode::kSgts: sexpr = fmt::format("float(xe_gt({}, 0.0))", sa); break;
    case AluScalarOpcode::kSges: sexpr = fmt::format("float(xe_ge({}, 0.0))", sa); break;
    case AluScalarOpcode::kSnes: sexpr = fmt::format("float(xe_ne({}, 0.0))", sa); break;
    case AluScalarOpcode::kFrcs: sexpr = fmt::format("frac({})", sa); break;
    case AluScalarOpcode::kTruncs: sexpr = fmt::format("trunc({})", sa); break;
    case AluScalarOpcode::kFloors: sexpr = fmt::format("floor({})", sa); break;
    case AluScalarOpcode::kExp: sexpr = fmt::format("exp2({})", sa); break;
    case AluScalarOpcode::kLogc: sexpr = fmt::format("xe_clampmax(log2({}))", sa); break;
    case AluScalarOpcode::kLog: sexpr = fmt::format("log2({})", sa); break;
    case AluScalarOpcode::kRcpc: sexpr = fmt::format("xe_clampmax(1.0 / {})", sa); break;
    case AluScalarOpcode::kRcpf: sexpr = fmt::format("xe_clampzero(1.0 / {})", sa); break;
    case AluScalarOpcode::kRcp: sexpr = fmt::format("1.0 / {}", sa); break;
    case AluScalarOpcode::kRsqc: sexpr = fmt::format("xe_clampmax(rsqrt({}))", sa); break;
    case AluScalarOpcode::kRsqf: sexpr = fmt::format("xe_clampzero(rsqrt({}))", sa); break;
    case AluScalarOpcode::kRsq: sexpr = fmt::format("rsqrt({})", sa); break;
    case AluScalarOpcode::kMaxAs: sexpr = fmt::format("xe_max({}, {})", sa, sb); sa0 = fmt::format("xe_addr({}, true)", sa); break;
    case AluScalarOpcode::kMaxAsf: sexpr = fmt::format("xe_max({}, {})", sa, sb); sa0 = fmt::format("xe_addr({}, false)", sa); break;
    case AluScalarOpcode::kSubs: case AluScalarOpcode::kSubsc0: case AluScalarOpcode::kSubsc1:
      sexpr = fmt::format("{} - {}", sa, sb); break;
    case AluScalarOpcode::kSubsPrev: sexpr = fmt::format("{} - xe_ps", sa); break;
    case AluScalarOpcode::kSetpEq: spred = fmt::format("xe_eq({}, 0.0)", sa); sexpr = "float(!(" + spred + "))"; break;
    case AluScalarOpcode::kSetpNe: spred = fmt::format("xe_ne({}, 0.0)", sa); sexpr = "float(!(" + spred + "))"; break;
    case AluScalarOpcode::kSetpGt: spred = fmt::format("xe_gt({}, 0.0)", sa); sexpr = "float(!(" + spred + "))"; break;
    case AluScalarOpcode::kSetpGe: spred = fmt::format("xe_ge({}, 0.0)", sa); sexpr = "float(!(" + spred + "))"; break;
    case AluScalarOpcode::kSetpInv:
      spred = fmt::format("{} == 1.0", sa);
      sexpr = fmt::format("({0} == 1.0) ? 0.0 : (({0} == 0.0) ? 1.0 : {0})", sa);
      break;
    case AluScalarOpcode::kSetpPop:
      spred = fmt::format("({} - 1.0) <= 0.0", sa);
      sexpr = fmt::format("(({0} - 1.0) <= 0.0) ? 0.0 : ({0} - 1.0)", sa);
      break;
    case AluScalarOpcode::kSetpClr: spred = "false"; sexpr = "3.402823466e+38"; break;
    case AluScalarOpcode::kSetpRstr:
      spred = fmt::format("{} == 0.0", sa);
      sexpr = fmt::format("({0} == 0.0) ? 0.0 : {0}", sa);
      break;
    case AluScalarOpcode::kKillsEq: sexpr = fmt::format("float(xe_eq({}, 0.0))", sa); skill = "xe_s != 0.0"; break;
    case AluScalarOpcode::kKillsGt: sexpr = fmt::format("float(xe_gt({}, 0.0))", sa); skill = "xe_s != 0.0"; break;
    case AluScalarOpcode::kKillsGe: sexpr = fmt::format("float(xe_ge({}, 0.0))", sa); skill = "xe_s != 0.0"; break;
    case AluScalarOpcode::kKillsNe: sexpr = fmt::format("float(xe_ne({}, 0.0))", sa); skill = "xe_s != 0.0"; break;
    case AluScalarOpcode::kKillsOne: sexpr = fmt::format("float(xe_eq({}, 1.0))", sa); skill = "xe_s != 0.0"; break;
    case AluScalarOpcode::kSqrt: sexpr = fmt::format("sqrt({})", sa); break;
    case AluScalarOpcode::kSin: sexpr = fmt::format("sin({})", sa); break;
    case AluScalarOpcode::kCos: sexpr = fmt::format("cos({})", sa); break;
    case AluScalarOpcode::kRetainPrev: sexpr = "xe_ps"; break;
    default:
      report_.notes.push_back(fmt::format("opcode escalar desconocido {}", uint32_t(instr.scalar_opcode)));
      sexpr = "xe_ps";
      break;
  }
  if (!skill.empty()) report_.uses_kill = true;
  Line("float xe_s = " + sexpr + ";");

  // efectos: descartar pixel, predicado, registro de direcciones; despues los resultados
  if (is_pixel_shader()) {
    if (!vkill.empty()) Line("if (" + vkill + ") discard;");
    if (!skill.empty()) Line("if (" + skill + ") discard;");
  }
  if (!spred.empty() || !vpred.empty()) {
    // primero se calcula todo con el predicado viejo; despues se actualiza
    Line("xe_p0 = " + (spred.empty() ? vpred : spred) + ";");
  }
  if (!va0.empty()) Line("xe_a0 = " + va0 + ";");
  if (!sa0.empty()) Line("xe_a0 = " + sa0 + ";");
  Line("xe_ps = xe_s;");
  if (vec_needed) Store(instr.vector_and_constant_result, "xe_v");
  else Store(instr.vector_and_constant_result, "float4(0.0, 0.0, 0.0, 0.0)");   // solo 0 / 1 constantes
  Store(instr.scalar_result, "xe_s.xxxx");
  Close();
  ClosePredication(pred);
}

// ------------------------------------------------------------------------------------------- archivo completo
std::vector<uint8_t> HlslTranslator::CompleteTranslation() {
  // cerrar el control de flujo
  if (exec_cond_open_) {
    Close();
    exec_cond_open_ = false;
  }
  if (use_switch_) {
    Jump(kPcEnd);
    Close();                                  // ultimo bloque
    Line("xe_pc = 0xFFFFu;");                 // direccion desconocida: terminar
    Close();                                  // while
  } else {
    --indent_;
    Line("} while (false);");
  }

  const Shader& sh = current_shader();
  const bool vs = is_vertex_shader();
  const uint32_t n = std::max(register_count(), 1u);
  std::string out;
  out += fmt::format("// {} shader {:016X} de la 360, traducido por xenos2hlsl. No editar a mano.\n",
                     vs ? "Vertex" : "Pixel", sh.ucode_data_hash());
  out += "#include \"xe_common.hlsli\"\n\n";
  // texturas usadas (por constante de fetch y dimension)
  std::vector<std::string> decl;
  for (const auto& t : sh.texture_bindings()) {
    std::string d;
    switch (t.fetch_instr.dimension) {
      case xenos::FetchOpDimension::k3DOrStacked:
        d = fmt::format("Texture3D<float4> xe_t3d_{0} : register(t{1});", t.fetch_constant, 33 + t.fetch_constant);
        break;
      case xenos::FetchOpDimension::kCube:
        d = fmt::format("TextureCube<float4> xe_tcube_{0} : register(t{1});", t.fetch_constant, 65 + t.fetch_constant);
        break;
      default:
        d = fmt::format("Texture2D<float4> xe_t2d_{0} : register(t{1});", t.fetch_constant, 1 + t.fetch_constant);
        break;
    }
    if (std::find(decl.begin(), decl.end(), d) == decl.end()) decl.push_back(d);
    const std::string s = fmt::format("SamplerState xe_s_{0} : register(s{0});", t.fetch_constant);
    if (std::find(decl.begin(), decl.end(), s) == decl.end()) decl.push_back(s);
  }
  for (const auto& d : decl) out += d + "\n";
  if (!decl.empty()) out += "\n";

  std::string pro;   // prologo comun
  // registros: variables sueltas salvo que el shader los indexe (fxc compila mal escrituras parciales a
  // elementos de un array dentro de un bucle)
  if (current_shader().uses_register_dynamic_addressing()) {
    pro += fmt::format("  float4 r[{0}] = (float4[{0}])0;\n", n);
  } else {
    for (uint32_t i = 0; i < n; ++i) pro += fmt::format("  float4 {} = float4(0.0, 0.0, 0.0, 0.0);\n", Reg(i));
  }
  pro += "  float xe_ps = 0.0;\n  bool xe_p0 = false;\n  int xe_a0 = 0;\n  int4 xe_al = int4(0, 0, 0, 0);\n"
         "  uint4 xe_lc = uint4(0u, 0u, 0u, 0u);\n  float xe_lod = 0.0;\n  float3 xe_gh = float3(0.0, 0.0, 0.0), xe_gv = float3(0.0, 0.0, 0.0);\n";

  if (vs) {
    out += "XeVaryings main(uint xe_vid : SV_VertexID) {\n";
    out += pro;
    out += "  " + Reg(0) + ".x = float(xe_vid);\n";
    out += "  uint xe_vaddr = 0u, xe_vend = 0u;\n";
    out += "  float4 xe_pos = float4(0.0, 0.0, 0.0, 0.0), xe_psize = float4(0.0, 0.0, 0.0, 0.0);\n";
    for (uint32_t i = 0; i < 16; ++i) out += fmt::format("  float4 xe_o{} = float4(0.0, 0.0, 0.0, 0.0);\n", i);
    out += "\n";
    out += body_;
    out += "\n  XeVaryings xe_out;\n  xe_out.pos = xe_finish_position(xe_pos);\n";
    for (uint32_t i = 0; i < 16; ++i) out += fmt::format("  xe_out.o[{0}] = xe_o{0};\n", i);
    out += "  return xe_out;\n}\n";
  } else {
    const uint32_t colors = sh.writes_color_targets();
    out += "struct XePsOut {\n";
    for (uint32_t i = 0; i < 4; ++i)
      if (colors & (1u << i)) out += fmt::format("  float4 c{0} : SV_Target{0};\n", i);
    if (sh.writes_depth()) out += "  float depth : SV_Depth;\n";
    if (!colors && !sh.writes_depth()) out += "  float4 c0 : SV_Target0;\n";
    out += "};\n\n";
    out += "XePsOut main(XeVaryings xe_in, bool xe_front : SV_IsFrontFace) {\n";
    out += pro;
    out += "  uint xe_vaddr = 0u, xe_vend = 0u;\n";
    for (uint32_t i = 0; i < 4; ++i) out += fmt::format("  float4 xe_oc{} = float4(0.0, 0.0, 0.0, 0.0);\n", i);
    out += "  float4 xe_depth = float4(0.0, 0.0, 0.0, 0.0);\n";
    // interpoladores a r0..r15 y PsParamGen (posicion del pixel, cara y punto) al registro que diga el render
    const uint32_t ni = std::min(n, 16u);
    for (uint32_t i = 0; i < ni; ++i) out += fmt::format("  {} = xe_in.o[{}];\n", Reg(i), i);
    out += "  if ((xe_param_gen & 0xFFu) < " + std::to_string(ni) + "u) {\n"
           "    float2 xe_px = floor(xe_in.pos.xy) / xe_res_scale;\n"
           "    float4 xe_pg = float4(xe_px, 0.0, 0.0);\n"
           "    if (xe_param_gen & 0x100u) xe_pg.y = -xe_pg.y;\n"
           "    else if ((xe_flags & XE_FLAG_POLYGONAL) && !xe_front) xe_pg.x = asfloat(asuint(xe_pg.x) | 0x80000000u);\n";
    for (uint32_t i = 0; i < ni; ++i)
      out += fmt::format("    {}if ((xe_param_gen & 0xFFu) == {}u) {} = xe_pg;\n", i ? "else " : "", i, Reg(i));
    out += "  }\n\n";
    out += body_;
    out += "\n  XePsOut xe_out;\n";
    if (colors & 1u) out += "  xe_alpha_test(xe_oc0.w);\n";
    for (uint32_t i = 0; i < 4; ++i)
      if (colors & (1u << i)) out += fmt::format("  xe_out.c{0} = xe_oc{0};\n", i);
    if (sh.writes_depth()) out += "  xe_out.depth = xe_depth.x;\n";
    if (!colors && !sh.writes_depth()) out += "  xe_out.c0 = float4(0.0, 0.0, 0.0, 0.0);\n";
    out += "  return xe_out;\n}\n";
  }
  return std::vector<uint8_t>(out.begin(), out.end());
}

}  // namespace re_wor::xenos2hlsl
