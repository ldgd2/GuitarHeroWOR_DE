"""Compara la maquina C++ de re-wor con el diario de Python (trace.py).

    python tests/conformance/run_cpp.py <carpeta de trace.py> [ejecutable run_cpp]

Si no se da el ejecutable, lo compila con clang++ (hace falta el entorno de Visual Studio en Windows).
"""
from __future__ import annotations

import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[3]
sys.path.insert(0, str(HERE))
from trace import norm  # noqa: E402

SOURCES = [ROOT / "re-wor/src/de/wui_vm.cpp", ROOT / "re-wor/src/de/wui_stdlib.cpp", HERE / "run_cpp.cpp"]


def build(out: Path) -> Path:
    cxx = os.environ.get("CXX") or shutil.which("clang++") or r"C:\Program Files\LLVM\bin\clang++.exe"
    exe = out / ("run_cpp.exe" if os.name == "nt" else "run_cpp")
    cmd = [cxx, "-std=c++20", "-O1", "-I", str(ROOT / "re-wor/src"), *map(str, SOURCES), "-o", str(exe)]
    subprocess.run(cmd, check=True)
    return exe


def unesc(s: str) -> str:
    out, i = [], 0
    while i < len(s):
        if s[i] == "\\" and i + 1 < len(s):
            out.append({"n": "\n", "t": "\t", "\\": "\\"}.get(s[i + 1], s[i + 1]))
            i += 2
        else:
            out.append(s[i])
            i += 1
    return "".join(out)


def run_case(exe: Path, d: Path, name: str) -> dict:
    c = json.loads((d / f"{name}.case.json").read_text(encoding="utf-8"))
    lines = [f"seed {c['seed']}", f"seconds {c['seconds']}"]
    lines += [f"layer {n}" for n in c["layers"]] + [f"screen {n}" for n in c["screens"]]
    lines += [f"at {t!r} {k} {'-' if v is None else v}" for t, k, v in c["script"]]
    (d / f"{name}.txt").write_text("\n".join(lines) + "\n", encoding="utf-8")
    p = subprocess.run([str(exe), str(d / f"{name}.wuib"), str(d / f"{name}.txt")], capture_output=True, timeout=60)
    if p.returncode:
        raise RuntimeError(p.stderr.decode("utf-8", "replace") or f"codigo {p.returncode}")
    got = {"logs": [], "journal": [], "errors": [], "globals": {}}
    for raw in p.stdout.decode("utf-8").splitlines():
        parts = raw.split("\t")
        if parts[0] == "L":
            got["logs"].append(f"{parts[1]}: {norm(unesc(parts[2]))}")
        elif parts[0] == "J":
            got["journal"].append(norm(unesc(parts[1])))
        elif parts[0] == "E":
            got["errors"].append(unesc(parts[1]))
        elif parts[0] == "G":
            got["globals"][parts[1]] = norm(unesc(parts[2]))
    return got


def main(d: Path, exe: Path | None) -> int:
    exe = exe or build(d)
    bad = total = 0
    for f in sorted(d.glob("*.case.json")):
        name = f.name[: -len(".case.json")]
        total += 1
        exp = json.loads((d / f"{name}.expected.json").read_text(encoding="utf-8"))
        try:
            got = run_case(exe, d, name)
        except Exception as e:  # noqa: BLE001
            print(f"FALLA {name}: {e}")
            bad += 1
            continue
        diffs = []
        for k in ("logs", "errors", "journal"):
            a, b = exp[k], got[k]
            for i in range(max(len(a), len(b))):
                x = a[i] if i < len(a) else None
                y = b[i] if i < len(b) else None
                if x != y:
                    diffs.append(f"{k}[{i}]\n    python: {x}\n    c++:    {y}")
                    break
        for k, v in exp["globals"].items():
            if got["globals"].get(k) != v:
                diffs.append(f"global {k}: python {v} / c++ {got['globals'].get(k)}")
        if diffs:
            bad += 1
            print(f"DISTINTO {name}\n  " + "\n  ".join(diffs))
        else:
            print(f"igual    {name}")
    print(f"{total - bad}/{total} casos iguales")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(Path(sys.argv[1]), Path(sys.argv[2]) if len(sys.argv) > 2 else None))
