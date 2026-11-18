"""Resumen de una captura de frame del render nativo (cache/framecap/frame_<n>.xfc, ver src/de/framecap.cpp).

Recorre los trozos (SHDR, MEM, CALL, SWAP), interpreta el PM4 de cada llamada y cuenta draws, resolves (copias de
EDRAM: RB_MODECONTROL en modo copia), render targets y memoria. Sirve para ver que una captura tiene todo lo que
necesita el reproductor.

Uso:  python tools/framecap_info.py cache/framecap/frame_1.xfc [--calls]
"""
from __future__ import annotations

import argparse
import struct
import sys
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))

FUNCS = {0x822D18A0: "draw", 0x822D1D80: "draw_b", 0x822D2170: "draw_c", 0x822D6180: "draw_d",
         0x822D13C0: "rect_a", 0x822D8690: "rect_b", 0x822D9F90: "rect_c", 0x822DA0E8: "rect_d",
         0x822E9220: "rect_e", 0x822EF320: "rect_f", 0x822E4538: "resolve"}
PRIMS = {1: "puntos", 2: "lineas", 3: "tira de lineas", 4: "triangulos", 5: "abanico", 6: "tira",
         8: "rectangulos", 13: "quads", 7: "tri con adyacencia"}

# bloques de registros copiados en el dispositivo (tools/d3d_device_map.py)
BLOCKS = [(0x4800, 1152, 192), (0x4000, 1920, 1024), (0x4400, 6016, 1024), (0x4900, 10112, 8), (0x4908, 10144, 32),
          (0x2388, 10272, 24), (0x2000, 10368, 16), (0x2080, 10432, 3), (0x2100, 10444, 21), (0x2180, 10528, 5),
          (0x2200, 10548, 12), (0x2280, 10596, 21), (0x2300, 10680, 38), (0x2380, 10832, 8)]
SHADOW_OFF = 1152


def shadow_regs(shadow: bytes) -> dict[int, int]:
    regs = {}
    for reg, off, n in BLOCKS:
        o = off - SHADOW_OFF
        vals = struct.unpack_from(f">{n}I", shadow, o)
        for i, v in enumerate(vals):
            regs[reg + i] = v
    return regs


def walk(dw: list[int], regs: dict[int, int], events: list):
    """PM4 de un tramo: aplica registros y apunta draws."""
    i = 0
    while i < len(dw):
        h = dw[i]
        i += 1
        t = h >> 30
        if t == 0:
            n, r, one = ((h >> 16) & 0x3FFF) + 1, h & 0x7FFF, h & 0x8000
            for k in range(n):
                if i < len(dw):
                    regs[r if one else r + k] = dw[i]
                i += 1
        elif t == 1:
            if i + 1 < len(dw):
                regs[h & 0x7FF] = dw[i]
                regs[(h >> 11) & 0x7FF] = dw[i + 1]
            i += 2
        elif t == 2:
            continue
        else:
            n, op = ((h >> 16) & 0x3FFF) + 1, (h >> 8) & 0x7F
            data = dw[i:i + n]
            if op == 0x2D and n >= 2:
                d0 = data[0]
                typ, idx = (d0 >> 16) & 0xFF, d0 & 0x7FF
                first = {0: 0x4000, 1: 0x4800, 2: 0x4900, 3: 0x4908}.get(typ, 0x2000)
                for k, v in enumerate(data[1:]):
                    regs[first + idx + k] = v
            elif op in (0x22, 0x36):
                init = data[1] if op == 0x22 else data[0]
                events.append({"op": op, "init": init, "regs": dict(regs)})
            elif op == 0x27:
                events.append({"op": op, "addr": data[0] & ~3, "type": data[0] & 3})
            i += n


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("file")
    ap.add_argument("--calls", action="store_true", help="una linea por llamada")
    a = ap.parse_args()
    b = Path(a.file).read_bytes()
    assert b[:4] == b"XFC1", "no es una captura XFC1"
    pos = 16
    shaders, mem_chunks, mem_bytes = {}, 0, 0
    per_func, prims, rts = Counter(), Counter(), Counter()
    draws = resolves = 0
    resolve_list = []
    regs = None
    while pos + 8 <= len(b):
        tag, size = struct.unpack_from("<4sI", b, pos)
        body = b[pos + 8: pos + 8 + size]
        pos += 8 + size
        if tag == b"SHDR":
            typ, addr, ndw = struct.unpack_from("<3I", body)
            shaders[(typ, addr)] = ndw
        elif tag == b"MEM ":
            mem_chunks += 1
            mem_bytes += struct.unpack_from("<2I", body)[1]
        elif tag == b"CALL":
            func, dev = struct.unpack_from("<2I", body)
            nspans = struct.unpack_from("<I", body, 40)[0]
            shadow = body[44:44 + 9712]
            if regs is None:   # estado de la GPU: la copia del dispositivo al empezar, luego solo lo que llega por PM4
                regs = shadow_regs(shadow)
            p = 44 + 9712 + 48
            events = []
            for _ in range(nspans):
                addr, n = struct.unpack_from("<2I", body, p)
                dw = list(struct.unpack_from(f">{n}I", body, p + 8))
                p += 8 + 4 * n
                walk(dw, regs, events)
            name = FUNCS.get(func, hex(func))
            per_func[name] += 1
            for e in events:
                if e["op"] not in (0x22, 0x36):
                    continue
                r = e["regs"]
                mode = r.get(0x2208, 0) & 7   # RB_MODECONTROL.edram_mode: 4 color+profundidad, 6 copia
                surf = r.get(0x2000, 0)       # RB_SURFACE_INFO: pitch en pixeles (14 bits)
                cinfo = r.get(0x2001, 0)      # RB_COLOR_INFO: formato (bits 16-19), base EDRAM (0-11)
                if mode == 6:
                    resolves += 1
                    dest = r.get(0x2319, 0)   # RB_COPY_DEST_BASE
                    pitch = r.get(0x231A, 0)  # RB_COPY_DEST_PITCH: ancho (0-13), alto (16-29)
                    resolve_list.append((draws, dest, pitch & 0x3FFF, (pitch >> 16) & 0x3FFF))
                else:
                    draws += 1
                    prims[PRIMS.get(e["init"] & 0x3F, str(e["init"] & 0x3F))] += 1
                    rts[(surf & 0x3FFF, (cinfo >> 16) & 0xF, cinfo & 0xFFF)] += 1
                if a.calls:
                    print(f"{name:8} modo={mode} prim={e['init'] & 0x3F} indices={e['init'] >> 16}")
        elif tag == b"SWAP":
            break
    print(f"{a.file}: {len(b) >> 20} MB")
    print(f"  llamadas: {dict(per_func)}")
    print(f"  draws: {draws}  resolves: {resolves}")
    print(f"  primitivas: {dict(prims)}")
    print(f"  shaders: {sum(1 for t, _ in shaders if t == 0)} de vertices, {sum(1 for t, _ in shaders if t == 1)} de pixeles")
    print(f"  memoria: {mem_chunks} bloques, {mem_bytes >> 20} MB")
    print("  render targets (pitch, formato, base EDRAM): draws")
    for k, v in rts.most_common(12):
        print(f"    {k}: {v}")
    print("  resolves (draws antes, destino, ancho x alto):")
    for d, dest, w, h in resolve_list:
        print(f"    tras {d:5} draws -> 0x{dest:08X} {w} x {h}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
