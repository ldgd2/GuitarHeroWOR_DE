"""Comprueba un PAK de cancion generado contra las reglas de las canciones vanilla de WoR.

Cada regla salio de comparar las canciones del juego con las convertidas; si algo falla aqui, en el
juego se ve (pose T, cantante tieso, camaras por defecto, crash...).
"""
from __future__ import annotations

import struct
import zlib
from pathlib import Path

import workspace  # noqa: F401  (rutas del conversor)
from audio_rms import rms_map_key
from build_customs import perf_section_key, scriptevents_qb_key
from qb_io import QbArray, QbFile, QbStruct
from wor_pak import dechnk, read_pak
from ska_convert import needs_seek_index
from wor_song import (WOR_CAMERAS, WOR_EVENT_SCRIPTS, WOR_MOMENT_TRACK, clip_windows, loop_pairs, perf_lists,
                      wor_loop_anims, wor_role_loops)


def _edfx_keys(path, name) -> set:
    """Claves de los eventos de sincronia de EDFX (EditorSong + re-wor), si la cancion trae su .edfx."""
    from pathlib import Path
    f = Path(path).with_name(f"{name}.edfx")
    if not f.exists():
        return set()
    return {int(ln.split()[1], 16) for ln in f.read_text(encoding="utf-8").splitlines() if ln.startswith("sync ")}


def qk(s: str) -> int:
    return (~zlib.crc32(s.lower().encode())) & 0xFFFFFFFF


BS = "\\"
SKA = 0x745DCD45
PLAY_CLIP, PLAY_LOOP, FACIAL = qk("Band_PlayClip"), qk("Band_PlayLoop"), qk("Band_PlayFacialAnim")
IK_VOCALIST = 0xFBF730F6


_VANILLA_SCRIPTS: set | None = None


def vanilla_scripts() -> set:
    """Scripts que usan los scriptevents de las canciones del juego base (cache en vanilla_scripts.json)."""
    global _VANILLA_SCRIPTS
    if _VANILLA_SCRIPTS is not None:
        return _VANILLA_SCRIPTS
    import json
    cache = Path(__file__).with_name("vanilla_scripts.json")
    if cache.exists():
        _VANILLA_SCRIPTS = set(json.loads(cache.read_text()))
        return _VANILLA_SCRIPTS
    root = Path(__file__).resolve().parent.parent.parent / "re-wor" / "assets" / "data" / "songs"
    found = set()
    for p in root.glob("*_song.pak.xen"):
        try:
            d = p.read_bytes()
            ents = read_pak(dechnk(d) if d[:4] == b"CHNK" else d)[0]
        except Exception:
            continue
        for e in ents:
            if e.type != qk(".qb"):
                continue
            try:
                items = QbFile(e.data).items
            except Exception:
                continue
            for it in items:
                if isinstance(it.value, QbArray) and it.value.elem == "struct":
                    found |= {ev.get(qk("scr")) for ev in it.value.values
                              if isinstance(ev, QbStruct) and isinstance(ev.get(qk("scr")), int)}
    if found:
        cache.write_text(json.dumps(sorted(found)))
    _VANILLA_SCRIPTS = found
    return found


