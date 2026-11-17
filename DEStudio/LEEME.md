# DE Studio

Estudio de herramientas de Warriors of Rock Definitive Edition. Al abrirlo sale el **inicio** (hub) con lo que
se puede crear y la biblioteca de proyectos:

| Herramienta | Pagina | Que hace |
|---|---|---|
| **Canciones** | `cancion.html` | Puesta en escena de canciones de mods: camaras, clips de banda (animaciones de performance), loops, luces/pirotecnia, publico y animaciones `.ska`, sobre el dato REAL de la cancion. Se compila al PAK de la cancion |
| **Interfaces** | `ui.html` | Pantallas y menus del juego en **2D**, **2.5D** o **3D**, con capas, imagenes del juego o propias, claves y clips de entrada/bucle/salida. Se exporta a algo que el juego entiende (ver "Editor de interfaces") |

## Abrir

`DEStudio.bat` (o `python server.py`). Se abre en el navegador en `http://127.0.0.1:8765/` (el inicio).
Solo escucha en tu equipo. Necesita Python 3.11+.

La interfaz es React 18 + Vite + three.js + anime.js (`web/src`). El servidor sirve la version compilada
(`web/dist`); tras cambiar algo en `web/src`:

```
cd web
npm install        (solo la primera vez)
npm run build      (o: npx vite build)
```

Para desarrollar con recarga en caliente: `npm run dev` (pasa `/api` al servidor en el puerto 8799).

## Proyectos: un solo archivo `.desp`

Cada proyecto (cancion o interfaz) es **un archivo `.desp`** con todo dentro: el proyecto y los recursos que no
estan en el juego (animaciones `.ska` importadas a una cancion, PNG propios de una interfaz). Solo lo entiende DE
Studio (por dentro es un ZIP con `manifest.json`, `project.json` y `assets/`; ver `editor/core/projfile.py`).

* **Biblioteca**: `DEStudio/proyectos/*.desp` (fuera de git). El inicio la lista; Guardar escribe ahi.
* **Abrir archivo .desp** (inicio o editores): trae un `.desp` de cualquier carpeta a la biblioteca y lo abre en
  su editor. **Copia** guarda el `.desp` donde elijas (dialogo nativo en Edge/Chrome; si no, se descarga).
* Mientras se edita, los recursos se desempaquetan en `cache/work/` (se puede borrar: el `.desp` manda).
* Los proyectos de cancion del formato anterior (`projects/*.json`) se pasan solos a la biblioteca al arrancar
  el servidor (el JSON se deja donde estaba).

## Pantalla

| Zona | Que hace |
|---|---|
| Vista 3D (izquierda) | El escenario real del juego (geometria y texturas de `zones/`), sus nodos, y la banda con modelos del juego animados con los `.ska` reales (clips de la cancion y loops de `anim_loops`) |
| Monitor (centro) | Lo que ve la camara de programa: toma del editor, camara de clip (`.ska`) o plano del escenario (arrays `<zona>_cameras_*` del `.qb` de la zona: fijas, en mano, dos posiciones, orbita) |
| Panel derecho | **Inspector**: lo seleccionado (sin seleccion, resumen y revision). **Camaras**: presets de toma (fija, en mano, temblor, acercamiento, alejamiento, zoom, vertigo, orbita, grua, travelling, contrapicado, cenital, barrido, latido, plano holandes) con distancia, angulo, altura, lente e intensidad, y efectos de imagen. **Objetos 3D**: cajas, paneles, pantallas, texto, focos, fuego y modelos `.glb` con gizmo mover/girar/escalar. **Banda**: personaje, piezas, instrumento y animacion de prueba de cada musico (solo vista previa). **Vista**: escena, texturas (on/off, calidad 256-2048, recargar), luz horneada o dinamica con sombras, IK, esqueleto, lente y velocidad de vuelo |
| Barra flotante (vista 3D) | Encuadres (frente, arriba, musicos 1-4), copiar camara de programa, ver por la camara de programa, texturas, luz, esqueleto, ampliar la vista. Barra de progreso al cargar modelos/texturas |
| Linea de tiempo (abajo) | Grupos plegables con subpistas: **Cancion** (audio mezclado con forma de onda y una subpista por instrumento: bateria, guitarra, bajo, voz, resto, publico; el ojo/altavoz silencia cada una), **Camaras** (varias "Escena N": la de arriba manda; automaticas y de momento), **Efectos de imagen**, **Fuego** (nota 78), **Luces** (ambiente, estrobo, focos, colores, resto), **Especiales** (eventos, notas de banda, publico, momentos), un grupo por musico (clips y loops) y **Objetos 3D**. Cada grupo y pista se renombra (doble clic), se oculta, se bloquea, se mueve y se le anaden subpistas (`+` o menu `···`); "Grupo" crea grupos nuevos. Las tomas, efectos y objetos se arrastran de una subpista a otra. El separador de arriba cambia la altura |
| Audio | El de las canciones de mods (`.fsb` WoR o WT, MP3) se descifra y decodifica en el servidor (cache en `cache/audio/`). El reloj del editor sigue al audio. Las canciones base del juego van en XMA y no suenan en el editor |

Clic: seleccionar / mover el cursor. Arrastrar: mover (se ajusta a compas; Alt para libre). Borde derecho:
duracion. Doble clic en una pista: anadir. Supr: borrar. Ctrl+Z / Ctrl+Y. Ctrl+rueda: zoom. Espacio: reproducir.
Inicio: volver al principio. Los loops se escriben por nombre (autocompleta con las animaciones del juego).

Vista 3D: arrastrar gira, clic derecho mueve, rueda acerca. Con el raton encima: **WASD** vuela, **Q/E**
baja/sube, **Shift** rapido, **1-4** encuadra guitarra/bajo/voz/bateria, **0** frente, **9** arriba,
**C** copia la camara de programa, doble clic centra en ese punto, clic sobre un musico abre su ficha de banda.

## Guardar y compilar

* **Guardar proyecto**: guarda el `.desp` de la cancion en la biblioteca (no toca el juego). **Copia**: el `.desp` a otra carpeta.
* **Compilar al juego**: escribe la cancion en su mod, con copia previa en `DEStudio/backups/`, y la
  valida con el mismo validador del gestor de mods. **Copias** restaura la ultima.
