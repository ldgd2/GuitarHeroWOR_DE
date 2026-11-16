// =============================================================================
// voice - API de voz de Xbox (XamVoice*) con el microfono del PC.
//
// WoR lee el microfono con el motor de voz XHV de Xbox. El SDK no tiene voz (XamVoiceCreate devuelve "acceso
// denegado"), asi que el juego nunca veia un microfono. Aqui se implementa con mic_capture:
//   - XamVoiceCreate(usuario, 0xF, &voz): si el jugador (= mando) tiene microfono, da una voz.
//   - XamVoiceHeadsetPresent(voz): 1 si el microfono esta abierto.
//   - XamVoiceSubmitPacket(voz, captura, paquete): el juego entrega buffers; los de captura se llenan con el
//     microfono y los de salida (auriculares) se dan por reproducidos, al ritmo real del audio.
//   - XamVoiceClose(voz).
// Paquete (28 bytes, big endian): +0 estado (259 pendiente, 0 hecho), +4 bytes hechos, +8 buffer, +12 tamano,
// +20 formato. El tamano es la mitad de las muestras de 16 kHz del buffer (160 = 20 ms).
//   - formato 1 (el que usa WoR): ADPCM G.726 de 4 bits a 16 kHz. Al terminar el paquete el juego lo
//     descodifica con sub_8275F2C0 (modo 1) en dos mitades. En vez de codificar, se guarda el PCM del
//     microfono y el gancho de sub_8275F2C0 lo entrega tal cual cuando descodifica ese buffer: sin perdidas.
//   - formato 2: mu-law (G.711) de 8 kHz; el juego lo pasa a 16 kHz (tabla en 0x8289FD40).
// =============================================================================
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <deque>
#include <map>
#include <mutex>
#include <thread>
#include <vector>

#include "core/crash_handler.hpp"
#include "de/de_config.hpp"
#include "generated/re_wor_pch.h"
#include "input/mic_capture.hpp"

