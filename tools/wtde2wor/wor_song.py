"""Construccion de un PAK de cancion de WoR a partir de una cancion de World Tour / WTDE.

Modo "reemplazo": se toma el PAK de una cancion vanilla de WoR como plantilla y se sustituyen
  songs\\<destino>.note     <- chart convertido (wor_note)
  songs\\<destino>.mid.qb   <- notas de animacion / bateria / publico / luces de WT (traducidas)
                               + animaciones faciales (Band_PlayFacialAnim de <cancion>_performance)
  songs\\<destino>.perf     <- autocutcameras = cameras_notes de WT; momentcameras vacio
  scriptevents             <- solo los eventos de inicio (t=0) de la plantilla: se quitan los clips
                               (momentos de banda) de la cancion original
  .ska de WT               <- se anaden al PAK los que usan las animaciones faciales

Como funciona en WoR:
  * Camaras: seccion autocutcameras del .perf (gh5_camera_note = u32 tiempo ms, u16 duracion,
    u8 numero de camara). La numeracion es la misma que la de las notas de camara de WT.
  * Momentos de banda: clips (structs: musico + .ska + frames + camara) disparados por
    Band_PlayClip en scriptevents. Las definiciones de clips de WT no viajan en el PAK de la
    cancion (son globales del juego), asi que no se pueden reconstruir.
  * Bateria / guitarrista / publico / luces: arrays <cancion>_*_notes del .mid.qb, pares
    (tiempo, velocidad<<24 | nota<<16 | duracion).
"""
from __future__ import annotations

import copy
import struct

from qb_io import QbArray, QbFile, QbItem, QbMember, QbStruct
from wt_clips import PLAY_CLIP, build_clip_structs, ska_info
from wt_clipdefs import wt_clips
from ska_convert import convert_wt_to_wor
from band_loops import FORCE_IDLE, build_loop_events, wor_note_times, wt_sections
from wor_note import chart_to_note
from wor_pak import dechnk, read_pak, write_pak
from wt_chart import qbkey
from wt_song import EXT_QB, WtSong, load_chart, pak_entries

BS = chr(92)
SKA_TYPE = 0x745DCD45

# Notas que WoR usa en cada array (medido sobre las 167 canciones vanilla). Las de WT que caen
# fuera no tienen equivalente y se descartan.
WOR_NOTES = {
    "anim_notes": {36, 72, 74, 75, 76, 77, 78, 79, *range(85, 104), *range(109, 128)},
    "crowd_notes": {*range(81, 90), 95, 96, 97},
    "lightshow_notes": {19, *range(21, 28), *range(29, 38), *range(39, 54), 57, 58, 59, *range(61, 70),
                        *range(71, 86), 87, 88, *range(90, 108)},
}
SHARED_ANIM = ("anim_notes", "drums_notes", "crowd_notes", "lightshow_notes")

# Bateria: WT usa 60-70 (+13: 73-83) y WoR 30-50 (+56: 86-106). Tabla obtenida comparando nota a
# nota que pieza suena en el chart en ese instante (bombo 60->30, caja 64->35, hi-hat 65/66->39/40,
# platillos 67-69->42/44/45, toms 61-63->32-34).
_DRUM_LOW = {60: 30, 61: 32, 62: 33, 63: 34, 64: 35, 65: 39, 66: 40, 67: 42, 68: 44, 69: 45, 70: 49}
DRUM_MAP = {**_DRUM_LOW, **{k + 13: v + 56 for k, v in _DRUM_LOW.items()}}

# Numeros de camara que usa WoR en autocutcameras (medido en las 167 canciones vanilla).
WOR_CAMERAS = {8, 9, 10, 11, 13, 14, 15, 16, 18, 19, 20, 21, 23, 24, 25, 26, 28, 29, 30, 31, 38, 43, 45,
               46, 48, 57, 58, 60, 61, 62, 63, 68, 69, 74, 75}


def filter_events(values: list[int], allowed: set[int]) -> list[int]:
    out: list[int] = []
    for i in range(0, len(values) - 1, 2):
        if (values[i + 1] >> 16) & 0xFF in allowed:
            out += values[i:i + 2]
    return out


def map_drums(values: list[int]) -> list[int]:
    out: list[int] = []
    for i in range(0, len(values) - 1, 2):
        v = values[i + 1]
        n = DRUM_MAP.get((v >> 16) & 0xFF)
        if n is not None:
            out += [values[i], (v & 0xFF00FFFF) | (n << 16)]
    return out


def _wt_mid_qb(song: WtSong) -> QbFile:
    key = qbkey(f"songs{BS}{song.name}.mid.qb")
    for t, data in pak_entries(dechnk(song.pak_path.read_bytes())):
        if t == EXT_QB and len(data) > 28:
            q = QbFile(data)
            if q.file_key == key or q.find(qbkey(f"{song.name}_fretbars")):
                return q
    raise ValueError(f"No se encontro {song.name}.mid.qb en {song.pak_path}")


