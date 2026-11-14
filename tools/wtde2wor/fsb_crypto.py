"""Cifrado de audio .fsb.xen de World Tour (WTDE) y Warriors of Rock.

World Tour / WTDE: inversion de bits + XOR con una clave derivada del nombre del archivo.

Warriors of Rock (obtenido de la rutina del juego sub_823A9BB8, LibTomCrypt):
  archivo = FSB4 cifrado  +  bloque final de 2048 bytes
  - bloque final: AES-128-CTR (IV = 0, contador big-endian) con la clave base = tabla[0:16];
    descifrado empieza por la magia 0x5E83AC07; los 4 bytes en +4 eligen la clave del contenido:
        off = ((b0 + b1 + b2 + b3) << 4) & 0xFF0 ; clave = tabla[off:off+16]
  - contenido: AES-128-CTR (IV = 0) con esa clave, desde el byte 0.
La tabla de 4096 bytes vive en la memoria del juego (0x82856A40) y se lee de la copia del usuario;
no se distribuye con esta herramienta.
"""
from __future__ import annotations

import os
import struct
import zlib

from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes

WOR_MAGIC = 0x5E83AC07
WOR_KEY_TABLE_ADDR = 0x82856A40
WOR_TRAILER = 2048

# ----------------------------------------------------------------- World Tour

_REV = bytes(int(f"{i:08b}"[::-1], 2) for i in range(256))


def _qbkey_char(ch: str) -> int:
    return (~zlib.crc32(ch.lower().encode("utf-8"))) & 0xFFFFFFFF


def wt_key(name: str) -> bytes:
    xor_val = 0xFFFFFFFF
    enc = []
    for i in range(32):
        xor_val = (_qbkey_char(name[i % len(name)]) ^ xor_val) & 0xFFFFFFFF
        enc.append(name[xor_val % len(name)])
    key = bytearray()
    for i in range(31):
        xor_val = (_qbkey_char(enc[i]) ^ xor_val) & 0xFFFFFFFF
        c = i & 3
        xor_val = (xor_val >> c) & ((1 << (32 - c)) - 1)
        b = xor_val & 0xFF
        if b == 0:
            break
        key.append(b)
    return bytes(key)


def wt_decrypt(data: bytes, name: str) -> bytes:
    """name = nombre del archivo sin '.fsb.xen' (p.ej. 'TakeMeOut_1')."""
    key = wt_key(name)
    kl = len(key)
    rev = data.translate(_REV)
    reps = (len(data) + kl - 1) // kl
    stream = (key * reps)[: len(data)]
    return (int.from_bytes(rev, "big") ^ int.from_bytes(stream, "big")).to_bytes(len(data), "big")


def wt_decrypt_file(path: str) -> bytes:
    base = os.path.basename(path)
    name = base[: -len(".fsb.xen")] if base.lower().endswith(".fsb.xen") else base
    data = open(path, "rb").read()
    for cand in (name, name.lower()):
        dec = wt_decrypt(data, cand)
        if dec[:3] == b"FSB":
            return dec
    if data[:3] == b"FSB":
        return data  # ya descifrado
    raise ValueError(f"No se pudo descifrar {path} (clave World Tour)")

# ----------------------------------------------------------------- Warriors of Rock


def _ctr(key: bytes, data: bytes) -> bytes:
    return Cipher(algorithms.AES(key), modes.CTR(b"\0" * 16)).decryptor().update(data)


def _content_key(table: bytes, trailer_plain: bytes) -> bytes:
    w = trailer_plain[4:8]
    off = ((w[0] + w[1] + w[2] + w[3]) << 4) & 0xFF0
    return table[off:off + 16]


def wor_decrypt(data: bytes, table: bytes) -> tuple[bytes, bytes]:
    """Devuelve (fsb4 en claro, bloque final en claro)."""
    trailer = _ctr(table[:16], data[-WOR_TRAILER:])
    if struct.unpack_from(">I", trailer, 0)[0] != WOR_MAGIC:
        raise ValueError("Bloque final WoR invalido (tabla de claves incorrecta?)")
    return _ctr(_content_key(table, trailer), data[:-WOR_TRAILER]), trailer


def wor_encrypt(fsb: bytes, table: bytes, trailer_plain: bytes | None = None) -> bytes:
    """Cifra un FSB4 en formato WoR. Si no se da bloque final, se genera uno nuevo."""
    if trailer_plain is None:
        trailer = bytearray(os.urandom(WOR_TRAILER))
        struct.pack_into(">I", trailer, 0, WOR_MAGIC)
        trailer_plain = bytes(trailer)
    assert struct.unpack_from(">I", trailer_plain, 0)[0] == WOR_MAGIC
    body = _ctr(_content_key(table, trailer_plain), fsb)
    return body + _ctr(table[:16], trailer_plain)


def load_key_table(path: str) -> bytes:
    """Tabla de claves (4096 bytes) volcada desde la memoria del juego del usuario."""
    t = open(path, "rb").read()
    if len(t) < 4096:
        raise ValueError("Tabla de claves WoR incompleta")
    return t[:4096]
