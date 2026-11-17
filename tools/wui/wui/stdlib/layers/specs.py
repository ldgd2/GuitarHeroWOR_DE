"""Capas de la interfaz: propiedades, efectos (resplandor, desenfoque, sombra, contorno, tinte) y animaciones."""
from __future__ import annotations


CAT = "capas"

# nombre, tipo, documentacion, animable, valor de diseno por defecto
PROPS = [
    ("x", "numero", "Posicion horizontal (pantalla de 1280 x 720).", True, 0.0),
    ("y", "numero", "Posicion vertical.", True, 0.0),
    ("z", "numero", "Profundidad (2.5D y 3D).", True, 0.0),
    ("scale", "numero", "Escala (1 = tamano de diseno).", True, 1.0),
    ("sx", "numero", "Escala horizontal (se multiplica por scale).", True, 1.0),
    ("sy", "numero", "Escala vertical.", True, 1.0),
    ("rot", "numero", "Giro en grados.", True, 0.0),
    ("alpha", "numero", "Opacidad, de 0 (invisible) a 1.", True, 1.0),
    ("visible", "bool", "Si se dibuja.", False, True),
    ("tint", "color", "Color que multiplica la capa (#ffffff = sin tinte).", True, "#ffffff"),
    ("brightness", "numero", "Brillo, de 0 (negro) a 2 (el doble); 1 = normal.", True, 1.0),
    ("glow", "numero", "Resplandor exterior: tamano en pixeles (0 = sin resplandor).", True, 0.0),
    ("glow_color", "color", "Color del resplandor.", True, "#ffffff"),
    ("glow_alpha", "numero", "Intensidad del resplandor, de 0 a 1.", True, 1.0),
    ("blur", "numero", "Desenfoque en pixeles (0 = nitida).", True, 0.0),
    ("shadow", "numero", "Sombra: desenfoque en pixeles (0 = sin sombra).", True, 0.0),
    ("shadow_color", "color", "Color de la sombra.", True, "#000000"),
    ("shadow_alpha", "numero", "Opacidad de la sombra.", True, 0.6),
    ("shadow_x", "numero", "Desplazamiento horizontal de la sombra.", True, 4.0),
    ("shadow_y", "numero", "Desplazamiento vertical de la sombra.", True, 4.0),
    ("outline", "numero", "Contorno: grosor en pixeles (0 = sin contorno).", True, 0.0),
    ("outline_color", "color", "Color del contorno.", True, "#ffffff"),
    ("fill", "numero", "Cuanto se ve, de 0 a 1 (barras de progreso, medidores).", True, 1.0),
    ("fill_dir", "texto", "Desde donde se llena: right, left, up, down, center.", False, "right"),
    ("text", "texto", "Texto (solo capas de texto). Cambiarlo lo redibuja en el juego.", False, ""),
    ("image", "texto", "Imagen que muestra (nombre de un recurso del proyecto o del juego).", False, ""),
    ("order", "numero", "Orden de dibujo (mayor = encima).", False, 0),
    ("focusable", "bool", "Si puede recibir el foco (si es un boton).", False, False),
]
READONLY = [
    ("name", "texto", "Nombre de la capa."),
    ("w", "numero", "Ancho de diseno."),
    ("h", "numero", "Alto de diseno."),
    ("kind", "texto", "Tipo: image, text, shape, group."),
    ("focused", "bool", "Si tiene el foco."),
    ("animating", "bool", "Si tiene alguna animacion en marcha."),
]
DEFAULTS = {p[0]: p[4] for p in PROPS}
ANIMATABLE = {p[0] for p in PROPS if p[3]}
COLORS = {p[0] for p in PROPS if p[1] == "color"}
EASES_DOC = "linear, in, out, inout, in_cubic, out_cubic, inout_cubic, in_back, out_back, out_elastic, out_bounce, " \
            "in_bounce, step"


from . import impl as I  # noqa: E402  (despues de las constantes: impl las usa)

