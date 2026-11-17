"""Implementacion de referencia del juego: todo pasa al anfitrion (en re-wor, el puente QB)."""
from __future__ import annotations

from ...shared.values import is_number, to_text, type_name
from .specs import BAND_FIELDS, PLAYER_FIELDS


def _name(ctx, v, what):
    if not isinstance(v, str) or not v:
        ctx.vm.fault(f"{what}: se esperaba un nombre (texto) y llego {type_name(v)}")
    return v.lstrip("$")


def get(ctx, a, kw):
    v = ctx.vm.host.game_get(_name(ctx, a[0], "game.get"))
    return a[1] if v is None else v


def set_(ctx, a, kw):
    v = a[1]
    if not (is_number(v) or isinstance(v, (str, bool))):
        ctx.vm.fault(f"game.set(): solo numeros, textos o bool (llego {type_name(v)})")
    ctx.vm.host.game_set(_name(ctx, a[0], "game.set"), v)


def call(ctx, a, kw):
    return ctx.vm.host.game_call(_name(ctx, a[0], "game.call"), dict(kw))


def player(ctx, a, kw):
    field = to_text(a[0])
    if field not in PLAYER_FIELDS:
        ctx.vm.fault(f"game.player(): campo '{field}' (hay: {', '.join(PLAYER_FIELDS)})")
    return ctx.vm.host.player_info(int(a[1]), field)


def band(ctx, a, kw):
    field = to_text(a[0])
    if field not in BAND_FIELDS:
        ctx.vm.fault(f"game.band(): campo '{field}' (hay: {', '.join(BAND_FIELDS)})")
    return ctx.vm.host.band_info(field)


def open_menu(ctx, a, kw):
    st = _name(ctx, a[0], "game.open_menu")
    ctx.vm.host.game_call("ui_event", {"event": "menu_change", "data": {"state": st}})


def in_song(ctx, a, kw): return bool(ctx.vm.host.game_state("in_song"))
def paused(ctx, a, kw): return bool(ctx.vm.host.game_state("paused"))
def language(ctx, a, kw): return to_text(ctx.vm.host.game_state("language") or "en")
