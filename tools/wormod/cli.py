"""wormod desde la linea de comandos.

  python cli.py list                       mods instalados (orden = prioridad)
  python cli.py enable|disable <carpeta>
  python cli.py up|down <carpeta>
  python cli.py remove <carpeta>           (va a mods/_cache/_papelera)
  python cli.py songs                      canciones custom encontradas y su estado
  python cli.py build [--force]            convierte las customs como canciones nuevas y valida
  python cli.py replace <cancion> <vanilla>
  python cli.py install <ruta>             instala lo que sea (cancion WTDE, mod WoR, zip...)
  python cli.py validate                   valida los PAK de cancion de los mods
"""
from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import workspace  # noqa: E402
from registry import Registry  # noqa: E402


def main(argv: list[str]) -> int:
    ws = workspace.Workspace.default()
    reg = Registry(ws)
    if not argv:
        print(__doc__)
        return 0
    cmd, args = argv[0], argv[1:]
    for p in ws.check():
        print("!", p)
    if cmd == "list":
        for i, m in enumerate(reg.scan(), 1):
            print(f"{i:2}. [{'x' if m.enabled else ' '}] {m.folder:28} {m.type:9} {m.files:4} arch "
                  f"{m.size_mb:8.1f} MB  {m.name}")
    elif cmd in ("enable", "disable"):
        reg.set_enabled(args[0], cmd == "enable")
    elif cmd in ("up", "down"):
        reg.move(args[0], -1 if cmd == "up" else 1)
    elif cmd == "remove":
        print("movido a", reg.remove(args[0]))
    elif cmd == "songs":
        from modules.wtde_song import list_songs
        for s in list_songs(ws):
            print(f"{s.internal:26} {s.status:12} {s.title} - {s.artist}")
    elif cmd == "build":
        from modules.wtde_song import build_new
        print(build_new(ws, print, force="--force" in args).text())
    elif cmd == "replace":
        from modules.wtde_song import list_songs, replace
        item = next(s for s in list_songs(ws) if args[0].lower() in (s.name, s.internal))
        print(replace(ws, item, args[1], print).text())
    elif cmd == "install":
        from modules import detect
        mod, det = detect(Path(args[0]))
        if det is None:
            print("No se reconoce el contenido")
            return 1
        print(f"Detectado: {det.label} [{det.kind}] por {det.module}")
        print(mod.install(det, ws, print).text())
    elif cmd == "validate":
        from validate import validate_song_pak
        for m in reg.scan():
            for p in sorted((m.path / "data" / "songs").glob("*_song.pak.xen")):
                n = p.name[: -len("_song.pak.xen")]
                res = validate_song_pak(p, n)
                bad = [r for r in res if r[0] not in ("ok", "info")]
                print(f"{m.folder}/{p.name}: " + ("OK" if not bad else ""))
                for lvl, t in bad:
                    print(f"   {lvl}: {t}")
    else:
        print(__doc__)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
