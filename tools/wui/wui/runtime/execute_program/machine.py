"""Caso de uso: ejecutar un programa de wui (maquina de referencia; las del navegador y re-wor hacen lo mismo).

Modelo:
  - Todo corre en TAREAS (como hilos ligeros). Cada evento lanza una tarea con su funcion; spawn() y after()
    tambien. Una tarea corre hasta que espera (wait, wait_press, animate(wait=True)...) o termina, y las demas
    siguen. Un error para solo esa tarea: la interfaz sigue funcionando y el error queda en el registro.
  - Un fotograma (step) hace, en orden: leer el mando, eventos de botones (pulsar, soltar, mantener, repetir,
    rasgueo, elegir), temporizadores (@every), @on_tick, variables del juego (@on_game), correr las tareas
    listas y avanzar las animaciones.
  - Una tarea tiene un presupuesto de instrucciones por fotograma: un bucle sin wait() se corta con un error
    claro en vez de colgar el juego.
"""
from __future__ import annotations

import random
from dataclasses import dataclass, field
from typing import Any

from ...shared.errors import ProgramFormatError, RuntimeFault
from ...shared.opcodes import Op
from ...shared.program import NO_FUNC, Function, Handler, Program
from ...shared.values import Block, FuncRef, Layer, TaskRef, is_number, parse_color, color_hex, receiver_of, \
    to_text, truthy, type_name
from ...stdlib.registry import REQUIRED, Builtin, Registry, registry
from ...stdlib.layers.specs import COLORS, DEFAULTS
from ..animate_layers.engine import Animator
from ..host.protocol import Host
from ..read_input.pad_state import Inputs, PadState
from .operators import binary, compare, format_value, get_index, set_index

UNSET = object()
INIT_NAME = "__init__"
MAX_DEPTH = 200


@dataclass
class _Frame:
    fi: int
    fn: Function
    code: list
    locals: list
    base: int
    pc: int = 0


@dataclass(eq=False)
class _Task:
    id: int
    name: str
    func: int
    args: list
    frames: list = field(default_factory=list)
    stack: list = field(default_factory=list)
    block: Block | None = None
    push_result: bool = True
    start_at: float = 0.0
    every: float | None = None
    done: bool = False
    result: Any = None
    ref: TaskRef | None = None


class _Ctx:
    __slots__ = ("vm", "task")

    def __init__(self, vm, task):
        self.vm, self.task = vm, task


@dataclass
class ErrorRecord:
    message: str
    function: str
    line: int
    time: float
    trace: list[str]


