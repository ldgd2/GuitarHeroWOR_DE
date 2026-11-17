// UI3D: interfaces del editor de interfaces de DE Studio (2D, 2.5D y 3D, con pantallas y botones). Ver ui3d.hpp.
#include "de/ui3d.hpp"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

#include <rex/input/input.h>
#include <rex/input/input_system.h>
#include <rex/runtime.h>
#include <rex/ui/image_decode.h>
#include <rex/ui/imgui_drawer.h>
#include <rex/ui/immediate_drawer.h>

#include "core/crash_handler.hpp"
#include "core/path_resolver.hpp"
#include "de/qb_bridge.hpp"
#include "de/wui_vm.hpp"

namespace re_wor::de::ui3d {

namespace {

namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

struct Cam { float p[3], R[3], U[3], F[3], fov = 50; };
struct ObjFrame { int obj; float rgba[4]; float c[12]; };
struct Frame { Cam cam; std::vector<ObjFrame> objs; };
struct Obj {
  int tex = -1;
  float order = 0;
  int focus = -1;        // boton de su pantalla (indice) o -1
  int show_on = -1;      // solo visible con el foco de este boton
  std::string cond;      // variable del juego de la condicion ("" = sin condicion)
  std::string op = "=";
  int cond_val = 0;
  int cond_cur = 0;      // ultimo valor leido (hilo del juego)
  // para la logica (objinfo)
  int screen = 0;
  std::string kind = "shape", name;
  float x0 = 640, y0 = 360, w0 = 0, h0 = 0;   // diseno en px del editor (1280x720)
};
struct Action {
  enum Kind { kNone, kGoto, kBack, kClose, kSet, kScript } kind = kNone;
  int screen = 0, value = 0;
  std::string text;
};
struct Focus {
  int obj = -1;
  int nb[4] = {-1, -1, -1, -1};   // arriba, abajo, izquierda, derecha
  bool sound = true;
  float scale = 1, col[3] = {1, 1, 1}, alpha = 1, dx = 0, dy = 0;
  Action act;
};
struct ScreenDef {
  std::string name, on_back = "auto";
  int default_focus = -1;
  std::vector<Focus> focus;
  std::map<std::string, std::vector<Frame>> clips;
};

struct Scene {
  std::string name;
  fs::path dir;
  fs::file_time_type mtime{};
  float fps = 30;
  int start = 0;
  std::vector<std::string> tex_files;
  std::vector<Obj> objs;
  std::vector<ScreenDef> screens;
  // texturas del drawer (se crean en el hilo de dibujo; nullptr = aun no / fallo)
  std::vector<std::unique_ptr<rex::ui::ImmediateTexture>> tex;
  std::vector<bool> tex_tried;
  std::shared_ptr<const wui::Program> logic;   // ui3d/<n>/logic.wuib (o nada)
};

enum class Mode { kHidden, kIntro, kLoop, kOutro };
enum Key { kUp = 1, kDown, kLeft, kRight, kChoose, kBack };
struct Play {
  Mode mode = Mode::kHidden;
  Clock::time_point start{};
  int last_seq = 0, last_kseq = 0;
  bool seen = false;               // primera lectura de las globales: solo se anota
  int screen = 0;
  int pending = -2;                // al acabar la salida: -2 nada, -1 cerrar, >= 0 ir a esa pantalla
  bool closing = false;            // la salida es de cerrar la interfaz (re_ui_<n>_hide ya pedido)
  std::vector<int> history;
  int focus = -1;
  std::vector<float> ff;           // foco animado de cada boton de la pantalla (0..1)
  Clock::time_point last_anim{};
  // raton (hilo de dibujo -> hilo del juego)
  ImVec2 last_mouse{-1, -1};
  std::vector<std::pair<int, std::array<ImVec2, 4>>> hit;   // boton -> cuadrilatero en pantalla del ultimo dibujo
  std::vector<int> mouse_events;   // 100 + b = encima de b, 200 + b = clic en b
  // logica (wui): la maquina vive mientras la interfaz esta abierta
  std::shared_ptr<struct LogicHost> host;
  std::shared_ptr<wui::Machine> vm;
  std::map<std::string, std::map<std::string, wui::Value>> ov;   // capa -> propiedades que cambio la logica
  Clock::time_point last_logic{};
  bool locked = false;              // lock_focus(): el mando no mueve el foco ni elige
  int vm_focus = -2;                // ultimo foco avisado a la maquina
  ImVec2 mouse_ed{-1, -1};          // raton en px del editor (lo escribe el hilo de dibujo)
  bool mouse_down = false;
  std::map<int, std::array<ImVec2, 4>> quads;   // obj -> cuadrilatero en px del editor (ultimo dibujo)
  std::vector<int> clicks;          // capas clicadas (obj) para on_click de capas que no son botones
};

std::mutex g_mu;
std::map<std::string, std::unique_ptr<Scene>> g_scenes;   // por carpeta
std::map<std::string, Play> g_play;                       // por nombre de interfaz
// escenas sustituidas: sus texturas pueden seguir en la lista de dibujo de ImGui de este frame, asi que se
// liberan unos segundos despues
std::vector<std::pair<Clock::time_point, std::unique_ptr<Scene>>> g_old;

bool LoadScene(const fs::path& file, Scene& s) {
  std::ifstream in(file);
  if (!in) return false;
  std::string line, cmd;
  ScreenDef* scr = nullptr;
  std::vector<Frame>* clip = nullptr;
  Frame* fr = nullptr;
  bool ok = false;
  auto cur = [&]() -> ScreenDef& {   // version 1: sin lineas "screen", todo es de la pantalla 0
    if (!scr) { if (s.screens.empty()) s.screens.emplace_back(); scr = &s.screens[0]; }
    return *scr;
  };
  while (std::getline(in, line)) {
    std::istringstream ss(line);
    if (!(ss >> cmd)) continue;
    if (cmd == "ui3d") ok = true;
    else if (cmd == "name") ss >> s.name;
    else if (cmd == "fps") ss >> s.fps;
    else if (cmd == "start") ss >> s.start;
    else if (cmd == "screens") {
      int n = 0; ss >> n;
      s.screens.reserve(std::clamp(n, 0, 256));   // los punteros a pantallas no se mueven al crecer
    } else if (cmd == "tex") {
      int i; std::string f; ss >> i >> f;
      if (i >= 0 && i < 4096) { if (static_cast<int>(s.tex_files.size()) <= i) s.tex_files.resize(i + 1); s.tex_files[i] = f; }
    } else if (cmd == "screen") {
      int i = 0; ss >> i;
      if (i < 0 || i >= static_cast<int>(s.screens.capacity()) || i >= 256) continue;
      if (static_cast<int>(s.screens.size()) <= i) s.screens.resize(i + 1);
      scr = &s.screens[i];
      ss >> scr->name >> scr->on_back;
      clip = nullptr; fr = nullptr;
    } else if (cmd == "obj") {
      int i; Obj o; ss >> i >> o.tex >> o.order;
      cur();
      o.screen = static_cast<int>(scr - s.screens.data());
      if (i >= 0 && i < 8192) { if (static_cast<int>(s.objs.size()) <= i) s.objs.resize(i + 1); s.objs[i] = o; }
    } else if (cmd == "objui") {
      int i = -1; ss >> i;
      if (i < 0 || i >= static_cast<int>(s.objs.size())) continue;
      Obj& o = s.objs[i];
      std::string g;
      ss >> o.focus >> o.show_on >> g >> o.op >> o.cond_val;
      o.cond = g == "-" ? "" : g;
    } else if (cmd == "objinfo") {
      int i = -1; ss >> i;
      if (i < 0 || i >= static_cast<int>(s.objs.size())) continue;
      Obj& o = s.objs[i];
      ss >> o.kind >> o.x0 >> o.y0 >> o.w0 >> o.h0;
      std::getline(ss >> std::ws, o.name);
    } else if (cmd == "defaultfocus") {
      ss >> cur().default_focus;
    } else if (cmd == "focus") {
      int b = -1; ss >> b;
      if (b < 0 || b >= 1024) continue;
      auto& F = cur().focus;
      if (static_cast<int>(F.size()) <= b) F.resize(b + 1);
      Focus& f = F[b];
      int snd = 1;
      ss >> f.obj >> f.nb[0] >> f.nb[1] >> f.nb[2] >> f.nb[3] >> snd >> f.scale >> f.col[0] >> f.col[1] >> f.col[2] >> f.alpha >> f.dx >> f.dy;
      f.sound = snd != 0;
    } else if (cmd == "act") {
      int b = -1; std::string k; ss >> b >> k;
      auto& F = cur().focus;
      if (b < 0 || b >= static_cast<int>(F.size())) continue;
      Action& a = F[b].act;
      if (k == "goto") { a.kind = Action::kGoto; ss >> a.screen; }
      else if (k == "back") a.kind = Action::kBack;
      else if (k == "close") a.kind = Action::kClose;
      else if (k == "set") { a.kind = Action::kSet; ss >> a.text >> a.value; }
      else if (k == "script") { a.kind = Action::kScript; std::getline(ss >> std::ws, a.text); }
    } else if (cmd == "clip") {
      std::string n; int frames = 0; ss >> n >> frames;
      clip = &cur().clips[n];
      clip->reserve(std::max(0, std::min(frames, 100000)));
      fr = nullptr;
    } else if (cmd == "f" && clip) {
      clip->emplace_back();
      fr = &clip->back();
    } else if (cmd == "cam" && fr) {
      Cam& c = fr->cam;
      ss >> c.p[0] >> c.p[1] >> c.p[2] >> c.R[0] >> c.R[1] >> c.R[2] >> c.U[0] >> c.U[1] >> c.U[2]
         >> c.F[0] >> c.F[1] >> c.F[2] >> c.fov;
    } else if (cmd == "o" && fr) {
      ObjFrame o{};
      ss >> o.obj;
      for (float& v : o.rgba) ss >> v;
      for (float& v : o.c) ss >> v;
      if (ss && o.obj >= 0) fr->objs.push_back(o);
    }
  }
  s.tex.resize(s.tex_files.size());
  s.tex_tried.assign(s.tex_files.size(), false);
  s.fps = std::max(1.f, s.fps);
  if (s.screens.empty()) s.screens.emplace_back();
  s.start = std::clamp(s.start, 0, static_cast<int>(s.screens.size()) - 1);
  return ok && !s.name.empty();
}

// Relee <exe>/ui3d/*/scene.ui3d si cambiaron (el editor exporta con el juego abierto)
void Rescan() {
  const fs::path root = re_wor::core::get_executable_directory() / "ui3d";
  std::error_code ec;
  if (!fs::is_directory(root, ec)) return;
  for (const auto& d : fs::directory_iterator(root, ec)) {
    const fs::path f = d.path() / "scene.ui3d";
    if (!fs::is_regular_file(f, ec)) continue;
    const auto mt = fs::last_write_time(f, ec);
    const std::string key = d.path().filename().string();
    {
      std::lock_guard<std::mutex> lk(g_mu);
      auto it = g_scenes.find(key);
      if (it != g_scenes.end() && it->second->mtime == mt) continue;
    }
    auto s = std::make_unique<Scene>();
    s->dir = d.path();
    s->mtime = mt;
    if (!LoadScene(f, *s)) {
      TRACE("[UI3D] no se pudo leer " << f.string());
      continue;
    }
    {
      std::ifstream lf(d.path() / "logic.wuib", std::ios::binary);
      if (lf) {
        std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(lf)), std::istreambuf_iterator<char>());
        auto prog = std::make_shared<wui::Program>();
        std::string err;
        if (wui::LoadProgram(bytes.data(), bytes.size(), prog.get(), &err)) s->logic = std::move(prog);
        else TRACE("[UI3D] " << s->name << ": logica no valida: " << err);
      }
    }
    size_t frames = 0, buttons = 0;
    for (const auto& sc : s->screens) {
      buttons += sc.focus.size();
      for (const auto& [n, c] : sc.clips) frames += c.size();
    }
    TRACE("[UI3D] interfaz " << s->name << ": " << s->screens.size() << " pantallas, " << s->objs.size() << " capas, "
                             << buttons << " botones, " << s->tex_files.size() << " imagenes, " << frames << " fotogramas");
    std::lock_guard<std::mutex> lk(g_mu);
    auto& slot = g_scenes[key];
    if (slot) g_old.emplace_back(Clock::now(), std::move(slot));
    slot = std::move(s);
    Play& p = g_play[slot->name];   // la pantalla abierta puede no existir ya
    if (p.screen >= static_cast<int>(slot->screens.size())) { p.screen = slot->start; p.history.clear(); }
    std::erase_if(p.history, [&](int i) { return i >= static_cast<int>(slot->screens.size()); });
    p.ff.clear();
    p.vm.reset();      // la logica nueva empieza la proxima vez que se abra
    p.host.reset();
    p.ov.clear();
  }
  std::lock_guard<std::mutex> lk(g_mu);
  const auto now = Clock::now();
  std::erase_if(g_old, [&](const auto& e) { return now - e.first > std::chrono::seconds(3); });
}

