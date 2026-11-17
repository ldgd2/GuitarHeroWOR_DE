"""Edicion de imagenes no destructiva: el original se conserva y se aplica una lista de pasos.

    apply(im, ops) -> Image (RGBA)

Cada paso es {"op": nombre, ...parametros}. Pasos (OPS describe los parametros para el editor):
    crop, bucket, removebg, erase_color, adjust, hue, tint, gray, invert, blur, sharpen, glow, chroma,
    outline, shadow, pixelate, vignette, noise, posterize, flip, rotate, pad, round, opacity
"""
from __future__ import annotations

from collections import deque

import numpy as np
from PIL import Image, ImageChops, ImageEnhance, ImageFilter, ImageOps

# nombre -> (titulo, [(param, tipo, defecto, min, max)])  tipo: num, color, bool, choice:a|b
OPS = {
    "crop": ("Recortar", [("x", "num", 0, 0, 99999), ("y", "num", 0, 0, 99999), ("w", "num", 0, 0, 99999), ("h", "num", 0, 0, 99999)]),
    "trim": ("Recortar lo transparente", [("pad", "num", 0, 0, 500)]),
    "bucket": ("Cubeta de pintura", [("x", "num", 0, 0, 99999), ("y", "num", 0, 0, 99999), ("color", "color", "#ffffff", 0, 0),
                                     ("alpha", "num", 1, 0, 1), ("tolerance", "num", 32, 0, 255), ("contiguous", "bool", True, 0, 0)]),
    "removebg": ("Quitar el fondo", [("tolerance", "num", 40, 0, 255), ("feather", "num", 1, 0, 20), ("mode", "choice:bordes|ia", "bordes", 0, 0)]),
    "erase_color": ("Borrar un color", [("color", "color", "#ffffff", 0, 0), ("tolerance", "num", 30, 0, 255)]),
    "adjust": ("Brillo, contraste, saturacion", [("brightness", "num", 1, 0, 3), ("contrast", "num", 1, 0, 3),
                                                 ("saturation", "num", 1, 0, 3), ("gamma", "num", 1, 0.1, 4)]),
    "hue": ("Tono", [("shift", "num", 0, -180, 180)]),
    "tint": ("Tenir", [("color", "color", "#ff4040", 0, 0), ("amount", "num", 0.5, 0, 1)]),
    "gray": ("Blanco y negro", []),
    "invert": ("Invertir", []),
    "posterize": ("Posterizar", [("levels", "num", 4, 2, 32)]),
    "blur": ("Desenfoque", [("radius", "num", 4, 0, 100)]),
    "sharpen": ("Enfocar", [("amount", "num", 1.5, 0, 5)]),
    "glow": ("Resplandor", [("radius", "num", 12, 1, 200), ("color", "color", "#ffd080", 0, 0), ("strength", "num", 1.5, 0, 5),
                            ("inner", "bool", False, 0, 0)]),
    "chroma": ("Aberracion cromatica", [("offset", "num", 4, 0, 60), ("angle", "num", 0, -180, 180)]),
    "outline": ("Contorno", [("width", "num", 4, 1, 100), ("color", "color", "#000000", 0, 0)]),
    "shadow": ("Sombra", [("dx", "num", 6, -200, 200), ("dy", "num", 6, -200, 200), ("blur", "num", 8, 0, 100),
                          ("color", "color", "#000000", 0, 0), ("alpha", "num", 0.6, 0, 1)]),
    "pixelate": ("Pixelar", [("size", "num", 8, 2, 128)]),
    "vignette": ("Vineta", [("amount", "num", 0.6, 0, 1)]),
    "noise": ("Ruido", [("amount", "num", 20, 0, 128)]),
    "flip": ("Voltear", [("axis", "choice:horizontal|vertical", "horizontal", 0, 0)]),
    "rotate": ("Girar", [("angle", "num", 90, -360, 360)]),
    "pad": ("Margen transparente", [("size", "num", 16, 0, 1000)]),
    "round": ("Esquinas redondeadas", [("radius", "num", 24, 0, 2000)]),
    "opacity": ("Opacidad", [("alpha", "num", 0.8, 0, 1)]),
}


