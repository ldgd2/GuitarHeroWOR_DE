// Opciones graficas "de PC" (pestana Graficos del panel F8 y [graphics] de re_wor_de.toml).
#pragma once

#include <cstdint>
#include <string>

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
// Frecuencia elegida (Frame Rate). Con mas de 60 el motor va sin candado y re-wor lo limita a esto.
int FrameCapHz();
// Resolucion / modo de pantalla al momento, con 10 s para confirmar (Enter / A) o se vuelve a lo anterior
void RequestDisplayChange(const std::string& prev_res, int prev_mode, const std::string& res, int mode);
bool DisplayConfirmPending();
void TickDisplayConfirm(uint8_t* base);   // cada frame logico
bool NeedsRestart(const GraphicsConfig& g);   // hay cambios que solo se aplican al reiniciar
// Medidas del plugin de GPU (una vez por segundo): apunta en re_wor_trace.log los tirones con su causa
void PerfTick();

// Contador de FPS en una esquina ([graphics] show_fps). Se crea una vez en OnCreateDialogs.
void CreateFpsCounter(rex::ui::ImGuiDrawer* drawer);

}  // namespace re_wor::de
