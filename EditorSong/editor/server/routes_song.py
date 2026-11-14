"""Rutas de la cancion: listado, proyecto, guardar, compilar, copias, .ska."""
from __future__ import annotations

import json
import re

from ..core import paths
from ..formats.ska import ska_full, ska_preview
from ..song import build as B
from ..song.library import list_songs
from ..song.project import import_ska, load_project, ska_entry
from .http import router

SAFE_NAME = re.compile(r"^[A-Za-z0-9_.\-]{1,120}$")


def _saved_file(song_id: str):
    return paths.PROJECTS / (re.sub(r"[^A-Za-z0-9_.-]", "_", song_id) + ".json")


@router.get("/api/songs")
def songs(req):
    return list_songs()


@router.get("/api/project")
def project(req):
    sid = req.arg("id")
    f = _saved_file(sid)
    if req.query.get("saved") == "1" and f.exists():
        return json.loads(f.read_text(encoding="utf-8"))
    return load_project(sid)


@router.get("/api/ska")
def ska(req):
    data = ska_entry(req.arg("song"), req.arg("id"))
    return ska_full(data) if req.query.get("full") == "1" else ska_preview(data)


@router.get("/api/backups")
def backups(req):
    return B.backups_for(req.arg("id"))


@router.post("/api/save")
def save(req):
    proj = req.json()
    f = _saved_file(proj["song"]["id"])
    f.write_text(json.dumps(proj), encoding="utf-8")
    return {"saved": str(f)}


@router.post("/api/build")
def build(req):
    from .routes_system import game_running
    res = B.build(req.json())
    res["game_running"] = game_running()
    return res


@router.post("/api/import_ska")
def import_(req):
    name = req.query.get("name", "")
    if not SAFE_NAME.match(name):
        raise ValueError("nombre no valido (letras, numeros, _ . -)")
    return import_ska(name, req.body)


@router.post("/api/restore")
def restore(req):
    j = req.json()
    return {"restored": B.restore(j["id"], j["backup"])}


@router.post("/api/camera/make")
def camera_make(req):
    """Camara de clip generada por un preset del editor -> .ska en cache/imports (se anade al PAK al compilar)."""
    from ..song.camgen import make_camera
    j = json.loads(req.body or b"{}")
    name = str(j.get("name", ""))
    if not re.fullmatch(r"[A-Za-z0-9_]{1,60}", name):
        raise ValueError("nombre de camara no valido")
    return make_camera(name, j.get("keys") or [], j.get("fov") or [])


@router.get("/api/audio")
def audio_info(req):
    """Grupos de audio de la cancion (bateria, guitarra, bajo, voz, resto, publico) y picos para la onda."""
    from ..song.audio import info
    return info(req.arg("id"))


@router.get("/api/audio/mix")
def audio_mix(req):
    """Mezcla WAV de los grupos activos (?groups=drums,guitar,...)."""
    from ..song.audio import mix_wav
    from .http import Binary
    groups = [g for g in req.query.get("groups", "").split(",") if g]
    return Binary(mix_wav(req.arg("id"), groups), "audio/wav", cache=True)
