"""Lectura/escritura de PAK de Neversoft (WoR) y del contenedor comprimido CHNK.

Entrada de PAK (32 bytes, big-endian):
  u32 tipo(QBKey ext), u32 offset (relativo a la propia entrada), u32 tamano, u32 ctx,
  u32 nombre completo (QBKey 'songs\\x.note'), u32 nombre corto, u32 padre, u32 flags
CHNK (cada bloque de hasta 0x80000 bytes descomprimidos, deflate crudo):
  'CHNK', u32 0x80, u32 tam_comprimido, u32 offset_siguiente (tamano del bloque alineado a 0x800,
  0xFFFFFFFF en el ultimo), u32 tamano_siguiente, u32 tam_descomprimido, u32 offset_descomprimido,
  relleno hasta 0x80, datos comprimidos, relleno hasta 0x800.
"""
from __future__ import annotations

import struct
import zlib
from dataclasses import dataclass

from wt_chart import qbkey

LAST_TYPES = {qbkey(".last"), 0x2CB3EF3B}
CHUNK = 0x80000


@dataclass
class PakEntry:
    type: int
    ctx: int
    fullname: int
    name: int
    parent: int
    flags: int
    data: bytes
    align: int = 32  # alineacion del dato (se conserva la del archivo original)


def dechnk(d: bytes) -> bytes:
    if d[:4] != b"CHNK":
        return d
    out = bytearray()
    off = 0
    while off + 0x80 <= len(d) and d[off:off + 4] == b"CHNK":
        hdr, comp, nxt, _ns, _dec, _doff = struct.unpack_from(">IIIIII", d, off + 4)
        raw = d[off + hdr: off + hdr + comp]
        try:
            out += zlib.decompress(raw, -15)
        except zlib.error:
            out += zlib.decompress(raw)
        if nxt in (0, 0xFFFFFFFF):
            break
        off += nxt
    return bytes(out)


def chnk(data: bytes) -> bytes:
    blocks = []
    for i in range(0, max(len(data), 1), CHUNK):
        part = data[i:i + CHUNK]
        co = zlib.compressobj(9, zlib.DEFLATED, -15)
        comp = co.compress(part) + co.flush()
        total = (0x80 + len(comp) + 0x7FF) & ~0x7FF
        blocks.append((part, comp, total, i))
    out = bytearray()
    for idx, (part, comp, total, doff) in enumerate(blocks):
        last = idx == len(blocks) - 1
        nxt = 0xFFFFFFFF if last else total
        nsize = 0 if last else blocks[idx + 1][2]
        head = b"CHNK" + struct.pack(">IIIIII", 0x80, len(comp), nxt, nsize, len(part), doff)
        out += head + b"\0" * (0x80 - len(head)) + comp
        out += b"\0" * (total - 0x80 - len(comp))
    return bytes(out)


def read_pak(d: bytes) -> tuple[list[PakEntry], PakEntry | None, int]:
    """Devuelve (entradas, entrada .last, offset del primer dato)."""
    d = dechnk(d)
    entries: list[PakEntry] = []
    last = None
    off = 0
    first_data = len(d)
    while off + 32 <= len(d):
        t, o, size, ctx, full, name, parent, flags = struct.unpack_from(">IIIIIIII", d, off)
        if t in LAST_TYPES or (size == 0 and o == 0):
            last = PakEntry(t, ctx, full, name, parent, flags, d[off + o: off + o + size] if o else b"")
            break
        start = off + o
        first_data = min(first_data, start)
        al = 32
        while al < 0x1000 and start % (al * 2) == 0:
            al *= 2
        entries.append(PakEntry(t, ctx, full, name, parent, flags, d[start:start + size], al))
        off += 32
    return entries, last, first_data


def patch_pak(pak: bytes, replace: dict[int, bytes], align: int = 128) -> bytes:
    """Sustituye el dato de las entradas indicadas (por fullname QBKey) sin mover el resto:
    los datos nuevos se anaden al final (antes de .last); las demas entradas quedan intactas."""
    d = bytearray(dechnk(pak))
    entries = []
    off = 0
    last_idx = None
    while off + 32 <= len(d):
        t, o, size = struct.unpack_from(">III", d, off)
        if t in LAST_TYPES or (size == 0 and o == 0):
            last_idx = off
            break
        entries.append(off)
        off += 32
    if last_idx is None:
        raise ValueError("PAK sin entrada .last")
    lt, lo, lsize = struct.unpack_from(">III", d, last_idx)
    end = last_idx + lo  # donde estaba el dato de .last
    body = d[:end]
    done = set()
    for eoff in entries:
        full = struct.unpack_from(">I", d, eoff + 16)[0]
        if full in replace:
            body += b"\x00" * ((-len(body)) % align)
            struct.pack_into(">II", body, eoff + 4, len(body) - eoff, len(replace[full]))
            body += replace[full]
            done.add(full)
    missing = set(replace) - done
    if missing:
        raise KeyError(f"Entradas no encontradas en el PAK: {[hex(m) for m in missing]}")
    body += b"\x00" * ((-len(body)) % 32)
    struct.pack_into(">I", body, last_idx + 4, len(body) - last_idx)
    body += b"\xab\xab\xab\xab"
    body += b"\x00" * ((-len(body)) % 16)
    body += b"\xab" * ((-len(body)) % 0x1000)
    return bytes(body)


TYPE_ALIGN = {0x745DCD45: 128}  # medido sobre las 167 canciones vanilla; el resto 32


def write_pak(entries: list[PakEntry], header_area: int = 0x1000, last: PakEntry | None = None) -> bytes:
    """Reconstruye un PAK compacto (mismo orden de entradas, alineacion por tipo)."""
    n = len(entries) + 1
    head_size = max(header_area, (n * 32 + 0xFFF) & ~0xFFF)
    out = bytearray(head_size)
    for i, e in enumerate(entries):
        a = TYPE_ALIGN.get(e.type, 32)
        out += bytes((-len(out)) % a)
        struct.pack_into(">IIIIIIII", out, i * 32, e.type, len(out) - i * 32, len(e.data), e.ctx,
                         e.fullname, e.name, e.parent, e.flags)
        out += e.data
    out += bytes((-len(out)) % 32)
    li = len(entries) * 32
    lf = (last.ctx, last.fullname, last.name, last.parent, last.flags) if last else (0, 0, 0, 0, 0)
    struct.pack_into(">IIIIIIII", out, li, 0x2CB3EF3B, len(out) - li, 4, *lf)
    out += bytes([0xAB] * 4)
    out += bytes((-len(out)) % 16)
    out += bytes([0xAB] * ((-len(out)) % 0x1000))
    if len(out) < 0x8000:
        out += bytes([0xAB] * (0x8000 - len(out)))
    return bytes(out)


def replace_entries(pak: bytes, replace: dict[int, bytes]) -> bytes:
    """Sustituye datos de entradas (por fullname) reconstruyendo el PAK compacto."""
    entries, last, first = read_pak(pak)
    found = set()
    for e in entries:
        if e.fullname in replace:
            e.data = replace[e.fullname]
            found.add(e.fullname)
    missing = set(replace) - found
    if missing:
        raise KeyError(f"Entradas no encontradas: {[hex(m) for m in missing]}")
    return write_pak(entries, header_area=first, last=last)
