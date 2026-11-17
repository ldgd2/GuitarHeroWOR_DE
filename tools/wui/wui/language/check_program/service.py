"""Caso de uso: comprobar que el programa usa solo la parte de Python que entiende wui.

Se aceptan: variables, numeros, textos, listas, f-strings, operaciones, comparaciones, and/or/not, if/elif/else,
while, for (sobre range, listas y textos), break, continue, funciones con parametros y valores por defecto,
return, global, decoradores de eventos y "from wui import *". Lo demas se explica con un mensaje y una
alternativa, todos a la vez (para corregirlos de una pasada)."""
from __future__ import annotations

import ast
from dataclasses import dataclass, field

from ...shared.errors import Diagnostic

UNSUPPORTED: dict[type, tuple[str, str]] = {
    ast.ClassDef: ("las clases no se pueden usar", "usa funciones y variables globales"),
    ast.Lambda: ("lambda no se puede usar", "define una funcion con def y pasa su nombre"),
    ast.ListComp: ("las listas por comprension no se pueden usar", "usa un for que haga append()"),
    ast.SetComp: ("los conjuntos no se pueden usar", "usa una lista"),
    ast.DictComp: ("los diccionarios no se pueden usar", "usa listas o variables"),
    ast.GeneratorExp: ("los generadores no se pueden usar", "usa un for"),
    ast.Dict: ("los diccionarios no se pueden usar", "usa listas o variables con nombre"),
    ast.Set: ("los conjuntos no se pueden usar", "usa una lista"),
    ast.Try: ("try/except no se puede usar", "comprueba antes con if (los errores paran solo esa funcion)"),
    ast.With: ("with no se puede usar", ""),
    ast.Raise: ("raise no se puede usar", "usa warn() o log() y return"),
    ast.Assert: ("assert no se puede usar", "usa if y warn()"),
    ast.Delete: ("del no se puede usar", "para listas usa .pop() o .remove()"),
    ast.Yield: ("yield no se puede usar", "para esperar usa wait()"),
    ast.YieldFrom: ("yield no se puede usar", "para esperar usa wait()"),
    ast.Await: ("await no se puede usar", "las funciones ya pueden esperar con wait()"),
    ast.AsyncFunctionDef: ("async no hace falta", "cualquier funcion puede esperar con wait()"),
    ast.AsyncFor: ("async no hace falta", ""),
    ast.AsyncWith: ("async no hace falta", ""),
    ast.Nonlocal: ("nonlocal no se puede usar", "usa una variable global"),
    ast.NamedExpr: ("el operador := no se puede usar", "asigna en una linea aparte"),
    ast.Starred: ("*valores no se puede usar", "pasa los valores uno a uno"),
    ast.Match: ("match no se puede usar", "usa if / elif"),
}
TRY_STAR = getattr(ast, "TryStar", None)
if TRY_STAR:
    UNSUPPORTED[TRY_STAR] = UNSUPPORTED[ast.Try]

BAD_BINOPS = {ast.BitAnd: "&", ast.BitOr: "|", ast.BitXor: "^", ast.LShift: "<<", ast.RShift: ">>",
              ast.MatMult: "@"}


@dataclass
class CheckRequest:
    tree: ast.Module


@dataclass
class CheckResult:
    diagnostics: list[Diagnostic] = field(default_factory=list)

    @property
    def ok(self) -> bool:
        return not any(d.severity == "error" for d in self.diagnostics)


def _diag(node, msg, hint="", code="E_UNSUPPORTED", severity="error") -> Diagnostic:
    return Diagnostic(msg, getattr(node, "lineno", 0), getattr(node, "col_offset", 0),
                      getattr(node, "end_lineno", 0) or 0, getattr(node, "end_col_offset", 0) or 0, severity, code,
                      hint)