def register(r) -> None:
    r.categories[CAT] = "Capas: posicion, efectos (resplandor, desenfoque, sombra, contorno, tinte) y animaciones"
    for name, typ, doc, anim, dflt in PROPS:
        r.prop("Layer", name, typ, doc, animatable=anim, default=dflt)
    for name, typ, doc in READONLY:
        r.prop("Layer", name, typ, doc, writable=False)
    for e in ("LINEAR", "IN", "OUT", "INOUT", "IN_CUBIC", "OUT_CUBIC", "INOUT_CUBIC", "IN_BACK", "OUT_BACK",
              "OUT_ELASTIC", "OUT_BOUNCE", "IN_BOUNCE", "STEP"):
        r.const(f"Ease.{e}", e.lower())
    for d in ("RIGHT", "LEFT", "UP", "DOWN", "CENTER"):
        r.const(f"Fill.{d}", d.lower())

    f = lambda *a, **k: r.fn(*a, cat=CAT, **k)  # noqa: E731
    f("layer", "name", "La capa con ese nombre (el del panel Capas).", I.layer, ret="capa",
      example='boton = layer("jugar")\nboton.glow(20, "gold")')
    f("layers", "prefix=''", "Todas las capas (o las que empiezan por un prefijo).", I.layers, ret="lista",
      example='for t in layers("traste_"):\n    t.alpha = 0.4')
    f("exists", "name", "Si existe una capa con ese nombre.", I.exists, ret="bool")

    m = lambda name, sig, doc, impl, **k: r.fn(name, sig, doc, impl, cat=CAT, receiver="Layer", **k)  # noqa: E731
    m("show", "self, time=0.0", "La muestra (con fundido si time > 0).", I.show)
    m("hide", "self, time=0.0", "La oculta (con fundido si time > 0).", I.hide)
    m("toggle", "self", "La muestra si estaba oculta y al reves.", I.toggle)
    m("set", "self, **props", "Cambia varias propiedades a la vez.", I.set_,
      example='layer("panel").set(alpha=0.8, tint="#202040", blur=4)')
    m("get", "self, prop", "Lee una propiedad por su nombre.", I.get, ret="cualquiera")
    m("reset", "self, time=0.0", "Vuelve a como esta en el diseno (posicion, escala, efectos...).", I.reset)
    m("animate", "self, time=0.3, ease='out', delay=0.0, loop=0, yoyo=False, wait=False, **props",
      "Anima propiedades hasta esos valores. loop: repeticiones extra (-1 = siempre); yoyo: ida y vuelta; "
      f"wait: esperar a que termine. Curvas: {EASES_DOC}.", I.animate, blocking=True,
      example='layer("logo").animate(scale=1.2, alpha=1, time=0.4, ease=Ease.OUT_BACK)')
    m("stop", "self", "Para sus animaciones (se queda donde este).", I.stop)
    m("fade_in", "self, time=0.3, wait=False", "Aparece con fundido.", I.fade_in, blocking=True)
    m("fade_out", "self, time=0.3, wait=False", "Desaparece con fundido (y queda oculta).", I.fade_out, blocking=True)
    m("move_to", "self, x, y, time=0.3, ease='out', wait=False", "Se mueve a una posicion.", I.move_to,
      blocking=True)
    m("move_by", "self, dx, dy, time=0.3, ease='out', wait=False", "Se mueve una distancia.", I.move_by,
      blocking=True)
    m("scale_to", "self, scale, time=0.2, ease='out', wait=False", "Cambia de escala.", I.scale_to, blocking=True)
    m("rotate_to", "self, degrees, time=0.3, ease='out', wait=False", "Gira hasta un angulo.", I.rotate_to,
      blocking=True)
    m("glow", "self, size=16.0, color='#ffffff', alpha=1.0, time=0.0", "Resplandor exterior.", I.glow,
      example='layer("rojo").glow(24, "red", time=0.1)')
    m("unglow", "self, time=0.0", "Quita el resplandor.", I.unglow)
    m("blur", "self, px=6.0, time=0.0", "Desenfoque (0 = nitida).", I.blur)
    m("shadow", "self, blur=8.0, color='#000000', x=4.0, y=4.0, alpha=0.6, time=0.0", "Sombra paralela.",
      I.shadow)
    m("outline", "self, width=2.0, color='#ffffff', time=0.0", "Contorno alrededor.", I.outline)
    m("tint", "self, color, time=0.0", "Tine la capa de un color.", I.tint, example='layer("nota").tint("gold")')
    m("untint", "self, time=0.0", "Quita el tinte.", I.untint)
    m("pulse", "self, prop='scale', amount=0.08, period=0.6, times=-1",
      "Late: sube y baja una propiedad (times = cuantas veces; -1 siempre).", I.pulse,
      example='layer("pulsa_start").pulse("alpha", -0.6, 1.2)')
    m("shake", "self, amount=8.0, time=0.3", "Tiembla (se queda donde estaba).", I.shake)
    m("flash", "self, color='#ffffff', time=0.2", "Destello de un color que se apaga.", I.flash)
    m("bounce", "self, height=20.0, time=0.4", "Da un salto y vuelve.", I.bounce)
    m("spin", "self, speed=180.0", "Gira sin parar (grados por segundo; 0 para).", I.spin)
    m("set_text", "self, text", "Cambia su texto (igual que .text = ...).", I.set_text)
    m("set_image", "self, name", "Cambia su imagen.", I.set_image)
    m("focus", "self", "Le da el foco (si es un boton).", I.focus)
    m("contains", "self, x, y", "Si el punto (x, y) cae dentro de la capa.", I.contains, ret="bool")
    m("busy", "self", "Si tiene alguna animacion en marcha.", I.busy, ret="bool")
