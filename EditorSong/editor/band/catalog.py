"""Catalogo de piezas de personaje e instrumento (pak/archive/cas_pieces) con nombres reales.

Un personaje de WoR = cuerpo base (cas_globalarchive, uno por genero, con su esqueleto) + piezas con pesos al
mismo esqueleto de 128 huesos: torso, piernas, zapatos, pelo, barba, sombrero, accesorios, gafas...
Los instrumentos son mallas rigidas modeladas en la pose de enlace del esqueleto y se cuelgan de un hueso:
  guitarra/bajo -> Bone_Guitar_Body (112), pie de micro -> Bone_Mic_Stand (122), micro -> Bone_Mic_Microphone (125)
  baquetas -> palmas (10 izq. / 14 der.), bateria -> raiz.
Los presets de los personajes del juego se arman por el nombre de sus piezas (axel, judynails, lars...).
"""
from __future__ import annotations

import re
from functools import lru_cache

from ..core.names import qbkey
from ..formats import pak as P
from .gamenames import names

BS = "\\"
SKIN = qbkey(".skin")

# genero -> (cuerpo, textura del cuerpo, esqueleto) en cas_globalarchive (medido: el cuerpo masculino es mas
# ancho y su esqueleto tiene la pelvis a 1.027 m; el femenino a 1.109 m)
BODIES = {
    "m": {"skin": 0x17237EA0, "tex": 0x65BCF809, "ske": 0x1E91D081},
    "f": {"skin": 0x528AA594, "tex": 0x133BB924, "ske": 0x9B22720A},
}
BONE_GUITAR, BONE_MIC_STAND, BONE_MIC, BONE_PALM_L, BONE_PALM_R = 112, 122, 125, 10, 14

CATEGORY_ES = {"torso": "Torso", "legs": "Piernas", "shoe": "Zapatos", "hair": "Pelo", "hathair": "Pelo con gorro",
               "hat": "Sombrero", "fhair": "Barba", "acc": "Accesorio", "glasses": "Gafas", "head": "Cabeza",
               "teeth": "Dientes", "other": "Otro"}

PRESETS = [  # nombre, genero, token en el nombre de las piezas, instrumento por defecto
    ("Axel Steel", "m", "axel", "guitar"), ("Johnny Napalm", "m", "johnny", "guitar"),
    ("Judy Nails", "f", "judy", "guitar"), ("Lars Umlaut", "m", "lars", "guitar"),
    ("Izzy Sparks", "m", "izzy", "guitar"), ("Clive Winston", "m", "clive", "bass"),
    ("Eddie Knox", "m", "eddie", "drums"),
]


def _category(stem: str) -> str:
    b = stem.lower().split("_")
    for c in b[2:3] + b[1:2]:
        c = re.sub(r"\d+$", "", c)
        if c in ("shoes",):
            return "shoe"
        if c in ("accl", "accr", "accs"):
            return "acc"
        if c.startswith("teeth"):
            return "teeth"
        if c in CATEGORY_ES:
            return c
    return "other"


def _instrument_kind(path: str) -> str | None:
    p = path.lower()
    if BS + "guitar" + BS in p:
        return "guitar"
    if BS + "bass" + BS in p:
        return "bass"
    if BS + "drums" + BS in p:
        return "sticks" if "stick" in p else "drums"
    if BS + "microphone" + BS in p:
        return "mic_stand" if "stand" in p else "mic"
    return None


