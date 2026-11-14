"""Escritura del formato .note de Guitar Hero: Warriors of Rock.

Cabecera (28 bytes, big-endian):
  u32 0x40C001A3, u32 QBKey(cancion), u32 numero de secciones, u32 QBKey("note"), 12 bytes a cero
Seccion:
  u32 QBKey(nombre), u32 numero, u32 QBKey(tipo), u32 tamano de elemento, datos
Verificado contra data/songs/lasso_song.pak.xen.
"""
from __future__ import annotations

import struct

from wt_chart import DIFFS, Chart, qbkey

MAGIC = 0x40C001A3


def _wstr_fixed(text: str, chars: int) -> bytes:
    enc = text.encode("utf-16-be")[: (chars - 1) * 2]
    return enc + b"\0" * (chars * 2 - len(enc))


class NoteWriter:
    def __init__(self, song: str):
        self.song = song.lower()
        self.sections: list[bytes] = []

    def add(self, name: str, typ: str, elem_size: int, elems: list[bytes]) -> None:
        for e in elems:
            assert len(e) == elem_size, (name, len(e), elem_size)
        head = struct.pack(">IIII", qbkey(name), len(elems), qbkey(typ), elem_size)
        self.sections.append(head + b"".join(elems))

    def build(self) -> bytes:
        head = struct.pack(">IIII", MAGIC, qbkey(self.song), len(self.sections), qbkey("note")) + b"\0" * 12
        return head + b"".join(self.sections)


def chart_to_note(ch: Chart) -> bytes:
    w = NoteWriter(ch.name)
    for instr in ("guitar", "bass", "drums"):
        for diff in DIFFS:
            notes = sorted(ch.notes.get((instr, diff), []), key=lambda n: n.time)
            if instr == "drums" and diff == "expert":
                elems = [struct.pack(">IHBBB", n.time, min(n.length, 0xFFFF), n.mask, n.accent, 0) for n in notes]
                w.add(f"drums{diff}instrument", "gh6_expert_drum_note", 9, elems)
            else:
                elems = [struct.pack(">IHBB", n.time, min(n.length, 0xFFFF), n.mask, n.accent) for n in notes]
                w.add(f"{instr}{diff}instrument", "gh5_instrument_note", 8, elems)
            stars = ch.star.get((instr, diff), [])
            w.add(f"{instr}{diff}starpower", "gh5_star_note", 6,
                  [struct.pack(">IH", t, min(l, 0xFFFF)) for t, l in stars])
            if instr != "drums":
                w.add(f"{instr}{diff}tapping", "gh5_tapping_note", 8, [])
    for diff in DIFFS:
        w.add(f"{diff}drumfill", "gh5_drumfill_note", 8,
              [struct.pack(">II", a, b) for a, b in ch.drumfill.get(diff, [])])
    w.add("fretbar", "gh5_fretbar_note", 4, [struct.pack(">I", t) for t in ch.fretbars])
    w.add("timesig", "gh5_timesig_note", 6, [struct.pack(">IBB", t, n, d) for t, n, d in ch.timesig] or
          [struct.pack(">IBB", 0, 4, 4)])
    w.add("vocals", "gh5_vocal_note", 7,
          [struct.pack(">IHB", t, min(l, 0xFFFF), p & 0xFF) for t, l, p in ch.vocals])
    w.add("vocalphrase", "gh5_vocal_phrase", 4, [struct.pack(">I", t) for t in ch.vocal_phrases])
    w.add("vocalfreeform", "gh5_vocal_freeform_note", 10,
          [struct.pack(">IIH", t, l, 0) for t, l in ch.vocal_freeform])
    w.add("vocallyrics", "gh5_vocal_lyric", 68,
          [struct.pack(">I", t) + _wstr_fixed(txt, 32) for t, txt in ch.lyrics])
    w.add("vocalsmarkers", "gh5_vocal_marker_note", 260,
          [struct.pack(">I", t) + _wstr_fixed(txt, 128) for t, txt in ch.vocal_markers])
    w.add("vocalstarpower", "gh5_star_note", 6, [])
    w.add("guitarmarkers", "gh5_marker_note", 8, [])
    w.add("bandmoment", "gh5_band_moment_note", 8, [])
    return w.build()
