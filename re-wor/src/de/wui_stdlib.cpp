// Libreria de wui en C++: traduccion directa de tools/wui/wui/stdlib/*/impl.py (y de DEStudio/web/src/wui/stdlib.js).
// Cada funcion recibe a = argumentos ordenados segun su firma (fijos con sus valores por defecto, despues los extra
// de *args) y kw = los extra con nombre (**props). Para dormir la tarea pone ctx.block.
#include <algorithm>
#include <cmath>
#include <cstdio>

#include "de/wui_internal.hpp"

namespace wui {
namespace {

using A = std::vector<Value>;
using M = std::unordered_map<std::string, BuiltinFn>;

const Value& Num(Machine& vm, const Value& v, const char* what = "un numero") {
  static const Value kT = V::I(1), kF = V::I(0);
  if (v.num()) return v;
  if (v.t == V::T::Bool) return v.b ? kT : kF;
  vm.Raise(std::string("se esperaba ") + what + " y llego " + TypeName(v) + " (" + ToText(v) + ")");
}
double D(Machine& vm, const Value& v) { return Num(vm, v).d(); }
int64_t Int(Machine& vm, const Value& v) {
  const Value& x = Num(vm, v);
  return x.t == V::T::Float ? static_cast<int64_t>(std::trunc(x.f)) : x.i;
}
double Secs(Machine& vm, const Value& v, const char* what = "segundos") {
  if (!v.num()) vm.Raise(std::string("se esperaban ") + what + " (un numero) y llego " + TypeName(v) + " (" + ToText(v) + ")");
  return v.d();
}
const Value& FnRef(Machine& vm, const Value& v) {
  if (v.t != V::T::Func) vm.Raise("se esperaba una funcion (su nombre, sin parentesis) y llego " + TypeName(v));
  return v;
}
const TaskPtr& TaskOf(Machine& vm, const Value& v) {
  if (v.t != V::T::Task) vm.Raise("se esperaba una tarea y llego " + TypeName(v));
  return v.task;
}
const std::string& L(Machine& vm, const Value& v) {
  if (v.t != V::T::Layer) vm.Raise("se esperaba una capa y llego " + TypeName(v) + " (" + ToText(v) + ")");
  return v.s;
}
double LNum(Machine& vm, const Value& v, const char* what) {
  if (!v.num()) vm.Raise(std::string(what) + ": se esperaba un numero y llego " + TypeName(v) + " (" + ToText(v) + ")");
  return v.d();
}
const Value& Color(Machine& vm, const Value& v, const char* what = "color") {
  int rgb[3];
  if (!ParseColor(v, rgb)) vm.Raise(std::string(what) + ": '" + ToText(v) + "' no es un color (usa \"#ff0000\", \"red\" o [255, 0, 0])");
  return v;
}
std::vector<Value>& Lst(Machine& vm, const Value& v) {
  if (v.t != V::T::List) vm.Raise("se esperaba una lista y llego " + TypeName(v));
  return v.l->v;
}
std::vector<Value>& Mut(Machine& vm, const Value& v) {
  auto& l = Lst(vm, v);
  if (v.l->frozen) vm.Raise("esta lista es constante");
  return l;
}
const std::string& Button(Machine& vm, const Value& v) {
  const auto& b = ButtonNames();
  if (v.t != V::T::Str || std::find(b.begin(), b.end(), v.s) == b.end()) vm.Raise("boton desconocido '" + ToText(v) + "'");
  return v.s;
}
std::string GName(Machine& vm, const Value& v, const char* what) {
  if (v.t != V::T::Str || v.s.empty()) vm.Raise(std::string(what) + ": se esperaba un nombre (texto) y llego " + TypeName(v));
  std::string s = v.s;
  while (!s.empty() && s[0] == '$') s.erase(0, 1);
  return s;
}
bool Animatable(const std::string& p) { const PropSig* s = FindProp(p); return s && s->anim; }
bool HasDefault(const std::string& p) { const PropSig* s = FindProp(p); return s && s->writable; }

// numero de Python: entero o decimal segun los operandos
Value NumOut(double r, bool fl) { return fl ? V::F(r) : V::I(static_cast<int64_t>(r)); }

double PyRoundD(double x, int64_t d) {
  if (d <= 0) {
    const double k = std::pow(10.0, static_cast<double>(-d));
    return std::nearbyint(x / k) * k;
  }
  return std::stod(FixedStr(x, static_cast<int>(d)));
}

std::optional<double> ParseFloat(const std::string& s0) {
  std::string s = s0;
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.erase(0, 1);
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
  if (s.empty()) return std::nullopt;
  std::string low = s;
  for (auto& c : low) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  const std::string body = low[0] == '+' || low[0] == '-' ? low.substr(1) : low;
  if (body == "inf" || body == "infinity") return low[0] == '-' ? -INFINITY : INFINITY;
  if (body == "nan") return NAN;
  size_t i = 0, digits = 0;
  if (i < low.size() && (low[i] == '+' || low[i] == '-')) ++i;
  while (i < low.size() && std::isdigit(static_cast<unsigned char>(low[i]))) { ++i; ++digits; }
  if (i < low.size() && low[i] == '.') { ++i; while (i < low.size() && std::isdigit(static_cast<unsigned char>(low[i]))) { ++i; ++digits; } }
  if (!digits) return std::nullopt;
  if (i < low.size() && low[i] == 'e') {
    ++i;
    if (i < low.size() && (low[i] == '+' || low[i] == '-')) ++i;
    const size_t e0 = i;
    while (i < low.size() && std::isdigit(static_cast<unsigned char>(low[i]))) ++i;
    if (i == e0) return std::nullopt;
  }
  if (i != low.size()) return std::nullopt;
  return std::strtod(low.c_str(), nullptr);
}

int64_t PyIndex(Machine& vm, const Value& i, int64_t len) {
  int64_t k = Int(vm, i);
  return k < 0 ? std::max<int64_t>(0, k + len) : std::min(k, len);
}

Value MaybeWait(Ctx& c, const std::vector<int>& ids, const Value& wait) {
  if (Truthy(wait) && !ids.empty()) c.block = c.vm.WaitFor(ids);
  return {};
}
void Fx(Ctx& c, const std::string& n, const std::vector<std::pair<std::string, Value>>& vals, const Value& tv) {
  const double t = LNum(c.vm, tv, "time");
  for (const auto& [k, v] : vals) {
    if (t > 0) c.vm.Tween(n, k, v, t, "out");
    else c.vm.LayerSetP(n, k, v);
  }
}
Value To(Ctx& c, const std::string& n, const std::vector<std::pair<std::string, Value>>& props, const Value& t, const Value& ease,
         const Value& wait) {
  std::vector<int> ids;
  for (const auto& [k, v] : props) ids.push_back(c.vm.Tween(n, k, v, LNum(c.vm, t, "time"), ToText(ease)));
  return MaybeWait(c, ids, wait);
}

Value Many(Machine& vm, const A& a) {
  const std::vector<Value>& vals = a.size() == 1 && a[0].t == V::T::List ? a[0].l->v : a;
  if (vals.empty()) vm.Raise("min()/max() sin valores");
  return {};
}
Value MinMax(Machine& vm, const A& a, int sgn) {
  Many(vm, a);
  const std::vector<Value>& vals = a.size() == 1 && a[0].t == V::T::List ? a[0].l->v : a;
  Value best = Num(vm, vals[0]);
  for (size_t i = 1; i < vals.size(); ++i) {
    const Value& x = Num(vm, vals[i]);
    if (sgn > 0 ? x.d() > best.d() : x.d() < best.d()) best = x;
  }
  return best;
}

void SortList(Machine& vm, std::vector<Value>& v, bool reverse) {
  bool ok = true;
  std::stable_sort(v.begin(), v.end(), [&](const Value& x, const Value& y) {
    bool k;
    const int c = reverse ? Cmp(y, x, &k) : Cmp(x, y, &k);
    if (!k) ok = false;
    return c < 0;
  });
  if (!ok) vm.Raise("sort(): la lista mezcla tipos que no se pueden comparar");
}

std::string Join(const std::vector<Value>& v, const std::string& sep, size_t from = 0) {
  std::string out;
  for (size_t i = from; i < v.size(); ++i) { if (i > from) out += sep; out += ToText(v[i]); }
  return out;
}

const std::vector<std::string> kFrets = {"green", "red", "yellow", "blue", "orange"};

M Build() {
  M m;
  // ----- conversiones y registro
  m["int"] = [](Ctx& c, A& a, Kw&) -> Value {
    if (a[0].t == V::T::Str) {
      auto x = ParseFloat(a[0].s);
      if (!x || !std::isfinite(*x)) c.vm.Raise("'" + a[0].s + "' no es un numero");
      return V::I(static_cast<int64_t>(std::trunc(*x)));
    }
    return V::I(Int(c.vm, a[0]));
  };
  m["float"] = [](Ctx& c, A& a, Kw&) -> Value {
    if (a[0].t == V::T::Str) {
      auto x = ParseFloat(a[0].s);
      if (!x) c.vm.Raise("'" + a[0].s + "' no es un numero");
      return V::F(*x);
    }
    return V::F(D(c.vm, a[0]));
  };
  m["str"] = [](Ctx&, A& a, Kw&) { return V::S(ToText(a[0])); };
  m["bool"] = [](Ctx&, A& a, Kw&) { return V::B(Truthy(a[0])); };
  m["len"] = [](Ctx& c, A& a, Kw&) -> Value {
    if (a[0].t == V::T::List) return V::I(static_cast<int64_t>(a[0].l->v.size()));
    if (a[0].t == V::T::Str) return V::I(static_cast<int64_t>(Utf8Decode(a[0].s).size()));
    c.vm.Raise("len() necesita una lista o un texto, no " + TypeName(a[0]));
  };
  m["range"] = [](Ctx& c, A& a, Kw&) -> Value {
    Value start = a[0], stop = a[1];
    if (stop.none()) { stop = start; start = V::I(0); }
    const int64_t s = Int(c.vm, start), e = Int(c.vm, stop), st = Int(c.vm, a[2]);
    if (st == 0) c.vm.Raise("range() con paso 0");
    const int64_t cnt = st > 0 ? std::max<int64_t>(0, (e - s + st - 1) / st) : std::max<int64_t>(0, (s - e + (-st) - 1) / (-st));
    if (cnt > 100000) c.vm.Raise("range() demasiado grande (100000 como mucho)");
    std::vector<Value> out;
    out.reserve(cnt);
    for (int64_t i = 0; i < cnt; ++i) out.push_back(V::I(s + i * st));
    return V::Lst(std::move(out));
  };
  m["slice"] = [](Ctx& c, A& a, Kw&) -> Value {
    const Value& seq = a[0];
    if (seq.t != V::T::List && seq.t != V::T::Str) c.vm.Raise("[a:b] necesita una lista o un texto, no " + TypeName(seq));
    if (seq.t == V::T::Str) {
      const auto cps = Utf8Decode(seq.s);
      const int64_t n = static_cast<int64_t>(cps.size());
      const int64_t s = a[1].none() ? 0 : PyIndex(c.vm, a[1], n), e = a[2].none() ? n : PyIndex(c.vm, a[2], n);
      return V::S(Utf8Encode(std::vector<uint32_t>(cps.begin() + s, cps.begin() + std::max(s, e))));
    }
    const auto& v = seq.l->v;
    const int64_t n = static_cast<int64_t>(v.size());
    const int64_t s = a[1].none() ? 0 : PyIndex(c.vm, a[1], n), e = a[2].none() ? n : PyIndex(c.vm, a[2], n);
    return V::Lst(std::vector<Value>(v.begin() + s, v.begin() + std::max(s, e)));
  };
  m["type"] = [](Ctx&, A& a, Kw&) { return V::S(TypeName(a[0])); };
  m["log"] = [](Ctx& c, A& a, Kw&) -> Value {
    const std::string sep = a[0].t == V::T::Str ? a[0].s : " ";
    c.vm.LogMsg("info", Join(a, sep, 1));
    return {};
  };
  m["print"] = m["log"];
  m["warn"] = [](Ctx& c, A& a, Kw&) -> Value { c.vm.LogMsg("warn", Join(a, " ")); return {}; };

  // ----- matematicas
  m["abs"] = [](Ctx& c, A& a, Kw&) -> Value { const Value& x = Num(c.vm, a[0]); return x.t == V::T::Float ? V::F(std::fabs(x.f)) : V::I(std::llabs(x.i)); };
  m["min"] = [](Ctx& c, A& a, Kw&) { return MinMax(c.vm, a, -1); };
  m["max"] = [](Ctx& c, A& a, Kw&) { return MinMax(c.vm, a, 1); };
  m["sum"] = [](Ctx& c, A& a, Kw&) -> Value {
    if (a[0].t != V::T::List) c.vm.Raise("sum() necesita una lista");
    int64_t ti = 0;
    double tf = 0;
    bool fl = false;
    for (const auto& v : a[0].l->v) {
      const Value& x = Num(c.vm, v);
      if (x.t == V::T::Float) { if (!fl) { fl = true; tf = static_cast<double>(ti); } tf += x.f; }
      else if (fl) tf += static_cast<double>(x.i);
      else ti += x.i;
    }
    return fl ? V::F(tf) : V::I(ti);
  };
  m["round"] = [](Ctx& c, A& a, Kw&) -> Value {
    const Value& x = Num(c.vm, a[0]);
    const int64_t d = Int(c.vm, a[1]);
    if (x.t != V::T::Float) return d >= 0 ? V::I(x.i) : V::I(static_cast<int64_t>(PyRoundD(static_cast<double>(x.i), d)));
    const double v = PyRoundD(x.f, d);
    return d <= 0 ? V::I(static_cast<int64_t>(std::nearbyint(v))) : V::F(v);
  };
  m["floor"] = [](Ctx& c, A& a, Kw&) { return V::I(static_cast<int64_t>(std::floor(D(c.vm, a[0])))); };
  m["ceil"] = [](Ctx& c, A& a, Kw&) { return V::I(static_cast<int64_t>(std::ceil(D(c.vm, a[0])))); };
  m["sqrt"] = [](Ctx& c, A& a, Kw&) -> Value {
    const double x = D(c.vm, a[0]);
    if (x < 0) c.vm.Raise("sqrt() de un numero negativo");
    return V::F(std::sqrt(x));
  };
  constexpr double kDeg = 3.14159265358979323846 / 180.0;
  m["sin"] = [](Ctx& c, A& a, Kw&) { return V::F(std::sin(D(c.vm, a[0]) * kDeg)); };
  m["cos"] = [](Ctx& c, A& a, Kw&) { return V::F(std::cos(D(c.vm, a[0]) * kDeg)); };
  m["atan2"] = [](Ctx& c, A& a, Kw&) { return V::F(std::atan2(D(c.vm, a[0]), D(c.vm, a[1])) / kDeg); };
  m["clamp"] = [](Ctx& c, A& a, Kw&) -> Value {
    Value x = Num(c.vm, a[0]), lo = Num(c.vm, a[1]), hi = Num(c.vm, a[2]);
    return x.d() < lo.d() ? lo : x.d() > hi.d() ? hi : x;
  };
  m["lerp"] = [](Ctx& c, A& a, Kw&) -> Value {
    const Value &x = Num(c.vm, a[0]), &y = Num(c.vm, a[1]), &t = Num(c.vm, a[2]);
    const bool fl = x.t == V::T::Float || y.t == V::T::Float || t.t == V::T::Float;
    if (!fl) return V::I(x.i + (y.i - x.i) * t.i);
    return V::F(x.d() + (y.d() - x.d()) * t.d());
  };
  m["remap"] = [](Ctx& c, A& a, Kw&) -> Value {
    const double x = D(c.vm, a[0]), lo = D(c.vm, a[1]), hi = D(c.vm, a[2]), cc = D(c.vm, a[3]), d = D(c.vm, a[4]);
    if (hi == lo) return V::F(cc);
    return V::F(cc + (x - lo) / (hi - lo) * (d - cc));
  };
  m["approach"] = [](Ctx& c, A& a, Kw&) -> Value {
    const Value x = Num(c.vm, a[0]), t = Num(c.vm, a[1]), s = Num(c.vm, a[2]);
    const double sa = std::fabs(s.d());
    const bool fl = x.t == V::T::Float || s.t == V::T::Float;
    if (x.d() < t.d()) { const double r = x.d() + sa; return t.d() <= r ? t : NumOut(r, fl); }
    const double r = x.d() - sa;
    return t.d() >= r ? t : NumOut(r, fl);
  };
  m["sign"] = [](Ctx& c, A& a, Kw&) { const double x = D(c.vm, a[0]); return V::I((x > 0) - (x < 0)); };
  m["random"] = [](Ctx& c, A& a, Kw&) { const double lo = D(c.vm, a[0]), hi = D(c.vm, a[1]); return V::F(lo + c.vm.Random() * (hi - lo)); };
  m["randint"] = [](Ctx& c, A& a, Kw&) -> Value {
    const int64_t lo = Int(c.vm, a[0]), hi = Int(c.vm, a[1]);
    if (hi < lo) c.vm.Raise("randint(" + std::to_string(lo) + ", " + std::to_string(hi) + "): el segundo tiene que ser mayor");
    return V::I(lo + static_cast<int64_t>(c.vm.Below(static_cast<uint64_t>(hi - lo + 1))));
  };
  m["choice"] = [](Ctx& c, A& a, Kw&) -> Value {
    if (a[0].t != V::T::List || a[0].l->v.empty()) c.vm.Raise("choice() necesita una lista con elementos");
    return a[0].l->v[c.vm.Below(a[0].l->v.size())];
  };
  m["shuffle"] = [](Ctx& c, A& a, Kw&) -> Value {
    if (a[0].t != V::T::List) c.vm.Raise("shuffle() necesita una lista");
    auto& x = Mut(c.vm, a[0]);
    for (size_t i = x.size(); i-- > 1;) std::swap(x[i], x[c.vm.Below(i + 1)]);
    return {};
  };
  m["format_number"] = [](Ctx& c, A& a, Kw&) {
    const double x = D(c.vm, a[0]);
    const int64_t d = Int(c.vm, a[1]);
    return V::S(FormatValue(V::F(x), std::string(Truthy(a[2]) ? "," : "") + "." + std::to_string(d) + "f"));
  };
  m["time_text"] = [](Ctx& c, A& a, Kw&) {
    const int64_t s = std::max<int64_t>(0, Int(c.vm, a[0]));
    const int64_t h = s / 3600, mi = (s / 60) % 60, sec = s % 60;
    char b[64];
    if (Truthy(a[1]) || h) std::snprintf(b, sizeof b, "%lld:%02lld:%02lld", static_cast<long long>(h), static_cast<long long>(mi), static_cast<long long>(sec));
    else std::snprintf(b, sizeof b, "%lld:%02lld", static_cast<long long>(s / 60), static_cast<long long>(sec));
    return V::S(b);
  };
  m["percent"] = [](Ctx& c, A& a, Kw&) {
    const double v = D(c.vm, a[0]), t = D(c.vm, a[1]);
    const int64_t d = Int(c.vm, a[2]);
    return V::S(FixedStr(t == 0 ? 0.0 : v * 100.0 / t, static_cast<int>(d)) + "%");
  };

  // ----- tiempo y tareas
  m["time"] = [](Ctx& c, A&, Kw&) { return V::F(c.vm.time); };
  m["dt"] = [](Ctx& c, A&, Kw&) { return V::F(c.vm.dt); };
  m["frame"] = [](Ctx& c, A&, Kw&) { return V::I(c.vm.frame); };
  m["wait"] = [](Ctx& c, A& a, Kw&) -> Value {
    const double until = c.vm.time + std::max(0.0, Secs(c.vm, a[0]));
    const int64_t first = c.vm.frame;
    c.block = [until, first](Machine& vm, Value*) { return vm.frame > first && vm.time >= until - 1e-9; };
    return {};
  };
  m["wait_frames"] = [](Ctx& c, A& a, Kw&) -> Value {
    const int64_t target = c.vm.frame + std::max<int64_t>(1, static_cast<int64_t>(Secs(c.vm, a[0], "fotogramas")));
    c.block = [target](Machine& vm, Value*) { return vm.frame >= target; };
    return {};
  };
  m["wait_press"] = [](Ctx& c, A& a, Kw&) -> Value {
    const int p = static_cast<int>(Int(c.vm, a[1]));
    std::vector<std::string> buttons;
    for (size_t i = 2; i < a.size(); ++i) buttons.push_back(ToText(a[i]));
    std::optional<double> until;
    if (a[0].num() && a[0].d() >= 0) until = c.vm.time + Secs(c.vm, a[0]);
    const int64_t first = c.vm.frame;
    c.block = [=](Machine& vm, Value* out) {
      if (vm.frame > first) {
        std::vector<std::string> bs = buttons;
        if (bs.empty()) { const auto s = vm.Buttons(p); bs.assign(s.begin(), s.end()); }
        for (const auto& b : bs) if (vm.JustPressed(b, p)) { *out = V::S(b); return true; }
      }
      if (until && vm.time >= *until) { *out = V::N(); return true; }
      return false;
    };
    return {};
  };
  m["wait_release"] = [](Ctx& c, A& a, Kw&) -> Value {
    const std::string b = ToText(a[0]);
    const int p = static_cast<int>(Int(c.vm, a[1]));
    c.block = [b, p](Machine& vm, Value*) { return !vm.Pressed(b, p); };
    return {};
  };
  m["wait_until"] = [](Ctx& c, A& a, Kw&) -> Value {
    const Value fn = FnRef(c.vm, a[0]);
    std::optional<double> until;
    if (a[1].num() && a[1].d() >= 0) until = c.vm.time + Secs(c.vm, a[1]);
    c.block = [fn, until](Machine& vm, Value* out) {
      if (Truthy(vm.CallNow(fn, {}))) { *out = V::B(true); return true; }
      if (until && vm.time >= *until) { *out = V::B(false); return true; }
      return false;
    };
    return {};
  };
  m["spawn"] = [](Ctx& c, A& a, Kw&) { return c.vm.Spawn(FnRef(c.vm, a[0]), A(a.begin() + 1, a.end())); };
  m["after"] = [](Ctx& c, A& a, Kw&) { return c.vm.Spawn(FnRef(c.vm, a[1]), A(a.begin() + 2, a.end()), Secs(c.vm, a[0])); };
  m["every_seconds"] = [](Ctx& c, A& a, Kw&) {
    return c.vm.Spawn(FnRef(c.vm, a[1]), A(a.begin() + 2, a.end()), 0, std::max(0.0, Secs(c.vm, a[0])));
  };
  m["stop"] = [](Ctx& c, A& a, Kw&) -> Value { if (!a[0].none()) c.vm.StopTask(TaskOf(c.vm, a[0])); return {}; };
  m["stop_all"] = [](Ctx& c, A&, Kw&) -> Value { c.vm.StopAll(c.task); return {}; };
  m["Task.stop"] = [](Ctx& c, A& a, Kw&) -> Value { c.vm.StopTask(TaskOf(c.vm, a[0])); return {}; };
  m["Task.join"] = [](Ctx& c, A& a, Kw&) -> Value {
    const TaskPtr t = TaskOf(c.vm, a[0]);
    std::optional<double> until;
    if (a[1].num() && a[1].d() >= 0) until = c.vm.time + Secs(c.vm, a[1]);
    c.block = [t, until](Machine& vm, Value* out) {
      if (!vm.TaskRunning(t)) { *out = t->result; return true; }
      if (until && vm.time >= *until) { *out = V::N(); return true; }
      return false;
    };
    return {};
  };

  // ----- mando
  m["pad.pressed"] = [](Ctx& c, A& a, Kw&) { return V::B(c.vm.Pressed(Button(c.vm, a[0]), static_cast<int>(Int(c.vm, a[1])))); };
  m["pad.just_pressed"] = [](Ctx& c, A& a, Kw&) { return V::B(c.vm.JustPressed(Button(c.vm, a[0]), static_cast<int>(Int(c.vm, a[1])))); };
  m["pad.just_released"] = [](Ctx& c, A& a, Kw&) { return V::B(c.vm.JustReleased(Button(c.vm, a[0]), static_cast<int>(Int(c.vm, a[1])))); };
  m["pad.held_time"] = [](Ctx& c, A& a, Kw&) { return V::F(c.vm.HeldTime(Button(c.vm, a[0]), static_cast<int>(Int(c.vm, a[1])))); };
  m["pad.any_pressed"] = [](Ctx& c, A& a, Kw&) { return V::B(!c.vm.Buttons(static_cast<int>(Int(c.vm, a[0]))).empty()); };
  m["pad.buttons"] = [](Ctx& c, A& a, Kw&) {
    std::vector<Value> out;
    for (const auto& b : c.vm.Buttons(static_cast<int>(Int(c.vm, a[0])))) out.push_back(V::S(b));
    return V::Lst(std::move(out));
  };
  m["pad.frets"] = [](Ctx& c, A& a, Kw&) {
    const auto on = c.vm.Buttons(static_cast<int>(Int(c.vm, a[0])));
    std::vector<Value> out;
    for (const auto& f : kFrets) if (on.count(f)) out.push_back(V::S(f));
    return V::Lst(std::move(out));
  };
  m["pad.chord"] = [](Ctx& c, A& a, Kw&) {
    const int p = static_cast<int>(Int(c.vm, a[0]));
    std::set<std::string> want;
    for (size_t i = 1; i < a.size(); ++i) want.insert(Button(c.vm, a[i]));
    std::set<std::string> on;
    for (const auto& f : kFrets) if (c.vm.Pressed(f, p)) on.insert(f);
    return V::B(on == want);
  };
  m["pad.strum"] = [](Ctx& c, A& a, Kw&) {
    const int p = static_cast<int>(Int(c.vm, a[0]));
    return V::I(c.vm.JustPressed("strum_up", p) ? -1 : c.vm.JustPressed("strum_down", p) ? 1 : 0);
  };
  m["pad.whammy"] = [](Ctx& c, A& a, Kw&) { return V::F(c.vm.Axis("whammy", static_cast<int>(Int(c.vm, a[0])))); };
  m["pad.tilt"] = [](Ctx& c, A& a, Kw&) { return V::F(c.vm.Axis("tilt", static_cast<int>(Int(c.vm, a[0])))); };
  m["pad.axis"] = [](Ctx& c, A& a, Kw&) { return V::F(c.vm.Axis(ToText(a[0]), static_cast<int>(Int(c.vm, a[1])))); };
  m["pad.connected"] = [](Ctx& c, A& a, Kw&) { return V::B(c.vm.Connected(static_cast<int>(Int(c.vm, a[0])))); };
  m["pad.kind"] = [](Ctx& c, A& a, Kw&) { return V::S(c.vm.Kind(static_cast<int>(Int(c.vm, a[0])))); };
  m["pad.vibrate"] = [](Ctx& c, A& a, Kw&) -> Value {
    c.vm.host->Vibrate(static_cast<int>(Int(c.vm, a[2])), D(c.vm, a[0]), D(c.vm, a[1]));
    return {};
  };

  // ----- capas
  m["layer"] = [](Ctx& c, A& a, Kw&) -> Value {
    if (a[0].t != V::T::Str) c.vm.Raise("layer() necesita el nombre de la capa (texto), no " + TypeName(a[0]));
    c.vm.CheckLayer(a[0].s);
    return V::Lay(a[0].s);
  };
  m["layers"] = [](Ctx& c, A& a, Kw&) {
    const std::string p = Truthy(a[0]) ? ToText(a[0]) : "";
    std::vector<Value> out;
    for (const auto& n : c.vm.host->LayerNames()) if (n.rfind(p, 0) == 0) out.push_back(V::Lay(n));
    return V::Lst(std::move(out));
  };
  m["exists"] = [](Ctx& c, A& a, Kw&) {
    const auto names = c.vm.host->LayerNames();
    return V::B(a[0].t == V::T::Str && std::find(names.begin(), names.end(), a[0].s) != names.end());
  };
  m["Layer.show"] = [](Ctx& c, A& a, Kw&) -> Value {
    const std::string n = L(c.vm, a[0]);
    const double t = LNum(c.vm, a[1], "time");
    c.vm.LayerSetP(n, "visible", V::B(true));
    if (t > 0) { c.vm.LayerSetP(n, "alpha", V::F(0)); c.vm.Tween(n, "alpha", V::F(1), t, "out"); }
    return {};
  };
  m["Layer.hide"] = [](Ctx& c, A& a, Kw&) -> Value {
    const std::string n = L(c.vm, a[0]);
    const double t = LNum(c.vm, a[1], "time");
    if (t > 0) c.vm.Tween(n, "alpha", V::F(0), t, "in", 0, 0, false, [n](Machine& vm) { vm.LayerSetP(n, "visible", V::B(false)); });
    else c.vm.LayerSetP(n, "visible", V::B(false));
    return {};
  };
  m["Layer.toggle"] = [](Ctx& c, A& a, Kw&) -> Value {
    const std::string n = L(c.vm, a[0]);
    c.vm.LayerSetP(n, "visible", V::B(!Truthy(c.vm.LayerGetP(n, "visible"))));
    return {};
  };
  m["Layer.fade_in"] = [](Ctx& c, A& a, Kw&) -> Value {
    const std::string n = L(c.vm, a[0]);
    const double t = LNum(c.vm, a[1], "time");
    if (!Truthy(c.vm.LayerGetP(n, "visible"))) c.vm.LayerSetP(n, "alpha", V::F(0));
    c.vm.LayerSetP(n, "visible", V::B(true));
    return MaybeWait(c, {c.vm.Tween(n, "alpha", V::F(1), t, "out")}, a[2]);
  };
  m["Layer.fade_out"] = [](Ctx& c, A& a, Kw&) -> Value {
    const std::string n = L(c.vm, a[0]);
    const double t = LNum(c.vm, a[1], "time");
    const int id = c.vm.Tween(n, "alpha", V::F(0), t, "in", 0, 0, false, [n](Machine& vm) { vm.LayerSetP(n, "visible", V::B(false)); });
    return MaybeWait(c, {id}, a[2]);
  };
  m["Layer.set"] = [](Ctx& c, A& a, Kw& kw) -> Value {
    const std::string n = L(c.vm, a[0]);
    for (auto& [k, v] : kw) c.vm.LayerSetP(n, k, v);
    return {};
  };
  m["Layer.get"] = [](Ctx& c, A& a, Kw&) { return c.vm.LayerGetP(L(c.vm, a[0]), ToText(a[1])); };
  m["Layer.reset"] = [](Ctx& c, A& a, Kw&) -> Value {
    const std::string n = L(c.vm, a[0]);
    const double t = LNum(c.vm, a[1], "time");
    c.vm.StopAnim(n);
    const auto design = c.vm.host->LayerDesign(n);
    for (const auto& p : Props()) {
      if (!p.writable) continue;
      auto it = design.find(p.name);
      const Value v = it == design.end() ? p.dflt : it->second;
      if (t > 0 && p.anim) c.vm.Tween(n, p.name, v, t, "out");
      else c.vm.LayerSetP(n, p.name, v);
    }
    return {};
  };
  m["Layer.animate"] = [](Ctx& c, A& a, Kw& kw) -> Value {
    const std::string n = L(c.vm, a[0]);
    if (kw.empty()) c.vm.Raise("animate() sin propiedades: p. ej. animate(x=300, alpha=0.5, time=0.4)");
    std::vector<int> ids;
    for (auto& [k, v] : kw) {
      if (!Animatable(k)) c.vm.Raise("la propiedad '" + k + "' no se puede animar" + (HasDefault(k) ? " (usa set())" : ""));
      ids.push_back(c.vm.Tween(n, k, v, LNum(c.vm, a[1], "time"), ToText(a[2]), LNum(c.vm, a[3], "delay"),
                               static_cast<int>(LNum(c.vm, a[4], "loop")), Truthy(a[5])));
    }
    return MaybeWait(c, ids, a[6]);
  };
  m["Layer.stop"] = [](Ctx& c, A& a, Kw&) -> Value { c.vm.StopAnim(L(c.vm, a[0])); return {}; };
  m["Layer.move_to"] = [](Ctx& c, A& a, Kw&) {
    return To(c, L(c.vm, a[0]), {{"x", V::F(LNum(c.vm, a[1], "x"))}, {"y", V::F(LNum(c.vm, a[2], "y"))}}, a[3], a[4], a[5]);
  };
  m["Layer.move_by"] = [](Ctx& c, A& a, Kw&) {
    const std::string n = L(c.vm, a[0]);
    const double x = c.vm.AnimTarget(n, "x").d(), y = c.vm.AnimTarget(n, "y").d();
    return To(c, n, {{"x", V::F(x + LNum(c.vm, a[1], "dx"))}, {"y", V::F(y + LNum(c.vm, a[2], "dy"))}}, a[3], a[4], a[5]);
  };
  m["Layer.scale_to"] = [](Ctx& c, A& a, Kw&) { return To(c, L(c.vm, a[0]), {{"scale", V::F(LNum(c.vm, a[1], "scale"))}}, a[2], a[3], a[4]); };
  m["Layer.rotate_to"] = [](Ctx& c, A& a, Kw&) { return To(c, L(c.vm, a[0]), {{"rot", V::F(LNum(c.vm, a[1], "degrees"))}}, a[2], a[3], a[4]); };
  m["Layer.glow"] = [](Ctx& c, A& a, Kw&) -> Value {
    const std::string n = L(c.vm, a[0]);
    c.vm.LayerSetP(n, "glow_color", Color(c.vm, a[2]));
    Fx(c, n, {{"glow", V::F(LNum(c.vm, a[1], "size"))}, {"glow_alpha", V::F(LNum(c.vm, a[3], "alpha"))}}, a[4]);
    return {};
  };
  m["Layer.unglow"] = [](Ctx& c, A& a, Kw&) -> Value { Fx(c, L(c.vm, a[0]), {{"glow", V::F(0)}}, a[1]); return {}; };
  m["Layer.blur"] = [](Ctx& c, A& a, Kw&) -> Value { Fx(c, L(c.vm, a[0]), {{"blur", V::F(std::max(0.0, LNum(c.vm, a[1], "px")))}}, a[2]); return {}; };
  m["Layer.shadow"] = [](Ctx& c, A& a, Kw&) -> Value {
    const std::string n = L(c.vm, a[0]);
    c.vm.LayerSetP(n, "shadow_color", Color(c.vm, a[2]));
    Fx(c, n, {{"shadow", V::F(LNum(c.vm, a[1], "blur"))}, {"shadow_x", V::F(LNum(c.vm, a[3], "x"))},
              {"shadow_y", V::F(LNum(c.vm, a[4], "y"))}, {"shadow_alpha", V::F(LNum(c.vm, a[5], "alpha"))}}, a[6]);
    return {};
  };
  m["Layer.outline"] = [](Ctx& c, A& a, Kw&) -> Value {
    const std::string n = L(c.vm, a[0]);
    c.vm.LayerSetP(n, "outline_color", Color(c.vm, a[2]));
    Fx(c, n, {{"outline", V::F(LNum(c.vm, a[1], "width"))}}, a[3]);
    return {};
  };
  m["Layer.tint"] = [](Ctx& c, A& a, Kw&) -> Value { Fx(c, L(c.vm, a[0]), {{"tint", Color(c.vm, a[1])}}, a[2]); return {}; };
  m["Layer.untint"] = [](Ctx& c, A& a, Kw&) -> Value { Fx(c, L(c.vm, a[0]), {{"tint", V::S("#ffffff")}}, a[1]); return {}; };
  m["Layer.pulse"] = [](Ctx& c, A& a, Kw&) -> Value {
    const std::string n = L(c.vm, a[0]), prop = ToText(a[1]);
    const PropSig* ps = FindProp(prop);
    if (!ps || !ps->anim || ps->type == 3) c.vm.Raise("pulse(): '" + prop + "' no es una propiedad numerica animable");
    const double amount = LNum(c.vm, a[2], "amount"), period = LNum(c.vm, a[3], "period");
    const int times = static_cast<int>(LNum(c.vm, a[4], "times"));
    const double base = c.vm.AnimTarget(n, prop).d();
    const int loops = times < 0 ? -1 : std::max(0, times * 2 - 1);
    c.vm.Tween(n, prop, V::F(base + amount), std::max(0.02, period / 2), "inout", 0, loops, true);
    return {};
  };
  m["Layer.shake"] = [](Ctx& c, A& a, Kw&) -> Value {
    const std::string n = L(c.vm, a[0]);
    const double amount = LNum(c.vm, a[1], "amount"), t = LNum(c.vm, a[2], "time");
    for (const auto& [prop, phase] : {std::pair<const char*, double>{"x", 0.0}, {"y", 1.7}}) {
      const Value base = c.vm.AnimTarget(n, prop);
      const double b = base.d(), ph = phase;
      c.vm.Tween(n, prop, base, t, "linear", 0, 0, false, {}, [b, ph, amount](double u) { return V::F(b + std::sin(u * 50 + ph) * amount * (1 - u)); });
    }
    return {};
  };
  m["Layer.flash"] = [](Ctx& c, A& a, Kw&) -> Value {
    const std::string n = L(c.vm, a[0]);
    const double t = LNum(c.vm, a[2], "time");
    c.vm.LayerSetP(n, "glow_color", Color(c.vm, a[1]));
    const Value g = c.vm.LayerGetP(n, "glow");
    c.vm.LayerSetP(n, "glow", V::F(std::max(24.0, g.num() ? g.d() : 0.0)));
    c.vm.LayerSetP(n, "glow_alpha", V::F(1));
    c.vm.LayerSetP(n, "brightness", V::F(1.8));
    c.vm.Tween(n, "glow_alpha", V::F(0), t, "out");
    c.vm.Tween(n, "brightness", V::F(1), t, "out");
    return {};
  };
  m["Layer.bounce"] = [](Ctx& c, A& a, Kw&) -> Value {
    const std::string n = L(c.vm, a[0]);
    const double h = LNum(c.vm, a[1], "height"), t = LNum(c.vm, a[2], "time");
    const Value base = c.vm.AnimTarget(n, "y");
    const double b = base.d();
    c.vm.Tween(n, "y", base, t, "linear", 0, 0, false, {}, [b, h](double u) { return V::F(b - std::sin(u * 3.14159265358979323846) * h); });
    return {};
  };
  m["Layer.spin"] = [](Ctx& c, A& a, Kw&) -> Value {
    const std::string n = L(c.vm, a[0]);
    const double speed = LNum(c.vm, a[1], "speed");
    const std::vector<std::string> rot{"rot"};
    c.vm.StopAnim(n, &rot);
    if (speed != 0) {
      const Value r = c.vm.LayerGetP(n, "rot");
      const double b = r.num() ? r.d() : 0.0;
      c.vm.Tween(n, "rot", V::F(b), 1.0, "linear", 0, -1, false, {}, [b, speed](double u) { return V::F(b + speed * u); }, true);
    }
    return {};
  };
  m["Layer.set_text"] = [](Ctx& c, A& a, Kw&) -> Value { c.vm.LayerSetP(L(c.vm, a[0]), "text", V::S(ToText(a[1]))); return {}; };
  m["Layer.set_image"] = [](Ctx& c, A& a, Kw&) -> Value { c.vm.LayerSetP(L(c.vm, a[0]), "image", V::S(ToText(a[1]))); return {}; };
  m["Layer.focus"] = [](Ctx& c, A& a, Kw&) -> Value { c.vm.SetFocus(L(c.vm, a[0])); return {}; };
  m["Layer.contains"] = [](Ctx& c, A& a, Kw&) {
    return V::B(c.vm.host->LayerContains(L(c.vm, a[0]), LNum(c.vm, a[1], "x"), LNum(c.vm, a[2], "y")));
  };
  m["Layer.busy"] = [](Ctx& c, A& a, Kw&) { return V::B(c.vm.AnimBusy(L(c.vm, a[0]))); };

  // ----- pantallas, foco y raton
  m["goto"] = [](Ctx& c, A& a, Kw&) -> Value {
    if (a[0].t != V::T::Str) c.vm.Raise("goto() necesita el nombre de la pantalla, no " + TypeName(a[0]));
    c.vm.Goto(a[0].s, ToText(a[1]));
    return {};
  };
  m["back"] = [](Ctx& c, A& a, Kw&) -> Value { c.vm.Back(ToText(a[0])); return {}; };
  m["close"] = [](Ctx& c, A&, Kw&) -> Value { c.vm.CloseUi(); return {}; };
  m["screen"] = [](Ctx& c, A&, Kw&) { return c.vm.Screen() ? V::S(*c.vm.Screen()) : V::N(); };
  m["screens"] = [](Ctx& c, A&, Kw&) {
    std::vector<Value> out;
    for (const auto& s : c.vm.host->ScreenNames()) out.push_back(V::S(s));
    return V::Lst(std::move(out));
  };
  m["history"] = [](Ctx& c, A&, Kw&) {
    std::vector<Value> out;
    for (const auto& s : c.vm.history) out.push_back(V::S(s));
    return V::Lst(std::move(out));
  };
  m["focus"] = [](Ctx& c, A& a, Kw&) -> Value {
    const Value& t = a[0];
    if (t.t == V::T::Layer || t.t == V::T::Str) c.vm.SetFocus(t.s);
    else if (t.none()) c.vm.SetFocus(std::nullopt);
    else c.vm.Raise("focus() necesita una capa o su nombre, no " + TypeName(t));
    return {};
  };
  m["focused"] = [](Ctx& c, A&, Kw&) { return c.vm.Focused() ? V::Lay(*c.vm.Focused()) : V::N(); };
  m["focus_move"] = [](Ctx& c, A& a, Kw&) -> Value {
    std::string d = ToText(a[0]);
    for (auto& ch : d) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    if (d != "up" && d != "down" && d != "left" && d != "right") c.vm.Raise("focus_move(): direccion '" + d + "' (usa up, down, left o right)");
    c.vm.host->MoveFocus(d);
    return {};
  };
  m["lock_focus"] = [](Ctx& c, A& a, Kw&) -> Value {
    c.vm.focus_locked = Truthy(a[0]);
    c.vm.host->LockFocus(c.vm.focus_locked);
    return {};
  };
  m["mouse_x"] = [](Ctx& c, A&, Kw&) { return V::F(c.vm.mouse_x); };
  m["mouse_y"] = [](Ctx& c, A&, Kw&) { return V::F(c.vm.mouse_y); };
  m["mouse_down"] = [](Ctx& c, A&, Kw&) { return V::B(c.vm.mouse_down); };
  m["hovered"] = [](Ctx& c, A&, Kw&) {
    const auto n = c.vm.host->LayerAt(c.vm.mouse_x, c.vm.mouse_y);
    return n ? V::Lay(*n) : V::N();
  };

  // ----- eventos y sonido
  m["emit"] = [](Ctx& c, A& a, Kw&) -> Value { c.vm.Emit(ToText(a[0]), a[1]); return {}; };
  m["sound"] = [](Ctx& c, A& a, Kw&) -> Value {
    const std::string n = ToText(a[0]);
    const auto& s = SoundNames();
    if (std::find(s.begin(), s.end(), n) == s.end()) c.vm.LogMsg("warn", "sound(): '" + n + "' no es un sonido conocido del menu");
    c.vm.host->PlaySound(n, D(c.vm, a[1]));
    return {};
  };

  // ----- juego
  m["game.get"] = [](Ctx& c, A& a, Kw&) {
    Value v = c.vm.host->GameGet(GName(c.vm, a[0], "game.get"));
    return v.none() ? a[1] : v;
  };
  m["game.set"] = [](Ctx& c, A& a, Kw&) -> Value {
    const Value& v = a[1];
    if (!(v.num() || v.t == V::T::Str || v.t == V::T::Bool)) c.vm.Raise("game.set(): solo numeros, textos o bool (llego " + TypeName(v) + ")");
    c.vm.host->GameSet(GName(c.vm, a[0], "game.set"), v);
    return {};
  };
  m["game.call"] = [](Ctx& c, A& a, Kw& kw) { return c.vm.host->GameCall(GName(c.vm, a[0], "game.call"), kw); };
  m["game.player"] = [](Ctx& c, A& a, Kw&) -> Value {
    const std::string f = ToText(a[0]);
    const auto& F = PlayerFields();
    if (std::find(F.begin(), F.end(), f) == F.end()) {
      std::string all;
      for (const auto& x : F) all += (all.empty() ? "" : ", ") + x;
      c.vm.Raise("game.player(): campo '" + f + "' (hay: " + all + ")");
    }
    return c.vm.host->PlayerInfo(static_cast<int>(Int(c.vm, a[1])), f);
  };
  m["game.band"] = [](Ctx& c, A& a, Kw&) -> Value {
    const std::string f = ToText(a[0]);
    const auto& F = BandFields();
    if (std::find(F.begin(), F.end(), f) == F.end()) {
      std::string all;
      for (const auto& x : F) all += (all.empty() ? "" : ", ") + x;
      c.vm.Raise("game.band(): campo '" + f + "' (hay: " + all + ")");
    }
    return c.vm.host->BandInfo(f);
  };
  m["game.open_menu"] = [](Ctx& c, A& a, Kw&) -> Value {
    const std::string st = GName(c.vm, a[0], "game.open_menu");
    c.vm.host->GameCall("ui_event", {{"event", V::S("menu_change")}, {"state", V::S(st)}});
    return {};
  };
  m["game.in_song"] = [](Ctx& c, A&, Kw&) { return V::B(Truthy(c.vm.host->GameState("in_song"))); };
  m["game.paused"] = [](Ctx& c, A&, Kw&) { return V::B(Truthy(c.vm.host->GameState("paused"))); };
  m["game.language"] = [](Ctx& c, A&, Kw&) {
    const Value v = c.vm.host->GameState("language");
    return V::S(Truthy(v) ? ToText(v) : "en");
  };

  // ----- metodos de listas
  m["list.append"] = [](Ctx& c, A& a, Kw&) -> Value { Mut(c.vm, a[0]).push_back(a[1]); return {}; };
  m["list.insert"] = [](Ctx& c, A& a, Kw&) -> Value {
    auto& x = Mut(c.vm, a[0]);
    x.insert(x.begin() + PyIndex(c.vm, a[1], static_cast<int64_t>(x.size())), a[2]);
    return {};
  };
  m["list.pop"] = [](Ctx& c, A& a, Kw&) -> Value {
    auto& x = Mut(c.vm, a[0]);
    if (x.empty()) c.vm.Raise("pop() en una lista vacia");
    const int64_t n = static_cast<int64_t>(x.size()), i = Int(c.vm, a[1]);
    if (!(-n <= i && i < n)) c.vm.Raise("pop(" + std::to_string(i) + "): la lista tiene " + std::to_string(n) + " elementos");
    const int64_t k = i < 0 ? i + n : i;
    Value v = x[k];
    x.erase(x.begin() + k);
    return v;
  };
  m["list.remove"] = [](Ctx& c, A& a, Kw&) -> Value {
    auto& x = Mut(c.vm, a[0]);
    auto it = std::find_if(x.begin(), x.end(), [&](const Value& v) { return Eq(v, a[1]); });
    if (it == x.end()) return V::B(false);
    x.erase(it);
    return V::B(true);
  };
  m["list.clear"] = [](Ctx& c, A& a, Kw&) -> Value { Mut(c.vm, a[0]).clear(); return {}; };
  m["list.index"] = [](Ctx& c, A& a, Kw&) -> Value {
    const auto& x = Lst(c.vm, a[0]);
    for (size_t i = 0; i < x.size(); ++i) if (Eq(x[i], a[1])) return V::I(static_cast<int64_t>(i));
    return V::I(-1);
  };
  m["list.count"] = [](Ctx& c, A& a, Kw&) {
    const auto& x = Lst(c.vm, a[0]);
    return V::I(std::count_if(x.begin(), x.end(), [&](const Value& v) { return Eq(v, a[1]); }));
  };
  m["list.contains"] = [](Ctx& c, A& a, Kw&) {
    const auto& x = Lst(c.vm, a[0]);
    return V::B(std::any_of(x.begin(), x.end(), [&](const Value& v) { return Eq(v, a[1]); }));
  };
  m["list.reverse"] = [](Ctx& c, A& a, Kw&) -> Value { auto& x = Mut(c.vm, a[0]); std::reverse(x.begin(), x.end()); return {}; };
  m["list.sort"] = [](Ctx& c, A& a, Kw&) -> Value { SortList(c.vm, Mut(c.vm, a[0]), Truthy(a[1])); return {}; };
  m["list.copy"] = [](Ctx& c, A& a, Kw&) { return V::Lst(Lst(c.vm, a[0])); };
  m["list.join"] = [](Ctx& c, A& a, Kw&) -> Value {
    if (a[1].t != V::T::Str) c.vm.Raise("join() necesita un texto como separador");
    return V::S(Join(Lst(c.vm, a[0]), a[1].s));
  };

  // ----- metodos de textos
  auto mapcase = [](const std::string& s, bool up) {
    auto cps = Utf8Decode(s);
    for (auto& c : cps) {
      if (up) {
        if ((c >= 'a' && c <= 'z') || (c >= 0xE0 && c <= 0xFE && c != 0xF7)) c -= 0x20;
      } else {
        if ((c >= 'A' && c <= 'Z') || (c >= 0xC0 && c <= 0xDE && c != 0xD7)) c += 0x20;
      }
    }
    return Utf8Encode(cps);
  };
  m["str.upper"] = [mapcase](Ctx&, A& a, Kw&) { return V::S(mapcase(a[0].s, true)); };
  m["str.lower"] = [mapcase](Ctx&, A& a, Kw&) { return V::S(mapcase(a[0].s, false)); };
  m["str.strip"] = [](Ctx&, A& a, Kw&) {
    std::string s = a[0].s;
    const char* ws = " \t\n\r\f\v";
    s.erase(0, s.find_first_not_of(ws));
    const auto e = s.find_last_not_of(ws);
    s.erase(e == std::string::npos ? 0 : e + 1);
    return V::S(s);
  };
  m["str.replace"] = [](Ctx&, A& a, Kw&) {
    const std::string s = a[0].s, from = ToText(a[1]), to = ToText(a[2]);
    if (from.empty()) {   // como Python: entre cada letra
      std::string out = to;
      for (auto c : Utf8Decode(s)) out += Utf8Encode({c}) + to;
      return V::S(out);
    }
    std::string out;
    size_t pos = 0, f;
    while ((f = s.find(from, pos)) != std::string::npos) { out += s.substr(pos, f - pos) + to; pos = f + from.size(); }
    return V::S(out + s.substr(pos));
  };
  m["str.split"] = [](Ctx&, A& a, Kw&) {
    const std::string& s = a[0].s;
    std::vector<Value> out;
    if (Truthy(a[1])) {
      const std::string sep = ToText(a[1]);
      size_t pos = 0, f;
      while ((f = s.find(sep, pos)) != std::string::npos) { out.push_back(V::S(s.substr(pos, f - pos))); pos = f + sep.size(); }
      out.push_back(V::S(s.substr(pos)));
    } else {
      std::string cur;
      for (char ch : s) {
        if (std::isspace(static_cast<unsigned char>(ch))) { if (!cur.empty()) out.push_back(V::S(cur)); cur.clear(); }
        else cur += ch;
      }
      if (!cur.empty()) out.push_back(V::S(cur));
    }
    return V::Lst(std::move(out));
  };
  m["str.startswith"] = [](Ctx&, A& a, Kw&) { return V::B(a[0].s.rfind(ToText(a[1]), 0) == 0); };
  m["str.endswith"] = [](Ctx&, A& a, Kw&) {
    const std::string suf = ToText(a[1]);
    return V::B(a[0].s.size() >= suf.size() && a[0].s.compare(a[0].s.size() - suf.size(), suf.size(), suf) == 0);
  };
  m["str.find"] = [](Ctx&, A& a, Kw&) {
    const auto f = a[0].s.find(ToText(a[1]));
    return V::I(f == std::string::npos ? -1 : static_cast<int64_t>(Utf8Decode(a[0].s.substr(0, f)).size()));
  };
  m["str.contains"] = [](Ctx&, A& a, Kw&) { return V::B(a[0].s.find(ToText(a[1])) != std::string::npos); };
  m["str.pad"] = [](Ctx& c, A& a, Kw&) {
    const auto f = Utf8Decode(ToText(a[2]));
    const std::string ch = f.empty() ? " " : Utf8Encode({f[0]});
    const int64_t w = Int(c.vm, a[1]), len = static_cast<int64_t>(Utf8Decode(a[0].s).size());
    std::string out;
    for (int64_t k = len; k < w; ++k) out += ch;
    return V::S(out + a[0].s);
  };
  return m;
}

}  // namespace

const std::unordered_map<std::string, BuiltinFn>& Stdlib() {
  static const M m = Build();
  return m;
}

}  // namespace wui
