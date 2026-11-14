"""Skins y venues de GHWT:DE.

Estado: se RECONOCEN y se analizan (que contienen, a que personaje/escenario afectan), pero la
conversion de World Tour a Warriors of Rock aun no esta hecha: los modelos, materiales, esqueletos y
el formato de los escenarios cambian entre los dos juegos. Este modulo deja el punto de entrada para
ir anadiendo conversores por partes (texturas -> modelos -> escenarios) sin tocar la app.
"""
from __future__ import annotations

import configparser
from pathlib import Path

from modules.base import Detection, Log, Module, Report

INI_HINTS = {
    "venue": ("venue", "zone", "stage", "z_"),
    "skin": ("character", "instrument", "guitar", "bass", "drum", "mic", "outfit", "skin", "highway"),
}


def _read_ini(p: Path) -> dict[str, str]:
    cp = configparser.ConfigParser(interpolation=None, strict=False)
    cp.optionxform = str
    try:
        raw = p.read_bytes()
        try:
            text = raw.decode("utf-8-sig")
        except UnicodeDecodeError:  # .ini en ANSI (Latin-1 / cp1252)
            text = raw.decode("cp1252", errors="replace")
        cp.read_string(text, source=str(p))
    except (OSError, configparser.Error):
        return {}
    out = {"__sections__": ",".join(cp.sections())}
    for s in cp.sections():
        for k, v in cp.items(s):
            out[k] = v
    return out


def analyze(folder: Path) -> tuple[str, dict, list[str]]:
    inis = [p for p in folder.rglob("*.ini") if p.name.lower() != "song.ini"]
    info: dict[str, str] = {}
    for p in inis[:4]:
        info.update(_read_ini(p))
    files = [f for f in folder.rglob("*") if f.is_file()]
    blob = " ".join([info.get("__sections__", "")] + list(info.keys()) + list(info.values())
                    + [f.name for f in files]).lower()
    score = {k: sum(blob.count(h) for h in hints) for k, hints in INI_HINTS.items()}
    kind = max(score, key=score.get) if any(score.values()) else "other"
    exts: dict[str, int] = {}
    for f in files:
        e = "".join(f.suffixes[-2:]).lower() or f.suffix.lower()
        exts[e] = exts.get(e, 0) + 1
    notes = [f"{n} x {e}" for e, n in sorted(exts.items(), key=lambda kv: -kv[1])[:6]]
    return kind, info, notes


class WtdeAssetModule(Module):
    name = "wtde_assets"
    kinds = ("skin", "venue")

    def detect(self, path: Path) -> Detection | None:
        path = Path(path)
        if not path.is_dir() or (path / "song.ini").exists():
            return None
        inis = [p for p in path.rglob("*.ini") if p.name.lower() != "song.ini"]
        if not inis:
            return None  # sin .ini de WTDE: lo trata wor_native
        kind, info, notes = analyze(path)
        label = info.get("Name") or info.get("name") or path.name
        return Detection(self.name, kind, path, f"{label} (GHWT:DE)", False,
                         [f"Tipo detectado: {kind}"] + notes)

    def install(self, det: Detection, ws, log: Log, **_) -> Report:
        rep = Report(f"Convertir {det.label}")
        rep.error(f"La conversion de {det.kind}s de GHWT:DE a Warriors of Rock aun no esta disponible.")
        for n in det.notes:
            rep.info(n)
        rep.info("Si el mod ya esta en formato de WoR, instalalo como 'mod de WoR'.")
        return rep
