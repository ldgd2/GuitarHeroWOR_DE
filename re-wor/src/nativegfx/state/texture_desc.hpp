// nativegfx/state/texture_desc.hpp
// Decodificacion tipada de constantes de fetch de textura de la GPU Xenos.
#pragma once

#include <cstdint>
#include "nativegfx/common/types.hpp"

namespace re_wor::nativegfx {

// Modos de direccionamiento de textura (Wrap / Clamp)
enum class TextureAddressMode : uint32_t {
  Repeat = 0,
  MirroredRepeat,
  ClampToEdge,
  MirrorClampToEdge,
  ClampToBorder,
  MirrorClampToBorder,
};

// Formato de textura decodificado
struct TextureFormatInfo {
  uint32_t bytes_per_block = 4;
  uint32_t block_width     = 1;
  uint32_t block_height    = 1;
  bool is_compressed       = false;
  const char* name         = "Unknown";
};

// Descripcion completa decodificada de una textura
struct TextureDesc {
  TextureDimension dimension = TextureDimension::TwoD;
  uint32_t width             = 0;
  uint32_t height            = 0;
  uint32_t depth             = 1;
  uint32_t mip_levels        = 1;
  uint32_t pitch_bytes       = 0;
  bool is_tiled              = false;
  bool is_packed_mips        = false;

  uint32_t format_id         = 0;   // xenos::TextureFormat
  TextureFormatInfo format_info;

  uint32_t guest_physical_address = 0;
  uint32_t endianness             = 0;

  // Swizzle de 4 componentes: cada uno 0=R, 1=G, 2=B, 3=A, 4=0.0, 5=1.0
  uint8_t swizzle[4] = {0, 1, 2, 3};

  TextureAddressMode address_u = TextureAddressMode::Repeat;
  TextureAddressMode address_v = TextureAddressMode::Repeat;
  TextureAddressMode address_w = TextureAddressMode::Repeat;

  // Valida si la constante describe una textura valida en memoria de la 360
  bool IsValid() const {
    return width > 0 && height > 0 && width <= 8192 && height <= 8192 &&
           guest_physical_address >= 0x00010000 && guest_physical_address < (512u << 20);
  }
};

// Decodifica las 6 palabras uint32 de una constante de fetch en TextureDesc
TextureDesc DecodeTextureFetchConstant(const uint32_t fc[6]);

// Obtiene la informacion de bloques y bytes de un formato de la 360
TextureFormatInfo GetTextureFormatInfo(uint32_t format_id);

}  // namespace re_wor::nativegfx
