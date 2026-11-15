// Vigia de la camara: punto de parada de hardware sobre la camara del escenario para saber que funcion del motor
// la escribe (ver cam_watch.cpp). Diagnostico de una vez por sesion; no cambia nada del juego.
#pragma once
#include <cstdint>

namespace re_wor::de::camwatch {
void ArmOnce(uint8_t* base, uint32_t cam);   // empieza a vigilar (solo la primera vez)
void Tick();                                 // cada frame logico: a los 3 s quita el vigia e informa
struct SelfWrite {                           // mientras exista, las escrituras de este hilo no cuentan
  SelfWrite();
  ~SelfWrite();
  bool prev_;
};
}  // namespace re_wor::de::camwatch
