"""Animaciones de personaje nuevas (.ska) a partir de la captura del movil (cara y cuerpo), en el formato de WoR.

Formato medido en las animaciones del propio juego (260 faciales de 25 canciones y los clips de cuerpo):
  * esqueleto de 128 huesos, animacion PARCIAL: una mascara de 4 palabras dice que huesos trae (hueso i -> palabra
    i // 32, bit i % 32). Faciales: b063..b092 (mandibula, ojos, parpados, cejas, mejillas, labios, nariz, lengua).
    Cuerpo de clip: pelvis, columna, brazos, muslos, pies y los huesos objetivo de IK (93/94 pies, 126/127 manos);
    rodillas y tobillos los cierra el motor con IK.
  * rotacion = rotacion LOCAL del hueso, i16/32768 (x, y, z) con w >= 0, CONJUGADA respecto a three.js/glTF.
  * traslacion = desplazamiento respecto a la posicion de enlace del hueso (en el espacio del padre, metros).
  * claves cada 2 fotogramas de 60 (30 por segundo); flags 0x960B5000 (como las faciales del juego de <= 10 s:
    sin indice de busqueda). Por eso el editor parte las tomas en trozos de 10 s como mucho, igual que el juego
    reparte la cara de una cancion en varias animaciones (gh6_singer_male_<cancion>_1, _1b, _2...).
El editor manda ya las rotaciones locales (convencion three.js) y los desplazamientos: aqui solo se codifican.
"""
from __future__ import annotations

import math
import struct

from ..core import paths
from ..core.names import qbkey
from wor_pak import PakEntry
from ska_convert import BONE_POINTERS, Ska, needs_seek_index, write_wor_ska, WOR_FLAGS_BODY

from .project import SKA, ska_summary

NUM_BONES = 128
FACE_BONES = list(range(63, 93))
MAX_CHUNK_S = 10.0
_FLOATS = struct.pack(">16f", 0, 0, 0, 1, -0.5, -0.5, -0.5, 0.5, 0, 0, 0, 1, -0.5, -0.5, -0.5, 0.5)


def _q16(x: float, y: float, z: float, w: float) -> tuple[int, int, int]:
    """three.js (x, y, z, w) local -> clave del .ska: conjugada, w >= 0, i16."""
    n = math.sqrt(x * x + y * y + z * z + w * w) or 1.0
    x, y, z, w = x / n, y / n, z / n, w / n
    if w < 0:
        x, y, z = -x, -y, -z
    c = lambda v: max(-32767, min(32767, round(v * 32768)))  # noqa: E731
    return c(-x), c(-y), c(-z)


def _reduce(keys: list[tuple], tol: float) -> list[tuple]:
    """Quita claves que la interpolacion lineal entre sus vecinas ya reproduce (como hace el exportador del juego)."""
    if len(keys) <= 2:
        return keys
    out = [keys[0]]
    for i in range(1, len(keys) - 1):
        a, b, c = out[-1], keys[i], keys[i + 1]
        k = (b[0] - a[0]) / ((c[0] - a[0]) or 1)
        if any(abs(a[j] + (c[j] - a[j]) * k - b[j]) > tol for j in range(1, len(b))):
            out.append(b)
    out.append(keys[-1])
    return out


def partial_mask(bones) -> list[int]:
    m = [0, 0, 0, 0]
    for b in bones:
        m[b // 32] |= 1 << (b % 32)
    return m


def make_anim(name: str, n: int, bones: dict) -> dict:
    """n fotogramas a 30/s. bones: {"63": {"q": [x,y,z,w, ...n*4], "p": [x,y,z, ...n*3] (opcional)}, ...}"""
    if n < 2:
        raise ValueError("la animacion necesita al menos 2 fotogramas")
    if (n - 1) / 30.0 > MAX_CHUNK_S + 1e-6:
        raise ValueError(f"trozo de mas de {MAX_CHUNK_S:.0f} s: el editor debe partirlo")
    dur = (n - 1) / 30.0
    quats: dict[int, list] = {}
    trans: dict[int, list] = {}
    for sb, d in bones.items():
        b = int(sb)
        if not 0 <= b < NUM_BONES:
            raise ValueError(f"hueso fuera de rango: {b}")
        q = d.get("q") or []
        if len(q) != n * 4:
            raise ValueError(f"hueso {b}: {len(q) // 4} rotaciones para {n} fotogramas")
        qk = [(i * 2, *_q16(*q[i * 4:i * 4 + 4])) for i in range(n)]
        quats[b] = _reduce(qk, 1)   # 1/32768: sin perdida visible
        p = d.get("p")
        if p:
            if len(p) != n * 3:
                raise ValueError(f"hueso {b}: {len(p) // 3} posiciones para {n} fotogramas")
            tk = [(float(i * 2), float(p[i * 3]), float(p[i * 3 + 1]), float(p[i * 3 + 2])) for i in range(n)]
            trans[b] = _reduce(tk, 0.00005)   # 0.05 mm
        else:
            trans[b] = [(0.0, 0.0, 0.0, 0.0), (float((n - 1) * 2), 0.0, 0.0, 0.0)]
    flags = WOR_FLAGS_BODY | (BONE_POINTERS if needs_seek_index(dur) else 0)
    s = Ska(flags=flags, duration=dur, num_bones=NUM_BONES,
            quat_frames=sum(len(v) for v in quats.values()), trans_frames=sum(len(v) for v in trans.values()),
            floats=_FLOATS, quats=quats, trans=trans, custom=[], partial=partial_mask(quats), unknown_a=500)
    data = write_wor_ska(s)
    key = qbkey(name)
    (paths.CACHE / "imports").mkdir(exist_ok=True)
    (paths.CACHE / "imports" / f"0x{key:08x}.ska").write_bytes(data)
    out = ska_summary(PakEntry(SKA, 0, key, key, 0, 0, data))
    out.update(label=name, imported=True, generated=True)
    return out