class Machine:
    def __init__(self, program: Program, host: Host | None = None, *, seed: int = 0, budget: int = 200_000,
                 max_tasks: int = 256, reg: Registry | None = None):
        self.prog, self.host = program, host or Host()
        self.reg = reg or registry()
        self.budget, self.max_tasks = budget, max_tasks
        self.rng = random.Random(seed)
        self.code = [f.instructions() for f in program.functions]
        self.builtins: list[Builtin] = []
        for name in program.builtins:
            b = self.reg.functions.get(name)
            if b is None or b.impl is None:
                raise ProgramFormatError(f"el programa usa '{name}', que esta version de la libreria no tiene")
            self.builtins.append(b)
        self.globals: list[Any] = [None] * len(program.globals)
        self.gindex = {n: i for i, n in enumerate(program.globals)}
        self.time = self.dt = 0.0
        self.frame = 0
        self.pad = PadState()
        self.anim = Animator(self)
        self.mouse: tuple[float, float, bool] = (0.0, 0.0, False)
        self.screen: str | None = None
        self.history: list[str] = []
        self.focused: str | None = None
        self.hovered: str | None = None
        self.focus_locked = False
        self.closed = False
        self.started = False
        self.tasks: list[_Task] = []
        self._task_id = 0
        self.errors: list[ErrorRecord] = []
        self.logs: list[tuple[str, str]] = []
        self._holds: set = set()             # (handler, jugador) que ya dispararon en esta pulsacion
        self._repeat: dict = {}              # (handler, jugador) -> proximo momento
        self._every: dict = {}               # handler -> proximo momento
        self._game_last: dict = {}
        self._watch = {h.args[0] for h in program.handlers if h.event == "on_change" and h.args}

    # ================================================================== ciclo de vida
    def start(self, screen: str | None = None) -> None:
        """Valores iniciales de las variables, @on_start y la pantalla inicial (@on_enter)."""
        if self.started:
            return
        self.started = True
        if self.prog.init != NO_FUNC:
            watch, self._watch = self._watch, set()       # los valores iniciales no cuentan como cambio
            try:
                self.call_sync(self.prog.init, [], "el codigo de arriba del todo")
            except RuntimeFault as ex:
                self._error(ex.message, ex.function or INIT_NAME, ex.line or 0, [])
            finally:
                self._watch = watch
        self.fire("on_start", [])
        names = self.host.screen_names()
        first = screen or (names[0] if names else None)
        if first:
            self.screen = first
            self.host.show_screen(first, "auto", None)
            self.fire("on_enter", [first], lambda h: h.args[0] in (None, first))
        self.run_tasks()

    def step(self, dt: float, inputs: Inputs | None = None) -> None:
        """Un fotograma."""
        if not self.started:
            self.start()
        self.dt = max(0.0, float(dt))
        self.time += self.dt
        self.frame += 1
        inputs = inputs or Inputs()
        self.mouse = inputs.mouse
        pressed, released = self.pad.update(inputs, self.time)
        if not self.closed:
            self._input_events(pressed, released)
            self._timers()
            self.fire("on_tick", [self.dt])
            self._game_watch()
        self.run_tasks()
        self.anim.update()

    # ================================================================== eventos
    def handlers(self, event: str) -> list[Handler]:
        return [h for h in self.prog.handlers if h.event == event]

    def fire(self, event: str, args: list, match=None) -> int:
        n = 0
        for h in self.prog.handlers:
            if h.event == event and (match is None or match(h)):
                self._spawn_handler(h, args)
                n += 1
        return n

    def _spawn_handler(self, h: Handler, args: list) -> None:
        fn = self.prog.functions[h.func]
        self._new_task(h.func, self._fill(fn, list(args[:fn.nparams])), f"@{h.event} {fn.name}")

    def _input_events(self, pressed, released) -> None:
        for p, b in pressed:
            self.fire("on_press", [b, p], lambda h: h.args[0] in (None, b) and h.args[1] == p)
            if b in ("strum_up", "strum_down"):
                self.fire("on_strum", [-1 if b == "strum_up" else 1, p], lambda h: h.args[0] == p)
            for i, h in enumerate(self.prog.handlers):
                if h.event == "on_repeat" and h.args[0] == b and h.args[3] == p:
                    self._spawn_handler(h, [b, p])
                    self._repeat[(i, p)] = self.time + float(h.args[2])
            if b == "choose" and self.focused and not self.focus_locked:
                self.notify_click(self.focused)
        for p, b in released:
            self.fire("on_release", [b, p], lambda h: h.args[0] in (None, b) and h.args[1] == p)
            self._holds = {k for k in self._holds if not (k[1] == p and self.prog.handlers[k[0]].args[0] == b)}
            for k in [k for k in self._repeat if k[1] == p and self.prog.handlers[k[0]].args[0] == b]:
                del self._repeat[k]
        for i, h in enumerate(self.prog.handlers):
            if h.event == "on_hold":
                b, secs, p = h.args
                if (i, p) not in self._holds and self.pad.pressed(b, p) and self.pad.held_time(b, p) >= secs - 1e-9:
                    self._holds.add((i, p))
                    self._spawn_handler(h, [b, p])
        for (i, p), at in list(self._repeat.items()):
            h = self.prog.handlers[i]
            if self.time >= at - 1e-9 and self.pad.pressed(h.args[0], p):
                self._spawn_handler(h, [h.args[0], p])
                self._repeat[(i, p)] = at + max(0.01, float(h.args[1]))
        x, y, _ = self.mouse
        over = self.host.layer_at(x, y)
        if over != self.hovered:
            self.hovered = over
            if over:
                self.fire("on_hover", [Layer(over)], lambda h: h.args[0] in (None, over))

    def _timers(self) -> None:
        for i, h in enumerate(self.prog.handlers):
            if h.event == "every":
                secs = max(0.001, float(h.args[0]))
                nxt = self._every.setdefault(i, secs)
                if self.time >= nxt - 1e-9:
                    self._every[i] = nxt + secs
                    self._spawn_handler(h, [])

    def _game_watch(self) -> None:
        for h in self.prog.handlers:
            if h.event == "on_game":
                var = h.args[0]
                v = self.host.game_get(str(var).lstrip("$"))
                if var in self._game_last and self._game_last[var] != v:
                    self._spawn_handler(h, [v, self._game_last[var]])
                self._game_last[var] = v

    # ------------------------------------------------------------------ avisos del anfitrion
    def notify_focus(self, name: str | None) -> None:
        """el anfitrion movio el foco (flechas o raton)"""
        if name == self.focused:
            return
        old, self.focused = self.focused, name
        if old:
            self.fire("on_blur", [Layer(old)], lambda h: h.args[0] in (None, old))
        if name:
            self.fire("on_focus", [Layer(name)], lambda h: h.args[0] in (None, name))

    def notify_click(self, name: str) -> None:
        """se eligio un boton (verde / Intro o clic)"""
        self.fire("on_click", [Layer(name)], lambda h: h.args[0] in (None, name))

    # ================================================================== api para la libreria
    def fault(self, message: str):
        raise RuntimeFault(message)

    def log(self, level: str, message: str) -> None:
        self.logs.append((level, message))
        self.host.log(level, message)

    def check_layer(self, name: str) -> None:
        names = self.host.layer_names()
        if names and name not in names:
            from difflib import get_close_matches
            near = get_close_matches(name, names, 1)
            self.fault(f"no hay ninguna capa llamada '{name}'" + (f" (quisiste decir '{near[0]}'?)" if near else ""))

    def layer_get(self, name: str, prop: str) -> Any:
        if prop == "focused":
            return self.focused == name
        if prop == "animating":
            return self.anim.busy(name)
        if ("Layer", prop) not in self.reg.props:
            self.fault(f"las capas no tienen la propiedad '{prop}'")
        v = self.host.layer_get(name, prop)
        return DEFAULTS.get(prop) if v is None else v

    def layer_set(self, name: str, prop: str, value: Any, from_anim: bool = False) -> None:
        spec = self.reg.props.get(("Layer", prop))
        if spec is None:
            self.fault(f"las capas no tienen la propiedad '{prop}'")
        if not spec.writable:
            self.fault(f"la propiedad '{prop}' de las capas solo se puede leer")
        if prop in COLORS:
            rgb = parse_color(value)
            if rgb is None:
                self.fault(f"{prop}: '{to_text(value)}' no es un color (usa \"#ff0000\", \"red\" o [255, 0, 0])")
            value = color_hex(rgb)
        elif spec.type == "numero":
            if isinstance(value, bool) or not is_number(value):
                self.fault(f"{prop}: se esperaba un numero y llego {type_name(value)} ({to_text(value)})")
            value = float(value)
        elif spec.type == "bool":
            value = truthy(value)
        elif spec.type == "texto":
            value = to_text(value)
        if not from_anim:
            self.anim.stop(name, [prop])
        self.host.layer_set(name, prop, value)

    # ------------------------------------------------------------------ pantallas y foco
    def goto(self, name: str, transition: str = "auto", push: bool = True) -> None:
        names = self.host.screen_names()
        if names and name not in names:
            from difflib import get_close_matches
            near = get_close_matches(name, names, 1)
            self.fault(f"no hay ninguna pantalla llamada '{name}'" + (f" (quisiste decir '{near[0]}'?)" if near else ""))
        if name == self.screen:
            return
        old = self.screen
        if old:
            self.fire("on_exit", [old], lambda h: h.args[0] in (None, old))
            if push:
                self.history.append(old)
        self.screen = name
        self.focused = None
        self.host.show_screen(name, transition, old)
        self.fire("on_enter", [name], lambda h: h.args[0] in (None, name))

    def back(self, transition: str = "auto") -> None:
        if self.history:
            self.goto(self.history.pop(), transition, push=False)
        else:
            self.close()

    def close(self) -> None:
        if self.closed:
            return
        self.closed = True
        if self.screen:
            old = self.screen
            self.fire("on_exit", [old], lambda h: h.args[0] in (None, old))
        self.fire("on_close", [])
        self.host.close()

    def set_focus(self, name: str | None) -> None:
        if name:
            self.check_layer(name)
        self.host.set_focus(name)
        self.notify_focus(name)

    def emit(self, name: str, value: Any) -> None:
        self.fire("on_message", [value], lambda h: h.args[0] == name)

    # ------------------------------------------------------------------ tareas
    def spawn(self, fn: FuncRef, args: list, delay: float = 0.0, every: float | None = None) -> TaskRef:
        f = self.prog.functions[fn.index]
        t = self._new_task(fn.index, self._fill(f, args), f.name)
        t.start_at = self.time + max(0.0, delay)
        t.every = every
        if every is not None and delay <= 0:
            t.start_at = self.time + every
        return t.ref

    def stop_task(self, ref: TaskRef) -> None:
        for t in self.tasks:
            if t.ref is ref:
                t.done = t._stopped = True

    def stop_all(self, except_task=None) -> None:
        for t in self.tasks:
            if t is not except_task:
                t.done = t._stopped = True

    def task_running(self, ref: TaskRef) -> bool:
        return any(t.ref is ref and not t.done for t in self.tasks)

    def task_result(self, ref: TaskRef) -> Any:
        return getattr(ref, "_result", None)

    def call_now(self, fn: FuncRef, args: list) -> Any:
        """llama a una funcion hasta el final (sin esperas): para wait_until y similares"""
        return self.call_sync(fn.index, args, f"{fn.name}()")

    def call_sync(self, fi: int, args: list, what: str) -> Any:
        f = self.prog.functions[fi]
        t = _Task(-1, f.name, fi, self._fill(f, args))
        self._enter(t, fi, t.args)
        self._run(t)
        if not t.done:
            self.fault(f"{what} no puede esperar (wait) aqui")
        return t.result

    def _fill(self, f: Function, args: list) -> list:
        n = f.nparams
        if len(args) > n:
            self.fault(f"{f.name}() recibe como mucho {n} argumento(s) y le llegan {len(args)}")
        missing = n - len(args)
        if missing > len(f.defaults):
            self.fault(f"{f.name}() necesita {n - len(f.defaults)} argumento(s) y le llegan {len(args)}")
        return list(args) + list(f.defaults[len(f.defaults) - missing:]) if missing else list(args)

    def _new_task(self, fi: int, args: list, name: str) -> _Task:
        live = [t for t in self.tasks if not t.done]
        if len(live) >= self.max_tasks:
            self._error(f"demasiadas tareas a la vez ({self.max_tasks}): ¿un spawn dentro de un bucle?", name, 0, [])
            t = _Task(-1, name, fi, args, done=True)
            t.ref = TaskRef(-1, name)
            return t
        self._task_id += 1
        t = _Task(self._task_id, name, fi, args, start_at=self.time)
        t.ref = TaskRef(self._task_id, name)
        self.tasks.append(t)
        return t

    # ================================================================== ejecucion
    def run_tasks(self) -> None:
        rounds = 0
        ran = True
        while ran and rounds < 64:          # las tareas nuevas de este fotograma tambien corren en el
            ran = False
            rounds += 1
            for t in list(self.tasks):
                # cada tarea avanza como mucho una vez por fotograma
                if t.done or t.start_at > self.time + 1e-9 or getattr(t, "_ran", -1) == self.frame:
                    continue
                if t.block is not None:
                    try:
                        ok, val = t.block.poll(self)
                    except RuntimeFault as ex:
                        self._task_error(t, ex)
                        continue
                    if not ok:
                        t._ran = self.frame
                        continue
                    t.block = None
                    if t.push_result:
                        t.stack.append(val)
                if not t.frames:
                    self._enter(t, t.func, t.args)
                t._ran = self.frame
                ran = True
                self._run(t)
                if t.done and t.every is not None and not getattr(t, "_stopped", False):
                    t.done, t.frames, t.stack = False, [], []
                    t.start_at = self.time + t.every
                    t._ran = self.frame
        for t in self.tasks:
            if t.done and t.ref is not None:
                t.ref._result = t.result
        self.tasks = [t for t in self.tasks if not t.done]

    def _enter(self, t: _Task, fi: int, args: list) -> None:
        if len(t.frames) >= MAX_DEPTH:
            self.fault("demasiadas llamadas anidadas (una funcion que se llama a si misma sin parar?)")
        f = self.prog.functions[fi]
        loc = list(args) + [UNSET] * (f.nlocals - len(args))
        t.frames.append(_Frame(fi, f, self.code[fi], loc, len(t.stack)))

    def _task_error(self, t: _Task, ex: RuntimeFault) -> None:
        fr = t.frames[-1] if t.frames else None
        fname = fr.fn.name if fr else t.name
        line = fr.fn.line_at(max(0, fr.pc - 1)) if fr else 0
        trace = [f"{f.fn.name}() linea {f.fn.line_at(max(0, f.pc - 1))}" for f in t.frames]
        t.done = True
        t.frames = []
        self._error(ex.message, fname, line, trace)

    def _error(self, message, fname, line, trace) -> None:
        self.errors.append(ErrorRecord(message, fname, line, self.time, trace))
        where = f" en {fname}()" + (f", linea {line}" if line else "")
        self.log("error", f"{message}{where}")

    def _run(self, t: _Task) -> None:
        try:
            self._exec(t)
        except RuntimeFault as ex:
            if t.id == -1:
                fr = t.frames[-1] if t.frames else None
                raise RuntimeFault(ex.message, fr.fn.name if fr else t.name,
                                   fr.fn.line_at(max(0, fr.pc - 1)) if fr else 0) from None
            self._task_error(t, ex)

    def _exec(self, t: _Task) -> None:
        stack = t.stack
        budget = self.budget
        consts, names, G = self.prog.consts, self.prog.names, self.globals
        while t.frames:
            fr = t.frames[-1]
            code, L = fr.code, fr.locals
            while True:
                budget -= 1
                if budget < 0:
                    self.fault(f"{fr.fn.name}() lleva demasiadas instrucciones sin esperar: si es un bucle, pon "
                               "wait() dentro")
                op, a, b, c = code[fr.pc]
                fr.pc += 1
                if op is Op.CONST:
                    stack.append(consts[c])
                elif op is Op.LOADL:
                    v = L[b]
                    if v is UNSET:
                        nm = fr.fn.local_names[b] if b < len(fr.fn.local_names) else f"#{b}"
                        self.fault(f"la variable '{nm}' se usa antes de darle un valor")
                    stack.append(v)
                elif op is Op.STOREL:
                    L[b] = stack.pop()
                elif op is Op.LOADG:
                    stack.append(G[b])
                elif op is Op.STOREG:
                    v = stack.pop()
                    old = G[b]
                    G[b] = v
                    if self._watch and self.prog.globals[b] in self._watch and not _same(old, v):
                        gname = self.prog.globals[b]
                        self.fire("on_change", [v, old], lambda h: h.args[0] == gname)
                elif op is Op.JMP:
                    fr.pc = c
                elif op is Op.JF:
                    if not truthy(stack.pop()):
                        fr.pc = c
                elif op is Op.JT:
                    if truthy(stack.pop()):
                        fr.pc = c
                elif op is Op.JFK:
                    if not truthy(stack[-1]):
                        fr.pc = c
                    else:
                        stack.pop()
                elif op is Op.JTK:
                    if truthy(stack[-1]):
                        fr.pc = c
                    else:
                        stack.pop()
                elif Op.ADD <= op <= Op.POW:
                    r = stack.pop()
                    stack[-1] = binary(self, op, stack[-1], r)
                elif Op.EQ <= op <= Op.NOTIN:
                    r = stack.pop()
                    stack[-1] = compare(self, op, stack[-1], r)
                elif op is Op.NONE:
                    stack.append(None)
                elif op is Op.TRUE:
                    stack.append(True)
                elif op is Op.FALSE:
                    stack.append(False)
                elif op is Op.POP:
                    stack.pop()
                elif op is Op.DUP:
                    stack.append(stack[-1])
                elif op is Op.SWAP:
                    stack[-1], stack[-2] = stack[-2], stack[-1]
                elif op is Op.NEG:
                    v = stack[-1]
                    if not is_number(v):
                        self.fault(f"no se puede poner '-' delante de {type_name(v)}")
                    stack[-1] = -v
                elif op is Op.POS:
                    if not is_number(stack[-1]):
                        self.fault(f"no se puede poner '+' delante de {type_name(stack[-1])}")
                elif op is Op.NOT:
                    stack[-1] = not truthy(stack[-1])
                elif op is Op.CALLB:
                    spec = self.builtins[b]
                    args = stack[len(stack) - a:]
                    del stack[len(stack) - a:]
                    kw = {}
                    if c >= 0:
                        kn = consts[c]
                        vals = args[len(args) - len(kn):]
                        del args[len(args) - len(kn):]
                        kw = dict(zip(kn, vals))
                    r = spec.impl(_Ctx(self, t), args, kw)
                    if isinstance(r, Block):
                        t.block, t.push_result = r, True
                        return
                    stack.append(r)
                elif op is Op.CALLM:
                    r = self._call_method(t, stack, a, names[b], consts[c] if c >= 0 else ())
                    if isinstance(r, Block):
                        t.block, t.push_result = r, True
                        return
                    stack.append(r)
                elif op is Op.CALL:
                    args = stack[len(stack) - a:]
                    del stack[len(stack) - a:]
                    self._enter(t, b, args)
                    break
                elif op is Op.CALLV:
                    args = stack[len(stack) - a:]
                    del stack[len(stack) - a:]
                    fnv = stack.pop()
                    if not isinstance(fnv, FuncRef):
                        self.fault(f"no se puede llamar a {type_name(fnv)} ({to_text(fnv)}) como si fuera una funcion")
                    self._enter(t, fnv.index, self._fill(self.prog.functions[fnv.index], args))
                    break
                elif op is Op.FREF:
                    stack.append(FuncRef(b, self.prog.functions[b].name))
                elif op is Op.RET or op is Op.RETN:
                    v = stack.pop() if op is Op.RET else None
                    t.frames.pop()
                    del stack[fr.base:]
                    if t.frames:
                        stack.append(v)
                    else:
                        t.result = v
                        t.done = True
                    break
                elif op is Op.WAIT:
                    secs = stack.pop()
                    if not is_number(secs):
                        self.fault(f"wait() necesita segundos (un numero), no {type_name(secs)}")
                    until, first = self.time + max(0.0, float(secs)), self.frame
                    t.block = Block(lambda vm, u=until, f0=first: (vm.frame > f0 and vm.time >= u - 1e-9, None),
                                    "wait")
                    t.push_result = False
                    return
                elif op is Op.GETA:
                    stack[-1] = self._get_attr(stack[-1], names[b])
                elif op is Op.SETA:
                    v = stack.pop()
                    obj = stack.pop()
                    self._set_attr(obj, names[b], v)
                elif op is Op.GETI:
                    i = stack.pop()
                    stack[-1] = get_index(self, stack[-1], i)
                elif op is Op.SETI:
                    v = stack.pop()
                    i = stack.pop()
                    set_index(self, stack.pop(), i, v)
                elif op is Op.LIST:
                    items = stack[len(stack) - b:] if b else []
                    del stack[len(stack) - b:]
                    stack.append(list(items))
                elif op is Op.FORMAT:
                    stack[-1] = format_value(self, stack[-1], consts[c])
                elif op is Op.CONCAT:
                    parts = stack[len(stack) - b:]
                    del stack[len(stack) - b:]
                    stack.append("".join(p if isinstance(p, str) else to_text(p) for p in parts))
                elif op is Op.NOP:
                    pass
                else:
                    self.fault(f"instruccion desconocida {op}")

    # ------------------------------------------------------------------ metodos y atributos
    def _call_method(self, t, stack, argc, name, kwnames):
        args = stack[len(stack) - argc:]
        del stack[len(stack) - argc:]
        obj = stack.pop()
        spec = self.reg.methods.get((receiver_of(obj), name))
        if spec is None:
            self.fault(f"{type_name(obj)} no tiene el metodo .{name}()")
        pos = args[:len(args) - len(kwnames)] if kwnames else args
        kw = dict(zip(kwnames, args[len(args) - len(kwnames):])) if kwnames else {}
        full, extra = bind_runtime(self, spec, [obj] + pos, kw)
        return spec.impl(_Ctx(self, t), full, extra)

    def _get_attr(self, obj, name):
        if isinstance(obj, Layer):
            return self.layer_get(obj.name, name) if name != "name" else obj.name
        if isinstance(obj, TaskRef):
            if name == "running":
                return self.task_running(obj)
            if name == "result":
                return self.task_result(obj)
        self.fault(f"{type_name(obj)} no tiene la propiedad '{name}'")

    def _set_attr(self, obj, name, v):
        if isinstance(obj, Layer):
            self.layer_set(obj.name, name, v)
            return
        self.fault(f"no se puede cambiar '{name}' de {type_name(obj)}")

    # ------------------------------------------------------------------ consultas
    def global_value(self, name: str) -> Any:
        return self.globals[self.gindex[name]]

    def set_global(self, name: str, value: Any) -> None:
        self.globals[self.gindex[name]] = value


