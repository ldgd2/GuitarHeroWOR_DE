// EDFX: efectos de imagen, objetos 3D y textos del editor de canciones dibujados encima del juego.
// Ver edfx.hpp para el mecanismo. Formato del .edfx (texto, una orden por linea, numeros en decimal):
//   EDFX 1
//   song <nombre>
//   sync <qbkey hex> <ms>                       evento de sincronia (su clave y el ms de la cancion)
//   fx <tipo> <ms> <duracion ms> <intensidad>   letterbox vignette flash fadein fadeout bw sepia saturate
//                                               contrast glow chroma blur shake strobe redflash dream
//                                               night cold warm blackout scanlines grain vhs iris
//                                               spotlight heartbeat confetti sparks snow rain lightning
//                                               rainbow smoke lasers
//   shot <ms> <duracion ms>                     toma de camara del editor (la camara del juego es esta)
//   k <ms> px py pz qx qy qz qw hfov            fotograma de la toma anterior (mundo; mira a -Z, FOV horizontal)
//   prop <ms> <duracion ms>                     objeto 3D (solo se ve durante las tomas del editor)
//   tri ax ay az bx by bz cx cy cz rrggbbaa     triangulo del objeto anterior (mundo)
//   text <ms> <duracion ms> x y z alto rrggbbaa <texto...>
#include "de/edfx.hpp"

#include <imgui.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <cctype>
#include <cfloat>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/crash_handler.hpp"
#include "de/mod_manager.hpp"

