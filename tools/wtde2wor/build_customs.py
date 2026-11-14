"""Convierte canciones custom de GHWT:DE (carpetas con song.ini) en canciones NUEVAS de Warriors of Rock.

No reemplaza ninguna cancion: cada custom recibe su propio nombre interno (wtde_<checksum>) y se
genera un mod (por defecto mods/_wtde_customs) con:
  data/songs/<nombre>_song.pak.xen          chart, camaras, luces, clips y animaciones convertidas
  data/music/<nombre>_{1,2,3,preview}.fsb.xen  audio re-cifrado para WoR
  data/pak/qb.pak.xen + qb.pab.xen          gh6_songlist_props / gh6_songlist con las entradas nuevas
  data/pak/qs*.pak.xen                      titulo y artista (textos qskey)
Todo lo que en el PAK de una cancion vanilla depende de su NOMBRE se renombra (items <nombre>_xxx,
songs/<nombre>.*, <nombre>_rms.qd); la curva de volumen _rms se calcula del audio de la custom.
El ModManager del juego inserta los archivos nuevos y corrige compress.toc al arrancar.

Uso:  python build_customs.py <carpeta_con_customs> [...] [--out <mod>] [--assets <assets>]
"""
from __future__ import annotations

import argparse
import copy
import hashlib
import json
import re
import struct
import sys
import zlib
from pathlib import Path

from fsb_crypto import load_key_table, wor_decrypt, wor_encrypt, wt_decrypt_file
from qb_io import QbArray, QbFile, QbStruct
from wor_pak import chnk, dechnk, read_pak, write_pak
from wor_song import build_replacement_pak
from wt_song import find_songs, load_chart

BS = "\\"
TEMPLATE = "lasso"          # cancion vanilla que aporta la estructura (loops de banda, pak, etc.)
PREFIX = "wtde_"
VERSION = 15                # sube si cambia el formato de salida (fuerza reconversion)

K_PROPS = 0x150A123B        # gh6_songlist_props
K_LIST = 0xD9B0FFBE         # gh6_songlist
# Lista de DLC del juego (vacia en el disco). La partida NO depende de ella (gh6_songlist si: un registro
# por cancion, y si esa lista cambia el juego da la partida por corrupta), pero el menu solo muestra sus
# canciones si el juego encuentra el paquete DLC instalado, y este port aun no enumera DLC. Por eso las
# customs siguen yendo a gh6_songlist (ver wormod/saves.py para proteger la partida).
K_DLC_PROPS = 0xF3A94A45    # gh6_dlc_songlist_props
K_DLC_LIST = 0xE2458F2A     # gh6_dlc_songlist
M_CHECKSUM = 0x21902065
M_NAME = 0xA1DC81F9
M_TITLE = 0xD4C98794
M_ARTIST = 0xFEA66978
M_YEAR = 0x447D8CC8
M_DURATION = 0x79A07F3F

# Sufijos de items <cancion>_xxx en los QB de una cancion de WoR.
ITEM_SUFFIXES = ["anim_notes", "anim", "drums_notes", "scripts", "cameras_notes", "cameras", "performance",
                 "crowd_notes", "crowd", "lightshow_notes", "lightshow", "facial", "scriptevents",
                 "song_scripts", "facial_notes", "lyrics", "markers", "guitar_markers", "vocals_markers",
                 "localized_strings"]
FILE_SUFFIXES = [".mid.qb", ".mid.qs", ".note", ".perf"]


def qbkey(s: str) -> int:
    return (~zlib.crc32(s.lower().encode())) & 0xFFFFFFFF


def qskey(text: str) -> int:
    return (~zlib.crc32(text.encode("utf-16-le"))) & 0xFFFFFFFF


def _load(p: Path) -> bytes:
    d = p.read_bytes()
    return dechnk(d) if d[:4] == b"CHNK" else d


def internal_name(checksum: str) -> str:
    return PREFIX + re.sub(r"[^a-z0-9]", "", checksum.lower())


