// Ejecuta un caso de conformidad con la maquina C++ de re-wor (re-wor/src/de/wui_vm.cpp).
//   run_cpp <caso.wuib> <caso.txt>    (caso.txt lo escribe run_cpp.py)
// Escribe en la salida una linea por resultado: L nivel mensaje / J diario / E error / G global valor
// (separados por tabuladores; los saltos de linea van como \n). run_cpp.py normaliza y compara.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>

#include "de/wui_internal.hpp"

using namespace wui;

static std::string Esc(const std::string& s) {
  std::string o;
  for (char c : s) {
    if (c == '\n') o += "\\n";
    else if (c == '\t') o += "\\t";
    else if (c == '\\') o += "\\\\";
    else o += c;
  }
  return o;
}

struct SimLayer {
  double w = 200, h = 80;
  std::map<std::string, Value> props;
};

class SimHost : public Host {
 public:
  std::vector<std::string> order, screens;
  std::map<std::string, SimLayer> layers;
  std::map<std::string, Value> game;
  std::vector<std::string> journal;

  void Add(const std::string& n) {
    SimLayer L;
    for (const auto& p : Props()) if (p.writable) L.props[p.name] = p.dflt;
    L.props["x"] = V::F(640);
    L.props["y"] = V::F(360);
    L.props["focusable"] = V::B(false);
    layers[n] = L;
    order.push_back(n);
  }
  void J(std::initializer_list<std::string> parts) {
    std::string s;
    for (const auto& p : parts) s += (s.empty() ? "" : " ") + p;
    journal.push_back(s);
  }
  std::vector<std::string> LayerNames() override { return order; }
  Value LayerGet(const std::string& n, const std::string& p) override {
    auto it = layers.find(n);
    if (it == layers.end()) return {};
    if (p == "w") return V::F(it->second.w);
    if (p == "h") return V::F(it->second.h);
    if (p == "kind") return V::S("shape");
    if (p == "name") return V::S(n);
    auto j = it->second.props.find(p);
    return j == it->second.props.end() ? Value{} : j->second;
  }
  void LayerSet(const std::string& n, const std::string& p, const Value& v) override {
    auto it = layers.find(n);
    if (it == layers.end()) return;
    it->second.props[p] = v;
    J({"set", n, p, ToText(v)});
  }
  std::map<std::string, Value> LayerDesign(const std::string& n) override {
    std::map<std::string, Value> d;
    if (!layers.count(n)) return d;
    for (const auto& p : Props()) if (p.writable) d[p.name] = p.dflt;
    d["x"] = V::F(640);
    d["y"] = V::F(360);
    return d;
  }
  bool LayerContains(const std::string& n, double x, double y) override {
    auto it = layers.find(n);
    if (it == layers.end()) return false;
    auto& P = it->second.props;
    double s = P.count("scale") && P["scale"].numb() ? P["scale"].d() : 1;
    if (s == 0) s = 1;
    return std::fabs(x - P["x"].d()) <= it->second.w * s / 2 && std::fabs(y - P["y"].d()) <= it->second.h * s / 2;
  }
  std::optional<std::string> LayerAt(double x, double y) override {
    for (auto it = order.rbegin(); it != order.rend(); ++it) {
      auto& P = layers[*it].props;
      const bool hidden = P.count("visible") && P["visible"].t == V::T::Bool && !P["visible"].b;
      if (!hidden && LayerContains(*it, x, y)) return *it;
    }
    return std::nullopt;
  }
  std::vector<std::string> ScreenNames() override { return screens; }
  void ShowScreen(const std::string& n, const std::string& tr, const std::optional<std::string>&) override { J({"screen", n, tr}); }
  void Close() override { J({"close"}); }
  void SetFocus(const std::optional<std::string>& n) override { J({"focus", n ? *n : "nada"}); }
  void MoveFocus(const std::string& d) override { J({"move_focus", d}); }
  void LockFocus(bool l) override { J({"lock_focus", l ? "True" : "False"}); }
  Value GameGet(const std::string& n) override { auto it = game.find(n); return it == game.end() ? Value{} : it->second; }
  void GameSet(const std::string& n, const Value& v) override { game[n] = v; J({"game_set", n, ToText(v)}); }
  Value GameCall(const std::string& s, const std::vector<std::pair<std::string, Value>>& p) override {
    auto q = p;
    std::sort(q.begin(), q.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    std::string d = "{";
    for (size_t i = 0; i < q.size(); ++i) d += (i ? ", " : "") + q[i].first + "=" + ToText(q[i].second);
    J({"game_call", s, d + "}"});
    return {};
  }
  Value PlayerInfo(int, const std::string&) override { return V::I(0); }
  Value BandInfo(const std::string&) override { return V::I(0); }
  Value GameState(const std::string& w) override { return GameGet("#" + w); }
  void PlaySound(const std::string& n, double) override { J({"sound", n}); }
  void Vibrate(int p, double s, double t) override { J({"vibrate", std::to_string(p), ToText(V::F(s)), ToText(V::F(t))}); }
};

int main(int argc, char** argv) {
  if (argc < 3) { std::cerr << "uso: run_cpp caso.wuib caso.txt\n"; return 2; }
  std::ifstream bf(argv[1], std::ios::binary);
  std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(bf)), {});
  Program prog;
  std::string err;
  if (!LoadProgram(bytes.data(), bytes.size(), &prog, &err)) { std::cerr << "no carga: " << err << "\n"; return 3; }

  // caso.txt: seed N / seconds X / layer n / screen n / at t kind val
  SimHost host;
  host.game["#language"] = V::S("es");
  uint64_t seed = 0;
  double seconds = 1;
  struct Ev { double t; std::string kind, val; };
  std::vector<Ev> script;
  std::ifstream cf(argv[2]);
  std::string line;
  while (std::getline(cf, line)) {
    std::istringstream ls(line);
    std::string k;
    ls >> k;
    if (k == "seed") ls >> seed;
    else if (k == "seconds") ls >> seconds;
    else if (k == "layer") { std::string n; ls >> n; host.Add(n); }
    else if (k == "screen") { std::string n; ls >> n; host.screens.push_back(n); }
    else if (k == "at") { Ev e; ls >> e.t >> e.kind >> e.val; script.push_back(e); }
  }
  std::stable_sort(script.begin(), script.end(), [](const Ev& a, const Ev& b) { return a.t < b.t; });

  Machine vm(std::move(prog), &host, seed);
  vm.Start();
  std::set<std::string> held;
  size_t i = 0;
  const int frames = static_cast<int>(std::nearbyint(seconds * 60));
  for (int f = 0; f < frames; ++f) {
    const double t = f / 60.0;
    while (i < script.size() && script[i].t <= t + 1e-9) {
      const Ev& e = script[i++];
      if (e.kind == "down") held.insert(e.val);
      else if (e.kind == "up") held.erase(e.val);
      else if (e.kind == "focus") vm.NotifyFocus(e.val == "-" ? std::nullopt : std::optional<std::string>(e.val));
      else if (e.kind == "click") vm.NotifyClick(e.val);
    }
    Inputs in;
    in.buttons[1] = held;
    vm.Step(1.0 / 60, in);
  }
  for (const auto& [lvl, m] : vm.Logs()) std::cout << "L\t" << lvl << "\t" << Esc(m) << "\n";
  for (const auto& j : host.journal) std::cout << "J\t" << Esc(j) << "\n";
  for (const auto& e : vm.Errors()) std::cout << "E\t" << Esc(e.function + ":" + std::to_string(e.line) + ": " + e.message) << "\n";
  for (const auto& g : vm.Prog().globals) std::cout << "G\t" << g << "\t" << Esc(ToText(vm.GlobalValue(g))) << "\n";
  return 0;
}
