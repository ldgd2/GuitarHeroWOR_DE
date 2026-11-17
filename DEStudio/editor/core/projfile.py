"""Archivo de proyecto del estudio (.desp): todo un proyecto en un solo archivo.

Por dentro es un ZIP (no hace falta que el usuario lo sepa) con:
  manifest.json   {"app": "DE Studio", "format": 1, "kind": "song" | "ui", "name", "created", "modified"}
  project.json    el proyecto tal cual lo usa su editor
  assets/...      lo que el proyecto necesita y no esta en el juego: imagenes propias de una interfaz,
                  animaciones .ska importadas a una cancion...

Biblioteca: los proyectos viven en <estudio>/proyectos/<nombre>.desp. Mientras se edita, los recursos se
desempaquetan en cache/work/<tipo>/<nombre>/ (carpeta de trabajo, se puede borrar) y al guardar se vuelven
a meter en el archivo. "Abrir archivo" de otra carpeta lo copia a la biblioteca.
"""
from __future__ import annotations

import io
import json
import re
import shutil
import time
import zipfile
from pathlib import Path

from . import paths

APP = "DE Studio"
FORMAT = 1
EXT = ".desp"
KINDS = ("song", "ui")
LIBRARY = paths.EDITOR / "proyectos"
WORK = paths.CACHE / "work"
LIBRARY.mkdir(parents=True, exist_ok=True)


class ProjectFileError(ValueError):
    pass


def safe_name(name: str) -> str:
    n = re.sub(r"[^A-Za-z0-9_.\-]+", "_", (name or "").strip()).strip("._")[:100]
    if not n:
        raise ProjectFileError("nombre de proyecto vacio")
    return n


def library_file(name: str) -> Path:
    return LIBRARY / (safe_name(name) + EXT)


def workdir(kind: str, name: str) -> Path:
    if kind not in KINDS:
        raise ProjectFileError(f"tipo de proyecto desconocido: {kind}")
    d = WORK / kind / safe_name(name)
    d.mkdir(parents=True, exist_ok=True)
    return d


def pack(kind: str, name: str, project: dict, assets: dict[str, bytes] | None = None,
         created: float | None = None, extra: dict | None = None) -> bytes:
    """Proyecto + recursos -> bytes del .desp. assets: {"ruta/dentro.png": bytes}; extra: mas datos del
    manifiesto (p. ej. la cancion o el tipo de interfaz, para listarlos sin abrir el proyecto)."""
    now = time.time()
    man = {**(extra or {}), "app": APP, "format": FORMAT, "kind": kind, "name": name, "created": created or now,
           "modified": now}
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w", zipfile.ZIP_DEFLATED) as z:
        z.writestr("manifest.json", json.dumps(man, ensure_ascii=False, indent=1))
        z.writestr("project.json", json.dumps(project, ensure_ascii=False))
        for rel, data in sorted((assets or {}).items()):
            rel = rel.replace("\\", "/").lstrip("/")
            if ".." in rel.split("/"):
                raise ProjectFileError(f"ruta no valida: {rel}")
            # PNG y .ska ya van comprimidos o casi: guardarlos sin recomprimir es mas rapido
            z.writestr("assets/" + rel, data, zipfile.ZIP_STORED if rel.endswith(".png") else zipfile.ZIP_DEFLATED)
    return buf.getvalue()


def unpack(data: bytes) -> tuple[dict, dict, dict[str, bytes]]:
    """bytes del .desp -> (manifest, project, assets)"""
    try:
        z = zipfile.ZipFile(io.BytesIO(data))
    except zipfile.BadZipFile as ex:
        raise ProjectFileError("no es un proyecto de DE Studio (.desp)") from ex
    with z:
        names = set(z.namelist())
        if "manifest.json" not in names or "project.json" not in names:
            raise ProjectFileError("no es un proyecto de DE Studio (.desp)")
        man = json.loads(z.read("manifest.json"))
        if man.get("app") != APP or man.get("kind") not in KINDS:
            raise ProjectFileError("no es un proyecto de DE Studio (.desp)")
        if int(man.get("format", 0)) > FORMAT:
            raise ProjectFileError("el proyecto es de una version mas nueva de DE Studio")
        proj = json.loads(z.read("project.json"))
        assets = {}
        for n in names:
            if n.startswith("assets/") and not n.endswith("/"):
                rel = n[len("assets/"):]
                if ".." in rel.split("/"):
                    continue
                assets[rel] = z.read(n)
    return man, proj, assets


def read_manifest(f: Path) -> dict | None:
    try:
        with zipfile.ZipFile(f) as z:
            return json.loads(z.read("manifest.json"))
    except Exception:  # noqa: BLE001
        return None


def list_library(kind: str | None = None) -> list[dict]:
    out = []
    for f in sorted(LIBRARY.glob("*" + EXT), key=lambda p: -p.stat().st_mtime):
        m = read_manifest(f)
        if not m or (kind and m.get("kind") != kind):
            continue
        out.append({"file": f.stem, "kind": m["kind"], "name": m.get("name", f.stem), "modified": f.stat().st_mtime,
                    "size": f.stat().st_size, "song": m.get("song"), "mode": m.get("mode"),
                    "title": m.get("title", "")})
    return out


def save_library(name: str, data: bytes, kind: str | None = None) -> Path:
    f = library_file(name)
    if kind and f.exists():
        m = read_manifest(f)
        if m and m.get("kind") != kind:
            raise ProjectFileError(f"ya hay un proyecto de otro tipo llamado {f.stem}: usa otro nombre")
    tmp = f.with_suffix(".tmp")
    tmp.write_bytes(data)
    tmp.replace(f)
    return f


def load_library(name: str) -> tuple[dict, dict, dict[str, bytes]]:
    f = library_file(name)
    if not f.exists():
        raise KeyError(name)
    return unpack(f.read_bytes())


def extract_to_work(kind: str, name: str, assets: dict[str, bytes]) -> Path:
    """Recursos del archivo -> carpeta de trabajo limpia."""
    d = workdir(kind, name)
    shutil.rmtree(d, ignore_errors=True)
    d.mkdir(parents=True, exist_ok=True)
    for rel, data in assets.items():
        p = d / rel
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_bytes(data)
    return d


def work_assets(kind: str, name: str) -> dict[str, bytes]:
    d = workdir(kind, name)
    return {p.relative_to(d).as_posix(): p.read_bytes() for p in d.rglob("*") if p.is_file()}
