// Maquina de wui en C++ (ver wui_vm.hpp). Traduccion directa de tools/wui/wui/runtime (Python) y
// DEStudio/web/src/wui/vm.js: si cambias algo aqui, cambialo alli (y pasa tools/wui/tests/conformance).
#include "de/wui_vm.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <regex>

#include "de/wui_internal.hpp"

namespace wui {

#include "de/wui_tables.inc"

namespace {
enum Op : uint8_t {
  NOP = 0, CONST = 1, NONE = 2, TRUE_ = 3, FALSE_ = 4, POP = 5, DUP = 6, SWAP = 7, LOADL = 10, STOREL = 11, LOADG = 12,
  STOREG = 13, NEG = 20, NOT = 21, POS = 22, ADD = 30, SUB = 31, MUL = 32, DIV = 33, IDIV = 34, MOD = 35, POW = 36,
  EQ = 40, NE = 41, LT = 42, LE = 43, GT = 44, GE = 45, IN = 46, NOTIN = 47, JMP = 50, JF = 51, JT = 52, JFK = 53,
  JTK = 54, CALL = 60, CALLB = 61, CALLM = 62, RET = 63, RETN = 64, FREF = 65, CALLV = 66, GETA = 70, SETA = 71,
  GETI = 72, SETI = 73, LIST = 74, FORMAT = 75, CONCAT = 76, WAIT = 80,
};
constexpr int kNoFunc = 0xFFFF;
constexpr int kMaxDepth = 200;
const char* kInit = "__init__";
}  // namespace

// ================================================================== tablas
const std::vector<MethodSig>& Methods() {
  static const std::vector<MethodSig> v(std::begin(kMethods), std::end(kMethods));
  return v;
}
const std::vector<PropSig>& Props() {
  static const std::vector<PropSig> v(std::begin(kProps), std::end(kProps));
  return v;
}
const PropSig* FindProp(const std::string& name) {
  static const std::unordered_map<std::string, const PropSig*> m = [] {
    std::unordered_map<std::string, const PropSig*> r;
    for (const auto& p : Props()) r[p.name] = &p;
    return r;
  }();
  auto it = m.find(name);
  return it == m.end() ? nullptr : it->second;
}
#define WUI_NAMES(fn, arr) \
  const std::vector<std::string>& fn() { static const std::vector<std::string> v(std::begin(arr), std::end(arr)); return v; }
WUI_NAMES(ButtonNames, kButtons)
WUI_NAMES(SoundNames, kSounds)
WUI_NAMES(PlayerFields, kPlayerFields)
WUI_NAMES(BandFields, kBandFields)
#undef WUI_NAMES

// ================================================================== utf-8
std::vector<uint32_t> Utf8Decode(const std::string& s) {
  std::vector<uint32_t> out;
  out.reserve(s.size());
  for (size_t i = 0; i < s.size();) {
    const auto c = static_cast<unsigned char>(s[i]);
    uint32_t cp = c;
    int n = 0;
    if (c >= 0xF0) { cp = c & 0x07; n = 3; }
    else if (c >= 0xE0) { cp = c & 0x0F; n = 2; }
    else if (c >= 0xC0) { cp = c & 0x1F; n = 1; }
    ++i;
    for (int k = 0; k < n && i < s.size(); ++k, ++i) cp = (cp << 6) | (static_cast<unsigned char>(s[i]) & 0x3F);
    out.push_back(cp);
  }
  return out;
}
std::string Utf8Encode(const std::vector<uint32_t>& cps) {
  std::string out;
  for (uint32_t c : cps) {
    if (c < 0x80) out += static_cast<char>(c);
    else if (c < 0x800) { out += static_cast<char>(0xC0 | (c >> 6)); out += static_cast<char>(0x80 | (c & 0x3F)); }
    else if (c < 0x10000) {
      out += static_cast<char>(0xE0 | (c >> 12)); out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
      out += static_cast<char>(0x80 | (c & 0x3F));
    } else {
      out += static_cast<char>(0xF0 | (c >> 18)); out += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
      out += static_cast<char>(0x80 | ((c >> 6) & 0x3F)); out += static_cast<char>(0x80 | (c & 0x3F));
    }
  }
  return out;
}

// ================================================================== texto
std::string TypeName(const Value& v) {
  switch (v.t) {
    case V::T::None: return "nada";
    case V::T::Bool: return "bool";
    case V::T::Int: return "entero";
    case V::T::Float: return "decimal";
    case V::T::Str: return "texto";
    case V::T::List: return "lista";
    case V::T::Layer: return "capa";
    case V::T::Func: return "funcion";
    case V::T::Task: return "tarea";
    default: return "?";
  }
}
std::string ReceiverOf(const Value& v) {
  switch (v.t) {
    case V::T::Layer: return "Layer";
    case V::T::Task: return "Task";
    case V::T::List: return "list";
    case V::T::Str: return "str";
    default: return TypeName(v);
  }
}

std::string ReprFloat(double x) {
  if (std::isnan(x)) return "nan";
  if (std::isinf(x)) return x > 0 ? "inf" : "-inf";
  if (x == 0) return std::signbit(x) ? "-0.0" : "0.0";
  char buf[64];
  auto r = std::to_chars(buf, buf + sizeof buf, x, std::chars_format::scientific);   // digitos mas cortos
  std::string m(buf, r.ptr);
  const auto epos = m.find('e');
  const int e = std::stoi(m.substr(epos + 1));
  std::string mant = m.substr(0, epos);
  const bool neg = mant[0] == '-';
  if (neg) mant.erase(0, 1);
  std::string digits;
  for (char ch : mant) if (ch != '.') digits += ch;
  std::string s;
  if (e >= -4 && e < 16) {
    if (e >= 0) {
      std::string ip = digits.substr(0, std::min<size_t>(digits.size(), e + 1));
      while (static_cast<int>(ip.size()) < e + 1) ip += '0';
      std::string fp = static_cast<int>(digits.size()) > e + 1 ? digits.substr(e + 1) : "";
      s = ip + "." + (fp.empty() ? "0" : fp);
    } else {
      s = "0." + std::string(-e - 1, '0') + digits;
    }
  } else {
    char eb[16];
    std::snprintf(eb, sizeof eb, "e%c%02d", e < 0 ? '-' : '+', std::abs(e));
    s = digits.substr(0, 1) + (digits.size() > 1 ? "." + digits.substr(1) : "") + eb;
  }
  return (neg ? "-" : "") + s;
}

static std::string PyRepr(const std::string& s) {
  const bool sq = s.find('\'') != std::string::npos, dq = s.find('"') != std::string::npos;
  const char q = sq && !dq ? '"' : '\'';
  std::string out(1, q);
  for (unsigned char ch : s) {
    if (ch == '\\') out += "\\\\";
    else if (ch == q) { out += '\\'; out += static_cast<char>(q); }
    else if (ch == '\n') out += "\\n";
    else if (ch == '\r') out += "\\r";
    else if (ch == '\t') out += "\\t";
    else if (ch < 0x20 || ch == 0x7F) { char b[8]; std::snprintf(b, sizeof b, "\\x%02x", ch); out += b; }
    else out += static_cast<char>(ch);
  }
  return out + q;
}

std::string ToText(const Value& v) {
  switch (v.t) {
    case V::T::None: return "nada";
    case V::T::Bool: return v.b ? "True" : "False";
    case V::T::Int: return std::to_string(v.i);
    case V::T::Float: {
      if (std::isfinite(v.f) && v.f == std::floor(v.f) && std::fabs(v.f) < 1e15) {
        return std::to_string(static_cast<int64_t>(v.f)) + ".0";
      }
      return ReprFloat(v.f);
    }
    case V::T::Str: return v.s;
    case V::T::List: {
      std::string out = "[";
      bool first = true;
      for (const auto& x : v.l->v) {
        if (!first) out += ", ";
        first = false;
        out += x.t == V::T::Str ? PyRepr(x.s) : ToText(x);
      }
      return out + "]";
    }
    case V::T::Layer: return "capa(" + v.s + ")";
    case V::T::Func: return "funcion " + v.s;
    case V::T::Task: return "tarea " + (v.task && !v.task->name.empty() ? v.task->name : std::to_string(v.task ? v.task->id : 0));
    default: return "?";
  }
}

bool Truthy(const Value& v) {
  switch (v.t) {
    case V::T::None: return false;
    case V::T::Bool: return v.b;
    case V::T::Int: return v.i != 0;
    case V::T::Float: return v.f != 0;
    case V::T::Str: return !v.s.empty();
    case V::T::List: return !v.l->v.empty();
    default: return true;
  }
}

bool Eq(const Value& a, const Value& b) {
  if (a.numb() && b.numb()) {
    if (a.t != V::T::Float && b.t != V::T::Float) {
      const int64_t x = a.t == V::T::Bool ? a.b : a.i, y = b.t == V::T::Bool ? b.b : b.i;
      return x == y;
    }
    return a.d() == b.d();
  }
  if (a.t != b.t) return false;
  switch (a.t) {
    case V::T::None: return true;
    case V::T::Str: case V::T::Layer: return a.s == b.s;
    case V::T::Func: return a.i == b.i;
    case V::T::Task: return a.task == b.task;
    case V::T::List: {
      if (a.l == b.l) return true;
      if (a.l->v.size() != b.l->v.size()) return false;
      for (size_t i = 0; i < a.l->v.size(); ++i) if (!Eq(a.l->v[i], b.l->v[i])) return false;
      return true;
    }
    default: return false;
  }
}

int Cmp(const Value& a, const Value& b, bool* ok) {
  *ok = true;
  if (a.numb() && b.numb()) {
    if (a.t != V::T::Float && b.t != V::T::Float) {
      const int64_t x = a.t == V::T::Bool ? a.b : a.i, y = b.t == V::T::Bool ? b.b : b.i;
      return x < y ? -1 : x > y ? 1 : 0;
    }
    const double x = a.d(), y = b.d();
    return x < y ? -1 : x > y ? 1 : 0;
  }
  if (a.t == V::T::Str && b.t == V::T::Str) {
    const auto A = Utf8Decode(a.s), B = Utf8Decode(b.s);
    return A < B ? -1 : A > B ? 1 : 0;
  }
  if (a.t == V::T::List && b.t == V::T::List) {
    const auto& A = a.l->v; const auto& B = b.l->v;
    for (size_t i = 0; i < std::min(A.size(), B.size()); ++i) if (!Eq(A[i], B[i])) return Cmp(A[i], B[i], ok);
    return A.size() < B.size() ? -1 : A.size() > B.size() ? 1 : 0;
  }
  *ok = false;
  return 0;
}

// ---------------------------------------------------------------- formato (mini lenguaje de Python)
std::string FixedStr(double x, int p) {
  if (std::isnan(x)) return "nan";
  if (std::isinf(x)) return x > 0 ? "inf" : "-inf";
  // printf con precision: valor binario exacto y empates al par (como Python)
  std::vector<char> buf(400 + p);
  std::snprintf(buf.data(), buf.size(), "%.*f", p, x);
  return buf.data();
}
static std::string ExpStr(double x, int p, bool upper) {
  if (!std::isfinite(x)) return FixedStr(x, 0);
  char buf[512];
  std::snprintf(buf, sizeof buf, upper ? "%.*E" : "%.*e", p, x);
  return buf;
}
static std::string GenStr(double x, int p, bool alt, bool upper) {
  if (!std::isfinite(x)) return FixedStr(x, 0);
  if (p == 0) p = 1;
  char buf[512];
  std::snprintf(buf, sizeof buf, alt ? (upper ? "%#.*G" : "%#.*g") : (upper ? "%.*G" : "%.*g"), p, x);
  return buf;
}
static std::string Group(const std::string& digits, char sep) {
  std::string out;
  const int n = static_cast<int>(digits.size());
  for (int i = 0; i < n; ++i) {
    out += digits[i];
    if ((n - 1 - i) % 3 == 0 && i != n - 1) out += sep;
  }
  return out;
}

std::string FormatValue(const Value& v, const std::string& spec) {
  if (spec.rfind("!r", 0) == 0) return v.t == V::T::Str ? PyRepr(v.s) : ToText(v);
  if (spec.empty()) return v.t == V::T::Str ? v.s : ToText(v);
  // [[relleno]alineacion][signo][#][0][ancho][,_][.precision][tipo]
  const auto cps = Utf8Decode(spec);
  size_t i = 0;
  uint32_t fill = 0;
  char align = 0, sign = 0, gsep = 0, type = 0;
  bool alt = false, zero = false;
  int width = 0, prec = -1;
  auto isal = [](uint32_t c) { return c == '<' || c == '>' || c == '=' || c == '^'; };
  if (cps.size() >= 2 && isal(cps[1])) { fill = cps[0]; align = static_cast<char>(cps[1]); i = 2; }
  else if (!cps.empty() && isal(cps[0])) { align = static_cast<char>(cps[0]); i = 1; }
  if (i < cps.size() && (cps[i] == '+' || cps[i] == '-' || cps[i] == ' ')) sign = static_cast<char>(cps[i++]);
  if (i < cps.size() && cps[i] == '#') { alt = true; ++i; }
  if (i < cps.size() && cps[i] == '0') { zero = true; ++i; }
  while (i < cps.size() && cps[i] >= '0' && cps[i] <= '9') width = width * 10 + static_cast<int>(cps[i++] - '0');
  if (i < cps.size() && (cps[i] == ',' || cps[i] == '_')) gsep = static_cast<char>(cps[i++]);
  if (i < cps.size() && cps[i] == '.') {
    ++i; prec = 0;
    const size_t s0 = i;
    while (i < cps.size() && cps[i] >= '0' && cps[i] <= '9') prec = prec * 10 + static_cast<int>(cps[i++] - '0');
    if (i == s0) throw Fault{"el formato ':" + spec + "' no es valido"};
  }
  if (i < cps.size() && std::strchr("bcdeEfFgGnosxX%", static_cast<int>(cps[i])) && cps[i] < 128) type = static_cast<char>(cps[i++]);
  if (i != cps.size()) throw Fault{"el formato ':" + spec + "' no es valido"};
  auto bad = [&]() -> void { throw Fault{"el formato ':" + spec + "' no sirve para " + TypeName(v) + " (" + ToText(v) + ")"}; };

  std::string body;
  bool neg = false, numeric = true;
  if (v.t == V::T::Str) {
    if (type && type != 's') bad();
    if (sign || gsep) bad();
    if (prec >= 0) {
      auto c = Utf8Decode(v.s);
      if (static_cast<int>(c.size()) > prec) c.resize(prec);
      body = Utf8Encode(c);
    } else body = v.s;
    numeric = false;
  } else if (v.numb()) {
    const bool isf = v.t == V::T::Float;
    if (!type) type = isf ? (prec < 0 ? 'r' : 'g') : 'd';
    if (std::strchr("dbxXoc", type)) {
      if (isf || prec >= 0) bad();
      int64_t x = v.t == V::T::Bool ? v.b : v.i;
      neg = x < 0;
      uint64_t ux = neg ? 0 - static_cast<uint64_t>(x) : static_cast<uint64_t>(x);
      if (type == 'c') body = Utf8Encode({static_cast<uint32_t>(ux)});
      else {
        const int base = type == 'd' ? 10 : type == 'b' ? 2 : type == 'o' ? 8 : 16;
        do { const int dg = static_cast<int>(ux % base); body.insert(body.begin(), static_cast<char>(dg < 10 ? '0' + dg : (type == 'X' ? 'A' : 'a') + dg - 10)); ux /= base; } while (ux);
        if (alt && type != 'd') body = std::string("0") + type + body;
        if (gsep && type == 'd') body = Group(body, gsep);
      }
    } else if (type == 's') {
      bad();
    } else {
      double x = v.d();
      neg = std::signbit(x) && !std::isnan(x);
      x = std::fabs(x);
      const int p = prec < 0 ? 6 : prec;
      if (type == 'r') body = ToText(V::F(x));
      else if (type == 'f' || type == 'F') body = FixedStr(x, p);
      else if (type == 'e' || type == 'E') body = ExpStr(x, p, type == 'E');
      else if (type == 'g' || type == 'G' || type == 'n') body = GenStr(x, p, alt, type == 'G');
      else if (type == '%') body = FixedStr(x * 100, p) + "%";
      if (gsep) {
        const auto dot = body.find_first_not_of("0123456789");
        const std::string ip = body.substr(0, dot);
        if (!ip.empty()) body = Group(ip, gsep) + (dot == std::string::npos ? "" : body.substr(dot));
      }
      if (type == 'F') for (auto& ch : body) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    }
  } else {
    bad();
  }
  std::string pre = numeric ? (neg ? "-" : sign == '+' ? "+" : sign == ' ' ? " " : "") : "";
  if (zero && !align) { fill = '0'; align = '='; }
  if (!fill) fill = ' ';
  if (!align) align = numeric ? '>' : '<';
  const int len = static_cast<int>(Utf8Decode(pre + body).size());
  if (len >= width) return pre + body;
  auto pad = [&](int n) { std::string s; const std::string f = Utf8Encode({fill}); for (int k = 0; k < n; ++k) s += f; return s; };
  const int n = width - len;
  if (align == '<') return pre + body + pad(n);
  if (align == '>') return pad(n) + pre + body;
  if (align == '=') return pre + pad(n) + body;
  return pad(n / 2) + pre + body + pad(n - n / 2);
}

// ---------------------------------------------------------------- colores y curvas
static const std::unordered_map<std::string, const char*>& NamedColors() {
  static const std::unordered_map<std::string, const char*> m = {
      {"white", "ffffff"}, {"black", "000000"}, {"red", "ff2a2a"}, {"green", "2ee05a"}, {"yellow", "ffd21f"},
      {"blue", "2a8cff"}, {"orange", "ff8a1f"}, {"purple", "9b5cff"}, {"pink", "ff5cc8"}, {"cyan", "2ae6ff"},
      {"gray", "808080"}, {"grey", "808080"}, {"gold", "e3a043"}, {"star", "6ad7ff"}, {"blanco", "ffffff"},
      {"negro", "000000"}, {"rojo", "ff2a2a"}, {"verde", "2ee05a"}, {"amarillo", "ffd21f"}, {"azul", "2a8cff"},
      {"naranja", "ff8a1f"}, {"morado", "9b5cff"}, {"rosa", "ff5cc8"}, {"gris", "808080"}, {"dorado", "e3a043"}};
  return m;
}
static double RoundHalfEven(double x) {
  const double r = std::nearbyint(x);   // modo por defecto: al par
  return r;
}
bool ParseColor(const Value& v, int rgb[3]) {
  if (v.t == V::T::List && v.l->v.size() >= 3 && v.l->v[0].num() && v.l->v[1].num() && v.l->v[2].num()) {
    for (int k = 0; k < 3; ++k) rgb[k] = static_cast<int>(std::clamp(RoundHalfEven(v.l->v[k].d()), 0.0, 255.0));
    return true;
  }
  if (v.t != V::T::Str) return false;
  std::string s = v.s;
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.erase(0, 1);
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
  std::string low = s;
  for (auto& ch : low) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  auto it = NamedColors().find(low);
  if (it != NamedColors().end()) s = it->second;
  else while (!s.empty() && s[0] == '#') s.erase(0, 1);
  if (s.size() == 3) s = {s[0], s[0], s[1], s[1], s[2], s[2]};
  if (s.size() != 6) return false;
  for (char ch : s) if (!std::isxdigit(static_cast<unsigned char>(ch))) return false;
  for (int k = 0; k < 3; ++k) rgb[k] = std::stoi(s.substr(k * 2, 2), nullptr, 16);
  return true;
}
std::string ColorHex(const int rgb[3]) {
  char b[8];
  std::snprintf(b, sizeof b, "#%02x%02x%02x", rgb[0], rgb[1], rgb[2]);
  return b;
}

static double OutBounce(double t) {
  const double n = 7.5625, d = 2.75;
  if (t < 1 / d) return n * t * t;
  if (t < 2 / d) { t -= 1.5 / d; return n * t * t + 0.75; }
  if (t < 2.5 / d) { t -= 2.25 / d; return n * t * t + 0.9375; }
  t -= 2.625 / d;
  return n * t * t + 0.984375;
}
double Ease(const std::string& name, double t) {
  t = t < 0 ? 0 : t > 1 ? 1 : t;
  const double c1 = 1.70158, c3 = c1 + 1, pi = 3.14159265358979323846;
  if (name == "linear") return t;
  if (name == "in") return t * t;
  if (name == "inout") return t < 0.5 ? 2 * t * t : 1 - std::pow(-2 * t + 2, 2) / 2;
  if (name == "in_cubic") return t * t * t;
  if (name == "out_cubic") return 1 - std::pow(1 - t, 3);
  if (name == "inout_cubic") return t < 0.5 ? 4 * t * t * t : 1 - std::pow(-2 * t + 2, 3) / 2;
  if (name == "in_back") return c3 * t * t * t - c1 * t * t;
  if (name == "out_back") return 1 + c3 * std::pow(t - 1, 3) + c1 * std::pow(t - 1, 2);
  if (name == "out_elastic") return t == 0 || t == 1 ? t : std::pow(2, -10 * t) * std::sin((t * 10 - 0.75) * (2 * pi / 3)) + 1;
  if (name == "out_bounce") return OutBounce(t);
  if (name == "in_bounce") return 1 - OutBounce(1 - t);
  if (name == "step") return t >= 1 ? 1 : 0;
  return 1 - (1 - t) * (1 - t);   // out
}

// ================================================================== programa (.wuib)
int Function::LineAt(uint32_t pc) const {
  int ln = 0;
  for (const auto& [at, l] : lines) { if (at <= pc) ln = static_cast<int>(l); else break; }
  return ln;
}

bool LoadProgram(const uint8_t* d, size_t n, Program* out, std::string* error) {
  size_t p = 0;
  struct Cut {};
  auto need = [&](size_t k) { if (p + k > n) throw Cut{}; };
  auto u8 = [&]() { need(1); return d[p++]; };
  auto u16 = [&]() { need(2); uint16_t v; std::memcpy(&v, d + p, 2); p += 2; return v; };
  auto u32 = [&]() { need(4); uint32_t v; std::memcpy(&v, d + p, 4); p += 4; return v; };
  auto str = [&]() { const uint32_t k = u32(); need(k); std::string s(reinterpret_cast<const char*>(d + p), k); p += k; return s; };
  std::function<Value()> cst = [&]() -> Value {
    const uint8_t t = u8();
    switch (t) {
      case 0: return V::N();
      case 1: return V::B(u8() != 0);
      case 2: { need(8); int64_t v; std::memcpy(&v, d + p, 8); p += 8; return V::I(v); }
      case 3: { need(8); double v; std::memcpy(&v, d + p, 8); p += 8; return V::F(v); }
      case 4: return V::S(str());
      case 5: {
        const uint16_t k = u16();
        std::vector<Value> items;
        for (int i = 0; i < k; ++i) items.push_back(cst());
        return V::Lst(std::move(items), true);
      }
      default: throw Fault{"tipo de constante desconocido " + std::to_string(t)};
    }
  };
  try {
    need(4);
    if (std::memcmp(d, "WUIB", 4) != 0) { if (error) *error = "no es un programa de wui (.wuib)"; return false; }
    p = 4;
    const uint16_t ver = u16();
    u16();
    if (ver != 2) { if (error) *error = "version " + std::to_string(ver) + " del programa; esta maquina entiende la 2"; return false; }
    Program pr;
    pr.source = str();
    for (uint32_t k = u32(); k; --k) pr.consts.push_back(cst());
    for (auto* t : {&pr.names, &pr.builtins, &pr.globals}) for (uint16_t k = u16(); k; --k) t->push_back(str());
    for (uint16_t k = u16(); k; --k) {
      Function f;
      f.name = str();
      f.nparams = u8();
      f.nlocals = u16();
      f.flags = u8();
      const uint32_t ni = u32();
      need(static_cast<size_t>(ni) * 8);
      f.code.resize(ni);
      for (uint32_t i = 0; i < ni; ++i) {
        Ins& in = f.code[i];
        in.op = d[p]; in.a = d[p + 1];
        std::memcpy(&in.b, d + p + 2, 2);
        std::memcpy(&in.c, d + p + 4, 4);
        p += 8;
      }
      for (uint32_t m = u32(); m; --m) { const uint32_t a = u32(), b = u32(); f.lines.emplace_back(a, b); }
      for (uint16_t m = u16(); m; --m) f.defaults.push_back(cst());
      for (uint16_t m = u16(); m; --m) f.local_names.push_back(str());
      pr.functions.push_back(std::move(f));
    }
    for (uint16_t k = u16(); k; --k) {
      Handler h;
      h.event = str();
      h.func = u16();
      for (uint16_t m = u16(); m; --m) h.args.push_back(cst());
      pr.handlers.push_back(std::move(h));
    }
    pr.init = u16();
    // comprobaciones para no salirse de las tablas al ejecutar
    for (const auto& f : pr.functions) {
      for (const auto& in : f.code) {
        const bool jump = in.op >= JMP && in.op <= JTK;
        if (jump && (in.c < 0 || static_cast<size_t>(in.c) >= f.code.size())) throw Fault{"salto fuera de la funcion " + f.name};
        if ((in.op == CONST || in.op == FORMAT) && (in.c < 0 || static_cast<size_t>(in.c) >= pr.consts.size())) throw Fault{"constante fuera de rango"};
        if ((in.op == CALLB || in.op == CALLM) && in.c >= 0 && static_cast<size_t>(in.c) >= pr.consts.size()) throw Fault{"constante fuera de rango"};
        if ((in.op == LOADL || in.op == STOREL) && in.b >= f.nlocals) throw Fault{"variable local fuera de rango en " + f.name};
        if ((in.op == LOADG || in.op == STOREG) && in.b >= pr.globals.size()) throw Fault{"variable global fuera de rango"};
        if ((in.op == CALL || in.op == FREF) && in.b >= pr.functions.size()) throw Fault{"funcion fuera de rango"};
        if (in.op == CALLB && in.b >= pr.builtins.size()) throw Fault{"funcion de la libreria fuera de rango"};
        if ((in.op == CALLM || in.op == GETA || in.op == SETA) && in.b >= pr.names.size()) throw Fault{"nombre fuera de rango"};
      }
      if (f.code.empty() || (f.code.back().op != RET && f.code.back().op != RETN && f.code.back().op != JMP)) throw Fault{"funcion sin final: " + f.name};
      if (f.nparams > f.nlocals) throw Fault{"funcion mal formada: " + f.name};
    }
    for (const auto& h : pr.handlers) if (h.func >= static_cast<int>(pr.functions.size())) throw Fault{"evento con funcion fuera de rango"};
    if (pr.init != kNoFunc && pr.init >= static_cast<int>(pr.functions.size())) throw Fault{"funcion de arranque fuera de rango"};
    *out = std::move(pr);
    return true;
  } catch (const Cut&) {
    if (error) *error = "programa cortado";
  } catch (const Fault& f) {
    if (error) *error = f.message;
  }
  return false;
}

// ================================================================== maquina
struct Frame {
  int fi;
  const Function* fn;
  std::vector<Value> locals;
  size_t base;
  uint32_t pc = 0;
};

struct Task {
  int id = 0;
  std::string name;
  int func = 0;
  std::vector<Value> args;
  std::vector<Frame> frames;
  std::vector<Value> stack;
  Block block;
  bool push_result = true;
  double start_at = 0;
  std::optional<double> every;
  bool done = false, stopped = false;
  Value result;
  TaskPtr ref;
  int64_t ran = -1;
};

struct TweenRec {
  int id;
  std::string layer, prop;
  Value start, end;
  bool has_start = false;
  double duration, delay;
  std::string ease;
  int loops;
  bool yoyo;
  double t0;
  std::function<Value(double)> fn;
  bool accumulate = false;
  std::function<void(Machine&)> on_end;
  int cycle = 0;
  bool done = false, started = false;
};

struct Machine::Impl {
  Machine& vm;
  std::vector<const BuiltinFn*> builtins;
  std::vector<std::string> missing;
  std::unordered_map<std::string, const MethodSig*> methods;
  std::vector<std::unique_ptr<Task>> tasks;
  int task_id = 0;
  int budget, max_tasks;
  std::set<std::string> watch;
  std::set<std::pair<int, int>> holds;
  std::map<std::pair<int, int>, double> repeat;
  std::map<int, double> every_at;
  std::map<std::string, Value> game_last;
  // mando
  Inputs now, prev;
  std::map<std::pair<int, std::string>, double> since;
  // azar
  uint32_t mt[624];
  int mti = 625;
  // animaciones
  std::vector<std::unique_ptr<TweenRec>> tweens;
  int next_tween = 1;

