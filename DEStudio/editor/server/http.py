"""Servidor HTTP minimo (biblioteca estandar, solo 127.0.0.1) con un enrutador por decoradores.

Cada modulo routes_*.py registra sus rutas con @router.get("/api/...") / @router.post(...). Una ruta
recibe un Request y devuelve dict/list (JSON), bytes con tipo ("bytes", ctype) o lanza una excepcion.
"""
from __future__ import annotations

import json
import mimetypes
import traceback
from dataclasses import dataclass
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlparse

from ..core import paths

# interfaz compilada con Vite (cd web && npm run build); sin compilar no hay interfaz
WEB = paths.EDITOR / "web" / "dist"


@dataclass
class Request:
    path: str
    query: dict
    body: bytes

    def json(self):
        return json.loads(self.body or b"null")

    def arg(self, name: str, default=None):
        v = self.query.get(name, default)
        if v is None:
            raise ValueError(f"falta el parametro '{name}'")
        return v


class Binary:
    def __init__(self, data: bytes, ctype: str, cache: bool = False):
        self.data, self.ctype, self.cache = data, ctype, cache


class Router:
    def __init__(self):
        self.routes: dict[tuple[str, str], callable] = {}

    def get(self, path: str):
        return self._add("GET", path)

    def post(self, path: str):
        return self._add("POST", path)

    def _add(self, method: str, path: str):
        def deco(fn):
            self.routes[(method, path)] = fn
            return fn
        return deco


router = Router()


class Handler(BaseHTTPRequestHandler):
    server_version = "DEStudio/2"

    def log_message(self, fmt, *args):
        pass

    def _send(self, code: int, body: bytes, ctype: str, cache: bool = False):
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "max-age=86400" if cache else "no-store")
        self.end_headers()
        self.wfile.write(body)

    def _json(self, obj, code: int = 200):
        self._send(code, json.dumps(obj).encode(), "application/json; charset=utf-8")

    def _dispatch(self, method: str):
        u = urlparse(self.path)
        try:
            if u.path.startswith("/api/"):
                fn = router.routes.get((method, u.path))
                if not fn:
                    return self._json({"error": "ruta desconocida"}, 404)
                n = int(self.headers.get("Content-Length") or 0)
                req = Request(u.path, {k: v[0] for k, v in parse_qs(u.query).items()},
                              self.rfile.read(n) if n else b"")
                res = fn(req)
                if isinstance(res, Binary):
                    return self._send(200, res.data, res.ctype, res.cache)
                return self._json(res)
            if method != "GET":
                return self._json({"error": "metodo no permitido"}, 405)
            self._static(u.path)
        except KeyError as ex:
            self._json({"error": f"no encontrado: {ex}"}, 404)
        except Exception as ex:  # noqa: BLE001
            traceback.print_exc()
            self._json({"error": str(ex)}, 500)

    def _static(self, path: str):
        rel = "index.html" if path in ("/", "") else path.lstrip("/")
        f = (WEB / rel).resolve()
        if not f.is_relative_to(WEB.resolve()) or not f.is_file():
            return self._send(404, b"no encontrado", "text/plain")
        ctype = ("text/javascript" if f.suffix == ".js" else "application/wasm" if f.suffix == ".wasm"
                 else (mimetypes.guess_type(f.name)[0] or "application/octet-stream"))
        self._send(200, f.read_bytes(), ctype)

    def do_GET(self):
        self._dispatch("GET")

    def do_POST(self):
        self._dispatch("POST")


def serve(port: int) -> ThreadingHTTPServer:
    from . import routes_assets, routes_band, routes_capture, routes_song, routes_stage, routes_system, routes_ui, routes_files  # noqa: F401  (registran rutas)
    # sin SO_REUSEADDR: en Windows deja que DOS servidores escuchen en el mismo puerto y el navegador acaba hablando
    # con el viejo. Asi, si el editor ya esta abierto, este falla y lo dice.
    class Server(ThreadingHTTPServer):
        allow_reuse_address = False
    try:
        srv = Server(("127.0.0.1", port), Handler)
    except OSError as ex:
        raise SystemExit(f"El puerto {port} ya esta en uso: DE Studio ya esta abierto en otra ventana. Cierrala y vuelve a abrirlo. ({ex})")
    from . import vcam
    vcam.start(port + 1)   # puente con la app del movil (otro puerto, en la red local, con PIN)
    return srv
