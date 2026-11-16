// =============================================================================
// qb_bridge.cpp - Puente de desarrollo con el motor de scripts QB (ver qb_bridge.hpp).
// =============================================================================

#include "de/qb_bridge.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <random>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#pragma comment(lib, "Ws2_32.lib")
#endif

#include <rex/cvar.h>
#include <rex/logging.h>

#include "core/crash_handler.hpp"
#include "core/path_resolver.hpp"
#include "de/de_hooks.hpp"
#include "de/frame_pacing.hpp"
#include "de/de_hooks.hpp"
#include "de/edfx.hpp"
#include "generated/re_wor_pch.h"

// Funciones del motor (direcciones medidas en el codigo recompilado)
extern "C" void sub_823A5450(PPCContext& __restrict ctx, uint8_t* base);  // crear script / llamar cfunc
extern "C" void sub_821E3D50(PPCContext& __restrict ctx, uint8_t* base);  // reservar componente (16 bytes)
extern "C" void sub_820E6490(PPCContext& __restrict ctx, uint8_t* base);  // reservar CStruct (8 bytes)
extern "C" void sub_82397720(PPCContext& __restrict ctx, uint8_t* base);  // CStruct: anadir componente
extern "C" void sub_82395A00(PPCContext& __restrict ctx, uint8_t* base);  // CStruct: vaciar (libera componentes)
extern "C" void sub_823D6978(PPCContext& __restrict ctx, uint8_t* base);  // copiar texto al monton de scripts
extern "C" void sub_823E4958(PPCContext& __restrict ctx, uint8_t* base);  // registrar un archivo QB (r3 nombre, r4 datos)

