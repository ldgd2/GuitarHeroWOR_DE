"""Conversion de animaciones .ska de World Tour (WT / Smash Hits / WTDE) al formato de Warriors of Rock.

Ambos juegos usan el mismo esqueleto de 128 huesos (y 3 para camaras); lo que cambia es la
codificacion de los fotogramas clave:
  WT : rotaciones comprimidas (cada componente en 1 byte o int16 segun banderas) y traslaciones con
       cabecera de tiempo de 1+2+1 bytes. Flags tipicos 0x068B5000 (+BIG_TIME 0x100).
  WoR: rotaciones sin comprimir [u16 tiempo, i16 x, y, z] y traslaciones [f32 tiempo, f32 x, y, z].
       Cabecera +0x08 = 500, flags 0x960B5000 (camara 0x96011000).
Si el juego lee los datos de WT como si fueran de WoR, los huesos salen estirados/deformados.

Formato (big-endian):
  0x00 tamano, 0x04 0x20, 0x08 UnknownA (0 WT, 500 WoR), 0x0C ptr punteros de hueso / -1,
  0x10 -1, 0x14 -1, 0x18 ptr animacion parcial / -1, 0x1C UnknownD (-1 WT, 0 WoR)
  0x20 version 0x68, 0x24 flags, 0x28 duracion f32, 0x2C u16 huesos, 0x2E u16 n_rot,
  0x30 4x4 f32, 0x70 u16 n_tras, 0x72 u16 n_custom, 0x74 ptr custom, 0x78 ptr rot, 0x7C ptr tras,
  0x80 ptr tabla tamanos rot (u16 por hueso), 0x84 ptr tabla tamanos tras.
Tiempos en fotogramas a 60 fps. Implementacion propia a partir de la descripcion del formato.
"""
from __future__ import annotations

import struct
from dataclasses import dataclass, field

TRANS_FLOAT_TIME = 1 << 31
NO_SEP_TIME = 1 << 28
COMP_FLAGS = 1 << 23
BONE_POINTERS = 1 << 22
PARTIAL_ANIM = 1 << 19
BIG_TIME = 1 << 8
SINGLE_FRAME = 1 << 6

Q_COMP = 1 << 14
Q_X, Q_Y, Q_Z = 1 << 13, 1 << 12, 1 << 11

WOR_FLAGS_BODY = 0x960B5000
WOR_FLAGS_CAMERA = 0x96011000


@dataclass
class CustomKey:
    time: float
    type: int
    value: int
    mod: float | None = None


@dataclass
class Ska:
    flags: int
    duration: float
    num_bones: int
    quat_frames: int
    trans_frames: int
    floats: bytes  # 64 bytes (4 x 4 f32)
    quats: dict[int, list[tuple[int, int, int, int]]] = field(default_factory=dict)   # hueso -> (t,x,y,z)
    trans: dict[int, list[tuple[float, float, float, float]]] = field(default_factory=dict)
    custom: list[CustomKey] = field(default_factory=list)
    partial: list[int] | None = None  # 4 palabras de mascara de huesos
    unknown_a: int = 0
    has_bone_ptrs: bool = False

    @property
    def camera(self) -> bool:
        return self.num_bones <= 4


def _u16(d, o):
    return struct.unpack_from(">H", d, o)[0]


def _i16(d, o):
    return struct.unpack_from(">h", d, o)[0]


def _u32(d, o):
    return struct.unpack_from(">I", d, o)[0]


def _i32(d, o):
    return struct.unpack_from(">i", d, o)[0]


def _f32(d, o):
    return struct.unpack_from(">f", d, o)[0]