* **Tomas del editor**: al compilar, cada toma se convierte en lo que el juego entiende: una camara `.ska` nueva
  (3 huesos, una clave cada 1/30 s, FOV animado en las claves custom), un clip con esa camara en el slot 0
  anclada al nodo del musico (y el musico con su loop de ese momento), el `Band_PlayClip` y un corte de momento
  numero 3. El proyecto fuente con las tomas se guarda a la vez, asi que al reabrir se siguen editando.
* **Solo vista previa** (no llegan al juego): efectos de imagen (franjas de cine, vineta, destello, fundidos,
  filtros) y objetos 3D. WoR no tiene pista para ellos ni carga geometria nueva desde una cancion.
* Solo cambia el contenido de una cancion existente, nunca la lista de canciones: la partida guardada no se ve
  afectada. Las canciones del juego base se abren solo como referencia.

## Como funciona WoR por dentro (lo que edita cada pista)

| Pista | Dato en la cancion |
|---|---|
| Camaras autocut / momento | `.perf`: `autocutcameras` y `momentcameras` = [tiempo, duracion, numero]. Los numeros de escenario los define cada escenario; 3-6 (y 90-99) son "la camara del slot N del clip que suena" |
| Clips | `Band_PlayClip` en `<cancion>_scriptevents` + definicion global `{characters, cameras}`: musico, nodo de salida, animacion, tramo, IK de manos, manos automaticas y camaras por slot |
| Loops | `Band_PlayLoop {name, male, female}`: animacion de hombre y de mujer |
| Luces / pirotecnia | `<cancion>_lightshow_notes`: notas [tiempo, nota, duracion] que interpreta el escenario |
| Notas de banda / publico | `<cancion>_anim_notes`, `<cancion>_crowd_notes` (mismo formato) |
| Camara de clip | `.ska` de 3 huesos: hueso 1 = posicion/orientacion relativa al nodo `TRG_Geo_Camera_Performance_*`, FOV en una clave custom |

## Lo que llega al juego (y como)

| En el editor | En el juego |
|---|---|
| Tomas de camara (todos los presets, incluido el temblor) | Camara `.ska` nueva + clip + corte de momento 3. El musico del clip usa su loop, que se copia al PAK |
| Columna de fuego | Pirotecnia nativa (nota 78 de luces): al empezar y cada 4 s; sin tiempo, al empezar cada toma |
| Efectos de imagen | **EDFX** (re-wor): `<cancion>.edfx` junto al PAK + eventos de sincronia |
| Objetos 3D y textos | **EDFX**, solo durante las tomas del editor (unico momento en que se conoce la camara) |
| Focos de luz | No llegan |

EDFX: el juego dispara los eventos `edfx_<cancion>_<n>` en su ms exacto; re-wor (`src/de/edfx.cpp`) reconoce la
clave en la busqueda de simbolos (`sub_8239E2A0`), la resuelve a `nullscript` y dibuja encima de la imagen con
el reloj anclado al ultimo evento (si no llegan eventos en 2.6 s, por pausa o fin, la capa se apaga). Los
filtros de color (B/N, sepia, colores vivos, desenfoque, resplandor) se aproximan con un velo; franjas,
fundidos, destellos, vineta y tintes salen como en el editor. Los objetos se dibujan siempre delante del
escenario (sin oclusion) y con sombreado plano. Verificacion: `re_wor_trace.log` debe mostrar `[EDFX] sync ...`
a lo largo de la cancion.

## Estructura del codigo

```
server.py                 entrada (python server.py [--port N] [--no-browser])
editor/core/              rutas, nombres QBKey, QB <-> JSON
editor/formats/           lectores de formatos del juego: pak, texture (.tex Xbox), scene (.scn/.skin),
                          skinned (mallas con pesos), skeleton (.ske), ska, glb (escritor glTF)
editor/assets/            texturas -> PNG y mallas -> glTF con cache en cache/
editor/stage/             escenarios: nodos, geometria y camaras de escenario (cameras.py)
editor/band/              nombres reales (dbg.pak), catalogo de piezas, personaje -> .glb, animaciones
editor/song/              canciones: biblioteca, pistas, proyecto, compilar/restaurar, camgen.py (camaras nuevas)
editor/server/            HTTP + rutas por area (routes_song, routes_stage, routes_band, ...)
editor/song/audio.py      audio de la cancion: descifra los .fsb, separa los MP3, mezcla por instrumento
web/src/core|song|viewport|band   logica y 3D (three.js); viewport/stagecams.js, fx.js, props.js,
                          hands.js (agarre de guitarra/bajo), skin.js (tono de piel), scene.js (ACES + bloom)
web/src/timeline/         layout.js (grupos y subpistas), lanes.js (contenido de cada pista), edit.js, coords.js
web/src/audio/player.js   reproduccion y silencio por instrumento
web/src/camera/           presets.js (tomas y efectos) y shots.js (pose, horneado y conversion al compilar)
web/src/ui/               componentes React (.jsx): components/, inspector/, band/, view/, camera/, props/,
                          timeline/Timeline.jsx; animaciones de interfaz con anime.js; web/src/css/ por area
web/dist/                 compilado (npm run build); es lo que sirve el servidor
```

Datos del juego que se leen (solo lectura): `zones/*.pak`, `pak/archive/cas_pieces`, `cas_globalarchive`,
`anim_loops`, `pak/dbg.pak` (nombres). Todo lo convertido se guarda en `DEStudio/cache/` (se puede borrar).

## Convenciones verificadas

* `.ske`/`.ska`: los cuaterniones del archivo son los conjugados de los de glTF; con eso la pose de enlace
  coincide exactamente con las matrices del `.ske`. Las traslaciones de las animaciones de cuerpo son
  desplazamientos sobre la pose de enlace.
* Camaras de clip: la camara mira hacia +Z de (hueso 0 x hueso 1), relativa al nodo del musico.
* Instrumentos y micros son mallas rigidas colgadas de huesos del esqueleto: `Bone_Guitar_Body` (112),
  `Bone_Mic_Stand` (122), `Bone_Mic_Microphone` (125); baquetas de las palmas.
* Los `.ska` NO animan antebrazo, mano, rodilla ni tobillo: traen huesos objetivo y el motor cierra los
  miembros con IK de dos huesos. 93/94 = pies, 126/127 = manos (hijos del micro). Sin IK los personajes
  quedan "flotando" (manos que no agarran, pies en el aire). Clips con `ik_targetl/r = guitar`: mano derecha
  al punto de rasgueo (hueso 115) y la izquierda al mastil (0.47, -0.06, 0.06 en el espacio del hueso 112);
  `slave` = manos libres (las mueve la animacion).