Scene* FindScene(const std::string& name) {
  for (auto& [k, s] : g_scenes) if (s->name == name) return s.get();
  return nullptr;
}

void Sfx(const char* type) { qb_bridge::ExecuteScriptQuiet(std::string("UI_SFX menustate = generic uitype = ") + type); }

// ---- logica de pantallas (con g_mu tomado)
void EnterScreen(Scene& s, Play& p, int screen, Clock::time_point now) {
  p.screen = std::clamp(screen, 0, static_cast<int>(s.screens.size()) - 1);
  p.mode = Mode::kIntro;
  p.start = now;
  p.pending = -2;
  p.closing = false;
  const ScreenDef& sd = s.screens[p.screen];
  p.focus = sd.focus.empty() ? -1 : std::clamp(sd.default_focus, 0, static_cast<int>(sd.focus.size()) - 1);
  p.ff.assign(sd.focus.size(), 0.f);
}

void StartOutro(Play& p, int pending, Clock::time_point now) {
  p.pending = pending;
  p.mode = Mode::kOutro;
  p.start = now;
}

void Close(Scene& s, Play& p, Clock::time_point now) {
  if (p.closing) return;
  p.closing = true;
  StartOutro(p, -1, now);
  // los scripts devuelven el foco al menu del juego y avisan con la orden 2 (que ya no reinicia la salida)
  qb_bridge::ExecuteScriptQuiet("re_ui_" + s.name + "_hide");
}

