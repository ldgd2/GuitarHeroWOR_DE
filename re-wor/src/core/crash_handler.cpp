#include "core/crash_handler.hpp"

#include <chrono>
#include <csignal>
#include <condition_variable>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <dbghelp.h>
#pragma comment(lib, "dbghelp.lib")
#endif

namespace re_wor::core {

namespace {
std::mutex g_file_mutex;    // archivo de trace
std::mutex g_queue_mutex;   // cola de lineas pendientes
std::condition_variable g_queue_cv;
std::vector<std::string> g_queue;
std::once_flag g_writer_once;
constexpr size_t kWakeAt = 512;  // con muchas lineas seguidas se escribe antes de los 100 ms

void write_lines(std::vector<std::string>& lines) {
  if (lines.empty()) return;
  std::lock_guard<std::mutex> lock(g_file_mutex);
  auto& f = get_trace_log();
  for (auto& l : lines) f << l << '\n';
  f.flush();
  lines.clear();
}

void writer_loop() {
  std::vector<std::string> local;
  for (;;) {
    {
      std::unique_lock<std::mutex> lock(g_queue_mutex);
      g_queue_cv.wait_for(lock, std::chrono::milliseconds(100), [] { return g_queue.size() >= kWakeAt; });
      local.swap(g_queue);
    }
    write_lines(local);
  }
}

void start_writer() {
  std::call_once(g_writer_once, [] {
    std::thread(writer_loop).detach();
    std::atexit(flush_trace);
  });
}
}  // namespace

std::ofstream& get_trace_log() {
  static std::ofstream log("re_wor_trace.log", std::ios::out | std::ios::trunc);
  return log;
}

TraceStream& get_trace_stream() {
  static TraceStream s_stream;
  return s_stream;
}

std::ostringstream& TraceStream::line() {
  thread_local std::ostringstream s_line;
  return s_line;
}

void TraceStream::end_line() {
  auto& l = line();
  log_trace_message(l.str());
  l.str(std::string());
  l.clear();
  static const std::ostringstream kDefaultFormat;
  l.copyfmt(kDefaultFormat);  // std::hex de una linea no se arrastra a la siguiente
}

void log_trace_message(const std::string& msg) {
  start_writer();
  bool wake;
  {
    std::lock_guard<std::mutex> lock(g_queue_mutex);
    g_queue.push_back(msg);
    wake = g_queue.size() >= kWakeAt;
  }
  if (wake) g_queue_cv.notify_one();
}

void flush_trace() {
  std::vector<std::string> local;
  {
    std::lock_guard<std::mutex> lock(g_queue_mutex);
    local.swap(g_queue);
  }
  write_lines(local);
}

#ifdef _WIN32
static std::string (*g_crash_note)() = nullptr;
void set_crash_note(std::string (*note)()) { g_crash_note = note; }

static LONG WINAPI CrashHandler(EXCEPTION_POINTERS* ep) {
  flush_trace();  // que el trace llegue a disco antes de morir
  std::ofstream log("re_wor_crash_detail.log", std::ios::out | std::ios::trunc);
  log << "Exception: 0x" << std::hex << ep->ExceptionRecord->ExceptionCode << "\n";
  log << "RIP: 0x" << std::hex << ep->ContextRecord->Rip << "\n";
  if (ep->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION) {
    log << "Access: " << (ep->ExceptionRecord->ExceptionInformation[0] == 0 ? "READ" :
                          ep->ExceptionRecord->ExceptionInformation[0] == 1 ? "WRITE" : "EXEC") << "\n";
    log << "Target: 0x" << std::hex << ep->ExceptionRecord->ExceptionInformation[1] << "\n";
  }
  void* stack[64];
  USHORT frames = CaptureStackBackTrace(0, 64, stack, NULL);
  log << "\nStack (" << std::dec << frames << " frames):\n";
  SymInitialize(GetCurrentProcess(), NULL, TRUE);
  for (USHORT i = 0; i < frames; i++) {
    DWORD64 displacement = 0;
    char buffer[sizeof(SYMBOL_INFO) + MAX_SYM_NAME * sizeof(TCHAR)];
    PSYMBOL_INFO pSymbol = (PSYMBOL_INFO)buffer;
    pSymbol->SizeOfStruct = sizeof(SYMBOL_INFO);
    pSymbol->MaxNameLen = MAX_SYM_NAME;
    if (SymFromAddr(GetCurrentProcess(), (DWORD64)stack[i], &displacement, pSymbol)) {
      log << "  [" << std::dec << i << "] " << pSymbol->Name << " + 0x" << std::hex << displacement << "\n";
    } else {
      log << "  [" << std::dec << i << "] 0x" << std::hex << (uintptr_t)stack[i] << "\n";
    }
  }
  log.flush();
  return EXCEPTION_CONTINUE_SEARCH;
}

static LONG WINAPI VectoredCrashHandler(EXCEPTION_POINTERS* ep) {
  if (ep->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION) {
    uintptr_t target = ep->ExceptionRecord->ExceptionInformation[1];
    DWORD64 displacement = 0;
    char buffer[sizeof(SYMBOL_INFO) + MAX_SYM_NAME * sizeof(TCHAR)];
    PSYMBOL_INFO pSymbol = (PSYMBOL_INFO)buffer;
    pSymbol->SizeOfStruct = sizeof(SYMBOL_INFO);
    pSymbol->MaxNameLen = MAX_SYM_NAME;
    SymInitialize(GetCurrentProcess(), NULL, TRUE);
    std::string name = "unknown";
    if (SymFromAddr(GetCurrentProcess(), ep->ContextRecord->Rip, &displacement, pSymbol)) {
      name = pSymbol->Name;
    }
    TRACE("[VEH] Access Violation at RIP=0x" << std::hex << ep->ContextRecord->Rip
          << " (" << name << " + 0x" << displacement << ") target=0x" << target);
    if (g_crash_note) TRACE("[VEH] " << g_crash_note());
  }
  return EXCEPTION_CONTINUE_SEARCH;
}

// rex::FatalError / asserts del SDK escriben en stderr y llaman a abort(): sin consola eso se pierde y el
// juego "se cierra solo" sin dump. Este manejador deja el motivo y el stack en re_wor_crash_detail.log.
static void AbortHandler(int) {
  flush_trace();
  std::fflush(stderr);
  std::ofstream log("re_wor_crash_detail.log", std::ios::out | std::ios::trunc);
  log << "abort() (rex::FatalError o assert del SDK). Mensaje en re_wor_stderr.log\n";
  void* stack[64];
  USHORT frames = CaptureStackBackTrace(0, 64, stack, NULL);
  SymInitialize(GetCurrentProcess(), NULL, TRUE);
  log << "\nStack (" << std::dec << frames << " frames):\n";
  for (USHORT i = 0; i < frames; i++) {
    DWORD64 displacement = 0;
    char buffer[sizeof(SYMBOL_INFO) + MAX_SYM_NAME * sizeof(TCHAR)];
    PSYMBOL_INFO pSymbol = (PSYMBOL_INFO)buffer;
    pSymbol->SizeOfStruct = sizeof(SYMBOL_INFO);
    pSymbol->MaxNameLen = MAX_SYM_NAME;
    if (SymFromAddr(GetCurrentProcess(), (DWORD64)stack[i], &displacement, pSymbol)) {
      log << "  [" << std::dec << i << "] " << pSymbol->Name << " + 0x" << std::hex << displacement << "\n";
    } else {
      log << "  [" << std::dec << i << "] 0x" << std::hex << (uintptr_t)stack[i] << "\n";
    }
  }
  log.flush();
}
#endif

void install_crash_handlers() {
#ifdef _WIN32
  if (AttachConsole(ATTACH_PARENT_PROCESS)) {
    FILE* fp = nullptr;
    freopen_s(&fp, "CONOUT$", "w", stdout);
    freopen_s(&fp, "CONOUT$", "w", stderr);
    freopen_s(&fp, "CONIN$", "r", stdin);
    std::ios::sync_with_stdio(true);
  } else {
    FILE* fp = nullptr;
    freopen_s(&fp, "re_wor_stderr.log", "w", stderr);  // mensajes FATAL ERROR del SDK
  }
  std::signal(SIGABRT, AbortHandler);
  SetUnhandledExceptionFilter(CrashHandler);
  AddVectoredExceptionHandler(1, VectoredCrashHandler);
#endif
}

}  // namespace re_wor::core
