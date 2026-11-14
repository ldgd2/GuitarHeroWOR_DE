"""Escenario completo (zones/<Zona>.pak.xen) -> .glb con su geometria y texturas reales.

Cada zona trae una o varias escenas .scn (la principal y piezas sueltas) y sus .tex. Las coordenadas son
las del juego (Y arriba), las mismas que los nodos del .nqb, asi que musicos y camaras encajan sin
transformar nada. Resultado en cache/stage/<zona>.glb.
"""
from __future__ import annotations

from ..assets.meshes import add_scene
from ..core import paths
from ..core.names import qbkey
from ..formats import pak as P
from ..formats.glb import GlbBuilder
from ..formats.scene import read_scene

SCN = qbkey(".scn")
VERSION = 1


def venue_glb(zone: str) -> bytes:
    f = paths.CACHE / "stage" / f"{zone.lower()}_v{VERSION}.glb"
    if f.exists():
        return f.read_bytes()
    ents = P.entries(f"zones/{zone}.pak.xen")
    b = GlbBuilder()
    src = [f"zone/{zone}", "pak/pak/global_textures.pak.xen"]
    mats: dict = {}
    for e in ents:
        if e.type != SCN or len(e.data) < 0x200:
            continue
        try:
            sc = read_scene(e.data)
        except Exception:  # noqa: BLE001
            continue
        add_scene(b, sc, f"scn_{e.fullname:08x}", src, None, mats)
    data = b.build()
    f.parent.mkdir(parents=True, exist_ok=True)
    f.write_bytes(data)
    return data
