"""Fuentes del editor de interfaces.

- Nativas de Warriors of Rock: las .fnt de pak/ui_shared.pak.xen (siempre cargado). Cada una lleva su
  textura dentro. Formato (big endian):
      0x00  f32 ?, f32 ancho del espacio, f32 espaciado entre letras, f32 ?, f32 alto de linea
      0x14  u16 ?, u16 ?
      0x18  u16[65536] codigo unicode -> numero de glifo (0 = no hay)
      0x200A8 + n*36  glifo n: f32 u0 v0 u1 v1 (0..1), ancho, alto (px), bajada bajo la linea base,
                      desplazamiento x, avance extra
      despues         la textura (cabecera 0x0A28..., la misma que las .img)
  Se decodifican a cache/ui_fonts/wor/<nombre>.png + .json (lo dibuja el navegador; no va a git).
- Del sistema: las TTF/OTF/TTC instaladas en Windows (todas las del equipo y las del usuario).
- Empaquetado: al exportar, cada archivo de fuente que usa la interfaz se copia a
  <build>/ui3d/fonts/<sha1>.<ext>. El nombre sale del contenido: si ya esta, no se vuelve a copiar
  (la misma fuente nunca se pone dos veces, aunque la usen varias interfaces).
"""
from __future__ import annotations

import hashlib
import json
import os
import shutil
import struct
import threading
from pathlib import Path

from ..core import paths

CACHE = paths.CACHE / "ui_fonts"
_lock = threading.Lock()

# ------------------------------------------------------------------ nativas de WoR
FNT_TYPE = 0x7E1ABC70
NATIVE_LABELS = {
    "fontgrid_text_a1": "Texto de menus",
    "fontgrid_text_a3": "Texto de menus (fina)",
    "fontgrid_title_a1": "Titulos",
    "fontgrid_lyric_a1": "Letras de canciones",
    "fontgrid_numeral_a1": "Numeros (puntuacion)",
    "fontgrid_numeral_a1_b": "Numeros B",
    "fontgrid_numeral_a2": "Numeros 2",
    "debug": "Depuracion",
    "buttons_x360": "Botones del mando",
}
_native: dict[str, bytes] | None = None


def _stem(name: str) -> str:
    n = name.rsplit("/", 1)[-1]
    for suf in (".xen", ".fnt"):
        if n.lower().endswith(suf):
            n = n[: -len(suf)]
    return n


def _native_data() -> dict[str, bytes]:
    global _native
    if _native is None:
        from ..core.names import sdk_keys
        from ..formats import pak
        N = sdk_keys()
        out = {}
        for e in pak.entries("pak/ui_shared.pak.xen"):
            if e.type == FNT_TYPE:
                out[_stem(N.get(e.fullname, f"{e.fullname:08x}"))] = e.data
        _native = out
    return _native


def native_list() -> list[dict]:
    try:
        order = list(NATIVE_LABELS)
        names = sorted(_native_data(), key=lambda n: (order.index(n.lower()) if n.lower() in order else 99, n.lower()))
    except FileNotFoundError:
        return []
    return [{"id": "wor:" + n, "name": n, "label": NATIVE_LABELS.get(n.lower(), n)} for n in names]


def _decode_native(name: str) -> dict:
    from ..formats.texture import decode
    from . import gameimg
    d = _native_data()[name]
    space, spacing, line_h = struct.unpack_from(">f", d, 4)[0], struct.unpack_from(">f", d, 8)[0], \
        struct.unpack_from(">f", d, 16)[0]
    cmap = struct.unpack_from(">65536H", d, 0x18)
    count = max(cmap) + 1
    t = d.find(b"\x0a\x28", 0x200A8 + count * 36 - 36)
    ti = gameimg._texinfo(d[t:])
    img = decode(d[t:], ti)
    if img is None:
        raise ValueError(f"textura de {name} en un formato no soportado")
    W, H = img.size
    glyphs, ascent, descent = {}, 0.0, 0.0
    for cp, g in enumerate(cmap):
        if not g:
            continue
        u0, v0, u1, v1, w, h, down, ox, adv = struct.unpack_from(">9f", d, 0x200A8 + g * 36)
        if not (0 <= u0 <= u1 <= 1 and 0 <= v0 <= v1 <= 1):
            continue
        glyphs[cp] = [round(u0 * W), round(v0 * H), round((u1 - u0) * W), round((v1 - v0) * H),
                      w, h, down, ox, adv]
        ascent, descent = max(ascent, h - down), max(descent, down)
    return {"name": name, "w": W, "h": H, "space": space or line_h * 0.3, "spacing": spacing,
            "line": line_h, "ascent": ascent, "descent": descent, "glyphs": glyphs, "img": img}


