"""Lanzador: ajustes de pantalla, graficos, controles y rendimiento, y arranque del juego.

Donde va cada ajuste:
  * Graficos / pantalla (variables del SDK): se pasan por LINEA DE COMANDOS al lanzar. El SDK lee su
    archivo "<exe>.toml" con un fallo (guarda en secciones [GPU] pero las relee como GPU_vsync), asi
    que no se depende de el; la linea de comandos ademas tiene prioridad sobre ese archivo.
    Se guardan en tools/wormod/settings.json ("launch").
  * Controles, rendimiento y diagnostico (del juego): re-wor/re_wor_de.toml, que lee el juego al
    arrancar ([input], [performance], [dev]). Se reescribe conservando el resto (mods, etc.).

Bloqueados a proposito: vsync = on y refresco del modo de video = 60 Hz. El juego cuenta el tiempo en
"vblanks" de la consola: probado a 120 y 144 Hz sigue a 60 FPS (no gana nada) y sin vsync el vblank va
a 1000 Hz. Desbloquear FPS requiere parchear el limitador del motor, no estos valores.
"""
from __future__ import annotations

import re
import subprocess
import tomllib
from dataclasses import asdict, dataclass, fields
from pathlib import Path

from workspace import Workspace

GAME_EXE = "Guitar Hero Warriors of Rock.exe"
BUILD_DIR = Path("out") / "build" / "win-amd64-relwithdebinfo"

# ------------------------------------------------------------------ ajustes de lanzamiento
DISPLAY_MODES = {"borderless": "Pantalla completa (sin bordes)",
                 "exclusive": "Pantalla completa exclusiva",
                 "window": "Ventana"}
EFFECTS = {"bilinear": "Bilineal (original)", "cas": "AMD CAS (nitidez)", "fsr": "AMD FSR 1 (escalado + nitidez)"}
ANISO = {-1: "Lo que pida el juego", 0: "Desactivado", 1: "1x", 2: "2x", 3: "4x", 4: "8x", 5: "16x"}


@dataclass
class LaunchSettings:
    display: str = "borderless"
    window_width: int = 1600
    window_height: int = 900
    monitor: int = 0                    # 0 = el que elija Windows
    resolution_scale: int = 1           # 1..4: resolucion interna de render (1 = 720p de la consola)
    effect: str = "bilinear"
    sharpness: float = 0.0              # CAS: nitidez adicional 0..1
    anisotropic: int = 3
    async_shaders: bool = True          # menos tirones al compilar sombreadores
    letterbox: bool = True
    vibration: bool = True
    mute: bool = False

    @classmethod
    def load(cls, ws: Workspace) -> "LaunchSettings":
        raw = ws.settings().get("launch", {})
        known = {f.name for f in fields(cls)}
        s = cls(**{k: v for k, v in raw.items() if k in known})
        s.clamp()
        return s

    def save(self, ws: Workspace) -> None:
        self.clamp()
        data = ws.settings()
        data["launch"] = asdict(self)
        ws.save_settings(data)

    def clamp(self) -> None:
        self.display = self.display if self.display in DISPLAY_MODES else "borderless"
        self.effect = self.effect if self.effect in EFFECTS else "bilinear"
        self.resolution_scale = min(4, max(1, int(self.resolution_scale)))
        self.anisotropic = min(5, max(-1, int(self.anisotropic)))
        self.window_width = min(8192, max(640, int(self.window_width)))
        self.window_height = min(8192, max(480, int(self.window_height)))
        self.monitor = min(16, max(0, int(self.monitor)))
        self.sharpness = min(1.0, max(0.0, float(self.sharpness)))

    def args(self) -> list[str]:
        """Argumentos para el ejecutable (nombres = variables del SDK)."""
        a = []

        def flag(name: str, on: bool) -> None:
            a.append(f"--{name}" if on else f"--no-{name}")

        flag("fullscreen", self.display != "window")
        flag("fullscreen_exclusive", self.display == "exclusive")
        if self.display == "window":
            a += [f"--window_width={self.window_width}", f"--window_height={self.window_height}"]
        a.append(f"--monitor={self.monitor}")
        a.append(f"--resolution_scale={self.resolution_scale}")
        a.append(f"--present_effect={self.effect}")
        if self.effect == "cas":
            a.append(f"--present_cas_additional_sharpness={self.sharpness:.2f}")
        a.append(f"--anisotropic_override={self.anisotropic}")
        flag("async_shader_compilation", self.async_shaders)
        flag("present_letterbox", self.letterbox)
        flag("vibration", self.vibration)
        flag("audio_mute", self.mute)
        # Bloqueados (ver docstring): el ritmo del juego depende de ellos.
        flag("vsync", True)
        a.append("--video_mode_refresh_rate=60")
        return a


