// VIGIA DE LA CAMARA (re-wor). Averigua QUE funcion del juego escribe la camara del escenario justo antes de
// dibujar, para engancharla. Usa los registros de depuracion del CPU (DR0-DR3): un punto de parada de hardware sobre
// la memoria de la camara (la memoria del Xenon es memoria del PC: base + direccion). Cada escritura del motor salta
// aqui con la pila del hilo; se apuntan la funcion y sus llamadores y, a los pocos segundos, se quita el vigia y se
// escribe en re_wor_trace.log con los nombres sub_XXXXXXXX del PDB (sub_XXXXXXXX = direccion de la funcion en el
// Xbox). Las escrituras propias (edfx) se ignoran.
#include "de/cam_watch.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <sstream>
#include <string>
#include <thread>

#include <windows.h>
#include <tlhelp32.h>
#include <dbghelp.h>

#include "core/crash_handler.hpp"

namespace re_wor::de::camwatch {
namespace {

using Clock = std::chrono::steady_clock;
constexpr int kDepth = 8;
constexpr int kMaxHits = 24;

struct Hit {
  uint64_t frames[kDepth] = {};
  int n = 0;
  uint32_t count = 0;
  uint32_t field = 0;   // que campo (0 pos, 1 up, 2 at, 3 fov)
};
Hit g_hits[kMaxHits];
std::atomic<int> g_nhits{0};
std::atomic_flag g_lock = ATOMIC_FLAG_INIT;
std::atomic<int> g_state{0};   // 0 libre, 1 vigilando, 2 informado
std::atomic<uint32_t> g_total{0};
uint64_t g_addr[4] = {};
thread_local bool t_self = false;
PVOID g_veh = nullptr;
Clock::time_point g_armed_at{};
uint32_t g_cam = 0;

LONG WINAPI Veh(EXCEPTION_POINTERS* ep) {
  if (ep->ExceptionRecord->ExceptionCode != EXCEPTION_SINGLE_STEP) return EXCEPTION_CONTINUE_SEARCH;
  CONTEXT* c = ep->ContextRecord;
  const uint64_t dr6 = c->Dr6;
  if (!(dr6 & 0xF) || g_state.load(std::memory_order_relaxed) != 1) return EXCEPTION_CONTINUE_SEARCH;
  c->Dr6 = 0;
  if (t_self) return EXCEPTION_CONTINUE_EXECUTION;
  g_total.fetch_add(1, std::memory_order_relaxed);
  uint32_t field = 0;
  for (uint32_t i = 0; i < 4; ++i) if (dr6 & (1ull << i)) { field = i; break; }
  // pila: desenrollar desde el contexto del hilo que escribio
  uint64_t fr[kDepth] = {};
  int n = 0;
  CONTEXT u = *c;
  for (; n < kDepth; ++n) {
    fr[n] = u.Rip;
    DWORD64 img = 0;
    PRUNTIME_FUNCTION fe = RtlLookupFunctionEntry(u.Rip, &img, nullptr);
    if (!fe) {
      u.Rip = *reinterpret_cast<uint64_t*>(u.Rsp);
      u.Rsp += 8;
    } else {
      PVOID hd = nullptr;
      DWORD64 est = 0;
      RtlVirtualUnwind(UNW_FLAG_NHANDLER, img, u.Rip, fe, &u, &hd, &est, nullptr);
    }
    if (!u.Rip) { ++n; break; }
  }
  while (g_lock.test_and_set(std::memory_order_acquire)) {
  }
  int k = 0;
  const int nh = g_nhits.load(std::memory_order_relaxed);
  for (; k < nh; ++k)
    if (g_hits[k].field == field && g_hits[k].frames[0] == fr[0] && g_hits[k].frames[1] == fr[1] &&
        g_hits[k].frames[2] == fr[2])
      break;
  if (k < nh) ++g_hits[k].count;
  else if (nh < kMaxHits) {
    Hit& h = g_hits[nh];
    for (int i = 0; i < kDepth; ++i) h.frames[i] = fr[i];
    h.n = n;
    h.count = 1;
    h.field = field;
    g_nhits.store(nh + 1, std::memory_order_relaxed);
  }
  g_lock.clear(std::memory_order_release);
  return EXCEPTION_CONTINUE_EXECUTION;
}

// Pone (o quita, con addr = nullptr) los registros de depuracion en TODOS los hilos del proceso menos este.
// Lo hace un hilo aparte: un hilo no puede cambiar sus propios registros con SetThreadContext.
void SetAll(const uint64_t* addr) {
  std::thread t([addr] {
    const DWORD me = GetCurrentThreadId(), pid = GetCurrentProcessId();
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) return;
    THREADENTRY32 te{};
    te.dwSize = sizeof(te);
    for (BOOL ok = Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te)) {
      if (te.th32OwnerProcessID != pid || te.th32ThreadID == me) continue;
      HANDLE th = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_SET_CONTEXT, FALSE, te.th32ThreadID);
      if (!th) continue;
      if (SuspendThread(th) != DWORD(-1)) {
        CONTEXT c{};
        c.ContextFlags = CONTEXT_DEBUG_REGISTERS;
        if (GetThreadContext(th, &c)) {
          uint64_t dr7 = 0;
          if (addr) {
            c.Dr0 = addr[0]; c.Dr1 = addr[1]; c.Dr2 = addr[2]; c.Dr3 = addr[3];
            for (int i = 0; i < 4; ++i)   // Li = 1, RWi = 01 (escritura), LENi = 11 (4 bytes)
              dr7 |= (1ull << (2 * i)) | (1ull << (16 + 4 * i)) | (3ull << (18 + 4 * i));
          } else {
            c.Dr0 = c.Dr1 = c.Dr2 = c.Dr3 = 0;
          }
          c.Dr7 = dr7;
          c.Dr6 = 0;
          SetThreadContext(th, &c);
        }
        ResumeThread(th);
      }
      CloseHandle(th);
    }
    CloseHandle(snap);
  });
  t.join();
}

