"""wormod - gestor de mods y conversor para Guitar Hero: Warriors of Rock DE (re-wor).

Pestanas:
  Jugar       pantalla, graficos, rendimiento y controles; lanza el juego con esos ajustes
  Mods        mods instalados: activar/desactivar, prioridad, quitar (a la papelera), validar
  Canciones   customs de GHWT:DE: convertir como canciones nuevas o encima de una vanilla
  Instalar    cualquier cosa (carpeta, .zip, archivo): se detecta el tipo y se instala

Los cambios se aplican al reiniciar el juego. Con el juego abierto no se tocan los mods (el juego
reescribe re_wor_de.toml al cerrarse y perderia los cambios).
"""
from __future__ import annotations

import os
import queue
import sys
import threading
import traceback
import tkinter as tk
from pathlib import Path
from tkinter import filedialog, messagebox, ttk

sys.path.insert(0, str(Path(__file__).resolve().parent))

import workspace  # noqa: E402
from launcher import game_running  # noqa: E402
from launcher_tab import LauncherTab  # noqa: E402
from registry import TYPES, Registry  # noqa: E402


class App(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title("Warriors of Rock DE - lanzador y mods")
        self.geometry("1120x800")
        self.minsize(860, 560)
        self.ws = workspace.Workspace.default()
        self.reg = Registry(self.ws)
        self.q: queue.Queue = queue.Queue()
        self.busy = False
        self.songs = []
        self.pending = None  # (modulo, deteccion) en la pestana Instalar

        style = ttk.Style(self)
        if "vista" in style.theme_names():
            style.theme_use("vista")
        style.configure("Treeview", rowheight=24)

        top = ttk.Frame(self, padding=(10, 8, 10, 0))
        top.pack(fill="x")
        ttk.Label(top, text="Guitar Hero: Warriors of Rock DE",
                  font=("Segoe UI", 13, "bold")).pack(side="left")
        self.status = ttk.Label(top, text="", foreground="#a33")
        self.status.pack(side="right")

        pw = ttk.PanedWindow(self, orient="vertical")
        pw.pack(fill="both", expand=True, padx=10, pady=8)
        self.nb = ttk.Notebook(pw)
        pw.add(self.nb, weight=3)
        self._build_mods()
        self._build_songs()
        self._build_install()
        self.launcher = LauncherTab(self, self.nb)

        logf = ttk.LabelFrame(pw, text="Registro", padding=4)
        pw.add(logf, weight=1)
        self.log_box = tk.Text(logf, height=9, wrap="word", font=("Consolas", 9), state="disabled")
        sb = ttk.Scrollbar(logf, command=self.log_box.yview)
        self.log_box.configure(yscrollcommand=sb.set)
        sb.pack(side="right", fill="y")
        self.log_box.pack(fill="both", expand=True)
        for tag, col in (("warn", "#b26b00"), ("error", "#c0392b"), ("ok", "#1e8449"), ("head", "#1f4e99")):
            self.log_box.tag_configure(tag, foreground=col)

        for p in self.ws.check():
            self.log("! " + p, "warn")
        self.after(100, self._pump)
        self.refresh_mods()
        self.refresh_songs()
        self._check_game()

    # ------------------------------------------------------------------ util
    def log(self, text: str, tag: str | None = None) -> None:
        self.q.put((text, tag))

    def _pump(self):
        try:
            while True:
                item = self.q.get_nowait()
                if callable(item):
                    item()
                    continue
                text, tag = item
                if tag is None:
                    s = text.lstrip()
                    tag = "error" if s.startswith("X ") else "warn" if s.startswith("! ") else None
                self.log_box.configure(state="normal")
                self.log_box.insert("end", text + "\n", tag or ())
                self.log_box.see("end")
                self.log_box.configure(state="disabled")
        except queue.Empty:
            pass
        self.after(100, self._pump)

    def _check_game(self):
        self.game_open = game_running()
        self.status.configure(text="Juego abierto: cierralo para aplicar cambios de mods" if self.game_open
                              else "")
        self.after(4000, self._check_game)

    def guard(self) -> bool:
        if self.busy:
            messagebox.showinfo("wormod", "Espera a que termine la tarea en curso.")
            return False
        if game_running():
            messagebox.showwarning("wormod", "El juego esta abierto. Cierralo primero: al cerrarse "
                                             "reescribe la lista de mods y se perderian los cambios.")
            return False
        return True

    def run_task(self, title: str, fn, done=None):
        """Ejecuta fn() en segundo plano; fn devuelve un Report o None."""
        if not self.guard():
            return
        self.busy = True
        self.log(f"=== {title} ===", "head")

        def work():
            try:
                rep = fn()
                if rep is not None:
                    self.log(rep.title, "head")
                    for lvl, t in rep.lines:
                        self.log(("  " if lvl == "info" else "! " if lvl == "warn" else "X ") + t,
                                 {"info": None, "warn": "warn", "error": "error"}[lvl])
                    self.log("Listo." if rep.ok else "Termino con errores.", "ok" if rep.ok else "error")
            except Exception as ex:  # noqa: BLE001
                self.log("X " + "".join(traceback.format_exception_only(type(ex), ex)).strip(), "error")
                self.log(traceback.format_exc(), "error")
            finally:
                self.busy = False
                self.q.put(lambda: (self.refresh_mods(), self.refresh_songs(), done and done()))

        threading.Thread(target=work, daemon=True).start()

    # ------------------------------------------------------------------ Mods
    def _build_mods(self):
        f = ttk.Frame(self.nb, padding=8)
        self.nb.add(f, text="  Mods  ")
        bar = ttk.Frame(f)
        bar.pack(fill="x", pady=(0, 6))
        for txt, cmd in (("Activar / desactivar", self.toggle_mod), ("Subir prioridad", lambda: self.move_mod(-1)),
                         ("Bajar prioridad", lambda: self.move_mod(1)), ("Validar canciones", self.validate_mods),
                         ("Abrir carpeta", self.open_mod), ("Quitar", self.remove_mod),
                         ("Actualizar", self.refresh_mods)):
            ttk.Button(bar, text=txt, command=cmd).pack(side="left", padx=(0, 6))
        cols = ("on", "folder", "type", "files", "size", "name")
        self.mods_tv = ttk.Treeview(f, columns=cols, show="headings", selectmode="browse")
        for c, t, w in (("on", "Activo", 60), ("folder", "Carpeta", 200), ("type", "Tipo", 80),
                        ("files", "Archivos", 70), ("size", "Tamano", 90), ("name", "Nombre / descripcion", 420)):
            self.mods_tv.heading(c, text=t)
            self.mods_tv.column(c, width=w, anchor="w" if c in ("folder", "name") else "center")
        self.mods_tv.tag_configure("off", foreground="#999")
        self.mods_tv.pack(fill="both", expand=True)
        self.mods_tv.bind("<Double-1>", lambda e: self.toggle_mod())
        ttk.Label(f, text="Orden = prioridad: si dos mods traen el mismo archivo gana el de arriba. "
                          "Los cambios se aplican al reiniciar el juego.", foreground="#666").pack(anchor="w", pady=(6, 0))

    def refresh_mods(self):
        sel = self.sel_mod()
        self.mods_tv.delete(*self.mods_tv.get_children())
        for m in self.reg.scan():
            desc = m.name + (f"  -  {m.description}" if m.description else "") + \
                (f"  [{m.manifest_error}]" if m.manifest_error else "")
            self.mods_tv.insert("", "end", iid=m.folder, tags=() if m.enabled else ("off",),
                                values=("si" if m.enabled else "no", m.folder, m.type, m.files,
                                        f"{m.size_mb:.1f} MB", desc))
        if sel and self.mods_tv.exists(sel):
            self.mods_tv.selection_set(sel)

    def sel_mod(self) -> str | None:
        s = self.mods_tv.selection() if hasattr(self, "mods_tv") else ()
        return s[0] if s else None

    def toggle_mod(self):
        m = self.sel_mod()
        if m and self.guard():
            cur = next(x for x in self.reg.scan() if x.folder == m)
            self.reg.set_enabled(m, not cur.enabled)
            self.log(f"{m}: {'desactivado' if cur.enabled else 'activado'}")
            self.refresh_mods()

    def move_mod(self, d):
        m = self.sel_mod()
        if m and self.guard():
            self.reg.move(m, d)
            self.refresh_mods()

    def open_mod(self):
        m = self.sel_mod()
        os.startfile(self.ws.mods / m if m else self.ws.mods)  # noqa: S606

    def remove_mod(self):
        m = self.sel_mod()
        if m and self.guard() and messagebox.askyesno(
                "Quitar mod", f"Mover '{m}' a la papelera (mods/_cache/_papelera)?\nSe puede recuperar."):
            self.log(f"{m} -> {self.reg.remove(m)}")
            self.refresh_mods()

    def validate_mods(self):
        from modules.base import Report
        from validate import validate_song_pak

        def fn():
            rep = Report("Validacion de canciones")
            for m in self.reg.scan():
                for p in sorted((m.path / "data" / "songs").glob("*_song.pak.xen")):
                    n = p.name[: -len("_song.pak.xen")]
                    try:
                        res = validate_song_pak(p, n)
                    except Exception as ex:  # noqa: BLE001
                        res = [("error", f"no se pudo leer ({ex})")]
                    for lvl, t in res:
                        msg = f"{m.folder}/{n}: {'OK - ' if lvl == 'ok' else ''}{t}"
                        rep.info(msg) if lvl in ("ok", "info") else rep.warn(msg) if lvl == "warn" else rep.error(msg)
            return rep
        self.run_task("Validar", fn)

    # ------------------------------------------------------------------ Canciones
    def _build_songs(self):
        from modules.wtde_song import vanilla_targets
        f = ttk.Frame(self.nb, padding=8)
        self.nb.add(f, text="  Canciones  ")
        src = ttk.LabelFrame(f, text="Carpetas de canciones custom (GHWT:DE)", padding=6)
        src.pack(fill="x")
        self.src_lb = tk.Listbox(src, height=3)
        self.src_lb.pack(side="left", fill="x", expand=True)
        b = ttk.Frame(src)
        b.pack(side="left", padx=6)
        ttk.Button(b, text="Anadir carpeta...", command=self.add_source).pack(fill="x")
        ttk.Button(b, text="Quitar carpeta", command=self.del_source).pack(fill="x", pady=4)

        bar = ttk.Frame(f)
        bar.pack(fill="x", pady=6)
        ttk.Button(bar, text="Convertir pendientes (canciones nuevas)",
                   command=lambda: self.build_songs(False)).pack(side="left")
        ttk.Button(bar, text="Reconvertir todas", command=lambda: self.build_songs(True)).pack(side="left", padx=6)
        ttk.Separator(bar, orient="vertical").pack(side="left", fill="y", padx=8)
        ttk.Label(bar, text="Seleccionada encima de:").pack(side="left")
        self.target = ttk.Combobox(bar, values=vanilla_targets(self.ws), width=24, state="readonly")
        self.target.pack(side="left", padx=4)
        ttk.Button(bar, text="Reemplazar (prueba)", command=self.replace_song).pack(side="left")
        ttk.Button(bar, text="Actualizar", command=self.refresh_songs).pack(side="right")

        cols = ("title", "artist", "internal", "status", "folder")
        self.songs_tv = ttk.Treeview(f, columns=cols, show="headings", selectmode="browse")
        for c, t, w in (("title", "Cancion", 230), ("artist", "Artista", 170), ("internal", "Nombre en WoR", 170),
                        ("status", "Estado", 120), ("folder", "Carpeta", 300)):
            self.songs_tv.heading(c, text=t)
            self.songs_tv.column(c, width=w)
        self.songs_tv.tag_configure("pend", foreground="#b26b00")
        self.songs_tv.tag_configure("bad", foreground="#c0392b")
        self.songs_tv.pack(fill="both", expand=True)
        ttk.Label(f, text="Las canciones nuevas van al mod _wtde_customs y aparecen en la lista del juego. "
                          "'Reemplazar' pone la seleccionada encima de una vanilla (mod _PruebaReemplazo).",
                  foreground="#666").pack(anchor="w", pady=(6, 0))

    def refresh_songs(self):
        self.src_lb.delete(0, "end")
        for p in self.ws.song_sources():
            self.src_lb.insert("end", str(p) + ("" if p.is_dir() else "   (no existe)"))

        def work():
            from modules.wtde_song import list_songs
            try:
                songs = list_songs(self.ws)
            except Exception as ex:  # noqa: BLE001
                self.log(f"X no se pudieron leer las canciones: {ex}", "error")
                return
            self.q.put(lambda: self._fill_songs(songs))
        threading.Thread(target=work, daemon=True).start()

    def _fill_songs(self, songs):
        self.songs = songs
        self.songs_tv.delete(*self.songs_tv.get_children())
        for i, s in enumerate(songs):
            tag = () if s.status == "convertida" else ("pend",) if s.status == "pendiente" else ("bad",)
            self.songs_tv.insert("", "end", iid=str(i), tags=tag,
                                 values=(s.title, s.artist, s.internal, s.status, str(s.folder)))

    def add_source(self):
        d = filedialog.askdirectory(title="Carpeta con canciones de GHWT:DE (song.ini)")
        if d:
            self.ws.add_song_source(Path(d))
            self.refresh_songs()

    def del_source(self):
        s = self.src_lb.curselection()
        if s:
            self.ws.remove_song_source(self.ws.song_sources()[s[0]])
            self.refresh_songs()

    def build_songs(self, force):
        from modules.wtde_song import build_new
        self.run_task("Convertir canciones custom", lambda: build_new(self.ws, self.log, force=force))

    def replace_song(self):
        from modules.wtde_song import replace
        s = self.songs_tv.selection()
        tgt = self.target.get()
        if not s or not tgt:
            messagebox.showinfo("wormod", "Elige una cancion de la lista y la cancion vanilla a reemplazar.")
            return
        item = self.songs[int(s[0])]
        self.run_task(f"Reemplazar {tgt}", lambda: replace(self.ws, item, tgt, self.log))

    # ------------------------------------------------------------------ Instalar
    def _build_install(self):
        f = ttk.Frame(self.nb, padding=12)
        self.nb.add(f, text="  Instalar  ")
        ttk.Label(f, text="Elige lo que quieres instalar. Se detecta solo: cancion de GHWT:DE (song.ini), "
                          "mod de Warriors of Rock (carpeta con data/ o archivos .xen, o .zip), "
                          "skin o venue de GHWT:DE.", wraplength=900).pack(anchor="w")
        b = ttk.Frame(f)
        b.pack(anchor="w", pady=10)
        ttk.Button(b, text="Carpeta...", command=lambda: self.pick(True)).pack(side="left")
        ttk.Button(b, text="Archivo o .zip...", command=lambda: self.pick(False)).pack(side="left", padx=6)

        info = ttk.LabelFrame(f, text="Detectado", padding=8)
        info.pack(fill="x")
        self.det_lbl = ttk.Label(info, text="(nada seleccionado)", justify="left")
        self.det_lbl.grid(row=0, column=0, columnspan=4, sticky="w")
        ttk.Label(info, text="Nombre del mod:").grid(row=1, column=0, sticky="w", pady=(8, 0))
        self.inst_name = ttk.Entry(info, width=40)
        self.inst_name.grid(row=1, column=1, sticky="w", pady=(8, 0), padx=4)
        ttk.Label(info, text="Tipo:").grid(row=1, column=2, sticky="w", pady=(8, 0), padx=(12, 0))
        self.inst_type = ttk.Combobox(info, values=TYPES, width=12, state="readonly")
        self.inst_type.grid(row=1, column=3, sticky="w", pady=(8, 0), padx=4)
        self.inst_btn = ttk.Button(f, text="Instalar", command=self.install, state="disabled")
        self.inst_btn.pack(anchor="w", pady=10)

    def pick(self, folder: bool):
        from modules import detect
        p = filedialog.askdirectory() if folder else filedialog.askopenfilename(
            filetypes=[("Mods", "*.zip *.xen"), ("Todos", "*.*")])
        if not p:
            return
        mod, det = detect(Path(p))
        self.pending = (mod, det) if det else None
        if det is None:
            self.det_lbl.configure(text=f"{p}\nNo se reconoce: no hay song.ini, data/, .xen ni .ini de GHWT:DE.")
            self.inst_btn.configure(state="disabled")
            return
        state = "se puede instalar" if det.supported else "AUN NO SE PUEDE CONVERTIR"
        self.det_lbl.configure(text=f"{det.label}\nTipo: {det.kind}   Modulo: {det.module}   ({state})\n"
                                    + "\n".join("  - " + n for n in det.notes))
        self.inst_name.delete(0, "end")
        self.inst_name.insert(0, det.label.split(" (")[0])
        self.inst_type.set(det.kind if det.kind in TYPES else "other")
        song = det.module == "wtde_song"
        self.inst_name.configure(state="disabled" if song else "normal")
        self.inst_type.configure(state="disabled" if song else "readonly")
        self.inst_btn.configure(state="normal")

    def install(self):
        if not self.pending:
            return
        mod, det = self.pending
        name, typ = self.inst_name.get().strip() or None, self.inst_type.get() or None
        self.run_task(f"Instalar {det.label}", lambda: mod.install(det, self.ws, self.log, name=name, type_=typ))


if __name__ == "__main__":
    App().mainloop()