* `.ska` de un solo fotograma (bit 1<<6, sin comprimir: 4 floats por rotacion y por traslacion) = poses base.
  Capas aditivas (biceps en identidad en el primer fotograma) se multiplican sobre una pose: la bateria es
  `drum_s1t3` (sentado) + `drum_loop_generic_100`.
* Loops: el sufijo `_075`, `_150`... es una familia de tempo; la velocidad es el tempo de la cancion llevado a
  la misma octava (`bpm * 2^round(log2(cancion/bpm))`), si no los loops iban al doble.
* Clips: manda el ultimo `Band_PlayClip` que empezo (el juego sustituye al anterior).
* Materiales con modo de mezcla 1/2 = aditivos (humo, haces de luz): se ocultan salvo la opcion "Humo y haces".
  Tarjetas del publico (plantilla 0xdd6d5659) ocultas salvo la opcion "Publico".
* Camaras de escenario: `pos` y `Quat` sin conjugar (w deducido), la camara mira a +Z; LockTo = musico o World.
  Numero de corte -> nombre segun CamHandler.js del Guitar Hero SDK (3-6 momento, 8-26 musicos, 28-48 generales).
* Nombres legibles: `tools/ghsdk/SDKCode/Checksums/ghwor_keys.txt` (107k QBKeys) resuelve clips y animaciones.
  Los clips usan `timefactor` como velocidad. Al cambiar de animacion se funde en 0.25 s.

## Pendiente

* Compilador Blender (`.glb`) -> `.ska`: falta el esqueleto de WoR (nombres y jerarquia de los 128 huesos y su
  pose de reposo). Hoy se pueden **importar `.ska`** de WT o WoR (se convierten solos).
* Los personajes "de heroe" se arman por el nombre de sus piezas (no se encontro la tabla del juego). La piel
  (amarillo puro en las texturas = zona de tinte) se pinta con el tono elegido en la pestana Banda.
* Manos en guitarra y bajo: agarre calculado sobre los huesos del instrumento (mastil, cuerdas, punto de rasgueo);
  la mano del mastil cambia de traste y cuerda con las notas, pero no es la tabla de digitacion real del juego.
  Las poses `gh_rocker_*_fret_NN_d` de `perm_anims` estan localizadas (`/api/band/hands`) pero son deltas cuyo
  formato aun no se reconstruye bien. La animacion facial no se simula.
* Algunos `.tex` marcados como mapa de normales no lo son (o usan otra codificacion) y ponian la cara negra: se
  descartan si su media XY se aleja de 128.
* Nota 78 de luces como pirotecnia: deducido por estadistica (19 de 20 canciones base), sin confirmar.
* Significado de cada nota de luces por escenario; nombre de los cortes de camara por encima de 48.
* Audio de las canciones base del juego (XMA): no se decodifica.
* EDFX sin probar todavia dentro del juego (ver la tabla de arriba).

## Reloj del juego para EDFX (actualizado 2026-11-14)
- El hook ya no depende de los eventos de sincronia: lee el reloj de la cancion del motor (double en segundos
  en `0x82993A58`, el que avanza el CFunc `IncrementSongTimer` y leen `GetSongTime`/`TimeMarkerReached`) y sabe
  que cancion se cargo por la apertura de `songs/<nombre>_song.pak.xen`. Los eventos `edfx_*` quedan de respaldo.
- El hook antiguo de `sub_8239E2A0` se quito: esa funcion busca ARRAYS (tipo 12), nunca veia los scripts.
- El trace muestra `[EDFX] cancion cargada`, `[EDFX] reloj del motor` y `[EDFX] dibujando` para comprobarlo.
- 14 efectos dibujados nuevos (lineas de TV, grano, VHS, iris, foco, latido, confeti, chispas, nieve, lluvia,
  rayos, arcoiris, humo, laseres): `web/src/viewport/fx2d.js` y su copia en `re-wor/src/de/edfx.cpp` (DrawFx2D).
- Combinaciones de efectos en la pestana Efectos y 5 presets de camara mas.

## Saltar la intro (re-wor)
- Tecla F (`[hotkeys].skip_intro`): para los videos del arranque y hace que los scripts los den por terminados.
  Si se pulsa en la pantalla legal, queda armada. Deja de funcionar al llegar al menu (band_lobby).

## Camara del juego en el hook (2026-11-14)
- re-wor lee cada frame la camara del viewport principal: lista de viewports en `0x82860320`; viewport +116 =
  camara; camara +112 pos, +128 fila lateral, +144 up, +160 at, +192 hfov (de GetViewportCameraPos/Orient y
  Get/SetHFOV). Con ella los objetos 3D y textos se dibujan en TODA la cancion, no solo en las tomas del editor.
- Durante cada toma del editor el hook ESCRIBE la camara de la toma en el motor (posicion, orientacion, hfov),
  asi la toma se ve en el juego aunque el clip nativo no se reproduzca.
- Trace: `[EDFX] camara del juego ...` (muestras) y `[EDFX] toma del editor en t=...` (compara la camara del
  juego con la del editor: si no coinciden en posicion, el mundo del editor y el del juego estan desplazados).

## Efectos nativos (ScreenFX del motor) y zona sagrada del traste (2026-11-14)
- `re-wor/src/de/native_fx.cpp`: los filtros del editor se aplican con el post-proceso PROPIO de WoR
  (CFuncs ScreenFX_AddFXInstance 825090D0 / UpdateFXInstanceParams 82509178 / FxInstanceExists 82509360)
  sobre `bg_viewport`: solo afecta al escenario, el traste, las notas y el HUD quedan intactos.
  - bright_Sat (saturacion, brillo, contraste, mezclador RGB): B/N, sepia, saturar, contraste, resplandor,
    sueno, vision nocturna, frio, calido, arcoiris, golpe rojo, latido, fundidos, apagon, destellos,
    estrobo, rayos.
  - Vignette (inner/outer_radius): vineta, iris, foco.   dof3: desenfoque.
  - Parametros sacados de los lectores del motor (825293F0...) y de las venues del juego (qb.pak).
  - Los parametros se pasan como Script::CStruct construido en la pila del juego (cabecera 8 bytes,
    componentes de 16: +2 tipo, +4 nombre, +8 valor, +12 siguiente).
  - Trace: `[NativeFX] anadida instancia ScreenFX ...`.