namespace re_wor::de::edfx {

namespace {

using Clock = std::chrono::steady_clock;

struct Key { double t; float p[3]; float q[4]; float hfov; };
struct Shot { double t, dur; std::vector<Key> keys; };
struct Fx {
  std::string type;
  double t, dur, amount;
  std::vector<std::pair<std::string, float>> params;
  Shot strips[4];   // pantalla dividida: camara propia de cada franja (sin claves = camara del juego congelada)
};
struct Tri { float v[9]; uint32_t rgba; };
struct Prop { double t, dur; std::vector<Tri> tris; };
struct Text { double t, dur; float p[3]; float h; uint32_t rgba; std::string s; };
// Objeto NATIVO: lo crea el propio motor (luz, particulas, modelo del juego). Ver native_fx.cpp.
struct Native { std::string kind; double t, dur; float p[3], q[4], rgb[3], v[3]; std::string asset; };
struct Song {
  std::string name;
  std::vector<Fx> fx;
  std::vector<Shot> shots;
  std::vector<Prop> props;
  std::vector<Text> texts;
  std::vector<Native> natives;
};
struct SyncRef { int song; double t; };

// Indice inmutable: se sustituye entero al releer (el hook de simbolos lo consulta sin bloqueo).
struct Index {
  std::vector<Song> songs;
  std::unordered_map<uint32_t, SyncRef> sync;
  std::unordered_map<std::string, int> by_name;   // nombre de la cancion (minusculas) -> indice
  uint64_t bloom[1024] = {};   // filtro rapido: la busqueda de simbolos se llama muchisimo
  std::vector<std::pair<std::filesystem::path, std::filesystem::file_time_type>> files;
};
std::atomic<const Index*> g_index{nullptr};
std::mutex g_scan_mutex;

// Reloj de la cancion: ultimo evento recibido
std::mutex g_clock_mutex;
int g_song = -1;
double g_anchor_ms = 0;
Clock::time_point g_anchor_at{};
constexpr double kHoldMs = 2600;   // sin eventos durante este tiempo (pausa, fin) -> se apaga la capa

// Reloj principal: el de la cancion del propio motor (double en segundos en 0x82993A58)
constexpr uint32_t kSongTimeAddr = 0x82993A58;
std::string g_loaded_song;               // ultima "songs/<nombre>_song.pak.xen" abierta (minusculas)
double g_game_ms = 0;                    // ultimo valor leido
Clock::time_point g_game_moved_at{};     // cuando cambio por ultima vez (si se queda quieto: pausa/menu)
constexpr double kStillMs = 400;

uint32_t ParseHex(const std::string& s) { return static_cast<uint32_t>(std::stoul(s, nullptr, 16)); }

bool Load(const std::filesystem::path& file, Song& out, std::vector<std::pair<uint32_t, double>>& sync) {
  std::ifstream in(file);
  if (!in) return false;
  std::string line;
  if (!std::getline(in, line) || line.rfind("EDFX", 0) != 0) return false;
  Shot* shot = nullptr;
  Prop* prop = nullptr;
  out.fx.reserve(4096);
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    std::istringstream ss(line);
    std::string cmd;
    if (!(ss >> cmd)) continue;
    try {
      if (cmd == "song") {
        ss >> out.name;
      } else if (cmd == "sync") {
        std::string h; double t; ss >> h >> t; sync.emplace_back(ParseHex(h), t);
      } else if (cmd == "fx") {
        Fx f; ss >> f.type >> f.t >> f.dur >> f.amount;
        std::string kv;   // parametros opcionales: nombre=valor
        while (ss >> kv) {
          const auto eq = kv.find('=');
          if (eq != std::string::npos && eq > 0) f.params.emplace_back(kv.substr(0, eq), std::stof(kv.substr(eq + 1)));
        }
        out.fx.push_back(f);
        shot = nullptr;
      } else if (cmd == "scam" && !out.fx.empty()) {
        // scam <franja> <ms>: camara propia de una franja del ultimo "fx split"; siguen sus claves "k"
        int i = 0; double t0 = 0; ss >> i >> t0;
        if (i >= 0 && i < 4) { shot = &out.fx.back().strips[i]; shot->t = t0; shot->dur = 0; shot->keys.clear(); }
      } else if (cmd == "shot") {
        Shot s; ss >> s.t >> s.dur; out.shots.push_back(std::move(s)); shot = &out.shots.back();
      } else if (cmd == "k" && shot) {
        Key k; ss >> k.t >> k.p[0] >> k.p[1] >> k.p[2] >> k.q[0] >> k.q[1] >> k.q[2] >> k.q[3] >> k.hfov;
        shot->keys.push_back(k);
      } else if (cmd == "prop") {
        Prop p; ss >> p.t >> p.dur; out.props.push_back(std::move(p)); prop = &out.props.back();
      } else if (cmd == "tri" && prop) {
        Tri tr; std::string c;
        for (float& v : tr.v) ss >> v;
        ss >> c; tr.rgba = ParseHex(c); prop->tris.push_back(tr);
      } else if (cmd == "native") {
        // native <tipo> <t> <dur> x y z qx qy qz qw r g b v0 v1 v2 <recurso>
        Native n;
        ss >> n.kind >> n.t >> n.dur;
        for (float& v : n.p) ss >> v;
        for (float& v : n.q) ss >> v;
        for (float& v : n.rgb) ss >> v;
        for (float& v : n.v) ss >> v;
        std::getline(ss, n.asset);
        while (!n.asset.empty() && n.asset.front() == ' ') n.asset.erase(0, 1);
        if (!ss.fail() || !n.asset.empty()) out.natives.push_back(std::move(n));
      } else if (cmd == "text") {
        Text tx; std::string c;
        ss >> tx.t >> tx.dur >> tx.p[0] >> tx.p[1] >> tx.p[2] >> tx.h >> c;
        tx.rgba = ParseHex(c);
        std::getline(ss, tx.s);
        if (!tx.s.empty() && tx.s.front() == ' ') tx.s.erase(0, 1);
        out.texts.push_back(std::move(tx));
      }
    } catch (...) {
      // linea mal formada: se ignora
    }
  }
  return true;
}

bool FilesChanged(const Index* idx, const std::vector<std::filesystem::path>& now) {
  if (!idx) return true;
  if (idx->files.size() != now.size()) return true;
  for (size_t i = 0; i < now.size(); ++i) {
    std::error_code ec;
    if (idx->files[i].first != now[i] || idx->files[i].second != std::filesystem::last_write_time(now[i], ec))
      return true;
  }
  return false;
}

// ImGui: ABGR
ImU32 Col(uint32_t rgba, float k = 1.f) {
  auto r = static_cast<int>(((rgba >> 24) & 0xFF) * k), g = static_cast<int>(((rgba >> 16) & 0xFF) * k),
       b = static_cast<int>(((rgba >> 8) & 0xFF) * k);
  return IM_COL32(std::min(r, 255), std::min(g, 255), std::min(b, 255), rgba & 0xFF);
}
ImU32 Rgba(int r, int g, int b, float a) { return IM_COL32(r, g, b, static_cast<int>(std::clamp(a, 0.f, 1.f) * 255)); }
float Smooth(float k) { k = std::clamp(k, 0.f, 1.f); return k * k * (3 - 2 * k); }

void QRot(const float q[4], const float v[3], float out[3]) {   // q * v * q^-1
  const float x = q[0], y = q[1], z = q[2], w = q[3];
  const float tx = 2 * (y * v[2] - z * v[1]), ty = 2 * (z * v[0] - x * v[2]), tz = 2 * (x * v[1] - y * v[0]);
  out[0] = v[0] + w * tx + (y * tz - z * ty);
  out[1] = v[1] + w * ty + (z * tx - x * tz);
  out[2] = v[2] + w * tz + (x * ty - y * tx);
}

bool CameraAt(const Shot& s, double t, Key& out) {
  if (s.keys.empty()) return false;
  const auto& k = s.keys;
  if (t <= k.front().t) { out = k.front(); return true; }
  if (t >= k.back().t) { out = k.back(); return true; }
  size_t i = 1;
  while (i < k.size() && k[i].t < t) ++i;
  const Key &a = k[i - 1], &b = k[i];
  const float f = static_cast<float>((t - a.t) / std::max(1e-6, b.t - a.t));
  for (int j = 0; j < 3; ++j) out.p[j] = a.p[j] + (b.p[j] - a.p[j]) * f;
  float d = 0;
  for (int j = 0; j < 4; ++j) d += a.q[j] * b.q[j];
  const float sgn = d < 0 ? -1.f : 1.f;
  float n = 0;
  for (int j = 0; j < 4; ++j) { out.q[j] = a.q[j] + (sgn * b.q[j] - a.q[j]) * f; n += out.q[j] * out.q[j]; }
  n = std::sqrt(n);
  for (float& v : out.q) v /= (n > 0 ? n : 1);
  out.hfov = a.hfov + (b.hfov - a.hfov) * f;
  return true;
}

// Rectangulo de la imagen del juego (16:9 centrado en la ventana)
ImVec4 GameRect(const ImGuiIO& io) {
  float w = io.DisplaySize.x, h = io.DisplaySize.y, gw = w, gh = h;
  if (w / h > 16.f / 9.f) gw = h * 16.f / 9.f; else gh = w * 9.f / 16.f;
  return ImVec4((w - gw) / 2, (h - gh) / 2, gw, gh);
}

// Camara como base ortonormal: R derecha, U arriba, F hacia donde mira
struct Projector {
  float p[3], R[3], U[3], F[3];
  float f = 1, cx = 0, cy = 0;
  // mundo -> pantalla; false si queda detras de la camara
  bool operator()(const float* w, ImVec2& out, float& depth) const {
    const float d[3] = {w[0] - p[0], w[1] - p[1], w[2] - p[2]};
    depth = d[0] * F[0] + d[1] * F[1] + d[2] * F[2];
    if (depth < 0.05f) return false;
    const float x = d[0] * R[0] + d[1] * R[1] + d[2] * R[2], y = d[0] * U[0] + d[1] * U[1] + d[2] * U[2];
    out = ImVec2(cx + f * x / depth, cy - f * y / depth);
    return true;
  }
};
// Fotograma del editor (cuaternion, mira a -Z) -> base
void BasisFromKey(const Key& k, float R[3], float U[3], float F[3]) {
  const float x[3] = {1, 0, 0}, y[3] = {0, 1, 0}, z[3] = {0, 0, -1};
  QRot(k.q, x, R); QRot(k.q, y, U); QRot(k.q, z, F);
}

// ---- Camara del juego ----
// Lista de viewports en 0x82860320 (cabeza; primer nodo en +4). Nodo: +4 siguiente, +16 viewport, +864 id.
// Viewport: +104 flags (0x10 = activo), +116 camara. Camara: +112 pos, +128 fila "lado", +144 up, +160 at,
// +176 (a veces copia de pos), +192 hfov. Sacado de los CFuncs GetViewportCameraPos/Orient (8251C400/8251C4E8)
// y Get/SetHFOV (8245BFF0/8245C048).
struct GameCam {
  bool ok = false;
  uint32_t addr = 0;
  float p[3], side[3], U[3], F[3], hfov_raw = 0;
  bool side_is_right = true;   // la fila "lado" es la derecha (= F x U) o la izquierda
  bool rad = true;             // el hfov del motor esta en radianes
  float HfovDeg() const { return rad ? hfov_raw * 57.2957795f : hfov_raw; }
};
GameCam g_cam;   // protegido por g_clock_mutex
std::atomic<bool> g_native_letterbox{false};   // native_fx recorto el viewport del escenario (franjas de cine)

// ---- Zona sagrada: el traste (notas) no se tapa NUNCA ----
// Trapecio del traste de 1 jugador en coordenadas normalizadas de la imagen del juego (generoso).
// Todo lo que dibuja esta capa se recorta fuera de el. Los filtros de imagen ya no van aqui: los aplica el
// propio motor (ScreenFX nativo, native_fx.cpp) solo al escenario, por debajo del traste y del HUD.
struct Highway {
  ImVec2 q[4];   // arriba-izq, arriba-der, abajo-der, abajo-izq (pixeles)
  void Set(const ImVec4& r) {
    const float yt = 0.26f, xt0 = 0.39f, xt1 = 0.61f, xb0 = 0.21f, xb1 = 0.79f;
    q[0] = ImVec2(r.x + xt0 * r.z, r.y + yt * r.w); q[1] = ImVec2(r.x + xt1 * r.z, r.y + yt * r.w);
    q[2] = ImVec2(r.x + xb1 * r.z, r.y + r.w + 2); q[3] = ImVec2(r.x + xb0 * r.z, r.y + r.w + 2);
  }
  static float Cross(ImVec2 a, ImVec2 b, ImVec2 p) { return (b.x - a.x) * (p.y - a.y) - (b.y - a.y) * (p.x - a.x); }
  bool Inside(ImVec2 p, float pad = 0) const {
    for (int i = 0; i < 4; ++i) {
      const ImVec2 a = q[i], b = q[(i + 1) & 3];
      const float len = std::hypot(b.x - a.x, b.y - a.y);
      if (Cross(a, b, p) < -pad * len) return false;
    }
    return true;
  }
  // tramo [x0,x1] del traste a la altura y (false si no lo corta)
  bool Span(float y, float& x0, float& x1) const {
    if (y < q[0].y || y > q[3].y) return false;
    const float k = (y - q[0].y) / std::max(1.f, q[3].y - q[0].y);
    x0 = q[0].x + (q[3].x - q[0].x) * k; x1 = q[1].x + (q[2].x - q[1].x) * k;
    return true;
  }
  // parte del segmento a-b DENTRO del trapecio: [t0,t1] (Cyrus-Beck); false si no entra
  bool ClipIn(ImVec2 a, ImVec2 b, float& t0, float& t1) const {
    t0 = 0; t1 = 1;
    const ImVec2 d(b.x - a.x, b.y - a.y);
    for (int i = 0; i < 4; ++i) {
      const ImVec2 e0 = q[i], e1 = q[(i + 1) & 3];
      const ImVec2 n(-(e1.y - e0.y), e1.x - e0.x);   // normal hacia dentro (orden horario en pantalla)
      const float num = n.x * (a.x - e0.x) + n.y * (a.y - e0.y), den = n.x * d.x + n.y * d.y;
      if (std::fabs(den) < 1e-6f) { if (num < 0) return false; continue; }
      const float t = -num / den;
      if (den > 0) t0 = std::max(t0, t); else t1 = std::min(t1, t);
      if (t0 > t1) return false;
    }
    return true;
  }
  // un triangulo toca el trapecio (separacion de ejes)
  bool Touches(const ImVec2* tri) const {
    auto sep = [&](const ImVec2* A, int na, const ImVec2* B, int nb) {
      for (int i = 0; i < na; ++i) {
        const ImVec2 e0 = A[i], e1 = A[(i + 1) % na], n(-(e1.y - e0.y), e1.x - e0.x);
        float amin = 1e30f, amax = -1e30f, bmin = 1e30f, bmax = -1e30f;
        for (int j = 0; j < na; ++j) { const float v = n.x * A[j].x + n.y * A[j].y; amin = std::min(amin, v); amax = std::max(amax, v); }
        for (int j = 0; j < nb; ++j) { const float v = n.x * B[j].x + n.y * B[j].y; bmin = std::min(bmin, v); bmax = std::max(bmax, v); }
        if (amax < bmin || bmax < amin) return true;
      }
      return false;
    };
    return !sep(tri, 3, q, 4) && !sep(q, 4, tri, 3);
  }
};
Highway g_hw;

// Lista de dibujo que no pisa el traste (misma interfaz que ImDrawList para lo que usan los efectos)
struct Masked {
  ImDrawList* dl;
  void AddLine(ImVec2 a, ImVec2 b, ImU32 c, float th) {
    float t0, t1;
    if (!g_hw.ClipIn(a, b, t0, t1)) { dl->AddLine(a, b, c, th); return; }
    auto at = [&](float t) { return ImVec2(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t); };
    if (t0 > 1e-4f) dl->AddLine(a, at(t0), c, th);
    if (t1 < 1 - 1e-4f) dl->AddLine(at(t1), b, c, th);
  }
  void AddPolyline(const ImVec2* pts, int n, ImU32 c, int, float th) { for (int i = 1; i < n; ++i) AddLine(pts[i - 1], pts[i], c, th); }
  void AddCircleFilled(ImVec2 p, float rad, ImU32 c, int seg) { if (!g_hw.Inside(p, rad)) dl->AddCircleFilled(p, rad, c, seg); }
  void AddQuadFilled(ImVec2 a, ImVec2 b, ImVec2 c2, ImVec2 d, ImU32 c) {
    const ImVec2 m((a.x + c2.x) / 2, (a.y + c2.y) / 2);
    if (!g_hw.Inside(m, std::hypot(a.x - c2.x, a.y - c2.y))) dl->AddQuadFilled(a, b, c2, d, c);
  }
  // rectangulos: se parten por filas alrededor del traste
  void AddRectFilled(ImVec2 a, ImVec2 b, ImU32 c) {
    const float step = std::max(2.f, (b.y - a.y) / 24.f);
    for (float y = a.y; y < b.y; y += step) {
      const float y2 = std::min(b.y, y + step);
      float x0, x1, u0, u1;
      const bool s0 = g_hw.Span(y, x0, x1), s1 = g_hw.Span(y2, u0, u1);
      if (!s0 && !s1) { dl->AddRectFilled(ImVec2(a.x, y), ImVec2(b.x, y2), c); continue; }
      const float lo = std::min(s0 ? x0 : u0, s1 ? u0 : x0), hi = std::max(s0 ? x1 : u1, s1 ? u1 : x1);
      if (a.x < lo) dl->AddRectFilled(ImVec2(a.x, y), ImVec2(std::min(b.x, lo), y2), c);
      if (b.x > hi) dl->AddRectFilled(ImVec2(std::max(a.x, hi), y), ImVec2(b.x, y2), c);
    }
  }
  void AddRectFilledMultiColor(ImVec2 a, ImVec2 b, ImU32 c0, ImU32 c1, ImU32 c2, ImU32 c3) {
    if (b.x <= g_hw.q[3].x || a.x >= g_hw.q[2].x) dl->AddRectFilledMultiColor(a, b, c0, c1, c2, c3);
  }
};

// ---- Efectos dibujados: copia exacta de EditorSong/web/src/viewport/fx2d.js (si cambias uno, cambia el otro) ----
struct Act { float k = 0, el = 0, left = 0; };
using ActMap = std::unordered_map<std::string, Act>;
float Fr(float x) { return x - std::floor(x); }
float Hash(float i, float s) { return Fr(std::sin(i * 12.9898f + s * 78.233f) * 43758.5453f); }
const int kPal[6][3] = {{255, 60, 80}, {255, 210, 40}, {60, 220, 120}, {60, 160, 255}, {200, 90, 255}, {255, 140, 30}};
ImU32 PalCol(int i, float a) { return Rgba(kPal[i][0], kPal[i][1], kPal[i][2], a); }

void Scan(Masked* dl, const ImVec4& r, float a) {
  const float st = std::max(2.f, r.w / 240);
  for (float y = 0; y < r.w; y += st) dl->AddRectFilled(ImVec2(r.x, r.y + y), ImVec2(r.x + r.z, r.y + y + std::max(1.f, st / 3)), Rgba(0, 0, 0, 0.22f * a));
}
void Grain(Masked* dl, const ImVec4& r, float T, float a, int n = 500, float y0 = 0, float y1 = 1) {
  const float f = std::floor(T * 24), sz = std::max(1.f, r.w / 480);
  for (int i = 0; i < n; ++i) {
    const float x = r.x + Hash(i, f) * r.z, y = r.y + Hash(i, f + 0.5f) * r.w;
    if (y < r.y + y0 * r.w || y > r.y + y1 * r.w) continue;
    const int v = Hash(i, f + 0.7f) > 0.5f ? 255 : 0;
    dl->AddRectFilled(ImVec2(x, y), ImVec2(x + sz, y + sz), Rgba(v, v, v, 0.35f * a));
  }
}
void Hsv(float h, int out[3]) {
  for (int j = 0; j < 3; ++j) {
    const float n = j == 0 ? 5.f : j == 1 ? 3.f : 1.f;
    const float x = std::fmod(n + h * 6, 6.f);
    out[j] = static_cast<int>(255 * (1 - std::clamp(std::min(x, 4 - x), 0.f, 1.f)));
  }
}
void DrawFx2D(Masked* dl, const ImVec4& r, const ActMap& on, float T) {
  auto get = [&](const char* n) -> const Act* { auto it = on.find(n); return it == on.end() ? nullptr : &it->second; };
  const float W = r.z, H = r.w, D = std::hypot(W, H);
  auto P = [&](float x, float y) { return ImVec2(r.x + x, r.y + y); };
  if (const Act* f = get("smoke")) {
    for (int i = 0; i < 14; ++i) {
      const float life = Fr(Hash(i, 3) + f->el * 0.05f), x = Fr(Hash(i, 1) + f->el * 0.02f * (Hash(i, 2) - 0.5f)) * W;
      const float y = H - life * H * 0.6f, rad = H * (0.15f + 0.15f * Hash(i, 4));
      for (float k : {1.f, 0.7f, 0.4f}) dl->AddCircleFilled(P(x, y), rad * k, Rgba(200, 200, 205, 0.055f * f->k * (1 - life)), 32);
    }
  }
  if (const Act* f = get("rain")) {
    const ImU32 c = Rgba(180, 200, 255, 0.35f * f->k);
    for (int i = 0; i < 200; ++i) {
      const float L = H * 0.05f, y = Fr(Hash(i, 3) + f->el * (1.6f + 0.8f * Hash(i, 2))) * (H + L) - L;
      const float x = Hash(i, 1) * (W + H * 0.3f) - y * 0.25f;
      dl->AddLine(P(x, y), P(x - L * 0.25f, y + L), c, std::max(1.f, H / 720));
    }
  }
  if (const Act* f = get("snow")) {
    for (int i = 0; i < 160; ++i) {
      const float y = Fr(Hash(i, 3) + f->el * (0.08f + 0.1f * Hash(i, 2))) * H * 1.05f;
      const float x = Hash(i, 1) * W + std::sin(f->el * 1.3f + i) * W * 0.015f;
      dl->AddCircleFilled(P(x, y), H * (0.003f + 0.004f * Hash(i, 4)), Rgba(255, 255, 255, 0.85f * f->k), 8);
    }
  }
  if (const Act* f = get("confetti")) {
    const float sz = H * 0.012f;
    for (int i = 0; i < 140; ++i) {
      const float y = Fr(Hash(i, 3) + f->el * (0.15f + 0.25f * Hash(i, 2))) * H * 1.1f - H * 0.05f;
      const float x = Hash(i, 1) * W + std::sin(f->el * 2 + i) * W * 0.02f, an = f->el * 3 + i;
      const float w = std::max(1.f, sz * std::fabs(std::cos(an * 0.7f)) / 2);
      const float c = std::cos(an), s = std::sin(an), hx = sz / 2, hy = w / 2;
      auto Q = [&](float a, float b) { return P(x + a * c - b * s, y + a * s + b * c); };
      dl->AddQuadFilled(Q(-hx, -hy), Q(hx, -hy), Q(hx, hy), Q(-hx, hy), PalCol(i % 6, f->k));
    }
  }
  if (const Act* f = get("sparks")) {
    for (int i = 0; i < 90; ++i) {
      const float life = Fr(Hash(i, 3) + f->el * (0.3f + 0.4f * Hash(i, 2))), y = H - life * H * 0.9f;
      const float x = Hash(i, 1) * W + std::sin(f->el * 3 + i) * W * 0.01f;
      dl->AddCircleFilled(P(x, y), std::max(0.5f, H * 0.004f * (1.2f - life)),
                          Rgba(255, static_cast<int>(150 + 100 * Hash(i, 5)), 40, f->k * (1 - life)), 8);
    }
  }
  if (const Act* f = get("lasers")) {
    static const int pal[6] = {0, 2, 3, 4, 0, 2};
    for (int i = 0; i < 6; ++i) {
      const bool right = i % 2;
      const float ox = right ? W : 0;
      const float an = (right ? 3.14159265f - 1.0f : 1.0f) + std::sin(T * 1.5f + i) * 0.5f * (right ? -1 : 1);
      const ImVec2 a = P(ox, 0), b = P(ox + std::cos(an) * D, std::sin(an) * D);
      dl->AddLine(a, b, PalCol(pal[i], 0.25f * f->k), H * 0.014f);
      dl->AddLine(a, b, PalCol(pal[i], f->k), H * 0.004f);
    }
  }
  if (const Act* f = get("lightning")) {
    const float seed = std::floor(f->el / 1.6f), ph = Fr(f->el / 1.6f);
    if (ph < 0.12f) {
      const float k = (0.12f - ph) / 0.12f;
      float x = (0.2f + 0.6f * Hash(seed, 1)) * W, y = 0;
      const float end = H * (0.6f + 0.3f * Hash(seed, 2));
      ImVec2 pts[13];
      pts[0] = P(x, y);
      for (int j = 1; j <= 12; ++j) { x += (Hash(seed * 13 + j, 3) - 0.5f) * W * 0.08f; y = end * j / 12; pts[j] = P(x, y); }
      dl->AddPolyline(pts, 13, Rgba(255, 255, 255, 0.3f * f->k * k), 0, H * 0.012f);
      dl->AddPolyline(pts, 13, Rgba(255, 255, 255, f->k * k), 0, H * 0.003f);
    }
  }
  if (const Act* f = get("vhs")) {
    Scan(dl, r, 0.6f * f->k);
    for (int j = 0; j < 3; ++j) { const float y = Fr(T * 0.13f + j * 0.37f) * H; dl->AddRectFilled(P(0, y), P(W, y + H * 0.02f), Rgba(255, 255, 255, 0.07f * f->k)); }
    const float bw = W * 0.12f;
    dl->AddRectFilledMultiColor(P(0, 0), P(bw, H), Rgba(255, 0, 60, 0.18f * f->k), 0, 0, Rgba(255, 0, 60, 0.18f * f->k));
    dl->AddRectFilledMultiColor(P(W - bw, 0), P(W, H), 0, Rgba(0, 160, 255, 0.2f * f->k), Rgba(0, 160, 255, 0.2f * f->k), 0);
    Grain(dl, r, T, 2 * f->k, 900, 0.9f, 0.94f);
  }
  if (const Act* f = get("scanlines")) Scan(dl, r, f->k);
  if (const Act* f = get("vaporwave")) Scan(dl, r, 0.45f * f->k);
}

void DrawFx(ImDrawList* raw, const ImVec4& r, const Song& song, double t) {
  ActMap on;
  for (const auto& f : song.fx) {
    if (t < f.t || t >= f.t + f.dur) continue;
    auto& a = on[f.type];
    if (f.amount > a.k) a = {static_cast<float>(f.amount), static_cast<float>((t - f.t) / 1000), static_cast<float>((f.t + f.dur - t) / 1000)};
  }
  if (on.empty()) return;
  Masked m{raw};
  Masked* dl = &m;
  const ImVec2 p0(r.x, r.y), p1(r.x + r.z, r.y + r.w);
  auto k = [&](const char* n) { auto it = on.find(n); return it == on.end() ? 0.f : it->second.k; };
  // (B/N, sepia, saturacion, contraste, brillo, tintes, vineta, desenfoque, fundidos, destellos, estrobo,
  //  golpe rojo, arcoiris, latido, iris y foco los hace el motor: native_fx.cpp)
  if (float a = k("chroma")) {
    const float bw = r.z * 0.12f;
    dl->AddRectFilledMultiColor(p0, ImVec2(p0.x + bw, p1.y), Rgba(255, 0, 60, 0.22f * a), 0, 0, Rgba(255, 0, 60, 0.22f * a));
    dl->AddRectFilledMultiColor(ImVec2(p1.x - bw, p0.y), p1, 0, Rgba(0, 160, 255, 0.25f * a), Rgba(0, 160, 255, 0.25f * a), 0);
  }
  DrawFx2D(dl, r, on, static_cast<float>(t / 1000));
  if (auto it = on.find("letterbox"); it != on.end() && !g_native_letterbox.load()) {
    const auto& a = it->second;
    const float h = r.w * 0.122f * a.k * Smooth(std::min({1.f, a.el / 0.4f, a.left / 0.4f}));
    dl->AddRectFilled(p0, ImVec2(p1.x, p0.y + h), IM_COL32(0, 0, 0, 255));
    dl->AddRectFilled(ImVec2(p0.x, p1.y - h), p1, IM_COL32(0, 0, 0, 255));   // se parte alrededor del traste
  }
}

void Draw3D(ImDrawList* dl, const ImVec4& r, const Song& song, double t, const GameCam& gc) {
  const Shot* shot = nullptr;
  for (const auto& s : song.shots) if (t >= s.t && t < s.t + s.dur) shot = &s;
  Projector pr;
  float hfov;
  Key k;
  if (shot && CameraAt(*shot, t - shot->t, k)) {
    // toma del editor: su camara exacta (y el hook la ha escrito en el motor)
    std::memcpy(pr.p, k.p, sizeof(pr.p));
    BasisFromKey(k, pr.R, pr.U, pr.F);
    hfov = k.hfov;
  } else if (gc.ok) {
    // cualquier otro plano: la camara real del juego
    std::memcpy(pr.p, gc.p, sizeof(pr.p));
    std::memcpy(pr.U, gc.U, sizeof(pr.U));
    std::memcpy(pr.F, gc.F, sizeof(pr.F));
    for (int j = 0; j < 3; ++j) pr.R[j] = gc.side_is_right ? gc.side[j] : -gc.side[j];
    hfov = gc.HfovDeg();
  } else {
    return;
  }
  pr.f = (r.z / 2) / std::tan(std::clamp(hfov, 5.f, 170.f) * 3.14159265f / 360.f);
  pr.cx = r.x + r.z / 2; pr.cy = r.y + r.w / 2;

  struct Draw { float depth; ImVec2 a, b, c; ImU32 col; };
  std::vector<Draw> list;
  static const float L[3] = {0.3f, 0.8f, 0.5f};
  for (const auto& p : song.props) {
    if (t < p.t || t >= p.t + p.dur) continue;
    for (const auto& tr : p.tris) {
      Draw d; float z0, z1, z2;
      if (!pr(tr.v, d.a, z0) || !pr(tr.v + 3, d.b, z1) || !pr(tr.v + 6, d.c, z2)) continue;
      const float e1[3] = {tr.v[3] - tr.v[0], tr.v[4] - tr.v[1], tr.v[5] - tr.v[2]};
      const float e2[3] = {tr.v[6] - tr.v[0], tr.v[7] - tr.v[1], tr.v[8] - tr.v[2]};
      float n[3] = {e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]};
      const float nl = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
      const float lit = nl > 0 ? std::fabs(n[0] * L[0] + n[1] * L[1] + n[2] * L[2]) / (nl * 1.02f) : 0.5f;
      const ImVec2 tri[3] = {d.a, d.b, d.c};
      if (g_hw.Touches(tri)) continue;   // el traste no se tapa
      d.depth = (z0 + z1 + z2) / 3;
      d.col = Col(tr.rgba, 0.45f + 0.55f * lit);
      list.push_back(d);
    }
  }
  std::sort(list.begin(), list.end(), [](const Draw& x, const Draw& y) { return x.depth > y.depth; });
  dl->PushClipRect(ImVec2(r.x, r.y), ImVec2(r.x + r.z, r.y + r.w), true);
  for (const auto& d : list) dl->AddTriangleFilled(d.a, d.b, d.c, d.col);
  ImFont* font = ImGui::GetFont();
  for (const auto& tx : song.texts) {
    if (t < tx.t || t >= tx.t + tx.dur) continue;
    ImVec2 c; float z;
    if (!pr(tx.p, c, z)) continue;
    const float size = std::clamp(pr.f * tx.h / z, 8.f, 400.f);
    const ImVec2 ts = font->CalcTextSizeA(size, FLT_MAX, 0, tx.s.c_str());
    {
      const ImVec2 bx[3] = {ImVec2(c.x - ts.x / 2, c.y - ts.y / 2), ImVec2(c.x + ts.x / 2, c.y - ts.y / 2), ImVec2(c.x + ts.x / 2, c.y + ts.y / 2)};
      const ImVec2 by[3] = {ImVec2(c.x - ts.x / 2, c.y - ts.y / 2), ImVec2(c.x + ts.x / 2, c.y + ts.y / 2), ImVec2(c.x - ts.x / 2, c.y + ts.y / 2)};
      if (g_hw.Touches(bx) || g_hw.Touches(by)) continue;
    }
    dl->AddText(font, size, ImVec2(c.x - ts.x / 2 + 2, c.y - ts.y / 2 + 2), IM_COL32(0, 0, 0, 160), tx.s.c_str());
    dl->AddText(font, size, ImVec2(c.x - ts.x / 2, c.y - ts.y / 2), Col(tx.rgba), tx.s.c_str());
  }
  dl->PopClipRect();
}

}  // namespace

