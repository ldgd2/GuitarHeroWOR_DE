"""Compila todos los shaders traducidos por xenos2hlsl con fxc (SM 5.0), dxc (SM 6.0) y dxc -spirv, y resume fallos.

Uso:  python tools/xenos2hlsl/validate.py [carpeta]   (por defecto cache/native_shaders)
      --only fxc|dxc|spirv   --jobs N   --show N (errores de ejemplo por compilador)
      SPIR-V: dxc de DXC_SPIRV, del Vulkan SDK o de cache/tools/dxc-*/bin/x64 (version de GitHub)

Busca fxc.exe / dxc.exe en el Windows SDK (o en el PATH). Escribe validation.json junto a los shaders.
Siempre con IEEE estricto (/Gis, -Gis): sin el, los NaN no se comportan como en la 360 (ver xenos2hlsl_check).
"""
from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
import tempfile
from collections import Counter
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def find_tool(name: str) -> str | None:
    if p := shutil.which(name):
        return p
    kits = Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")) / "Windows Kits" / "10" / "bin"
    cands = sorted(kits.glob(f"10.*/x64/{name}"), reverse=True)
    return str(cands[0]) if cands else None


def run(cmd: list[str]) -> tuple[bool, str]:
    r = subprocess.run(cmd, capture_output=True, text=True, errors="replace")
    return r.returncode == 0, (r.stderr + r.stdout).strip()


def compile_one(tool: str, kind: str, src: Path, out_dir: Path) -> tuple[bool, str]:
    """fxc: SM 5.0 (D3D11 / referencia); dxc: SM 6.0 DXIL (D3D12); spirv: SPIR-V con dxc (Vulkan)."""
    stage = "vs" if ".vs." in src.name else "ps"
    out = str(out_dir / (src.stem + "." + kind))
    if kind == "fxc":
        ok, msg = run([tool, "/nologo", "/T", f"{stage}_5_0", "/E", "main", "/O1", "/Gis", "/Fo", out, str(src)])
        if not ok:   # el optimizador de fxc se rompe con algunas maquinas de estados: sin optimizar
            ok2, msg2 = run([tool, "/nologo", "/T", f"{stage}_5_0", "/E", "main", "/Od", "/Gis", "/Fo", out, str(src)])
            if ok2:
                return True, "/Od"
        return ok, msg
    cmd = [tool, "-T", f"{stage}_6_0", "-E", "main", "-O3", "-Gis", "-Fo", out, str(src)]
    if kind == "spirv":
        # Vulkan 1.2: -Gis pide SignedZeroInfNanPreserve (float controls, nucleo en 1.2)
        # bindings de Vulkan: b0-b3 -> 0-3, t0-t96 -> 16-112, s0-s31 -> 128-159 (todos en el set 0)
        cmd[1:1] = ["-spirv", "-fspv-target-env=vulkan1.2", "-fvk-use-dx-layout",
                    "-fvk-t-shift", "16", "0", "-fvk-s-shift", "128", "0"]
    return run(cmd)


def first_error(msg: str) -> str:
    for line in msg.splitlines():
        if "error" in line.lower():
            # quitar ruta y posicion para agrupar
            return re.sub(r"^.*?\(\d+,\d+(-\d+)?\):\s*", "", re.sub(r"^.*?:\d+:\d+:\s*", "", line)).strip()[:160]
    return msg.splitlines()[0][:160] if msg else "?"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("dir", nargs="?", default=str(ROOT / "cache" / "native_shaders"))
    ap.add_argument("--only", choices=["fxc", "dxc", "spirv"])
    ap.add_argument("--jobs", type=int, default=os.cpu_count() or 4)
    ap.add_argument("--show", type=int, default=3)
    a = ap.parse_args()
    d = Path(a.dir)
    srcs = sorted(d.glob("*.hlsl"))
    dxc = find_tool("dxc.exe")
    # la dxc del Windows SDK viene sin SPIR-V; la del Vulkan SDK o la de las versiones de GitHub lo traen.
    # Orden: variable DXC_SPIRV, Vulkan SDK, cache/tools/dxc-*/bin/x64 (la version de GitHub descomprimida ahi)
    cands = [os.environ.get("DXC_SPIRV", "")]
    if os.environ.get("VULKAN_SDK"):
        cands.append(str(Path(os.environ["VULKAN_SDK"]) / "Bin" / "dxc.exe"))
    cands += [str(p) for p in sorted((ROOT / "cache" / "tools").glob("dxc-*/bin/x64/dxc.exe"), reverse=True)]
    spirv_dxc = next((c for c in cands if c and Path(c).exists()), dxc)
    if spirv_dxc and "SPIR-V CodeGen not available" in run([spirv_dxc, "-spirv", "-T", "ps_6_0", "nul"])[1]:
        spirv_dxc = None
    pick = {"fxc": find_tool("fxc.exe"), "dxc": dxc, "spirv": spirv_dxc}
    tools = {k: v for k, v in pick.items() if a.only in (None, k)}
    report = {"total": len(srcs)}
    with tempfile.TemporaryDirectory() as tmp:
        for kind, tool in tools.items():
            if not tool:
                print(f"{kind}: no disponible (falta " + ("una dxc con SPIR-V: instala el Vulkan SDK)" if kind == "spirv"
                                                          else f"{kind}.exe)"))
                report[kind] = None
                continue
            with ThreadPoolExecutor(a.jobs) as ex:
                res = list(ex.map(lambda s: compile_one(tool, kind, s, Path(tmp)), srcs))
            fails = {s.name: m for s, (ok, m) in zip(srcs, res) if not ok}
            unopt = sum(1 for ok, m in res if ok and m == "/Od")
            groups = Counter(first_error(m) for m in fails.values())
            print(f"{kind}: {len(srcs) - len(fails)}/{len(srcs)} compilan" + (f" ({unopt} sin optimizar)" if unopt else ""))
            for e, n in groups.most_common(12):
                print(f"   {n:4d}  {e}")
            for name in list(fails)[: a.show]:
                print(f"   ejemplo {name}:\n      " + "\n      ".join(fails[name].splitlines()[:6]))
            report[kind] = {"ok": len(srcs) - len(fails), "unoptimized": unopt, "failed": sorted(fails),
                            "groups": dict(groups.most_common())}
    (d / "validation.json").write_text(json.dumps(report, indent=1), encoding="utf-8")


if __name__ == "__main__":
    main()
