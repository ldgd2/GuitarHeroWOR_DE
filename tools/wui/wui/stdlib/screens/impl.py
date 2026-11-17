"""Implementacion de referencia de pantallas, foco y raton."""
from __future__ import annotations

from ...shared.values import Layer, to_text, type_name

DIRS = ("up", "down", "left", "right")


def goto(ctx, a, kw):
    if not isinstance(a[0], str):
        ctx.vm.fault(f"goto() necesita el nombre de la pantalla, no {type_name(a[0])}")
    ctx.vm.goto(a[0], to_text(a[1]))


def back(ctx, a, kw): ctx.vm.back(to_text(a[0]))
def close(ctx, a, kw): ctx.vm.close()
def screen(ctx, a, kw): return ctx.vm.screen
def screens(ctx, a, kw): return list(ctx.vm.host.screen_names())
def history(ctx, a, kw): return list(ctx.vm.history)


def focus(ctx, a, kw):
    t = a[0]
    if isinstance(t, Layer):
        t = t.name
    if t is not None and not isinstance(t, str):
        ctx.vm.fault(f"focus() necesita una capa o su nombre, no {type_name(t)}")
    ctx.vm.set_focus(t)


def focused(ctx, a, kw):
    return Layer(ctx.vm.focused) if ctx.vm.focused else None


def focus_move(ctx, a, kw):
    d = to_text(a[0]).lower()
    if d not in DIRS:
        ctx.vm.fault(f"focus_move(): direccion '{d}' (usa up, down, left o right)")
    ctx.vm.host.move_focus(d)


def lock_focus(ctx, a, kw):
    ctx.vm.focus_locked = bool(a[0])
    ctx.vm.host.lock_focus(ctx.vm.focus_locked)


def mouse_x(ctx, a, kw): return ctx.vm.mouse[0]
def mouse_y(ctx, a, kw): return ctx.vm.mouse[1]
def mouse_down(ctx, a, kw): return bool(ctx.vm.mouse[2])


def hovered(ctx, a, kw):
    n = ctx.vm.host.layer_at(ctx.vm.mouse[0], ctx.vm.mouse[1])
    return Layer(n) if n else None