void Goto(Scene& s, Play& p, int screen, bool push, Clock::time_point now) {
  if (screen < 0 || screen >= static_cast<int>(s.screens.size()) || screen == p.screen) return;
  if (push) p.history.push_back(p.screen);
  StartOutro(p, screen, now);
}

void Back(Scene& s, Play& p, Clock::time_point now) {
  if (p.history.empty()) { Close(s, p, now); return; }
  const int to = p.history.back();
  p.history.pop_back();
  StartOutro(p, to, now);
}

void Run(Scene& s, Play& p, const Action& a, uint8_t* base, Clock::time_point now) {
  switch (a.kind) {
    case Action::kGoto: Goto(s, p, a.screen, true, now); break;
    case Action::kBack: Back(s, p, now); break;
    case Action::kClose: Close(s, p, now); break;
    case Action::kSet:
      if (!qb_bridge::WriteIntGlobal(a.text, base, a.value)) TRACE("[UI3D] " << s.name << ": no existe la variable entera " << a.text);
      break;
    case Action::kScript: qb_bridge::ExecuteScriptQuiet(a.text); break;
    default: break;
  }
}

void Input(Scene& s, Play& p, int key, uint8_t* base, Clock::time_point now) {
  if (p.mode != Mode::kIntro && p.mode != Mode::kLoop) return;
  const ScreenDef& sd = s.screens[p.screen];
  if (p.locked && key != kBack) return;
  if (key >= kUp && key <= kRight) {
    if (sd.focus.empty()) return;
    if (p.focus < 0) { p.focus = 0; return; }
    const int to = sd.focus[p.focus].nb[key - kUp];
    if (to >= 0 && to < static_cast<int>(sd.focus.size()) && to != p.focus) {
      p.focus = to;
      if (sd.focus[to].sound) Sfx(key == kUp || key == kLeft ? "scrollup" : "scrolldown");
    }
  } else if (key == kChoose) {
    if (p.focus < 0 || p.focus >= static_cast<int>(sd.focus.size())) return;
    const Action a = sd.focus[p.focus].act;   // copia: Run puede cambiar de pantalla
    if (sd.focus[p.focus].sound) Sfx(a.kind == Action::kBack || a.kind == Action::kClose ? "back" : "select");
    Run(s, p, a, base, now);
  } else if (key == kBack) {
    if (sd.on_back == "none") return;
    Sfx("back");
    if (sd.on_back == "close") Close(s, p, now);
    else Back(s, p, now);   // auto y back: la anterior; en la primera, cerrar
  }
}

// ---- logica (wui), con g_mu tomado y en el hilo del juego
rex::Runtime* g_runtime = nullptr;

int ScreenOf(const Play& p) { return p.mode == Mode::kOutro && p.pending >= 0 ? p.pending : p.screen; }

struct LogicHost final : wui::Host {
  Scene* s = nullptr;
  Play* p = nullptr;
  uint8_t* base = nullptr;
  Clock::time_point now{};