def wt_facial_events(wt: QbFile, wt_name: str) -> list:
    it = wt.find(qbkey(f"{wt_name}_performance"))
    if it is None or not isinstance(it.value, QbArray) or it.value.elem != "struct":
        return []
    k_scr = qbkey("scr")
    return [copy.deepcopy(e) for e in it.value.values if e.get(k_scr) == qbkey("Band_PlayFacialAnim")]


def build_mid_qb(template: bytes, target: str, wt: QbFile, wt_name: str) -> bytes:
    """Mantiene el orden/estructura del .mid.qb vanilla y rellena cada array con los datos de WT."""
    q = QbFile(template)
    for it in q.items:
        if it.kind != "array":
            continue
        suffix = next((s for s in SHARED_ANIM if it.name == qbkey(f"{target}_{s}")), None)
        if suffix is not None:
            src = wt.find(qbkey(f"{wt_name}_{suffix}"))
            vals: list[int] = []
            if src is not None and isinstance(src.value, QbArray) and src.value.elem == "int":
                if suffix == "drums_notes":
                    vals = map_drums(src.value.values)
                else:
                    vals = filter_events(src.value.values, WOR_NOTES[suffix])
            it.value = QbArray("int", vals) if vals else QbArray("empty", [])
        elif it.name == qbkey(f"{target}_lightshow"):
            src = wt.find(qbkey(f"{wt_name}_lightshow"))
            ok = src is not None and isinstance(src.value, QbArray) and src.value.elem == "struct"
            it.value = copy.deepcopy(src.value) if ok and src.value.values else QbArray("empty", [])
        elif it.name == qbkey(f"{target}_facial"):
            ev = wt_facial_events(wt, wt_name)
            it.value = QbArray("struct", ev) if ev else QbArray("empty", [])
        else:
            # Cualquier otro array temporizado de la cancion original quedaria desfasado.
            it.value = QbArray("empty", [])
    return q.build()


# Notas de camara de momento (momentcameras) en WoR. 3..6 eligen la camara del clip activo
# (nota 3 -> slot 0, 4 -> slot 1, ...); los clips se reconstruyen en wt_clips.
WOR_MOMENT_CAMERAS = {3, 4, 5, 6, 33, 35, 36, 64, 65, 94, 95, 96, 97, 98}
AUTO_CUT_MS = 3000  # duracion de cada corte cuando WT pide camara automatica (nota 110)


# Notas de camara de WT que en la pista de autocut de WoR tienen otro numero (igual que hace el
# conversor WT->GH5 de Addy's Guitar Hero Toolkit): 33-36 son planos de escenario que ademas van a la
# pista de momento; 40/41 son la orbita (74 en WoR).
AUTOCUT_REMAP = {33: 8, 34: 13, 35: 23, 36: 18, 40: 74, 41: 74}


def _cuts(wt_cameras: list[int], allowed: set[int], remap: dict | None = None) -> list[tuple[int, int, int]]:
    out = []
    for i in range(0, len(wt_cameras) - 1, 2):
        v = wt_cameras[i + 1]
        note = (v >> 16) & 0xFF
        if remap:
            note = remap.get(note, note)
        if note in allowed:
            out.append((wt_cameras[i], v & 0xFFFF, note))
    out.sort()
    return out


def camera_sequence(wt_cameras: list[int], song_end: int) -> list[tuple[int, int, int]]:
    """Secuencia continua de cortes al estilo WoR (cada corte dura hasta el siguiente).

    Los cortes explicitos de WT se conservan con sus tiempos. Donde WT no tiene corte explicito
    (zonas de camara automatica / clips) se rellena rotando las camaras que usa la propia cancion."""
    cuts = _cuts(wt_cameras, WOR_CAMERAS, AUTOCUT_REMAP)
    if not cuts:
        return []
    pool = []
    for _, _, n in cuts:
        if n not in pool:
            pool.append(n)
    seq: list[tuple[int, int]] = []  # (tiempo, camara)
    rot = 0
    starts = [c[0] for c in cuts] + [max(song_end, cuts[-1][0] + cuts[-1][1])]
    if cuts[0][0] > 0:
        starts.insert(0, 0)
        cuts.insert(0, (0, cuts[0][0], cuts[0][2]))
    for i, (t, length, note) in enumerate(cuts):
        nxt = starts[i + 1] if i + 1 < len(starts) else t + length
        seq.append((t, note))
        # Si el corte de WT termina mucho antes del siguiente, el hueco era camara automatica.
        t2 = t + max(length, AUTO_CUT_MS)
        while nxt - t2 >= AUTO_CUT_MS:
            cam = pool[rot % len(pool)]
            rot += 1
            if cam == seq[-1][1] and len(pool) > 1:
                cam = pool[rot % len(pool)]
                rot += 1
            seq.append((t2, cam))
            t2 += AUTO_CUT_MS
    out = []
    for i, (t, cam) in enumerate(seq):
        end = seq[i + 1][0] if i + 1 < len(seq) else max(song_end, t + AUTO_CUT_MS)
        out.append((t, min(end - t, 0xFFFF), cam))
    return out


