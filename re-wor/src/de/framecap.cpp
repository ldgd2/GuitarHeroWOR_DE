// CAPTURA DE FRAMES (render nativo, fase 3a: docs/RENDER_NATIVO.md). Diagnostico, apagado salvo con la variable de
// entorno REWOR_FRAMECAP=1.
//
// Graba un frame entero tal como lo pide el D3D de la 360, para reproducirlo fuera del juego con el render propio
// (tools: reproductor de capturas). Se dispara creando el archivo cache/framecap/capturar (relativo al directorio de
// trabajo del juego): en el siguiente VdSwap empieza a grabar y en el otro termina, y escribe
// cache/framecap/frame_<n>.xfc.
//
// Formato: "XFC1" + version + dispositivo, y luego trozos [etiqueta u32][tamano u32][datos], en orden:
//   SHDR  shader cargado (IM_LOAD): tipo (0 vertices, 1 pixeles), direccion fisica, palabras, microcodigo tal cual
//   MEM   memoria fisica que usa la llamada siguiente: direccion, bytes, contenido tal cual (big-endian)
//         (vertices y texturas de las constantes de fetch que usan sus shaders, indices, LOAD_ALU_CONSTANT)
//   CALL  llamada del D3D que escribe ordenes: funcion, dispositivo, r3..r10, copia de registros del dispositivo
//         antes de la llamada (dev+1152, 9712 bytes), mascaras de sucio (dev+0, 48 bytes) y sus tramos de PM4
//   SWAP  fin del frame
// Lo que el juego escribe en la GPU sale del dispositivo (copia de registros) y de los tramos de PM4 de cada
// llamada; los shaders y la memoria se copian una vez por frame (la memoria de vertices / indices otra vez si cambia).
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <windows.h>

#include "core/crash_handler.hpp"
#include "de/d3d_tap.hpp"
#include "de/nativegfx_device_map.h"