  Impl(Machine& m, int b, int mx) : vm(m), budget(b), max_tasks(mx) {}

  // ---------------------------------------------------------------- azar (MT19937 como CPython)
  void InitGenrand(uint32_t s) {
    mt[0] = s;
    for (int i = 1; i < 624; ++i) mt[i] = 1812433253u * (mt[i - 1] ^ (mt[i - 1] >> 30)) + static_cast<uint32_t>(i);
    mti = 624;
  }
  void Seed(uint64_t seed) {
    std::vector<uint32_t> key;
    do { key.push_back(static_cast<uint32_t>(seed & 0xFFFFFFFFu)); seed >>= 32; } while (seed);
    InitGenrand(19650218u);
    int i = 1, j = 0;
    for (int k = std::max<int>(624, static_cast<int>(key.size())); k; --k) {
      mt[i] = (mt[i] ^ ((mt[i - 1] ^ (mt[i - 1] >> 30)) * 1664525u)) + key[j] + static_cast<uint32_t>(j);
      ++i; ++j;
      if (i >= 624) { mt[0] = mt[623]; i = 1; }
      if (j >= static_cast<int>(key.size())) j = 0;
    }
    for (int k = 623; k; --k) {
      mt[i] = (mt[i] ^ ((mt[i - 1] ^ (mt[i - 1] >> 30)) * 1566083941u)) - static_cast<uint32_t>(i);
      ++i;
      if (i >= 624) { mt[0] = mt[623]; i = 1; }
    }
    mt[0] = 0x80000000u;
  }
  uint32_t U32() {
    if (mti >= 624) {
      for (int k = 0; k < 624; ++k) {
        const uint32_t y = (mt[k] & 0x80000000u) | (mt[(k + 1) % 624] & 0x7FFFFFFFu);
        mt[k] = mt[(k + 397) % 624] ^ (y >> 1) ^ ((y & 1) ? 0x9908B0DFu : 0u);
      }
      mti = 0;
    }
    uint32_t y = mt[mti++];
    y ^= y >> 11; y ^= (y << 7) & 0x9D2C5680u; y ^= (y << 15) & 0xEFC60000u; y ^= y >> 18;
    return y;
  }
  uint64_t Bits(int k) {
    if (k <= 32) return U32() >> (32 - k);
    uint64_t r = 0;
    int shift = 0;
    for (int left = k; left > 0; left -= 32, shift += 32) {
      uint64_t w = U32();
      if (left < 32) w >>= 32 - left;
      r |= w << shift;
    }
    return r;
  }

