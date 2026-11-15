# Modo desarrollador: puente QB

El puente permite hablar con el motor de scripts de Neversoft mientras el juego está abierto: lanzar scripts, llamar a funciones nativas, leer y cambiar globales y abrir el menú de depuración original.

Las órdenes se ejecutan en el hilo del juego, una vez por fotograma lógico. Si el motor falla durante una orden, el puente captura el error y lo anota en el registro en lugar de cerrar el juego.

## Cómo entrar

| Desde | Cómo |
|---|---|
| Teclado | **F6** abre el menú de depuración de Neversoft y la pestaña Dev del panel F8 |
| Panel F8, pestaña Dev | Menús (pantallas de depuración y calibración), Consola (órdenes con historial), Globales (vigiladas en vivo), Trucos, Motor, Archivos, Memoria, Atajos |
| Consola del SDK (tecla de `[hotkeys] console`, `Backtick` por defecto) | `qb <orden>` |
| Otro programa (el editor) | puerto `127.0.0.1:8770` con clave; cliente: `python tools/qb.py <orden>` |

## Órdenes

```
run <script|funcion> [parametros]   lanza un script (como SpawnScriptLater) o llama a una funcion nativa
exists <nombre>                     tipo del simbolo: script, funcion del motor, entero, estructura...
get <global>                        valor de una global (entero, real, nombre o texto)
set <global> <valor>                cambia una global entera, real o nombre (con su mismo tipo)
debugmenu                           = state UIstate_debug
state <UIstate_...>                 abre esa pantalla (solo en el menu principal, no en una cancion)
debugbuild on|off                   lo que responde DebugBuild() a los scripts
runfirst a b c                      lanza el primero de la lista que exista
count                               simbolos cargados
```

Los parámetros se escriben como en QB: `clave=valor`. El valor puede ser un entero, un real, `"texto"`, un nombre (se convierte en su clave), `0x1234ABCD` o una estructura `{...}` anidada. Un nombre suelto se pasa como bandera.

```
python tools/qb.py run ui_event event=menu_change data={state=UIstate_debug}
python tools/qb.py get controller_unplugged_frame_count
```

## Puerto local

- Solo escucha en `127.0.0.1`.
- Al arrancar, el juego escribe `dev_bridge.json` (puerto y una clave aleatoria) en su carpeta de trabajo y lo borra al cerrar. Git ignora ese archivo.
- El protocolo va por líneas:
  1. el cliente envía `auth <clave>`;
  2. después envía una orden por línea;
  3. cada orden recibe una respuesta `ok ...` o `err ...`.
- `[dev] bridge_port` en `re_wor_de.toml` cambia el puerto (8770 por defecto); `0` apaga el puente.

## Qué hay detrás (ejecutable de WoR)

- **Tabla de símbolos global** en `*0x82986970`:
  - cubos de 4 bytes, índice `rotl(clave, 2) & 0x1FFFC`;
  - cada entrada lleva `+2` tipo, `+4` clave, `+12` valor y `+16` siguiente;
  - tipos: 1 entero, 2 real, 3 texto, 7 script, 8 función, 10 estructura, 13 nombre, 26 alias.
- **Funciones del motor:**
  - `sub_823A5450`: crea un script (r3 = clave, r4 = parámetros). Si el símbolo es una función nativa, la llama al momento.
  - `sub_820E6490` / `sub_821E3D50` reservan estructuras y componentes;
  - `sub_82397720` añade un componente;
  - `sub_82395A00` vacía una estructura.
- **`DebugBuild`** (`sub_8259EBA8`) devolvía siempre 0. Ahora responde lo que diga `debugbuild`.
- **Tabla completa de funciones nativas:** 1.947 nombres → dirección, extraídos de la copia del juego en `cache/re/cfuncs.tsv` (no va a git).

## Cuidado

`debug_menu_unlockall` y el resto de opciones de desbloqueo **cambian tu partida guardada**. En el panel están detrás de una casilla de confirmación. Haz una copia de `user/` antes de usarlas.

## Estabilidad

- El menú de depuración solo se abre cuando el juego ya está en el menú principal y no hay una canción en marcha. Abrirlo durante el arranque o en mitad de una canción dejaba al juego sin la interfaz que espera.
- **Canciones lanzadas desde el menú de depuración.** Llegan sin su lista de eventos. `sub_82400540` leía la dirección 0 y el juego se cerraba. Ahora esa llamada se ignora y queda anotada en el registro como `[Guard]`.

# Options > Graphics (dentro del juego)

