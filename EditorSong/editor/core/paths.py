"""Rutas del repo. El editor reutiliza el lector/escritor de formatos de tools/wtde2wor y el validador de
tools/wormod (no duplica el conocimiento de los formatos)."""
from __future__ import annotations

import sys
from pathlib import Path

EDITOR = Path(__file__).resolve().parents[2]       # .../GuitarHeroWOR_DE/EditorSong
ROOT = EDITOR.parent                                       # .../GuitarHeroWOR_DE
TOOLS_WT = ROOT / "tools" / "wtde2wor"
TOOLS_MOD = ROOT / "tools" / "wormod"
GAME = ROOT / "re-wor"
ASSETS = GAME / "assets" / "data"
MODS = GAME / "mods"
PROJECTS = EDITOR / "projects"
BACKUPS = EDITOR / "backups"
CACHE = EDITOR / "cache"

for p in (TOOLS_WT, TOOLS_MOD):
    if str(p) not in sys.path:
        sys.path.insert(0, str(p))

for p in (PROJECTS, BACKUPS, CACHE):
    p.mkdir(parents=True, exist_ok=True)
