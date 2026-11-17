# wui

Lenguaje para programar interfaces de juego **en Python**. Se escribe Python normal (un subconjunto), con
eventos, capas, mando, tiempo y acceso al juego. wui lo compila a instrucciones de una máquina pequeña, que
corre igual en Python (pruebas y simulador), en el navegador (vista previa de DE Studio) y dentro del juego
(re-wor).

```python
from wui import *

@on_press(Button.RED)
def rojo():
    layer("red").glow(24, "#ff3040", time=0.08)

@on_hold(Button.START, seconds=1.5)
def salir():
    back()
```

## Usar

```
python -m wui check   menu.py --layers a,b --screens uno     # errores y avisos, en español
python -m wui run     menu.py --script pasos.json            # simular sin el juego
python -m wui compile menu.py                                # -> menu.wuib
python -m wui dis     menu.py                                # ver las instrucciones
python -m wui api  -o api.json                               # la librería como datos (editores)
python -m wui docs -o REFERENCIA.md                          # referencia completa
python -m unittest discover -s tests
```

Desde Python:

```python
import wui
res = wui.compile_source(texto, layers=[...], screens=[...])
vm = wui.Machine(res.program, wui.SimulatedHost([...], [...]))
vm.start()
vm.step(1 / 60, wui.Inputs.of("red"))
```

## El lenguaje

- Se puede usar: variables, `if/elif/else`, `while`, `for ... in`, `break/continue`, `def` con valores por
  defecto, `global`, listas, `a[i]`, `a[x:y]`, f-strings y desempaquetar (`a, b = 1, 2`). También se pueden
  guardar funciones en variables.
- No se puede usar (el compilador lo explica): clases, diccionarios, `lambda`, `try`, `import` (salvo
  `from wui import *`) ni funciones dentro de funciones.
- **Eventos**: un decorador conecta una función con algo que pasa. Por ejemplo `@on_press`, `@on_hold`,
  `@on_repeat`, `@on_enter`, `@on_click`, `@every(segundos)`, `@on_change("variable")` y `@on_game("$var")`.
- **Tiempo**: `wait(s)` pausa solo esa función. Cada evento corre en su propia tarea, así que las demás siguen.
  También hay `spawn`, `after`, `wait_press` y `wait_until`.
- **Errores**:
  - Al compilar, cada error trae línea, columna y una sugerencia ("quisiste decir...").
  - Al ejecutar, un error para solo esa tarea; la interfaz sigue funcionando.
  - Un bucle sin `wait()` se corta en vez de congelar el juego.

La lista completa de funciones, propiedades de capa, eventos y constantes está en
[REFERENCIA.md](REFERENCIA.md), generada del mismo registro que usa el compilador.

## Arquitectura

Está organizada por casos de uso: cada carpeta hace una cosa de principio a fin, con su petición y su resultado.

| Carpeta | Contenido |
|---|---|
| `language/` | `parse_source` → `check_program` → `compile_program` |
| `stdlib/<categoría>/` | `specs.py` (firma y documentación) + `impl.py` (comportamiento de referencia) |
| `runtime/` | `execute_program` (máquina), `read_input`, `animate_layers`, `host` (contrato + simulador) |
| `tooling/` | `describe_api`, `generate_docs`, `disassemble` |
| `shared/` | instrucciones, formato `.wuib`, valores, curvas, errores |

Para añadir una función nueva hay que tocar dos archivos de su categoría: la firma va en `specs.py` y el
comportamiento en `impl.py`. El compilador, la documentación y el autocompletado la toman de ahí solos.

Para llevar wui a otro motor basta con implementar `Host` (`runtime/host/protocol.py`) y una máquina que
siga el formato `.wuib`.
