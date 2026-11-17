"""Interfaz del editor (2D, 2.5D o 3D, con varias pantallas) -> escena que dibuja re-wor (src/de/ui3d.cpp).

Los tres tipos salen igual: planos con textura y sus cuatro esquinas en el mundo, fotograma a fotograma (30 por
segundo), vistos con una camara. En 2D y 2.5D la camara es la de por defecto (z = 772, 50 grados), que ve el plano
z = 0 exactamente como la pantalla de 1280 x 720; en 3D, la camara activa de la pantalla (con sus cortes).

  2D    cada capa en pantalla: posicion (suma de sus grupos), escala, giro y ancla. Misma cuenta que
        web/src/uiedit/view2d.js.
  2.5D  la capa se proyecta con la camara 2.5D (profundidad y paralaje): misma formula que model.js project25.
  3D    matrices T * Rx * Ry * Rz * S (orden XYZ de three.js), la camara mira a -Z, 1 unidad = 1 px.

Las imagenes de las capas (textos, formas, SVG, estilos) las dibuja el navegador (studio/raster.js) y llegan como
PNG con su margen ("pad"): el plano se agranda ese margen. Las imagenes del juego y los PNG propios sin estilos van
tal cual.

Botones: cada capa con ui.focus es un boton de su pantalla, con vecinos arriba/abajo/izquierda/derecha (misma
regla que model.js navTable), efecto al tener el foco y una accion. Ver el formato en re-wor/src/de/ui3d.hpp.
"""
from __future__ import annotations

import math
import re
import struct

from . import easing

W, H = 1280, 720
PERSP = 800.0
CAM25 = "__cam"
FPS = 30
CLIPS = ("intro", "loop", "outro")
DEFAULT_CAM = "cam 0 0 772 1 0 0 0 1 0 0 0 -1 50"
BASE2D = {"x": W / 2, "y": H / 2, "w": 200, "h": 120, "scale": 1.0, "rot": 0.0, "alpha": 1.0, "color": [255, 255, 255],
          "order": 10, "ax": 0.5, "ay": 0.5, "depth": 0.0}
BASE3D = {"x": 0.0, "y": 0.0, "z": 0.0, "rx": 0.0, "ry": 0.0, "rz": 0.0, "w": 400, "h": 225, "scale": 1.0, "alpha": 1.0,
          "color": [255, 255, 255], "ax": 0.5, "ay": 0.5, "order": 10, "fov": 50.0}
VISUAL = ("image", "text", "shape")
OPS = ("=", "!=", ">", "<", ">=", "<=")


def safe(n: str) -> str:
    return re.sub(r"[^a-z0-9_]+", "_", (n or "").lower()).strip("_")[:32] or "pantalla"


# ------------------------------------------------------------------ muestreo (igual que studio/keys.js)
def _lerp(a, b, f):
    if isinstance(a, (list, tuple)):
        return [x + (y - x) * f for x, y in zip(a, b)]
    return a + (b - a) * f


def sample(keys, t):
    if not keys:
        return None
    keys = sorted(keys, key=lambda k: k["t"])
    if t <= keys[0]["t"]:
        return keys[0]["v"]
    for a, b in zip(keys, keys[1:]):
        if t < b["t"]:
            span = b["t"] - a["t"]
            if span <= 0:
                return b["v"]
            e = b.get("ease") or "linear"
            if e == "hold":
                return a["v"]
            return _lerp(a["v"], b["v"], easing.value(e, b, (t - a["t"]) / span))
    return keys[-1]["v"]