  int Find(const std::string& n) const {   // capa de la pantalla actual con ese nombre
    const int scr = ScreenOf(*p);
    for (size_t i = 0; i < s->objs.size(); ++i)
      if (s->objs[i].screen == scr && s->objs[i].name == n) return static_cast<int>(i);
    return -1;
  }
  std::vector<std::string> LayerNames() override {
    std::vector<std::string> out;
    const int scr = ScreenOf(*p);
    for (const auto& o : s->objs)
      if (o.screen == scr && !o.name.empty() && std::find(out.begin(), out.end(), o.name) == out.end()) out.push_back(o.name);
    return out;
  }
  wui::Value LayerGet(const std::string& n, const std::string& prop) override {
    auto it = p->ov.find(n);
    if (it != p->ov.end()) { auto j = it->second.find(prop); if (j != it->second.end()) return j->second; }
    const int i = Find(n);
    if (i < 0) return {};
    const Obj& o = s->objs[i];
    if (prop == "x") return wui::V::F(o.x0);
    if (prop == "y") return wui::V::F(o.y0);
    if (prop == "w") return wui::V::F(o.w0);
    if (prop == "h") return wui::V::F(o.h0);
    if (prop == "kind") return wui::V::S(o.kind);
    if (prop == "name") return wui::V::S(o.name);
    if (prop == "focusable") return wui::V::B(o.focus >= 0);
    return {};
  }
  void LayerSet(const std::string& n, const std::string& prop, const wui::Value& v) override { p->ov[n][prop] = v; }
  std::map<std::string, wui::Value> LayerDesign(const std::string& n) override {
    const int i = Find(n);
    if (i < 0) return {};
    return {{"x", wui::V::F(s->objs[i].x0)}, {"y", wui::V::F(s->objs[i].y0)}};
  }
  bool LayerContains(const std::string& n, double x, double y) override {
    const int i = Find(n);
    auto it = p->quads.find(i);
    if (i < 0 || it == p->quads.end()) return false;
    int pos = 0, neg = 0;
    const auto& q = it->second;
    for (int k = 0; k < 4; ++k) {
      const ImVec2 a = q[k], b = q[(k + 1) % 4];
      const double cr = (b.x - a.x) * (y - a.y) - (b.y - a.y) * (x - a.x);
      (cr >= 0 ? pos : neg)++;
    }
    return pos == 0 || neg == 0;
  }
  std::optional<std::string> LayerAt(double x, double y) override {
    std::optional<std::string> best;
    float order = -1e30f;
    for (const auto& [i, q] : p->quads) {
      const Obj& o = s->objs[i];
      auto ov = p->ov.find(o.name);
      if (ov != p->ov.end()) { auto v = ov->second.find("visible"); if (v != ov->second.end() && !wui::Truthy(v->second)) continue; }
      if (o.order >= order && LayerContains(o.name, x, y)) { order = o.order; best = o.name; }
    }
    return best;
  }
  std::vector<std::string> ScreenNames() override {
    std::vector<std::string> out;
    for (const auto& sc : s->screens) out.push_back(sc.name);
    return out;
  }
  void ShowScreen(const std::string& n, const std::string&, const std::optional<std::string>&) override {
    for (size_t i = 0; i < s->screens.size(); ++i) {
      if (s->screens[i].name != n) continue;
      if (static_cast<int>(i) == ScreenOf(*p)) return;
      p->ov.clear();   // las capas de la otra pantalla empiezan con su diseno
      Goto(*s, *p, static_cast<int>(i), true, now);
      return;
    }
  }
  void Close() override { ui3d::Close(*s, *p, now); }
  void SetFocus(const std::optional<std::string>& n) override {
    if (!n) return;
    const int i = Find(*n);
    const auto& F = s->screens[p->screen].focus;
    for (size_t b = 0; b < F.size(); ++b) if (F[b].obj == i) { p->focus = static_cast<int>(b); p->vm_focus = p->focus; return; }
  }
  void MoveFocus(const std::string& d) override {
    const bool was = p->locked;
    p->locked = false;
    Input(*s, *p, d == "up" ? kUp : d == "down" ? kDown : d == "left" ? kLeft : kRight, base, now);
    p->locked = was;
  }
  void LockFocus(bool l) override { p->locked = l; }
  wui::Value GameGet(const std::string& n) override {
    const std::string v = qb_bridge::PeekGlobal(n, base);
    const auto sp = v.rfind(" (");
    if (v.empty() || sp == std::string::npos) return {};
    const std::string kind = v.substr(sp + 2), val = v.substr(0, sp);
    try {
      if (kind == "entero)") return wui::V::I(std::stoll(val));
      if (kind == "real)") return wui::V::F(std::stod(val));
    } catch (...) { return {}; }
    if (kind == "texto)" && val.size() >= 2) return wui::V::S(val.substr(1, val.size() - 2));
    return {};
  }
  void GameSet(const std::string& n, const wui::Value& v) override {
    bool ok = false;
    if (v.t == wui::V::T::Float) ok = qb_bridge::WriteFloatGlobal(n, base, static_cast<float>(v.f));
    else if (v.t == wui::V::T::Int || v.t == wui::V::T::Bool) ok = qb_bridge::WriteIntGlobal(n, base, static_cast<int>(v.t == wui::V::T::Bool ? v.b : v.i));
    if (!ok) TRACE("[UI3D] " << s->name << ": game.set('" << n << "'): no existe esa variable del juego o es de otro tipo");
  }
  wui::Value GameCall(const std::string& script, const std::vector<std::pair<std::string, wui::Value>>& params) override {
    auto ident = [](const std::string& t) {
      if (t.empty() || std::isdigit(static_cast<unsigned char>(t[0]))) return false;
      return std::all_of(t.begin(), t.end(), [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; });
    };
    if (!ident(script)) return {};
    std::string line = script;
    for (const auto& [k, v] : params) {
      if (!ident(k)) continue;
      std::string val;
      if (v.t == wui::V::T::Str) {   // un nombre (checksum) si parece un identificador; si no, texto
        if (ident(v.s)) val = v.s;
        else { std::string t = v.s; std::erase(t, '"'); val = "\"" + t + "\""; }
      } else if (v.t == wui::V::T::Bool) val = v.b ? "1" : "0";
      else if (v.num()) val = wui::ToText(v);
      else continue;
      line += " " + k + " = " + val;
    }
    qb_bridge::ExecuteScriptQuiet(line);
    return {};
  }
  void PlaySound(const std::string& n, double) override { Sfx(n.c_str()); }
  void Log(const std::string& level, const std::string& m) override {
    TRACE("[UI3D] " << s->name << " (logica) " << level << ": " << m);
  }
};

// Mando de un jugador como lo ve la logica (nombres de tools/wui/wui/stdlib/pad)
void ReadPad(wui::Inputs& in) {
  auto* sys = g_runtime ? dynamic_cast<rex::input::InputSystem*>(g_runtime->input_system()) : nullptr;
  if (!sys) return;
  const auto users = sys->GetConnectedUsers();
  for (uint32_t u = 0; u < 4; ++u) {
    if (!users.test(u)) continue;
    rex::input::X_INPUT_STATE st{};
    sys->GetStateForUI(u, &st);
    const uint16_t b = st.gamepad.buttons;
    auto& set = in.buttons[static_cast<int>(u) + 1];
    using namespace rex::input;
    if (b & X_INPUT_GAMEPAD_A) { set.insert("green"); set.insert("choose"); }
    if (b & X_INPUT_GAMEPAD_B) { set.insert("red"); set.insert("cancel"); }
    if (b & X_INPUT_GAMEPAD_Y) set.insert("yellow");
    if (b & X_INPUT_GAMEPAD_X) set.insert("blue");
    if (b & X_INPUT_GAMEPAD_LEFT_SHOULDER) set.insert("orange");
    if (b & X_INPUT_GAMEPAD_DPAD_UP) { set.insert("strum_up"); set.insert("up"); }
    if (b & X_INPUT_GAMEPAD_DPAD_DOWN) { set.insert("strum_down"); set.insert("down"); }
    if (b & X_INPUT_GAMEPAD_DPAD_LEFT) set.insert("left");
    if (b & X_INPUT_GAMEPAD_DPAD_RIGHT) set.insert("right");
    if (b & X_INPUT_GAMEPAD_START) set.insert("start");
    if (b & X_INPUT_GAMEPAD_BACK) { set.insert("back"); set.insert("star"); }
    auto& ax = in.axes[static_cast<int>(u) + 1];
    ax["whammy"] = std::clamp((static_cast<int16_t>(st.gamepad.thumb_rx) + 32768) / 65535.0, 0.0, 1.0);
    ax["tilt"] = std::clamp(static_cast<int16_t>(st.gamepad.thumb_ry) / 32767.0, -1.0, 1.0);
    ax["lx"] = static_cast<int16_t>(st.gamepad.thumb_lx) / 32767.0;
    ax["ly"] = static_cast<int16_t>(st.gamepad.thumb_ly) / 32767.0;
  }
}

