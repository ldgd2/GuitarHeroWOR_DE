"""Caso de uso: compilar un programa de wui (Python) a instrucciones de la maquina.

Entrada: CompileRequest (texto y, opcional, las capas y pantallas de la interfaz para avisar de nombres mal
escritos). Salida: CompileResult (programa o nada, y todos los diagnosticos).

Reglas de nombres (como Python): dentro de una funcion, un nombre al que se asigna es local salvo que se declare
global. Al leer un nombre se busca: local, global, funcion propia, constante (PI, Button.RED...) y libreria.
"""
from __future__ import annotations

import ast
from dataclasses import dataclass, field
from difflib import get_close_matches
from typing import Any

from ...shared.errors import Diagnostic
from ...shared.opcodes import Op
from ...shared.program import NO_FUNC, Function, Handler, Program
from ...stdlib.registry import REQUIRED, Builtin, Registry, registry
from ..check_program.service import CheckRequest, check_program
from ..parse_source.service import ParseRequest, parse_source
from .emitter import FunctionBuilder, Pools

BINOPS = {ast.Add: Op.ADD, ast.Sub: Op.SUB, ast.Mult: Op.MUL, ast.Div: Op.DIV, ast.FloorDiv: Op.IDIV,
          ast.Mod: Op.MOD, ast.Pow: Op.POW}
CMPOPS = {ast.Eq: Op.EQ, ast.NotEq: Op.NE, ast.Lt: Op.LT, ast.LtE: Op.LE, ast.Gt: Op.GT, ast.GtE: Op.GE,
          ast.In: Op.IN, ast.NotIn: Op.NOTIN, ast.Is: Op.EQ, ast.IsNot: Op.NE}
INIT = "__init__"
WAITERS = {"wait", "wait_frames", "wait_press", "wait_release", "wait_until"}


@dataclass
class CompileRequest:
    source: str
    filename: str = "<interfaz>"
    layers: list[str] | None = None      # nombres de capas de la interfaz (para avisos)
    screens: list[str] | None = None     # nombres de pantallas


@dataclass
class CompileResult:
    program: Program | None
    diagnostics: list[Diagnostic] = field(default_factory=list)

    @property
    def ok(self) -> bool:
        return self.program is not None

    @property
    def errors(self) -> list[Diagnostic]:
        return [d for d in self.diagnostics if d.severity == "error"]


class _Abort(Exception):
    """error en una expresion: se anota y se sigue con la siguiente sentencia"""


@dataclass
class _UserFunc:
    index: int
    node: ast.FunctionDef
    params: list[str]
    defaults: list[Any]


