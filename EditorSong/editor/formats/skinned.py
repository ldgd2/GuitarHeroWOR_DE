"""Mallas con pesos de WoR (personajes, instrumentos con huesos).

Bloque de vertices ("SubMesh Cafe", offset en +0x68 de la malla):
  4 x 0xCAFEBAB4, u32 0, u32 n grupos de 1 hueso, u32 n de 2 huesos, u32 n de 3 huesos
  cada grupo: u32 n, u8[4] huesos (indices del esqueleto de 128), u32 n, u32 0xFACEF000
              y n vertices de 32 bytes: f32 x, y, z | u16 peso1, u16 peso0 | normal 11/11/10 |
              2 tangentes | 0xBAADF00D
  peso del hueso 0 = segundo u16, hueso 1 = primer u16, hueso 2 = el resto hasta 1 (verificado: suman 1).
UV: flujo aparte (offset en +0x18, tamano de vertice en +0x1C, 32 bytes de cabecera), mismo orden.
"""
from __future__ import annotations

import struct

import numpy as np

from .scene import _dec_normal


def read_weighted(d: bytes, uv_start: int, vstart: int, n: int, flags: int, m) -> None:
    counts = struct.unpack_from(">3I", d, vstart + 20)
    o = vstart + 32
    pos, wts, jts, nor = [], [], [], []
    for gi in range(sum(counts)):
        cnt, bones, _cnt2, _mk = struct.unpack_from(">I4sII", d, o)
        o += 16
        raw = np.frombuffer(d, np.uint8, cnt * 32, o).reshape(cnt, 32)
        o += cnt * 32
        pos.append(np.ascontiguousarray(raw[:, :12]).view(">f4").astype(np.float32).reshape(cnt, 3))
        w = np.ascontiguousarray(raw[:, 12:16]).view(">u2").astype(np.float32).reshape(cnt, 2) / 65535.0
        nb = 1 if gi < counts[0] else 2 if gi < counts[0] + counts[1] else 3
        ww = np.zeros((cnt, 4), np.float32)
        ww[:, 0] = w[:, 1]
        if nb >= 2:
            ww[:, 1] = w[:, 0]
        if nb == 3:
            ww[:, 2] = np.clip(1 - w[:, 0] - w[:, 1], 0, 1)
        if nb == 1:
            ww[:, 0] = 1.0
        s = ww.sum(1, keepdims=True)
        wts.append(ww / np.where(s > 0, s, 1))
        jj = np.zeros((cnt, 4), np.uint16)
        jj[:, :nb] = list(bones[:nb])
        jts.append(jj)
        nor.append(_dec_normal(np.ascontiguousarray(raw[:, 16:20]).view(">u4").reshape(cnt)))
    m.positions = np.concatenate(pos)[:n]
    m.weights = np.concatenate(wts)[:n]
    m.joints = np.concatenate(jts)[:n]
    m.normals = np.concatenate(nor)[:n]
    if uv_start:
        st = m.stride or 4
        raw = np.frombuffer(d, np.uint8, n * st, uv_start + 32).reshape(n, st)
        m.uvs.append(np.ascontiguousarray(raw[:, :4]).view(">f2").astype(np.float32).reshape(n, 2))
        if st >= 8:
            m.uvs.append(np.ascontiguousarray(raw[:, 4:8]).view(">f2").astype(np.float32).reshape(n, 2))