void StepLogic(Scene& s, Play& p, uint8_t* base, Clock::time_point now, double dt) {
  if (p.mode == Mode::kHidden) {
    if (p.vm) { p.vm.reset(); p.host.reset(); p.ov.clear(); p.locked = false; }
    return;
  }
  if (!s.logic) return;
  if (!p.host) p.host = std::make_shared<LogicHost>();
  p.host->s = &s; p.host->p = &p; p.host->base = base; p.host->now = now;
  try {
    if (!p.vm) {
      p.vm = std::make_shared<wui::Machine>(*s.logic, p.host.get(),
                                            static_cast<uint64_t>(now.time_since_epoch().count()));
      p.vm_focus = -2;
      std::string why;
      if (!p.vm->Ok(&why)) TRACE("[UI3D] " << s.name << ": " << why);
      p.vm->Start(s.screens[ScreenOf(p)].name);
    }
    // la pantalla y el foco nativos (botones, raton, scripts) -> la maquina
    const std::string& cur = s.screens[ScreenOf(p)].name;
    if (!p.vm->Closed() && (!p.vm->Screen() || *p.vm->Screen() != cur)) p.vm->Goto(cur, "auto");
    const int f = p.mode == Mode::kOutro ? -1 : p.focus;
    if (f != p.vm_focus) {
      p.vm_focus = f;
      const auto& F = s.screens[p.screen].focus;
      if (f >= 0 && f < static_cast<int>(F.size()) && F[f].obj >= 0 && F[f].obj < static_cast<int>(s.objs.size()))
        p.vm->NotifyFocus(s.objs[F[f].obj].name);
      else p.vm->NotifyFocus(std::nullopt);
    }
    for (int o : p.clicks) if (o >= 0 && o < static_cast<int>(s.objs.size())) p.vm->NotifyClick(s.objs[o].name);
    p.clicks.clear();
    wui::Inputs in;
    ReadPad(in);
    in.mouse_x = p.mouse_ed.x;
    in.mouse_y = p.mouse_ed.y;
    in.mouse_down = p.mouse_down;
    p.vm->Step(dt, in);
  } catch (const wui::Fault& e) {
    TRACE("[UI3D] " << s.name << " (logica): " << e.message);
  }
  if (p.vm && p.vm->Closed() && !p.closing && p.mode != Mode::kHidden) Close(s, p, now);
}

const Frame* PickFrame(Scene& s, Play& p, Clock::time_point now) {
  for (int guard = 0; guard < 4; ++guard) {
    ScreenDef& sd = s.screens[std::clamp(p.screen, 0, static_cast<int>(s.screens.size()) - 1)];
    auto clip = [&](const char* n) -> const std::vector<Frame>* {
      auto it = sd.clips.find(n);
      return it != sd.clips.end() && !it->second.empty() ? &it->second : nullptr;
    };
    const double el = std::chrono::duration<double>(now - p.start).count();
    const int f = static_cast<int>(el * s.fps);
    if (p.mode == Mode::kIntro) {
      const auto* c = clip("intro");
      if (c && f < static_cast<int>(c->size())) return &(*c)[f];
      p.mode = Mode::kLoop;
      p.start = now - std::chrono::duration_cast<Clock::duration>(
                          std::chrono::duration<double>(c ? std::max(0.0, el - c->size() / s.fps) : 0.0));
    }
    if (p.mode == Mode::kLoop) {
      const auto* c = clip("loop");
      if (!c) c = clip("intro");
      if (!c) return nullptr;
      const double el2 = std::max(0.0, std::chrono::duration<double>(now - p.start).count());
      return &(*c)[static_cast<size_t>(el2 * s.fps) % c->size()];
    }
    if (p.mode == Mode::kOutro) {
      const auto* c = clip("outro");
      if (c && f < static_cast<int>(c->size())) return &(*c)[f];
      if (p.pending >= 0) { const auto keep = p.history; EnterScreen(s, p, p.pending, now); p.history = keep; continue; }
      p.mode = Mode::kHidden;
      p.history.clear();
    }
    return nullptr;
  }
  return nullptr;
}

struct Proj {
  const Cam* c;
  float cx, cy, focal;
  // mundo -> pantalla; false si queda detras (o casi) de la camara
  bool operator()(const float* w, ImVec2& out, float& depth) const {
    const float d[3] = {w[0] - c->p[0], w[1] - c->p[1], w[2] - c->p[2]};
    depth = d[0] * c->F[0] + d[1] * c->F[1] + d[2] * c->F[2];
    if (depth < 1.f) return false;
    const float x = d[0] * c->R[0] + d[1] * c->R[1] + d[2] * c->R[2];
    const float y = d[0] * c->U[0] + d[1] * c->U[1] + d[2] * c->U[2];
    out = ImVec2(cx + focal * x / depth, cy - focal * y / depth);
    return true;
  }
};

ImU32 Col(const float* rgba) {
  auto b = [](float v) { return static_cast<int>(std::clamp(v, 0.f, 1.f) * 255.f + 0.5f); };
  return IM_COL32(b(rgba[0]), b(rgba[1]), b(rgba[2]), b(rgba[3]));
}

bool CondOk(const Obj& o) {
  if (o.cond.empty()) return true;
  const int v = o.cond_cur, x = o.cond_val;
  if (o.op == "!=") return v != x;
  if (o.op == ">") return v > x;
  if (o.op == "<") return v < x;
  if (o.op == ">=") return v >= x;
  if (o.op == "<=") return v <= x;
  return v == x;
}

bool InQuad(const std::array<ImVec2, 4>& q, ImVec2 m) {
  int pos = 0, neg = 0;
  for (int i = 0; i < 4; ++i) {
    const ImVec2 a = q[i], b = q[(i + 1) % 4];
    const float cr = (b.x - a.x) * (m.y - a.y) - (b.y - a.y) * (m.x - a.x);
    (cr >= 0 ? pos : neg)++;
  }
  return pos == 0 || neg == 0;
}