class Compiler:
    def __init__(self, req: CompileRequest, reg: Registry):
        self.req, self.reg = req, reg
        self.diags: list[Diagnostic] = []
        self.pools = Pools()
        self.funcs: dict[str, _UserFunc] = {}
        self.globals: set[str] = set()
        self.handlers: list[Handler] = []
        self.fb: FunctionBuilder | None = None
        self.declared_global: set[str] = set()
        self.in_init = False
        self.loops: list[tuple] = []
        self.const_groups = reg.constant_groups()

    # ================================================================== diagnosticos
    def diag(self, node, msg, hint="", code="E", severity="error"):
        self.diags.append(Diagnostic(msg, getattr(node, "lineno", 0) or 0, getattr(node, "col_offset", 0) or 0,
                                     getattr(node, "end_lineno", 0) or 0, getattr(node, "end_col_offset", 0) or 0,
                                     severity, code, hint))

    def fail(self, node, msg, hint="", code="E"):
        self.diag(node, msg, hint, code)
        raise _Abort

    def warn(self, node, msg, hint="", code="W"):
        self.diag(node, msg, hint, code, "warning")

    @staticmethod
    def near(word: str, options) -> str:
        m = get_close_matches(word, list(options), 1, 0.7)
        return f"quisiste decir '{m[0]}'?" if m else ""

    # ================================================================== programa
    def compile(self, tree: ast.Module) -> Program | None:
        init_body = []
        for st in tree.body:
            if isinstance(st, ast.FunctionDef):
                if st.name in self.funcs:
                    self.diag(st, f"la funcion '{st.name}' esta definida dos veces", code="E_DUP")
                self.funcs[st.name] = _UserFunc(len(self.funcs), st, [a.arg for a in st.args.args], [])
            elif isinstance(st, (ast.Import, ast.ImportFrom)):
                continue
            else:
                init_body.append(st)
                for t in _assigned(st):
                    self.globals.add(t)
        for f in self.funcs.values():
            for st in ast.walk(f.node):
                if isinstance(st, ast.Global):
                    self.globals.update(st.names)
        for g in sorted(self.globals):
            self.pools.glob(g)
            if g in self.funcs:
                self.diag(self.funcs[g].node, f"'{g}' es a la vez una variable y una funcion", code="E_DUP")
            elif g in self.reg.functions:
                self.warn(tree, f"la variable global '{g}' tapa la funcion {g}() de la libreria", code="W_SHADOW")
        for name, f in self.funcs.items():
            if name in self.reg.functions:
                self.warn(f.node, f"tu funcion '{name}' tapa la de la libreria con el mismo nombre", code="W_SHADOW")
            f.defaults = [self.const_value(d, "un valor por defecto") for d in f.node.args.defaults]

        functions: list[Function] = []
        for f in self.funcs.values():
            functions.append(self.compile_function(f))
            for dec in f.node.decorator_list:
                self.handler(dec, f)
        init = NO_FUNC
        if init_body:
            init = len(functions)
            functions.append(self.compile_body(INIT, [], init_body, is_init=True))

        if any(d.severity == "error" for d in self.diags):
            return None
        return Program(source=self.req.filename, consts=self.pools.consts, names=self.pools.names,
                       builtins=self.pools.builtins, globals=self.pools.globals, functions=functions,
                       handlers=self.handlers, init=init)

    def compile_function(self, f: _UserFunc) -> Function:
        fn = self.compile_body(f.node.name, f.params, f.node.body, node=f.node)
        fn.defaults = f.defaults
        fn.flags = 1 if f.node.decorator_list else 0
        return fn

    def compile_body(self, name, params, body, is_init=False, node=None) -> Function:
        self.fb = FunctionBuilder(name, params)
        self.in_init = is_init
        self.loops = []
        self.declared_global = set()
        assigned = set()
        for st in body:
            for sub in ast.walk(st):
                if isinstance(sub, ast.Global):
                    self.declared_global.update(sub.names)
            assigned.update(_assigned(st))
        if not is_init:
            for n in sorted(assigned - self.declared_global - set(params)):
                self.fb.local(n)
                if n in self.globals:
                    where = next((s for s in ast.walk(ast.Module(body=body, type_ignores=[]))
                                  if isinstance(s, ast.Name) and s.id == n and isinstance(s.ctx, ast.Store)), node)
                    self.warn(where, f"'{n}' es una variable local de {name}() (hay una global con el mismo nombre)",
                              f"si quieres cambiar la global, escribe 'global {n}' al principio de la funcion",
                              "W_LOCAL_SHADOWS_GLOBAL")
        self.block(body)
        self.fb.emit(Op.RETN)
        fb = self.fb
        names = sorted(fb.locals, key=fb.locals.get)
        return Function(name, len(params), len(fb.locals), fb.bytes(), fb.lines, 0, [], names)

    # ================================================================== eventos
    def handler(self, dec: ast.expr, f: _UserFunc) -> None:
        call = dec if isinstance(dec, ast.Call) else None
        name = (call.func if call else dec)
        if not isinstance(name, ast.Name) or name.id not in self.reg.events:
            label = ast.unparse(name) if isinstance(name, ast.AST) else "?"
            self.diag(dec, f"'{label}' no es un evento", self.near(label, self.reg.events) or
                      "eventos: " + ", ".join(sorted(self.reg.events)), "E_EVENT")
            return
        ev = self.reg.events[name.id]
        try:
            args = self.bind_const(ev.params, call.args if call else [], call.keywords if call else [], dec,
                                   f"@{ev.name}")
        except _Abort:
            return
        n = len(f.params) - len(f.defaults)
        if n > len(ev.gives):
            give = ", ".join(ev.gives) or "nada"
            self.diag(f.node, f"{f.node.name}() pide {n} parametro(s) pero @{ev.name} solo da: {give}",
                      code="E_EVENT_ARGS")
        self.check_event_args(ev.name, args, dec)
        self.handlers.append(Handler(ev.name, f.index, args))

    def check_event_args(self, ev: str, args: list, node) -> None:
        from ...stdlib.pad.specs import BUTTONS
        if ev in ("on_press", "on_release", "on_hold", "on_repeat") and args and args[0] is not None \
                and args[0] not in BUTTONS.values():
            self.diag(node, f"boton desconocido '{args[0]}'", self.near(str(args[0]), BUTTONS.values()) or
                      "usa Button.GREEN, Button.RED, ...", "E_BUTTON")
        if ev in ("every", "on_hold") and isinstance(args[-2 if ev == "on_hold" else 0], (int, float)) \
                and args[-2 if ev == "on_hold" else 0] <= 0:
            self.diag(node, f"@{ev}: el tiempo tiene que ser mayor que 0", code="E_EVENT_ARGS")
        if ev in ("on_enter", "on_exit") and args and isinstance(args[0], str):
            self.check_name(args[0], self.req.screens, "pantalla", node)
        if ev in ("on_click", "on_focus", "on_blur", "on_hover") and args and isinstance(args[0], str):
            self.check_name(args[0], self.req.layers, "capa", node)
        if ev == "on_change" and args and args[0] not in self.globals:
            self.diag(node, f"@on_change('{args[0]}'): no hay ninguna variable global con ese nombre",
                      self.near(str(args[0]), self.globals), "E_VAR")

    def check_name(self, name: str, known: list[str] | None, what: str, node) -> None:
        if known is not None and name not in known:
            self.warn(node, f"no hay ninguna {what} llamada '{name}'", self.near(name, known), "W_NAME")

    # ================================================================== constantes
    def const_value(self, node: ast.expr, what: str) -> Any:
        if isinstance(node, ast.Constant):
            return node.value
        if isinstance(node, ast.UnaryOp) and isinstance(node.op, (ast.USub, ast.UAdd)):
            v = self.const_value(node.operand, what)
            if isinstance(v, (int, float)) and not isinstance(v, bool):
                return -v if isinstance(node.op, ast.USub) else v
        if isinstance(node, ast.Attribute) and isinstance(node.value, ast.Name):
            key = f"{node.value.id}.{node.attr}"
            if key in self.reg.constants:
                return self.reg.constants[key]
        if isinstance(node, ast.Name) and node.id in self.reg.constants:
            return self.reg.constants[node.id]
        self.fail(node, f"{what} tiene que ser un valor fijo (numero, texto, True/False, None o una constante como "
                        "Button.RED)", code="E_CONST")

    def bind_const(self, params, args, keywords, node, who) -> list[Any]:
        vals = [self.const_value(a, f"el argumento de {who}") for a in args]
        kws = {k.arg: self.const_value(k.value, f"el argumento {k.arg} de {who}") for k in keywords}
        if len(vals) > len(params):
            self.fail(node, f"{who} recibe como mucho {len(params)} argumento(s)", code="E_ARGS")
        out = []
        for i, p in enumerate(params):
            if i < len(vals):
                if p.name in kws:
                    self.fail(node, f"{who}: '{p.name}' esta dos veces", code="E_ARGS")
                out.append(vals[i])
            elif p.name in kws:
                out.append(kws.pop(p.name))
            elif p.default is not REQUIRED:
                out.append(p.default)
            else:
                self.fail(node, f"{who}: falta '{p.name}'", code="E_ARGS")
        for k in kws:
            self.fail(node, f"{who} no tiene un argumento '{k}'", self.near(k, [p.name for p in params]), "E_ARGS")
        return out

    # ================================================================== sentencias
    def block(self, body: list[ast.stmt]) -> None:
        for i, st in enumerate(body):
            if i == 0 and isinstance(st, ast.Expr) and isinstance(st.value, ast.Constant) \
                    and isinstance(st.value.value, str):
                continue                                         # texto de documentacion
            try:
                self.stmt(st)
            except _Abort:
                pass

    def stmt(self, st: ast.stmt) -> None:
        fb = self.fb
        fb.at(st)
        m = getattr(self, "s_" + type(st).__name__, None)
        if m is None:
            self.fail(st, f"'{type(st).__name__}' no se puede usar aqui", code="E_UNSUPPORTED")
        m(st)

    def s_Expr(self, st):
        v = st.value
        if not isinstance(v, ast.Call):
            self.warn(st, "esta linea calcula algo pero no lo usa", "quizas querias asignarlo: x = ...",
                      "W_UNUSED")
        self.expr(v)
        self.fb.emit(Op.POP)

    def s_Pass(self, st):
        pass

    def s_Global(self, st):
        if self.in_init:
            return

    def s_FunctionDef(self, st):
        self.fail(st, "no se pueden definir funciones dentro de otras", code="E_NESTED")

    def s_Return(self, st):
        if self.in_init:
            self.fail(st, "return fuera de una funcion")
        if st.value is None:
            self.fb.emit(Op.RETN)
        else:
            self.expr(st.value)
            self.fb.emit(Op.RET)

    def s_Assign(self, st):
        if len(st.targets) == 1:
            self.store(st.targets[0], lambda: self.expr(st.value))
            return
        t = self.fb.temp()
        self.expr(st.value)
        self.fb.emit(Op.STOREL, b=t)
        for tg in st.targets:
            self.store(tg, lambda: self.fb.emit(Op.LOADL, b=t))

    def s_AnnAssign(self, st):
        if st.value is not None:
            self.store(st.target, lambda: self.expr(st.value))

    def s_AugAssign(self, st):
        op = BINOPS[type(st.op)]
        tg, fb = st.target, self.fb
        if isinstance(tg, ast.Name):
            self.load_name(tg)
            self.expr(st.value)
            fb.emit(op)
            self.store_name(tg)
        elif isinstance(tg, ast.Attribute):
            t = fb.temp()
            self.expr(tg.value)
            fb.emit(Op.STOREL, b=t)
            fb.emit(Op.LOADL, b=t)
            fb.emit(Op.LOADL, b=t)
            self.check_prop(tg, write=True)
            fb.emit(Op.GETA, b=self.pools.name(tg.attr))
            self.expr(st.value)
            fb.emit(op)
            fb.emit(Op.SETA, b=self.pools.name(tg.attr))
        elif isinstance(tg, ast.Subscript):
            if isinstance(tg.slice, ast.Slice):
                self.fail(tg, "no se puede cambiar un corte [a:b]")
            to, ti = fb.temp(), fb.temp()
            self.expr(tg.value)
            fb.emit(Op.STOREL, b=to)
            self.expr(tg.slice)
            fb.emit(Op.STOREL, b=ti)
            for x in (to, ti, to, ti):
                fb.emit(Op.LOADL, b=x)
            fb.emit(Op.GETI)
            self.expr(st.value)
            fb.emit(op)
            fb.emit(Op.SETI)
        else:
            self.fail(tg, "no se puede usar ese operador aqui")

    def s_If(self, st):
        fb = self.fb
        orelse, end = fb.label(), fb.label()
        self.cond(st.test)
        fb.jump(Op.JF, orelse)
        self.block(st.body)
        if st.orelse:
            fb.jump(Op.JMP, end)
        fb.mark(orelse)
        if st.orelse:
            self.block(st.orelse)
        fb.mark(end)

    def s_While(self, st):
        fb = self.fb
        top, end = fb.label(), fb.label()
        fb.mark(top)
        forever = isinstance(st.test, ast.Constant) and bool(st.test.value)
        if not forever:
            self.cond(st.test)
            fb.jump(Op.JF, end)
        if forever and not _waits(st.body, self.funcs):
            self.warn(st, "este bucle infinito no espera nunca: congelaria la interfaz",
                      "pon wait() dentro (wait(0) espera un fotograma)", "W_BUSY_LOOP")
        self.loops.append((top, end))
        self.block(st.body)
        self.loops.pop()
        fb.jump(Op.JMP, top)
        fb.mark(end)

    def s_For(self, st):
        fb = self.fb
        it = st.iter
        if isinstance(it, ast.Call) and isinstance(it.func, ast.Name) and it.func.id == "range" \
                and it.func.id not in self.funcs and not it.keywords and 1 <= len(it.args) <= 3 \
                and self.step_sign(it) is not None:
            return self.for_range(st, it)
        seq, idx = fb.temp(), fb.temp()
        self.expr(it)
        fb.emit(Op.STOREL, b=seq)
        fb.emit(Op.CONST, c=self.pools.const(0))
        fb.emit(Op.STOREL, b=idx)
        top, cont, end = fb.label(), fb.label(), fb.label()
        fb.mark(top)
        fb.emit(Op.LOADL, b=idx)
        fb.emit(Op.LOADL, b=seq)
        fb.emit(Op.CALLB, a=1, b=self.pools.builtin("len"), c=-1)
        fb.emit(Op.LT)
        fb.jump(Op.JF, end)
        self.store(st.target, lambda: (fb.emit(Op.LOADL, b=seq), fb.emit(Op.LOADL, b=idx), fb.emit(Op.GETI)))
        self.loops.append((cont, end))
        self.block(st.body)
        self.loops.pop()
        fb.mark(cont)
        fb.emit(Op.LOADL, b=idx)
        fb.emit(Op.CONST, c=self.pools.const(1))
        fb.emit(Op.ADD)
        fb.emit(Op.STOREL, b=idx)
        fb.jump(Op.JMP, top)
        fb.mark(end)

    def step_sign(self, call: ast.Call):
        if len(call.args) < 3:
            return 1
        try:
            v = self.const_value(call.args[2], "el paso de range()")
        except _Abort:
            self.diags.pop()
            return None
        if not isinstance(v, int) or v == 0:
            return None
        return 1 if v > 0 else -1

    def for_range(self, st, call):
        fb = self.fb
        a = call.args
        sign = self.step_sign(call)
        i, stop = fb.temp(), fb.temp()
        if len(a) == 1:
            fb.emit(Op.CONST, c=self.pools.const(0))
            fb.emit(Op.STOREL, b=i)
            self.num_expr(a[0])
        else:
            self.num_expr(a[0])
            fb.emit(Op.STOREL, b=i)
            self.num_expr(a[1])
        fb.emit(Op.STOREL, b=stop)
        step = self.const_value(a[2], "el paso") if len(a) == 3 else 1
        top, cont, end = fb.label(), fb.label(), fb.label()
        fb.mark(top)
        fb.emit(Op.LOADL, b=i)
        fb.emit(Op.LOADL, b=stop)
        fb.emit(Op.LT if sign > 0 else Op.GT)
        fb.jump(Op.JF, end)
        self.store(st.target, lambda: fb.emit(Op.LOADL, b=i))
        self.loops.append((cont, end))
        self.block(st.body)
        self.loops.pop()
        fb.mark(cont)
        fb.emit(Op.LOADL, b=i)
        fb.emit(Op.CONST, c=self.pools.const(step))
        fb.emit(Op.ADD)
        fb.emit(Op.STOREL, b=i)
        fb.jump(Op.JMP, top)
        fb.mark(end)

    def num_expr(self, node):
        """expresion que range() convierte a entero"""
        self.expr(node)
        self.fb.emit(Op.CALLB, a=1, b=self.pools.builtin("int"), c=-1)

    def s_Break(self, st):
        if not self.loops:
            self.fail(st, "break fuera de un bucle")
        self.fb.jump(Op.JMP, self.loops[-1][1])

    def s_Continue(self, st):
        if not self.loops:
            self.fail(st, "continue fuera de un bucle")
        self.fb.jump(Op.JMP, self.loops[-1][0])

    # ================================================================== asignar
    def store(self, tg: ast.expr, value) -> None:
        fb = self.fb
        if isinstance(tg, ast.Name):
            value()
            self.store_name(tg)
        elif isinstance(tg, ast.Attribute):
            self.check_prop(tg, write=True)
            self.expr(tg.value)
            value()
            fb.emit(Op.SETA, b=self.pools.name(tg.attr))
        elif isinstance(tg, ast.Subscript):
            if isinstance(tg.slice, ast.Slice):
                self.fail(tg, "no se puede asignar a un corte [a:b]", "cambia los elementos uno a uno")
            self.expr(tg.value)
            self.expr(tg.slice)
            value()
            fb.emit(Op.SETI)
        elif isinstance(tg, (ast.Tuple, ast.List)):
            t = fb.temp()
            value()
            fb.emit(Op.STOREL, b=t)
            for i, sub in enumerate(tg.elts):
                self.store(sub, lambda i=i: (fb.emit(Op.LOADL, b=t), fb.emit(Op.CONST, c=self.pools.const(i)),
                                             fb.emit(Op.GETI)))
        else:
            self.fail(tg, "no se puede asignar a eso", code="E_TARGET")

    def store_name(self, tg: ast.Name) -> None:
        n = tg.id
        if n in self.reg.constants or n in self.const_groups:
            self.fail(tg, f"'{n}' es una constante de la libreria y no se puede cambiar")
        if n in self.funcs:
            self.fail(tg, f"'{n}' es una funcion; no se le puede asignar un valor")
        if self.in_init or n in self.declared_global:
            self.fb.emit(Op.STOREG, b=self.pools.glob(n))
        else:
            self.fb.emit(Op.STOREL, b=self.fb.local(n))

    # ================================================================== expresiones
    def cond(self, node) -> None:
        self.expr(node)

    def expr(self, node: ast.expr) -> None:
        m = getattr(self, "e_" + type(node).__name__, None)
        if m is None:
            self.fail(node, f"esa expresion ({type(node).__name__}) no se puede usar", code="E_UNSUPPORTED")
        m(node)

    def e_Constant(self, n):
        v = n.value
        fb = self.fb
        if v is None:
            fb.emit(Op.NONE)
        elif v is True:
            fb.emit(Op.TRUE)
        elif v is False:
            fb.emit(Op.FALSE)
        else:
            fb.emit(Op.CONST, c=self.pools.const(v))

    def e_Name(self, n):
        self.load_name(n)

    def load_name(self, n: ast.Name) -> None:
        name, fb = n.id, self.fb
        if not self.in_init and name in fb.locals and name not in self.declared_global:
            fb.emit(Op.LOADL, b=fb.locals[name])
        elif name in self.globals:
            fb.emit(Op.LOADG, b=self.pools.glob(name))
        elif name in self.funcs:
            fb.emit(Op.FREF, b=self.funcs[name].index)
        elif name in self.reg.constants:
            self.push_const(self.reg.constants[name])
        elif name in self.const_groups:
            self.fail(n, f"'{name}' es un grupo de constantes: elige una, p. ej. {name}."
                         f"{next(iter(self.const_groups[name]))}", code="E_NAME")
        elif name in self.reg.functions or name in self.reg.namespaces:
            self.fail(n, f"'{name}' es de la libreria: para usarla escribe {name}(...)",
                      "para pasar una funcion a after()/spawn() define una tuya con def", "E_NAME")
        else:
            opts = list(fb.locals) + list(self.globals) + list(self.funcs) + list(self.reg.functions) + \
                list(self.reg.constants) + list(self.const_groups)
            self.fail(n, f"'{name}' no existe", self.near(name, [o for o in opts if not o.startswith("#")]) or
                      "asignale un valor antes de usarla", "E_NAME")

    def push_const(self, v) -> None:
        """constante de la libreria; las tuplas salen como lista nueva (asi nadie cambia la constante)"""
        if isinstance(v, tuple):
            for x in v:
                self.push_const(x)
            self.fb.emit(Op.LIST, b=len(v))
        elif v is None or isinstance(v, bool):
            self.fb.emit({None: Op.NONE, True: Op.TRUE, False: Op.FALSE}[v])
        else:
            self.fb.emit(Op.CONST, c=self.pools.const(v))

    def e_BinOp(self, n):
        self.expr(n.left)
        self.expr(n.right)
        self.fb.emit(BINOPS[type(n.op)])

    def e_UnaryOp(self, n):
        if isinstance(n.op, ast.USub) and isinstance(n.operand, ast.Constant) and \
                isinstance(n.operand.value, (int, float)) and not isinstance(n.operand.value, bool):
            self.fb.emit(Op.CONST, c=self.pools.const(-n.operand.value))
            return
        self.expr(n.operand)
        self.fb.emit({ast.USub: Op.NEG, ast.UAdd: Op.POS, ast.Not: Op.NOT}[type(n.op)])

    def e_BoolOp(self, n):
        fb = self.fb
        end = fb.label()
        op = Op.JFK if isinstance(n.op, ast.And) else Op.JTK
        for i, v in enumerate(n.values):
            self.expr(v)
            if i < len(n.values) - 1:
                fb.jump(op, end)
        fb.mark(end)

    def e_Compare(self, n):
        fb = self.fb
        for op in n.ops:
            if isinstance(op, (ast.Is, ast.IsNot)):
                self.warn(n, "'is' se trata como '=='", "usa == o != (is solo tiene sentido con None)", "W_IS")
        self.expr(n.left)
        if len(n.ops) == 1:
            self.expr(n.comparators[0])
            fb.emit(CMPOPS[type(n.ops[0])])
            return
        end, t = fb.label(), fb.temp()
        for i, (op, right) in enumerate(zip(n.ops, n.comparators)):
            self.expr(right)
            if i < len(n.ops) - 1:
                fb.emit(Op.STOREL, b=t)
                fb.emit(Op.LOADL, b=t)
                fb.emit(CMPOPS[type(op)])
                fb.jump(Op.JFK, end)
                fb.emit(Op.LOADL, b=t)
            else:
                fb.emit(CMPOPS[type(op)])
        fb.mark(end)

    def e_IfExp(self, n):
        fb = self.fb
        orelse, end = fb.label(), fb.label()
        self.expr(n.test)
        fb.jump(Op.JF, orelse)
        self.expr(n.body)
        fb.jump(Op.JMP, end)
        fb.mark(orelse)
        self.expr(n.orelse)
        fb.mark(end)

    def e_List(self, n):
        for e in n.elts:
            self.expr(e)
        self.fb.emit(Op.LIST, b=len(n.elts))

    e_Tuple = e_List

    def e_JoinedStr(self, n):
        parts = 0
        for v in n.values:
            if isinstance(v, ast.Constant):
                if v.value:
                    self.fb.emit(Op.CONST, c=self.pools.const(v.value))
                    parts += 1
            else:
                self.expr(v.value)
                spec = ""
                if v.format_spec is not None:
                    spec = "".join(x.value for x in v.format_spec.values if isinstance(x, ast.Constant))
                if v.conversion == ord("r"):
                    spec = "!r" + spec
                self.fb.emit(Op.FORMAT, c=self.pools.const(spec))
                parts += 1
        if parts == 0:
            self.fb.emit(Op.CONST, c=self.pools.const(""))
        elif parts > 1:
            self.fb.emit(Op.CONCAT, b=parts)

    def e_Subscript(self, n):
        if isinstance(n.slice, ast.Slice):
            self.expr(n.value)
            for part in (n.slice.lower, n.slice.upper):
                if part is None:
                    self.fb.emit(Op.NONE)
                else:
                    self.expr(part)
            self.fb.emit(Op.CALLB, a=3, b=self.pools.builtin("slice"), c=-1)
            return
        self.expr(n.value)
        self.expr(n.slice)
        self.fb.emit(Op.GETI)

    def e_Attribute(self, n):
        v = n.value
        if isinstance(v, ast.Name) and v.id in self.const_groups and not self.shadowed(v.id):
            key = f"{v.id}.{n.attr}"
            if key not in self.reg.constants:
                self.fail(n, f"{v.id}.{n.attr} no existe", self.near(n.attr, self.const_groups[v.id]) or
                          f"hay: {', '.join(self.const_groups[v.id])}", "E_NAME")
            self.push_const(self.reg.constants[key])
            return
        if isinstance(v, ast.Name) and v.id in self.reg.namespaces and not self.shadowed(v.id):
            self.fail(n, f"{v.id}.{n.attr} es una funcion: escribe {v.id}.{n.attr}(...)", code="E_NAME")
        self.check_prop(n, write=False)
        self.expr(v)
        self.fb.emit(Op.GETA, b=self.pools.name(n.attr))

    def check_prop(self, n: ast.Attribute, write: bool) -> None:
        props = self.reg.props_named(n.attr)
        if not props:
            if self.reg.methods_named(n.attr):
                self.fail(n, f"'{n.attr}' es un metodo: escribe .{n.attr}(...)", code="E_PROP")
            allp = sorted({p for (_, p) in self.reg.props})
            self.fail(n, f"no existe la propiedad '{n.attr}'", self.near(n.attr, allp), "E_PROP")
        if write and not any(p.writable for p in props):
            self.fail(n, f"la propiedad '{n.attr}' solo se puede leer", code="E_PROP")

    def shadowed(self, name: str) -> bool:
        fb = self.fb
        return name in self.globals or name in self.funcs or (not self.in_init and name in fb.locals)

    # ================================================================== llamadas
    def e_Call(self, n: ast.Call):
        f, fb = n.func, self.fb
        if isinstance(f, ast.Name):
            name = f.id
            if not self.in_init and name in fb.locals and name not in self.declared_global or name in self.globals:
                self.load_name(f)
                return self.call_value(n)
            if name in self.funcs:
                return self.call_user(n, self.funcs[name])
            if name == "wait" and name in self.reg.functions:
                return self.call_wait(n)
            if name in self.reg.functions:
                self.check_literal_names(name, n)
                return self.call_builtin(n, self.reg.functions[name], name)
            if name in self.reg.events:
                self.fail(n, f"@{name} es un evento: va encima de una funcion (@{name}(...))", code="E_NAME")
            opts = list(self.funcs) + list(self.reg.functions)
            self.fail(f, f"la funcion '{name}' no existe", self.near(name, opts), "E_NAME")
        if isinstance(f, ast.Attribute):
            v = f.value
            if isinstance(v, ast.Name) and v.id in self.reg.namespaces and not self.shadowed(v.id):
                q = f"{v.id}.{f.attr}"
                if q not in self.reg.functions:
                    opts = [k.split(".", 1)[1] for k in self.reg.functions if k.startswith(v.id + ".")]
                    self.fail(f, f"{q}() no existe", self.near(f.attr, opts) or f"hay: {', '.join(opts)}", "E_NAME")
                return self.call_builtin(n, self.reg.functions[q], q)
            return self.call_method(n, f)
        self.expr(f)
        self.call_value(n)

    def args_plain(self, n: ast.Call, who: str) -> None:
        if n.keywords:
            self.fail(n, f"{who}: aqui los argumentos no pueden llevar nombre", code="E_ARGS")
        for a in n.args:
            self.expr(a)

    def call_value(self, n: ast.Call) -> None:
        self.args_plain(n, "llamar a una funcion guardada en una variable")
        self.fb.emit(Op.CALLV, a=len(n.args))

    def call_wait(self, n: ast.Call) -> None:
        if n.keywords and (len(n.keywords) > 1 or n.keywords[0].arg != "seconds") or len(n.args) > 1:
            self.fail(n, "wait() recibe solo los segundos: wait(0.5)", code="E_ARGS")
        arg = n.args[0] if n.args else (n.keywords[0].value if n.keywords else None)
        if arg is None:
            self.fb.emit(Op.CONST, c=self.pools.const(0.0))
        else:
            self.expr(arg)
        self.fb.emit(Op.WAIT)
        self.fb.emit(Op.NONE)

    def call_user(self, n: ast.Call, f: _UserFunc) -> None:
        params, defaults = f.params, f.defaults
        first_default = len(params) - len(defaults)
        if len(n.args) > len(params):
            self.fail(n, f"{f.node.name}() recibe como mucho {len(params)} argumento(s) y le das {len(n.args)}",
                      code="E_ARGS")
        slots: dict[str, ast.expr] = {params[i]: a for i, a in enumerate(n.args)}
        for k in n.keywords:
            if k.arg not in params:
                self.fail(k.value, f"{f.node.name}() no tiene un parametro '{k.arg}'", self.near(k.arg, params),
                          "E_ARGS")
            if k.arg in slots:
                self.fail(k.value, f"'{k.arg}' esta dos veces", code="E_ARGS")
            slots[k.arg] = k.value
        for i, p in enumerate(params):
            if p in slots:
                self.expr(slots[p])
            elif i >= first_default:
                self.fb.emit(Op.CONST, c=self.pools.const(defaults[i - first_default])) \
                    if defaults[i - first_default] is not None else self.fb.emit(Op.NONE)
            else:
                self.fail(n, f"{f.node.name}(): falta el argumento '{p}'", code="E_ARGS")
        self.fb.emit(Op.CALL, a=len(params), b=f.index)

    def call_builtin(self, n: ast.Call, spec: Builtin, qname: str) -> None:
        """argumentos ya ordenados segun la firma: fijos (con sus valores por defecto), extra de *args y al final
        los extra con nombre de **props (sus nombres en una constante)"""
        argc, kwnames = self.bind(spec, n, spec.fixed, qname)
        self.fb.emit(Op.CALLB, a=argc, b=self.pools.builtin(qname), c=kwnames)

    def bind(self, spec: Builtin, n: ast.Call, fixed, who: str) -> tuple[int, int]:
        normal = [p for p in fixed if p.kind == "normal"]
        slots: dict[str, ast.expr] = {}
        extra: list[ast.expr] = []
        for i, a in enumerate(n.args):
            if i < len(normal):
                slots[normal[i].name] = a
            elif spec.varargs:
                extra.append(a)
            else:
                self.fail(n, f"{who}() recibe como mucho {len(normal)} argumento(s) sin nombre y le das {len(n.args)}",
                          f"firma: {spec.signature()}", "E_ARGS")
        kwextra: dict[str, ast.expr] = {}
        names = {p.name for p in fixed}
        for k in n.keywords:
            if k.arg in slots:
                self.fail(k.value, f"{who}(): '{k.arg}' esta dos veces", code="E_ARGS")
            if k.arg in names:
                slots[k.arg] = k.value
            elif spec.varkw:
                kwextra[k.arg] = k.value
            else:
                self.fail(k.value, f"{who}() no tiene un argumento '{k.arg}'",
                          self.near(k.arg, names) or f"firma: {spec.signature()}", "E_ARGS")
        for p in fixed:
            if p.name in slots:
                self.expr(slots[p.name])
            elif p.default is REQUIRED:
                self.fail(n, f"{who}(): falta el argumento '{p.name}'", f"firma: {spec.signature()}", "E_ARGS")
            elif p.default is None:
                self.fb.emit(Op.NONE)
            else:
                self.fb.emit(Op.CONST, c=self.pools.const(p.default))
        for a in extra:
            self.expr(a)
        for a in kwextra.values():
            self.expr(a)
        kw = self.pools.const(tuple(kwextra)) if kwextra else -1
        return len(fixed) + len(extra) + len(kwextra), kw

    def call_method(self, n: ast.Call, f: ast.Attribute) -> None:
        cands = self.reg.methods_named(f.attr)
        if not cands:
            if self.reg.props_named(f.attr):
                self.fail(f, f"'{f.attr}' es una propiedad, no un metodo: sin parentesis", code="E_METHOD")
            allm = sorted({m for (_, m) in self.reg.methods})
            self.fail(f, f"el metodo '{f.attr}' no existe", self.near(f.attr, allm), "E_METHOD")
        # la comprobacion se hace con la primera firma que encaje; el orden real lo pone la maquina al ejecutar
        problems = []
        for c in cands:
            if self.fits(c, n):
                break
            problems.append(c.signature())
        else:
            self.fail(n, f".{f.attr}(): esos argumentos no encajan", "firmas: " + " | ".join(problems), "E_ARGS")
        self.expr(f.value)
        for a in n.args:
            self.expr(a)
        for k in n.keywords:
            self.expr(k.value)
        kw = self.pools.const(tuple(k.arg for k in n.keywords)) if n.keywords else -1
        self.fb.emit(Op.CALLM, a=len(n.args) + len(n.keywords), b=self.pools.name(f.attr), c=kw)

    @staticmethod
    def fits(spec: Builtin, n: ast.Call) -> bool:
        fixed = spec.fixed[1:]                      # sin self
        normal = [p for p in fixed if p.kind == "normal"]
        if len(n.args) > len(normal) and not spec.varargs:
            return False
        given = {p.name for p in normal[:len(n.args)]}
        names = {p.name for p in fixed}
        for k in n.keywords:
            if k.arg in given or (k.arg not in names and not spec.varkw):
                return False
            given.add(k.arg)
        return all(p.name in given for p in fixed if p.default is REQUIRED)

    def check_literal_names(self, fname: str, n: ast.Call) -> None:
        if not n.args or not isinstance(n.args[0], ast.Constant) or not isinstance(n.args[0].value, str):
            return
        val = n.args[0].value
        if fname in ("layer", "focus", "exists"):
            self.check_name(val, self.req.layers, "capa", n.args[0])
        elif fname == "goto":
            self.check_name(val, self.req.screens, "pantalla", n.args[0])


