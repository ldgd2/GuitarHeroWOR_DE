"""Cancion de WoR (.pak.xen) -> proyecto del editor (JSON).

Que se edita y donde vive en el juego:
  eventos de banda      <cancion>_scriptevents (.qb)  Band_PlayClip / Band_PlayLoop / microfono...
  definiciones de clip  structs globales con characters=[...] y cameras=[...] (mismo .qb)
  cortes de camara      autocutcameras y momentcameras (.perf)
  luces / pirotecnia    <cancion>_lightshow_notes, banda _anim_notes, publico _crowd_notes (.mid.qb)
  animaciones           entradas .ska del PAK (cuerpo 128 huesos, camara 3 huesos)
Todo lo demas del PAK (chart, audio, faciales...) se copia sin tocar.
"""
from __future__ import annotations

import struct

from ..core import paths
from ..core.names import name_of, qbkey
from ..core.qbjson import struct_to_json
from qb_io import QbArray, QbFile, QbStruct
from wor_pak import PakEntry

from .library import load_pak, song_path
from .tracks import NOTE_TRACKS, decode_notes, note_section, perf_tracks

EXT_QB = qbkey(".qb")
SKA = 0x745DCD45
BS = "\\"


def ska_summary(e: PakEntry) -> dict:
    d = e.data
    dur = struct.unpack_from(">f", d, 0x28)[0] if len(d) >= 0x30 else 0.0
    bones = struct.unpack_from(">H", d, 0x2C)[0] if len(d) >= 0x30 else 0
    return {"id": f"0x{e.fullname:08x}", "label": name_of(e.fullname), "bones": bones,
            "duration": round(dur, 3), "frames": round(dur * 30), "camera": bones <= 4}


def load_project(song_id: str) -> dict:
    p = song_path(song_id)
    name = p.name[:-13].lower()
    ents, _last, _first, compressed = load_pak(p)
    proj = {"version": 1, "song": {"id": song_id, "name": name, "file": str(p), "compressed": compressed,
                                   "editable": song_id.startswith("mod:")},
            "events": [], "clips": {}, "cameras": {"auto": [], "moment": []},
            "notes": {k: [] for k in NOTE_TRACKS}, "fretbars": [], "gems": [], "bandmoments": [], "skas": [],
            "imports": {}}
    for e in ents:
        if e.type == SKA:
            proj["skas"].append(ska_summary(e))
        elif e.fullname == qbkey(f"songs{BS}{name}.perf"):
            proj["cameras"].update(perf_tracks(e.data))
        elif e.fullname == qbkey(f"songs{BS}{name}.note"):
            from band_loops import wor_note_times
            proj["gems"] = wor_note_times(e.data)
            proj["fretbars"] = wor_note_times(e.data, "fretbar")
            proj["bandmoments"] = note_section(e.data, "bandmoment")
        elif e.type == EXT_QB:
            try:
                q = QbFile(e.data)
            except Exception:  # noqa: BLE001
                continue
            for it in q.items:
                if it.name == qbkey(f"{name}_scriptevents") and isinstance(it.value, QbArray):
                    proj["events"] = [struct_to_json(s) for s in it.value.values] if it.value.elem == "struct" else []
                elif isinstance(it.value, QbStruct) and isinstance(it.value.get(qbkey("characters")), QbArray):
                    proj["clips"][f"0x{it.name:08x}"] = struct_to_json(it.value)
                else:
                    for k in NOTE_TRACKS:
                        if it.name == qbkey(f"{name}_{k}") and isinstance(it.value, QbArray):
                            proj["notes"][k] = decode_notes(list(it.value.values)) if it.value.elem == "int" else []
    # nombres legibles (diccionario del SDK) de clips y animaciones, solo para mostrar
    proj["names"] = {k: n for k in [*proj["clips"], *(x["id"] for x in proj["skas"])]
                     if not (n := name_of(int(k, 16))).startswith("0x")}
    proj["length"] = max([proj["fretbars"][-1] if proj["fretbars"] else 0] +
                         [t + ln for t, ln, _ in proj["cameras"]["auto"]] + [180000 if not proj["fretbars"] else 0])
    return proj


def ska_entry(song_id: str, ska_id: str) -> bytes:
    """Datos crudos de un .ska de la cancion (o importado al proyecto, en cache)."""
    key = int(ska_id, 16)
    imp = paths.CACHE / "imports" / f"0x{key:08x}.ska"
    if imp.exists():
        return imp.read_bytes()
    for e in load_pak(song_path(song_id))[0]:
        if e.type == SKA and e.fullname == key:
            return e.data
    raise KeyError(ska_id)


def import_ska(name: str, data: bytes) -> dict:
    """Un .ska de WT o de WoR -> formato WoR, guardado en cache hasta la compilacion."""
    from ska_convert import convert_wt_to_wor, is_wor
    if not is_wor(data):
        data = convert_wt_to_wor(data)
    key = qbkey(name)
    (paths.CACHE / "imports").mkdir(exist_ok=True)
    (paths.CACHE / "imports" / f"0x{key:08x}.ska").write_bytes(data)
    s = ska_summary(PakEntry(SKA, 0, key, key, 0, 0, data))
    s["label"] = name
    return s
