"""Catalogo del juego para el editor de interfaces: lo que usan los menus de WoR, sacado de sus propios scripts.

Lee data/pak/qb.pak.xen + qb.pab.xen (los .qb del juego) y, con los nombres del Guitar Hero SDK
(ghwor_keys.txt), lista:
  scripts   [nombre, archivo, [parametros]]      parametros = los <nombre> que lee el script (bytecode LZSS)
  globals   [nombre, tipo, valor, archivo]        int, float, string, wstring, qbkey
  structs   [nombre, archivo, [[miembro, tipo, valor], ...]]   miembros escalares (y de estructuras hijas: a.b)
  states    estados de interfaz del juego (UIstate_*, nombrados en sus scripts): se abren con
            ui_event event = menu_change data = {state = X}
  getters   campos que los menus piden a GetPlayerInfo <jugador> <campo> y GetBandInfo <banda> <campo>
  sounds    tipos de sonido de menu que acepta UI_SFX (uitype)

Se genera una vez (unos segundos) y se guarda en DEStudio/cache/qb_catalog.json; si cambia el qb.pab del juego se
vuelve a generar. Nada de esto se copia al repositorio: sale de los archivos del juego del usuario.
"""
from __future__ import annotations

import json
import struct
import threading
from pathlib import Path

from ..core import paths   # primero: anade tools/wtde2wor al path
from ..core.names import sdk_keys
from qb_io import QbArray, QbFile, QbStruct  # noqa: E402
from wor_pak import dechnk  # noqa: E402

_LOCK = threading.Lock()
_MEM: dict | None = None
VERSION = 2
SCALAR = ("int", "float", "string", "wstring", "qbkey", "qbkey_s", "qskey")


def _lzss(d: bytes) -> bytes:
    """LZSS de Neversoft (anillo de 4096, coincidencias de 3 a 18 bytes)."""
    n, f = 4096, 18
    buf = bytearray(b" " * (n + f))
    r, out, flags, o = n - f, bytearray(), 0, 0
    while o < len(d):
        flags >>= 1
        if not flags & 256:
            flags = d[o] | 0xFF00
            o += 1
        if flags & 1:
            if o >= len(d):
                break
            c = d[o]
            o += 1
            out.append(c)
            buf[r] = c
            r = (r + 1) & (n - 1)
        else:
            if o + 1 >= len(d):
                break
            i, j = d[o], d[o + 1]
            o += 2
            i |= (j & 0xF0) << 4
            for k in range((j & 0x0F) + 3):
                c = buf[(i + k) & (n - 1)]
                out.append(c)
                buf[r] = c
                r = (r + 1) & (n - 1)
    return bytes(out)


def _load(p: Path) -> bytes:
    d = p.read_bytes()
    return dechnk(d) if d[:4] == b"CHNK" else d


def _index(pak: bytes):
    o = 0
    while o + 32 <= len(pak):
        f = struct.unpack_from(">8I", pak, o)
        if f[0] == 0x2CB3EF3B:      # .last
            break
        yield f
        o += 32


def _scalar(kind, v, names):
    if kind == "float":
        return round(struct.unpack(">f", struct.pack(">I", v & 0xFFFFFFFF))[0], 4) if isinstance(v, int) else v
    if kind == "int":
        return struct.unpack(">i", struct.pack(">I", v & 0xFFFFFFFF))[0] if isinstance(v, int) else v
    if kind in ("string", "wstring"):
        if isinstance(v, bytes):
            return (v.decode("utf-16-be", "replace") if kind == "wstring" else v.decode("latin-1"))[:60]
        return str(v)[:60]
    if kind.startswith("qbkey") or kind == "qskey":
        return names.get(v, f"#{v:08x}") if isinstance(v, int) else str(v)
    return None


def _members(st: QbStruct, names, prefix="", depth=0, out=None):
    out = [] if out is None else out
    for m in st.members:
        nm = names.get(m.name)
        if not nm or len(out) > 80:
            continue
        if m.kind in SCALAR:
            out.append([prefix + nm, "float" if m.kind == "float" else "int" if m.kind == "int" else
                        "text" if m.kind in ("string", "wstring") else "name", _scalar(m.kind, m.value, names)])
        elif m.kind == "struct" and isinstance(m.value, QbStruct):
            out.append([prefix + nm, "struct", None])
            if depth < 1:
                _members(m.value, names, prefix + nm + ".", depth + 1, out)
        elif m.kind == "array":
            out.append([prefix + nm, "array", len(m.value.values) if isinstance(m.value, QbArray) else None])
    return out


def _code(v: bytes) -> bytes:
    if len(v) < 12:
        return b""
    _unk, unc, comp = struct.unpack_from(">III", v, 0)
    body = v[12:12 + min(unc, comp)]
    try:
        return _lzss(body) if comp < unc else body
    except Exception:
        return b""


GETTERS = ("GetPlayerInfo", "GetBandInfo")