# ---------------------------------------------------------------------- ayudantes
def _assigned(st: ast.stmt) -> set[str]:
    """nombres a los que se asigna en una sentencia (sin entrar en funciones)"""
    out: set[str] = set()

    def target(t):
        if isinstance(t, ast.Name):
            out.add(t.id)
        elif isinstance(t, (ast.Tuple, ast.List)):
            for e in t.elts:
                target(e)

    for node in ast.walk(st):
        if isinstance(node, ast.FunctionDef):
            continue
        if isinstance(node, ast.Assign):
            for t in node.targets:
                target(t)
        elif isinstance(node, (ast.AugAssign, ast.AnnAssign)):
            target(node.target)
        elif isinstance(node, ast.For):
            target(node.target)
    return out


def _waits(body: list[ast.stmt], funcs: dict) -> bool:
    """si el bloque llama a algo que espera (wait, wait_press...) o a una funcion propia (que podria esperar)"""
    for st in body:
        for node in ast.walk(st):
            if isinstance(node, ast.Call):
                f = node.func
                name = f.id if isinstance(f, ast.Name) else f.attr if isinstance(f, ast.Attribute) else ""
                if name in WAITERS or name in funcs or name == "join" or \
                        any(k.arg == "wait" for k in node.keywords):
                    return True
            if isinstance(node, (ast.Break, ast.Return)):
                return True
    return False


def compile_program(req: CompileRequest, reg: Registry | None = None) -> CompileResult:
    parsed = parse_source(ParseRequest(req.source, req.filename))
    if parsed.tree is None:
        return CompileResult(None, parsed.diagnostics)
    checked = check_program(CheckRequest(parsed.tree))
    if not checked.ok:
        return CompileResult(None, checked.diagnostics)
    c = Compiler(req, reg or registry())
    prog = c.compile(parsed.tree)
    diags = sorted(checked.diagnostics + c.diags, key=lambda d: (d.line, d.col))
    return CompileResult(prog, diags)