class _Screen:
    """Una pantalla del proyecto (layers, clips, cam25, activeCam) con el muestreo de sus capas."""

    def __init__(self, scr: dict, mode: str):
        self.p = scr
        self.mode = mode
        self.layers = [l for l in scr.get("layers", []) if l.get("visible", True)]
        self.by_id = {l["id"]: l for l in self.layers}
        self.clips = scr.get("clips") or {}
        self.defaults = BASE3D if mode == "3d" else BASE2D

    def tracks(self, clip, lid):
        if clip == "base":
            return {}
        return ((self.clips.get(clip) or {}).get("tracks") or {}).get(lid) or {}

    def base(self, l):
        b = dict(self.defaults)
        b.update(l.get("base") or {})
        return b

    def state(self, l, clip, t):
        s = self.base(l)
        for k, ks in self.tracks(clip, l["id"]).items():
            v = sample(ks, t)
            if v is not None:
                s[k] = v
        return s

    def ancestors(self, l):
        out, cur = [], l
        while cur.get("parent") in self.by_id and len(out) < 64:
            cur = self.by_id[cur["parent"]]
            out.append(cur)
        return out

    def children(self, pid):
        return [l for l in self.layers if (l.get("parent") or None) == pid]

    def duration(self, clip):
        return max(0.0, float((self.clips.get(clip) or {}).get("duration") or 0))

    def animated(self, clip):
        c = self.clips.get(clip) or {}
        return any(v for v in (c.get("tracks") or {}).values()) or bool(c.get("cuts"))

    # ---- 2D / 2.5D: estado en pantalla
    def world2d(self, l, clip, t):
        s = self.state(l, clip, t)
        x, y, depth, alpha = s["x"], s["y"], s.get("depth", 0), s["alpha"]
        for a in self.ancestors(l):
            q = self.state(a, clip, t)
            x += q["x"]; y += q["y"]; depth += q.get("depth", 0); alpha *= q["alpha"]
        return s, x, y, depth, alpha

    def cam25(self, clip, t):
        c = {"x": W / 2, "y": H / 2, "zoom": 1.0, "rot": 0.0}
        c.update(self.p.get("cam25") or {})
        for k, ks in self.tracks(clip, CAM25).items():
            v = sample(ks, t)
            if v is not None:
                c[k] = v
        return c

    def screen(self, l, clip, t):
        """{x, y, scale, rot, alpha, s} en la pantalla de 1280 x 720 (2D y 2.5D)"""
        s, x, y, depth, alpha = self.world2d(l, clip, t)
        if self.mode != "25d":
            return {"x": x, "y": y, "scale": s["scale"], "rot": s["rot"], "alpha": alpha, "s": s}
        cam = self.cam25(clip, t)
        pr = project25(cam, x, y, depth)
        if pr is None:
            return {"x": 0, "y": 0, "scale": 0.0, "rot": 0, "alpha": 0.0, "s": s}
        px, py, k = pr
        return {"x": px, "y": py, "scale": s["scale"] * k, "rot": s["rot"] - cam["rot"], "alpha": alpha, "s": s}

    def paint_order(self):
        """capas visibles en orden de pintado (grupos en su sitio; 2.5D: lo lejano detras)"""
        out = []

        def walk(pid):
            for l in sorted(self.children(pid), key=lambda q: float(self.base(q).get("order", 0))):
                if l.get("type") == "group":
                    walk(l["id"])
                elif l.get("type") in VISUAL:
                    out.append(l)
        walk(None)
        if self.mode == "25d":
            def depth(l):
                return self.base(l).get("depth", 0) + sum(self.base(a).get("depth", 0) for a in self.ancestors(l))
            out = sorted(out, key=lambda l: -depth(l))     # sorted es estable: a igual profundidad, el orden de capas
        return out

    # ---- botones
    def focusables(self):
        F = [l for l in self.layers if (l.get("ui") or {}).get("focus") and l.get("type") in VISUAL]
        return sorted(F, key=lambda l: (float(l["ui"].get("order", 0)), float(self.base(l)["y"]), float(self.base(l)["x"])))

    def nav(self, F):
        """vecinos de cada boton (misma regla que model.js navTable)"""
        pos = []
        for l in F:
            s, x, y, _d, _a = self.world2d(l, "base", 0)
            pos.append((x + (0.5 - s["ax"]) * s["w"], y + (0.5 - s["ay"]) * s["h"]))

        def pick(i, dx, dy):
            best, bs = -1, float("inf")
            for j, p in enumerate(pos):
                if j == i:
                    continue
                vx, vy = p[0] - pos[i][0], p[1] - pos[i][1]
                along, across = vx * dx + vy * dy, abs(vx * dy - vy * dx)
                if along <= 1:
                    continue
                sc = along + across * 2
                if sc < bs:
                    bs, best = sc, j
            return best
        out = []
        n = len(F)
        for i in range(n):
            up, down = pick(i, 0, -1), pick(i, 0, 1)
            if up < 0 and n > 1:
                up = (i - 1) % n
            if down < 0 and n > 1:
                down = (i + 1) % n
            out.append((up, down, pick(i, -1, 0), pick(i, 1, 0)))
        return out


