"""Canciones disponibles y su archivo: mods editables (re-wor/mods/<mod>/data/songs) y juego base (solo
lectura)."""
from __future__ import annotations

from pathlib import Path

from ..core import paths
from wor_pak import dechnk, read_pak


def list_songs() -> list[dict]:
    out = []
    for mod in sorted(p for p in paths.MODS.iterdir() if p.is_dir()):
        for f in sorted((mod / "data" / "songs").glob("*_song.pak.xen")):
            out.append({"id": f"mod:{mod.name}/{f.name}", "mod": mod.name, "name": f.name[:-13], "editable": True})
    for f in sorted((paths.ASSETS / "songs").glob("*_song.pak.xen")):
        n = f.name[:-13]
        if n.lower().startswith("debug"):
            continue
        out.append({"id": f"base:{f.name}", "mod": "(juego base)", "name": n, "editable": False})
    return out


def song_path(song_id: str) -> Path:
    kind, sep, rest = song_id.partition(":")
    if not sep:
        raise ValueError("id de cancion no valido")
    if kind == "mod":
        mod, _, fname = rest.partition("/")
        p = paths.MODS / mod / "data" / "songs" / fname
    elif kind == "base":
        p = paths.ASSETS / "songs" / rest
    else:
        raise ValueError(song_id)
    p = p.resolve()
    if not (p.is_relative_to(paths.MODS.resolve()) or p.is_relative_to(paths.ASSETS.resolve())):
        raise ValueError("ruta fuera del juego")
    return p


def load_pak(p: Path):
    """(entradas, entrada .last, offset del primer dato, comprimido)"""
    raw = p.read_bytes()
    compressed = raw[:4] == b"CHNK"
    ents, last, first = read_pak(dechnk(raw) if compressed else raw)
    return ents, last, first, compressed
