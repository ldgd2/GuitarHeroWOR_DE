// Generado por tools/d3d_device_map.py: no editar a mano.
// Bloques de registros de la GPU copiados dentro del dispositivo D3D de la 360.
#pragma once
#include <cstdint>

namespace re_wor::nativegfx {
struct DeviceRegBlock { uint32_t reg, offset, count; };
inline constexpr DeviceRegBlock kDeviceRegBlocks[] = {
    {0x4800, 1152, 192},   // SHADER_CONSTANT_FETCH_00_0
    {0x4000, 1920, 1024},   // SHADER_CONSTANT_000_X
    {0x4400, 6016, 1024},   // SHADER_CONSTANT_256_X
    {0x4900, 10112, 8},   // SHADER_CONSTANT_BOOL_000_031
    {0x4908, 10144, 32},   // SHADER_CONSTANT_LOOP_00
    {0x2388, 10272, 24},   // PA_CL_UCP_0_X
    {0x2000, 10368, 16},   // RB_SURFACE_INFO
    {0x2080, 10432, 3},   // PA_SC_WINDOW_OFFSET
    {0x2100, 10444, 21},   // VGT_MAX_VTX_INDX
    {0x2180, 10528, 5},   // SQ_PROGRAM_CNTL
    {0x2200, 10548, 12},   // RB_DEPTHCONTROL
    {0x2280, 10596, 21},   // PA_SU_POINT_SIZE
    {0x2300, 10680, 38},   // PA_SC_LINE_CNTL
    {0x2380, 10832, 8},   // PA_SU_POLY_OFFSET_FRONT_SCALE
};
}  // namespace re_wor::nativegfx
