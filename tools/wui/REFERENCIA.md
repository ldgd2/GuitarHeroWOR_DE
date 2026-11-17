# Referencia de wui

Generada desde el registro de la libreria (`python -m wui docs`). No editar a mano.

## Eventos

Decoradores que conectan una funcion con algo que pasa. La funcion puede recibir (en orden) lo que da el evento, o menos.

| Evento | Da | Que hace |
|---|---|---|
| `@on_start()` | - | Al abrirse la interfaz. |
| `@on_close()` | - | Al cerrarse la interfaz. |
| `@on_enter(screen=None)` | screen | Al entrar en una pantalla (o en cualquiera si no se da). |
| `@on_exit(screen=None)` | screen | Al salir de una pantalla (o de cualquiera). |
| `@on_press(button=None, player=1)` | button, player | Al pulsar un boton (o cualquiera si no se da). Varios decoradores pueden ir en la misma funcion. |
| `@on_release(button=None, player=1)` | button, player | Al soltar un boton. |
| `@on_hold(button, seconds=1.0, player=1)` | button, player | Cuando un boton lleva pulsado cierto tiempo (una vez por pulsacion). |
| `@on_repeat(button, every=0.12, delay=0.35, player=1)` | button, player | Al pulsar y luego cada cierto tiempo mientras siga pulsado (como mantener una flecha). |
| `@on_strum(player=1)` | direction, player | Al rasguear (direction: -1 arriba, 1 abajo). |
| `@on_click(target=None)` | layer | Al elegir un boton (Intro / verde o clic del raton). |
| `@on_focus(target=None)` | layer | Cuando una capa recibe el foco. |
| `@on_blur(target=None)` | layer | Cuando una capa pierde el foco. |
| `@on_hover(target=None)` | layer | Cuando el raton entra en una capa. |
| `@on_tick()` | dt | Cada fotograma (recibe los segundos del fotograma). |
| `@every(seconds)` | - | Cada cierto tiempo. |
| `@on_change(var)` | value, old | Cuando cambia una variable global de la interfaz. |
| `@on_message(name)` | value | Cuando alguien hace emit() con ese nombre. |
| `@on_game(var)` | value, old | Cuando cambia una variable global del juego (se mira cada fotograma). |

## Constantes

- **Button**: `Button.GREEN`, `Button.RED`, `Button.YELLOW`, `Button.BLUE`, `Button.ORANGE`, `Button.STRUM_UP`, `Button.STRUM_DOWN`, `Button.UP`, `Button.DOWN`, `Button.LEFT`, `Button.RIGHT`, `Button.START`, `Button.BACK`, `Button.STAR`, `Button.CHOOSE`, `Button.CANCEL`
- **Ease**: `Ease.LINEAR`, `Ease.IN`, `Ease.OUT`, `Ease.INOUT`, `Ease.IN_CUBIC`, `Ease.OUT_CUBIC`, `Ease.INOUT_CUBIC`, `Ease.IN_BACK`, `Ease.OUT_BACK`, `Ease.OUT_ELASTIC`, `Ease.OUT_BOUNCE`, `Ease.IN_BOUNCE`, `Ease.STEP`
- **Fill**: `Fill.RIGHT`, `Fill.LEFT`, `Fill.UP`, `Fill.DOWN`, `Fill.CENTER`
- **Transition**: `Transition.AUTO`, `Transition.NONE`, `Transition.FADE`, `Transition.SLIDE_LEFT`, `Transition.SLIDE_RIGHT`, `Transition.SLIDE_UP`, `Transition.SLIDE_DOWN`, `Transition.ZOOM`
- **Sound**: `Sound.SELECT`, `Sound.BACK`, `Sound.SCROLL_UP`, `Sound.SCROLL_DOWN`, `Sound.SCROLL_LEFT`, `Sound.SCROLL_RIGHT`, `Sound.SELECT_ALT1`, `Sound.SELECT_ALT2`, `Sound.TOGGLE_ON`, `Sound.TOGGLE_OFF`, `Sound.NEGATIVE`, `Sound.SCROLL_END`, `Sound.WARNING`, `Sound.TOGGLE`, `Sound.SCROLL_UP_BIG`, `Sound.SCROLL_DOWN_BIG`
- `PI`, `TAU`, `INF`, `FRETS`