# ------------------------------------------------------------------ re_wor_de.toml
ACTIONS = [("green", "Traste verde"), ("red", "Traste rojo"), ("yellow", "Traste amarillo"),
           ("blue", "Traste azul"), ("orange", "Traste naranja"), ("strum_up", "Rasgueo arriba"),
           ("strum_down", "Rasgueo abajo"), ("start", "Start / Pausa"), ("star_power", "Star Power"),
           ("whammy", "Palanca (whammy)"), ("tilt", "Inclinacion (tilt)"), ("nav_left", "Menu izquierda"),
           ("nav_right", "Menu derecha")]
DEFAULT_KEYS = {"green": "A,1,F1", "red": "S,2,F2,Escape", "yellow": "J,3,F3", "blue": "K,4,F4",
                "orange": "L,5,F5",
                "strum_up": "Up", "strum_down": "Down", "start": "Return", "star_power": "Space,RShift",
                "whammy": "RControl,Semicolon", "tilt": "Backspace", "nav_left": "Left", "nav_right": "Right"}
GUITAR_SUBTYPES = {6: "Guitarra (6)", 7: "Guitarra alternativa GH (7)", 11: "Bajo (11)"}

# Mismos nombres que acepta el juego (src/input/key_names.cpp)
NAMED_KEYS = ["Backspace", "Tab", "Return", "Enter", "Shift", "Control", "Ctrl", "Alt", "Pause",
              "CapsLock", "Escape", "Esc", "Space", "PageUp", "PageDown", "End", "Home", "Left", "Up",
              "Right", "Down", "Insert", "Delete"] + [f"Numpad{i}" for i in range(10)] + [
              "Multiply", "Add", "Subtract", "Decimal", "Divide", "LShift", "RShift", "LControl",
              "RControl", "LAlt", "RAlt", "Semicolon", "Equals", "Comma", "Minus", "Period", "Slash",
              "Backtick", "LBracket", "Backslash", "RBracket", "Quote",
              "Oem102", "NumLock", "ScrollLock", "PrintScreen", "Apps", "Clear", "Separator",
              "NumpadEquals", "LWin", "RWin"]
_NAMED_LOWER = {k.lower(): k for k in NAMED_KEYS}

# Tecla de Tk (event.keysym) -> nombre del juego
TK_KEYS = {"BackSpace": "Backspace", "Tab": "Tab", "Return": "Return", "KP_Enter": "Return",
           "Escape": "Escape", "space": "Space", "Prior": "PageUp", "Next": "PageDown", "End": "End",
           "Home": "Home", "Left": "Left", "Up": "Up", "Right": "Right", "Down": "Down",
           "Insert": "Insert", "Delete": "Delete", "KP_Multiply": "Multiply", "KP_Add": "Add",
           "KP_Subtract": "Subtract", "KP_Decimal": "Decimal", "KP_Divide": "Divide",
           "Shift_L": "LShift", "Shift_R": "RShift", "Control_L": "LControl", "Control_R": "RControl",
           "Alt_L": "LAlt", "Alt_R": "RAlt", "semicolon": "Semicolon", "equal": "Equals",
           "comma": "Comma", "minus": "Minus", "period": "Period", "slash": "Slash", "grave": "Backtick",
           "bracketleft": "LBracket", "backslash": "Backslash", "bracketright": "RBracket",
           "apostrophe": "Quote", "Caps_Lock": "CapsLock", "Pause": "Pause"}
TK_KEYS.update({f"KP_{i}": f"Numpad{i}" for i in range(10)})


# Posicion fisica (scancode set 1, teclado US) -> nombre del juego. El juego (SDK sobre SDL) lee
# las teclas por posicion, no por idioma: en un teclado espanol la "n con tilde" es la tecla de
# Semicolon y la "< >" es Oem102. Asi lo que se captura aqui coincide siempre con lo que ve el juego.
_SCAN_NAMES = {0x01: "Escape", 0x0C: "Minus", 0x0D: "Equals", 0x0E: "Backspace", 0x0F: "Tab",
               0x1A: "LBracket", 0x1B: "RBracket", 0x1C: "Return", 0x27: "Semicolon", 0x28: "Quote",
               0x29: "Backtick", 0x2B: "Backslash", 0x33: "Comma", 0x34: "Period", 0x35: "Slash",
               0x39: "Space", 0x3A: "CapsLock", 0x56: "Oem102"}
