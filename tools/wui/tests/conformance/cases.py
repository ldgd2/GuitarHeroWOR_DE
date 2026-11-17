"""Programas de prueba de conformidad: las maquinas de Python, JS y C++ tienen que dar el mismo diario.

Cada caso: fuente, capas, pantallas, segundos simulados (a 60 fps) y guion de mando
[(segundo, "down"|"up"|"focus"|"click", valor)]."""

CASES = {}


def case(name, source, layers=("a", "b", "c"), screens=("uno", "dos"), seconds=1.0, script=(), seed=0):
    CASES[name] = {"source": source, "layers": list(layers), "screens": list(screens), "seconds": seconds,
                   "script": [list(s) for s in script], "seed": seed}


case("aritmetica", """
x = 7
y = 2.5
log(x + 1, x - 10, x * 3, x / 2, x // 2, -x // 2, x % 3, -x % 3, 2 ** 10, 2 ** -1, 7.0 // 2, -7.5 % 2)
log(y * 2, y + y, 1 / 3, 0.1 + 0.2, 1e20, 1e-7, 123456789.0 * 1000000, 1.5e16, abs(-3), abs(-2.5))
log(round(2.5), round(3.5), round(2.675, 2), round(7, 2), round(1234, -2), floor(-2.5), ceil(2.1))
log(min(3, 1, 2), max([4.0, 9, 2]), sum([1, 2, 3.5]), clamp(15, 0, 10), lerp(0, 10, 0.25), sign(-4), sqrt(16))
log(int("42"), int(3.9), int(-3.9), float("2.5"), str(3.0), bool(0), bool([1]), type(1), type(1.0), type("a"))
log(True + 1, 3 == 3.0, 1 < 2 < 3, 3 > 2 > 2, not 0, 0 or "z", 4 and 5, None or 0)
""")

case("textos", """
s = "Hola Mundo"
log(s.upper(), s.lower(), s.replace("o", "0"), s.split(), "a,b,,c".split(","), s.find("Mun"), s.find("x"))
log(s[0], s[-1], s[1:4], s[:2], s[5:], s[-5:-1], len(s), "Mu" in s, "  pad ".strip(), "7".pad(3), "x".pad(4, "-"))
log(s.startswith("Ho"), s.endswith("do"), s.contains("a M"), "ab" * 3)
n = 3
p = 2.0 / 3
log(f"n={n} p={p} {p:.2f} {n:03d} {n:>5}| {n:<5}| {n:^5}| {12345678:,} {p:.1%} {255:x} {1e6:g} {0.00001234:g}")
log(f"{'izq':<6}|{'der':>6}|{'cen':^7}|{-3:+d}|{3:+d}|{2.0}|{[1, 'a', 2.5]}|{None}|{True}")
log(format_number(1234567.891, 2), format_number(1500), time_text(75), time_text(3725), percent(1, 3), percent(2, 3, 1))
""")

case("listas", """
a = [5, 3, 8, 1]
a.append(7)
a.insert(1, 10)
log(a, len(a), a[2], a[-1], a[1:3], a[:])
a.sort()
log(a, a.index(8), a.index(99), a.count(3), a.contains(5), 5 in a, 6 not in a)
a.sort(reverse=True)
x = a.pop()
y = a.pop(0)
log(a, x, y, a.remove(5), a.remove(42), a)
b = a.copy()
b.reverse()
b[0] = "z"
log(a, b, [1, 2] + [3], [0] * 3, ["b", "a", "c"].join("-"))
nombres = ["pera", "Manzana", "uva"]
nombres.sort()
log(nombres, range(3), range(1, 10, 3), range(5, 0, -2), sum(range(101)))
m = [[1, 2], [3, 4]]
m[1][0] = 9
q, w = m[1]
log(m, q, w, FRETS, FRETS.index("blue"), Button.START)
""")

case("funciones", """
def fact(n):
    if n <= 1:
        return 1
    return n * fact(n - 1)

def saluda(nombre, signo="!"):
    return f"hola {nombre}{signo}"

def aplica(f, x):
    return f(x)

def doble(v):
    return v * 2

total = 0
for i in range(5):
    if i == 3:
        continue
    total += i
while total < 50:
    total *= 2
log(fact(10), saluda("ana"), saluda("bo", signo="?"), aplica(doble, 21), total)
""")

case("azar", """
log(randint(1, 100), randint(1, 6), choice(["a", "b", "c"]), random(), random(5, 10))
xs = [1, 2, 3, 4, 5, 6, 7, 8]
shuffle(xs)
log(xs, randint(-50, 50), randint(0, 1000000))
""", seed=1234)

case("errores", """
@on_start
def a():
    log("antes")
    x = [1, 2]
    log(x[5])

@on_start
def b():
    log("b sigue")
    log("t" + 3)

@on_start
def c():
    n = 0
    while True:
        n += 1

@on_start
def d():
    layer("noexiste").show()

@on_start
def e():
    log(1 / 0)
""")