## Nucleo

Conversiones, listas, textos y mensajes de depuracion

- `int(value)` -> entero: Convierte a entero (quita los decimales).

  ```python
  int("42") + 1
  ```

- `float(value)` -> decimal: Convierte a decimal.
- `str(value)` -> texto: Convierte cualquier valor a texto.

  ```python
  "Puntos: " + str(score)
  ```

- `bool(value)` -> bool: Convierte a verdadero o falso.
- `len(value)` -> entero: Cantidad de elementos de una lista o de letras de un texto.
- `range(start, stop=None, step=1)` -> lista: Lista de numeros (para recorrer con for).

  ```python
  for i in range(5):
      log(i)
  ```

- `slice(items, start=None, stop=None)` -> cualquiera: Un trozo de una lista o de un texto (igual que items[start:stop]).
- `type(value)` -> texto: Nombre del tipo de un valor (entero, texto, capa...).
- `log(*values, sep=' ')`: Escribe en el registro de la vista previa (y del juego con el modo desarrollador).

  ```python
  log("puntos", score)
  ```

- `print(*values, sep=' ')`: Igual que log().
- `warn(*values)`: Escribe un aviso en el registro.
- `list.append(value)`: Anade al final.

  ```python
  items.append(5)
  ```

- `list.insert(index, value)`: Inserta en una posicion.
- `list.pop(index=-1)` -> cualquiera: Quita y devuelve un elemento (por defecto el ultimo).
- `list.remove(value)` -> bool: Quita la primera aparicion; devuelve si estaba.
- `list.clear()`: Vacia la lista.
- `list.index(value)` -> entero: Posicion de un valor (-1 si no esta).
- `list.count(value)` -> entero: Cuantas veces aparece un valor.
- `list.contains(value)` -> bool: Si el valor esta en la lista (igual que: value in lista).
- `list.reverse()`: Invierte el orden.
- `list.sort(reverse=False)`: Ordena de menor a mayor (o al reves).
- `list.copy()` -> lista: Una copia de la lista.
- `list.join(sep='')` -> texto: Une los elementos en un texto.

  ```python
  ", ".join(nombres)
  ```

- `str.upper()` -> texto: En mayusculas.
- `str.lower()` -> texto: En minusculas.
- `str.strip()` -> texto: Sin espacios al principio ni al final.
- `str.replace(old, new)` -> texto: Cambia un trozo por otro.
- `str.split(sep='')` -> lista: Parte el texto en una lista.
- `str.startswith(prefix)` -> bool: Si empieza por...
- `str.endswith(suffix)` -> bool: Si termina en...
- `str.find(part)` -> entero: Posicion de un trozo (-1 si no esta).
- `str.contains(part)` -> bool: Si contiene un trozo.
- `str.pad(width, fill='0')` -> texto: Rellena por la izquierda hasta un ancho ("7".pad(3) -> "007").

## Matematicas

Numeros, azar, interpolacion y formato

- `abs(x)` -> numero: Valor absoluto.
- `min(*values)` -> numero: El menor (de varios valores o de una lista).
- `max(*values)` -> numero: El mayor (de varios valores o de una lista).
- `sum(values)` -> numero: Suma de una lista.
- `round(x, digits=0)` -> numero: Redondea (a cierta cantidad de decimales).
- `floor(x)` -> entero: Redondea hacia abajo.
- `ceil(x)` -> entero: Redondea hacia arriba.
- `sqrt(x)` -> decimal: Raiz cuadrada.
- `sin(degrees)` -> decimal: Seno (en grados).

  ```python
  y = 360 + sin(time() * 180) * 20
  ```

- `cos(degrees)` -> decimal: Coseno (en grados).
- `atan2(y, x)` -> decimal: Angulo en grados de un vector.
- `clamp(x, lo, hi)` -> numero: Limita x entre lo y hi.

  ```python
  vida = clamp(vida - 10, 0, 100)
  ```

