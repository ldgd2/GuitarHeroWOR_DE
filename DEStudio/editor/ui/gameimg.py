"""Imagenes de interfaz del juego (entradas .img de los PAK de data/pak) para el editor de interfaces.

Cada .img es una textura de Xbox 360 con la misma cabecera de 40 bytes que las del .tex (ver
formats/texture.py); los pixeles van al final de la entrada. El juego la nombra por la clave (QBKey) de su
nombre corto, que es lo que un SpriteElement pone en `texture`. Los nombres legibles salen de la tabla de
claves de tools/ghsdk (si no esta, se usa la clave en hexadecimal).

Indice por PAK en cache/ui_images/<pak>.json y PNG en cache/ui_images/<pak>/<clave>.png (se crean al
pedirlos; nunca se escribe nada en los datos del juego).
"""
from __future__ import annotations

import json
import struct
import threading
from pathlib import Path

from ..core import paths
from ..formats.texture import TexInfo, decode
from wor_pak import dechnk, read_pak

IMG_TYPE = 0xDAD5E950          # QBKey(".img")
CACHE = paths.CACHE / "ui_images"
KEYS_FILE = paths.ROOT / "tools" / "ghsdk" / "SDKCode" / "Checksums" / "ghwor_keys.txt"
_LOCK = threading.Lock()
_NAMES: dict[int, str] | None = None

# PAKs de muestras de bateria e instrumentos del estudio: no tienen interfaz
_SKIP = ("_kick", "_snare", "_hitom", "_floortom", "_hihat", "_cymbal", "musicstudio_kb_")
# los que el juego tiene siempre cargados en los menus: sus imagenes se pueden usar en cualquier pantalla
ALWAYS_LOADED = {"ui_shared", "global_textures"}
# recomendados primero en la lista
FIRST = ["ui_shared", "global_textures", "band_lobby", "cas_ui", "career_map", "songlist", "rewards",
         "team_select", "music_store", "song_summary_details", "leaderboard"]


def names() -> dict[int, str]:
    global _NAMES
    with _LOCK:
        if _NAMES is None:
            _NAMES = {}
            if KEYS_FILE.exists():
                for line in KEYS_FILE.read_text(encoding="latin-1", errors="replace").splitlines():
                    k, _, n = line.partition(" ")
                    if k.startswith("0x") and n and "/" not in n:
                        try:
                            _NAMES.setdefault(int(k, 16), n.strip())
                        except ValueError:
                            pass
        return _NAMES


def list_paks() -> list[str]:
    d = paths.ASSETS / "pak"
    out = sorted(p.name[:-8] for p in d.glob("*.pak.xen") if not any(s in p.name for s in _SKIP))
    first = [n for n in FIRST if n in out]
    return first + [n for n in out if n not in first]


def _pak_path(pak: str) -> Path:
    p = (paths.ASSETS / "pak" / f"{pak}.pak.xen").resolve()
    if not p.is_relative_to((paths.ASSETS / "pak").resolve()) or not p.is_file():
        raise KeyError(pak)
    return p


def _entries(pak: str):
    p = _pak_path(pak)
    d = dechnk(p.read_bytes())
    pab = p.with_name(p.name.replace(".pak.", ".pab."))
    if pab.exists():
        d += dechnk(pab.read_bytes())
    return [e for e in read_pak(d)[0] if e.type == IMG_TYPE]


def _texinfo(x: bytes) -> TexInfo:
    (_v, _hs, _fl, kind, key, _bw, _bh, _a, _aw, _ah, _b, levels, _bpp, _dxt, _z,
     _mips, d3d, size, data_off) = struct.unpack_from(">BBBBIHHHHHHBBBBIIII", x, 0)
    w0, w1, w2 = struct.unpack_from(">III", x, d3d + 28)
    return TexInfo(key, kind, (w2 & 0x1FFF) + 1, ((w2 >> 13) & 0x1FFF) + 1, levels, w1 & 0x3F, bool(w0 >> 31),
                   (w1 >> 6) & 3, data_off or (len(x) - size), size)


def index(pak: str) -> list[dict]:
    """[{key, name, w, h, fmt}] de las imagenes de un PAK (con cache)."""
    CACHE.mkdir(parents=True, exist_ok=True)
    f = CACHE / f"{pak}.json"
    if f.exists():
        return json.loads(f.read_text(encoding="utf-8"))
    nm = names()
    out = []
    for e in _entries(pak):
        try:
            t = _texinfo(e.data)
        except struct.error:
            continue
        out.append({"key": f"{e.name:08x}", "name": nm.get(e.name, ""), "w": t.width, "h": t.height,
                    "fmt": t.name})
    out.sort(key=lambda r: (r["name"] or "~" + r["key"]).lower())
    f.write_text(json.dumps(out), encoding="utf-8")
    return out


def png(pak: str, key: str) -> bytes:
    k = int(key, 16)
    out = CACHE / pak / f"{k:08x}.png"
    if out.exists():
        return out.read_bytes()
    out.parent.mkdir(parents=True, exist_ok=True)
    for e in _entries(pak):
        if e.name != k:
            continue
        img = decode(e.data, _texinfo(e.data), max_size=2048)
        if img is None:
            raise KeyError(key)
        img.save(out)
        return out.read_bytes()
    raise KeyError(key)
