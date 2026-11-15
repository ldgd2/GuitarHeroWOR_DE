// =============================================================================
// native_fx.cpp - Lo que hace el editor de canciones (EDFX), hecho por el PROPIO MOTOR de WoR.
//
// 1) Post-proceso nativo (ScreenFX) sobre el viewport del escenario (bg_viewport): se aplica ANTES de dibujar
//    el traste, las notas y el HUD, asi que solo afecta a la actuacion (escenario, banda, publico, fondo).
//    Tipos registrados por el motor (RegisterScreenFX 821DAC48): bright_Sat, Vignette, dof3, bloom2, noise2,
//    MotionBlur, fisheye, godray, convolution, inkblotch, projection, ToneMap, ambientocclusion, UberScreenFX,
//    AudioVisualize. Lectores de parametros: 825293F0 (bright_Sat), 8252B4D8 (Vignette), 8252BA48 (dof3),
//    8252A568 (bloom2), 8252A9E8 (noise2), 8252B098 (fisheye), 8252C878 (godray), 8252DBE8 (MotionBlur),
//    8252EA90 (convolution: kernel = [ancho, alto, valores...]).
// 2) Franjas de cine nativas: se recorta el rectangulo del viewport del escenario (el traste va en otro).
// 3) Objetos 3D NATIVOS, creados con los CFuncs del motor como hacen sus scripts:
//      CreateFromStructure 825012B8  -> GameObject (focos con haz volumetrico y modelos del juego)
//      Kill 82502BA0 (name=)         -> lo quita
//      CreateParticleSystem 821DB160 -> particulas del juego (params_Script = estructura global gp_fx_*)
//      DestroyParticlesByGroupID 824C05B8
//    Solo con recursos que el juego tiene en memoria (global.pak: focos, siluetas, texturas de particulas;
//    o modelos del escenario cargado), asi que el motor no tiene que cargar nada nuevo.
//
// Los parametros son un Script::CStruct construido en memoria del juego, igual que hace su codigo:
//   CStruct: +0 0, +4 primer componente
//   componente (16 bytes): +2 tipo, +4 nombre, +8 valor, +12 siguiente
//   tipos: 1 int, 2 float, 3 cadena (puntero), 6 vector (puntero a {0,x,y,z}), 12 array (puntero a
//   {+2 tipo, +4 n, +8 datos | valor si n==1}), 13 checksum, 26 referencia a un simbolo global.
// Todas las llamadas al motor van protegidas: si algo falla, se apaga esa parte y el juego sigue.
// =============================================================================
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <map>
#include <string>
#include <vector>

#include "core/crash_handler.hpp"
#include "de/edfx.hpp"
#include "de/render_sandbox.hpp"
#include "generated/re_wor_pch.h"

extern "C" void sub_825090D0(PPCContext& __restrict ctx, uint8_t* base);  // ScreenFX_AddFXInstance
extern "C" void sub_82509178(PPCContext& __restrict ctx, uint8_t* base);  // ScreenFX_UpdateFXInstanceParams
extern "C" void sub_82509360(PPCContext& __restrict ctx, uint8_t* base);  // ScreenFX_FxInstanceExists
extern "C" void sub_825012B8(PPCContext& __restrict ctx, uint8_t* base);  // CreateFromStructure
extern "C" void sub_82502BA0(PPCContext& __restrict ctx, uint8_t* base);  // Kill
extern "C" void sub_821DB160(PPCContext& __restrict ctx, uint8_t* base);  // CreateParticleSystem
extern "C" void sub_824C05B8(PPCContext& __restrict ctx, uint8_t* base);  // DestroyParticlesByGroupID
extern "C" void sub_82392380(PPCContext& __restrict ctx, uint8_t* base);  // reserva en un monton del motor
extern "C" void sub_8251BEC0(PPCContext& __restrict ctx, uint8_t* base);  // CreateViewport {id, style, priority}
extern "C" void sub_8251BF98(PPCContext& __restrict ctx, uint8_t* base);  // DestroyViewport {id}

