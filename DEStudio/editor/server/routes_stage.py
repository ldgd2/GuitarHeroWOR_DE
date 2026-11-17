"""Rutas del escenario: lista, nodos y geometria (.glb)."""
from __future__ import annotations

from ..stage import nodes
from ..stage.cameras import venue_cameras
from ..stage.geometry import venue_glb
from .http import Binary, router


def _zone(req) -> str:
    z = req.arg("zone")
    if z not in nodes.list_venues():
        raise KeyError(z)
    return z


@router.get("/api/venues")
def venues(req):
    return nodes.list_venues()


@router.get("/api/venue")
def venue(req):
    return nodes.venue_nodes(_zone(req))


@router.get("/api/venue/glb")
def venue_model(req):
    return Binary(venue_glb(_zone(req)), "model/gltf-binary", cache=True)


@router.get("/api/venue/cameras")
def venue_cams(req):
    return venue_cameras(_zone(req))


# Modelos .glb que el usuario anade como objetos 3D de la vista previa (cache/props/)
def _prop_file(name: str):
    import re
    from ..core import paths
    if not re.fullmatch(r"[A-Za-z0-9_.\-]{1,80}\.glb", name):
        raise ValueError("nombre de modelo no valido (letras, numeros, _ . - y extension .glb)")
    d = paths.CACHE / "props"
    d.mkdir(exist_ok=True)
    return d / name


@router.post("/api/props/upload")
def prop_upload(req):
    if req.body[:4] != b"glTF":
        raise ValueError("el archivo no es un .glb (glTF binario)")
    f = _prop_file(req.arg("name"))
    f.write_bytes(req.body)
    return {"url": f"/api/props/file?name={f.name}"}


@router.get("/api/props/file")
def prop_file(req):
    f = _prop_file(req.arg("name"))
    if not f.exists():
        raise ValueError("modelo no encontrado")
    return Binary(f.read_bytes(), "model/gltf-binary")
