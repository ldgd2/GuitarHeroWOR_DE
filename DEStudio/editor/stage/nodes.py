"""Mapa espacial de los escenarios (zones/Z_*.pak.xen).

Cada escenario trae sus NodeArray (.nqb) con nodos {name, pos, angles, class, type...}. Los que anclan a
los musicos y a las camaras de clip se llaman z_<escenario>_TRG_Geo_Camera_Performance_<SING|GUIT|BASS|DRUM>01
(verificado en Z_Barbarian: cantante (0, 0, 6.68), guitarra (4.97, 0, 6.18), bajo (-4.95, 0, 6.18),
bateria (0, 1.32, 1.63)). El resto de nodos (luces, pirotecnia, publico...) se devuelven como puntos con
su hash; si alguno se identifica se le pone nombre.
"""
from __future__ import annotations

import json
import struct

from ..core import paths
from ..core.names import qbkey
from qb_io import QbArray, QbFile

from ..formats import pak as P


NQB = qbkey(".nqb")
PERF_NODES = {"vocalist": "sing01", "guitarist": "guit01", "bassist": "bass01", "drummer": "drum01"}


def list_venues() -> list[str]:
    skip = {"global", "global_sfx", "z_in_game", "z_viewer", "z_screenshot", "z_credits", "z_gh6intro"}
    return sorted(p.name[:-8] for p in (paths.ASSETS / "zones").glob("*.pak.xen")
                  if p.name[:-8].lower() not in skip)


def _vec(data: bytes, ptr) -> list[float] | None:
    if not isinstance(ptr, int) or ptr + 16 > len(data):
        return None
    return [round(x, 4) for x in struct.unpack_from(">3f", data, ptr + 4)]


def venue_nodes(zone: str) -> dict:
    if zone not in list_venues():
        raise ValueError(zone)
    cache = paths.CACHE / f"venue_{zone.lower()}.json"
    if cache.exists():
        return json.loads(cache.read_text())
    zl = zone.lower()
    named = {qbkey(f"{zl}_TRG_Geo_Camera_Performance_{v}"): f"TRG_Geo_Camera_Performance_{v.upper()}"
             for v in PERF_NODES.values()}
    role_of = {qbkey(f"{zl}_TRG_Geo_Camera_Performance_{v}"): r for r, v in PERF_NODES.items()}
    nodes, perf = [], {}
    for e in P.entries(f"zones/{zone}.pak.xen"):
        if e.type != NQB:
            continue
        try:
            q = QbFile(e.data)
        except Exception:
            continue
        for it in q.items:
            if not (isinstance(it.value, QbArray) and it.value.elem == "struct"):
                continue
            for st in it.value.values:
                n = st.get(qbkey("name"))
                pos = _vec(e.data, next((m.value for m in st.members if m.name == qbkey("pos")), None))
                if n is None or pos is None:
                    continue
                ang = _vec(e.data, next((m.value for m in st.members if m.name == qbkey("angles")), None))
                node = {"id": f"0x{n:08x}", "name": named.get(n), "pos": pos, "angles": ang or [0, 0, 0],
                        "class": f"0x{(st.get(qbkey('class')) or 0):08x}"}
                nodes.append(node)
                if n in role_of:
                    perf[role_of[n]] = node
    out = {"zone": zone, "perf": perf, "nodes": nodes}
    cache.write_text(json.dumps(out))
    return out