# --------------------------------------------------------------------------- pak de cancion
# El .qb de una cancion con <cancion>_scriptevents (loops de banda, clips) se llama
# <prefijo><cancion><12 caracteres>. El texto exacto no aparece en el juego, pero como CRC32 es
# lineal, el prefijo y el sufijo se despejan de las 94 canciones vanilla (prediccion verificada
# 47/47 en canciones no usadas para calcularlo). Con otro nombre el juego no ejecuta esos eventos:
# banda en pose T al empezar, sin clips.
_SE_V, _SE_C, _SE_SUFFIX_LEN = 0xD7B8542F, 0x44020422, 12


def scriptevents_qb_key(name: str) -> int:
    def lin(n: int, x: int) -> int:
        return zlib.crc32(bytes(n), x) ^ zlib.crc32(bytes(n), 0)
    h = zlib.crc32(name.lower().encode(), 0)
    return (~(lin(_SE_SUFFIX_LEN, lin(len(name), _SE_V) ^ h) ^ _SE_C)) & 0xFFFFFFFF


# El .perf tiene, ademas de las camaras, 4 secciones (una por musico) con las animaciones que el juego
# precarga del archivo de loops antes de empezar. Se llaman <prefijo del musico><cancion>; el prefijo
# tampoco aparece como texto, se despejo igual (47/47). Si no se renombran, la cancion suelta sigue
# usando las de la plantilla: el juego no las encuentra, no precarga los loops y el musico queda en
# pose T o tieso hasta que algun clip lo mueve.
PERF_SECTION_STATES = (0x7F1D1A34, 0xC43C34DE, 0xC71C04B4, 0x9C21B1E5)


def perf_section_key(song: str, index: int) -> int:
    return (~zlib.crc32(song.lower().encode(), PERF_SECTION_STATES[index])) & 0xFFFFFFFF


def rename_perf_sections(data: bytes, old: str, new: str) -> bytes:
    smap = {perf_section_key(old, i): perf_section_key(new, i) for i in range(4)}
    d = bytearray(data)
    count, off = struct.unpack_from(">I", d, 8)[0], 0x1C
    k_cam = qbkey("gh5_camera_note")
    for _ in range(count):
        name, n, typ = struct.unpack_from(">III", d, off)
        if name in smap:
            struct.pack_into(">I", d, off, smap[name])
        off += 12 + n * (7 if typ == k_cam else 1000)
    return bytes(d)


def rename_song_pak(pak: bytes, old: str, new: str) -> bytes:
    """Cambia todas las claves que dependen del nombre de la cancion (rutas, contexto, items)."""
    entries, last, first = read_pak(pak)
    kmap = {qbkey(f"songs{BS}{old}{s}"): qbkey(f"songs{BS}{new}{s}") for s in FILE_SUFFIXES}
    kmap[qbkey(old)] = qbkey(new)
    imap = {qbkey(f"{old}_{s}"): qbkey(f"{new}_{s}") for s in ITEM_SUFFIXES}
    kmap[qbkey(f"songs{BS}{old}_rms.qd")] = qbkey(f"songs{BS}{new}_rms.qd")
    kmap[qbkey(f"{old}_rms")] = qbkey(f"{new}_rms")
    k_qb = qbkey(".qb")
    for i, e in enumerate(entries):
        if e.type == k_qb and e.fullname not in kmap:
            if e.fullname == scriptevents_qb_key(old):
                kmap[e.fullname] = scriptevents_qb_key(new)
            else:
                kmap[e.fullname] = qbkey(f"songs{BS}{new}_extra{i}.qb")
        e.fullname = kmap.get(e.fullname, e.fullname)
        e.name = kmap.get(e.name, e.name)
        e.ctx = kmap.get(e.ctx, e.ctx)
        e.parent = kmap.get(e.parent, e.parent)
        if e.type == k_qb:
            q = QbFile(e.data)
            for it in q.items:
                it.name = imap.get(it.name, it.name)
            q.file_key = e.fullname
            e.data = q.build()
        elif e.type in (qbkey(".note"), qbkey(".perf")) and len(e.data) >= 8:
            if struct.unpack_from(">I", e.data, 4)[0] == qbkey(old):
                d = bytearray(e.data)
                struct.pack_into(">I", d, 4, qbkey(new))
                e.data = bytes(d)
            if e.type == qbkey(".perf"):
                e.data = rename_perf_sections(e.data, old, new)
    if last is not None:
        last.ctx = kmap.get(last.ctx, last.ctx)
    return write_pak(entries, header_area=first, last=last)