@lru_cache(maxsize=1)
def catalog() -> dict:
    nm = names()
    arch = P.archive("cas_pieces")
    pieces = []
    for e in arch.entries:
        if e.type != SKIN:
            continue
        path = nm.get(e.key)
        if not path or "_def.skin" in path.lower():
            continue
        stem = path.split(BS)[-1][:-5]
        tex = qbkey(path[:-5] + ".tex")
        item = {"id": f"0x{e.key:08x}", "name": stem, "path": path,
                "tex": f"0x{tex:08x}" if tex in arch.by_key else None}
        kind = _instrument_kind(path)
        if kind:
            item.update(kind=kind, cat=kind)
            m = re.match(r"^(gtr|bass|bas|bs)(\d+[a-z]?)_", stem.lower())
            item["model"] = (m.group(1)[:3] + m.group(2)) if m else None
        elif (BS + "car" + BS) in path.lower():
            g = "f" if stem.lower().startswith(("f_", "car_female")) else "m"
            item.update(kind="body", gender=g, cat=_category(stem))
        else:
            continue
        pieces.append(item)
    pieces.sort(key=lambda p: (p["kind"], p.get("gender", ""), p["cat"], p["name"]))
    return {"pieces": pieces, "presets": _presets(pieces), "instruments": _instrument_sets(pieces),
            "categories": CATEGORY_ES}


def _presets(pieces: list[dict]) -> list[dict]:
    out = []
    for label, g, tok, inst in PRESETS:
        cand = [p for p in pieces if p["kind"] == "body" and tok in p["name"].lower()]
        if not cand:
            continue

        def variant(p):
            m = re.search(tok + r"[a-z]*_?(\d*)", p["name"].lower())
            return m.group(1) if m and m.group(1) else "1"
        by_var: dict[str, dict[str, dict]] = {}
        for p in cand:
            if p.get("gender") != g:
                continue
            by_var.setdefault(variant(p), {}).setdefault(p["cat"], p)
        if not by_var:
            continue
        best = max(by_var, key=lambda v: (len(by_var[v]), -int(v) if v.isdigit() else 0))
        chosen = dict(by_var[best])
        for v in sorted(by_var):                     # completa categorias que falten con otras variantes
            for c, p in by_var[v].items():
                if c == "hathair" and "hair" in chosen:
                    continue
                chosen.setdefault(c, p)
        chosen.pop("hathair", None) if "hair" in chosen else None
        accs = [p for p in cand if p["cat"] == "acc" and p.get("gender") == g]
        ids = [p["id"] for c, p in chosen.items() if c != "acc"] + [p["id"] for p in accs[:2]]
        out.append({"name": label, "gender": g, "pieces": ids, "instrument": inst})
    return out


def _instrument_sets(pieces: list[dict]) -> list[dict]:
    """Instrumentos completos: piezas que comparten numero de modelo (gtr130_*, bass133_*...)."""
    groups: dict[tuple, list] = {}
    for p in pieces:
        if p["kind"] in ("guitar", "bass") and p.get("model") and "ghost" not in p["name"]:
            groups.setdefault((p["kind"], p["model"]), []).append(p)
    by_name = {p["name"]: p for p in pieces}
    default_fret = {"guitar": by_name.get("gtr_fretboard_sp"), "bass": by_name.get("bass_fretboard_143")}
    sets = []
    for (kind, model), ps in sorted(groups.items()):
        names_ = " ".join(p["name"] for p in ps)
        if "neck" not in names_ and "nekhed" not in names_ and "nhead" not in names_:
            continue
        if len(ps) < 2:
            continue
        parts: dict[str, dict] = {}                  # una pieza por parte (neckhead_01..07 son alternativas)
        for p in sorted(ps, key=lambda p: p["name"]):
            rest = p["name"].lower().split("_", 1)[1] if "_" in p["name"] else p["name"]
            part = rest
            while True:
                nxt = re.sub(r"(_?\d+[a-z]?|_[a-z])$", "", part)
                if nxt == part or not nxt:
                    break
                part = nxt
            parts.setdefault(part, p)
        chosen = list(parts.values())
        if not any("fret" in p["name"] or "fbrd" in p["name"] or "fboard" in p["name"] for p in chosen) and default_fret[kind]:
            chosen.append(default_fret[kind])
        sets.append({"name": f"{'Guitarra' if kind == 'guitar' else 'Bajo'} {model}", "kind": kind,
                     "pieces": [p["id"] for p in chosen]})
    return sets