def project25(cam, x, y, depth):
    d = PERSP + depth
    if d <= 1:
        return None
    k = cam["zoom"] * PERSP / d
    dx, dy = (x - cam["x"]) * k, (y - cam["y"]) * k
    r = -math.radians(cam["rot"])
    c, s = math.cos(r), math.sin(r)
    return W / 2 + dx * c - dy * s, H / 2 + dx * s + dy * c, k


# ------------------------------------------------------------------ 3D
def _mat(s):
    """matriz 3x4 de un soporte: T * Rx * Ry * Rz * S (orden XYZ de three.js)"""
    ax, ay, az = (math.radians(float(s.get(k, 0))) for k in ("rx", "ry", "rz"))
    cx, sx, cy, sy, cz, sz = math.cos(ax), math.sin(ax), math.cos(ay), math.sin(ay), math.cos(az), math.sin(az)
    r = [[cy * cz, -cy * sz, sy],
         [cx * sz + sx * sy * cz, cx * cz - sx * sy * sz, -sx * cy],
         [sx * sz - cx * sy * cz, sx * cz + cx * sy * sz, cx * cy]]
    k = float(s.get("scale", 1.0))
    return [[r[i][0] * k, r[i][1] * k, r[i][2] * k, float(s.get(("x", "y", "z")[i], 0))] for i in range(3)]


def _mul(a, b):
    out = []
    for i in range(3):
        row = [sum(a[i][k] * b[k][j] for k in range(3)) for j in range(3)]
        row.append(sum(a[i][k] * b[k][3] for k in range(3)) + a[i][3])
        out.append(row)
    return out


def _apply(m, p):
    return [m[i][0] * p[0] + m[i][1] * p[1] + m[i][2] * p[2] + m[i][3] for i in range(3)]


def _norm(v):
    n = math.sqrt(sum(x * x for x in v)) or 1.0
    return [x / n for x in v]


def png_size(data: bytes):
    if data[:8] == b"\x89PNG\r\n\x1a\n":
        return struct.unpack(">II", data[16:24])
    return 0, 0


