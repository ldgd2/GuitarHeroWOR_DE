"""Esqueleto .ske de WoR (128 huesos para musicos; global_skeletons.pak y cas_*).

Cabecera (big-endian): u16 1, u16 0x30, u32 (n huesos en el byte bajo), 8 bytes, y 8 offsets absolutos:
  nombres (QBKey), padres (QBKey), espejo (QBKey), indice espejo (i32), tipo (u8),
  matrices (4x4 f32, inversa de la pose de enlace), posiciones locales (vec4), rotaciones locales (x,y,z,w)
"""
from __future__ import annotations

import struct
from dataclasses import dataclass

import numpy as np

BONE_NAMES = """Control_Root Bone_Pelvis Bone_Stomach_Lower Bone_Stomach_Upper Bone_Chest Bone_Collar_L
Bone_Forearm_L Bone_Palm_L Bone_Collar_R Bone_Shoulder_L Bone_Shoulder_R Bone_Thigh_L Bone_Thigh_R Bone_Knee_R
Bone_Knee_L Bone_Ankle_R Bone_Ankle_L Bone_Toe_L Bone_Toe_R Bone_Bicep_R Bone_Bicep_L Bone_Fingers_Base_L
Bone_Fingers_Base_R Bone_Fingers_Tip_L Bone_Fingers_Tip_R Bone_Forefinger_Base_L Bone_Forefinger_Base_R
Bone_Forefinger_Tip_L Bone_Forefinger_Tip_R Bone_Thumb_L Bone_Thumb_R Bone_Wrist_L Bone_Wrist_R Bone_Neck
Bone_Head Bone_Jaw Bone_Nostrils Bone_Twist_Wrist_L Bone_Twist_Wrist_R Bone_Hand_Middle_Base_L
Bone_Hand_Middle_Base_R Bone_Hand_Middle_Mid_L Bone_Hand_Middle_Mid_R Bone_Hand_Index_Base_L
Bone_Hand_Index_Base_R Bone_Hand_Index_Mid_L Bone_Hand_Index_Mid_R Bone_Hand_Thumb_Base_L Bone_Hand_Thumb_Base_R
Bone_ACC_Head_01 Bone_ACC_Head_02 Bone_ACC_Head_03 Bone_ACC_Head_04 Bone_ACC_Head_05 Bone_ACC_Head_06
Bone_ACC_Legs_01 Bone_ACC_Legs_02 Bone_ACC_Legs_03 Bone_ACC_Legs_04 Bone_ACC_Legs_05 Bone_ACC_Legs_06
Bone_ACC_Shirt_Back Bone_ACC_Shirt_R Bone_ACC_Shirt_L Bone_ACC_Misc Bone_Tongue Bone_Twist_Bicep_Top_L
Bone_Twist_Bicep_Top_R Bone_Forearm_R Bone_Palm_R Bone_Twist_Bicep_Mid_L Bone_Twist_Bicep_Mid_R
Bone_ACC_TSleeve_L Bone_ACC_TSleeve_R Bone_Head_Top_Scale Bone_PonyTail_1 Bone_Nose_Scale Bone_Chin_Scale
Bone_Bicep_Twist_Mid_L Bone_Bicep_Twist_Mid_R Bone_Bicep_Twist_Top_L Bone_Bicep_Twist_Top_R Cloth_Breast
Cloth_Shirt_L Cloth_Shirt_R Cloth_Shirt_C""".split()


@dataclass
class Skeleton:
    names: list[int]
    parents: list[int]            # indice del padre o -1
    pos: np.ndarray               # (n, 3) locales
    rot: np.ndarray               # (n, 4) locales x, y, z, w
    inv_bind: np.ndarray          # (n, 4, 4) tal como vienen en el archivo

    def labels(self) -> list[str]:
        from ..core.names import qbkey
        known = {qbkey(n): n for n in BONE_NAMES}
        return [known.get(k, f"bone_{i:03d}_{k:08x}") for i, k in enumerate(self.names)]


def read_ske(d: bytes) -> Skeleton:
    count = struct.unpack_from(">I", d, 4)[0] & 0xFF
    o_names, o_par, _o_flip, _o_flipi, _o_types, o_mat, o_vec, o_quat = struct.unpack_from(">8I", d, 0x10)
    names = list(struct.unpack_from(f">{count}I", d, o_names))
    par_keys = list(struct.unpack_from(f">{count}I", d, o_par))
    index = {k: i for i, k in enumerate(names)}
    parents = [index.get(k, -1) if k != names[i] else -1 for i, k in enumerate(par_keys)]
    pos = np.frombuffer(d, ">f4", count * 4, o_vec).reshape(count, 4)[:, :3].astype(np.float32)
    rot = np.frombuffer(d, ">f4", count * 4, o_quat).reshape(count, 4).astype(np.float32)
    mats = np.frombuffer(d, ">f4", count * 16, o_mat).reshape(count, 4, 4).astype(np.float32)
    return Skeleton(names, parents, pos, rot, mats)


def quat_to_mat(q) -> np.ndarray:
    x, y, z, w = q
    return np.array([[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
                     [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
                     [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]], np.float32)


def world_bind(sk: Skeleton) -> np.ndarray:
    """Matrices de mundo (columna, 4x4) de la pose de enlace a partir de las posiciones/rotaciones locales."""
    n = len(sk.names)
    out = np.zeros((n, 4, 4), np.float32)
    for i in range(n):
        m = np.eye(4, dtype=np.float32)
        m[:3, :3] = quat_to_mat(sk.rot[i])
        m[:3, 3] = sk.pos[i]
        p = sk.parents[i]
        out[i] = out[p] @ m if 0 <= p < i else m
    return out
