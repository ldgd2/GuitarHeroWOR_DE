"""Acceso a los PAK del juego, con cache en memoria.

Dos variantes:
  * PAK normal (.pak.xen [+ .pab.xen]): tabla de entradas de 32 bytes con offsets relativos a la entrada
    (lo lee tools/wtde2wor/wor_pak.read_pak). Si hay .pab, los datos siguen a la cabecera.
  * Archivo (pak/archive/*.pak.xen + .pab.xen, p. ej. cas_pieces): entradas de 32 bytes
      u32 tipo, u32 offset en el .pab, u32 tamano guardado, u32 tamano real, u32 QBKey, u32 n, u32 -, u32 flags
    y cada dato es un bloque CHNK propio dentro del .pab (se descomprime al pedirlo).
"""
from __future__ import annotations

import struct
import threading
from collections import OrderedDict
from dataclasses import dataclass
from pathlib import Path

from ..core import paths
from wor_pak import PakEntry, dechnk, read_pak

_LOCK = threading.Lock()
_CACHE: "OrderedDict[str, list[PakEntry]]" = OrderedDict()
_MAX = 6


def _resolve(rel: str) -> Path:
    p = (paths.ASSETS / rel).resolve()
    if not p.is_relative_to(paths.ASSETS.resolve()):
        raise ValueError("ruta fuera de los datos del juego")
    return p


def entries(rel: str) -> list[PakEntry]:
    """Entradas de un PAK normal ('zones/Z_Barbarian.pak.xen'), en cache."""
    with _LOCK:
        if rel in _CACHE:
            _CACHE.move_to_end(rel)
            return _CACHE[rel]
    p = _resolve(rel)
    d = dechnk(p.read_bytes())
    pab = p.with_name(p.name.replace(".pak.", ".pab."))
    if pab.exists():
        d += dechnk(pab.read_bytes())
    ents = read_pak(d)[0]
    with _LOCK:
        _CACHE[rel] = ents
        while len(_CACHE) > _MAX:
            _CACHE.popitem(last=False)
    return ents


@dataclass
class ArchiveEntry:
    type: int
    offset: int
    size: int
    real_size: int
    key: int
    count: int
    flags: int


class Archive:
    """pak/archive/<nombre>.pak.xen + .pab.xen (lectura perezosa)."""

    def __init__(self, name: str):
        self.pak = _resolve(f"pak/archive/{name}.pak.xen")
        self.pab = _resolve(f"pak/archive/{name}.pab.xen")
        a = self.pak.read_bytes()
        self.entries: list[ArchiveEntry] = []
        for i in range(0, len(a) - 31, 32):
            t, o, sz, rsz, key, n, _x, fl = struct.unpack_from(">8I", a, i)
            if t == 0:
                break
            self.entries.append(ArchiveEntry(t, o, sz, rsz, key, n, fl))
        self.by_key = {e.key: e for e in self.entries}

    def read(self, e: ArchiveEntry) -> bytes:
        with open(self.pab, "rb") as f:
            f.seek(e.offset)
            raw = f.read(e.size)
        return dechnk(raw)


_ARCH: dict[str, Archive] = {}


def archive(name: str) -> Archive:
    with _LOCK:
        if name not in _ARCH:
            _ARCH[name] = Archive(name)
        return _ARCH[name]
