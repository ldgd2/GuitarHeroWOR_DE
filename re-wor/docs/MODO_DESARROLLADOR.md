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
| Quality Preset | Low, Medium, High, Ultra (Custom si se toca algo a mano) | según la opción |
| Graphics API | Direct3D 12, Vulkan | al reiniciar (*) |
| Resolution | Desktop, 720p, 1080p, 1440p, 4K | al momento, con 10 s para confirmar |
| Display Mode | Windowed, Borderless, Fullscreen | al momento, con 10 s para confirmar |
| Frame Rate | 60 a 240 FPS; la velocidad del juego no cambia | al momento |
| VSync | Off / On | al momento |
| Show FPS | Off, On, Detailed (tiempo de frame, esperas a la GPU, draws) | al momento |
| Render Resolution | 1x a 4x | al reiniciar (*) |
| Anti-Aliasing | Off, FXAA, MSAA 2x, MSAA 2x + FXAA | FXAA al momento, MSAA al reiniciar |
| Texture Filtering | Trilinear, Anisotropic 2x a 16x | al reiniciar (*) |
| Post-Processing | Off / On (apagado ahorra una pasada a pantalla completa) | al momento |
| Bloom, Glow, Blur, Brightness, Contrast, Saturation, Vignette, Film Grain | 0-10 (o -5 a 5) | al momento, en el escenario durante las canciones |
| Reset to Defaults | preset High | |

Los presets **no tocan la resolución ni la pantalla**:

| Preset | Anti-Aliasing | Filtrado | Post-proceso | Caché de texturas |
|---|---|---|---|---|
| Low | Off | Trilinear | Off | 512 MB |
| Medium | FXAA | 4x | On | 1024 MB |
| High | MSAA 2x | 8x | On | 1024 MB |
| Ultra | MSAA 2x + FXAA | 16x | On | 2048 MB |

**API gráfica:** el plugin de GPU se compila con Direct3D 12 y Vulkan. Si Vulkan no puede presentar en la tarjeta, el SDK vuelve solo a Direct3D 12. Los efectos de imagen propios (bloom, glow...) y las medidas de rendimiento solo existen en Direct3D 12. Direct3D 11 no existe en el SDK: emular la EDRAM del Xenos necesita funciones que D3D11 no tiene.

## Resolución y frecuencia al momento

- **Resolución y modo de pantalla** se aplican al momento. Aparece un aviso con una cuenta atrás de 10 s: Enter o A mantiene, Esc o B vuelve. Si no se pulsa nada, se vuelve sola a la anterior. El `.toml` solo cambia al confirmar.
- **La resolución son píxeles reales:** el SDK ya no la multiplica por la escala de Windows, y la ventana nunca es más grande que la pantalla (`window_sdl.cpp`).
- **Frame Rate (`src/de/frame_pacing.cpp`):** los FPS no cambian la velocidad del juego.
  - El motor tiene dos candados, las globales `game_lock_framerate` y `render_lock_framerate`, que lo fijan a 60. Con más de 60, re-wor los quita y limita él mismo el bucle del motor a la frecuencia elegida (sin límite iría a 700+ FPS).
  - Sin candados, el tiempo de frame del motor (`0x8285684C`, `GetDeltaTime`) ya es tiempo real. El reloj de la canción (`0x82993A58`), que mueve notas y highway, no: el motor (`sub_823D1490`) y el script (`IncrementSongTimer`) lo avanzan por frames (a 144 FPS iba a 2,41x). Con FPS altos, cada avance se sustituye por el tiempo real transcurrido desde el anterior × slomo (los frames en los que el juego suma 0 no pierden su tiempo) (`0x82856844`, lo que devuelve `GetSlomo`), igual que el audio, sin depender de cuántos frames haya. Pausas, saltos y la cuenta atrás se dejan como los hace el juego.
  - Canciones custom: `SongFileManager` (CFunc `0x821AF560`) pedía secciones que la canción no tiene y el motor original leía basura de la pila (crash en `sub_821AE590 + 0x213`). `src/de/engine_fixes.cpp` cancela esa petición y lo apunta como `[Arreglos]` en `re_wor_trace.log`.
  - Dentro de una canción se escribe en `re_wor_trace.log` una línea `[FPS] en cancion` cada 2 s (máximo 600) con FPS, velocidad del juego y de la canción, slomo y candados.
  - El VBlank del SDK y `XGetVideoMode` se quedan en 60 Hz, como en la consola.
  - Protección: cada 2 s se mide la velocidad del motor (segundos de juego por segundo real). Si con FPS altos se sale de 0,9–1,1 dos veces seguidas, vuelve sola a 60 con candados y lo apunta en `re_wor_trace.log`. Cambiar el Frame Rate en el menú lo vuelve a intentar.
  - `python tools/qb.py fps` y el contador de FPS en modo Detallado muestran la velocidad del juego y de la canción (deben ser 1,00x).