void Rescan(const std::filesystem::path& mods_root) {
  std::lock_guard<std::mutex> lk(g_scan_mutex);
  std::vector<std::filesystem::path> found;
  std::error_code ec;
  if (!mods_root.empty() && std::filesystem::exists(mods_root, ec)) {
    for (auto it = std::filesystem::recursive_directory_iterator(mods_root, ec);
         !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
      if (it->is_regular_file(ec) && it->path().extension() == ".edfx") found.push_back(it->path());
    }
  }
  std::sort(found.begin(), found.end());
  if (!FilesChanged(g_index.load(), found)) return;
  auto* idx = new Index();
  for (const auto& f : found) {
    Song s;
    std::vector<std::pair<uint32_t, double>> sync;
    std::error_code e2;
    idx->files.emplace_back(f, std::filesystem::last_write_time(f, e2));
    if (!Load(f, s, sync)) continue;
    const int n = static_cast<int>(idx->songs.size());
    for (const auto& [key, t] : sync) {
      idx->sync[key] = {n, t};
      idx->bloom[(key >> 6) & 1023] |= 1ull << (key & 63);
    }
    std::string key = s.name.empty() ? f.stem().string() : s.name;
    for (auto& ch : key) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    idx->by_name[key] = n;
    TRACE("[EDFX] " << f.filename().string() << ": " << s.fx.size() << " efectos, " << s.shots.size()
                    << " tomas, " << s.props.size() << " objetos, " << s.texts.size() << " textos, "
                    << sync.size() << " eventos de sincronia");
    idx->songs.push_back(std::move(s));
  }
  // el indice anterior se deja vivo a proposito: el hook de simbolos puede estar leyendolo en otro hilo
  g_index.store(idx);
}