# Pista de momento. Las camaras 3..6 son "la camara 0..3 DEL CLIP QUE ESTA SONANDO": sin un clip con
# camaras activo no hay nada que seguir y la camara cae al origen, debajo del escenario. Por eso:
#   * un corte 3..6 de WT solo se deja dentro de un clip activo con camaras suficientes;
#   * el resto del tiempo se usa una camara normal de escenario que exista tanto en la pista de autocut
#     como en la de momento (medido en las canciones vanilla). Las de autocut que no existen en la de
#     momento se cambian por la de su mismo grupo de planos (8-11, 13-16, 18-21, 23-26, 28-31...).
CLIP_CAMS = {3, 4, 5, 6}
WOR_MOMENT_TRACK = {3, 4, 5, 6, 8, 9, 13, 14, 18, 19, 23, 24, 26, 28, 29, 31, 33, 35, 36, 43, 45, 57,
                    60, 62, 63, 64, 65, 68, 69, 75, 94, 95, 96, 97, 98}
STAGE_CAMS = (WOR_MOMENT_TRACK & WOR_CAMERAS) - CLIP_CAMS
MOMENT_FOR_AUTO = {c: c for c in STAGE_CAMS}
MOMENT_FOR_AUTO.update({10: 8, 11: 9, 15: 13, 16: 14, 20: 18, 21: 19, 25: 24, 30: 28, 38: 43,
                        46: 45, 48: 43, 58: 57, 61: 60, 74: 75})


def clip_windows(events: list, clips: dict) -> list[tuple[int, int, set]]:
    """[(inicio, fin, {slots de camara})] de cada Band_PlayClip de los eventos finales."""
    out = []
    for ev in events:
        p = ev.get(K_PARAMS)
        if ev.get(K_SCR) != PLAY_CLIP or not isinstance(p, QbStruct):
            continue
        st = clips.get(p.get(K_CLIP))
        if not isinstance(st, QbStruct):
            continue
        cams = st.get(qbkey("cameras"))
        slots = ({c.get(qbkey("slot"), i) for i, c in enumerate(cams.values)}
                 if isinstance(cams, QbArray) and cams.elem == "struct" else set())
        n = len(slots)
        sf = p.get(qbkey("startframe"), 0) or 0
        ef = p.get(K_EF)
        if ef is None:
            ch = st.get(K_CHARS)
            ef = ch.values[0].get(K_EF) if isinstance(ch, QbArray) and ch.values else None
        if not ef or ef <= sf or n == 0:
            continue
        t = ev.get(K_TIME, 0)
        out.append((t, t + round((ef - sf) * 1000 / 30), slots))
    return out


def merge_moment(moment: list, auto: list, windows: list | None = None) -> list:
    """Pista de momento continua y siempre valida (ver arriba)."""
    windows = windows or []
    clip_cuts = []
    for t, ln, cam in moment:
        a, b = t, t + ln
        if cam not in CLIP_CAMS and cam < 90:  # plano de escenario de la pista de momento: va tal cual
            clip_cuts.append((t, ln, cam))
            continue
        for ws, we, slots in windows:
            slot = cam - 3 if cam < 90 else cam - 89  # 3+i / 90+: camaras del slot i del clip
            if slot not in slots:
                continue
            x, y = max(a, ws), min(b, we)
            if y - x >= 500:  # trozos de menos de medio segundo no compensan el corte
                clip_cuts.append((x, y - x, cam))
    clip_cuts.sort()
    # Sin solapes entre cortes de clip.
    mom = []
    for t, ln, cam in clip_cuts:
        if mom and t < mom[-1][0] + mom[-1][1]:
            pt, pl, pc = mom[-1]
            mom[-1] = (pt, t - pt, pc)
            if mom[-1][1] <= 0:
                mom.pop()
        mom.append((t, ln, cam))
    out = []
    for t, ln, cam in auto:
        cam = MOMENT_FOR_AUTO.get(cam, 8)
        a, b = t, t + ln
        for mt, ml, mc in mom:
            if mt + ml <= a or mt >= b:
                continue
            if mt > a:
                out.append((a, mt - a, cam))
            a = max(a, mt + ml)
        if b > a:
            out.append((a, b - a, cam))
    out += mom
    out.sort()
    return [(t, min(ln, 0xFFFF), c) for t, ln, c in out if ln > 0]


def rebuild_moment_track(perf: bytes, windows: list, wt_cameras: list[int], song_end: int) -> bytes:
    """Reescribe momentcameras con los clips que de verdad suenan en la cancion final."""
    auto = camera_sequence(wt_cameras, song_end)
    seq = merge_moment(_cuts(wt_cameras, WOR_MOMENT_CAMERAS), auto, windows)
    if not seq:
        return perf
    count = struct.unpack_from(">I", perf, 8)[0]
    out = bytearray(perf[:0x1C])
    off = 0x1C
    k_cam = qbkey("gh5_camera_note")
    for _ in range(count):
        name, n, typ = struct.unpack_from(">III", perf, off)
        size = 7 if typ == k_cam else 1000
        body = perf[off + 12: off + 12 + n * size]
        if name == qbkey("momentcameras"):
            n, body = len(seq), b"".join(struct.pack(">IHB", *c) for c in seq)
        out += struct.pack(">III", name, n, typ) + body
        off += 12 + struct.unpack_from(">I", perf, off + 4)[0] * size
    return bytes(out)


