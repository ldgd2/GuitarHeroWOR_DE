"""Implementacion de referencia de las capas. Todo pasa por la maquina (vm.layer_get / layer_set) y su motor de
animaciones (vm.anim), que a su vez le dice al anfitrion que cambie cada propiedad."""
from __future__ import annotations

import math

from ...shared.values import Block, Layer, is_number, parse_color, to_text, type_name
from .specs import ANIMATABLE, DEFAULTS


def _L(ctx, v) -> str:
    if not isinstance(v, Layer):
        ctx.vm.fault(f"se esperaba una capa y llego {type_name(v)} ({to_text(v)})")
    return v.name


def _num(ctx, v, what):
    if not is_number(v):
        ctx.vm.fault(f"{what}: se esperaba un numero y llego {type_name(v)} ({to_text(v)})")
    return float(v)


def _maybe_wait(ctx, ids, wait):
    if wait and ids:
        return ctx.vm.anim.wait_for(ids)
    return None


def layer(ctx, a, kw):
    name = a[0]
    if not isinstance(name, str):
        ctx.vm.fault(f"layer() necesita el nombre de la capa (texto), no {type_name(name)}")
    ctx.vm.check_layer(name)
    return Layer(name)


def layers(ctx, a, kw):
    p = to_text(a[0]) if a[0] else ""
    return [Layer(n) for n in ctx.vm.host.layer_names() if n.startswith(p)]


def exists(ctx, a, kw):
    return a[0] in ctx.vm.host.layer_names()


# ------------------------------------------------------------------ visibilidad
def show(ctx, a, kw):
    n, t = _L(ctx, a[0]), _num(ctx, a[1], "time")
    ctx.vm.layer_set(n, "visible", True)
    if t > 0:
        ctx.vm.layer_set(n, "alpha", 0.0)
        ctx.vm.anim.tween(n, "alpha", 1.0, t, "out")


def hide(ctx, a, kw):
    n, t = _L(ctx, a[0]), _num(ctx, a[1], "time")
    if t > 0:
        ctx.vm.anim.tween(n, "alpha", 0.0, t, "in", on_end=lambda vm: vm.layer_set(n, "visible", False))
    else:
        ctx.vm.layer_set(n, "visible", False)


def toggle(ctx, a, kw):
    n = _L(ctx, a[0])
    ctx.vm.layer_set(n, "visible", not ctx.vm.layer_get(n, "visible"))


def fade_in(ctx, a, kw):
    n, t = _L(ctx, a[0]), _num(ctx, a[1], "time")
    if not ctx.vm.layer_get(n, "visible"):
        ctx.vm.layer_set(n, "alpha", 0.0)
    ctx.vm.layer_set(n, "visible", True)
    return _maybe_wait(ctx, [ctx.vm.anim.tween(n, "alpha", 1.0, t, "out")], a[2])


def fade_out(ctx, a, kw):
    n, t = _L(ctx, a[0]), _num(ctx, a[1], "time")
    i = ctx.vm.anim.tween(n, "alpha", 0.0, t, "in", on_end=lambda vm: vm.layer_set(n, "visible", False))
    return _maybe_wait(ctx, [i], a[2])


# ------------------------------------------------------------------ propiedades
def set_(ctx, a, kw):
    n = _L(ctx, a[0])
    for k, v in kw.items():
        ctx.vm.layer_set(n, k, v)


def get(ctx, a, kw):
    return ctx.vm.layer_get(_L(ctx, a[0]), to_text(a[1]))


def reset(ctx, a, kw):
    n, t = _L(ctx, a[0]), _num(ctx, a[1], "time")
    ctx.vm.anim.stop(n)
    design = ctx.vm.host.layer_design(n)
    for k, dflt in DEFAULTS.items():
        v = design.get(k, dflt)
        if t > 0 and k in ANIMATABLE:
            ctx.vm.anim.tween(n, k, v, t, "out")
        else:
            ctx.vm.layer_set(n, k, v)


def animate(ctx, a, kw):
    n = _L(ctx, a[0])
    t, ease, delay, loop, yoyo, wait = a[1:7]
    if not kw:
        ctx.vm.fault("animate() sin propiedades: p. ej. animate(x=300, alpha=0.5, time=0.4)")
    ids = []
    for k, v in kw.items():
        if k not in ANIMATABLE:
            ctx.vm.fault(f"la propiedad '{k}' no se puede animar" + (" (usa set())" if k in DEFAULTS else ""))
        ids.append(ctx.vm.anim.tween(n, k, v, _num(ctx, t, "time"), to_text(ease), _num(ctx, delay, "delay"),
                                     int(_num(ctx, loop, "loop")), bool(yoyo)))
    return _maybe_wait(ctx, ids, wait)


def stop(ctx, a, kw):
    ctx.vm.anim.stop(_L(ctx, a[0]))


def _to(ctx, n, props: dict, t, ease, wait):
    ids = [ctx.vm.anim.tween(n, k, v, _num(ctx, t, "time"), to_text(ease)) for k, v in props.items()]
    return _maybe_wait(ctx, ids, wait)


def move_to(ctx, a, kw):
    n = _L(ctx, a[0])
    return _to(ctx, n, {"x": _num(ctx, a[1], "x"), "y": _num(ctx, a[2], "y")}, a[3], a[4], a[5])


def move_by(ctx, a, kw):
    n = _L(ctx, a[0])
    x, y = ctx.vm.anim.target(n, "x"), ctx.vm.anim.target(n, "y")
    return _to(ctx, n, {"x": x + _num(ctx, a[1], "dx"), "y": y + _num(ctx, a[2], "dy")}, a[3], a[4], a[5])


