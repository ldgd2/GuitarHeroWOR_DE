// ESCUCHA DEL D3D DE LA 360 (render nativo, fases 2 y 3: docs/RENDER_NATIVO.md). Engancha las funciones de la
// libreria D3D del juego que escriben ordenes a la GPU (draws, rectangulos, resolve) y sabe que tramo del buffer de
// ordenes escribio cada llamada, aunque cambie de segmento. Los consumidores (sonda del mapa del dispositivo,
// captura de frames) reciben cada llamada con sus tramos y la copia de registros del dispositivo.
#pragma once

#include <cstdint>
#include <cstring>
#include <vector>

#include <stdlib.h>

namespace re_wor::de::d3dtap {

// copia de los registros de la GPU dentro del dispositivo (tools/d3d_device_map.py)
constexpr uint32_t kShadowOffset = 1152, kShadowBytes = 9712;
constexpr uint32_t kDirtyBytes = 48;   // dev+0..47: mascaras de sucio
constexpr uint32_t kCmdWritePtr = 48;  // dev+48: ultima palabra escrita en el buffer de ordenes

struct Span {   // palabras de ordenes en (begin, end], direcciones virtuales del juego
  uint32_t begin, end;
};

struct CallInfo {
  uint32_t func = 0;   // direccion de la funcion enganchada (0x822D18A0...)
  uint32_t dev = 0;
  uint32_t args[8] = {};   // r3..r10 al entrar
  const std::vector<Span>* spans = nullptr;
  bool spans_ok = false;   // tramos coherentes (si no, no hay que leerlos)
};

inline uint32_t Rd(const uint8_t* base, uint32_t addr) {
  uint32_t v;
  std::memcpy(&v, base + addr, 4);
  return _byteswap_ulong(v);
}

// memoria fisica de la GPU: base + 4 GB (ver rex/system/xmemory.h, TranslatePhysical)
inline const uint8_t* Phys(const uint8_t* base, uint32_t addr) { return base + 0x100000000ull + (addr & 0x1FFFFFFF); }

// recorrido de los paquetes PM4 de unos tramos
struct Pm4Visitor {
  virtual ~Pm4Visitor() = default;
  virtual void Reg(uint32_t reg, uint32_t value) {}
  // paquete tipo 3: opcode, direccion de su primera palabra de datos y numero de palabras
  virtual void Packet3(uint32_t op, uint32_t data, uint32_t count) {}
};
void Walk(const uint8_t* base, const std::vector<Span>& spans, Pm4Visitor& v);
// entrega la ultima llamada pendiente (cada llamada se entrega al siguiente evento; el swap llama a esto)
void FlushPending(const uint8_t* base);

}  // namespace re_wor::de::d3dtap

// consumidores (los llama d3d_tap.cpp)
namespace re_wor::de::devmap {
bool Enabled();
void OnCall(const uint8_t* base, const d3dtap::CallInfo& c);
}  // namespace re_wor::de::devmap

namespace re_wor::de::framecap {
bool Enabled();
void BeforeCall(const uint8_t* base, const d3dtap::CallInfo& c);   // aun sin tramos
void OnCall(const uint8_t* base, const d3dtap::CallInfo& c);
void OnSwap(const uint8_t* base);   // fin de frame (desde el gancho de VdSwap, sub_822E5E10)
}  // namespace re_wor::de::framecap
