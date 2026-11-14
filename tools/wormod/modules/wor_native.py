"""Mods ya en formato Warriors of Rock (skins, venues, highways, UI, canciones de reemplazo...).

Acepta: carpeta con mod.toml, carpeta con data/, carpeta de archivos sueltos, .zip o un archivo.
Al instalar:
  * cada archivo se coloca en la ruta del juego (data/...). Los archivos sueltos se ubican buscando un
    archivo con el mismo nombre en el juego; si no existe se deduce por la extension.
  * se iguala la compresion del original: si el juego lo tiene en CHNK, el reemplazo se comprime
    (si no, el juego no lo lee); si el original va sin comprimir, se descomprime.
"""
from __future__ import annotations

import shutil
import tempfile
import zipfile
from pathlib import Path

from modules.base import Detection, Log, Module, Report
from registry import Registry, read_manifest, write_manifest

GAME_EXTS = (".xen",)
_INDEX: dict[str, dict[str, str]] = {}


def asset_index(assets: Path) -> dict[str, str]:
    """{ruta relativa en minusculas -> ruta real} de assets/data, y {nombre -> ruta} si es unico."""
    key = str(assets)
    if key in _INDEX:
        return _INDEX[key]
    idx: dict[str, str] = {}
    names: dict[str, list[str]] = {}
    data = assets / "data"
    for f in data.rglob("*"):
        if f.is_file():
            rel = f.relative_to(data).as_posix()
            idx[rel.lower()] = rel
            names.setdefault(f.name.lower(), []).append(rel)
    for n, rels in names.items():
        if len(rels) == 1:
            idx["name:" + n] = rels[0]
    _INDEX[key] = idx
    return idx


def guess_kind(rels: list[str]) -> str:
    low = [r.lower() for r in rels]
    if any(r.startswith("zones/") or "/z_" in r or r.startswith("z_") for r in low):
        return "venue"
    if any(r.startswith("songs/") or r.startswith("music/") for r in low):
        return "song"
    if any(r.startswith("ui_images/") or r.startswith("ui/") for r in low):
        return "ui"
    if any("highway" in r or "gem" in r for r in low):
        return "highway"
    if any(r.startswith("pak/") for r in low):
        return "skin"
    return "other"


def _default_dir(name: str) -> str:
    n = name.lower()
    if n.endswith(".fsb.xen"):
        return "music"
    if n.endswith("_song.pak.xen"):
        return "songs"
    if n.endswith(".img.xen"):
        return "ui_images"
    if n.startswith("z_") and (n.endswith(".pak.xen") or n.endswith(".pab.xen")):
        return "zones"
    return "pak"


def collect(root: Path, assets: Path) -> tuple[list[tuple[Path, str]], list[str]]:
    """[(archivo, ruta destino relativa a data/)] y avisos."""
    idx = asset_index(assets)
    out, notes = [], []
    data = next((d for d in [root / "data", root / "DATA", root / "Data"] if d.is_dir()), None)
    if data is not None:
        for f in sorted(data.rglob("*")):
            if f.is_file():
                rel = f.relative_to(data).as_posix()
                out.append((f, idx.get(rel.lower(), rel)))
        return out, notes
    for f in sorted(root.rglob("*")):
        if not f.is_file() or not f.name.lower().endswith(GAME_EXTS):
            continue
        rel = idx.get("name:" + f.name.lower())
        if rel is None:
            rel = f"{_default_dir(f.name)}/{f.name}"
            notes.append(f"{f.name}: no existe en el juego, se instala como archivo nuevo en data/{rel}")
        out.append((f, rel))
    return out, notes


class WorNativeModule(Module):
    name = "wor_native"
    kinds = ("skin", "venue", "song", "highway", "ui", "other")

    def detect(self, path: Path) -> Detection | None:
        path = Path(path)
        if path.is_file() and path.suffix.lower() == ".zip":
            return Detection(self.name, "other", path, path.stem, True,
                             [f"{path.name}: se descomprime al instalar"])
        if path.is_file() and path.name.lower().endswith(GAME_EXTS):
            return Detection(self.name, "other", path, path.name, True)
        if not path.is_dir():
            return None
        if (path / "song.ini").exists():
            return None  # cancion de WTDE: otro modulo
        man, _ = read_manifest(path)
        has_data = any((path / d).is_dir() for d in ("data", "DATA", "Data"))
        files = [f for f in path.rglob("*") if f.is_file() and f.name.lower().endswith(GAME_EXTS)]
        if not (man or has_data or files):
            return None
        rels = [f.relative_to(path).as_posix() for f in files]
        kind = str(man.get("type") or guess_kind([r.split("data/", 1)[-1] for r in rels]))
        return Detection(self.name, kind, path, str(man.get("name") or path.name), True,
                         [f"{len(files)} archivo(s) del juego"])

    def install(self, det: Detection, ws, log: Log, name: str | None = None, type_: str | None = None,
                **_) -> Report:
        from wor_pak import chnk, dechnk
        rep = Report(f"Instalar mod: {det.label}")
        tmp = None
        src = Path(det.source)
        try:
            if src.is_file() and src.suffix.lower() == ".zip":
                tmp = Path(tempfile.mkdtemp(prefix="wormod_"))
                with zipfile.ZipFile(src) as z:
                    for m in z.namelist():  # sin rutas absolutas ni ".."
                        if m.startswith(("/", "\\")) or ".." in Path(m).parts:
                            rep.error(f"zip con ruta no valida: {m}")
                            return rep
                    z.extractall(tmp)
                kids = [k for k in tmp.iterdir()]
                src = kids[0] if len(kids) == 1 and kids[0].is_dir() else tmp
                if src is not tmp and name is None:
                    name = src.name
            if src.is_file():
                one = Path(tempfile.mkdtemp(prefix="wormod_"))
                shutil.copy2(src, one / src.name)
                tmp = tmp or one
                src = one
            files, notes = collect(src, ws.assets)
            for n in notes:
                rep.warn(n)
            if not files:
                rep.error("No hay archivos del juego (.xen) para instalar")
                return rep
            man, _ = read_manifest(src)
            reg = Registry(ws)
            folder = reg.unique_folder(name or str(man.get("name") or det.label))
            dst = ws.mods / folder
            kind = type_ or str(man.get("type") or guess_kind([r for _, r in files]))
            replaced = added = recompressed = 0
            for f, rel in files:
                out = dst / "data" / Path(rel)
                out.parent.mkdir(parents=True, exist_ok=True)
                orig = ws.assets / "data" / Path(rel)
                data = f.read_bytes()
                if orig.exists():
                    replaced += 1
                    o_chnk = orig.open("rb").read(4) == b"CHNK"
                    if o_chnk and data[:4] != b"CHNK":
                        data, recompressed = chnk(data), recompressed + 1
                    elif not o_chnk and data[:4] == b"CHNK":
                        data, recompressed = dechnk(data), recompressed + 1
                else:
                    added += 1
                out.write_bytes(data)
                log(f"  {rel}")
            write_manifest(dst, name or str(man.get("name") or det.label), kind,
                           str(man.get("author", "")), str(man.get("version", "1")),
                           str(man.get("description", "")))
            rep.mod_folder = folder
            rep.info(f"Instalado en mods/{folder} (tipo {kind})")
            rep.info(f"{replaced} archivo(s) reemplazan originales, {added} nuevo(s)")
            if recompressed:
                rep.info(f"{recompressed} archivo(s) ajustados a la compresion del juego (CHNK)")
            rep.info("Se carga al reiniciar el juego")
            return rep
        finally:
            if tmp is not None:
                shutil.rmtree(tmp, ignore_errors=True)
