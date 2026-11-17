"""Caso de uso: animar propiedades de capas en el tiempo (interpolaciones con curva, repeticiones, ida y vuelta,
colores y funciones propias como temblar o saltar). Cada fotograma calcula los valores y se los pasa a la
maquina, que los manda al anfitrion."""
from __future__ import annotations

from dataclasses import dataclass, field
from typing import Any, Callable

from ...shared.easing import ease as ease_fn
from ...shared.values import Block, color_hex, is_number, parse_color


@dataclass
class Tween:
    id: int
    layer: str
    prop: str
    start: Any
    end: Any
    duration: float
    ease: str
    delay: float
    loops: int                 # repeticiones extra (-1 = siempre)
    yoyo: bool
    t0: float
    fn: Callable | None = None
    accumulate: bool = False
    on_end: Callable | None = None
    cycle: int = 0
    done: bool = False
    started: bool = False

    def value(self, u: float) -> Any:
        if self.fn is not None:
            return self.fn(self.cycle + u if self.accumulate else u)
        k = ease_fn(self.ease, u)
        a, b = self.start, self.end
        if is_number(a) and is_number(b):
            return a + (b - a) * k
        ca, cb = parse_color(a), parse_color(b)
        if ca and cb:
            return color_hex(tuple(round(x + (y - x) * k) for x, y in zip(ca, cb)))
        return b if k >= 1 else a


@dataclass
class Animator:
    vm: Any
    tweens: list[Tween] = field(default_factory=list)
    _next: int = 1

    def tween(self, layer: str, prop: str, to: Any, time: float, ease: str = "out", delay: float = 0.0,
              loop: int = 0, yoyo: bool = False, on_end: Callable | None = None, fn: Callable | None = None,
              accumulate: bool = False, frm: Any = None) -> int:
        self.stop(layer, [prop])
        tw = Tween(self._next, layer, prop, frm, to, max(0.0, float(time)), ease or "out", max(0.0, float(delay)),
                   int(loop), bool(yoyo), self.vm.time, fn, accumulate, on_end)
        self._next += 1
        self.tweens.append(tw)
        if tw.duration <= 0 and tw.delay <= 0 and fn is None:          # instantaneo
            self.vm.layer_set(layer, prop, to, from_anim=True)
            tw.done = True
            self.tweens.remove(tw)
            if on_end:
                on_end(self.vm)
        return tw.id

    def update(self) -> None:
        now = self.vm.time
        for tw in list(self.tweens):
            if tw.done:
                continue
            t = now - tw.t0 - tw.delay
            if t < 0:
                continue
            if not tw.started:
                tw.started = True
                if tw.start is None:
                    tw.start = self.vm.layer_get(tw.layer, tw.prop)
                    if tw.start is None:
                        tw.start = tw.end
            dur = tw.duration if tw.duration > 0 else 1e-6
            cycle = int(t // dur)
            if tw.loops >= 0 and cycle > tw.loops:
                u, finished = 1.0, True
                cycle = tw.loops
            else:
                u, finished = (t - cycle * dur) / dur, False
            tw.cycle = cycle
            if tw.yoyo and cycle % 2 == 1:
                u = 1.0 - u
            if finished and tw.fn is not None and not tw.accumulate:
                val = tw.start if tw.yoyo and tw.loops % 2 == 1 else tw.fn(1.0)
            else:
                val = tw.value(u)
            self.vm.layer_set(tw.layer, tw.prop, val, from_anim=True)
            if finished:
                tw.done = True
                self.tweens.remove(tw)
                if tw.on_end:
                    tw.on_end(self.vm)

    def stop(self, layer: str, props: list[str] | None = None) -> None:
        self.tweens = [t for t in self.tweens if not (t.layer == layer and (props is None or t.prop in props))]

    def busy(self, layer: str) -> bool:
        return any(t.layer == layer for t in self.tweens)

    def target(self, layer: str, prop: str) -> Any:
        """a donde va la propiedad (el final de su animacion, o su valor actual)"""
        for t in reversed(self.tweens):
            if t.layer == layer and t.prop == prop and t.fn is None and t.loops == 0:
                return t.end
        v = self.vm.layer_get(layer, prop)
        return 0.0 if v is None else v

    def wait_for(self, ids: list[int]) -> Block:
        want = set(ids)
        return Block(lambda vm: (not any(t.id in want for t in self.tweens), None), "animate")
