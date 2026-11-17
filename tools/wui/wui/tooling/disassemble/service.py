"""Caso de uso: mostrar las instrucciones de un programa compilado, de forma legible (para depurar el compilador
y las maquinas de otros lenguajes)."""
from __future__ import annotations

from ...shared.opcodes import JUMPS, Op
from ...shared.program import NO_FUNC, Program


def disassemble(p: Program) -> str:
    out = [f"; {p.source}  ({len(p.functions)} funciones, {len(p.consts)} constantes, {len(p.globals)} globales)"]
    if p.globals:
        out.append("; globales: " + ", ".join(p.globals))
    for h in p.handlers:
        out.append(f"; @{h.event}{tuple(h.args)!r} -> {p.functions[h.func].name}")
    for fi, f in enumerate(p.functions):
        tag = " (arranque)" if fi == p.init and p.init != NO_FUNC else ""
        out += ["", f"funcion {fi} {f.name}({f.nparams} params, {f.nlocals} locales){tag}"]
        lines = dict(f.lines)
        for pc, (op, a, b, c) in enumerate(f.instructions()):
            note = ""
            if op is Op.CONST or op is Op.FORMAT:
                note = repr(p.consts[c])
            elif op in (Op.LOADG, Op.STOREG):
                note = p.globals[b]
            elif op in (Op.LOADL, Op.STOREL):
                note = f.local_names[b] if b < len(f.local_names) else f"#{b}"
            elif op in (Op.CALL, Op.FREF):
                note = p.functions[b].name + (f" ({a} args)" if op is Op.CALL else "")
            elif op is Op.CALLB:
                note = f"{p.builtins[b]} ({a} args" + (f", con nombre {p.consts[c]}" if c >= 0 else "") + ")"
            elif op is Op.CALLM:
                note = f".{p.names[b]} ({a} args" + (f", con nombre {p.consts[c]}" if c >= 0 else "") + ")"
            elif op in (Op.GETA, Op.SETA):
                note = "." + p.names[b]
            elif op in JUMPS:
                note = f"-> {c}"
            elif op in (Op.LIST, Op.CONCAT):
                note = str(b)
            elif op is Op.CALLV:
                note = f"{a} args"
            ln = f"{lines[pc]:>4}" if pc in lines else "    "
            out.append(f"{ln} {pc:>5}  {op.name:<7} {note}")
    return "\n".join(out)
