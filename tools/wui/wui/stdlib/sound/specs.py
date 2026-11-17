"""Sonidos del menu del juego."""
from __future__ import annotations

from ...shared.values import to_text

CAT = "sonido"

# los nombres que entiende UI_SFX en Warriors of Rock (leidos del bytecode del juego)
SOUNDS = {
    "SELECT": "select", "BACK": "back", "SCROLL_UP": "ScrollUp", "SCROLL_DOWN": "ScrollDown",
    "SCROLL_LEFT": "ScrollLeft", "SCROLL_RIGHT": "ScrollRight", "SELECT_ALT1": "SelectAlt1",
    "SELECT_ALT2": "SelectAlt2", "TOGGLE_ON": "ToggleOn", "TOGGLE_OFF": "ToggleOff", "NEGATIVE": "negativeselect",
    "SCROLL_END": "ScrollEndWarning", "WARNING": "warningmessage", "TOGGLE": "toggle", "SCROLL_UP_BIG": "ScrollUpBig",
    "SCROLL_DOWN_BIG": "ScrollDownBig",
}


def play(ctx, a, kw):
    name = to_text(a[0])
    if name not in SOUNDS.values():
        ctx.vm.log("warn", f"sound(): '{name}' no es un sonido conocido del menu")
    ctx.vm.host.play_sound(name, float(a[1]))


def register(r) -> None:
    r.categories[CAT] = "Sonidos del menu del juego"
    for k, v in SOUNDS.items():
        r.const(f"Sound.{k}", v)
    r.fn("sound", "name, volume=1.0", "Suena un efecto del menu del juego.", play, cat=CAT,
         example="sound(Sound.SELECT)")
