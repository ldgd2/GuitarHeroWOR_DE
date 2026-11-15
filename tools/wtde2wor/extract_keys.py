"""Saca la tabla de claves de audio de WoR (4096 bytes) del default.xex de TU copia del juego.

La tabla esta fija en la imagen del ejecutable, en 0x82856A40 (ver fsb_crypto.py), asi que no hace falta tener el
juego abierto: se descifra el xex (XEX2, clave AES de retail) y se copia de ahi.

  python tools/wtde2wor/extract_keys.py              -> re-wor/mods/_cache/wor_fsb_keys.bin
  python tools/wtde2wor/extract_keys.py --xex X --out Y

Tambien lo usan setup.bat y wormod (si falta la tabla, la generan solos).
"""
from __future__ import annotations

import argparse
import hashlib
import struct
import sys
from pathlib import Path

from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_XEX = ROOT / "re-wor" / "assets" / "default.xex"
DEFAULT_OUT = ROOT / "re-wor" / "mods" / "_cache" / "wor_fsb_keys.bin"

TABLE_ADDR = 0x82856A40
TABLE_SIZE = 4096
# SHA-1 de la tabla de la version que se conoce (Xbox 360, retail). Si no coincide se avisa, pero se guarda igual.
KNOWN_SHA1 = None

_XEX_KEYS = (("retail", bytes.fromhex("20B185A59D28FDC340583FBB0896BF91")), ("devkit", bytes(16)))


def _aes_cbc_dec(key: bytes, data: bytes) -> bytes:
    d = Cipher(algorithms.AES(key), modes.CBC(bytes(16))).decryptor()
    return d.update(data) + d.finalize()


def xex_image(xex: bytes) -> tuple[bytes, int]:
    """Imagen en memoria (PE) de un XEX2 cifrado con compresion basica. Devuelve (imagen, direccion base)."""
    if xex[:4] != b"XEX2":
        raise ValueError("no es un archivo XEX2")
    pe_off, sec_off, n_hdr = struct.unpack(">I", xex[8:12])[0], struct.unpack(">I", xex[16:20])[0], struct.unpack(">I", xex[20:24])[0]
    ff = None
    for i in range(n_hdr):
        k, v = struct.unpack(">II", xex[24 + i * 8:32 + i * 8])
        if k == 0x3FF:   # formato del archivo (cifrado / compresion)
            ff = v
    if ff is None:
        raise ValueError("el xex no tiene cabecera de formato")
    enc, comp = struct.unpack(">HH", xex[ff + 4:ff + 8])
    if comp != 1:
        raise ValueError(f"compresion {comp} no soportada (solo la 'basica'; la de este juego lo es)")
    base = struct.unpack(">I", xex[sec_off + 0x110:sec_off + 0x114])[0]
    file_key = xex[sec_off + 0x150:sec_off + 0x160]
    raw = xex[pe_off:pe_off + (len(xex) - pe_off) // 16 * 16]
    ff_size = struct.unpack(">I", xex[ff:ff + 4])[0]
    blocks = [struct.unpack(">II", xex[ff + 8 + j * 8:ff + 16 + j * 8]) for j in range((ff_size - 8) // 8)]
    for _name, key in (_XEX_KEYS if enc == 1 else (("sin cifrar", None),)):
        body = _aes_cbc_dec(_aes_cbc_dec(key, file_key), raw) if key else raw
        img, p = bytearray(), 0
        for data_size, zero_size in blocks:
            img += body[p:p + data_size] + bytes(zero_size)
            p += data_size
        if img[:2] == b"MZ":
            return bytes(img), base
    raise ValueError("no se pudo descifrar el xex (no es una copia de retail/devkit valida?)")


def extract(xex_path: Path = DEFAULT_XEX, out_path: Path = DEFAULT_OUT) -> Path:
    if not xex_path.exists():
        raise FileNotFoundError(f"no esta {xex_path}: extrae primero tu ISO en re-wor/assets")
    img, base = xex_image(xex_path.read_bytes())
    off = TABLE_ADDR - base
    table = img[off:off + TABLE_SIZE]
    if len(table) != TABLE_SIZE or len(set(table)) < 64:
        raise ValueError("la tabla no esta donde se esperaba (otra version del juego?)")
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_bytes(table)
    return out_path


def ensure(out_path: Path = DEFAULT_OUT, xex_path: Path = DEFAULT_XEX) -> bool:
    """Genera la tabla si falta y hay xex. True si al final existe."""
    if out_path.exists():
        return True
    try:
        extract(xex_path, out_path)
        return True
    except Exception as ex:  # noqa: BLE001
        print(f"[claves] no se pudo sacar la tabla de {xex_path}: {ex}", file=sys.stderr)
        return False


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--xex", type=Path, default=DEFAULT_XEX)
    ap.add_argument("--out", type=Path, default=DEFAULT_OUT)
    a = ap.parse_args()
    try:
        out = extract(a.xex, a.out)
    except Exception as ex:  # noqa: BLE001
        print(f"[ERROR] {ex}")
        return 1
    print(f"Tabla de claves guardada en {out} (sha1 {hashlib.sha1(out.read_bytes()).hexdigest()[:12]})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