# ================================================================== escena
class SceneBuilder:
    """textures: {id de capa: {"png": bytes, "pad": margen, "w": ancho, "h": alto}} (las dibuja el editor).
    image_bytes(capa) -> (nombre de archivo, bytes) para imagenes sin estilos (del juego o PNG propios)."""

    def __init__(self, proj: dict, textures: dict, image_bytes):
        self.p = proj
        self.mode = proj.get("mode", "2d")
        self.textures = textures
        self.image_bytes = image_bytes
        self.files: dict[str, bytes] = {}
        self.warnings: list[str] = []
        self.tex_index: dict[str, int] = {}
        self.screens = proj.get("screens") or []
        self.sidx = {s["id"]: i for i, s in enumerate(self.screens)}

    def _tex(self, l):
        """(indice de textura, margen) de una capa, o (-1, 0)"""
        got, pad = None, 0.0
        t = self.textures.get(l["id"])
        if t:
            got, pad = (f"l_{l['id']}.png", t["png"]), float(t.get("pad", 0))
        elif l.get("type") == "image":
            got = self.image_bytes(l)
            if not got:
                self.warnings.append(f"'{l.get('name')}': sin imagen")
        else:
            self.warnings.append(f"'{l.get('name')}': falta su imagen (vuelve a exportar desde el editor)")
        if not got:
            return -1, 0.0
        fname, data = got
        if fname not in self.tex_index:
            self.tex_index[fname] = len(self.tex_index)
            self.files[fname] = data
        return self.tex_index[fname], pad

    def _cam3d(self, sc: _Screen, clip, t):
        cid = sc.p.get("activeCam")
        cuts = sorted((sc.clips.get(clip) or {}).get("cuts") or [], key=lambda c: c["t"]) if clip != "base" else []
        for c in cuts:
            if c["t"] <= t + 1e-6:
                cid = c["cam"]
        cams = [l for l in sc.layers if l.get("type") == "camera"]
        cam = sc.by_id.get(cid) if cid in sc.by_id and sc.by_id[cid].get("type") == "camera" else (cams[0] if cams else None)
        if not cam:
            return DEFAULT_CAM
        m = self._world3d(sc, cam, clip, t)
        R = _norm([m[0][0], m[1][0], m[2][0]])
        U = _norm([m[0][1], m[1][1], m[2][1]])
        F = _norm([-m[0][2], -m[1][2], -m[2][2]])
        fov = float(sc.state(cam, clip, t).get("fov", 50))
        return ("cam " + " ".join(f"{v:.2f}" for v in (m[0][3], m[1][3], m[2][3])) + " " +
                " ".join(f"{v:.5f}" for v in (*R, *U, *F)) + f" {fov:.3f}")

    def _world3d(self, sc, l, clip, t):
        m = _mat(sc.state(l, clip, t))
        for a in sc.ancestors(l):
            m = _mul(_mat(sc.state(a, clip, t)), m)
        return m

    def _corners(self, sc: _Screen, l, clip, t, pad):
        """(alpha, color, 4 esquinas en el mundo) o None si no se ve"""
        if self.mode == "3d":
            s = sc.state(l, clip, t)
            alpha = float(s["alpha"])
            for a in sc.ancestors(l):
                alpha *= float(sc.state(a, clip, t)["alpha"])
            w, h, ax, ay = float(s["w"]), float(s["h"]), float(s["ax"]), float(s["ay"])
            local = [(-ax * w - pad, ay * h + pad, 0), ((1 - ax) * w + pad, ay * h + pad, 0),
                     ((1 - ax) * w + pad, (ay - 1) * h - pad, 0), (-ax * w - pad, (ay - 1) * h - pad, 0)]
            m = self._world3d(sc, l, clip, t)
            return alpha, s["color"], [_apply(m, p) for p in local]
        st = sc.screen(l, clip, t)
        s = st["s"]
        w, h, ax, ay = float(s["w"]), float(s["h"]), float(s["ax"]), float(s["ay"])
        k = float(st["scale"])
        r = math.radians(float(st["rot"]))
        c, sn = math.cos(r), math.sin(r)
        pts = []
        for lx, ly in ((-ax * w - pad, -ay * h - pad), ((1 - ax) * w + pad, -ay * h - pad),
                       ((1 - ax) * w + pad, (1 - ay) * h + pad), (-ax * w - pad, (1 - ay) * h + pad)):
            lx, ly = lx * k, ly * k
            X, Y = st["x"] + lx * c - ly * sn, st["y"] + lx * sn + ly * c     # giro en pantalla (y hacia abajo)
            pts.append((X - W / 2, H / 2 - Y, 0.0))
        return float(st["alpha"]), s["color"], pts

    def build(self) -> str:
        out_screens = []
        obj_i = 0
        for si, scr in enumerate(self.screens):
            sc = _Screen(scr, self.mode)
            lines = [f"screen {si} {safe(scr.get('name'))} {scr.get('onBack') or 'auto'}"]
            objs = sc.paint_order() if self.mode != "3d" else [l for l in sc.layers if l.get("type") in VISUAL]
            gidx = {}
            pads = {}
            F = sc.focusables()
            fidx = {l["id"]: i for i, l in enumerate(F)}
            for rank, l in enumerate(objs):
                ti, pad = self._tex(l)
                pads[l["id"]] = pad
                gidx[l["id"]] = obj_i
                order = float(sc.base(l).get("order", 0)) if self.mode == "3d" else rank
                lines.append(f"obj {obj_i} {ti} {order:g}")
                # para la logica (wui): tipo, posicion y tamano de diseno (px del editor) y nombre de la capa
                b = sc.base(l)
                bx, by = (float(b.get("x", W / 2)), float(b.get("y", H / 2))) if self.mode != "3d" else (W / 2, H / 2)
                name = " ".join(str(l.get("name") or l["id"]).split())
                lines.append(f"objinfo {obj_i} {l.get('type', 'shape')} {bx:g} {by:g} {float(b.get('w', 0)):g} "
                             f"{float(b.get('h', 0)):g} {name}")
                cond = l.get("showIf") or {}
                show_on = fidx.get(l.get("showOnFocus") or "", -1)
                if l["id"] in fidx or show_on >= 0 or cond.get("global"):
                    g = safe(cond.get("global") or "") if cond.get("global") else "-"
                    op = cond.get("op") if cond.get("op") in OPS else "="
                    lines.append(f"objui {obj_i} {fidx.get(l['id'], -1)} {show_on} {g} {op} {int(cond.get('value') or 0)}")
                obj_i += 1
            # botones
            start = fidx.get(scr.get("focusStart") or "", 0) if F else -1
            lines.append(f"defaultfocus {start}")
            for i, (l, nb) in enumerate(zip(F, sc.nav(F))):
                ui = l.get("ui") or {}
                hv = {"scale": 1.08, "color": [255, 220, 150], "alpha": 1, "dx": 0, "dy": 0, **(ui.get("hover") or {})}
                col = [max(0.0, min(255.0, float(c))) / 255 for c in (list(hv["color"]) + [255, 255, 255])[:3]]
                lines.append(f"focus {i} {gidx[l['id']]} {nb[0]} {nb[1]} {nb[2]} {nb[3]} {1 if ui.get('sound', True) else 0} "
                             f"{float(hv['scale']):.3f} {col[0]:.3f} {col[1]:.3f} {col[2]:.3f} {float(hv['alpha']):.3f} "
                             f"{float(hv['dx']):.1f} {float(hv['dy']):.1f}")
                lines.append(f"act {i} " + self._action(l, ui.get("action") or {}))
            # clips
            for clip in CLIPS:
                dur = sc.duration(clip)
                if not sc.animated(clip) and not (self.mode == "25d" and sc.tracks(clip, CAM25)):
                    times = [0.0]                                    # sin claves: un fotograma
                else:
                    n = max(1, int(round(dur * FPS)))
                    frames = n if clip == "loop" else n + 1
                    times = [min(dur, f / FPS) for f in range(frames)]
                lines.append(f"clip {clip} {len(times)}")
                for t in times:
                    lines.append("f")
                    lines.append(self._cam3d(sc, clip, t) if self.mode == "3d" else DEFAULT_CAM)
                    for l in objs:
                        r = self._corners(sc, l, clip, t, pads[l["id"]])
                        alpha, color, pts = r
                        if alpha <= 0.002 and l["id"] not in fidx:
                            continue
                        col = [max(0.0, min(255.0, float(c))) / 255 for c in (list(color) + [255, 255, 255])[:3]]
                        lines.append(f"o {gidx[l['id']]} " + " ".join(f"{c:.3f}" for c in (*col, max(0.0, min(1.0, alpha)))) + " " +
                                     " ".join(f"{v:.2f}" for p in pts for v in p))
            if self.mode == "3d" and not any(l.get("type") == "camera" for l in sc.layers):
                self.warnings.append(f"pantalla '{scr.get('name')}': sin camara (se usa la de por defecto)")
            out_screens.append(lines)
        start = self.sidx.get(self.p.get("start"), 0)
        head = ["ui3d 2", f"name {self.p.get('name')}", f"fps {FPS}", f"screens {len(self.screens)}", f"start {start}"]
        head += [f"tex {i} {f}" for f, i in sorted(self.tex_index.items(), key=lambda x: x[1])]
        return "\n".join(head + [x for s in out_screens for x in s]) + "\n"

    def _action(self, l, a):
        k = a.get("type") or "none"
        if k == "goto":
            if a.get("screen") not in self.sidx:
                self.warnings.append(f"'{l.get('name')}': su pantalla de destino no existe")
                return "none"
            return f"goto {self.sidx[a['screen']]}"
        if k in ("back", "close"):
            return k
        if k == "set":
            g = safe(a.get("global") or "")
            if not a.get("global"):
                self.warnings.append(f"'{l.get('name')}': falta el nombre de la variable")
                return "none"
            return f"set {g} {int(a.get('value') or 0)}"
        if k == "script":
            s = " ".join((a.get("script") or "").split())
            if not s:
                self.warnings.append(f"'{l.get('name')}': falta el script")
                return "none"
            return f"script {s}"
        return "none"


