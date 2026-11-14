"""Lectura de charts de Guitar Hero World Tour / WTDE (archivo .qb dentro de a<Song>_song.pak.xen).

Formato QB de WT (big-endian):
  cabecera de 28 bytes, luego items de 20 bytes:
    u32 tipo (0x0020xx00), u32 nombre (QBKey), u32 clave de archivo, u32 offset de datos, u32 siguiente
  Arrays: u32 tipo (0x0001xx00), u32 numero, u32 puntero (si numero==1 los datos van en linea).
"""
from __future__ import annotations

import struct
import zlib
from dataclasses import dataclass, field

T_INT = 0x00010100
T_FLOAT = 0x00010200
T_STRING = 0x00010300
T_WSTRING = 0x00010400
T_STRUCT = 0x00010A00
T_ARRAY = 0x00010C00
T_QBKEY = 0x00010D00
T_EMPTY = 0x00010000

# Tipos de miembros de struct (version WT)
M_INT = 0x00000100
M_FLOAT = 0x00000200
M_STRING = 0x00000300
M_WSTRING = 0x00000400
M_STRUCT = 0x00000A00
M_ARRAY = 0x00000C00
M_QBKEY = 0x00000D00


def qbkey(text: str) -> int:
    return (~zlib.crc32(text.lower().replace("/", "\\").encode("utf-8"))) & 0xFFFFFFFF


def _u32(d: bytes, off: int) -> int:
    return struct.unpack_from(">I", d, off)[0]


def _f32(v: int) -> float:
    return struct.unpack(">f", struct.pack(">I", v))[0]


def _cstr(d: bytes, off: int) -> str:
    end = d.index(b"\0", off)
    return d[off:end].decode("latin-1")


def _wstr(d: bytes, off: int) -> str:
    out = []
    while off + 2 <= len(d):
        c = struct.unpack_from(">H", d, off)[0]
        if c == 0:
            break
        out.append(chr(c))
        off += 2
    return "".join(out)


class QbFile:
    """Parser minimo del QB de WT: devuelve {QBKey del nombre: valor python}."""

    def __init__(self, data: bytes):
        self.d = data
        self.items: dict[int, object] = {}
        self._parse()

    def _parse(self) -> None:
        d = self.d
        off = 28
        while off + 20 <= len(d):
            typ, name, fkey, dptr, _nxt = struct.unpack_from(">IIIII", d, off)
            if (typ & 0xFFFF00FF) != 0x00200000 or dptr == 0 or dptr >= len(d):
                break
            kind = typ & 0xFF00
            if kind == 0x0C00:
                self.items[name] = self._array(dptr)
            elif kind == 0x0A00:
                self.items[name] = self._struct(dptr)
            elif kind == 0x0100:
                self.items[name] = dptr  # entero en linea
            else:
                self.items[name] = None
            # Siguiente item: buscar la siguiente cabecera con la misma clave de archivo.
            o = off + 20
            nxt = None
            while o + 12 <= len(d):
                if _u32(d, o + 8) == fkey and (_u32(d, o) & 0xFFFF00FF) == 0x00200000 and _u32(d, o + 4):
                    nxt = o
                    break
                o += 4
            if nxt is None:
                break
            off = nxt

    def _array(self, off: int):
        d = self.d
        typ, n = struct.unpack_from(">II", d, off)
        if n == 0 or typ == T_EMPTY:
            return []
        ptr = _u32(d, off + 8)
        base = ptr if n > 1 else off + 8
        if typ in (T_INT, T_QBKEY):
            return [_u32(d, base + 4 * i) for i in range(n)]
        if typ == T_FLOAT:
            return [_f32(_u32(d, base + 4 * i)) for i in range(n)]
        ptrs = [_u32(d, ptr + 4 * i) for i in range(n)] if n > 1 else [ptr]
        if typ == T_ARRAY:
            return [self._array(p) for p in ptrs]
        if typ == T_STRUCT:
            return [self._struct(p) for p in ptrs]
        if typ == T_STRING:
            return [_cstr(d, p) for p in ptrs]
        if typ == T_WSTRING:
            return [_wstr(d, p) for p in ptrs]
        return []

    def _struct(self, off: int) -> dict:
        """Struct de WT: u32 cabecera (0x00000100), u32 puntero al primer miembro."""
        d = self.d
        res: dict[int, object] = {}
        if off + 8 > len(d):
            return res
        p = _u32(d, off + 4)
        guard = 0
        while p and p + 16 <= len(d) and guard < 512:
            guard += 1
            mtyp, mname, mval, mnext = struct.unpack_from(">IIII", d, p)
            kind = mtyp & 0xFF00
            if kind == M_INT:
                v = mval if mval < 0x80000000 else mval - 0x100000000
            elif kind == M_FLOAT:
                v = _f32(mval)
            elif kind == M_QBKEY:
                v = mval
            elif kind == M_STRING:
                v = _cstr(d, mval)
            elif kind == M_WSTRING:
                v = _wstr(d, mval)
            elif kind == M_STRUCT:
                v = self._struct(mval)
            elif kind == M_ARRAY:
                v = self._array(mval)
            else:
                v = mval
            res[mname] = v
            p = mnext
        return res

    def get(self, name: str, default=None):
        return self.items.get(qbkey(name), default)


