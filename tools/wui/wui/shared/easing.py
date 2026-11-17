"""Curvas de animacion (las mismas formulas en todas las maquinas). t va de 0 a 1."""
from __future__ import annotations

import math


def _out_bounce(t: float) -> float:
    n, d = 7.5625, 2.75
    if t < 1 / d:
        return n * t * t
    if t < 2 / d:
        t -= 1.5 / d
        return n * t * t + 0.75
    if t < 2.5 / d:
        t -= 2.25 / d
        return n * t * t + 0.9375
    t -= 2.625 / d
    return n * t * t + 0.984375


_C1 = 1.70158
_C3 = _C1 + 1

EASES = {
    "linear": lambda t: t,
    "in": lambda t: t * t,
    "out": lambda t: 1 - (1 - t) ** 2,
    "inout": lambda t: 2 * t * t if t < 0.5 else 1 - (-2 * t + 2) ** 2 / 2,
    "in_cubic": lambda t: t ** 3,
    "out_cubic": lambda t: 1 - (1 - t) ** 3,
    "inout_cubic": lambda t: 4 * t ** 3 if t < 0.5 else 1 - (-2 * t + 2) ** 3 / 2,
    "in_back": lambda t: _C3 * t ** 3 - _C1 * t * t,
    "out_back": lambda t: 1 + _C3 * (t - 1) ** 3 + _C1 * (t - 1) ** 2,
    "out_elastic": lambda t: t if t in (0, 1) else 2 ** (-10 * t) * math.sin((t * 10 - 0.75) * (2 * math.pi / 3)) + 1,
    "out_bounce": _out_bounce,
    "in_bounce": lambda t: 1 - _out_bounce(1 - t),
    "step": lambda t: 1.0 if t >= 1 else 0.0,
}


def ease(name: str, t: float) -> float:
    t = 0.0 if t < 0 else 1.0 if t > 1 else t
    return EASES.get(name, EASES["out"])(t)
