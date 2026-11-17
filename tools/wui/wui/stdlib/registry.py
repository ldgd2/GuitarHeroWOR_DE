"""Registro de la libreria de wui: funciones, metodos, propiedades, eventos y constantes.

Es el contrato unico entre el compilador (comprueba nombres y argumentos), las maquinas (ejecutan) y el editor
(autocompletado, ayuda y bloques). Cada categoria vive en su propio paquete (stdlib/<categoria>/) con sus
especificaciones y su implementacion de referencia, y se registra aqui con register().

Firma de una implementacion:  impl(ctx, args: list, kw: dict) -> valor | Block
  - args ya viene completo: el compilador rellena los valores por defecto de la firma, en orden.
  - kw solo trae los argumentos con nombre extra de las funciones con **props.
"""
from __future__ import annotations

import ast
from dataclasses import dataclass, field
from typing import Any, Callable

REQUIRED = object()


@dataclass(frozen=True)
class Param:
    name: str
    default: Any = REQUIRED
    kind: str = "normal"      # normal | kwonly (despues de *args) | varargs (*args) | varkw (**props)

    @property
    def required(self) -> bool:
        return self.kind in ("normal", "kwonly") and self.default is REQUIRED


@dataclass
class Builtin:
    """Funcion de la libreria (receiver=None) o metodo de un tipo (receiver = Layer, Task, list, str)."""
    name: str
    params: list[Param]
    category: str
    doc: str
    ret: str = "nada"
    receiver: str | None = None
    impl: Callable | None = None
    example: str = ""
    blocking: bool = False       # puede dormir la tarea (wait, wait_press, animate(wait=True)...)

    @property
    def fixed(self) -> list[Param]:
        """parametros con sitio fijo en args: los normales y luego los de solo nombre (los extra de *args van detras)"""
        return [p for p in self.params if p.kind == "normal"] + [p for p in self.params if p.kind == "kwonly"]

    @property
    def varargs(self) -> bool:
        return any(p.kind == "varargs" for p in self.params)

    @property
    def varkw(self) -> bool:
        return any(p.kind == "varkw" for p in self.params)

    def signature(self) -> str:
        out = []
        star = self.varargs
        for p in self.params:
            if p.kind == "varargs":
                out.append("*" + p.name)
            elif p.kind == "varkw":
                out.append("**" + p.name)
            else:
                if p.kind == "kwonly" and not star:
                    out.append("*")
                    star = True
                out.append(p.name if p.default is REQUIRED else f"{p.name}={p.default!r}")
        head = f"{self.receiver}." if self.receiver else ""
        return f"{head}{self.name}({', '.join(out)})"


@dataclass
class Prop:
    receiver: str
    name: str
    type: str
    doc: str
    writable: bool = True
    animatable: bool = False
    default: Any = None


@dataclass
class Event:
    """Decorador de evento: @on_press(Button.RED). params = argumentos del decorador (constantes);
    gives = lo que recibe la funcion (en orden; puede pedir menos)."""
    name: str
    params: list[Param]
    gives: list[str]
    doc: str
    example: str = ""


@dataclass
class Registry:
    functions: dict[str, Builtin] = field(default_factory=dict)          # "pad.pressed", "layer", "clamp"
    methods: dict[tuple[str, str], Builtin] = field(default_factory=dict)  # ("Layer", "glow")
    props: dict[tuple[str, str], Prop] = field(default_factory=dict)       # ("Layer", "alpha")
    events: dict[str, Event] = field(default_factory=dict)
    constants: dict[str, Any] = field(default_factory=dict)                # "PI", "Button.RED"
    namespaces: dict[str, str] = field(default_factory=dict)               # "pad" -> descripcion
    categories: dict[str, str] = field(default_factory=dict)

    # ------------------------------------------------------------------ alta
    def fn(self, name: str, sig: str, doc: str, impl: Callable | None, *, ret: str = "nada", cat: str,
           example: str = "", receiver: str | None = None, blocking: bool = False) -> Builtin:
        b = Builtin(name, parse_sig(sig), cat, doc, ret, receiver, impl, example, blocking)
        if receiver:
            self.methods[(receiver, name)] = b
        else:
            self.functions[name] = b
        return b

    def prop(self, receiver: str, name: str, type: str, doc: str, *, writable: bool = True,
             animatable: bool = False, default: Any = None) -> None:
        self.props[(receiver, name)] = Prop(receiver, name, type, doc, writable, animatable, default)

    def event(self, name: str, sig: str, gives: str, doc: str, example: str = "") -> None:
        self.events[name] = Event(name, parse_sig(sig), [g.strip() for g in gives.split(",") if g.strip()], doc,
                                  example)

    def const(self, name: str, value: Any) -> None:
        self.constants[name] = value

    def namespace(self, name: str, doc: str) -> None:
        self.namespaces[name] = doc

    # ------------------------------------------------------------------ consultas
    def methods_named(self, name: str) -> list[Builtin]:
        return [b for (_, n), b in self.methods.items() if n == name]

    def props_named(self, name: str) -> list[Prop]:
        return [p for (_, n), p in self.props.items() if n == name]

    def constant_groups(self) -> dict[str, dict[str, Any]]:
        out: dict[str, dict[str, Any]] = {}
        for k, v in self.constants.items():
            if "." in k:
                g, n = k.split(".", 1)
                out.setdefault(g, {})[n] = v
        return out


def parse_sig(sig: str) -> list[Param]:
    """'button, player=1, *args, **props' -> [Param]"""
    if not sig.strip():
        return []
    tree = ast.parse(f"def _({sig}): pass").body[0].args
    out = []
    pos = tree.args
    defaults = [ast.literal_eval(d) for d in tree.defaults]
    first_default = len(pos) - len(defaults)
    for i, a in enumerate(pos):
        out.append(Param(a.arg, defaults[i - first_default] if i >= first_default else REQUIRED))
    if tree.vararg:
        out.append(Param(tree.vararg.arg, kind="varargs"))
    for a, d in zip(tree.kwonlyargs, tree.kw_defaults):
        out.append(Param(a.arg, ast.literal_eval(d) if d is not None else REQUIRED, "kwonly"))
    if tree.kwarg:
        out.append(Param(tree.kwarg.arg, kind="varkw"))
    return out


_REGISTRY: Registry | None = None


def registry() -> Registry:
    """El registro completo (todas las categorias), creado una vez."""
    global _REGISTRY
    if _REGISTRY is None:
        from . import CATEGORIES
        reg = Registry()
        for mod in CATEGORIES:
            mod.register(reg)
        _REGISTRY = reg
    return _REGISTRY