def bind_runtime(vm, spec: Builtin, pos: list, kw: dict) -> tuple[list, dict]:
    """ordena los argumentos de un metodo segun su firma (como hace el compilador con las funciones)"""
    fixed = spec.fixed
    normal = [p for p in fixed if p.kind == "normal"]
    slots: dict[str, Any] = {}
    extra_pos = []
    for i, v in enumerate(pos):
        if i < len(normal):
            slots[normal[i].name] = v
        elif spec.varargs:
            extra_pos.append(v)
        else:
            vm.fault(f".{spec.name}() recibe como mucho {len(normal) - 1} argumento(s)")
    extra_kw = {}
    names = {p.name for p in fixed}
    for k, v in kw.items():
        if k in slots:
            vm.fault(f".{spec.name}(): '{k}' esta dos veces")
        if k in names:
            slots[k] = v
        elif spec.varkw:
            extra_kw[k] = v
        else:
            vm.fault(f".{spec.name}() no tiene un argumento '{k}'")
    out = []
    for p in fixed:
        if p.name in slots:
            out.append(slots[p.name])
        elif p.default is REQUIRED:
            vm.fault(f".{spec.name}(): falta el argumento '{p.name}'")
        else:
            out.append(p.default)
    return out + extra_pos, extra_kw


def _same(a, b) -> bool:
    try:
        return type(a) is type(b) and a == b
    except Exception:  # noqa: BLE001
        return False