- La capa superpuesta (particulas, laseres, rayos, VHS, franjas, objetos 3D, textos) se recorta fuera de un
  trapecio que cubre el traste de 1 jugador: nada se dibuja encima de las notas.
- Pendiente: objetos 3D nativos (CreateCompositeObject 824C53C8 / CreateFromStructure 825012B8) y
  particulas nativas (el motor tiene confeti real, PH_MultiConfetti_02).

## Recursos cinematograficos nativos e inyector 3D nativo (2026-11-14)
Todo en `re-wor/src/de/native_fx.cpp` (frame logico del juego) + camara en `edfx.cpp`.
- ScreenFX del motor (solo escenario, nunca el traste/HUD): bright_Sat, Vignette (con Color/alpha), dof3,
  bloom2, noise2, MotionBlur, fisheye (warp_strength/warp_x/warp_y/scale), godray, convolution (kernel 3x3).
  Efectos nuevos: bloom, motionblur, godrays, tiltshift, rackfocus, sharpen, emboss, edges, comic, bleach,
  teal, matrix, hueshift, flicker, danger, fisheye, wobble, shockwave. El grano pasa a ser nativo (noise2).
- Camara del motor: shake, dutch (holandes), zoompunch, vertigo (dolly zoom), handheld.
- Franjas de cine nativas: se recorta el rect {x,y,w,h} (+52, copia +68) del objeto de vista del viewport del
  escenario (busqueda validada en tiempo real; si no se encuentra, se dibujan encima como antes).
  Trace: `[NativeFX] franjas de cine nativas: rect del escenario @...`.
- Objetos nativos (linea `native <tipo> t dur x y z qx qy qz qw r g b v0 v1 v2 recurso` del .edfx):
  - `light`: CreateFromStructure 825012B8 con un GameObject de carcasa de foco (models/lighthousings,
    global.pak) y haz volumetrico (Volume2DSoftEdge, VolumeColor). v0 alcance, v1 apertura, v2 carcasa.
  - `pfx`: CreateParticleSystem 821DB160 {name, params_Script = referencia global (tipo 26) a gp_fx_*, pos,
    groupID}; se quita con DestroyParticlesByGroupID 824C05B8. 292 definiciones, texturas en global.pak.
  - `model`: GameObject type=Ghost con Model="ruta" (global.pak o "zona:ruta" si ese escenario esta cargado).
  - Se quitan con Kill 82502BA0 {name}. Al cambiar de cancion, reiniciar o rebobinar se borran todos.
  - Memoria: 64 KB propios en el monton del motor (sub_82392380 sobre el monton de 0x8297F0DC).
  - Llamadas protegidas con SEH: si el motor falla al crear, se desactivan los objetos nativos en esa sesion.
  - Orientacion: el foco del motor apunta a +Z local (supuesto); el editor gira -Y -> +Z (fixSpot en edfx.js).
- CStruct de parametros (tipos): 1 int, 2 float, 3 cadena, 6 vector {0,x,y,z}, 12 array {+2 tipo,+4 n,+8 datos},
  13 checksum, 26 referencia global; flag sin nombre = checksum con nombre 0.
- Pendiente: geometria NUEVA (.glb del usuario) como .mdl.xen/.tex.xen propios (formato de malla de Xbox 360).

## Edicion de video: pantalla dividida, ajustes de imagen y looks

- **Pantalla dividida** (`split`): de 2 a 4 franjas horizontales o verticales, o una cuadricula 2x2. Cada franja es un
  viewport real del motor (`CreateViewport`) que usa un estilo propio inyectado en `viewport_Params.styles` mediante
  un hook de `sub_823E7858`. Tiene prioridad sobre el escenario y queda por debajo del traste y del HUD.
  Cada franja aparece en el pulso que toca ("Pulsos entre franjas") y se queda con el plano de la camara del juego
  en ese momento. La entrada puede ser con "Corte seco" o con "Barrido". Al terminar el efecto se destruyen los viewports.
- **Ajustes de imagen** (`grade`): exposicion, contraste, saturacion, temperatura, tinte, tono y vineta.
  Se aplican con bright_Sat y Vignette nativos.
- **Enfoque** (`focus`): distancia de enfoque, ancho de la zona nitida y fuerza del desenfoque (dof3 nativo).
- **Looks**: vaporwave, cyberpunk, cine negro, atardecer, lomo, technicolor, infrarrojo, pastel y pelicula.
  Combinan matriz de color, bloom, grano y vineta nativos.
- Formato `.edfx`: `fx <tipo> <t> <dur> <intensidad> clave=valor ...`. Para `split`, el editor exporta `r0..r3`,
  que son los segundos en que aparece cada franja, calculados con los pulsos de la cancion.

## Efectos v3: formatos del juego, ajustes editables y presets

- Los valores salen de los presets que trae el propio juego en `qb.pab`: bright_Sat, bloom2, dof3, godray, MotionBlur,
  Vignette y noise2.
- **Arreglo importante:** el mezclador de color se manda como `red_mix/green_mix/blue_mix = [r,g,b,0]`, que es lo que lee
  el motor. Antes iba como `red_mix_r…`, que el motor ignoraba, y por eso los looks de color no hacian nada.
- **Glow** real con bloom2: umbral (`subtract`), color e intensidad (`colormodulate` rgb) y tamano (`colormodulate` w).
- Todos los efectos tienen ajustes, fundido de entrada y fundido de salida (`fin`/`fout`). Los colores se exportan como
  `<clave>r/g/b`. Hay 22 looks de color (matriz + saturacion/contraste/brillo + vineta/grano/glow), mas exposicion y tinte.
- **Presets** = grupos de efectos ya ajustados. Se editan juntos (mover, duracion, intensidad) o uno por uno, y se pueden
  guardar como preset propio (`proj.userCombos`).
- **Pantalla dividida:** cada franja tiene su camara propia, que es una toma del editor (musico, encuadre, animacion de
  entrada y su duracion; al terminar se queda quieta), o la del juego congelada. En el .edfx van como `scam <franja> <ms>`
  seguido de claves `k`. Los viewports comparten la visibilidad del escenario (`sharevisibilityflagswith = bg_viewport`).
  En el monitor cada franja se dibuja en vivo con su camara.
- **Camara:** el hook recuerda la pose del juego antes de tocarla. Si el motor no reescribe la camara en un frame (planos
  fijos), los efectos parten de esa pose y no se acumulan. Al acabar el efecto o la toma, se devuelve la camara del juego.
