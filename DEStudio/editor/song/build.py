"""Proyecto del editor -> PAK de la cancion (con copia de seguridad previa) + validacion."""
from __future__ import annotations

import datetime as _dt
import shutil

from ..core import paths
from ..core.names import qbkey
from ..core.qbjson import struct_from_json
from qb_io import QbArray, QbFile, QbItem, QbStruct
from wor_pak import PakEntry, chnk, write_pak

from .library import load_pak, song_path
from .project import BS, EXT_QB, SKA
from .tracks import NOTE_TRACKS, encode_notes, perf_write


def _time_of(ev: QbStruct) -> int:
    return ev.get(qbkey("time"), 0) or 0


def _write_qbs(ents, name: str, events, clips, notes, facial=None) -> None:
    fkey = qbkey(f"{name}_facial")
    facial_val = (QbArray("struct", facial) if facial else QbArray("empty", [])) if facial is not None else None
    has_facial = False
    for e in ents:
        if e.type != EXT_QB or facial_val is None:
            continue
        try:
            has_facial |= QbFile(e.data).find(fkey) is not None
        except Exception:  # noqa: BLE001
            pass
    for e in ents:
        if e.type != EXT_QB:
            continue
        try:
            q = QbFile(e.data)
        except Exception:  # noqa: BLE001
            continue
        # Cada pieza se escribe en el .qb donde esta: las notas suelen ir en el .mid.qb y los eventos +
        # definiciones de clip en el .qb de scriptevents.
        is_events = q.find(qbkey(f"{name}_scriptevents")) is not None
        touched = is_events
        if facial_val is not None and not has_facial and is_events:
            # la cancion no tenia array de caras: va junto a los eventos
            q.items.append(QbItem("array", fkey, facial_val))
            has_facial = True
        items = []
        for it in q.items:
            if is_events and isinstance(it.value, QbStruct) and isinstance(it.value.get(qbkey("characters")), QbArray):
                continue  # las definiciones de clip se reescriben todas en el .qb de eventos
            if it.name == qbkey(f"{name}_scriptevents"):
                it.value = QbArray("struct", events) if events else QbArray("empty", [])
            if it.name == fkey and facial_val is not None:
                it.value = facial_val
                touched = True
            for k in NOTE_TRACKS:
                if it.name == qbkey(f"{name}_{k}"):
                    vals = encode_notes(notes.get(k, []))
                    it.value = QbArray("int", vals) if vals else QbArray("empty", [])
                    touched = True
            items.append(it)
        if is_events:
            items += [QbItem("struct", k, v) for k, v in clips.items()]
        if touched:
            q.items = items
            e.data = q.build()


def _add_imported_skas(ents, clips, name: str, facial=()) -> list[str]:
    have = {e.fullname: e for e in ents}
    ctx = ents[0].ctx if ents else qbkey(name)
    used = set()
    for ev in facial:   # animaciones de cara (Band_PlayFacialAnim)
        par = ev.get(qbkey("params"))
        if isinstance(par, QbStruct) and isinstance(par.get(qbkey("anim")), int):
            used.add(par.get(qbkey("anim")))
    for st in clips.values():
        for key in ("characters", "cameras"):
            arr = st.get(qbkey(key))
            if isinstance(arr, QbArray) and arr.elem == "struct":
                used |= {c.get(qbkey("anim")) for c in arr.values}
    added = []
    imports = paths.CACHE / "imports"
    for f in imports.glob("0x*.ska") if imports.exists() else []:
        key = int(f.stem, 16)
        if key not in used:
            continue
        if key in have:   # camara regenerada (toma editada): se sustituye
            if have[key].data != (d := f.read_bytes()):
                have[key].data = d
                added.append(f.stem)
        else:
            ents.append(PakEntry(SKA, ctx, key, key, 0, 0, f.read_bytes(), 128))
            added.append(f.stem)
            have[key] = ents[-1]
    # Animaciones de cuerpo que los clips usan pero no estan en el PAK (p. ej. el loop del musico en las tomas
    # del editor): se copian de la biblioteca del juego (anim_loops) para que el clip sea autonomo, como los de
    # las canciones base (el juego solo busca las animaciones de clip en el PAK de la cancion).
    from ..band.anims import loop_ska_bytes
    for key in sorted(k for k in used if isinstance(k, int) and k not in have):
        d = loop_ska_bytes(key)
        if d is not None:
            ents.append(PakEntry(SKA, ctx, key, key, 0, 0, d, 128))
            have[key] = ents[-1]
            added.append(f"0x{key:08x}")
    return added


def build(proj: dict) -> dict:
    if not proj["song"].get("editable"):
        raise ValueError("Las canciones del juego base solo se pueden abrir como referencia.")
    p = song_path(proj["song"]["id"])
    name = proj["song"]["name"]
    ents, last, first, compressed = load_pak(p)
    events = sorted((struct_from_json(e) for e in proj["events"]), key=_time_of)
    clips = {int(k, 16): struct_from_json(v) for k, v in proj["clips"].items()}
    facial = [struct_from_json(e) for e in proj["facial"]] if "facial" in proj else None
    if facial is not None:
        facial.sort(key=_time_of)
    _write_qbs(ents, name, events, clips, proj["notes"], facial)

    from wor_song import rebuild_perf_lists
    for e in ents:
        if e.fullname == qbkey(f"songs{BS}{name}.perf"):
            e.data = rebuild_perf_lists(perf_write(e.data, proj["cameras"]), events)
    added = _add_imported_skas(ents, clips, name, facial or ())

    data = write_pak(ents, header_area=first, last=last)
    if compressed:
        data = chnk(data)
    # EDFX (efectos, objetos 3D y textos que dibuja re-wor): archivo de texto junto al PAK; sin nada, se borra
    edfx_path = p.with_name(f"{name}.edfx")
    edfx = proj.get("edfx") or ""
    stamp = _dt.datetime.now().strftime("%Y%m%d_%H%M%S")
    backup = paths.BACKUPS / f"{p.parent.parent.parent.name}__{p.name}.{stamp}"
    shutil.copy2(p, backup)
    p.write_bytes(data)
    if edfx:
        edfx_path.write_text(edfx, encoding="utf-8")
    elif edfx_path.exists():
        edfx_path.unlink()

    from validate import validate_song_pak
    try:
        checks = validate_song_pak(p, name)
    except Exception as ex:  # noqa: BLE001
        checks = [("error", f"no se pudo validar: {ex}")]
    return {"written": str(p), "backup": str(backup), "added_skas": added, "edfx": str(edfx_path) if edfx else "",
            "checks": [{"level": lvl, "text": t} for lvl, t in checks]}


def restore(song_id: str, backup_name: str) -> str:
    p = song_path(song_id)
    b = (paths.BACKUPS / backup_name).resolve()
    if not b.is_relative_to(paths.BACKUPS.resolve()) or not b.exists():
        raise ValueError("copia no encontrada")
    shutil.copy2(b, p)
    return str(p)


def backups_for(song_id: str) -> list[str]:
    p = song_path(song_id)
    pref = f"{p.parent.parent.parent.name}__{p.name}."
    return sorted((b.name for b in paths.BACKUPS.glob(pref + "*")), reverse=True)
