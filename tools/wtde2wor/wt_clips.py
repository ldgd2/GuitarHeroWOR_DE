"""Reconstruccion de los clips de banda ("momentos": el guitarrista camina hacia el cantante, poses,
saltos...) de una cancion de World Tour / Smash Hits / WTDE para Warriors of Rock.

En WT la cancion dispara los clips con Band_PlayClip {clip, startframe, endframe} desde
<cancion>_performance, pero la DEFINICION del clip (que musico, que .ska, que camara) es global del
juego de origen y no viaja en el PAK. Lo que si viaja son los .ska de cada clip:
  * animacion de cuerpo: 128 huesos (u16 en +0x2C)
  * animacion de camara: 3 huesos
  todos los .ska de un mismo clip tienen exactamente la misma duracion (float en +0x28, segundos).

Reconstruccion:
  1. Se agrupan los .ska que no son faciales por duracion -> un grupo por clip.
  2. Cada Band_PlayClip exige frames >= endframe (o, si no tiene, lo que dura su corte de camara de
     momento 3..6). Se asigna a cada clip el grupo que mejor encaja (minima holgura total); los clips
     sin camara de momento (usan la 33, camara de escenario) prefieren grupos sin camara propia.
  3. Musicos: el rol de cada cuerpo se deduce de los huesos que mueve (ska_roles: modelo entrenado
     con los clips vanilla de WoR); en grupos de varios cuerpos se elige la combinacion de roles
     distintos mas probable. Las animaciones que solo mueven la cara (huesos 60-100) no son cuerpos:
     son faciales que la cancion lanza por otra lista y se descartan.
  4. Camaras: slot = indice dentro del grupo (WoR: nota de momento 3 -> slot 0, 4 -> slot 1...).
Formato del clip de WoR (struct global con el nombre del clip):
  {characters=[{name, startnode='<musico>_start', anim, startframe, endframe, timefactor,
                ik_targetL, ik_targetR, strum, fret, chord}],
   cameras=[{slot, name='TRG_Geo_Camera_Performance_XXXX01', anim}], 0x65c1ecd3=[]}
"""
from __future__ import annotations

import struct
from dataclasses import dataclass, field

from qb_io import QbArray, QbMember, QbStruct
from wt_chart import qbkey

K_CLIP = qbkey("clip")
K_SF = qbkey("startframe")
K_EF = qbkey("endframe")
K_SCR = qbkey("scr")
K_TIME = qbkey("time")
K_PARAMS = qbkey("params")
PLAY_CLIP = 0x01DE43A7  # Band_PlayClip
MOMENT_NOTES = {3, 4, 5, 6}
FPS = 30.0
SLACK = 12  # frames de tolerancia para finales estimados por camara

CAM_NODE = {"guitarist": b"TRG_Geo_Camera_Performance_GUIT01",
            "bassist": b"TRG_Geo_Camera_Performance_BASS01",
            "vocalist": b"TRG_Geo_Camera_Performance_SING01",
            "drummer": b"TRG_Geo_Camera_Performance_SING01"}  # vanilla: nunca nodo propio
# ik_targetL/R en los clips vanilla: cantante 0xFBF730F6 (554/554), guitarra/bajo 0xBDC53CF2 (manos
# pegadas al instrumento). Al reves, el cantante busca un mastil que no tiene y el guitarrista suelta
# la guitarra (brazos caidos / retorcidos).
IK_VOCALIST = 0xFBF730F6
IK_INSTRUMENT = 0xBDC53CF2
FACE_BONES = range(60, 101)
HANDS_AUTO = 0x0203B372  # strum/fret/chord: valor mas comun en WoR (1012 de 1082)


@dataclass
class SkaInfo:
    key: int
    frames: int
    camera: bool
    face: bool = False
    roles: dict | None = None  # probabilidad de cada rol (None si no se pudo leer)
    hip: float = 0.0           # desplazamiento inicial de la cadera (hueso 1) respecto al nodo


_MODEL = None


def _role_model():
    global _MODEL
    if _MODEL is None:
        from ska_roles import RoleModel
        _MODEL = RoleModel.load() or False
    return _MODEL or None


@dataclass
class Group:
    frames: int
    bodies: list[int] = field(default_factory=list)
    cameras: list[int] = field(default_factory=list)


def ska_info(key: int, data: bytes) -> SkaInfo | None:
    if len(data) < 0x30:
        return None
    dur = struct.unpack_from(">f", data, 0x28)[0]
    bones = struct.unpack_from(">H", data, 0x2C)[0]  # numero de huesos (u16); 0x2E son los frames
    info = SkaInfo(key, round(dur * FPS), bones <= 4)
    if info.camera:
        return info
    try:
        from ska_convert import read_ska
        from ska_roles import features
        s = read_ska(data)
    except Exception:
        return info
    moving = [b for b, q in s.quats.items() if q and len(q) > 1
              and max(max(k[j] for k in q) - min(k[j] for k in q) for j in (1, 2, 3)) > 300]
    info.face = bool(moving) and all(b in FACE_BONES for b in moving)
    t1 = s.trans.get(1) or []
    info.hip = sum(abs(c) for c in t1[0][1:]) if t1 else 0.0
    model = _role_model()
    if model is not None and not info.face:
        info.roles = model.proba(features(s.quats, s.trans, s.partial))
    return info


