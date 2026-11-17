"""Nombres reales de los archivos del juego.

pak/dbg.pak.xen trae la lista de archivos de desarrollo ('/gh6_burn/data/models/car/male/x.skin', nombres de
animaciones...). Los archivos (cas_pieces, anim_loops) se indexan por QBKey de la ruta relativa a data/
con barras invertidas ('models\\car\\male\\x.skin') o, en las animaciones, por el nombre sin extension.
Se calcula una vez y se guarda en cache/game_names.json.
"""
from __future__ import annotations

import json
import re
import threading

from ..core import paths
from ..core.names import qbkey
from wor_pak import dechnk

BS = "\\"
_LOCK = threading.Lock()
_NAMES: dict[int, str] | None = None
VERSION = 1
ARCHIVES = ("cas_pieces", "cas_globalarchive", "anim_loops")


def names() -> dict[int, str]:
    global _NAMES
    with _LOCK:
        if _NAMES is not None:
            return _NAMES
        f = paths.CACHE / f"game_names_v{VERSION}.json"
        if f.exists():
            _NAMES = {int(k, 16): v for k, v in json.loads(f.read_text(encoding="utf-8")).items()}
            return _NAMES
        d = dechnk((paths.ASSETS / "pak" / "dbg.pak.xen").read_bytes())
        out: dict[int, str] = {}
        for m in re.finditer(rb"[A-Za-z0-9_\\/.\-]{4,}", d):
            s = m.group().decode("latin-1").replace("/", BS)
            i = s.lower().find("data" + BS)
            if i >= 0:
                s = s[i + 5:]
            s = s.strip(BS)
            for v in (s, s.lower()):
                out.setdefault(qbkey(v), s)
            stem = s.split(BS)[-1]
            if "." in stem:
                out.setdefault(qbkey(stem.rsplit(".", 1)[0]), stem.rsplit(".", 1)[0])
        from ..formats import pak as P
        wanted = set()
        for arch in ARCHIVES:
            wanted |= set(P.archive(arch).by_key)
        out = {k: v for k, v in out.items() if k in wanted}
        f.write_text(json.dumps({f"{k:08x}": v for k, v in out.items()}), encoding="utf-8")
        _NAMES = out
        return out


def name(key: int) -> str | None:
    return names().get(key)
