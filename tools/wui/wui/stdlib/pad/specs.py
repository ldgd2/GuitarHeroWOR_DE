"""El mando: guitarra (trastes, rasgueo, palanca, estrella) y mando normal (cruceta, start, atras)."""
from __future__ import annotations


CAT = "mando"

# nombre interno de cada boton (el anfitrion traduce del mando real: guitarra, Xbox, teclado...)
BUTTONS = {
    "GREEN": "green", "RED": "red", "YELLOW": "yellow", "BLUE": "blue", "ORANGE": "orange",
    "STRUM_UP": "strum_up", "STRUM_DOWN": "strum_down",
    "UP": "up", "DOWN": "down", "LEFT": "left", "RIGHT": "right",
    "START": "start", "BACK": "back", "STAR": "star",
    "CHOOSE": "choose", "CANCEL": "cancel",     # elegir / atras del menu (en la guitarra: verde / rojo)
}
FRETS = ["green", "red", "yellow", "blue", "orange"]


from . import impl as I  # noqa: E402  (despues de las constantes: impl las usa)

def register(r) -> None:
    r.categories[CAT] = "Botones, trastes, rasgueo, palanca y vibracion del mando"
    r.namespace("pad", "El mando (por defecto el del jugador 1)")
    for k, v in BUTTONS.items():
        r.const(f"Button.{k}", v)
    r.const("FRETS", tuple(FRETS))
    f = lambda name, *a, **k: r.fn("pad." + name, *a, cat=CAT, **k)  # noqa: E731
    f("pressed", "button, player=1", "Si el boton esta pulsado ahora.", I.pressed, ret="bool",
      example="if pad.pressed(Button.RED):\n    layer(\"rojo\").glow(20, \"red\")")
    f("just_pressed", "button, player=1", "Si se acaba de pulsar en este fotograma.", I.just_pressed, ret="bool")
    f("just_released", "button, player=1", "Si se acaba de soltar en este fotograma.", I.just_released, ret="bool")
    f("held_time", "button, player=1", "Segundos que lleva pulsado (0 si no lo esta).", I.held_time, ret="decimal",
      example="barra.fill = clamp(pad.held_time(Button.YELLOW) / 1.5, 0, 1)")
    f("any_pressed", "player=1", "Si hay algun boton pulsado.", I.any_pressed, ret="bool")
    f("buttons", "player=1", "Lista de los botones pulsados.", I.buttons, ret="lista")
    f("frets", "player=1", "Lista de los trastes pulsados (verde, rojo, amarillo, azul, naranja).", I.frets,
      ret="lista")
    f("chord", "*buttons, player=1", "Si estan pulsados exactamente esos trastes a la vez.", I.chord, ret="bool",
      example="if pad.chord(Button.GREEN, Button.RED):\n    log(\"acorde\")")
    f("strum", "player=1", "Rasgueo: -1 arriba, 1 abajo, 0 nada (solo el fotograma en que se rasguea).", I.strum,
      ret="entero")
    f("whammy", "player=1", "Palanca de tremolo, de 0 (suelta) a 1 (a fondo).", I.whammy, ret="decimal",
      example="layer(\"onda\").sy = 1 + pad.whammy() * 0.5")
    f("tilt", "player=1", "Inclinacion de la guitarra, de 0 a 1 (para activar el poder estelar).", I.tilt,
      ret="decimal")
    f("axis", "name, player=1", "Un eje del mando normal: lx, ly, rx, ry, lt, rt (de -1 a 1 o de 0 a 1).", I.axis,
      ret="decimal")
    f("connected", "player=1", "Si ese mando esta conectado.", I.connected, ret="bool")
    f("kind", "player=1", "Que mando es: guitar, drums, mic, pad o keyboard.", I.kind, ret="texto")
    f("vibrate", "strength=0.5, seconds=0.2, player=1", "Hace vibrar el mando (si puede).", I.vibrate)