# --------------------------------------------------------------------------- modelo


@dataclass
class Note:
    time: int
    length: int
    mask: int  # bits 0-4 trastes/pads, bit5 open/bombo, bit6 HOPO forzado
    accent: int = 0


@dataclass
class Chart:
    name: str
    fretbars: list[int] = field(default_factory=list)
    timesig: list[tuple[int, int, int]] = field(default_factory=list)
    notes: dict[tuple[str, str], list[Note]] = field(default_factory=dict)  # (instr, diff)
    star: dict[tuple[str, str], list[tuple[int, int]]] = field(default_factory=dict)
    drumfill: dict[str, list[tuple[int, int]]] = field(default_factory=dict)
    vocals: list[tuple[int, int, int]] = field(default_factory=list)
    vocal_phrases: list[int] = field(default_factory=list)
    vocal_freeform: list[tuple[int, int]] = field(default_factory=list)
    lyrics: list[tuple[int, str]] = field(default_factory=list)
    vocal_markers: list[tuple[int, str]] = field(default_factory=list)


DIFFS = ("easy", "medium", "hard", "expert")
# instrumento WoR -> prefijo del array de notas de WT
WT_PARTS = {"guitar": "song", "bass": "song_rhythm", "drums": "song_drum"}
WT_STAR = {"guitar": "", "bass": "rhythm_", "drums": "drum_"}


def _unpack_wt_note(time: int, v: int, instr: str) -> Note:
    length = v & 0xFFFF
    mask = (v >> 16) & 0x3F
    forced = (v >> 22) & 0x1
    accent = (v >> 23) & 0x1F
    if instr == "drums":
        return Note(time, length, mask, accent)
    if forced:
        mask |= 0x40
    # WoR usa 0x1F como "acento" en guitarra/bajo en todas las notas.
    return Note(time, length, mask, 0x1F)


def parse_qs(data: bytes) -> dict[int, str]:
    """Tabla de cadenas .qs (UTF-16, lineas 'xxxxxxxx "texto"')."""
    try:
        text = data.decode("utf-16")
    except UnicodeDecodeError:
        return {}
    res: dict[int, str] = {}
    for line in text.splitlines():
        line = line.strip()
        if len(line) < 11 or line[8] != " ":
            continue
        try:
            key = int(line[:8], 16)
        except ValueError:
            continue
        val = line[9:].strip()
        if val.startswith('"') and val.endswith('"'):
            val = val[1:-1]
        res[key] = val
    return res


def _clean_lyric(text: str) -> str:
    for prefix in (r"\L", r"\u[m]"):
        if text.startswith(prefix):
            text = text[len(prefix):]
    return text


def load_wt_chart(qb_data: bytes, song: str, qs: dict[int, str] | None = None) -> Chart:
    qs = qs or {}
    qb = QbFile(qb_data)
    s = song.lower()
    ch = Chart(name=s)
    ch.fretbars = list(qb.get(f"{s}_fretbars", []) or [])
    for ts in qb.get(f"{s}_timesig", []) or []:
        if isinstance(ts, list) and len(ts) >= 3:
            ch.timesig.append((ts[0], ts[1], ts[2]))
    for instr, part in WT_PARTS.items():
        for diff in DIFFS:
            arr = qb.get(f"{s}_{part}_{diff}", []) or []
            ch.notes[(instr, diff)] = [
                _unpack_wt_note(arr[i], arr[i + 1], instr) for i in range(0, len(arr) - 1, 2)
            ]
            stars = qb.get(f"{s}_{WT_STAR[instr]}{diff}_star", []) or []
            ch.star[(instr, diff)] = [(x[0], x[1]) for x in stars if isinstance(x, list) and len(x) >= 2]
    for diff in DIFFS:
        fills = qb.get(f"{s}_{diff}_drumfill", []) or []
        ch.drumfill[diff] = [(x[0], x[1]) for x in fills if isinstance(x, list) and len(x) >= 2]
    voc = qb.get(f"{s}_song_vocals", []) or []
    ch.vocals = [(voc[i], voc[i + 1], voc[i + 2]) for i in range(0, len(voc) - 2, 3)]
    ph = qb.get(f"{s}_vocals_phrases", []) or []
    ch.vocal_phrases = [ph[i] for i in range(0, len(ph), 2)]
    for ff in qb.get(f"{s}_vocals_freeform", []) or []:
        if isinstance(ff, list) and len(ff) >= 2:
            ch.vocal_freeform.append((ff[0], ff[1]))
    k_time, k_text, k_marker = qbkey("time"), qbkey("text"), qbkey("marker")
    for st in qb.get(f"{s}_lyrics", []) or []:
        if isinstance(st, dict) and k_time in st:
            txt = st.get(k_text, "")
            if isinstance(txt, int):
                txt = qs.get(txt, "")
            ch.lyrics.append((int(st[k_time]), _clean_lyric(str(txt))))
    for st in qb.get(f"{s}_vocals_markers", []) or []:
        if isinstance(st, dict) and k_time in st:
            txt = st.get(k_marker, "")
            if isinstance(txt, int):
                txt = qs.get(txt, "")
            ch.vocal_markers.append((int(st[k_time]), _clean_lyric(str(txt))))
    return ch
