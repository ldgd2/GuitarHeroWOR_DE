"""Caso de uso: leer el mando cada fotograma y saber que se acaba de pulsar, soltar o cuanto lleva pulsado.

El anfitrion manda un Inputs por fotograma (botones pulsados por jugador, ejes, raton). Los botones virtuales
choose / cancel (elegir / atras del menu) los pone el anfitrion segun el mando (guitarra: verde / rojo)."""
from __future__ import annotations

from dataclasses import dataclass, field


@dataclass
class Inputs:
    buttons: dict[int, set[str]] = field(default_factory=dict)       # jugador -> botones pulsados
    axes: dict[int, dict[str, float]] = field(default_factory=dict)  # jugador -> {whammy, tilt, lx, ...}
    mouse: tuple[float, float, bool] = (0.0, 0.0, False)
    kinds: dict[int, str] = field(default_factory=dict)              # jugador -> guitar, pad, keyboard...

    @classmethod
    def of(cls, *buttons: str, player: int = 1, **axes: float) -> "Inputs":
        """atajo para pruebas: Inputs.of("red", "green", whammy=0.5)"""
        return cls({player: set(buttons)}, {player: dict(axes)} if axes else {})


class PadState:
    def __init__(self):
        self.now: Inputs = Inputs()
        self.prev: Inputs = Inputs()
        self.since: dict[tuple[int, str], float] = {}     # (jugador, boton) -> momento en que se pulso
        self.time = 0.0

    def update(self, inputs: Inputs, time: float) -> tuple[list, list]:
        """-> (pulsados ahora, soltados ahora) como listas de (jugador, boton)"""
        self.prev, self.now, self.time = self.now, inputs, time
        pressed, released = [], []
        players = set(inputs.buttons) | set(self.prev.buttons)
        for p in sorted(players):
            cur, old = inputs.buttons.get(p, set()), self.prev.buttons.get(p, set())
            for b in sorted(cur - old):
                self.since[(p, b)] = time
                pressed.append((p, b))
            for b in sorted(old - cur):
                self.since.pop((p, b), None)
                released.append((p, b))
        return pressed, released

    def buttons(self, player: int = 1) -> set[str]:
        return set(self.now.buttons.get(player, set()))

    def pressed(self, b: str, player: int = 1) -> bool:
        return b in self.now.buttons.get(player, ())

    def just_pressed(self, b: str, player: int = 1) -> bool:
        return b in self.now.buttons.get(player, ()) and b not in self.prev.buttons.get(player, ())

    def just_released(self, b: str, player: int = 1) -> bool:
        return b not in self.now.buttons.get(player, ()) and b in self.prev.buttons.get(player, ())

    def held_time(self, b: str, player: int = 1) -> float:
        t = self.since.get((player, b))
        return 0.0 if t is None else max(0.0, self.time - t)

    def axis(self, name: str, player: int = 1) -> float:
        return float(self.now.axes.get(player, {}).get(name, 0.0))

    def connected(self, player: int = 1) -> bool:
        return player in self.now.buttons or player in self.now.kinds or player == 1

    def kind(self, player: int = 1) -> str:
        return self.now.kinds.get(player, "guitar")