- Experimentales (ocultos, sin verificar): fisheye, mareo, onda expansiva y convolucion.

## Animacion de camara por keyframes, multicamara en Camaras y camara del movil

- **Keyframes:** el inspector de una toma tiene el boton "Editar animacion". Al pulsarlo se ocultan los controles y queda
  una barra de tiempo con las claves:
  - "Clave aqui" guarda la pose de la vista 3D en el cursor.
  - Las claves se arrastran para cambiar su tiempo.
  - "Llevar la vista a la clave" pone la vista 3D en esa pose para retocarla.
  - Interpolacion suave (Catmull-Rom) o lineal, y frecuencia de exportacion (5-60 poses/s, `s.fps`).
  - Datos en `shot.keys = [{t, p, q, fov}]`, en el mundo, con `preset = 'keys'`; ver `keyPose` en `camera/shots.js`.
- **Multicamara:** ahora se crea desde la pestana Camaras. Cada franja puede usar:
  - una camara propia (toma del editor con animacion de entrada);
  - una camara del escenario de la actuacion (se exporta como claves);
  - la camara de la cancion en vivo (`m<i>=2`: el juego copia su camara cada frame);
  - la camara de la cancion congelada al aparecer.
- **Camara del movil:**
  - `editor/server/vcam.py` es un puente WebSocket en el puerto del editor + 1 (8766), en la red local y con PIN. El editor
    HTTP sigue solo en 127.0.0.1.
  - App Flutter en `vcam_app/` (APK en `vcam_app/build/app/outputs/flutter-apk/app-release.apk`):
    - giroscopio integrado (girar el telefono = girar la camara);
    - mando para moverse, deslizador de altura y pellizco para el zoom;
    - el acelerometro anade el pulso real de la mano (amortiguado: vuelve solo al centro);
    - "Mantener: soltar" sirve para recolocar el telefono sin mover la camara.
  - El telefono ve el monitor (JPEG a 4-30 fps).
  - Al GRABAR, la toma elegida pasa a keyframes, la cancion suena desde su inicio y se guardan 30 claves/s hasta su
    final. Logica en `web/src/vcam/vcam.js`.

## Editor de animacion (keyframes) aislado y camara del movil con posicion real

- **Editor de animacion** (`web/src/keyframes/`):
  - Se abre con "Editar animacion" en una toma. Ocupa la linea de tiempo y el panel:
    - vista 3D para encuadrar, con la trayectoria y las claves dibujadas;
    - monitor con el resultado;
    - pistas (Camara / Posicion / Rotacion / FOV) o curvas por canal;
    - inspector numerico de la clave elegida.
  - Trabaja sobre una COPIA de la toma (`KF.doc`), con deshacer, portapapeles y atajos propios. El proyecto no se
    toca hasta "Aplicar", que deja un solo paso de deshacer; "Cancelar" lo deja todo igual.
  - Cada clave tiene su interpolacion hacia la siguiente (`key.ease`: curva, lineal, acelerar/frenar, mantener).
    `camera/shots.js` -> `keyPose` la usa, asi que el monitor, el .ska y el .edfx salen iguales.
  - Pilotar (P): mover la vista 3D mueve la clave en directo.
  - Simplificar: quita las claves que sobran de una grabacion del movil sin desviarse mas de 1 cm / 0,4 grados.
  - Si el movil graba con el editor abierto, graba en la copia (`VCAM_HOOKS` en `vcam/vcam.js`).
- **App del movil:**
  - ARCore (`android/.../MainActivity.kt`) da la posicion REAL del telefono en metros: caminar, agacharse o subir mueve
    la camara. Sin ARCore se usa el giroscopio.
  - Escala del movimiento, estabilizador, guias de tercios, zoom vertical y "Soltar" para recolocarse.
- **Juego:** `ReadCamRaw` (`re-wor/src/de/edfx.cpp`) recorre todos los viewports activos con el mismo criterio que el
  motor (`sub_823E8568`). Escribe en `re_wor_trace.log` una linea `[EDFX] viewports (...)` con cada camara y cual eligio.

## Interfaz v3 y captura de cara (2026-11-14)
- **Interfaz.** Tema unico oscuro tipo mesa de realizacion: grafito neutro, un solo acento ambar (cursor, seleccion,
  accion principal) y rojo solo para PROGRAMA / GRABANDO. Tipografia Inter + JetBrains Mono (timecodes), empaquetadas
  con npm (`@fontsource-variable/*`), sin depender de internet. Tokens en `web/src/css/base.css`.
- **Iconos.** Solo SVG propios en `web/src/ui/icons.jsx` (rejilla 24, trazo 1.75). Prohibido usar emojis o caracteres
  como icono. Movimientos de camara: campo `icon` de `camera/presets.js`; efectos: `camera/fx_icons.js`; los looks de
  color se muestran con una muestra del propio filtro (`FxGlyph` en `ui/camera/FxPanel.jsx`).
- **Panel derecho.** Riel vertical de secciones (`ui/components/Panel.jsx`): Inspector, Camaras, Efectos, Objetos 3D,
  Banda, Cara y voz, Vista.
- **Cara y voz.** Sustituido por la captura v2 (seccion siguiente).
- **Rig facial de WoR** (igual en los 17 modelos de banda; nombres resueltos con `ghwor_keys.txt`): b063 Jaw, b064/065 Eye
  L/R, b066/067 Eyelid_Upper, b081/082 Eyelid_Lower, b068/069 Mouth L/R, b070 Lip_Lower_Mid, b071 Lip_Upper_Mid,
  b072/073 Brow, b074 Brow_Mid, b075/076 Brow_Mid L/R, b077/078 Cheek, b079/080 Cheek_Upper, b083/084
  Lip_Lower_Corner, b085/086 Lip_Lower, b087/088 Lip_Upper_Corner, b089/090 Lip_Upper, b091 Nostrils, b092 Tongue.
  Todos cuelgan de b006 Bone_Head (los labios de abajo tambien): al abrir la mandibula hay que llevar con ella el labio
  inferior. Marco de enlace: +X izquierda del personaje, +Y arriba, +Z delante, metros. Mapa en `FACE_TABLE` (`web/src/rig/faceRig.js`).