En el menú de opciones del juego hay una entrada nueva, **Graphics**, que se maneja con el mando como cualquier otra opción: arriba/abajo para elegir, izquierda/derecha o aceptar para cambiar el valor, atrás para salir.

| Opción | Valores | Se aplica |
|---|---|---|
| Resolution | Desktop, 720p, 1080p, 1440p, 4K | al reiniciar (*) |
| Display Mode | Windowed, Borderless, Fullscreen | al reiniciar (*) |
| Frame Rate | 60 a 240 FPS (frecuencia del juego; más de 60 es experimental) | al reiniciar (*) |
| VSync | Off / On (sincroniza con el monitor) | al momento |
| Show FPS | Off / On (contador en la esquina) | al momento |
| Render Resolution | 1x a 4x | al reiniciar (*) |
| Anti-Aliasing | Off / MSAA 2x | al reiniciar (*) |
| Upscaling, Sharpness | Bilinear, AMD CAS, AMD FSR 1; nitidez 0-10 | al reiniciar (*) |
| Bloom, Glow, Blur, Brightness, Contrast, Saturation, Vignette, Film Grain | 0-10 (o -5 a 5) | al momento, en el escenario durante las canciones |
| Reset to Defaults | | |

Todo se guarda en `[graphics]` de `re_wor_de.toml`. Son los mismos valores que la pestaña Gráficos del panel F8.

## Cómo está hecho

- **El menú** es un script QB propio: `qb/options_graphics.q`. CMake lo compila con `tools/qbc.py` en `<carpeta del .exe>/qb/options_graphics.qb.xen`, y el juego lo registra al terminar de arrancar.
- **Registro:** se copia a memoria del juego y se llama a `sub_823E4958`, la función que usa `LoadQB`.
- **`hook`:** renombra en la tabla de símbolos los scripts del juego que el archivo envuelve. Por ejemplo, `ui_create_options` pasa a llamarse `re_orig_ui_create_options` y la versión nueva lo llama antes de añadir su fila. No se copia ningún script del juego.
- **El estado de interfaz** de la pantalla Graphics es el de `options_facebook`, un servicio que ya no existe. Solo cuando se entra desde Graphics; si no, Facebook funciona como siempre. El motor no admite estados nuevos sin cambiar su tabla nativa.
- **Comunicación con el C++:** cada opción es una global entera `re_gfx_v<N>`. `SyncGameMenu` (`src/de/gfx_options.cpp`) la lee, la traduce a `[graphics]`, guarda y aplica lo que puede aplicarse al momento.
- **`qbc.py`** genera exactamente el mismo bytecode que el compilador de Neversoft: 1.111 de 1.111 scripts del juego comprobados en ida y vuelta.
- **Órdenes del puente:**
  - `loadqb <archivo>` registra otro archivo compilado;
  - `rename <a> <b>` renombra un símbolo.

# Opciones gráficas (panel F8, pestaña Gráficos)

Se guardan en `[graphics]` de `re_wor_de.toml`.

| Opción | Qué hace | Cuándo se aplica |
|---|---|---|
| Resolución | modo de vídeo del juego y de la ventana (cvar `resolution`) | al reiniciar |
| Modo de pantalla | ventana, pantalla completa sin bordes o exclusiva | al reiniciar (F11 alterna en el momento) |
| Frecuencia | VBlank del juego (`video_mode_refresh_rate`); 60 = consola, más alto es experimental | al reiniciar |
| Resolución de render | escala interna 1x-4x (`draw_resolution_scale`) | al reiniciar |
| Antialiasing | MSAA 2x nativo del Xenos | al reiniciar |
| Escalado y nitidez | bilineal, AMD CAS o AMD FSR 1, con nitidez 0-1 | al reiniciar |
| Franjas | letterbox si la ventana no es 16:9 | en el momento |
| Bloom, glow, brillo, contraste, saturación, viñeta, grano | post-proceso propio en la pasada sandbox. Solo afecta al escenario durante las canciones; la autopista y el HUD no se tocan. Los efectos del editor (EDFX) se suman encima. | en el momento |

`vsync` no aparece en el panel. En este SDK no es el vsync del monitor: es el reloj de VBlank del juego. Si se apaga, el juego va a 1000 Hz y da tirones.

VSync (del monitor) es una cvar nueva del SDK, `present_vsync`, en `d3d12_presenter.cpp`. Antes se presentaba siempre sin esperar.

`target_refresh_rate` no existe en el SDK, así que el combo antiguo de "tasa de refresco" no hacía nada. Se ha sustituido por Frecuencia.