def build_perf(template: bytes, wt_cameras: list[int], song_end: int = 0) -> bytes:
    """Reescribe el .perf: autocutcameras y momentcameras salen de las camaras de WT."""
    count = struct.unpack_from(">I", template, 8)[0]
    out = bytearray(template[:0x1C])
    off = 0x1C
    k_cam = qbkey("gh5_camera_note")
    auto = camera_sequence(wt_cameras, song_end)
    moment = _cuts(wt_cameras, WOR_MOMENT_CAMERAS)
    for _ in range(count):
        name, n, typ = struct.unpack_from(">III", template, off)
        size = 7 if typ == k_cam else 1000
        body = template[off + 12: off + 12 + n * size]
        next_off = off + 12 + n * size
        if name == qbkey("autocutcameras") and auto:
            n, body = len(auto), b"".join(struct.pack(">IHB", *c) for c in auto)
        elif name == qbkey("momentcameras"):
            # WoR: la pista de momento cubre casi toda la cancion (~85%): camaras de clip (3..6)
            # donde WT las pide y, en los huecos, los cortes normales de la secuencia.
            seq = merge_moment(moment, auto)
            n, body = len(seq), b"".join(struct.pack(">IHB", *c) for c in seq)
        out += struct.pack(">III", name, n, typ) + body
        off = next_off
    if off != len(template):
        raise ValueError(f".perf con formato inesperado ({off} != {len(template)})")
    return bytes(out)


K_SCR, K_TIME, K_PARAMS = qbkey("scr"), qbkey("time"), qbkey("params")
K_CLIP, K_EF, K_TF, K_NAME = qbkey("clip"), qbkey("endframe"), qbkey("timefactor"), qbkey("name")
K_CHARS = qbkey("characters")
PLAY_LOOP = qbkey("Band_PlayLoop")


def fix_clip_events(events: list, clips: dict) -> list:
    """Deja los Band_PlayClip como los de WoR.

    * endframe siempre presente (sin el, el clip no tiene final y el musico queda sin animacion).
    * timefactor = 1 (todos los clips vanilla lo llevan).
    * Al TERMINAR el clip se vuelve a pedir el loop actual de cada musico que participa en el, como
      hace WoR (3000 de 3496 clips vanilla): sin eso se queda tieso / en pose T. Pedirlo en el mismo
      instante que el clip hace que primero se vea el loop y el clip entre de golpe."""
    out = []
    pending: list[QbStruct] = []
    current: dict[int, QbStruct] = {}   # musico -> ultimo Band_PlayLoop
    for ev in events:
        scr = ev.get(K_SCR)
        if scr == PLAY_LOOP:
            p = ev.get(K_PARAMS)
            if isinstance(p, QbStruct) and p.get(K_NAME) is not None:
                current[p.get(K_NAME)] = ev
            out.append(ev)
            continue
        if scr != PLAY_CLIP or not isinstance(ev.get(K_PARAMS), QbStruct):
            out.append(ev)
            continue
        ev = copy.deepcopy(ev)
        p = ev.get(K_PARAMS)
        st = clips.get(p.get(K_CLIP))
        chars = st.get(K_CHARS) if isinstance(st, QbStruct) else None
        if isinstance(chars, QbArray) and chars.values:
            # Vanilla: el evento lleva el mismo tramo que el clip (1504 de 1504).
            if p.get(qbkey("startframe")) is None:
                p.members.append(QbMember("int", qbkey("startframe"),
                                          chars.values[0].get(qbkey("startframe")) or 0))
            if p.get(K_EF) is None:
                p.members.append(QbMember("int", K_EF, chars.values[0].get(K_EF)))
        if p.get(K_TF) is None:
            p.members.append(QbMember("int", K_TF, 1))
        out.append(ev)
        st = clips.get(p.get(K_CLIP))
        chars = st.get(K_CHARS) if isinstance(st, QbStruct) else None
        members = {c.get(K_NAME) for c in chars.values} if isinstance(chars, QbArray) else set()
        sf, ef = p.get(qbkey("startframe"), 0) or 0, p.get(K_EF)
        if ef is None or ef <= sf:
            continue
        end = ev.get(K_TIME, 0) + round((ef - sf) * 1000 / 30)
        for who in members:
            loop = current.get(who)
            if loop is None:
                continue
            again = copy.deepcopy(loop)
            for m in again.members:
                if m.name == K_TIME:
                    m.value = end
            pending.append(again)
    # Loops de fin de clip: se insertan en orden de tiempo; si antes de ese momento el musico ya
    # recibe otro loop o clip, el de fin de clip sobra.
    out.sort(key=lambda e: e.get(K_TIME, 0))
    final = list(out)
    for lp in pending:
        who, t = lp.get(K_PARAMS).get(K_NAME), lp.get(K_TIME, 0)
        superseded = any(e.get(K_SCR) == PLAY_LOOP and isinstance(e.get(K_PARAMS), QbStruct)
                         and e.get(K_PARAMS).get(K_NAME) == who and t - 2000 <= e.get(K_TIME, 0) <= t
                         for e in out)
        if not superseded:
            final.append(lp)
    final.sort(key=lambda e: e.get(K_TIME, 0))
    return final


