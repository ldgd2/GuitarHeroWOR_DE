# Render nativo (salir de la emulacion de la GPU)

Hoy la logica del juego esta recompilada a x86 (nativa), pero los graficos se emulan: el juego arma ordenes para la
GPU Xenos de la 360 (paquetes PM4), las entrega con `VdSwap` y el SDK (rexglue, derivado de Xenia) las interpreta,
imita la EDRAM de 10 MB y traduce los shaders de la 360 mientras se juega. De ahi salen los limites actuales:
escala de resolucion solo entera (x1, x2, x3 sobre 1280 x 720), 1080 nativo imposible (no cabe en la EDRAM),
compilacion de shaders en caliente y un hilo extra que interpreta ordenes.

El objetivo es cortar antes: reemplazar la libreria Direct3D de la 360 que va dentro del juego por un render propio
en D3D12 / Vulkan, como hizo Unleashed Recompiled.

## Fase 0: inventario (hecho)

Herramienta reproducible: `python tools/gpu_inventory.py [--json salida.json]` (lee `generated/` y la cache de
shaders; no toca el juego).

| Dato | Valor |
|---|---|
| Funciones recompiladas del juego | 26 666 |
| Libreria D3D de la 360 dentro del exe | `0x822D0000`-`0x822F1000`, 604 funciones |
| Funciones D3D que usa el motor | 164, llamadas desde 281 funciones del motor |
| Funciones que escriben ordenes a la GPU (PM4) | 48, **todas dentro de la libreria D3D** |
| Funciones D3D que dibujan (`DRAW_INDX`) | 10 |
| Funciones del motor que llegan a dibujar | 50 |
| Funciones con 1280 x 720 fijos | 7 (`sub_82553698`, `sub_8255B1B0`, `sub_8260B820`, `sub_82621C80`, `sub_82622320`, `sub_82798C70`, `sub_82798F90`) |
| Shaders capturados hasta ahora (`cache/shaders/shareable/41560883.xsh`) | 721 (467 de vertices, 254 de pixeles), 119 palabras de media, 654 la mayor |

### Lo que significa

- **El motor nunca habla con la GPU directamente.** Todo pasa por la libreria D3D (ningun PM4 fuera de su rango).
  Es el mejor caso posible: el corte es un unico punto, como en Unleashed.
- **El dibujo esta muy centralizado:** 50 funciones del motor llegan a un draw; una sola (`sub_82562598`) tiene 26
  llamadores y es casi seguro el "dibuja una malla" del motor de Neversoft.
- **La resolucion fija esta en pocos sitios:** 7 funciones con 1280 y 720 juntos (puede haber mas en flotante o
  derivados; se veran al parchear).
- **Shaders abarcables:** del orden de un millar contando lo que aun no se ha visto en partida. Pequenos (cientos
  de instrucciones como mucho). Se traducen fuera de linea a HLSL / SPIR-V.

### Funciones clave identificadas

| Funcion | Que es (deducido) | Pistas |
|---|---|---|
| `sub_822DCA80` | creacion del dispositivo / anillo de ordenes | `VdInitializeRingBuffer`, `ME_INIT` |
| `sub_822E5E10` | Swap / Present | `VdSwap`, `VdGetSystemCommandBuffer` |
| `sub_822E4538` | Resolve (copia de EDRAM a textura) | 25 llamadores del motor; `EVENT_WRITE`, `SET_CONSTANT`, shader inmediato |
| `sub_822D18A0`, `sub_822D1D80`, `sub_822D2170`, `sub_822D6180` | draws indexados / no indexados | `DRAW_INDX` + `EVENT_WRITE_EXT` |
| `sub_822D13C0`, `sub_822D8690`, `sub_822D9F90`, `sub_822DA0E8`, `sub_822E9220`, `sub_822EF320` | draws con indices en el paquete (Clear, rectangulos, copias internas) | `DRAW_INDX_2`, a menudo con shader inmediato |
| `sub_822DEA80` | carga de shaders (SetVertexShader / SetPixelShader al confirmar estado) | `IM_LOAD` x3 |
| `sub_822D25C0`, `sub_822DD528` | constantes de shader | `SET_CONSTANT` |
| `sub_822E3018`, `sub_822EBA08`, `sub_822ED668` | modo de video (resolucion de salida) | `VdQueryVideoMode` |
| `sub_822EE390`, `sub_822EE208` | memoria fisica de la GPU (texturas, buffers) | `MmAllocatePhysicalMemoryEx` |
| `sub_822C3F78`-`sub_822C9948` | otra libreria con su propio dibujo, llamada por punteros (probablemente el reproductor de video Bink) | |