# ================================================================== scripts QB
def qscripts(n: str, proj: dict) -> str:
    """Scripts QB de la interfaz: ordenes para re-wor (globales) y, si tiene botones, captura del mando."""
    screens = proj.get("screens") or []
    interactive = proj.get("settings", {}).get("capture", True) and any(
        (l.get("ui") or {}).get("focus") for s in screens for l in s.get("layers", []) if l.get("visible", True))
    g = f"re_ui3d_{n}"

    def signal(cmd):
        return [f"    Change {g}_cmd = {cmd}", f"    Change {g}_seq = (${g}_seq + 1)"]   # $ = valor de la global
    out = [f"// Interfaz '{n}' generada por DE Studio (editor de interfaces). No editar a mano.",
           f"// La dibuja re-wor (ui3d/{n}/scene.ui3d); estos scripts le dan las ordenes con globales:",
           f"// {g}_cmd = 1 mostrar, 2 salida, 3 quitar ya, 4 ir a la pantalla {g}_arg; {g}_seq sube en cada orden.",
           f"// Mando: {g}_key = 1 arriba, 2 abajo, 3 izquierda, 4 derecha, 5 elegir, 6 atras; {g}_kseq sube en cada tecla.",
           f"int re_ui_{n}_active = 0"]
    out += [f"int {g}_{k} = 0" for k in ("cmd", "seq", "arg", "key", "kseq")]
    out.append("")
    out += [f"script re_ui_{n}_show parent = root_window", *signal(1), f"    Change re_ui_{n}_active = 1"]
    if interactive:
        out.append(f"    SpawnScriptNow re_ui_{n}_grab")
    out += ["endscript", "",
            f"script re_ui_{n}_hide", *signal(2), f"    Change re_ui_{n}_active = 0", f"    re_ui_{n}_release", "endscript", "",
            f"script re_ui_{n}_hide_now", *signal(3), f"    Change re_ui_{n}_active = 0", f"    re_ui_{n}_release", "endscript", "",
            f"script re_ui_{n}_goto screen = 0", f"    Change {g}_arg = <screen>", *signal(4), "endscript", ""]
    for i, s in enumerate(screens):
        out += [f"script re_ui_{n}_goto_{safe(s.get('name'))}_{i}", f"    re_ui_{n}_goto screen = {i}", "endscript", ""]
    # mando: un elemento invisible con el foco recibe los botones y se los pasa a re-wor
    out += [f"script re_ui_{n}_key k = 0", f"    Change {g}_key = <k>", f"    Change {g}_kseq = (${g}_kseq + 1)", "endscript", ""]
    out += [f"script re_ui_{n}_release",
            f"    if ScreenElementExists id = re_ui_{n}_input",
            f"        DestroyScreenElement id = re_ui_{n}_input",
            "        LaunchEvent type = focus target = current_menu",
            "    endif", "endscript", ""]
    if interactive:
        pads = [("pad_up", 1), ("pad_down", 2), ("pad_left", 3), ("pad_right", 4), ("pad_choose", 5), ("pad_back", 6)]
        out += [f"script re_ui_{n}_grab",
                f"    if ScreenElementExists id = re_ui_{n}_input",
                f"        DestroyScreenElement id = re_ui_{n}_input",
                "    endif",
                "    LaunchEvent type = unfocus target = current_menu",
                "    CreateScreenElement {",
                "        type = ContainerElement",
                "        parent = root_window",
                f"        id = re_ui_{n}_input",
                "        event_handlers = ["]
        out += [f"            {{ {p} re_ui_{n}_key params = {{ k = {k} }} }}" for p, k in pads]
        out += ["        ]",
                "        exclusive_device = ($primary_controller)",
                "    }",
                f"    LaunchEvent type = focus target = re_ui_{n}_input",
                "endscript", ""]
    return "\n".join(out)
