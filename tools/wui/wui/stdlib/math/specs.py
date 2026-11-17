"""Matematicas, azar y formato de numeros."""
from __future__ import annotations

from . import impl as I

CAT = "matematicas"


def register(r) -> None:
    r.categories[CAT] = "Numeros, azar, interpolacion y formato"
    f = lambda *a, **k: r.fn(*a, cat=CAT, **k)  # noqa: E731
    f("abs", "x", "Valor absoluto.", I.abs_, ret="numero")
    f("min", "*values", "El menor (de varios valores o de una lista).", I.min_, ret="numero")
    f("max", "*values", "El mayor (de varios valores o de una lista).", I.max_, ret="numero")
    f("sum", "values", "Suma de una lista.", I.sum_, ret="numero")
    f("round", "x, digits=0", "Redondea (a cierta cantidad de decimales).", I.round_, ret="numero")
    f("floor", "x", "Redondea hacia abajo.", I.floor, ret="entero")
    f("ceil", "x", "Redondea hacia arriba.", I.ceil, ret="entero")
    f("sqrt", "x", "Raiz cuadrada.", I.sqrt, ret="decimal")
    f("sin", "degrees", "Seno (en grados).", I.sin, ret="decimal", example="y = 360 + sin(time() * 180) * 20")
    f("cos", "degrees", "Coseno (en grados).", I.cos, ret="decimal")
    f("atan2", "y, x", "Angulo en grados de un vector.", I.atan2, ret="decimal")
    f("clamp", "x, lo, hi", "Limita x entre lo y hi.", I.clamp, ret="numero", example="vida = clamp(vida - 10, 0, 100)")
    f("lerp", "a, b, t", "Mezcla entre a y b (t de 0 a 1).", I.lerp, ret="decimal")
    f("remap", "x, a, b, c, d", "Pasa x del rango a..b al rango c..d.", I.remap, ret="decimal",
      example="ancho = remap(score, 0, 100000, 0, 400)")
    f("approach", "x, target, step", "Acerca x a target sin pasarse (para movimientos suaves).", I.approach,
      ret="decimal")
    f("sign", "x", "-1, 0 o 1.", I.sign, ret="entero")
    f("random", "lo=0.0, hi=1.0", "Decimal al azar entre lo y hi.", I.random, ret="decimal")
    f("randint", "lo, hi", "Entero al azar entre lo y hi (ambos incluidos).", I.randint, ret="entero")
    f("choice", "items", "Un elemento al azar de una lista.", I.choice, ret="cualquiera")
    f("shuffle", "items", "Desordena una lista.", I.shuffle)
    f("format_number", "x, decimals=0, sep=True", "Numero con separador de miles (12345 -> 12,345).",
      I.format_number, ret="texto", example='layer("puntos").text = format_number(score)')
    f("time_text", "seconds, hours=False", "Segundos como reloj (75 -> 1:15).", I.time_text, ret="texto")
    f("percent", "value, total, decimals=0", "Porcentaje como texto (3, 4 -> 75%).", I.percent, ret="texto")
    r.const("PI", 3.141592653589793)
    r.const("TAU", 6.283185307179586)
    r.const("INF", float("inf"))