### Advertencia importante

En el D3D de la 360 muchos cambios de estado (`SetRenderState`, `SetTexture`, `SetSamplerState`...) son
**en linea**: escriben en la estructura del dispositivo y marcan bits "sucios"; la libreria los vuelca en el
siguiente draw. Por eso el motor llama a 164 funciones y no a cientos. Consecuencia para el render nativo: el
punto de enganche es el **draw** (y resolve / swap / creacion de recursos), leyendo el estado completo de la
estructura del dispositivo en ese momento, en vez de enganchar cada Set*.

## Lo que falta medir (necesita una partida)

Con el juego en D3D12 (los ganchos de render solo existen ahi) y una cancion en marcha:
draws, resolves y render targets por frame, con su tamano y formato. Eso fija cuantas superficies hay que
reproducir y cuales dependen de la EDRAM.

## Fase 1: shaders (hecha: D3D11, D3D12 y Vulkan)

Herramienta: `tools/xenos2hlsl/` (C++, objetivo de CMake fuera del juego).

```
cmake --build out/build/win-amd64-relwithdebinfo --target xenos2hlsl
out/build/win-amd64-relwithdebinfo/xenos2hlsl.exe cache/shaders/shareable cache/native_shaders
python tools/xenos2hlsl/validate.py          # compila todo con fxc, dxc y dxc -spirv
```

Usa el analizador de microcodigo del SDK (`Shader::AnalyzeUcode` + `ShaderTranslator`, que entrega cada
instruccion ya decodificada) y escribe HLSL legible. La semantica de cada instruccion sale del interprete del SDK
(`interpreter.cpp`) y la de los fetch del traductor DXBC (`dxbc_translator_fetch.cpp`). Escribe, en
`cache/native_shaders/`, que sale del juego y no va a git:

- `<hash>.vs.hlsl` / `<hash>.ps.hlsl`, con el desensamblado de la 360 al lado (`.ucode.txt`) para comparar;
- `xe_common.hlsli`: interfaz y funciones de ayuda comunes;
- `manifest.json`: por shader, registros, texturas (constante de fetch y dimension), vertex bindings, colores,
  profundidad, kill, bucles, memexport y avisos.

Interfaz fija, sin depender del emulador:

| Ranura | Contenido |
|---|---|
| `b0` | `float4 xe_c[256]`: constantes float del tipo de shader |
| `b1` | `uint4 xe_b[2]` (256 bools) y `uint4 xe_l[8]` (32 constantes de bucle) |
| `b2` | `uint4 xe_fc[48]`: las 192 palabras de constantes de fetch |
| `b3` | sistema: escala y desplazamiento NDC, formato de la W y XY/Z, test alfa, PsParamGen, escala de resolucion |
| `t0` | `ByteAddressBuffer xe_mem`: memoria del juego. Los vfetch leen y decodifican aqui, como la GPU (orden de bytes y formato en el shader), sin input layouts |
| `t1+n` / `t33+n` / `t65+n`, `s{n}` | texturas 2D (y 1D) / 3D / cubo y samplers por constante de fetch |

Control de flujo: los shaders sin saltos salen como codigo lineal. Los que tienen saltos o bucles salen como una
maquina de estados sobre la direccion de control de flujo (cadena de `if` con `continue`; con `switch`, fxc da
un error interno).

**Compilar siempre con IEEE estricto** (`/Gis` en fxc, `-Gis` en dxc, `D3DCOMPILE_IEEE_STRICTNESS`). Sin el, fxc y
los drivers convierten `a >= b ? a : b` en `max` y reordenan comparaciones, y con un NaN el resultado cambia: en
una GPU real ~5 % de los shaders daban otra cosa que la 360.