## Captura v2: cuerpo, cabeza y rostro al juego (2026-11-15)
- **App del movil** (`vcam_app`), por modulos: `lib/app` tema, `lib/core` cuaterniones y formato, `lib/net/editor_link.dart`
  conexion compartida, `lib/native` puentes, `lib/pages` (conexion, inicio con riel de modos, `camera/` camara virtual,
  `capture/` captura), `lib/widgets` piezas y dibujos. Android: `MainActivity.kt` solo registra canales;
  `ArPoseTracker.kt` (ARCore, camara virtual), `CaptureEngine.kt` (CameraX + MediaPipe Tasks), `OneEuro.kt` (filtro).
- **Modos.** *Cuerpo*: MediaPipe Pose (33 puntos 3D en metros, modelo full o heavy en precision alta), sin cara.
  *Cabeza y rostro*: Face Landmarker, 52 blendshapes ARKit + giro de cabeza (matriz de transformacion facial).
  *Rostro*: solo blendshapes, 1280x720, para detalle (la cabeza la pone la animacion del juego). Camara frontal o
  trasera, GPU con caida a CPU, filtro One Euro en todos los canales (suavizado ajustable), puntos tapados se congelan.
- **Modelos.** `face_landmarker.task`, `pose_landmarker_full/heavy.task` (Google, Apache 2.0) NO van en git: la tarea
  `downloadModels` de `android/app/build.gradle.kts` los baja a `src/main/assets` si faltan. Se empaquetan sin comprimir.
- **Protocolo.** movil -> editor `{type:'cap', mode, ok, bs, q, lm:[x,y,z,v]*33}`, `capmode`, `caprec`, `capneutral`;
  el editor devuelve `capRec` dentro de `state`. Editor: `web/src/capture/` (estado, calibracion, tomas por tramos,
  exportacion) y `web/src/rig/` (`faceRig.js`, `bodyRig.js`, `apply.js` vista previa y horneado).
- **Datos.** `proyecto.capture.face.frames [{t, v:[51 blendshapes + yaw, pitch, roll]}]`,
  `proyecto.capture.body.frames [{t, p:[x,y,z]*33}]`, cada uno con `neutral` y `cfg` (rol, ganancias, espejo).
- **Exportacion al compilar** (`capture/export.js`): cada toma se hornea a 30 fps en tramos de <=10 s y
  `POST /api/anim/make` (`editor/song/animgen.py`) escribe `.ska` parciales con el formato de WoR (flags 0x960B5000,
  mascara de huesos, cuaterniones i16 conjugados, w>=0, traslaciones como delta del enlace; verificado byte a byte
  contra anims faciales nativas).
  - Cara: huesos 63-92 (+ cuello/cabeza en modo cabeza) -> eventos `Band_PlayFacialAnim {name, anim}` en
    `<cancion>_facial` (se crea el array si la cancion no lo tiene); los .ska van dentro del PAK de la cancion.
  - Cuerpo: huesos 0-15, 18, 25, 26, 93, 94, 126, 127 (rodillas y tobillos los resuelve el juego por IK desde 93/94)
    -> clip `characters[{name, anim, startframe, endframe, ik...}]` + evento `Band_PlayClip`.
- **Verificado:** ida y vuelta rig -> .ska -> lectura: cara 0.005 mm, cuerpo 0.094 mm (con IK). Pendiente: probar en
  el telefono y en el juego.

## Editor de animacion v2, retoques de pose y seleccion multiple (2026-11-15)

- **Arreglo de la exportacion de camaras**: el editor de keyframes trabaja sobre una copia; antes, si no se pulsaba
  *Aplicar*, Guardar y Compilar usaban la version anterior (la toma quedaba sin claves y "no se exportaba nada").
  Ahora Guardar y Compilar aplican primero lo que este abierto, y la escritura busca la toma por id en el proyecto
  actual (deshacer del proyecto lo sustituye por una copia). El dialogo de compilacion cuenta las tomas exportadas de
  verdad y avisa de las que estan en escenas apagadas.
- **Transiciones** (`keyframes/easing.js`): lineal, suave, mantener, escalones, Bezier editable (dos tiradores y
  presets) y 10 familias (sine, quad, cubic, quart, quint, expo, circ, back, elastic, bounce) en entrada, salida,
  ambas y salida-entrada. Por clave, con miniatura de la curva. En camaras ademas se elige el camino: curvo
  (Catmull-Rom) o recto, independiente del ritmo.
- **Gizmo 3D** (`keyframes/gizmo3d.js`, estilo Blender/Unity): clic en un cono de la camara (o en una articulacion
  del musico) y moverlo/girarlo en la vista. G mover, R girar, L ejes mundo/propios, I clave automatica.
  En el panel: valores numericos, etiquetas arrastrables (Mayus = fino), operaciones sobre varias claves (desplazar,
  girar, escalar tiempo, invertir, repartir, igualar).
- **Retoques de pose** (`rig/poseLayer.js`, `rig/poseExport.js`): pista *Retoques de pose* en cada musico. Las claves
  son giros extra por hueso sobre lo que ya hace el musico (loop, clip o captura), con fundido. Al compilar se hornea
  la pose final en clips del juego (`es_pose_*`, ska parcial a 30 fps) y se quita al musico de los clips que se
  solapan (las camaras de esos clips se conservan).
- **Seleccion multiple** (`core/selection.js`): Ctrl/Mayus + clic, Mayus + arrastrar (recuadro por varias pistas),
  Ctrl+A, arrastrar el grupo, Alt+flechas, Ctrl+C/V/D, Supr. Inspector de grupo: encadenar, repartir, al cursor,
  igualar duracion.
- Con el editor de keyframes abierto, sus atajos mandan; el global solo conserva Ctrl+S.
- `?debug` expone `window.__ES` (estado, kf, shots, pose, poseExport, sel, edit, easing) para pruebas sin ventana.

## App del movil: deteccion, 16 KB y personaje 3D (2026-11-15)

