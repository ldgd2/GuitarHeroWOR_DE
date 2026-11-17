"""Pistas con tiempo de la cancion.

  .perf   autocutcameras / momentcameras: [tiempo u32, duracion u16, camara u8] (registros de 7 bytes)
  .mid.qb <cancion>_lightshow_notes, _anim_notes, _crowd_notes: pares [tiempo, vel<<24 | nota<<16 | duracion]
  .note   secciones de 8 bytes [tiempo, duracion] (bandmoment), solo lectura
"""
from __future__ import annotations

import struct

from ..core.names import qbkey

K_CAM_NOTE = qbkey("gh5_camera_note")
NOTE_TRACKS = ("lightshow_notes", "anim_notes", "crowd_notes")
_CAM_KEYS = {qbkey("autocutcameras"): "auto", qbkey("momentcameras"): "moment"}


def perf_tracks(perf: bytes) -> dict[str, list]:
    out = {}
    cnt = struct.unpack_from(">I", perf, 8)[0]
    off = 0x1C
    for _ in range(cnt):
        nm, n, typ = struct.unpack_from(">III", perf, off)
        size = 7 if typ == K_CAM_NOTE else 1000
        if size == 7 and nm in _CAM_KEYS:
            out[_CAM_KEYS[nm]] = [list(struct.unpack_from(">IHB", perf, off + 12 + i * 7)) for i in range(n)]
        off += 12 + n * size
    return out


def perf_write(perf: bytes, tracks: dict[str, list]) -> bytes:
    cnt = struct.unpack_from(">I", perf, 8)[0]
    out = bytearray(perf[:0x1C])
    off = 0x1C
    for _ in range(cnt):
        nm, n, typ = struct.unpack_from(">III", perf, off)
        size = 7 if typ == K_CAM_NOTE else 1000
        key = _CAM_KEYS.get(nm)
        if key and key in tracks:
            cuts = sorted(((int(t), max(1, min(int(ln), 0xFFFF)), int(c) & 0xFF) for t, ln, c in tracks[key]),
                          key=lambda c: c[0])
            out += struct.pack(">III", nm, len(cuts), typ) + b"".join(struct.pack(">IHB", *c) for c in cuts)
        else:
            out += perf[off: off + 12 + n * size]
        off += 12 + n * size
    return bytes(out)


def decode_notes(vals: list[int]) -> list[list[int]]:
    return [[vals[i], (vals[i + 1] >> 16) & 0xFF, vals[i + 1] & 0xFFFF, (vals[i + 1] >> 24) & 0xFF]
            for i in range(0, len(vals) - 1, 2)]


def encode_notes(notes: list) -> list[int]:
    out = []
    for t, note, ln, vel in sorted(notes, key=lambda n: int(n[0])):  # estable: respeta el orden de empates
        out += [int(t), ((int(vel) & 0xFF) << 24) | ((int(note) & 0xFF) << 16) | (int(ln) & 0xFFFF)]
    return out


def note_section(note: bytes, section: str) -> list[list[int]]:
    n = struct.unpack_from(">I", note, 8)[0]
    off = 28
    for _ in range(n):
        name, cnt, _typ, size = struct.unpack_from(">IIII", note, off)
        off += 16
        if name == qbkey(section) and size == 8:
            return [list(struct.unpack_from(">II", note, off + i * 8)) for i in range(cnt)]
        off += cnt * size
    return []
