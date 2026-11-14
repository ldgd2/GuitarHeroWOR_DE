"""PRUEBA: pone cada custom de song/ ENCIMA de una cancion vanilla (mod mods/_PruebaReemplazo).

Usa exactamente el mismo convertidor que build_customs.py (build_replacement_pak + curva de volumen),
pero con la cancion vanilla como plantilla y sin renombrar. Sirve para comparar en el juego la misma
custom suelta (lista de canciones nuevas) y reemplazando una vanilla.

Uso:  python build_replace_test.py           (genera el mod)
      python build_replace_test.py --off     (lo mueve a mods/_cache y deja el juego vanilla)
"""
from __future__ import annotations

import shutil
import sys
from pathlib import Path

from build_customs import BS, _load, qbkey, set_rms  # noqa: F401
from fsb_crypto import load_key_table, wor_decrypt, wor_encrypt, wt_decrypt_file
from wor_pak import chnk
from wor_song import build_replacement_pak
from wt_song import find_songs, load_chart

ROOT = Path(__file__).resolve().parent.parent.parent
ASSETS = ROOT / "re-wor" / "assets"
MOD = ROOT / "re-wor" / "mods" / "_PruebaReemplazo"
KEYS = ROOT / "re-wor" / "mods" / "_cache" / "wor_fsb_keys.bin"

# custom (nombre interno WT) -> cancion vanilla que reemplaza
TARGETS = {
    "inthend": "BohemianRhapsody",
    "takemeout": "SevenNationArmy",
    "throughthefire": "WhatDoIGet",
    "creep": "ImNotOk",
    "dontspeak": "Again",
    "fnfseptember": "Aqualung",
    "happytogether": "BatCountry",
    "missmurder": "BeenCaughtStealing",
}


def replace_song(song, target: str, mod_dir: Path = MOD, table: bytes | None = None,
                 assets: Path = ASSETS) -> str:
    """Convierte `song` (WtSong) encima de la cancion vanilla `target` (p.ej. "SevenNationArmy")."""
    table = table if table is not None else load_key_table(str(KEYS))
    if song.chart is None:
        load_chart(song)
    src_pak = next(p for p in (assets / "data" / "songs").iterdir()
                   if p.name.lower() == f"{target.lower()}_song.pak.xen")
    target = src_pak.name[: -len("_song.pak.xen")]
    name = target.lower()
    (mod_dir / "data" / "songs").mkdir(parents=True, exist_ok=True)
    (mod_dir / "data" / "music").mkdir(parents=True, exist_ok=True)
    pak = build_replacement_pak(_load_bytes(src_pak.read_bytes()), name, song)
    pak, how = set_rms(pak, name, song)
    (mod_dir / "data" / "songs" / src_pak.name).write_bytes(chnk(pak))
    for k in ("1", "2", "3", "preview"):
        src = song.audio.get(k) or song.audio.get("1")
        van = assets / "data" / "music" / f"{name}_{k}.fsb.xen"
        if not van.exists() or src is None:
            continue
        _, trailer = wor_decrypt(van.read_bytes(), table)
        (mod_dir / "data" / "music" / van.name).write_bytes(
            wor_encrypt(wt_decrypt_file(str(src)), table, trailer))
    if not (mod_dir / "mod.toml").exists():
        (mod_dir / "mod.toml").write_text(
            "\n".join([f'name = "{mod_dir.name}"', 'author = "wormod"', 'version = "1"', 'type = "song"',
                       'description = "Canciones custom encima de canciones vanilla (reemplazo)"', ""]),
            encoding="utf-8")
    return how


def main() -> int:
    if "--off" in sys.argv:
        if MOD.exists():
            dst = MOD.parent / "_cache" / (MOD.name + "_desactivado")
            shutil.rmtree(dst, ignore_errors=True)
            shutil.move(str(MOD), str(dst))
            print("desactivado ->", dst)
        return 0
    table = load_key_table(str(KEYS))
    songs = {s.name.lower(): s for s in find_songs(ROOT / "song") if s.kind == "custom"}
    for wt, target in TARGETS.items():
        song = songs.get(wt)
        if song is None:
            print(f"  ! {wt}: no esta en song/")
            continue
        how = replace_song(song, target, MOD, table)
        print(f"  {song.title} ({song.artist})  ->  reemplaza a {target}   [volumen: {how}]")
    return 0


def _load_bytes(d: bytes) -> bytes:
    from wor_pak import dechnk
    return dechnk(d) if d[:4] == b"CHNK" else d


if __name__ == "__main__":
    sys.exit(main())
