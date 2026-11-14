#include "app/re_wor_app.hpp"
#include <rex/rex_app.h>

REX_DEFINE_APP(re_wor, ReWorApp::Create)

#ifdef _WIN32
// Portatiles con graficos hibridos (Intel + NVIDIA/AMD): pedir al driver que
// ejecute el juego en la GPU DEDICADA. Sin esto Windows/Optimus suele usar la
// integrada y el 3D (personajes, escenarios) va muy lento.
extern "C" {
__declspec(dllexport) unsigned long NvOptimusEnablement = 0x00000001;
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}
#endif
