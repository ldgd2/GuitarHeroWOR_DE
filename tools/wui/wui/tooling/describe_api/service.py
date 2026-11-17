"""Caso de uso: describir toda la libreria como datos (JSON) para el editor: autocompletado, ayuda al pasar el
raton, la paleta de bloques y las maquinas de otros lenguajes (que necesitan las firmas y valores por defecto)."""
from __future__ import annotations

from dataclasses import dataclass

from ...shared.opcodes import Op
from ...shared.program import VERSION
from ...stdlib.registry import REQUIRED, Builtin, Registry, registry


@dataclass
class DescribeRequest:
    with_opcodes: bool = True


def _params(b) -> list[dict]:
    out = []
    for p in b.params:
        d = {"name": p.name, "kind": p.kind}
        if p.default is not REQUIRED:
            d["default"] = p.default
        out.append(d)
    return out


def _fn(b: Builtin) -> dict:
    return {"name": b.name, "receiver": b.receiver, "category": b.category, "signature": b.signature(),
            "params": _params(b), "returns": b.ret, "doc": b.doc, "example": b.example, "blocking": b.blocking}


def describe_api(req: DescribeRequest | None = None, reg: Registry | None = None) -> dict:
    req = req or DescribeRequest()
    reg = reg or registry()
    out = {
        "format": "wui-api", "program_version": VERSION,
        "categories": reg.categories,
        "namespaces": reg.namespaces,
        "functions": [_fn(b) for b in reg.functions.values()],
        "methods": [_fn(b) for b in reg.methods.values()],
        "properties": [{"receiver": p.receiver, "name": p.name, "type": p.type, "doc": p.doc,
                        "writable": p.writable, "animatable": p.animatable, "default": p.default}
                       for p in reg.props.values()],
        "events": [{"name": e.name, "params": _params(e), "gives": e.gives, "doc": e.doc, "example": e.example}
                   for e in reg.events.values()],
        "constants": reg.constants,
        "keywords": ["if", "elif", "else", "while", "for", "in", "break", "continue", "def", "return", "global",
                     "and", "or", "not", "True", "False", "None", "pass"],
    }
    if req.with_opcodes:
        out["opcodes"] = {o.name: int(o) for o in Op}
    return out