def native_font(name: str) -> tuple[dict, Path]:
    """-> (metricas, png del atlas). Se decodifica una vez y queda en cache."""
    if name not in _native_data():
        raise KeyError(name)
    d = CACHE / "wor"
    js, png = d / f"{name}.json", d / f"{name}.png"
    with _lock:
        if not (js.exists() and png.exists()):
            d.mkdir(parents=True, exist_ok=True)
            info = _decode_native(name)
            info.pop("img").save(png)
            js.write_text(json.dumps(info), encoding="utf-8")
    return json.loads(js.read_text(encoding="utf-8")), png


# ------------------------------------------------------------------ del sistema
def _font_dirs() -> list[Path]:
    out = [Path(os.environ.get("WINDIR", r"C:\Windows")) / "Fonts"]
    la = os.environ.get("LOCALAPPDATA")
    if la:
        out.append(Path(la) / "Microsoft" / "Windows" / "Fonts")
    return [p for p in out if p.is_dir()]


def _sfnt_info(data: bytes, off: int = 0) -> dict | None:
    """familia, estilo, peso e italica de una fuente TrueType / OpenType (tabla name y OS/2)"""
    if data[off:off + 4] == b"ttcf":
        off = struct.unpack_from(">I", data, 12)[0]
    num = struct.unpack_from(">H", data, off + 4)[0]
    tables = {}
    for i in range(num):
        tag, _, toff, tlen = struct.unpack_from(">4sIII", data, off + 12 + i * 16)
        tables[tag] = (toff, tlen)
    if b"name" not in tables:
        return None
    no, _ = tables[b"name"]
    _, count, soff = struct.unpack_from(">HHH", data, no)
    names: dict[int, tuple[int, str]] = {}
    for i in range(count):
        pid, eid, lid, nid, ln, so = struct.unpack_from(">6H", data, no + 6 + i * 12)
        if nid not in (1, 2, 4, 16, 17):
            continue
        raw = data[no + soff + so: no + soff + so + ln]
        if pid == 3:
            s, rank = raw.decode("utf-16-be", "replace"), (0 if lid == 0x409 else 1)
        elif pid == 1 and eid == 0:
            s, rank = raw.decode("mac_roman", "replace"), 2
        else:
            continue
        if nid not in names or rank < names[nid][0]:
            names[nid] = (rank, s.strip())
    fam = (names.get(16) or names.get(1) or (0, ""))[1]
    sub = (names.get(17) or names.get(2) or (0, "Regular"))[1]
    if not fam or fam.startswith("."):
        return None
    weight, italic = 400, "italic" in sub.lower() or "oblique" in sub.lower()
    if b"OS/2" in tables:
        oo, ol = tables[b"OS/2"]
        if ol >= 64:
            weight = struct.unpack_from(">H", data, oo + 4)[0] or 400
            italic = italic or bool(struct.unpack_from(">H", data, oo + 62)[0] & 1)
    return {"family": fam, "style": sub, "weight": max(100, min(900, weight)), "italic": italic,
            "full": (names.get(4) or (0, f"{fam} {sub}"))[1]}