bool OnSymbolLookup(uint32_t checksum) {
  const Index* idx = g_index.load(std::memory_order_acquire);
  if (!idx || !(idx->bloom[(checksum >> 6) & 1023] & (1ull << (checksum & 63)))) return false;
  auto it = idx->sync.find(checksum);
  if (it == idx->sync.end()) return false;
  {
    std::lock_guard<std::mutex> lk(g_clock_mutex);
    g_song = it->second.song;
    g_anchor_ms = it->second.t;
    g_anchor_at = Clock::now();
  }
  static std::atomic<int> logged{0};
  if (logged.fetch_add(1) < 40) {
    TRACE("[EDFX] sync " << idx->songs[it->second.song].name << " t=" << it->second.t << " ms");
  }
  return true;
}

std::mutex g_zone_mutex;
std::vector<std::string> g_zones;   // ultimas zonas (escenarios) abiertas, la mas reciente al final

bool ZoneLoaded(const std::string& zone) {
  std::string z = zone;
  for (auto& ch : z) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  std::lock_guard<std::mutex> lk(g_zone_mutex);
  for (const auto& x : g_zones) if (x == z) return true;
  return false;
}

void OnFileOpen(const char* path) {
  if (!path) return;
  std::string p(path);
  for (auto& ch : p) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  if (const auto zp = p.find("zones/"); zp != std::string::npos && p.size() > 8 &&
      p.compare(p.size() - 8, 8, ".pak.xen") == 0) {
    std::string z = p.substr(p.find_last_of("/\\") + 1);
    z = z.substr(0, z.size() - 8);
    if (z != "global" && z.rfind("global_", 0) != 0) {
      std::lock_guard<std::mutex> lk(g_zone_mutex);
      g_zones.erase(std::remove(g_zones.begin(), g_zones.end(), z), g_zones.end());
      g_zones.push_back(z);
      if (g_zones.size() > 3) g_zones.erase(g_zones.begin());
    }
  }
  const auto suf = std::string("_song.pak.xen");
  if (p.size() <= suf.size() || p.compare(p.size() - suf.size(), suf.size(), suf) != 0) return;
  const auto slash = p.find_last_of("/\\");
  if (slash == std::string::npos || p.find("songs") == std::string::npos) return;
  std::string name = p.substr(slash + 1, p.size() - suf.size() - slash - 1);
  std::lock_guard<std::mutex> lk(g_clock_mutex);
  if (name != g_loaded_song) {
    g_loaded_song = name;
    const Index* idx = g_index.load();
    const bool has = idx && idx->by_name.count(name);
    TRACE("[EDFX] cancion cargada: " << name << (has ? " (tiene .edfx)" : " (sin .edfx)"));
  }
}

