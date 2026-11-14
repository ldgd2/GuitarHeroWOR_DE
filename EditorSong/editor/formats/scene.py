"""Geometria de Neversoft para WoR (Xbox 360): .scn (escenario) y .skin (personajes, instrumentos, props).

Estructura (big-endian; formato comprobado contra los archivos del juego):
  0x00   u32 offset del pie (desde 0x80), 7 x 0xFAAABACA, relleno hasta 0x80
  0x80   lista de materiales: u8 version, u8, u16 numero, u32 tamano, u32 offset (relativo), u32 -1
         cada material (tamano propio en +0x94, alineado a 16):
           +0x00 QBKey  +0x70 plantilla  +0x88 n texturas  +0x8C offset de la lista de QBKeys de textura
           +0x9C flags  +0xA8 modo de mezcla  +0xB8 corte de alfa
  escena (alineada a 128 tras la lista; offsets relativos a su inicio):
    +0x4C n mallas  +0x6C n sectores  +0x70 sectores (96 bytes)  +0x74 CGeom  +0x80 mallas (144 bytes)
  malla (sMesh de WoR, 144 bytes): +0x10 offset de vertices, +0x14 u8 tamano de vertice (si +0x10 es 0,
    en +0x18/+0x1C: mallas con pesos, cuyo flujo solo lleva UV),
    +0x24 flags de formato, +0x30 offset de indices, +0x50 QBKey del material, +0x64 tipo de primitiva
    (4 lista de triangulos, 5 abanico, 6 tira, 13 lista de quads), +0x68 offset del bloque de pesos (-1 = no),
    +0x7A u16 numero de indices, +0x7C u16 numero de vertices
  vertices: 32 bytes de cabecera y despues cada vertice segun los flags (ver vertex_layout)
  indices: 32 bytes de cabecera y despues u16
"""
from __future__ import annotations

import struct
from dataclasses import dataclass, field

import numpy as np

F_SHORTPOS = 1 << 15
F_PIVOT = 1 << 12
F_TAN2 = 1 << 11
F_TAN1 = 1 << 10
F_EXTRA8 = 1 << 8
F_WEIGHTS = 1 << 7
F_NORMAL = 1 << 5
F_COLOR = 1 << 4
F_EXTRA1 = 1 << 1


@dataclass
class Material:
    key: int
    template: int
    textures: list[int]
    flags: int
    blend: int
    alpha_cut: int
    draw_order: int = 0


@dataclass
class Mesh:
    index: int
    material: int
    prim: int
    flags: int
    stride: int
    positions: np.ndarray | None = None      # (n, 3) float32
    normals: np.ndarray | None = None        # (n, 3) float32
    colors: np.ndarray | None = None         # (n, 4) uint8 RGBA
    uvs: list[np.ndarray] = field(default_factory=list)   # [(n, 2) float32]
    indices: np.ndarray | None = None        # (m,) uint32 (triangulos)
    joints: np.ndarray | None = None         # (n, 4) uint16
    weights: np.ndarray | None = None        # (n, 4) float32
    sector: int = 0
    error: str = ""


@dataclass
class Scene:
    materials: dict[int, Material]
    meshes: list[Mesh]
    sectors: list[int]
    bbox: tuple


def _u32(d, o):
    return struct.unpack_from(">I", d, o)[0]


def read_materials(d: bytes, base: int = 0x80) -> tuple[dict[int, Material], int]:
    ver, _unk, count, size, rel = struct.unpack_from(">BBhII", d, base)
    mats = {}
    o = base + rel
    for _ in range(max(0, count)):
        key, msize = _u32(d, o), _u32(d, o + 0x94)
        if msize <= 0 or o + msize > len(d):
            break
        n, pt = _u32(d, o + 0x88), _u32(d, o + 0x8C)
        tex = [_u32(d, o + pt + 4 * k) for k in range(min(n, 16))] if 0 < pt < msize else []
        mats[key] = Material(key, _u32(d, o + 0x70), tex, _u32(d, o + 0x9C), _u32(d, o + 0xA8), d[o + 0xB8],
                             _u32(d, o + 0xA4))
        o = (o + msize + 15) & ~15
    return mats, base + size


def vertex_layout(flags: int) -> tuple[list[tuple[str, int, int]], int]:
    """[(atributo, offset, bytes)] y tamano total del vertice, segun los flags de la malla."""
    out, o = [], 0

    def add(name, n):
        nonlocal o
        out.append((name, o, n))
        o += n
    add("pos16" if flags & F_SHORTPOS else "pos", 8 if flags & F_SHORTPOS else 12)
    if flags & F_PIVOT:
        add("pivot", 12)
    if flags & F_NORMAL:
        add("normal", 4)
    if flags & F_EXTRA8:
        add("tan0", 4)
    if flags & F_TAN1:
        add("tan1", 4)
    if flags & F_TAN2:
        add("tan2", 8)
    if flags & F_COLOR:
        add("color", 4)
    if flags & F_EXTRA1:
        add("extra", 4)
    for k in range(4):                       # juegos de UV: bit par = comprimido (half2), impar = float2
        if flags & (1 << (16 + 2 * k)):
            add(f"uv{k}h", 4)
        elif flags & (1 << (17 + 2 * k)):
            add(f"uv{k}f", 8)
    for k, bit in ((0, 24), (1, 26)):        # lightmaps
        if flags & (1 << bit):
            add(f"lm{k}h", 4)
        elif flags & (1 << (bit + 1)):
            add(f"lm{k}f", 8)
    return out, o


