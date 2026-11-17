"""Caso de uso: escribir la referencia de la libreria en Markdown (sale del mismo registro que usa el compilador,
asi nunca se queda vieja)."""
from __future__ import annotations

from ...stdlib.registry import Registry, registry


def generate_docs(reg: Registry | None = None) -> str:
    reg = reg or registry()
    out = ["# Referencia de wui", "",
           "Generada desde el registro de la libreria (`python -m wui docs`). No editar a mano.", ""]
    out += ["## Eventos", "", "Decoradores que conectan una funcion con algo que pasa. La funcion puede recibir "
            "(en orden) lo que da el evento, o menos.", "", "| Evento | Da | Que hace |", "|---|---|---|"]
    for e in reg.events.values():
        args = ", ".join(p.name + ("" if p.required else f"={p.default!r}") for p in e.params)
        out.append(f"| `@{e.name}({args})` | {', '.join(e.gives) or '-'} | {e.doc} |")
    out.append("")
    groups = reg.constant_groups()
    if groups:
        out += ["## Constantes", ""]
        for g, vals in groups.items():
            out.append(f"- **{g}**: " + ", ".join(f"`{g}.{k}`" for k in vals))
        out.append("- " + ", ".join(f"`{k}`" for k in reg.constants if "." not in k))
        out.append("")
    for cat, desc in reg.categories.items():
        fns = [b for b in reg.functions.values() if b.category == cat]
        mets = [b for b in reg.methods.values() if b.category == cat]
        if not fns and not mets:
            continue
        out += [f"## {cat.capitalize()}", "", desc, ""]
        for b in fns + mets:
            ret = f" -> {b.ret}" if b.ret != "nada" else ""
            wait = " *(puede esperar)*" if b.blocking else ""
            out.append(f"- `{b.signature().replace('self, ', '').replace('(self)', '()')}`{ret}{wait}: {b.doc}")
            if b.example:
                out += ["", "  ```python"] + [f"  {ln}" for ln in b.example.splitlines()] + ["  ```", ""]
        props = [p for p in reg.props.values() if any(m.receiver == p.receiver for m in mets)]
        if props:
            out += ["", f"Propiedades de {props[0].receiver}:", "", "| Propiedad | Tipo | Animable | Que es |",
                    "|---|---|---|---|"]
            for p in props:
                rw = "" if p.writable else " (solo leer)"
                out.append(f"| `{p.name}` | {p.type}{rw} | {'si' if p.animatable else ''} | {p.doc} |")
        out.append("")
    return "\n".join(out)
