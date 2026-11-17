"""Implementacion de referencia del mando: lee el estado que la maquina guarda de cada fotograma."""
from __future__ import annotations

from .specs import BUTTONS, FRETS

VALID = set(BUTTONS.values())


def _b(ctx, v):
    if v not in VALID:
        from difflib import get_close_matches
        near = get_close_matches(str(v), sorted(VALID), 1)
        ctx.vm.fault(f"boton desconocido '{v}'" + (f" (quisiste decir '{near[0]}'?)" if near else ""))
    return v


def pressed(ctx, a, kw): return ctx.vm.pad.pressed(_b(ctx, a[0]), int(a[1]))
def just_pressed(ctx, a, kw): return ctx.vm.pad.just_pressed(_b(ctx, a[0]), int(a[1]))
def just_released(ctx, a, kw): return ctx.vm.pad.just_released(_b(ctx, a[0]), int(a[1]))
def held_time(ctx, a, kw): return ctx.vm.pad.held_time(_b(ctx, a[0]), int(a[1]))
def any_pressed(ctx, a, kw): return bool(ctx.vm.pad.buttons(int(a[0])))
def buttons(ctx, a, kw): return sorted(ctx.vm.pad.buttons(int(a[0])))


def frets(ctx, a, kw):
    on = ctx.vm.pad.buttons(int(a[0]))
    return [f for f in FRETS if f in on]


def chord(ctx, a, kw):
    player, want = int(a[0]), {_b(ctx, b) for b in a[1:]}
    on = {f for f in FRETS if ctx.vm.pad.pressed(f, player)}
    return on == want


def strum(ctx, a, kw):
    p = int(a[0])
    if ctx.vm.pad.just_pressed("strum_up", p):
        return -1
    if ctx.vm.pad.just_pressed("strum_down", p):
        return 1
    return 0


def whammy(ctx, a, kw): return ctx.vm.pad.axis("whammy", int(a[0]))
def tilt(ctx, a, kw): return ctx.vm.pad.axis("tilt", int(a[0]))
def axis(ctx, a, kw): return ctx.vm.pad.axis(str(a[0]), int(a[1]))
def connected(ctx, a, kw): return ctx.vm.pad.connected(int(a[0]))
def kind(ctx, a, kw): return ctx.vm.pad.kind(int(a[0]))


def vibrate(ctx, a, kw):
    ctx.vm.host.vibrate(int(a[2]), float(a[0]), float(a[1]))
