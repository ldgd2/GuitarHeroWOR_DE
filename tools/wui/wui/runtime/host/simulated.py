"""Anfitrion simulado: capas y pantallas en memoria, juego falso y un diario de todo lo que se pidio.
Sirve para las pruebas, para la linea de comandos (python -m wui run) y como ejemplo de anfitrion."""
from __future__ import annotations

from dataclasses import dataclass, field
from typing import Any

from ...stdlib.layers.specs import DEFAULTS
from .protocol import Host


@dataclass
class SimLayer:
    name: str
    x: float = 640.0
    y: float = 360.0
    w: float = 200.0
    h: float = 80.0
    kind: str = "shape"
    screen: str = ""
    focusable: bool = False
    props: dict = field(default_factory=dict)


class SimulatedHost(Host):
    def __init__(self, layers: list[SimLayer] | list[str] | None = None, screens: list[str] | None = None,
                 game: dict | None = None):
        self.layers: dict[str, SimLayer] = {}
        for L in layers or []:
            L = SimLayer(L) if isinstance(L, str) else L
            L.props = {**DEFAULTS, "x": L.x, "y": L.y, "focusable": L.focusable, **L.props}
            self.layers[L.name] = L
        self.screens = list(screens or ["principal"])
        self.game: dict[str, Any] = dict(game or {})
        self.players: dict[tuple[int, str], Any] = {}
        self.journal: list[tuple] = []        # (que, ...) en orden
        self.logs: list[tuple[str, str]] = []
        self.current: str | None = None
        self.focus: str | None = None
        self.closed = False

    # ------------------------------------------------------------------ capas
    def layer_names(self):
        return list(self.layers)

    def layer_get(self, name, prop):
        L = self.layers.get(name)
        if L is None:
            return None
        if prop in ("w", "h", "kind", "name"):
            return {"w": L.w, "h": L.h, "kind": L.kind, "name": L.name}[prop]
        return L.props.get(prop)

    def layer_set(self, name, prop, value):
        L = self.layers.get(name)
        if L is not None:
            L.props[prop] = value
            self.journal.append(("set", name, prop, value))

    def layer_design(self, name):
        L = self.layers.get(name)
        return {**DEFAULTS, "x": L.x, "y": L.y} if L else {}

    def layer_contains(self, name, x, y):
        L = self.layers.get(name)
        if not L:
            return False
        s = (L.props.get("scale") or 1.0)
        px, py = L.props.get("x", L.x), L.props.get("y", L.y)
        return abs(x - px) <= L.w * s / 2 and abs(y - py) <= L.h * s / 2

    def layer_at(self, x, y):
        for n in reversed(list(self.layers)):
            if self.layers[n].props.get("visible", True) and self.layer_contains(n, x, y):
                return n
        return None

    # ------------------------------------------------------------------ pantallas
    def screen_names(self):
        return list(self.screens)

    def show_screen(self, name, transition, previous):
        self.current = name
        self.journal.append(("screen", name, transition))

    def close(self):
        self.closed = True
        self.journal.append(("close",))

    def set_focus(self, name):
        self.focus = name
        self.journal.append(("focus", name))

    def move_focus(self, direction):
        self.journal.append(("move_focus", direction))

    def lock_focus(self, locked):
        self.journal.append(("lock_focus", locked))

    # ------------------------------------------------------------------ juego
    def game_get(self, name):
        return self.game.get(name)

    def game_set(self, name, value):
        self.game[name] = value
        self.journal.append(("game_set", name, value))

    def game_call(self, script, params):
        self.journal.append(("game_call", script, params))
        return None

    def player_info(self, player, field):
        return self.players.get((player, field), 0)

    def band_info(self, field):
        return self.players.get((0, field), 0)

    def game_state(self, what):
        return self.game.get("#" + what)

    # ------------------------------------------------------------------ otros
    def play_sound(self, name, volume):
        self.journal.append(("sound", name))

    def vibrate(self, player, strength, seconds):
        self.journal.append(("vibrate", player, strength, seconds))

    def log(self, level, message):
        self.logs.append((level, message))

    # ------------------------------------------------------------------ consultas para pruebas
    def prop(self, layer: str, prop: str):
        return self.layers[layer].props.get(prop)

    def events(self, kind: str) -> list[tuple]:
        return [j for j in self.journal if j[0] == kind]
