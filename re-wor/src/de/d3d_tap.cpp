// ESCUCHA DEL D3D DE LA 360: ver d3d_tap.hpp.
//
// Funciones enganchadas (todas con r3 = dispositivo; ver docs/RENDER_NATIVO.md, fase 0):
//   draws con DRAW_INDX: sub_822D18A0, sub_822D1D80, sub_822D2170, sub_822D6180
//   draws con indices en el paquete (DRAW_INDX_2: clear, rectangulos, copias): sub_822D13C0, sub_822D8690,
//     sub_822D9F90, sub_822DA0E8, sub_822E9220, sub_822EF320
//   resolve (copia de EDRAM a textura): sub_822E4538
// Algunas se llaman entre si (el resolve dibuja su rectangulo): solo cuenta la llamada exterior.
// Cambio de segmento del buffer de ordenes: sub_822DC700 entra con el puntero viejo en dev+48 y sale con el nuevo.
// Todo apagado (un salto) si ningun consumidor esta activo.
#include "de/d3d_tap.hpp"
#include "nativegfx/device/device_tracker.hpp"

#include "generated/re_wor_pch.h"

namespace re_wor::de::d3dtap {
namespace {

thread_local int t_depth = 0;   // llamadas enganchadas anidadas
thread_local uint32_t t_span_begin = 0;
thread_local std::vector<Span> t_spans;

// La llamada se entrega al siguiente evento (otra llamada, cambio de segmento fuera de llamada, swap), con su ultimo
// tramo alargado hasta el puntero de ese momento: sub_822D18A0 deja el paquete de draw y los vertices pendientes
// (dev+13652) y quien llama los copia y confirma despues (dev+48 = dev+13652, sub_822D1890). Asi tambien entra lo que
// escriban funciones no enganchadas entre dos llamadas: la captura es el flujo de ordenes completo.
struct Pending {
  bool has = false;
  CallInfo c;
  std::vector<Span> spans;
};
thread_local Pending t_pend;

bool AnyConsumer() {
  return devmap::Enabled() || framecap::Enabled() || nativegfx::DeviceTracker::Instance().IsEnabled();
}

void Dispatch(const uint8_t* base, Pending& p) {
  p.c.spans = &p.spans;
  p.c.spans_ok = true;
  for (const Span& sp : p.spans) p.c.spans_ok = p.c.spans_ok && sp.end >= sp.begin && sp.end - sp.begin < (1u << 20);
  if (devmap::Enabled()) devmap::OnCall(base, p.c);
  if (framecap::Enabled()) framecap::OnCall(base, p.c);
  if (nativegfx::DeviceTracker::Instance().IsEnabled()) {
    nativegfx::DeviceTracker::Instance().OnD3DCall(base, p.c.func, p.c.dev, p.c.args);
  }
  p.has = false;
}

}  // namespace

void Walk(const uint8_t* base, const std::vector<Span>& spans, Pm4Visitor& v) {
  for (const Span& sp : spans) {
    uint32_t p = sp.begin + 4;
    while (p <= sp.end) {
      const uint32_t h = Rd(base, p);
      p += 4;
      switch (h >> 30) {
        case 0: {   // registros seguidos (o uno repetido)
          const uint32_t n = ((h >> 16) & 0x3FFF) + 1, r = h & 0x7FFF;
          const bool one = (h & 0x8000) != 0;
          for (uint32_t i = 0; i < n && p <= sp.end; ++i, p += 4) v.Reg(one ? r : r + i, Rd(base, p));
        } break;
        case 1:   // dos registros sueltos
          if (p + 4 <= sp.end) {
            v.Reg(h & 0x7FF, Rd(base, p));
            v.Reg((h >> 11) & 0x7FF, Rd(base, p + 4));
          }
          p += 8;
          break;
        case 2:   // relleno
          break;
        default: {
          const uint32_t n = ((h >> 16) & 0x3FFF) + 1, op = (h >> 8) & 0x7F;
          if (p + 4 * (n - 1) <= sp.end) {
            if (op == 0x2D && n >= 2) {   // SET_CONSTANT
              const uint32_t d0 = Rd(base, p), type = (d0 >> 16) & 0xFF, idx = d0 & 0x7FF;
              const uint32_t first = type == 0   ? 0x4000
                                     : type == 1 ? 0x4800
                                     : type == 2 ? 0x4900
                                     : type == 3 ? 0x4908
                                                 : 0x2000;
              for (uint32_t i = 1; i < n; ++i) v.Reg(first + idx + i - 1, Rd(base, p + 4 * i));
            }
            v.Packet3(op, p, n);
          }
          p += 4 * n;
        } break;
      }
    }
  }
}

void FlushPending(const uint8_t* base) {
  if (!t_pend.has) return;
  Span& last = t_pend.spans.back();
  const uint32_t now = Rd(base, t_pend.c.dev + kCmdWritePtr);
  if (now >= last.end) last.end = now;
  Dispatch(base, t_pend);
}

bool Enter(PPCContext& ctx, const uint8_t* base, CallInfo& c, uint32_t func) {
  if (!AnyConsumer() || t_depth++ > 0) return false;
  FlushPending(base);
  c.func = func;
  c.dev = ctx.r3.u32;
  const uint32_t r[8] = {ctx.r3.u32, ctx.r4.u32, ctx.r5.u32, ctx.r6.u32,
                         ctx.r7.u32, ctx.r8.u32, ctx.r9.u32, ctx.r10.u32};
  std::memcpy(c.args, r, sizeof(r));
  t_spans.clear();
  t_span_begin = Rd(base, c.dev + kCmdWritePtr);
  if (framecap::Enabled()) framecap::BeforeCall(base, c);
  return true;
}

void Leave(const uint8_t* base, CallInfo& c, bool outer) {
  if (!outer) {
    if (t_depth > 0) --t_depth;
    return;
  }
  t_depth = 0;
  t_spans.push_back({t_span_begin, Rd(base, c.dev + kCmdWritePtr)});
  t_pend.c = c;
  t_pend.spans = t_spans;
  t_pend.has = true;
}

void SegmentSwitch(const uint8_t* base, uint32_t dev, bool begin) {
  const uint32_t ptr = Rd(base, dev + kCmdWritePtr);
  if (t_depth > 0) {
    if (begin) t_spans.push_back({t_span_begin, ptr});
    else t_span_begin = ptr;
  } else if (t_pend.has) {   // entre llamadas: sigue el tramo de la pendiente
    if (begin) {
      if (ptr >= t_pend.spans.back().end) t_pend.spans.back().end = ptr;
    } else {
      t_pend.spans.push_back({ptr, ptr});
    }
  }
}

}  // namespace re_wor::de::d3dtap

