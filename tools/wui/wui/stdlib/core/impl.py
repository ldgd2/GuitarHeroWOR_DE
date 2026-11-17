"""Implementacion de referencia: conversiones, listas, textos y registro."""
from __future__ import annotations

from ...shared.values import is_number, to_text, type_name


def _num(ctx, v, what="un numero"):
    if is_number(v):
        return v
    if isinstance(v, bool):
        return int(v)
    ctx.vm.fault(f"se esperaba {what} y llego {type_name(v)} ({to_text(v)})")


# ------------------------------------------------------------------ conversiones
def int_(ctx, a, kw):
    v = a[0]
    if isinstance(v, str):
        try:
            return int(float(v.strip()))
        except ValueError:
            ctx.vm.fault(f"'{v}' no es un numero")
    return int(_num(ctx, v))


def float_(ctx, a, kw):
    v = a[0]
    if isinstance(v, str):
        try:
            return float(v.strip())
        except ValueError:
            ctx.vm.fault(f"'{v}' no es un numero")
    return float(_num(ctx, v))


def str_(ctx, a, kw):
    return to_text(a[0])


def bool_(ctx, a, kw):
    from ...shared.values import truthy
    return truthy(a[0])


def len_(ctx, a, kw):
    v = a[0]
    if isinstance(v, (list, str)):
        return len(v)
    ctx.vm.fault(f"len() necesita una lista o un texto, no {type_name(v)}")


def range_(ctx, a, kw):
    start, stop, step = a
    if stop is None:
        start, stop = 0, start
    start, stop, step = int(_num(ctx, start)), int(_num(ctx, stop)), int(_num(ctx, step))
    if step == 0:
        ctx.vm.fault("range() con paso 0")
    n = len(range(start, stop, step))
    if n > 100000:
        ctx.vm.fault("range() demasiado grande (100000 como mucho)")
    return list(range(start, stop, step))


def type_(ctx, a, kw):
    return type_name(a[0])


def log(ctx, a, kw):
    sep = a[0] if isinstance(a[0], str) else " "
    ctx.vm.log("info", sep.join(to_text(x) for x in a[1:]))


def warn(ctx, a, kw):
    ctx.vm.log("warn", " ".join(to_text(x) for x in a))


# ------------------------------------------------------------------ listas
def _list(ctx, v):
    if not isinstance(v, list):
        ctx.vm.fault(f"se esperaba una lista y llego {type_name(v)}")
    return v


def l_append(ctx, a, kw):
    _list(ctx, a[0]).append(a[1])


def l_insert(ctx, a, kw):
    _list(ctx, a[0]).insert(int(_num(ctx, a[1])), a[2])


def l_pop(ctx, a, kw):
    lst = _list(ctx, a[0])
    if not lst:
        ctx.vm.fault("pop() en una lista vacia")
    i = int(_num(ctx, a[1]))
    if not -len(lst) <= i < len(lst):
        ctx.vm.fault(f"pop({i}): la lista tiene {len(lst)} elementos")
    return lst.pop(i)


def l_remove(ctx, a, kw):
    lst = _list(ctx, a[0])
    if a[1] in lst:
        lst.remove(a[1])
        return True
    return False


def l_clear(ctx, a, kw):
    _list(ctx, a[0]).clear()


def l_index(ctx, a, kw):
    lst = _list(ctx, a[0])
    return lst.index(a[1]) if a[1] in lst else -1


def l_count(ctx, a, kw):
    return _list(ctx, a[0]).count(a[1])


def l_contains(ctx, a, kw):
    return a[1] in _list(ctx, a[0])


def l_reverse(ctx, a, kw):
    _list(ctx, a[0]).reverse()


def l_sort(ctx, a, kw):
    lst = _list(ctx, a[0])
    try:
        lst.sort(reverse=bool(a[1]))
    except TypeError:
        ctx.vm.fault("sort(): la lista mezcla tipos que no se pueden comparar")


def l_copy(ctx, a, kw):
    return list(_list(ctx, a[0]))


def l_join(ctx, a, kw):
    return a[1].join(to_text(x) for x in _list(ctx, a[0]))


# ------------------------------------------------------------------ textos
def s_upper(ctx, a, kw):
    return a[0].upper()


def s_lower(ctx, a, kw):
    return a[0].lower()


def s_strip(ctx, a, kw):
    return a[0].strip()


def s_replace(ctx, a, kw):
    return a[0].replace(to_text(a[1]), to_text(a[2]))


def s_split(ctx, a, kw):
    return a[0].split(a[1]) if a[1] else a[0].split()


def s_startswith(ctx, a, kw):
    return a[0].startswith(to_text(a[1]))


def s_endswith(ctx, a, kw):
    return a[0].endswith(to_text(a[1]))


def s_find(ctx, a, kw):
    return a[0].find(to_text(a[1]))


def s_contains(ctx, a, kw):
    return to_text(a[1]) in a[0]


def s_pad(ctx, a, kw):
    s, width, fill = a
    fill = (to_text(fill) or " ")[0]
    return s.rjust(int(width), fill)


def slice_(ctx, a, kw):
    seq, start, stop = a
    if not isinstance(seq, (list, str)):
        ctx.vm.fault(f"[a:b] necesita una lista o un texto, no {type_name(seq)}")
    s = None if start is None else int(_num(ctx, start))
    e = None if stop is None else int(_num(ctx, stop))
    return seq[s:e]
