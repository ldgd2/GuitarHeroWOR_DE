"""Rutas de la cancion: listado, proyecto, guardar, compilar, copias, .ska."""
from __future__ import annotations

import json
import re

from ..core import paths
from ..core import projfile as PF
from ..formats.ska import ska_full, ska_preview
from ..song import build as B
from ..song.library import list_songs
from ..song.project import import_ska, load_project, ska_entry
from .http import router

SAFE_NAME = re.compile(r"^[A-Za-z0-9_.\-]{1,120}$")


def _saved_file(song_id: str):
    """Proyecto guardado con el formato anterior (JSON suelto en projects/): solo se lee, para no perderlo."""
    return paths.PROJECTS / (re.sub(r"[^A-Za-z0-9_.-]", "_", song_id) + ".json")


def _song_desp(song_id: str):
    """El .desp de la biblioteca que es de esta cancion (o None)."""
    for it in PF.list_library("song"):
        if it.get("song") == song_id:
            return PF.library_file(it["file"])
    return None


def _load_saved(song_id: str):
    f = _song_desp(song_id)
    if f:
        _man, proj, assets = PF.unpack(f.read_bytes())
        # animaciones importadas: vuelven a cache/imports, donde las busca la compilacion
        imp = paths.CACHE / "imports"
        imp.mkdir(exist_ok=True)
        for rel, data in assets.items():
            if rel.startswith("skas/") and rel.endswith(".ska"):
                (imp / rel[5:]).write_bytes(data)
        return proj
    f = _saved_file(song_id)
    if f.exists():
        return json.loads(f.read_text(encoding="utf-8"))
    return None


@router.get("/api/songs")
def songs(req):
    return list_songs()


@router.get("/api/project")
def project(req):
    sid = req.arg("id")
    saved = _load_saved(sid) if req.query.get("saved") == "1" else None
    if saved is not None:
        if "facial" not in saved:   # proyectos guardados antes de exportar caras: se leen del PAK
            saved["facial"] = load_project(sid).get("facial", [])
        return saved
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
    """Guarda el proyecto de la cancion en la biblioteca (<estudio>/proyectos/<cancion>.desp), con las
    animaciones .ska importadas dentro del archivo."""
    proj = req.json()
    sid = proj["song"]["id"]
    assets = {}
    for s in proj.get("skas", []):
        if s.get("imported"):
            f = paths.CACHE / "imports" / f"{s['id']}.ska"
            if f.exists():
                assets[f"skas/{f.name}"] = f.read_bytes()
    old = _song_desp(sid)
    name = old.stem if old else PF.safe_name(proj["song"].get("name") or sid)
    man = PF.read_manifest(old) if old else None
    data = PF.pack("song", name, proj, assets, created=(man or {}).get("created"),
                   extra={"song": sid, "title": proj["song"].get("name", "")})
    f = PF.save_library(name, data, kind="song")
    return {"saved": str(f), "file": f.stem}


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


@router.post("/api/anim/make")
def anim_make(req):
    """Animacion de cara o cuerpo capturada con el movil -> .ska parcial en cache/imports (al PAK al compilar)."""
    from ..song.animgen import make_anim
    j = json.loads(req.body or b"{}")
    name = str(j.get("name", ""))
    if not re.fullmatch(r"[A-Za-z0-9_]{1,60}", name):
        raise ValueError("nombre de animacion no valido")
    return make_anim(name, int(j.get("n", 0)), j.get("bones") or {})


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


def migrate_legacy() -> int:
    """Proyectos de cancion del formato anterior (projects/*.json sueltos) -> biblioteca .desp, una sola vez por
    cancion (si ya hay un .desp de esa cancion no se toca). El JSON original se deja donde estaba."""
    n = 0
    for f in sorted(paths.PROJECTS.glob("*.json")):
        try:
            proj = json.loads(f.read_text(encoding="utf-8"))
            sid = proj["song"]["id"]
            if _song_desp(sid):
                continue
            assets = {}
            for s in proj.get("skas", []):
                imp = paths.CACHE / "imports" / f"{s.get('id')}.ska"
                if s.get("imported") and imp.exists():
                    assets[f"skas/{imp.name}"] = imp.read_bytes()
            name = PF.safe_name(proj["song"].get("name") or sid)
            if PF.library_file(name).exists():
                name = PF.safe_name(name + "_cancion")
            PF.save_library(name, PF.pack("song", name, proj, assets, created=f.stat().st_mtime,
                                          extra={"song": sid, "title": proj["song"].get("name", "")}), kind="song")
            n += 1
        except Exception as ex:  # noqa: BLE001
            print(f"[DE Studio] no se pudo convertir {f.name}: {ex}")
    if n:
        print(f"[DE Studio] {n} proyecto(s) de cancion pasados a la biblioteca ({PF.LIBRARY})")
    return n


migrate_legacy()
