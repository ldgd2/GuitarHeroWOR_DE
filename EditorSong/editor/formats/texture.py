"""Contenedor de texturas .tex de WoR (Xbox 360) -> imagenes PNG.

Cabecera del .tex (big-endian): u32 0xFACECAA7, u16 0x011C, u16 numero de texturas, u32 inicio de la tabla.
Cada textura (40 bytes, version 10):
  u8 version(10) u8 tam(40) u8 flags u8 tipo(0 normal, 1 normal map, 3 especular, 4 cubemap) u32 QBKey
  u16 ancho, u16 alto, u16 1, u16 ancho real, u16 alto real, u16 1, u8 mips, u8 bpp, u8 dxt, u8 0,
  u32 offset de los mips (relativo a los pixeles), u32 offset de la cabecera D3D de Xbox, u32 tamano,
  u32 offset de los pixeles (nivel 0) en el archivo
La cabecera D3D (52 bytes) lleva el GPUTEXTURE_FETCH_CONSTANT en +28: dword0 bit31 = en mosaico (tiled),
dword1 bits 0-5 = formato GPU, bits 6-7 = endian (1 = intercambiar bytes de cada u16).
Los pixeles estan en el orden de mosaico de Xenos: se desordenan con XGAddress2DTiledX/Y por bloques.
"""
from __future__ import annotations

import io
import struct
from dataclasses import dataclass

import numpy as np

MAGIC = 0xFACECAA7

# formato GPU de Xenos -> (bytes por bloque, lado del bloque, nombre)
GPU_FORMATS = {
    0x12: (8, 4, "DXT1"),
    0x13: (16, 4, "DXT3"),
    0x14: (16, 4, "DXT5"),
    0x31: (16, 4, "BC5"),     # DXN: mapas de normales (2 canales)
    0x3C: (8, 4, "DXT1"),     # DXT1 usado como 16_16_16_16 (normales comprimidas)
    0x06: (4, 1, "ARGB8"),
    0x02: (1, 1, "L8"),
    0x0A: (2, 1, "A8L8"),
    0x04: (2, 1, "R5G6B5"),
    0x03: (2, 1, "A1R5G5B5"),
    0x0F: (2, 1, "A4R4G4B4"),
}


@dataclass
class TexInfo:
    key: int
    kind: int          # 0 normal, 1 mapa de normales, 3 especular, 4 cubemap
    width: int
    height: int
    levels: int
    fmt: int           # formato GPU de Xenos
    tiled: bool
    endian: int
    data_off: int
    size: int

    @property
    def name(self) -> str:
        return GPU_FORMATS.get(self.fmt, (0, 0, f"0x{self.fmt:02x}"))[2]


def read_tex(d: bytes) -> list[TexInfo]:
    if len(d) < 16 or struct.unpack_from(">I", d, 0)[0] != MAGIC:
        return []
    n = struct.unpack_from(">H", d, 6)[0]
    table = struct.unpack_from(">I", d, 8)[0]
    out = []
    for i in range(n):
        o = table + 40 * i
        (_ver, _hs, _fl, kind, key, bw, bh, _a, _aw, _ah, _b, levels, _bpp, _dxt, _z,
         _mips, d3d, size, data_off) = struct.unpack_from(">BBBBIHHHHHHBBBBIIII", d, o)
        w0, w1, w2 = struct.unpack_from(">III", d, d3d + 28)
        width, height = (w2 & 0x1FFF) + 1, ((w2 >> 13) & 0x1FFF) + 1
        out.append(TexInfo(key, kind, width or bw, height or bh, levels, w1 & 0x3F, bool(w0 >> 31),
                           (w1 >> 6) & 3, data_off, size))
    return out