def filter_scriptevents(data: bytes, target: str, extra_events: list | None = None,
                        clips: dict | None = None, loop_builder=None) -> bytes:
    """Deja solo los eventos de arranque (t=0) de la plantilla, anade los eventos de la cancion de WT
    (clips, etc.) y las definiciones de clip reconstruidas como structs globales."""
    q = QbFile(data)
    k_time = qbkey("time")
    it = q.find(qbkey(f"{target}_scriptevents"))
    if it is not None and isinstance(it.value, QbArray) and it.value.elem == "struct":
        keep = [s for s in it.value.values if s.get(k_time, 0) == 0]
        keep += [copy.deepcopy(e) for e in (extra_events or [])]
        if loop_builder is not None:
            keep += loop_builder(it.value.values)
        keep.sort(key=lambda e: e.get(k_time, 0))
        keep = fix_clip_events(keep, clips or {})
        it.value = QbArray("struct", keep) if keep else QbArray("empty", [])
    for name, st in (clips or {}).items():
        old = q.find(name)
        if old is not None:
            old.kind, old.value = "struct", st
        else:
            q.items.insert(0, QbItem("struct", name, st))
    return q.build()


# Eventos de WT que se pasan a WoR: solo scripts que las canciones vanilla de WoR usan. WT tiene
# otros (Band_PlayIdle, Band_SetStrumStyle, Band_ForceAllToIdle, Band_SetAnimTempo...) que en WoR
# dejan al musico quieto en la pose idle (cantante tieso estirando la mano hacia el microfono).
# Scripts de banda de WT que WoR tambien define (comprobado en qb.pab) y que pasan tal cual.
# Band_SetStrumStyle no existe en WoR. Band_PlayFacialAnim va al array _facial aparte y Band_PlayLoop
# se trata en normalize_wt_loop.
WOR_EVENT_SCRIPTS = {PLAY_CLIP} | {qbkey(n) for n in (
    "Band_ChangeFacialAnims", "CameraCutsEffect_FOVPulse", "SetSongHandCamParams", "Band_ShowMic",
    "Band_HideMic", "Band_ShowMic_Stand", "Band_HideMic_stand", "Band_ShowMicStand", "Band_HideMicStand",
    "Band_ShowMic_microphone", "Band_HideMic_microphone", "Band_PlayIdle", "Band_ForceAllToIdle",
    "Band_SetAnimTempo", "Band_ClearAnimTempo", "Band_EnableAutoChords", "Band_DisableAutoChords",
    "Band_EnableAutoFret", "Band_DisableAutoFret", "Band_EnableAutoStrums", "Band_DisableAutoStrums",
    "Band_SetIKChainTarget", "Band_MoveToNode", "Band_PlayRockinFacialAnim")} | {
    0x754E711B, 0x3B948900, 0x4EF6985B}  # sin nombre conocido; presentes en vanilla / qb.pab
_LOOP_A, _LOOP_B = 0xAA721F56, 0xB58B7593
_WOR_LOOPS: set | None = None


def wor_loop_anims(assets_songs=None) -> set:
    """Animaciones de loop que existen en WoR (las que usan sus canciones). Se cachean en disco."""
    global _WOR_LOOPS
    if _WOR_LOOPS is not None:
        return _WOR_LOOPS
    import json
    from pathlib import Path
    cache = Path(__file__).with_name("wor_loops.json")
    if cache.exists():
        _WOR_LOOPS = set(json.loads(cache.read_text()))
        return _WOR_LOOPS
    root = Path(assets_songs) if assets_songs else (
        Path(__file__).resolve().parent.parent.parent / "re-wor" / "assets" / "data" / "songs")
    found = set()
    for p in root.glob("*_song.pak.xen"):
        try:
            d = p.read_bytes()
            ents = read_pak(dechnk(d) if d[:4] == b"CHNK" else d)[0]
        except Exception:
            continue
        for e in ents:
            if e.type != EXT_QB:
                continue
            try:
                items = QbFile(e.data).items
            except Exception:
                continue
            for it in items:
                if isinstance(it.value, QbArray) and it.value.elem == "struct":
                    for ev in it.value.values:
                        if ev.get(qbkey("scr")) == qbkey("Band_PlayLoop") and isinstance(ev.get(qbkey("params")), QbStruct):
                            pr = ev.get(qbkey("params"))
                            found.update(x for x in (pr.get(_LOOP_A), pr.get(_LOOP_B)) if x)
    if found:
        cache.write_text(json.dumps(sorted(found)))
    _WOR_LOOPS = found
    return found


_ROLE_LOOPS: dict | None = None


