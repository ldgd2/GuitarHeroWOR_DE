"""Mapa de la estructura del dispositivo D3D de la 360 (fase 2 del render nativo, ver docs/RENDER_NATIVO.md).

El D3D de la 360 guarda dentro del dispositivo una copia de los registros de la GPU (estado de render, constantes de
fetch, constantes de shader...) con mascaras de bits "sucio"; en cada draw vuelca a PM4 solo los bloques marcados.
Este script saca del codigo recompilado (generated/re_wor_recomp.*.cpp) que bloque de registros vive en que
desplazamiento del dispositivo, comprueba que cuadra y le pone los nombres del SDK (register_table.inc).

Lo saca de las llamadas a los volcadores de la libreria D3D:
  sub_822DD8C0(dev, mascara, registro, dev + desplazamiento)  un registro por bit
  sub_822DDC58(dev, mascara, registro, dev + desplazamiento)  constantes de shader (float4)
  sub_822DDB00(dev, mascara)                                   constantes de fetch: 6 palabras por bit
  sub_822DD980(dev, mascara)                                   planos de recorte de usuario: 4 palabras por bit

Uso:  python tools/d3d_device_map.py [--json salida.json] [--header salida.h]
"""
from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
GEN = ROOT / "generated"
REG_TABLE = ROOT / "thirdparty" / "rexglue-sdk" / "include" / "rex" / "graphics" / "register_table.inc"
D3D_LO, D3D_HI = 0x822D0000, 0x822F1000

FDEF = re.compile(r"^DEFINE_REX_FUNC\((sub_([0-9A-F]+))\)")
ASM = re.compile(r"^\s*// (\S+)\s*(.*)$")


def functions(gen: Path, lo: int, hi: int) -> dict[str, list[tuple[str, list[str]]]]:
    """Desensamblado (comentarios del codegen) de las funciones en [lo, hi)."""
    out, cur = {}, None
    for f in sorted(gen.glob("re_wor_recomp.*.cpp")):
        with open(f, encoding="utf-8", errors="replace") as fh:
            for line in fh:
                m = FDEF.match(line)
                if m:
                    a = int(m.group(2), 16)
                    cur = out.setdefault(m.group(1), []) if lo <= a < hi else None
                    continue
                if cur is not None and (m := ASM.match(line)):
                    cur.append((m.group(1), [x.strip() for x in m.group(2).split(",")] if m.group(2) else []))
    return out


def imm(s: str) -> int:
    return int(s, 0)


def leading_ones32(v: int) -> int:
    v &= 0xFFFFFFFF
    n = 0
    while n < 32 and v & (0x80000000 >> n):
        n += 1
    return n


def call_sites(funcs: dict, target: str) -> list[dict]:
    """Llamadas a target con r5 = li REG y r6 = addi r6,rX,OFF; el numero de registros sale de la mascara r4."""
    sites = []
    for name, code in funcs.items():
        regs: dict[str, tuple] = {}
        for op, a in code:
            if op == "li" and len(a) == 2:
                regs[a[0]] = ("li", imm(a[1]))
            elif op == "lis" and len(a) == 2:
                regs[a[0]] = ("lis", imm(a[1]) << 16)
            elif op == "addi" and len(a) == 3:
                regs[a[0]] = ("addi", a[1], imm(a[2]))
            elif op == "rldicr" and len(a) == 4:
                regs[a[0]] = ("rldicr", imm(a[2]), imm(a[3]))
            elif op == "bl":
                if "sub_" + a[0][2:].upper() == target:
                    r4, r5, r6 = regs.get("r4"), regs.get("r5"), regs.get("r6")
                    if r5 and r5[0] == "li" and r6 and r6[0] == "addi":
                        count = None
                        if r4 and r4[0] == "rldicr":   # bits [sh, sh + me] de la mascara -> me + 1 registros
                            count = r4[2] + 1
                        elif r4 and r4[0] == "lis":     # mascara constante en los bits altos
                            count = leading_ones32(r4[1])
                        sites.append({"func": name, "reg": r5[1], "offset": r6[2], "count": count})
                regs = {}
            elif a:
                regs.pop(a[0], None)
    return sites


def helper_constants(code: list) -> tuple[list[int], list[int]]:
    """Inmediatos (li) y desplazamientos (addi rX,r3,...) de una funcion volcadora."""
    lis = [imm(a[1]) for op, a in code if op == "li" and len(a) == 2]
    adds = [imm(a[2]) for op, a in code if op == "addi" and len(a) == 3 and a[1] in ("r3", "r29", "r27")]
    return lis, adds


