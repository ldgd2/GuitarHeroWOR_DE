"""Rutas del proyecto y ajustes de la herramienta."""
from __future__ import annotations

import json
import sys
from dataclasses import dataclass, field
from pathlib import Path

TOOLS = Path(__file__).resolve().parent.parent
CONVERTER = TOOLS / "wtde2wor"
if str(CONVERTER) not in sys.path:
    sys.path.insert(0, str(CONVERTER))


@dataclass
class Workspace:
    root: Path                                   # GuitarHeroWOR_DE/
    game: Path = field(init=False)               # re-wor/
    assets: Path = field(init=False)             # re-wor/assets
    mods: Path = field(init=False)               # re-wor/mods
    config: Path = field(init=False)             # re-wor/re_wor_de.toml (orden/estado de mods)
    cache: Path = field(init=False)              # re-wor/mods/_cache
    keys: Path = field(init=False)               # tabla de claves FSB de WoR
    settings_path: Path = field(init=False)

    def __post_init__(self):
        self.game = self.root / "re-wor"
        self.assets = self.game / "assets"
        self.mods = self.game / "mods"
        self.config = self.game / "re_wor_de.toml"
        self.cache = self.mods / "_cache"
        self.keys = self.cache / "wor_fsb_keys.bin"
        self.settings_path = TOOLS / "wormod" / "settings.json"

    @classmethod
    def default(cls) -> "Workspace":
        return cls(TOOLS.parent)

    # ---------------------------------------------------------------- ajustes
    def settings(self) -> dict:
        try:
            return json.loads(self.settings_path.read_text(encoding="utf-8"))
        except (OSError, ValueError):
            return {}

    def save_settings(self, data: dict) -> None:
        self.settings_path.write_text(json.dumps(data, indent=1, ensure_ascii=False), encoding="utf-8")

    def song_sources(self) -> list[Path]:
        """Carpetas donde se buscan canciones custom de WTDE (por defecto GuitarHeroWOR_DE/song)."""
        src = self.settings().get("song_sources") or [str(self.root / "song")]
        return [Path(p) for p in src]

    def add_song_source(self, folder: Path) -> None:
        s = self.settings()
        src = s.get("song_sources") or [str(self.root / "song")]
        if str(folder) not in src:
            src.append(str(folder))
        s["song_sources"] = src
        self.save_settings(s)

    def remove_song_source(self, folder: Path) -> None:
        s = self.settings()
        src = [p for p in (s.get("song_sources") or [str(self.root / "song")]) if Path(p) != Path(folder)]
        s["song_sources"] = src
        self.save_settings(s)

    def check(self) -> list[str]:
        """Problemas que impiden trabajar (texto para mostrar)."""
        out = []
        if not (self.assets / "data").is_dir():
            out.append(f"No se encuentra el juego extraido en {self.assets}")
        if not self.keys.exists():   # se saca sola del default.xex de tu copia
            try:
                import sys
                sys.path.insert(0, str(TOOLS / "wtde2wor"))
                from extract_keys import ensure
                ensure(self.keys, self.assets / "default.xex")
            except Exception:  # noqa: BLE001
                pass
        if not self.keys.exists():
            out.append(f"Falta la tabla de claves de audio ({self.keys}); el audio de las canciones no se "
                       "podra convertir")
        return out