namespace {
uint32_t Be32(const uint8_t* m) { return (uint32_t(m[0]) << 24) | (uint32_t(m[1]) << 16) | (uint32_t(m[2]) << 8) | m[3]; }
float BeF(const uint8_t* m) { uint32_t u = Be32(m); float f; std::memcpy(&f, &u, 4); return f; }
void PutF(uint8_t* m, float f) { uint32_t u; std::memcpy(&u, &f, 4); m[0] = u >> 24; m[1] = u >> 16; m[2] = u >> 8; m[3] = u; }
bool GuestPtr(uint32_t a) { return a >= 0x40000000u && a < 0xF0000000u && (a & 3) == 0; }

// Lectura protegida: si una direccion no esta mapeada no se cae el juego, simplemente no hay camara.
// Camara del escenario. Candidatos: el viewport del escenario (bg_viewport, donde el motor dibuja la imagen 3D en
// las canciones) y el principal (id 0). Se queda con el primero cuya camara tenga una pose valida (vectores unitarios
// y un FOV razonable). Antes solo se miraba el id 0, que en las canciones no lleva la camara 3D: el hook nunca la
// encontraba y ni las tomas ni los efectos de camara llegaban al juego.
bool CamLooksValid(const uint8_t* r) {
  auto len = [&](int off) { float s = 0; for (int j = 0; j < 3; ++j) { const float v = BeF(r + off + 4 * j); s += v * v; } return std::sqrt(s); };
  const float lf = len(48), lu = len(32), h = BeF(r + 80), px = BeF(r);
  return std::isfinite(lf) && std::fabs(lf - 1) < 0.1f && std::fabs(lu - 1) < 0.1f && std::isfinite(px) && std::fabs(px) < 1e5f &&
         h > 0.05f && h < 175.f;
}
bool ReadCamRaw(const uint8_t* base, uint32_t& cam_out, uint8_t raw[96]) {
  static constexpr uint32_t kIds[2] = {0x071F4183u /* bg_viewport */, 0u};
  static int logged = 0;
  __try {
    const uint32_t head = 0x82860320;
    for (uint32_t want : kIds) {
      uint32_t node = Be32(base + head + 4);
      for (int i = 0; i < 32 && node != head && GuestPtr(node); ++i, node = Be32(base + node + 4)) {
        if (Be32(base + node + 864) != want) continue;
        const uint32_t vp = Be32(base + node + 16);
        if (!GuestPtr(vp)) continue;
        const uint32_t cam = Be32(base + vp + 116);
        if (!GuestPtr(cam)) continue;
        uint8_t r[96];
        std::memcpy(r, base + cam + 112, 96);
        if (!CamLooksValid(r)) continue;
        std::memcpy(raw, r, 96);
        static uint32_t last = 0;
        if (last != cam && logged < 6) {
          last = cam;
          ++logged;
          TRACE("[EDFX] camara del escenario: viewport " << std::hex << want << " camara @" << cam << std::dec);
        }
        cam_out = cam;
        return true;
      }
    }
  } __except (1) {
  }
  return false;
}

// Escritura protegida de la camara (funcion aparte: __try no admite objetos con destructor)
void WriteCam(uint8_t* c, const float old_p[3], const float p[3], bool side_right, const float R[3], const float U[3],
              const float F[3], float hfov) {
  __try {
    const bool pos2 = std::fabs(BeF(c + 176) - old_p[0]) < 1e-3f && std::fabs(BeF(c + 180) - old_p[1]) < 1e-3f &&
                      std::fabs(BeF(c + 184) - old_p[2]) < 1e-3f;
    for (int j = 0; j < 3; ++j) {
      PutF(c + 112 + 4 * j, p[j]);
      PutF(c + 128 + 4 * j, side_right ? R[j] : -R[j]);
      PutF(c + 144 + 4 * j, U[j]);
      PutF(c + 160 + 4 * j, F[j]);
      if (pos2) PutF(c + 176 + 4 * j, p[j]);
    }
    PutF(c + 192, hfov);
  } __except (1) {
  }
}

float Len3(const float v[3]) { return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); }

