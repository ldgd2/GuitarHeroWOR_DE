"""Interfaz comun de los modulos de la herramienta.

Cada modulo sabe reconocer un tipo de contenido (detect) e instalarlo como mod del juego (install).
Los resultados se devuelven como Report (lineas info/aviso/error) para mostrarlos en la app o CLI.
"""
from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import Path
from typing import Callable

Log = Callable[[str], None]


@dataclass
class Report:
    title: str
    ok: bool = True
    lines: list[tuple[str, str]] = field(default_factory=list)  # (nivel, texto)  nivel: info|warn|error
    mod_folder: str = ""

    def info(self, t: str) -> None:
        self.lines.append(("info", t))

    def warn(self, t: str) -> None:
        self.lines.append(("warn", t))

    def error(self, t: str) -> None:
        self.lines.append(("error", t))
        self.ok = False

    def text(self) -> str:
        mark = {"info": "  ", "warn": "! ", "error": "X "}
        return "\n".join([self.title] + [mark[l] + t for l, t in self.lines])


@dataclass
class Detection:
    module: str          # nombre del modulo que lo reconoce
    kind: str            # song | skin | venue | other
    source: Path
    label: str           # texto corto para la lista
    supported: bool      # se puede instalar tal cual con esta version
    notes: list[str] = field(default_factory=list)


class Module:
    name = "base"
    kinds: tuple[str, ...] = ()

    def detect(self, path: Path) -> Detection | None:
        raise NotImplementedError

    def install(self, det: Detection, ws, log: Log, **opts) -> Report:
        raise NotImplementedError