def read_ska(d: bytes) -> Ska:
    if _u32(d, 0) == 0:
        raise ValueError("formato de GH3 (antiguo) no soportado")
    unknown_a = _i32(d, 0x08)
    partial_ptr = _i32(d, 0x18)
    a = _u32(d, 0x04)
    version, flags = _u32(d, a), _u32(d, a + 4)
    if version != 0x68:
        raise ValueError(f"version de ska no soportada: {version:#x}")
    duration = _f32(d, a + 8)
    nb, qf = _u16(d, a + 0x0C), _u16(d, a + 0x0E)
    floats = d[a + 0x10:a + 0x50]
    tf, nc = _u16(d, a + 0x50), _u16(d, a + 0x52)
    p_custom, p_quat, p_trans, p_qsize, p_tsize = struct.unpack_from(">iiiii", d, a + 0x54)
    ska = Ska(flags, duration, nb, qf, tf, floats, unknown_a=unknown_a, has_bone_ptrs=_i32(d, 0x0C) > 0)
    qsizes = [_u16(d, p_qsize + 2 * i) for i in range(nb)] if p_qsize > 0 else [0] * nb
    tsizes = [_u16(d, p_tsize + 2 * i) for i in range(nb)] if p_tsize > 0 else [0] * nb
    if flags & PARTIAL_ANIM and partial_ptr > 0:
        ska.partial = list(struct.unpack_from(">IIII", d, partial_ptr + 4))

    compressed = bool(flags & COMP_FLAGS)
    big_time = bool(flags & BIG_TIME) and unknown_a in (0, -1) and compressed
    # --- rotaciones
    o = p_quat
    for b in range(nb):
        size = qsizes[b]
        if not size:
            continue
        end = o + size
        keys = []
        while o < end:
            if compressed:
                if big_time:
                    t, fl = _u16(d, o), _u16(d, o + 2)
                    o += 4
                else:
                    w = _u16(d, o)
                    t, fl = w & 0x7FF, w & 0xF800
                    o += 2
                if not fl & Q_COMP:
                    x, y, z = _i16(d, o), _i16(d, o + 2), _i16(d, o + 4)
                    o += 6
                elif not fl & (Q_X | Q_Y | Q_Z):
                    x, y = d[o], d[o + 1]
                    z = y
                    o += 2
                    o += o & 1
                else:
                    vals = []
                    for bit in (Q_X, Q_Y, Q_Z):
                        if fl & bit:
                            vals.append(d[o])
                            o += 1
                        else:
                            o += o & 1
                            vals.append(_i16(d, o))
                            o += 2
                    o += o & 1
                    x, y, z = vals
            else:
                t = _u16(d, o)
                if t == 0xDEAD:
                    o += 8
                    end += 8
                    continue
                x, y, z = _i16(d, o + 2), _i16(d, o + 4), _i16(d, o + 6)
                o += 8
            keys.append((t, x, y, z))
        ska.quats[b] = keys
        o = end
    # --- traslaciones
    o = p_trans
    first = True
    for b in range(nb):
        size = tsizes[b]
        if not size:
            continue
        end = o + size
        keys = []
        while o < end:
            if flags & TRANS_FLOAT_TIME:
                t = _f32(d, o)
                o += 4
            else:
                ft, lt = d[o], _u16(d, o + 1)
                o += 4
                t = float(lt if ft == 0 else ft - 0x40)
                if first:
                    o += 12  # tres floats a cero solo en el primer registro de la seccion
                    first = False
            keys.append((t, _f32(d, o), _f32(d, o + 4), _f32(d, o + 8)))
            o += 12
        ska.trans[b] = keys
        o = end
    # --- claves personalizadas
    if p_custom > 0:
        o = p_custom
        for _ in range(nc):
            t, ty, val = _f32(d, o), _i32(d, o + 4), _i32(d, o + 8)
            o += 12
            mod = None
            if ty == 1:
                mod = _f32(d, o)
                o += 4
            ska.custom.append(CustomKey(t, ty, val, mod))
    return ska


def _f32r(x: float) -> float:
    """Redondea a float32 (el motor calcula en precision simple)."""
    return struct.unpack(">f", struct.pack(">f", x))[0]


SEEK_INDEX_MIN_DURATION = 10.0


def needs_seek_index(duration: float) -> bool:
    return duration > SEEK_INDEX_MIN_DURATION


def _pad(out: bytearray, n: int) -> None:
    out += bytes((-len(out)) % n)


