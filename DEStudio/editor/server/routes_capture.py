"""Captura con la camara del PC: modelos de MediaPipe para el navegador.

Los mismos .task que usa la app del movil (publicos de Google, licencia Apache 2.0). Si la app ya se compilo, estan en
vcam_app/android/app/src/main/assets; si no, se descargan una vez a cache/models.
"""
from __future__ import annotations

import urllib.request

from ..core import paths
from .http import Binary, router

MODELS = {
    "face_landmarker.task": "face_landmarker/face_landmarker/float16/1/face_landmarker.task",
    "pose_landmarker_full.task": "pose_landmarker/pose_landmarker_full/float16/1/pose_landmarker_full.task",
    "pose_landmarker_heavy.task": "pose_landmarker/pose_landmarker_heavy/float16/1/pose_landmarker_heavy.task",
    "hand_landmarker.task": "hand_landmarker/hand_landmarker/float16/1/hand_landmarker.task",
}


def model_bytes(name: str) -> bytes:
    if name not in MODELS:
        raise ValueError("modelo no valido")
    for f in (paths.EDITOR / "vcam_app" / "android" / "app" / "src" / "main" / "assets" / name, paths.CACHE / "models" / name):
        if f.is_file() and f.stat().st_size > 0:
            return f.read_bytes()
    f = paths.CACHE / "models" / name
    f.parent.mkdir(parents=True, exist_ok=True)
    with urllib.request.urlopen(f"https://storage.googleapis.com/mediapipe-models/{MODELS[name]}", timeout=120) as r:
        data = r.read()
    tmp = f.with_suffix(".part")
    tmp.write_bytes(data)
    tmp.replace(f)
    return data


@router.get("/api/mp/model")
def mp_model(req):
    return Binary(model_bytes(req.arg("name")), "application/octet-stream", cache=True)