def wor_role_loops(assets_songs=None) -> dict:
    """{musico: {anim_A: [[(tipo, nombre, valor)...], ...]}}: para cada musico y cada animacion que WoR
    usa como cuerpo (slot A) de un Band_PlayLoop, TODAS las combinaciones de parametros vanilla (anim B
    que la acompana, mezcla...), la mas frecuente primero. Se cachea en wor_role_loops.json."""
    global _ROLE_LOOPS
    if _ROLE_LOOPS is not None:
        return _ROLE_LOOPS
    import json
    from collections import Counter
    from pathlib import Path
    cache = Path(__file__).with_name("wor_role_loops.json")
    if cache.exists():
        raw = json.loads(cache.read_text())
        _ROLE_LOOPS = {int(r): {int(a): [[tuple(m) for m in combo] for combo in combos]
                                for a, combos in d.items()} for r, d in raw.items()}
        return _ROLE_LOOPS
    root = Path(assets_songs) if assets_songs else (
        Path(__file__).resolve().parent.parent.parent / "re-wor" / "assets" / "data" / "songs")
    count: dict = {}
    k_name = qbkey("name")
    for p in root.glob("*_song.pak.xen"):
        try:
            d = p.read_bytes()
            ents = read_pak(dechnk(d) if d[:4] == b"CHNK" else d)[0]
        except Exception:
            continue
        for e in ents:
            if e.type != EXT_QB:
                continue
            try:
                items = QbFile(e.data).items
            except Exception:
                continue
            for it in items:
                if not (isinstance(it.value, QbArray) and it.value.elem == "struct"):
                    continue
                for ev in it.value.values:
                    pr = ev.get(qbkey("params"))
                    if ev.get(qbkey("scr")) != PLAY_LOOP or not isinstance(pr, QbStruct):
                        continue
                    who, a = pr.get(k_name), pr.get(_LOOP_A)
                    if who is None or a is None:
                        continue
                    key = tuple((m.kind, m.name, m.value) for m in pr.members if m.name != k_name)
                    count.setdefault(who, {}).setdefault(a, Counter())[key] += 1
    out = {who: {a: [list(k) for k, _ in c.most_common()] for a, c in d.items()} for who, d in count.items()}
    if out:
        cache.write_text(json.dumps({str(r): {str(a): ms for a, ms in d.items()} for r, d in out.items()}))
    _ROLE_LOOPS = out
    return out


def normalize_wt_loop(ev):
    """Band_PlayLoop de WT para WoR, o None.

    Los parametros son {name: musico, male: anim, female: anim}: la animacion para un musico hombre y
    la de una mujer. Se respetan las de la cancion; si una de las dos no existe en WoR se usa la otra
    para ambos, y si no existe ninguna el loop se descarta (lo cubren los loops de la plantilla)."""
    pr = ev.get(qbkey("params"))
    if not isinstance(pr, QbStruct) or pr.get(qbkey("name")) is None:
        return None
    known = wor_loop_anims()
    male, female = pr.get(_LOOP_A), pr.get(_LOOP_B)
    ok_m, ok_f = male in known, female in known
    if not ok_m and not ok_f:
        return None
    ev = copy.deepcopy(ev)
    pr = ev.get(qbkey("params"))
    have_f = False
    for m in pr.members:
        if m.name == _LOOP_A and not ok_m:
            m.value = female
        elif m.name == _LOOP_B:
            have_f = True
            if not ok_f:
                m.value = male
    if not have_f:
        pr.members.append(QbMember("qbkey", _LOOP_B, male if ok_m else female))
    return ev


def wt_event_ok(ev, clips: dict) -> bool:
    scr = ev.get(qbkey("scr"))
    params = ev.get(qbkey("params"))
    if scr == PLAY_CLIP:
        return isinstance(params, QbStruct) and params.get(qbkey("clip")) in clips
    if scr == qbkey("Band_PlayLoop"):
        return normalize_wt_loop(ev) is not None
    return scr in WOR_EVENT_SCRIPTS


_LOOP_COMPANIONS: dict | None = None


