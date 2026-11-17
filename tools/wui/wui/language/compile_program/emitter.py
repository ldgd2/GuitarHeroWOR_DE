"""Emisor: tablas compartidas del programa (constantes, nombres, libreria, globales) y el codigo de una funcion
con sus etiquetas de salto y su tabla de lineas."""
from __future__ import annotations

from dataclasses import dataclass, field
from typing import Any

from ...shared.opcodes import JUMPS, Op, encode


class Pools:
    """Tablas del programa, sin repetidos."""

    def __init__(self):
        self.consts: list[Any] = []
        self._consts: dict[tuple, int] = {}
        self.names: list[str] = []
        self.builtins: list[str] = []
        self.globals: list[str] = []

    def const(self, v: Any) -> int:
        key = (type(v).__name__, v)
        if key not in self._consts:
            self._consts[key] = len(self.consts)
            self.consts.append(v)
        return self._consts[key]

    @staticmethod
    def _index(table: list[str], s: str) -> int:
        if s not in table:
            table.append(s)
        return table.index(s)

    def name(self, s: str) -> int:
        return self._index(self.names, s)

    def builtin(self, s: str) -> int:
        return self._index(self.builtins, s)

    def glob(self, s: str) -> int:
        return self._index(self.globals, s)


@dataclass
class Label:
    target: int = -1
    uses: list[int] = field(default_factory=list)


class FunctionBuilder:
    def __init__(self, name: str, params: list[str]):
        self.name = name
        self.params = list(params)
        self.locals: dict[str, int] = {p: i for i, p in enumerate(params)}
        self.code: list[list[int]] = []
        self.lines: list[tuple[int, int]] = []
        self._line = -1
        self._temps = 0

    # ------------------------------------------------------------------ variables
    def local(self, name: str) -> int:
        if name not in self.locals:
            self.locals[name] = len(self.locals)
        return self.locals[name]

    def temp(self) -> int:
        self._temps += 1
        return self.local(f"#t{self._temps}")

    # ------------------------------------------------------------------ codigo
    def at(self, node) -> None:
        ln = getattr(node, "lineno", None)
        if ln and ln != self._line:
            self._line = ln
            if self.lines and self.lines[-1][0] == len(self.code):
                self.lines[-1] = (len(self.code), ln)
            else:
                self.lines.append((len(self.code), ln))

    def emit(self, op: Op, a: int = 0, b: int = 0, c: int = 0) -> int:
        self.code.append([int(op), a, b, c])
        return len(self.code) - 1

    def label(self) -> Label:
        return Label()

    def jump(self, op: Op, lab: Label) -> None:
        assert op in JUMPS
        i = self.emit(op, c=lab.target)
        if lab.target < 0:
            lab.uses.append(i)

    def mark(self, lab: Label) -> None:
        lab.target = len(self.code)
        for i in lab.uses:
            self.code[i][3] = lab.target
        lab.uses.clear()

    def bytes(self) -> bytes:
        return b"".join(encode(Op(o), a, b, c) for o, a, b, c in self.code)
