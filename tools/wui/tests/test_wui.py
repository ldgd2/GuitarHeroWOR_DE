import unittest

import wui
from wui import Inputs, Machine, SimulatedHost
from wui.shared.program import Program

DT = 1 / 60


def build(src, layers=("a", "b"), screens=("uno", "dos")):
    res = wui.compile_source(src)
    if not res.ok:
        raise AssertionError("\n".join(map(str, res.diagnostics)))
    host = SimulatedHost(list(layers), list(screens))
    vm = Machine(res.program, host)
    vm.start()
    return vm, host


def logs(vm):
    return [m for _, m in vm.logs]


def run(vm, seconds, inputs=None):
    for _ in range(round(seconds / DT)):
        vm.step(DT, inputs)


class Lenguaje(unittest.TestCase):
    def test_aritmetica_y_textos(self):
        vm, _ = build('x = 7\nlog(x // 2, x % 3, 2 ** 3, -x, f"{x:03d}", "a" + "b", [1, 2] + [3])')
        self.assertEqual(logs(vm), ["3 1 8 -7 007 ab [1, 2, 3]"])

    def test_control(self):
        src = """
total = 0
for i in range(10):
    if i == 7:
        break
    if i % 2:
        continue
    total += i
n = 0
while n < 3:
    n += 1
log(total, n, 1 < n <= 3, n if n > 5 else "no", not n, n and "y" or "z")
"""
        vm, _ = build(src)
        self.assertEqual(logs(vm), ["12 3 True no False y"])

    def test_funciones_listas_y_cortes(self):
        src = """
def suma(a, b=10):
    return a + b

def doble(x):
    return x * 2

items = [3, 1, 2]
items.append(5)
items.sort()
f = doble
a, b = 1, 2
log(suma(1), suma(1, b=2), f(4), items, items[1:3], "hola"[-1], len(items), a + b)
"""
        vm, _ = build(src)
        self.assertEqual(logs(vm), ["11 3 8 [1, 2, 3, 5] [2, 3] a 4 3"])

    def test_constantes(self):
        vm, _ = build('log(Button.RED, "red" in FRETS, FRETS.index("blue"))')
        self.assertEqual(logs(vm), ["red True 3"])


class Errores(unittest.TestCase):
    def errores(self, src):
        res = wui.compile_source(src)
        self.assertFalse(res.ok)
        return " | ".join(d.message for d in res.errors)

    def test_nombre_mal_escrito(self):
        res = wui.compile_source("puntos = 1\nlog(punots)")
        self.assertIn("puntos", res.errors[0].hint)

    def test_cosas_no_soportadas(self):
        self.assertTrue(self.errores("class A:\n    pass"))
        self.assertTrue(self.errores("x = {1: 2}"))
        self.assertTrue(self.errores("import os"))

    def test_argumentos(self):
        self.assertIn("falta", self.errores("layer()"))
        self.assertIn("glow", self.errores('layer("a").glow(tamano=3)'))
        self.assertIn("evento", self.errores("@on_pres()\ndef f():\n    pass"))

    def test_avisos(self):
        res = wui.compile_source("while True:\n    log(1)")
        self.assertTrue(res.ok)
        self.assertTrue(any(d.code == "W_BUSY_LOOP" for d in res.diagnostics))
        res = wui.compile_source('goto("tres")', screens=["uno"])
        self.assertTrue(any(d.code == "W_NAME" for d in res.diagnostics))

    def test_error_en_ejecucion_no_tumba(self):
        src = """
@on_start
def malo():
    log(1 / 0)

@on_start
def bueno():
    log("sigo")
"""
        vm, _ = build(src)
        self.assertEqual(logs(vm)[-1], "sigo")
        self.assertIn("cero", vm.errors[0].message)
        self.assertEqual(vm.errors[0].line, 4)

    def test_bucle_sin_espera_se_corta(self):
        vm, _ = build("@on_start\ndef f():\n    while True:\n        pass")
        self.assertTrue(vm.errors)