// Lo que el juego puso en la camara antes de que la tocaramos y lo que escribimos nosotros. Si un frame el motor
// no reescribe la camara (planos fijos), lo que hay es lo NUESTRO: se parte de la pose del juego guardada, para
// que los efectos no se acumulen frame a frame ni la camara se quede donde la dejamos al acabar el efecto.
struct CamMemo { uint32_t addr = 0; bool wrote = false; uint8_t base[96] = {}, written[96] = {}; } g_memo;
bool RawAt(const uint8_t* base, uint32_t cam, uint8_t raw[96]) {
  __try { std::memcpy(raw, base + cam + 112, 96); return true; } __except (1) { return false; }
}
bool PutRawAt(uint8_t* base, uint32_t cam, const uint8_t raw[96]) {
  __try { std::memcpy(base + cam + 112, raw, 96); return true; } __except (1) { return false; }
}
}  // namespace

void UpdateGameCamera(uint8_t* base, double ms, const std::string& song) {
  uint8_t raw[96];
  uint32_t cam = 0;
  GameCam gc;
  bool wrote_now = false;
  if (ReadCamRaw(base, cam, raw)) {
    if (g_memo.wrote && g_memo.addr == cam)
      for (int c = 0; c < 6; ++c)
        if (std::memcmp(raw + 16 * c, g_memo.written + 16 * c, 16) == 0) std::memcpy(raw + 16 * c, g_memo.base + 16 * c, 16);
    if (g_memo.addr != cam) g_memo.wrote = false;
    std::memcpy(g_memo.base, raw, 96);
    g_memo.addr = cam;
    auto v3 = [&](int off, float out[3]) { for (int j = 0; j < 3; ++j) out[j] = BeF(raw + off - 112 + 4 * j); };
    v3(112, gc.p); v3(128, gc.side); v3(144, gc.U); v3(160, gc.F);
    gc.hfov_raw = BeF(raw + 192 - 112);
    gc.addr = cam;
    const float lf = Len3(gc.F), lu = Len3(gc.U);
    gc.ok = std::isfinite(lf) && std::fabs(lf - 1) < 0.1f && std::fabs(lu - 1) < 0.1f && std::isfinite(gc.p[0]) &&
            gc.hfov_raw > 0.05f && gc.hfov_raw < 175.f;
    if (gc.ok) {
      gc.rad = gc.hfov_raw < 3.2f;
      const float cr[3] = {gc.F[1] * gc.U[2] - gc.F[2] * gc.U[1], gc.F[2] * gc.U[0] - gc.F[0] * gc.U[2],
                           gc.F[0] * gc.U[1] - gc.F[1] * gc.U[0]};
      gc.side_is_right = cr[0] * gc.side[0] + cr[1] * gc.side[1] + cr[2] * gc.side[2] >= 0;
    }
  }
  static int logs = 0;
  static Clock::time_point last{};
  if (gc.ok && logs < 10 && Clock::now() - last > std::chrono::seconds(6)) {
    last = Clock::now();
    ++logs;
    TRACE("[EDFX] camara del juego @" << std::hex << gc.addr << std::dec << " pos=(" << gc.p[0] << "," << gc.p[1] << ","
          << gc.p[2] << ") at=(" << gc.F[0] << "," << gc.F[1] << "," << gc.F[2] << ") up=(" << gc.U[0] << ","
          << gc.U[1] << "," << gc.U[2] << ") hfov=" << gc.hfov_raw << (gc.side_is_right ? " lado=derecha" : " lado=izquierda"));
  }

  // Toma del editor activa -> escribir su camara en el motor
  const Index* idx = g_index.load();
  const Shot* shot = nullptr;
  if (gc.ok && idx && ms > 0) {
    if (auto it = idx->by_name.find(song); it != idx->by_name.end())
      for (const auto& s : idx->songs[it->second].shots) if (ms >= s.t && ms < s.t + s.dur) shot = &s;
  }
  Key k;
  if (shot && CameraAt(*shot, ms - shot->t, k)) {
    float R[3], U[3], F[3];
    BasisFromKey(k, R, U, F);
    static const Shot* logged_shot = nullptr;
    if (logged_shot != shot) {
      logged_shot = shot;
      TRACE("[EDFX] toma del editor en t=" << static_cast<int>(shot->t) << ": camara del juego pos=(" << gc.p[0] << ","
            << gc.p[1] << "," << gc.p[2] << ") -> editor pos=(" << k.p[0] << "," << k.p[1] << "," << k.p[2] << ")");
    }
    WriteCam(base + gc.addr, gc.p, k.p, gc.side_is_right, R, U, F, gc.rad ? k.hfov / 57.2957795f : k.hfov);
    wrote_now = true;
    std::memcpy(gc.p, k.p, sizeof(gc.p));
    std::memcpy(gc.U, U, sizeof(gc.U));
    std::memcpy(gc.F, F, sizeof(gc.F));
    for (int j = 0; j < 3; ++j) gc.side[j] = gc.side_is_right ? R[j] : -R[j];
    gc.hfov_raw = gc.rad ? k.hfov / 57.2957795f : k.hfov;
  }
  // Efectos de camara (temblor, plano holandes, golpe de zoom, vertigo, camara en mano): se aplican sobre la
  // camara que el motor acaba de calcular (o la de la toma del editor) y se escriben en el propio motor.
  if (gc.ok && ms > 0) {
    float shake = 0, dutch = 0, punch = 0, vert = 0, hand = 0, el = 0, el_p = 0, left_v = 0, el_v = 0;
    float dutch_deg = 15, punch_zoom = 0.3f, punch_len = 0.6f, vert_amt = 0.45f, shake_spd = 1, hand_spd = 1;
    for (const auto& f : ActiveFxNow()) {
      if (f.type == "shake") { shake = std::max(shake, f.k); shake_spd = f.P("speed", 1); }
      else if (f.type == "dutch") { dutch = std::max(dutch, f.k * std::min({1.f, f.el / 0.5f, f.left / 0.5f})); dutch_deg = f.P("angle", 15); }
      else if (f.type == "zoompunch") { punch = std::max(punch, f.k); el_p = f.el; punch_zoom = f.P("zoom", 0.3f); punch_len = f.P("len", 0.6f); }
      else if (f.type == "vertigo") { vert = std::max(vert, f.k); el_v = f.el; left_v = f.left; vert_amt = f.P("amount", 0.45f); }
      else if (f.type == "handheld") { hand = std::max(hand, f.k); hand_spd = f.P("speed", 1); }
      (void)el;
    }
    if (shake + dutch + punch + vert + hand > 0) {
      const float T = static_cast<float>(ms / 1000.0);
      float R[3], U[3], F[3], P[3];
      for (int j = 0; j < 3; ++j) { R[j] = gc.side_is_right ? gc.side[j] : -gc.side[j]; U[j] = gc.U[j]; F[j] = gc.F[j]; P[j] = gc.p[j]; }
      auto noise = [](float t, float s) { return std::sin(t * 1.7f + s) * 0.5f + std::sin(t * 3.1f + s * 2.3f) * 0.3f + std::sin(t * 7.3f + s * 0.7f) * 0.2f; };
      // desplazamiento (temblor rapido + camara en mano lenta)
      const float Ts = T * shake_spd, Th = T * hand_spd;
      const float ax = shake * 0.06f * noise(Ts * 9, 1) + hand * 0.05f * noise(Th * 1.3f, 4);
      const float ay = shake * 0.06f * noise(Ts * 9, 2) + hand * 0.04f * noise(Th * 1.1f, 5);
      for (int j = 0; j < 3; ++j) P[j] += R[j] * ax + U[j] * ay;
      // giro sobre el eje de mirada: holandes (fijo) + temblor + mano
      const float roll = dutch * dutch_deg * 0.0174533f + shake * 0.02f * noise(Ts * 11, 3) + hand * 0.012f * noise(Th * 0.9f, 6);
      if (roll != 0) {
        const float c = std::cos(roll), s2 = std::sin(roll);
        for (int j = 0; j < 3; ++j) { const float r = R[j], u = U[j]; R[j] = r * c + u * s2; U[j] = u * c - r * s2; }
      }
      float fov = gc.hfov_raw;
      if (punch > 0) {   // entra de golpe y vuelve en 0.6 s
        const float e = el_p < 0.06f ? el_p / 0.06f : std::max(0.f, 1 - (el_p - 0.06f) / std::max(0.05f, punch_len));
        fov *= 1 - std::clamp(punch_zoom, 0.f, 0.9f) * punch * e;
      }
      if (vert > 0) {    // vertigo: el angulo se abre mientras la camara se acerca
        const float d = std::min(1.f, el_v / std::max(0.3f, el_v + left_v));
        fov *= 1 + vert_amt * vert * d;
        for (int j = 0; j < 3; ++j) P[j] += F[j] * 1.5f * vert * d;
      }
      WriteCam(base + gc.addr, gc.p, P, gc.side_is_right, R, U, F, fov);
      wrote_now = true;
      std::memcpy(gc.p, P, sizeof(P)); std::memcpy(gc.U, U, sizeof(U));
      for (int j = 0; j < 3; ++j) gc.side[j] = gc.side_is_right ? R[j] : -R[j];
      gc.hfov_raw = fov;
    }
  }
  if (gc.ok) {
    if (wrote_now) {
      g_memo.wrote = RawAt(base, gc.addr, g_memo.written);
    } else if (g_memo.wrote) {   // se acabo la toma/efecto: devolver la camara del juego
      PutRawAt(base, gc.addr, g_memo.base);
      g_memo.wrote = false;
    }
  }
  std::lock_guard<std::mutex> lk(g_clock_mutex);
  g_cam = gc;
}

