"""Diario de referencia (maquina de Python) de cada caso y comparacion con las otras maquinas.

    python tests/conformance/trace.py <carpeta>   escribe <caso>.wuib, <caso>.case.json y <caso>.expected.json
"""
from __future__ import annotations

import json
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent.parent))

import wui  # noqa: E402
from wui import Inputs, Machine, SimulatedHost  # noqa: E402
from wui.shared.program import Program  # noqa: E402
from wui.shared.values import to_text  # noqa: E402

from cases import CASES  # noqa: E402

FPS = 60
_FLOAT = re.compile(r"-?\d+\.\d+(?:e[+-]?\d+)?|-?\d+e[+-]?\d+")


def norm(s: str) -> str:
    """los decimales se comparan con 9 cifras (sin y exp pueden variar en la ultima cifra entre lenguajes)"""
    def one(m):
        r = f"{float(m.group()):.9g}"
        return r if any(ch in r for ch in ".einf") else r + ".0"
    return _FLOAT.sub(one, s)


def txt(v) -> str:
    if isinstance(v, dict):
        return "{" + ", ".join(f"{k}={txt(x)}" for k, x in sorted(v.items())) + "}"
    return to_text(v)


def run_python(c: dict, prog: Program) -> dict:
    host = SimulatedHost(c["layers"], c["screens"], game={"#language": "es"})
    vm = Machine(prog, host, seed=c["seed"])
    vm.start()
    held: set = set()
    script = sorted(c["script"], key=lambda s: s[0])
    i, frames = 0, round(c["seconds"] * FPS)
    for f in range(frames):
        t = f / FPS
        while i < len(script) and script[i][0] <= t + 1e-9:
            _, kind, val = script[i]
            if kind == "down":
                held.add(val)
            elif kind == "up":
                held.discard(val)
            elif kind == "focus":
                vm.notify_focus(val)
            elif kind == "click":
                vm.notify_click(val)
            i += 1
        vm.step(1 / FPS, Inputs({1: set(held)}))
    return {
        "logs": [f"{lvl}: {norm(m)}" for lvl, m in vm.logs],
        "journal": [norm(" ".join(txt(x) for x in j)) for j in host.journal],
        "errors": [f"{e.function}:{e.line}: {e.message}" for e in vm.errors],
        "globals": {n: norm(txt(vm.global_value(n))) for n in prog.globals},
    }


def main(out: Path) -> None:
    out.mkdir(parents=True, exist_ok=True)
    for name, c in CASES.items():
        res = wui.compile_source(c["source"], name, c["layers"], c["screens"])
        if not res.ok:
            raise SystemExit(f"{name}: no compila\n" + "\n".join(map(str, res.diagnostics)))
        data = res.program.to_bytes()
        (out / f"{name}.wuib").write_bytes(data)
        (out / f"{name}.case.json").write_text(json.dumps(c, ensure_ascii=False), encoding="utf-8")
        exp = run_python(c, Program.from_bytes(data))
        (out / f"{name}.expected.json").write_text(json.dumps(exp, ensure_ascii=False, indent=1), encoding="utf-8")


if __name__ == "__main__":
    main(Path(sys.argv[1]))
