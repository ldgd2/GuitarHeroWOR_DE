"""Modo test: cada traste brilla mientras se pulsa; mantener START 1.5 s para salir.

Capas que espera la interfaz: green, red, yellow, blue, orange, contador, aviso_salir.
Probar:  python -m wui run examples/modo_test.py --layers green,red,yellow,blue,orange,contador,aviso_salir
"""
from wui import *

COLORES = ["#30e060", "#ff3040", "#ffe030", "#3080ff", "#ff9020"]
pulsaciones = 0


@on_start
def preparar():
    layer("aviso_salir").hide()
    layer("contador").text = "Pulsa un traste"


@on_press()
def al_pulsar(boton):
    global pulsaciones
    if boton in FRETS:
        i = FRETS.index(boton)
        layer(boton).glow(24, COLORES[i], time=0.08)
        layer(boton).scale_to(1.15, time=0.08)
        pulsaciones += 1
        layer("contador").text = f"Pulsaciones: {pulsaciones}"
    elif boton == Button.START:
        layer("aviso_salir").fade_in(0.2)


@on_release()
def al_soltar(boton):
    if boton in FRETS:
        layer(boton).unglow(time=0.25)
        layer(boton).scale_to(1.0, time=0.25)
    elif boton == Button.START:
        layer("aviso_salir").fade_out(0.2)


@on_hold(Button.START, seconds=1.5)
def salir():
    sound(Sound.BACK)
    back()
