"""Tiempo, esperas y tareas en paralelo."""
from __future__ import annotations

from . import impl as I

CAT = "tiempo"


def register(r) -> None:
    r.categories[CAT] = "Reloj, esperas y tareas en paralelo"
    f = lambda *a, **k: r.fn(*a, cat=CAT, **k)  # noqa: E731
    f("time", "", "Segundos desde que se abrio la interfaz.", I.time, ret="decimal")
    f("dt", "", "Segundos que duro el ultimo fotograma.", I.dt, ret="decimal",
      example='layer("aguja").rot += 90 * dt()')
    f("frame", "", "Numero de fotograma.", I.frame, ret="entero")
    f("wait", "seconds=0.0", "Duerme esta tarea (0 = hasta el siguiente fotograma). Las demas siguen.", I.wait,
      blocking=True, example="layer(\"aviso\").show()\nwait(2)\nlayer(\"aviso\").hide()")
    f("wait_frames", "n=1", "Duerme esta tarea unos fotogramas.", I.wait_frames, blocking=True)
    f("wait_press", "*buttons, timeout=-1.0, player=1",
      "Espera a que se pulse uno de los botones (o cualquiera si no se dan). Devuelve cual, o nada si se acabo "
      "el tiempo.", I.wait_press, ret="texto", blocking=True,
      example="b = wait_press(Button.GREEN, Button.RED, timeout=5)")
    f("wait_release", "button, player=1", "Espera a que se suelte un boton.", I.wait_release, blocking=True)
    f("wait_until", "fn, timeout=-1.0", "Espera hasta que la funcion devuelva verdadero (se pregunta cada "
      "fotograma). Devuelve False si se acabo el tiempo.", I.wait_until, ret="bool", blocking=True,
      example="def listo():\n    return score > 1000\nwait_until(listo)")
    f("spawn", "fn, *args", "Lanza una funcion en paralelo (una tarea). Devuelve la tarea.", I.spawn, ret="tarea",
      example="def parpadeo():\n    while True:\n        layer(\"luz\").toggle()\n        wait(0.5)\n"
              "luz = spawn(parpadeo)")
    f("after", "seconds, fn, *args", "Lanza una funcion dentro de unos segundos. Devuelve la tarea.", I.after,
      ret="tarea", example="after(3, cerrar_aviso)")
    f("every_seconds", "seconds, fn, *args", "Llama a una funcion cada cierto tiempo (hasta parar la tarea).",
      I.every_seconds, ret="tarea")
    f("stop", "task", "Para una tarea.", I.stop)
    f("stop_all", "", "Para todas las tareas menos la actual.", I.stop_all)

    m = lambda name, sig, doc, impl, **k: r.fn(name, sig, doc, impl, cat=CAT, receiver="Task", **k)  # noqa: E731
    m("stop", "self", "Para la tarea.", I.t_stop)
    m("join", "self, timeout=-1.0", "Espera a que termine la tarea. Devuelve lo que devolvio.", I.t_join,
      ret="cualquiera", blocking=True)
    r.prop("Task", "running", "bool", "Si sigue en marcha.", writable=False)
    r.prop("Task", "result", "cualquiera", "Lo que devolvio la funcion (cuando termina).", writable=False)
