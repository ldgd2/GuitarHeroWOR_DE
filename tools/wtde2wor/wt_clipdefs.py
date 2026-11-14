"""Traduccion DIRECTA de las definiciones de clip ("momentos" de banda) de World Tour a Warriors of Rock.

Las canciones de WT / WTDE traen las definiciones de sus clips dentro del propio PAK (un .qb aparte del
chart). Aparecen en dos formatos:

  A) Formato GH5/WoR (canciones hechas con toolkits modernos):
       {characters=[{name, startnode, anim, startframe, endframe, timefactor, ik_targetl, ik_targetr,
                     strum, fret, chord}],
        cameras=[{slot, name, anim}], 65c1ecd3=[...], 1ff9c3f6=[...], e5c2ad23=<int>}
     Es lo mismo que usa WoR: se copia tal cual (solo se normaliza lo que falte).

  B) Formato nativo de WT:
       {anims={guitarist: <anim|none>, bassist: ..., vocalist: ..., drummer: ...},
        startnodes={<musico>: '<nodo>'},            (opcional; por defecto <musico>_start)
        cameras=[{name, anim, 9ad1dd52=[...], weight}], guitarist_cameras / bassist_cameras / ...,
        arms=..., af75e3aa=<int>}
     Se traduce: cada musico con anim distinta de `none` pasa a characters; la camara i de `cameras` es
     el slot i (medido en las canciones: la nota de momento 3+i suena mientras dura el clip).

Asi no se pierde nada de lo que la cancion pide: que musico participa, desde que nodo parte, que tramo
de la animacion se reproduce (startframe/endframe), el IK y las manos automaticas de cada uno (p.ej. un
bajista con las manos libres lleva ik=slave y strum/fret/chord=false; forzarle el IK al instrumento le
estira los brazos hacia el vacio) y cada camara con su slot y su nodo.
"""
from __future__ import annotations

import struct

from qb_io import QbArray, QbFile, QbMember, QbStruct
from wt_chart import qbkey

EXT_QB = qbkey(".qb")
SKA_TYPE = 0x745DCD45
NONE = qbkey("none")
ROLES = ("guitarist", "bassist", "vocalist", "drummer")
K = {w: qbkey(w) for w in (
    "characters", "cameras", "anims", "startnodes", "name", "startnode", "anim", "startframe",
    "endframe", "timefactor", "ik_targetl", "ik_targetr", "strum", "fret", "chord", "slot",
    "guitarist_cameras", "bassist_cameras", "vocalist_cameras", "drummer_cameras", "secondary_cameras")}
IK_SLAVE, IK_GUITAR = qbkey("slave"), qbkey("guitar")
TRUE = qbkey("true")
# Arrays de clip que WoR conoce (los vanilla llevan 65c1ecd3 siempre; 1ff9c3f6 y d91ab002 a veces).
CLIP_ARRAYS = (0x65C1ECD3, 0x1FF9C3F6, 0xD91AB002)
CHAR_ORDER = ("name", "startnode", "anim", "startframe", "endframe", "timefactor", "ik_targetl",
              "ik_targetr", "strum", "fret", "chord")
CAM_NODE = {"guitarist": b"TRG_Geo_Camera_Performance_GUIT01",
            "bassist": b"TRG_Geo_Camera_Performance_BASS01",
            "vocalist": b"TRG_Geo_Camera_Performance_SING01",
            "drummer": b"TRG_Geo_Camera_Performance_SING01"}
FPS = 30.0


def ska_frames(entries) -> dict[int, int]:
    """{anim: fotogramas a 30 fps} de los .ska del PAK de WT."""
    out = {}
    for e in entries:
        if e.type == SKA_TYPE and len(e.data) >= 0x30:
            out[e.fullname] = round(struct.unpack_from(">f", e.data, 0x28)[0] * FPS)
    return out


def raw_clip_defs(entries, chart_key: int) -> dict[int, QbStruct]:
    """Structs de clip (formato A o B) de los .qb del PAK de WT que no son el chart."""
    out = {}
    for e in entries:
        if e.type != EXT_QB:
            continue
        try:
            q = QbFile(e.data)
        except Exception:
            continue
        if q.find(chart_key) is not None:
            continue
        for it in q.items:
            if it.kind != "struct" or not isinstance(it.value, QbStruct):
                continue
            v = it.value
            if isinstance(v.get(K["characters"]), QbArray) or isinstance(v.get(K["anims"]), QbStruct):
                out[it.name] = v
    return out


def _qbkey_name(v) -> str | None:
    return next((r for r in ROLES if qbkey(r) == v), None)


def _character(role: str, node: bytes, anim: int, sf: int, ef: int, src: QbStruct | None) -> QbStruct:
    """Personaje de clip con el orden y los campos de WoR; lo que traiga la cancion manda."""
    vals = {"name": ("qbkey", qbkey(role)), "startnode": ("string", node), "anim": ("qbkey", anim),
            "startframe": ("int", sf), "endframe": ("int", ef), "timefactor": ("int", 1),
            "ik_targetl": ("qbkey", IK_SLAVE if role == "vocalist" else IK_GUITAR),
            "ik_targetr": ("qbkey", IK_SLAVE if role == "vocalist" else IK_GUITAR),
            "strum": ("qbkey", TRUE), "fret": ("qbkey", TRUE), "chord": ("qbkey", TRUE)}
    if src is not None:
        for k in ("timefactor", "ik_targetl", "ik_targetr", "strum", "fret", "chord"):
            m = next((m for m in src.members if m.name == K[k]), None)
            if m is not None:
                vals[k] = (m.kind, m.value)
    if role == "drummer":  # los vanilla del bateria no llevan manos automaticas
        for k in ("strum", "fret", "chord"):
            vals.pop(k)
    return QbStruct([QbMember(vals[k][0], K[k], vals[k][1]) for k in CHAR_ORDER if k in vals])