# ------------------------------------------------------------------ desordenar mosaico de Xenos
def _tiled_xy(n_blocks_w_aligned: int, n_total: int, bpb: int):
    """Posicion (x, y) en bloques de cada bloque del buffer en mosaico (vectorizado)."""
    off = np.arange(n_total, dtype=np.int64)
    log_bpp = (bpb >> 2) + ((bpb >> 1) >> (bpb >> 2))
    ob = off << log_bpp
    ot = ((ob & ~4095) >> 3) + ((ob & 1792) >> 2) + (ob & 63)
    om = ot >> (7 + log_bpp)
    tiles_w = n_blocks_w_aligned >> 5
    # X
    macro_x = (om % tiles_w) << 2
    tile_x = ((((ot >> (5 + log_bpp)) & 2) + (ob >> 6)) & 3)
    micro_x = ((((ot >> 1) & ~15) + (ot & 15)) & ((bpb << 3) - 1)) >> log_bpp
    x = ((macro_x + tile_x) << 3) + micro_x
    # Y
    macro_y = (om // tiles_w) << 2
    tile_y = ((ot >> (6 + log_bpp)) & 1) + ((ob & 2048) >> 10)
    micro_y = (((ot & (((bpb << 6) - 1) & ~31)) + ((ot & 15) << 1)) >> (3 + log_bpp)) & ~1
    y = ((macro_y + tile_y) << 3) + micro_y + ((ot & 16) >> 4)
    return x, y


def untile(data: bytes, bw: int, bh: int, bpb: int) -> bytes:
    """bw/bh: tamano en bloques. Devuelve el nivel 0 en orden lineal."""
    aw = (bw + 31) & ~31
    ah = (bh + 31) & ~31
    total = aw * ah
    src = np.frombuffer(data[:total * bpb].ljust(total * bpb, b"\0"), dtype=np.uint8).reshape(total, bpb)
    # el bloque n del buffer en mosaico va a la posicion lineal (x, y)
    x, y = _tiled_xy(aw, total, bpb)
    dst = np.zeros((total, bpb), dtype=np.uint8)
    dst[np.clip(y * aw + x, 0, total - 1)] = src
    return np.ascontiguousarray(dst.reshape(ah, aw, bpb)[:bh, :bw]).tobytes()


def _swap16(b: bytes) -> bytes:
    a = np.frombuffer(b[: len(b) & ~1], dtype=">u2").astype("<u2")
    return a.tobytes()


def _dds(fourcc: bytes, w: int, h: int, data: bytes) -> bytes:
    hdr = struct.pack("<4sIIIIIII44s", b"DDS ", 124, 0x1007 | 0x80000, h, w, len(data), 0, 1, b"\0" * 44)
    hdr += struct.pack("<IIII16s", 32, 4, int.from_bytes(fourcc, "little"), 0, b"\0" * 16)  # pixel format
    hdr += struct.pack("<IIIII", 0x1000, 0, 0, 0, 0)                                        # caps
    return hdr + data


def decode(d: bytes, t: TexInfo, max_size: int = 1024):
    """Nivel 0 de la textura como imagen PIL RGBA (o None si el formato no se soporta)."""
    from PIL import Image
    spec = GPU_FORMATS.get(t.fmt)
    if not spec:
        return None
    bpb, blk, name = spec
    bw, bh = max(1, (t.width + blk - 1) // blk), max(1, (t.height + blk - 1) // blk)
    raw = d[t.data_off: t.data_off + t.size]
    if t.endian == 1 or (blk == 4 and t.endian != 0):
        raw = _swap16(raw)
    elif t.endian == 2:
        raw = np.frombuffer(raw[: len(raw) & ~3], dtype=">u4").astype("<u4").tobytes()
    lin = untile(raw, bw, bh, bpb) if t.tiled else raw[: bw * bh * bpb]
    if blk == 4:
        fourcc = {"DXT1": b"DXT1", "DXT3": b"DXT3", "DXT5": b"DXT5", "BC5": b"ATI2"}[name]
        img = Image.open(io.BytesIO(_dds(fourcc, bw * 4, bh * 4, lin)))
        img.load()
        img = img.convert("RGBA")
        if name == "BC5":  # normal map: reconstruir Z
            a = np.asarray(img).astype(np.float32)
            nx, ny = a[..., 0] / 127.5 - 1, a[..., 1] / 127.5 - 1
            nz = np.sqrt(np.clip(1 - nx * nx - ny * ny, 0, 1))
            a[..., 2] = (nz + 1) * 127.5
            a[..., 3] = 255
            img = Image.fromarray(a.astype(np.uint8), "RGBA")
        img = img.crop((0, 0, t.width, t.height))
    elif name == "ARGB8":
        a = np.frombuffer(lin, dtype=np.uint8).reshape(bh, bw, 4)
        img = Image.fromarray(a[..., [1, 2, 3, 0]].copy(), "RGBA")
    elif name == "L8":
        img = Image.frombytes("L", (bw, bh), lin).convert("RGBA")
    elif name == "A8L8":
        a = np.frombuffer(lin, dtype=np.uint8).reshape(bh, bw, 2)
        img = Image.fromarray(np.dstack([a[..., 1]] * 3 + [a[..., 0]]), "RGBA")
    elif name == "A1R5G5B5":
        a = np.frombuffer(lin, dtype="<u2").reshape(bh, bw).astype(np.uint32)
        alpha = (a >> 15) * 255
        if not alpha.any():          # bit de alfa sin usar (X1R5G5B5): opaca
            alpha[:] = 255
        img = Image.fromarray(np.dstack([((a >> 10) & 31) * 255 // 31, ((a >> 5) & 31) * 255 // 31, (a & 31) * 255 // 31,
                                         alpha]).astype(np.uint8), "RGBA")
    elif name == "A4R4G4B4":
        a = np.frombuffer(lin, dtype="<u2").reshape(bh, bw).astype(np.uint32)
        img = Image.fromarray(np.dstack([((a >> s_) & 15) * 17 for s_ in (8, 4, 0, 12)]).astype(np.uint8), "RGBA")
    else:
        a = np.frombuffer(lin, dtype="<u2").reshape(bh, bw).astype(np.uint32)
        rgb = np.dstack([((a >> 11) & 31) * 255 // 31, ((a >> 5) & 63) * 255 // 63, (a & 31) * 255 // 31,
                         np.full_like(a, 255)]).astype(np.uint8)
        img = Image.fromarray(rgb, "RGBA")
    if max(img.size) > max_size:
        img.thumbnail((max_size, max_size))
    return img
