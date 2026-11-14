"""Lectura/escritura tipada de archivos .qb de Neversoft (formato GH World Tour / Warriors of Rock).

Modelo:
  QbFile.items: lista de QbItem(kind, name, value)       kind: 'array' | 'struct' | 'int' | ...
  Valor de array:  QbArray(elem, values)   elem: 'int' 'float' 'qbkey' 'string' 'wstring'
                                                 'struct' 'array' 'empty'
  Valor de struct: QbStruct(members=[QbMember(kind, name, value)])
Codificacion (big-endian):
  cabecera 28 bytes: u32 flags, u32 tamano del archivo, 20 bytes (se conservan)
  item: u32 tipo 0x0020KK00, u32 nombre, u32 clave de archivo, u32 ptr datos, u32 siguiente(0)
  array: u32 tipo 0x0001KK00, u32 n, u32 ptr (n==1 -> dato en linea / puntero directo)
  struct: u32 0x00000100, u32 ptr primer miembro; miembro: u32 tipo, u32 nombre, u32 valor, u32 siguiente
"""
from __future__ import annotations

import struct
from dataclasses import dataclass, field

KIND = {0x00: "empty", 0x01: "int", 0x02: "float", 0x03: "string", 0x04: "wstring",
        0x0A: "struct", 0x0C: "array", 0x0D: "qbkey", 0x1A: "qbkey_s", 0x1B: "qbkey_p", 0x1C: "qskey", 0x07: "k07"}
CODE = {v: k for k, v in KIND.items()}


@dataclass
class QbArray:
    elem: str
    values: list


@dataclass
class QbMember:
    kind: str
    name: int
    value: object


@dataclass
class QbStruct:
    members: list[QbMember] = field(default_factory=list)

    def get(self, name: int, default=None):
        for m in self.members:
            if m.name == name:
                return m.value
        return default


@dataclass
class QbItem:
    kind: str
    name: int
    value: object


def _u32(d: bytes, o: int) -> int:
    return struct.unpack_from(">I", d, o)[0]


def _cstr(d: bytes, o: int) -> bytes:
    return d[o:d.index(b"\0", o)]


def _wstr(d: bytes, o: int) -> bytes:
    e = o
    while d[e:e + 2] != b"\0\0":
        e += 2
    return d[o:e]


