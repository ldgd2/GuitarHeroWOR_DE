"""Rutas del editor de interfaces (web: ui.html): imagenes del juego, proyectos, imagenes propias,
exportar al juego y probar en el juego abierto."""
from __future__ import annotations

import base64
from pathlib import Path
import io
import re

from ..core import paths
from ..core import projfile as PF
from ..ui import bridge, catalog, export, fonts, gameimg, imgedit, layered, logic
from .http import Binary, router



def _build_dir():
    return paths.GAME / "out" / "build" / "win-amd64-relwithdebinfo"


def _pid(v) -> str:
    """id estable del proyecto (no cambia al renombrarlo): nombre de su carpeta de trabajo"""
    v = re.sub(r"[^a-z0-9]", "", str(v or "").lower())[:32]
    if not v:
        raise ValueError("proyecto sin id")
    return v


def _work(pid: str):
    return PF.workdir("ui", _pid(pid))


# ------------------------------------------------------------------ imagenes del juego
@router.get("/api/ui/paks")
def paks(req):
    return {"paks": gameimg.list_paks(), "always": sorted(gameimg.ALWAYS_LOADED)}


@router.get("/api/ui/images")
def images(req):
    return {"pak": req.arg("pak"), "images": gameimg.index(req.arg("pak"))}


@router.get("/api/ui/image")
def image(req):
    return Binary(gameimg.png(req.arg("pak"), req.arg("key")), "image/png", cache=True)


@router.get("/api/ui/catalog")
def game_catalog(req):
    """scripts, variables, estructuras, pantallas y sonidos de los menus del juego (ui/catalog.py)"""
    try:
        return catalog.catalog()
    except FileNotFoundError as ex:
        return {"error": str(ex)}


# ------------------------------------------------------------------ proyectos (.desp de la biblioteca)
@router.get("/api/ui/projects")
def projects(req):
    return {"projects": PF.list_library("ui")}


@router.get("/api/ui/project")
def project_load(req):
    man, proj, assets = PF.load_library(req.arg("file"))
    if man["kind"] != "ui":
        raise ValueError("ese proyecto no es una interfaz")
    proj.setdefault("id", re.sub(r"[^a-z0-9]", "", req.arg("file").lower())[:24] or "ui")
    PF.extract_to_work("ui", _pid(proj["id"]), assets)
    proj["file"] = req.arg("file")
    return proj


@router.post("/api/ui/project")
def project_save(req):
    """Guarda en <estudio>/proyectos/<nombre>.desp con las imagenes propias que usa dentro."""
    proj = dict(req.json())
    prev = proj.pop("file", None)
    name = export.clean_name(proj.get("name")) or "interfaz"
    proj["name"] = name
    used = {L["image"]["file"] for scr in proj.get("screens", []) for L in scr.get("layers", [])
            if (L.get("image") or {}).get("custom")}
    used |= {f.get("file") for f in proj.get("fonts", []) if f.get("file")}
    assets = {k: v for k, v in PF.work_assets("ui", _pid(proj["id"])).items() if k in used}
    old = PF.library_file(name)
    created = (PF.read_manifest(old) or {}).get("created") if old.exists() else None
    data = PF.pack("ui", name, proj, assets, created=created, extra={"mode": proj.get("mode", "2d")})
    f = PF.save_library(name, data, kind="ui")
    # renombrado: el archivo con el nombre anterior era este mismo proyecto
    if prev and prev != f.stem:
        o = PF.library_file(prev)
        try:
            if o.exists() and PF.unpack(o.read_bytes())[1].get("id") == proj["id"]:
                o.unlink()
        except PF.ProjectFileError:
            pass
    return {"ok": True, "name": name, "file": f.stem}


# ------------------------------------------------------------------ fuentes: nativas de WoR y del sistema
@router.get("/api/ui/fonts")
def font_list(req):
    return {"wor": fonts.native_list(), "system": fonts.system_list()}


@router.get("/api/ui/worfont")
def wor_font(req):
    """metricas de una fuente nativa (?name=) o su atlas (?name=&png=1)"""
    info, png = fonts.native_font(req.arg("name"))
    if req.arg("png", ""):
        return Binary(png.read_bytes(), "image/png")
    return info


@router.get("/api/ui/sysfont")
def sys_font(req):
    f = fonts.system_file(req.arg("file"))
    return Binary(f.read_bytes(), {".otf": "font/otf"}.get(f.suffix.lower(), "font/ttf"))


# ------------------------------------------------------------------ recursos propios (imagenes, SVG, fuentes)
KINDS = {".png": ("image", "image/png"), ".jpg": ("image", "image/jpeg"), ".jpeg": ("image", "image/jpeg"),
         ".webp": ("image", "image/webp"), ".svg": ("svg", "image/svg+xml"), ".ttf": ("font", "font/ttf"),
         ".otf": ("font", "font/otf"), ".woff": ("font", "font/woff"), ".woff2": ("font", "font/woff2")}