def _dec_normal(u: np.ndarray) -> np.ndarray:
    u = u.astype(np.int64)
    x = (u & 0x7FF)
    y = (u >> 11) & 0x7FF
    z = (u >> 22) & 0x3FF
    x = np.where(x >= 1024, x - 2048, x) / 1023.0
    y = np.where(y >= 1024, y - 2048, y) / 1023.0
    z = np.where(z >= 512, z - 1024, z) / 511.0
    n = np.stack([x, y, z], 1).astype(np.float32)
    ln = np.linalg.norm(n, axis=1, keepdims=True)
    return np.where(ln > 1e-6, n / np.maximum(ln, 1e-6), np.array([0, 1, 0], np.float32))


def _triangulate(idx: np.ndarray, prim: int) -> np.ndarray:
    if prim == 4:
        return idx[: len(idx) // 3 * 3]
    if prim == 13:                                       # quads -> 2 triangulos
        q = idx[: len(idx) // 4 * 4].reshape(-1, 4)
        return np.concatenate([q[:, [0, 1, 2]], q[:, [0, 2, 3]]], 1).reshape(-1)
    if prim == 6:                                        # tira con reinicio 0xFFFF
        out, strip = [], []
        for v in list(idx) + [0xFFFF]:
            if v == 0xFFFF:
                for i in range(len(strip) - 2):
                    a, b, c = strip[i], strip[i + 1], strip[i + 2]
                    if a != b and b != c and a != c:
                        out += [a, b, c] if i % 2 == 0 else [b, a, c]
                strip = []
            else:
                strip.append(v)
        return np.array(out, np.uint32)
    if prim == 5:
        return np.array([[idx[0], idx[i], idx[i + 1]] for i in range(1, len(idx) - 1)], np.uint32).reshape(-1)
    return np.zeros(0, np.uint32)


def _read_static_vertices(d: bytes, start: int, n: int, flags: int, stride: int, m: Mesh) -> None:
    lay, size = vertex_layout(flags)
    if size != stride:
        m.error = f"tamano de vertice {stride} != {size} (flags 0x{flags:x})"
        return
    raw = np.frombuffer(d, dtype=np.uint8, count=n * stride, offset=start + 32).reshape(n, stride)
    for name, o, nb in lay:
        col = np.ascontiguousarray(raw[:, o:o + nb])
        if name == "pos":
            m.positions = col.view(">f4").astype(np.float32).reshape(n, 3)
        elif name == "pos16":
            m.positions = col.view(">f2").astype(np.float32).reshape(n, 4)[:, :3]   # 4 x half
        elif name == "normal":
            m.normals = _dec_normal(col.view(">u4").reshape(n))
        elif name == "color":
            c = col.reshape(n, 4)
            m.colors = c[:, [1, 2, 3, 0]].copy()           # ARGB -> RGBA
        elif name.startswith(("uv", "lm")):
            uv = (col.view(">f2") if name.endswith("h") else col.view(">f4")).astype(np.float32).reshape(n, 2)
            m.uvs.append(uv)


def read_scene(d: bytes) -> Scene:
    mats, end = read_materials(d)
    ss = (end + 127) & ~127
    bbox = struct.unpack_from(">8f", d, ss)
    nmesh, nsec = _u32(d, ss + 0x4C), _u32(d, ss + 0x6C)
    p_sec, p_geom, p_mesh = _u32(d, ss + 0x70), _u32(d, ss + 0x74), _u32(d, ss + 0x80)
    sectors = [_u32(d, ss + p_sec + 96 * i + 4) for i in range(nsec)]
    # CGeom: asigna mallas a sectores (objetos)
    sector_of = {}
    o = ss + p_geom
    for si in range(nsec):
        flags = _u32(d, ss + p_sec + 96 * si + 8)
        if flags & 0x20:
            continue
        first, cnt = struct.unpack_from(">ii", d, o + 0x40)
        for k in range(max(0, cnt)):
            sector_of[first + k] = si
        o += 96
    meshes = []
    for i in range(nmesh):
        b = ss + p_mesh + 144 * i
        # dos variantes de cabecera: si +0x10 es 0, el flujo de UV/vertices esta en +0x18 (tamano en +0x1C)
        uv_start, stride = (_u32(d, b + 0x10), d[b + 0x14]) if _u32(d, b + 0x10) else (_u32(d, b + 0x18), d[b + 0x1C])
        flags, face_start = _u32(d, b + 0x24), _u32(d, b + 0x30)
        mat, prim, vert_start = _u32(d, b + 0x50), _u32(d, b + 0x64), _u32(d, b + 0x68)
        nidx, nvert = struct.unpack_from(">HH", d, b + 0x7A)
        m = Mesh(i, mat, prim, flags, stride, sector=sector_of.get(i, -1))
        try:
            if vert_start != 0xFFFFFFFF and flags & F_WEIGHTS:
                _read_weighted(d, ss + uv_start, ss + vert_start, nvert, flags, m)
            else:
                _read_static_vertices(d, ss + uv_start, nvert, flags, stride, m)
            if m.positions is not None:
                idx = np.frombuffer(d, dtype=">u2", count=nidx, offset=ss + face_start + 32).astype(np.uint32)
                tri = _triangulate(idx, prim)
                m.indices = tri[tri.reshape(-1) < nvert] if len(tri) and tri.max() >= nvert else tri
                if len(m.indices) % 3:
                    m.indices = m.indices[: len(m.indices) // 3 * 3]
        except Exception as ex:  # noqa: BLE001
            m.error = str(ex)
        meshes.append(m)
    return Scene(mats, meshes, sectors, bbox)


def _read_weighted(d: bytes, uv_start: int, vstart: int, n: int, flags: int, m: Mesh) -> None:
    """Mallas con pesos (personajes): se rellenan en skinned.py."""
    from .skinned import read_weighted
    read_weighted(d, uv_start, vstart, n, flags, m)
