"""Nombres legibles de las QBKeys (CRC32) que aparecen en las canciones. Solo para mostrar: el editor
guarda siempre el hash, asi que una clave desconocida se conserva intacta."""
from __future__ import annotations

import zlib


def qbkey(s: str) -> int:
    return (~zlib.crc32(s.lower().encode())) & 0xFFFFFFFF


WORDS = """
Band_PlayClip Band_PlayLoop Band_PlayFacialAnim Band_ChangeFacialAnims Band_PlayIdle Band_ForceAllToIdle
Band_ShowMic Band_HideMic Band_ShowMic_Stand Band_HideMic_stand Band_ShowMicStand Band_HideMicStand
Band_ShowMic_microphone Band_HideMic_microphone Band_SetAnimTempo Band_ClearAnimTempo
Band_EnableAutoChords Band_DisableAutoChords Band_EnableAutoFret Band_DisableAutoFret
Band_EnableAutoStrums Band_DisableAutoStrums Band_SetIKChainTarget Band_MoveToNode
Band_PlayRockinFacialAnim Band_PlaySimpleAnim CameraCutsEffect_FOVPulse SetSongHandCamParams
Band_SetStrumStyle Band_EnableAutoFacials Band_DisableAutoFacials
scr time params clip startframe endframe timefactor name anim anims characters cameras startnode
slot ik_targetl ik_targetr strum fret chord male female guitarist bassist vocalist drummer
guitar bass drum vocals slave true false none weight pak anim_set finger_anims fret_anims
strum_anims facial_anims startnodes arms blendduration tempo node member
""".split()

NAMES: dict[int, str] = {qbkey(w): w for w in WORDS}
NAMES.update({0x65C1ECD3: "clip_extra_65c1", 0x1FF9C3F6: "clip_extra_1ff9", 0xD91AB002: "clip_extra_d91a",
              0x3F52D9C3: "loop_param_3f52"})

ROLES = ("guitarist", "bassist", "vocalist", "drummer")
ROLE_KEYS = {qbkey(r): r for r in ROLES}


_SDK: dict[int, str] | None = None


def sdk_keys() -> dict[int, str]:
    """Diccionario QBKey -> texto de WoR del Guitar Hero SDK (tools/ghsdk/.../ghwor_keys.txt), si esta."""
    global _SDK
    if _SDK is None:
        import re
        from . import paths
        _SDK = {}
        f = paths.ROOT / "tools" / "ghsdk" / "SDKCode" / "Checksums" / "ghwor_keys.txt"
        if f.exists():
            for line in f.read_text(encoding="utf-8", errors="ignore").splitlines():
                m = re.match(r"\s*0x([0-9a-fA-F]+)\s+(\S.*)$", line)
                if m:
                    _SDK[int(m.group(1), 16)] = m.group(2).strip()
    return _SDK


def short(text: str) -> str:
    """Ruta de archivo -> nombre corto (c:/gh6_burn/data/anims/.../s_adam_075_11.ska.xen -> s_adam_075_11)."""
    base = text.replace("\\", "/").rsplit("/", 1)[-1]
    for ext in (".xen", ".ska", ".qb", ".pak"):
        if base.lower().endswith(ext):
            base = base[: -len(ext)]
    return base


def name_of(k: int) -> str:
    if k in NAMES:
        return NAMES[k]
    t = sdk_keys().get(k)
    return short(t) if t else f"0x{k:08x}"
