"""Caso de uso: leer el texto de Python y convertirlo en un arbol (o en un diagnostico si no es Python valido)."""
from __future__ import annotations

import ast
from dataclasses import dataclass, field

from ...shared.errors import Diagnostic

SYNTAX_ES = [
    ("invalid syntax", "sintaxis no valida"),
    ("expected ':'", "falta ':' al final de la linea"),
    ("unexpected indent", "sangria inesperada (sobran espacios al principio)"),
    ("expected an indented block", "falta el bloque con sangria debajo"),
    ("unindent does not match any outer indentation level", "la sangria no coincide con ninguna anterior"),
    ("was never closed", "no se cerro"),
    ("unterminated string literal", "texto sin cerrar (falta una comilla)"),
    ("invalid character", "caracter no valido"),
    ("unmatched", "sobra un cierre"),
    ("Perhaps you forgot a comma?", "quizas falta una coma"),
    ("cannot assign to", "no se puede asignar a"),
]


@dataclass
class ParseRequest:
    source: str
    filename: str = "<interfaz>"


@dataclass
class ParseResult:
    tree: ast.Module | None
    diagnostics: list[Diagnostic] = field(default_factory=list)


def translate(msg: str) -> str:
    for en, es in SYNTAX_ES:
        if en in msg:
            msg = msg.replace(en, es)
    return msg


def parse_source(req: ParseRequest) -> ParseResult:
    src = req.source.replace("\r\n", "\n").replace("\t", "    ")
    try:
        return ParseResult(ast.parse(src, filename=req.filename))
    except SyntaxError as ex:
        d = Diagnostic(f"error de sintaxis: {translate(ex.msg)}", ex.lineno or 0, max(0, (ex.offset or 1) - 1),
                       ex.end_lineno or ex.lineno or 0, max(0, (ex.end_offset or 1) - 1), code="E_SYNTAX")
        return ParseResult(None, [d])
