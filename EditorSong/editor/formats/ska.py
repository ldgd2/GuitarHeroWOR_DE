"""Animaciones .ska para la vista 3D.

Convenciones verificadas contra el esqueleto (.ske) del juego:
  * Las rotaciones se guardan como i16/32768 (x, y, z) con w >= 0 reconstruido, y representan la rotacion
    INVERSA de la que usa three.js/glTF: hay que conjugarlas (x, y, z, w) -> (-x, -y, -z, w). Con eso la
    pose de enlace calculada coincide exactamente con las matrices inversas del .ske (error 0).
  * Las traslaciones son la posicion local del hueso respecto a su padre (metros).
  * Tiempos en fotogramas a 60 fps.
  * Camaras (3 huesos): hueso 0 = cambio de ejes, hueso 1 = la camara relativa al nodo, FOV en una clave
    custom tipo 1 / valor 16 (radianes).
Aqui se devuelven ya conjugadas y con el tiempo en segundos.
"""
from __future__ import annotations

import math
import struct

import numpy as np

from .. import core  # noqa: F401  (sys.path a tools/)
from ska_convert import read_ska

Q = 32768.0


def _quats(keys) -> np.ndarray:
    a = np.array(keys, dtype=np.float64)
    t = a[:, 0] / 60.0
    xyz = a[:, 1:4] / Q
    w = np.sqrt(np.clip(1 - (xyz ** 2).sum(1), 0, 1))
    return np.column_stack([t, -xyz, w])          # conjugado


SINGLE_FRAME, COMPRESSED = 1 << 6, 1 << 23


def _single_frame(data: bytes) -> dict | None:
    """Poses de un fotograma (flags 0x160B5040: p. ej. drum_s1t3, la postura sentada de la bateria).
    Por hueso: rotacion = 4 float32 (x, y, z, w) y traslacion = 4 float32 (x, y, z, 1), sin tiempo.
    ska_convert.read_ska las lee como si fueran claves int16 (sale basura), por eso se leen aqui."""
    a = struct.unpack_from(">I", data, 4)[0]
    flags = struct.unpack_from(">I", data, a + 4)[0]
    if not flags & SINGLE_FRAME or flags & COMPRESSED:
        return None
    nb = struct.unpack_from(">H", data, a + 0x0C)[0]
    _pc, pq, pt, pqs, pts = struct.unpack_from(">iiiii", data, a + 0x54)
    out = {"duration": 0.0, "bones": nb, "camera": nb <= 4, "rot": {}, "pos": {}, "fov": [], "partial": False,
           "flags": flags, "additive": False, "single": True}
    for table, ptr, key in ((pqs, pq, "rot"), (pts, pt, "pos")):
        o = ptr
        for b in range(nb):
            size = struct.unpack_from(">H", data, table + 2 * b)[0] if table > 0 else 16
            if size >= 16:
                x, y, z, w = struct.unpack_from(">4f", data, o)
                out[key][b] = [0.0, -x, -y, -z, w] if key == "rot" else [0.0, x, y, z]
            o += size
    return out


def ska_full(data: bytes) -> dict:
    """{duration, bones, rot: {hueso: [t, x, y, z, w, ...]}, pos: {hueso: [t, x, y, z, ...]}, fov: [[t, grados]]}"""
    single = _single_frame(data)
    if single:
        return single
    s = read_ska(data)
    out = {"duration": s.duration, "bones": s.num_bones, "camera": s.num_bones <= 4, "rot": {}, "pos": {}, "fov": [],
           "partial": bool(s.partial), "flags": s.flags, "additive": False}
    for b, keys in s.quats.items():
        if keys:
            out["rot"][b] = np.round(_quats(keys), 5).reshape(-1).tolist()
    for b, keys in s.trans.items():
        if keys:
            a = np.array(keys, dtype=np.float64)
            a[:, 0] /= 60.0
            out["pos"][b] = np.round(a, 5).reshape(-1).tolist()
    for c in s.custom:
        if c.type == 1 and c.value == 16 and c.mod is not None:
            out["fov"].append([c.time, round(math.degrees(c.mod), 3)])
    out["additive"] = _is_additive(out["rot"])
    return out


def _is_additive(rot: dict) -> bool:
    """Capa aditiva (p. ej. drum_loop_generic_100 sobre la pose drum_s1t3): sus rotaciones son deltas.
    Se detecta por los biceps: en la pose de enlace estan a ~45 grados, asi que en una animacion normal nunca
    valen la identidad; en una capa aditiva empiezan en la identidad."""
    keys = [rot[b] for b in (8, 12) if b in rot]
    return bool(keys) and all(abs(k[4]) > 0.9995 for k in keys)


def ska_preview(data: bytes) -> dict:
    """Resumen ligero (compatibilidad): solo raiz y hueso 1, sin conjugar."""
    s = read_ska(data)
    cam = s.num_bones <= 4

    def track(bone, step):
        q = s.quats.get(bone) or []
        t = s.trans.get(bone) or []
        rot = []
        for k in q[::step]:
            x, y, z = k[1] / Q, k[2] / Q, k[3] / Q
            rot.append([k[0] / 60.0, x, y, z, math.sqrt(max(0.0, 1 - x * x - y * y - z * z))])
        return {"rot": rot, "pos": [[k[0] / 60.0, k[1], k[2], k[3]] for k in t[::step]]}
    out = {"camera": cam, "duration": s.duration, "bones": s.num_bones,
           "root": track(0, 1 if cam else 2), "main": track(1, 1 if cam else 2), "fov": []}
    for c in s.custom:
        if c.type == 1 and c.value == 16 and c.mod is not None:
            out["fov"].append([c.time, round(math.degrees(c.mod), 3)])
    return out
