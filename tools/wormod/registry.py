"""Lista de mods del juego: la misma que lee el ModManager de re-wor.

  re-wor/mods/<carpeta>/mod.toml + data/...       un mod por carpeta
  re-wor/re_wor_de.toml  ->  mods = [{folder, enabled}, ...]   orden = prioridad (el primero gana)

Los cambios se aplican al reiniciar el juego. No editar mientras el juego esta abierto: el panel DE (F8 por defecto)
del juego reescribe re_wor_de.toml al guardar.
"""
from __future__ import annotations

import re
import shutil
import time
import tomllib
from dataclasses import dataclass
from pathlib import Path

from workspace import Workspace

RESERVED = {"data", "baked", "raw", "song", "_templates", "_cache"}  # igual que mod_manager.cpp
TYPES = ["song", "music", "skin", "venue", "highway", "character", "ui", "other"]


@dataclass
class Mod:
    folder: str
    path: Path
    name: str = ""
    author: str = ""
    version: str = ""
    type: str = "other"
    description: str = ""
    enabled: bool = True
    files: int = 0
    size: int = 0
    manifest_error: str = ""

    @property
    def size_mb(self) -> float:
        return self.size / (1024 * 1024)


# ------------------------------------------------------------------ mod.toml
def _q(s: str) -> str:
    return '"' + str(s).replace("\\", "\\\\").replace('"', '\\"') + '"'


def write_manifest(folder: Path, name: str, type_: str, author: str = "", version: str = "1",
                   description: str = "") -> None:
    folder.mkdir(parents=True, exist_ok=True)
    (folder / "mod.toml").write_text(
        "\n".join([f"name = {_q(name)}", f"author = {_q(author)}", f"version = {_q(version)}",
                   f"type = {_q(type_)}", f"description = {_q(description)}", ""]), encoding="utf-8")


def read_manifest(folder: Path) -> tuple[dict, str]:
    p = folder / "mod.toml"
    if not p.exists():
        return {}, ""
    try:
        return tomllib.loads(p.read_text(encoding="utf-8")), ""
    except (tomllib.TOMLDecodeError, UnicodeDecodeError) as ex:
        return {}, f"mod.toml invalido: {ex}"


# ------------------------------------------------------------------ re_wor_de.toml
def _read_config(path: Path) -> list[tuple[str, bool]]:
    try:
        data = tomllib.loads(path.read_text(encoding="utf-8"))
    except (OSError, tomllib.TOMLDecodeError):
        return []
    out = []
    for m in data.get("mods", []) or []:
        if isinstance(m, dict) and m.get("folder"):
            out.append((str(m["folder"]), bool(m.get("enabled", True))))
    return out


def _mods_block(entries: list[tuple[str, bool]]) -> str:
    if not entries:
        return "mods = []"
    lines = ["mods = ["]
    for folder, en in entries:
        lines.append(f"    {{ enabled = {'true' if en else 'false'}, folder = {_q(folder)} }},")
    lines.append("]")
    return "\n".join(lines)


def _write_config(path: Path, entries: list[tuple[str, bool]]) -> None:
    """Reescribe SOLO la clave mods de re_wor_de.toml; el resto del archivo queda igual."""
    text = path.read_text(encoding="utf-8") if path.exists() else ""
    block = _mods_block(entries)
    m = re.search(r"^mods\s*=\s*\[", text, re.M)
    if m:
        i, depth, in_str = m.end() - 1, 0, None
        while i < len(text):
            c = text[i]
            if in_str:
                if c == "\\" and in_str == '"':
                    i += 2
                    continue
                if c == in_str:
                    in_str = None
            elif c in "\"'":
                in_str = c
            elif c == "[":
                depth += 1
            elif c == "]":
                depth -= 1
                if depth == 0:
                    break
            i += 1
        text = text[:m.start()] + block + text[i + 1:]
    else:
        t = re.search(r"^\[", text, re.M)
        pos = t.start() if t else len(text)
        text = text[:pos] + block + "\n\n" + text[pos:]
    tomllib.loads(text)  # no escribir nunca un archivo que el juego no pueda leer
    path.write_text(text, encoding="utf-8")


# ------------------------------------------------------------------ registro
class Registry:
    def __init__(self, ws: Workspace):
        self.ws = ws

    def scan(self) -> list[Mod]:
        found: dict[str, Mod] = {}
        if self.ws.mods.is_dir():
            for d in sorted(self.ws.mods.iterdir()):
                if not d.is_dir() or d.name.lower() in RESERVED:
                    continue
                man, err = read_manifest(d)
                m = Mod(folder=d.name, path=d, name=str(man.get("name") or d.name),
                        author=str(man.get("author", "")), version=str(man.get("version", "")),
                        type=str(man.get("type", "other")), description=str(man.get("description", "")),
                        manifest_error=err)
                data = d / "data"
                if not data.is_dir() and any(d.glob("*.ini")):
                    # Carpeta de GHWT:DE copiada tal cual: el juego solo carga data/, asi que no hace nada.
                    m.type = "sin convertir"
                    m.manifest_error = "formato GHWT:DE (character.ini/song.ini): el juego no lo carga"
                if data.is_dir():
                    for f in data.rglob("*"):
                        if f.is_file():
                            m.files += 1
                            m.size += f.stat().st_size
                found[d.name] = m
        ordered = []
        for folder, en in _read_config(self.ws.config):
            if folder in found:
                m = found.pop(folder)
                m.enabled = en
                ordered.append(m)
        ordered += [found[k] for k in sorted(found)]  # nuevos: al final y activos (como el juego)
        return ordered

    def save(self, mods: list[Mod]) -> None:
        _write_config(self.ws.config, [(m.folder, m.enabled) for m in mods])

    def set_enabled(self, folder: str, enabled: bool) -> None:
        mods = self.scan()
        for m in mods:
            if m.folder == folder:
                m.enabled = enabled
        self.save(mods)

    def move(self, folder: str, delta: int) -> None:
        mods = self.scan()
        i = next((k for k, m in enumerate(mods) if m.folder == folder), None)
        if i is None:
            return
        j = i + delta
        if 0 <= j < len(mods):
            mods[i], mods[j] = mods[j], mods[i]
            self.save(mods)

    def remove(self, folder: str) -> Path:
        """No borra: mueve el mod a mods/_cache/_papelera/ (recuperable)."""
        src = self.ws.mods / folder
        dst = self.ws.cache / "_papelera" / f"{folder}_{time.strftime('%Y%m%d_%H%M%S')}"
        dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.move(str(src), str(dst))
        self.save([m for m in self.scan() if m.folder != folder])
        return dst

    def unique_folder(self, name: str) -> str:
        base = re.sub(r"[^\w\- ]", "", name).strip().replace(" ", "_") or "mod"
        if base.lower() in RESERVED:
            base = "mod_" + base
        folder, n = base, 2
        while (self.ws.mods / folder).exists():
            folder, n = f"{base}_{n}", n + 1
        return folder