def validate_song_pak(pak_path: Path, name: str) -> list[tuple[str, str]]:
    """[(nivel, texto)] nivel: ok | warn | error."""
    out: list[tuple[str, str]] = []
    name = name.lower()
    d = pak_path.read_bytes()
    ents = read_pak(dechnk(d) if d[:4] == b"CHNK" else d)[0]
    by_full = {e.fullname: e for e in ents}
    skas = {e.fullname: struct.unpack_from(">H", e.data, 0x2C)[0] for e in ents if e.type == SKA}

    for suffix in (".mid.qb", ".note", ".perf", "_rms.qd"):
        if qk(f"songs{BS}{name}{suffix}") not in by_full:
            out.append(("error", f"falta songs\\{name}{suffix}"))
    qd = by_full.get(qk(f"songs{BS}{name}_rms.qd"))
    if qd is not None and qd.data[4:8] != rms_map_key(name):
        out.append(("error", "_rms.qd con clave interna de otra cancion (crash al tocarla)"))

    clips: dict[int, QbStruct] = {}
    events: list = []
    se_file = None
    for e in ents:
        if e.type != qk(".qb"):
            continue
        for it in QbFile(e.data).items:
            if it.kind == "struct" and isinstance(it.value.get(qk("characters")), QbArray):
                clips[it.name] = it.value
            if it.name == qk(f"{name}_scriptevents"):
                se_file = e.fullname
                events = it.value.values if isinstance(it.value, QbArray) else []
    if se_file is None:
        out.append(("error", f"falta {name}_scriptevents (banda sin loops ni clips)"))
    elif se_file != scriptevents_qb_key(name):
        out.append(("error", "el .qb de scriptevents tiene otro nombre: el juego no lo ejecuta (pose T)"))

    loops = wor_loop_anims()
    allowed = WOR_EVENT_SCRIPTS | {PLAY_LOOP, FACIAL, 0x754E711B} | vanilla_scripts() | _edfx_keys(pak_path, name)
    bad_scr = {ev.get(qk("scr")) for ev in events if ev.get(qk("scr")) not in allowed}
    if bad_scr:
        out.append(("warn", f"eventos que ninguna cancion de WoR usa (el juego puede ignorarlos): "
                            f"{', '.join('%08x' % s for s in bad_scr)}"))
    bad_loops = sum(1 for ev in events if ev.get(qk("scr")) == PLAY_LOOP
                    and isinstance(ev.get(qk("params")), QbStruct)
                    and ev.get(qk("params")).get(0xAA721F56) not in loops)
    if bad_loops:
        out.append(("error", f"{bad_loops} loop(s) con animaciones que no existen en WoR (musico tieso)"))
    # Cada loop: animacion de hombre (male) y de mujer (female) que existan en WoR.
    known = wor_loop_anims()
    bad_loops = sum(1 for ev in events if ev.get(qk("scr")) == PLAY_LOOP
                    and isinstance(ev.get(qk("params")), QbStruct)
                    and not (ev.get(qk("params")).get(0xAA721F56) in known
                             and ev.get(qk("params")).get(0xB58B7593) in known))
    if bad_loops:
        out.append(("error", f"{bad_loops} loop(s) con animacion que no existe en WoR (pose T)"))
    t0 = {ev.get(qk("params")).get(qk("name")) for ev in events
          if ev.get(qk("scr")) == PLAY_LOOP and ev.get(qk("time"), 0) == 0}
    for who in ("guitarist", "bassist", "vocalist"):
        if qk(who) not in t0:
            out.append(("warn", f"{who} no tiene loop al empezar (pose T hasta el primer evento)"))

    n_clip = no_ef = no_loop_after = 0
    for ev in events:
        if ev.get(qk("scr")) != PLAY_CLIP:
            continue
        n_clip += 1
        p = ev.get(qk("params"))
        if p.get(qk("endframe")) is None:
            no_ef += 1
        st = clips.get(p.get(qk("clip")))
        if st is None:
            out.append(("error", f"clip {p.get(qk('clip')):08x} sin definicion"))
            continue
        t = ev.get(qk("time"), 0)
        for c in st.get(qk("characters")).values:
            who = c.get(qk("name"))
            if who == qk("drummer"):
                continue  # en WoR el bateria nunca recibe Band_PlayLoop (tampoco en vanilla)
            if not any(x.get(qk("scr")) == PLAY_LOOP and x.get(qk("params")).get(qk("name")) == who
                       and x.get(qk("time"), 0) >= t for x in events):
                no_loop_after += 1
    if no_ef:
        out.append(("error", f"{no_ef} clip(s) sin endframe"))
    if no_loop_after:
        out.append(("warn", f"{no_loop_after} musico(s) sin loop al acabar su clip"))

    no_index = sum(1 for e in ents if e.type == SKA and len(e.data) > 0x80
                   and struct.unpack_from(">I", e.data, 4)[0] == 0x20
                   and needs_seek_index(struct.unpack_from(">f", e.data, 0x28)[0])
                   and struct.unpack_from(">i", e.data, 0x0C)[0] <= 0)
    if no_index:
        out.append(("error", f"{no_index} animacion(es) de mas de 10 s sin indice de busqueda "
                             "(pose T / brazos estirados / camara fija)"))

    node_bad = 0
    sing = b"TRG_Geo_Camera_Performance_SING01"
    node = {qk("guitarist"): b"TRG_Geo_Camera_Performance_GUIT01",
            qk("bassist"): b"TRG_Geo_Camera_Performance_BASS01", qk("vocalist"): sing}
    used_clips = {ev.get(qk("params")).get(qk("clip")) for ev in events if ev.get(qk("scr")) == PLAY_CLIP}
    for name_, st in clips.items():
        if name_ not in used_clips:
            continue  # struct sobrante de la plantilla: no se reproduce
        cams = st.get(qk("cameras"))
        chars = st.get(qk("characters")).values
        if not isinstance(cams, QbArray) or not cams.values:
            continue
        sn = chars[0].get(qk("startnode")) or b""
        want = node.get(qk(sn.decode(errors="replace").split("_")[0]), sing)
        node_bad += sum(1 for c in cams.values if c.get(qk("name")) != want)
    if node_bad:
        out.append(("info", f"{node_bad} camara(s) de clip ancladas a otro musico que el primero (lo define la cancion)"))

    face_bad = cam_body = missing = ik_bad = 0
    for st in clips.values():
        for c in st.get(qk("characters")).values:
            a = c.get(qk("anim"))
            if a not in skas:
                missing += 1
            elif skas[a] <= 4:
                cam_body += 1
            if (c.get(qk("name")) == qk("vocalist")) != (c.get(qk("ik_targetL")) == IK_VOCALIST):
                ik_bad += 1
    if missing:
        out.append(("error", f"{missing} animacion(es) de clip que no estan en el PAK"))
    if cam_body:
        out.append(("error", f"{cam_body} animacion(es) de camara usadas como cuerpo (deformacion)"))
    if ik_bad > 10:
        out.append(("info", f"{ik_bad} musico(s) con IK de manos poco habitual (lo define la cancion)"))

    perf = by_full.get(qk(f"songs{BS}{name}.perf"))
    if perf is not None:
        t = perf.data
        cnt, off, song_len, moment = struct.unpack_from(">I", t, 8)[0], 0x1C, 0, 0
        for _ in range(cnt):
            nm, n, typ = struct.unpack_from(">III", t, off)
            size = 7 if typ == qk("gh5_camera_note") else 1000
            if size == 7:
                ent = [struct.unpack_from(">IHB", t, off + 12 + i * 7) for i in range(n)]
                allowed = WOR_CAMERAS if nm == qk("autocutcameras") else WOR_MOMENT_TRACK
                bad_ids = sorted({c for _, _, c in ent if c not in allowed})
                if bad_ids and nm in (qk("autocutcameras"), qk("momentcameras")):
                    out.append(("error", f"camaras {bad_ids} que esa pista de WoR no tiene "
                                         "(camara bajo el escenario o congelada)"))
                if nm == qk("autocutcameras"):
                    song_len = max((a + b for a, b, _ in ent), default=0)
                    if not ent:
                        out.append(("error", "sin cortes de camara (autocut vacio)"))
                elif nm == qk("momentcameras"):
                    moment = sum(b for _, b, _ in ent)
                    # Camaras 3..6 = camaras del clip que suena: sin clip activo caen bajo el escenario.
                    wins = clip_windows(events, clips)
                    orphan = sum(ln for t, ln, c in ent if 3 <= c <= 6 and not any(
                        a <= t + ln // 2 < b and c - 3 in k for a, b, k in wins))
                    total = sum(ln for _, ln, _ in ent) or 1
                    if orphan * 10 > total:
                        out.append(("warn", f"{100 * orphan // total}% de la pista de momento usa camaras de "
                                            "clip sin clip sonando (camara bajo el escenario)"))
            off += 12 + n * size
        if song_len and moment / song_len < 0.6:
            out.append(("warn", f"pista de camaras de momento cubre solo {100 * moment // song_len}% "
                                "(vanilla ~85%)"))
        # Secciones de precarga: nombre ligado a la cancion y todos los loops usados dentro.
        secs, off = [], 0x1C
        for _ in range(cnt):
            nm, n, typ = struct.unpack_from(">III", t, off)
            if typ != qk("gh5_camera_note"):
                secs.append(nm)
            off += 12 + n * (7 if typ == qk("gh5_camera_note") else 1000)
        if [s for i, s in enumerate(secs) if s != perf_section_key(name, i)]:
            out.append(("error", ".perf con secciones de precarga de otra cancion: el juego no las "
                                 "encuentra (banda en pose T / tieso)"))
        lists = perf_lists(t)
        if len(lists) >= 2:
            missing = {a for a, _ in loop_pairs(events) if a not in lists[0]} | \
                      {b for _, b in loop_pairs(events) if b not in lists[1]}
            if missing:
                out.append(("error", f"{len(missing)} animacion(es) de loop sin precargar en el .perf "
                                     "(ese musico queda en pose T / tieso)"))
    if not any(l == "error" for l, _ in out):
        out.insert(0, ("ok", f"{n_clip} clips, {len(events)} eventos, {len(skas)} animaciones"))
    return out
