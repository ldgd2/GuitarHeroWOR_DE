"""Contrato entre la maquina de wui y quien dibuja la interfaz (el anfitrion): re-wor en el juego, el editor en
el navegador o el simulador de las pruebas. La maquina decide QUE pasa; el anfitrion lo muestra y da el estado
del mundo (capas, pantallas, juego). Todo tiene una version por defecto que no hace nada, asi un anfitrion
implementa solo lo que soporta."""
from __future__ import annotations

from typing import Any


class Host:
    # ------------------------------------------------------------------ capas
    def layer_names(self) -> list[str]:
        return []

    def layer_get(self, name: str, prop: str) -> Any:
        return None

    def layer_set(self, name: str, prop: str, value: Any) -> None:
        pass

    def layer_design(self, name: str) -> dict:
        """propiedades de la capa tal como esta en el diseno (para reset())"""
        return {}

    def layer_contains(self, name: str, x: float, y: float) -> bool:
        return False

    def layer_at(self, x: float, y: float) -> str | None:
        return None

    # ------------------------------------------------------------------ pantallas y foco
    def screen_names(self) -> list[str]:
        return []

    def show_screen(self, name: str, transition: str, previous: str | None) -> None:
        pass

    def close(self) -> None:
        pass

    def set_focus(self, name: str | None) -> None:
        pass

    def move_focus(self, direction: str) -> None:
        pass

    def lock_focus(self, locked: bool) -> None:
        pass

    # ------------------------------------------------------------------ juego
    def game_get(self, name: str) -> Any:
        return None

    def game_set(self, name: str, value: Any) -> None:
        pass

    def game_call(self, script: str, params: dict) -> Any:
        return None

    def player_info(self, player: int, field: str) -> Any:
        return None

    def band_info(self, field: str) -> Any:
        return None

    def game_state(self, what: str) -> Any:
        return None

    # ------------------------------------------------------------------ otros
    def play_sound(self, name: str, volume: float) -> None:
        pass

    def vibrate(self, player: int, strength: float, seconds: float) -> None:
        pass

    def log(self, level: str, message: str) -> None:
        pass