def group_skas(skas: list[SkaInfo]) -> list[Group]:
    """Agrupa en el ORDEN DEL PAK: cada clip se guarda como sus cuerpos seguidos de sus camaras.
    Un cuerpo se une al grupo actual solo si tiene la misma duracion y el grupo aun no tiene camaras;
    una camara se une al grupo actual si coincide la duracion, o al ultimo grupo de esa duracion."""
    groups: list[Group] = []
    cur: Group | None = None
    for s in skas:
        if s.face:
            continue
        if s.camera:
            if cur is not None and cur.frames == s.frames:
                cur.cameras.append(s.key)
            else:
                prev = next((g for g in reversed(groups) if g.frames == s.frames), None)
                if prev is not None:
                    prev.cameras.append(s.key)
            continue
        if cur is not None and cur.frames == s.frames and not cur.cameras and len(cur.bodies) < 4:
            cur.bodies.append(s.key)
        else:
            cur = Group(s.frames, [s.key])
            groups.append(cur)
    return groups


@dataclass
class ClipUse:
    name: int
    need: int            # frames minimos que debe tener la animacion
    exact: bool          # need viene de un endframe explicito
    moment_cam: bool     # la cancion usa camara de momento (3..6) durante el clip
    first_time: int
    span_ms: int


def clip_uses(perf_events: list, cameras: list[int]) -> dict[int, ClipUse]:
    cams = [(cameras[i], (cameras[i + 1] >> 16) & 0xFF, cameras[i + 1] & 0xFFFF)
            for i in range(0, len(cameras) - 1, 2)]
    uses: dict[int, ClipUse] = {}
    for ev in perf_events:
        if ev.get(K_SCR) != PLAY_CLIP or not isinstance(ev.get(K_PARAMS), QbStruct):
            continue
        p = ev.get(K_PARAMS)
        name, sf, ef = p.get(K_CLIP), p.get(K_SF, 0), p.get(K_EF)
        t = ev.get(K_TIME, 0)
        near = [(n, ln) for (ct, n, ln) in cams if abs(ct - t) < 120]
        mom = [ln for n, ln in near if n in MOMENT_NOTES]
        if ef is not None:
            need, exact = ef, True
        else:
            need, exact = sf + (round(max(mom) * FPS / 1000) - SLACK if mom else 0), False
        span = round((ef - sf) * 1000 / FPS) if ef is not None else (max(mom) if mom else 0)
        u = uses.get(name)
        if u is None:
            uses[name] = ClipUse(name, need, exact, bool(mom), t, span)
        else:
            u.need = max(u.need, need)
            u.exact = u.exact or exact
            u.moment_cam = u.moment_cam or bool(mom)
    return uses


def assign(uses: list[ClipUse], groups: list[Group]) -> dict[int, Group]:
    """Asignacion clip -> grupo minimizando la holgura total (algoritmo hungaro).

    Si hay mas clips que grupos, los que sobran quedan sin asignar (no se crean)."""
    BIG = 1e7

    def cost(u: ClipUse, g: Group) -> float:
        if g.frames < u.need:
            return BIG
        c = g.frames - u.need
        if not u.moment_cam and g.cameras:
            c += 400  # clip sin camara de momento: prefiere grupo sin camaras propias
        if u.moment_cam and not g.cameras:
            c += 400
        return c

    n = max(len(uses), len(groups))
    if n == 0:
        return {}
    a = [[(cost(uses[i], groups[j]) if i < len(uses) and j < len(groups) else BIG / 10)
          for j in range(n)] for i in range(n)]
    # Hungaro O(n^3) (version con potenciales, filas = clips, columnas = grupos)
    INF = float("inf")
    u_pot, v_pot = [0.0] * (n + 1), [0.0] * (n + 1)
    p, way = [0] * (n + 1), [0] * (n + 1)
    for i in range(1, n + 1):
        p[0] = i
        j0 = 0
        minv = [INF] * (n + 1)
        usedc = [False] * (n + 1)
        while True:
            usedc[j0] = True
            i0, delta, j1 = p[j0], INF, 0
            for j in range(1, n + 1):
                if not usedc[j]:
                    cur = a[i0 - 1][j - 1] - u_pot[i0] - v_pot[j]
                    if cur < minv[j]:
                        minv[j], way[j] = cur, j0
                    if minv[j] < delta:
                        delta, j1 = minv[j], j
            for j in range(n + 1):
                if usedc[j]:
                    u_pot[p[j]] += delta
                    v_pot[j] -= delta
                else:
                    minv[j] -= delta
            j0 = j1
            if p[j0] == 0:
                break
        while True:
            j1 = way[j0]
            p[j0] = p[j1]
            j0 = j1
            if j0 == 0:
                break
    out = {}
    for j in range(1, n + 1):
        i = p[j] - 1
        if i < len(uses) and j - 1 < len(groups) and a[i][j - 1] < BIG:
            out[uses[i].name] = groups[j - 1]
    return out


