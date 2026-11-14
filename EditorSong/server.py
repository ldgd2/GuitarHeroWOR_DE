"""EditorSong: punto de entrada. Toda la logica vive en el paquete editor/ (ver LEEME.md)."""
from __future__ import annotations

import argparse
import webbrowser

from editor.server.http import serve


def main() -> int:
    ap = argparse.ArgumentParser(description="Editor de escenas/animaciones de canciones de GH:WoR")
    ap.add_argument("--port", type=int, default=8765)
    ap.add_argument("--no-browser", action="store_true")
    a = ap.parse_args()
    srv = serve(a.port)
    url = f"http://127.0.0.1:{a.port}/"
    print(f"EditorSong en {url}  (Ctrl+C para cerrar)")
    if not a.no_browser:
        webbrowser.open(url)
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        pass
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