# --------------------------------------------------------------------------- qb.pak / qb.pab
def _pak_index(pak: bytes):
    out = []
    o = 0
    while o + 32 <= len(pak):
        f = list(struct.unpack_from(">8I", pak, o))
        out.append(f)
        if f[0] == 0x2CB3EF3B:
            break
        o += 32
    return out


def patch_qb_pab(assets: Path, new_entries: list[QbStruct], names: list[str],
                 props_key: int = K_PROPS, list_key: int = K_LIST) -> tuple[bytes, bytes]:
    pak = bytearray(_load(assets / "data" / "pak" / "qb.pak.xen"))
    pab = _load(assets / "data" / "pak" / "qb.pab.xen")
    idx = _pak_index(pak)
    target = None
    for i, f in enumerate(idx[:-1]):
        data = pab[f[1]:f[1] + f[2]]
        if props_key.to_bytes(4, "big") in data:
            q = QbFile(data)
            if q.find(props_key) is not None and q.find(list_key) is not None:
                target = (i, q)
                break
    if target is None:
        raise RuntimeError(f"no se encontro la lista {list_key:08x} en qb.pab")
    i, q = target
    props = q.find(props_key).value
    lst = q.find(list_key).value
    if lst.elem == "empty":  # la lista de DLC viene vacia en el disco
        lst.elem = "qbkey"
    have = {m.name for m in props.members}
    for st, name in zip(new_entries, names):
        key = qbkey(name)
        props.members = [m for m in props.members if m.name != key]
        from qb_io import QbMember
        props.members.append(QbMember("struct", key, st))
        if key not in lst.values:
            lst.values.append(key)
    _ = have
    new_data = q.build()
    old_off, old_size = idx[i][1], idx[i][2]
    end_old = old_off + old_size
    end_new = old_off + len(new_data)
    delta = (max(0, end_new - end_old) + 0xFFF) & ~0xFFF
    out = bytearray(pab[:old_off]) + new_data
    out += bytes(end_old + delta - len(out)) if end_old + delta > len(out) else b""
    out = out[:end_old + delta]
    out += pab[end_old:]
    struct.pack_into(">I", pak, i * 32 + 8, len(new_data))
    for j, f in enumerate(idx):
        if j != i and f[1] >= end_old and f[0] != 0x2CB3EF3B:
            struct.pack_into(">I", pak, j * 32 + 4, f[1] + delta)
    return bytes(pak), bytes(out)


# --------------------------------------------------------------------------- textos (qs)
QS_FILES = ["qs.pak.xen", "qs_f.pak.xen", "qs_g.pak.xen", "qs_i.pak.xen", "qs_s.pak.xen"]


def patch_qs(path: Path, lines: list[str]) -> bytes:
    entries, last, first = read_pak(_load(path))
    anchor = '"' + BS + 'LLasso"'
    for e in entries:
        if len(e.data) < 64 or e.data[:2] != b"\xff\xfe":
            continue
        txt = e.data[2:].decode("utf-16-le", "ignore")
        if anchor not in txt:
            continue
        body = txt.rstrip("\n")
        existing = set(body.split("\n"))
        add = [ln for ln in lines if ln not in existing]
        txt = body + "\n" + "\n".join(add) + "\n\n"
        e.data = b"\xff\xfe" + txt.encode("utf-16-le")
        break
    return chnk(write_pak(entries, header_area=first, last=last))