void DrawScene(ImDrawList* dl, const ImVec4& r, Scene& s, Play& p, const Frame& fr, rex::ui::ImmediateDrawer* drawer) {
  const float fov = std::clamp(fr.cam.fov, 1.f, 179.f) * 0.0174532925f;
  const Proj pj{&fr.cam, r.x + r.z / 2, r.y + r.w / 2, (r.w / 2) / std::tan(fov / 2)};
  const ScreenDef& sd = s.screens[p.screen];
  const float px_unit = r.z / 1280.f;   // 1 px del editor en pantalla
  p.hit.clear();
  p.quads.clear();
  // orden de pintado: por "orden" del editor y, a igual orden, lo lejano primero (como three.js en el editor)
  struct Item { const ObjFrame* o; float order, depth; };
  std::vector<Item> items;
  items.reserve(fr.objs.size());
  for (const auto& o : fr.objs) {
    if (o.obj >= static_cast<int>(s.objs.size())) continue;
    float cen[3] = {0, 0, 0};
    for (int k = 0; k < 4; ++k) for (int a = 0; a < 3; ++a) cen[a] += o.c[k * 3 + a] / 4;
    ImVec2 tmp;
    float dep = 0;
    pj(cen, tmp, dep);
    items.push_back({&o, s.objs[o.obj].order, dep});
  }
  std::stable_sort(items.begin(), items.end(), [](const Item& a, const Item& b) {
    return a.order != b.order ? a.order < b.order : a.depth > b.depth;
  });
  constexpr int N = 6;   // subdivisiones por lado: la perspectiva de un quad de ImGui es afin
  for (const auto& it : items) {
    const ObjFrame& o = *it.o;
    const Obj& ob = s.objs[o.obj];
    if (!CondOk(ob)) continue;
    float rgba[4] = {o.rgba[0], o.rgba[1], o.rgba[2], o.rgba[3]};
    if (ob.show_on >= 0) rgba[3] *= ob.show_on < static_cast<int>(p.ff.size()) ? p.ff[ob.show_on] : 0.f;
    // efecto de foco del boton (en pantalla, desde el centro del plano)
    float fs = 1, fdx = 0, fdy = 0;
    const Focus* fo = ob.focus >= 0 && ob.focus < static_cast<int>(sd.focus.size()) ? &sd.focus[ob.focus] : nullptr;
    if (fo) {
      const float f = ob.focus < static_cast<int>(p.ff.size()) ? p.ff[ob.focus] : 0.f;
      fs = 1 + (fo->scale - 1) * f;
      fdx = fo->dx * f * px_unit; fdy = fo->dy * f * px_unit;
      for (int k = 0; k < 3; ++k) rgba[k] *= 1 + (fo->col[k] - 1) * f;
      rgba[3] *= 1 + (fo->alpha - 1) * f;
    }
    // lo que cambio la logica (wui) en esta capa
    const std::map<std::string, wui::Value>* ov = nullptr;
    if (!ob.name.empty() && !p.ov.empty()) {
      auto oi = p.ov.find(ob.name);
      if (oi != p.ov.end()) ov = &oi->second;
    }
    auto num = [&](const char* k, double d) {
      if (!ov) return d;
      auto j = ov->find(k);
      return j != ov->end() && j->second.numb() ? j->second.d() : d;
    };
    auto text = [&](const char* k) -> const std::string* {
      if (!ov) return nullptr;
      auto j = ov->find(k);
      return j != ov->end() && j->second.t == wui::V::T::Str ? &j->second.s : nullptr;
    };
    auto color = [&](const char* k, float* out) {
      const std::string* c = text(k);
      int rgb[3];
      if (!c || !wui::ParseColor(wui::V::S(*c), rgb)) return false;
      for (int i = 0; i < 3; ++i) out[i] = rgb[i] / 255.f;
      return true;
    };
    if (ov) {
      auto v = ov->find("visible");
      if (v != ov->end() && !wui::Truthy(v->second)) continue;
    }
    const float lsx = static_cast<float>(num("scale", 1) * num("sx", 1)), lsy = static_cast<float>(num("scale", 1) * num("sy", 1));
    const float rot = static_cast<float>(num("rot", 0)) * 0.0174532925f;
    const float ldx = static_cast<float>(num("x", ob.x0) - ob.x0) * px_unit, ldy = static_cast<float>(num("y", ob.y0) - ob.y0) * px_unit;
    rgba[3] *= static_cast<float>(std::clamp(num("alpha", 1), 0.0, 1.0));
    {
      float tint[3] = {1, 1, 1};
      color("tint", tint);
      const float br = static_cast<float>(std::max(0.0, num("brightness", 1)));
      for (int k = 0; k < 3; ++k) rgba[k] *= tint[k] * br;
    }
    const float cs = std::cos(rot), sn = std::sin(rot);

    ImVec2 corner[4];
    bool all = true;
    float dep;
    for (int k = 0; k < 4 && all; ++k) all = pj(&o.c[k * 3], corner[k], dep);
    ImVec2 cen(0, 0);
    if (all) for (auto& c : corner) { cen.x += c.x / 4; cen.y += c.y / 4; }
    float half = 1;   // medio tamano en pantalla (para crecer en px)
    if (all) half = std::max(1.f, (std::hypot(corner[1].x - corner[0].x, corner[1].y - corner[0].y) +
                                   std::hypot(corner[3].x - corner[0].x, corner[3].y - corner[0].y)) / 4);
    const bool moved = all && (fs != 1 || fdx != 0 || fdy != 0 || ov);
    // pantalla -> pantalla: escala y giro de la logica, efecto de foco, desplazamientos (y crecer `grow` px)
    auto fx = [&](ImVec2 v, ImVec2 off = ImVec2(0, 0), float grow = 0) {
      if (!moved && grow == 0 && off.x == 0 && off.y == 0) return v;
      const float g = 1 + grow / half;
      float x = (v.x - cen.x) * lsx * g, y = (v.y - cen.y) * lsy * g;
      const float rx = x * cs - y * sn, ry = x * sn + y * cs;
      return ImVec2(cen.x + rx * fs + fdx + ldx + off.x, cen.y + ry * fs + fdy + ldy + off.y);
    };
    if (all) {
      const std::array<ImVec2, 4> q = {fx(corner[0]), fx(corner[1]), fx(corner[2]), fx(corner[3])};
      if (fo) p.hit.push_back({ob.focus, q});
      std::array<ImVec2, 4> ed;
      for (int k = 0; k < 4; ++k) ed[k] = ImVec2((q[k].x - r.x) / px_unit, (q[k].y - r.y) / px_unit);
      p.quads[o.obj] = ed;
    }
    if (rgba[3] <= 0.002f) continue;
    auto get_tex = [&](int ti) -> ImTextureID {
      if (ti < 0 || ti >= static_cast<int>(s.tex_files.size())) return ImTextureID{};
      if (!s.tex_tried[ti] && drawer) {
        s.tex_tried[ti] = true;
        std::ifstream f(s.dir / s.tex_files[ti], std::ios::binary);
        std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        int w = 0, h = 0;
        auto data = rex::ui::DecodeImageRGBA(bytes.data(), bytes.size(), w, h);
        if (!data.empty() && w > 0 && h > 0) {
          s.tex[ti] = drawer->CreateTexture(static_cast<uint32_t>(w), static_cast<uint32_t>(h),
                                            rex::ui::ImmediateTextureFilter::kLinear, false, data.data());
        }
        if (!s.tex[ti]) TRACE("[UI3D] no se pudo cargar la imagen " << s.tex_files[ti]);
      }
      return s.tex[ti] ? reinterpret_cast<ImTextureID>(s.tex[ti].get()) : ImTextureID{};
    };
    int ti = ob.tex;
    if (const std::string* img = text("image"); img && !img->empty()) {   // otra imagen de la interfaz
      for (size_t k = 0; k < s.tex_files.size(); ++k) if (s.tex_files[k] == *img) { ti = static_cast<int>(k); break; }
    }
    const std::string* new_text = ob.kind == "text" ? text("text") : nullptr;
    ImTextureID tex = get_tex(ti);
    if (ti >= 0 && !tex) continue;
    // relleno (barras): recorta el plano por un lado
    const float fill = static_cast<float>(std::clamp(num("fill", 1), 0.0, 1.0));
    float ua = 0, ub = 1, va = 0, vb = 1;
    if (fill < 1) {
      const std::string* dir = text("fill_dir");
      const std::string d = dir ? *dir : "right";
      if (d == "left") ua = 1 - fill;
      else if (d == "up") va = 1 - fill;
      else if (d == "down") vb = fill;
      else ub = fill;
      if (fill <= 0) continue;
    }
    // punto del plano en (u, v): bilineal de las esquinas (el plano es plano: es exacto)
    auto at = [&](float u, float v, float* w) {
      for (int a = 0; a < 3; ++a) {
        const float top = o.c[a] + (o.c[3 + a] - o.c[a]) * u;
        const float bot = o.c[9 + a] + (o.c[6 + a] - o.c[9 + a]) * u;
        w[a] = top + (bot - top) * v;
      }
    };
    // el plano (o su texto nuevo) con un desplazamiento, crecido `grow` px y con un color
    auto plane = [&](ImVec2 off, float grow, ImU32 col) {
      if (new_text) {
        ImFont* font = ImGui::GetFont();
        const float size = std::max(6.f, ob.h0 * 0.55f * px_unit * lsy * fs * (1 + grow / half));
        const ImVec2 ts = font->CalcTextSizeA(size, FLT_MAX, 0, new_text->c_str());
        const ImVec2 c = fx(cen, off);
        dl->AddText(font, size, ImVec2(c.x - ts.x / 2, c.y - ts.y / 2), col, new_text->c_str());
        return;
      }
      for (int j = 0; j < N; ++j) {
        for (int i = 0; i < N; ++i) {
          const float u0 = ua + (ub - ua) * i / N, u1 = ua + (ub - ua) * (i + 1) / N;
          const float v0 = va + (vb - va) * j / N, v1 = va + (vb - va) * (j + 1) / N;
          float w[4][3];
          at(u0, v0, w[0]); at(u1, v0, w[1]); at(u1, v1, w[2]); at(u0, v1, w[3]);
          ImVec2 q[4];
          bool ok = true;
          for (int k = 0; k < 4 && ok; ++k) ok = pj(w[k], q[k], dep);
          if (!ok) continue;
          for (auto& v : q) v = fx(v, off, grow);
          if (tex) dl->AddImageQuad(tex, q[0], q[1], q[2], q[3], ImVec2(u0, v0), ImVec2(u1, v0), ImVec2(u1, v1), ImVec2(u0, v1), col);
          else dl->AddQuadFilled(q[0], q[1], q[2], q[3], col);
        }
      }
    };
    auto tinted = [&](const float* c3, float a) {
      const float c[4] = {c3[0], c3[1], c3[2], std::clamp(a, 0.f, 1.f)};
      return Col(c);
    };
    static const float kDir[8][2] = {{1, 0}, {0.7071f, 0.7071f}, {0, 1}, {-0.7071f, 0.7071f},
                                     {-1, 0}, {-0.7071f, -0.7071f}, {0, -1}, {0.7071f, -0.7071f}};
    // sombra: copias desplazadas y repartidas en un circulo del tamano del desenfoque
    if (const float sh = static_cast<float>(num("shadow", 0)); ov && sh > 0) {
      float c[3] = {0, 0, 0};
      color("shadow_color", c);
      const ImVec2 base(static_cast<float>(num("shadow_x", 4)) * px_unit, static_cast<float>(num("shadow_y", 4)) * px_unit);
      const float a = static_cast<float>(num("shadow_alpha", 0.6)) * rgba[3];
      const float rad = sh * px_unit * 0.5f;
      plane(base, 0, tinted(c, a * 0.4f));
      for (const auto& d : kDir) plane(ImVec2(base.x + d[0] * rad, base.y + d[1] * rad), 0, tinted(c, a * 0.12f));
    }
    // resplandor: copias crecidas del color del resplandor, cada vez mas tenues
    if (const float gl = static_cast<float>(num("glow", 0)); ov && gl > 0) {
      float c[3] = {1, 1, 1};
      color("glow_color", c);
      const float a = static_cast<float>(num("glow_alpha", 1)) * rgba[3];
      constexpr int kRings = 4;
      for (int k = kRings; k >= 1; --k) plane(ImVec2(0, 0), gl * px_unit * k / kRings, tinted(c, a * 0.45f / k));
    }
    // contorno: 8 copias solidas desplazadas el grosor
    if (const float ol = static_cast<float>(num("outline", 0)); ov && ol > 0) {
      float c[3] = {1, 1, 1};
      color("outline_color", c);
      for (const auto& d : kDir) plane(ImVec2(d[0] * ol * px_unit, d[1] * ol * px_unit), 0, tinted(c, rgba[3]));
    }
    // la capa; con desenfoque, repartida en copias tenues alrededor
    const ImU32 col = Col(rgba);
    if (const float bl = static_cast<float>(num("blur", 0)); ov && bl > 0.25f) {
      const float rad = bl * px_unit * 0.5f;
      plane(ImVec2(0, 0), 0, tinted(rgba, rgba[3] * 0.3f));
      for (const auto& d : kDir) plane(ImVec2(d[0] * rad, d[1] * rad), 0, tinted(rgba, rgba[3] * 0.16f));
    } else {
      plane(ImVec2(0, 0), 0, col);
    }
  }
}

}  // namespace