def _svg_size(data: bytes):
    head = data[:4096].decode("utf-8", "replace")
    m = re.search(r"<svg\b[^>]*>", head, re.S)
    tag = m.group(0) if m else ""
    def num(attr):
        mm = re.search(attr + r"""\s*=\s*["']\s*([0-9.]+)\s*(px)?\s*["']""", tag)
        return float(mm.group(1)) if mm else 0
    w, h = num(r"\bwidth"), num(r"\bheight")
    vb = re.search(r"""viewBox\s*=\s*["']\s*[-0-9.]+[\s,]+[-0-9.]+[\s,]+([0-9.]+)[\s,]+([0-9.]+)""", tag)
    if (not w or not h) and vb:
        w, h = float(vb.group(1)), float(vb.group(2))
    return round(w or 256), round(h or 256)


def _info(f):
    kind, _ = KINDS.get(f.suffix.lower(), ("other", ""))
    w = h = 0
    try:
        if kind == "image":
            from PIL import Image
            with Image.open(f) as im:
                w, h = im.size
        elif kind == "svg":
            w, h = _svg_size(f.read_bytes())
    except Exception:  # noqa: BLE001
        pass
    return {"file": f.name, "kind": kind, "w": w, "h": h, "size": f.stat().st_size}


@router.post("/api/ui/upload")
def upload(req):
    """{pid, filename, data: base64} -> carpeta de trabajo del proyecto (entra en el .desp al guardar).
    PNG, JPG, WEBP, SVG (vectorial: se dibuja nitida a cualquier tamano) y fuentes TTF, OTF, WOFF, WOFF2."""
    b = req.json()
    fn = b.get("filename") or "archivo.png"
    ext = ("." + fn.rsplit(".", 1)[-1].lower()) if "." in fn else ""
    if ext not in KINDS:
        raise ValueError("formato no admitido: PNG, JPG, WEBP, SVG, TTF, OTF, WOFF o WOFF2")
    stem = re.sub(r"[^a-z0-9_]+", "_", fn.rsplit(".", 1)[0].lower()).strip("_") or "archivo"
    data = base64.b64decode(b.get("data") or "")
    if ext == ".svg" and b"<svg" not in data[:8192]:
        raise ValueError("ese archivo no parece un SVG")
    if len(data) > 40 * 1024 * 1024:
        raise ValueError("archivo demasiado grande (40 MB como mucho)")
    f = _work(b.get("pid")) / f"{stem}{'.jpg' if ext == '.jpeg' else ext}"
    f.write_bytes(data)
    return _info(f)


@router.post("/api/ui/import_layered")
def import_layered(req):
    """{pid, filename, data: base64} de un PSD/PSB, AI o PDF -> cada capa como PNG en la carpeta de trabajo.
    -> {w, h, layers: [{name, file, x, y, w, h, opacity, visible, group}], warnings} (px del documento)"""
    b = req.json()
    fn = b.get("filename") or "diseno.psd"
    data = base64.b64decode(b.get("data") or "")
    try:
        doc = layered.read(data, fn)
    except layered.LayeredError as ex:
        return {"error": str(ex)}
    stem = re.sub(r"[^a-z0-9_]+", "_", fn.rsplit(".", 1)[0].lower()).strip("_")[:24] or "diseno"
    work = _work(b.get("pid"))
    out = []
    for i, L in enumerate(doc["layers"]):
        nm = re.sub(r"[^a-z0-9_]+", "_", L["name"].lower()).strip("_")[:24] or "capa"
        f = work / f"{stem}__{i:02d}_{nm}.png"
        f.write_bytes(L["png"])
        out.append({k: v for k, v in L.items() if k != "png"} | {"file": f.name})
    return {"w": doc["w"], "h": doc["h"], "layers": out, "warnings": doc.get("warnings", []), "name": fn.rsplit(".", 1)[0]}


def _open_image(f):
    from PIL import Image
    if f.suffix.lower() == ".svg":
        import pymupdf
        doc = pymupdf.open(stream=f.read_bytes(), filetype="svg")
        pg = doc[0]
        k = max(1.0, 1024 / max(pg.rect.width, pg.rect.height, 1))
        pix = pg.get_pixmap(matrix=pymupdf.Matrix(k, k), alpha=True)
        return Image.frombytes("RGBA", (pix.width, pix.height), pix.samples)
    return Image.open(f).convert("RGBA")


@router.get("/api/ui/imgedit/ops")
def imgedit_ops(req):
    return {"ops": imgedit.describe()}


