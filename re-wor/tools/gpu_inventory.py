"""Inventario del camino grafico de WoR (fase 0 del render nativo, ver docs/RENDER_NATIVO.md).

Lee el codigo recompilado (generated/re_wor_recomp.*.cpp) y la cache de shaders del SDK y responde:
  - donde vive la libreria Direct3D de la 360 dentro del ejecutable y que funciones suyas usa el motor
  - que funciones escriben ordenes a la GPU (paquetes PM4) y de que tipo
  - que funciones del motor dibujan (llegan a un DRAW_INDX)
  - donde estan fijados 1280 x 720 en el codigo
  - cuantos shaders del juego hay capturados (cache/shaders/shareable/*.xsh)

Uso:  python tools/gpu_inventory.py [--json salida.json]
"""
from __future__ import annotations

import argparse
import json
import re
import struct
from collections import Counter, defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
# Libreria D3D de la 360 enlazada en el juego (deducido: todas las ordenes PM4 y las llamadas Vd* del kernel
# salen de aqui; por debajo, 0x822CA000-0x822D0000, estan los envoltorios de XAM / kernel).
D3D_LO, D3D_HI = 0x822D0000, 0x822F1000

PM4 = {0x10: "NOP", 0x3F: "INDIRECT_BUFFER", 0x37: "IB_PFD", 0x26: "WAIT_FOR_IDLE", 0x3C: "WAIT_REG_MEM",
       0x52: "WAIT_REG_EQ", 0x53: "WAIT_REG_GTE", 0x5C: "WAIT_UNTIL_READ", 0x21: "REG_RMW", 0x3E: "REG_TO_MEM",
       0x3D: "MEM_WRITE", 0x4F: "MEM_WRITE_CNTR", 0x44: "COND_EXEC", 0x45: "COND_WRITE", 0x46: "EVENT_WRITE",
       0x58: "EVENT_WRITE_SHD", 0x59: "EVENT_WRITE_CFL", 0x5A: "EVENT_WRITE_EXT", 0x5B: "EVENT_WRITE_ZPD",
       0x22: "DRAW_INDX", 0x36: "DRAW_INDX_2", 0x34: "DRAW_INDX_BIN", 0x35: "DRAW_INDX_2_BIN", 0x23: "VIZ_QUERY",
       0x25: "SET_STATE", 0x2D: "SET_CONSTANT", 0x55: "SET_CONSTANT2", 0x56: "SET_SHADER_CONSTANTS",
       0x2F: "LOAD_ALU_CONSTANT", 0x27: "IM_LOAD", 0x2B: "IM_LOAD_IMMEDIATE", 0x2E: "LOAD_CONSTANT_CONTEXT",
       0x3B: "INVALIDATE_STATE", 0x4A: "SET_SHADER_BASES", 0x48: "ME_INIT"}

FDEF = re.compile(r"^DEFINE_REX_FUNC\((sub_[0-9A-F]+)\)")
CALL = re.compile(r"^\s*(sub_[0-9A-F]+)\(ctx, base\);")
IMP = re.compile(r"^\s*__imp__([A-Za-z_][A-Za-z0-9_]*)\(ctx, base\);")
ASM = re.compile(r"^\s*// (\w[\w.]*)\s*(.*)$")


def addr(k: str) -> int:
    return int(k[4:], 16)


def in_d3d(k: str) -> bool:
    return D3D_LO <= addr(k) < D3D_HI


def call_graph(gen: Path) -> dict:
    g, cur = {}, None
    for f in sorted(gen.glob("re_wor_recomp.*.cpp")):
        with open(f, encoding="utf-8", errors="replace") as fh:
            for line in fh:
                m = FDEF.match(line)
                if m:
                    cur = g.setdefault(m.group(1), {"calls": set(), "imports": set(), "size": 0, "imm": []})
                    continue
                if cur is None:
                    continue
                if m := CALL.match(line):
                    cur["calls"].add(m.group(1))
                elif m := IMP.match(line):
                    cur["imports"].add(m.group(1))
                elif m := ASM.match(line):
                    cur["size"] += 1
                    if m.group(1) in ("lis", "li", "ori"):
                        cur["imm"].append(m.group(1) + " " + m.group(2))
    return g


