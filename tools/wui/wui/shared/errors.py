"""Errores y diagnosticos de wui (los mensajes van en espanol: los lee quien hace la interfaz)."""
from __future__ import annotations

from dataclasses import asdict, dataclass, field


@dataclass
class Diagnostic:
    """Un problema en el codigo fuente. severity: error | warning | info"""
    message: str
    line: int = 0
    col: int = 0
    end_line: int = 0
    end_col: int = 0
    severity: str = "error"
    code: str = ""
    hint: str = ""

    def to_dict(self) -> dict:
        return asdict(self)

    def __str__(self) -> str:
        where = f"linea {self.line}" + (f", columna {self.col + 1}" if self.col else "") if self.line else "programa"
        tip = f"\n    sugerencia: {self.hint}" if self.hint else ""
        return f"{self.severity} ({where}): {self.message}{tip}"


class WuiError(Exception):
    """Base de todos los errores de wui."""


@dataclass
class CompileError(WuiError):
    diagnostics: list[Diagnostic] = field(default_factory=list)

    def __str__(self) -> str:
        return "\n".join(str(d) for d in self.diagnostics if d.severity == "error") or "error de compilacion"


class ProgramFormatError(WuiError):
    """Un .wuib danado o de una version que no se entiende."""


@dataclass
class RuntimeFault(WuiError):
    """Error al ejecutar: detiene solo la tarea que lo provoco (la interfaz sigue)."""
    message: str
    function: str = ""
    line: int = 0
    trace: list[str] = field(default_factory=list)

    def __str__(self) -> str:
        at = f" en {self.function}()" if self.function else ""
        ln = f", linea {self.line}" if self.line else ""
        return f"{self.message}{at}{ln}"
