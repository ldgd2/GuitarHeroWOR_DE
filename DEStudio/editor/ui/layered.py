"""Archivos con capas (Photoshop .psd/.psb, Illustrator .ai, PDF) -> capas sueltas en PNG para una pantalla.

    read(data, filename) -> {"w", "h", "layers": [{"name", "png", "x", "y", "w", "h", "opacity", "visible", "group"}]}

x, y, w, h en pixeles del documento (x, y = esquina superior izquierda); group = lista de nombres de grupos
(de fuera hacia dentro); las capas van de abajo arriba (la ultima se dibuja encima).

PSD: lector propio (sin dependencias): RGB y escala de grises de 8 bits, sin comprimir, RLE (PackBits) y ZIP.
     Las capas de texto, formas y objetos inteligentes se importan como la imagen que Photoshop guarda de ellas.
     Los ajustes, efectos de capa y modos de fusion no se aplican (se avisa).
AI/PDF: con PyMuPDF; cada capa de Illustrator (grupo de contenido opcional del PDF) por separado, si las hay.
"""
from __future__ import annotations

import io
import struct
import zlib

from PIL import Image

MAX_SIDE = 8192


class LayeredError(Exception):
    pass


def read(data: bytes, filename: str) -> dict:
    ext = filename.rsplit(".", 1)[-1].lower() if "." in filename else ""
    if data[:4] == b"8BPS":
        try:
            import psd_tools  # noqa: F401
        except ImportError:
            return read_psd(data)     # sin la libreria: lector propio (sin efectos ni fusiones)
        return read_psd_full(data)
    if ext in ("ai", "pdf") or data[:5] == b"%PDF-":
        return read_pdf(data)
    raise LayeredError("formato no admitido: PSD, PSB, AI o PDF")


# ================================================================== Photoshop
class _R:
    def __init__(self, b: bytes, psb: bool):
        self.b, self.p, self.psb = b, 0, psb

    def take(self, n: int) -> bytes:
        if self.p + n > len(self.b):
            raise LayeredError("archivo PSD cortado")
        v = self.b[self.p:self.p + n]
        self.p += n
        return v

    def u8(self): return self.take(1)[0]
    def u16(self): return struct.unpack(">H", self.take(2))[0]
    def i16(self): return struct.unpack(">h", self.take(2))[0]
    def u32(self): return struct.unpack(">I", self.take(4))[0]
    def i32(self): return struct.unpack(">i", self.take(4))[0]
    def ulen(self): return struct.unpack(">Q", self.take(8))[0] if self.psb else self.u32()


def _packbits(src: bytes, size: int) -> bytes:
    out = bytearray()
    i, n = 0, len(src)
    while i < n and len(out) < size:
        c = src[i]
        i += 1
        if c < 128:
            out += src[i:i + c + 1]
            i += c + 1
        elif c > 128:
            if i < n:
                out += bytes([src[i]]) * (257 - c)
            i += 1
    return bytes(out[:size].ljust(size, b"\0"))


def _channel(r: _R, length: int, w: int, h: int) -> bytes:
    """datos de un canal de una capa (8 bits): w*h bytes"""
    end = r.p + length
    if length < 2:
        r.p = end
        return bytes(w * h)
    comp = r.u16()
    if w <= 0 or h <= 0:
        r.p = end
        return b""
    size = w * h
    if comp == 0:
        out = r.take(min(size, end - r.p)).ljust(size, b"\0")
    elif comp == 1:
        counts = [r.u32() if r.psb else r.u16() for _ in range(h)]
        rows = []
        for c in counts:
            rows.append(_packbits(r.take(c), w))
        out = b"".join(rows)
    elif comp in (2, 3):
        raw = zlib.decompress(r.take(end - r.p))
        out = bytearray(raw[:size].ljust(size, b"\0"))
        if comp == 3:   # con prediccion: cada byte es la diferencia con el de su izquierda
            for y in range(h):
                o = y * w
                for x in range(1, w):
                    out[o + x] = (out[o + x] + out[o + x - 1]) & 0xFF
        out = bytes(out)
    else:
        raise LayeredError(f"compresion {comp} desconocida en el PSD")
    r.p = end
    return out