void Report(Hit* hits, int n, uint32_t total, uint32_t cam) {
  // nombres desde el PDB (dbghelp se carga aqui para no depender de el al compilar)
  using SymInitFn = BOOL(WINAPI*)(HANDLE, PCSTR, BOOL);
  using SymFromAddrFn = BOOL(WINAPI*)(HANDLE, DWORD64, PDWORD64, PSYMBOL_INFO);
  using SymOptFn = DWORD(WINAPI*)(DWORD);
  HMODULE dh = LoadLibraryA("dbghelp.dll");
  auto init = dh ? reinterpret_cast<SymInitFn>(GetProcAddress(dh, "SymInitialize")) : nullptr;
  auto from = dh ? reinterpret_cast<SymFromAddrFn>(GetProcAddress(dh, "SymFromAddr")) : nullptr;
  auto opt = dh ? reinterpret_cast<SymOptFn>(GetProcAddress(dh, "SymSetOptions")) : nullptr;
  HANDLE proc = GetCurrentProcess();
  bool sym = false;
  if (init && from) {
    if (opt) opt(0x00000002 /*SYMOPT_UNDNAME*/ | 0x00000004 /*DEFERRED_LOADS*/);
    sym = init(proc, nullptr, TRUE) != FALSE;
  }
  auto name = [&](uint64_t a) {
    char buf[sizeof(SYMBOL_INFO) + 256] = {};
    auto* si = reinterpret_cast<SYMBOL_INFO*>(buf);
    si->SizeOfStruct = sizeof(SYMBOL_INFO);
    si->MaxNameLen = 255;
    DWORD64 d = 0;
    char out[300];
    if (sym && from(proc, a, &d, si)) std::snprintf(out, sizeof(out), "%s+0x%llx", si->Name, (unsigned long long)d);
    else std::snprintf(out, sizeof(out), "0x%llx", (unsigned long long)a);
    return std::string(out);
  };
  static const char* fields[4] = {"pos", "up", "at", "fov"};
  std::ostringstream o;
  o << "[CamWatch] escrituras del motor en la camara del escenario @" << std::hex << cam << std::dec << ": " << total
    << " en ~3 s, " << n << " sitios distintos";
  for (int i = 0; i < n; ++i) {
    o << "\n  " << hits[i].count << "x campo " << fields[hits[i].field & 3] << ":";
    for (int f = 0; f < hits[i].n; ++f) o << (f ? " <- " : " ") << name(hits[i].frames[f]);
  }
  if (!n) o << " (ninguna: el motor no reescribe esta camara; dibuja con otra copia)";
  TRACE(o.str());
}

}  // namespace

void ArmOnce(uint8_t* base, uint32_t cam) {
  if (g_state.load() != 0 || !cam) return;
  if (!g_veh) g_veh = AddVectoredExceptionHandler(1, &Veh);
  if (!g_veh) { g_state = 2; return; }
  const uint32_t offs[4] = {112, 144, 160, 192};
  for (int i = 0; i < 4; ++i) g_addr[i] = reinterpret_cast<uint64_t>(base + cam + offs[i]);
  g_cam = cam;
  g_nhits = 0;
  g_total = 0;
  g_state = 1;
  g_armed_at = Clock::now();
  SetAll(g_addr);
  TRACE("[CamWatch] vigilando la camara del escenario @" << std::hex << cam << std::dec
        << " (3 s) para saber que funcion del motor la escribe");
}

void Tick() {
  if (g_state.load() != 1 || Clock::now() - g_armed_at < std::chrono::seconds(3)) return;
  SetAll(nullptr);
  g_state = 2;
  static Hit copy[kMaxHits];
  const int n = g_nhits.load();
  for (int i = 0; i < n; ++i) copy[i] = g_hits[i];
  const uint32_t total = g_total.load(), cam = g_cam;
  std::thread([n, total, cam] { Report(copy, n, total, cam); }).detach();
}

SelfWrite::SelfWrite() : prev_(t_self) { t_self = true; }
SelfWrite::~SelfWrite() { t_self = prev_; }

}  // namespace re_wor::de::camwatch
