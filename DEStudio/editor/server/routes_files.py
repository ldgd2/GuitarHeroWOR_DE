"""Biblioteca de proyectos (.desp) del estudio: listar, abrir un archivo de cualquier carpeta (se copia a la
biblioteca), descargar/guardar una copia en otra carpeta y borrar. Ver core/projfile.py."""
from __future__ import annotations

from ..core import projfile as PF
from .http import Binary, router


@router.get("/api/files")
def files(req):
    return {"files": PF.list_library(req.query.get("kind") or None), "ext": PF.EXT, "folder": str(PF.LIBRARY)}


@router.post("/api/files/import")
def import_file(req):
    """Cuerpo = bytes de un .desp. Se valida y se copia a la biblioteca (si el nombre esta ocupado por otro
    tipo de proyecto, se le anade un numero)."""
    man, proj, _assets = PF.unpack(req.body)
    base = PF.safe_name(req.query.get("name") or man.get("name") or "proyecto")
    name, i = base, 2
    while PF.library_file(name).exists() and (PF.read_manifest(PF.library_file(name)) or {}).get("kind") != man["kind"]:
        name, i = f"{base}_{i}", i + 1
    PF.save_library(name, req.body)
    return {"file": name, "kind": man["kind"], "name": man.get("name"), "song": man.get("song"), "mode": man.get("mode")}


@router.get("/api/files/download")
def download(req):
    f = PF.library_file(req.arg("file"))
    if not f.exists():
        raise KeyError(req.arg("file"))
    return Binary(f.read_bytes(), "application/octet-stream")


@router.post("/api/files/delete")
def delete(req):
    f = PF.library_file(req.json().get("file", ""))
    if f.exists():
        f.unlink()
    return {"ok": True}