def read_psd(data: bytes) -> dict:
    r = _R(data, False)
    if r.take(4) != b"8BPS":
        raise LayeredError("no es un archivo de Photoshop")
    ver = r.u16()
    if ver not in (1, 2):
        raise LayeredError(f"version {ver} de PSD desconocida")
    r.psb = ver == 2
    r.take(6)
    nch, H, W, depth, mode = r.u16(), r.u32(), r.u32(), r.u16(), r.u16()
    if depth != 8:
        raise LayeredError(f"el PSD es de {depth} bits por canal: guardalo en 8 bits (Imagen > Modo > 8 bits/canal)")
    if mode not in (1, 3):   # 1 escala de grises, 3 RGB
        raise LayeredError("el PSD tiene que estar en modo RGB o escala de grises (Imagen > Modo > Color RGB)")
    r.take(r.u32())          # datos de modo de color
    r.take(r.u32())          # recursos de imagen
    lm_len = r.ulen()
    lm_end = r.p + lm_len
    layers: list[dict] = []
    warnings: list[str] = []
    if lm_len:
        li_len = r.ulen()
        li_end = r.p + li_len
        if li_len:
            count = abs(r.i16())
            recs = []
            for _ in range(count):
                top, left, bottom, right = r.i32(), r.i32(), r.i32(), r.i32()
                chans = [(r.i16(), r.ulen()) for _ in range(r.u16())]
                r.take(4)
                blend = r.take(4)
                opacity, _clip, flags, _ = r.u8(), r.u8(), r.u8(), r.u8()
                extra_len = r.u32()
                extra_end = r.p + extra_len
                r.take(r.u32())           # mascara
                r.take(r.u32())           # rangos de fusion
                nlen = r.u8()
                name = r.take(nlen).decode("latin-1", "replace")
                r.p += (4 - (nlen + 1) % 4) % 4
                section, uname = 0, None
                while r.p + 12 <= extra_end:      # bloques extra: nombre unicode, grupos
                    sig, key = r.take(4), r.take(4)
                    big = r.psb and key in (b"LMsk", b"Lr16", b"Lr32", b"Layr", b"Mt16", b"Mt32", b"Mtrn", b"Alph",
                                            b"FMsk", b"lnk2", b"FEid", b"FXid", b"PxSD")
                    ln = struct.unpack(">Q", r.take(8))[0] if big else r.u32()
                    blk = r.take(ln)
                    if sig not in (b"8BIM", b"8B64"):
                        break
                    if key == b"luni" and len(blk) >= 4:
                        n = struct.unpack(">I", blk[:4])[0]
                        uname = blk[4:4 + n * 2].decode("utf-16-be", "replace").rstrip("\0")
                    elif key in (b"lsct", b"lsdk") and len(blk) >= 4:
                        section = struct.unpack(">I", blk[:4])[0]
                r.p = extra_end
                recs.append({"rect": (top, left, bottom, right), "chans": chans, "opacity": opacity,
                             "visible": not (flags & 2), "name": uname or name, "section": section,
                             "blend": blend.decode("latin-1")})
            # datos de los canales, en el mismo orden
            for rec in recs:
                top, left, bottom, right = rec["rect"]
                w, h = right - left, bottom - top
                planes = {}
                for cid, ln in rec["chans"]:
                    planes[cid] = _channel(r, ln, w, h) if w > 0 and h > 0 else (r.take(ln), b"")[1]
                rec["planes"] = planes
                rec["w"], rec["h"] = w, h
        r.p = li_end
    # grupos: en el archivo van de abajo arriba; un divisor (3) abre el grupo y su carpeta (1 o 2), mas arriba,
    # lo cierra y trae el nombre: se pone delante en el camino de todo lo que entro desde su divisor
    starts: list[int] = []
    for rec in (recs if lm_len and li_len else []):
        sec = rec["section"]
        if sec == 3:
            starts.append(len(layers))
            continue
        if sec in (1, 2):
            first = starts.pop() if starts else 0
            for L in layers[first:]:
                L["group"].insert(0, rec["name"])
            continue
        if rec["w"] <= 0 or rec["h"] <= 0:
            continue
        if max(rec["w"], rec["h"]) > MAX_SIDE:
            warnings.append(f"capa '{rec['name']}' demasiado grande: no importada")
            continue
        P = rec["planes"]
        n = rec["w"] * rec["h"]
        if mode == 3:
            rgb = [P.get(i, bytes(n)) for i in (0, 1, 2)]
        else:
            rgb = [P.get(0, bytes(n))] * 3
        a = P.get(-1, b"\xff" * n)
        im = Image.merge("RGBA", [Image.frombytes("L", (rec["w"], rec["h"]), c) for c in (*rgb, a)])
        bbox = im.getbbox()
        if not bbox:
            continue
        im = im.crop(bbox)
        if rec["blend"] not in ("norm", "pass"):
            warnings.append(f"capa '{rec['name']}': su modo de fusion no se importa (queda normal)")
        buf = io.BytesIO()
        im.save(buf, "PNG", optimize=True)
        layers.append({"name": rec["name"] or "capa", "png": buf.getvalue(), "x": rec["rect"][1] + bbox[0],
                       "y": rec["rect"][0] + bbox[1], "w": im.width, "h": im.height, "opacity": rec["opacity"] / 255,
                       "visible": rec["visible"], "group": []})
    # sin capas (PSD aplanado): la imagen compuesta
    if not layers:
        r.p = lm_end
        comp = r.u16()
        plane = W * H
        nread = min(nch, 4 if mode == 3 else 2)
        if comp == 1:
            counts = [r.u32() if r.psb else r.u16() for _ in range(H * nch)]
            chans = []
            k = 0
            for c in range(nch):
                rows = [_packbits(r.take(counts[k + y]), W) for y in range(H)]
                k += H
                chans.append(b"".join(rows))
        elif comp == 0:
            chans = [r.take(plane) for _ in range(nch)]
        else:
            raise LayeredError("PSD sin capas con una compresion que no se puede leer")
        chans = chans[:nread]
        if mode == 3:
            bands = chans[:3] + ([chans[3]] if len(chans) > 3 else [b"\xff" * plane])
        else:
            bands = [chans[0]] * 3 + ([chans[1]] if len(chans) > 1 else [b"\xff" * plane])
        im = Image.merge("RGBA", [Image.frombytes("L", (W, H), c) for c in bands])
        buf = io.BytesIO()
        im.save(buf, "PNG", optimize=True)
        layers.append({"name": "imagen", "png": buf.getvalue(), "x": 0, "y": 0, "w": W, "h": H, "opacity": 1.0,
                       "visible": True, "group": []})
    return {"w": W, "h": H, "layers": layers, "warnings": sorted(set(warnings))}


