"""Audio de la cancion para el editor (solo lectura del juego; lo convertido va a cache/audio/).

Cada cancion de WoR trae tres .fsb.xen (cifrados): _1 = bateria (4 flujos estereo), _2 = guitarra, bajo y voz
(3 flujos), _3 = publico y resto de la cancion (2 flujos). Las customs convertidas desde GHWT:DE llevan MP3
dentro del FSB4 (flujos intercalados trama a trama): se descifran, se separan y se decodifican con miniaudio.
Las canciones del juego base van en XMA (sin decodificador libre en Python): no tienen audio en el editor.

El editor recibe UNA mezcla WAV (16 bits, 48 kHz) de los grupos activos, mas los picos para dibujar la onda.
Una sola fuente de audio = sincronizacion exacta: la linea de tiempo sigue al reloj del audio.
En disco (cache/audio/) solo quedan los picos y las mezclas pedidas; lo decodificado vive en memoria.
"""
from __future__ import annotations

import hashlib
import io
import json
import struct
import wave
from pathlib import Path

from ..core import paths
from .library import song_path

GROUPS = [  # (clave, nombre, archivo, flujos)
    ("drums", "Bateria", "1", None),
    ("guitar", "Guitarra", "2", [0]),
    ("bass", "Bajo", "2", [1]),
    ("vocals", "Voz", "2", [2]),
    ("song", "Resto de la cancion", "3", [1]),
    ("crowd", "Publico", "3", [0]),
]
PEAKS_PER_S = 100
CACHE = paths.CACHE / "audio"


def _music_files(song_id: str) -> dict[str, Path]:
    p = song_path(song_id)
    music = p.parent.parent / "music"
    base = p.name[: -len("_song.pak.xen")]
    cands = {base.lower(), base[1:].lower()} if base[:1].lower() == "a" else {base.lower()}
    out = {}
    if music.is_dir():
        for f in music.iterdir():
            n = f.name.lower()
            for c in cands:
                for k in ("1", "2", "3"):
                    if n == f"{c}_{k}.fsb.xen":
                        out[k] = f
    return out


def _fsb(path: Path) -> bytes:
    import sys
    sys.path.insert(0, str(paths.TOOLS_WT))
    from fsb_crypto import load_key_table, wor_decrypt, wt_decrypt_file
    data = path.read_bytes()
    table = paths.MODS / "_cache" / "wor_fsb_keys.bin"
    if table.exists():
        try:
            return wor_decrypt(data, load_key_table(str(table)))[0]
        except ValueError:
            pass
    return wt_decrypt_file(str(path))


def _streams(path: Path) -> list[bytes] | None:
    """MP3 de cada flujo estereo (None si el FSB no es MP3)."""
    from audio_rms import split_fsb_mp3
    fsb = _fsb(path)
    mode = struct.unpack_from("<I", fsb, 48 + 48)[0]
    if not mode & 0x200:
        return None
    return split_fsb_mp3(fsb)[0]


def _key(song_id: str) -> str:
    p = song_path(song_id)
    st = p.stat()
    return hashlib.sha1(f"{p}|{st.st_size}|{st.st_mtime_ns}".encode()).hexdigest()[:16]


_MEM: dict = {}   # una sola cancion decodificada en memoria (int16 estereo por grupo)


def _decoded(song_id: str) -> dict:
    """Grupo -> muestras estereo int16 (N, 2) a 48 kHz."""
    import miniaudio
    import numpy as np
    key = _key(song_id)
    if _MEM.get("key") == key:
        return _MEM["data"]
    files = _music_files(song_id)
    if not files:
        raise ValueError("no se encontraron los archivos de audio de la cancion")
    streams = {k: _streams(f) for k, f in files.items()}
    if all(v is None for v in streams.values()):
        raise ValueError("el audio de esta cancion es XMA (canciones del juego base): el editor solo reproduce "
                         "el audio MP3 de las canciones de mods")
    out = {}
    for g, _label, k, idx in GROUPS:
        ss = streams.get(k)
        if not ss:
            continue
        mix = None
        for i in (idx if idx is not None else range(len(ss))):
            if i >= len(ss) or not ss[i]:
                continue
            snd = miniaudio.decode(ss[i], output_format=miniaudio.SampleFormat.SIGNED16, nchannels=2, sample_rate=48000)
            x = np.frombuffer(snd.samples, dtype=np.int16).reshape(-1, 2).astype(np.int32)
            if mix is None:
                mix = x
            else:
                if len(x) > len(mix):
                    mix = np.pad(mix, ((0, len(x) - len(mix)), (0, 0)))
                mix[: len(x)] += x
        if mix is not None:
            out[g] = np.clip(mix, -32768, 32767).astype(np.int16)
    _MEM.clear()
    _MEM.update(key=key, data=out)
    return out


def info(song_id: str) -> dict:
    f = CACHE / f"{_key(song_id)}_info.json"
    if f.exists():
        return json.loads(f.read_text(encoding="utf-8"))
    try:
        dec = _decoded(song_id)
    except ValueError as ex:
        return {"available": False, "reason": str(ex), "groups": []}
    import numpy as np
    groups, peaks = [], {}
    win = 48000 // PEAKS_PER_S
    for g, label, *_ in GROUPS:
        if g not in dec:
            continue
        x = np.abs(dec[g].astype(np.int32)).max(axis=1)
        n = len(x) // win
        pk = x[: n * win].reshape(n, win).max(axis=1) / 32768.0
        groups.append({"id": g, "label": label, "seconds": round(len(x) / 48000, 3)})
        peaks[g] = [round(float(v), 3) for v in pk]
    res = {"available": True, "groups": groups, "peaks": peaks, "peaks_per_s": PEAKS_PER_S}
    CACHE.mkdir(parents=True, exist_ok=True)
    f.write_text(json.dumps(res), encoding="utf-8")
    return res


def mix_wav(song_id: str, active: list[str]) -> bytes:
    """Mezcla WAV de los grupos pedidos (cacheada en disco por combinacion)."""
    import numpy as np
    known = [g for g, *_ in GROUPS]
    sel = sorted({g for g in active if g in known}) or known
    f = CACHE / f"{_key(song_id)}_mix_{'-'.join(sel)}.wav"
    if f.exists():
        return f.read_bytes()
    dec = _decoded(song_id)
    sel = [g for g in sel if g in dec] or list(dec)
    n = max(len(dec[g]) for g in sel)
    mix = np.zeros((n, 2), np.float32)
    for g in sel:
        mix[: len(dec[g])] += dec[g].astype(np.float32)
    peak = float(np.abs(mix).max()) or 1.0
    if peak > 32000:
        mix *= 32000 / peak
    buf = io.BytesIO()
    with wave.open(buf, "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(48000)
        w.writeframes(mix.astype("<i2").tobytes())
    CACHE.mkdir(parents=True, exist_ok=True)
    f.write_bytes(buf.getvalue())
    return buf.getvalue()