- **Deteccion**: el modelo de MediaPipe se carga en el hilo de la camara (antes congelaba la interfaz: "Skipped 276
  frames"); la imagen se gira a mano antes de detectar (como en los ejemplos oficiales); umbrales 0,4. MediaPipe usa
  la CPU por defecto: en GPU Mali (p. ej. Redmi Note 10S) el delegado GPU detecta mal sin dar error (los avisos
  `Gralloc4 ... format 56/59` del log). Opcion "Acelerar con la GPU" en la pantalla de captura; el chip de fps dice
  cual se usa.
- **16 KB**: CameraX 1.6.2 y MediaPipe 0.10.35 (sus .so ya vienen alineados), librerias nativas sin comprimir
  (`useLegacyPackaging = false`). Comprobado en el APK arm64: los 9 .so alineados (segmentos y posicion en el zip).
  `libVkLayer_khronos_validation.so` solo esta en los APK de depuracion. Instalar el de release:
  `flutter build apk --release --split-per-abi` -> `app-arm64-v8a-release.apk`.
- **Personaje 3D** en la captura: WebView con three.js y el MISMO rig que el editor (`web/preview/main.js` usa
  `rig/bodyRig.js` y `rig/faceRig.js`), asi que lo que se ve en el telefono es lo que se exporta. Arrastrar gira la
  vista; la miniatura muestra los puntos detectados (tocarla los pone en grande). Generar antes de compilar la app:
  `python -m editor.band.phone_model` (desde DEStudio): elige el personaje vestido mas ligero de `cache/band`,
  quita instrumentos, incrusta las texturas a 512 px y compila `preview.js`. Ni el .glb ni preview.js van a git.
- **Captura que moria a 0 fps (arreglado)**: MediaPipe en vivo descarta fotogramas sin llamar al resultado; la marca
  "fotograma en curso" caducaba nunca y la deteccion se paraba tras el primer resultado. Ahora caduca a los 400 ms.
  Ademas: cada `start` devuelve `ok:<sesion>` y la pantalla que se cierra solo para la suya (antes la de cabeza
  apagaba la de cuerpo recien abierta); al volver a la app se reanuda sola (`onPause`/`onResume`); un vigilante
  manda fps reales y "no detecto" cada medio segundo.

## Captura fluida, camara del PC y movimiento mas natural (2026-11-15)

- **Fluidez**: `capture/live.js` interpola las muestras en vivo (reproduce ~1,25 intervalos en el pasado). La vista
  del editor, la vista previa del movil y la grabacion van a 60/30 por segundo aunque la deteccion de menos. La
  grabacion rellena todos los huecos de 33 ms (comprobado: 84 fotogramas en 2,7 s, cada 33 ms exactos, sin repetidos,
  con solo 11 detecciones/s) y descuenta el retraso para que la toma caiga en su sitio de la cancion.
- **Movil mas rapido**: analisis a 640x480 (720p solo con "Precision alta"), 2 fotogramas en MediaPipe a la vez
  (conversion solapada con la deteccion) y la interfaz se repinta como mucho 20 veces/s.
- **Camara del PC** (seccion Captura, tarjeta "Camara del PC"): lista las camaras, elige modo (cuerpo / cabeza y rostro
  / rostro) y corre MediaPipe en el navegador con la GPU (`capture/webcam.js`). Mismos mensajes que el movil: calibrar,
  grabar y exportar igual. Con ella encendida se ignora el movil. El WASM va en `dist/mediapipe` (copiado al compilar)
  y los modelos los sirve `/api/mp/model` (los de la app si existen; si no, se descargan una vez a `cache/models`).
  Hay que reiniciar el editor para que cargue la ruta nueva.
- **Movimiento** (`rig/bodyRig.js`, compartido por editor, exportacion y movil): cada brazo y pierna pesa segun la
  visibilidad de sus puntos (lo que no se ve vuelve suave al reposo), la pelvis sin caderas visibles solo sigue el
  giro de los hombros, la rodilla no se dobla hacia atras y los pies solo se apoyan en el suelo si se ven. Las tomas
  de cuerpo guardan ahora la visibilidad (x, y, z, vis); las antiguas siguen funcionando.
- **Cara sin deformar** (`rig/faceRig.js`, igual en editor, exportacion y movil): curva por canal (zona muerta y
  compresion de los valores altos), los canales que MediaPipe dispara juntos ya no se suman dos veces (sonrisa con
  estirar/hoyuelo/labio; mandibula con labio inferior), simetria parcial de la boca y tope suave por hueso con las
  amplitudes del juego. Sonrisa fuerte: comisura de 22 a 10 mm; boca abierta: mandibula de 24 a ~18 grados.
  Ajustes nuevos en "Rostro en el personaje": Contencion (0 = como antes) y Simetria de la boca.

## Dedos, micro y pie del cantante (2026-11-15)

- **Dedos**: en modo cuerpo se detectan tambien las manos (MediaPipe Hand Landmarker; camara del PC y app, opcion
  "Manos y dedos"). `rig/handRig.js` orienta la palma y las 3 falanges de cada dedo (huesos 33-62 del juego) hacia
  los 21 puntos de cada mano, con tope de flexion. Sin mano a la vista los dedos se quedan como estaban.
- **Formato**: el fotograma de cuerpo pasa a 262 valores (33x4 cuerpo + 2 manos x 64 + 2 de agarre del micro; ver
  `capture/channels.js`). Las tomas antiguas siguen valiendo.
- **Micro** (`rig/micRig.js`): en WoR el micro cuelga de huesos del cantante (122 pie -> 123 alto del pie -> 124 ->
  125 micro -> 126/127 manos). Medido en el juego: en la mano, el micro va con un agarre fijo sobre la palma
  (~7 cm) y el pie se queda donde estaba. La camara no reconoce el micro como objeto; se deduce de las manos: mano
  cerrada junto a la boca 1/4 s = lo coge; abierta 0.3 s = lo suelta (sin dedos: muneca en la boca / lejos 1.5 s).
  El pie se pone en el suelo delante de la boca del personaje, a su altura. "Rostro/Cuerpo en el personaje" ->
  Microfono: automatico, siempre en una mano, siempre en el pie, o no tocar.
- **Exportacion**: los clips del cuerpo llevan los 30 huesos de dedos (si la toma tiene manos) y 122-125 con
  posicion (cantante). Comprobado en memoria: 57 huesos horneados, el micro viaja del pie a la mano y vuelve.
- Arreglado: la captura en vivo no movia al personaje en un proyecto sin calibrar ni grabar nada.

## Editor de interfaces (2026-11-16)

Un programa de diseno al estilo de Photoshop/Illustrator. Se crea desde el inicio eligiendo **2D**, **2.5D** o **3D**.

**Disposicion.** Menus (Archivo, Editar, Capa, Pantalla, Ver, Ayuda), barra de opciones de la herramienta o de la
capa elegida (fuente, tamano, color, relleno, trazo, alinear y repartir), herramientas a la izquierda, lienzo con
reglas y guias, pestanas de pantallas, linea de tiempo, y paneles a la derecha: Propiedades / Recursos arriba y
Capas / Pantallas abajo. Atajos en Ayuda > Atajos de teclado.

**Herramientas (2D y 2.5D).** Mover (V: seleccion por clic, Mayus o recuadro; 8 tiradores, giro desde las esquinas,
Alt desde el centro, Mayus proporcional; ajuste magnetico a bordes, centro, guias y otras capas), mano (H o barra
espaciadora), zoom (Z, Ctrl + rueda), texto (T: clic = texto de punto, arrastrar = parrafo; doble clic para
editarlo en el lienzo), rectangulo con esquinas redondeadas (U), elipse (E), poligono (Y), estrella (K), linea (L) y
pluma de curvas Bezier (P). Copiar, cortar, pegar y duplicar (tambien entre pantallas), agrupar, orden de apilado,
bloquear, ocultar, renombrar con doble clic, arrastrar en Capas para ordenar o meter en grupos.

**Capas.** Imagen (de los PAK del juego, PNG/JPG/WEBP propios o **SVG**, que es vectorial y se dibuja nitido a
cualquier tamano), texto (cualquier fuente: del sistema o TTF/OTF/WOFF importadas; peso, interletra, interlinea,
alineacion, mayusculas), forma (relleno de color o degradado lineal/radial, trazo, discontinuo), grupo y, en 3D,
camara. **Estilos de capa**: sombra, resplandor exterior, contorno y desenfoque.

**Fuentes.** El selector (con busqueda y muestra de cada fuente) tiene tres grupos: las **nativas de Warriors of
Rock** (las 9 `.fnt` de `pak/ui_shared`: texto de menus, titulos, letras, numeros, botones del mando...), las
importadas al proyecto y las **de este equipo** (las TTF/OTF instaladas en Windows, del sistema y del usuario).
Las nativas las decodifica `editor/ui/fonts.py` (formato documentado ahi: mapa `u16[65536]` codigo -> glifo en
0x18, glifos de 36 bytes en 0x200A8, textura al final) a `cache/ui_fonts/wor/` y el navegador dibuja con su atlas.
Al exportar, cada archivo de fuente que usa la interfaz se copia a `<build>/ui3d/fonts/<sha1>.<ext>`: el nombre sale
del contenido, asi que si ya esta no se vuelve a copiar (aunque la usen varias interfaces) y
`ui3d/<nombre>/fonts.txt` dice que archivo es cada familia. El texto estatico va horneado en PNG, asi que se ve
igual aunque falte la fuente; el archivo empaquetado es para el texto dinamico.

**Recursos.** Ventana grande (Archivo > Recursos, Ctrl+Mayus+R, o el boton de ampliar del panel): imagenes del
proyecto con miniaturas a escala, todas las fuentes y las imagenes del juego. Se pueden **soltar archivos en
cualquier parte del editor**: las imagenes y SVG se ponen en la pantalla y las fuentes se anaden al proyecto.
**Seleccion multiple** con Mayus o Ctrl tanto en el lienzo (2D y 3D) como en Capas (Mayus en Capas: rango).

**Pantallas y botones.** Una interfaz tiene varias pantallas, cada una con sus capas y sus clips (Entrada, Bucle,
Salida). Cualquier capa puede ser un **boton** (recibe el foco con el mando o el raton) con una accion: **ir a otra
pantalla**, volver, cerrar, **ejecutar un script del juego** o **cambiar una variable entera del juego**. Con el foco
se escala, se tine, se mueve o cambia de opacidad; hay sonidos del menu. Una capa puede verse solo cuando cierto boton
tiene el foco, o segun una variable del juego (`showIf`). El orden de foco con las flechas se calcula por posicion.
**Vista previa** (F5) la hace funcionar como en el juego (`uiedit/runtime.js`, misma logica que re-wor), con
registro de acciones y variables simuladas.

**En el juego.** Los tres modos los dibuja **re-wor** (`src/de/ui3d.cpp`, formato `ui3d 2` documentado en
`ui3d.hpp`). Al exportar, el navegador dibuja cada capa a PNG a la calidad del proyecto (1x, 2x o 3x;
`studio/raster.js`, lo mismo que se ve en el editor) y `editor/ui/modes.py` escribe la escena en
`<build>/ui3d/<nombre>/scene.ui3d`. Los scripts QB (`re-wor/qb/ui_<nombre>.q`, compilados con `tools/qbc.py`):
`re_ui_<nombre>_show`, `_hide`, `_hide_now`, `_goto screen = n`, `_goto_<pantalla>_<n>` y la global
`re_ui_<nombre>_active`. Con botones, mientras esta abierta se quita el foco al menu del juego y un
ContainerElement invisible recoge el mando (`_grab` / `_release`); re-wor mueve el foco, cambia de pantalla
(salida de una, entrada de la otra), ejecuta las acciones con el puente QB y acepta el raton.

| Tipo | Que cambia |
|---|---|
| **2D** | Plano, 1280 x 720 |
| **2.5D** | Cada capa tiene **profundidad** y hay una **camara 2.5D** (x, y, zoom, giro) con claves: paralaje |
| **3D** | Planos en el espacio y **camaras** con claves (posicion, giro XYZ, campo de vision) y **cortes de camara** por clip. Vista 3D con gizmo (G, R, S, F encuadrar, C ver por la camara activa) |

Codigo: `web/src/studio/` (compartido: `raster.js` dibujo de capas, `LayerCanvas.jsx`, `keys.js`, `Timeline.jsx`,
`fields.jsx`, `AssetBrowser.jsx`, `Viewport3D.js`, `files.js`), `web/src/uiedit/` (`model.js` modelo v3 con
pantallas, `view2d.js`, `Stage2D.jsx` lienzo y herramientas, `Stage3D.jsx`, `chrome.jsx` menus y barras,
`panels.jsx` paneles, `runtime.js` + `Preview.jsx` vista previa, `bake.js` imagenes para el juego, `main.jsx`,
`ue.css`), `editor/ui/` (`export.py`, `modes.py`, `gameimg.py`, `bridge.py`), `editor/server/routes_ui.py`
(proyectos, subida de PNG/JPG/SVG/fuentes, exportar, probar). Los proyectos antiguos (v1/v2) se abren solos en la
primera pantalla.

Sin probar todavia dentro del juego: la captura del mando con el ContainerElement, los nombres de sonido
`scrollup` / `scrolldown`, y el aspecto final en el juego de las capas con estilos. Pendiente: pasar la vista 3D del
editor de canciones a `studio/Viewport3D.js`.
