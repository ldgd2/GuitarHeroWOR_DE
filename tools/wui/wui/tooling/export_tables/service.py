"""Caso de uso: escribir las tablas que necesitan las maquinas de otros lenguajes (JS del editor y C++ de re-wor):
firmas de los metodos (se ordenan al ejecutar), propiedades de las capas, botones y nombres de la libreria.

Los decimales van marcados ({"$f": 1.0}) para que JS no los confunda con enteros (1.0 se veria como 1)."""
from __future__ import annotations

import json
from typing import Any

from ...stdlib.layers.specs import PROPS
from ...stdlib.pad.specs import BUTTONS
from ...stdlib.registry import REQUIRED, Registry, registry


def _v(v: Any) -> Any:
    if isinstance(v, float):
        return {"$f": v}
    if isinstance(v, (list, tuple)):
        return [_v(x) for x in v]
    return v


def tables(reg: Registry | None = None) -> dict:
    reg = reg or registry()
    methods = {}
    for (recv, name), b in sorted(reg.methods.items()):
        methods[f"{recv}.{name}"] = [[p.name, p.kind, p.default is not REQUIRED,
                                      None if p.default is REQUIRED else _v(p.default)] for p in b.params]
    props = {name: [typ, True, anim, _v(dflt)] for name, typ, _doc, anim, dflt in PROPS}
    for (recv, name), p in reg.props.items():
        if recv == "Layer" and name not in props:
            props[name] = [p.type, p.writable, p.animatable, _v(p.default)]
    from ...stdlib.game.specs import BAND_FIELDS, PLAYER_FIELDS
    from ...stdlib.sound.specs import SOUNDS
    return {"methods": methods, "props": props, "buttons": sorted(set(BUTTONS.values())),
            "functions": sorted(reg.functions), "sounds": sorted(set(SOUNDS.values())),
            "player_fields": PLAYER_FIELDS, "band_fields": BAND_FIELDS}


def to_json(reg: Registry | None = None) -> str:
    return json.dumps(tables(reg), ensure_ascii=False, indent=0)


def _cpp_val(v) -> str:
    if isinstance(v, dict) and "$f" in v:
        return f"V::F({v['$f']!r})"
    if v is None:
        return "V()"
    if isinstance(v, bool):
        return f"V::B({'true' if v else 'false'})"
    if isinstance(v, int):
        return f"V::I({v})"
    if isinstance(v, str):
        return "V::S(" + json.dumps(v) + ")"
    raise ValueError(v)


def to_cpp(reg: Registry | None = None) -> str:
    t = tables(reg)
    kinds = {"normal": 0, "kwonly": 1, "varargs": 2, "varkw": 3}
    out = ["// Generado por `python -m wui tables` desde la libreria de wui. No editar a mano.",
           "// Firmas de los metodos (para ordenar sus argumentos al ejecutar) y propiedades de las capas.",
           "// clang-format off"]
    out.append("static const MethodSig kMethods[] = {")
    for name, params in t["methods"].items():
        ps = ", ".join(f"{{\"{p[0]}\", {kinds[p[1]]}, {'true' if p[2] else 'false'}, {_cpp_val(p[3]) if p[2] else 'V()'}}}"
                       for p in params)
        out.append(f"  {{\"{name}\", {{{ps}}}}},")
    out.append("};")
    types = {"numero": 0, "bool": 1, "texto": 2, "color": 3}
    out.append("static const PropSig kProps[] = {")
    for name, (typ, w, anim, dflt) in t["props"].items():
        out.append(f"  {{\"{name}\", {types.get(typ, 2)}, {'true' if w else 'false'}, {'true' if anim else 'false'}, "
                   f"{_cpp_val(dflt)}}},")
    out.append("};")
    for key, var in (("buttons", "kButtons"), ("sounds", "kSounds"), ("player_fields", "kPlayerFields"),
                     ("band_fields", "kBandFields")):
        out.append(f"static const char* const {var}[] = {{" + ", ".join(json.dumps(x) for x in t[key]) + "};")
    out.append("// clang-format on")
    return "\n".join(out) + "\n"
