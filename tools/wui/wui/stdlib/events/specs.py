"""Eventos: los decoradores que conectan funciones con lo que pasa (botones, pantallas, foco, tiempo,
variables y mensajes) y emit() para mandar mensajes propios."""
from __future__ import annotations

from ...shared.values import to_text

CAT = "eventos"


def emit(ctx, a, kw):
    ctx.vm.emit(to_text(a[0]), a[1])


def register(r) -> None:
    r.categories[CAT] = "Eventos: botones, pantallas, foco, tiempo, variables y mensajes"
    ev = r.event
    ev("on_start", "", "", "Al abrirse la interfaz.", "@on_start\ndef empezar():\n    sound(Sound.SELECT)")
    ev("on_close", "", "", "Al cerrarse la interfaz.")
    ev("on_enter", "screen=None", "screen", "Al entrar en una pantalla (o en cualquiera si no se da).",
       '@on_enter("opciones")\ndef abrir_opciones():\n    focus("volumen")')
    ev("on_exit", "screen=None", "screen", "Al salir de una pantalla (o de cualquiera).")
    ev("on_press", "button=None, player=1", "button, player",
       "Al pulsar un boton (o cualquiera si no se da). Varios decoradores pueden ir en la misma funcion.",
       "@on_press(Button.RED)\ndef rojo():\n    layer(\"rojo\").glow(24, \"red\")")
    ev("on_release", "button=None, player=1", "button, player", "Al soltar un boton.")
    ev("on_hold", "button, seconds=1.0, player=1", "button, player",
       "Cuando un boton lleva pulsado cierto tiempo (una vez por pulsacion).",
       "@on_hold(Button.START, seconds=1.5)\ndef salir():\n    close()")
    ev("on_repeat", "button, every=0.12, delay=0.35, player=1", "button, player",
       "Al pulsar y luego cada cierto tiempo mientras siga pulsado (como mantener una flecha).")
    ev("on_strum", "player=1", "direction, player", "Al rasguear (direction: -1 arriba, 1 abajo).")
    ev("on_click", "target=None", "layer", "Al elegir un boton (Intro / verde o clic del raton).",
       '@on_click("jugar")\ndef jugar():\n    goto("canciones")')
    ev("on_focus", "target=None", "layer", "Cuando una capa recibe el foco.")
    ev("on_blur", "target=None", "layer", "Cuando una capa pierde el foco.")
    ev("on_hover", "target=None", "layer", "Cuando el raton entra en una capa.")
    ev("on_tick", "", "dt", "Cada fotograma (recibe los segundos del fotograma).",
       "@on_tick\ndef cada_fotograma(dt):\n    layer(\"aguja\").rot += 90 * dt")
    ev("every", "seconds", "", "Cada cierto tiempo.", "@every(1.0)\ndef cada_segundo():\n    log(time())")
    ev("on_change", "var", "value, old", "Cuando cambia una variable global de la interfaz.",
       '@on_change("score")\ndef puntos(nuevo, viejo):\n    layer("puntos").text = str(nuevo)')
    ev("on_message", "name", "value", "Cuando alguien hace emit() con ese nombre.",
       '@on_message("acierto")\ndef acierto(valor):\n    layer("nota").flash()')
    ev("on_game", "var", "value, old", "Cuando cambia una variable global del juego (se mira cada fotograma).",
       '@on_game("current_num_players")\ndef jugadores(n):\n    log(n)')
    r.fn("emit", "name, value=None", "Manda un mensaje: lo reciben las funciones con @on_message(name).", emit,
         cat=CAT, example='emit("acierto", 100)')