- **CAS / FSR:** se quitaron del menú porque el SDK está compilado sin FidelityFX y solo tiene escalado bilineal.

## Rendimiento

- **Cvars de GPU perdidas (arreglado):** las opciones gráficas se ponían en `OnPreSetup`, antes de cargar el plugin de GPU. El SDK rechazaba en silencio las cvars que aún no existían: MSAA, escala de render, caché de texturas, hilos de shaders... Ahora `SetFlagByName` guarda el valor y lo aplica cuando el plugin registra la cvar (`src/core/cvar.cpp` del SDK).
- **Diagnóstico de cámara:** el vigía `cam_watch` paraba todos los hilos del juego para poner puntos de interrupción la primera vez que el editor movía la cámara. Ahora solo se activa con `[performance] trace_probes = true`.
- **Gancho por draw:** se quitó, porque no hacía nada y el SDK preparaba datos en cada draw.
- **Medidas:** el procesador de comandos D3D12 cuenta frames, draws, resolves y esperas a la GPU, y las publica cada segundo en `DrawHooks::stats`. Con Show FPS = Detailed se ven en pantalla. Cada tirón (un frame de más de 40 ms y 2,5 veces la media) se apunta en `re_wor_trace.log` como `[Perf] tiron: ...`, con las esperas a la GPU de ese segundo.

Todo se guarda en `[graphics]` de `re_wor_de.toml`. Son los mismos valores que la pestaña Gráficos del panel F8.

## Público (Options > Graphics > Crowd)

`[graphics] crowd`: 0 None, 1 Low (solo las filas 3D de delante), 2 Medium (solo el público 2D del fondo), 3 High (el original). Se aplica al momento durante la canción (`qb/crowd.q`, `SyncCrowd` en `gfx_options.cpp`). Oculta con lo mismo que el script del juego `hide_crowd_models`. No usa `disable_crowd`: cargar el escenario sin público cierra el juego (Access Violation en `sub_82406D60`).

## Idioma (Options > Language)

WoR trae los textos en cinco idiomas: `pak/qs.pak` (inglés), `qs_s` (español), `qs_f` (francés), `qs_g` (alemán) y `qs_i` (italiano), más las pantallas de carga con texto (`*_s.img`...). `[game] language` (`en`, `es`, `fr`, `de`, `it`) elige cuál. Se cambia en el menú de opciones del juego (Language) o en el panel F8 (Gráficos > Idioma), y se aplica al reiniciar: sale un aviso.

- **Motor** (`src/de/language.cpp`): el idioma es una clave en `0x82860430`. De ella salen el sufijo de los archivos (`sub_82403230`) y el nombre del idioma (`sub_82403310`). La pone `sub_824AD288` al arrancar y la cambia `SetConfig`. re-wor la fija al idioma elegido.
- **Sistema** (`xam_info.cpp` del SDK): `XGetLanguage` devuelve el idioma de la variable de entorno `REWOR_LANGUAGE`. Antes devolvía siempre inglés.
- **Partida guardada:** WoR usa como nombre de archivo de la partida el texto traducido (qs `0x7235ca15`: Progress, Progreso, Progression, Fortschritt, Progressione). Sin arreglo, cada idioma crearía su propia partida vacía. `REWOR_SAVEGAME_ALIAS` (p. ej. `Progreso=Progress`) hace que en disco sea siempre `Progress` y que el juego vea el nombre de su idioma (`content_manager.cpp` del SDK). Un `CREATE_ALWAYS` con el nombre traducido nunca borra la partida (`xam_content.cpp`).
- Se usan variables de entorno porque el ejecutable y la DLL del SDK tienen cada uno su registro de cvars: una cvar puesta desde el juego no llega a la DLL.

## Aviso de reinicio

Al salir de Options > Graphics con cambios que se aplican al reiniciar (API, resolución de render, MSAA, filtrado...), o al elegir otro idioma, sale un cuadro del juego (`create_dialog_box`) que lo avisa (`re_restart_popup` en `qb/options_graphics.q`).

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