class QbFile:
    def __init__(self, data: bytes | None = None, file_key: int = 0):
        self.header = b"\0" * 28
        self.file_key = file_key
        self.items: list[QbItem] = []
        if data is not None:
            self._parse(data)

    # ------------------------------------------------------------------ lectura
    def _parse(self, d: bytes) -> None:
        self.header = d[:28]
        off = 28
        while off + 20 <= len(d):
            typ, name, fkey, dptr, _nxt = struct.unpack_from(">IIIII", d, off)
            if (typ & 0xFFFF00FF) != 0x00200000:
                break
            self.file_key = fkey
            kind = KIND.get((typ >> 8) & 0xFF, hex(typ))
            if kind == "array":
                val, end = self._array(d, dptr)
            elif kind == "struct":
                val, end = self._struct(d, dptr)
            else:
                val, end = dptr, off + 20  # escalar en linea
                if kind == "k07":
                    # tipo 0x07 (datos binarios propios): se busca el siguiente item del mismo archivo
                    nxt = dptr
                    while nxt + 20 <= len(d) and not (
                            (_u32(d, nxt) & 0xFFFF00FF) == 0x00200000 and _u32(d, nxt + 8) == fkey):
                        nxt += 4
                    val = d[dptr:nxt]
                    end = nxt
            self.items.append(QbItem(kind, name, val))
            off = max(end, off + 20)
            off = (off + 3) & ~3

    def _array(self, d: bytes, o: int):
        typ, n = struct.unpack_from(">II", d, o)
        elem = KIND.get((typ >> 8) & 0xFF, hex(typ))
        if n == 0 or elem == "empty":
            return QbArray("empty" if elem == "empty" else elem, []), o + 12
        ptr = _u32(d, o + 8)
        if elem in ("int", "float", "qbkey", "qbkey_s", "qbkey_p", "qskey"):
            base = ptr if n > 1 else o + 8
            vals = [_u32(d, base + 4 * i) for i in range(n)]
            return QbArray(elem, vals), base + 4 * n
        ptrs = [_u32(d, ptr + 4 * i) for i in range(n)] if n > 1 else [ptr]
        end = ptr + 4 * n if n > 1 else o + 12
        vals = []
        for p in ptrs:
            if elem == "struct":
                v, e = self._struct(d, p)
            elif elem == "array":
                v, e = self._array(d, p)
            elif elem == "string":
                v = _cstr(d, p)
                e = (p + len(v) + 1 + 3) & ~3
            elif elem == "wstring":
                v = _wstr(d, p)
                e = (p + len(v) + 2 + 3) & ~3
            else:
                v, e = p, end  # tipo no soportado (vector, etc.): se guarda el puntero (solo lectura)
            vals.append(v)
            end = max(end, e)
        return QbArray(elem, vals), end

    def _struct(self, d: bytes, o: int):
        st = QbStruct()
        end = o + 8
        p = _u32(d, o + 4)
        while p:
            mtyp, name, val, nxt = struct.unpack_from(">IIII", d, p)
            kind = KIND.get((mtyp >> 8) & 0xFF, hex(mtyp))
            end = max(end, p + 16)
            if kind == "struct":
                v, e = self._struct(d, val)
            elif kind == "array":
                v, e = self._array(d, val)
            elif kind == "string":
                v = _cstr(d, val)
                e = (val + len(v) + 1 + 3) & ~3
            elif kind == "wstring":
                v = _wstr(d, val)
                e = (val + len(v) + 2 + 3) & ~3
            else:
                v, e = val, p + 16
            end = max(end, e)
            st.members.append(QbMember(kind, name, v))
            p = nxt
        return st, end

    # ------------------------------------------------------------------ escritura
    def build(self) -> bytes:
        out = bytearray(self.header)
        for it in self.items:
            pos = len(out)
            out += b"\0" * 20
            if it.kind in ("array", "struct"):
                dptr = len(out)
                if it.kind == "array":
                    self._w_array(out, it.value)
                else:
                    self._w_struct(out, it.value)
            elif it.kind == "k07":
                dptr = len(out)
                out += it.value
            else:
                dptr = it.value
            struct.pack_into(">IIIII", out, pos, 0x00200000 | (CODE[it.kind] << 8), it.name,
                             self.file_key, dptr, 0)
        h = self.header
        if len(h) >= 20 and struct.unpack_from(">I", h, 16)[0] == struct.unpack_from(">I", h, 4)[0] - 28:
            struct.pack_into(">I", out, 16, len(out) - 28)  # WoR: tamano del cuerpo (sin cabecera)
        struct.pack_into(">I", out, 4, len(out))
        return bytes(out)

    def _w_array(self, out: bytearray, a: QbArray) -> None:
        o = len(out)
        n = len(a.values)
        if n == 0:
            out += struct.pack(">III", 0x00010000 if a.elem == "empty" else 0x00010000 | (CODE[a.elem] << 8), 0, 0)
            return
        out += struct.pack(">III", 0x00010000 | (CODE[a.elem] << 8), n, 0)
        if a.elem in ("int", "float", "qbkey", "qbkey_s", "qbkey_p", "qskey"):
            if n == 1:
                struct.pack_into(">I", out, o + 8, a.values[0])
            else:
                struct.pack_into(">I", out, o + 8, len(out))
                out += struct.pack(f">{n}I", *a.values)
            return
        if n > 1:
            struct.pack_into(">I", out, o + 8, len(out))
            plist = len(out)
            out += b"\0" * (4 * n)
        for i, v in enumerate(a.values):
            p = len(out)
            if n > 1:
                struct.pack_into(">I", out, plist + 4 * i, p)
            else:
                struct.pack_into(">I", out, o + 8, p)
            if a.elem == "struct":
                self._w_struct(out, v)
            elif a.elem == "array":
                self._w_array(out, v)
            elif a.elem == "string":
                out += v + b"\0"
                out += b"\0" * ((-len(out)) % 4)
            elif a.elem == "wstring":
                out += v + b"\0\0"
                out += b"\0" * ((-len(out)) % 4)

    def _w_struct(self, out: bytearray, s: QbStruct) -> None:
        o = len(out)
        out += struct.pack(">II", 0x00000100, 0)
        prev = None
        for m in s.members:
            p = len(out)
            if prev is None:
                struct.pack_into(">I", out, o + 4, p)
            else:
                struct.pack_into(">I", out, prev + 12, p)
            out += b"\0" * 16
            if m.kind in ("struct", "array", "string", "wstring"):
                val = len(out)
                if m.kind == "struct":
                    self._w_struct(out, m.value)
                elif m.kind == "array":
                    self._w_array(out, m.value)
                elif m.kind == "string":
                    out += m.value + b"\0"
                    out += b"\0" * ((-len(out)) % 4)
                else:
                    out += m.value + b"\0\0"
                    out += b"\0" * ((-len(out)) % 4)
            else:
                val = m.value
            struct.pack_into(">III", out, p, 0x00010000 | (CODE[m.kind] << 8), m.name, val)
            prev = p

    # ------------------------------------------------------------------ utilidades
    def find(self, name: int) -> QbItem | None:
        for it in self.items:
            if it.name == name:
                return it
        return None
