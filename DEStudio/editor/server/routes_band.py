"""Rutas de la banda: catalogo de piezas, personaje .glb y biblioteca de animaciones."""
from __future__ import annotations

import json

from ..band import anims, catalog
from ..band.character import character_glb
from .http import Binary, router


@router.get("/api/band/catalog")
def band_catalog(req):
    return catalog.catalog()


@router.get("/api/band/character.glb")
def band_character(req):
    spec = json.loads(req.arg("spec"))
    return Binary(character_glb(spec), "model/gltf-binary", cache=True)


@router.get("/api/band/anims")
def band_anims(req):
    return anims.library()


@router.get("/api/band/anim")
def band_anim(req):
    return anims.anim_data(req.arg("id"))


@router.get("/api/band/hands")
def band_hands(req):
    """Poses de manos del juego (pak/perm_anims): gh_rocker_male_fret_NN_d, gh_male_g_strum_base..."""
    return anims.hand_anims()