def _scan(code: bytes, names, states: set, getters: dict) -> list[str]:
    """parametros <x> del script; de paso, estados UIstate_* nombrados y campos pedidos a GetPlayerInfo/GetBandInfo"""
    params = []
    gkeys = getters.setdefault("_keys", {k: g for g in GETTERS for k, v in names.items() if v == g} if "_keys" not in getters else getters["_keys"])
    p, n = 0, len(code) - 5
    while p < n:
        b = code[p]
        if b == 0x2D and code[p + 1] == 0x16:                 # <nombre>
            nm = names.get(struct.unpack_from("<I", code, p + 2)[0])
            if nm and nm not in params:
                params.append(nm)
            p += 6
            continue
        if b == 0x16:
            k = struct.unpack_from("<I", code, p + 1)[0]
            nm = names.get(k, "")
            if nm[:8].lower() == "uistate_":
                states.add(nm)
            g = gkeys.get(k)
            if g:                                           # GetPlayerInfo <jugador> campo
                q, got = p + 5, []
                while q < n and code[q] != 0x01 and len(got) < 4:
                    if code[q] == 0x16:
                        got.append(names.get(struct.unpack_from("<I", code, q + 1)[0], ""))
                        q += 5
                    else:
                        q += 1
                got = [x for x in got if x and x.lower() not in ("player", "band", "out", "i", "this_player", "player_num")]
                if len(got) == 1:
                    c = getters.setdefault(g, {})
                    c[got[0]] = c.get(got[0], 0) + 1
            p += 5
            continue
        p += 1
    return params[:24]


def _build(pak_p: Path, pab_p: Path) -> dict:
    names = sdk_keys()
    pak, pab = _load(pak_p), _load(pab_p)
    scripts, globs, structs, sounds = [], [], [], []
    states: set = set()
    getters: dict = {}
    for f in _index(pak):
        data = pab[f[1]:f[1] + f[2]]
        fname = names.get(f[4], "").replace("\\", "/").rsplit("/", 1)[-1].replace(".qb.xen", "")
        try:
            items = QbFile(data).items
        except Exception:
            continue
        for it in items:
            nm = names.get(it.name)
            if not nm:
                continue
            if it.kind == "k07":
                params = _scan(_code(it.value), names, states, getters)
                scripts.append([nm, fname, params])
                if nm.lower() == "ui_sfx":
                    sounds = _sfx_types(it.value, names)
            elif it.kind in SCALAR:
                t = {"float": "float", "int": "int", "string": "text", "wstring": "text"}.get(it.kind, "name")
                globs.append([nm, t, _scalar(it.kind, it.value, names), fname])
            elif it.kind == "struct" and isinstance(it.value, QbStruct):
                structs.append([nm, fname, _members(it.value, names)])
            elif it.kind == "array" and isinstance(it.value, QbArray):
                globs.append([nm, "array", len(it.value.values), fname])
    getters.pop("_keys", None)
    gets = [{"fn": g, "arg": "player" if g == "GetPlayerInfo" else "band",
             "fields": [k for k, _ in sorted(getters.get(g, {}).items(), key=lambda kv: -kv[1])][:80]} for g in GETTERS]
    key = lambda x: x[0].lower()
    return {"version": VERSION, "source": [pab_p.stat().st_size, int(pab_p.stat().st_mtime)],
            "scripts": sorted(scripts, key=key), "globals": sorted(globs, key=key), "structs": sorted(structs, key=key),
            "states": sorted(states, key=str.lower), "sounds": sounds, "getters": gets}


def _sfx_types(v: bytes, names) -> list[str]:
    """uitype que reconoce UI_SFX: las claves que compara su codigo (las de sonidos de menu)"""
    _unk, unc, comp = struct.unpack_from(">III", v, 0)
    body = v[12:12 + min(unc, comp)]
    code = _lzss(body) if comp < unc else body
    seen = []
    for p in range(len(code) - 5):
        if code[p] == 0x16:
            nm = names.get(struct.unpack_from("<I", code, p + 1)[0])
            if nm and nm not in seen:
                seen.append(nm)
    want = ("select", "back", "scroll", "toggle", "negative", "warning")
    return [s for s in seen if any(w in s.lower() for w in want)]


def catalog() -> dict:
    """El catalogo (en memoria; en disco en cache/qb_catalog.json)."""
    global _MEM
    pak_p = paths.ASSETS / "pak" / "qb.pak.xen"
    pab_p = paths.ASSETS / "pak" / "qb.pab.xen"
    if not pab_p.exists():
        raise FileNotFoundError("no se encuentran los scripts del juego (data/pak/qb.pab.xen)")
    src = [pab_p.stat().st_size, int(pab_p.stat().st_mtime)]
    with _LOCK:
        if _MEM and _MEM.get("source") == src:
            return _MEM
        cache = paths.CACHE / "qb_catalog.json"
        try:
            c = json.loads(cache.read_text(encoding="utf-8"))
            if c.get("version") == VERSION and c.get("source") == src:
                _MEM = c
                return c
        except (OSError, ValueError):
            pass
        c = _build(pak_p, pab_p)
        cache.parent.mkdir(parents=True, exist_ok=True)
        cache.write_text(json.dumps(c, ensure_ascii=False), encoding="utf-8")
        _MEM = c
        return c