class _Checker(ast.NodeVisitor):
    def __init__(self):
        self.out: list[Diagnostic] = []
        self.depth = 0          # funciones anidadas

    def generic_visit(self, node):
        bad = UNSUPPORTED.get(type(node))
        if bad:
            self.out.append(_diag(node, bad[0], bad[1]))
            return
        super().generic_visit(node)

    def visit_Import(self, node):
        if any(a.name != "wui" for a in node.names):
            self.out.append(_diag(node, "solo se puede importar wui", "todo lo demas ya esta disponible"))

    def visit_ImportFrom(self, node):
        if node.module != "wui" and not (node.module or "").startswith("wui."):
            self.out.append(_diag(node, f"no se puede importar '{node.module}'",
                                  "las funciones de la libreria ya estan disponibles sin importar nada"))

    def visit_FunctionDef(self, node):
        if self.depth:
            self.out.append(_diag(node, "no se pueden definir funciones dentro de otras",
                                  "definela fuera, al principio de la linea"))
        a = node.args
        if a.vararg or a.kwarg or a.kwonlyargs or a.posonlyargs:
            self.out.append(_diag(node, "las funciones solo pueden tener parametros normales (con o sin valor por "
                                        "defecto)", "quita *args, **kwargs y los parametros despues de *"))
        if node.returns or any(x.annotation for x in a.args):
            pass                                       # las anotaciones de tipo se ignoran
        self.depth += 1
        for st in node.body:
            self.visit(st)
        self.depth -= 1
        for d in node.decorator_list:
            self.visit(d)
        for d in a.defaults:
            self.visit(d)

    def visit_For(self, node):
        if node.orelse:
            self.out.append(_diag(node, "for ... else no se puede usar", "usa una variable que diga si se encontro"))
        self.generic_visit(node)

    def visit_While(self, node):
        if node.orelse:
            self.out.append(_diag(node, "while ... else no se puede usar", ""))
        self.generic_visit(node)

    def visit_BinOp(self, node):
        if type(node.op) in BAD_BINOPS:
            self.out.append(_diag(node, f"el operador {BAD_BINOPS[type(node.op)]} no se puede usar",
                                  "usa + - * / // % y **"))
        self.generic_visit(node)

    def visit_AugAssign(self, node):
        if type(node.op) in BAD_BINOPS:
            self.out.append(_diag(node, f"el operador {BAD_BINOPS[type(node.op)]}= no se puede usar", ""))
        self.generic_visit(node)

    def visit_UnaryOp(self, node):
        if isinstance(node.op, ast.Invert):
            self.out.append(_diag(node, "el operador ~ no se puede usar", ""))
        self.generic_visit(node)

    def visit_Constant(self, node):
        if isinstance(node.value, (bytes, complex)) or node.value is Ellipsis:
            self.out.append(_diag(node, "ese tipo de valor no se puede usar", "usa numeros, textos, True/False o None"))

    def visit_Subscript(self, node):
        if isinstance(node.slice, ast.Slice) and node.slice.step is not None:
            self.out.append(_diag(node, "los cortes con paso [a:b:c] no se pueden usar", "usa [a:b]"))
        self.generic_visit(node)

    def visit_Call(self, node):
        for k in node.keywords:
            if k.arg is None:
                self.out.append(_diag(k.value, "**valores no se puede usar", "escribe cada argumento con su nombre"))
        self.generic_visit(node)

    def visit_JoinedStr(self, node):
        for v in node.values:
            if isinstance(v, ast.FormattedValue) and isinstance(v.format_spec, ast.JoinedStr) and \
                    any(not isinstance(x, ast.Constant) for x in v.format_spec.values):
                self.out.append(_diag(v, "el formato de un f-string tiene que ser fijo", 'p. ej. f"{x:.2f}"'))
        self.generic_visit(node)

    def visit_Global(self, node):
        if not self.depth:
            self.out.append(_diag(node, "global solo tiene sentido dentro de una funcion", "", severity="warning",
                                  code="W_GLOBAL"))


def check_program(req: CheckRequest) -> CheckResult:
    c = _Checker()
    for st in req.tree.body:
        if isinstance(st, ast.Return):
            c.out.append(_diag(st, "return fuera de una funcion"))
        c.visit(st)
    return CheckResult(c.out)