# --------------------------------------------------------------------------- songstat
def patch_songstat(assets: Path, names: list[str]) -> bytes:
    """Anade songs/<nombre>.stat (copia del de la plantilla con el nombre cambiado) para cada custom."""
    entries, last, first = read_pak(_load(assets / "data" / "pak" / "songstat.pak.xen"))
    tmpl = next(e for e in entries if e.fullname == qbkey(f"songs{BS}{TEMPLATE}.stat"))
    have = {e.fullname for e in entries}
    for n in names:
        full = qbkey(f"songs{BS}{n}.stat")
        if full in have:
            continue
        e = copy.copy(tmpl)
        e.fullname, e.name = full, qbkey(n)
        d = bytearray(tmpl.data)
        if struct.unpack_from(">I", d, 4)[0] == qbkey(TEMPLATE):
            struct.pack_into(">I", d, 4, qbkey(n))
        e.data = bytes(d)
        entries.append(e)
    return chnk(write_pak(entries, header_area=first, last=last))


# --------------------------------------------------------------------------- volumen (_rms.qd)
def set_rms(pak: bytes, name: str, song) -> tuple[bytes, str]:
    """Sustituye la curva de volumen de la plantilla por la del audio de la custom."""
    from audio_rms import build_qd, read_qd, rms_curve
    entries, last, first = read_pak(pak)
    e = next((x for x in entries if x.fullname == qbkey(f"songs{BS}{name}_rms.qd")), None)
    if e is None:
        return pak, "sin _rms"
    try:
        fsbs = [wt_decrypt_file(str(song.audio[k])) for k in ("1", "2", "3") if k in song.audio]
        vals, how = rms_curve(fsbs), "audio"
    except Exception as ex:  # sin decodificador: curva de la plantilla
        vals, how = [], f"plantilla ({ex.__class__.__name__})"
    if not vals:
        vals = read_qd(e.data)
    e.data = build_qd(name, vals)
    return write_pak(entries, header_area=first, last=last), how


# --------------------------------------------------------------------------- audio
def build_audio(song, name: str, assets: Path, table: bytes, out_music: Path) -> None:
    out_music.mkdir(parents=True, exist_ok=True)
    for k in ("1", "2", "3", "preview"):
        src = song.audio.get(k) or song.audio.get("1")
        van = (assets / "data" / "music" / f"{TEMPLATE}_{k}.fsb.xen").read_bytes()
        _, trailer = wor_decrypt(van, table)
        fsb = wt_decrypt_file(str(src))
        (out_music / f"{name}_{k}.fsb.xen").write_bytes(wor_encrypt(fsb, table, trailer))


# --------------------------------------------------------------------------- principal
def _escape(s: str) -> str:
    return s.replace('"', "'").replace("\n", " ").strip()


def props_entry(template: QbStruct, name: str, song, duration_s: int) -> tuple[QbStruct, list[str]]:
    st = copy.deepcopy(template)
    title = BS + "L" + _escape(song.title or song.name)
    artist = BS + "L" + _escape(song.artist or "")
    lines = []
    for m in st.members:
        if m.name == M_CHECKSUM:
            m.value = qbkey(name)
        elif m.name == M_NAME:
            m.value = name.encode()
        elif m.name == M_TITLE:
            m.value = qskey(title)
        elif m.name == M_ARTIST:
            m.value = qskey(artist)
        elif m.name == M_YEAR and str(song.year).strip().isdigit():
            m.value = int(str(song.year).strip())
        elif m.name == M_DURATION and duration_s > 0:
            m.value = duration_s
    lines.append(f'{qskey(title):08x} "{title}"')
    lines.append(f'{qskey(artist):08x} "{artist}"')
    return st, lines


def source_hash(song) -> str:
    h = hashlib.sha1(str(VERSION).encode())
    for p in [song.pak_path, *sorted(song.audio.values())]:
        st = Path(p).stat()
        h.update(f"{p}|{st.st_size}|{int(st.st_mtime)}".encode())
    return h.hexdigest()