- `lerp(a, b, t)` -> decimal: Mezcla entre a y b (t de 0 a 1).
- `remap(x, a, b, c, d)` -> decimal: Pasa x del rango a..b al rango c..d.

  ```python
  ancho = remap(score, 0, 100000, 0, 400)
  ```

- `approach(x, target, step)` -> decimal: Acerca x a target sin pasarse (para movimientos suaves).
- `sign(x)` -> entero: -1, 0 o 1.
- `random(lo=0.0, hi=1.0)` -> decimal: Decimal al azar entre lo y hi.
- `randint(lo, hi)` -> entero: Entero al azar entre lo y hi (ambos incluidos).
- `choice(items)` -> cualquiera: Un elemento al azar de una lista.
- `shuffle(items)`: Desordena una lista.
- `format_number(x, decimals=0, sep=True)` -> texto: Numero con separador de miles (12345 -> 12,345).

  ```python
  layer("puntos").text = format_number(score)
  ```

- `time_text(seconds, hours=False)` -> texto: Segundos como reloj (75 -> 1:15).
- `percent(value, total, decimals=0)` -> texto: Porcentaje como texto (3, 4 -> 75%).

## Tiempo

Reloj, esperas y tareas en paralelo

- `time()` -> decimal: Segundos desde que se abrio la interfaz.
- `dt()` -> decimal: Segundos que duro el ultimo fotograma.

  ```python
  layer("aguja").rot += 90 * dt()
  ```

- `frame()` -> entero: Numero de fotograma.
- `wait(seconds=0.0)` *(puede esperar)*: Duerme esta tarea (0 = hasta el siguiente fotograma). Las demas siguen.

  ```python
  layer("aviso").show()
  wait(2)
  layer("aviso").hide()
  ```

- `wait_frames(n=1)` *(puede esperar)*: Duerme esta tarea unos fotogramas.
- `wait_press(*buttons, timeout=-1.0, player=1)` -> texto *(puede esperar)*: Espera a que se pulse uno de los botones (o cualquiera si no se dan). Devuelve cual, o nada si se acabo el tiempo.

  ```python
  b = wait_press(Button.GREEN, Button.RED, timeout=5)
  ```

- `wait_release(button, player=1)` *(puede esperar)*: Espera a que se suelte un boton.
- `wait_until(fn, timeout=-1.0)` -> bool *(puede esperar)*: Espera hasta que la funcion devuelva verdadero (se pregunta cada fotograma). Devuelve False si se acabo el tiempo.

  ```python
  def listo():
      return score > 1000
  wait_until(listo)
  ```

- `spawn(fn, *args)` -> tarea: Lanza una funcion en paralelo (una tarea). Devuelve la tarea.

  ```python
  def parpadeo():
      while True:
          layer("luz").toggle()
          wait(0.5)
  luz = spawn(parpadeo)
  ```

- `after(seconds, fn, *args)` -> tarea: Lanza una funcion dentro de unos segundos. Devuelve la tarea.

  ```python
  after(3, cerrar_aviso)
  ```

- `every_seconds(seconds, fn, *args)` -> tarea: Llama a una funcion cada cierto tiempo (hasta parar la tarea).
- `stop(task)`: Para una tarea.
- `stop_all()`: Para todas las tareas menos la actual.
- `Task.stop()`: Para la tarea.
- `Task.join(timeout=-1.0)` -> cualquiera *(puede esperar)*: Espera a que termine la tarea. Devuelve lo que devolvio.

Propiedades de Task:

| Propiedad | Tipo | Animable | Que es |
|---|---|---|---|
| `running` | bool (solo leer) |  | Si sigue en marcha. |
| `result` | cualquiera (solo leer) |  | Lo que devolvio la funcion (cuando termina). |

## Mando

Botones, trastes, rasgueo, palanca y vibracion del mando

- `pad.pressed(button, player=1)` -> bool: Si el boton esta pulsado ahora.

  ```python
  if pad.pressed(Button.RED):
      layer("rojo").glow(20, "red")
  ```