def write_wor_ska(s: Ska) -> bytes:
    if s.unknown_a not in (0, -1):
        flags = s.flags  # ya era de WoR
    else:
        # WT -> WoR: sin compresion, traslaciones con tiempo float, indice de punteros de hueso.
        flags = (s.flags & ~COMP_FLAGS) | TRANS_FLOAT_TIME | NO_SEP_TIME | BONE_POINTERS
        if s.duration > 34.11:
            flags |= BIG_TIME
    out = bytearray(0x100)
    # rotaciones
    p_quat = len(out)
    qsizes = [0] * s.num_bones
    key_offs: dict[int, list[tuple[int, int]]] = {}
    for b in range(s.num_bones):
        keys = s.quats.get(b)
        if not keys:
            continue
        start = len(out)
        key_offs[b] = []
        for t, x, y, z in keys:
            key_offs[b].append((t, len(out) - p_quat))
            out += struct.pack(">Hhhh", t, x, y, z)
        qsizes[b] = len(out) - start
        if len(out) % 16:
            out += struct.pack(">II", 0xDEADDEAD, 0xDEADDEAD)
    _pad(out, 0x80)
    p_trans = len(out)
    tsizes = [0] * s.num_bones
    for b in range(s.num_bones):
        keys = s.trans.get(b)
        if not keys:
            continue
        start = len(out)
        for t, x, y, z in keys:
            out += struct.pack(">ffff", float(t), x, y, z)
        tsizes[b] = len(out) - start
    _pad(out, 0x80)
    p_qsize = len(out)
    out += struct.pack(f">{s.num_bones}H", *qsizes)
    _pad(out, 0x80)
    p_tsize = len(out)
    out += struct.pack(f">{s.num_bones}H", *tsizes)
    _pad(out, 4)
    p_custom = -1
    if s.custom:
        p_custom = len(out)
        for k in s.custom:
            out += struct.pack(">fii", k.time, k.type, k.value)
            if k.type == 1:
                out += struct.pack(">f", k.mod or 0.0)
    p_partial = -1
    if s.partial is not None:
        p_partial = len(out)
        out += struct.pack(">IIIII", s.num_bones, *s.partial)
    p_bones = -1
    # Indice de busqueda: en WoR existe si y solo si la animacion dura mas de 10 s (sin excepciones en
    # ~10.000 animaciones del juego, cuerpos y camaras). Sin el, al empezar una animacion larga a mitad
    # (clips con startframe, loops) el motor no encuentra las claves: pose T, brazos estirados, camara fija.
    if needs_seek_index(s.duration):
        # Indice de busqueda: 20 bloques; el bloque i (instante i*duracion/20) guarda para cada hueso
        # la posicion (relativa a los datos de rotacion) de la ultima clave anterior a ese instante.
        p_bones = len(out)
        out += struct.pack(">20i", *[80 + i * s.num_bones * 4 for i in range(20)])
        for i in range(20):
            # Regla ajustada contra las animaciones de WoR: 99.4% de entradas identicas y nunca apunta
            # a una clave posterior a la de WoR (el motor busca hacia delante desde aqui).
            limit = _f32r(_f32r(_f32r(s.duration) * i) / 20.0) * 60.0 - 0.5
            limit = _f32r(limit)
            row = []
            for b in range(s.num_bones):
                ko = key_offs.get(b)
                if not ko:
                    row.append(-1)
                    continue
                best = ko[0][1]
                for t, off in ko:
                    if t < limit:
                        best = off
                    else:
                        break
                row.append(best)
            out += struct.pack(f">{s.num_bones}i", *row)
    total = len(out)
    struct.pack_into(">IIiiiiii", out, 0, total, 0x20, 500, p_bones, -1, -1, p_partial, 0)
    struct.pack_into(">IIfHH", out, 0x20, 0x68, flags, s.duration, s.num_bones, s.quat_frames)
    out[0x30:0x70] = s.floats
    struct.pack_into(">HHiiiii", out, 0x70, s.trans_frames, len(s.custom), p_custom, p_quat, p_trans,
                     p_qsize, p_tsize)
    return bytes(out)


def is_wor(d: bytes) -> bool:
    return len(d) > 0x28 and _i32(d, 0x08) != 0


def convert_wt_to_wor(d: bytes) -> bytes:
    """Devuelve la animacion en formato WoR (si ya lo es, la devuelve igual)."""
    if is_wor(d):
        if not needs_seek_index(_f32(d, 0x28)) or _i32(d, 0x0C) > 0:
            return d
        s = read_ska(d)  # ya en formato WoR pero sin indice de busqueda: se reconstruye con indice
        return write_wor_ska(s)
    s = read_ska(d)
    if s.flags & SINGLE_FRAME:
        raise ValueError("ska de un solo fotograma no soportado")
    return write_wor_ska(s)