def _sings(vocals: list[tuple[int, int, int]], t0: int, t1: int) -> float:
    if t1 <= t0:
        return 0.0
    cov = 0
    for t, ln, _p in vocals:
        a, b = max(t, t0), min(t + ln, t1)
        if b > a:
            cov += b - a
    return cov / (t1 - t0)


ROLE_ORDER = ["vocalist", "guitarist", "bassist", "drummer"]


def pick_roles(probs: list[dict | None], sing: float) -> list[str]:
    """Roles distintos para los cuerpos de un clip, maximizando la probabilidad conjunta."""
    import itertools
    import math
    n = min(len(probs), 4)
    best, best_score = ROLE_ORDER[:n], -1e18
    for perm in itertools.permutations(ROLE_ORDER, n):
        score = 0.0
        for r, p in zip(perm, probs):
            if p is None:  # sin modelo: reparto clasico
                p = {"vocalist": 0.6 if (n == 1 and sing > 0.25) else 0.1, "guitarist": 0.5,
                     "bassist": 0.3, "drummer": 0.01}
            score += math.log(max(p.get(r, 0.0), 1e-6))
        if score > best_score:
            best, best_score = list(perm), score
    return best


# Nodo de partida del musico en el clip. Medido en los clips vanilla: si la animacion empieza con la
# cadera en el origen (<= 1.8) el musico parte de SU nodo (2669 de 2669); si empieza desplazada, la
# animacion esta hecha respecto al cantante y parte de vocalist_start (664 de 818). Poner una de esas en
# el nodo propio suma el desplazamiento dos veces: el musico sale del escenario y el IK le estira los
# brazos hacia el instrumento (bajista con los brazos rotos por debajo del escenario).
HIP_FROM_SINGER = 1.8


def start_node(role: str, hip: float) -> bytes:
    return b"vocalist_start" if hip > HIP_FROM_SINGER else f"{role}_start".encode()


def _character(role: str, anim: int, frames: int, hip: float = 0.0) -> QbStruct:
    ik = IK_VOCALIST if role == "vocalist" else IK_INSTRUMENT
    m = [QbMember("qbkey", qbkey("name"), qbkey(role)),
         QbMember("string", qbkey("startnode"), start_node(role, hip)),
         QbMember("qbkey", qbkey("anim"), anim),
         QbMember("int", K_SF, 0),
         QbMember("int", K_EF, frames),
         QbMember("int", qbkey("timefactor"), 1),
         QbMember("qbkey", qbkey("ik_targetL"), ik),
         QbMember("qbkey", qbkey("ik_targetR"), ik),
         QbMember("qbkey", qbkey("strum"), HANDS_AUTO),
         QbMember("qbkey", qbkey("fret"), HANDS_AUTO),
         QbMember("qbkey", qbkey("chord"), HANDS_AUTO)]
    return QbStruct(m)


def build_clip_structs(perf_events: list, cameras: list[int], skas: list[SkaInfo],
                       vocals: list[tuple[int, int, int]]) -> tuple[dict[int, QbStruct], set[int], list[str]]:
    """Devuelve ({nombre_clip: struct WoR}, {ska usados}, informe)."""
    groups = group_skas([s for s in skas])
    by_key = {s.key: s for s in skas}
    uses = clip_uses(perf_events, cameras)
    mapping = assign(list(uses.values()), groups)
    out: dict[int, QbStruct] = {}
    used: set[int] = set()
    report = []
    for name, g in mapping.items():
        u = uses[name]
        sing = _sings(vocals, u.first_time, u.first_time + max(u.span_ms, 1000))
        roles = pick_roles([by_key[b].roles for b in g.bodies], sing)
        chars = [_character(r, b, g.frames, by_key[b].hip) for r, b in zip(roles, g.bodies)]
        # Nodo de referencia de las camaras = el nodo del que parte el PRIMER musico del clip (medido
        # en vanilla: 5043 de 5283). La animacion de camara es relativa a ese mismo origen; anclarla a
        # otro musico deja la camara desplazada, bajo el escenario o mirando a la nada.
        first_node = start_node(roles[0], by_key[g.bodies[0]].hip).split(b"_")[0].decode()

        def node_for(i):
            return CAM_NODE.get(first_node, CAM_NODE["vocalist"])

        cams = [QbStruct([QbMember("int", qbkey("slot"), i),
                          QbMember("string", qbkey("name"), node_for(i)),
                          QbMember("qbkey", qbkey("anim"), c)]) for i, c in enumerate(g.cameras)]
        st = QbStruct([QbMember("array", qbkey("characters"), QbArray("struct", chars)),
                       QbMember("array", qbkey("cameras"),
                                QbArray("struct", cams) if cams else QbArray("empty", [])),
                       QbMember("array", 0x65C1ECD3, QbArray("empty", []))])
        out[name] = st
        used.update(g.bodies)
        used.update(g.cameras)
        report.append(f"clip {name:08x}: {g.frames} frames, {'+'.join(roles)}, {len(g.cameras)} camara(s)")
    return out, used, report