def scale_to(ctx, a, kw):
    return _to(ctx, _L(ctx, a[0]), {"scale": _num(ctx, a[1], "scale")}, a[2], a[3], a[4])


def rotate_to(ctx, a, kw):
    return _to(ctx, _L(ctx, a[0]), {"rot": _num(ctx, a[1], "degrees")}, a[2], a[3], a[4])


# ------------------------------------------------------------------ efectos
def _fx(ctx, n, values: dict, t):
    t = _num(ctx, t, "time")
    for k, v in values.items():
        if t > 0:
            ctx.vm.anim.tween(n, k, v, t, "out")
        else:
            ctx.vm.layer_set(n, k, v)


def _color(ctx, v, what="color"):
    if parse_color(v) is None:
        ctx.vm.fault(f"{what}: '{to_text(v)}' no es un color (usa \"#ff0000\", \"red\" o [255, 0, 0])")
    return v


def glow(ctx, a, kw):
    n = _L(ctx, a[0])
    ctx.vm.layer_set(n, "glow_color", _color(ctx, a[2]))
    _fx(ctx, n, {"glow": _num(ctx, a[1], "size"), "glow_alpha": _num(ctx, a[3], "alpha")}, a[4])


def unglow(ctx, a, kw):
    _fx(ctx, _L(ctx, a[0]), {"glow": 0.0}, a[1])


def blur(ctx, a, kw):
    _fx(ctx, _L(ctx, a[0]), {"blur": max(0.0, _num(ctx, a[1], "px"))}, a[2])


def shadow(ctx, a, kw):
    n = _L(ctx, a[0])
    ctx.vm.layer_set(n, "shadow_color", _color(ctx, a[2]))
    _fx(ctx, n, {"shadow": _num(ctx, a[1], "blur"), "shadow_x": _num(ctx, a[3], "x"),
                 "shadow_y": _num(ctx, a[4], "y"), "shadow_alpha": _num(ctx, a[5], "alpha")}, a[6])


def outline(ctx, a, kw):
    n = _L(ctx, a[0])
    ctx.vm.layer_set(n, "outline_color", _color(ctx, a[2]))
    _fx(ctx, n, {"outline": _num(ctx, a[1], "width")}, a[3])


def tint(ctx, a, kw):
    _fx(ctx, _L(ctx, a[0]), {"tint": _color(ctx, a[1])}, a[2])


def untint(ctx, a, kw):
    _fx(ctx, _L(ctx, a[0]), {"tint": "#ffffff"}, a[1])


def pulse(ctx, a, kw):
    n, prop = _L(ctx, a[0]), to_text(a[1])
    if prop not in ANIMATABLE or prop in ("tint", "glow_color", "shadow_color", "outline_color"):
        ctx.vm.fault(f"pulse(): '{prop}' no es una propiedad numerica animable")
    amount, period, times = _num(ctx, a[2], "amount"), _num(ctx, a[3], "period"), int(_num(ctx, a[4], "times"))
    base = ctx.vm.anim.target(n, prop)
    loops = -1 if times < 0 else max(0, times * 2 - 1)
    ctx.vm.anim.tween(n, prop, base + amount, max(0.02, period / 2), "inout", loop=loops, yoyo=True)


def shake(ctx, a, kw):
    n, amount, t = _L(ctx, a[0]), _num(ctx, a[1], "amount"), _num(ctx, a[2], "time")
    for prop, phase in (("x", 0.0), ("y", 1.7)):
        base = ctx.vm.anim.target(n, prop)
        ctx.vm.anim.tween(n, prop, base, t, "linear",
                          fn=lambda u, b=base, ph=phase: b + math.sin(u * 50 + ph) * amount * (1 - u))


def flash(ctx, a, kw):
    n, t = _L(ctx, a[0]), _num(ctx, a[2], "time")
    ctx.vm.layer_set(n, "glow_color", _color(ctx, a[1]))
    ctx.vm.layer_set(n, "glow", max(24.0, ctx.vm.layer_get(n, "glow") or 0.0))
    ctx.vm.layer_set(n, "glow_alpha", 1.0)
    ctx.vm.layer_set(n, "brightness", 1.8)
    ctx.vm.anim.tween(n, "glow_alpha", 0.0, t, "out")
    ctx.vm.anim.tween(n, "brightness", 1.0, t, "out")


def bounce(ctx, a, kw):
    n, hgt, t = _L(ctx, a[0]), _num(ctx, a[1], "height"), _num(ctx, a[2], "time")
    base = ctx.vm.anim.target(n, "y")
    ctx.vm.anim.tween(n, "y", base, t, "linear", fn=lambda u: base - math.sin(u * math.pi) * hgt)


def spin(ctx, a, kw):
    n, speed = _L(ctx, a[0]), _num(ctx, a[1], "speed")
    ctx.vm.anim.stop(n, ["rot"])
    if speed:
        base = ctx.vm.layer_get(n, "rot") or 0.0
        ctx.vm.anim.tween(n, "rot", base, 1.0, "linear", loop=-1, fn=lambda u, s=speed, b=base: b + s * u,
                          accumulate=True)


def set_text(ctx, a, kw):
    ctx.vm.layer_set(_L(ctx, a[0]), "text", to_text(a[1]))


def set_image(ctx, a, kw):
    ctx.vm.layer_set(_L(ctx, a[0]), "image", to_text(a[1]))


def focus(ctx, a, kw):
    ctx.vm.set_focus(_L(ctx, a[0]))


def contains(ctx, a, kw):
    return bool(ctx.vm.host.layer_contains(_L(ctx, a[0]), _num(ctx, a[1], "x"), _num(ctx, a[2], "y")))


def busy(ctx, a, kw):
    return ctx.vm.anim.busy(_L(ctx, a[0]))


_ = Block