- `pad.just_pressed(button, player=1)` -> bool: Si se acaba de pulsar en este fotograma.
- `pad.just_released(button, player=1)` -> bool: Si se acaba de soltar en este fotograma.
- `pad.held_time(button, player=1)` -> decimal: Segundos que lleva pulsado (0 si no lo esta).

  ```python
  barra.fill = clamp(pad.held_time(Button.YELLOW) / 1.5, 0, 1)
  ```

- `pad.any_pressed(player=1)` -> bool: Si hay algun boton pulsado.
- `pad.buttons(player=1)` -> lista: Lista de los botones pulsados.
- `pad.frets(player=1)` -> lista: Lista de los trastes pulsados (verde, rojo, amarillo, azul, naranja).
- `pad.chord(*buttons, player=1)` -> bool: Si estan pulsados exactamente esos trastes a la vez.

  ```python
  if pad.chord(Button.GREEN, Button.RED):
      log("acorde")
  ```

- `pad.strum(player=1)` -> entero: Rasgueo: -1 arriba, 1 abajo, 0 nada (solo el fotograma en que se rasguea).
- `pad.whammy(player=1)` -> decimal: Palanca de tremolo, de 0 (suelta) a 1 (a fondo).

  ```python
  layer("onda").sy = 1 + pad.whammy() * 0.5
  ```

- `pad.tilt(player=1)` -> decimal: Inclinacion de la guitarra, de 0 a 1 (para activar el poder estelar).
- `pad.axis(name, player=1)` -> decimal: Un eje del mando normal: lx, ly, rx, ry, lt, rt (de -1 a 1 o de 0 a 1).
- `pad.connected(player=1)` -> bool: Si ese mando esta conectado.
- `pad.kind(player=1)` -> texto: Que mando es: guitar, drums, mic, pad o keyboard.
- `pad.vibrate(strength=0.5, seconds=0.2, player=1)`: Hace vibrar el mando (si puede).

## Capas

Capas: posicion, efectos (resplandor, desenfoque, sombra, contorno, tinte) y animaciones

- `layer(name)` -> capa: La capa con ese nombre (el del panel Capas).

  ```python
  boton = layer("jugar")
  boton.glow(20, "gold")
  ```

- `layers(prefix='')` -> lista: Todas las capas (o las que empiezan por un prefijo).

  ```python
  for t in layers("traste_"):
      t.alpha = 0.4
  ```

- `exists(name)` -> bool: Si existe una capa con ese nombre.
- `Layer.show(time=0.0)`: La muestra (con fundido si time > 0).
- `Layer.hide(time=0.0)`: La oculta (con fundido si time > 0).
- `Layer.toggle()`: La muestra si estaba oculta y al reves.
- `Layer.set(**props)`: Cambia varias propiedades a la vez.

  ```python
  layer("panel").set(alpha=0.8, tint="#202040", blur=4)
  ```

- `Layer.get(prop)` -> cualquiera: Lee una propiedad por su nombre.
- `Layer.reset(time=0.0)`: Vuelve a como esta en el diseno (posicion, escala, efectos...).
- `Layer.animate(time=0.3, ease='out', delay=0.0, loop=0, yoyo=False, wait=False, **props)` *(puede esperar)*: Anima propiedades hasta esos valores. loop: repeticiones extra (-1 = siempre); yoyo: ida y vuelta; wait: esperar a que termine. Curvas: linear, in, out, inout, in_cubic, out_cubic, inout_cubic, in_back, out_back, out_elastic, out_bounce, in_bounce, step.

  ```python
  layer("logo").animate(scale=1.2, alpha=1, time=0.4, ease=Ease.OUT_BACK)
  ```