# ================================================================== Photoshop con psd-tools (tal cual)
_NORMAL = {"normal", "pass_through", "BlendMode.NORMAL", "BlendMode.PASS_THROUGH"}


def read_psd_full(data: bytes) -> dict:
    """Cada capa se pinta como la pinta Photoshop: estilos de capa, mascaras, recortes, objetos inteligentes,
    texto y formas. Lo que depende de lo de debajo (capas de ajuste, modos de fusion distintos de normal) se
    funde con la capa de abajo, para que el resultado sea identico al documento."""
    from psd_tools import PSDImage
    psd = PSDImage.open(io.BytesIO(data))
    W, H = psd.width, psd.height
    warnings: list[str] = []

    def groups_of(l):
        out, p = [], l.parent
        while p is not None and p is not psd:
            out.insert(0, p.name)
            p = p.parent
        return out

    def blend(l):
        b = getattr(l, "blend_mode", None)
        return str(getattr(b, "name", b)).lower() if b is not None else "normal"

    def effective_blend(l):
        b = blend(l)
        p = l.parent
        while b in ("normal", "pass_through") and p is not None and p is not psd:
            b = blend(p) if blend(p) not in ("pass_through",) else b
            p = p.parent
        return b

    def hidden_by_parent(l):
        p = l.parent
        while p is not None and p is not psd:
            if not p.visible:
                return True
            p = p.parent
        return False

    chunks: list[dict] = []
    for l in psd.descendants():
        if l.is_group() or getattr(l, "clipping", False):
            continue      # los grupos son carpetas; las capas recortadas van dentro de su base
        merge = l.kind == "adjustment" or effective_blend(l) not in ("normal", "pass_through")
        if merge and chunks and chunks[-1]["visible"] == (l.visible and not hidden_by_parent(l)):
            chunks[-1]["layers"].append(l)
            continue
        if merge and not chunks:
            warnings.append(f"capa '{l.name}': no hay nada debajo con que fundirla")
        chunks.append({"layers": [l], "name": l.name, "group": groups_of(l),
                       "visible": l.visible and not hidden_by_parent(l)})

    layers = []
    for ch in chunks:
        keep = set()
        for l in ch["layers"]:
            keep.add(id(l))
            for c in getattr(l, "clip_layers", []) or []:
                keep.add(id(c))
            p = l.parent
            while p is not None and p is not psd:
                keep.add(id(p))
                p = p.parent
        try:
            im = psd.composite(force=True, alpha=0.0, layer_filter=lambda x: id(x) in keep)
        except Exception as ex:
            warnings.append(f"capa '{ch['name']}': no se pudo pintar ({ex})")
            continue
        if im is None:
            continue
        im = im.convert("RGBA")
        bbox = im.getbbox()
        if not bbox:
            continue
        im = im.crop(bbox)
        buf = io.BytesIO()
        im.save(buf, "PNG", optimize=True)
        layers.append({"name": ch["name"] or "capa", "png": buf.getvalue(), "x": bbox[0], "y": bbox[1],
                       "w": im.width, "h": im.height, "opacity": 1.0, "visible": ch["visible"], "group": ch["group"]})
    return {"w": W, "h": H, "layers": layers, "warnings": sorted(set(warnings))}


