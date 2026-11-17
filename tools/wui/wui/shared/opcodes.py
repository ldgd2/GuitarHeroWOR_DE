"""Juego de instrucciones de la maquina de wui.

Cada instruccion mide 8 bytes (little endian), igual en todas las maquinas (Python, navegador y re-wor):

    u8 op | u8 a | u16 b | i32 c

Asi se decodifica sin tablas de tamanos y un salto es un numero de instruccion (no un desplazamiento en bytes).
La maquina es de pila. Uso de los campos por instruccion (lo que no se nombra vale 0):

    CONST   c = constante                       LOADL / STOREL  b = variable local
    LOADG / STOREG  b = variable global          JMP JF JT JFK JTK  c = instruccion destino
    CALL    b = funcion, a = argumentos          CALLB   b = funcion de la libreria, a = argumentos,
    CALLM   b = nombre del metodo, a = args              c = constante con los nombres de los argumentos con
    GETA / SETA  b = nombre del atributo                     nombre (tupla) o -1
    LIST    b = elementos                        FORMAT  c = constante con el formato ("", ".2f", ...)
    CONCAT  b = partes                           FREF    b = funcion (como valor)
    CALLV   a = argumentos (la funcion esta debajo de ellos en la pila)
"""
from __future__ import annotations

import struct
from enum import IntEnum

INSTR = struct.Struct("<BBHi")
SIZE = INSTR.size  # 8


class Op(IntEnum):
    NOP = 0
    CONST = 1
    NONE = 2
    TRUE = 3
    FALSE = 4
    POP = 5
    DUP = 6
    SWAP = 7
    LOADL = 10
    STOREL = 11
    LOADG = 12
    STOREG = 13
    # unarias
    NEG = 20
    NOT = 21
    POS = 22
    # binarias (sacan b, sacan a, meten a <op> b)
    ADD = 30
    SUB = 31
    MUL = 32
    DIV = 33
    IDIV = 34
    MOD = 35
    POW = 36
    EQ = 40
    NE = 41
    LT = 42
    LE = 43
    GT = 44
    GE = 45
    IN = 46
    NOTIN = 47
    # saltos
    JMP = 50
    JF = 51    # saca; salta si es falso
    JT = 52    # saca; salta si es verdadero
    JFK = 53   # mira; si es falso salta y lo deja (and), si no lo saca
    JTK = 54   # mira; si es verdadero salta y lo deja (or), si no lo saca
    # llamadas
    CALL = 60
    CALLB = 61
    CALLM = 62
    RET = 63
    RETN = 64
    FREF = 65   # mete la funcion b como valor
    CALLV = 66  # llama al valor funcion que hay debajo de los a argumentos
    # datos
    GETA = 70
    SETA = 71
    GETI = 72
    SETI = 73
    LIST = 74
    FORMAT = 75
    CONCAT = 76
    # control del tiempo
    WAIT = 80  # saca segundos; la tarea duerme (0 = hasta el siguiente fotograma)


BINARY = {Op.ADD, Op.SUB, Op.MUL, Op.DIV, Op.IDIV, Op.MOD, Op.POW, Op.EQ, Op.NE, Op.LT, Op.LE, Op.GT, Op.GE,
          Op.IN, Op.NOTIN}
JUMPS = {Op.JMP, Op.JF, Op.JT, Op.JFK, Op.JTK}


def encode(op: Op, a: int = 0, b: int = 0, c: int = 0) -> bytes:
    return INSTR.pack(int(op), a & 0xFF, b & 0xFFFF, c)


def decode(code: bytes) -> list[tuple[Op, int, int, int]]:
    if len(code) % SIZE:
        raise ValueError("codigo con un tamano que no es multiplo de 8")
    return [(Op(o), a, b, c) for o, a, b, c in INSTR.iter_unpack(code)]