def _rgb(c, default=(255, 255, 255)):
    if isinstance(c, (list, tuple)) and len(c) >= 3:
        return tuple(int(v) for v in c[:3])
    if isinstance(c, str) and c.startswith("#") and len(c) in (4, 7):
        h = c[1:]
        if len(h) == 3:
            h = "".join(ch * 2 for ch in h)
        return tuple(int(h[i:i + 2], 16) for i in (0, 2, 4))
    return default


def _flood(arr: np.ndarray, seeds, tol: float) -> np.ndarray:
    """mascara de los pixeles conectados a las semillas con color parecido (RGBA, distancia max por canal)"""
    h, w = arr.shape[:2]
    a = arr.astype(np.int16)
    mask = np.zeros((h, w), bool)
    q = deque()
    for x, y in seeds:
        if 0 <= x < w and 0 <= y < h and not mask[y, x]:
            mask[y, x] = True
            q.append((x, y, a[y, x]))
    while q:
        x, y, ref = q.popleft()
        for nx, ny in ((x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)):
            if 0 <= nx < w and 0 <= ny < h and not mask[ny, nx] and np.abs(a[ny, nx] - ref).max() <= tol:
                mask[ny, nx] = True
                q.append((nx, ny, ref))
    return mask


def _flood_fast(arr: np.ndarray, seed_color, seeds, tol: float) -> np.ndarray:
    """como _flood pero contra un color fijo, por componentes conexas (rapido en imagenes grandes)"""
    from scipy import ndimage  # noqa: F401  (si no esta, se usa _flood)
    near = np.abs(arr.astype(np.int16) - np.array(seed_color, np.int16)).max(axis=2) <= tol
    lab, _ = ndimage.label(near)
    keep = {lab[y, x] for x, y in seeds if 0 <= y < lab.shape[0] and 0 <= x < lab.shape[1] and lab[y, x]}
    return np.isin(lab, list(keep)) if keep else np.zeros(near.shape, bool)


def _region(arr, seeds, tol, contiguous=True):
    x0, y0 = seeds[0]
    ref = arr[y0, x0].astype(np.int16)
    if not contiguous:
        return np.abs(arr.astype(np.int16) - ref).max(axis=2) <= tol
    try:
        if len(seeds) == 1:
            return _flood_fast(arr, ref, seeds, tol)
        out = np.zeros(arr.shape[:2], bool)
        for s in seeds:
            if not out[s[1], s[0]]:
                out |= _flood_fast(arr, arr[s[1], s[0]], [s], tol)
        return out
    except ImportError:
        return _flood(arr, seeds, tol)


def _glow(im: Image.Image, radius, color, strength, inner=False):
    a = im.getchannel("A")
    if inner:
        a = ImageOps.invert(a)
    g = a.filter(ImageFilter.GaussianBlur(radius))
    g = g.point(lambda v: min(255, int(v * strength)))
    layer = Image.new("RGBA", im.size, _rgb(color) + (0,))
    layer.putalpha(g)
    if inner:
        layer.putalpha(ImageChops.multiply(g, im.getchannel("A")))
        return Image.alpha_composite(im, layer)
    return Image.alpha_composite(layer, im)


def _grow(im, n):
    if n <= 0:
        return im
    out = Image.new("RGBA", (im.width + 2 * n, im.height + 2 * n), (0, 0, 0, 0))
    out.paste(im, (n, n))
    return out


