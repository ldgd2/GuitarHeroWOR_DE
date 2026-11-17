"""Implementacion de referencia de matematicas."""
from __future__ import annotations

import math as M
import random as R

from ...shared.values import is_number, to_text, type_name


def _n(ctx, v):
    if is_number(v):
        return v
    if isinstance(v, bool):
        return int(v)
    ctx.vm.fault(f"se esperaba un numero y llego {type_name(v)} ({to_text(v)})")


def _many(ctx, a):
    vals = a[0] if len(a) == 1 and isinstance(a[0], list) else a
    if not vals:
        ctx.vm.fault("min()/max() sin valores")
    return [_n(ctx, v) for v in vals]


def abs_(ctx, a, kw): return abs(_n(ctx, a[0]))
def min_(ctx, a, kw): return min(_many(ctx, a))
def max_(ctx, a, kw): return max(_many(ctx, a))


def sum_(ctx, a, kw):
    if not isinstance(a[0], list):
        ctx.vm.fault("sum() necesita una lista")
    return sum(_n(ctx, v) for v in a[0])


def round_(ctx, a, kw):
    d = int(_n(ctx, a[1]))
    v = round(_n(ctx, a[0]), d)
    return int(v) if d <= 0 else v


def floor(ctx, a, kw): return M.floor(_n(ctx, a[0]))
def ceil(ctx, a, kw): return M.ceil(_n(ctx, a[0]))


def sqrt(ctx, a, kw):
    x = _n(ctx, a[0])
    if x < 0:
        ctx.vm.fault("sqrt() de un numero negativo")
    return M.sqrt(x)


def sin(ctx, a, kw): return M.sin(M.radians(_n(ctx, a[0])))
def cos(ctx, a, kw): return M.cos(M.radians(_n(ctx, a[0])))
def atan2(ctx, a, kw): return M.degrees(M.atan2(_n(ctx, a[0]), _n(ctx, a[1])))


def clamp(ctx, a, kw):
    x, lo, hi = (_n(ctx, v) for v in a)
    return lo if x < lo else hi if x > hi else x


def lerp(ctx, a, kw):
    x, y, t = (_n(ctx, v) for v in a)
    return x + (y - x) * t


def remap(ctx, a, kw):
    x, lo, hi, c, d = (_n(ctx, v) for v in a)
    if hi == lo:
        return float(c)
    return c + (x - lo) / (hi - lo) * (d - c)


def approach(ctx, a, kw):
    x, t, s = (_n(ctx, v) for v in a)
    s = abs(s)
    return min(t, x + s) if x < t else max(t, x - s)


def sign(ctx, a, kw):
    x = _n(ctx, a[0])
    return (x > 0) - (x < 0)


def random(ctx, a, kw):
    lo, hi = _n(ctx, a[0]), _n(ctx, a[1])
    return lo + ctx.vm.rng.random() * (hi - lo)


def randint(ctx, a, kw):
    lo, hi = int(_n(ctx, a[0])), int(_n(ctx, a[1]))
    if hi < lo:
        ctx.vm.fault(f"randint({lo}, {hi}): el segundo tiene que ser mayor")
    return ctx.vm.rng.randint(lo, hi)


def choice(ctx, a, kw):
    if not isinstance(a[0], list) or not a[0]:
        ctx.vm.fault("choice() necesita una lista con elementos")
    return ctx.vm.rng.choice(a[0])


def shuffle(ctx, a, kw):
    if not isinstance(a[0], list):
        ctx.vm.fault("shuffle() necesita una lista")
    ctx.vm.rng.shuffle(a[0])


def format_number(ctx, a, kw):
    x, d, sep = _n(ctx, a[0]), int(_n(ctx, a[1])), a[2]
    s = f"{x:,.{d}f}" if sep else f"{x:.{d}f}"
    return s


def time_text(ctx, a, kw):
    s = max(0, int(_n(ctx, a[0])))
    h, m, sec = s // 3600, (s // 60) % 60, s % 60
    if a[1] or h:
        return f"{h}:{m:02d}:{sec:02d}"
    return f"{s // 60}:{sec:02d}"


def percent(ctx, a, kw):
    v, t, d = _n(ctx, a[0]), _n(ctx, a[1]), int(_n(ctx, a[2]))
    p = 0.0 if not t else v * 100.0 / t
    return f"{p:.{d}f}%"


_ = R  # el azar va por ctx.vm.rng (con semilla: las pruebas se repiten igual)