  // ---------------------------------------------------------------- tareas
  Task* NewTask(int fi, std::vector<Value> args, const std::string& name) {
    int live = 0;
    for (const auto& t : tasks) if (!t->done) ++live;
    if (live >= max_tasks) {
      vm.Error("demasiadas tareas a la vez (" + std::to_string(max_tasks) + "): \xC2\xBFun spawn dentro de un bucle?", name, 0, {});
      static Task dead;
      dead = Task{};
      dead.done = true;
      dead.ref = std::make_shared<TaskObj>();
      dead.ref->id = -1;
      return &dead;
    }
    auto t = std::make_unique<Task>();
    t->id = ++task_id;
    t->name = name;
    t->func = fi;
    t->args = std::move(args);
    t->start_at = vm.time;
    t->ref = std::make_shared<TaskObj>();
    t->ref->id = t->id;
    t->ref->name = name;
    Task* raw = t.get();
    tasks.push_back(std::move(t));
    return raw;
  }

  std::vector<Value> Fill(const Function& f, std::vector<Value> args) {
    const int n = f.nparams;
    if (static_cast<int>(args.size()) > n)
      vm.Raise(f.name + "() recibe como mucho " + std::to_string(n) + " argumento(s) y le llegan " + std::to_string(args.size()));
    const int missing = n - static_cast<int>(args.size());
    if (missing > static_cast<int>(f.defaults.size()))
      vm.Raise(f.name + "() necesita " + std::to_string(n - static_cast<int>(f.defaults.size())) + " argumento(s) y le llegan " + std::to_string(args.size()));
    for (int k = static_cast<int>(f.defaults.size()) - missing; k < static_cast<int>(f.defaults.size()); ++k) args.push_back(f.defaults[k]);
    return args;
  }