def step(im: Image.Image, o: dict) -> Image.Image:
    op = o.get("op")
    g = lambda k, d=None: o.get(k, d)  # noqa: E731
    if op == "crop":
        x, y = int(g("x", 0)), int(g("y", 0))
        w, h = int(g("w", 0)) or im.width - x, int(g("h", 0)) or im.height - y
        return im.crop((x, y, min(im.width, x + w), min(im.height, y + h)))
    if op == "trim":
        b = im.getchannel("A").getbbox()
        if not b:
            return im
        p = int(g("pad", 0))
        return im.crop((max(0, b[0] - p), max(0, b[1] - p), min(im.width, b[2] + p), min(im.height, b[3] + p)))
    if op == "bucket":
        arr = np.array(im)
        x, y = int(g("x", 0)), int(g("y", 0))
        if not (0 <= x < im.width and 0 <= y < im.height):
            return im
        m = _region(arr, [(x, y)], float(g("tolerance", 32)), bool(g("contiguous", True)))
        arr[m] = _rgb(g("color")) + (int(255 * float(g("alpha", 1))),)
        return Image.fromarray(arr, "RGBA")
    if op == "removebg":
        if g("mode") == "ia":
            try:
                from rembg import remove
                return remove(im).convert("RGBA")
            except ImportError:
                pass      # sin rembg: por bordes
        arr = np.array(im)
        h, w = arr.shape[:2]
        seeds = [(x, 0) for x in range(0, w, max(1, w // 64))] + [(x, h - 1) for x in range(0, w, max(1, w // 64))] + \
                [(0, y) for y in range(0, h, max(1, h // 64))] + [(w - 1, y) for y in range(0, h, max(1, h // 64))]
        m = _region(arr, seeds, float(g("tolerance", 40)))
        alpha = Image.fromarray(np.where(m, 0, arr[..., 3]).astype(np.uint8), "L")
        f = float(g("feather", 1))
        if f > 0:
            alpha = ImageChops.multiply(alpha, alpha.filter(ImageFilter.GaussianBlur(f)).point(lambda v: min(255, v * 2)))
        im = im.copy()
        im.putalpha(alpha)
        return im
    if op == "erase_color":
        arr = np.array(im)
        m = np.abs(arr[..., :3].astype(np.int16) - np.array(_rgb(g("color")), np.int16)).max(axis=2) <= float(g("tolerance", 30))
        arr[m, 3] = 0
        return Image.fromarray(arr, "RGBA")
    if op == "adjust":
        a = im.getchannel("A")
        rgb = im.convert("RGB")
        rgb = ImageEnhance.Brightness(rgb).enhance(float(g("brightness", 1)))
        rgb = ImageEnhance.Contrast(rgb).enhance(float(g("contrast", 1)))
        rgb = ImageEnhance.Color(rgb).enhance(float(g("saturation", 1)))
        gm = float(g("gamma", 1)) or 1
        if gm != 1:
            rgb = rgb.point(lambda v: int(255 * (v / 255) ** (1 / gm)))
        rgb.putalpha(a)
        return rgb
    if op == "hue":
        arr = np.array(im).astype(np.float32) / 255
        hsv = np.array(Image.fromarray((arr[..., :3] * 255).astype(np.uint8)).convert("HSV")).astype(np.int16)
        hsv[..., 0] = (hsv[..., 0] + int(float(g("shift", 0)) / 360 * 255)) % 256
        rgb = Image.fromarray(hsv.astype(np.uint8), "HSV").convert("RGB")
        rgb.putalpha(im.getchannel("A"))
        return rgb
    if op == "tint":
        c = Image.new("RGB", im.size, _rgb(g("color")))
        gray = ImageOps.grayscale(im.convert("RGB"))
        col = ImageChops.multiply(Image.merge("RGB", [gray] * 3), c)
        out = Image.blend(im.convert("RGB"), col, float(g("amount", 0.5)))
        out.putalpha(im.getchannel("A"))
        return out
    if op == "gray":
        out = ImageOps.grayscale(im.convert("RGB")).convert("RGB")
        out.putalpha(im.getchannel("A"))
        return out
    if op == "invert":
        out = ImageOps.invert(im.convert("RGB"))
        out.putalpha(im.getchannel("A"))
        return out
    if op == "posterize":
        lv = max(2, int(g("levels", 4)))
        out = im.convert("RGB").point(lambda v: int(round(v / 255 * (lv - 1)) * 255 / (lv - 1)))
        out.putalpha(im.getchannel("A"))
        return out
    if op == "blur":
        r = float(g("radius", 4))
        return _grow(im, int(r * 2)).filter(ImageFilter.GaussianBlur(r)) if r > 0 else im
    if op == "sharpen":
        return im.filter(ImageFilter.UnsharpMask(2, int(float(g("amount", 1.5)) * 100), 2))
    if op == "glow":
        r = float(g("radius", 12))
        inner = bool(g("inner", False))
        return _glow(im if inner else _grow(im, int(r * 2)), r, g("color"), float(g("strength", 1.5)), inner)
    if op == "chroma":
        off = float(g("offset", 4))
        ang = np.radians(float(g("angle", 0)))
        dx, dy = int(round(off * np.cos(ang))), int(round(off * np.sin(ang)))
        n = int(abs(off)) + 1
        base = _grow(im, n)
        r, gg, b, a = base.split()
        r2 = ImageChops.offset(Image.merge("LA", (r, a)), dx, dy)
        b2 = ImageChops.offset(Image.merge("LA", (b, a)), -dx, -dy)
        alpha = ImageChops.lighter(ImageChops.lighter(r2.getchannel("A"), b2.getchannel("A")), a)
        return Image.merge("RGBA", (r2.getchannel("L"), gg, b2.getchannel("L"), alpha))
    if op == "outline":
        wd = int(g("width", 4))
        big = _grow(im, wd + 1)
        a = big.getchannel("A").filter(ImageFilter.MaxFilter(wd * 2 + 1))
        layer = Image.new("RGBA", big.size, _rgb(g("color"), (0, 0, 0)) + (0,))
        layer.putalpha(a)
        return Image.alpha_composite(layer, big)
    if op == "shadow":
        dx, dy, bl = int(g("dx", 6)), int(g("dy", 6)), float(g("blur", 8))
        n = int(max(abs(dx), abs(dy)) + bl * 2 + 1)
        big = _grow(im, n)
        a = big.getchannel("A").point(lambda v: int(v * float(g("alpha", 0.6))))
        sh = Image.new("RGBA", big.size, _rgb(g("color"), (0, 0, 0)) + (0,))
        sh.putalpha(a)
        sh = ImageChops.offset(sh, dx, dy).filter(ImageFilter.GaussianBlur(bl))
        return Image.alpha_composite(sh, big)
    if op == "pixelate":
        s = max(2, int(g("size", 8)))
        small = im.resize((max(1, im.width // s), max(1, im.height // s)), Image.BILINEAR)
        return small.resize(im.size, Image.NEAREST)
    if op == "vignette":
        w, h = im.size
        yy, xx = np.mgrid[0:h, 0:w]
        d = np.sqrt(((xx - w / 2) / (w / 2)) ** 2 + ((yy - h / 2) / (h / 2)) ** 2) / np.sqrt(2)
        k = 1 - float(g("amount", 0.6)) * np.clip(d, 0, 1) ** 2
        arr = np.array(im).astype(np.float32)
        arr[..., :3] *= k[..., None]
        return Image.fromarray(arr.clip(0, 255).astype(np.uint8), "RGBA")
    if op == "noise":
        arr = np.array(im).astype(np.int16)
        n = np.random.default_rng(1).integers(-int(g("amount", 20)), int(g("amount", 20)) + 1, arr.shape[:2])
        arr[..., :3] += n[..., None]
        return Image.fromarray(arr.clip(0, 255).astype(np.uint8), "RGBA")
    if op == "flip":
        return ImageOps.mirror(im) if g("axis", "horizontal") == "horizontal" else ImageOps.flip(im)
    if op == "rotate":
        return im.rotate(-float(g("angle", 90)), Image.BICUBIC, expand=True)
    if op == "pad":
        return _grow(im, int(g("size", 16)))
    if op == "round":
        from PIL import ImageDraw
        m = Image.new("L", im.size, 0)
        ImageDraw.Draw(m).rounded_rectangle((0, 0, im.width - 1, im.height - 1), int(g("radius", 24)), fill=255)
        out = im.copy()
        out.putalpha(ImageChops.multiply(im.getchannel("A"), m))
        return out
    if op == "opacity":
        out = im.copy()
        out.putalpha(im.getchannel("A").point(lambda v: int(v * float(g("alpha", 0.8)))))
        return out
    raise ValueError(f"paso desconocido: {op}")


def apply(im: Image.Image, ops: list) -> Image.Image:
    im = im.convert("RGBA")
    for o in ops or []:
        if o.get("off"):
            continue
        im = step(im, o)
        if max(im.size) > 8192:
            raise ValueError("la imagen pasa de 8192 px")
    return im


def describe() -> list:
    return [{"op": k, "title": t, "params": [{"name": n, "type": ty, "default": d, "min": lo, "max": hi} for n, ty, d, lo, hi in ps]}
            for k, (t, ps) in OPS.items()]


