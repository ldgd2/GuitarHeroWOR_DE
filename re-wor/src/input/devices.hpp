#pragma once
// =============================================================================
// devices - Perifericos del PC para la pantalla Controles: teclados fisicos (raw input), microfonos
// (WASAPI) y mandos. Cada teclado se guarda por su ruta de Windows, que no cambia entre arranques;
// su id es el mismo que SDL pone en cada tecla (KeyEvent::keyboard_id).
// =============================================================================

#include <cstdint>
#include <string>
#include <vector>

namespace re_wor::input {

struct KeyboardInfo {
  uint32_t id = 0;     // handle de raw input (= SDL_KeyboardID)
  std::string path;    // ruta del dispositivo
  std::string name;    // nombre para mostrar
};

struct MicInfo {
  std::string id;      // id del endpoint WASAPI
  std::string name;
  bool is_default = false;
};

struct PadInfo {
  std::string guid;
  std::string name;
  uint32_t ordinal = 0;
};

std::vector<KeyboardInfo> ListKeyboards();
std::vector<MicInfo> ListMics();
std::vector<PadInfo> ListPads();

// Modo escucha de mandos: BeginPadDetect toma nota de lo que ya esta pulsado; PollPadDetect devuelve el
// primer mando (no teclado) en el que se pulse un boton nuevo despues.
void BeginPadDetect();
bool PollPadDetect(PadInfo* out);

// Ruta del teclado con ese id ("" si no se conoce). Con cache: se puede llamar en cada tecla.
std::string KeyboardPathForId(uint32_t id);
// Nombre para mostrar de un teclado por su ruta
std::string KeyboardNameForPath(const std::string& path);

}  // namespace re_wor::input