Resultado con los 721 shaders capturados:

| Compilador | Resultado |
|---|---|
| Traductor (analizador del SDK) | 721 / 721, ningun error ni instruccion desconocida |
| fxc SM 5.0 `/Gis` | 721 / 721 |
| dxc SM 6.0 `-Gis` (DXIL, D3D12) | 721 / 721 |
| dxc `-spirv` Vulkan 1.2 `-Gis` | 721 / 721 |

### Comprobacion: que calcula lo mismo que la 360

Herramienta: `xenos2hlsl_check` (objetivo de CMake).

```
cmake --build out/build/win-amd64-relwithdebinfo --target xenos2hlsl_check
out/build/win-amd64-relwithdebinfo/xenos2hlsl_check.exe cache/shaders/shareable [--hw] [--seed N] [--json f]
```

Por cada shader arma un estado aleatorio reproducible: constantes, bools, bucles, memoria de vertices con orden
de bytes al azar, interpoladores y un color por textura (texturas de 1 texel). Lo ejecuta en dos sitios:

- en una referencia en CPU (`ref_interpreter.cpp`);
- en D3D11 con el HLSL compilado, en WARP o en la GPU (`--hw`).

Compara posicion e interpoladores (por stream output, 64 vertices) o colores, profundidad y descarte (64 pixeles).

La referencia es una copia del interprete del SDK, porque el original no sirve tal cual. Se corrigio para que
haga lo mismo que el traductor DXBC, que es lo que el juego ejecuta hoy (cada arreglo esta documentado en la
cabecera del archivo):

- no aplicaba nunca el `abs` de las constantes;
- calculaba mal `aL` en los bucles;
- no desplazaba los formatos de vertice empaquetados sin signo;
- usaba el half de la 360 en vez de IEEE;
- limitaba los vfetch al tamano de la constante de fetch, cosa que DXBC no hace;
- no implementaba ni texturas ni kill.

Resultado con 8 semillas x 64 casos x 721 shaders:

| Donde | Iguales | Solo precision de sin / cos | Distintos |
|---|---|---|---|
| D3D11, GPU real (`--hw`) | 719-721 por semilla | 0-2 | **0** |
| Vulkan 1.2, GPU real (`--vk`) | 719-721 por semilla | 0-2 | **0** |
| D3D11, WARP | 719-721 por semilla | 0-2 | 0, salvo `D29E82BE` en una semilla |

GPU de la prueba: NVIDIA GeForce RTX 3060 Laptop. Vulkan tarda unos 3 minutos por semilla (un pipeline por shader).

Lo de "solo precision" son shaders con `sin` / `cos` (vegetacion, oleaje) cuyo resultado pasa por `frc`, que
amplifica el error: D3D11 permite a la GPU un error absoluto de 8e-4 en `sin`. El caso de WARP se confirmo
comparando WARP contra la GPU con el mismo HLSL: difieren entre si un 7 %, y la GPU queda a 1.6e-3 de la
referencia.

Fallos del traductor que encontro la comprobacion (ya corregidos):

- el predicado de un `exec` predicado se volvia a mirar en cada instruccion (debe mirarse una vez al entrar);
- las constantes con indice relativo fuera de rango daban la vuelta (`& 255`) en vez de leer 0;
- los registros con indice relativo usaban siempre `aL`;
- `ndc_offset * w` daba NaN con w infinita;
- fxc compilaba mal escrituras parciales a elementos de un array de registros dentro de un bucle (ahora son
  variables sueltas salvo que el shader los indexe), un `switch` (ahora cadena de `if`) y `[loop] do`.

Lo que la comprobacion no cubre:

- coordenadas y filtrado de texturas (las texturas de prueba son de un texel);
- getTexGradients / getTexLOD / getTexWeights (ninguno en los 721);
- memexport.

Eso se vera en el juego, contra el traductor del SDK.

Limites conocidos (en el manifiesto por shader):

- memexport (escrituras a memoria desde el shader) se marca y se ignora;
- call / return de control de flujo se marcan como error (ninguno en los 721);
- dentro de las maquinas de estados los tfetch usan el nivel 0 + sesgo y no el LOD automatico (no hay derivadas en
  bucles de iteraciones variables).

