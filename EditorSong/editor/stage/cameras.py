"""Camaras de escenario (las que usan los cortes "autocut" y los planos de escenario de la pista de momento).

Cada escenario trae en su .qb arrays <zona>_cameras_<categoria> (guitarist, singer, drummer, zoom, Orbits...)
de structs. Lo verificado en Z_Barbarian / Z_Festival:
  {name, type, controlscript, params{...}, DOF}  o  {name, LockTo, LockToBone, pos, Quat, FOV, DOF}
  controlscript CameraCuts_HandCam  -> plano fijo con camara en mano, apuntando a LookAt/LookAtBone
                CameraCuts_TwoCam   -> params.Cam1 -> params.Cam2 durante el corte (zoom in/out, travelling)
                CameraCuts_Orbit    -> gira `orbitangle` grados alrededor del musico durante el corte
  LockTo  = GUITARIST / VOCALIST / BASSIST / DRUMMER (relativo a su nodo) o World
  pos/Quat: vector [cabecera, x, y, z]; el cuaternion no va conjugado y la camara mira hacia su +Z
  (comprobado: Guitarist_CloseUp queda a la derecha del guitarrista mirandole).
El numero de corte -> nombre de camara sale de la tabla del Guitar Hero SDK (Converter/CamHandler.js):
8 guitarist_closeup, 9 guitarist_stage, ... 28 longshot, 45 zoom_in, 46 zoom_out, 48 spotlight.
"""
from __future__ import annotations

import json
import math
import re
import struct

from ..core import paths
from ..core.names import qbkey
from qb_io import QbArray, QbFile, QbStruct

from ..formats import pak as P
from .nodes import list_venues

# numero de corte -> nombre de camara (indice = numero). "" = sin camara.
CUT_NAMES = ["", "", "", "moment01", "moment02", "moment03", "moment04", "",
             "guitarist_closeup", "guitarist_stage", "guitarist_mocap01", "guitarist_mocap02", "",
             "singer_closeup", "singer_stage", "singer_mocap01", "singer_mocap02", "",
             "bassist_closeup", "bassist_stage", "bassist_mocap01", "bassist_mocap02", "",
             "drummer_closeup", "drummer_stage", "drummer_mocap01", "drummer_mocap02", "",
             "longshot", "midshot", "venue_midshot", "generic_stage_shot", "",
             "solo_guitar", "solo_vocal", "solo_drum", "solo_bass", "", "single", "", "special01", "special02", "",
             "audience", "", "zoom_in", "zoom_out", "", "spotlight"]

CATEGORIES = ("moments guitarist singer bassist drummer zoom orbits solo closeups single special audience spotlight "
              "headtohead pan dolly zoom_fast zoom_slow zoom_out walk stagedive starpower encore intro fastintro "
              "verts_front verts_rear g_across_stage s_across_stage vert_drummer vert_guitarist vert_stagefront "
              "vert_stagerear win lose no_band preencore longshot generic midshot").split()

FIELDS = {qbkey(n): n.lower() for n in (
    "name type controlscript params LockTo LockToBone pos Quat FOV LookAt LookAtBone ScreenOffset "
    "DistFromLookAt orbitangle Cam1 Cam2 UseHandCam singleplayer DOF").split()}
VEC = {"pos", "quat", "screenoffset"}

_KEYS: dict[int, str] | None = None


def _keys() -> dict[int, str]:
    """Nombres de QBKey de WoR (lista del Guitar Hero SDK, si esta) para mostrar nombres legibles."""
    global _KEYS
    if _KEYS is None:
        _KEYS = {}
        f = paths.ROOT / "tools" / "ghsdk" / "SDKCode" / "Checksums" / "ghwor_keys.txt"
        if f.exists():
            for line in f.read_text(encoding="utf-8", errors="ignore").splitlines():
                m = re.match(r"\s*0x([0-9a-fA-F]+)\s+(\S.*)$", line)
                if m:
                    _KEYS[int(m.group(1), 16)] = m.group(2).strip()
    return _KEYS


def _name(k) -> str:
    return _keys().get(k, f"0x{k:08x}") if isinstance(k, int) else str(k)


def _vec(data: bytes, ptr) -> list[float] | None:
    if not isinstance(ptr, int) or ptr + 16 > len(data):
        return None
    return [round(x, 5) for x in struct.unpack_from(">3f", data, ptr + 4)]


def _f(v) -> float | None:
    if isinstance(v, float):
        return v
    if isinstance(v, int):    # los floats de QB llegan como entero con los bits del float
        f = struct.unpack(">f", struct.pack(">I", v & 0xFFFFFFFF))[0]
        if math.isfinite(f) and 1e-3 < abs(f) < 1e4:
            return f
        return float(v - (1 << 32) if v >= 1 << 31 else v)
    return None


def _struct(st: QbStruct, data: bytes) -> dict:
    out = {}
    for m in st.members:
        k = FIELDS.get(m.name)
        v = m.value
        if k is None:
            if v == qbkey("UseHandCam") or m.name == 0:
                out["usehandcam"] = True
            continue
        if isinstance(v, QbStruct):
            out[k] = _struct(v, data)
        elif k in VEC:
            out[k] = _vec(data, v)
        elif k in ("fov", "distfromlookat", "orbitangle"):
            out[k] = _f(v)
        else:
            out[k] = _name(v)
    return out


def venue_cameras(zone: str) -> dict:
    if zone not in list_venues():
        raise ValueError(zone)
    cache = paths.CACHE / f"venue_cams_v2_{zone.lower()}.json"
    if cache.exists():
        return json.loads(cache.read_text())
    arrays = {}
    for pre in (zone, zone.lower(), "Z_" + zone[2:].lower()):
        for c in CATEGORIES:
            arrays[qbkey(f"{pre}_cameras_{c}")] = c
    cams = []
    for e in P.entries(f"zones/{zone}.pak.xen"):
        if e.type != qbkey(".qb"):
            continue
        try:
            q = QbFile(e.data)
        except Exception:  # noqa: BLE001
            continue
        for it in q.items:
            if not isinstance(it.value, QbArray) or it.value.elem != "struct" or not it.value.values:
                continue
            # array de camaras: sus structs llevan controlscript/params o pos+Quat (el nombre del array varia
            # por escenario: los planos generales de Barbarian y Festival estan en arrays con nombres distintos)
            mem = {m.name for m in it.value.values[0].members}
            if not ({qbkey("controlscript"), qbkey("params")} & mem or {qbkey("pos"), qbkey("Quat")} <= mem):
                continue
            cat = arrays.get(it.name) or _name(it.name)
            for st in it.value.values:
                c = _struct(st, e.data)
                p = c.pop("params", {}) or {}
                c = {**p, **c} if "name" in c else {**c, **p}
                c["category"] = cat
                c["script"] = c.pop("controlscript", "") or "static"
                cams.append(c)
    out = {"cameras": cams, "cut_names": CUT_NAMES}
    cache.write_text(json.dumps(out))
    return out
