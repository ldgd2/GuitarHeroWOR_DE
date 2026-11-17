"""Valores de wui y su conversion a texto.

Tipos: nada (None), bool, entero, decimal, texto, lista y estos de aqui:
  Layer   una capa de la interfaz (por nombre)
  FuncRef una funcion del programa usada como valor (para spawn, after, callbacks)
  TaskRef una tarea en marcha (lo que devuelven spawn y after)
"""
from __future__ import annotations

from dataclasses import dataclass
from typing import Any, Callable


@dataclass(frozen=True)
class Layer:
    name: str

    def __str__(self) -> str:
        return f"capa({self.name})"


@dataclass(frozen=True)
class FuncRef:
    index: int
    name: str

    def __str__(self) -> str:
        return f"funcion {self.name}"


@dataclass(eq=False)
class TaskRef:
    id: int
    name: str = ""

    def __str__(self) -> str:
        return f"tarea {self.name or self.id}"


@dataclass
class Block:
    """Lo que devuelve una funcion de la libreria que duerme la tarea. poll(vm) -> (terminado, valor).
    La maquina lo vuelve a preguntar cada fotograma; al terminar, el valor es el resultado de la llamada."""
    poll: Callable[[Any], tuple[bool, Any]]
    why: str = ""


def type_name(v: Any) -> str:
    if v is None:
        return "nada"
    if isinstance(v, bool):
        return "bool"
    if isinstance(v, int):
        return "entero"
    if isinstance(v, float):
        return "decimal"
    if isinstance(v, str):
        return "texto"
    if isinstance(v, list):
        return "lista"
    if isinstance(v, Layer):
        return "capa"
    if isinstance(v, FuncRef):
        return "funcion"
    if isinstance(v, TaskRef):
        return "tarea"
    return type(v).__name__


def receiver_of(v: Any) -> str:
    """nombre del tipo para buscar metodos y propiedades en el registro"""
    if isinstance(v, Layer):
        return "Layer"
    if isinstance(v, TaskRef):
        return "Task"
    if isinstance(v, list):
        return "list"
    if isinstance(v, str):
        return "str"
    return type_name(v)


def to_text(v: Any) -> str:
    if v is None:
        return "nada"
    if v is True:
        return "True"
    if v is False:
        return "False"
    if isinstance(v, float):
        return repr(int(v)) + ".0" if v.is_integer() and abs(v) < 1e15 else repr(v)
    if isinstance(v, list):
        return "[" + ", ".join(repr(x) if isinstance(x, str) else to_text(x) for x in v) + "]"
    return str(v)


def truthy(v: Any) -> bool:
    if isinstance(v, (Layer, FuncRef, TaskRef)):
        return True
    return bool(v)


def is_number(v: Any) -> bool:
    return isinstance(v, (int, float)) and not isinstance(v, bool)


# ---------------------------------------------------------------------- colores
def parse_color(v: Any) -> tuple[int, int, int] | None:
    """'#f00', '#ff0000', 'ff0000', [255, 0, 0] o un nombre (red, white...) -> (r, g, b)"""
    if isinstance(v, (list, tuple)) and len(v) >= 3 and all(is_number(x) for x in v[:3]):
        return tuple(max(0, min(255, int(round(x)))) for x in v[:3])
    if not isinstance(v, str):
        return None
    s = NAMED_COLORS.get(v.strip().lower(), v.strip().lstrip("#"))
    if len(s) == 3:
        s = "".join(c * 2 for c in s)
    if len(s) != 6:
        return None
    try:
        return int(s[0:2], 16), int(s[2:4], 16), int(s[4:6], 16)
    except ValueError:
        return None


def color_hex(rgb: tuple[int, int, int]) -> str:
    return "#{:02x}{:02x}{:02x}".format(*rgb)


NAMED_COLORS = {
    "white": "ffffff", "black": "000000", "red": "ff2a2a", "green": "2ee05a", "yellow": "ffd21f",
    "blue": "2a8cff", "orange": "ff8a1f", "purple": "9b5cff", "pink": "ff5cc8", "cyan": "2ae6ff",
    "gray": "808080", "grey": "808080", "gold": "e3a043", "star": "6ad7ff",
    "blanco": "ffffff", "negro": "000000", "rojo": "ff2a2a", "verde": "2ee05a", "amarillo": "ffd21f",
    "azul": "2a8cff", "naranja": "ff8a1f", "morado": "9b5cff", "rosa": "ff5cc8", "gris": "808080",
    "dorado": "e3a043",
}
