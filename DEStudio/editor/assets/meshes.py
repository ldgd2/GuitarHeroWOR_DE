"""Escena de Neversoft (.scn/.skin ya leida) -> glTF, agrupando las mallas por material.

Los materiales usan la primera textura de color de su lista (las de tipo 1 son mapas de normales).
Mezcla: modo != 0 -> transparente; corte de alfa > 0 -> recorte (hojas, rejas, publico plano).
"""
from __future__ import annotations

from urllib.parse import quote

import numpy as np

from ..formats.glb import GlbBuilder
from ..formats.scene import Scene
from . import textures


def tex_url(src: str, key: int) -> str:
    return f"/api/asset/tex?src={quote(src, safe='')}&key=0x{key:08x}"


def material_for(b: GlbBuilder, mat, tex_sources: list[str], cache: dict) -> int:
    if mat.key in cache:
        return cache[mat.key]
    base = normal = None
    for k in mat.textures:
        for src in tex_sources:
            t = textures.info(src).get(k)
            if t is None:
                continue
            if t.kind in (0, 3) and base is None and t.kind == 0:
                base = b.texture(tex_url(src, k))
            elif t.kind == 1 and normal is None:
                normal = b.texture(tex_url(src, k))
            break
    alpha, cut = "OPAQUE", 0.5
    if mat.alpha_cut:
        alpha, cut = "MASK", max(0.05, mat.alpha_cut / 255.0)
    elif mat.blend not in (0,):
        alpha = "BLEND"
    cache[mat.key] = b.material(f"0x{mat.key:08x}", base, normal, alpha, cut, True,
                                extras={"blend": mat.blend, "flags": mat.flags, "template": f"0x{mat.template:08x}"})
    return cache[mat.key]


def add_scene(b: GlbBuilder, sc: Scene, name: str, tex_sources: list[str], parent_children: list | None = None,
              mat_cache: dict | None = None) -> int:
    """Anade la escena como un nodo con una malla (una primitiva por material). Devuelve el nodo."""
    mat_cache = {} if mat_cache is None else mat_cache
    groups: dict[int, list] = {}
    for m in sc.meshes:
        if m.positions is None or m.indices is None or not len(m.indices) or m.error:
            continue
        groups.setdefault(m.material, []).append(m)
    prims = []
    for mk, ms in groups.items():
        pos, nor, uv, col, idx, base = [], [], [], [], [], 0
        has_col = any(m.colors is not None and (m.colors[:, :3] < 250).any() for m in ms)
        for m in ms:
            n = len(m.positions)
            pos.append(m.positions)
            nor.append(m.normals if m.normals is not None else np.tile([0, 1, 0], (n, 1)).astype(np.float32))
            uv.append(m.uvs[0] if m.uvs else np.zeros((n, 2), np.float32))
            col.append(m.colors if m.colors is not None else np.full((n, 4), 255, np.uint8))
            idx.append(m.indices + base)
            base += n
        mat = sc.materials.get(mk)
        mi = material_for(b, mat, tex_sources, mat_cache) if mat else None
        P = np.concatenate(pos)
        ok = np.isfinite(P).all(1)
        if not ok.all():
            P[~ok] = 0
        prims.append(b.primitive(P, np.concatenate(idx), np.concatenate(nor), np.concatenate(uv),
                                 colors=np.concatenate(col) if has_col else None, material=mi))
    mesh = b.mesh(name, prims) if prims else None
    return b.node(name, mesh, root=parent_children is None)
