// nativegfx/state/texture_desc.cpp
// Implementacion de la decodificacion de constantes de fetch de textura de Xenos.
#include "nativegfx/state/texture_desc.hpp"

namespace re_wor::nativegfx {

namespace {

TextureAddressMode DecodeAddressMode(uint32_t clamp) {
  switch (clamp & 7) {
    case 0: return TextureAddressMode::Repeat;
    case 1: return TextureAddressMode::MirroredRepeat;
    case 2: return TextureAddressMode::ClampToEdge;
    case 3: return TextureAddressMode::MirrorClampToEdge;
    case 4: return TextureAddressMode::ClampToBorder;
    case 5: return TextureAddressMode::MirrorClampToBorder;
    default: return TextureAddressMode::Repeat;
  }
}

}  // namespace

TextureFormatInfo GetTextureFormatInfo(uint32_t format_id) {
  switch (format_id) {
    case 2:  return {1, 1, 1, false, "8"};
    case 8:  return {1, 1, 1, false, "8_A"};
    case 9:  return {1, 1, 1, false, "8_B"};
    case 3:  return {2, 1, 1, false, "1_5_5_5"};
    case 4:  return {2, 1, 1, false, "5_6_5"};
    case 5:  return {2, 1, 1, false, "6_5_5"};
    case 10: return {2, 1, 1, false, "8_8"};
    case 15: return {2, 1, 1, false, "4_4_4_4"};
    case 24: return {2, 1, 1, false, "16"};
    case 27: return {2, 1, 1, false, "16_EXPAND"};
    case 30: return {2, 1, 1, false, "16_FLOAT"};
    case 6:  return {4, 1, 1, false, "8_8_8_8"};
    case 7:  return {4, 1, 1, false, "2_10_10_10"};
    case 14: return {4, 1, 1, false, "8_8_8_8_A"};
    case 50: return {4, 1, 1, false, "8_8_8_8_AS_16_16_16_16"};
    case 54: return {4, 1, 1, false, "2_10_10_10_AS_16_16_16_16"};
    // Bloques DXT / BC comprimidos (8 bytes por bloque 4x4)
    case 18: return {8, 4, 4, true, "DXT1"};
    case 51: return {8, 4, 4, true, "DXT1_AS_16_16_16_16"};
    case 58: return {8, 4, 4, true, "DXT1_A"};
    case 59: return {8, 4, 4, true, "DXT1_A_AS_16_16_16_16"};
    case 60: return {8, 4, 4, true, "DXGO"};
    // Bloques DXT2..5 / BC3..5 (16 bytes por bloque 4x4)
    case 19: return {16, 4, 4, true, "DXT2_3"};
    case 20: return {16, 4, 4, true, "DXT4_5"};
    case 49: return {16, 4, 4, true, "DXN"};
    case 52: return {16, 4, 4, true, "DXT2_3_AS_16_16_16_16"};
    case 53: return {16, 4, 4, true, "DXT4_5_AS_16_16_16_16"};
    // Formatos RGBA de 64 bits (8 bytes por pixel)
    case 25: return {4, 1, 1, false, "16_16"};
    case 26: return {8, 1, 1, false, "16_16_16_16"};
    case 28: return {4, 1, 1, false, "16_16_EXPAND"};
    case 29: return {8, 1, 1, false, "16_16_16_16_EXPAND"};
    case 31: return {4, 1, 1, false, "16_16_FLOAT"};
    case 32: return {8, 1, 1, false, "16_16_16_16_FLOAT"};
    case 34: return {8, 1, 1, false, "32_32_FLOAT"};
    case 35: return {16, 1, 1, false, "32_32_32_32_FLOAT"};
    default: return {4, 1, 1, false, "Unknown"};
  }
}

TextureDesc DecodeTextureFetchConstant(const uint32_t fc[6]) {
  TextureDesc desc;

  // fc[0]: tipo de dimension, pitch, tiled, clamp modes
  const uint32_t dim_type = fc[0] & 3;
  switch (dim_type) {
    case 0: desc.dimension = TextureDimension::OneD; break;
    case 1: desc.dimension = TextureDimension::TwoD; break;
    case 2: desc.dimension = TextureDimension::ThreeD; break;
    case 3: desc.dimension = TextureDimension::Cube; break;
  }

  desc.address_u = DecodeAddressMode((fc[0] >> 10) & 7);
  desc.address_v = DecodeAddressMode((fc[0] >> 13) & 7);
  desc.address_w = DecodeAddressMode((fc[0] >> 16) & 7);

  const uint32_t pitch_tiles = (fc[0] >> 22) & 0x1FF;
  desc.pitch_bytes = pitch_tiles * 32;
  desc.is_tiled = ((fc[0] >> 31) != 0);

  // fc[1]: direccion fisica base en memoria de la consola
  desc.guest_physical_address = (fc[1] & 0x1FFFFFFF) << 12;

  // fc[2]: dimensiones
  desc.width  = ((fc[2] >> 0) & 0x1FFF) + 1;
  desc.height = ((fc[2] >> 13) & 0x1FFF) + 1;
  desc.depth  = ((fc[2] >> 26) & 0x3F) + 1;

  // fc[3]: formato, swizzle, orden de bytes
  const uint32_t swz = fc[3] & 0xFFF;
  for (uint32_t i = 0; i < 4; ++i) {
    desc.swizzle[i] = uint8_t((swz >> (3 * i)) & 7);
  }

  desc.format_id = (fc[3] >> 19) & 0x3F;
  desc.format_info = GetTextureFormatInfo(desc.format_id);
  desc.endianness = (fc[3] >> 26) & 3;

  // fc[4]: niveles mipmap
  const uint32_t mip_min = (fc[4] >> 0) & 0xF;
  const uint32_t mip_max = (fc[4] >> 4) & 0xF;
  desc.mip_levels = (mip_max >= mip_min) ? (mip_max - mip_min + 1) : 1;

  // fc[5]: mips empaquetados
  desc.is_packed_mips = ((fc[5] >> 11) & 1) != 0;

  return desc;
}

}  // namespace re_wor::nativegfx
