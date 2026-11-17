"""wui: un lenguaje para programar interfaces de juego en Python.

Se escribe Python normal (un subconjunto) con eventos, capas, mando, tiempo y el juego que hay debajo; wui lo
compila a instrucciones de una maquina pequena que corre igual en Python, en el navegador y dentro del juego.

    import wui
    res = wui.compile_source(open("menu.py").read(), layers=["rojo", "verde"], screens=["test"])
    if not res.ok:
        print("\\n".join(map(str, res.diagnostics)))
    vm = wui.Machine(res.program, wui.SimulatedHost(["rojo", "verde"], ["test"]))
    vm.start()
    vm.step(1 / 60, wui.Inputs.of("red"))

Arquitectura por casos de uso (cada carpeta hace una cosa, de principio a fin):
    language/   parse_source, check_program, compile_program      el lenguaje
    stdlib/     core, math, time, pad, layers, screens, events, sound, game   la libreria (spec + implementacion)
    runtime/    execute_program, read_input, animate_layers, host  la maquina de referencia
    tooling/    describe_api, generate_docs, disassemble           para editores y depuracion
    shared/     opcodes, program (.wuib), values, easing, errors   el nucleo comun
En programas de wui, `from wui import *` da los nombres del lenguaje (para el autocompletado del editor).
"""
from __future__ import annotations

from .language.compile_program.service import CompileRequest, CompileResult, compile_program
from .runtime.execute_program.machine import Machine
from .runtime.host.protocol import Host
from .runtime.host.simulated import SimLayer, SimulatedHost
from .runtime.read_input.pad_state import Inputs
from .shared.errors import CompileError, Diagnostic, ProgramFormatError, RuntimeFault, WuiError
from .shared.program import Program
from .stdlib.registry import registry
from .stubs import *  # noqa: F401,F403
from .stubs import __all__ as _lang_names
from .tooling.describe_api.service import describe_api
from .tooling.disassemble.service import disassemble
from .tooling.generate_docs.service import generate_docs

__version__ = "0.1.0"


def compile_source(source: str, filename: str = "<interfaz>", layers: list[str] | None = None,
                   screens: list[str] | None = None) -> CompileResult:
    """Compila el texto de un programa. Nunca lanza por errores del programa: van en result.diagnostics."""
    return compile_program(CompileRequest(source, filename, layers, screens))


def compile_or_raise(source: str, **kw) -> Program:
    res = compile_source(source, **kw)
    if not res.ok:
        raise CompileError(res.diagnostics)
    return res.program


def load_program(data: bytes) -> Program:
    return Program.from_bytes(data)


# `from wui import *` en un programa de wui trae el lenguaje, no las herramientas
__all__ = list(_lang_names)
