"""Logica de las interfaces (proj["logic"]["source"], Python de wui) -> ui3d/<n>/logic.wuib.

El lenguaje vive en tools/wui (libreria independiente); aqui solo se le pasan las capas y pantallas del proyecto.
"""
from __future__ import annotations

import base64
import sys

from ..core.paths import ROOT

WUI = ROOT / "tools" / "wui"
if str(WUI) not in sys.path:
    sys.path.insert(0, str(WUI))

import wui  # noqa: E402


_KIND = {"anims": "generado", "project": "proyecto", "screen": "pantalla", "layer": "capa"}


def _layer_name(l: dict) -> str:
    return " ".join(str(l.get("name") or l.get("id")).split())


def parts(proj: dict) -> list[tuple[str, str, str]]:
    """(tipo, titulo, codigo): proyecto, y por pantalla la pantalla y sus capas con codigo propio
    (igual que web/src/uiedit/scripts.js)"""
    lg = proj.get("logic") or {}
    out = [("anims", "Animaciones", lg.get("anims") or ""),     # generado por el editor (web/src/uiedit/anims.js)
           ("project", "Proyecto", (lg.get("source") or "").strip("﻿"))]
    for s in proj.get("screens") or []:
        out.append(("screen", s.get("name") or "", (s.get("logic") or {}).get("source") or ""))
        for l in s.get("layers") or []:
            src = (l.get("script") or {}).get("source")
            if src is not None:
                out.append(("layer", _layer_name(l), src))
    return out


def assemble(proj: dict) -> tuple[str, list[tuple[str, int, int]]]:
    """un solo programa y donde empieza cada parte: [(titulo, desde, hasta)]"""
    lines: list[str] = []
    where = []
    for kind, title, src in parts(proj):
        if not src.strip():
            continue
        lines.append(f"# ===== {_KIND[kind]} {title} =====")
        body = src.rstrip("\n").split("\n")
        where.append((f"{_KIND[kind]} {title}", len(lines) + 1, len(lines) + len(body)))
        lines.extend(body)
        lines.append("")
    return "\n".join(lines), where


def locate(where, line: int) -> str:
    for title, a, b in where:
        if a <= line <= b:
            return f"{title}, linea {line - a + 1}"
    return f"linea {line}"


def source(proj: dict) -> str:
    return assemble(proj)[0]


def names(proj: dict) -> tuple[list[str], list[str]]:
    """(nombres de capas de todas las pantallas, nombres de pantallas), como los ve la logica"""
    from .modes import safe
    layers: list[str] = []
    for scr in proj.get("screens") or []:
        for l in scr.get("layers") or []:
            n = " ".join(str(l.get("name") or l.get("id")).split())
            if n and n not in layers:
                layers.append(n)
    screens = [safe(s.get("name")) for s in proj.get("screens") or []]
    return layers, screens


def compile_project(proj: dict, text: str | None = None) -> dict:
    """-> {"ok", "bytes" (o None), "diagnostics": [dict]}"""
    src = source(proj) if text is None else text
    layers, screens = names(proj)
    res = wui.compile_source(src, "logica.py", layers, screens)
    return {"ok": res.ok, "bytes": res.program.to_bytes() if res.ok and res.program else None,
            "diagnostics": [d.to_dict() for d in res.diagnostics]}


def compile_b64(proj: dict, text: str | None = None) -> dict:
    r = compile_project(proj, text)
    return {"ok": r["ok"], "wuib": base64.b64encode(r["bytes"]).decode() if r["bytes"] else None,
            "diagnostics": r["diagnostics"]}


def _finite(v):
    """JSON del navegador: sin Infinity ni NaN"""
    if isinstance(v, float) and (v != v or v in (float("inf"), float("-inf"))):
        return str(v)
    if isinstance(v, dict):
        return {k: _finite(x) for k, x in v.items()}
    if isinstance(v, (list, tuple)):
        return [_finite(x) for x in v]
    return v


def api() -> dict:
    return _finite(wui.describe_api())