namespace re_wor::de::framecap {
namespace {

using d3dtap::Phys;
using d3dtap::Rd;
namespace fs = std::filesystem;

constexpr uint32_t kTag(const char (&s)[5]) {
  return uint32_t(uint8_t(s[0])) | uint32_t(uint8_t(s[1])) << 8 | uint32_t(uint8_t(s[2])) << 16 |
         uint32_t(uint8_t(s[3])) << 24;
}
constexpr uint32_t kMaxRange = 64u << 20;   // un rango de memoria mas grande es basura (constante de fetch vieja)

std::atomic<int> g_enabled{-1};
std::atomic_flag g_lock = ATOMIC_FLAG_INIT;
bool g_recording = false;
uint32_t g_frame_no = 0;
std::vector<uint8_t> g_out;

struct ShaderRef {
  uint32_t addr = 0, ndw = 0;   // direccion fisica (0 = ninguno)
};
ShaderRef g_active[2];   // ultimo IM_LOAD de vertices / pixeles (se sigue siempre, no solo al grabar)

struct FetchUse {
  uint32_t vfetch[3] = {};   // 96 constantes de fetch de vertices
  uint32_t tfetch = 0;       // 32 de texturas
};
std::map<std::pair<uint32_t, uint32_t>, FetchUse> g_fetch_cache;   // (direccion, palabras) -> constantes usadas

// por captura
std::set<std::pair<uint32_t, uint32_t>> g_emitted_shaders;
std::map<std::pair<uint32_t, uint32_t>, uint64_t> g_emitted_mem;   // (direccion, bytes) -> hash
std::set<std::pair<uint32_t, uint32_t>> g_emitted_tex;
uint32_t g_n_calls = 0, g_n_draws = 0, g_n_mem = 0;
uint64_t g_mem_bytes = 0;

thread_local std::vector<uint8_t> t_pre;   // copia del dispositivo antes de la llamada

void Put32(uint32_t v) {
  const size_t n = g_out.size();
  g_out.resize(n + 4);
  std::memcpy(&g_out[n], &v, 4);
}
void PutBytes(const void* p, size_t n) {
  const size_t o = g_out.size();
  g_out.resize(o + n);
  std::memcpy(&g_out[o], p, n);
}
size_t BeginChunk(uint32_t tag) {
  Put32(tag);
  Put32(0);
  return g_out.size();
}
void EndChunk(size_t start) {
  const uint32_t size = uint32_t(g_out.size() - start);
  std::memcpy(&g_out[start - 4], &size, 4);
}

uint64_t Hash(const uint8_t* p, size_t n) {   // FNV-1a de 64 bits por palabras de 8 bytes
  uint64_t h = 1469598103934665603ull;
  size_t i = 0;
  for (; i + 8 <= n; i += 8) {
    uint64_t w;
    std::memcpy(&w, p + i, 8);
    h = (h ^ w) * 1099511628211ull;
  }
  for (; i < n; ++i) h = (h ^ p[i]) * 1099511628211ull;
  return h;
}

// --------------------------------------------------------------------------------------------- shaders
// constantes de fetch que usa un microcodigo: recorre los exec del control de flujo (ver rex/graphics/format/ucode.h)
FetchUse ScanFetches(const uint8_t* be, uint32_t ndw) {
  FetchUse u;
  std::vector<uint32_t> d(ndw);
  for (uint32_t i = 0; i < ndw; ++i) d[i] = Rd(be, 4 * i);
  uint32_t limit = ndw / 3;   // las instrucciones empiezan donde apunta el primer exec
  for (uint32_t k = 0; k < limit && 3 * k + 2 < ndw; ++k) {
    const uint32_t w0 = d[3 * k], w1 = d[3 * k + 1], w2 = d[3 * k + 2];
    const uint32_t cf[2][2] = {{w0, w1 & 0xFFFF}, {(w1 >> 16) | (w2 << 16), w2 >> 16}};
    for (const auto& c : cf) {
      const uint32_t op = (c[1] >> 12) & 0xF;
      const bool exec = (op >= 1 && op <= 6) || op == 13 || op == 14;
      if (!exec) continue;
      const uint32_t addr = c[0] & 0xFFF, count = (c[0] >> 12) & 7, seq = (c[0] >> 16) & 0xFFF;
      limit = std::min(limit, addr);
      for (uint32_t j = 0; j < count; ++j) {
        if (!((seq >> (2 * j)) & 1)) continue;   // ALU
        const uint32_t at = (addr + j) * 3;
        if (at >= ndw) continue;
        const uint32_t f = d[at], fop = f & 0x1F, idx = (f >> 20) & 0x1F;
        if (fop == 0) {
          const uint32_t n = idx * 3 + ((f >> 25) & 3);
          u.vfetch[n >> 5] |= 1u << (n & 31);
        } else if (fop == 1) {
          u.tfetch |= 1u << idx;
        }
      }
    }
  }
  return u;
}

const FetchUse& Fetches(const uint8_t* base, const ShaderRef& s) {
  const auto key = std::make_pair(s.addr, s.ndw);
  auto it = g_fetch_cache.find(key);
  if (it == g_fetch_cache.end()) it = g_fetch_cache.emplace(key, ScanFetches(Phys(base, s.addr), s.ndw)).first;
  return it->second;
}

void EmitShader(const uint8_t* base, uint32_t type, const ShaderRef& s) {
  if (!s.addr || !g_emitted_shaders.insert({s.addr, s.ndw}).second) return;
  const size_t c = BeginChunk(kTag("SHDR"));
  Put32(type);
  Put32(s.addr);
  Put32(s.ndw);
  PutBytes(Phys(base, s.addr), size_t(s.ndw) * 4);
  EndChunk(c);
}

// --------------------------------------------------------------------------------------------- memoria
void EmitMem(const uint8_t* base, uint32_t addr, uint32_t bytes, bool static_texture) {
  addr &= 0x1FFFFFFF;
  if (!bytes || bytes > kMaxRange) return;
  bytes = std::min(bytes, 0x20000000u - addr);
  const auto key = std::make_pair(addr, bytes);
  if (static_texture) {   // texturas: una vez por frame (las de render las hace el propio reproductor)
    if (!g_emitted_tex.insert(key).second) return;
  } else {
    const uint64_t h = Hash(Phys(base, addr), bytes);
    auto it = g_emitted_mem.find(key);
    if (it != g_emitted_mem.end() && it->second == h) return;
    g_emitted_mem[key] = h;
  }
  const size_t c = BeginChunk(kTag("MEM "));
  Put32(addr);
  Put32(bytes);
  PutBytes(Phys(base, addr), bytes);
  EndChunk(c);
  ++g_n_mem;
  g_mem_bytes += bytes;
}

// bytes por bloque, ancho y alto del bloque de cada formato de textura (xenos::TextureFormat)
void FormatBlock(uint32_t fmt, uint32_t& bpb, uint32_t& bw, uint32_t& bh) {
  bw = bh = 1;
  switch (fmt) {
    case 2: case 8: case 9: bpb = 1; break;                                    // 8
    case 3: case 4: case 5: case 10: case 15: case 24: case 27: case 30:      // 16 bits
    case 41: case 43: case 46: case 47: bpb = 2; break;
    case 18: case 58: case 59: case 60: case 61: case 51: bpb = 8, bw = bh = 4; break;   // DXT1, DXT3A/5A, CTX1
    case 19: case 20: case 49: case 52: case 53: bpb = 16, bw = bh = 4; break;            // DXT2_3, DXT4_5, DXN
    case 26: case 29: case 32: case 34: case 37: case 50: case 54: case 55: case 56: bpb = 8; break;   // 64 bits
    case 35: case 38: bpb = 16; break;                                         // 128 bits
    case 57: bpb = 12; break;
    case 11: case 12: bpb = 4, bw = 2; break;                                  // YUV empaquetado
    default: bpb = 4; break;                                                   // 32 bits
  }
}

void EmitTexture(const uint8_t* base, const uint32_t fc[6]) {
  if ((fc[0] & 3) != 2) return;
  const uint32_t pitch = ((fc[0] >> 22) & 0x1FF) * 32;
  const bool tiled = (fc[0] >> 31) != 0;
  const uint32_t fmt = fc[1] & 0x3F, addr = fc[1] & 0xFFFFF000;
  const uint32_t dim = (fc[5] >> 9) & 3, mip_addr = fc[5] & 0xFFFFF000;
  uint32_t w, h, layers;
  if (dim == 2) {   // 3D
    w = (fc[2] & 0x7FF) + 1, h = ((fc[2] >> 11) & 0x7FF) + 1, layers = ((fc[2] >> 22) & 0x3FF) + 1;
    if (tiled) layers = (layers + 3) & ~3u;
  } else {
    w = (fc[2] & 0x1FFF) + 1, h = ((fc[2] >> 13) & 0x1FFF) + 1;
    layers = dim == 3 ? 6 : (dim == 1 && ((fc[1] >> 10) & 1)) ? ((fc[2] >> 26) & 0x3F) + 1 : 1;
    if (dim == 0) h = 1;
  }
  uint32_t bpb, bw, bh;
  FormatBlock(fmt, bpb, bw, bh);
  uint32_t wb = (std::max(pitch, w) + bw - 1) / bw, hb = (h + bh - 1) / bh;
  if (tiled) wb = (wb + 31) & ~31u, hb = (hb + 31) & ~31u;
  const uint64_t slice = (uint64_t(wb) * hb * bpb + 4095) & ~4095ull;
  const uint64_t bytes = slice * layers;
  if (bytes > kMaxRange) return;
  EmitMem(base, addr, uint32_t(bytes), true);
  if (mip_addr) EmitMem(base, mip_addr, uint32_t(bytes), true);   // cadena de mips: cota generosa
}

// --------------------------------------------------------------------------------------------- llamada
struct Collector : d3dtap::Pm4Visitor {
  const uint8_t* base = nullptr;
  std::vector<std::pair<uint32_t, uint32_t>> regs;   // escrituras de registro en orden
  std::vector<std::pair<uint32_t, ShaderRef>> loads;   // IM_LOAD (tipo, shader)
  std::vector<std::pair<uint32_t, uint32_t>> mem;    // rangos que piden los paquetes (indices, constantes)
  uint32_t draws = 0;
  void Reg(uint32_t reg, uint32_t v) override { regs.push_back({reg, v}); }
  void Packet3(uint32_t op, uint32_t p, uint32_t n) override {
    if (op == 0x27 && n >= 2) {   // IM_LOAD: direccion | tipo, inicio << 16 | palabras
      const uint32_t a = Rd(base, p), sz = Rd(base, p + 4);
      loads.push_back({a & 3, ShaderRef{a & ~3u, sz & 0xFFFF}});
    } else if (op == 0x2B && n >= 2) {   // IM_LOAD_IMMEDIATE: microcodigo dentro del paquete (tipo, inicio / palabras)
      // el buffer de ordenes esta en memoria fisica (0xA0000000 + fisica): el shader se lee de ahi mismo
      const uint32_t t = Rd(base, p), sz = std::min(Rd(base, p + 4) & 0xFFFF, n - 2);
      loads.push_back({t & 3, ShaderRef{(p + 8) & 0x1FFFFFFF, sz}});
    } else if (op == 0x22 && n >= 2) {   // DRAW_INDX: viz, iniciador, [base, tamano de indices]
      ++draws;
      const uint32_t init = Rd(base, p + 4);
      if (((init >> 6) & 3) == 0 && n >= 4) {   // indices en memoria
        const uint32_t isz = ((init >> 11) & 1) ? 4 : 2;
        const uint32_t ib = Rd(base, p + 8) & ~(isz - 1), words = Rd(base, p + 12) & 0xFFFFFF;
        mem.push_back({ib, words * isz});
      }
    } else if (op == 0x36) {   // DRAW_INDX_2: indices dentro del paquete
      ++draws;
    } else if (op == 0x2F && n >= 3) {   // LOAD_ALU_CONSTANT: direccion, tipo / indice, palabras
      mem.push_back({Rd(base, p) & 0x3FFFFFFF, (Rd(base, p + 8) & 0xFFF) * 4});
    }
  }
};

// valor de un registro tras la llamada: lo ultimo escrito en su PM4 o, si no, la copia del dispositivo de antes
uint32_t RegAfter(const Collector& col, uint32_t reg) {
  for (auto it = col.regs.rbegin(); it != col.regs.rend(); ++it)
    if (it->first == reg) return it->second;
  for (const auto& b : nativegfx::kDeviceRegBlocks)
    if (reg >= b.reg && reg < b.reg + b.count) {
      const uint32_t off = b.offset + 4 * (reg - b.reg) - d3dtap::kShadowOffset;
      return off + 4 <= t_pre.size() ? Rd(t_pre.data(), off) : 0;
    }
  return 0;
}

void EmitFetchMemory(const uint8_t* base, const Collector& col) {
  for (uint32_t type = 0; type < 2; ++type) {
    if (!g_active[type].addr) continue;
    const FetchUse& u = Fetches(base, g_active[type]);
    for (uint32_t n = 0; n < 96; ++n) {
      if (!((u.vfetch[n >> 5] >> (n & 31)) & 1)) continue;
      const uint32_t d0 = RegAfter(col, 0x4800 + 2 * n), d1 = RegAfter(col, 0x4800 + 2 * n + 1);
      if ((d0 & 3) == 3) EmitMem(base, d0 & ~3u, ((d1 >> 2) & 0xFFFFFF) * 4, false);
    }
    for (uint32_t n = 0; n < 32; ++n) {
      if (!((u.tfetch >> n) & 1)) continue;
      uint32_t fc[6];
      for (uint32_t i = 0; i < 6; ++i) fc[i] = RegAfter(col, 0x4800 + 6 * n + i);
      EmitTexture(base, fc);
    }
  }
}

void StartRecording(const uint8_t* base) {
  g_out.clear();
  g_emitted_shaders.clear();
  g_emitted_mem.clear();
  g_emitted_tex.clear();
  g_n_calls = g_n_draws = g_n_mem = 0;
  g_mem_bytes = 0;
  PutBytes("XFC1", 4);
  Put32(1);   // version
  Put32(d3dtap::kShadowOffset);
  Put32(d3dtap::kShadowBytes);
  EmitShader(base, 0, g_active[0]);   // los shaders activos al empezar el frame
  EmitShader(base, 1, g_active[1]);
  g_recording = true;
}

void FinishRecording() {
  const size_t c = BeginChunk(kTag("SWAP"));
  EndChunk(c);
  g_recording = false;
  std::error_code ec;
  fs::create_directories("cache/framecap", ec);
  const std::string path = "cache/framecap/frame_" + std::to_string(++g_frame_no) + ".xfc";
  if (FILE* f = std::fopen(path.c_str(), "wb")) {
    std::fwrite(g_out.data(), 1, g_out.size(), f);
    std::fclose(f);
    TRACE("[FrameCap] " << path << ": " << g_n_calls << " llamadas, " << g_n_draws << " draws, "
                        << g_emitted_shaders.size() << " shaders, " << g_n_mem << " bloques de memoria ("
                        << (g_mem_bytes >> 20) << " MB), " << (g_out.size() >> 20) << " MB en total");
  } else {
    TRACE("[FrameCap] no se pudo escribir " << path);
  }
  g_out.clear();
  g_out.shrink_to_fit();
}

struct Lock {
  Lock() {
    while (g_lock.test_and_set(std::memory_order_acquire)) {
    }
  }
  ~Lock() { g_lock.clear(std::memory_order_release); }
};

}  // namespace

bool Enabled() {
  int e = g_enabled.load(std::memory_order_relaxed);
  if (e < 0) {
    char v[4] = {};
    e = (GetEnvironmentVariableA("REWOR_FRAMECAP", v, sizeof(v)) > 0 && v[0] == '1') ? 1 : 0;
    if (e) TRACE("[FrameCap] captura de frames activa: crear cache/framecap/capturar para grabar el siguiente frame");
    g_enabled.store(e, std::memory_order_relaxed);
  }
  return e != 0;
}

void BeforeCall(const uint8_t* base, const d3dtap::CallInfo& c) {
  if (!g_recording) return;
  t_pre.resize(d3dtap::kShadowBytes + d3dtap::kDirtyBytes);
  std::memcpy(t_pre.data(), base + c.dev + d3dtap::kShadowOffset, d3dtap::kShadowBytes);
  std::memcpy(t_pre.data() + d3dtap::kShadowBytes, base + c.dev, d3dtap::kDirtyBytes);
}

void OnCall(const uint8_t* base, const d3dtap::CallInfo& c) {
  if (!c.spans_ok) return;
  Lock lock;
  Collector col;
  col.base = base;
  d3dtap::Walk(base, *c.spans, col);
  for (const auto& l : col.loads)
    if (l.first < 2) g_active[l.first] = l.second;
  if (!g_recording) return;

  for (const auto& l : col.loads)
    if (l.first < 2) EmitShader(base, l.first, l.second);
  if (col.draws) EmitFetchMemory(base, col);
  for (const auto& m : col.mem) EmitMem(base, m.first, m.second, false);

  const size_t ch = BeginChunk(kTag("CALL"));
  Put32(c.func);
  Put32(c.dev);
  for (uint32_t a : c.args) Put32(a);
  Put32(uint32_t(c.spans->size()));
  PutBytes(t_pre.data(), t_pre.size());
  for (const auto& sp : *c.spans) {
    const uint32_t n = (sp.end - sp.begin) / 4;
    Put32(sp.begin + 4);
    Put32(n);
    PutBytes(base + sp.begin + 4, size_t(n) * 4);
  }
  EndChunk(ch);
  ++g_n_calls;
  g_n_draws += col.draws;
}

void OnSwap(const uint8_t* base) {
  if (!Enabled()) return;
  Lock lock;
  if (g_recording) {
    FinishRecording();
    return;
  }
  std::error_code ec;
  if (fs::exists("cache/framecap/capturar", ec)) {
    fs::remove("cache/framecap/capturar", ec);
    StartRecording(base);
    TRACE("[FrameCap] grabando el frame siguiente");
  }
}

}  // namespace re_wor::de::framecap