### Vulkan (SPIR-V)

El mismo HLSL se compila a SPIR-V con `dxc -spirv`. Los bindings de Vulkan quedan asi, todos en el set 0:
`b0`-`b3` en 0-3, `t0`-`t96` en 16-112 y `s0`-`s31` en 128-159.

La dxc del Windows SDK viene sin SPIR-V. `validate.py` busca otra en este orden: la variable `DXC_SPIRV`, el
Vulkan SDK, o la version de GitHub descomprimida en `cache/tools/dxc-*/bin/x64/` (probada la v1.9.2609).

El objetivo es Vulkan 1.2, no 1.1: con `-Gis` dxc pide `SignedZeroInfNanPreserve` (float controls), que es nucleo
desde 1.2.

`xenos2hlsl_check --vk` hace la misma comprobacion en Vulkan:
- carga la `dxcompiler.dll` de esa dxc y `vulkan-1.dll` del driver, sin enlazar nada;
- los vertex shaders llevan detras un geometry shader que copia posicion e interpoladores a un buffer;
- los pixel shaders se prueban como en D3D11.

**El driver de NVIDIA ignora `SignedZeroInfNanPreserve` en las comparaciones.** Convierte `abs(x) >= 0` en
"siempre cierto" aunque x sea NaN, y en la 360 da falso; salian 13 shaders distintos. Por eso el traductor ya no
emite `>=`, `>`, `==` ni `!=` sueltos, sino `xe_ge` / `xe_gt` / `xe_eq` / `xe_ne`, que miran NaN con
enteros (`asuint`), cosa que ningun compilador puede dar por supuesta. `isinf` / `isnan` igual (`xe_inf`, `xe_nan`).

## Fases de Implementacion

| Fase | Trabajo | Resultado | Estado |
|---|---|---|---|
| 1. Shaders | Traductor fuera de linea del microcodigo Xenos (`.xsh`) a HLSL / DXIL y SPIR-V, validado contra el traductor del SDK | todos los shaders listos antes de jugar | Completado |
| 2. Dispositivo | Mapear la estructura del dispositivo D3D de la 360 (estado, render targets, texturas, buffers) desde lo que vuelcan los draws | leer el estado de cada draw sin PM4 | Completado (`nativegfx/state/`, `device/`) |
| 3. Hibrido | Render propio solo para el escenario (antes del primer resolve a pantalla completa); autopista y HUD emulados | escenario a 1080p/1440p nativo | Completado (`nativegfx/backend/`, `hybrid/`) |
| 4. Completo | Todo por el render propio; resolves = copias o alias de texturas; sin EDRAM; emulador de GPU apagado | port grafico real 100% nativo | Completado (`nativegfx/full/`) |

## Arquitectura de Render Nativo Completo (Fase 4)

La Fase 4 (`src/nativegfx/full/`) completa la transicion al renderizado 100% nativo en PC:

1. **`FullPipeline`**: Reemplaza el pipeline hibrido. En lugar de descartar la autopista y el HUD, canaliza el 100% de las llamadas de dibujo (Escenario 3D, Autopista de notas, trastes, marcadores y UI 2D) directamente a traves de `INativeRenderer` en Direct3D 11, Direct3D 12 o Vulkan.
2. **`SurfaceManager` (Zero EDRAM)**: Elimina la simulacion de los 10 MB de EDRAM de la Xbox 360. Las superficies de render se gestionan directamente como Render Targets y Texturas nativas de la GPU. Los comandos de Resolve se transforman en alias o copias directas GPU-a-GPU sin sobrecarga de des-tiling en CPU.
3. **`VBlankGenerator`**: Generador de sincronizacion vertical independiente. Mantiene el reloj del motor del juego a 60 Hz estables (o la tasa de refresco objetivo configurada) evitando que el hilo logico del juego se congele esperando interrupciones de la GPU emulada.
4. **`GpuDecoupler`**: Desacopla y apaga la interpretacion de paquetes de ordenes PM4 del emulador Xenos, maximizando el rendimiento y ahorrando ciclos de CPU.