void OnLogicFrame(uint8_t* base) {
  uint64_t bits = 0;
  for (int i = 0; i < 8; ++i) bits = (bits << 8) | base[kSongTimeAddr + i];   // big-endian
  double sec;
  std::memcpy(&sec, &bits, sizeof(sec));
  if (!std::isfinite(sec)) return;
  const double ms = sec * 1000.0;
  {
    std::string song;
    { std::lock_guard<std::mutex> lk(g_clock_mutex); song = g_loaded_song; }
    if (!song.empty()) UpdateGameCamera(base, ms, song);
  }
  std::lock_guard<std::mutex> lk(g_clock_mutex);
  if (ms != g_game_ms) {
    g_game_ms = ms;
    g_game_moved_at = Clock::now();
  }
  // diagnostico: unas pocas muestras del reloj del motor con una cancion cargada
  static Clock::time_point last_log{};
  static int logs = 0;
  if (!g_loaded_song.empty() && logs < 12 && Clock::now() - last_log > std::chrono::seconds(5)) {
    last_log = Clock::now();
    ++logs;
    TRACE("[EDFX] reloj del motor: " << g_loaded_song << " songtime=" << ms << " ms");
  }
}

std::vector<FxState> ActiveFxNow() {
  std::vector<FxState> out;
  const Index* idx = g_index.load();
  if (!idx) return out;
  std::string song;
  double t;
  {
    std::lock_guard<std::mutex> lk(g_clock_mutex);
    const double still = std::chrono::duration<double, std::milli>(Clock::now() - g_game_moved_at).count();
    if (still > kStillMs || g_game_ms <= 0) return out;   // pausa, menu o sin cancion
    song = g_loaded_song;
    t = g_game_ms;
  }
  auto it = idx->by_name.find(song);
  if (it == idx->by_name.end()) return out;
  for (const auto& f : idx->songs[it->second].fx) {
    if (t < f.t || t >= f.t + f.dur) continue;
    FxState st{f.type, static_cast<float>(f.amount), static_cast<float>((t - f.t) / 1000), static_cast<float>((f.t + f.dur - t) / 1000), f.params};
    {   // fundidos de entrada y salida del efecto (segundos, "fin" y "fout")
      const float fin = st.P("fin", 0), fout = st.P("fout", 0);
      if (fin > 0) st.k *= Smooth(st.el / fin);
      if (fout > 0) st.k *= Smooth(st.left / fout);
    }
    bool merged = false;
    for (auto& o : out) if (o.type == st.type) { if (st.k > o.k) o = st; merged = true; }
    if (!merged) out.push_back(st);
  }
  return out;
}

