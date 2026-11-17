"""Linea de comandos de wui.

    python -m wui check  menu.py [--layers a,b] [--screens x,y]   comprobar (errores y avisos)
    python -m wui compile menu.py [-o menu.wuib] [--json]          compilar
    python -m wui dis    menu.py                                   ver las instrucciones
    python -m wui run    menu.py [--seconds 3] [--script pasos.json] [--layers ...] [--screens ...]
                                                                    simular (diario de lo que hace)
    python -m wui api    [-o api.json]                             la libreria como JSON (para editores)
    python -m wui docs   [-o REFERENCIA.md]                        la referencia en Markdown
    python -m wui tables [--js tables.js] [--cpp tablas.inc]     tablas para las maquinas de JS y C++

Guion de run: lista JSON de pasos {"t": segundos, "down": ["red"], "up": ["red"], "click": "capa",
"focus": "capa", "axes": {"whammy": 0.5}}.
"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

from . import Inputs, Machine, SimulatedHost, compile_source, describe_api, disassemble, generate_docs


def _split(v):
    return [x.strip() for x in v.split(",") if x.strip()] if v else None


def _compile(a):
    src = Path(a.file).read_text(encoding="utf-8")
    res = compile_source(src, a.file, _split(getattr(a, "layers", None)), _split(getattr(a, "screens", None)))
    for d in res.diagnostics:
        print(f"{a.file}:{d}", file=sys.stderr)
    return res


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(prog="wui", description="Lenguaje de interfaces de wui")
    sub = ap.add_subparsers(dest="cmd", required=True)
    for name in ("check", "compile", "dis", "run"):
        p = sub.add_parser(name)
        p.add_argument("file")
        p.add_argument("--layers")
        p.add_argument("--screens")
        if name == "compile":
            p.add_argument("-o", "--out")
            p.add_argument("--json", action="store_true")
        if name == "run":
            p.add_argument("--seconds", type=float, default=2.0)
            p.add_argument("--fps", type=float, default=60.0)
            p.add_argument("--script")
    for name in ("api", "docs"):
        p = sub.add_parser(name)
        p.add_argument("-o", "--out")
    p = sub.add_parser("tables", help="tablas para las maquinas de JS y C++")
    p.add_argument("--js")
    p.add_argument("--cpp")
    a = ap.parse_args(argv)

    if a.cmd == "tables":
        from .tooling.export_tables.service import to_cpp, to_json
        if a.js:
            head = "// Generado por `python -m wui tables` desde la libreria de wui. No editar a mano.\n"
            Path(a.js).write_text(head + "export default " + to_json() + ";\n", encoding="utf-8", newline="\n")
        if a.cpp:
            Path(a.cpp).write_text(to_cpp(), encoding="utf-8", newline="\n")
        return 0

    if a.cmd in ("api", "docs"):
        text = json.dumps(describe_api(), ensure_ascii=False, indent=1) if a.cmd == "api" else generate_docs()
        if a.out:
            Path(a.out).write_text(text, encoding="utf-8")
        else:
            sys.stdout.write(text + "\n")
        return 0

    res = _compile(a)
    if not res.ok:
        return 1
    prog = res.program
    if a.cmd == "check":
        n = len(res.diagnostics)
        print(f"bien: {len(prog.functions)} funciones, {len(prog.handlers)} eventos" + (f", {n} aviso(s)" if n else ""))
        return 0
    if a.cmd == "compile":
        out = Path(a.out or Path(a.file).with_suffix(".wuib"))
        if a.json:
            out = out.with_suffix(".json")
            out.write_text(json.dumps(prog.to_json(), ensure_ascii=False, indent=1), encoding="utf-8")
        else:
            out.write_bytes(prog.to_bytes())
        print(f"{out}  ({out.stat().st_size} bytes)")
        return 0
    if a.cmd == "dis":
        print(disassemble(prog))
        return 0
    # run
    layers = _split(a.layers) or []
    host = SimulatedHost(layers, _split(a.screens) or ["principal"])
    vm = Machine(prog, host)
    steps = json.loads(Path(a.script).read_text(encoding="utf-8")) if a.script else []
    steps.sort(key=lambda s: s.get("t", 0))
    held: set[str] = set()
    axes: dict = {}
    vm.start()
    dt, t, i = 1.0 / a.fps, 0.0, 0
    seen = 0
    while t < a.seconds:
        while i < len(steps) and steps[i].get("t", 0) <= t + 1e-9:
            s = steps[i]
            held |= set(s.get("down", []))
            held -= set(s.get("up", []))
            axes.update(s.get("axes", {}))
            if s.get("focus"):
                vm.notify_focus(s["focus"])
            if s.get("click"):
                vm.notify_click(s["click"])
            i += 1
        vm.step(dt, Inputs({1: set(held)}, {1: dict(axes)}))
        t += dt
        for j in host.journal[seen:]:
            if j[0] != "set":
                print(f"{vm.time:7.3f}  {' '.join(map(str, j))}")
        seen = len(host.journal)
    for lvl, msg in host.logs:
        print(f"[{lvl}] {msg}")
    for L in host.layers.values():
        changed = {k: v for k, v in L.props.items() if v != SimulatedHost([L.name]).layers[L.name].props.get(k)}
        if changed:
            print(f"capa {L.name}: {changed}")
    return 1 if vm.errors else 0


if __name__ == "__main__":
    sys.exit(main())
