"""Personaje para la vista previa de la app del movil: el mas ligero de los que ya ha montado el editor
(cache/band/*.glb), con sus texturas de color incrustadas y reducidas, sin mapas de normales.

Sale de TU copia del juego (los .glb de la cache se montan con sus archivos), asi que el resultado no se guarda en
git: vcam_app/assets/preview/character.glb esta en .gitignore. Uso (desde DEStudio):

    python -m editor.band.phone_model [salida.glb] [--max-tex 512]

Tambien compila el paquete JS de la vista previa (web/preview -> vcam_app/assets/preview/preview.js).
"""
from __future__ import annotations

import json
import shutil
import struct
import subprocess
import sys
from pathlib import Path
from urllib.parse import parse_qs, urlparse

from ..assets import textures
from ..core import paths

DEFAULT_OUT = paths.EDITOR / "vcam_app" / "assets" / "preview" / "character.glb"


def _read_glb(data: bytes) -> tuple[dict, bytes]:
    magic, _ver, _len = struct.unpack_from("<III", data, 0)
    if magic != 0x46546C67:
        raise ValueError("no es un .glb")
    off, js, binc = 12, None, b""
    while off < len(data):
        n, kind = struct.unpack_from("<II", data, off)
        chunk = data[off + 8: off + 8 + n]
        if kind == 0x4E4F534A:
            js = json.loads(chunk)
        elif kind == 0x004E4942:
            binc = chunk
        off += 8 + n
    return js, binc


def _write_glb(js: dict, binc: bytes) -> bytes:
    j = json.dumps(js, separators=(",", ":")).encode()
    j += b" " * (-len(j) % 4)
    binc += b"\0" * (-len(binc) % 4)
    total = 12 + 8 + len(j) + 8 + len(binc)
    return (struct.pack("<III", 0x46546C67, 2, total) + struct.pack("<II", len(j), 0x4E4F534A) + j
            + struct.pack("<II", len(binc), 0x004E4942) + binc)


INSTRUMENTS = ("guitar", "bass", "mic", "sticks", "drum")


def _parts(js: dict) -> list[dict]:
    """Nodos con malla del personaje, sin instrumentos."""
    return [n for n in js["nodes"] if "mesh" in n and not n["name"].startswith(INSTRUMENTS)]


def triangles(js: dict) -> int:
    n = 0
    for node in _parts(js):
        for p in js["meshes"][node["mesh"]]["primitives"]:
            if "indices" in p:
                n += js["accessors"][p["indices"]]["count"] // 3
    return n


def lightest(min_pieces: int = 3) -> Path:
    """El de menos triangulos entre los personajes vestidos (cuerpo + al menos min_pieces piezas: ropa, pelo...)."""
    best = None
    for f in sorted((paths.CACHE / "band").glob("*.glb")):
        try:
            js, _ = _read_glb(f.read_bytes())
        except Exception:  # noqa: BLE001
            continue
        if len(_parts(js)) < 1 + min_pieces:
            continue
        tri = triangles(js)
        if best is None or tri < best[0]:
            best = (tri, f)
    if not best:
        raise SystemExit("No hay personajes en cache/band: abre una cancion en el editor para que se monte la banda.")
    return best[1]


def phone_glb(src: Path, max_tex: int = 512) -> tuple[bytes, dict]:
    js, binc = _read_glb(src.read_bytes())
    # sin instrumentos: el nodo se queda (los indices no cambian) pero sin malla
    for n in js["nodes"]:
        if "mesh" in n and n["name"].startswith(INSTRUMENTS):
            n.pop("mesh"); n.pop("skin", None)
    used_mats = {p.get("material") for n in _parts(js) for p in js["meshes"][n["mesh"]]["primitives"]}
    used_imgs = set()
    for mi in used_mats:
        if mi is None:
            continue
        bt = js["materials"][mi].get("pbrMetallicRoughness", {}).get("baseColorTexture")
        if bt:
            used_imgs.add(js["textures"][bt["index"]]["source"])
    buf = bytearray(binc)
    # texturas de color: de URL del editor a PNG incrustado
    keep = set()
    for mat in js.get("materials", []):
        pbr = mat.get("pbrMetallicRoughness", {})
        if "baseColorTexture" in pbr:
            keep.add(js["textures"][pbr["baseColorTexture"]["index"]]["source"])
        mat.pop("normalTexture", None)
        mat["doubleSided"] = mat.get("doubleSided", True)
    embedded = 0
    for i, img in enumerate(js.get("images", [])):
        uri = img.get("uri")
        if not uri:
            continue
        png = b""
        if i in keep and i in used_imgs:
            q = parse_qs(urlparse(uri).query)
            try:
                png = textures.png(q["src"][0], int(q["key"][0], 16), max_tex)
            except Exception:  # noqa: BLE001
                png = b""
        if not png:   # 1x1 gris si no se pudo leer (no deja referencias a URL)
            png = bytes.fromhex("89504e470d0a1a0a0000000d49484452000000010000000108020000009077"
                                "53de0000000c4944415408d763787878000003840181a0fb6e3a0000000049454e44ae426082")
        while len(buf) % 4:
            buf.append(0)
        js.setdefault("bufferViews", []).append({"buffer": 0, "byteOffset": len(buf), "byteLength": len(png)})
        buf += png
        img.pop("uri")
        img["bufferView"] = len(js["bufferViews"]) - 1
        img["mimeType"] = "image/png"
        embedded += 1
    js["buffers"][0]["byteLength"] = len(buf)
    return _write_glb(js, bytes(buf)), {"source": src.name, "triangles": triangles(js), "textures": embedded}


def main(argv: list[str]) -> None:
    out = Path(argv[0]) if argv and not argv[0].startswith("--") else DEFAULT_OUT
    mt = int(argv[argv.index("--max-tex") + 1]) if "--max-tex" in argv else 512
    data, info = phone_glb(lightest(), mt)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(data)
    print(f"{out}  {len(data) / 1e6:.2f} MB  {info}")
    web = paths.EDITOR / "web"
    npx = shutil.which("npx")
    if npx and (web / "node_modules").exists():
        r = subprocess.run([npx, "vite", "build", "-c", "vite.preview.config.js"], cwd=web, capture_output=True, text=True)
        print("preview.js compilado" if r.returncode == 0 else "No se pudo compilar preview.js: " + r.stdout[-2000:] + r.stderr[-2000:])
    else:
        print("Falta Node o web/node_modules: compila preview.js con 'npx vite build -c vite.preview.config.js' en web/")


if __name__ == "__main__":
    main(sys.argv[1:])
