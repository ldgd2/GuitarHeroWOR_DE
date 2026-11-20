// nativegfx/device/device_reader.cpp
// Implementacion del lector desacoplado del estado del dispositivo Xbox 360 D3D (Fase 2).
#include "nativegfx/device/device_reader.hpp"

#include <algorithm>
#include <cstring>

namespace re_wor::nativegfx {

namespace {

inline uint32_t ReadBe32(const uint8_t* p) {
  uint32_t v;
  std::memcpy(&v, p, 4);
  return Swap32(v);
}

}  // namespace

DeviceReader::DeviceReader() {
  std::memset(shadow_regs_, 0, sizeof(shadow_regs_));
}

void DeviceReader::UpdateFromDevice(const uint8_t* memory_base, uint32_t device_guest_addr, bool force_all) {
  if (!memory_base || device_guest_addr < 0x00010000) return;
  current_device_addr_ = device_guest_addr;

  const uint8_t* dev_ptr = memory_base + device_guest_addr;

  for (const auto& block : kDeviceRegBlocks) {
    const uint8_t* src = dev_ptr + block.offset;
    for (uint32_t i = 0; i < block.count; ++i) {
      shadow_regs_[block.reg + i] = ReadBe32(src + 4 * i);
    }
  }
}

void DeviceReader::SetActiveShader(uint32_t type, uint32_t guest_addr, uint32_t dword_count, uint64_t hash) {
  BoundShaderInfo info;
  info.guest_address = guest_addr;
  info.dword_count = dword_count;
  info.hash = hash;
  info.is_valid = (guest_addr != 0 && dword_count > 0);

  if (type == 0) {
    active_vs_ = info;
  } else {
    active_ps_ = info;
  }
}

PrimitiveTopology DeviceReader::DecodeTopology(uint32_t prim_type) const {
  switch (prim_type) {
    case 1: return PrimitiveTopology::PointList;
    case 2: return PrimitiveTopology::LineList;
    case 3: return PrimitiveTopology::LineStrip;
    case 4: return PrimitiveTopology::TriangleList;
    case 5: return PrimitiveTopology::TriangleFan;
    case 6: return PrimitiveTopology::TriangleStrip;
    case 8: return PrimitiveTopology::RectangleList;
    case 13: return PrimitiveTopology::QuadList;
    default: return PrimitiveTopology::TriangleList;
  }
}

bool DeviceReader::CalculateScissor(Rect2D& out_rect, float scale) const {
  const uint32_t wtl_v = shadow_regs_[RegIndex::PA_SC_WINDOW_SCISSOR_TL];
  const uint32_t wbr_v = shadow_regs_[RegIndex::PA_SC_WINDOW_SCISSOR_BR];

  int32_t tlx = int32_t(wtl_v & 0x7FFF);
  int32_t tly = int32_t((wtl_v >> 16) & 0x7FFF);
  int32_t brx = int32_t(wbr_v & 0x7FFF);
  int32_t bry = int32_t((wbr_v >> 16) & 0x7FFF);

  const bool window_offset_disable = ((wtl_v >> 31) & 1) != 0;
  if (!window_offset_disable) {
    const uint32_t wo_v = shadow_regs_[RegIndex::PA_SC_WINDOW_OFFSET];
    const int32_t wox = int32_t(int16_t(wo_v & 0xFFFF));
    const int32_t woy = int32_t(int16_t((wo_v >> 16) & 0xFFFF));
    tlx += wox;
    tly += woy;
    brx += wox;
    bry += woy;
  }

  // Limitar a dimensiones positivas
  tlx = std::max(tlx, 0);
  tly = std::max(tly, 0);

  if (brx <= tlx || bry <= tly) {
    out_rect = {0, 0, 0, 0};
    return false;
  }

  out_rect.left   = int32_t(tlx * scale);
  out_rect.top    = int32_t(tly * scale);
  out_rect.right  = int32_t(brx * scale);
  out_rect.bottom = int32_t(bry * scale);
  return true;
}

bool DeviceReader::ExtractPipelineState(PipelineState& out_state, float resolution_scale) const {
  // 1. Render Targets & Depth
  const uint32_t si = shadow_regs_[RegIndex::RB_SURFACE_INFO];
  out_state.render_targets.surface_pitch = (si & 0x3FFF);
  out_state.render_targets.surface_height = 720;
  out_state.render_targets.msaa_samples = 1u << ((si >> 16) & 3);

  const uint32_t mask = shadow_regs_[RegIndex::RB_COLOR_MASK];
  static const uint32_t kColorRegs[4] = {
      RegIndex::RB_COLOR_INFO,
      RegIndex::RB_COLOR1_INFO,
      RegIndex::RB_COLOR2_INFO,
      RegIndex::RB_COLOR3_INFO
  };

  for (uint32_t i = 0; i < 4; ++i) {
    const uint32_t ci = shadow_regs_[kColorRegs[i]];
    ColorTargetDesc& rt = out_state.render_targets.color_targets[i];
    rt.base_address = ci & 0xFFF;
    rt.format       = (ci >> 16) & 0x1F;
    rt.write_mask   = uint8_t((mask >> (4 * i)) & 0xF);
    rt.enabled      = (rt.write_mask != 0 && rt.base_address != 0);
  }

  const uint32_t di = shadow_regs_[RegIndex::RB_DEPTH_INFO];
  const uint32_t dc = shadow_regs_[RegIndex::RB_DEPTHCONTROL];
  out_state.render_targets.depth_target.base_address = di & 0xFFF;
  out_state.render_targets.depth_target.format       = (di >> 16) & 3;
  out_state.render_targets.depth_target.enabled      = ((dc & 1) != 0 || ((dc >> 7) & 1) != 0);

  // 2. Mezcla (Blend State)
  out_state.blend.independent_blend_enable = true;
  static const uint32_t kBlendRegs[4] = {
      RegIndex::RB_BLENDCONTROL_0,
      RegIndex::RB_BLENDCONTROL_1,
      RegIndex::RB_BLENDCONTROL_2,
      RegIndex::RB_BLENDCONTROL_3
  };

  for (uint32_t i = 0; i < 4; ++i) {
    const uint32_t bc = shadow_regs_[kBlendRegs[i]];
    RenderTargetBlendDesc& b = out_state.blend.targets[i];
    b.write_mask = out_state.render_targets.color_targets[i].write_mask;

    const uint32_t c_src = (bc >> 0) & 0x1F;
    const uint32_t c_op  = (bc >> 5) & 0x7;
    const uint32_t c_dst = (bc >> 8) & 0x1F;
    const uint32_t a_src = (bc >> 16) & 0x1F;
    const uint32_t a_op  = (bc >> 21) & 0x7;
    const uint32_t a_dst = (bc >> 24) & 0x1F;

    b.src_blend        = DecodeBlendFactor(c_src, false);
    b.dest_blend       = DecodeBlendFactor(c_dst, false);
    b.blend_op         = DecodeBlendOp(c_op);
    b.src_blend_alpha  = DecodeBlendFactor(a_src, true);
    b.dest_blend_alpha = DecodeBlendFactor(a_dst, true);
    b.blend_op_alpha   = DecodeBlendOp(a_op);

    b.blend_enable = !(b.src_blend == BlendFactor::One && b.dest_blend == BlendFactor::Zero &&
                       b.blend_op == BlendOp::Add && b.src_blend_alpha == BlendFactor::One &&
                       b.dest_blend_alpha == BlendFactor::Zero && b.blend_op_alpha == BlendOp::Add);
  }

  out_state.blend.blend_constant.x = AsFloat(shadow_regs_[RegIndex::RB_BLEND_RED]);
  out_state.blend.blend_constant.y = AsFloat(shadow_regs_[RegIndex::RB_BLEND_GREEN]);
  out_state.blend.blend_constant.z = AsFloat(shadow_regs_[RegIndex::RB_BLEND_BLUE]);
  out_state.blend.blend_constant.w = AsFloat(shadow_regs_[RegIndex::RB_BLEND_ALPHA]);

  // 3. Profundidad y Plantilla (Depth-Stencil)
  out_state.depth_stencil.depth_enable       = (dc & 1) != 0;
  out_state.depth_stencil.depth_write_enable = ((dc >> 1) & 1) != 0;
  out_state.depth_stencil.depth_func         = DecodeComparisonFunc((dc >> 4) & 7);
  out_state.depth_stencil.stencil_enable     = ((dc >> 7) & 1) != 0;
  out_state.depth_stencil.front_face.compare_func = DecodeComparisonFunc((dc >> 8) & 7);
  out_state.depth_stencil.front_face.fail_op       = DecodeStencilOp((dc >> 11) & 7);
  out_state.depth_stencil.front_face.pass_op       = DecodeStencilOp((dc >> 14) & 7);
  out_state.depth_stencil.front_face.depth_fail_op = DecodeStencilOp((dc >> 17) & 7);

  // 4. Rasterizador y Scissor
  const uint32_t sm = shadow_regs_[RegIndex::PA_SU_SC_MODE_CNTL];
  const bool cull_front = ((sm >> 0) & 1) != 0;
  const bool cull_back  = ((sm >> 1) & 1) != 0;
  out_state.rasterizer.cull_mode = (cull_front && cull_back) ? CullMode::None
                                 : cull_front ? CullMode::Front
                                 : cull_back ? CullMode::Back : CullMode::None;
  out_state.rasterizer.front_counter_clockwise = ((sm >> 2) & 1) == 0;
  out_state.rasterizer.fill_mode = (((sm >> 3) & 3) == 0) ? FillMode::Solid : FillMode::Wireframe;
  out_state.rasterizer.vtx_window_offset_enable = ((sm >> 16) & 1) != 0;

  const uint32_t wo = shadow_regs_[RegIndex::PA_SC_WINDOW_OFFSET];
  out_state.rasterizer.window_offset.x = float(int16_t(wo & 0xFFFF));
  out_state.rasterizer.window_offset.y = float(int16_t((wo >> 16) & 0xFFFF));

  CalculateScissor(out_state.rasterizer.scissor_rect, resolution_scale);

  // 5. Constantes de Transformación NDC del Sistema
  const uint32_t vte = shadow_regs_[RegIndex::PA_CL_VTE_CNTL];
  const float W = float(out_state.render_targets.surface_pitch ? out_state.render_targets.surface_pitch : 1280);
  constexpr float H = 1024.0f;   // Alto fijo de coordenadas de superficie Xenos

  const bool vport_x_scale_ena  = ((vte >> 8) & 1) != 0;
  const bool vport_x_offset_ena = ((vte >> 9) & 1) != 0;
  const bool vport_y_scale_ena  = ((vte >> 10) & 1) != 0;
  const bool vport_y_offset_ena = ((vte >> 11) & 1) != 0;
  const bool vport_z_scale_ena  = ((vte >> 12) & 1) != 0;
  const bool vport_z_offset_ena = ((vte >> 13) & 1) != 0;

  float xs = vport_x_scale_ena  ? AsFloat(shadow_regs_[RegIndex::PA_CL_VPORT_XSCALE])  : 1.0f;
  float xo = vport_x_offset_ena ? AsFloat(shadow_regs_[RegIndex::PA_CL_VPORT_XOFFSET]) : 0.0f;
  float ys = vport_y_scale_ena  ? AsFloat(shadow_regs_[RegIndex::PA_CL_VPORT_YSCALE])  : 1.0f;
  float yo = vport_y_offset_ena ? AsFloat(shadow_regs_[RegIndex::PA_CL_VPORT_YOFFSET]) : 0.0f;
  float zs = vport_z_scale_ena  ? AsFloat(shadow_regs_[RegIndex::PA_CL_VPORT_ZSCALE])  : 1.0f;
  float zo = vport_z_offset_ena ? AsFloat(shadow_regs_[RegIndex::PA_CL_VPORT_ZOFFSET]) : 0.0f;

  if (out_state.rasterizer.vtx_window_offset_enable) {
    xo += out_state.rasterizer.window_offset.x;
    yo += out_state.rasterizer.window_offset.y;
  }

  // Centro de pixel de Direct3D (0.5 texel offset)
  const uint32_t vtx_cntl = shadow_regs_[RegIndex::PA_SU_VTX_CNTL];
  if ((vtx_cntl & 1) == 0) {
    xo += 0.5f;
    yo += 0.5f;
  }

  SystemNdcConstants& sys = out_state.constants.system;
  sys.ndc_scale[0]  = 2.0f * xs / W;
  sys.ndc_offset[0] = 2.0f * xo / W - 1.0f;
  sys.ndc_scale[1]  = -2.0f * ys / H;
  sys.ndc_offset[1] = 1.0f - 2.0f * yo / H;
  sys.ndc_scale[2]  = zs;
  sys.ndc_offset[2] = zo;
  sys.ndc_scale[3]  = 1.0f;

  const bool vtx_w0_fmt = (vte & 1) != 0;
  const bool vtx_xy_fmt = ((vte >> 1) & 1) != 0;
  const bool vtx_z_fmt  = ((vte >> 2) & 1) != 0;
  sys.flags = (vtx_w0_fmt ? 1u : 0u) | (vtx_xy_fmt ? 2u : 0u) | (vtx_z_fmt ? 4u : 0u) | 8u;

  const uint32_t cctl = shadow_regs_[RegIndex::RB_COLORCONTROL];
  const bool alpha_test_enable = ((cctl >> 3) & 1) != 0;
  sys.alpha_func = alpha_test_enable ? ((cctl >> 4) & 7) : 7u;
  sys.alpha_ref  = AsFloat(shadow_regs_[RegIndex::RB_COLOR_REF]);

  const uint32_t pc = shadow_regs_[RegIndex::SQ_PROGRAM_CNTL];
  const bool param_gen = ((pc >> 8) & 1) != 0;
  sys.param_gen = param_gen ? ((shadow_regs_[RegIndex::SQ_CONTEXT_MISC] >> 8) & 0xFF) : 0xFFFFu;
  sys.res_scale[0] = resolution_scale;
  sys.res_scale[1] = resolution_scale;

  if (std::memcmp(&cached_system_, &sys, sizeof(sys)) != 0) {
    cached_system_ = sys;
    ++system_version_;
  }
  out_state.constants.system = cached_system_;
  out_state.constants.system_version = system_version_;

  // 6. Constantes de Shaders con versionado monótono (omite 9KB de memcpy redundante por draw call)
  if (std::memcmp(out_state.constants.vs_float, &shadow_regs_[RegIndex::SHADER_CONSTANT_000], sizeof(out_state.constants.vs_float)) != 0) {
    std::memcpy(out_state.constants.vs_float, &shadow_regs_[RegIndex::SHADER_CONSTANT_000], sizeof(out_state.constants.vs_float));
    ++out_state.constants.vs_version;
  }
  if (std::memcmp(out_state.constants.ps_float, &shadow_regs_[RegIndex::SHADER_CONSTANT_256], sizeof(out_state.constants.ps_float)) != 0) {
    std::memcpy(out_state.constants.ps_float, &shadow_regs_[RegIndex::SHADER_CONSTANT_256], sizeof(out_state.constants.ps_float));
    ++out_state.constants.ps_version;
  }
  if (std::memcmp(out_state.constants.bool_bits, &shadow_regs_[RegIndex::SHADER_BOOL_CONSTANT_00], sizeof(out_state.constants.bool_bits)) != 0 ||
      std::memcmp(out_state.constants.loop_consts, &shadow_regs_[RegIndex::SHADER_LOOP_CONSTANT_00], sizeof(out_state.constants.loop_consts)) != 0) {
    std::memcpy(out_state.constants.bool_bits, &shadow_regs_[RegIndex::SHADER_BOOL_CONSTANT_00], sizeof(out_state.constants.bool_bits));
    std::memcpy(out_state.constants.loop_consts, &shadow_regs_[RegIndex::SHADER_LOOP_CONSTANT_00], sizeof(out_state.constants.loop_consts));
    ++out_state.constants.bool_version;
  }
  if (std::memcmp(out_state.constants.fetch_consts, &shadow_regs_[RegIndex::SHADER_FETCH_CONSTANT_00], sizeof(out_state.constants.fetch_consts)) != 0) {
    std::memcpy(out_state.constants.fetch_consts, &shadow_regs_[RegIndex::SHADER_FETCH_CONSTANT_00], sizeof(out_state.constants.fetch_consts));
    ++out_state.constants.fetch_version;

    // 7. Decodificación en cache de las 32 texturas solo al cambiar fetch constants
    for (uint32_t i = 0; i < 32; ++i) {
      const uint32_t* fc = &shadow_regs_[RegIndex::SHADER_FETCH_CONSTANT_00 + 6 * i];
      if (std::memcmp(&cached_fetch_words_[6 * i], fc, 24) != 0) {
        std::memcpy(&cached_fetch_words_[6 * i], fc, 24);
        cached_textures_[i] = DecodeTextureFetchConstant(fc);
      }
      out_state.bound_textures[i] = cached_textures_[i];
    }
  }

  // 8. Shaders activos
  out_state.vertex_shader = active_vs_;
  out_state.pixel_shader  = active_ps_;

  return true;
}

bool DeviceReader::ExtractDrawCommand(uint32_t func_addr, const uint32_t args[8], const uint8_t* memory_base,
                                      DrawCommand& out_cmd, float resolution_scale) const {
  out_cmd.source_function = func_addr;
  ExtractPipelineState(out_cmd.state, resolution_scale);

  // Analizar parámetros según la función de dibujo de Xbox 360 D3D
  switch (func_addr) {
    // Draws indexados estándar (DRAW_INDX)
    case 0x822D18A0:
    case 0x822D1D80:
    case 0x822D2170:
    case 0x822D6180: {
      out_cmd.is_indexed = true;
      out_cmd.topology = DecodeTopology(args[1]);
      out_cmd.primitive_count = args[2];
      out_cmd.index_count     = args[2];
      out_cmd.index_buffer_address = args[3];
      out_cmd.index_format    = IndexFormat::Index16;
      return true;
    }

    // Draws con índices inmediatos o clears/rectángulos (DRAW_INDX_2)
    case 0x822D13C0:
    case 0x822D8690:
    case 0x822D9F90:
    case 0x822DA0E8:
    case 0x822E9220:
    case 0x822EF320: {
      out_cmd.is_indexed = false;
      out_cmd.topology = PrimitiveTopology::RectangleList;
      out_cmd.primitive_count = args[2] ? args[2] : 1;
      out_cmd.vertex_count = 4;
      return true;
    }

    default:
      return false;
  }
}

bool DeviceReader::ExtractResolveCommand(uint32_t func_addr, const uint32_t args[8], ResolveCommand& out_cmd) const {
  if (func_addr != 0x822E4538) return false;

  out_cmd.source_function = func_addr;
  out_cmd.source_surface_base = shadow_regs_[RegIndex::RB_COLOR_INFO] & 0xFFF;
  out_cmd.dest_texture_address = args[1];   // Destino pasado en argumentos
  out_cmd.dest_pitch           = shadow_regs_[RegIndex::RB_SURFACE_INFO] & 0x3FFF;
  out_cmd.width                = 1280;
  out_cmd.height               = 720;
  return true;
}

}  // namespace re_wor::nativegfx
