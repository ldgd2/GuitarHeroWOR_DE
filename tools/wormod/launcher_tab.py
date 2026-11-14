"""Pestana "Jugar": pantalla, graficos, rendimiento, controles y boton de lanzar."""
from __future__ import annotations

import tkinter as tk
from tkinter import messagebox, ttk

import launcher as L

SCALE_TXT = {1: "1x  (1280x720, original)", 2: "2x  (2560x1440)", 3: "3x  (3840x2160)", 4: "4x  (5120x2880)"}
BACKENDS = {"sdl": "SDL (XInput + DirectInput + HID: guitarras USB, mandos)", "xinput": "Solo XInput"}


def _inv(d: dict, label: str, default):
    return next((k for k, v in d.items() if v == label), default)


class LauncherTab:
    def __init__(self, app, nb: ttk.Notebook):
        self.app, self.ws = app, app.ws
        self.ls = L.LaunchSettings.load(self.ws)
        self.gs = L.GameSettings.load(self.ws)
        self.proc = None

        f = ttk.Frame(nb, padding=8)
        nb.insert(0, f, text="  Jugar  ")
        nb.select(0)

        bar = ttk.Frame(f)
        bar.pack(fill="x", side="bottom", pady=(8, 0))
        self.play_btn = ttk.Button(bar, text="▶  JUGAR", command=self.play)
        self.play_btn.pack(side="right", ipadx=18, ipady=4)
        ttk.Button(bar, text="Guardar", command=self.save).pack(side="right", padx=6)
        ttk.Button(bar, text="Valores por defecto", command=self.defaults).pack(side="right")
        self.info = ttk.Label(bar, text="", foreground="#555")
        self.info.pack(side="left")

        cols = ttk.Frame(f)
        cols.pack(fill="both", expand=True)
        left, right = ttk.Frame(cols), ttk.Frame(cols)
        left.pack(side="left", fill="both", expand=True, padx=(0, 6))
        right.pack(side="left", fill="both", expand=True, padx=(6, 0))

        self.v = {}
        self._display(left)
        self._graphics(left)
        self._perf(left)
        self._controls(right)
        self._hotkeys(right)
        self.load_vars()

    # ------------------------------------------------------------------ secciones
    def _row(self, parent, r, text, widget, hint=None):
        ttk.Label(parent, text=text).grid(row=r, column=0, sticky="w", pady=2, padx=(0, 8))
        widget.grid(row=r, column=1, sticky="we", pady=2)
        if hint:
            ttk.Label(parent, text=hint, foreground="#777").grid(row=r, column=2, sticky="w", padx=6)
        parent.columnconfigure(1, weight=1)

    def _combo(self, parent, key, values, width=34):
        self.v[key] = tk.StringVar()
        return ttk.Combobox(parent, textvariable=self.v[key], values=list(values), state="readonly", width=width)

    def _check(self, parent, key, text):
        self.v[key] = tk.BooleanVar()
        return ttk.Checkbutton(parent, text=text, variable=self.v[key])

    def _display(self, p):
        g = ttk.LabelFrame(p, text="Pantalla", padding=8)
        g.pack(fill="x", pady=(0, 8))
        cb = self._combo(g, "display", L.DISPLAY_MODES.values())
        cb.bind("<<ComboboxSelected>>", lambda e: self._sync())
        self._row(g, 0, "Modo", cb)
        wf = ttk.Frame(g)
        self.v["ww"], self.v["wh"] = tk.IntVar(), tk.IntVar()
        self.ww = ttk.Spinbox(wf, from_=640, to=7680, increment=80, textvariable=self.v["ww"], width=7)
        self.wh = ttk.Spinbox(wf, from_=480, to=4320, increment=45, textvariable=self.v["wh"], width=7)
        self.ww.pack(side="left")
        ttk.Label(wf, text=" x ").pack(side="left")
        self.wh.pack(side="left")
        self._row(g, 1, "Tamano de ventana", wf, "solo en modo ventana")
        self.v["monitor"] = tk.IntVar()
        self._row(g, 2, "Monitor", ttk.Spinbox(g, from_=0, to=8, textvariable=self.v["monitor"], width=7),
                  "0 = el principal")
        self._row(g, 3, "", self._check(g, "letterbox", "Bandas negras para mantener 16:9"))

    def _graphics(self, p):
        g = ttk.LabelFrame(p, text="Graficos", padding=8)
        g.pack(fill="x", pady=(0, 8))
        self._row(g, 0, "Resolucion interna", self._combo(g, "scale", SCALE_TXT.values()),
                  "mas nitidez, mas GPU")
        cb = self._combo(g, "effect", L.EFFECTS.values())
        cb.bind("<<ComboboxSelected>>", lambda e: self._sync())
        self._row(g, 1, "Escalado final", cb)
        self.v["sharp"] = tk.DoubleVar()
        self.sharp = ttk.Scale(g, from_=0.0, to=1.0, variable=self.v["sharp"])
        self._row(g, 2, "Nitidez extra (CAS)", self.sharp)
        self._row(g, 3, "Filtro anisotropico", self._combo(g, "aniso", L.ANISO.values()))
        self._row(g, 4, "", self._check(g, "async", "Compilar sombreadores en segundo plano (menos tirones)"))
        fps = ttk.Label(g, text="60 (fijo)", font=("Segoe UI", 9, "bold"))
        self._row(g, 5, "FPS", fps, "el motor del juego no admite mas")

    def _perf(self, p):
        g = ttk.LabelFrame(p, text="Rendimiento y sonido", padding=8)
        g.pack(fill="x", pady=(0, 8))
        self._check(g, "prio", "Prioridad de proceso 'por encima de lo normal'").pack(anchor="w")
        self._check(g, "probes", "Medir FPS / audio en el registro (diagnostico)").pack(anchor="w")
        self._check(g, "opens", "Registrar cada archivo que abre el juego (lento, diagnostico)").pack(anchor="w")
        self._check(g, "vib", "Vibracion del mando").pack(anchor="w")
        self._check(g, "mute", "Silenciar audio").pack(anchor="w")
        ttk.Label(g, foreground="#777", wraplength=430, justify="left",
                  text="FPS: el motor esta atado a 60 (cuenta el tiempo en vblanks de la consola). "
                       "Subir el refresco o quitar vsync no da mas FPS: acelera o rompe el ritmo, "
                       "por eso se fijan en 60 Hz / vsync al lanzar.").pack(anchor="w", pady=(6, 0))

    def _controls(self, p):
        g = ttk.LabelFrame(p, text="Controles", padding=8)
        g.pack(fill="x")
        top = ttk.Frame(g)
        top.pack(fill="x")
        self._row(top, 0, "Sistema de entrada", self._combo(top, "backend", BACKENDS.values(), 40))
        self._row(top, 1, "", self._check(top, "kguitar", "Teclado como guitarra"))
        self._row(top, 2, "", self._check(top, "kpad", "Teclado como mando (menus)"))
        self._row(top, 3, "Tipo de guitarra", self._combo(top, "subtype", L.GUITAR_SUBTYPES.values(), 30))

        ttk.Label(g, text="Teclas (varias separadas por coma). 'Asignar' y pulsa una tecla; "
                          "Mayus+clic en 'Asignar' la anade. Sirve todo el teclado, pad numerico incluido "
                          "(con o sin Bloq Num). Los simbolos se guardan por su posicion: en teclado "
                          "espanol la n con tilde es 'Semicolon' y '< >' es 'Oem102'.",
                  foreground="#777", wraplength=440,
                  justify="left").pack(anchor="w", pady=(8, 4))
        keys = ttk.Frame(g)
        keys.pack(fill="both", expand=True)
        self.key_vars = {}
        for r, (aid, label) in enumerate(L.ACTIONS):
            ttk.Label(keys, text=label).grid(row=r, column=0, sticky="w", padx=(0, 8), pady=1)
            var = tk.StringVar()
            self.key_vars[aid] = var
            ttk.Entry(keys, textvariable=var, width=26).grid(row=r, column=1, sticky="we", pady=1)
            b = ttk.Button(keys, text="Asignar", width=8)
            b.grid(row=r, column=2, padx=(6, 0))
            b.bind("<Button-1>", lambda e, a=aid: self.capture(a, bool(e.state & 0x1)))
        keys.columnconfigure(1, weight=1)

    def _hotkeys(self, p):
        g = ttk.LabelFrame(p, text="Atajos del juego", padding=8)
        g.pack(fill="x", pady=(8, 0))
        self.hk_vars = {}
        for r, (hid, label, _default) in enumerate(L.HOTKEYS):
            ttk.Label(g, text=label).grid(row=r, column=0, sticky="w", padx=(0, 8), pady=1)
            var = tk.StringVar()
            self.hk_vars[hid] = var
            ttk.Entry(g, textvariable=var, width=12).grid(row=r, column=1, sticky="w", pady=1)
            ttk.Button(g, text="Asignar", width=8,
                       command=lambda h=hid, t=label: self.capture_hotkey(h, t)).grid(row=r, column=2, padx=(6, 0))
        ttk.Label(g, foreground="#777", wraplength=440, justify="left",
                  text="Se aplican al abrir el juego. F1-F5 estan reservadas para los trastes; "
                       "los atajos tampoco pueden coincidir entre si ni con otras teclas de la guitarra.").grid(row=len(L.HOTKEYS), column=0, columnspan=3,
                                                          sticky="w", pady=(6, 0))
        g.columnconfigure(0, weight=1)

    def capture_hotkey(self, hid: str, label: str):
        top = tk.Toplevel(self.app)
        top.title("Asignar atajo")
        top.transient(self.app)
        top.resizable(False, False)
        msg = ttk.Label(top, padding=(24, 18, 24, 8), justify="center", text="Pulsa la tecla para:\n" + label)
        msg.pack()
        ttk.Button(top, text="Cancelar", command=top.destroy, takefocus=False).pack(pady=(0, 14))

        def on_key(e):
            name = L.hotkey_from_event(e)
            if name and not L.valid_hotkey(name):
                msg.configure(text=f"{name} esta reservada para los trastes (F1-F5).\n"
                                   "Pulsa otra tecla para:\n" + label)
                return "break"
            if not name:
                msg.configure(text=f"'{e.keysym}' no sirve como atajo (Shift, Ctrl o Alt solos no).\n"
                                   "Pulsa otra tecla para:\n" + label)
                return "break"
            self.hk_vars[hid].set(name)
            top.destroy()
            return "break"

        top.bind("<Key>", on_key)
        top.grab_set()
        top.focus_force()

    # ------------------------------------------------------------------ captura de tecla
    def capture(self, action: str, append: bool):
        top = tk.Toplevel(self.app)
        top.title("Asignar tecla")
        top.transient(self.app)
        top.resizable(False, False)
        ttk.Label(top, padding=(24, 18, 24, 8), justify="center",
                  text="Pulsa la tecla para:\n" + dict(L.ACTIONS)[action]).pack()
        ttk.Button(top, text="Cancelar", command=top.destroy, takefocus=False).pack(pady=(0, 14))

        def on_key(e):
            name = L.key_from_event(e)
            if name:
                _commit(name)
            return "break"

        def _commit(name):
            var = self.key_vars[action]
            cur = [k.strip() for k in var.get().split(",") if k.strip()]
            var.set(",".join(dict.fromkeys(cur + [name])) if append else name)
            if top.winfo_exists():
                top.destroy()

        top.bind("<Key>", on_key)
        top.grab_set()
        top.focus_force()
        return "break"

    # ------------------------------------------------------------------ datos <-> controles
    def load_vars(self):
        ls, gs, v = self.ls, self.gs, self.v
        v["display"].set(L.DISPLAY_MODES[ls.display])
        v["ww"].set(ls.window_width)
        v["wh"].set(ls.window_height)
        v["monitor"].set(ls.monitor)
        v["letterbox"].set(ls.letterbox)
        v["scale"].set(SCALE_TXT[ls.resolution_scale])
        v["effect"].set(L.EFFECTS[ls.effect])
        v["sharp"].set(ls.sharpness)
        v["aniso"].set(L.ANISO[ls.anisotropic])
        v["async"].set(ls.async_shaders)
        v["vib"].set(ls.vibration)
        v["mute"].set(ls.mute)
        v["prio"].set(gs.above_normal_priority)
        v["probes"].set(gs.trace_probes)
        v["opens"].set(gs.log_file_opens)
        v["backend"].set(BACKENDS.get(gs.backend, BACKENDS["sdl"]))
        v["kguitar"].set(gs.keyboard_guitar)
        v["kpad"].set(gs.keyboard_gamepad)
        v["subtype"].set(L.GUITAR_SUBTYPES.get(gs.keyboard_guitar_subtype, L.GUITAR_SUBTYPES[6]))
        for aid, var in self.key_vars.items():
            var.set((gs.keys or L.DEFAULT_KEYS).get(aid, ""))
        for hid, var in self.hk_vars.items():
            var.set((gs.hotkeys or L.DEFAULT_HOTKEYS).get(hid, L.DEFAULT_HOTKEYS[hid]))
        self._sync()

    def _sync(self):
        win = self.v["display"].get() == L.DISPLAY_MODES["window"]
        for w in (self.ww, self.wh):
            w.configure(state="normal" if win else "disabled")
        self.sharp.configure(state="normal" if self.v["effect"].get() == L.EFFECTS["cas"] else "disabled")

    def collect(self) -> list[str]:
        """Pasa los controles a self.ls / self.gs. Devuelve errores (teclas invalidas, numeros)."""
        v, errs = self.v, []
        ls = self.ls
        ls.display = _inv(L.DISPLAY_MODES, v["display"].get(), "borderless")
        try:
            ls.window_width, ls.window_height, ls.monitor = int(v["ww"].get()), int(v["wh"].get()), int(v["monitor"].get())
        except (tk.TclError, ValueError):
            errs.append("tamano de ventana / monitor: numero no valido")
        ls.letterbox = v["letterbox"].get()
        ls.resolution_scale = _inv(SCALE_TXT, v["scale"].get(), 1)
        ls.effect = _inv(L.EFFECTS, v["effect"].get(), "bilinear")
        ls.sharpness = round(float(v["sharp"].get()), 2)
        ls.anisotropic = _inv(L.ANISO, v["aniso"].get(), 3)
        ls.async_shaders, ls.vibration, ls.mute = v["async"].get(), v["vib"].get(), v["mute"].get()
        ls.clamp()

        gs = self.gs
        gs.above_normal_priority, gs.trace_probes, gs.log_file_opens = v["prio"].get(), v["probes"].get(), v["opens"].get()
        gs.backend = _inv(BACKENDS, v["backend"].get(), "sdl")
        gs.keyboard_guitar, gs.keyboard_gamepad = v["kguitar"].get(), v["kpad"].get()
        gs.keyboard_guitar_subtype = _inv(L.GUITAR_SUBTYPES, v["subtype"].get(), 6)
        keys = {}
        for aid, label in L.ACTIONS:
            txt = ",".join(k.strip() for k in self.key_vars[aid].get().split(",") if k.strip())
            bad = L.bad_keys(txt)
            if bad:
                errs.append(f"{label}: tecla(s) desconocida(s) {', '.join(bad)}")
            keys[aid] = txt
        gs.keys = keys
        hotkeys = {}
        for hid, label, default in L.HOTKEYS:
            name = self.hk_vars[hid].get().strip() or default
            if not L.valid_hotkey(name):
                errs.append(f"Atajo '{label}': {name} no se puede usar"
                            + (" (F1-F5 son de los trastes)" if name.lower() in L.RESERVED_HOTKEYS else ""))
            hotkeys[hid] = name
        gs.hotkeys = hotkeys
        errs += L.hotkey_conflicts(gs)
        return errs

    # ------------------------------------------------------------------ acciones
    def save(self, quiet=False) -> bool:
        errs = self.collect()
        if errs:
            messagebox.showerror("wormod", "No se guardo:\n\n" + "\n".join(errs))
            return False
        self.ls.save(self.ws)
        if L.game_running():
            # El juego reescribe re_wor_de.toml al cerrarse: guardarlo ahora se perderia.
            messagebox.showwarning("wormod", "Pantalla y graficos guardados (se aplican al lanzar).\n\n"
                                             "Controles y rendimiento NO: el juego esta abierto y los "
                                             "sobrescribiria al cerrarse. Cierralo y vuelve a guardar.")
            return False
        self.gs.save(self.ws)
        if not quiet:
            self.app.log("Ajustes guardados.", "ok")
        return True

    def defaults(self):
        if not messagebox.askyesno("wormod", "Volver a los valores por defecto (pantalla, graficos, "
                                             "rendimiento y teclas)?\nLos mods no se tocan."):
            return
        self.ls = L.LaunchSettings()
        keep = self.gs
        self.gs = L.GameSettings(keys=dict(L.DEFAULT_KEYS), log_file_opens=False, show_fps=keep.show_fps,
                                 hotkeys=dict(L.DEFAULT_HOTKEYS))
        self.load_vars()
        self.app.log("Valores por defecto cargados (pulsa Guardar o Jugar para aplicarlos).")

    def play(self):
        if self.app.busy:
            messagebox.showinfo("wormod", "Espera a que termine la tarea en curso.")
            return
        if L.game_running():
            messagebox.showinfo("wormod", "El juego ya esta abierto.")
            return
        if not self.save(quiet=True):
            return
        try:
            self.proc, args = L.launch(self.ws, self.ls)
        except (OSError, RuntimeError) as ex:
            messagebox.showerror("wormod", str(ex))
            return
        self.app.log("=== Lanzando el juego ===", "head")
        self.app.log("  " + " ".join(a if " " not in a else f'"{a}"' for a in args[1:]))
        self.play_btn.configure(state="disabled")
        self.info.configure(text="Juego en marcha...")
        self.app.after(2000, self._watch)

    def _watch(self):
        if self.proc is not None and self.proc.poll() is None:
            self.app.after(2000, self._watch)
            return
        code = self.proc.returncode if self.proc is not None else 0
        self.proc = None
        self.play_btn.configure(state="normal")
        if code not in (0, None):
            self.info.configure(text=f"El juego se cerro con codigo {code & 0xFFFFFFFF:#x}")
            self.app.log(f"! El juego termino con codigo {code & 0xFFFFFFFF:#x} "
                         "(ver re_wor_crash_detail.log en la carpeta del juego)", "warn")
        else:
            self.info.configure(text="")
            self.app.log("El juego se cerro.", "ok")
        self.gs = L.GameSettings.load(self.ws)  # el panel DE del juego puede haber cambiado teclas
        self.load_vars()
