// SONDA DEL MAPA DEL DISPOSITIVO D3D (fase 2 del render nativo, docs/RENDER_NATIVO.md). Diagnostico, apagado salvo
// con la variable de entorno REWOR_DEVMAP_PROBE=1.
//
// El D3D de la 360 guarda una copia de los registros de la GPU dentro del dispositivo (tabla en
// nativegfx_device_map.h, sacada por tools/d3d_device_map.py del codigo recompilado). Para comprobar esa tabla con el
// juego en marcha, d3d_tap pasa cada llamada que escribe ordenes con sus tramos de PM4; por cada registro escrito se
// compara su valor con el de la tabla (dev + desplazamiento). Cada 10 s se escribe un resumen en re_wor_trace.log,
// con la cobertura de cada bloque (cuantos de sus registros se han visto escribir).
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <sstream>
#include <vector>

#include <windows.h>

#include "core/crash_handler.hpp"
#include "de/d3d_tap.hpp"
#include "de/nativegfx_device_map.h"

namespace re_wor::de::devmap {
namespace {

using Clock = std::chrono::steady_clock;
using d3dtap::Rd;
constexpr uint32_t kRegs = 0x5000;

struct Stat {
  uint64_t ok = 0, bad = 0;
  uint32_t dev_value = 0, pm4_value = 0;   // primer desacuerdo
};

std::atomic<int> g_enabled{-1};
std::atomic_flag g_lock = ATOMIC_FLAG_INIT;
int32_t g_offset_of[kRegs];
Stat g_stat[kRegs];
uint64_t g_unmapped[kRegs];
uint64_t g_calls = 0, g_skipped = 0, g_suspicious = 0;
uint32_t g_device = 0;
Clock::time_point g_last_report;

struct Checker : d3dtap::Pm4Visitor {
  const uint8_t* base;
  uint32_t dev;
  uint32_t suspicious = 0;   // registros que una llamada de dibujo no toca (< 0x2000): el recorrido se perdio
  void Reg(uint32_t reg, uint32_t v) override {
    if (reg >= kRegs) return;
    if (reg < 0x2000) ++suspicious;
    const int32_t off = g_offset_of[reg];
    if (off < 0) {
      ++g_unmapped[reg];
      return;
    }
    const uint32_t d = Rd(base, dev + uint32_t(off));
    Stat& s = g_stat[reg];
    if (d == v) {
      ++s.ok;
    } else {
      if (!s.bad) s.dev_value = d, s.pm4_value = v;
      ++s.bad;
    }
  }
};

void Report() {
  std::ostringstream o;
  uint64_t ok = 0, bad = 0, unmapped = 0;
  std::vector<uint32_t> bad_regs, unmapped_regs;
  for (uint32_t r = 0; r < kRegs; ++r) {
    ok += g_stat[r].ok;
    bad += g_stat[r].bad;
    unmapped += g_unmapped[r];
    if (g_stat[r].bad) bad_regs.push_back(r);
    if (g_unmapped[r]) unmapped_regs.push_back(r);
  }
  o << "[DevMap] dispositivo @" << std::hex << g_device << std::dec << ": " << g_calls << " llamadas (" << g_skipped
    << " sin comprobar, " << g_suspicious << " con paquetes no entendidos), escrituras de registro: " << ok
    << " coinciden con la copia, " << bad << " no, " << unmapped << " fuera del mapa";
  for (const auto& b : nativegfx::kDeviceRegBlocks) {
    uint32_t seen = 0;
    for (uint32_t i = 0; i < b.count; ++i) seen += (g_stat[b.reg + i].ok + g_stat[b.reg + i].bad) ? 1 : 0;
    o << "\n[DevMap]   bloque 0x" << std::hex << b.reg << std::dec << " (dev+" << b.offset << "): " << seen << " de "
      << b.count << " registros vistos";
  }
  for (uint32_t r : bad_regs)
    o << "\n[DevMap]   distinto 0x" << std::hex << r << ": " << std::dec << g_stat[r].bad << " de "
      << (g_stat[r].ok + g_stat[r].bad) << " (copia 0x" << std::hex << g_stat[r].dev_value << ", PM4 0x"
      << g_stat[r].pm4_value << ")" << std::dec;
  for (uint32_t r : unmapped_regs)
    o << "\n[DevMap]   fuera del mapa 0x" << std::hex << r << std::dec << ": " << g_unmapped[r];
  TRACE(o.str());
}

}  // namespace

bool Enabled() {
  int e = g_enabled.load(std::memory_order_relaxed);
  if (e < 0) {
    char v[4] = {};
    e = (GetEnvironmentVariableA("REWOR_DEVMAP_PROBE", v, sizeof(v)) > 0 && v[0] == '1') ? 1 : 0;
    if (e) {
      std::fill(std::begin(g_offset_of), std::end(g_offset_of), -1);
      for (const auto& b : nativegfx::kDeviceRegBlocks)
        for (uint32_t i = 0; i < b.count; ++i) g_offset_of[b.reg + i] = int32_t(b.offset + 4 * i);
      g_last_report = Clock::now();
      TRACE("[DevMap] sonda del mapa del dispositivo activa (REWOR_DEVMAP_PROBE=1)");
    }
    g_enabled.store(e, std::memory_order_relaxed);
  }
  return e != 0;
}

void OnCall(const uint8_t* base, const d3dtap::CallInfo& c) {
  while (g_lock.test_and_set(std::memory_order_acquire)) {
  }
  g_device = c.dev;
  ++g_calls;
  if (!c.spans_ok) {
    ++g_skipped;
  } else {
    Checker ck;
    ck.base = base;
    ck.dev = c.dev;
    d3dtap::Walk(base, *c.spans, ck);
    if (ck.suspicious) ++g_suspicious;
  }
  const auto now = Clock::now();
  if (now - g_last_report >= std::chrono::seconds(10)) {
    g_last_report = now;
    Report();
  }
  g_lock.clear(std::memory_order_release);
}

}  // namespace re_wor::de::devmap