namespace re_wor::de::nativefx {
namespace {

using GuestFn = void (*)(PPCContext&, uint8_t*);

// QBKey (CRC32 sin inversion final, en minusculas, '/' -> '\')
uint32_t QbKey(const std::string& s) {
  uint32_t c = 0xFFFFFFFFu;
  for (unsigned char ch : s) {
    if (ch >= 'A' && ch <= 'Z') ch = static_cast<unsigned char>(ch + 32);
    if (ch == '/') ch = '\\';
    c ^= ch;
    for (int k = 0; k < 8; ++k) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
  }
  return c;
}

constexpr uint32_t k_viewport = 0x9FD29151, k_bg_viewport = 0x071F4183;
constexpr uint32_t k_name = 0xA1DC81F9, k_scefName = 0x33705D83, k_type = 0x7321A8D6, k_on = 0xF649D637,
                   k_time = 0x906B67BA;
constexpr uint32_t k_bright_Sat = 0x90258C6A, k_Vignette = 0xF4B4A9E1, k_dof3 = 0xA503619D, k_bloom2 = 0x6F894563,
                   k_noise2 = 0xA7657008, k_MotionBlur = 0x6E8324AC, k_fisheye = 0x68D71C9C, k_godray = 0xF6A0CE36,
                   k_convolution = 0x5BF24337;
constexpr uint32_t k_target = 0xB990D003, k_saturation = 0xD5D55277, k_brightness = 0x2689291C,
                   k_contrast = 0x5CB2C5F7;
constexpr uint32_t k_mix[3][3] = {{0x23CF1F1F, 0x4E12FBF4, 0x3E780F7B},
                                  {0x49C31CB5, 0x241EF85E, 0x54740CD1},
                                  {0x4D9014F6, 0x204DF01D, 0x50270492}};
constexpr uint32_t k_inner_radius = 0x5BFFE255, k_outer_radius = 0x0999151B, k_AspectCorrection = 0x3ADF9426;
constexpr uint32_t k_strength = 0x29461E27, k_nearblurbegin = 0x6645454E, k_nearblurend = 0x68DF40F5,
                   k_farblurbegin = 0x916FC1BD, k_farblurend = 0x09479F52;

void Put32(uint8_t* base, uint32_t a, uint32_t v) {
  base[a] = uint8_t(v >> 24); base[a + 1] = uint8_t(v >> 16); base[a + 2] = uint8_t(v >> 8); base[a + 3] = uint8_t(v);
}
uint32_t Get32(const uint8_t* base, uint32_t a) {
  return (uint32_t(base[a]) << 24) | (uint32_t(base[a + 1]) << 16) | (uint32_t(base[a + 2]) << 8) | base[a + 3];
}
uint32_t FBits(float f) { uint32_t u; std::memcpy(&u, &f, 4); return u; }
float BeF(const uint8_t* p) { uint32_t u = (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3]; float f; std::memcpy(&f, &u, 4); return f; }
void PutBeF(uint8_t* p, float f) { const uint32_t u = FBits(f); p[0] = u >> 24; p[1] = (u >> 16) & 0xFF; p[2] = (u >> 8) & 0xFF; p[3] = u & 0xFF; }
float Smooth01(float k) { k = std::clamp(k, 0.f, 1.f); return k * k * (3 - 2 * k); }
float BitsF(uint32_t u) { float f; std::memcpy(&f, &u, 4); return f; }
bool GuestPtr(uint32_t a) { return a >= 0x40000000u && a < 0xF0000000u && (a & 3) == 0; }

// Memoria del juego para los parametros: zona propia en el monton del motor (persistente: un objeto puede
// quedarse con punteros a su estructura de creacion) o la pila del juego.
struct Arena {
  uint32_t start = 0, cur = 0, end = 0;
  uint32_t Alloc(uint32_t n) {
    n = (n + 15u) & ~15u;
    if (!end || cur + n > end) return 0;
    const uint32_t a = cur;
    cur += n;
    return a;
  }
};

struct Params {
  uint8_t* base;
  Arena& ar;
  uint32_t addr = 0, last = 0;
  bool ok = true;
  Params(uint8_t* b, Arena& a) : base(b), ar(a) {
    addr = ar.Alloc(8);
    if (!addr) { ok = false; return; }
    Put32(base, addr, 0); Put32(base, addr + 4, 0);
  }
  void Add(uint8_t type, uint32_t name, uint32_t value) {
    if (!ok) return;
    const uint32_t c = ar.Alloc(16);
    if (!c) { ok = false; return; }
    Put32(base, c, 0); base[c + 2] = type;
    Put32(base, c + 4, name); Put32(base, c + 8, value); Put32(base, c + 12, 0);
    if (last) Put32(base, last + 12, c); else Put32(base, addr + 4, c);
    last = c;
  }
  void Int(uint32_t n, int32_t v) { Add(1, n, uint32_t(v)); }
  void Float(uint32_t n, float v) { Add(2, n, FBits(v)); }
  void Key(uint32_t n, uint32_t v) { Add(13, n, v); }
  void Flag(uint32_t v) { Add(13, 0, v); }
  void Ref(uint32_t n, uint32_t global) { Add(26, n, global); }
  void Str(uint32_t n, const std::string& s) {
    const uint32_t a = ar.Alloc(static_cast<uint32_t>(s.size()) + 1);
    if (!a) { ok = false; return; }
    std::memcpy(base + a, s.data(), s.size()); base[a + s.size()] = 0;
    Add(3, n, a);
  }
  void Vec(uint32_t n, float x, float y, float z) {
    const uint32_t a = ar.Alloc(16);
    if (!a) { ok = false; return; }
    Put32(base, a, 0); Put32(base, a + 4, FBits(x)); Put32(base, a + 8, FBits(y)); Put32(base, a + 12, FBits(z));
    Add(6, n, a);
  }
  void Arr(uint32_t n, uint8_t elem, const std::vector<uint32_t>& vals) {
    const uint32_t a = ar.Alloc(12);
    if (!a) { ok = false; return; }
    Put32(base, a, 0); base[a + 2] = elem; Put32(base, a + 4, static_cast<uint32_t>(vals.size()));
    if (vals.size() == 1) {
      Put32(base, a + 8, vals[0]);
    } else {
      const uint32_t d = ar.Alloc(static_cast<uint32_t>(vals.size()) * 4);
      if (!d) { ok = false; return; }
      for (size_t i = 0; i < vals.size(); ++i) Put32(base, d + 4 * static_cast<uint32_t>(i), vals[i]);
      Put32(base, a + 8, d);
    }
    Add(12, n, a);
  }
  void FArr(uint32_t n, const std::vector<float>& v) {
    std::vector<uint32_t> u;
    for (float f : v) u.push_back(FBits(f));
    Arr(n, 2, u);
  }
  void Pair(uint32_t n, float x, float y) {
    const uint32_t a = ar.Alloc(16);
    if (!a) { ok = false; return; }
    Put32(base, a, 0); Put32(base, a + 4, FBits(x)); Put32(base, a + 8, FBits(y)); Put32(base, a + 12, 0);
    Add(5, n, a);
  }
  void IArr(uint32_t n, const std::vector<int>& v) {
    std::vector<uint32_t> u;
    for (int i : v) u.push_back(uint32_t(i));
    Arr(n, 1, u);
  }
};

// Llamada protegida a una funcion del juego. false si el motor fallo (excepcion).
bool CallGuestRaw(GuestFn fn, PPCContext& ctx, uint8_t* base, uint32_t sp, const uint32_t* r, int nr, uint32_t* ret) {
  PPCContext c = ctx;
  c.r1.u64 = sp;
  if (nr > 0) c.r3.u64 = r[0];
  if (nr > 1) c.r4.u64 = r[1];
  if (nr > 2) c.r5.u64 = r[2];
  if (nr > 3) c.r6.u64 = r[3];
  if (nr > 4) c.r7.u64 = r[4];
  __try {
    fn(c, base);
  } __except (1) {
    return false;
  }
  if (ret) *ret = c.r3.u32;
  return true;
}

uint32_t g_sp = 0;   // pila de las llamadas (por debajo de la del juego); las zonas de datos van por encima
std::atomic<int> g_faults{0};

Arena StackArena(uint32_t size) {
  const uint32_t s = (g_sp + 0x1000) & ~15u;
  return Arena{s, s, s + size};
}

bool CallCFunc(GuestFn fn, PPCContext& ctx, uint8_t* base, const Params& p, uint32_t* ret = nullptr) {
  if (!p.ok) return false;
  const uint32_t r[2] = {p.addr, 0};
  const bool ok = CallGuestRaw(fn, ctx, base, g_sp, r, 2, ret);
  if (!ok) ++g_faults;
  return ok;
}

// Zona persistente (64 KB) en el monton principal del motor; se recicla cuando no queda ningun objeto.
Arena g_heap;
bool g_heap_tried = false;
void EnsureHeap(PPCContext& ctx, uint8_t* base) {
  if (g_heap_tried) return;
  g_heap_tried = true;
  const uint32_t heap = Get32(base, 0x8297F0DC);
  uint32_t a = 0;
  const uint32_t r[5] = {heap, 0x10000, 1, 0, 0};
  if (GuestPtr(heap) && CallGuestRaw(sub_82392380, ctx, base, g_sp, r, 5, &a) && GuestPtr(a)) {
    g_heap = Arena{a, a, a + 0x10000};
    TRACE("[NativeFX] memoria propia en el motor: 64 KB en 0x" << std::hex << a);
  } else {
    TRACE("[NativeFX] no se pudo reservar memoria en el motor; los objetos usaran la pila");
  }
}

// Viewports del motor (lista en 0x82860320: +4 siguiente, +16 viewport, +864 id)
struct VpNode { uint32_t node, vp, id; };
std::vector<VpNode> Viewports(const uint8_t* base) {
  std::vector<VpNode> out;
  const uint32_t head = 0x82860320;
  uint32_t node = Get32(base, head + 4);
  for (int i = 0; i < 32 && node != head && GuestPtr(node); ++i) {
    out.push_back({node, Get32(base, node + 16), Get32(base, node + 864)});
    node = Get32(base, node + 4);
  }
  return out;
}
bool ViewportExists(const uint8_t* base, uint32_t id) {
  for (const auto& v : Viewports(base)) if (v.id == id) return true;
  return false;
}

// ---------------------------------------------------------------------------------------------------------
// ScreenFX
struct Inst {
  uint32_t name, type;
  bool live = false, active = false;
};
Inst g_bs{0x69A6443F, k_bright_Sat}, g_vig{0xCEDB8EF5, k_Vignette}, g_dof{0xF0246EFB, k_dof3},
    g_bloom{QbKey("edfx_bloom"), k_bloom2}, g_noise{QbKey("edfx_noise"), k_noise2},
    g_mblur{QbKey("edfx_mblur"), k_MotionBlur}, g_fish{QbKey("edfx_fish"), k_fisheye},
    g_god{QbKey("edfx_godray"), k_godray}, g_conv{QbKey("edfx_conv"), k_convolution};
std::atomic<int> g_logs{0};

float Get(const std::vector<edfx::FxState>& fx, const char* t, float* el = nullptr, float* left = nullptr) {
  for (const auto& f : fx)
    if (f.type == t) { if (el) *el = f.el; if (left) *left = f.left; return f.k; }
  return 0.f;
}

template <class F>
void Send(PPCContext& ctx, uint8_t* base, Inst& in, bool on, F&& fill) {
  if (!on && !in.active) return;
  {
    Arena ar = StackArena(0x400);
    Params ex(base, ar);
    ex.Key(k_viewport, k_bg_viewport);
    ex.Key(k_name, in.name);
    uint32_t r = 0;
    if (!CallCFunc(sub_82509360, ctx, base, ex, &r)) return;
    in.live = r != 0;
  }
  Arena ar = StackArena(0x1400);
  Params p(base, ar);
  p.Key(k_viewport, k_bg_viewport);
  p.Key(k_name, in.name);
  p.Key(k_scefName, in.name);
  p.Key(k_type, in.type);
  p.Int(k_on, on ? 1 : 0);
  p.Float(k_time, 0.f);
  fill(p);
  uint32_t r = 0;
  const bool ok = CallCFunc(in.live ? sub_82509178 : sub_825090D0, ctx, base, p, &r);
  if (!in.live && g_logs.fetch_add(1) < 24)
    TRACE("[NativeFX] anadida instancia ScreenFX 0x" << std::hex << in.type << std::dec
          << (ok ? (r ? " (ok)" : " (el motor dijo que no)") : " (FALLO: excepcion en el motor)"));
  in.live = ok;
  in.active = on;
}

// ---------------------------------------------------------------------------------------------------------
// Franjas de cine nativas: rectangulo {x, y, ancho, alto} del objeto de vista del escenario (+52 y copia +68)
struct Letterbox {
  uint32_t obj = 0;
  float orig[4] = {0, 0, 1, 1};
  bool applied = false, logged = false;
} g_lb;

bool RectAt(const uint8_t* base, uint32_t o, float r[4]) {
  if (!GuestPtr(o)) return false;
  for (int i = 0; i < 4; ++i) r[i] = BitsF(Get32(base, o + 52 + 4 * i));
  for (int i = 0; i < 4; ++i) if (Get32(base, o + 52 + 4 * i) != Get32(base, o + 68 + 4 * i)) return false;
  return r[0] >= 0 && r[0] < 1 && r[1] >= 0 && r[1] < 1 && r[2] > 0.05f && r[2] <= 1.001f && r[3] > 0.05f &&
         r[3] <= 1.001f;
}
bool FindStageRectRaw(const uint8_t* base, uint32_t& obj, float r[4]) {
  __try {
    const uint32_t head = 0x82860320;
    for (int pass = 0; pass < 2; ++pass) {
      uint32_t node = Get32(base, head + 4);
      for (int i = 0; i < 32 && node != head && GuestPtr(node); ++i) {
        const uint32_t id = Get32(base, node + 864), vp = Get32(base, node + 16);
        if (pass == 0 ? id == k_bg_viewport : id == 0) {
          const uint32_t cand[4] = {Get32(base, node), vp, GuestPtr(vp) ? Get32(base, vp) : 0, node};
          for (uint32_t c : cand)
            if (RectAt(base, c, r)) { obj = c; return true; }
        }
        node = Get32(base, node + 4);
      }
    }
  } __except (1) {
  }
  return false;
}
void WriteRect(uint8_t* base, uint32_t o, const float r[4]) {
  __try {
    for (int i = 0; i < 4; ++i) { Put32(base, o + 52 + 4 * i, FBits(r[i])); Put32(base, o + 68 + 4 * i, FBits(r[i])); }
  } __except (1) {
  }
}

void TickLetterbox(uint8_t* base, const std::vector<edfx::FxState>& fx) {
  float el = 0, left = 0;
  const float k = Get(fx, "letterbox", &el, &left);
  if (k <= 0) {
    if (g_lb.applied) { WriteRect(base, g_lb.obj, g_lb.orig); g_lb.applied = false; }
    edfx::SetNativeLetterbox(false);
    return;
  }
  if (!g_lb.applied) {
    float r[4];
    if (!FindStageRectRaw(base, g_lb.obj, r)) {
      if (!g_lb.logged) { g_lb.logged = true; TRACE("[NativeFX] franjas de cine: no encontre el rectangulo del escenario (se dibujan encima)"); }
      edfx::SetNativeLetterbox(false);
      return;
    }
    std::memcpy(g_lb.orig, r, sizeof(r));
    g_lb.applied = true;
    if (!g_lb.logged) {
      g_lb.logged = true;
      TRACE("[NativeFX] franjas de cine nativas: rect del escenario @" << std::hex << g_lb.obj << std::dec << " = ("
            << r[0] << "," << r[1] << "," << r[2] << "," << r[3] << ")");
    }
  }
  const float e = std::min({1.f, el / 0.4f, left / 0.4f});
  float ratio = 2.39f;
  for (const auto& f : fx) if (f.type == "letterbox") ratio = std::max(1.0f, f.P("ratio", 2.39f));
  const float full = std::max(0.f, (1 - (16.f / 9.f) / ratio) / 2);   // alto de cada franja para esa proporcion
  const float bar = std::min(0.45f, full) * k * e * e * (3 - 2 * e);
  const float* o = g_lb.orig;
  const float nr[4] = {o[0], o[1] + o[3] * bar, o[2], o[3] * (1 - 2 * bar)};
  WriteRect(base, g_lb.obj, nr);
  edfx::SetNativeLetterbox(true);
}

// ---------------------------------------------------------------------------------------------------------
// Objetos nativos
struct Live { std::string kind; uint32_t key; };
std::map<int, Live> g_live;
std::string g_live_song;
double g_last_ms = 0;
bool g_objects_off = false;

struct Housing { const char* type; const char* model; };
const Housing kHousings[] = {
    {"GO_LightHousing02_SmallFlare01", "LightHousings\\GO_LightHousing02_SmallFlare01\\GO_LightHousing02_SmallFlare01.mdl"},
    {"GO_NoHousing01_Flare01", "LightHousings\\GO_NoHousing01_Flare01\\GO_NoHousing01_Flare01.mdl"},
    {"GO_LightHousing01", "LightHousings\\GO_LightHousing01\\GO_LightHousing01.mdl"},
    {"GO_BarnHousing01", "LightHousings\\GO_BarnHousing01\\GO_BarnHousing01.mdl"},
    {"GO_LH_Bowl", "LightHousings\\GO_LH_Bowl\\GO_LH_Bowl.mdl"},
    {"GO_NoHousing01_MomentFlare01", "LightHousings\\GO_NoHousing01_MomentFlare01\\GO_NoHousing01_MomentFlare01.mdl"},
    {"GO_LightHousing02_Small01", "LightHousings\\GO_LightHousing02_Small01\\GO_LightHousing02_Small01.mdl"},
    {"GO_NoHousing01", "LightHousings\\GO_NoHousing01\\GO_NoHousing01.mdl"},
};

void GameObjectBase(Params& p, uint32_t key, const edfx::NativeItem& it) {
  const float s = it.q[3] < 0 ? -1.f : 1.f;   // el motor guarda x, y, z (w implicito, positivo)
  p.Key(k_name, key);
  p.Vec(QbKey("pos"), it.p[0], it.p[1], it.p[2]);
  p.Vec(QbKey("Orientation"), it.q[0] * s, it.q[1] * s, it.q[2] * s);
  p.Key(QbKey("Class"), QbKey("GameObject"));
}

bool CreateNative(PPCContext& ctx, uint8_t* base, const edfx::NativeItem& it, uint32_t key) {
  Arena stack = StackArena(0x1800);
  Arena& mem = g_heap.end ? g_heap : stack;
  if (it.kind == "pfx") {
    Params p(base, mem);
    p.Key(k_name, key);
    p.Ref(QbKey("params_Script"), QbKey(it.asset));
    p.Vec(QbKey("pos"), it.p[0], it.p[1], it.p[2]);
    p.Key(QbKey("groupID"), key);
    return CallCFunc(sub_821DB160, ctx, base, p);
  }
  if (it.kind == "light") {
    const int h = std::clamp(static_cast<int>(it.v[2]), 0, static_cast<int>(std::size(kHousings)) - 1);
    auto c255 = [](float v) { return std::clamp(static_cast<int>(v * 255), 0, 255); };
    const int r = c255(it.rgb[0]), g = c255(it.rgb[1]), b = c255(it.rgb[2]);
    Params p(base, mem);
    GameObjectBase(p, key, it);
    p.Key(k_type, QbKey(kHousings[h].type));
    p.Flag(QbKey("CreatedAtStart"));
    p.Flag(QbKey("RenderToViewport"));
    p.Key(QbKey("LightGroup"), QbKey("none"));
    p.Key(QbKey("LightGroup2"), QbKey("Empty"));
    p.Key(QbKey("LightGroup3"), QbKey("Empty"));
    p.Key(QbKey("LightGroup4"), QbKey("Empty"));
    p.Key(QbKey("DisableShadowCasting"), QbKey("false"));
    p.Key(QbKey("VolumeLightGroup"), QbKey("none"));
    p.Int(QbKey("lod_dist1"), 400);
    p.Int(QbKey("lod_dist2"), 401);
    p.Str(QbKey("Model"), kHousings[h].model);
    p.Float(QbKey("StartRadius"), 0.f);
    p.Float(QbKey("EndRadius"), std::max(0.1f, it.v[1]));
    p.Float(QbKey("Range"), std::max(0.5f, it.v[0]));
    p.Key(QbKey("LightVolumeType"), QbKey("Volume2DSoftEdge"));
    p.Float(QbKey("VolumeDensity"), 0.75f);
    p.IArr(QbKey("VolumeColor"), {r, g, b});
    p.Float(QbKey("VolumeQuality"), 0.f);
    p.Key(QbKey("ProjectorType"), QbKey("Everything"));
    p.IArr(QbKey("ProjectorColor"), {r, g, b});
    p.Key(QbKey("ProjectorShadow"), QbKey("false"));
    p.Key(QbKey("ProjectorMainShadow"), QbKey("false"));
    p.Key(QbKey("ProjectorSelfShadow"), QbKey("false"));
    p.Float(QbKey("ShadowQuality"), 0.f);
    p.Float(QbKey("ShadowZBias"), 0.f);
    p.Key(QbKey("SmokeEffect"), QbKey("true"));
    p.Key(QbKey("VolumeShadow"), QbKey("false"));
    p.Key(QbKey("Volume2DFalloffTexture"), QbKey("none"));
    p.Key(QbKey("Volume2DPatternTexture"), QbKey("none"));
    p.Key(QbKey("ProjectorFalloffTexture"), QbKey("none"));
    p.Key(QbKey("ProjectorPatternTexture"), QbKey("none"));
    p.Float(QbKey("HotspotIntensity"), 0.25f);
    p.Float(QbKey("ProjectorIntensity"), 5.f);
    p.Float(QbKey("FlareAlpha"), 1.f);
    return CallCFunc(sub_825012B8, ctx, base, p);
  }
  if (it.kind == "model") {
    // recurso: "Ruta\\Modelo.skin" o "zona:Ruta\\Modelo.skin" (solo si ese escenario esta cargado)
    std::string path = it.asset;
    if (const auto c = path.find(':'); c != std::string::npos && c > 1) {
      if (!edfx::ZoneLoaded(path.substr(0, c))) return false;
      path = path.substr(c + 1);
    }
    std::replace(path.begin(), path.end(), '/', '\\');
    Params p(base, mem);
    GameObjectBase(p, key, it);
    p.Key(k_type, QbKey("Ghost"));
    p.Flag(QbKey("CreatedAtStart"));
    p.Key(QbKey("LightGroup"), QbKey("Combo"));
    p.Key(QbKey("LightGroup2"), QbKey("Empty"));
    p.Key(QbKey("LightGroup3"), QbKey("Empty"));
    p.Key(QbKey("LightGroup4"), QbKey("Empty"));
    p.Key(QbKey("DisableShadowCasting"), QbKey("false"));
    p.Key(QbKey("VolumeLightGroup"), QbKey("none"));
    p.Str(QbKey("Model"), path);
    p.Int(QbKey("SuspendDistance"), 0);
    p.Int(QbKey("lod_dist1"), 400);
    p.Int(QbKey("lod_dist2"), 401);
    return CallCFunc(sub_825012B8, ctx, base, p);
  }
  return false;
}

void DestroyNative(PPCContext& ctx, uint8_t* base, const Live& l) {
  Arena ar = StackArena(0x400);
  Params p(base, ar);
  if (l.kind == "pfx") {
    p.Key(QbKey("groupID"), l.key);
    CallCFunc(sub_824C05B8, ctx, base, p);
  } else {
    p.Key(k_name, l.key);
    CallCFunc(sub_82502BA0, ctx, base, p);
  }
}

void KillAll(PPCContext& ctx, uint8_t* base) {
  for (const auto& [id, l] : g_live) DestroyNative(ctx, base, l);
  g_live.clear();
  g_heap.cur = g_heap.start;
}

void TickObjects(PPCContext& ctx, uint8_t* base, bool has_stage) {
  if (g_objects_off) return;
  std::string song;
  double ms = 0;
  std::vector<edfx::NativeItem> items;
  const bool clock = edfx::NativeNow(song, ms, items);
  if (!has_stage) {                 // sin escenario el motor ya destruyo todo lo que habia en el
    g_live.clear();
    g_heap.cur = g_heap.start;
    g_live_song = song;
    return;
  }
  if (song != g_live_song || (clock && ms + 400 < g_last_ms)) {   // otra cancion o reinicio/rebobinado
    if (!g_live.empty()) KillAll(ctx, base);
    g_live_song = song;
  }
  if (clock) g_last_ms = ms;
  if (!clock) return;                                             // pausa / menu: no se toca nada
  EnsureHeap(ctx, base);
  for (auto it = g_live.begin(); it != g_live.end();) {
    const bool still = std::any_of(items.begin(), items.end(), [&](const edfx::NativeItem& n) { return n.id == it->first; });
    if (!still) { DestroyNative(ctx, base, it->second); it = g_live.erase(it); }
    else ++it;
  }
  if (g_live.empty()) g_heap.cur = g_heap.start;
  int made = 0;
  for (const auto& it : items) {
    if (g_live.count(it.id) || made >= 3) continue;   // pocos por frame: sin tirones
    const uint32_t key = QbKey("edfx_n_" + song + "_" + std::to_string(it.id));
    const int faults0 = g_faults.load();
    const bool ok = CreateNative(ctx, base, it, key);
    ++made;
    g_live[it.id] = {it.kind, key};   // aunque no se cree: no se reintenta cada frame
    static int logs = 0;
    if (logs++ < 40)
      TRACE("[NativeFX] objeto nativo " << it.kind << " '" << it.asset << "' en (" << it.p[0] << "," << it.p[1] << ","
            << it.p[2] << "): " << (ok ? "creado" : "no creado"));
    if (g_faults.load() != faults0) {
      g_objects_off = true;
      TRACE("[NativeFX] el motor fallo al crear un objeto: objetos nativos desactivados en esta sesion");
      return;
    }
  }
}

// ---------------------------------------------------------------------------------------------------------
// PANTALLA DIVIDIDA nativa (efecto "split"): cada franja es un viewport REAL del motor (CreateViewport) con su
// propia camara, dibujado por el motor por encima del escenario y por debajo del traste/HUD.
//   - Los estilos de viewport del juego viven en viewport_Params.styles.<estilo> {rect, managed, ...} y los
//     busca sub_823E7858; el hook (abajo, fuera del namespace) devuelve NUESTRA estructura para nuestras claves.
//   - Cada franja aparece en su momento (al ritmo: tiempos r0..r3 que calcula el editor) y se queda con la
//     camara que tenia el juego en ese instante (plano fijo). Modo 1: entra con barrido en vez de corte.
struct Strip {
  uint32_t id = 0, node = 0, vp = 0, cam = 0, orig_cam = 0, rect_obj = 0;
  bool up = false, own_cam = false;
  uint8_t pose[96] = {};
  float rect[4] = {0, 0, 1, 1};
  float born = 0;
};
struct Split {
  bool on = false, off_for_session = false;
  int n = 0, dir = 0;
  Strip s[4];
  uint32_t cam_slots[4] = {};
} g_split;
Arena g_style_mem;
std::map<uint32_t, uint32_t> g_styles;   // clave de estilo -> CStruct en memoria del juego

void EnsureStyleMem(PPCContext& ctx, uint8_t* base) {
  if (g_style_mem.end) return;
  const uint32_t heap = Get32(base, 0x8297F0DC);
  uint32_t a = 0;
  const uint32_t r[5] = {heap, 0x4000, 1, 0, 0};
  if (GuestPtr(heap) && CallGuestRaw(sub_82392380, ctx, base, g_sp, r, 5, &a) && GuestPtr(a)) g_style_mem = Arena{a, a, a + 0x4000};
}

void StripRect(int n, int dir, int i, float r[4]) {
  if (dir == 2 && n == 4) { r[0] = (i % 2) * 0.5f; r[1] = (i / 2) * 0.5f; r[2] = 0.5f; r[3] = 0.5f; return; }
  if (dir == 1) { r[0] = float(i) / n; r[1] = 0; r[2] = 1.f / n; r[3] = 1; return; }
  r[0] = 0; r[1] = float(i) / n; r[2] = 1; r[3] = 1.f / n;
}

uint32_t StyleKey(uint8_t* base, int n, int dir, int i) {
  const uint32_t key = QbKey("edfx_split_" + std::to_string(n) + "_" + std::to_string(dir) + "_" + std::to_string(i));
  if (g_styles.count(key)) return key;
  if (!g_style_mem.end) return 0;
  float r[4];
  StripRect(n, dir, i, r);
  Params p(base, g_style_mem);
  p.FArr(QbKey("rect"), {r[0], r[1], r[2], r[3]});
  p.Flag(QbKey("managed"));
  p.Flag(QbKey("discard_previous_frame"));
  p.Pair(QbKey("uidims"), 1280.f, 720.f);
  p.Key(QbKey("ProfilerID"), QbKey("MainViewport"));
  p.Key(QbKey("sharevisibilityflagswith"), k_bg_viewport);   // que dibuje lo mismo que el escenario
  if (!p.ok) return 0;
  g_styles[key] = p.addr;
  return key;
}

uint32_t FindNode(const uint8_t* base, uint32_t id) {
  for (const auto& v : Viewports(base)) if (v.id == id) return v.node;
  return 0;
}

bool RectObjOfNode(const uint8_t* base, uint32_t node, uint32_t& obj) {
  __try {
    const uint32_t vp = Get32(base, node + 16);
    const uint32_t cand[4] = {Get32(base, node), vp, GuestPtr(vp) ? Get32(base, vp) : 0, node};
    float r[4];
    for (uint32_t c : cand)
      if (RectAt(base, c, r)) { obj = c; return true; }
  } __except (1) {
  }
  return false;
}

bool CopyGuest(uint8_t* base, uint32_t dst, uint32_t src, uint32_t n) {
  __try {
    std::memmove(base + dst, base + src, n);
    return true;
  } __except (1) {
    return false;
  }
}
bool Write32Safe(uint8_t* base, uint32_t a, uint32_t v) {
  __try {
    Put32(base, a, v);
    return true;
  } __except (1) {
    return false;
  }
}

bool CopyOut(const uint8_t* base, uint8_t* dst, uint32_t src, uint32_t n) {
  __try {
    std::memcpy(dst, base + src, n);
    return true;
  } __except (1) {
    return false;
  }
}
bool CopyIn(uint8_t* base, uint32_t dst, const uint8_t* src, uint32_t n) {
  __try {
    std::memcpy(base + dst, src, n);
    return true;
  } __except (1) {
    return false;
  }
}

void DestroyStrip(PPCContext& ctx, uint8_t* base, Strip& st) {
  if (!st.up) return;
  if (st.own_cam && GuestPtr(st.vp)) Write32Safe(base, st.vp + 116, st.orig_cam);   // el motor no libera lo nuestro
  Arena ar = StackArena(0x200);
  Params p(base, ar);
  p.Key(QbKey("id"), st.id);
  CallCFunc(sub_8251BF98, ctx, base, p);
  st = Strip{};
}

void EndSplit(PPCContext& ctx, uint8_t* base) {
  for (auto& st : g_split.s) DestroyStrip(ctx, base, st);
  g_split.on = false;
}

void TickSplit(PPCContext& ctx, uint8_t* base, const std::vector<edfx::FxState>& fx, uint32_t stage_node) {
  const edfx::FxState* f = nullptr;
  for (const auto& x : fx) if (x.type == "split") f = &x;
  if (!f || g_split.off_for_session || !stage_node) {
    if (g_split.on) EndSplit(ctx, base);
    return;
  }
  const int n = std::clamp(static_cast<int>(f->P("n", 4)), 2, 4);
  const int dir = std::clamp(static_cast<int>(f->P("dir", 0)), 0, 2);
  const int mode = static_cast<int>(f->P("mode", 0));
  if (g_split.on && (g_split.n != n || g_split.dir != dir)) EndSplit(ctx, base);
  if (!g_split.on) { g_split.on = true; g_split.n = n; g_split.dir = dir; }
  EnsureStyleMem(ctx, base);
  uint32_t main_cam = 0;
  edfx::MainCamera(main_cam);
  const uint32_t prio = Get32(base, stage_node + 872);
  const float total = f->el + f->left;
  for (int i = 0; i < n; ++i) {
    Strip& st = g_split.s[i];
    const char rk[4] = {'r', static_cast<char>('0' + i), 0, 0};
    const float at = f->P(rk, total * 0.8f * i / n);   // segundos desde el inicio del efecto
    if (!st.up && f->el >= at && main_cam) {
      const uint32_t style = StyleKey(base, n, dir, i);
      if (!style) continue;
      st.id = QbKey("edfx_split_vp_" + std::to_string(i));
      Arena ar = StackArena(0x300);
      Params p(base, ar);
      p.Key(QbKey("id"), st.id);
      p.Key(QbKey("style"), style);
      p.Int(QbKey("priority"), static_cast<int32_t>(prio) + 1 + i);
      p.Key(QbKey("sharevisibilityflagswith"), k_bg_viewport);
      const int faults0 = g_faults.load();
      CallCFunc(sub_8251BEC0, ctx, base, p);
      st.node = FindNode(base, st.id);
      if (g_faults.load() != faults0 || !st.node) {
        TRACE("[NativeFX] pantalla dividida: el motor no creo el viewport " << i << (g_faults.load() != faults0 ? " (FALLO)" : ""));
        if (g_faults.load() != faults0) g_split.off_for_session = true;
        st = Strip{};
        continue;
      }
      st.up = true;
      st.born = f->el;
      st.vp = Get32(base, st.node + 16);
      st.orig_cam = GuestPtr(st.vp) ? Get32(base, st.vp + 116) : 0;
      CopyOut(base, st.pose, main_cam + 112, 96);   // plano fijo: la camara del juego en este instante
      st.cam = st.orig_cam;
      if (GuestPtr(st.vp) && (!GuestPtr(st.cam) || st.cam == main_cam)) {
        if (!g_split.cam_slots[i]) g_split.cam_slots[i] = g_style_mem.Alloc(0x200);
        if (g_split.cam_slots[i] && CopyGuest(base, g_split.cam_slots[i], main_cam, 0x200)) {
          st.cam = g_split.cam_slots[i];
          st.own_cam = Write32Safe(base, st.vp + 116, st.cam);
        }
      }
      RectObjOfNode(base, st.node, st.rect_obj);
      StripRect(n, dir, i, st.rect);
      static int logs = 0;
      if (logs++ < 16)
        TRACE("[NativeFX] pantalla dividida: franja " << i + 1 << "/" << n << " viewport @" << std::hex << st.vp << " camara @"
              << st.cam << (st.own_cam ? " (propia)" : " (del motor)") << " rect@" << st.rect_obj << std::dec
              << " prioridad " << prio + 1 + i);
    }
    if (!st.up) continue;
    // camara de la franja: la suya del editor (animada) o el plano del juego congelado al aparecer
    {
      float cp[3], cs[3], cu[3], cf[3], hf;
      const char mk[3] = {'m', static_cast<char>('0' + i), 0};
      const int smode = static_cast<int>(f->P(mk, 1));   // 0 congelada, 1 claves del editor, 2 la camara del juego en vivo
      if (smode == 2 && main_cam) CopyOut(base, st.pose, main_cam + 112, 96);
      else if (edfx::StripCamera(i, cp, cs, cu, cf, hf)) {
        for (int j = 0; j < 3; ++j) {
          PutBeF(st.pose + 4 * j, cp[j]); PutBeF(st.pose + 16 + 4 * j, cs[j]); PutBeF(st.pose + 32 + 4 * j, cu[j]);
          PutBeF(st.pose + 48 + 4 * j, cf[j]); PutBeF(st.pose + 64 + 4 * j, cp[j]);
        }
        PutBeF(st.pose + 80, hf);
      }
    }
    if (GuestPtr(st.cam) && st.cam != main_cam) {
      uint8_t pose[96];
      std::memcpy(pose, st.pose, 96);
      if (mode == 1 && dir != 1) {   // barrido a lo ancho: se recorta (mismo tamano de imagen), no se aplasta
        const float e = std::min(1.f, (f->el - st.born) / 0.3f), k = std::max(0.02f, e * e * (3 - 2 * e));
        const float h = BeF(pose + 80), deg = h > 3.2f ? 0.0174533f : 1.f;
        PutBeF(pose + 80, 2 * std::atan(std::tan(h * deg / 2) * k) / deg);
      }
      CopyIn(base, st.cam + 112, pose, 96);
    }
    // entrada con barrido
    if (mode == 1 && st.rect_obj) {
      const float e = std::min(1.f, (f->el - st.born) / 0.3f), k = e * e * (3 - 2 * e);
      float r[4] = {st.rect[0], st.rect[1], st.rect[2], st.rect[3]};
      if (dir == 1) r[3] *= std::max(0.02f, k); else r[2] *= std::max(0.02f, k);
      WriteRect(base, st.rect_obj, r);
    }
  }
}

// Matrices de convolucion 3x3
const float kSharpen[9] = {0, -1, 0, -1, 5, -1, 0, -1, 0};
const float kEmboss[9] = {-2, -1, 0, -1, 1, 1, 0, 1, 2};
const float kEdges[9] = {-1, -1, -1, -1, 8, -1, -1, -1, -1};
const float kIdentity[9] = {0, 0, 0, 0, 1, 0, 0, 0, 0};

// Looks de color (matriz de canales + saturacion/contraste/brillo + extras nativos)
struct Look { const char* name; float M[3][3]; float sat, con, bri, vig, grain, bloom; };
const Look kLooks[] = {
    {"teal", {{1.12f, 0.0f, -0.08f}, {-0.02f, 1.0f, 0.04f}, {-0.1f, 0.12f, 1.02f}}, 1.15f, 1.12f, 1.0f, 0.25f, 0.0f, 0.0f},
    {"bleach", {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}, 0.45f, 1.4f, 1.05f, 0.5f, 0.15f, 0.0f},
    {"matrix", {{0.65f, 0.25f, 0.0f}, {0.1f, 1.05f, 0.05f}, {0.0f, 0.25f, 0.55f}}, 1.0f, 1.18f, 0.95f, 0.45f, 0.2f, 0.2f},
    {"vaporwave", {{0.95f, 0.05f, 0.25f}, {0.05f, 0.78f, 0.2f}, {0.25f, 0.1f, 1.0f}}, 1.25f, 0.95f, 1.05f, 0.25f, 0.25f, 0.7f},
    {"cyberpunk", {{1.05f, 0.0f, 0.2f}, {0.0f, 0.82f, 0.15f}, {0.15f, 0.05f, 1.15f}}, 1.3f, 1.2f, 1.0f, 0.4f, 0.1f, 0.8f},
    {"noir", {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}, 0.0f, 1.55f, 0.95f, 0.85f, 0.45f, 0.0f},
    {"sunset", {{1.15f, 0.08f, 0.0f}, {0.04f, 1.0f, 0.0f}, {0.0f, 0.0f, 0.78f}}, 1.15f, 1.05f, 1.04f, 0.35f, 0.0f, 0.5f},
    {"lomo", {{1.06f, 0, 0}, {0, 1.04f, 0}, {0, 0, 0.9f}}, 1.4f, 1.3f, 1.0f, 0.95f, 0.2f, 0.0f},
    {"technicolor", {{1.0f, -0.12f, 0.12f}, {-0.08f, 1.08f, 0.0f}, {0.0f, -0.05f, 1.05f}}, 1.5f, 1.15f, 1.0f, 0.2f, 0.0f, 0.0f},
    {"infrared", {{0.1f, 1.0f, 0.2f}, {0.6f, 0.3f, 0.1f}, {0.2f, 0.1f, 0.5f}}, 1.2f, 1.2f, 1.05f, 0.3f, 0.2f, 0.3f},
    {"pastel", {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}, 0.6f, 0.85f, 1.12f, 0.0f, 0.0f, 0.4f},
    {"film", {{1.05f, 0.02f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.02f, 0.92f}}, 1.1f, 1.1f, 1.0f, 0.35f, 0.3f, 0.0f},
    {"polaroid", {{1.2f, -0.04f, -0.04f}, {-0.07f, 1.2f, -0.07f}, {-0.01f, -0.01f, 1.25f}}, 0.9f, 0.95f, 1.05f, 0.4f, 0.15f, 0.0f},
    {"kodak", {{1.1f, 0.05f, -0.05f}, {0.0f, 1.04f, -0.02f}, {-0.05f, 0.0f, 0.92f}}, 1.2f, 1.12f, 1.0f, 0.3f, 0.2f, 0.0f},
    {"blockbuster", {{0.92f, 0.04f, 0.04f}, {0.0f, 1.0f, 0.05f}, {0.0f, 0.08f, 1.15f}}, 0.9f, 1.2f, 0.97f, 0.4f, 0.0f, 0.0f},
    {"desert", {{1.18f, 0.1f, 0.0f}, {0.05f, 1.0f, 0.0f}, {0.0f, 0.0f, 0.65f}}, 1.1f, 1.1f, 1.05f, 0.3f, 0.1f, 0.3f},
    {"toxic", {{0.8f, 0.3f, 0.0f}, {0.1f, 1.15f, 0.0f}, {0.0f, 0.1f, 0.6f}}, 1.3f, 1.2f, 1.0f, 0.5f, 0.2f, 0.2f},
    {"blood", {{1.2f, 0.1f, 0.1f}, {0.0f, 0.55f, 0.05f}, {0.0f, 0.05f, 0.55f}}, 1.1f, 1.3f, 0.95f, 0.8f, 0.3f, 0.0f},
    {"golden", {{1.12f, 0.1f, 0.0f}, {0.05f, 1.05f, 0.0f}, {0.0f, 0.05f, 0.75f}}, 1.05f, 1.05f, 1.08f, 0.2f, 0.0f, 0.8f},
    {"ice", {{0.85f, 0.05f, 0.1f}, {0.05f, 0.95f, 0.15f}, {0.05f, 0.1f, 1.2f}}, 0.7f, 1.1f, 1.08f, 0.3f, 0.0f, 0.4f},
    {"retro70", {{1.08f, 0.1f, 0.0f}, {0.05f, 0.98f, 0.05f}, {0.05f, 0.05f, 0.8f}}, 0.75f, 0.88f, 1.04f, 0.45f, 0.35f, 0.0f},
    {"miami", {{1.1f, 0.0f, 0.15f}, {0.0f, 0.9f, 0.15f}, {0.1f, 0.1f, 1.05f}}, 1.35f, 1.1f, 1.04f, 0.2f, 0.0f, 0.6f},
    {"dramatic", {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}, 0.8f, 1.45f, 0.95f, 0.7f, 0.1f, 0.0f},
};

void HueMatrix(float h, float R[3][3]) {
  const float c = std::cos(h), s = std::sin(h), k1 = (1 - c) / 3, k2 = s * 0.57735f;
  const float M[3][3] = {{c + k1, k1 - k2, k1 + k2}, {k1 + k2, c + k1, k1 - k2}, {k1 - k2, k1 + k2, c + k1}};
  std::memcpy(R, M, sizeof(M));
}

}  // namespace

uint32_t StyleFor(uint32_t key) {
  const auto it = g_styles.find(key);
  return it == g_styles.end() ? 0 : it->second;
}

void Tick(PPCContext& ctx, uint8_t* base) {
  g_sp = (ctx.r1.u32 - 0x4000) & ~0xFu;
  const bool has_stage = ViewportExists(base, k_bg_viewport);
  uint32_t stage_node = FindNode(base, k_bg_viewport);
  if (!stage_node) stage_node = FindNode(base, 0);
  TickObjects(ctx, base, has_stage || stage_node != 0);
  const auto fx = edfx::ActiveFxNow();
  TickSplit(ctx, base, fx, stage_node);
  if (!has_stage) return;

  // SANDBOX DE RENDER: el motor solo dibuja un bright_Sat neutro (el marcador); los efectos los pinta la pasada
  // propia justo despues (render_sandbox.cpp). Si la pasada no llega, se sigue con el camino de siempre.
  const sandbox::NativeMode smode = sandbox::Mode();
  if (smode == sandbox::NativeMode::kMarkerOnly || smode == sandbox::NativeMode::kAllOff ||
      (smode == sandbox::NativeMode::kSandbox && (sandbox::DrawsImage() || fx.empty()))) {
    if (g_faults.load() > 8) return;
    TickLetterbox(base, {});
    const bool marker = false;   // el sandbox ya no necesita el filtro marcador
    Send(ctx, base, g_bs, marker, [&](Params& p) {
      p.Float(k_target, 0.25f);
      p.Float(k_saturation, 1.f);
      p.Float(k_brightness, 1.f);
      p.Float(k_contrast, 1.f);
      p.Float(QbKey("hue"), 1.f);
      p.FArr(QbKey("red_mix"), {1.f, 0.f, 0.f, 0.f});
      p.FArr(QbKey("green_mix"), {0.f, 1.f, 0.f, 0.f});
      p.FArr(QbKey("blue_mix"), {0.f, 0.f, 1.f, 0.f});
    });
    auto off = [](Params&) {};
    for (Inst* in : {&g_vig, &g_dof, &g_bloom, &g_noise, &g_mblur, &g_fish, &g_god, &g_conv}) Send(ctx, base, *in, false, off);
    return;
  }
  TickLetterbox(base, fx);

  const bool any_live = g_bs.active || g_vig.active || g_dof.active || g_bloom.active || g_noise.active ||
                        g_mblur.active || g_fish.active || g_god.active || g_conv.active;
  if (fx.empty() && !any_live) return;
  if (g_faults.load() > 8) return;   // el motor no acepta nuestras llamadas: no insistir

  // ---- color: bright_Sat ----
  // Formato del juego (presets de qb.pab): brightness/contrast/saturation y el mezclador de canales como TRES
  // arrays red_mix/green_mix/blue_mix = [r, g, b, 0] (el lector del motor no conoce red_mix_r ni similares).
  float sat = 1, bri = 1, con = 1;
  float M[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
  auto mixTo = [&](const float Tm[3][3], float k) {
    float N[3][3];   // M <- lerp(M, Tm*M, k): las matrices se encadenan
    for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) {
      float v = 0;
      for (int x = 0; x < 3; ++x) v += Tm[i][x] * M[x][j];
      N[i][j] = M[i][j] + (v - M[i][j]) * k;
    }
    std::memcpy(M, N, sizeof(N));
  };
  auto scaleRow = [&](int r, float s) { for (int j = 0; j < 3; ++j) M[r][j] *= s; };
  auto colorize = [&](float cr, float cg, float cb, float k) {   // todo a luminancia * color
    const float L[3] = {0.299f, 0.587f, 0.114f}, c[3] = {cr, cg, cb};
    float Tm[3][3];
    for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) Tm[i][j] = L[j] * c[i] * 1.6f;
    mixTo(Tm, k);
  };
  const float T = static_cast<float>(edfx::SongMsNow()) / 1000.f;
  float el = 0, left = 0, a;
  float lk_vig = 0, lk_grain = 0, lk_bloom = 0;
  for (const auto& f : fx) {
    const float k = f.k;
    const std::string& ty = f.type;
    if (ty == "bw") { sat *= 1 - k; con *= 1 + f.P("contrast", 0.15f) * k; }
    else if (ty == "exposure") bri *= std::pow(2.f, f.P("stops", 0.6f) * k);
    else if (ty == "glow") bri *= 1 + 0.25f * k * f.P("bright", 0.f);   // el resplandor de verdad va en bloom2
    else if (ty == "saturate") sat *= std::max(0.f, 1 + f.P("amount", 0.8f) * k);
    else if (ty == "contrast") { con *= 1 + f.P("amount", 0.6f) * k; lk_vig = std::max(lk_vig, f.P("vig", 0.6f) * k); }
    else if (ty == "sepia") {
      static const float S[3][3] = {{0.393f, 0.769f, 0.189f}, {0.349f, 0.686f, 0.168f}, {0.272f, 0.534f, 0.131f}};
      mixTo(S, 0.9f * k);
    }
    else if (ty == "tint") colorize(f.P("colr", 1.f), f.P("colg", 0.5f), f.P("colb", 0.2f), f.P("mix", 0.5f) * k);
    else if (ty == "dream") { bri *= 1 + 0.12f * k; con *= 1 - 0.2f * k; sat *= 1 - 0.2f * k; lk_bloom = std::max(lk_bloom, 0.8f * k); lk_vig = std::max(lk_vig, 0.5f * k); }
    else if (ty == "night") {
      static const float N[3][3] = {{0.05f, 0.1f, 0.02f}, {0.3f, 0.6f, 0.12f}, {0.05f, 0.1f, 0.02f}};
      mixTo(N, k); bri *= 1 + 0.3f * k; lk_vig = std::max(lk_vig, k); lk_grain = std::max(lk_grain, 0.6f * k);
    }
    else if (ty == "cold") { const float m = f.P("amount", 1.f) * k; scaleRow(0, 1 - 0.12f * m); scaleRow(1, 1 - 0.02f * m); scaleRow(2, 1 + 0.18f * m); }
    else if (ty == "warm") { const float m = f.P("amount", 1.f) * k; scaleRow(0, 1 + 0.14f * m); scaleRow(1, 1 + 0.02f * m); scaleRow(2, 1 - 0.18f * m); }
    else if (ty == "hueshift") { float R[3][3]; HueMatrix(f.P("deg", 120.f) * 0.0174533f, R); mixTo(R, k); }
    else if (ty == "rainbow") { float R[3][3]; HueMatrix(std::fmod(T * 0.2f * f.P("speed", 1.f), 1.f) * 6.2831853f, R); mixTo(R, 0.8f * k); }
    else if (ty == "grade") {
      bri *= std::pow(2.f, f.P("exposure", 0) * k);
      con *= 1 + (f.P("contrast", 1) - 1) * k;
      sat *= 1 + (f.P("saturation", 1) - 1) * k;
      const float tt = f.P("temperature", 0) * k, ti = f.P("tint", 0) * k, hu = f.P("hue", 0) * k;
      scaleRow(0, 1 + 0.18f * tt); scaleRow(2, 1 - 0.18f * tt);
      scaleRow(1, 1 + 0.15f * ti);
      if (hu != 0) { float R[3][3]; HueMatrix(hu * 0.0174533f, R); mixTo(R, 1.f); }
      lk_vig = std::max(lk_vig, f.P("vignette", 0) * k);
    }
    else {
      for (const auto& L : kLooks) {
        if (ty != L.name) continue;
        mixTo(L.M, k);
        sat *= 1 + (L.sat - 1) * k; con *= 1 + (L.con - 1) * k; bri *= 1 + (L.bri - 1) * k;
        lk_vig = std::max(lk_vig, L.vig * k); lk_grain = std::max(lk_grain, L.grain * k); lk_bloom = std::max(lk_bloom, L.bloom * k);
      }
    }
  }
  if ((a = Get(fx, "redflash", &el))) {
    const float p = a * std::max(0.f, 1 - el / 0.6f);
    scaleRow(0, 1 + 1.2f * p); scaleRow(1, 1 - 0.6f * p); scaleRow(2, 1 - 0.6f * p);
  }
  if ((a = Get(fx, "heartbeat"))) {
    const float p = a * std::max(0.f, 1 - std::fmod(T * 1.2f, 1.f) * 4);
    scaleRow(0, 1 + 0.5f * p); bri *= 1 + 0.15f * p;
  }
  for (const auto& f : fx)
    if (f.type == "flicker") bri *= 1 - 0.35f * f.k * (0.5f + 0.5f * std::sin(T * 47 * f.P("speed", 1.f)) * std::sin(T * 13.3f * f.P("speed", 1.f)));
  float fade = Get(fx, "blackout");
  if ((a = Get(fx, "fadein", &el))) fade = std::max(fade, a * (1 - std::min(1.f, el / 0.8f)));
  if ((a = Get(fx, "fadeout", nullptr, &left))) fade = std::max(fade, a * (1 - std::min(1.f, left / 0.8f)));
  float boost = 0;
  for (const auto& f : fx) {
    if (f.type == "flash") boost = std::max(boost, f.P("power", 2.5f) * f.k * std::max(0.f, 1 - f.el / std::max(0.05f, f.P("len", 0.35f))));
    else if (f.type == "strobe" && static_cast<int>(f.el * 2 * f.P("rate", 8.f)) % 2 == 0) boost = std::max(boost, 2.5f * f.k);
    else if (f.type == "lightning") {
      const float ph = std::fmod(f.el / std::max(0.2f, f.P("every", 1.6f)), 1.f);
      if (ph < 0.12f) boost = std::max(boost, 2.f * f.k * (0.12f - ph) / 0.12f);
    }
    else if (f.type == "fadewhite") boost = std::max(boost, 3.f * f.k * Smooth01(f.el / std::max(0.05f, f.el + f.left)));
  }
  bri *= (1 - fade) * (1 + boost);
  bool bs_on = sat != 1 || bri != 1 || con != 1;
  for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) bs_on |= M[i][j] != (i == j ? 1.f : 0.f);
  Send(ctx, base, g_bs, bs_on, [&](Params& p) {
    p.Float(k_target, 0.25f);
    p.Float(k_saturation, std::max(0.f, sat));
    p.Float(k_brightness, std::max(0.f, bri));
    p.Float(k_contrast, std::max(0.05f, con));
    p.Float(QbKey("hue"), 1.f);
    p.FArr(QbKey("red_mix"), {M[0][0], M[0][1], M[0][2], 0.f});
    p.FArr(QbKey("green_mix"), {M[1][0], M[1][1], M[1][2], 0.f});
    p.FArr(QbKey("blue_mix"), {M[2][0], M[2][1], M[2][2], 0.f});
  });

  // ---- vineta: negra (o del color pedido), iris, foco, o roja de "peligro" ----
  float vig = lk_vig, vsize = 0.5f, vsoft = 0.5f, col[3] = {0, 0, 0};
  for (const auto& f : fx) {
    if (f.type == "vignette" && f.k >= vig) {
      vig = f.k; vsize = f.P("size", 0.5f); vsoft = f.P("soft", 0.5f);
      col[0] = f.P("colr", 0); col[1] = f.P("colg", 0); col[2] = f.P("colb", 0);
    }
  }
  vig = std::max({vig, 0.8f * Get(fx, "heartbeat"), 0.7f * Get(fx, "bleach")});
  float inner = 1.3f - 1.4f * vsize * vig, outer = inner + 0.1f + 1.0f * vsoft;
  if ((a = Get(fx, "iris", &el, &left))) {
    const float k = std::min({1.f, el / 0.6f, left / 0.6f});
    const float r = 1.3f * k * k * (3 - 2 * k);
    inner = std::min(inner, r); outer = std::min(outer, r + 0.05f); vig = std::max(vig, a);
  }
  if ((a = Get(fx, "spotlight"))) { inner = std::min(inner, 0.35f); outer = std::min(outer, 0.6f); vig = std::max(vig, a); }
  const float danger = Get(fx, "danger");
  float alpha = 1;
  if (danger > 0) {
    alpha = danger * (0.6f + 0.4f * std::sin(T * 6.f));
    col[0] = 0.9f; col[1] = 0.02f; col[2] = 0.05f;
    inner = std::min(inner, 0.55f); outer = std::min(outer, 1.25f); vig = std::max(vig, danger);
  }
  Send(ctx, base, g_vig, vig > 0, [&](Params& p) {
    p.Int(k_AspectCorrection, 1);
    p.Float(k_inner_radius, std::max(0.f, inner));
    p.Float(k_outer_radius, std::max(inner + 0.02f, outer));
    p.Float(QbKey("alpha"), alpha);
    p.FArr(QbKey("Color"), {col[0], col[1], col[2], 1.f});
  });

  // ---- profundidad de campo (valores como los del juego: metros, fuerza 0..1) ----
  float strength = 0, nb = 0, ne = 0, fb = 0, fe = 0.5f;
  for (const auto& f : fx) {
    float s = 0, a0 = 0, a1 = 0, b0 = 0, b1 = 0;
    if (f.type == "blur") { s = f.k; a0 = 0; a1 = 0; b0 = 0; b1 = 0.5f; }
    else if (f.type == "tiltshift") { s = f.k; a0 = 0; a1 = 2.5f; b0 = 6.f; b1 = 12.f; }
    else if (f.type == "rackfocus") {
      const float d = std::min(1.f, f.el / std::max(0.3f, f.el + f.left));
      const float from = f.P("from", 1.f), to = f.P("to", 12.f), m = from + (to - from) * Smooth01(d);
      s = f.k; a0 = std::max(0.f, m - 3.f); a1 = std::max(0.f, m - 0.5f); b0 = m + 0.5f; b1 = m + 5.f;
    } else if (f.type == "focus") {
      const float d = f.P("dist", 6.f), rg = std::max(0.2f, f.P("range", 3.f));
      s = f.k * f.P("strength", 0.8f);
      a1 = std::max(0.f, d - rg / 2); a0 = std::max(0.f, a1 - 2.f); b0 = d + rg / 2; b1 = b0 + 4.f;
    }
    if (s > strength) { strength = s; nb = a0; ne = a1; fb = b0; fe = b1; }
  }
  Send(ctx, base, g_dof, strength > 0, [&](Params& p) {
    p.Float(k_strength, std::min(1.f, strength));
    p.Float(k_nearblurbegin, nb);
    p.Float(k_nearblurend, ne);
    p.Float(k_farblurbegin, fb);
    p.Float(k_farblurend, fe);
  });

  // ---- glow / bloom (formato del juego: subtract = umbral, colormodulate = color * intensidad, w = tamano) ----
  float bl = lk_bloom, thr = 0.3f, inten = 1.2f, rad = 3.f, bc[3] = {1, 1, 1};
  for (const auto& f : fx) {
    if ((f.type == "glow" || f.type == "bloom") && f.k >= bl) {
      bl = f.k;
      thr = f.P("threshold", f.type == "glow" ? 0.3f : 0.45f);
      inten = f.P("intensity", f.type == "glow" ? 1.2f : 0.8f);
      rad = f.P("radius", 3.f);
      bc[0] = f.P("colr", 1); bc[1] = f.P("colg", 1); bc[2] = f.P("colb", 1);
    }
  }
  Send(ctx, base, g_bloom, bl > 0, [&](Params& p) {
    p.Int(QbKey("fullscreen"), 1);
    p.Int(QbKey("materialfilter"), 1);
    p.Float(k_saturation, 1.f);
    const float sub = std::clamp(thr + (1 - bl) * (1 - thr) * 0.8f, 0.f, 1.f);   // menos intensidad = umbral mas alto
    p.FArr(QbKey("subtract"), {sub, sub, sub, 1.f});
    const float m = inten * bl;
    p.FArr(QbKey("colormodulate"), {bc[0] * m, bc[1] * m, bc[2] * m, std::clamp(rad, 0.25f, 4.f)});
  });

  // ---- grano de pelicula ----
  float grain = lk_grain, gsize = 0;
  for (const auto& f : fx) {
    if (f.type == "grain" && f.k >= grain) { grain = f.k; gsize = f.P("size", 0); }
    if (f.type == "vhs") grain = std::max(grain, 0.7f * f.k);
  }
  Send(ctx, base, g_noise, grain > 0, [&](Params& p) {
    p.Float(QbKey("Intensity"), 0.25f * grain);
    p.FArr(QbKey("Color"), {1.f, 1.f, 1.f, 1.f});
    if (gsize > 0) p.FArr(QbKey("UV"), {8.f / gsize, 6.f / gsize, 0.f, 0.f});
  });

  // ---- desenfoque de movimiento (preset del juego) ----
  const float mb = Get(fx, "motionblur");
  Send(ctx, base, g_mblur, mb > 0, [&](Params& p) {
    p.Float(QbKey("Samples"), 9.f);
    p.Float(QbKey("spacing"), 5.f);
    p.Float(QbKey("RotationScale"), 1.f);
    p.Float(QbKey("Dir"), 8.f);
    p.Float(QbKey("ObjectMotionBlurMultiplier"), 1.25f * mb);
    p.Float(QbKey("CameraMotionBlurMultiplier"), 1.25f * mb);
    p.Key(QbKey("SkinMotionBlur"), QbKey("CurrentPos"));
    p.Int(QbKey("CameraMotion"), 1);
    p.Float(QbKey("threshold"), 0.f);
    p.Float(QbKey("NearPlane"), 0.f);
    p.Float(QbKey("ShellSize"), 0.01f);
    p.Float(QbKey("ForwardExtrusion"), 0.f);
    p.Float(QbKey("BackwardExtrusion"), 1.f);
    p.Float(QbKey("alpha"), 1.f);
    p.Float(QbKey("AlphaClamp"), 1.f);
  });

  // ---- lente (experimental, no verificado) ----
  const float fish = Get(fx, "fisheye"), wob = Get(fx, "wobble");
  float shock_el = 0;
  const float shock = Get(fx, "shockwave", &shock_el);
  Send(ctx, base, g_fish, fish > 0 || wob > 0 || shock > 0, [&](Params& p) {
    float ws = 0.6f * fish, wx = 0, wy = 0, sc = 1;
    if (wob > 0) { wx = 0.03f * wob * std::sin(T * 2.1f); wy = 0.03f * wob * std::sin(T * 1.7f + 1); ws += 0.25f * wob; }
    if (shock > 0) {
      const float e = std::max(0.f, 1 - shock_el / 0.7f);
      ws += 0.8f * shock * e * std::sin(shock_el * 25.f);
      sc = 1 - 0.08f * shock * e;
    }
    p.Float(QbKey("warp_strength"), ws);
    p.Float(QbKey("warp_x"), wx);
    p.Float(QbKey("warp_y"), wy);
    p.Float(QbKey("scale_x"), sc);
    p.Float(QbKey("scale_Y"), sc);
  });

  // ---- rayos de luz (preset del juego con la luz delante y arriba de la camara) ----
  const edfx::FxState* gf = nullptr;
  for (const auto& f : fx) if (f.type == "godrays") gf = &f;
  Send(ctx, base, g_god, gf != nullptr, [&](Params& p) {
    const float k = gf ? gf->k : 0.f;
    float cp[3] = {0, 5, 0}, cf[3] = {0, 0, -1}, cu[3] = {0, 1, 0}, cs[3] = {1, 0, 0};
    uint32_t cam = 0;
    if (edfx::MainCamera(cam)) {
      uint8_t raw[96];
      if (CopyOut(base, raw, cam + 112, 96))
        for (int j = 0; j < 3; ++j) { cp[j] = BeF(raw + 4 * j); cs[j] = BeF(raw + 16 + 4 * j); cu[j] = BeF(raw + 32 + 4 * j); cf[j] = BeF(raw + 48 + 4 * j); }
    }
    const float h = gf ? gf->P("height", 12.f) : 12.f, sd = gf ? gf->P("side", 0.f) : 0.f;
    float lp[3];
    for (int j = 0; j < 3; ++j) lp[j] = cp[j] + cf[j] * 40.f + cu[j] * h + cs[j] * sd;
    const float cr = gf ? gf->P("colr", 1.f) : 1.f, cg = gf ? gf->P("colg", 0.96f) : 1.f, cb = gf ? gf->P("colb", 0.87f) : 1.f;
    p.FArr(QbKey("colormodulate"), {cr, cg, cb, 3.f * k});
    p.Float(QbKey("depthmodulate"), 0.f);
    p.Float(QbKey("steps"), 48.f);
    p.Float(QbKey("Range"), 1.f);
    p.Float(QbKey("fadeout_begin"), 90.f);
    p.Float(QbKey("fadeout_end"), 150.f);
    p.FArr(QbKey("skycolor"), {0.f, 0.f, 0.f, 0.f});
    p.FArr(QbKey("lightposition"), {lp[0], lp[1], lp[2], 1.f});
    p.Int(QbKey("cliplightposition"), 0);
    p.Key(QbKey("lightsource"), QbKey("object"));
  });

  // ---- convolucion (experimental) ----
  const float sh = Get(fx, "sharpen"), em = Get(fx, "emboss"), ed = Get(fx, "edges"), co = Get(fx, "comic");
  Send(ctx, base, g_conv, sh > 0 || em > 0 || ed > 0 || co > 0, [&](Params& p) {
    const float* K = kSharpen;
    float k = sh;
    if (em > k) { K = kEmboss; k = em; }
    if (ed > k) { K = kEdges; k = ed; }
    if (co > k) { K = kSharpen; k = std::min(1.f, co * 1.8f); }
    std::vector<float> v = {3.f, 3.f};
    for (int i = 0; i < 9; ++i) v.push_back(kIdentity[i] + (K[i] - kIdentity[i]) * k);
    p.FArr(QbKey("kernel"), v);
    p.Float(QbKey("scale"), 1.f);
  });

  static bool viewport_logged = false;
  if (!viewport_logged) {
    viewport_logged = true;
    std::string list;
    for (const auto& v : Viewports(base)) {
      char b[48];
      std::snprintf(b, sizeof(b), " [id %08X vp %08X]", v.id, v.vp);
      list += b;
    }
    TRACE("[NativeFX] viewports del motor:" << list);
  }
}

}  // namespace re_wor::de::nativefx

// Hook de la busqueda de estilos de viewport (viewport_Params.styles.<estilo>): nuestras franjas usan estilos
// propios que no existen en los scripts del juego.
extern "C" void __imp__sub_823E7858(PPCContext& __restrict ctx, uint8_t* base);
extern "C" REX_FUNC(sub_823E7858) {
  if (const uint32_t st = re_wor::de::nativefx::StyleFor(ctx.r3.u32)) {
    ctx.r3.u64 = st;
    return;
  }
  __imp__sub_823E7858(ctx, base);
}