# ================================================================== Illustrator / PDF
def read_pdf(data: bytes) -> dict:
    try:
        import pymupdf
    except ImportError:
        try:
            import fitz as pymupdf
        except ImportError:
            raise LayeredError("para importar AI o PDF hace falta PyMuPDF (pip install pymupdf)") from None
    try:
        doc = pymupdf.open(stream=data, filetype="pdf")
    except Exception:
        raise LayeredError("no se pudo abrir: guarda el .ai con 'Crear archivo compatible con PDF' activado") from None
    if doc.page_count == 0:
        raise LayeredError("el documento no tiene paginas")
    page = doc[0]
    scale = 2.0                              # 1 pt = 2 px: nitido al escalarlo a la pantalla
    W, H = round(page.rect.width * scale), round(page.rect.height * scale)
    mat = pymupdf.Matrix(scale, scale)

    def render() -> Image.Image:
        pix = page.get_pixmap(matrix=mat, alpha=True)
        return Image.frombytes("RGBA", (pix.width, pix.height), pix.samples)

    def pack(name, im, visible=True):
        bbox = im.getbbox()
        if not bbox:
            return None
        im = im.crop(bbox)
        buf = io.BytesIO()
        im.save(buf, "PNG", optimize=True)
        return {"name": name, "png": buf.getvalue(), "x": bbox[0], "y": bbox[1], "w": im.width, "h": im.height,
                "opacity": 1.0, "visible": visible, "group": []}

    layers = []
    warnings = []
    try:
        ui = doc.layer_ui_configs()
    except Exception:
        ui = []
    ui = [c for c in ui if c.get("type") == "checkbox"]
    if len(ui) > 1:
        was = {c["number"]: c.get("on", True) for c in ui}
        # en el orden en que el PDF las dibuja: la primera queda debajo
        for c in ui:
            for o in ui:
                doc.set_layer_ui_config(o["number"], 0 if o["number"] == c["number"] else 2)   # 0 = encender, 2 = apagar
            page = doc[0]
            L = pack(c.get("text") or f"capa {c['number']}", render(), bool(was[c["number"]]))
            if L:
                layers.append(L)
    if not layers:
        L = pack("dibujo", render())
        if L:
            layers.append(L)
        if len(ui) <= 1:
            warnings.append("el archivo no tiene capas separadas: se importa como una sola imagen")
    return {"w": W, "h": H, "layers": layers, "warnings": warnings}