  void Enter(Task& t, int fi, std::vector<Value> args) {
    if (t.frames.size() >= kMaxDepth) vm.Raise("demasiadas llamadas anidadas (una funcion que se llama a si misma sin parar?)");
    const Function& f = vm.prog_.functions[fi];
    Frame fr{fi, &f, std::move(args), t.stack.size(), 0};
    fr.locals.resize(std::max<size_t>(fr.locals.size(), f.nlocals), V::Unset());
    t.frames.push_back(std::move(fr));
  }

  void TaskError(Task& t, const Fault& e) {
    std::vector<std::string> trace;
    for (const auto& f : t.frames) trace.push_back(f.fn->name + "() linea " + std::to_string(f.fn->LineAt(f.pc ? f.pc - 1 : 0)));
    std::string fname = t.name;
    int line = 0;
    if (!t.frames.empty()) { const Frame& fr = t.frames.back(); fname = fr.fn->name; line = fr.fn->LineAt(fr.pc ? fr.pc - 1 : 0); }
    t.done = true;
    t.frames.clear();
    vm.Error(e.message, fname, line, trace);
  }

  struct SyncFault { Fault f; std::string fn; int line; };

  void Run(Task& t) {
    try {
      Exec(t);
    } catch (const Fault& e) {
      if (t.id == -1) {
        std::string fn = t.name;
        int line = 0;
        if (!t.frames.empty()) { const Frame& fr = t.frames.back(); fn = fr.fn->name; line = fr.fn->LineAt(fr.pc ? fr.pc - 1 : 0); }
        throw SyncFault{e, fn, line};
      }
      TaskError(t, e);
    }
  }

  Value CallSync(int fi, std::vector<Value> args, const std::string& what) {
    const Function& f = vm.prog_.functions[fi];
    Task t;
    t.id = -1;
    t.name = f.name;
    t.func = fi;
    t.args = Fill(f, std::move(args));
    t.ref = std::make_shared<TaskObj>();
    Enter(t, fi, t.args);
    Run(t);
    if (!t.done) vm.Raise(what + " no puede esperar (wait) aqui");
    return t.result;
  }

  void RunTasks() {
    int rounds = 0;
    bool ran = true;
    while (ran && rounds < 64) {
      ran = false;
      ++rounds;
      std::vector<Task*> snap;
      for (auto& t : tasks) snap.push_back(t.get());
      for (Task* t : snap) {
        if (t->done || t->start_at > vm.time + 1e-9 || t->ran == vm.frame) continue;
        if (t->block) {
          Value val;
          bool ok;
          try { ok = t->block(vm, &val); } catch (const Fault& e) { TaskError(*t, e); continue; }
          if (!ok) { t->ran = vm.frame; continue; }
          t->block = nullptr;
          if (t->push_result) t->stack.push_back(val);
        }
        if (t->frames.empty()) {
          try { Enter(*t, t->func, t->args); } catch (const Fault& e) { TaskError(*t, e); continue; }
        }
        t->ran = vm.frame;
        ran = true;
        Run(*t);
        if (t->done && t->every && !t->stopped) {
          t->done = false;
          t->frames.clear();
          t->stack.clear();
          t->start_at = vm.time + *t->every;
          t->ran = vm.frame;
        }
      }
    }
    for (auto& t : tasks) if (t->done) t->ref->result = t->result;
    tasks.erase(std::remove_if(tasks.begin(), tasks.end(), [](const auto& t) { return t->done; }), tasks.end());
  }

  // ---------------------------------------------------------------- ejecucion
  static Value Binary(Machine& vm, uint8_t op, const Value& a, const Value& b);
  static Value Compare(Machine& vm, uint8_t op, const Value& a, const Value& b);
  static Value GetIndex(Machine& vm, const Value& seq, const Value& i);
  static void SetIndex(Machine& vm, Value& seq, const Value& i, Value v);