def _cam(slot: int, name: bytes, anim: int) -> QbStruct:
    return QbStruct([QbMember("int", K["slot"], slot), QbMember("string", K["name"], name),
                     QbMember("qbkey", K["anim"], anim)])


def _wrap(chars: list, cams: list, src: QbStruct) -> QbStruct:
    members = [QbMember("array", K["characters"], QbArray("struct", chars)),
               QbMember("array", K["cameras"], QbArray("struct", cams) if cams else QbArray("empty", []))]
    for key in CLIP_ARRAYS:
        m = next((m for m in src.members if m.name == key and isinstance(m.value, QbArray)), None)
        if m is not None:
            members.append(QbMember("array", key, m.value))
        elif key == 0x65C1ECD3:
            members.append(QbMember("array", key, QbArray("empty", [])))
    return QbStruct(members)


def translate(d: QbStruct, frames: dict[int, int]) -> QbStruct | None:
    """Definicion de WT (A o B) -> struct de clip de WoR. None si no tiene ningun musico."""
    chars_in = d.get(K["characters"])
    if isinstance(chars_in, QbArray):  # formato A
        chars = []
        for c in chars_in.values:
            role = _qbkey_name(c.get(K["name"]))
            anim = c.get(K["anim"])
            if role is None or anim in (None, NONE):
                continue
            node = c.get(K["startnode"]) or f"{role}_start".encode()
            sf = c.get(K["startframe"]) or 0
            ef = c.get(K["endframe"]) or frames.get(anim) or 0
            chars.append(_character(role, node, anim, sf, ef, c))
        cams = []
        cin = d.get(K["cameras"])
        if isinstance(cin, QbArray) and cin.elem == "struct":
            for i, c in enumerate(cin.values):
                slot = c.get(K["slot"])
                cams.append(_cam(i if slot is None else slot, c.get(K["name"]) or CAM_NODE["vocalist"],
                                 c.get(K["anim"])))
        return _wrap(chars, cams, d) if chars else None

    anims = d.get(K["anims"])  # formato B (nativo de WT)
    nodes = d.get(K["startnodes"])
    chars = []
    for role in ROLES:
        anim = anims.get(qbkey(role)) if isinstance(anims, QbStruct) else None
        if anim in (None, NONE):
            continue
        node = (nodes.get(qbkey(role)) if isinstance(nodes, QbStruct) else None) or f"{role}_start".encode()
        chars.append(_character(role, node, anim, 0, frames.get(anim, 0), None))
    if not chars:
        return None
    cams = []
    order = ["cameras", "vocalist_cameras", "guitarist_cameras", "bassist_cameras", "drummer_cameras",
             "secondary_cameras"]
    for key in order:
        arr = d.get(K[key])
        if not (isinstance(arr, QbArray) and arr.elem == "struct"):
            continue
        default = CAM_NODE.get(key.split("_")[0], CAM_NODE["vocalist"])
        for c in arr.values:
            if c.get(K["anim"]) in (None, NONE):
                continue
            cams.append(_cam(len(cams), c.get(K["name"]) or default, c.get(K["anim"])))
    return _wrap(chars, cams, d)


def clip_anims(st: QbStruct) -> set[int]:
    out = set()
    for key in ("characters", "cameras"):
        arr = st.get(K[key])
        if isinstance(arr, QbArray) and arr.elem == "struct":
            out |= {c.get(K["anim"]) for c in arr.values if c.get(K["anim"])}
    return out


def wt_clips(entries, chart_key: int, called: set[int]):
    """({clip: struct WoR} de los clips llamados que traen definicion, {anims .ska que usan},
    {"characters"/"cameras": cuantos se quitaron por usar un .ska que no esta en el PAK})."""
    frames = ska_frames(entries)
    have = {e.fullname for e in entries if e.type == SKA_TYPE}
    out, used, lost = {}, set(), {"characters": 0, "cameras": 0}
    for name, d in raw_clip_defs(entries, chart_key).items():
        if name not in called:
            continue
        st = translate(d, frames)
        if st is None:
            continue
        # Un .ska que no viaja en el PAK (global del juego de origen) no existe en WoR: ese musico se
        # quedaria en pose T y esa camara en el origen (bajo el escenario). Se quitan.
        for key in ("characters", "cameras"):
            arr = st.get(K[key])
            if isinstance(arr, QbArray) and arr.elem == "struct":
                keep = [c for c in arr.values if c.get(K["anim"]) in have]
                lost[key] += len(arr.values) - len(keep)
                for m in st.members:
                    if m.name == K[key]:
                        m.value = QbArray("struct", keep) if keep else QbArray("empty", [])
        chars = st.get(K["characters"])
        if not (isinstance(chars, QbArray) and chars.values):
            continue
        out[name] = st
        used |= clip_anims(st)
    return out, used, lost
