"""Puente QB de re-wor (127.0.0.1, token por arranque en dev_bridge.json junto al .exe): carga un .qb.xen en el
juego que esta abierto y ejecuta scripts, para probar una interfaz sin reiniciar."""
from __future__ import annotations

import json
import socket
from pathlib import Path


class BridgeError(Exception):
    pass


def send(build_dir: Path, commands: list[str], timeout: float = 8.0) -> list[str]:
    info = build_dir / "dev_bridge.json"
    if not info.exists():
        raise BridgeError("el juego no esta abierto (o no tiene el puente de desarrollo)")
    d = json.loads(info.read_text(encoding="utf-8"))
    try:
        s = socket.create_connection(("127.0.0.1", int(d["port"])), timeout=timeout)
    except OSError:
        raise BridgeError("el juego no esta abierto (no responde el puente)") from None
    with s:
        f = s.makefile("rw", encoding="latin-1", newline="\n")
        f.readline()
        f.write(f"auth {d['token']}\n")
        f.flush()
        if not f.readline().lower().startswith("ok"):
            raise BridgeError("el puente rechazo la conexion")
        out = []
        for c in commands:
            f.write(c.replace("\n", " ") + "\n")
            f.flush()
            out.append(f.readline().strip())
        return out