namespace {

constexpr uint32_t kHandleBase = 0x4D494330;   // 'MIC0' + jugador
constexpr uint32_t kStatusPending = 259;
constexpr uint32_t kErrorNoVoice = 0x80070005;  // lo que devolvia el SDK

using Clock = std::chrono::steady_clock;

struct Packet {
  uint32_t addr = 0;
  bool capture = false;
  Clock::time_point due;
};

std::mutex g_mutex;
std::deque<Packet> g_pending[re_wor::de::kMaxPlayers][2];   // [jugador][0 salida, 1 captura]
std::atomic<uint8_t*> g_base{nullptr};
std::atomic<bool> g_worker{false};
std::atomic<int> g_logged{0};
// PCM de 16 kHz de cada buffer de captura en formato 1, para el gancho del descodificador
std::mutex g_pcm_mutex;
std::map<uint32_t, std::vector<int16_t>> g_pcm_by_buffer;

int PlayerOf(uint32_t handle) {
  const uint32_t p = handle - kHandleBase;
  return p < static_cast<uint32_t>(re_wor::de::kMaxPlayers) ? static_cast<int>(p) : -1;
}

uint8_t LinearToMuLaw(int16_t pcm) {
  constexpr int kBias = 0x84, kClip = 32635;
  int sign = (pcm >> 8) & 0x80;
  int s = sign ? -static_cast<int>(pcm) : pcm;
  if (s > kClip) s = kClip;
  s += kBias;
  int exponent = 7;
  for (int mask = 0x4000; (s & mask) == 0 && exponent > 0; mask >>= 1) exponent--;
  const int mantissa = (s >> (exponent + 3)) & 0x0F;
  return static_cast<uint8_t>(~(sign | (exponent << 4) | mantissa));
}

// Duracion del audio del paquete: el tamano son muestras de 8 kHz
Clock::duration PacketDuration(uint8_t* base, uint32_t addr) {
  const uint32_t n = REX_LOAD_U32(addr + 12);
  return std::chrono::microseconds(static_cast<int64_t>(n) * 1000000 / 8000);
}

void Complete(uint8_t* base, const Packet& pk, int player, bool have_mic) {
  const uint32_t buf = REX_LOAD_U32(pk.addr + 8);
  const uint32_t n = REX_LOAD_U32(pk.addr + 12);
  const uint32_t fmt = REX_LOAD_U32(pk.addr + 20);
  if (pk.capture && buf && n) {
    std::vector<int16_t> pcm(static_cast<size_t>(n) * 2, 0);
    if (have_mic) re_wor::input::MicRead(player, pcm.data(), pcm.size());
    uint8_t* dst = base + buf;
    if (fmt == 1) {
      std::memset(dst, 0, n);   // los bytes ADPCM no se usan: el gancho entrega el PCM
      std::lock_guard lock(g_pcm_mutex);
      if (g_pcm_by_buffer.size() > 64) g_pcm_by_buffer.clear();   // buffers que el juego no llego a descodificar
      g_pcm_by_buffer[buf] = std::move(pcm);
    } else if (fmt == 2) {
      for (uint32_t i = 0; i < n; i++) dst[i] = LinearToMuLaw(static_cast<int16_t>((pcm[2 * i] + pcm[2 * i + 1]) / 2));
    } else {
      // 16 bits big endian a 16 kHz (2n muestras = 4n bytes)
      for (size_t i = 0; i < pcm.size(); i++) {
        dst[2 * i] = static_cast<uint8_t>(static_cast<uint16_t>(pcm[i]) >> 8);
        dst[2 * i + 1] = static_cast<uint8_t>(pcm[i]);
      }
    }
  }
  REX_STORE_U32(pk.addr + 4, n);
  std::atomic_thread_fence(std::memory_order_release);
  REX_STORE_U32(pk.addr + 0, 0);
}

void Worker() {
  while (g_worker) {
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    uint8_t* base = g_base.load();
    if (!base) continue;
    std::lock_guard lock(g_mutex);
    const auto now = Clock::now();
    for (int p = 0; p < re_wor::de::kMaxPlayers * 2; p++) {
      auto& q = g_pending[p / 2][p % 2];
      // por orden: captura cuando hay muestras (o con silencio si el micro no da nada a tiempo), salida al
      // terminar su duracion
      while (!q.empty()) {
        const Packet& pk = q.front();
        const bool mic = re_wor::input::MicActive(p / 2);
        if (pk.capture) {
          const uint32_t n = REX_LOAD_U32(pk.addr + 12);
          const bool enough = mic && re_wor::input::MicAvailable(p / 2) >= static_cast<size_t>(n) * 2;
          if (!enough && now < pk.due + std::chrono::milliseconds(60)) break;
          Complete(base, pk, p / 2, mic);
        } else {
          if (now < pk.due) break;
          Complete(base, pk, p / 2, false);
        }
        q.pop_front();
      }
    }
  }
}

void EnsureWorker(uint8_t* base) {
  g_base = base;
  bool expected = false;
  if (g_worker.compare_exchange_strong(expected, true)) {
    std::thread(Worker).detach();
  }
}

}  // namespace

// XamVoiceCreate(usuario, ?, &voz)
extern "C" REX_FUNC(__imp__XamVoiceCreate) {
  const uint32_t user = ctx.r3.u32;
  const uint32_t out = ctx.r5.u32;
  const int player = static_cast<int>(user);
  bool has_mic = false;
  if (player >= 0 && player < re_wor::de::kMaxPlayers) {
    std::lock_guard lock(re_wor::de::Mutex());
    has_mic = !re_wor::de::Get().input.players[player].mic.empty();
  }
  TRACE("[Voz] XamVoiceCreate usuario " << user << " (0x" << std::hex << ctx.r4.u32 << std::dec << ") -> "
                                         << (has_mic ? "microfono" : "sin microfono"));
  if (!has_mic) {
    if (out) REX_STORE_U32(out, 0);
    ctx.r3.u64 = kErrorNoVoice;
    return;
  }
  EnsureWorker(base);
  if (out) REX_STORE_U32(out, kHandleBase + user);
  ctx.r3.u64 = 0;
}

