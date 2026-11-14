"""Curva de volumen (<cancion>_rms.qd) para canciones nuevas de Warriors of Rock.

Toda cancion vanilla de WoR lleva en su PAK `songs\\<cancion>_rms.qd`: el nivel RMS de la mezcla en dB,
20 valores por segundo (ventanas de 50 ms, suelo ~ -81.5 dB). El juego lo busca por el NOMBRE de la
cancion; si una cancion nueva conserva el de la plantilla, el juego no lo encuentra.

Formato (.qd, little-endian salvo las claves):
  02 00 00 00 | K | u32 tam(=total-21) | K | 00 00 00 00 | 0C | K | 02 | u16 n | n x f32 | 00
K = qbkey("<cancion>_RMS_MAP") en little-endian. El juego arma ese nombre ("songs/" + cancion +
"_rms.qd" y cancion + "_RMS_MAP") y busca K dentro del archivo: si no coincide, usa un puntero sin
inicializar y crashea al tocar la cancion.

El audio de las customs de GHWT:DE es MP3 dentro de FSB4, con varios flujos estereo intercalados trama a
trama (canales/2 flujos). Se separan, se decodifican (miniaudio) y se mezclan.
"""
from __future__ import annotations

import math
import struct
import zlib

FLOOR_DB = -81.5
RATE = 20  # valores por segundo

_BITRATES = [0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320]
_RATES = {3: [44100, 48000, 32000], 2: [22050, 24000, 16000], 0: [11025, 12000, 8000]}


def _frame_len(d: bytes, o: int) -> int:
    if o + 4 > len(d) or d[o] != 0xFF or (d[o + 1] & 0xE0) != 0xE0:
        return 0
    ver = (d[o + 1] >> 3) & 3
    layer = (d[o + 1] >> 1) & 3
    bri = d[o + 2] >> 4
    sri = (d[o + 2] >> 2) & 3
    pad = (d[o + 2] >> 1) & 1
    if ver == 1 or layer != 1 or bri in (0, 15) or sri == 3:
        return 0
    br = _BITRATES[bri] if ver == 3 else [0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160][bri]
    sr = _RATES[ver][sri]
    return (144 if ver == 3 else 72) * br * 1000 // sr + pad


def split_fsb_mp3(fsb: bytes) -> tuple[list[bytes], int]:
    """FSB4 de un solo sample MP3 -> (flujos MP3 separados, frecuencia)."""
    if fsb[:4] != b"FSB4":
        raise ValueError("no es FSB4")
    shdr = struct.unpack_from("<i", fsb, 8)[0]
    o = 48
    freq = struct.unpack_from("<i", fsb, o + 52)[0]
    ch = struct.unpack_from("<H", fsb, o + 62)[0]
    nstreams = max(1, ch // 2)
    d = fsb[48 + shdr:]
    streams = [bytearray() for _ in range(nstreams)]
    pos, i = 0, 0
    while pos < len(d):
        n = _frame_len(d, pos)
        if not n:
            pos += 1  # relleno entre tramas
            continue
        streams[i % nstreams] += d[pos:pos + n]
        i += 1
        pos += n
    return [bytes(s) for s in streams], freq


def _decode(mp3: bytes):
    import miniaudio
    import numpy as np
    snd = miniaudio.decode(mp3, output_format=miniaudio.SampleFormat.FLOAT32, nchannels=1)
    return np.frombuffer(snd.samples, dtype=np.float32), snd.sample_rate


def rms_curve(fsbs: list[bytes], skip_first_stream_of_last: bool = True) -> list[float]:
    """Curva RMS (dB, 20/s) de la mezcla de todos los flujos (sin el publico del ultimo FSB)."""
    import numpy as np
    mix = None
    rate = 48000
    for fi, fsb in enumerate(fsbs):
        streams, _ = split_fsb_mp3(fsb)
        for si, s in enumerate(streams):
            if skip_first_stream_of_last and fi == len(fsbs) - 1 and len(fsbs) > 1 and si == 0:
                continue  # publico
            if not s:
                continue
            x, rate = _decode(s)
            if mix is None:
                mix = x.astype(np.float64).copy()
            else:
                if len(x) > len(mix):
                    mix = np.pad(mix, (0, len(x) - len(mix)))
                mix[:len(x)] += x
    if mix is None:
        return []
    win = rate // RATE
    n = len(mix) // win
    frames = mix[:n * win].reshape(n, win)
    rms = np.sqrt((frames ** 2).mean(axis=1))
    with np.errstate(divide="ignore"):
        db = 20.0 * np.log10(np.maximum(rms, 1e-12))
    return [max(FLOOR_DB, float(v)) for v in db]


def rms_map_key(song_name: str) -> bytes:
    return struct.pack("<I", (~zlib.crc32(f"{song_name}_rms_map".lower().encode())) & 0xFFFFFFFF)


def build_qd(song_name: str, values: list[float]) -> bytes:
    """.qd con los valores dados para la cancion `song_name`."""
    k = rms_map_key(song_name)
    n = min(len(values), 0xFFFF)
    body = struct.pack("<%df" % n, *values[:n])
    total = 28 + len(body) + 1
    return (b"\x02\x00\x00\x00" + k + struct.pack("<I", total - 21) + k + b"\x00" * 4
            + b"\x0c" + k + b"\x02" + struct.pack("<H", n) + body + b"\x00")


def read_qd(data: bytes) -> list[float]:
    n = struct.unpack_from("<H", data, 26)[0]
    return list(struct.unpack_from("<%df" % n, data, 28))