void OnLogicFrame(uint8_t* base) {
  static Clock::time_point last_scan{};
  const auto now = Clock::now();
  if (now - last_scan > std::chrono::seconds(2)) {
    last_scan = now;
    Rescan();
  }
  if (!base || !qb_bridge::ScriptsLoaded()) return;
  std::vector<std::string> names;
  {
    std::lock_guard<std::mutex> lk(g_mu);
    for (const auto& [k, s] : g_scenes) names.push_back(s->name);
  }
  for (const auto& n : names) {
    const std::string g = "re_ui3d_" + n;
    int seq = 0, cmd = 0, arg = 0, key = 0, kseq = 0;
    if (!qb_bridge::ReadIntGlobal(g + "_seq", base, &seq)) continue;
    qb_bridge::ReadIntGlobal(g + "_cmd", base, &cmd);
    qb_bridge::ReadIntGlobal(g + "_arg", base, &arg);
    const bool has_keys = qb_bridge::ReadIntGlobal(g + "_kseq", base, &kseq);
    qb_bridge::ReadIntGlobal(g + "_key", base, &key);
    std::lock_guard<std::mutex> lk(g_mu);
    Scene* s = FindScene(n);
    if (!s) continue;
    Play& p = g_play[n];
    if (!p.seen) { p.seen = true; p.last_seq = seq; p.last_kseq = kseq; continue; }
    if (seq != p.last_seq) {
      p.last_seq = seq;
      if (cmd == 1) { p.history.clear(); EnterScreen(*s, p, s->start, now); }
      else if (cmd == 2 && p.mode != Mode::kHidden && !p.closing) { p.closing = true; StartOutro(p, -1, now); }
      else if (cmd == 3) { p.mode = Mode::kHidden; p.history.clear(); }
      else if (cmd == 4) {
        if (p.mode == Mode::kHidden) { p.history.clear(); EnterScreen(*s, p, arg, now); }
        else Goto(*s, p, arg, true, now);
      }
      TRACE("[UI3D] " << n << ": orden " << cmd << (cmd == 4 ? " pantalla " + std::to_string(arg) : ""));
    }
    if (has_keys && kseq != p.last_kseq) {
      p.last_kseq = kseq;
      Input(*s, p, key, base, now);
    }
    for (int ev : p.mouse_events) {
      const ScreenDef& sd = s->screens[p.screen];
      const int b = ev % 100;
      if (b >= static_cast<int>(sd.focus.size()) || (p.mode != Mode::kIntro && p.mode != Mode::kLoop)) continue;
      if (ev >= 200 && p.locked) continue;
      if (ev >= 200) { p.focus = b; p.clicks.push_back(sd.focus[b].obj); Input(*s, p, kChoose, base, now); }
      else if (b != p.focus) { p.focus = b; if (sd.focus[b].sound) Sfx("scrolldown"); }
    }
    p.mouse_events.clear();
    // variables de las condiciones (solo con la interfaz abierta)
    if (p.mode != Mode::kHidden) {
      for (auto& o : s->objs) {
        if (o.cond.empty()) continue;
        int v = 0;
        if (qb_bridge::ReadIntGlobal(o.cond, base, &v)) o.cond_cur = v;
      }
    }
    const double dt = p.last_logic.time_since_epoch().count() ? std::chrono::duration<double>(now - p.last_logic).count() : 0.0;
    p.last_logic = now;
    StepLogic(*s, p, base, now, std::clamp(dt, 0.0, 0.1));
  }
}