  void Exec(Task& t) {
    auto& S = t.stack;
    const Program& P = vm.prog_;
    int left = budget;
    auto pop = [&]() { Value v = std::move(S.back()); S.pop_back(); return v; };
    while (!t.frames.empty()) {
      Frame* fr = &t.frames.back();
      bool reenter = false;
      while (!reenter) {
        if (--left < 0) vm.Raise(fr->fn->name + "() lleva demasiadas instrucciones sin esperar: si es un bucle, pon wait() dentro");
        if (fr->pc >= fr->fn->code.size()) vm.Raise("instruccion fuera de la funcion");
        const Ins in = fr->fn->code[fr->pc++];
        switch (in.op) {
          case CONST: S.push_back(P.consts[in.c]); break;
          case LOADL: {
            const Value& v = fr->locals[in.b];
            if (v.t == V::T::Unset) {
              const std::string nm = in.b < fr->fn->local_names.size() ? fr->fn->local_names[in.b] : "#" + std::to_string(in.b);
              vm.Raise("la variable '" + nm + "' se usa antes de darle un valor");
            }
            S.push_back(v);
            break;
          }
          case STOREL: fr->locals[in.b] = pop(); break;
          case LOADG: S.push_back(vm.globals_[in.b]); break;
          case STOREG: {
            Value v = pop();
            Value old = vm.globals_[in.b];
            vm.globals_[in.b] = v;
            const std::string& gname = P.globals[in.b];
            if (!watch.empty() && watch.count(gname) && !(TypeName(old) == TypeName(v) && Eq(old, v))) {
              vm.Fire("on_change", {v, old}, [&](const Handler& h) { return !h.args.empty() && h.args[0].t == V::T::Str && h.args[0].s == gname; });
            }
            fr = &t.frames.back();
            break;
          }
          case JMP: fr->pc = in.c; break;
          case JF: if (!Truthy(pop())) fr->pc = in.c; break;
          case JT: if (Truthy(pop())) fr->pc = in.c; break;
          case JFK: if (!Truthy(S.back())) fr->pc = in.c; else S.pop_back(); break;
          case JTK: if (Truthy(S.back())) fr->pc = in.c; else S.pop_back(); break;
          case ADD: case SUB: case MUL: case DIV: case IDIV: case MOD: case POW: {
            Value r = pop();
            S.back() = Binary(vm, in.op, S.back(), r);
            break;
          }
          case EQ: case NE: case LT: case LE: case GT: case GE: case IN: case NOTIN: {
            Value r = pop();
            S.back() = Compare(vm, in.op, S.back(), r);
            break;
          }
          case NONE: S.push_back(V::N()); break;
          case TRUE_: S.push_back(V::B(true)); break;
          case FALSE_: S.push_back(V::B(false)); break;
          case POP: S.pop_back(); break;
          case DUP: S.push_back(S.back()); break;
          case SWAP: std::swap(S[S.size() - 1], S[S.size() - 2]); break;
          case NEG: {
            Value& v = S.back();
            if (!v.num()) vm.Raise("no se puede poner '-' delante de " + TypeName(v));
            if (v.t == V::T::Float) v.f = -v.f;
            else { if (v.i == std::numeric_limits<int64_t>::min()) vm.Raise("numero entero demasiado grande"); v.i = -v.i; }
            break;
          }
          case POS: if (!S.back().num()) vm.Raise("no se puede poner '+' delante de " + TypeName(S.back())); break;
          case NOT: S.back() = V::B(!Truthy(S.back())); break;
          case CALLB: {
            const BuiltinFn* impl = builtins[in.b];
            std::vector<Value> args(std::make_move_iterator(S.end() - in.a), std::make_move_iterator(S.end()));
            S.resize(S.size() - in.a);
            Kw kw;
            if (in.c >= 0) {
              const auto& kn = P.consts[in.c].l->v;
              const size_t base = args.size() - kn.size();
              for (size_t k = 0; k < kn.size(); ++k) kw.emplace_back(kn[k].s, std::move(args[base + k]));
              args.resize(base);
            }
            Ctx ctx{vm, &t, nullptr};
            Value r = (*impl)(ctx, args, kw);
            if (ctx.block) { t.block = std::move(ctx.block); t.push_result = true; return; }
            S.push_back(std::move(r));
            fr = &t.frames.back();
            break;
          }
          case CALLM: {
            Ctx ctx{vm, &t, nullptr};
            Value r = CallMethod(ctx, S, in.a, P.names[in.b], in.c >= 0 ? &P.consts[in.c].l->v : nullptr);
            if (ctx.block) { t.block = std::move(ctx.block); t.push_result = true; return; }
            S.push_back(std::move(r));
            fr = &t.frames.back();
            break;
          }
          case CALL: {
            std::vector<Value> args(std::make_move_iterator(S.end() - in.a), std::make_move_iterator(S.end()));
            S.resize(S.size() - in.a);
            Enter(t, in.b, std::move(args));
            reenter = true;
            break;
          }
          case CALLV: {
            std::vector<Value> args(std::make_move_iterator(S.end() - in.a), std::make_move_iterator(S.end()));
            S.resize(S.size() - in.a);
            Value fnv = pop();
            if (fnv.t != V::T::Func) vm.Raise("no se puede llamar a " + TypeName(fnv) + " (" + ToText(fnv) + ") como si fuera una funcion");
            Enter(t, static_cast<int>(fnv.i), Fill(P.functions[fnv.i], std::move(args)));
            reenter = true;
            break;
          }
          case FREF: S.push_back(V::Fn(in.b, P.functions[in.b].name)); break;
          case RET: case RETN: {
            Value v = in.op == RET ? pop() : V::N();
            const size_t base = fr->base;
            t.frames.pop_back();
            S.resize(base);
            if (!t.frames.empty()) S.push_back(std::move(v));
            else { t.result = std::move(v); t.done = true; }
            reenter = true;
            break;
          }
          case WAIT: {
            Value secs = pop();
            if (!secs.num()) vm.Raise("wait() necesita segundos (un numero), no " + TypeName(secs));
            const double until = vm.time + std::max(0.0, secs.d());
            const int64_t first = vm.frame;
            t.block = [until, first](Machine& m, Value*) { return m.frame > first && m.time >= until - 1e-9; };
            t.push_result = false;
            return;
          }
          case GETA: S.back() = GetAttr(S.back(), P.names[in.b]); break;
          case SETA: {
            Value v = pop();
            Value obj = pop();
            SetAttr(obj, P.names[in.b], std::move(v));
            break;
          }
          case GETI: { Value i = pop(); S.back() = GetIndex(vm, S.back(), i); break; }
          case SETI: { Value v = pop(); Value i = pop(); Value seq = pop(); SetIndex(vm, seq, i, std::move(v)); break; }
          case LIST: {
            std::vector<Value> items(std::make_move_iterator(S.end() - in.b), std::make_move_iterator(S.end()));
            S.resize(S.size() - in.b);
            S.push_back(V::Lst(std::move(items)));
            break;
          }
          case FORMAT: S.back() = V::S(FormatValue(S.back(), P.consts[in.c].s)); break;
          case CONCAT: {
            std::string out;
            for (size_t k = S.size() - in.b; k < S.size(); ++k) out += S[k].t == V::T::Str ? S[k].s : ToText(S[k]);
            S.resize(S.size() - in.b);
            S.push_back(V::S(std::move(out)));
            break;
          }
          case NOP: break;
          default: vm.Raise("instruccion desconocida " + std::to_string(in.op));
        }
      }
    }
  }

  Value CallMethod(Ctx& ctx, std::vector<Value>& S, int argc, const std::string& name, const std::vector<Value>* kwnames) {
    std::vector<Value> args(std::make_move_iterator(S.end() - argc), std::make_move_iterator(S.end()));
    S.resize(S.size() - argc);
    Value obj = std::move(S.back());
    S.pop_back();
    const std::string key = ReceiverOf(obj) + "." + name;
    auto mi = methods.find(key);
    const auto& lib = Stdlib();
    auto li = lib.find(key);
    if (mi == methods.end() || li == lib.end()) vm.Raise(TypeName(obj) + " no tiene el metodo ." + name + "()");
    const size_t nk = kwnames ? kwnames->size() : 0;
    std::vector<Value> pos;
    pos.push_back(std::move(obj));
    for (size_t k = 0; k + nk < args.size(); ++k) pos.push_back(std::move(args[k]));
    Kw kw;
    for (size_t k = 0; k < nk; ++k) kw.emplace_back((*kwnames)[k].s, std::move(args[args.size() - nk + k]));
    // ordenar segun la firma (bind_runtime)
    const auto& sig = mi->second->params;
    std::vector<const ParamSig*> fixed, normal;
    bool varargs = false, varkw = false;
    for (const auto& p : sig) {
      if (p.kind == 0 || p.kind == 1) fixed.push_back(&p);
      if (p.kind == 0) normal.push_back(&p);
      if (p.kind == 2) varargs = true;
      if (p.kind == 3) varkw = true;
    }
    std::map<std::string, Value> slots;
    std::vector<Value> extra;
    for (size_t k = 0; k < pos.size(); ++k) {
      if (k < normal.size()) slots[normal[k]->name] = std::move(pos[k]);
      else if (varargs) extra.push_back(std::move(pos[k]));
      else vm.Raise("." + name + "() recibe como mucho " + std::to_string(normal.size() - 1) + " argumento(s)");
    }
    Kw extra_kw;
    for (auto& [k, v] : kw) {
      if (slots.count(k)) vm.Raise("." + name + "(): '" + k + "' esta dos veces");
      const bool known = std::any_of(fixed.begin(), fixed.end(), [&](const ParamSig* p) { return k == p->name; });
      if (known) slots[k] = std::move(v);
      else if (varkw) extra_kw.emplace_back(k, std::move(v));
      else vm.Raise("." + name + "() no tiene un argumento '" + k + "'");
    }
    std::vector<Value> full;
    for (const ParamSig* p : fixed) {
      auto it = slots.find(p->name);
      if (it != slots.end()) full.push_back(std::move(it->second));
      else if (!p->has) vm.Raise("." + name + "(): falta el argumento '" + std::string(p->name) + "'");
      else full.push_back(p->dflt);
    }
    for (auto& v : extra) full.push_back(std::move(v));
    return li->second(ctx, full, extra_kw);
  }

  Value GetAttr(const Value& obj, const std::string& name) {
    if (obj.t == V::T::Layer) return name == "name" ? V::S(obj.s) : vm.LayerGetP(obj.s, name);
    if (obj.t == V::T::Task) {
      if (name == "running") return V::B(vm.TaskRunning(obj.task));
      if (name == "result") return obj.task->result;
    }
    vm.Raise(TypeName(obj) + " no tiene la propiedad '" + name + "'");
  }
  void SetAttr(const Value& obj, const std::string& name, Value v) {
    if (obj.t == V::T::Layer) { vm.LayerSetP(obj.s, name, std::move(v)); return; }
    vm.Raise("no se puede cambiar '" + name + "' de " + TypeName(obj));
  }

