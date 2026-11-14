"""Camaras de clip nuevas (.ska de 3 huesos) a partir de fotogramas calculados en el editor (presets).

Formato copiado de las camaras de WoR (medido en 420 camaras de 12 canciones):
  hueso 0: rotacion fija (-0.5, -0.5, -0.5, 0.5) = cambio de ejes; hueso 2: (0, -1, 0, 0) fijo
  hueso 1: la camara; posicion (m) y rotacion relativas al nodo TRG_Geo_Camera_Performance_* del musico,
           una clave cada 2 fotogramas (30 por segundo; tiempos en fotogramas de 60)
  FOV: claves custom tipo 1, valor 16, en radianes (horizontal); puede variar en el tiempo (zoom/vertigo)
  flags 0x96011000; si dura mas de 10 s lleva indice de busqueda (bit 22), como en el juego.
El editor envia la posicion y el cuaternion YA en el espacio del archivo (ver web/js/camera/shots.js).
"""
from __future__ import annotations

import math
import struct

from ..core import paths
from ..core.names import qbkey
from wor_pak import PakEntry
from ska_convert import BONE_POINTERS, CustomKey, Ska, needs_seek_index, write_wor_ska

from .project import SKA, ska_summary


def _floats() -> bytes:
    """Bloque 4x4 de floats de cabecera, igual en las camaras del juego."""
    return struct.pack(">16f", 0, 0, 0, 1, -0.5, -0.5, -0.5, 0.5, 0, 0, 0, 1, -0.5, -0.5, -0.5, 0.5)


def _q16(x: float, y: float, z: float, w: float) -> tuple[int, int, int]:
    if w < 0:
        x, y, z = -x, -y, -z
    n = math.sqrt(x * x + y * y + z * z + w * w) or 1.0
    c = lambda v: max(-32768, min(32767, round(v / n * 32768)))  # noqa: E731
    return c(x), c(y), c(z)


def make_camera(name: str, keys: list[list[float]], fovs: list[list[float]]) -> dict:
    """keys: [[t_seg, px, py, pz, qx, qy, qz, qw], ...] (cada 1/30 s); fovs: [[t_seg, grados], ...]."""
    if len(keys) < 2:
        raise ValueError("la camara necesita al menos 2 fotogramas")
    dur = float(keys[-1][0])
    end = round(dur * 60)
    q1, t1 = [], []
    for t, px, py, pz, qx, qy, qz, qw in keys:
        f = round(t * 60)
        q1.append((f, *_q16(qx, qy, qz, qw)))
        t1.append((float(f), float(px), float(py), float(pz)))
    quats = {0: [(0, -16384, -16384, -16384), (end, -16384, -16384, -16384)], 1: q1,
             2: [(0, 0, -32768, 0), (end, 0, -32768, 0)]}
    trans = {0: [(0.0, 0.0, 0.0, 0.0), (float(end), 0.0, 0.0, 0.0)], 1: t1,
             2: [(0.0, 0.0, 0.0, 0.0), (float(end), 0.0, 0.0, 0.0)]}
    flags = 0x96011000 | (BONE_POINTERS if needs_seek_index(dur) else 0)
    fk = fovs or [[0.0, 72.0]]
    if fk[-1][0] < dur:
        fk = fk + [[dur, fk[-1][1]]]
    custom = [CustomKey(float(t), 1, 16, math.radians(float(v))) for t, v in fk]
    s = Ska(flags=flags, duration=dur, num_bones=3, quat_frames=sum(len(v) for v in quats.values()),
            trans_frames=sum(len(v) for v in trans.values()), floats=_floats(), quats=quats, trans=trans,
            custom=custom, unknown_a=500)
    data = write_wor_ska(s)
    key = qbkey(name)
    (paths.CACHE / "imports").mkdir(exist_ok=True)
    (paths.CACHE / "imports" / f"0x{key:08x}.ska").write_bytes(data)
    out = ska_summary(PakEntry(SKA, 0, key, key, 0, 0, data))
    out.update(label=name, imported=True, generated=True)
    return out