case("tareas", """
orden = []

def lento(nombre, t):
    wait(t)
    orden.append(nombre)
    return nombre + "!"

@on_start
def inicio():
    t1 = spawn(lento, "a", 0.3)
    t2 = spawn(lento, "b", 0.1)
    after(0.2, lento, "c", 0)
    r = t1.join()
    log("join", r, orden, t2.running)
    wait_frames(3)
    log("frames", frame())

veces = 0

@every(0.25)
def tic():
    global veces
    veces += 1

@on_start
def cuenta():
    t = every_seconds(0.4, lento, "e", 0)
    wait(1.0)
    stop(t)
    log("veces", veces, orden.count("e"))
""", seconds=1.6)

case("mando", """
log_eventos = []

@on_press()
def pr(b, p):
    log("press", b, p)

@on_release(Button.RED)
def rl():
    log("release red", pad.held_time(Button.GREEN))

@on_hold(Button.START, seconds=0.5)
def hold():
    log("hold start", round(pad.held_time(Button.START), 1))

@on_repeat(Button.DOWN, every=0.1, delay=0.2)
def rep():
    log("repeat", frame())

@on_strum()
def strum(d):
    log("strum", d, pad.frets(), pad.chord(Button.GREEN, Button.RED))

@on_start
def espera():
    b = wait_press(Button.YELLOW, Button.BLUE, timeout=2.0)
    log("wait_press", b, pad.pressed(Button.YELLOW), pad.buttons())
    wait_release(Button.YELLOW)
    log("soltado", time() > 0)
""", seconds=2.0, script=[(0.1, "down", "green"), (0.15, "down", "red"), (0.2, "down", "strum_down"),
                          (0.25, "up", "strum_down"), (0.3, "up", "red"), (0.4, "down", "start"),
                          (1.0, "up", "start"), (1.0, "down", "down"), (1.45, "up", "down"),
                          (1.5, "down", "yellow"), (1.7, "up", "yellow")])

case("capas", """
@on_start
def f():
    a = layer("a")
    a.alpha = 0.5
    a.tint = "rojo"
    a.set(glow=4, outline_color=[0, 255, 0])
    log(a.x, a.alpha, a.tint, a.glow, a.outline_color, a.w, a.kind, a.name, a.visible, a.focused)
    a.animate(x=100, y=50, time=0.5, ease="out_back", wait=True)
    log("fin", round(a.x, 3), round(a.y, 3), a.animating)
    a.fade_out(0.2, wait=True)
    log("oculta", a.visible, a.alpha)
    b = layer("b")
    b.pulse()
    b.shake(5, 0.2)
    b.glow(20, "gold", time=0.1)
    b.tint("#00ff00", time=0.3)
    wait(0.4)
    log("b", round(b.glow, 2), b.tint, b.busy())
    b.stop()
    b.reset()
    log("reset", b.x, b.scale, b.glow)
    c = layer("c")
    c.spin(90)
    c.bounce(30, 0.3)
    wait(0.5)
    log("c", round(c.rot, 1), c.y)
    log(len(layers()), exists("a"), exists("zz"))
""", seconds=2.0)

case("pantallas", """
visitas = 0

@on_enter("uno")
def en_uno():
    global visitas
    visitas += 1
    log("entra uno", visitas, screen(), history())
    if visitas == 1:
        goto("dos")

@on_enter("dos")
def en_dos():
    log("entra dos", history())
    wait(0.2)
    back()

@on_exit()
def sale(nombre):
    log("sale", nombre)

@on_close
def cerrar():
    log("cerrada")

@on_start
def fin():
    wait(0.5)
    close()
""")

case("foco", """
@on_focus()
def fo(l):
    log("foco", l.name, focused().name)

@on_blur("a")
def bl():
    log("pierde a")

@on_click("b")
def cl(l):
    log("click", l.name)
    emit("hecho", 42)

@on_message("hecho")
def msg(v):
    log("mensaje", v)

@on_start
def f():
    focus("a")
    wait(0.1)
    layer("b").focus()
""", script=[(0.5, "down", "choose"), (0.6, "up", "choose"), (0.7, "click", "c")])

case("cambios", """
vidas = 3
nivel = "facil"

@on_change("vidas")
def cv(v, antes):
    log("vidas", antes, "->", v)

@on_change("nivel")
def cn(v):
    log("nivel", v)

@on_press(Button.RED)
def quita():
    global vidas, nivel
    vidas -= 1
    vidas = vidas
    nivel = "dificil" if vidas < 2 else nivel

@on_tick
def tick(dt):
    global contador
    contador = frame()

contador = 0
""", script=[(0.1, "down", "red"), (0.2, "up", "red"), (0.3, "down", "red"), (0.4, "up", "red")], seconds=0.5)

case("juego", """
@on_start
def f():
    game.set("$puntos", 10)
    log(game.get("puntos"), game.get("nada", 7), game.player("score"), game.in_song(), game.language())
    game.call("ui_sfx", tipo="select")
    sound(Sound.SELECT)
    sound("raro")
    pad.vibrate(0.8, 0.1)
""")
