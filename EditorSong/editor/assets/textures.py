"""Texturas del juego servidas como PNG (decodificadas una vez y guardadas en cache/tex).

Una "fuente" agrupa contenedores .tex:
  zone/<Zona>            todos los .tex del PAK del escenario
  arch/<archivo>/<key>   un .tex de un archivo (p. ej. arch/cas_pieces/0x1234abcd)
  pak/<ruta>             todos los .tex de un PAK normal (p. ej. pak/pak/global_textures.pak.xen)
"""
from __future__ import annotations

import io
import re
import threading

from ..core import paths
from ..core.names import qbkey
from ..formats import pak as P
from ..formats.texture import TexInfo, decode, read_tex

TEX = qbkey(".tex")
_LOCK = threading.Lock()
_INDEX: dict[str, dict[int, tuple[bytes, TexInfo]]] = {}
SAFE = re.compile(r"^[A-Za-z0-9_./\-]+$")


def _containers(src: str) -> list[bytes]:
    kind, _, rest = src.partition("/")
    if kind == "zone":
        return [e.data for e in P.entries(f"zones/{rest}.pak.xen") if e.type == TEX]
    if kind == "pak":
        return [e.data for e in P.entries(rest) if e.type == TEX]
    if kind == "arch":
        name, _, key = rest.partition("/")
        a = P.archive(name)
        e = a.by_key.get(int(key, 16))
        return [a.read(e)] if e else []
    raise ValueError(src)


def index(src: str) -> dict[int, tuple[bytes, TexInfo]]:
    if not SAFE.match(src) or ".." in src:
        raise ValueError("fuente no valida")
    with _LOCK:
        if src in _INDEX:
            return _INDEX[src]
    out = {}
    for d in _containers(src):
        for t in read_tex(d):
            if t.size:
                out.setdefault(t.key, (d, t))
    with _LOCK:
        _INDEX[src] = out
    return out


def info(src: str) -> dict[int, TexInfo]:
    return {k: t for k, (_d, t) in index(src).items()}


def png(src: str, key: int, max_size: int = 1024) -> bytes:
    safe = re.sub(r"[^A-Za-z0-9_]", "_", src)
    f = paths.CACHE / "tex" / safe / f"{key:08x}_{max_size}.png"
    if f.exists():
        return f.read_bytes()
    d, t = index(src)[key]
    img = decode(d, t, max_size)
    if img is None:
        raise ValueError(f"formato de textura no soportado: {t.name}")
    buf = io.BytesIO()
    img.save(buf, "PNG", optimize=False, compress_level=1)
    f.parent.mkdir(parents=True, exist_ok=True)
    f.write_bytes(buf.getvalue())
    return buf.getvalue()
