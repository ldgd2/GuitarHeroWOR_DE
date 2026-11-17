"""Libreria estandar de wui, por categorias (cada una con sus especificaciones e implementacion)."""
from __future__ import annotations

from .core import specs as core
from .events import specs as events
from .game import specs as game
from .layers import specs as layers
from .math import specs as math
from .pad import specs as pad
from .screens import specs as screens
from .sound import specs as sound
from .time import specs as time

CATEGORIES = [core, math, time, pad, layers, screens, events, sound, game]

from .registry import registry  # noqa: E402

__all__ = ["CATEGORIES", "registry"]
