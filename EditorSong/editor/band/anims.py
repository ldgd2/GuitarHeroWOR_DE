"""Biblioteca de animaciones del juego (pak/archive/anim_loops: 2343 .ska, la mayoria con nombre real).

Las capas aditivas (golpes de bateria '_hit_', animaciones parciales) no sirven solas para previsualizar,
asi que se marcan; el resto son loops/poses completos de los musicos.
"""
from __future__ import annotations

import re
import struct
from functools import lru_cache

from ..core.names import qbkey
from ..formats import pak as P
from ..formats.ska import ska_full
from .gamenames import names

SKA = qbkey(".ska")


@lru_cache(maxsize=1)
def library() -> list[dict]:
    a = P.archive("anim_loops")
    nm = names()
    out = []
    for e in a.entries:
        if e.type != SKA:
            continue
        n = nm.get(e.key)
        role = None
        if n:
            low = n.lower()
            for pre, r in (("guit", "guitarist"), ("bass", "bassist"), ("sing", "vocalist"), ("drum", "drummer"),
                           ("l_guit", "guitarist"), ("l_bass", "bassist"), ("l_sing", "vocalist"), ("l_drum", "drummer")):
                if low.startswith(pre):
                    role = r
        out.append({"id": f"0x{e.key:08x}", "name": n or f"0x{e.key:08x}", "role": role,
                    "additive": bool(n and re.search(r"_(m|q|s)?hit_|_hit$|_add", n.lower()))})
    out.sort(key=lambda x: (x["name"].startswith("0x"), x["name"].lower()))
    return out


@lru_cache(maxsize=1)
def _perm() -> dict:
    """pak/perm_anims: poses de manos (trastes gh_rocker_*_fret_NN_d, rasgueos strums/*), por clave del nombre
    completo (c:/gh6_burn/data/anims/....ska.xen)."""
    return {e.fullname: e for e in P.entries("pak/perm_anims.pak.xen") if e.type == SKA}


def anim_data(key: str) -> dict:
    k = int(key, 16)
    a = P.archive("anim_loops")
    e = a.by_key.get(k)
    if e is not None:
        return ska_full(a.read(e))
    pe = _perm().get(k)
    if pe is None:
        raise KeyError(key)
    return ska_full(pe.data)


@lru_cache(maxsize=1)
def hand_anims() -> dict[str, str]:
    """Nombre corto -> id de las poses de manos de perm_anims (trastes y rasgueos), para la vista previa."""
    from ..core.names import sdk_keys, short
    k = sdk_keys()
    out = {}
    for key in _perm():
        n = k.get(key)
        if n and ("/car/" in n):
            out[short(n)] = f"0x{key:08x}"
    return out


def loop_ska_bytes(key: int) -> bytes | None:
    """Bytes del .ska de la biblioteca del juego (anim_loops o perm_anims) con esa clave, o None."""
    a = P.archive("anim_loops")
    e = a.by_key.get(key)
    if e is not None:
        return a.read(e)
    pe = _perm().get(key)
    return pe.data if pe is not None else None
