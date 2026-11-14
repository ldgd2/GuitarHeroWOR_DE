"""Modulos de contenido. El orden importa: el primero que reconoce una ruta se queda con ella."""
from modules.wtde_song import WtdeSongModule
from modules.wtde_assets import WtdeAssetModule
from modules.wor_native import WorNativeModule

MODULES = [WtdeSongModule(), WtdeAssetModule(), WorNativeModule()]


def detect(path):
    for m in MODULES:
        d = m.detect(path)
        if d is not None:
            return m, d
    return None, None