extern "C" REX_FUNC(__imp__XamVoiceHeadsetPresent) {
  const int player = PlayerOf(ctx.r3.u32);
  ctx.r3.u64 = (player >= 0 && re_wor::input::MicActive(player)) ? 1 : 0;
}

extern "C" REX_FUNC(__imp__XamVoiceClose) {
  const int player = PlayerOf(ctx.r3.u32);
  if (player >= 0) {
    std::lock_guard lock(g_mutex);
    for (auto& q : g_pending[player]) {   // los pendientes acaban (sin datos) para que el juego los suelte
      for (auto& pk : q) {
        REX_STORE_U32(pk.addr + 4, 0);
        REX_STORE_U32(pk.addr + 0, 0);
      }
      q.clear();
    }
    TRACE("[Voz] XamVoiceClose jugador " << (player + 1));
  }
  ctx.r3.u64 = 0;
}

// XamVoiceSubmitPacket(voz, captura, paquete)
extern "C" REX_FUNC(__imp__XamVoiceSubmitPacket) {
  const int player = PlayerOf(ctx.r3.u32);
  const bool capture = ctx.r4.u32 != 0;
  const uint32_t addr = ctx.r5.u32;
  if (player < 0 || !addr) {
    ctx.r3.u64 = kErrorNoVoice;
    return;
  }
  if (g_logged < 24) {
    g_logged++;
    TRACE("[Voz] paquete jugador " << (player + 1) << (capture ? " captura" : " salida") << ": buffer 0x" << std::hex
                                   << REX_LOAD_U32(addr + 8) << std::dec << " tamano " << REX_LOAD_U32(addr + 12)
                                   << " formato " << REX_LOAD_U32(addr + 20) << " estado " << REX_LOAD_U32(addr));
  }
  EnsureWorker(base);
  REX_STORE_U32(addr + 0, kStatusPending);
  REX_STORE_U32(addr + 4, 0);
  {
    std::lock_guard lock(g_mutex);
    auto& q = g_pending[player][capture ? 1 : 0];
    // cada paquete empieza cuando acaba el anterior del mismo tipo
    Clock::time_point start = Clock::now();
    if (!q.empty()) start = std::max(start, q.back().due);
    q.push_back({addr, capture, start + PacketDuration(base, addr)});
  }
  ctx.r3.u64 = 0;
}

// Codec de voz del juego: sub_8275F2C0(modo, origen, destino, muestras, estado). Modo 1 = descodificar ADPCM a
// PCM de 16 bits. Si el destino es un buffer de captura que lleno re-wor (o su segunda mitad), se escribe el PCM
// del microfono directamente.
extern "C" void __imp__sub_8275F2C0(PPCContext& __restrict ctx, uint8_t* base);
extern "C" REX_FUNC(sub_8275F2C0) {
  if (ctx.r3.u32 == 1) {
    const uint32_t dst = ctx.r5.u32;
    const uint32_t count = ctx.r6.u32;
    std::vector<int16_t> pcm;
    size_t first = 0;
    {
      std::lock_guard lock(g_pcm_mutex);
      auto it = g_pcm_by_buffer.find(dst);
      if (it == g_pcm_by_buffer.end()) {
        it = g_pcm_by_buffer.find(dst - count * 2);   // segunda mitad
        first = count;
      }
      if (it != g_pcm_by_buffer.end() && it->second.size() >= first + count) {
        pcm.assign(it->second.begin() + first, it->second.begin() + first + count);
        if (first) g_pcm_by_buffer.erase(it);
      }
    }
    if (!pcm.empty()) {
      static std::atomic<int> s_hits{0};
      if (s_hits.fetch_add(1) == 0) TRACE("[Voz] el juego recibe el audio del microfono (PCM 16 kHz, sin ADPCM)");
      uint8_t* out = base + dst;
      for (uint32_t i = 0; i < count; i++) {
        out[2 * i] = static_cast<uint8_t>(static_cast<uint16_t>(pcm[i]) >> 8);
        out[2 * i + 1] = static_cast<uint8_t>(pcm[i]);
      }
      return;
    }
  }
  __imp__sub_8275F2C0(ctx, base);
}
