"""Personaje montado (cuerpo + piezas + instrumento) -> .glb con esqueleto de 128 huesos listo para animar.

Huesos: un nodo por hueso con la pose de enlace del .ske (rotacion conjugada, ver formats/ska.py), nombrados
"b<indice>_<nombre>" para que la vista 3D los case con las pistas del .ska por indice. Las matrices inversas
de enlace se copian tal cual del .ske (fila en el archivo = columna en glTF).
Piezas con pesos -> JOINTS/WEIGHTS del archivo; piezas rigidas (instrumentos) -> peso 1 a su hueso.
"""
from __future__ import annotations

import hashlib
import json

import numpy as np

from ..assets.meshes import material_for
from ..core import paths
from ..core.names import qbkey
from ..formats import pak as P
from ..formats.glb import GlbBuilder
from ..formats.scene import read_scene
from ..formats.skeleton import read_ske
from . import catalog as C

VERSION = 1


def _rigid_bone(kind: str, mesh_center_x: float) -> int:
    if kind in ("guitar", "bass"):
        return C.BONE_GUITAR
    if kind == "mic_stand":
        return C.BONE_MIC_STAND
    if kind == "mic":
        return C.BONE_MIC
    if kind == "sticks":
        return C.BONE_PALM_L if mesh_center_x > 0 else C.BONE_PALM_R
    return 0


def character_glb(spec: dict) -> bytes:
    g = "f" if spec.get("gender") == "f" else "m"
    ids = [int(x, 16) for x in spec.get("pieces", [])][:40]
    key = hashlib.sha1(json.dumps([VERSION, g, ids]).encode()).hexdigest()[:16]
    f = paths.CACHE / "band" / f"{key}.glb"
    if f.exists():
        return f.read_bytes()

    body = C.BODIES[g]
    ga, cp = P.archive("cas_globalarchive"), P.archive("cas_pieces")
    sk = read_ske(ga.read(ga.by_key[body["ske"]]))
    labels = sk.labels()
    b = GlbBuilder()

    # --- esqueleto
    joint_nodes = []
    for i in range(len(sk.names)):
        q = sk.rot[i].astype(np.float64)
        q[:3] *= -1
        q /= np.linalg.norm(q) or 1
        joint_nodes.append(b.node(f"b{i:03d}_{labels[i]}", t=sk.pos[i], r=q, root=False))
    for i, p in enumerate(sk.parents):
        if 0 <= p < len(joint_nodes):
            b.j["nodes"][joint_nodes[p]].setdefault("children", []).append(joint_nodes[i])
    roots = [joint_nodes[i] for i, p in enumerate(sk.parents) if p < 0]
    skin = b.skin(joint_nodes, sk.inv_bind, roots[0] if roots else None)

    # --- piezas
    by_id = {int(p["id"], 16): p for p in C.catalog()["pieces"]}
    sources_body = [f"arch/cas_globalarchive/0x{body['tex']:08x}"]
    parts = [("body", ga, body["skin"], sources_body)]
    for k in ids:
        info = by_id.get(k)
        if not info or k not in cp.by_key:
            continue
        src = ([f"arch/cas_pieces/{info['tex']}"] if info.get("tex") else []) + sources_body
        parts.append((info["kind"] if info["kind"] != "body" else "piece", cp, k, src))

    mats: dict = {}
    mesh_nodes = []
    for kind, arch, k, src in parts:
        try:
            sc = read_scene(arch.read(arch.by_key[k]))
        except Exception:  # noqa: BLE001
            continue
        prims = []
        for m in sc.meshes:
            if m.positions is None or m.indices is None or not len(m.indices) or m.error:
                continue
            n = len(m.positions)
            if m.joints is not None:
                joints, weights = m.joints, m.weights
            else:
                bone = _rigid_bone(kind, float(m.positions[:, 0].mean()))
                joints = np.zeros((n, 4), np.uint16)
                joints[:, 0] = bone
                weights = np.zeros((n, 4), np.float32)
                weights[:, 0] = 1
            mat = sc.materials.get(m.material)
            mi = material_for(b, mat, src, mats) if mat else None
            prims.append(b.primitive(m.positions, m.indices, m.normals, m.uvs[0] if m.uvs else None,
                                     joints=joints, weights=weights, material=mi))
        if prims:
            mesh_nodes.append(b.node(f"{kind}_{k:08x}", b.mesh(f"{kind}_{k:08x}", prims), root=False, skin=skin))
    b.node("character", children=roots + mesh_nodes, extras={"gender": g})
    data = b.build()
    f.parent.mkdir(parents=True, exist_ok=True)
    f.write_bytes(data)
    return data