@router.post("/api/ui/imgedit")
def imgedit_run(req):
    """{pid, src, ops, preview} -> preview: {png: base64, w, h} (reducida); si no: guarda <src>__ed.png -> info del archivo"""
    b = req.json()
    work = _work(b.get("pid"))
    src = work / Path(b.get("src") or "").name
    if not src.is_file():
        raise ValueError("no encuentro la imagen original en el proyecto")
    im = imgedit.apply(_open_image(src), b.get("ops") or [])
    if b.get("preview"):
        full = im.size
        k = min(1.0, 900 / max(im.size))
        if k < 1:
            im = im.resize((max(1, round(im.width * k)), max(1, round(im.height * k))))
        buf = io.BytesIO()
        im.save(buf, "PNG")
        return {"png": base64.b64encode(buf.getvalue()).decode(), "w": full[0], "h": full[1], "k": k}
    import hashlib
    import json as _json
    tag = hashlib.sha1(_json.dumps(b.get("ops") or [], sort_keys=True).encode()).hexdigest()[:8]
    out = work / f"{src.stem.split('__ed')[0]}__ed_{tag}.png"   # un archivo por receta: capas distintas no se pisan
    im.save(out, "PNG", optimize=True)
    return _info(out)


@router.get("/api/ui/assets")
def assets(req):
    """recursos propios del proyecto (los que hay en su carpeta de trabajo)"""
    d = _work(req.arg("pid"))
    return {"assets": [_info(f) for f in sorted(d.iterdir()) if f.is_file() and f.suffix.lower() in KINDS]}


@router.get("/api/ui/custom")
def custom(req):
    d = _work(req.arg("pid")).resolve()
    f = (d / req.arg("file")).resolve()
    if not f.is_relative_to(d) or not f.is_file():
        raise KeyError(req.arg("file"))
    return Binary(f.read_bytes(), KINDS.get(f.suffix.lower(), ("", "application/octet-stream"))[1])


# ------------------------------------------------------------------ exportar y probar
def _unpack(b: dict):
    """cuerpo {project, textures: {id de capa: {png: base64, pad, w, h}}}"""
    proj = b.get("project") if "project" in b else b
    tex = {}
    for k, v in (b.get("textures") or {}).items():
        if isinstance(v, dict):
            tex[k] = {**v, "png": base64.b64decode(v.get("png") or "")}
    work = _work(proj["id"]) if proj.get("id") else None
    return proj, tex, work


@router.post("/api/ui/export")
def do_export(req):
    proj, texts, work = _unpack(req.json())
    try:
        return export.write(proj, paths.GAME, _build_dir(), texts, work)
    except export.ExportError as ex:
        return {"error": str(ex)}


# ------------------------------------------------------------------ logica (wui)
@router.post("/api/ui/wui/compile")
def wui_compile(req):
    """{"project": proyecto, "source": texto opcional} -> {"ok", "wuib" (base64), "diagnostics"}"""
    b = req.json() or {}
    return logic.compile_b64(b.get("project") or {}, b.get("source"))


@router.get("/api/ui/wui/api")
def wui_api(req):
    """funciones, metodos, propiedades y eventos del lenguaje (ayuda y autocompletado del editor)"""
    return logic.api()


@router.post("/api/ui/qb")
def preview_qb(req):
    """Lo que se genera (el .q y, en 3D, el principio de la escena) para verlo en el editor."""
    proj, texts, work = _unpack(req.json())
    try:
        n, src = export.to_q(proj)
        text, files, warnings = export.scene(proj, texts, work)
        lines = text.splitlines()
        out = {"name": n, "q": src, "warnings": warnings,
               "scene": "\n".join(lines[:80]) + (f"\n... ({len(lines)} lineas, {len(files)} imagenes)" if len(lines) > 80 else "")}
        return out
    except export.ExportError as ex:
        return {"error": str(ex)}


@router.post("/api/ui/test")
def test_in_game(req):
    """Exporta y la muestra en el juego abierto: carga el .qb.xen y ejecuta show (o hide)."""
    b = req.json()
    action = b.get("action", "show")
    proj, texts, work = _unpack(b)
    try:
        if action == "show":
            res = export.write(proj, paths.GAME, _build_dir(), texts, work)
            out = bridge.send(_build_dir(), [f"loadqb {res['qb']}", f"run re_ui_{res['name']}_show"])
            return {"ok": all(r.lower().startswith("ok") for r in out), "replies": out, "warnings": res["warnings"]}
        n = export.clean_name(proj.get("name"))
        cmd = {"hide": f"run re_ui_{n}_hide", "hide_now": f"run re_ui_{n}_hide_now"}[action]
        out = bridge.send(_build_dir(), [cmd])
        return {"ok": all(r.lower().startswith("ok") for r in out), "replies": out}
    except (export.ExportError, bridge.BridgeError) as ex:
        return {"error": str(ex)}
