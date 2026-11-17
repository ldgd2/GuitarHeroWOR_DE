"""Nucleo: conversiones, listas, textos y registro."""
from __future__ import annotations

from . import impl as I

CAT = "nucleo"


def register(r) -> None:
    r.categories[CAT] = "Conversiones, listas, textos y mensajes de depuracion"
    f = lambda *a, **k: r.fn(*a, cat=CAT, **k)  # noqa: E731
    f("int", "value", "Convierte a entero (quita los decimales).", I.int_, ret="entero", example='int("42") + 1')
    f("float", "value", "Convierte a decimal.", I.float_, ret="decimal")
    f("str", "value", "Convierte cualquier valor a texto.", I.str_, ret="texto", example='"Puntos: " + str(score)')
    f("bool", "value", "Convierte a verdadero o falso.", I.bool_, ret="bool")
    f("len", "value", "Cantidad de elementos de una lista o de letras de un texto.", I.len_, ret="entero")
    f("range", "start, stop=None, step=1", "Lista de numeros (para recorrer con for).", I.range_, ret="lista",
      example="for i in range(5):\n    log(i)")
    f("slice", "items, start=None, stop=None", "Un trozo de una lista o de un texto (igual que items[start:stop]).",
      I.slice_, ret="cualquiera")
    f("type", "value", "Nombre del tipo de un valor (entero, texto, capa...).", I.type_, ret="texto")
    f("log", "*values, sep=' '", "Escribe en el registro de la vista previa (y del juego con el modo desarrollador).",
      I.log, example='log("puntos", score)')
    f("print", "*values, sep=' '", "Igual que log().", I.log)
    f("warn", "*values", "Escribe un aviso en el registro.", I.warn)

    m = lambda name, sig, doc, impl, **k: r.fn(name, sig, doc, impl, cat=CAT, receiver="list", **k)  # noqa: E731
    m("append", "self, value", "Anade al final.", I.l_append, example="items.append(5)")
    m("insert", "self, index, value", "Inserta en una posicion.", I.l_insert)
    m("pop", "self, index=-1", "Quita y devuelve un elemento (por defecto el ultimo).", I.l_pop, ret="cualquiera")
    m("remove", "self, value", "Quita la primera aparicion; devuelve si estaba.", I.l_remove, ret="bool")
    m("clear", "self", "Vacia la lista.", I.l_clear)
    m("index", "self, value", "Posicion de un valor (-1 si no esta).", I.l_index, ret="entero")
    m("count", "self, value", "Cuantas veces aparece un valor.", I.l_count, ret="entero")
    m("contains", "self, value", "Si el valor esta en la lista (igual que: value in lista).", I.l_contains, ret="bool")
    m("reverse", "self", "Invierte el orden.", I.l_reverse)
    m("sort", "self, reverse=False", "Ordena de menor a mayor (o al reves).", I.l_sort)
    m("copy", "self", "Una copia de la lista.", I.l_copy, ret="lista")
    m("join", "self, sep=''", "Une los elementos en un texto.", I.l_join, ret="texto", example='", ".join(nombres)')

    s = lambda name, sig, doc, impl, **k: r.fn(name, sig, doc, impl, cat=CAT, receiver="str", **k)  # noqa: E731
    s("upper", "self", "En mayusculas.", I.s_upper, ret="texto")
    s("lower", "self", "En minusculas.", I.s_lower, ret="texto")
    s("strip", "self", "Sin espacios al principio ni al final.", I.s_strip, ret="texto")
    s("replace", "self, old, new", "Cambia un trozo por otro.", I.s_replace, ret="texto")
    s("split", "self, sep=''", "Parte el texto en una lista.", I.s_split, ret="lista")
    s("startswith", "self, prefix", "Si empieza por...", I.s_startswith, ret="bool")
    s("endswith", "self, suffix", "Si termina en...", I.s_endswith, ret="bool")
    s("find", "self, part", "Posicion de un trozo (-1 si no esta).", I.s_find, ret="entero")
    s("contains", "self, part", "Si contiene un trozo.", I.s_contains, ret="bool")
    s("pad", "self, width, fill='0'", "Rellena por la izquierda hasta un ancho (\"7\".pad(3) -> \"007\").", I.s_pad,
      ret="texto")