def main(argv=None) -> int:
    here = Path(__file__).resolve().parent
    root = here.parent.parent
    ap = argparse.ArgumentParser()
    ap.add_argument("sources", nargs="*", default=[str(root / "song")])
    ap.add_argument("--assets", default=str(root / "re-wor" / "assets"))
    ap.add_argument("--out", default=str(root / "re-wor" / "mods" / "_wtde_customs"))
    ap.add_argument("--keys", default=str(root / "re-wor" / "mods" / "_cache" / "wor_fsb_keys.bin"))
    ap.add_argument("--force", action="store_true")
    a = ap.parse_args(argv)
    assets, out = Path(a.assets), Path(a.out)
    table = load_key_table(a.keys)
    songs = []
    for s in a.sources:
        songs += [x for x in find_songs(s) if x.kind == "custom"]
    seen = set()
    songs = [s for s in songs if not (s.name in seen or seen.add(s.name))]
    if not songs:
        print("No hay canciones custom.")
        return 0
    state_path = out / "wtde_state.json"
    state = json.loads(state_path.read_text()) if state_path.exists() and not a.force else {}

    tmpl_pak_path = next(p for p in (assets / "data" / "songs").iterdir()
                         if p.name.lower() == f"{TEMPLATE}_song.pak.xen")
    tmpl_pak = tmpl_pak_path.read_bytes()
    # Plantilla de entrada de la lista: la de la cancion plantilla.
    pak_d = _load(assets / "data" / "pak" / "qb.pak.xen")
    pab_d = _load(assets / "data" / "pak" / "qb.pab.xen")
    tmpl_entry = None
    for f in _pak_index(pak_d)[:-1]:
        data = pab_d[f[1]:f[1] + f[2]]
        if K_PROPS.to_bytes(4, "big") in data:
            q = QbFile(data)
            it = q.find(K_PROPS)
            if it is not None:
                tmpl_entry = it.value.get(qbkey(TEMPLATE))
                break
    if tmpl_entry is None:
        raise RuntimeError("no se encontro la entrada de la plantilla en gh6_songlist_props")

    entries, names, qs_lines, report = [], [], [], []
    for song in songs:
        name = internal_name(song.name)
        chart = load_chart(song)
        end_ms = max((n.time + n.length for v in chart.notes.values() for n in v), default=0)
        dur = max(1, round(end_ms / 1000) + 3)
        st, lines = props_entry(tmpl_entry, name, song, dur)
        entries.append(st)
        names.append(name)
        qs_lines += lines
        h = source_hash(song)
        pak_out = out / "data" / "songs" / f"{name}_song.pak.xen"
        if state.get(name) == h and pak_out.exists():
            report.append(f"  = {name}: sin cambios")
            continue
        try:
            pak = build_replacement_pak(tmpl_pak, TEMPLATE, song)
            pak = rename_song_pak(pak, TEMPLATE, name)
            pak, rms_how = set_rms(pak, name, song)
            pak_out.parent.mkdir(parents=True, exist_ok=True)
            pak_out.write_bytes(pak)  # archivo nuevo: no esta en compress.toc, va sin comprimir
            if song.audio:
                build_audio(song, name, assets, table, out / "data" / "music")
            state[name] = h
            report.append(f"  + {name}: {song.title} - {song.artist} (volumen: {rms_how})")
        except Exception as ex:  # una custom rota no debe impedir las demas
            report.append(f"  ! {name}: ERROR {ex}")
            entries.pop()
            names.pop()
            del qs_lines[-2:]
    qb_pak, qb_pab = patch_qb_pab(assets, entries, names)
    (out / "data" / "pak").mkdir(parents=True, exist_ok=True)
    (out / "data" / "pak" / "qb.pak.xen").write_bytes(chnk(qb_pak))
    (out / "data" / "pak" / "qb.pab.xen").write_bytes(chnk(qb_pab))
    # songstat.pak NO se toca: anadir entradas .stat hace crashear al entrar a quickplay.
    (out / "data" / "pak" / "songstat.pak.xen").unlink(missing_ok=True)
    for f in QS_FILES:
        p = assets / "data" / "pak" / f
        if p.exists():
            (out / "data" / "pak" / f).write_bytes(patch_qs(p, qs_lines))
    (out / "mod.toml").write_text(
        'name = "Canciones custom (WTDE)"\nauthor = "wtde2wor"\nversion = "1"\ntype = "song"\n'
        'description = "Canciones de GHWT:DE convertidas automaticamente como canciones nuevas"\n',
        encoding="utf-8")
    state_path.write_text(json.dumps(state, indent=1))
    print(f"{len(names)} cancion(es) custom en {out}")
    print("\n".join(report))
    return 0


if __name__ == "__main__":
    sys.exit(main())