def loop_companions(assets_songs=None) -> dict:
    """Para cada animacion de loop, las que la acompanan en las listas de precarga del .perf.

    El .perf de WoR tiene 4 listas (250 claves): 0, 2 y 3 identicas con los loops 'A' (primer
    parametro de Band_PlayLoop) seguidos de sus acompanantes; la 1 con los 'B'. En las 94 canciones
    vanilla, el 100% de los loops que usan los eventos esta en esas listas: es lo que el juego precarga
    del archivo de loops. Los acompanantes son siempre los mismos para cada animacion (0 conflictos),
    asi que se aprenden de las canciones del juego del usuario y se cachean en disco."""
    global _LOOP_COMPANIONS
    if _LOOP_COMPANIONS is not None:
        return _LOOP_COMPANIONS
    import json
    from pathlib import Path
    cache = Path(__file__).with_name("wor_loop_companions.json")
    if cache.exists():
        raw = json.loads(cache.read_text())
        _LOOP_COMPANIONS = {k: {int(a, 16): v for a, v in raw[k].items()} for k in ("A", "B")}
        return _LOOP_COMPANIONS
    root = Path(assets_songs) if assets_songs else (
        Path(__file__).resolve().parent.parent.parent / "re-wor" / "assets" / "data" / "songs")
    songs = []
    for pth in root.glob("*_song.pak.xen"):
        name = pth.name[: -len("_song.pak.xen")].lower()
        try:
            d = pth.read_bytes()
            ents = read_pak(dechnk(d) if d[:4] == b"CHNK" else d)[0]
        except Exception:
            continue
        perf = next((e.data for e in ents if e.fullname == qbkey(f"songs{BS}{name}.perf")), None)
        evs = None
        for e in ents:
            if e.type == EXT_QB:
                try:
                    it = QbFile(e.data).find(qbkey(f"{name}_scriptevents"))
                except Exception:
                    it = None
                if it is not None and isinstance(it.value, QbArray):
                    evs = it.value.values
        if perf is None or evs is None:
            continue
        songs.append((perf_lists(perf), loop_pairs(evs)))
    heads = {"A": {a for _, ev in songs for a, _ in ev}, "B": {b for _, ev in songs for _, b in ev}}
    comp: dict = {"A": {}, "B": {}}
    for lists, _ in songs:
        if len(lists) < 2:
            continue
        for key, lst in (("A", lists[0]), ("B", lists[1])):
            cur = None
            for x in lst:
                if x in heads[key]:
                    cur = comp[key].setdefault(x, [])
                    cur.clear()
                elif cur is not None:
                    cur.append(x)
            # (cada cabeza vuelve a escribir sus acompanantes: en vanilla siempre son los mismos)
    if comp["A"]:
        cache.write_text(json.dumps({k: {"%08x" % a: v for a, v in comp[k].items()} for k in comp}))
    _LOOP_COMPANIONS = comp
    return comp


_K_CAM_NOTE = qbkey("gh5_camera_note")


def perf_lists(perf: bytes) -> list[list[int]]:
    out, off = [], 0x1C
    for _ in range(struct.unpack_from(">I", perf, 8)[0]):
        name, n, typ = struct.unpack_from(">III", perf, off)
        if typ != _K_CAM_NOTE:
            out.append([x for x in struct.unpack_from(">250I", perf, off + 12) if x])
        off += 12 + n * (7 if typ == _K_CAM_NOTE else 1000)
    return out


def loop_pairs(events) -> list[tuple[int, int]]:
    out = []
    for ev in events:
        if ev.get(qbkey("scr")) == qbkey("Band_PlayLoop") and isinstance(ev.get(qbkey("params")), QbStruct):
            p = ev.get(qbkey("params"))
            out.append((p.get(0xAA721F56), p.get(0xB58B7593)))
    return out


def rebuild_perf_lists(perf: bytes, events) -> bytes:
    """Rehace las listas de precarga del .perf con TODOS los loops que usan los eventos."""
    comp = loop_companions()
    pairs = loop_pairs(events)
    built = {}
    for key, idx in (("A", 0), ("B", 1)):
        seq: list[int] = []
        for h in dict.fromkeys(p[idx] for p in pairs if p[idx]):
            seq += [h] + list(comp[key].get(h, []))
        if len(seq) > 250:
            raise ValueError(f"lista de precarga del .perf demasiado larga ({len(seq)} > 250)")
        built[key] = struct.pack(">250I", *(seq + [0] * (250 - len(seq))))
    d = bytearray(perf)
    off, j = 0x1C, 0
    for _ in range(struct.unpack_from(">I", d, 8)[0]):
        name, n, typ = struct.unpack_from(">III", d, off)
        if typ != _K_CAM_NOTE:
            if n == 1:
                d[off + 12:off + 1012] = built["B" if j == 1 else "A"]
            j += 1
        off += 12 + n * (7 if typ == _K_CAM_NOTE else 1000)
    return bytes(d)


