"""Programa compilado de wui: lo que produce el compilador y ejecutan las maquinas.

Formato binario .wuib (little endian), pensado para leerse igual de facil en C++ y en JavaScript:

    "WUIB"  u16 version  u16 flags
    str   nombre del fuente
    u32 n   constantes      (u8 tipo: 0 nada, 1 bool u8, 2 entero i64, 3 decimal f64, 4 texto str, 5 tupla u16 n + items)
    u16 n   nombres         (atributos y metodos: str)
    u16 n   libreria        (funciones de la libreria usadas: str con su nombre completo, p. ej. "pad.pressed")
    u16 n   globales        (str)
    u16 n   funciones       (str nombre, u8 parametros, u16 locales, u8 flags, u32 instrucciones + codigo,
                             u32 n + pares u32 instruccion / u32 linea, u16 n + valores por defecto de los
                             ultimos parametros)
    u16 n   manejadores     (str evento, u16 funcion, u16 n + constantes)
    u16     funcion de arranque (0xFFFF = ninguna)

    str = u32 bytes + UTF-8
"""
from __future__ import annotations

import json
import struct
from dataclasses import dataclass, field
from typing import Any

from .errors import ProgramFormatError
from .opcodes import SIZE, decode

MAGIC = b"WUIB"
VERSION = 2
NO_FUNC = 0xFFFF


@dataclass
class Function:
    name: str
    nparams: int
    nlocals: int
    code: bytes
    lines: list[tuple[int, int]] = field(default_factory=list)   # (instruccion, linea del fuente)
    flags: int = 0                                                  # 1 = manejador de evento
    defaults: list[Any] = field(default_factory=list)               # de los ultimos parametros
    local_names: list[str] = field(default_factory=list)            # para los mensajes de error

    def line_at(self, pc: int) -> int:
        best = 0
        for at, ln in self.lines:
            if at > pc:
                break
            best = ln
        return best

    def instructions(self):
        return decode(self.code)


@dataclass
class Handler:
    event: str
    func: int
    args: list[Any] = field(default_factory=list)