def system_list() -> list[dict]:
    """[{family, faces: [{id, style, weight, italic, file}]}] ordenadas por nombre (cache por fecha de carpetas)"""
    files = [f for d in _font_dirs() for f in d.iterdir()
             if f.suffix.lower() in (".ttf", ".otf", ".ttc") and f.is_file()]
    sig = hashlib.sha1("|".join(f"{f}:{f.stat().st_size}:{int(f.stat().st_mtime)}" for f in sorted(files))
                       .encode("utf-8", "replace")).hexdigest()
    cf = CACHE / "system.json"
    with _lock:
        try:
            c = json.loads(cf.read_text(encoding="utf-8"))
            if c.get("sig") == sig:
                return c["families"]
        except (OSError, ValueError, KeyError):
            pass
        fams: dict[str, list] = {}
        for f in files:
            try:
                with open(f, "rb") as fh:
                    data = fh.read()
                info = _sfnt_info(data)
            except (OSError, struct.error, IndexError):
                continue
            if not info:
                continue
            fams.setdefault(info["family"], []).append(
                {"style": info["style"], "full": info["full"], "weight": info["weight"], "italic": info["italic"], "file": str(f)})
        out = []
        for fam in sorted(fams, key=str.lower):
            faces = sorted(fams[fam], key=lambda x: (x["italic"], x["weight"]))
            seen, uniq = set(), []
            for x in faces:
                k = (x["weight"], x["italic"])
                if k not in seen:
                    seen.add(k)
                    uniq.append(x)
            out.append({"family": fam, "faces": uniq})
        CACHE.mkdir(parents=True, exist_ok=True)
        cf.write_text(json.dumps({"sig": sig, "families": out}), encoding="utf-8")
        return out


def system_file(path: str) -> Path:
    """solo archivos de las carpetas de fuentes del sistema"""
    f = Path(path).resolve()
    if f.suffix.lower() not in (".ttf", ".otf", ".ttc") or not f.is_file() \
            or not any(f.is_relative_to(d.resolve()) for d in _font_dirs()):
        raise KeyError(path)
    return f


def family_files(family: str) -> list[Path]:
    """archivos de una familia ("Arial") o de una cara con nombre propio ("Arial Black")"""
    want = family.lower()
    fams = system_list()
    for fam in fams:
        if fam["family"].lower() == want:
            return [Path(x["file"]) for x in fam["faces"]]
    for fam in fams:
        for x in fam["faces"]:
            if f"{fam['family']} {x['style']}".lower() == want:
                return [Path(x["file"])]
    return []


# ------------------------------------------------------------------ empaquetado
def _sha1(f: Path) -> str:
    h = hashlib.sha1()
    with open(f, "rb") as fh:
        for chunk in iter(lambda: fh.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def used_fonts(proj: dict) -> set[str]:
    out = set()
    for scr in proj.get("screens", []):
        for L in scr.get("layers", []):
            if L.get("type") == "text":
                out.add((L.get("text") or {}).get("font") or "Inter Variable")
    return out


def package(proj: dict, build_dir: Path, work_dir: Path | None) -> dict:
    """Copia al juego los archivos de las fuentes que usa la interfaz (las del proyecto y las del sistema).
    Las nativas de WoR ya estan en el juego. Devuelve {fonts: [{family, file, origin, copied}], skipped: [...]}"""
    dest = build_dir / "ui3d" / "fonts"
    own = {f.get("family"): f.get("file") for f in proj.get("fonts", []) if f.get("family")}
    report, skipped = [], []
    for fam in sorted(used_fonts(proj)):
        if fam.startswith("wor:"):
            report.append({"family": fam, "file": None, "origin": "wor", "copied": False})
            continue
        if fam in own and work_dir and (work_dir / own[fam]).is_file():
            srcs, origin = [work_dir / own[fam]], "proyecto"
        else:
            srcs, origin = family_files(fam), "sistema"
        srcs = [s for s in srcs if s.suffix.lower() in (".ttf", ".otf", ".ttc")]
        if not srcs:
            skipped.append(fam)
            continue
        for s in srcs:
            name = f"{_sha1(s)[:20]}{s.suffix.lower()}"
            target = dest / name
            copied = False
            if not target.exists() or target.stat().st_size != s.stat().st_size:
                dest.mkdir(parents=True, exist_ok=True)
                tmp = target.with_suffix(target.suffix + ".tmp")
                shutil.copyfile(s, tmp)
                tmp.replace(target)
                copied = True
            report.append({"family": fam, "file": f"ui3d/fonts/{name}", "origin": origin, "copied": copied})
    return {"fonts": report, "skipped": skipped}