def pm4_packets(f: dict) -> Counter:
    """cabeceras tipo 3 (0xC0nnOO0p) armadas con lis + ori sobre el mismo registro"""
    hi, found = {}, Counter()
    for s in f["imm"]:
        if m := re.match(r"lis (r\d+),(-?\d+)$", s):
            hi[m.group(1)] = int(m.group(2)) & 0xFFFF
        elif (m := re.match(r"ori (r\d+),(r\d+),(\d+)$", s)) and m.group(2) in hi:
            h = (hi[m.group(2)] << 16) | int(m.group(3))
            op = (h >> 8) & 0x7F
            if h >> 30 == 3 and h & 0xFE == 0 and op in PM4 and (h >> 16) & 0x3FFF < 0x400:
                found[PM4[op]] += 1
    return found


def shader_storage(path: Path) -> dict:
    b = path.read_bytes()
    o, out = 8, Counter()
    sizes = []
    while o + 12 <= len(b):
        w, = struct.unpack_from("<I", b, o + 8)
        n = w & 0x7FFFFFFF
        o += 12 + n * 4
        if o > len(b):
            break
        out["pixel" if w >> 31 else "vertex"] += 1
        sizes.append(n)
    return {"vertex": out["vertex"], "pixel": out["pixel"], "dwords_avg": sum(sizes) // max(len(sizes), 1),
            "dwords_max": max(sizes, default=0)}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--json")
    a = ap.parse_args()
    g = call_graph(ROOT / "generated")
    callers = defaultdict(set)
    for k, v in g.items():
        for c in v["calls"]:
            callers[c].add(k)

    d3d = [k for k in g if in_d3d(k)]
    api = sorted((k for k in d3d if any(not in_d3d(c) for c in callers[k])),
                 key=lambda k: -sum(not in_d3d(c) for c in callers[k]))
    engine_users = {c for k in api for c in callers[k] if not in_d3d(c)}
    pm4 = {k: pm4_packets(v) for k, v in g.items()}
    pm4 = {k: v for k, v in pm4.items() if v}
    outside = sorted(k for k in pm4 if not in_d3d(k))
    video = {k: sorted(i for i in v["imports"] if i.startswith("Vd")) for k, v in g.items()}
    video = {k: v for k, v in video.items() if v}

    draws = [k for k, v in pm4.items() if any(o.startswith("DRAW") for o in v)]
    drawers = set()
    for d in draws:
        todo, seen = [d], set()
        while todo:
            for c in callers[todo.pop()]:
                if c not in seen:
                    seen.add(c)
                    (todo.append(c) if in_d3d(c) else drawers.add(c))

    def lis(v):
        return {int(m.group(1)) for s in v["imm"] if (m := re.match(r"li r\d+,(\d+)$", s))}
    res720 = sorted(k for k, v in g.items() if {1280, 720} <= lis(v))

    xsh = sorted((ROOT / "cache" / "shaders" / "shareable").glob("*.xsh"))
    shaders = shader_storage(xsh[0]) if xsh else None

    print(f"funciones recompiladas: {len(g)}")
    print(f"libreria D3D {D3D_LO:#x}-{D3D_HI:#x}: {len(d3d)} funciones, {len(api)} usadas por el motor "
          f"desde {len(engine_users)} funciones")
    print(f"funciones que escriben PM4: {len(pm4)} (fuera de D3D: {len(outside)})")
    print(f"funciones con llamadas Vd* (video del kernel): {len(video)}")
    print(f"funciones D3D que dibujan: {len(draws)} | funciones del motor que llegan a dibujar: {len(drawers)}")
    print(f"funciones con 1280 y 720 fijos: {len(res720)} {res720}")
    print(f"shaders capturados: {shaders}")
    if a.json:
        json.dump({"d3d_range": [D3D_LO, D3D_HI], "d3d_functions": len(d3d),
                   "api": [{"f": k, "engine_callers": sum(not in_d3d(c) for c in callers[k]), "size": g[k]["size"],
                            "pm4": dict(pm4.get(k, {})), "video": video.get(k, [])} for k in api],
                   "pm4_writers": {k: dict(v) for k, v in pm4.items()}, "video_calls": video,
                   "draw_functions": sorted(draws), "engine_drawers": sorted(drawers, key=addr),
                   "hardcoded_1280x720": res720, "shaders": shaders}, open(a.json, "w"), indent=1)


if __name__ == "__main__":
    main()