void SetRuntime(rex::Runtime* runtime) { g_runtime = runtime; }

Ui3dOverlay::Ui3dOverlay(rex::ui::ImGuiDrawer* drawer) : ImGuiDialog(drawer) {}

void Ui3dOverlay::OnDraw(ImGuiIO& io) {
  std::lock_guard<std::mutex> lk(g_mu);
  if (g_scenes.empty()) return;
  const auto now = Clock::now();
  float w = io.DisplaySize.x, h = io.DisplaySize.y, gw = w, gh = h;
  if (w <= 0 || h <= 0) return;
  if (w / h > 16.f / 9.f) gw = h * 16.f / 9.f; else gh = w * 9.f / 16.f;
  const ImVec4 r((w - gw) / 2, (h - gh) / 2, gw, gh);
  ImDrawList* dl = ImGui::GetBackgroundDrawList();
  auto* drawer = imgui_drawer() ? imgui_drawer()->immediate_drawer() : nullptr;
  for (auto& [key, s] : g_scenes) {
    auto it = g_play.find(s->name);
    if (it == g_play.end() || it->second.mode == Mode::kHidden) continue;
    Play& p = it->second;
    const Frame* fr = PickFrame(*s, p, now);
    if (!fr) continue;
    // foco animado (unos 120 ms)
    const ScreenDef& sd = s->screens[p.screen];
    if (p.ff.size() != sd.focus.size()) p.ff.assign(sd.focus.size(), 0.f);
    const float dt = p.last_anim.time_since_epoch().count() ? std::min(0.1f, std::chrono::duration<float>(now - p.last_anim).count()) : 0.f;
    p.last_anim = now;
    for (size_t i = 0; i < p.ff.size(); ++i) {
      const float target = static_cast<int>(i) == p.focus && p.mode != Mode::kOutro ? 1.f : 0.f;
      p.ff[i] += (target - p.ff[i]) * std::min(1.f, dt * 14.f);
    }
    DrawScene(dl, r, *s, p, *fr, drawer);
    // raton para la logica, en px del editor
    if (io.MousePos.x > -1000 && !io.WantCaptureMouse) {
      p.mouse_ed = ImVec2((io.MousePos.x - r.x) * 1280.f / r.z, (io.MousePos.y - r.y) * 1280.f / r.z);
      p.mouse_down = io.MouseDown[0];
      // clic en una capa que no es boton: on_click de la logica (los botones van por su accion)
      if (p.vm && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        bool on_button = false;
        for (const auto& h2 : p.hit) on_button = on_button || InQuad(h2.second, io.MousePos);
        int top = -1;
        for (const auto& [i, q] : p.quads)
          if (s->objs[i].focus < 0 && InQuad(q, p.mouse_ed) && (top < 0 || s->objs[i].order >= s->objs[top].order)) top = i;
        if (!on_button && top >= 0) p.clicks.push_back(top);
      }
    }
    // raton: pasar por encima enfoca, clic elige (solo si ninguna ventana de ImGui lo esta usando)
    if (!sd.focus.empty() && !io.WantCaptureMouse && io.MousePos.x > -1000) {
      const bool moved = std::abs(io.MousePos.x - p.last_mouse.x) + std::abs(io.MousePos.y - p.last_mouse.y) > 0.5f;
      p.last_mouse = io.MousePos;
      int over = -1;
      for (auto h2 = p.hit.rbegin(); h2 != p.hit.rend() && over < 0; ++h2)
        if (InQuad(h2->second, io.MousePos)) over = h2->first;
      if (over >= 0 && moved && over != p.focus) p.mouse_events.push_back(100 + over);
      if (over >= 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) p.mouse_events.push_back(200 + over);
    }
  }
}

}  // namespace re_wor::de::ui3d
