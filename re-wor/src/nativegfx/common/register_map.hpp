// nativegfx/common/register_map.hpp
// Mapa de la estructura del dispositivo Direct3D de Xbox 360 en memoria (Fase 2).
// Mapea los offsets del dispositivo huésped (dev + N) con los registros de la GPU Xenos.
#pragma once

#include <cstdint>

namespace re_wor::nativegfx {

// Offsets principales dentro de la estructura del dispositivo Direct3D de la 360
inline constexpr uint32_t kDeviceShadowOffset = 1152;   // dev + 1152: inicio de copia de registros
inline constexpr uint32_t kDeviceShadowBytes  = 9712;   // 9712 bytes de registros en total
inline constexpr uint32_t kDeviceDirtyBytes   = 48;     // dev + 0..47: mascaras de registros sucios
inline constexpr uint32_t kDeviceCmdWritePtr  = 48;     // dev + 48: puntero de escritura de comandos

// Bloque de registros mapeado en el dispositivo
struct DeviceRegBlock {
  uint32_t reg;      // Registro base de la GPU (0x2000, 0x4000...)
  uint32_t offset;   // Desplazamiento desde el inicio del dispositivo (dev + offset)
  uint32_t count;    // Cantidad de palabras uint32 (4 bytes cada una)
};

// Tabla de bloques de registros en la memoria del dispositivo huésped
inline constexpr DeviceRegBlock kDeviceRegBlocks[] = {
    {0x4800, 1152, 192},   // SHADER_CONSTANT_FETCH_00_0 (32 constantes * 6 palabras)
    {0x4000, 1920, 1024},  // SHADER_CONSTANT_000_X (256 float4 de Vertex Shader)
    {0x4400, 6016, 1024},  // SHADER_CONSTANT_256_X (256 float4 de Pixel Shader)
    {0x4900, 10112, 8},    // SHADER_CONSTANT_BOOL_000_031 (256 bools)
    {0x4908, 10144, 32},   // SHADER_CONSTANT_LOOP_00 (32 constantes de bucle)
    {0x2388, 10272, 24},   // PA_CL_UCP_0_X (6 planos de recorte de usuario)
    {0x2000, 10368, 16},   // RB_SURFACE_INFO, RB_COLOR_INFO, RB_DEPTH_INFO
    {0x2080, 10432, 3},    // PA_SC_WINDOW_OFFSET, PA_SC_WINDOW_SCISSOR_TL/BR
    {0x2100, 10444, 21},   // VGT_MAX_VTX_INDX, mascara color, blend factor, viewport
    {0x2180, 10528, 5},    // SQ_PROGRAM_CNTL
    {0x2200, 10548, 12},   // RB_DEPTHCONTROL, RB_BLENDCONTROL_0..3
    {0x2280, 10596, 21},   // PA_SU_POINT_SIZE, PA_SU_SC_MODE_CNTL, VTX_CNTL
    {0x2300, 10680, 38},   // PA_SC_LINE_CNTL
    {0x2380, 10832, 8},    // PA_SU_POLY_OFFSET_FRONT_SCALE
};

// Mascaras de registros sucios (dirty masks) en dev + 0..47
inline constexpr uint32_t kDirtyMaskVsConstants   = 0;   // dev + 0 (8 bytes / 64 bits para VS float)
inline constexpr uint32_t kDirtyMaskPsConstants   = 8;   // dev + 8 (8 bytes / 64 bits para PS float)
inline constexpr uint32_t kDirtyMaskFetchConsts   = 16;  // dev + 16 (Fetch constants)
inline constexpr uint32_t kDirtyMaskBoolLoop      = 24;  // dev + 24 (Bools y loops)
inline constexpr uint32_t kDirtyMaskRenderState1  = 32;  // dev + 32 (Estados de render 1)
inline constexpr uint32_t kDirtyMaskRenderState2  = 40;  // dev + 40 (Estados de render 2)

// Registros individuales de interes critico
namespace RegIndex {
inline constexpr uint32_t RB_SURFACE_INFO            = 0x2000;
inline constexpr uint32_t RB_COLOR_INFO              = 0x2001;
inline constexpr uint32_t RB_DEPTH_INFO              = 0x2002;
inline constexpr uint32_t RB_COLOR1_INFO             = 0x2003;
inline constexpr uint32_t RB_COLOR2_INFO             = 0x2004;
inline constexpr uint32_t RB_COLOR3_INFO             = 0x2005;
inline constexpr uint32_t PA_SC_WINDOW_OFFSET        = 0x2080;
inline constexpr uint32_t PA_SC_WINDOW_SCISSOR_TL    = 0x2081;
inline constexpr uint32_t PA_SC_WINDOW_SCISSOR_BR    = 0x2082;
inline constexpr uint32_t RB_COLOR_MASK              = 0x2104;
inline constexpr uint32_t RB_BLEND_RED               = 0x2105;
inline constexpr uint32_t RB_BLEND_GREEN             = 0x2106;
inline constexpr uint32_t RB_BLEND_BLUE              = 0x2107;
inline constexpr uint32_t RB_BLEND_ALPHA             = 0x2108;
inline constexpr uint32_t RB_COLORCONTROL            = 0x2109;
inline constexpr uint32_t PA_CL_VTE_CNTL             = 0x210C;
inline constexpr uint32_t RB_COLOR_REF               = 0x210E;
inline constexpr uint32_t PA_CL_VPORT_XSCALE         = 0x210F;
inline constexpr uint32_t PA_CL_VPORT_XOFFSET        = 0x2110;
inline constexpr uint32_t PA_CL_VPORT_YSCALE         = 0x2111;
inline constexpr uint32_t PA_CL_VPORT_YOFFSET        = 0x2112;
inline constexpr uint32_t PA_CL_VPORT_ZSCALE         = 0x2113;
inline constexpr uint32_t PA_CL_VPORT_ZOFFSET        = 0x2114;
inline constexpr uint32_t SQ_PROGRAM_CNTL            = 0x2180;
inline constexpr uint32_t SQ_CONTEXT_MISC            = 0x2181;
inline constexpr uint32_t RB_DEPTHCONTROL            = 0x2200;
inline constexpr uint32_t RB_BLENDCONTROL_0          = 0x2201;
inline constexpr uint32_t RB_BLENDCONTROL_1          = 0x2209;
inline constexpr uint32_t RB_BLENDCONTROL_2          = 0x220A;
inline constexpr uint32_t RB_BLENDCONTROL_3          = 0x220B;
inline constexpr uint32_t PA_SU_SC_MODE_CNTL         = 0x2205;
inline constexpr uint32_t PA_SU_VTX_CNTL             = 0x2285;
inline constexpr uint32_t SHADER_CONSTANT_000        = 0x4000;
inline constexpr uint32_t SHADER_CONSTANT_256        = 0x4400;
inline constexpr uint32_t SHADER_FETCH_CONSTANT_00   = 0x4800;
inline constexpr uint32_t SHADER_BOOL_CONSTANT_00    = 0x4900;
inline constexpr uint32_t SHADER_LOOP_CONSTANT_00    = 0x4908;
}  // namespace RegIndex

}  // namespace re_wor::nativegfx
