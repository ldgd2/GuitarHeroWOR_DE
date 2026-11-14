"""Escritor minimo de glTF binario (.glb): mallas, materiales con textura, nodos y esqueletos.

Sirve para la vista 3D del editor (three.js GLTFLoader) y para exportar a Blender. Las imagenes pueden ir
por URL (rapido, para el editor) o incrustadas en el .glb (para llevarse el archivo).
"""
from __future__ import annotations

import json
import struct

import numpy as np

ARRAY_BUFFER, ELEMENT_ARRAY_BUFFER = 34962, 34963
FLOAT, UBYTE, USHORT, UINT = 5126, 5121, 5123, 5125


class GlbBuilder:
    def __init__(self):
        self.j = {"asset": {"version": "2.0", "generator": "EditorSong"}, "scenes": [{"nodes": []}], "scene": 0,
                  "nodes": [], "meshes": [], "materials": [], "accessors": [], "bufferViews": [],
                  "buffers": [{"byteLength": 0}]}
        self.bin = bytearray()
        self._tex_index: dict[str, int] = {}

    # ------------------------------------------------------------ datos
    def _view(self, data: bytes, target: int | None = None) -> int:
        while len(self.bin) % 4:
            self.bin.append(0)
        v = {"buffer": 0, "byteOffset": len(self.bin), "byteLength": len(data)}
        if target:
            v["target"] = target
        self.bin += data
        self.j["bufferViews"].append(v)
        return len(self.j["bufferViews"]) - 1

    def accessor(self, arr: np.ndarray, kind: str, ctype: int, target: int | None = ARRAY_BUFFER,
                 normalized: bool = False, minmax: bool = False) -> int:
        arr = np.ascontiguousarray(arr)
        a = {"bufferView": self._view(arr.tobytes(), target), "componentType": ctype,
             "count": int(arr.shape[0]), "type": kind}
        if normalized:
            a["normalized"] = True
        if minmax:
            a["min"] = [float(x) for x in arr.min(0)]
            a["max"] = [float(x) for x in arr.max(0)]
        self.j["accessors"].append(a)
        return len(self.j["accessors"]) - 1

    # ------------------------------------------------------------ texturas y materiales
    def texture(self, uri: str | None = None, png: bytes | None = None) -> int:
        key = uri or str(id(png))
        if key in self._tex_index:
            return self._tex_index[key]
        j = self.j
        j.setdefault("images", []); j.setdefault("textures", []); j.setdefault("samplers", [{
            "magFilter": 9729, "minFilter": 9987, "wrapS": 10497, "wrapT": 10497}])
        img = {"uri": uri} if uri else {"bufferView": self._view(png), "mimeType": "image/png"}
        j["images"].append(img)
        j["textures"].append({"source": len(j["images"]) - 1, "sampler": 0})
        self._tex_index[key] = len(j["textures"]) - 1
        return self._tex_index[key]

    def material(self, name: str, base_tex: int | None = None, normal_tex: int | None = None,
                 alpha: str = "OPAQUE", cutoff: float = 0.5, double: bool = True, color=None, extras=None) -> int:
        m = {"name": name, "doubleSided": double, "alphaMode": alpha,
             "pbrMetallicRoughness": {"metallicFactor": 0.0, "roughnessFactor": 0.85}}
        if color:
            m["pbrMetallicRoughness"]["baseColorFactor"] = list(color)
        if base_tex is not None:
            m["pbrMetallicRoughness"]["baseColorTexture"] = {"index": base_tex}
        if normal_tex is not None:
            m["normalTexture"] = {"index": normal_tex}
        if alpha == "MASK":
            m["alphaCutoff"] = cutoff
        if extras:
            m["extras"] = extras
        self.j["materials"].append(m)
        return len(self.j["materials"]) - 1

    # ------------------------------------------------------------ mallas y nodos
    def primitive(self, pos, idx, normals=None, uv0=None, uv1=None, colors=None, joints=None, weights=None,
                  material: int | None = None) -> dict:
        attrs = {"POSITION": self.accessor(pos.astype(np.float32), "VEC3", FLOAT, minmax=True)}
        if normals is not None:
            attrs["NORMAL"] = self.accessor(normals.astype(np.float32), "VEC3", FLOAT)
        if uv0 is not None:
            attrs["TEXCOORD_0"] = self.accessor(uv0.astype(np.float32), "VEC2", FLOAT)
        if uv1 is not None:
            attrs["TEXCOORD_1"] = self.accessor(uv1.astype(np.float32), "VEC2", FLOAT)
        if colors is not None:
            attrs["COLOR_0"] = self.accessor(colors.astype(np.uint8), "VEC4", UBYTE, normalized=True)
        if joints is not None:
            attrs["JOINTS_0"] = self.accessor(joints.astype(np.uint16), "VEC4", USHORT)
            attrs["WEIGHTS_0"] = self.accessor(weights.astype(np.float32), "VEC4", FLOAT)
        big = len(pos) > 65535
        ind = self.accessor(idx.astype(np.uint32 if big else np.uint16).reshape(-1, 1).reshape(-1),
                            "SCALAR", UINT if big else USHORT, ELEMENT_ARRAY_BUFFER)
        p = {"attributes": attrs, "indices": ind, "mode": 4}
        if material is not None:
            p["material"] = material
        return p

    def mesh(self, name: str, prims: list[dict]) -> int:
        self.j["meshes"].append({"name": name, "primitives": prims})
        return len(self.j["meshes"]) - 1

    def node(self, name: str, mesh: int | None = None, children=None, t=None, r=None, s=None, root=True,
             skin: int | None = None, extras=None) -> int:
        n = {"name": name}
        if mesh is not None:
            n["mesh"] = mesh
        if skin is not None:
            n["skin"] = skin
        if children:
            n["children"] = list(children)
        if t is not None:
            n["translation"] = [float(x) for x in t]
        if r is not None:
            n["rotation"] = [float(x) for x in r]
        if s is not None:
            n["scale"] = [float(x) for x in s]
        if extras:
            n["extras"] = extras
        self.j["nodes"].append(n)
        i = len(self.j["nodes"]) - 1
        if root:
            self.j["scenes"][0]["nodes"].append(i)
        return i

    def skin(self, joints: list[int], inverse_bind: np.ndarray, skeleton_root: int | None = None) -> int:
        self.j.setdefault("skins", [])
        s = {"joints": joints,
             "inverseBindMatrices": self.accessor(inverse_bind.astype(np.float32).reshape(-1, 16), "MAT4", FLOAT,
                                                  target=None)}
        if skeleton_root is not None:
            s["skeleton"] = skeleton_root
        self.j["skins"].append(s)
        return len(self.j["skins"]) - 1

    # ------------------------------------------------------------ salida
    def build(self) -> bytes:
        for k in ("materials", "meshes"):
            if not self.j[k]:
                del self.j[k]
        while len(self.bin) % 4:
            self.bin.append(0)
        self.j["buffers"][0]["byteLength"] = len(self.bin)
        js = json.dumps(self.j, separators=(",", ":")).encode()
        js += b" " * (-len(js) % 4)
        out = struct.pack("<III", 0x46546C67, 2, 12 + 8 + len(js) + 8 + len(self.bin))
        out += struct.pack("<II", len(js), 0x4E4F534A) + js
        out += struct.pack("<II", len(self.bin), 0x004E4942) + bytes(self.bin)
        return out
