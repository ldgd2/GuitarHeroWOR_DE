// Backend Vulkan de xenos2hlsl_check (--vk): el mismo HLSL compilado a SPIR-V con dxc y ejecutado en la GPU por
// Vulkan 1.2. Vulkan se carga en tiempo de ejecucion (vulkan-1.dll del driver) y dxc tambien (dxcompiler.dll de una
// version de dxc con SPIR-V: la del Windows SDK no lo trae).
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace re_wor::xenos2hlsl::vkcheck {

// estado de un shader, ya con la forma de los cbuffers del HLSL
struct Frame {
  const void* c;          // b0: 256 x float4
  const void* bl;         // b1: bools (8 uint) + bucles (32 uint) = 160 bytes
  const void* fc;         // b2: 192 uint
  const void* sys;        // b3: 64 bytes
  const uint8_t* mem;     // t0: memoria de vertices
  const float* ps_in;     // pixel: casos x 16 x float4 (interpoladores)
};

class Backend {
 public:
  // dxc_dll: ruta de dxcompiler.dll con SPIR-V. tex_colors: color de las texturas de un texel (32 por tipo)
  static std::unique_ptr<Backend> Create(uint32_t cases, uint32_t mem_size, const float (*tex_colors)[4],
                                         const std::string& dxc_dll, std::string& err);
  virtual ~Backend() = default;
  virtual const std::string& device_name() const = 0;
  // src: HLSL de xenos2hlsl con el #include de xe_common.hlsli ya sustituido
  virtual bool Compile(const std::string& src, bool pixel, std::vector<uint32_t>& spirv, std::string& err) = 0;
  // vertice: out = casos x 17 x float4 (posicion, o0..o15)
  virtual bool RunVertex(const std::vector<uint32_t>& spirv, const Frame& f, std::vector<float>& out,
                         std::string& err) = 0;
  // pixel: colors = casos x 4 x float4 (oC0..oC3), depth = casos (0.25 = pixel descartado)
  virtual bool RunPixel(const std::vector<uint32_t>& spirv, const Frame& f, std::vector<float>& colors,
                        std::vector<float>& depth, std::string& err) = 0;
};

}  // namespace re_wor::xenos2hlsl::vkcheck
