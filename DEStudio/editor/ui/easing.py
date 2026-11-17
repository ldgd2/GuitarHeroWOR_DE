"""Curvas de transicion entre claves: las mismas que web/src/keyframes/easing.js (el exportador tiene que
reproducir en el juego exactamente lo que se ve en el editor)."""
from __future__ import annotations

from math import cos, floor, pi, pow, sin, sqrt

C1 = 1.70158
C2 = C1 * 1.525
C3 = C1 + 1
C4 = (2 * pi) / 3
C5 = (2 * pi) / 4.5


def _bounce_out(x: float) -> float:
    n1, d1 = 7.5625, 2.75
    if x < 1 / d1:
        return n1 * x * x
    if x < 2 / d1:
        x -= 1.5 / d1
        return n1 * x * x + 0.75
    if x < 2.5 / d1:
        x -= 2.25 / d1
        return n1 * x * x + 0.9375
    x -= 2.625 / d1
    return n1 * x * x + 0.984375


IN = {
    "sine": lambda x: 1 - cos((x * pi) / 2),
    "quad": lambda x: x * x,
    "cubic": lambda x: x * x * x,
    "quart": lambda x: x ** 4,
    "quint": lambda x: x ** 5,
    "expo": lambda x: 0.0 if x == 0 else pow(2, 10 * x - 10),
    "circ": lambda x: 1 - sqrt(max(0.0, 1 - x * x)),
    "back": lambda x: C3 * x * x * x - C1 * x * x,
    "elastic": lambda x: 0.0 if x == 0 else 1.0 if x == 1 else -pow(2, 10 * x - 10) * sin((x * 10 - 10.75) * C4),
    "bounce": lambda x: 1 - _bounce_out(1 - x),
}


def _back_inout(x: float) -> float:
    if x < 0.5:
        return (pow(2 * x, 2) * ((C2 + 1) * 2 * x - C2)) / 2
    return (pow(2 * x - 2, 2) * ((C2 + 1) * (x * 2 - 2) + C2) + 2) / 2


def _elastic_inout(x: float) -> float:
    if x == 0 or x == 1:
        return x
    if x < 0.5:
        return -(pow(2, 20 * x - 10) * sin((20 * x - 11.125) * C5)) / 2
    return (pow(2, -20 * x + 10) * sin((20 * x - 11.125) * C5)) / 2 + 1


def _out(f):
    return lambda x: 1 - f(1 - x)


def _inout(f):
    return lambda x: f(2 * x) / 2 if x < 0.5 else 1 - f(2 - 2 * x) / 2


def _outin(f):
    o = _out(f)
    return lambda x: o(2 * x) / 2 if x < 0.5 else 0.5 + f(2 * x - 1) / 2


FN = {
    "linear": lambda x: x,
    "smooth": lambda x: x,
    "hold": lambda x: 0.0,
    "easeIn": IN["cubic"],
    "easeOut": _out(IN["cubic"]),
    "easeInOut": _inout(IN["cubic"]),
}
for _fam, _f in IN.items():
    FN[_fam + "In"] = _f
    FN[_fam + "Out"] = _out(_f)
    FN[_fam + "InOut"] = {"back": _back_inout, "elastic": _elastic_inout}.get(_fam, _inout(_f))
    FN[_fam + "OutIn"] = _outin(_f)


def bezier(b, x: float) -> float:
    x1, y1, x2, y2 = b or (0.42, 0, 0.58, 1)
    cx = 3 * x1
    bx = 3 * (x2 - x1) - cx
    ax = 1 - cx - bx
    cy = 3 * y1
    by = 3 * (y2 - y1) - cy
    ay = 1 - cy - by

    def X(t):
        return ((ax * t + bx) * t + cx) * t

    t = x
    for _ in range(8):
        e = X(t) - x
        if abs(e) < 1e-6:
            break
        d = (3 * ax * t + 2 * bx) * t + cx
        if abs(d) < 1e-6:
            break
        t -= e / d
    if abs(X(t) - x) > 1e-4:
        lo, hi, t = 0.0, 1.0, x
        for _ in range(30):
            if X(t) < x:
                lo = t
            else:
                hi = t
            t = (lo + hi) / 2
    return ((ay * t + by) * t + cy) * t


def value(name: str, key: dict | None, x: float) -> float:
    """f(x) de la transicion que termina en la clave `key` (0..1 -> 0..1)."""
    x = min(1.0, max(0.0, x))
    if name == "bezier":
        return bezier((key or {}).get("bez"), x)
    if name == "steps":
        n = max(1, int((key or {}).get("steps") or 4))
        return min(1.0, floor(x * n) / n)
    return FN.get(name, FN["linear"])(x)


def is_linear(name: str) -> bool:
    return name in ("linear", "smooth", None, "")
