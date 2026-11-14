"""Estado del sistema (juego abierto o no)."""
from __future__ import annotations

from .http import router


def game_running() -> bool:
    try:
        from launcher import game_running as gr  # tools/wormod/launcher.py
        return bool(gr())
    except Exception:  # noqa: BLE001
        return False


@router.get("/api/status")
def status(req):
    return {"game_running": game_running()}


@router.get("/api/vcam/info")
def vcam_info(req):
    """Camara del movil: IP de la red local, puerto y PIN para la app (y si hay un telefono conectado)."""
    from . import vcam
    return vcam.info()
