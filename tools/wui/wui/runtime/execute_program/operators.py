"""Operadores de la maquina con errores claros (en vez de los de Python)."""
from __future__ import annotations

from ...shared.opcodes import Op
from ...shared.values import is_number, to_text, type_name

SYMBOL = {Op.ADD: "+", Op.SUB: "-", Op.MUL: "*", Op.DIV: "/", Op.IDIV: "//", Op.MOD: "%", Op.POW: "**",
          Op.LT: "<", Op.LE: "<=", Op.GT: ">", Op.GE: ">="}


def _num(v) -> bool:
    return is_number(v) or isinstance(v, bool)


def binary(vm, op, a, b):
    if _num(a) and _num(b):
        if op is Op.ADD:
            return a + b
        if op is Op.SUB:
            return a - b
        if op is Op.MUL:
            return a * b
        if op in (Op.DIV, Op.IDIV, Op.MOD) and b == 0:
            vm.fault("division entre cero")
        if op is Op.DIV:
            return a / b
        if op is Op.IDIV:
            return a // b
        if op is Op.MOD:
            return a % b
        if op is Op.POW:
            try:
                r = a ** b
            except (OverflowError, ZeroDivisionError):
                vm.fault("potencia demasiado grande o de cero")
            if isinstance(r, complex):
                vm.fault("potencia de un negativo con decimales")
            return r
    if op is Op.ADD:
        if isinstance(a, str) and isinstance(b, str):
            return a + b
        if isinstance(a, list) and isinstance(b, list):
            return a + b
        if isinstance(a, str) or isinstance(b, str):
            vm.fault(f"no se puede sumar {type_name(a)} ({to_text(a)}) y {type_name(b)} ({to_text(b)}): "
                     "para unir texto y numeros usa str(x) o un f-string: f\"puntos: {x}\"")
    if op is Op.MUL:
        if isinstance(a, (str, list)) and isinstance(b, int) and not isinstance(b, bool):
            return a * max(0, min(b, 10000))
        if isinstance(b, (str, list)) and isinstance(a, int) and not isinstance(a, bool):
            return b * max(0, min(a, 10000))
    if op is Op.MOD and isinstance(a, str):
        vm.fault("el formato con % no se puede usar: usa un f-string")
    vm.fault(f"no se puede hacer {type_name(a)} {SYMBOL[op]} {type_name(b)} ({to_text(a)} {SYMBOL[op]} {to_text(b)})")


def compare(vm, op, a, b):
    if op is Op.EQ:
        return _eq(a, b)
    if op is Op.NE:
        return not _eq(a, b)
    if op is Op.IN or op is Op.NOTIN:
        if isinstance(b, list):
            r = any(_eq(a, x) for x in b)
        elif isinstance(b, str):
            if not isinstance(a, str):
                vm.fault(f"'in' con un texto necesita otro texto, no {type_name(a)}")
            r = a in b
        else:
            vm.fault(f"'in' necesita una lista o un texto a la derecha, no {type_name(b)}")
        return r if op is Op.IN else not r
    if (_num(a) and _num(b)) or (isinstance(a, str) and isinstance(b, str)):
        if op is Op.LT:
            return a < b
        if op is Op.LE:
            return a <= b
        if op is Op.GT:
            return a > b
        return a >= b
    vm.fault(f"no se puede comparar {type_name(a)} {SYMBOL[op]} {type_name(b)} ({to_text(a)}, {to_text(b)})")


def _eq(a, b) -> bool:
    if _num(a) and _num(b):
        return a == b
    if type(a) is not type(b):
        return False
    return a == b


def _index(vm, seq, i):
    if isinstance(i, bool) or not isinstance(i, int):
        if isinstance(i, float) and i.is_integer():
            i = int(i)
        else:
            vm.fault(f"el indice tiene que ser un entero, no {type_name(i)} ({to_text(i)})")
    if not -len(seq) <= i < len(seq):
        vm.fault(f"indice {i} fuera de rango (hay {len(seq)} elementos: de 0 a {len(seq) - 1})")
    return i


def get_index(vm, seq, i):
    if isinstance(seq, (list, str)):
        return seq[_index(vm, seq, i)]
    vm.fault(f"no se puede usar [ ] con {type_name(seq)}")


def set_index(vm, seq, i, v):
    if isinstance(seq, list):
        seq[_index(vm, seq, i)] = v
        return
    if isinstance(seq, str):
        vm.fault("los textos no se pueden cambiar letra a letra: crea uno nuevo")
    vm.fault(f"no se puede usar [ ] = con {type_name(seq)}")


def format_value(vm, v, spec: str) -> str:
    if spec.startswith("!r"):
        return repr(v) if isinstance(v, str) else to_text(v)
    if not spec:
        return v if isinstance(v, str) else to_text(v)
    try:
        return format(v, spec)
    except (ValueError, TypeError):
        vm.fault(f"el formato ':{spec}' no sirve para {type_name(v)} ({to_text(v)})")