- `Layer.stop()`: Para sus animaciones (se queda donde este).
- `Layer.fade_in(time=0.3, wait=False)` *(puede esperar)*: Aparece con fundido.
- `Layer.fade_out(time=0.3, wait=False)` *(puede esperar)*: Desaparece con fundido (y queda oculta).
- `Layer.move_to(x, y, time=0.3, ease='out', wait=False)` *(puede esperar)*: Se mueve a una posicion.
- `Layer.move_by(dx, dy, time=0.3, ease='out', wait=False)` *(puede esperar)*: Se mueve una distancia.
- `Layer.scale_to(scale, time=0.2, ease='out', wait=False)` *(puede esperar)*: Cambia de escala.
- `Layer.rotate_to(degrees, time=0.3, ease='out', wait=False)` *(puede esperar)*: Gira hasta un angulo.
- `Layer.glow(size=16.0, color='#ffffff', alpha=1.0, time=0.0)`: Resplandor exterior.

  ```python
  layer("rojo").glow(24, "red", time=0.1)
  ```

- `Layer.unglow(time=0.0)`: Quita el resplandor.
- `Layer.blur(px=6.0, time=0.0)`: Desenfoque (0 = nitida).
- `Layer.shadow(blur=8.0, color='#000000', x=4.0, y=4.0, alpha=0.6, time=0.0)`: Sombra paralela.
- `Layer.outline(width=2.0, color='#ffffff', time=0.0)`: Contorno alrededor.
- `Layer.tint(color, time=0.0)`: Tine la capa de un color.

  ```python
  layer("nota").tint("gold")
  ```

- `Layer.untint(time=0.0)`: Quita el tinte.
- `Layer.pulse(prop='scale', amount=0.08, period=0.6, times=-1)`: Late: sube y baja una propiedad (times = cuantas veces; -1 siempre).

  ```python
  layer("pulsa_start").pulse("alpha", -0.6, 1.2)
  ```

- `Layer.shake(amount=8.0, time=0.3)`: Tiembla (se queda donde estaba).
- `Layer.flash(color='#ffffff', time=0.2)`: Destello de un color que se apaga.
- `Layer.bounce(height=20.0, time=0.4)`: Da un salto y vuelve.
- `Layer.spin(speed=180.0)`: Gira sin parar (grados por segundo; 0 para).
- `Layer.set_text(text)`: Cambia su texto (igual que .text = ...).
- `Layer.set_image(name)`: Cambia su imagen.
- `Layer.focus()`: Le da el foco (si es un boton).
- `Layer.contains(x, y)` -> bool: Si el punto (x, y) cae dentro de la capa.
- `Layer.busy()` -> bool: Si tiene alguna animacion en marcha.

Propiedades de Layer:

| Propiedad | Tipo | Animable | Que es |
|---|---|---|---|
| `x` | numero | si | Posicion horizontal (pantalla de 1280 x 720). |
| `y` | numero | si | Posicion vertical. |
| `z` | numero | si | Profundidad (2.5D y 3D). |
| `scale` | numero | si | Escala (1 = tamano de diseno). |
| `sx` | numero | si | Escala horizontal (se multiplica por scale). |
| `sy` | numero | si | Escala vertical. |
| `rot` | numero | si | Giro en grados. |
| `alpha` | numero | si | Opacidad, de 0 (invisible) a 1. |
| `visible` | bool |  | Si se dibuja. |
| `tint` | color | si | Color que multiplica la capa (#ffffff = sin tinte). |
| `brightness` | numero | si | Brillo, de 0 (negro) a 2 (el doble); 1 = normal. |
| `glow` | numero | si | Resplandor exterior: tamano en pixeles (0 = sin resplandor). |
| `glow_color` | color | si | Color del resplandor. |
| `glow_alpha` | numero | si | Intensidad del resplandor, de 0 a 1. |
| `blur` | numero | si | Desenfoque en pixeles (0 = nitida). |
| `shadow` | numero | si | Sombra: desenfoque en pixeles (0 = sin sombra). |
| `shadow_color` | color | si | Color de la sombra. |
| `shadow_alpha` | numero | si | Opacidad de la sombra. |
| `shadow_x` | numero | si | Desplazamiento horizontal de la sombra. |
| `shadow_y` | numero | si | Desplazamiento vertical de la sombra. |
| `outline` | numero | si | Contorno: grosor en pixeles (0 = sin contorno). |
| `outline_color` | color | si | Color del contorno. |
| `fill` | numero | si | Cuanto se ve, de 0 a 1 (barras de progreso, medidores). |
| `fill_dir` | texto |  | Desde donde se llena: right, left, up, down, center. |
| `text` | texto |  | Texto (solo capas de texto). Cambiarlo lo redibuja en el juego. |
| `image` | texto |  | Imagen que muestra (nombre de un recurso del proyecto o del juego). |
| `order` | numero |  | Orden de dibujo (mayor = encima). |
| `focusable` | bool |  | Si puede recibir el foco (si es un boton). |
| `name` | texto (solo leer) |  | Nombre de la capa. |
| `w` | numero (solo leer) |  | Ancho de diseno. |
| `h` | numero (solo leer) |  | Alto de diseno. |
| `kind` | texto (solo leer) |  | Tipo: image, text, shape, group. |
| `focused` | bool (solo leer) |  | Si tiene el foco. |
| `animating` | bool (solo leer) |  | Si tiene alguna animacion en marcha. |

## Pantallas

Pantallas, foco de los botones y raton

- `goto(screen, transition='auto')`: Va a otra pantalla (salida de esta, entrada de la otra). transition: auto usa las animaciones del diseno.

  ```python
  goto("opciones")
  ```

- `back(transition='auto')`: Vuelve a la pantalla anterior (en la primera, cierra la interfaz).
- `close()`: Cierra la interfaz (con su animacion de salida).
- `screen()` -> texto: Nombre de la pantalla actual.
- `screens()` -> lista: Nombres de todas las pantallas.
- `history()` -> lista: Pantallas por las que se ha pasado (para back()).
- `focus(target)`: Da el foco a una capa (su nombre o la capa).

  ```python
  focus("jugar")
  ```

- `focused()` -> capa: La capa con el foco (o nada).
- `focus_move(direction)`: Mueve el foco como las flechas: up, down, left, right.
- `lock_focus(locked=True)`: Bloquea el foco: las flechas y elegir no hacen nada (para modos de juego dentro del menu). Los eventos de botones siguen llegando.

  ```python
  @on_enter("modo_test")
  def entrar():
      lock_focus(True)
  ```

- `mouse_x()` -> decimal: Posicion horizontal del raton (en la pantalla de 1280 x 720).
- `mouse_y()` -> decimal: Posicion vertical del raton.
- `mouse_down()` -> bool: Si el boton izquierdo del raton esta pulsado.
- `hovered()` -> capa: La capa bajo el raton (o nada).

## Eventos

Eventos: botones, pantallas, foco, tiempo, variables y mensajes

- `emit(name, value=None)`: Manda un mensaje: lo reciben las funciones con @on_message(name).

  ```python
  emit("acierto", 100)
  ```


## Sonido

Sonidos del menu del juego

- `sound(name, volume=1.0)`: Suena un efecto del menu del juego.

  ```python
  sound(Sound.SELECT)
  ```


## Juego

Variables y scripts del juego (QB), datos de jugadores y banda, menus del juego

- `game.get(name, default=None)` -> cualquiera: Lee una variable global del juego (QB).

  ```python
  modo = game.get("current_num_players")
  ```

- `game.set(name, value)`: Cambia una variable global del juego (entero, decimal o texto).
- `game.call(script, **params)`: Ejecuta un script del juego con sus parametros.

  ```python
  game.call("ui_event", event="menu_back")
  ```

- `game.player(field, player=1)` -> cualquiera: Dato de un jugador: score, notes_hit, total_notes, best_run, current_run, stars, multiplier, star_power, star_power_used, difficulty, part, controller, name.

  ```python
  layer("puntos").text = format_number(game.player("score"))
  ```

- `game.band(field)` -> cualquiera: Dato de la banda: score, current_mult, stars, projected_song_stars, star_power.
- `game.open_menu(state)`: Abre un menu del juego (un UIstate, p. ej. "UIstate_options").
- `game.in_song()` -> bool: Si se esta tocando una cancion.
- `game.paused()` -> bool: Si el juego esta en pausa.
- `game.language()` -> texto: Idioma del juego (en, es, fr, de, it).
