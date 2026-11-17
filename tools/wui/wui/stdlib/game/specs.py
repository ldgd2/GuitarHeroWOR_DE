"""El juego: variables globales, scripts QB, datos de los jugadores y de la banda, menus del juego."""
from __future__ import annotations


CAT = "juego"

PLAYER_FIELDS = ["score", "notes_hit", "total_notes", "best_run", "current_run", "stars", "multiplier",
                 "star_power", "star_power_used", "difficulty", "part", "controller", "name"]
BAND_FIELDS = ["score", "current_mult", "stars", "projected_song_stars", "star_power"]


from . import impl as I  # noqa: E402  (despues de las constantes: impl las usa)

def register(r) -> None:
    r.categories[CAT] = "Variables y scripts del juego (QB), datos de jugadores y banda, menus del juego"
    r.namespace("game", "El juego que esta debajo (Warriors of Rock con re-wor)")
    f = lambda name, *a, **k: r.fn("game." + name, *a, cat=CAT, **k)  # noqa: E731
    f("get", "name, default=None", "Lee una variable global del juego (QB).", I.get, ret="cualquiera",
      example='modo = game.get("current_num_players")')
    f("set", "name, value", "Cambia una variable global del juego (entero, decimal o texto).", I.set_)
    f("call", "script, **params", "Ejecuta un script del juego con sus parametros.", I.call,
      example='game.call("ui_event", event="menu_back")')
    f("player", "field, player=1", "Dato de un jugador: " + ", ".join(PLAYER_FIELDS) + ".", I.player,
      ret="cualquiera", example='layer("puntos").text = format_number(game.player("score"))')
    f("band", "field", "Dato de la banda: " + ", ".join(BAND_FIELDS) + ".", I.band, ret="cualquiera")
    f("open_menu", "state", "Abre un menu del juego (un UIstate, p. ej. \"UIstate_options\").", I.open_menu)
    f("in_song", "", "Si se esta tocando una cancion.", I.in_song, ret="bool")
    f("paused", "", "Si el juego esta en pausa.", I.paused, ret="bool")
    f("language", "", "Idioma del juego (en, es, fr, de, it).", I.language, ret="texto")
