"""Canciones custom de GHWT:DE (carpeta con song.ini + Content/).

Dos formas de instalarlas:
  * NUEVAS (por defecto): todas las customs de las carpetas de canciones van al mod _wtde_customs, con
    su propia entrada en la lista del juego. Se convierten juntas porque la lista de canciones del juego
    es un unico archivo.
  * REEMPLAZO: una custom encima de una cancion vanilla (mod _PruebaReemplazo), util para comparar.
"""
from __future__ import annotations

import contextlib
import io
import json
from dataclasses import dataclass
from pathlib import Path

from modules.base import Detection, Log, Module, Report

NEW_MOD = "_wtde_customs"
REPLACE_MOD = "_PruebaReemplazo"


@dataclass
class SongItem:
    name: str           # checksum WT (p.ej. takemeout)
    internal: str       # nombre en WoR (wtde_takemeout)
    title: str
    artist: str
    folder: Path
    status: str         # convertida | pendiente | sin audio
    song: object        # WtSong


class _Tee(io.TextIOBase):
    def __init__(self, log: Log):
        import sys
        self.log, self.buf, self.real = log, "", sys.stdout

    def write(self, s: str) -> int:
        self.buf += s
        while "\n" in self.buf:
            line, self.buf = self.buf.split("\n", 1)
            if line.strip():
                with contextlib.redirect_stdout(self.real):  # si log es print, no volver aqui
                    self.log(line)
        return len(s)


def list_songs(ws) -> list[SongItem]:
    from build_customs import internal_name, source_hash
    from wt_song import find_songs
    state_p = ws.mods / NEW_MOD / "wtde_state.json"
    state = json.loads(state_p.read_text()) if state_p.exists() else {}
    out, seen = [], set()
    for src in ws.song_sources():
        if not src.is_dir():
            continue
        for s in find_songs(src):
            if s.kind != "custom" or s.name in seen:
                continue
            seen.add(s.name)
            internal = internal_name(s.name)
            if s.missing():
                status = "sin audio: " + ",".join(s.missing())
            elif state.get(internal) == source_hash(s) and (
                    ws.mods / NEW_MOD / "data" / "songs" / f"{internal}_song.pak.xen").exists():
                status = "convertida"
            else:
                status = "pendiente"
            base = s.pak_path.parent.parent if s.pak_path.parent.name.lower() == "content" else s.pak_path.parent
            out.append(SongItem(s.name, internal, s.title or s.name, s.artist, base, status, s))
    return out


def vanilla_targets(ws) -> list[str]:
    out = []
    for p in sorted((ws.assets / "data" / "songs").glob("*_song.pak.xen")):
        n = p.name[: -len("_song.pak.xen")]
        if not n.lower().startswith(("tut_", "debug", "z_")) and n.lower() != "suddendeathgh6career":
            out.append(n)
    return out


def build_new(ws, log: Log, force: bool = False) -> Report:
    """Convierte (o actualiza) todas las customs como canciones nuevas y las valida."""
    import build_customs
    from validate import validate_song_pak
    rep = Report("Canciones nuevas (mod %s)" % NEW_MOD, mod_folder=NEW_MOD)
    args = [str(p) for p in ws.song_sources() if p.is_dir()]
    args += ["--out", str(ws.mods / NEW_MOD), "--assets", str(ws.assets), "--keys", str(ws.keys)]
    if force:
        args.append("--force")
    with contextlib.redirect_stdout(_Tee(log)):
        code = build_customs.main(args)
    if code != 0:
        rep.error("el conversor termino con error")
    for item in list_songs(ws):
        pak = ws.mods / NEW_MOD / "data" / "songs" / f"{item.internal}_song.pak.xen"
        if not pak.exists():
            rep.error(f"{item.title}: no se genero")
            continue
        _add_validation(rep, item.title, validate_song_pak(pak, item.internal))
    return rep


def replace(ws, item: SongItem, target: str, log: Log) -> Report:
    from build_replace_test import replace_song
    from fsb_crypto import load_key_table
    from validate import validate_song_pak
    rep = Report(f"{item.title} -> encima de {target}", mod_folder=REPLACE_MOD)
    log(f"Convirtiendo {item.title} encima de {target}...")
    how = replace_song(item.song, target, ws.mods / REPLACE_MOD, load_key_table(str(ws.keys)), ws.assets)
    rep.info(f"volumen: {how}")
    pak = next(p for p in (ws.mods / REPLACE_MOD / "data" / "songs").iterdir()
               if p.name.lower() == f"{target.lower()}_song.pak.xen")
    _add_validation(rep, item.title, validate_song_pak(pak, target.lower()))
    return rep


def _add_validation(rep: Report, title: str, checks: list[tuple[str, str]]) -> None:
    for level, msg in checks:
        if level == "ok":
            rep.info(f"{title}: OK ({msg})")
        elif level == "info":
            rep.info(f"{title}: {msg}")
        elif level == "warn":
            rep.warn(f"{title}: {msg}")
        else:
            rep.error(f"{title}: {msg}")


class WtdeSongModule(Module):
    name = "wtde_song"
    kinds = ("song",)

    def detect(self, path: Path) -> Detection | None:
        path = Path(path)
        if not path.is_dir():
            return None
        inis = list(path.glob("song.ini")) + list(path.glob("*/song.ini"))
        if not inis:
            return None
        n = len(inis)
        label = path.name if n == 1 else f"{path.name} ({n} canciones)"
        return Detection(self.name, "song", path, label, True, [f"{n} cancion(es) de GHWT:DE"])

    def install(self, det: Detection, ws, log: Log, **opts) -> Report:
        ws.add_song_source(Path(det.source))
        return build_new(ws, log, force=opts.get("force", False))
