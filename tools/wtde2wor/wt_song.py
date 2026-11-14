"""Localiza y carga una cancion de Guitar Hero World Tour (vanilla, DLC o custom WTDE).

Entradas aceptadas (cualquier carpeta que contenga los archivos, a cualquier profundidad):
  <nombre>_song.pak.xen   o   a<nombre>_song.pak.xen   (vanilla / DLC / custom)
  <nombre>_1.fsb.xen, <nombre>_2.fsb.xen, <nombre>_3.fsb.xen, <nombre>_preview.fsb.xen
  song.ini (opcional, solo en customs WTDE: titulo, artista, etc.)
El PAK puede venir sin comprimir o en contenedores CHNK.
"""
from __future__ import annotations

import configparser
import struct
import zlib
from dataclasses import dataclass, field
from pathlib import Path

from wt_chart import Chart, QbFile, load_wt_chart, parse_qs, qbkey

EXT_QB = qbkey(".qb")
EXT_QS = {qbkey(e) for e in (".qs", ".qs.en", ".qs.de", ".qs.fr", ".qs.it", ".qs.es")}
EXT_LAST = {qbkey(".last"), 0x2CB3EF3B}


def dechnk(d: bytes) -> bytes:
    if d[:4] != b"CHNK":
        return d
    out = bytearray()
    off = 0
    while off < len(d) and d[off:off + 4] == b"CHNK":
        hdr, comp, nxt, _nc, _dec = struct.unpack_from(">IIIII", d, off + 4)
        raw = d[off + hdr: off + hdr + comp]
        try:
            out += zlib.decompress(raw, -15)
        except zlib.error:
            out += zlib.decompress(raw)
        if nxt == 0:
            break
        off += nxt
    return bytes(out)


def pak_entries(d: bytes) -> list[tuple[int, bytes]]:
    """[(tipo/extension QBKey, datos)] de un PAK de Neversoft."""
    res = []
    off = 0
    while off + 32 <= len(d):
        t, o, size, _ctx, _full, _name, _parent, _flags = struct.unpack_from(">IIIIIIII", d, off)
        if t in EXT_LAST or (size == 0 and o == 0):
            break
        start = off + o
        res.append((t, d[start:start + size]))
        off += 32
    return res


@dataclass
class WtSong:
    name: str                      # checksum de la cancion (p.ej. "takemeout")
    pak_path: Path
    audio: dict[str, Path] = field(default_factory=dict)   # "1","2","3","preview"
    title: str = ""
    artist: str = ""
    year: str = ""
    info: dict[str, str] = field(default_factory=dict)
    chart: Chart | None = None
    kind: str = "custom"           # "custom" (song.ini), "dlc" (prefijo a), "vanilla"

    def missing(self) -> list[str]:
        return [k for k in ("1", "2", "3", "preview") if k not in self.audio]


def _song_name_from_pak(p: Path) -> tuple[str, str]:
    stem = p.name[: -len("_song.pak.xen")]
    return stem, stem


def _find_audio(root: Path, name: str) -> dict[str, Path]:
    found: dict[str, Path] = {}
    wanted = {f"{name}_{k}.fsb.xen".lower(): k for k in ("1", "2", "3", "preview")}
    for f in root.rglob("*.fsb.xen"):
        k = wanted.get(f.name.lower())
        if k and k not in found:
            found[k] = f
    return found


def _chart_qb(entries: list[tuple[int, bytes]], name: str) -> bytes | None:
    """El QB del chart es el que contiene <nombre>_fretbars."""
    key = qbkey(f"{name}_fretbars")
    for t, data in entries:
        if t == EXT_QB and key.to_bytes(4, "big") in data:
            return data
    return None


def find_songs(folder: str | Path) -> list[WtSong]:
    root = Path(folder)
    songs: list[WtSong] = []
    for pak in sorted(root.rglob("*_song.pak.xen")):
        raw_name, _ = _song_name_from_pak(pak)
        base = pak.parent.parent if pak.parent.name.lower() == "content" else pak.parent
        ini = base / "song.ini"
        info: dict[str, str] = {}
        if ini.exists():
            cp = configparser.ConfigParser(interpolation=None, strict=False)
            cp.optionxform = str
            raw = ini.read_bytes()
            try:
                text = raw.decode("utf-8-sig")
            except UnicodeDecodeError:  # muchos song.ini de toolkits van en ANSI (Latin-1 / cp1252)
                text = raw.decode("cp1252", errors="replace")
            cp.read_string(text, source=str(ini))
            for sec in cp.sections():
                for k, v in cp.items(sec):
                    info[k] = v.strip()
        # Nombre interno: Checksum del song.ini, o el nombre del pak sin el prefijo "a" de DLC.
        candidates = []
        if info.get("Checksum"):
            candidates.append(info["Checksum"])
        candidates.append(raw_name)
        if raw_name.lower().startswith("a"):
            candidates.append(raw_name[1:])
        song = None
        entries = pak_entries(dechnk(pak.read_bytes()))
        for cand in candidates:
            if _chart_qb(entries, cand.lower()) is not None:
                song = cand
                break
        if song is None:
            continue
        s = WtSong(name=song.lower(), pak_path=pak, info=info)
        s.kind = "custom" if info else ("dlc" if raw_name.lower() != song.lower() else "vanilla")
        s.title = info.get("Title", song)
        s.artist = info.get("Artist", "")
        s.year = info.get("Year", "")
        s.audio = _find_audio(base if base.exists() else pak.parent, song)
        if not s.audio and root != base:
            s.audio = _find_audio(root, song)
        songs.append(s)
    return songs


def load_chart(song: WtSong) -> Chart:
    entries = pak_entries(dechnk(song.pak_path.read_bytes()))
    qs: dict[int, str] = {}
    for t, data in entries:
        if t in EXT_QS:
            qs.update(parse_qs(data))
    qb = _chart_qb(entries, song.name)
    if qb is None:
        raise ValueError(f"No se encontro el chart de {song.name} en {song.pak_path}")
    song.chart = load_wt_chart(qb, song.name, qs)
    return song.chart
