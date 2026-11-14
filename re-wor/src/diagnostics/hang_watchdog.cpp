// =============================================================================
// hang_watchdog.cpp - Volcado de stacks de todos los hilos para diagnosticar
// cuelgues (pantalla negra sin crash). Se activa con la variable de entorno
// REWOR_HANG_DUMP_SECONDS=<segundos> (sin ella no hace nada).
//
// Tras REWOR_HANG_DUMP_SECONDS segundos
// escribe re_wor_hang_dump.log con el stack simbolizado de cada hilo.
// No modifica el estado del juego: suspende cada hilo un instante y lo reanuda.
// =============================================================================

#if defined(_WIN32)

#include <windows.h>
#include <dbghelp.h>
#include <tlhelp32.h>

#include <cstdlib>
#include <fstream>
#include <thread>

#pragma comment(lib, "dbghelp.lib")

namespace {

void DumpThread(std::ofstream& log, HANDLE process, DWORD tid) {
  HANDLE th = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION,
                         FALSE, tid);
  if (!th) return;

  log << "\n=== Thread " << std::dec << tid;
  PWSTR desc = nullptr;
  if (SUCCEEDED(GetThreadDescription(th, &desc)) && desc) {
    char name[256] = {};
    WideCharToMultiByte(CP_UTF8, 0, desc, -1, name, sizeof(name) - 1, nullptr, nullptr);
    if (name[0]) log << " '" << name << "'";
    LocalFree(desc);
  }
  log << " ===\n";

  if (SuspendThread(th) == static_cast<DWORD>(-1)) {
    CloseHandle(th);
    return;
  }

  CONTEXT ctx = {};
  ctx.ContextFlags = CONTEXT_FULL;
  if (GetThreadContext(th, &ctx)) {
    STACKFRAME64 frame = {};
    frame.AddrPC.Offset = ctx.Rip;
    frame.AddrPC.Mode = AddrModeFlat;
    frame.AddrFrame.Offset = ctx.Rbp;
    frame.AddrFrame.Mode = AddrModeFlat;
    frame.AddrStack.Offset = ctx.Rsp;
    frame.AddrStack.Mode = AddrModeFlat;

    alignas(SYMBOL_INFO) char buffer[sizeof(SYMBOL_INFO) + MAX_SYM_NAME];
    for (int i = 0; i < 40; i++) {
      if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, th, &frame, &ctx, nullptr,
                       SymFunctionTableAccess64, SymGetModuleBase64, nullptr)) {
        break;
      }
      if (frame.AddrPC.Offset == 0) break;
      auto* sym = reinterpret_cast<PSYMBOL_INFO>(buffer);
      sym->SizeOfStruct = sizeof(SYMBOL_INFO);
      sym->MaxNameLen = MAX_SYM_NAME;
      DWORD64 disp = 0;
      log << "  [" << std::dec << i << "] ";
      if (SymFromAddr(process, frame.AddrPC.Offset, &disp, sym)) {
        log << sym->Name << " + 0x" << std::hex << disp << "\n";
      } else {
        log << "0x" << std::hex << frame.AddrPC.Offset << "\n";
      }
    }
  }

  ResumeThread(th);
  CloseHandle(th);
}

void DumpAllThreads(const char* path) {
  HANDLE process = GetCurrentProcess();
  std::ofstream log(path, std::ios::out | std::ios::trunc);
  log << "Hang dump (todos los hilos)\n" << std::flush;

  SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
  BOOL sym_ok = SymInitialize(process, nullptr, TRUE);
  log << "SymInitialize=" << sym_ok << "\n" << std::flush;

  DWORD self = GetCurrentThreadId();
  DWORD pid = GetCurrentProcessId();
  HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
  if (snap == INVALID_HANDLE_VALUE) return;
  THREADENTRY32 te = {};
  te.dwSize = sizeof(te);
  if (Thread32First(snap, &te)) {
    do {
      if (te.th32OwnerProcessID == pid && te.th32ThreadID != self) {
        DumpThread(log, process, te.th32ThreadID);
        log.flush();
      }
    } while (Thread32Next(snap, &te));
  }
  CloseHandle(snap);
}

struct HangWatchdog {
  HangWatchdog() {
    int seconds = 0;
    if (const char* env = std::getenv("REWOR_HANG_DUMP_SECONDS")) {
      seconds = std::atoi(env);
    }
    if (seconds <= 0) return;
    std::thread([seconds]() {
      std::this_thread::sleep_for(std::chrono::seconds(seconds));
      // 3 volcados separados 5 s: si un hilo sigue en la misma funcion, esta en bucle.
      DumpAllThreads("re_wor_hang_dump.log");
      std::this_thread::sleep_for(std::chrono::seconds(5));
      DumpAllThreads("re_wor_hang_dump_2.log");
      std::this_thread::sleep_for(std::chrono::seconds(5));
      DumpAllThreads("re_wor_hang_dump_3.log");
    }).detach();
  }
};

HangWatchdog g_hang_watchdog;

}  // namespace

#endif
