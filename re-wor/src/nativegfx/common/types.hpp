// nativegfx/common/types.hpp
// Tipos primitivos, enumeraciones graficas desacopladas y utilidades matematicas.
#pragma once

#include <cmath>
#include <cstdint>
#include <cstring>
#include <stdlib.h>

namespace re_wor::nativegfx {

// Utilidades de orden de bytes (big-endian de la 360)
inline uint32_t Swap32(uint32_t v) { return _byteswap_ulong(v); }
inline uint16_t Swap16(uint16_t v) { return _byteswap_ushort(v); }

inline uint32_t SwapGpu(uint32_t v, uint32_t endian) {
  if (endian == 1 || endian == 2) v = ((v & 0x00FF00FFu) << 8) | ((v >> 8) & 0x00FF00FFu);
  if (endian == 2 || endian == 3) v = (v << 16) | (v >> 16);
  return v;
}

inline float AsFloat(uint32_t v) {
  float f;
  std::memcpy(&f, &v, 4);
  return f;
}

inline float HalfToFloat(uint16_t h) {
  const uint32_t s = (h >> 15) & 1, e = (h >> 10) & 0x1F, m = h & 0x3FF;
  float v;
  if (e == 0) v = std::ldexp(float(m), -24);
  else if (e == 31) v = m ? NAN : INFINITY;
  else v = std::ldexp(float(m | 0x400), int(e) - 25);
  return s ? -v : v;
}

inline float Float20e4ToFloat(uint32_t f24) {
  f24 &= 0xFFFFFF;
  const uint32_t m = f24 & 0xFFFFF, e = f24 >> 20;
  if (!e) return std::ldexp(float(m), -20 - 14);
  return std::ldexp(1.0f + float(m) / float(1 << 20), int(e) - 15);
}

// Estructuras matematicas basicas
struct Vec2 {
  float x = 0.0f, y = 0.0f;
};

struct Vec3 {
  float x = 0.0f, y = 0.0f, z = 0.0f;
};

struct Vec4 {
  float x = 0.0f, y = 0.0f, z = 0.0f, w = 0.0f;
};

struct Rect2D {
  int32_t left = 0;
  int32_t top = 0;
  int32_t right = 0;
  int32_t bottom = 0;

  int32_t Width() const { return right - left; }
  int32_t Height() const { return bottom - top; }
  bool IsEmpty() const { return right <= left || bottom <= top; }
};

struct Viewport {
  float x = 0.0f;
  float y = 0.0f;
  float width = 0.0f;
  float height = 0.0f;
  float min_z = 0.0f;
  float max_z = 1.0f;
};

// Topologias de primitivas
enum class PrimitiveTopology : uint32_t {
  Unknown = 0,
  PointList,
  LineList,
  LineStrip,
  TriangleList,
  TriangleFan,
  TriangleStrip,
  RectangleList,
  QuadList,
};

// Formato de indices de primitivas
enum class IndexFormat : uint32_t {
  None = 0,
  Index16,
  Index32,
};

// Modos de descarte de caras
enum class CullMode : uint32_t {
  None = 0,
  Front,
  Back,
};

// Modos de rellenado
enum class FillMode : uint32_t {
  Solid = 0,
  Wireframe,
};

// Funciones de comparacion (Depth, Stencil, Alpha test)
enum class ComparisonFunc : uint32_t {
  Never = 0,
  Less,
  Equal,
  LessEqual,
  Greater,
  NotEqual,
  GreaterEqual,
  Always,
};

// Factores de mezcla (Blend Factors)
enum class BlendFactor : uint32_t {
  Zero = 0,
  One,
  SrcColor,
  InvSrcColor,
  SrcAlpha,
  InvSrcAlpha,
  DestAlpha,
  InvDestAlpha,
  DestColor,
  InvDestColor,
  SrcAlphaSat,
  BlendFactor,
  InvBlendFactor,
};

// Operaciones de mezcla (Blend Operations)
enum class BlendOp : uint32_t {
  Add = 0,
  Subtract,
  RevSubtract,
  Min,
  Max,
};

// Operaciones de Stencil
enum class StencilOp : uint32_t {
  Keep = 0,
  Zero,
  Replace,
  IncrSat,
  DecrSat,
  Invert,
  Incr,
  Decr,
};

// Mascaras de escritura de color
namespace ColorWriteMask {
inline constexpr uint8_t Red = 0x1;
inline constexpr uint8_t Green = 0x2;
inline constexpr uint8_t Blue = 0x4;
inline constexpr uint8_t Alpha = 0x8;
inline constexpr uint8_t All = Red | Green | Blue | Alpha;
}  // namespace ColorWriteMask

// Dimension de textura
enum class TextureDimension : uint32_t {
  OneD = 0,
  TwoD,
  ThreeD,
  Cube,
};

}  // namespace re_wor::nativegfx
