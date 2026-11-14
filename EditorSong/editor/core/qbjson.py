"""QbStruct / QbArray <-> JSON sin perdidas (claves como hash hexadecimal, cadenas en latin-1)."""
from __future__ import annotations

import struct as _st

from qb_io import QbArray, QbMember, QbStruct

from .names import name_of

STR_KINDS = ("string", "wstring")


def _hex(k: int) -> str:
    return f"0x{k:08x}"


def _val_to_json(kind: str, v):
    if kind == "struct":
        return struct_to_json(v)
    if kind == "array":
        return array_to_json(v)
    if kind in STR_KINDS:
        return v.decode("latin-1") if isinstance(v, (bytes, bytearray)) else v
    if kind == "float":  # qb_io guarda los float como sus 32 bits crudos
        return _st.unpack(">f", _st.pack(">I", int(v) & 0xFFFFFFFF))[0]
    return int(v)


def _val_from_json(kind: str, v):
    if kind == "struct":
        return struct_from_json(v)
    if kind == "array":
        return array_from_json(v)
    if kind in STR_KINDS:
        return v.encode("latin-1") if isinstance(v, str) else bytes(v)
    if kind == "float":
        return _st.unpack(">I", _st.pack(">f", float(v)))[0]
    if isinstance(v, str):
        return int(v, 16) if v.startswith("0x") else int(v)
    return int(v)


def struct_to_json(s: QbStruct) -> dict:
    return {"struct": [{"k": m.kind, "n": _hex(m.name), "label": name_of(m.name),
                        "v": _val_to_json(m.kind, m.value)} for m in s.members]}


def struct_from_json(j: dict) -> QbStruct:
    return QbStruct([QbMember(m["k"], int(m["n"], 16), _val_from_json(m["k"], m["v"])) for m in j["struct"]])


def array_to_json(a: QbArray) -> dict:
    return {"elem": a.elem, "values": [_val_to_json(a.elem, x) for x in a.values]}


def array_from_json(j: dict) -> QbArray:
    vals = [_val_from_json(j["elem"], x) for x in j["values"]]
    return QbArray(j["elem"] if vals or j["elem"] != "empty" else "empty", vals)