_SCAN_NAMES.update({0x02 + i: str((i + 1) % 10) for i in range(10)})          # 1..9, 0
_SCAN_NAMES.update({0x10 + i: c for i, c in enumerate("QWERTYUIOP")})
_SCAN_NAMES.update({0x1E + i: c for i, c in enumerate("ASDFGHJKL")})
_SCAN_NAMES.update({0x2C + i: c for i, c in enumerate("ZXCVBNM")})
# Teclas que dependen del idioma (letras, numeros de arriba, simbolos): se resuelven por posicion.
_LAYOUT_VKS = set(range(0x30, 0x3A)) | set(range(0x41, 0x5B)) | set(range(0xBA, 0xC1)) | \
    set(range(0xDB, 0xE0)) | {0xE2}
# Teclas cuyo codigo de Windows ya dice exactamente cual es.
_VK_NAMES = {0x08: "Backspace", 0x09: "Tab", 0x0C: "Clear", 0x0D: "Return", 0x13: "Pause",
             0x14: "CapsLock", 0x1B: "Escape", 0x20: "Space", 0x21: "PageUp", 0x22: "PageDown",
             0x23: "End", 0x24: "Home", 0x25: "Left", 0x26: "Up", 0x27: "Right", 0x28: "Down",
             0x2C: "PrintScreen", 0x2D: "Insert", 0x2E: "Delete", 0x5B: "LWin", 0x5C: "RWin",
             0x5D: "Apps", 0x6A: "Multiply", 0x6B: "Add", 0x6C: "Separator", 0x6D: "Subtract",
             0x6E: "Decimal", 0x6F: "Divide", 0x90: "NumLock", 0x91: "ScrollLock", 0x92: "NumpadEquals"}
_VK_NAMES.update({0x60 + i: f"Numpad{i}" for i in range(10)})
_VK_NAMES.update({0x70 + i: f"F{i + 1}" for i in range(24)})
_TK_SIDES = {"Shift_L": "LShift", "Shift_R": "RShift", "Control_L": "LControl", "Control_R": "RControl",
             "Alt_L": "LAlt", "Alt_R": "RAlt", "Win_L": "LWin", "Win_R": "RWin"}


def _scancode(vk: int) -> int:
    try:
        import ctypes
        return ctypes.windll.user32.MapVirtualKeyW(vk, 0)  # MAPVK_VK_TO_VSC, con el idioma actual
    except (AttributeError, OSError):
        return 0


def key_from_event(e) -> str | None:
    """Tecla pulsada en Tk -> nombre del juego, por posicion fisica (como la lee el juego)."""
    sym = getattr(e, "keysym", "")
    if sym in _TK_SIDES:
        return _TK_SIDES[sym]
    vk = getattr(e, "keycode", 0) or 0
    if vk in _LAYOUT_VKS:
        name = _SCAN_NAMES.get(_scancode(vk))
        if name:
            return name
    if vk in _VK_NAMES:
        return _VK_NAMES[vk]
    return key_from_tk(sym)


def key_from_tk(keysym: str) -> str | None:
    if len(keysym) == 1 and keysym.isalnum():
        return keysym.upper()
    if re.fullmatch(r"F([1-9]|1[0-9]|2[0-4])", keysym):
        return keysym
    return TK_KEYS.get(keysym)


def valid_key(name: str) -> bool:
    n = name.strip()
    if len(n) == 1 and n.isascii() and n.isalnum():
        return True
    if re.fullmatch(r"[Ff]([1-9]|1[0-9]|2[0-4])", n):
        return True
    return n.lower() in _NAMED_LOWER


def bad_keys(keylist: str) -> list[str]:
    return [k for k in (x.strip() for x in keylist.split(",")) if k and not valid_key(k)]


# ---------------------------------------------------------------- atajos del juego ([hotkeys])
# (clave en el toml, texto, tecla por defecto). Los lee re-wor al arrancar.
HOTKEYS = [("panel", "Panel Definitive Edition (mods, controles, musica)", "F8"),
           ("fullscreen", "Pantalla completa", "F11"),
           ("perf_overlay", "Overlay de rendimiento (FPS)", "F9"),
           ("settings", "Ajustes avanzados (cvars del SDK)", "F10"),
           ("console", "Consola del SDK", "Backtick"),
           ("achievements", "Logros", "F7")]
