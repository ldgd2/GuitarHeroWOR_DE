// Opciones graficas "de PC" (pestana Graficos del panel F8 y [graphics] de re_wor_de.toml).
#pragma once

#include <cstdint>

#include "de/de_config.hpp"

namespace rex::ui {
class ImGuiDrawer;
}

namespace re_wor::de {

// Pasa las opciones de pantalla a las cvars del SDK (resolucion, ventana, frecuencia, reescalado, MSAA...).
// Casi todas son de arranque: se llama en OnPreSetup y, desde el panel, solo para las que cambian en caliente.
void ApplyDisplayOptions(const GraphicsConfig& g);
void ApplyLiveDisplayOptions(const GraphicsConfig& g);
// Opciones con las que arranco esta sesion (para avisar de lo que necesita reiniciar)
const GraphicsConfig& BootGraphics();

// Menu Options > Graphics del propio juego (qb/options_graphics.q). Cada opcion es una global entera
// re_gfx_v<N> que el menu cambia; aqui se traduce a [graphics], se guarda y se aplica lo que va en
// caliente. Hilo del juego, cada frame logico.
void SyncGameMenu(uint8_t* base);

// Contador de FPS en una esquina ([graphics] show_fps). Se crea una vez en OnCreateDialogs.
void CreateFpsCounter(rex::ui::ImGuiDrawer* drawer);

}  // namespace re_wor::de