  // ---------------------------------------------------------------- animaciones
  Value TweenValue(TweenRec& tw, double u) {
    if (tw.fn) return tw.fn(tw.accumulate ? tw.cycle + u : u);
    const double k = Ease(tw.ease, u);
    if (tw.start.num() && tw.end.num()) return V::F(tw.start.d() + (tw.end.d() - tw.start.d()) * k);
    int ca[3], cb[3];
    if (ParseColor(tw.start, ca) && ParseColor(tw.end, cb)) {
      int r[3];
      for (int i = 0; i < 3; ++i) r[i] = static_cast<int>(RoundHalfEven(ca[i] + (cb[i] - ca[i]) * k));
      return V::S(ColorHex(r));
    }
    return k >= 1 ? tw.end : tw.start;
  }
  void UpdateAnim() {
    const double now = vm.time;
    std::vector<TweenRec*> snap;
    for (auto& t : tweens) snap.push_back(t.get());
    for (TweenRec* tw : snap) {
      if (tw->done) continue;
      const double t = now - tw->t0 - tw->delay;
      if (t < 0) continue;
      if (!tw->started) {
        tw->started = true;
        if (!tw->has_start) {
          tw->start = vm.LayerGetP(tw->layer, tw->prop);
          if (tw->start.none()) tw->start = tw->end;
          tw->has_start = true;
        }
      }
      const double dur = tw->duration > 0 ? tw->duration : 1e-6;
      int cycle = static_cast<int>(std::floor(t / dur));
      double u;
      bool finished;
      if (tw->loops >= 0 && cycle > tw->loops) { u = 1; finished = true; cycle = tw->loops; }
      else { u = (t - cycle * dur) / dur; finished = false; }
      tw->cycle = cycle;
      if (tw->yoyo && cycle % 2 == 1) u = 1 - u;
      Value val = finished && tw->fn && !tw->accumulate ? (tw->yoyo && tw->loops % 2 == 1 ? tw->start : tw->fn(1.0)) : TweenValue(*tw, u);
      vm.LayerSetP(tw->layer, tw->prop, std::move(val), true);
      if (finished) {
        tw->done = true;
        auto on_end = tw->on_end;
        RemoveTween(tw);
        if (on_end) on_end(vm);
      }
    }
  }
  void RemoveTween(TweenRec* tw) {
    tweens.erase(std::remove_if(tweens.begin(), tweens.end(), [tw](const auto& p) { return p.get() == tw; }), tweens.end());
  }
};

// ================================================================== operadores (como operators.py)
static const char* Sym(uint8_t op) {
  switch (op) {
    case ADD: return "+"; case SUB: return "-"; case MUL: return "*"; case DIV: return "/"; case IDIV: return "//";
    case MOD: return "%"; case POW: return "**"; case LT: return "<"; case LE: return "<="; case GT: return ">";
    default: return ">=";
  }
}
static int64_t AsInt(const Value& v) { return v.t == V::T::Bool ? (v.b ? 1 : 0) : v.i; }

Value Machine::Impl::Binary(Machine& vm, uint8_t op, const Value& a, const Value& b) {
  if (a.numb() && b.numb()) {
    const bool fl = a.t == V::T::Float || b.t == V::T::Float;
    if (!fl) {
      const int64_t x = AsInt(a), y = AsInt(b);
      int64_t r;
      switch (op) {
        case ADD: if (__builtin_add_overflow(x, y, &r)) vm.Raise("numero entero demasiado grande"); return V::I(r);
        case SUB: if (__builtin_sub_overflow(x, y, &r)) vm.Raise("numero entero demasiado grande"); return V::I(r);
        case MUL: if (__builtin_mul_overflow(x, y, &r)) vm.Raise("numero entero demasiado grande"); return V::I(r);
        case DIV: if (y == 0) vm.Raise("division entre cero"); return V::F(static_cast<double>(x) / static_cast<double>(y));
        case IDIV: {
          if (y == 0) vm.Raise("division entre cero");
          int64_t q = x / y;
          if ((x % y != 0) && ((x < 0) != (y < 0))) --q;
          return V::I(q);
        }
        case MOD: {
          if (y == 0) vm.Raise("division entre cero");
          int64_t m = x % y;
          if (m != 0 && ((m < 0) != (y < 0))) m += y;
          return V::I(m);
        }
        case POW: {
          if (y < 0) {
            if (x == 0) vm.Raise("potencia demasiado grande o de cero");
            return V::F(std::pow(static_cast<double>(x), static_cast<double>(y)));
          }
          int64_t res = 1, base = x, e = y;
          while (e) {
            if (e & 1) if (__builtin_mul_overflow(res, base, &res)) vm.Raise("numero entero demasiado grande");
            e >>= 1;
            if (e && __builtin_mul_overflow(base, base, &base)) vm.Raise("numero entero demasiado grande");
          }
          return V::I(res);
        }
        default: break;
      }
    } else {
      const double x = a.d(), y = b.d();
      switch (op) {
        case ADD: return V::F(x + y);
        case SUB: return V::F(x - y);
        case MUL: return V::F(x * y);
        case DIV: if (y == 0) vm.Raise("division entre cero"); return V::F(x / y);
        case IDIV: if (y == 0) vm.Raise("division entre cero"); return V::F(std::floor(x / y));
        case MOD: {
          if (y == 0) vm.Raise("division entre cero");
          double m = std::fmod(x, y);
          if (m != 0 && ((m < 0) != (y < 0))) m += y;
          return V::F(m);
        }
        case POW: {
          if (x == 0 && y < 0) vm.Raise("potencia demasiado grande o de cero");
          const double r = std::pow(x, y);
          if (std::isnan(r)) vm.Raise("potencia de un negativo con decimales");
          if (std::isinf(r)) vm.Raise("potencia demasiado grande o de cero");
          return V::F(r);
        }
        default: break;
      }
    }
  }
  if (op == ADD) {
    if (a.t == V::T::Str && b.t == V::T::Str) return V::S(a.s + b.s);
    if (a.t == V::T::List && b.t == V::T::List) {
      std::vector<Value> v = a.l->v;
      v.insert(v.end(), b.l->v.begin(), b.l->v.end());
      return V::Lst(std::move(v));
    }
    if (a.t == V::T::Str || b.t == V::T::Str)
      vm.Raise("no se puede sumar " + TypeName(a) + " (" + ToText(a) + ") y " + TypeName(b) + " (" + ToText(b) +
               "): para unir texto y numeros usa str(x) o un f-string: f\"puntos: {x}\"");
  }
  if (op == MUL) {
    auto rep = [&](const Value& s, int64_t n) {
      n = std::clamp<int64_t>(n, 0, 10000);
      if (s.t == V::T::Str) { std::string out; for (int64_t k = 0; k < n; ++k) out += s.s; return V::S(out); }
      std::vector<Value> out;
      for (int64_t k = 0; k < n; ++k) out.insert(out.end(), s.l->v.begin(), s.l->v.end());
      return V::Lst(std::move(out));
    };
    if ((a.t == V::T::Str || a.t == V::T::List) && b.t == V::T::Int) return rep(a, b.i);
    if ((b.t == V::T::Str || b.t == V::T::List) && a.t == V::T::Int) return rep(b, a.i);
  }
  if (op == MOD && a.t == V::T::Str) vm.Raise("el formato con % no se puede usar: usa un f-string");
  vm.Raise(std::string("no se puede hacer ") + TypeName(a) + " " + Sym(op) + " " + TypeName(b) + " (" + ToText(a) + " " + Sym(op) + " " + ToText(b) + ")");
}

Value Machine::Impl::Compare(Machine& vm, uint8_t op, const Value& a, const Value& b) {
  if (op == EQ) return V::B(Eq(a, b));
  if (op == NE) return V::B(!Eq(a, b));
  if (op == IN || op == NOTIN) {
    bool r = false;
    if (b.t == V::T::List) r = std::any_of(b.l->v.begin(), b.l->v.end(), [&](const Value& x) { return Eq(a, x); });
    else if (b.t == V::T::Str) {
      if (a.t != V::T::Str) vm.Raise("'in' con un texto necesita otro texto, no " + TypeName(a));
      r = b.s.find(a.s) != std::string::npos;
    } else vm.Raise("'in' necesita una lista o un texto a la derecha, no " + TypeName(b));
    return V::B(op == IN ? r : !r);
  }
  if ((a.numb() && b.numb()) || (a.t == V::T::Str && b.t == V::T::Str)) {
    bool ok;
    const int c = Cmp(a, b, &ok);
    return V::B(op == LT ? c < 0 : op == LE ? c <= 0 : op == GT ? c > 0 : c >= 0);
  }
  vm.Raise(std::string("no se puede comparar ") + TypeName(a) + " " + Sym(op) + " " + TypeName(b) + " (" + ToText(a) + ", " + ToText(b) + ")");
}

static int64_t Index(Machine& vm, const Value& i, int64_t n) {
  int64_t k;
  if (i.t == V::T::Int) k = i.i;
  else if (i.t == V::T::Float && std::floor(i.f) == i.f && std::isfinite(i.f)) k = static_cast<int64_t>(i.f);
  else vm.Raise("el indice tiene que ser un entero, no " + TypeName(i) + " (" + ToText(i) + ")");
  if (!(-n <= k && k < n)) vm.Raise("indice " + std::to_string(k) + " fuera de rango (hay " + std::to_string(n) + " elementos: de 0 a " + std::to_string(n - 1) + ")");
  return k < 0 ? k + n : k;
}
Value Machine::Impl::GetIndex(Machine& vm, const Value& seq, const Value& i) {
  if (seq.t == V::T::List) return seq.l->v[Index(vm, i, static_cast<int64_t>(seq.l->v.size()))];
  if (seq.t == V::T::Str) {
    const auto cps = Utf8Decode(seq.s);
    return V::S(Utf8Encode({cps[Index(vm, i, static_cast<int64_t>(cps.size()))]}));
  }
  vm.Raise("no se puede usar [ ] con " + TypeName(seq));
}
void Machine::Impl::SetIndex(Machine& vm, Value& seq, const Value& i, Value v) {
  if (seq.t == V::T::List) {
    if (seq.l->frozen) vm.Raise("esta lista es constante");
    seq.l->v[Index(vm, i, static_cast<int64_t>(seq.l->v.size()))] = std::move(v);
    return;
  }
  if (seq.t == V::T::Str) vm.Raise("los textos no se pueden cambiar letra a letra: crea uno nuevo");
  vm.Raise("no se puede usar [ ] = con " + TypeName(seq));
}

// ================================================================== Machine
Machine::Machine(Program program, Host* h, uint64_t seed, int budget, int max_tasks)
    : host(h), im_(std::make_unique<Impl>(*this, budget, max_tasks)), prog_(std::move(program)) {
  static Host null_host;
  if (!host) host = &null_host;
  const auto& lib = Stdlib();
  for (const auto& n : prog_.builtins) {
    auto it = lib.find(n);
    if (it == lib.end()) { im_->missing.push_back(n); im_->builtins.push_back(nullptr); }
    else im_->builtins.push_back(&it->second);
  }
  for (const auto& m : Methods()) im_->methods[m.name] = &m;
  globals_.assign(prog_.globals.size(), V::N());
  for (const auto& h2 : prog_.handlers) if (h2.event == "on_change" && !h2.args.empty() && h2.args[0].t == V::T::Str) im_->watch.insert(h2.args[0].s);
  im_->Seed(seed);
}
Machine::~Machine() = default;

bool Machine::Ok(std::string* why) const {
  if (im_->missing.empty()) return true;
  if (why) *why = "el programa usa '" + im_->missing[0] + "', que esta version del juego no tiene";
  return false;
}

Value Machine::GlobalValue(const std::string& name) const {
  for (size_t i = 0; i < prog_.globals.size(); ++i) if (prog_.globals[i] == name) return globals_[i];
  return {};
}

void Machine::Error(const std::string& message, const std::string& fname, int line, std::vector<std::string> trace) {
  errors_.push_back({message, fname, line, time, std::move(trace)});
  LogMsg("error", message + " en " + fname + "()" + (line ? ", linea " + std::to_string(line) : ""));
}

void Machine::LogMsg(const std::string& level, const std::string& message) {
  logs_.emplace_back(level, message);
  if (logs_.size() > 4000) logs_.erase(logs_.begin(), logs_.begin() + 1000);
  host->Log(level, message);
}

void Machine::Start(const std::string& screen) {
  if (started_) return;
  started_ = true;
  if (!Ok()) return;
  if (prog_.init != kNoFunc) {
    auto w = std::move(im_->watch);
    im_->watch.clear();
    try {
      im_->CallSync(prog_.init, {}, "el codigo de arriba del todo");
    } catch (const Impl::SyncFault& e) {
      Error(e.f.message, e.fn, e.line, {});
    } catch (const Fault& e) {
      Error(e.message, kInit, 0, {});
    }
    im_->watch = std::move(w);
  }
  Fire("on_start", {});
  const auto names = host->ScreenNames();
  std::optional<std::string> first;
  if (!screen.empty()) first = screen;
  else if (!names.empty()) first = names[0];
  if (first) {
    screen_ = first;
    host->ShowScreen(*first, "auto", std::nullopt);
    const std::string f = *first;
    Fire("on_enter", {V::S(f)}, [&](const Handler& h) { return h.args.empty() || h.args[0].none() || (h.args[0].t == V::T::Str && h.args[0].s == f); });
  }
  im_->RunTasks();
}

void Machine::Fire(const std::string& event, std::vector<Value> args, const std::function<bool(const Handler&)>& match) {
  for (const auto& h : prog_.handlers) {
    if (h.event != event || (match && !match(h))) continue;
    SpawnHandler(h, args);
  }
}

void Machine::SpawnHandler(const Handler& h, const std::vector<Value>& args) {
  const Function& fn = prog_.functions[h.func];
  try {
    std::vector<Value> a(args.begin(), args.begin() + std::min<size_t>(args.size(), fn.nparams));
    im_->NewTask(h.func, im_->Fill(fn, std::move(a)), "@" + h.event + " " + fn.name);
  } catch (const Fault& e) {
    Error(e.message, fn.name, 0, {});
  }
}

static bool ArgIs(const Value& v, const std::string& s) { return v.t == V::T::Str && v.s == s; }
static bool ArgAny(const Value& v, const std::string& s) { return v.none() || ArgIs(v, s); }
static bool ArgInt(const Value& v, int p) { return v.t == V::T::Int && v.i == p; }

void Machine::Step(double step_dt, const Inputs& inputs) {
  if (!started_) Start();
  dt = std::max(0.0, step_dt);
  time += dt;
  ++frame;
  mouse_x = inputs.mouse_x;
  mouse_y = inputs.mouse_y;
  mouse_down = inputs.mouse_down;
  // mando: flancos
  im_->prev = std::move(im_->now);
  im_->now = inputs;
  std::vector<std::pair<int, std::string>> pressed, released;
  std::set<int> players;
  for (const auto& [p, _] : im_->now.buttons) players.insert(p);
  for (const auto& [p, _] : im_->prev.buttons) players.insert(p);
  static const std::set<std::string> kEmpty;
  for (int p : players) {
    auto ci = im_->now.buttons.find(p), oi = im_->prev.buttons.find(p);
    const auto& cur = ci == im_->now.buttons.end() ? kEmpty : ci->second;
    const auto& old = oi == im_->prev.buttons.end() ? kEmpty : oi->second;
    for (const auto& b : cur) if (!old.count(b)) { im_->since[{p, b}] = time; pressed.emplace_back(p, b); }
    for (const auto& b : old) if (!cur.count(b)) { im_->since.erase({p, b}); released.emplace_back(p, b); }
  }
  if (!Ok()) return;
  if (!closed_) {
    const auto& H = prog_.handlers;
    for (const auto& [p, b] : pressed) {
      Fire("on_press", {V::S(b), V::I(p)}, [&](const Handler& h) { return h.args.size() >= 2 && ArgAny(h.args[0], b) && ArgInt(h.args[1], p); });
      if (b == "strum_up" || b == "strum_down")
        Fire("on_strum", {V::I(b == "strum_up" ? -1 : 1), V::I(p)}, [&](const Handler& h) { return !h.args.empty() && ArgInt(h.args[0], p); });
      for (size_t i = 0; i < H.size(); ++i) {
        const Handler& h = H[i];
        if (h.event == "on_repeat" && h.args.size() >= 4 && ArgIs(h.args[0], b) && ArgInt(h.args[3], p)) {
          SpawnHandler(h, {V::S(b), V::I(p)});
          im_->repeat[{static_cast<int>(i), p}] = time + h.args[2].d();
        }
      }
      if (b == "choose" && focused_ && !focus_locked) NotifyClick(*focused_);
    }
    for (const auto& [p, b] : released) {
      Fire("on_release", {V::S(b), V::I(p)}, [&](const Handler& h) { return h.args.size() >= 2 && ArgAny(h.args[0], b) && ArgInt(h.args[1], p); });
      for (auto it = im_->holds.begin(); it != im_->holds.end();)
        it = it->second == p && ArgIs(H[it->first].args[0], b) ? im_->holds.erase(it) : std::next(it);
      for (auto it = im_->repeat.begin(); it != im_->repeat.end();)
        it = it->first.second == p && ArgIs(H[it->first.first].args[0], b) ? im_->repeat.erase(it) : std::next(it);
    }
    for (size_t i = 0; i < H.size(); ++i) {
      const Handler& h = H[i];
      if (h.event != "on_hold" || h.args.size() < 3 || h.args[0].t != V::T::Str) continue;
      const std::string& b = h.args[0].s;
      const int p = static_cast<int>(AsInt(h.args[2]));
      const std::pair<int, int> key{static_cast<int>(i), p};
      if (!im_->holds.count(key) && Pressed(b, p) && HeldTime(b, p) >= h.args[1].d() - 1e-9) {
        im_->holds.insert(key);
        SpawnHandler(h, {V::S(b), V::I(p)});
      }
    }
    for (auto& [key, at] : im_->repeat) {
      const Handler& h = H[key.first];
      if (time >= at - 1e-9 && Pressed(h.args[0].s, key.second)) {
        SpawnHandler(h, {h.args[0], V::I(key.second)});
        at = at + std::max(0.01, h.args[1].d());
      }
    }
    const auto over = host->LayerAt(mouse_x, mouse_y);
    if (over != hovered_) {
      hovered_ = over;
      if (over) { const std::string o = *over; Fire("on_hover", {V::Lay(o)}, [&](const Handler& h) { return h.args.empty() || ArgAny(h.args[0], o); }); }
    }
    // temporizadores
    for (size_t i = 0; i < H.size(); ++i) {
      const Handler& h = H[i];
      if (h.event != "every" || h.args.empty()) continue;
      const double secs = std::max(0.001, h.args[0].d());
      auto it = im_->every_at.find(static_cast<int>(i));
      if (it == im_->every_at.end()) it = im_->every_at.emplace(static_cast<int>(i), secs).first;
      if (time >= it->second - 1e-9) { it->second += secs; SpawnHandler(h, {}); }
    }
    Fire("on_tick", {V::F(dt)});
    for (const auto& h : H) {
      if (h.event != "on_game" || h.args.empty()) continue;
      std::string var = ToText(h.args[0]);
      const std::string key = var;
      while (!var.empty() && var[0] == '$') var.erase(0, 1);
      Value v = host->GameGet(var);
      auto it = im_->game_last.find(key);
      if (it != im_->game_last.end() && !Eq(it->second, v)) SpawnHandler(h, {v, it->second});
      im_->game_last[key] = v;
    }
  }
  im_->RunTasks();
  im_->UpdateAnim();
}

void Machine::NotifyFocus(const std::optional<std::string>& name) {
  if (name == focused_) return;
  const auto old = focused_;
  focused_ = name;
  if (old) { const std::string o = *old; Fire("on_blur", {V::Lay(o)}, [&](const Handler& h) { return h.args.empty() || ArgAny(h.args[0], o); }); }
  if (name) { const std::string n = *name; Fire("on_focus", {V::Lay(n)}, [&](const Handler& h) { return h.args.empty() || ArgAny(h.args[0], n); }); }
}
void Machine::NotifyClick(const std::string& name) {
  Fire("on_click", {V::Lay(name)}, [&](const Handler& h) { return h.args.empty() || ArgAny(h.args[0], name); });
}

// ---------------------------------------------------------------- api para la libreria
static std::string Near(const std::string& w, const std::vector<std::string>& opts) {
  auto lev = [](const std::string& a, const std::string& b) {
    std::vector<size_t> prev(b.size() + 1), cur(b.size() + 1);
    for (size_t j = 0; j <= b.size(); ++j) prev[j] = j;
    for (size_t i = 1; i <= a.size(); ++i) {
      cur[0] = i;
      for (size_t j = 1; j <= b.size(); ++j) cur[j] = std::min({prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + (a[i - 1] == b[j - 1] ? 0 : 1)});
      std::swap(prev, cur);
    }
    return prev[b.size()];
  };
  std::string best;
  double score = 0;
  for (const auto& o : opts) {
    const double s = 1.0 - static_cast<double>(lev(w, o)) / std::max<size_t>({w.size(), o.size(), 1});
    if (s > score) { score = s; best = o; }
  }
  return score >= 0.6 ? best : "";
}

void Machine::CheckLayer(const std::string& name) {
  const auto names = host->LayerNames();
  if (!names.empty() && std::find(names.begin(), names.end(), name) == names.end()) {
    const auto n = Near(name, names);
    Raise("no hay ninguna capa llamada '" + name + "'" + (n.empty() ? "" : " (quisiste decir '" + n + "'?)"));
  }
}

Value Machine::LayerGetP(const std::string& name, const std::string& prop) {
  if (prop == "focused") return V::B(focused_ && *focused_ == name);
  if (prop == "animating") return V::B(AnimBusy(name));
  const PropSig* ps = FindProp(prop);
  if (!ps) Raise("las capas no tienen la propiedad '" + prop + "'");
  Value v = host->LayerGet(name, prop);
  if (v.none() && ps->writable) return ps->dflt;
  return v;
}

void Machine::LayerSetP(const std::string& name, const std::string& prop, Value value, bool from_anim) {
  const PropSig* ps = FindProp(prop);
  if (!ps) Raise("las capas no tienen la propiedad '" + prop + "'");
  if (!ps->writable) Raise("la propiedad '" + prop + "' de las capas solo se puede leer");
  if (ps->type == 3) {
    int rgb[3];
    if (!ParseColor(value, rgb)) Raise(prop + ": '" + ToText(value) + "' no es un color (usa \"#ff0000\", \"red\" o [255, 0, 0])");
    value = V::S(ColorHex(rgb));
  } else if (ps->type == 0) {
    if (!value.num()) Raise(prop + ": se esperaba un numero y llego " + TypeName(value) + " (" + ToText(value) + ")");
    value = V::F(value.d());
  } else if (ps->type == 1) {
    value = V::B(Truthy(value));
  } else {
    value = V::S(ToText(value));
  }
  if (!from_anim) { const std::vector<std::string> one{prop}; StopAnim(name, &one); }
  host->LayerSet(name, prop, value);
}

void Machine::Goto(const std::string& name, const std::string& transition, bool push) {
  const auto names = host->ScreenNames();
  if (!names.empty() && std::find(names.begin(), names.end(), name) == names.end()) {
    const auto n = Near(name, names);
    Raise("no hay ninguna pantalla llamada '" + name + "'" + (n.empty() ? "" : " (quisiste decir '" + n + "'?)"));
  }
  if (screen_ && *screen_ == name) return;
  const auto old = screen_;
  if (old) {
    const std::string o = *old;
    Fire("on_exit", {V::S(o)}, [&](const Handler& h) { return h.args.empty() || ArgAny(h.args[0], o); });
    if (push) history.push_back(o);
  }
  screen_ = name;
  focused_.reset();
  host->ShowScreen(name, transition, old);
  Fire("on_enter", {V::S(name)}, [&](const Handler& h) { return h.args.empty() || ArgAny(h.args[0], name); });
}

void Machine::Back(const std::string& transition) {
  if (!history.empty()) {
    const std::string to = history.back();
    history.pop_back();
    Goto(to, transition, false);
  } else {
    CloseUi();
  }
}

void Machine::CloseUi() {
  if (closed_) return;
  closed_ = true;
  if (screen_) {
    const std::string o = *screen_;
    Fire("on_exit", {V::S(o)}, [&](const Handler& h) { return h.args.empty() || ArgAny(h.args[0], o); });
  }
  Fire("on_close", {});
  host->Close();
}

void Machine::SetFocus(const std::optional<std::string>& name) {
  if (name) CheckLayer(*name);
  host->SetFocus(name);
  NotifyFocus(name);
}

void Machine::Emit(const std::string& name, const Value& v) {
  Fire("on_message", {v}, [&](const Handler& h) { return !h.args.empty() && ArgIs(h.args[0], name); });
}

Value Machine::Spawn(const Value& fn, std::vector<Value> args, double delay, std::optional<double> every) {
  const Function& f = prog_.functions[fn.i];
  Task* t = im_->NewTask(static_cast<int>(fn.i), im_->Fill(f, std::move(args)), f.name);
  t->start_at = time + std::max(0.0, delay);
  t->every = every;
  if (every && delay <= 0) t->start_at = time + *every;
  return V::Tk(t->ref);
}
void Machine::StopTask(const TaskPtr& ref) {
  for (auto& t : im_->tasks) if (t->ref == ref) { t->done = true; t->stopped = true; }
}
void Machine::StopAll(Task* except) {
  for (auto& t : im_->tasks) if (t.get() != except) { t->done = true; t->stopped = true; }
}
bool Machine::TaskRunning(const TaskPtr& ref) const {
  return std::any_of(im_->tasks.begin(), im_->tasks.end(), [&](const auto& t) { return t->ref == ref && !t->done; });
}
Value Machine::CallNow(const Value& fn, std::vector<Value> args) {
  try {
    return im_->CallSync(static_cast<int>(fn.i), std::move(args), fn.s + "()");
  } catch (const Impl::SyncFault& e) {
    throw e.f;
  }
}

int Machine::Tween(const std::string& layer, const std::string& prop, Value to, double t, const std::string& ease, double delay,
                   int loop, bool yoyo, std::function<void(Machine&)> on_end, std::function<Value(double)> fn, bool accumulate) {
  const std::vector<std::string> one{prop};
  StopAnim(layer, &one);
  auto tw = std::make_unique<TweenRec>();
  tw->id = im_->next_tween++;
  tw->layer = layer;
  tw->prop = prop;
  tw->end = to;
  tw->duration = std::max(0.0, t);
  tw->ease = ease.empty() ? "out" : ease;
  tw->delay = std::max(0.0, delay);
  tw->loops = loop;
  tw->yoyo = yoyo;
  tw->t0 = time;
  tw->fn = std::move(fn);
  tw->accumulate = accumulate;
  tw->on_end = std::move(on_end);
  const int id = tw->id;
  TweenRec* raw = tw.get();
  im_->tweens.push_back(std::move(tw));
  if (raw->duration <= 0 && raw->delay <= 0 && !raw->fn) {
    LayerSetP(layer, prop, to, true);
    auto cb = raw->on_end;
    im_->RemoveTween(raw);
    if (cb) cb(*this);
  }
  return id;
}
void Machine::StopAnim(const std::string& layer, const std::vector<std::string>* props) {
  auto& T = im_->tweens;
  T.erase(std::remove_if(T.begin(), T.end(), [&](const auto& t) {
            return t->layer == layer && (!props || std::find(props->begin(), props->end(), t->prop) != props->end());
          }), T.end());
}
bool Machine::AnimBusy(const std::string& layer) const {
  return std::any_of(im_->tweens.begin(), im_->tweens.end(), [&](const auto& t) { return t->layer == layer; });
}
Value Machine::AnimTarget(const std::string& layer, const std::string& prop) {
  for (auto it = im_->tweens.rbegin(); it != im_->tweens.rend(); ++it) {
    const auto& t = *it;
    if (t->layer == layer && t->prop == prop && !t->fn && t->loops == 0) return t->end;
  }
  Value v = LayerGetP(layer, prop);
  return v.none() ? V::F(0) : v;
}
Block Machine::WaitFor(std::vector<int> ids) {
  return [ids = std::move(ids)](Machine& m, Value*) {
    for (const auto& t : m.im_->tweens) if (std::find(ids.begin(), ids.end(), t->id) != ids.end()) return false;
    return true;
  };
}

// ---------------------------------------------------------------- mando
bool Machine::Pressed(const std::string& b, int p) const {
  auto it = im_->now.buttons.find(p);
  return it != im_->now.buttons.end() && it->second.count(b);
}
bool Machine::JustPressed(const std::string& b, int p) const {
  auto it = im_->prev.buttons.find(p);
  return Pressed(b, p) && !(it != im_->prev.buttons.end() && it->second.count(b));
}
bool Machine::JustReleased(const std::string& b, int p) const {
  auto it = im_->prev.buttons.find(p);
  return !Pressed(b, p) && it != im_->prev.buttons.end() && it->second.count(b);
}
double Machine::HeldTime(const std::string& b, int p) const {
  auto it = im_->since.find({p, b});
  return it == im_->since.end() ? 0.0 : std::max(0.0, time - it->second);
}
std::set<std::string> Machine::Buttons(int p) const {
  auto it = im_->now.buttons.find(p);
  return it == im_->now.buttons.end() ? std::set<std::string>{} : it->second;
}
double Machine::Axis(const std::string& n, int p) const {
  auto it = im_->now.axes.find(p);
  if (it == im_->now.axes.end()) return 0;
  auto j = it->second.find(n);
  return j == it->second.end() ? 0 : j->second;
}
bool Machine::Connected(int p) const { return im_->now.buttons.count(p) || im_->now.kinds.count(p) || p == 1; }
std::string Machine::Kind(int p) const {
  auto it = im_->now.kinds.find(p);
  return it == im_->now.kinds.end() ? "guitar" : it->second;
}

double Machine::Random() {
  const uint32_t a = im_->U32() >> 5, b = im_->U32() >> 6;
  return (a * 67108864.0 + b) / 9007199254740992.0;
}
uint64_t Machine::Below(uint64_t n) {
  if (n == 0) return 0;
  int k = 0;
  for (uint64_t x = n; x; x >>= 1) ++k;
  uint64_t r = im_->Bits(k);
  while (r >= n) r = im_->Bits(k);
  return r;
}

}  // namespace wui