bool StripCamera(int i, float p[3], float side[3], float U[3], float F[3], float& hfov_raw) {
  if (i < 0 || i >= 4) return false;
  const Index* idx = g_index.load();
  if (!idx) return false;
  std::string song;
  double t;
  GameCam gc;
  {
    std::lock_guard<std::mutex> lk(g_clock_mutex);
    song = g_loaded_song; t = g_game_ms; gc = g_cam;
  }
  if (!gc.ok) return false;
  auto it = idx->by_name.find(song);
  if (it == idx->by_name.end()) return false;
  for (const auto& f : idx->songs[it->second].fx) {
    if (f.type != "split" || t < f.t || t >= f.t + f.dur) continue;
    const Shot& s = f.strips[i];
    Key k;
    if (s.keys.empty() || !CameraAt(s, t - s.t, k)) return false;
    float R[3];
    BasisFromKey(k, R, U, F);
    for (int j = 0; j < 3; ++j) { p[j] = k.p[j]; side[j] = gc.side_is_right ? R[j] : -R[j]; }
    hfov_raw = gc.rad ? k.hfov / 57.2957795f : k.hfov;
    return true;
  }
  return false;
}

bool MainCamera(uint32_t& addr) {
  std::lock_guard<std::mutex> lk(g_clock_mutex);
  addr = g_cam.ok ? g_cam.addr : 0;
  return addr != 0;
}

bool NativeNow(std::string& song, double& ms, std::vector<NativeItem>& out) {
  out.clear();
  const Index* idx = g_index.load();
  {
    std::lock_guard<std::mutex> lk(g_clock_mutex);
    song = g_loaded_song;
    ms = g_game_ms;
  }
  if (!idx || song.empty() || ms <= 0) return false;
  auto it = idx->by_name.find(song);
  if (it == idx->by_name.end()) return true;
  const auto& list = idx->songs[it->second].natives;
  for (int i = 0; i < static_cast<int>(list.size()); ++i) {
    const auto& n = list[i];
    if (ms < n.t || ms >= n.t + n.dur) continue;
    NativeItem o;
    o.id = i; o.kind = n.kind; o.asset = n.asset;
    std::memcpy(o.p, n.p, sizeof(o.p)); std::memcpy(o.q, n.q, sizeof(o.q));
    std::memcpy(o.rgb, n.rgb, sizeof(o.rgb)); std::memcpy(o.v, n.v, sizeof(o.v));
    out.push_back(std::move(o));
  }
  return true;
}

void SetNativeLetterbox(bool on) { g_native_letterbox = on; }

double SongMsNow() {
  std::lock_guard<std::mutex> lk(g_clock_mutex);
  return g_game_ms;
}

EdfxOverlay::EdfxOverlay(rex::ui::ImGuiDrawer* drawer) : ImGuiDialog(drawer) {}

void EdfxOverlay::OnDraw(ImGuiIO& io) {
  // releer los .edfx cada ~3 s (el editor puede recompilar con el juego abierto)
  static Clock::time_point last_scan{};
  if (Clock::now() - last_scan > std::chrono::seconds(3)) {
    last_scan = Clock::now();
    const auto& root = ModManager::Instance().root();
    if (!root.empty()) Rescan(root);
  }
  const Index* idx = g_index.load();
  if (!idx) return;
  int song = -1;
  double t = 0;
  {
    std::lock_guard<std::mutex> lk(g_clock_mutex);
    const auto now = Clock::now();
    // 1) reloj del motor + cancion cargada
    if (auto it = idx->by_name.find(g_loaded_song); it != idx->by_name.end()) {
      const double still = std::chrono::duration<double, std::milli>(now - g_game_moved_at).count();
      if (still < kStillMs && g_game_ms > 0) {
        song = it->second;
        t = g_game_ms + std::min(still, 50.0);   // entre frames logicos, interpolar un poco
      }
    }
    // 2) respaldo: eventos de sincronia
    if (song < 0 && g_song >= 0) {
      const double since = std::chrono::duration<double, std::milli>(now - g_anchor_at).count();
      if (since > kHoldMs) g_song = -1;
      else { song = g_song; t = g_anchor_ms + since; }
    }
    static int last_logged = -2;
    if (song != last_logged) {
      last_logged = song;
      if (song >= 0) TRACE("[EDFX] dibujando " << idx->songs[song].name << " desde t=" << static_cast<int>(t) << " ms");
    }
  }
  if (song < 0 || song >= static_cast<int>(idx->songs.size())) return;
  const Song& s = idx->songs[song];
  ImDrawList* dl = ImGui::GetBackgroundDrawList();
  const ImVec4 r = GameRect(io);
  g_hw.Set(r);
  GameCam gc;
  { std::lock_guard<std::mutex> lk(g_clock_mutex); gc = g_cam; }
  Draw3D(dl, r, s, t, gc);
  DrawFx(dl, r, s, t);
}

}  // namespace re_wor::de::edfx