def register_names() -> dict[int, str]:
    names = {}
    pat = re.compile(r"XE_GPU_REGISTER\((0x[0-9A-Fa-f]+),\s*\w+,\s*(\w+)\)")
    for line in open(REG_TABLE, encoding="utf-8"):
        if m := pat.search(line):
            names.setdefault(int(m.group(1), 16), m.group(2))
    return names


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--json")
    ap.add_argument("--header", help="cabecera C++ con la tabla (para el gancho de ejecucion)")
    a = ap.parse_args()

    funcs = functions(GEN, D3D_LO, D3D_HI)
    blocks: dict[int, dict] = {}   # registro base -> bloque

    def add(reg, offset, count, how, where):
        b = blocks.setdefault(reg, {"reg": reg, "offset": offset, "count": 0, "how": how, "sites": set()})
        if b["offset"] != offset:
            sys.exit(f"incoherente: registro 0x{reg:04X} en dev+{b['offset']} y en dev+{offset} ({where})")
        b["count"] = max(b["count"], count or 0)
        b["sites"].add(where)

    for s in call_sites(funcs, "sub_822DD8C0"):
        add(s["reg"], s["offset"], s["count"], "un registro por bit (sub_822DD8C0)", s["func"])
    for s in call_sites(funcs, "sub_822DDC58"):
        # constantes float: 256 float4 por tipo de shader
        add(s["reg"], s["offset"], 1024, "constantes de shader, float4 (sub_822DDC58)", s["func"])

    # volcadores con registro y origen fijos dentro
    lis, adds = helper_constants(funcs["sub_822DDB00"])
    assert 18432 in lis and 1152 in adds, "sub_822DDB00 ha cambiado"
    add(0x4800, 1152, 32 * 6, "constantes de fetch, 6 palabras por bit (sub_822DDB00)", "sub_822DDB00")
    lis, adds = helper_constants(funcs["sub_822DD980"])
    assert 9096 in lis and 10272 in adds, "sub_822DD980 ha cambiado"
    add(0x2388, 10272, 6 * 4, "planos de recorte de usuario, 4 palabras por bit (sub_822DD980)", "sub_822DD980")

    # huecos conocidos: se escriben por otro camino, pero viven en la misma tabla
    #   0x2080-0x2082 (ventana / scissor): sub_822D09A8 lee dev+10436 y dev+10440 = 0x2081 / 0x2082
    #   0x4908-0x4927 (bucles): entre los bools (dev+10112, 8 palabras) y los planos de recorte (dev+10272)
    add(0x2080, 10432, 3, "ventana y scissor (sub_822D09A8 -> sub_822D25C0)", "sub_822D09A8")
    add(0x4908, 10144, 32, "constantes de bucle (deducido por contiguidad)", "contiguo")

    # comprobar que los bloques forman una tabla sin solapes
    ordered = sorted(blocks.values(), key=lambda b: b["offset"])
    problems = []
    for x, y in zip(ordered, ordered[1:]):
        end = x["offset"] + 4 * x["count"]
        if end > y["offset"]:
            problems.append(f"solape: 0x{x['reg']:04X} acaba en dev+{end}, 0x{y['reg']:04X} empieza en dev+{y['offset']}")
        elif end < y["offset"]:
            problems.append(f"hueco de {y['offset'] - end} bytes entre 0x{x['reg']:04X} y 0x{y['reg']:04X}")

    names = register_names()
    lo = ordered[0]["offset"]
    hi = ordered[-1]["offset"] + 4 * ordered[-1]["count"]
    print(f"Copia de los registros de la GPU dentro del dispositivo: dev+{lo} .. dev+{hi} ({hi - lo} bytes)")
    print(f"{'dev+':>7}  {'registros':<15} {'n':>5}  como se vuelca")
    for b in ordered:
        last = b["reg"] + b["count"] - 1
        first_name = names.get(b["reg"], "?")
        print(f"{b['offset']:>7}  0x{b['reg']:04X}-0x{last:04X} {b['count']:>5}  {b['how']}  [{first_name}...]")
    for p in problems:
        print("AVISO:", p)
    print("mascaras de sucio: dev+0 (constantes VS), dev+8 (PS), dev+16, dev+24, dev+32, dev+40")

    table = [{"reg": b["reg"], "offset": b["offset"], "count": b["count"], "how": b["how"],
              "first": names.get(b["reg"]), "sites": sorted(b["sites"])} for b in ordered]
    if a.json:
        Path(a.json).write_text(json.dumps({"blocks": table, "problems": problems}, indent=1), encoding="utf-8")
    if a.header:
        lines = ["// Generado por tools/d3d_device_map.py: no editar a mano.",
                 "// Bloques de registros de la GPU copiados dentro del dispositivo D3D de la 360.",
                 "#pragma once", "#include <cstdint>", "", "namespace re_wor::nativegfx {",
                 "struct DeviceRegBlock { uint32_t reg, offset, count; };",
                 "inline constexpr DeviceRegBlock kDeviceRegBlocks[] = {"]
        for b in ordered:
            lines.append(f"    {{0x{b['reg']:04X}, {b['offset']}, {b['count']}}},   // {names.get(b['reg'], '?')}")
        lines += ["};", "}  // namespace re_wor::nativegfx", ""]
        Path(a.header).write_text("\n".join(lines), encoding="utf-8", newline="\n")
    return 1 if problems else 0


if __name__ == "__main__":
    raise SystemExit(main())