extern "C" void __imp__sub_822DC700(PPCContext& __restrict ctx, uint8_t* base);
extern "C" REX_FUNC(sub_822DC700) {
  const uint32_t dev = ctx.r3.u32;
  re_wor::de::d3dtap::SegmentSwitch(base, dev, true);
  __imp__sub_822DC700(ctx, base);
  re_wor::de::d3dtap::SegmentSwitch(base, dev, false);
}

#define REWOR_D3DTAP(addr)                                                        \
  extern "C" void __imp__sub_##addr(PPCContext& __restrict ctx, uint8_t* base);   \
  extern "C" REX_FUNC(sub_##addr) {                                               \
    re_wor::de::d3dtap::CallInfo c;                                               \
    const bool outer = re_wor::de::d3dtap::Enter(ctx, base, c, 0x##addr);         \
    __imp__sub_##addr(ctx, base);                                                 \
    if (outer || re_wor::de::d3dtap::Active()) re_wor::de::d3dtap::Leave(base, c, outer); \
  }

namespace re_wor::de::d3dtap {
// hay llamada enganchada en curso en este hilo (para cerrar las anidadas)
bool Active() { return t_depth > 0; }
}  // namespace re_wor::de::d3dtap

REWOR_D3DTAP(822D18A0)
REWOR_D3DTAP(822D1D80)
REWOR_D3DTAP(822D2170)
REWOR_D3DTAP(822D6180)
REWOR_D3DTAP(822D13C0)
REWOR_D3DTAP(822D8690)
REWOR_D3DTAP(822D9F90)
REWOR_D3DTAP(822DA0E8)
REWOR_D3DTAP(822E9220)
REWOR_D3DTAP(822EF320)
REWOR_D3DTAP(822E4538)
