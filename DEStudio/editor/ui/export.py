"""Proyecto de interfaz (JSON del editor, version 3) -> lo que carga el juego.

Salida (en la carpeta del juego compilado):
  ui3d/<n>/scene.ui3d + PNG   la escena horneada con todas sus pantallas y botones (modes.SceneBuilder). La dibuja
                              re-wor (src/de/ui3d.cpp) en 2D, 2.5D y 3D.
  qb/ui_<n>.qb.xen            scripts para la logica del juego (fuente en re-wor/qb/ui_<n>.q):
      re_ui_<n>_show / _hide / _hide_now     abrir (entrada y bucle), salida, quitar ya
      re_ui_<n>_goto screen = <i>            ir a una pantalla (y re_ui_<n>_goto_<pantalla>_<i>)
      re_ui_<n>_active                       1 mientras esta abierta
Si la interfaz tiene botones, al abrirse captura el mando (quita el foco al menu del juego y se lo devuelve al
cerrarse) y re-wor mueve el foco, anima los botones y hace sus acciones: ir a otra pantalla, volver, cerrar,
ejecutar un script del juego o cambiar una variable entera del juego.
"""
from __future__ import annotations

import re
from pathlib import Path


class ExportError(Exception):
    pass


def clean_name(n: str) -> str:
    n = re.sub(r"[^a-z0-9_]+", "_", (n or "").strip().lower()).strip("_")
    if not n:
        raise ExportError("la interfaz necesita un nombre (letras, numeros y _)")
    if len(n) > 40:
        raise ExportError("nombre demasiado largo (40 como mucho)")
    return n


def to_q(proj: dict) -> tuple[str, str]:
    """(nombre limpio, fuente .q)"""
    from . import modes
    n = clean_name(proj.get("name"))
    return n, modes.qscripts(n, proj)


def scene(proj: dict, textures: dict, work_dir: Path | None):
    """-> (texto de scene.ui3d, {archivo: bytes}, avisos)"""
    from . import gameimg, modes
    if not proj.get("screens"):
        raise ExportError("proyecto de una version anterior: abrelo y guardalo en el editor antes de exportar")

    def image_bytes(l):
        im = l.get("image") or {}
        if im.get("custom"):
            f = (work_dir / im.get("file", "")) if work_dir else None
            if not (f and f.is_file()):
                return None
            ext = f.suffix.lower()
            if ext == ".png":
                return (f"c_{f.name}", f.read_bytes())
            import io
            from PIL import Image
            if ext == ".svg":       # vectorial: se rasteriza al doble del tamano en pantalla (nitido al escalar)
                import pymupdf
                doc = pymupdf.open(stream=f.read_bytes(), filetype="svg")
                pg = doc[0]
                b = l.get("base") or {}
                want = max(float(b.get("w") or 0) * float(b.get("scale") or 1), float(b.get("h") or 0) * float(b.get("scale") or 1)) * 2
                k = max(1.0, min(4096.0, want or 512) / max(pg.rect.width, pg.rect.height, 1))
                pix = pg.get_pixmap(matrix=pymupdf.Matrix(k, k), alpha=True)
                im2 = Image.frombytes("RGBA", (pix.width, pix.height), pix.samples)
            elif ext in (".jpg", ".jpeg", ".webp"):
                im2 = Image.open(f).convert("RGBA")
            else:
                return None
            buf = io.BytesIO()
            im2.save(buf, "PNG")
            return (f"c_{f.stem}_{im2.width}x{im2.height}.png", buf.getvalue())
        if im.get("key") and im.get("pak"):
            return (f"g_{im['key']}.png", gameimg.png(im["pak"], im["key"]))
        return None

    sb = modes.SceneBuilder(proj, textures, image_bytes)
    text = sb.build()
    return text, sb.files, sb.warnings


def write(proj: dict, game_dir: Path, build_dir: Path, textures: dict | None = None, work_dir: Path | None = None) -> dict:
    """Escribe la escena y los scripts (re-wor/qb/ui_<n>.q, compilado a <build>/qb/ui_<n>.qb.xen)."""
    import importlib.util
    import shutil

    n, src = to_q(proj)
    text, files, warnings = scene(proj, textures or {}, work_dir)
    d = build_dir / "ui3d" / n
    shutil.rmtree(d, ignore_errors=True)
    d.mkdir(parents=True, exist_ok=True)
    for fname, data in files.items():
        (d / fname).write_bytes(data)
    from . import fonts
    packed = fonts.package(proj, build_dir, work_dir)
    # fuentes que usa la interfaz -> su archivo en el juego (texto dinamico); las de WoR ya estan en el juego
    lines = [f"{x['family']}\t{x['file'] or 'nativa'}\n" for x in packed["fonts"]]
    (d / "fonts.txt").write_text("".join(lines), encoding="utf-8", newline="\n")
    # logica (wui): si no compila, la interfaz se exporta igual pero sin logica, y se avisa
    from . import logic
    if logic.source(proj).strip():
        lr = logic.compile_project(proj)
        _, where = logic.assemble(proj)
        for dg in lr["diagnostics"]:
            if dg["severity"] in ("error", "warning"):
                warnings.append(f"logica ({logic.locate(where, dg['line'])}): {dg['message']}")
        if lr["bytes"]:
            (d / "logic.wuib").write_bytes(lr["bytes"])
        else:
            warnings.append("la logica tiene errores: la interfaz se exporto sin logica")
    for fam in packed["skipped"]:
        warnings.append(f"la fuente \"{fam}\" no se encontro como archivo TTF/OTF: el texto se ve bien (va horneado), "
                        "pero no se pudo copiar al juego")
    tmp = d / "scene.tmp"                       # la escena la ultima: re-wor recarga cuando cambia su fecha
    tmp.write_text(text, encoding="utf-8", newline="\n")
    tmp.replace(d / "scene.ui3d")
    qdir = game_dir / "qb"
    qfile = qdir / f"ui_{n}.q"
    qfile.write_text(src, encoding="utf-8", newline="\n")
    spec = importlib.util.spec_from_file_location("qbc", game_dir / "tools" / "qbc.py")
    qbc = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(qbc)
    try:
        data, _names = qbc.compile_source(src, f"scripts/re_wor/ui_{n}.qb")   # como CMake
    except qbc.QbcError as ex:
        raise ExportError(f"el compilador QB rechazo la interfaz: {ex}") from None
    out = build_dir / "qb" / f"ui_{n}.qb.xen"
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(data)
    return {"name": n, "q": str(qfile), "qb": str(out), "bytes": len(data), "warnings": warnings,
            "mode": proj.get("mode", "2d"), "scene_lines": text.count("\n"), "images": len(files)}