DEFAULT_HOTKEYS = {k: d for k, _, d in HOTKEYS}
# F1-F5 son de los trastes (clasico de Guitar Hero en PC): ningun atajo puede usarlas.
RESERVED_HOTKEYS = {f"f{i}" for i in range(1, 6)}
# Nombres que acepta el SDK para los atajos (rexglue-sdk/src/ui/keybinds.cpp).
SDK_KEYS = ([f"F{i}" for i in range(1, 25)] + [chr(c) for c in range(ord("A"), ord("Z") + 1)]
            + [str(i) for i in range(10)]
            + ["Backtick", "Minus", "Plus", "Comma", "Period", "Semicolon", "Slash", "Backslash", "LBracket",
               "RBracket", "Quote", "Escape", "Return", "Space", "Tab", "Backspace", "Delete", "Insert",
               "Home", "End", "PageUp", "PageDown", "Left", "Right", "Up", "Down"])
_SDK_LOWER = {k.lower(): k for k in SDK_KEYS}
_TK_TO_SDK = {"equal": "Plus", "plus": "Plus", "KP_Enter": "Return"}


def hotkey_from_tk(keysym: str) -> str | None:
    """Tecla pulsada en Tk -> nombre del SDK, o None si no sirve como atajo (Shift, Ctrl, Alt...)."""
    if keysym in _TK_TO_SDK:
        return _TK_TO_SDK[keysym]
    name = key_from_tk(keysym)
    return _SDK_LOWER.get(name.lower()) if name else None


def hotkey_from_event(e) -> str | None:
    """Igual que hotkey_from_tk pero por posicion fisica (los simbolos en teclados no US)."""
    name = key_from_event(e)
    if name == "Equals":
        name = "Plus"
    if name and name.lower() in _SDK_LOWER:
        return _SDK_LOWER[name.lower()]
    return hotkey_from_tk(getattr(e, "keysym", ""))


def valid_hotkey(name: str) -> bool:
    n = name.strip().lower()
    return n in _SDK_LOWER and n not in RESERVED_HOTKEYS


def hotkey_conflicts(gs: "GameSettings") -> list[str]:
    """Atajos repetidos entre si o que coinciden con una tecla de la guitarra de teclado."""
    out = []
    hk = gs.hotkeys or DEFAULT_HOTKEYS
    labels = {k: t for k, t, _ in HOTKEYS}
    seen: dict[str, str] = {}
    for k, _, _ in HOTKEYS:
        key = hk.get(k, "").lower()
        if key in seen:
            out.append(f"{hk[k]}: '{labels[seen[key]]}' y '{labels[k]}' usan la misma tecla")
        seen.setdefault(key, k)
    if gs.keyboard_guitar:
        names = dict(ACTIONS)
        for act, keylist in (gs.keys or DEFAULT_KEYS).items():
            for key in (x.strip().lower() for x in keylist.split(",")):
                if key in seen:
                    out.append(f"{hk[seen[key]]}: '{labels[seen[key]]}' choca con '{names.get(act, act)}' "
                               "de la guitarra de teclado")
    return out


def _q(s: str) -> str:
    return '"' + str(s).replace("\\", "\\\\").replace('"', '\\"') + '"'


def _val(v) -> str:
    if isinstance(v, bool):
        return "true" if v else "false"
    if isinstance(v, (int, float)):
        return repr(v)
    if isinstance(v, str):
        return _q(v)
    if isinstance(v, list):
        if all(isinstance(x, dict) for x in v) and v:
            return "[\n" + "".join("    { " + ", ".join(f"{k} = {_val(x[k])}" for k in x) + " },\n"
                                   for x in v) + "]"
        return "[" + ", ".join(_val(x) for x in v) + "]"
    raise TypeError(type(v))


def _dump_table(name: str, t: dict, out: list[str]) -> None:
    scalars = {k: v for k, v in t.items() if not isinstance(v, dict)}
    subs = {k: v for k, v in t.items() if isinstance(v, dict)}
    out.append(f"\n[{name}]")
    out += [f"{k} = {_val(v)}" for k, v in scalars.items()]
    for k, v in subs.items():
        _dump_table(f"{name}.{k}", v, out)


HEADER = ("# Guitar Hero: Warriors of Rock - Definitive Edition (re-wor)\n"
          "# Configuracion editable a mano, desde el lanzador (wormod) o desde el panel del juego. Atajos en [hotkeys].\n"
          "# input.backend: \"sdl\" (XInput + DirectInput + HID) o \"xinput\". Requiere reiniciar.\n"
          "# mods: el orden es la prioridad (el primero gana si dos mods tienen el mismo archivo).\n")


def read_game_config(ws: Workspace) -> dict:
    try:
        return tomllib.loads(ws.config.read_text(encoding="utf-8"))
    except (OSError, tomllib.TOMLDecodeError):
        return {}