namespace re_wor::de::qb_bridge {

namespace {

constexpr uint32_t kSymbolTable = 0x82986970;
enum : uint8_t { kInt = 1, kFloat = 2, kString = 3, kScript = 7, kCFunc = 8, kStruct = 10, kArray = 12, kName = 13, kAlias = 26 };

// ---------------------------------------------------------------- registro
std::mutex g_log_mutex;
std::vector<LogEntry> g_recent_logs;
std::atomic<bool> g_debug_build{false};

void AddLog(const std::string& msg, bool ok) {
  using Clock = std::chrono::steady_clock;
  static const auto s_start = Clock::now();
  {
    std::lock_guard lock(g_log_mutex);
    if (g_recent_logs.size() >= 100) g_recent_logs.erase(g_recent_logs.begin());
    g_recent_logs.push_back({msg, ok, std::chrono::duration<double>(Clock::now() - s_start).count()});
  }
  TRACE("[QBBridge] " << (ok ? "OK: " : "ERROR: ") << msg);
  if (ok) REXLOG_INFO("[qb] {}", msg);
  else REXLOG_WARN("[qb] {}", msg);
}

// ---------------------------------------------------------------- memoria del juego
bool SafeRead32(uint8_t* base, uint32_t addr, uint32_t* out) {
  if (!base || addr < 0x10000u || addr >= 0xF0000000u) return false;
#ifdef _WIN32
  __try {
    *out = REX_LOAD_U32(addr);
    return true;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
#else
  *out = REX_LOAD_U32(addr);
  return true;
#endif
}

bool SafeRead8(uint8_t* base, uint32_t addr, uint8_t* out) {
  if (!base || addr < 0x10000u || addr >= 0xF0000000u) return false;
#ifdef _WIN32
  __try {
    *out = REX_LOAD_U8(addr);
    return true;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
#else
  *out = REX_LOAD_U8(addr);
  return true;
#endif
}

using GuestFn = void (*)(PPCContext& __restrict, uint8_t*);
// llamada al motor; si revienta (acceso invalido) se devuelve false en vez de cerrar el juego
bool SafeCall(GuestFn fn, PPCContext& c, uint8_t* base) {
#ifdef _WIN32
  __try {
    fn(c, base);
    return true;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
#else
  fn(c, base);
  return true;
#endif
}

// entrada de la tabla de simbolos (siguiendo alias); 0 si no existe
uint32_t FindSymbol(uint8_t* base, uint32_t key) {
  uint32_t table = 0;
  if (!key || !SafeRead32(base, kSymbolTable, &table) || !table) return 0;
  for (int hop = 0; hop < 4; ++hop) {
    uint32_t e = 0;
    if (!SafeRead32(base, table + (((key << 2) | (key >> 30)) & 0x1FFFCu), &e)) return 0;
    for (int n = 0; e && n < 512; ++n) {
      uint32_t k = 0;
      if (!SafeRead32(base, e + 4, &k)) return 0;
      if (k == key) break;
      if (!SafeRead32(base, e + 16, &e)) return 0;
    }
    if (!e) return 0;
    uint8_t type = 0;
    if (!SafeRead8(base, e + 2, &type)) return 0;
    if (type != kAlias) return e;
    if (!SafeRead32(base, e + 12, &key)) return 0;
  }
  return 0;
}

// ---------------------------------------------------------------- parametros al estilo QB
struct Val {
  uint8_t type = kName;
  int32_t i = 0;
  float f = 0;
  uint32_t name = 0;
  std::string text;                            // texto, o el nombre tal cual se escribio
  std::vector<std::pair<uint32_t, Val>> fields;
};

struct Parser {
  const std::string& s;
  size_t p = 0;
  std::string error;
  void Skip() { while (p < s.size() && (s[p] == ' ' || s[p] == '\t' || s[p] == ',')) ++p; }
  static bool IdChar(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '.' || c == '/' || c == '\\' || c == ':' ||
           c == '-' || c == '+';
  }
  std::string Ident() {
    size_t a = p;
    while (p < s.size() && IdChar(s[p])) ++p;
    return s.substr(a, p - a);
  }
  bool Value(Val& v) {
    Skip();
    if (p >= s.size()) { error = "falta un valor"; return false; }
    if (s[p] == '{') {
      ++p;
      v.type = kStruct;
      if (!Fields(v.fields, '}')) return false;
      return true;
    }
    if (s[p] == '"') {
      size_t e = s.find('"', p + 1);
      if (e == std::string::npos) { error = "texto sin cerrar"; return false; }
      v.type = kString;
      v.text = s.substr(p + 1, e - p - 1);
      p = e + 1;
      return true;
    }
    std::string t = Ident();
    if (t.empty()) { error = std::string("caracter inesperado '") + s[p] + "'"; return false; }
    return Atom(t, v);
  }
  static bool Atom(const std::string& t, Val& v) {
    if (t.size() > 2 && t[0] == '0' && (t[1] == 'x' || t[1] == 'X')) {   // clave en hexadecimal
      v.type = kName;
      v.name = static_cast<uint32_t>(std::strtoul(t.c_str() + 2, nullptr, 16));
      v.text = t;
      return true;
    }
    char* end = nullptr;
    long iv = std::strtol(t.c_str(), &end, 10);
    if (end && *end == 0) { v.type = kInt; v.i = static_cast<int32_t>(iv); return true; }
    float fv = std::strtof(t.c_str(), &end);
    if (end && *end == 0) { v.type = kFloat; v.f = fv; return true; }
    v.type = kName;
    v.name = CalculateQbKey(t);
    v.text = t;
    return true;
  }
  // clave=valor ... o nombres sueltos (banderas, componente sin nombre)
  bool Fields(std::vector<std::pair<uint32_t, Val>>& out, char close) {
    for (;;) {
      Skip();
      if (p >= s.size()) {
        if (close) { error = "falta '}'"; return false; }
        return true;
      }
      if (s[p] == close) { ++p; return true; }
      std::string k = Ident();
      if (k.empty()) { error = std::string("caracter inesperado '") + s[p] + "'"; return false; }
      Skip();
      Val v;
      if (p < s.size() && s[p] == '=') {
        ++p;
        if (!Value(v)) return false;
        out.emplace_back(CalculateQbKey(k), std::move(v));
      } else {
        Atom(k, v);
        out.emplace_back(0u, std::move(v));
      }
    }
  }
};

std::string Describe(const Val& v) {
  char b[64];
  switch (v.type) {
    case kInt: std::snprintf(b, sizeof b, "%d", v.i); return b;
    case kFloat: std::snprintf(b, sizeof b, "%g", v.f); return b;
    case kString: return "\"" + v.text + "\"";
    case kStruct: return "{...}";
    default:
      if (!v.text.empty()) return v.text;
      std::snprintf(b, sizeof b, "0x%08X", v.name);
      return b;
  }
}

// ---------------------------------------------------------------- llamadas en el hilo del juego
// Pila del juego: por debajo de r1 (ya devuelto de la funcion enganchada) todo esta libre. Se reserva
// [datos: CStruct de parametros + texto temporal] y la pila de las llamadas va por debajo.
struct Guest {
  PPCContext& tmpl;
  uint8_t* base;
  uint32_t params = 0;    // CStruct raiz (en la pila)
  uint32_t scratch = 0;   // 256 bytes para textos
  uint32_t sp = 0;
  bool failed = false;

  Guest(PPCContext& c, uint8_t* b) : tmpl(c), base(b) {
    const uint32_t r1 = c.r1.u32;
    params = (r1 - 0x300) & ~0xFu;
    scratch = params + 0x10;
    sp = (params - 0x100) & ~0xFu;
    REX_STORE_U32(params, 0);
    REX_STORE_U32(params + 4, 0);
  }
  bool Call(GuestFn fn, uint32_t r3, uint32_t r4 = 0, uint32_t r5 = 0, uint32_t* ret = nullptr) {
    PPCContext c = tmpl;
    c.r1.u64 = sp;
    c.r3.u64 = r3;
    c.r4.u64 = r4;
    c.r5.u64 = r5;
    c.r6.u64 = c.r7.u64 = c.r8.u64 = c.r9.u64 = c.r10.u64 = 0;
    if (!SafeCall(fn, c, base)) { failed = true; return false; }
    if (ret) *ret = c.r3.u32;
    return true;
  }
  uint32_t NewStruct() {
    uint32_t s = 0;
    if (!Call(sub_820E6490, 8, 1, 0, &s) || !s) return 0;
    REX_STORE_U32(s + 4, 0);
    REX_STORE_U8(s + 2, 0);
    return s;
  }
  bool Fill(uint32_t st, const std::vector<std::pair<uint32_t, Val>>& fields) {
    for (auto& [key, v] : fields) {
      uint32_t comp = 0;
      if (!Call(sub_821E3D50, 16, 1, 0, &comp) || !comp) return false;
      REX_STORE_U8(comp + 1, 0);
      REX_STORE_U8(comp + 3, REX_LOAD_U8(comp + 3) & ~0x40);
      REX_STORE_U32(comp + 12, 0);
      uint32_t value = 0;
      switch (v.type) {
        case kInt: value = static_cast<uint32_t>(v.i); break;
        case kFloat: std::memcpy(&value, &v.f, 4); break;
        case kName: value = v.name; break;
        case kString: {
          const size_t n = std::min<size_t>(v.text.size(), 250);
          for (size_t i = 0; i < n; ++i) REX_STORE_U8(scratch + i, static_cast<uint8_t>(v.text[i]));
          REX_STORE_U8(scratch + n, 0);
          if (!Call(sub_823D6978, scratch, 0, 0, &value)) return false;
          break;
        }
        case kStruct:
          value = NewStruct();
          if (!value || !Fill(value, v.fields)) return false;
          break;
      }
      REX_STORE_U8(comp + 2, v.type);
      REX_STORE_U32(comp + 4, key);
      REX_STORE_U32(comp + 8, value);
      if (!Call(sub_82397720, st, comp, 0)) return false;
    }
    return true;
  }
  void Release() { Call(sub_82395A00, params); }
};

// ---------------------------------------------------------------- ordenes
struct Order {
  std::string line;
  std::function<void(bool, const std::string&)> done;
};
std::mutex g_queue_mutex;
std::vector<Order> g_queue;

std::string Hex(uint32_t v) {
  char b[16];
  std::snprintf(b, sizeof b, "0x%08X", v);
  return b;
}

bool Run(PPCContext& ctx, uint8_t* base, const std::string& script, const std::string& args, std::string& reply) {
  const uint32_t key = (script.size() > 2 && script[0] == '0' && (script[1] == 'x' || script[1] == 'X'))
                           ? static_cast<uint32_t>(std::strtoul(script.c_str() + 2, nullptr, 16))
                           : CalculateQbKey(script);
  const uint32_t e = FindSymbol(base, key);
  uint8_t type = 0;
  if (!e || !SafeRead8(base, e + 2, &type)) { reply = "no existe: " + script; return false; }
  if (type != kScript && type != kCFunc) {
    reply = script + " es " + SymbolTypeName(type) + ", no un script ni una funcion del motor";
    return false;
  }
  Parser ps{args};
  std::vector<std::pair<uint32_t, Val>> fields;
  if (!ps.Fields(fields, 0)) { reply = "parametros: " + ps.error; return false; }
  Guest g(ctx, base);
  if (!g.Fill(g.params, fields)) {
    g.Release();
    reply = "no se pudieron preparar los parametros";
    return false;
  }
  uint32_t obj = 0;
  const bool ok = g.Call(sub_823A5450, key, g.params, 0, &obj);
  g.Release();
  if (!ok) { reply = "el motor fallo al lanzar " + script + " (acceso invalido capturado)"; return false; }
  std::string p;
  for (auto& [k, v] : fields) p += " " + (k ? Hex(k) + "=" : std::string()) + Describe(v);
  if (type == kCFunc) {
    reply = "funcion " + script + " llamada";
  } else if (obj) {
    reply = "script " + script + " lanzado";
  } else {
    reply = "el motor no creo el script " + script;
    return false;
  }
  if (!fields.empty()) reply += " (" + std::to_string(fields.size()) + " parametros)";
  return true;
}

std::string ReadGlobal(uint8_t* base, uint32_t e, uint8_t type) {
  uint32_t raw = 0;
  SafeRead32(base, e + 12, &raw);
  char b[96];
  switch (type) {
    case kInt: std::snprintf(b, sizeof b, "%d (entero)", static_cast<int32_t>(raw)); return b;
    case kFloat: { float f; std::memcpy(&f, &raw, 4); std::snprintf(b, sizeof b, "%g (real)", f); return b; }
    case kName: return Hex(raw) + " (nombre)";
    case kString: {
      std::string t;
      for (uint32_t i = 0; i < 200; ++i) {
        uint8_t ch = 0;
        if (!SafeRead8(base, raw + i, &ch) || !ch) break;
        t += static_cast<char>(ch);
      }
      return "\"" + t + "\" (texto)";
    }
    default: return std::string(SymbolTypeName(type)) + " en " + Hex(e);
  }
}


// ---------------------------------------------------------------- scripts propios (qb/*.qb.xen)
std::function<uint32_t(uint32_t)> g_alloc;   // memoria del juego que no se libera nunca
std::atomic<bool> g_scripts_loaded{false};
std::atomic<int> g_scripts_count{0};

uint32_t BucketAddr(uint32_t table, uint32_t key) { return table + (((key << 2) | (key >> 30)) & 0x1FFFCu); }

// Entrada exacta de la tabla (sin seguir alias); prev_link = direccion que apunta a ella
uint32_t FindEntry(uint8_t* base, uint32_t key, uint32_t* prev_link) {
  uint32_t table = 0;
  if (!SafeRead32(base, kSymbolTable, &table) || !table) return 0;
  uint32_t link = BucketAddr(table, key), e = 0;
  if (!SafeRead32(base, link, &e)) return 0;
  for (int n = 0; e && n < 512; ++n) {
    uint32_t k = 0;
    if (!SafeRead32(base, e + 4, &k)) return 0;
    if (k == key) {
      if (prev_link) *prev_link = link;
      return e;
    }
    link = e + 16;
    if (!SafeRead32(base, link, &e)) return 0;
  }
  return 0;
}

// Cambia la clave de un simbolo: el script original sigue disponible con el nombre nuevo y el
// antiguo queda libre para la version de re-wor (que llama al original como re_orig_<nombre>).
bool RenameSymbol(uint8_t* base, const std::string& from, const std::string& to, std::string& reply) {
  const uint32_t kf = CalculateQbKey(from), kt = CalculateQbKey(to);
  if (FindEntry(base, kt, nullptr)) { reply = to + " ya existe"; return false; }
  uint32_t link = 0;
  const uint32_t e = FindEntry(base, kf, &link);
  if (!e) { reply = from + " no existe"; return false; }
  uint32_t table = 0, next = 0;
  SafeRead32(base, kSymbolTable, &table);
  SafeRead32(base, e + 16, &next);
  REX_STORE_U32(link, next);                       // fuera de su cubo
  REX_STORE_U32(e + 4, kt);
  const uint32_t dst = BucketAddr(table, kt);
  REX_STORE_U32(e + 16, REX_LOAD_U32(dst));        // al principio del cubo nuevo
  REX_STORE_U32(dst, e);
  reply = from + " -> " + to;
  return true;
}

// Lista "re_hooks_<nombre>" del propio archivo: scripts del juego que este archivo envuelve
std::vector<std::string> FileHooks(const std::vector<uint8_t>& d, const std::string& stem) {
  auto be = [&](size_t o) -> uint32_t {
    return o + 4 <= d.size()
               ? (uint32_t(d[o]) << 24) | (uint32_t(d[o + 1]) << 16) | (uint32_t(d[o + 2]) << 8) | d[o + 3]
               : 0;
  };
  const uint32_t want = CalculateQbKey("re_hooks_" + stem);
  std::vector<std::string> out;
  for (size_t o = 28; o + 20 <= d.size();) {
    const uint32_t type = be(o), name = be(o + 4), ptr = be(o + 12);
    if ((type & 0xFFFF00FFu) != 0x00200000u) break;
    const uint32_t kind = (type >> 8) & 0xFF;
    size_t next = o + 20;
    if (kind == 7) {   // script: 12 bytes de cabecera + bytecode alineado a 4
      const uint32_t comp = be(ptr + 8);
      next = (size_t(ptr) + 12 + comp + 3) & ~size_t(3);
    } else if (kind == 3) {   // texto en linea, alineado a 4
      size_t z = ptr;
      while (z < d.size() && d[z]) ++z;
      if (name == want && ptr < d.size()) {
        std::string list(reinterpret_cast<const char*>(&d[ptr]), z - ptr), w;
        for (char ch : list + " ") {
          if (ch == ' ' || ch == ',') {
            if (!w.empty()) out.push_back(w);
            w.clear();
          } else {
            w += ch;
          }
        }
      }
      next = (z + 1 + 3) & ~size_t(3);
    }
    if (next <= o) break;
    o = next;
  }
  return out;
}

bool LoadQbFile(PPCContext& ctx, uint8_t* base, const std::filesystem::path& path, std::string& reply) {
  if (!g_alloc) { reply = "sin memoria del juego"; return false; }
  std::ifstream f(path, std::ios::binary);
  std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  const std::string fname = path.filename().string();
  if (data.size() < 28) { reply = fname + ": no se pudo leer"; return false; }
  const std::string stem = fname.substr(0, fname.find('.'));
  // envolver: el original pasa a re_orig_<nombre> antes de registrar la version nueva
  TRACE("[scripts] " << fname << ": " << data.size() << " bytes leidos");
  std::string hooks;
  for (const auto& h : FileHooks(data, stem)) {
    std::string r;
    if (FindEntry(base, CalculateQbKey("re_orig_" + h), nullptr)) continue;   // ya envuelto
    if (!RenameSymbol(base, h, "re_orig_" + h, r)) { reply = stem + ": " + r; return false; }
    hooks += " " + h;
  }
  const std::string name = "scripts\\re_wor\\" + stem + ".qb";
  TRACE("[scripts] " << stem << ": envueltos" << hooks << "; reservando memoria");
  const uint32_t buf = g_alloc(static_cast<uint32_t>(data.size() + 16));
  const uint32_t str = g_alloc(static_cast<uint32_t>(name.size() + 1));
  if (!buf || !str) { reply = "sin memoria del juego"; return false; }
  std::memcpy(base + buf, data.data(), data.size());
  std::memcpy(base + str, name.c_str(), name.size() + 1);
  TRACE("[scripts] " << stem << ": datos en 0x" << std::hex << buf << std::dec << ", registrando");
  Guest g(ctx, base);
  if (!g.Call(sub_823E4958, str, buf, 0)) { reply = stem + ": el motor fallo al registrarlo"; return false; }
  reply = stem + " cargado (" + std::to_string(data.size()) + " bytes)" + (hooks.empty() ? "" : ", envuelve:" + hooks);
  return true;
}

// Carga <carpeta del juego>/qb/*.qb.xen por orden alfabetico
void LoadScriptFolder(PPCContext& ctx, uint8_t* base) {
  namespace fs = std::filesystem;
  std::error_code ec;
  const fs::path dir = re_wor::core::get_executable_directory() / "qb";   // junto al .exe (CMake)
  std::vector<fs::path> files;
  for (const auto& it : fs::directory_iterator(dir, ec)) {
    const std::string n = it.path().filename().string();
    if (it.is_regular_file() && n.size() > 7 && n.substr(n.size() - 7) == ".qb.xen") files.push_back(it.path());
  }
  std::sort(files.begin(), files.end());
  TRACE("[scripts] " << files.size() << " archivo(s) en " << dir.string());
  for (const auto& p : files) {
    std::string reply;
    const bool ok = LoadQbFile(ctx, base, p, reply);
    AddLog("[scripts] " + reply, ok);
    if (ok) ++g_scripts_count;
  }
}

bool Dispatch(PPCContext& ctx, uint8_t* base, const std::string& line, std::string& reply) {
  size_t a = line.find_first_not_of(" \t");
  if (a == std::string::npos) { reply = "orden vacia"; return false; }
  size_t sp = line.find_first_of(" \t", a);
  std::string cmd = line.substr(a, sp == std::string::npos ? std::string::npos : sp - a);
  std::string rest = sp == std::string::npos ? std::string() : line.substr(sp + 1);
  for (auto& ch : cmd) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  auto first = [&](std::string& tail) {
    size_t b = rest.find_first_not_of(" \t");
    if (b == std::string::npos) { tail.clear(); return std::string(); }
    size_t e = rest.find_first_of(" \t", b);
    tail = e == std::string::npos ? std::string() : rest.substr(e + 1);
    return rest.substr(b, e == std::string::npos ? std::string::npos : e - b);
  };

  if (cmd == "run") {
    std::string tail, name = first(tail);
    if (name.empty()) { reply = "uso: run <script> [clave=valor ...]"; return false; }
    return Run(ctx, base, name, tail, reply);
  }
  if (cmd == "runfirst") {   // el primero que exista de una lista
    std::string tail, name;
    while (!(name = first(tail)).empty()) {
      if (ScriptExists(name, base)) return Run(ctx, base, name, "", reply);
      rest = tail;
    }
    reply = "ninguno de esos scripts existe en esta version";
    return false;
  }
  if (cmd == "exists" || cmd == "get") {
    std::string tail, name = first(tail);
    const uint32_t e = FindSymbol(base, CalculateQbKey(name));
    uint8_t type = 0;
    if (!e || !SafeRead8(base, e + 2, &type)) { reply = name + ": no existe"; return cmd == "exists"; }
    reply = name + " = " + (cmd == "get" ? ReadGlobal(base, e, type) : std::string(SymbolTypeName(type)));
    return true;
  }
  if (cmd == "set") {
    std::string tail, name = first(tail);
    const uint32_t e = FindSymbol(base, CalculateQbKey(name));
    uint8_t type = 0;
    if (!e || !SafeRead8(base, e + 2, &type)) { reply = name + ": no existe"; return false; }
    Parser ps{tail};
    Val v;
    if (!ps.Value(v)) { reply = "valor: " + ps.error; return false; }
    uint32_t raw = 0;
    if (type == kInt && (v.type == kInt)) raw = static_cast<uint32_t>(v.i);
    else if (type == kFloat && (v.type == kFloat || v.type == kInt)) {
      float f = v.type == kInt ? static_cast<float>(v.i) : v.f;
      std::memcpy(&raw, &f, 4);
    } else if (type == kName && v.type == kName) raw = v.name;
    else {
      reply = name + " es " + SymbolTypeName(type) + ": solo se cambian enteros, reales y nombres con el tipo correcto";
      return false;
    }
    const std::string before = ReadGlobal(base, e, type);
    REX_STORE_U32(e + 12, raw);
    reply = name + ": " + before + " -> " + ReadGlobal(base, e, type);
    return true;
  }
  if (cmd == "debugmenu" || cmd == "state") {
    // abrirlo durante el arranque (videos, pantalla legal) o en mitad de una cancion deja al juego sin la
    // interfaz que el menu espera
    if (!BootDone()) { reply = "espera a que el juego llegue al menu principal"; return false; }
    if (edfx::SongPlaying()) { reply = "sal de la cancion primero (los menus de depuracion son del menu principal)"; return false; }
    if (cmd == "state") {
      std::string tail, st = first(tail);
      if (st.empty()) { reply = "uso: state <UIstate_...>"; return false; }
      return Run(ctx, base, "ui_event", "event=menu_change data={state=" + st + "}", reply);
    }
    // la opcion "debug" del menu principal: cambia al estado de interfaz UIstate_debug
    if (ScriptExists("ui_event", base))
      return Run(ctx, base, "ui_event", "event=menu_change data={state=UIstate_debug}", reply);
    return Run(ctx, base, "main_menu_select_debug", "", reply);
  }
  if (cmd == "debugbuild") {
    std::string tail, v = first(tail);
    if (v == "on" || v == "1") SetDebugBuild(true);
    else if (v == "off" || v == "0") SetDebugBuild(false);
    reply = std::string("DebugBuild = ") + (IsDebugBuild() ? "1" : "0");
    return true;
  }
  if (cmd == "loadqb") {
    std::string tail, file = first(tail);
    if (file.empty()) { reply = "uso: loadqb <archivo.qb.xen> (relativa a la carpeta del juego)"; return false; }
    std::filesystem::path p = file;
    if (p.is_relative()) p = re_wor::core::get_executable_directory() / p;
    return LoadQbFile(ctx, base, p, reply);
  }
  if (cmd == "rename") {
    std::string tail, a = first(tail);
    rest = tail;
    std::string tail2, b = first(tail2);
    if (a.empty() || b.empty()) { reply = "uso: rename <simbolo> <nombre nuevo>"; return false; }
    return RenameSymbol(base, a, b, reply);
  }
  if (cmd == "peek" || cmd == "poke") {   // memoria del juego: peek <addr hex>, poke <addr hex> <valor>
    std::string tail, a = first(tail);
    rest = tail;
    std::string tail2, v = first(tail2);
    const uint32_t addr = static_cast<uint32_t>(std::strtoul(a.c_str(), nullptr, 16));
    uint32_t cur = 0;
    if (!addr || !SafeRead32(base, addr, &cur)) { reply = "direccion no legible"; return false; }
    if (cmd == "poke") {
      if (v.empty()) { reply = "uso: poke <direccion hex> <valor>"; return false; }
      REX_STORE_U32(addr, static_cast<uint32_t>(std::strtol(v.c_str(), nullptr, 0)));
    }
    char b[64];
    std::snprintf(b, sizeof(b), "%08x = %u (antes %u)", addr, REX_LOAD_U32(addr), cur);
    reply = b;
    return true;
  }
  if (cmd == "fps") {
    char b[160];
    const auto st = re_wor::de::frame_pacing::Get();
    std::snprintf(b, sizeof(b), "motor %.1f fps, logica %.1f fps, objetivo %d%s, velocidad del motor %.2fx, cancion %.2fx",
                  re_wor::de::GameFps(), re_wor::de::LogicFps(), st.target_hz,
                  st.fallback ? " (protegido: 60)" : st.unlocked ? " sin candados" : "", st.engine_speed, st.song_speed);
    reply = b;
    return true;
  }
  if (cmd == "count") {
    reply = std::to_string(GetLoadedScriptCount(base)) + " simbolos en la tabla global";
    return true;
  }
  reply = "orden desconocida: " + cmd + " (run, runfirst, exists, get, set, debugmenu, state, debugbuild, loadqb, rename, count)";
  return false;
}

// ---------------------------------------------------------------- puerto local
std::atomic<int> g_server_port{0};
std::atomic<bool> g_server_stop{false};
std::thread g_server_thread;
std::string g_token;

#ifdef _WIN32
SOCKET g_listen = INVALID_SOCKET;

bool SendLine(SOCKET s, std::string t) {
  for (auto& ch : t) if (ch == '\n' || ch == '\r') ch = ' ';
  t += "\n";
  return send(s, t.data(), static_cast<int>(t.size()), 0) == static_cast<int>(t.size());
}

void Serve(SOCKET c) {
  std::string buf;
  bool authed = false;
  char tmp[1024];
  SendLine(c, "re-wor dev bridge 1 (auth <clave>)");
  while (!g_server_stop) {
    int n = recv(c, tmp, sizeof tmp, 0);
    if (n <= 0) break;
    buf.append(tmp, n);
    if (buf.size() > 65536) break;
    size_t nl;
    while ((nl = buf.find('\n')) != std::string::npos) {
      std::string line = buf.substr(0, nl);
      buf.erase(0, nl + 1);
      if (!line.empty() && line.back() == '\r') line.pop_back();
      if (line.empty()) continue;
      if (!authed) {
        if (line == "auth " + g_token) {
          authed = true;
          SendLine(c, "ok");
        } else {
          SendLine(c, "err clave incorrecta");
          return;
        }
        continue;
      }
      if (line == "quit") return;
      struct Wait {
        std::mutex m;
        std::condition_variable cv;
        bool ready = false, ok = false;
        std::string reply;
      };
      auto w = std::make_shared<Wait>();
      Submit(line, [w](bool ok, const std::string& r) {
        std::lock_guard l(w->m);
        w->ok = ok;
        w->reply = r;
        w->ready = true;
        w->cv.notify_all();
      });
      std::unique_lock l(w->m);
      if (!w->cv.wait_for(l, std::chrono::seconds(5), [&] { return w->ready; })) {
        SendLine(c, "err el juego no respondio (cargando o en pausa?)");
      } else {
        SendLine(c, (w->ok ? "ok " : "err ") + w->reply);
      }
    }
  }
}
#endif

}  // namespace

// =============================================================================

uint32_t CalculateQbKey(const std::string& s) {
  uint32_t c = 0xFFFFFFFFu;
  for (unsigned char ch : s) {
    if (ch >= 'A' && ch <= 'Z') ch = static_cast<unsigned char>(ch + 32);
    if (ch == '/') ch = '\\';
    c ^= ch;
    for (int k = 0; k < 8; ++k) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
  }
  return c;
}

int SymbolType(uint32_t checksum, uint8_t* base) {
  const uint32_t e = FindSymbol(base, checksum);
  uint8_t t = 0;
  return e && SafeRead8(base, e + 2, &t) ? t : 0;
}

const char* SymbolTypeName(int type) {
  switch (type) {
    case 0: return "inexistente";
    case kInt: return "entero";
    case kFloat: return "real";
    case kString: return "texto";
    case 4: return "texto localizado";
    case 5: return "par";
    case 6: return "vector";
    case kScript: return "script";
    case kCFunc: return "funcion del motor";
    case 9: return "funcion miembro";
    case kStruct: return "estructura";
    case kArray: return "array";
    case kName: return "nombre";
    default: return "otro tipo";
  }
}

bool ScriptExists(uint32_t checksum, uint8_t* base) {
  const int t = SymbolType(checksum, base);
  return t == kScript || t == kCFunc;
}
bool ScriptExists(const std::string& name, uint8_t* base) { return ScriptExists(CalculateQbKey(name), base); }

void Submit(const std::string& line, std::function<void(bool, const std::string&)> done) {
  std::lock_guard lock(g_queue_mutex);
  if (g_queue.size() < 256) g_queue.push_back({line, std::move(done)});
}

void ExecuteScript(const std::string& name_and_params) { Submit("run " + name_and_params); }

void OpenDebugMenu() { Submit("debugmenu"); }

void UnlockEverything() {
  Submit("run debug_menu_unlockall");
  Submit("run debug_unlock_career_songs");
}

void TriggerCheat(BuiltinCheat cheat) {
  switch (cheat) {
    case BuiltinCheat::kHyperspeed: Submit("runfirst Cheat_Hyperspeed cheat_hyperspeed"); break;
    case BuiltinCheat::kInvincible: Submit("runfirst Cheat_Invincible cheat_invincible_toggle cheat_nofail"); break;
    case BuiltinCheat::kAlwaysSlide: Submit("runfirst Cheat_AlwaysSlide cheat_alwaysslide"); break;
    case BuiltinCheat::kPerformanceMode: Submit("runfirst Cheat_PerformanceMode cheat_performancemode"); break;
    case BuiltinCheat::kAutoPlay: Submit("runfirst Cheat_AutoPlay cheat_autoplay"); break;
    case BuiltinCheat::kHideHud: Submit("runfirst Cheat_HUDFreeMode Cheat_HUD_Free guitar_destroy_hud_elements"); break;
    case BuiltinCheat::kFreeDrum: Submit("runfirst Cheat_FreeDrum cheat_freedrum"); break;
  }
}

void SetDebugBuild(bool enabled) { g_debug_build.store(enabled, std::memory_order_relaxed); }
bool IsDebugBuild() { return g_debug_build.load(std::memory_order_relaxed); }

void OnLogicFrame(PPCContext& ctx, uint8_t* base) {
  if (!base) return;
  // scripts propios: en cuanto el juego termina de arrancar (antes de que se abra ningun menu)
  if (!g_scripts_loaded.load(std::memory_order_relaxed) && BootDone()) {
    g_scripts_loaded = true;
    LoadScriptFolder(ctx, base);
    ApplyEngineLimitsPatches(base);
  }
  std::vector<Order> todo;
  {
    std::lock_guard lock(g_queue_mutex);
    if (g_queue.empty()) return;
    todo.swap(g_queue);
  }
  for (auto& o : todo) {
    std::string reply;
    const bool ok = Dispatch(ctx, base, o.line, reply);
    AddLog(o.line + " -> " + reply, ok);
    if (o.done) o.done(ok, reply);
  }
}

size_t GetLoadedScriptCount(uint8_t* base) {
  uint32_t table = 0;
  if (!SafeRead32(base, kSymbolTable, &table) || !table) return 0;
  size_t count = 0;
  for (uint32_t b = 0; b <= 0x1FFFC; b += 4) {
    uint32_t e = 0;
    if (!SafeRead32(base, table + b, &e)) continue;
    for (int n = 0; e && n < 256; ++n) {
      ++count;
      if (!SafeRead32(base, e + 16, &e)) break;
    }
  }
  return count;
}

std::string PeekGlobal(const std::string& name, uint8_t* base) {
  const uint32_t e = FindSymbol(base, CalculateQbKey(name));
  uint8_t type = 0;
  if (!e || !SafeRead8(base, e + 2, &type)) return {};
  return ReadGlobal(base, e, type);
}

const std::vector<DebugScreen>& DebugScreens() {
  static const std::vector<DebugScreen> k = {
      {"UIstate_debug", "Menu de depuracion"},
      {"UIstate_debug_color_calibration", "Calibracion de color"},
      {"UIstate_debug_brightness_calibration", "Calibracion de brillo"},
      {"UIstate_debug_gamma_calibration", "Calibracion de gamma"},
      {"UIstate_debug_color_histogram", "Histograma de color"},
      {"UIstate_debug_atvi_color_calibration", "Calibracion de color (Activision)"},
  };
  return k;
}

void SetGuestAllocator(std::function<uint32_t(uint32_t)> alloc) { g_alloc = std::move(alloc); }
bool ScriptsLoaded() { return g_scripts_loaded.load() && g_scripts_count.load() > 0; }

bool ReadIntGlobal(const std::string& name, uint8_t* base, int* out) {
  const uint32_t e = FindSymbol(base, CalculateQbKey(name));
  uint8_t type = 0;
  uint32_t raw = 0;
  if (!e || !SafeRead8(base, e + 2, &type) || type != kInt || !SafeRead32(base, e + 12, &raw)) return false;
  *out = static_cast<int32_t>(raw);
  return true;
}

bool WriteIntGlobal(const std::string& name, uint8_t* base, int value) {
  const uint32_t e = FindSymbol(base, CalculateQbKey(name));
  uint8_t type = 0;
  if (!e || !SafeRead8(base, e + 2, &type) || type != kInt) return false;
  REX_STORE_U32(e + 12, static_cast<uint32_t>(value));
  return true;
}

bool WriteFloatGlobal(const std::string& name, uint8_t* base, float value) {
  const uint32_t e = FindSymbol(base, CalculateQbKey(name));
  uint8_t type = 0;
  if (!e || !SafeRead8(base, e + 2, &type) || type != kFloat) return false;
  uint32_t raw = 0;
  std::memcpy(&raw, &value, sizeof(float));
  REX_STORE_U32(e + 12, raw);
  return true;
}

void ApplyEngineLimitsPatches(uint8_t* base) {
  if (!base) return;
  int count = 0;
  count += WriteIntGlobal("default_far_plane_distance", base, 1500) ? 1 : 0;
  count += WriteIntGlobal("cull_max_instances", base, 1500) ? 1 : 0;
  count += WriteIntGlobal("cull_max_geom", base, 3500) ? 1 : 0;
  count += WriteIntGlobal("cull_max_occluder", base, 512) ? 1 : 0;
  count += WriteFloatGlobal("cull_smallobj", base, 0.000001f) ? 1 : 0;
  count += WriteIntGlobal("max_streams", base, 32) ? 1 : 0;
  count += WriteIntGlobal("max_sound_entries", base, 3072) ? 1 : 0;
  count += WriteIntGlobal("num_sound_instances", base, 128) ? 1 : 0;
  count += WriteIntGlobal("pool_text_instances", base, 2048) ? 1 : 0;
  count += WriteIntGlobal("fmod_memory_size", base, 25165824) ? 1 : 0;
  count += WriteIntGlobal("cpu_skinning_buffer_size", base, 40000) ? 1 : 0;
  count += WriteIntGlobal("cpu_skin_wrap_buffer_size", base, 15000) ? 1 : 0;
  count += WriteIntGlobal("node_instances_buff", base, 600) ? 1 : 0;
  count += WriteIntGlobal("node_statics_buff", base, 500) ? 1 : 0;
  count += WriteIntGlobal("node_sort_buff", base, 512) ? 1 : 0;
  count += WriteIntGlobal("node_misc_buff", base, 4000) ? 1 : 0;
  count += WriteIntGlobal("node_2d_buff", base, 600) ? 1 : 0;
  count += WriteIntGlobal("node_particle_buff", base, 2500) ? 1 : 0;
  count += WriteIntGlobal("node_particle_node", base, 512) ? 1 : 0;
  count += WriteIntGlobal("node_light_buff", base, 300) ? 1 : 0;
  count += WriteIntGlobal("target_ntsc_framerate", base, 240) ? 1 : 0;
  count += WriteIntGlobal("gpu_vsync_time_interval", base, 0) ? 1 : 0;
  TRACE("[QBBridge] Parche en memoria de limites Xbox 360 aplicado a tabla QB: " << count << " simbolos actualizados");
}

std::vector<LogEntry> GetRecentLogs() {
  std::lock_guard lock(g_log_mutex);
  return g_recent_logs;
}

// ---------------------------------------------------------------- servidor
void StartServer(int port) {
#ifdef _WIN32
  if (port <= 0 || g_server_port.load()) return;
  WSADATA wsa;
  if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return;
  SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (s == INVALID_SOCKET) return;
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(static_cast<u_short>(port));
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);   // solo este equipo
  if (bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof addr) != 0 || listen(s, 2) != 0) {
    closesocket(s);
    TRACE("[QBBridge] puerto " << port << " ocupado: puente local apagado");
    return;
  }
  std::random_device rd;
  char tok[33];
  std::snprintf(tok, sizeof tok, "%08x%08x%08x%08x", rd(), rd(), rd(), rd());
  g_token = tok;
  {
    // la clave se escribe junto al registro del juego: quien pueda leer esta carpeta puede usar el puente
    std::ofstream f("dev_bridge.json", std::ios::trunc);
    f << "{\"port\": " << port << ", \"token\": \"" << g_token << "\"}\n";
  }
  g_listen = s;
  g_server_stop = false;
  g_server_port = port;
  g_server_thread = std::thread([] {
    while (!g_server_stop) {
      SOCKET c = accept(g_listen, nullptr, nullptr);
      if (c == INVALID_SOCKET) break;
      Serve(c);
      closesocket(c);
    }
  });
  TRACE("[QBBridge] puente local en 127.0.0.1:" << port);
#endif
}

void StopServer() {
#ifdef _WIN32
  if (!g_server_port.load()) return;
  g_server_stop = true;
  closesocket(g_listen);
  g_listen = INVALID_SOCKET;
  if (g_server_thread.joinable()) g_server_thread.detach();
  g_server_port = 0;
  std::remove("dev_bridge.json");
#endif
}

int ServerPort() { return g_server_port.load(); }

}  // namespace re_wor::de::qb_bridge

// Consola del SDK (tecla de [hotkeys].console): qb <orden>. La respuesta sale en la propia consola.
REXCVAR_DEFINE_COMMAND_ARGS(qb, [](std::string_view args) { re_wor::de::qb_bridge::Submit(std::string(args)); },
                            "DE", "Puente QB: qb run <script> [k=v], qb get/set <global>, qb exists, qb debugmenu");

// DebugBuild() de los scripts: en la version de tienda siempre 0; aqui responde lo que diga el puente.
extern "C" REX_FUNC(sub_8259EBA8) {
  (void)base;
  ctx.r3.u64 = re_wor::de::qb_bridge::IsDebugBuild() ? 1 : 0;
}