@dataclass
class Program:
    source: str = "<interfaz>"
    consts: list[Any] = field(default_factory=list)
    names: list[str] = field(default_factory=list)
    builtins: list[str] = field(default_factory=list)
    globals: list[str] = field(default_factory=list)
    functions: list[Function] = field(default_factory=list)
    handlers: list[Handler] = field(default_factory=list)
    init: int = NO_FUNC

    # ------------------------------------------------------------------ consultas
    def func(self, name: str) -> Function | None:
        return next((f for f in self.functions if f.name == name), None)

    def handlers_for(self, event: str) -> list[Handler]:
        return [h for h in self.handlers if h.event == event]

    # ------------------------------------------------------------------ binario
    def to_bytes(self) -> bytes:
        out = bytearray(MAGIC)
        out += struct.pack("<HH", VERSION, 0)
        _str(out, self.source)
        out += struct.pack("<I", len(self.consts))
        for c in self.consts:
            _const(out, c)
        for table in (self.names, self.builtins, self.globals):
            out += struct.pack("<H", len(table))
            for s in table:
                _str(out, s)
        out += struct.pack("<H", len(self.functions))
        for f in self.functions:
            _str(out, f.name)
            out += struct.pack("<BHBI", f.nparams, f.nlocals, f.flags, len(f.code) // SIZE)
            out += f.code
            out += struct.pack("<I", len(f.lines))
            for pc, ln in f.lines:
                out += struct.pack("<II", pc, ln)
            out += struct.pack("<H", len(f.defaults))
            for d in f.defaults:
                _const(out, d)
            out += struct.pack("<H", len(f.local_names))
            for n in f.local_names:
                _str(out, n)
        out += struct.pack("<H", len(self.handlers))
        for h in self.handlers:
            _str(out, h.event)
            out += struct.pack("<HH", h.func, len(h.args))
            for a in h.args:
                _const(out, a)
        out += struct.pack("<H", self.init)
        return bytes(out)

    @classmethod
    def from_bytes(cls, data: bytes) -> "Program":
        r = _Reader(data)
        if r.take(4) != MAGIC:
            raise ProgramFormatError("no es un programa de wui (.wuib)")
        ver, _flags = r.unpack("<HH")
        if ver != VERSION:
            raise ProgramFormatError(f"version {ver} del programa; esta libreria entiende la {VERSION}")
        p = cls(source=r.str())
        p.consts = [r.const() for _ in range(r.unpack("<I")[0])]
        p.names = [r.str() for _ in range(r.unpack("<H")[0])]
        p.builtins = [r.str() for _ in range(r.unpack("<H")[0])]
        p.globals = [r.str() for _ in range(r.unpack("<H")[0])]
        for _ in range(r.unpack("<H")[0]):
            name = r.str()
            nparams, nlocals, flags, n = r.unpack("<BHBI")
            code = r.take(n * SIZE)
            lines = [r.unpack("<II") for _ in range(r.unpack("<I")[0])]
            defaults = [r.const() for _ in range(r.unpack("<H")[0])]
            local_names = [r.str() for _ in range(r.unpack("<H")[0])]
            p.functions.append(Function(name, nparams, nlocals, code, lines, flags, defaults, local_names))
        for _ in range(r.unpack("<H")[0]):
            ev = r.str()
            fi, n = r.unpack("<HH")
            p.handlers.append(Handler(ev, fi, [r.const() for _ in range(n)]))
        p.init = r.unpack("<H")[0]
        return p

    # ------------------------------------------------------------------ JSON (para el editor y para depurar)
    def to_json(self) -> dict:
        return {
            "format": "wui", "version": VERSION, "source": self.source,
            "consts": [list(c) if isinstance(c, tuple) else c for c in self.consts],
            "names": self.names, "builtins": self.builtins, "globals": self.globals,
            "functions": [{"name": f.name, "nparams": f.nparams, "nlocals": f.nlocals, "flags": f.flags,
                           "defaults": f.defaults,
                           "code": [list(i) for i in decode(f.code)], "lines": [list(x) for x in f.lines]}
                          for f in self.functions],
            "handlers": [{"event": h.event, "func": h.func, "args": h.args} for h in self.handlers],
            "init": None if self.init == NO_FUNC else self.init,
        }

    def dumps(self) -> str:
        return json.dumps(self.to_json(), ensure_ascii=False)


# ---------------------------------------------------------------------- utilidades del binario
def _str(out: bytearray, s: str) -> None:
    b = s.encode("utf-8")
    out += struct.pack("<I", len(b)) + b


def _const(out: bytearray, c: Any) -> None:
    if c is None:
        out += b"\x00"
    elif isinstance(c, bool):
        out += struct.pack("<BB", 1, int(c))
    elif isinstance(c, int):
        out += struct.pack("<Bq", 2, c)
    elif isinstance(c, float):
        out += struct.pack("<Bd", 3, c)
    elif isinstance(c, str):
        out += b"\x04"
        _str(out, c)
    elif isinstance(c, (tuple, list)):
        out += struct.pack("<BH", 5, len(c))
        for x in c:
            _const(out, x)
    else:
        raise ProgramFormatError(f"constante que no se puede guardar: {c!r}")


class _Reader:
    def __init__(self, data: bytes):
        self.d, self.p = data, 0

    def take(self, n: int) -> bytes:
        if self.p + n > len(self.d):
            raise ProgramFormatError("programa cortado")
        b = self.d[self.p:self.p + n]
        self.p += n
        return b

    def unpack(self, fmt: str):
        s = struct.calcsize(fmt)
        return struct.unpack(fmt, self.take(s))

    def str(self) -> str:
        return self.take(self.unpack("<I")[0]).decode("utf-8")

    def const(self):
        t = self.unpack("<B")[0]
        if t == 0:
            return None
        if t == 1:
            return bool(self.unpack("<B")[0])
        if t == 2:
            return self.unpack("<q")[0]
        if t == 3:
            return self.unpack("<d")[0]
        if t == 4:
            return self.str()
        if t == 5:
            return tuple(self.const() for _ in range(self.unpack("<H")[0]))
        raise ProgramFormatError(f"tipo de constante desconocido {t}")