class Tiempo(unittest.TestCase):
    def test_wait_y_tareas(self):
        src = """
@on_start
def a():
    log("a1")
    wait(0.5)
    log("a2")

def b():
    log("b")

@on_start
def c():
    after(0.2, b)
"""
        vm, _ = build(src)
        self.assertEqual(logs(vm), ["a1"])
        run(vm, 0.3)
        self.assertEqual(logs(vm), ["a1", "b"])
        run(vm, 0.3)
        self.assertEqual(logs(vm), ["a1", "b", "a2"])

    def test_every(self):
        vm, _ = build("n = 0\n@every(0.25)\ndef t():\n    global n\n    n += 1")
        run(vm, 1.0)
        self.assertEqual(vm.global_value("n"), 4)


class Mando(unittest.TestCase):
    SRC = """
@on_press(Button.RED)
def r():
    log("press")

@on_release(Button.RED)
def s():
    log("release")

@on_hold(Button.START, seconds=1.0)
def h():
    log("hold")

@on_start
def esperar():
    wait_press(Button.GREEN)
    log("verde")
"""

    def test_flancos(self):
        vm, _ = build(self.SRC)
        vm.step(DT, Inputs.of("red"))
        vm.step(DT, Inputs.of("red"))
        vm.step(DT)
        self.assertEqual(logs(vm), ["press", "release"])

    def test_mantener(self):
        vm, _ = build(self.SRC)
        run(vm, 0.9, Inputs.of("start"))
        self.assertNotIn("hold", logs(vm))
        run(vm, 0.2, Inputs.of("start"))
        run(vm, 1.0, Inputs.of("start"))
        self.assertEqual(logs(vm).count("hold"), 1)

    def test_wait_press(self):
        vm, _ = build(self.SRC)
        run(vm, 0.2)
        vm.step(DT, Inputs.of("green"))
        vm.step(DT)
        self.assertIn("verde", logs(vm))


class Capas(unittest.TestCase):
    def test_propiedades_y_animacion(self):
        src = """
@on_start
def f():
    a = layer("a")
    a.alpha = 0.5
    a.tint = "#ff0000"
    a.animate(x=100, time=0.5, ease="linear", wait=True)
    log("fin", a.x)
"""
        vm, host = build(src)
        self.assertEqual(host.prop("a", "alpha"), 0.5)
        self.assertEqual(host.prop("a", "tint"), "#ff0000")
        run(vm, 0.25)
        self.assertGreater(host.prop("a", "x"), 100)
        run(vm, 0.4)
        self.assertEqual(host.prop("a", "x"), 100)
        self.assertEqual(logs(vm), ["fin 100.0"])

    def test_capa_que_no_existe(self):
        vm, _ = build('@on_start\ndef f():\n    layer("zz").show()')
        self.assertIn("zz", vm.errors[0].message)


class Pantallas(unittest.TestCase):
    def test_goto_back_y_eventos(self):
        src = """
@on_enter("dos")
def en_dos():
    log("entra dos")
    back()

@on_exit("dos")
def sale():
    log("sale dos")

@on_start
def f():
    goto("dos")
"""
        vm, host = build(src)
        run(vm, 0.1)
        self.assertEqual(logs(vm), ["entra dos", "sale dos"])
        self.assertEqual(vm.screen, "uno")

    def test_on_change(self):
        src = """
vidas = 3
@on_change("vidas")
def cambia(v, antes):
    log(antes, "->", v)

@on_press(Button.RED)
def quita():
    global vidas
    vidas -= 1
"""
        vm, _ = build(src)
        vm.step(DT, Inputs.of("red"))
        vm.step(DT)
        self.assertEqual(logs(vm), ["3 -> 2"])


class Formato(unittest.TestCase):
    def test_binario_ida_y_vuelta(self):
        src = open(__file__.replace("tests\\test_wui.py", "examples\\modo_test.py")
                   .replace("tests/test_wui.py", "examples/modo_test.py"), encoding="utf-8").read()
        p = wui.compile_or_raise(src)
        q = Program.from_bytes(p.to_bytes())
        self.assertEqual(q.to_json(), Program.from_bytes(q.to_bytes()).to_json())
        self.assertEqual(len(q.functions), len(p.functions))
        self.assertTrue(wui.disassemble(q))

    def test_api(self):
        api = wui.describe_api()
        self.assertTrue(any(f["name"] == "layer" for f in api["functions"]))
        self.assertIn("on_press", [e["name"] for e in api["events"]])
        self.assertIn("# Referencia", wui.generate_docs())


if __name__ == "__main__":
    unittest.main()
