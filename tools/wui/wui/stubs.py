"""Nombres del lenguaje para que un editor de Python normal (VS Code, PyCharm) reconozca `from wui import *`
y autocomplete. No ejecutan nada: los programas de wui los compila wui y los corre su maquina."""
from __future__ import annotations

from types import SimpleNamespace

from .stdlib.registry import registry

__all__: list[str] = []


def _not_here(name):
    def f(*args, **kwargs):
        raise RuntimeError(f"{name}() solo funciona dentro de una interfaz de wui (compilala con wui.compile_source)")
    f.__name__ = name
    return f


def _event(name):
    def deco(*args, **kwargs):
        if len(args) == 1 and callable(args[0]) and not kwargs:
            return args[0]
        return lambda fn: fn
    deco.__name__ = name
    return deco


def _build() -> dict:
    reg = registry()
    out: dict = {}
    for name, b in reg.functions.items():
        if "." in name:
            ns, fn = name.split(".", 1)
            out.setdefault(ns, SimpleNamespace(__doc__=reg.namespaces.get(ns, "")))
            setattr(out[ns], fn, _not_here(name))
        else:
            f = _not_here(name)
            f.__doc__ = b.doc
            out[name] = f
    for name, e in reg.events.items():
        d = _event(name)
        d.__doc__ = e.doc
        out[name] = d
    for k, v in reg.constants.items():
        if "." in k:
            g, n = k.split(".", 1)
            out.setdefault(g, SimpleNamespace())
            setattr(out[g], n, v)
        else:
            out[k] = v
    return out


_names = _build()
globals().update(_names)
__all__ = sorted(n for n in _names if n not in ("print", "range", "len", "int", "float", "str", "bool", "type",
                                                  "abs", "min", "max", "sum", "round"))