def build_replacement_pak(template_pak: bytes, target: str, song: WtSong, facial: bool = True) -> bytes:
    """PAK de WoR (sin comprimir) para la cancion `target` con el contenido de `song`."""
    target = target.lower()
    chart = song.chart or load_chart(song)
    chart = copy.copy(chart)
    chart.name = target
    note = chart_to_note(chart)
    wt_qb = _wt_mid_qb(song)
    if not facial:
        it = wt_qb.find(qbkey(f"{song.name}_performance"))
        if it is not None:
            it.value = QbArray("empty", [])

    entries, last, first = read_pak(template_pak)
    k_note = qbkey(f"songs{BS}{target}.note")
    k_mid = qbkey(f"songs{BS}{target}.mid.qb")
    k_perf = qbkey(f"songs{BS}{target}.perf")
    cams_item = wt_qb.find(qbkey(f"{song.name}_cameras_notes"))
    wt_cams = (cams_item.value.values
               if cams_item is not None and isinstance(cams_item.value, QbArray) else [])
    ctx = entries[0].ctx if entries else qbkey(target)

    # Clips de banda (momentos): se reconstruyen a partir de los .ska de la cancion de WT.
    wt_entries = read_pak(song.pak_path.read_bytes())[0]
    perf_it = wt_qb.find(qbkey(f"{song.name}_performance"))
    perf_events = (perf_it.value.values if perf_it is not None and isinstance(perf_it.value, QbArray)
                   and perf_it.value.elem == "struct" else [])
    k_scr = qbkey("scr")
    facial_keys = {ev.get(qbkey("params")).get(qbkey("anim")) for ev in perf_events
                   if ev.get(k_scr) == qbkey("Band_PlayFacialAnim") and ev.get(qbkey("params")) is not None}
    skas = [ska_info(e.fullname, e.data) for e in wt_entries
            if e.type == SKA_TYPE and e.fullname not in facial_keys]
    # 1) Clips con definicion dentro del PAK de WT (la mayoria): traduccion directa, sin perder tramo,
    #    nodos, IK, manos ni camaras. 2) Los que usan clips globales del juego de origen (no viajan en
    #    el PAK) se reconstruyen a partir de sus .ska como antes.
    called = {ev.get(qbkey("params")).get(qbkey("clip")) for ev in perf_events
              if ev.get(k_scr) == PLAY_CLIP and isinstance(ev.get(qbkey("params")), QbStruct)}
    def_clips, def_skas, _lost = wt_clips(wt_entries, qbkey(f"{song.name}_fretbars"), called)
    rest = [ev for ev in perf_events
            if not (ev.get(k_scr) == PLAY_CLIP and ev.get(qbkey("params")).get(qbkey("clip")) in def_clips)]
    heur, heur_skas, _report = build_clip_structs(rest, wt_cams,
                                                  [x for x in skas if x and x.key not in def_skas], chart.vocals)
    clips = {**heur, **def_clips}
    clip_skas = set(heur_skas) | def_skas
    # Eventos de WT que no son faciales (esos van al array _facial): clips, fin de clip, microfono...
    # Un Band_PlayClip de un clip que no se pudo reconstruir se omite.
    extra = [normalize_wt_loop(ev) if ev.get(k_scr) == PLAY_LOOP else ev
             for ev in perf_events if wt_event_ok(ev, clips)]
    tmpl_note = next((e.data for e in entries if e.fullname == k_note), b"")
    wt_times = sorted(n.time for n in chart.notes.get(("guitar", "expert"), []))
    sections = wt_sections(wt_qb, song.name, chart.fretbars)
    idle_times = [ev.get(qbkey("time"), 0) for ev in perf_events if ev.get(k_scr) == FORCE_IDLE]

    def loop_builder(template_events):
        return build_loop_events(template_events, wor_note_times(tmpl_note) if tmpl_note else [],
                                 wt_times, sections, idle_times)

    for e in entries:
        if e.fullname == k_note:
            e.data = note
        elif e.fullname == k_mid:
            e.data = build_mid_qb(e.data, target, wt_qb, song.name)
        elif e.fullname == k_perf:
            end = chart.fretbars[-1] if chart.fretbars else 0
            e.data = build_perf(e.data, wt_cams, end)
        elif e.type == EXT_QB:
            try:
                q = QbFile(e.data)
            except Exception:
                continue
            if q.find(qbkey(f"{target}_scriptevents")) is not None:
                e.data = filter_scriptevents(e.data, target, extra, clips, loop_builder)

    # .ska que usan las animaciones faciales de WT: se anaden despues de los de la plantilla.
    k_params, k_anim = qbkey("params"), qbkey("anim")
    needed = set(clip_skas)
    for ev in wt_facial_events(wt_qb, song.name):
        params = ev.get(k_params)
        if params is not None and params.get(k_anim):
            needed.add(params.get(k_anim))
    have = {e.fullname for e in entries if e.type == SKA_TYPE}
    for e in wt_entries:
        if e.type == SKA_TYPE and e.fullname in needed and e.fullname not in have:
            # WT comprime los fotogramas de otra forma: leidos tal cual, los huesos salen estirados.
            try:
                e.data = convert_wt_to_wor(e.data)
            except ValueError:
                continue
            e.ctx = ctx
            entries.append(e)
            have.add(e.fullname)
    # Listas de precarga del .perf = loops que usan los eventos finales (si falta uno, ese musico se
    # queda en pose T / tieso: el juego no lo carga del archivo de loops).
    k_perf = qbkey(f"songs{BS}{target}.perf")
    final_events = None
    for e in entries:
        if e.type == EXT_QB:
            it = QbFile(e.data).find(qbkey(f"{target}_scriptevents"))
            if it is not None and isinstance(it.value, QbArray):
                final_events = it.value.values
    for e in entries:
        if e.fullname == k_perf and final_events is not None:
            e.data = rebuild_perf_lists(e.data, final_events)
            e.data = rebuild_moment_track(e.data, clip_windows(final_events, clips), wt_cams,
                                          chart.fretbars[-1] if chart.fretbars else 0)
    return write_pak(entries, header_area=first, last=last)