def write_game_config(ws: Workspace, data: dict) -> None:
    out = [HEADER]
    for k, v in data.items():
        if not isinstance(v, dict):
            out.append(f"{k} = {_val(v)}")
    for k, v in data.items():
        if isinstance(v, dict):
            _dump_table(k, v, out)
    text = "\n".join(out) + "\n"
    tomllib.loads(text)  # nunca dejar un archivo que el juego no pueda leer
    ws.config.write_text(text, encoding="utf-8")


@dataclass
class GameSettings:
    """Lo que el juego lee de re_wor_de.toml."""
    backend: str = "sdl"
    keyboard_guitar: bool = True
    keyboard_gamepad: bool = False
    keyboard_guitar_subtype: int = 6
    keys: dict | None = None
    above_normal_priority: bool = True
    trace_probes: bool = False
    log_file_opens: bool = False
    show_fps: bool = False
    hotkeys: dict | None = None

    @classmethod
    def load(cls, ws: Workspace) -> "GameSettings":
        d = read_game_config(ws)
        inp, perf, dev = d.get("input", {}), d.get("performance", {}), d.get("dev", {})
        keys = dict(DEFAULT_KEYS)
        keys.update({k: str(v) for k, v in (inp.get("keys") or {}).items() if k in DEFAULT_KEYS})
        hotkeys = dict(DEFAULT_HOTKEYS)
        hotkeys.update({k: _SDK_LOWER[str(v).lower()] for k, v in (d.get("hotkeys") or {}).items()
                        if k in DEFAULT_HOTKEYS and valid_hotkey(str(v))})
        return cls(backend=str(inp.get("backend", "sdl")), keyboard_guitar=bool(inp.get("keyboard_guitar", True)),
                   keyboard_gamepad=bool(inp.get("keyboard_gamepad", False)),
                   keyboard_guitar_subtype=int(inp.get("keyboard_guitar_subtype", 6)), keys=keys,
                   above_normal_priority=bool(perf.get("above_normal_priority", True)),
                   trace_probes=bool(perf.get("trace_probes", False)),
                   log_file_opens=bool(dev.get("log_file_opens", False)), show_fps=bool(dev.get("show_fps", False)),
                   hotkeys=hotkeys)

    def save(self, ws: Workspace) -> None:
        d = read_game_config(ws)
        inp = d.setdefault("input", {})
        inp.update(backend=self.backend if self.backend in ("sdl", "xinput") else "sdl",
                   keyboard_guitar=self.keyboard_guitar, keyboard_gamepad=self.keyboard_gamepad,
                   keyboard_guitar_subtype=int(self.keyboard_guitar_subtype))
        inp["keys"] = {k: (self.keys or DEFAULT_KEYS).get(k, DEFAULT_KEYS[k]) for k in DEFAULT_KEYS}
        d.setdefault("performance", {}).update(above_normal_priority=self.above_normal_priority,
                                               trace_probes=self.trace_probes)
        d.setdefault("dev", {}).update(log_file_opens=self.log_file_opens, show_fps=self.show_fps)
        hk = self.hotkeys or DEFAULT_HOTKEYS
        d["hotkeys"] = {k: (hk.get(k) if valid_hotkey(hk.get(k, "")) else DEFAULT_HOTKEYS[k])
                        for k in DEFAULT_HOTKEYS}
        d.setdefault("mods", [])
        write_game_config(ws, d)


# ------------------------------------------------------------------ lanzar
def game_exe(ws: Workspace) -> Path:
    return ws.game / BUILD_DIR / GAME_EXE


def game_running() -> bool:
    try:
        out = subprocess.run(["tasklist", "/FI", f"IMAGENAME eq {GAME_EXE}", "/NH"], capture_output=True,
                             text=True, timeout=5,
                             creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0)).stdout
        return GAME_EXE.lower() in out.lower()
    except (OSError, subprocess.SubprocessError):
        return False


def launch(ws: Workspace, ls: LaunchSettings) -> tuple[subprocess.Popen, list[str]]:
    exe = game_exe(ws)
    if not exe.exists():
        raise FileNotFoundError(f"No esta compilado el juego: {exe} (ejecuta build.bat)")
    if game_running():
        raise RuntimeError("El juego ya esta abierto")
    args = [str(exe), "--game_data_root", str(ws.assets)] + ls.args()
    flags = getattr(subprocess, "DETACHED_PROCESS", 0) | getattr(subprocess, "CREATE_NEW_PROCESS_GROUP", 0)
    proc = subprocess.Popen(args, cwd=str(exe.parent), creationflags=flags, close_fds=True)
    return proc, args
