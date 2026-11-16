#include "de/controls_menu.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "core/crash_handler.hpp"
#include "de/de_config.hpp"
#include "de/qb_bridge.hpp"
#include "input/input_factory.hpp"
#include "input/devices.hpp"
#include "input/key_names.hpp"
#include "input/keyboard_guitar_driver.hpp"
#include "input/mic_capture.hpp"

namespace re_wor::de::controls {

namespace {

using Clock = std::chrono::steady_clock;
using re_wor::input::KeyboardGuitarDriver;

bool g_es = false;
const char* T(const char* es, const char* en) { return g_es ? es : en; }

// ---------------------------------------------------------------------------- texturas del juego
// El juego nombra sus texturas por el nombre corto del archivo (clave QB de "icon_guitar_fill"); los
// decompiladores muestran la ruta, pero en el bytecode va el nombre corto. Todas estan en ui_shared.
uint32_t UiTexture(const char* dir, const char* name) {
  (void)dir;
  return qb_bridge::CalculateQbKey(name);
}
[[maybe_unused]] uint32_t InstrumentIcon(Instrument i) {
  static const char* names[] = {"icon_guitar_fill", "icon_bass_fill", "icon_drum_fill", "icon_vocals_fill"};
  return UiTexture("songlist", names[static_cast<int>(i)]);
}
uint32_t InstrumentPicture(Instrument i) {
  static const char* names[] = {"fb_qp_session_guitar", "fb_qp_session_bass", "fb_qp_session_drums",
                                "fb_qp_session_vocals"};
  return UiTexture("facebook", names[static_cast<int>(i)]);
}

const char* InstrumentName(Instrument i) {
  switch (i) {
    case Instrument::kBass: return T("Bajo", "Bass");
    case Instrument::kDrums: return T("Bateria", "Drums");
    case Instrument::kVocals: return T("Voz", "Vocals");
    default: return T("Guitarra", "Guitar");
  }
}

struct Label {
  const char* es;
  const char* en;
};
const Label kGuitarLabels[] = {
    {"Traste verde", "Green fret"},       {"Traste rojo", "Red fret"},          {"Traste amarillo", "Yellow fret"},
    {"Traste azul", "Blue fret"},         {"Traste naranja", "Orange fret"},    {"Rasgueo arriba", "Strum up"},
    {"Rasgueo abajo", "Strum down"},      {"Start", "Start"},                   {"Star Power", "Star Power"},
    {"Palanca", "Whammy"},                {"Inclinar", "Tilt"},                 {"Menu izquierda", "Menu left"},
    {"Menu derecha", "Menu right"},
};
const Label kDrumLabels[] = {
    {"Pad rojo", "Red pad"},          {"Platillo amarillo", "Yellow cymbal"}, {"Pad azul", "Blue pad"},
    {"Platillo naranja", "Orange cymbal"}, {"Pad verde", "Green pad"},    {"Pedal", "Kick pedal"},
    {"Start", "Start"},               {"Star Power", "Star Power"},           {"Menu arriba", "Menu up"},
    {"Menu abajo", "Menu down"},      {"Menu izquierda", "Menu left"},        {"Menu derecha", "Menu right"},
};
const Label kPadLabels[] = {
    {"A (aceptar)", "A (accept)"}, {"B (volver)", "B (back)"}, {"X", "X"},          {"Y", "Y"},
    {"Start", "Start"},            {"Back", "Back"},           {"Arriba", "Up"},     {"Abajo", "Down"},
    {"Izquierda", "Left"},         {"Derecha", "Right"},       {"LB", "LB"},         {"RB", "RB"},
};
static_assert(sizeof(kGuitarLabels) / sizeof(kGuitarLabels[0]) == static_cast<size_t>(GuitarAction::kCount));
static_assert(sizeof(kDrumLabels) / sizeof(kDrumLabels[0]) == static_cast<size_t>(DrumAction::kCount));
static_assert(sizeof(kPadLabels) / sizeof(kPadLabels[0]) == static_cast<size_t>(PadAction::kCount));

std::string* KeysFor(PlayerSlotConfig& p, Instrument inst, int* count, const Label** labels) {
  if (inst == Instrument::kDrums) {
    *count = static_cast<int>(DrumAction::kCount);
    *labels = kDrumLabels;
    return p.drum_keys;
  }
  if (inst == Instrument::kVocals) {
    *count = static_cast<int>(PadAction::kCount);
    *labels = kPadLabels;
    return p.pad_keys;
  }
  *count = static_cast<int>(GuitarAction::kCount);
  *labels = kGuitarLabels;
  return p.guitar_keys;
}

// ---------------------------------------------------------------------------- texto para el juego
// Las cadenas del juego son Latin-1. Se quitan comillas (cierran el parametro) y lo que la fuente no tiene.
std::string GameText(const std::string& utf8, size_t max_chars = 44) {
  std::string out;
  size_t chars = 0;
  for (size_t i = 0; i < utf8.size() && chars < max_chars;) {
    const unsigned char c = static_cast<unsigned char>(utf8[i]);
    uint32_t cp = c;
    size_t n = 1;
    if (c >= 0xC0 && c < 0xE0 && i + 1 < utf8.size()) { cp = ((c & 0x1F) << 6) | (utf8[i + 1] & 0x3F); n = 2; }
    else if (c >= 0xE0 && c < 0xF0) { cp = 0xFFFF; n = 3; }
    else if (c >= 0xF0) { cp = 0xFFFF; n = 4; }
    i += n;
    if (cp == '"') cp = '\'';
    const bool ok = (cp >= 0x20 && cp < 0x7F) || cp == 0xE1 || cp == 0xE9 || cp == 0xED || cp == 0xF3 || cp == 0xFA ||
                    cp == 0xF1 || cp == 0xD1 || cp == 0xFC || cp == 0xC1 || cp == 0xC9 || cp == 0xCD || cp == 0xD3 ||
                    cp == 0xDA;
    if (!ok) continue;
    out += static_cast<char>(cp);
    chars++;
  }
  while (!out.empty() && out.back() == ' ') out.pop_back();
  if (chars >= max_chars) out += "...";
  return out;
}

std::string Hex(uint32_t v) {
  char b[16];
  std::snprintf(b, sizeof b, "0x%08x", v);
  return b;
}

std::vector<std::string> SplitKeys(const std::string& s) {
  std::vector<std::string> out;
  size_t a = 0;
  while (a <= s.size()) {
    const size_t b = s.find(',', a);
    std::string k = s.substr(a, b == std::string::npos ? std::string::npos : b - a);
    while (!k.empty() && k.front() == ' ') k.erase(k.begin());
    while (!k.empty() && k.back() == ' ') k.pop_back();
    if (!k.empty()) out.push_back(k);
    if (b == std::string::npos) break;
    a = b + 1;
  }
  return out;
}

std::string JoinKeys(const std::vector<std::string>& v) {
  std::string s;
  for (size_t i = 0; i < v.size(); i++) s += (i ? "," : "") + v[i];
  return s;
}

// ---------------------------------------------------------------------------- estado
// Vistas: lobby (4 jugadores) -> instrumento -> configuracion (dispositivo + mapeo; voz: microfono +
// control + mapeo) -> listas de dispositivos / microfonos.
enum class Step { kLobby, kInstrument, kConfig, kDeviceList, kMicList };
enum class Act {
  kNone, kPlayer, kLobbyDone, kInstrument, kListDevices, kListen, kListMics, kMicGain, kMic,
  kKeyboard, kPad, kNoDevice, kKey, kResetKeys, kTest, kConfigDone
};

// Una pieza del panel (qb/controls.q, re_ctl_item). Las elegibles van primero (pieza = opcion del menu
// invisible) y los adornos justo detras; el juego solo dibuja las que hacen falta.
enum Style { kRow = 0, kCard = 1, kButton = 2, kHeader = 3, kInfo = 4, kImage = 5 };
struct Item {
  int x = 0, y = 0, w = 100, h = 32;
  int style = kRow;
  std::string text, sub;
  uint32_t icon = 0, img = 0;
  int imgw = 0;
  bool focusable = false;
  Act act = Act::kNone;
  int arg = 0;
  std::string id, name;   // ruta del teclado, guid del mando o id del microfono
};
constexpr int kMaxOptions = 24, kMaxItems = 40;

struct State {
  bool active = false;
  Step step = Step::kLobby;
  Instrument inst = Instrument::kGuitar;
  int slot = 0;
  std::vector<Item> rows;    // elegibles, en orden (= opcion del menu invisible)
  std::vector<Item> decor;   // adornos
  int capture_row = -1;      // opcion de tecla esperando pulsacion
  int listen_row = -1;       // opcion "detectar" esperando un boton
  Clock::time_point wait_until{};
  int level_item = -1;       // pieza del nivel del microfono
  int gain_row = -1;
  int tick = 0;
  // foco pendiente: las opciones viejas se borran al acabar el frame; el foco se pone un poco despues
  std::vector<std::string> pending;
  // piezas de la vista: van un poco despues de re_ctl_begin (los scripts lanzados en el mismo frame no
  // corren en el orden en que se mandan y begin, que oculta todo, podia acabar el ultimo)
  std::vector<std::string> building;
  Clock::time_point pending_at{};
  int session = -1;     // re_ctl_session de la pantalla abierta
  bool opening = false; // pantalla recien abierta: se rellena en opened_at
  Clock::time_point opened_at{};
} g;

std::atomic<bool> g_open_request{false};

void Run(const std::string& s) { qb_bridge::ExecuteScriptQuiet(s); }

void SetItemText(int item, const std::string& text) {
  Run("re_ctl_itemtext idx=" + std::to_string(item) + " text=\"" + GameText(text, 60) + "\"");
}

// Se guarda al momento, pero los mandos virtuales del teclado se rehacen al salir de la pantalla: si cambian
// mientras se esta en el menu, el juego ve desconectarse su mando principal y se sale de los menus.
bool g_dirty_bindings = false;
void SaveAndApply(bool mic) {
  Save();
  g_dirty_bindings = true;
  if (mic) re_wor::input::ApplyMicConfig();
}
void ApplyBindingsNow() {
  if (!g_dirty_bindings) return;
  g_dirty_bindings = false;
  if (auto* kb = KeyboardGuitarDriver::Instance()) kb->ReloadBindings();
  re_wor::input::UpdatePlayerOneFallback(nullptr, true);
  TRACE("[Controles] cambios aplicados a los mandos de teclado");
}

PlayerSlotConfig Slot(int i) {
  std::lock_guard lock(Mutex());
  return Get().input.players[i];
}

bool HasDevice(const PlayerSlotConfig& p) { return !p.keyboard.empty() || !p.pad.empty() || !p.mic.empty(); }

std::string DeviceSummary(const PlayerSlotConfig& p) {
  std::string d;
  if (!p.keyboard.empty()) {
    d = p.keyboard == "*" ? T("cualquier teclado", "any keyboard")
                          : (p.keyboard_name.empty() ? re_wor::input::KeyboardNameForPath(p.keyboard) : p.keyboard_name);
  } else if (!p.pad.empty()) {
    d = p.pad_name;
  }
  return d;
}

std::string MicSummary(const PlayerSlotConfig& p) {
  if (p.mic.empty()) return {};
  return p.mic == "default" ? T("micro predeterminado", "default mic") : p.mic_name;
}

std::string PlayerSummary(const PlayerSlotConfig& p) {
  std::string dev = DeviceSummary(p);
  if (InstrumentFromId(p.instrument) == Instrument::kVocals && !p.mic.empty()) {
    const std::string mic = MicSummary(p);
    dev = dev.empty() ? mic : mic + " + " + dev;
  }
  return dev;
}

std::string OwnerOf(const std::string& id, bool pad) {
  std::lock_guard lock(Mutex());
  for (int i = 0; i < kMaxPlayers; i++) {
    if (i == g.slot) continue;
    const auto& p = Get().input.players[i];
    if ((pad ? p.pad : p.keyboard) == id && HasDevice(p))
      return std::string(T("Jugador ", "Player ")) + std::to_string(i + 1);
  }
  return {};
}

std::string MicLevelText() {
  const float lv = std::clamp(re_wor::input::MicLevel(g.slot), 0.f, 1.f);
  const int n = static_cast<int>(lv * 30 + 0.5f);
  return std::string(T("Nivel:  ", "Level:  ")) + std::string(n, '|') + std::string(30 - n, '.');
}

std::string GainText(float gain) {
  char b[64];
  std::snprintf(b, sizeof b, "%s:  < %.2fx >", T("Volumen", "Volume"), gain);
  return b;
}

std::string KeyRowText(int action) {
  PlayerSlotConfig p = Slot(g.slot);
  int count = 0;
  const Label* labels = nullptr;
  std::string* keys = KeysFor(p, g.inst, &count, &labels);
  std::string label = g_es ? labels[action].es : labels[action].en;
  const auto ks = SplitKeys(keys[action]);
  std::string v = ks.empty() ? T("(sin tecla)", "(unbound)") : "";
  for (size_t i = 0; i < ks.size(); i++) v += (i ? " / " : "") + ks[i];
  return label + ":   " + v;
}

const char* PartName(Instrument i) {
  switch (i) {
    case Instrument::kBass: return "bass";
    case Instrument::kDrums: return "drum";
    case Instrument::kVocals: return "vocals";
    default: return "guitar";
  }
}

std::string PlayerName(int slot) { return std::string(T("Jugador ", "Player ")) + std::to_string(slot + 1); }

// recorta un nombre largo (en letras, sin partir una UTF-8) para que quepa en su sitio
std::string Shorten(const std::string& s, size_t max_chars) {
  size_t chars = 0, i = 0;
  while (i < s.size() && chars < max_chars) {
    i++;
    while (i < s.size() && (static_cast<unsigned char>(s[i]) & 0xC0) == 0x80) i++;
    chars++;
  }
  return i < s.size() ? s.substr(0, i) + "..." : s;
}

// ---------------------------------------------------------------------------- piezas
Item Focus(int x, int y, int w, int h, int style, const std::string& text, Act act, int arg = 0) {
  Item it;
  it.x = x; it.y = y; it.w = w; it.h = h; it.style = style; it.text = text;
  it.focusable = true; it.act = act; it.arg = arg;
  return it;
}
Item Decor(int x, int y, int w, int h, int style, const std::string& text, const std::string& sub = {}) {
  Item it;
  it.x = x; it.y = y; it.w = w; it.h = h; it.style = style; it.text = text; it.sub = sub;
  return it;
}

// texto de una fila, recortado a lo que cabe en su ancho (letra de ~12.5 px a la escala de las filas)
std::string Fit(const Item& it) {
  if (it.style != kRow && it.style != kInfo) return it.text;
  const int pad = it.icon ? 50 : 20;
  return Shorten(it.text, static_cast<size_t>(std::max(8, (it.w - pad) * 2 / 25)));
}

std::string ItemCommand(int idx, const Item& it) {
  std::string s = "re_ctl_item idx=" + std::to_string(idx) + " x=" + std::to_string(it.x) + " y=" + std::to_string(it.y) +
                  " w=" + std::to_string(it.w) + " h=" + std::to_string(it.h) + " style=" + std::to_string(it.style) +
                  " text=\"" + GameText(Fit(it), 100) + "\" sub=\"" + GameText(it.sub, 40) + "\"";
  if (it.icon) s += " icon=" + Hex(it.icon);
  if (it.img) s += " img=" + Hex(it.img) + " imgw=" + std::to_string(it.imgw);
  return s;
}

void Show(const std::string& title, int focus) {
  TRACE("[Controles] vista " << static_cast<int>(g.step) << " (" << title << "): " << g.rows.size() << " opciones");
  Run("re_ctl_begin title=\"" + GameText(title, 50) + "\"");
  g.pending.clear();
  g.building.clear();
  g.pending_at = Clock::now() + std::chrono::milliseconds(80);
  g.level_item = g.gain_row = -1;
  g.building.push_back("re_ctl_add n=" + std::to_string(std::min<size_t>(g.rows.size(), kMaxOptions)));
  for (size_t i = 0; i < g.rows.size() && i < kMaxOptions; i++) {
    g.building.push_back(ItemCommand(static_cast<int>(i), g.rows[i]));
    if (g.rows[i].act == Act::kMicGain) g.gain_row = static_cast<int>(i);
  }
  const int first_decor = static_cast<int>(std::min<size_t>(g.rows.size(), kMaxOptions));
  for (size_t i = 0; i < g.decor.size() && first_decor + static_cast<int>(i) < kMaxItems; i++) {
    g.building.push_back(ItemCommand(first_decor + static_cast<int>(i), g.decor[i]));
    if (g.decor[i].arg == 1) g.level_item = first_decor + static_cast<int>(i);
  }
  g.building.push_back("re_ctl_end");
  g.pending.push_back("re_ctl_focus focus=" +
                      std::to_string(std::clamp(focus, 0, std::max(0, static_cast<int>(g.rows.size()) - 1))));
}

void Begin(Step step) {
  g.step = step;
  g.rows.clear();
  g.decor.clear();
}

// ---------------------------------------------------------------------------- vistas
// Pantalla del juego: 1280 x 720. Tarjetas de 250 x 340 con 50 de separacion, centradas.
constexpr int kCardX0 = 65, kCardStep = 300, kCardY = 130, kCardW = 250, kCardH = 340;

void BuildLobby(int focus) {
  Begin(Step::kLobby);
  for (int i = 0; i < kMaxPlayers; i++) {
    const PlayerSlotConfig p = Slot(i);
    const Instrument inst = InstrumentFromId(p.instrument);
    Item it = Focus(kCardX0 + i * kCardStep, kCardY, kCardW, kCardH, kCard, PlayerName(i), Act::kPlayer, i);
    const std::string dev = HasDevice(p) ? PlayerSummary(p) : T("sin dispositivo", "no device");
    it.sub = std::string(InstrumentName(inst)) + ": " + dev;
    if (it.sub.size() > 30) {   // cabe en la tarjeta (sin partir una letra UTF-8)
      size_t cut = 28;
      while (cut > 0 && (static_cast<unsigned char>(it.sub[cut]) & 0xC0) == 0x80) --cut;
      it.sub = it.sub.substr(0, cut) + "...";
    }
    it.img = InstrumentPicture(inst);
    it.imgw = 170;
    g.rows.push_back(it);
  }
  // abajo a la izquierda el juego pone su cuadro de consejos: Listo va a la derecha
  g.rows.push_back(Focus(955, 540, 260, 50, kButton, T("Listo", "Done"), Act::kLobbyDone));
  g.decor.push_back(Decor(kCardX0, 492, 1150, 30, kInfo,
                          T("Elige un jugador: instrumento, control y teclas.", "Pick a player: instrument, controller and keys.")));
  Show(T("Controles", "Controls"), focus);
}

void BuildInstrument() {
  Begin(Step::kInstrument);
  for (int i = 0; i < static_cast<int>(Instrument::kCount); i++) {
    const Instrument inst = static_cast<Instrument>(i);
    Item it = Focus(kCardX0 + i * kCardStep, kCardY, kCardW, kCardH, kCard, InstrumentName(inst), Act::kInstrument, i);
    it.img = InstrumentPicture(inst);
    it.imgw = 190;
    g.rows.push_back(it);
  }
  g.decor.push_back(Decor(kCardX0, 492, 1150, 30, kInfo,
                          T("Varios jugadores pueden tocar el mismo instrumento, cada uno con su propio control.",
                            "Several players can play the same instrument, each with their own controller.")));
  Show(PlayerName(g.slot) + ": " + T("elige instrumento", "pick an instrument"), static_cast<int>(g.inst));
}

// imagen del instrumento a la izquierda, con el jugador y su dispositivo debajo
void AddInstrumentPicture(const PlayerSlotConfig& p) {
  Item pic = Decor(80, 140, 320, 320, kImage, PlayerName(g.slot) + "  -  " + InstrumentName(g.inst),
                   HasDevice(p) ? PlayerSummary(p) : T("sin dispositivo", "no device"));
  pic.img = InstrumentPicture(g.inst);
  g.decor.push_back(pic);
}

constexpr int kRightX = 480, kRightW = 740, kHalfW = 360, kColStep = 380;

// opciones "buscar en la lista" y "detectar" de un dispositivo, en la fila y
void AddDeviceSection(const PlayerSlotConfig& p, const std::string& header, int y) {
  g.decor.push_back(Decor(kRightX, y, kRightW, 32, kHeader, header));
  const std::string dev = DeviceSummary(p);
  g.decor.push_back(Decor(kRightX, y + 36, kRightW, 26, kInfo,
                          std::string(T("Actual:  ", "Current:  ")) + (dev.empty() ? T("ninguno", "none") : Shorten(dev, 52))));
  g.rows.push_back(Focus(kRightX, y + 66, kHalfW, 34, kRow, T("Buscar en la lista", "Pick from the list"), Act::kListDevices));
  g.rows.push_back(Focus(kRightX + kColStep, y + 66, kHalfW, 34, kRow,
                         T("Detectar: pulsa un boton", "Detect: press a button"), Act::kListen));
}

// mapeo en dos columnas de 7
void AddMapping(const PlayerSlotConfig& p, const std::string& header, int y, int step) {
  g.decor.push_back(Decor(kRightX, y, kRightW, 32, kHeader, header));
  if (p.keyboard.empty()) {
    g.decor.push_back(Decor(kRightX, y + 40, kRightW, 26, kInfo,
                            p.pad.empty() ? T("Primero elige o detecta un dispositivo.", "First pick or detect a device.")
                                          : T("Los mandos y guitarras USB usan sus propios botones.",
                                              "USB controllers and guitars use their own buttons.")));
    return;
  }
  PlayerSlotConfig tmp = p;
  int count = 0;
  const Label* labels = nullptr;
  KeysFor(tmp, g.inst, &count, &labels);
  for (int k = 0; k <= count; k++) {
    const int col = k / 7, row = k % 7;
    const int x = kRightX + col * kColStep, yy = y + 40 + row * step;
    if (k < count) g.rows.push_back(Focus(x, yy, kHalfW, step - 2, kRow, KeyRowText(k), Act::kKey, k));
    else g.rows.push_back(Focus(x, yy, kHalfW, step - 2, kRow, T("Restaurar teclas de fabrica", "Restore default keys"),
                                Act::kResetKeys));
  }
}

void AddConfigButtons(int y) {
  // a la derecha: abajo a la izquierda el juego pone su cuadro de consejos
  g.rows.push_back(Focus(690, y, 240, 44, kButton, T("Prueba", "Test"), Act::kTest));
  g.rows.push_back(Focus(960, y, 240, 44, kButton, T("Listo", "Done"), Act::kConfigDone));
}

int FindRow(Act act, int arg = -1) {
  for (int i = 0; i < static_cast<int>(g.rows.size()); i++)
    if (g.rows[i].act == act && (arg < 0 || g.rows[i].arg == arg)) return i;
  return 0;
}

void BuildConfig(Act focus_act = Act::kListDevices) {
  Begin(Step::kConfig);
  const PlayerSlotConfig p = Slot(g.slot);
  AddInstrumentPicture(p);
  if (g.inst == Instrument::kVocals) {
    g.decor.push_back(Decor(kRightX, 108, kRightW, 32, kHeader, T("1.  Microfono", "1.  Microphone")));
    const std::string mic = MicSummary(p);
    g.decor.push_back(Decor(kRightX, 144, kRightW, 26, kInfo,
                            std::string(T("Actual:  ", "Current:  ")) + (mic.empty() ? T("ninguno", "none") : Shorten(mic, 52))));
    g.rows.push_back(Focus(kRightX, 172, kHalfW, 34, kRow, T("Elegir microfono", "Pick microphone"), Act::kListMics));
    g.rows.push_back(Focus(kRightX + kColStep, 172, kHalfW, 34, kRow, GainText(p.mic_gain), Act::kMicGain));
    Item lv = Decor(kRightX, 210, kRightW, 24, kInfo, MicLevelText());
    lv.arg = 1;
    g.decor.push_back(lv);
    AddDeviceSection(p, T("2.  Control", "2.  Controller"), 240);
    AddMapping(p, T("3.  Mapeo", "3.  Mapping"), 346, 26);
    AddConfigButtons(588);
  } else {
    AddDeviceSection(p, T("1.  Dispositivo", "1.  Device"), 112);
    AddMapping(p, T("2.  Mapeo", "2.  Mapping"), 230, 36);
    AddConfigButtons(560);
  }
  Show(PlayerName(g.slot) + ": " + InstrumentName(g.inst), FindRow(focus_act));
}

void BuildDeviceList() {
  Begin(Step::kDeviceList);
  const PlayerSlotConfig p = Slot(g.slot);
  AddInstrumentPicture(p);
  g.decor.push_back(Decor(kRightX, 112, kRightW, 32, kHeader, T("Elige un dispositivo", "Pick a device")));
  int focus = 0, y = 152;
  for (const auto& k : re_wor::input::ListKeyboards()) {
    std::string t = std::string(T("Teclado:  ", "Keyboard:  ")) + k.name;
    const std::string owner = OwnerOf(k.path, false);
    if (!owner.empty()) t += "  (" + owner + ")";
    if (p.keyboard == k.path) focus = static_cast<int>(g.rows.size());
    Item it = Focus(kRightX, y, kRightW, 34, kRow, t, Act::kKeyboard);
    it.id = k.path; it.name = k.name;
    g.rows.push_back(it);
    y += 38;
  }
  for (const auto& d : re_wor::input::ListPads()) {
    std::string t = std::string(T("Mando:  ", "Controller:  ")) + d.name;
    const std::string owner = OwnerOf(d.guid, true);
    if (!owner.empty()) t += "  (" + owner + ")";
    if (p.pad == d.guid && p.keyboard.empty()) focus = static_cast<int>(g.rows.size());
    Item it = Focus(kRightX, y, kRightW, 34, kRow, t, Act::kPad);
    it.id = d.guid; it.name = d.name;
    g.rows.push_back(it);
    y += 38;
  }
  g.rows.push_back(Focus(kRightX, y, kRightW, 34, kRow, T("Ninguno", "None"), Act::kNoDevice));
  Show(PlayerName(g.slot) + ": " + InstrumentName(g.inst), focus);
}

void BuildMicList() {
  Begin(Step::kMicList);
  const PlayerSlotConfig p = Slot(g.slot);
  AddInstrumentPicture(p);
  g.decor.push_back(Decor(kRightX, 112, kRightW, 32, kHeader, T("Elige un microfono", "Pick a microphone")));
  int focus = 0, y = 152;
  auto add = [&](const std::string& text, const std::string& id, const std::string& name) {
    if (p.mic == id) focus = static_cast<int>(g.rows.size());
    Item it = Focus(kRightX, y, kRightW, 34, kRow, text, Act::kMic);
    it.id = id; it.name = name;
    g.rows.push_back(it);
    y += 38;
  };
  add(T("Sin microfono", "No microphone"), "", "");
  add(T("Microfono predeterminado de Windows", "Windows default microphone"), "default",
      T("Microfono predeterminado", "Default microphone"));
  for (const auto& m : re_wor::input::ListMics()) add(m.name, m.id, m.name);
  Show(PlayerName(g.slot) + ": " + InstrumentName(g.inst), focus);
}

// ---------------------------------------------------------------------------- acciones
void CancelWaits() {
  auto* kb = KeyboardGuitarDriver::Instance();
  if (g.capture_row >= 0) {
    if (kb) kb->CancelKeyCapture();
    if (g.capture_row < static_cast<int>(g.rows.size())) SetItemText(g.capture_row, KeyRowText(g.rows[g.capture_row].arg));
    g.capture_row = -1;
  }
  if (g.listen_row >= 0) {
    if (kb) kb->CancelKeyboardDetect();
    if (g.listen_row < static_cast<int>(g.rows.size())) SetItemText(g.listen_row, g.rows[g.listen_row].text);
    g.listen_row = -1;
  }
}

void AssignKeyboard(const std::string& path, const std::string& name) {
  {
    std::lock_guard lock(Mutex());
    auto& pl = Get().input.players;
    for (int i = 0; i < kMaxPlayers; i++)
      if (i != g.slot && pl[i].keyboard == path) { pl[i].keyboard.clear(); pl[i].keyboard_name.clear(); }
    auto& p = pl[g.slot];
    p.instrument = InstrumentId(g.inst);
    p.keyboard = path;
    p.keyboard_name = name;
    p.pad.clear();
    p.pad_name.clear();
  }
  SaveAndApply(false);
  BuildConfig(Act::kKey);
}

void AssignPad(const std::string& guid, const std::string& name) {
  {
    std::lock_guard lock(Mutex());
    auto& pl = Get().input.players;
    for (int i = 0; i < kMaxPlayers; i++)
      if (i != g.slot && pl[i].pad == guid) { pl[i].pad.clear(); pl[i].pad_name.clear(); }
    auto& p = pl[g.slot];
    p.instrument = InstrumentId(g.inst);
    p.pad = guid;
    p.pad_name = name;
    p.keyboard.clear();
    p.keyboard_name.clear();
  }
  SaveAndApply(false);
  BuildConfig(Act::kTest);
}

void Back() {
  Run("re_ctl_sfx_back");
  switch (g.step) {
    case Step::kLobby: Run("re_ctl_exit"); break;
    case Step::kInstrument: BuildLobby(g.slot); break;
    case Step::kConfig: BuildInstrument(); break;
    case Step::kDeviceList: BuildConfig(Act::kListDevices); break;
    case Step::kMicList: BuildConfig(Act::kListMics); break;
  }
}

void Choose(int row) {
  TRACE("[Controles] elegir opcion " << row << " de " << g.rows.size());
  if (row < 0 || row >= static_cast<int>(g.rows.size())) return;
  const Item r = g.rows[row];
  auto* kb = KeyboardGuitarDriver::Instance();
  switch (r.act) {
    case Act::kPlayer:
      g.slot = r.arg;
      g.inst = InstrumentFromId(Slot(g.slot).instrument);
      BuildInstrument();
      break;
    case Act::kLobbyDone:
      Save();
      Run("re_ctl_exit");
      break;
    case Act::kInstrument:
      g.inst = static_cast<Instrument>(r.arg);
      {
        std::lock_guard lock(Mutex());
        Get().input.players[g.slot].instrument = InstrumentId(g.inst);
      }
      SaveAndApply(false);
      BuildConfig(g.inst == Instrument::kVocals ? Act::kListMics : Act::kListDevices);
      break;
    case Act::kListDevices: BuildDeviceList(); break;
    case Act::kListMics: BuildMicList(); break;
    case Act::kListen:
      if (kb) kb->BeginKeyboardDetect();
      re_wor::input::BeginPadDetect();
      g.listen_row = row;
      g.wait_until = Clock::now() + std::chrono::seconds(15);
      SetItemText(row, T("Pulsa un boton en el control...", "Press a button on the controller..."));
      break;
    case Act::kMic: {
      {
        std::lock_guard lock(Mutex());
        auto& p = Get().input.players[g.slot];
        p.instrument = InstrumentId(g.inst);
        p.mic = r.id;
        p.mic_name = r.name;
      }
      SaveAndApply(true);
      BuildConfig(Act::kListDevices);
      break;
    }
    case Act::kMicGain: break;
    case Act::kKeyboard: AssignKeyboard(r.id, r.name); break;
    case Act::kPad: AssignPad(r.id, r.name); break;
    case Act::kNoDevice: {
      {
        std::lock_guard lock(Mutex());
        auto& p = Get().input.players[g.slot];
        p.keyboard.clear();
        p.keyboard_name.clear();
        p.pad.clear();
        p.pad_name.clear();
      }
      SaveAndApply(false);
      BuildConfig(Act::kListDevices);
      break;
    }
    case Act::kKey: {
      if (!kb) break;
      kb->BeginKeyCapture();
      g.capture_row = row;
      g.wait_until = Clock::now() + std::chrono::seconds(10);
      const Label* labels = nullptr;
      int count = 0;
      PlayerSlotConfig p = Slot(g.slot);
      KeysFor(p, g.inst, &count, &labels);
      SetItemText(row, std::string(g_es ? labels[r.arg].es : labels[r.arg].en) + ":   " +
                           T("pulsa una tecla  (Esc cancela)", "press a key  (Esc cancels)"));
      break;
    }
    case Act::kResetKeys: {
      {
        std::lock_guard lock(Mutex());
        ResetPlayerKeys(Get().input.players[g.slot], g.inst);
      }
      SaveAndApply(false);
      for (int i = 0; i < static_cast<int>(g.rows.size()); i++)
        if (g.rows[i].act == Act::kKey) SetItemText(i, KeyRowText(g.rows[i].arg));
      break;
    }
    case Act::kTest: {
      Save();
      const std::string cmd = std::string("re_ctl_test part=") + PartName(g.inst) + " controller=" + std::to_string(g.slot);
      TRACE("[Controles] prueba: " << cmd);
      Run(cmd);
      break;
    }
    case Act::kConfigDone: BuildLobby(g.slot); break;
    default: break;
  }
}

// izquierda / derecha: entre tarjetas, entre las dos columnas, entre botones; en el volumen, lo cambia
void Side(int row, int dir) {
  if (row < 0 || row >= static_cast<int>(g.rows.size())) return;
  if (row == g.gain_row) {
    float gain = 1.f;
    {
      std::lock_guard lock(Mutex());
      auto& p = Get().input.players[g.slot];
      p.mic_gain = std::clamp(p.mic_gain + dir * 0.25f, 0.25f, 4.f);
      gain = p.mic_gain;
    }
    SaveAndApply(true);
    Run("re_ctl_sfx_select");
    SetItemText(row, GainText(gain));
    return;
  }
  const Item& cur = g.rows[row];
  // la opcion mas cercana en esa direccion, en la misma altura
  int best = -1, best_dx = 1 << 30;
  for (int i = 0; i < static_cast<int>(g.rows.size()); i++) {
    if (i == row) continue;
    const Item& o = g.rows[i];
    if (std::abs(o.y - cur.y) > 8) continue;
    const int dx = (o.x - cur.x) * dir;
    if (dx > 0 && dx < best_dx) { best_dx = dx; best = i; }
  }
  if (best < 0) return;
  Run("re_ctl_focus focus=" + std::to_string(best));
}

void PollWaits() {
  auto* kb = KeyboardGuitarDriver::Instance();
  if (!kb) return;
  if (g.listen_row >= 0) {
    const std::string path = kb->PollDetectedKeyboard();
    if (!path.empty()) {
      g.listen_row = -1;
      Run("re_ctl_sfx_select");
      AssignKeyboard(path, re_wor::input::KeyboardNameForPath(path));
      return;
    }
    re_wor::input::PadInfo pad;
    if (re_wor::input::PollPadDetect(&pad)) {
      g.listen_row = -1;
      kb->CancelKeyboardDetect();
      Run("re_ctl_sfx_select");
      AssignPad(pad.guid, pad.name);
      return;
    }
    if (Clock::now() > g.wait_until) CancelWaits();
  }
  if (g.capture_row >= 0) {
    std::string path;
    const uint16_t vk = kb->PollCapturedKey(&path);
    if (!vk) {
      if (Clock::now() > g.wait_until) CancelWaits();
      return;
    }
    const int row = g.capture_row;
    const int action = g.rows[row].arg;
    if (vk == 0x1B) {   // Esc: cancelar
      g.capture_row = -1;
      SetItemText(row, KeyRowText(action));
      Run("re_ctl_sfx_back");
      return;
    }
    const PlayerSlotConfig cur = Slot(g.slot);
    if (!cur.keyboard.empty() && cur.keyboard != "*" && !path.empty() && path != cur.keyboard) {
      // la tecla vino de otro teclado: se sigue esperando en el del jugador
      kb->BeginKeyCapture();
      g.wait_until = Clock::now() + std::chrono::seconds(10);
      SetItemText(row, std::string(T("Pulsala en: ", "Press it on: ")) + DeviceSummary(cur));
      return;
    }
    const std::string key = re_wor::input::VkToKeyName(vk);
    {
      std::lock_guard lock(Mutex());
      auto& p = Get().input.players[g.slot];
      int count = 0;
      const Label* labels = nullptr;
      std::string* keys = KeysFor(p, g.inst, &count, &labels);
      keys[action] = key;
      // la misma tecla en otra accion de este instrumento: se le quita alli
      for (int a = 0; a < count; a++) {
        if (a == action) continue;
        auto other = SplitKeys(keys[a]);
        auto it = std::find(other.begin(), other.end(), key);
        if (it != other.end()) {
          other.erase(it);
          keys[a] = JoinKeys(other);
        }
      }
    }
    g.capture_row = -1;
    SaveAndApply(false);
    Run("re_ctl_sfx_select");
    for (int i = 0; i < static_cast<int>(g.rows.size()); i++)
      if (g.rows[i].act == Act::kKey) SetItemText(i, KeyRowText(g.rows[i].arg));
  }
}

}  // namespace

void RequestOpen() { g_open_request = true; }

// Aviso "no tienes controles": el jugador 1 no tiene ningun control y el teclado hace de mando.
void TickNoControls(uint8_t* base) {
  static Clock::time_point next{};
  static bool fallback = false;
  if (Clock::now() < next) return;
  next = Clock::now() + std::chrono::seconds(2);
  bool nobody = false;
  re_wor::input::UpdatePlayerOneFallback(&nobody, false);
  const bool now_fallback = nobody;   // el aviso solo si nadie tiene ningun control
  if (!now_fallback) {
    if (fallback) qb_bridge::ExecuteScriptQuiet("re_noctl_reset");
    fallback = false;
    return;
  }
  fallback = true;
  int shown = 1;
  if (!qb_bridge::ReadIntGlobal("re_noctl_shown", base, &shown) || shown) return;
  const std::string heading = T("No tienes controles activos", "No active controllers");
  const std::string body = T("Ningun jugador tiene un control configurado. Mientras tanto el teclado hace de mando: "
                             "flechas para moverte, Enter para aceptar y Esc para volver. Configuralos en Controls.",
                             "No player has a controller set up. Meanwhile the keyboard works as a gamepad: arrows to "
                             "move, Enter to accept and Esc to go back. Set them up in Controls.");
  qb_bridge::ExecuteScriptQuiet("re_noctl_try heading=\"" + GameText(heading, 60) + "\" body=\"" + GameText(body, 300) +
                                "\" b1=\"" + GameText(T("Configurar controles", "Set up controls"), 40) + "\" b2=\"" +
                                GameText(T("Ahora no", "Not now"), 40) + "\"");
}

void Tick(uint8_t* base) {
  if (!base) return;
  TickNoControls(base);
  if (g_open_request.exchange(false)) qb_bridge::ExecuteScript("re_ctl_open");
  int active = 0;
  if (!qb_bridge::ReadIntGlobal("re_ctl_active", base, &active)) return;
  if (!active) {
    if (g.active) {
      g.active = false;
      CancelWaits();
      ApplyBindingsNow();
    }
    return;
  }
  int session = 0;
  qb_bridge::ReadIntGlobal("re_ctl_session", base, &session);
  if (!g.active || session != g.session) {   // la pantalla se acaba de abrir
    if (g.active) CancelWaits();
    g.active = true;
    g.session = session;
    g.pending.clear();
    g.building.clear();
    // la fila provisional tiene que estar ya en el menu para poder quitarla
    g.opening = true;
    g.opened_at = Clock::now() + std::chrono::milliseconds(400);
    return;
  }
  if (g.opening) {
    if (Clock::now() < g.opened_at) return;
    g.opening = false;
    g_es = Get().game.language == "es";
    g.capture_row = g.listen_row = -1;
    BuildLobby(0);
    return;
  }
  // foco pendiente del ultimo paso (ver State::pending); mientras tanto no se atiende nada mas
  if (!g.building.empty()) {
    if (Clock::now() < g.pending_at) return;
    for (const auto& s : g.building) Run(s);
    g.building.clear();
    g.pending_at = Clock::now() + std::chrono::milliseconds(150);
    return;
  }
  if (!g.pending.empty()) {
    if (Clock::now() < g.pending_at) return;
    for (const auto& s : g.pending) Run(s);
    g.pending.clear();
    return;
  }
  PollWaits();
  static Clock::time_point keepalive{};
  if (Clock::now() > keepalive) {
    keepalive = Clock::now() + std::chrono::seconds(2);
    Run("re_ctl_keepalive");
  }
  // nivel del microfono en vivo
  if (g.level_item >= 0 && (++g.tick % 6) == 0) SetItemText(g.level_item, MicLevelText());
  int cmd = 0;
  if (!qb_bridge::ReadIntGlobal("re_ctl_cmd", base, &cmd) || cmd == 0) return;
  qb_bridge::WriteIntGlobal("re_ctl_cmd", base, 0);
  const bool waiting = g.capture_row >= 0 || g.listen_row >= 0;
  if (waiting) {   // otro control se movio mientras se esperaba una tecla: se cancela la espera
    CancelWaits();
    if (cmd == 1000) return;
  }
  if (cmd == 1000) Back();
  else if (cmd >= 3000) Side(cmd - 3000, 1);
  else if (cmd >= 2000) Side(cmd - 2000, -1);
  else Choose(cmd - 1);
}

}  // namespace re_wor::de::controls
