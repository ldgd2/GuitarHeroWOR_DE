"""Pantallas de la interfaz, foco de los botones y raton."""
from __future__ import annotations

from . import impl as I

CAT = "pantallas"


def register(r) -> None:
    r.categories[CAT] = "Pantallas, foco de los botones y raton"
    for t in ("AUTO", "NONE", "FADE", "SLIDE_LEFT", "SLIDE_RIGHT", "SLIDE_UP", "SLIDE_DOWN", "ZOOM"):
        r.const(f"Transition.{t}", t.lower())
    f = lambda *a, **k: r.fn(*a, cat=CAT, **k)  # noqa: E731
    f("goto", "screen, transition='auto'",
      "Va a otra pantalla (salida de esta, entrada de la otra). transition: auto usa las animaciones del diseno.",
      I.goto, example='goto("opciones")')
    f("back", "transition='auto'", "Vuelve a la pantalla anterior (en la primera, cierra la interfaz).", I.back)
    f("close", "", "Cierra la interfaz (con su animacion de salida).", I.close)
    f("screen", "", "Nombre de la pantalla actual.", I.screen, ret="texto")
    f("screens", "", "Nombres de todas las pantallas.", I.screens, ret="lista")
    f("history", "", "Pantallas por las que se ha pasado (para back()).", I.history, ret="lista")
    f("focus", "target", "Da el foco a una capa (su nombre o la capa).", I.focus, example='focus("jugar")')
    f("focused", "", "La capa con el foco (o nada).", I.focused, ret="capa")
    f("focus_move", "direction", "Mueve el foco como las flechas: up, down, left, right.", I.focus_move)
    f("lock_focus", "locked=True", "Bloquea el foco: las flechas y elegir no hacen nada (para modos de juego "
      "dentro del menu). Los eventos de botones siguen llegando.", I.lock_focus,
      example="@on_enter(\"modo_test\")\ndef entrar():\n    lock_focus(True)")
    f("mouse_x", "", "Posicion horizontal del raton (en la pantalla de 1280 x 720).", I.mouse_x, ret="decimal")
    f("mouse_y", "", "Posicion vertical del raton.", I.mouse_y, ret="decimal")
    f("mouse_down", "", "Si el boton izquierdo del raton esta pulsado.", I.mouse_down, ret="bool")
    f("hovered", "", "La capa bajo el raton (o nada).", I.hovered, ret="capa")
