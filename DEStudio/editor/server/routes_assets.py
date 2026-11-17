"""Rutas de recursos: texturas PNG."""
from __future__ import annotations

from ..assets import textures
from .http import Binary, router


@router.get("/api/asset/tex")
def tex(req):
    size = min(2048, max(64, int(req.query.get("max", 1024))))
    return Binary(textures.png(req.arg("src"), int(req.arg("key"), 16), size), "image/png", cache=True)
