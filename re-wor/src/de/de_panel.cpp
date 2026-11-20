#include "de/de_panel.hpp"
#include "de/controls_menu.hpp"

#include <rex/input/input.h>
#include <rex/input/input_system.h>
#include <rex/runtime.h>

#include <imgui.h>

#include <algorithm>
#include <numeric>
#include <vector>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "core/crash_handler.hpp"
#include "core/path_resolver.hpp"
#include "de/de_config.hpp"
#include "de/de_hooks.hpp"
#include "de/edfx.hpp"
#include "de/gfx_options.hpp"
#include "de/render_sandbox.hpp"
#include "de/qb_bridge.hpp"
#include "input/key_names.hpp"
#include "input/devices.hpp"
#include "input/keyboard_guitar_driver.hpp"
#include "input/mic_capture.hpp"

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif

namespace re_wor::de {

namespace {
void Hint(const char* text);   // mas abajo

constexpr int kActionCount = static_cast<int>(GuitarAction::kCount);

const ImVec4 kFretColors[] = {
    ImVec4(0.15f, 0.80f, 0.25f, 1), ImVec4(0.90f, 0.15f, 0.15f, 1),
    ImVec4(0.95f, 0.85f, 0.10f, 1), ImVec4(0.15f, 0.45f, 0.95f, 1),
    ImVec4(1.00f, 0.55f, 0.05f, 1),
};

const char* kModTypes[] = {"music", "song", "skin", "highway", "character", "ui", "other"};
const char* kModTypeLabels[] = {"Musica (reemplazo de audio)", "Cancion (audio + chart)",
                                "Skin general",                "Highway / gemas",
                                "Personaje",                   "Interfaz / menus",
                                "Otro"};

void OpenInExplorer(const std::filesystem::path& p) {
#ifdef _WIN32
  std::error_code ec;
  std::filesystem::create_directories(p.has_extension() ? p.parent_path() : p, ec);
  ShellExecuteW(nullptr, L"open", p.wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#else
  (void)p;
#endif
}

std::string HumanBytes(uint64_t b) {
  char buf[32];
  if (b >= (1ull << 30)) std::snprintf(buf, sizeof(buf), "%.2f GB", b / double(1ull << 30));
  else if (b >= (1ull << 20)) std::snprintf(buf, sizeof(buf), "%.1f MB", b / double(1ull << 20));
  else if (b >= 1024) std::snprintf(buf, sizeof(buf), "%.1f KB", b / 1024.0);
  else std::snprintf(buf, sizeof(buf), "%" PRIu64 " B", b);
  return buf;
}

const char* SubtypeName(uint8_t st) {
  switch (st) {
    case 0x01: return "Mando";
    case 0x02: return "Volante";
    case 0x03: return "Arcade stick";
    case 0x04: return "Flight stick";
    case 0x05: return "Dance pad";
    case 0x06: return "Guitarra";
    case 0x07: return "Guitarra (alt.)";
    case 0x08: return "Bateria";
    case 0x0B: return "Bajo";
    case 0x0F: return "Arcade pad";
    default: return "Desconocido";
  }
}

void FretLight(const char* label, bool on, const ImVec4& color) {
  ImVec4 c = on ? color : ImVec4(color.x * 0.25f, color.y * 0.25f, color.z * 0.25f, 1);
  ImGui::PushStyleColor(ImGuiCol_Button, c);
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, c);
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, c);
  ImGui::Button(label, ImVec2(44, 28));
  ImGui::PopStyleColor(3);
}

// Lectura protegida: una direccion del guest no reservada no debe tumbar el juego.
bool SafeRead(const uint8_t* src, uint8_t* dst, size_t n) {
#ifdef _WIN32
  __try {
    for (size_t i = 0; i < n; i++) dst[i] = src[i];
    return true;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
#else
  std::memcpy(dst, src, n);
  return true;
#endif
}

DePanelDialog* g_panel = nullptr;

}  // namespace

void DePanelDialog::Toggle(rex::ui::ImGuiDrawer* drawer, rex::Runtime* runtime, int select_tab) {
  if (g_panel) {
    if (select_tab >= 0 && g_panel->selected_tab_ != select_tab) {
      g_panel->selected_tab_ = select_tab;
      return;
    }
    delete g_panel;  // el destructor quita el dialogo del drawer
    return;
  }
  if (drawer) {
    auto* panel = new DePanelDialog(drawer, runtime);
    panel->selected_tab_ = select_tab;
  }
}

bool DePanelDialog::IsOpen() {
  return g_panel != nullptr;
}

DePanelDialog::DePanelDialog(rex::ui::ImGuiDrawer* drawer, rex::Runtime* runtime)
    : ImGuiDialog(drawer), runtime_(runtime) {
  g_panel = this;
  re_wor::input::KeyboardGuitarDriver::SetSuppressed(true);
  if (runtime_) {
    if (auto* in = dynamic_cast<rex::input::InputSystem*>(runtime_->input_system())) {
      in->AddUIInputBlocker();
      blocking_input_ = true;
    }
  }
  RefreshMods();
}

DePanelDialog::~DePanelDialog() {
  if (g_panel == this) g_panel = nullptr;
  if (auto* kb = re_wor::input::KeyboardGuitarDriver::Instance()) kb->CancelKeyCapture();
  re_wor::input::KeyboardGuitarDriver::SetSuppressed(false);
  if (blocking_input_ && runtime_) {
    if (auto* in = dynamic_cast<rex::input::InputSystem*>(runtime_->input_system())) {
      in->RemoveUIInputBlocker();
    }
  }
  if (controls_dirty_) Save();
}

void DePanelDialog::RefreshMods() {
  mods_ = ModManager::Instance().List();
}

void DePanelDialog::OnDraw(ImGuiIO& io) {
  ImGui::SetNextWindowSize(ImVec2(760, 560), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f),
                          ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
  bool open = true;
  const std::string title = "Guitar Hero: Warriors of Rock - Definitive Edition  [" +
                            HotkeyOr(Get().hotkeys.panel, "F8") + "]###de_panel";
  if (!ImGui::Begin(title.c_str(), &open,
                    ImGuiWindowFlags_NoCollapse)) {
    ImGui::End();
    if (!open) Close();
    return;
  }

  ImGui::TextDisabled("FPS del juego: %.1f   |   Frames: %" PRIu64 "   |   El juego no recibe "
                      "entrada mientras este panel esta abierto.",
                      GameFps(), GameFrameCount());
  if (restart_needed_) {
    ImGui::TextColored(ImVec4(1, 0.7f, 0.2f, 1),
                       "Cambios en mods guardados: se aplican al reiniciar el juego.");
  }
  if (!status_.empty()) ImGui::TextColored(ImVec4(0.5f, 0.9f, 0.5f, 1), "%s", status_.c_str());
  ImGui::Separator();

  if (ImGui::BeginTabBar("de_tabs")) {
    ImGuiTabItemFlags tab_flags[6] = {};
    if (selected_tab_ >= 0 && selected_tab_ < 6) {
      tab_flags[selected_tab_] = ImGuiTabItemFlags_SetSelected;
      selected_tab_ = -1;
    }

    if (ImGui::BeginTabItem("Controles", nullptr, tab_flags[0])) {
      DrawControlsTab();
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Graficos", nullptr, tab_flags[5])) {
      DrawGraphicsTab();
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Mods", nullptr, tab_flags[1])) {
      DrawModsTab();
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Musica custom", nullptr, tab_flags[2])) {
      DrawContentTab("music", "song",
                     "Reemplaza canciones existentes: el audio va en data/music/<cancion>_1.fsb.xen "
                     "(tambien _2, _3 y _preview) y el chart en data/songs/<Cancion>_song.pak.xen.\n"
                     "Usa el MISMO nombre de archivo que la cancion original que quieres sustituir. "
                     "Los archivos se generan con herramientas de la comunidad (p. ej. Onyx).");
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Skins", nullptr, tab_flags[3])) {
      DrawContentTab("skin", "highway",
                     "Skins de highway, gemas, personajes o interfaz: copia el .pak.xen / .img.xen "
                     "modificado con la misma ruta que el original (data/pak/..., data/zones/..., "
                     "data/ui_images/...).\nTambien se aceptan los tipos 'character' y 'ui'.");
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Dev", nullptr, tab_flags[4])) {
      DrawDevTab();
      ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
  }
  ImGui::End();
  if (!open) Close();
}

// -----------------------------------------------------------------------------
// Controles
// -----------------------------------------------------------------------------

void DePanelDialog::DrawControlsTab() {
  auto* kb = re_wor::input::KeyboardGuitarDriver::Instance();
  auto& cfg = Get();

  // Captura de tecla en curso.
  if (capturing_action_ >= 0 && kb) {
    if (uint16_t vk = kb->PollCapturedKey()) {
      std::lock_guard lock(Mutex());
      std::string name = re_wor::input::VkToKeyName(vk);
      std::string& keys = cfg.input.players[0].guitar_keys[capturing_action_];
      if (vk == 0x1B && !capture_append_) {
        // Escape sin "anadir" = cancelar
      } else if (capture_append_ && !keys.empty()) {
        keys += "," + name;
      } else {
        keys = name;
      }
      capturing_action_ = -1;
      controls_dirty_ = true;
    }
  }
  if (controls_dirty_ && kb) kb->ReloadBindings();

  {
    std::lock_guard lock(Mutex());
    cfg.input.keyboard_guitar = !cfg.input.players[0].keyboard.empty();
    if (ImGui::Checkbox("Usar el teclado como guitarra (jugador 1)", &cfg.input.keyboard_guitar)) {
      // jugador 1: cualquier teclado libre, o ninguno (la pantalla Controles elige uno concreto)
      if (!cfg.input.keyboard_guitar) cfg.input.players[0].keyboard.clear();
      else if (cfg.input.players[0].keyboard.empty()) cfg.input.players[0].keyboard = "*";
      controls_dirty_ = true;
    }
    ImGui::SameLine();
    if (ImGui::Checkbox("Teclado como mando (MnK) [reinicio]", &cfg.input.keyboard_gamepad)) {
      controls_dirty_ = true;
    }

    int backend = cfg.input.backend == "xinput" ? 1 : 0;
    const char* backends[] = {"SDL3: XInput + DirectInput + HID (recomendado)",
                              "Solo XInput nativo"};
    ImGui::SetNextItemWidth(380);
    if (ImGui::Combo("Backend de mandos [reinicio]", &backend, backends, 2)) {
      cfg.input.backend = backend == 1 ? "xinput" : "sdl";
      controls_dirty_ = true;
    }

    int st = cfg.input.keyboard_guitar_subtype == 7 ? 1 : (cfg.input.keyboard_guitar_subtype == 11 ? 2 : 0);
    const char* subtypes[] = {"Guitarra (6)", "Guitarra alternativa (7)", "Bajo (11)"};
    ImGui::SetNextItemWidth(380);
    if (ImGui::Combo("Tipo de instrumento del teclado", &st, subtypes, 3)) {
      cfg.input.keyboard_guitar_subtype = st == 1 ? 7 : (st == 2 ? 11 : 6);
      controls_dirty_ = true;
    }
  }

  ImGui::Spacing();
  ImGui::Text("Visor de entrada (teclado):");
  if (kb) {
    const char* fret_names[] = {"V", "R", "A", "Az", "N"};
    for (int i = 0; i < 5; i++) {
      if (i) ImGui::SameLine();
      ImGui::PushID(i);
      FretLight(fret_names[i], kb->IsActionDown(static_cast<GuitarAction>(i)), kFretColors[i]);
      ImGui::PopID();
    }
    ImGui::SameLine(0, 24);
    bool strum = kb->IsActionDown(GuitarAction::kStrumUp) || kb->IsActionDown(GuitarAction::kStrumDown);
    FretLight("Rasg", strum, ImVec4(0.9f, 0.9f, 0.9f, 1));
    ImGui::SameLine();
    FretLight("SP", kb->IsActionDown(GuitarAction::kStarPower) || kb->IsActionDown(GuitarAction::kTilt),
              ImVec4(0.4f, 0.9f, 1.0f, 1));
    ImGui::SameLine();
    FretLight("Wh", kb->IsActionDown(GuitarAction::kWhammy), ImVec4(0.8f, 0.4f, 1.0f, 1));
    ImGui::TextDisabled("(con el panel abierto las teclas se muestran aqui pero no llegan al juego)");
  }

  ImGui::Spacing();
  if (ImGui::BeginTable("binds", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH)) {
    ImGui::TableSetupColumn("Accion", ImGuiTableColumnFlags_WidthFixed, 170);
    ImGui::TableSetupColumn("Teclas");
    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 230);
    ImGui::TableSetupColumn("Mando", ImGuiTableColumnFlags_WidthFixed, 110);
    ImGui::TableHeadersRow();
    const char* pad_names[] = {"A",        "B",       "Y",      "X",      "LB",
                               "D-pad ^",  "D-pad v", "Start",  "Back",   "Stick D X",
                               "Stick D Y", "D-pad <", "D-pad >"};
    for (int i = 0; i < kActionCount; i++) {
      ImGui::PushID(i);
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      if (i < 5) {
        ImGui::TextColored(kFretColors[i], "%s", GuitarActionLabel(static_cast<GuitarAction>(i)));
      } else {
        ImGui::TextUnformatted(GuitarActionLabel(static_cast<GuitarAction>(i)));
      }
      ImGui::TableNextColumn();
      if (capturing_action_ == i) {
        ImGui::TextColored(ImVec4(1, 0.8f, 0.2f, 1), "Pulsa una tecla...");
      } else {
        std::lock_guard lock(Mutex());
        ImGui::TextUnformatted(cfg.input.players[0].guitar_keys[i].empty() ? "(sin asignar)" : cfg.input.players[0].guitar_keys[i].c_str());
      }
      ImGui::TableNextColumn();
      if (capturing_action_ == i) {
        if (ImGui::SmallButton("Cancelar") && kb) {
          kb->CancelKeyCapture();
          capturing_action_ = -1;
        }
      } else if (kb) {
        if (ImGui::SmallButton("Asignar")) {
          capturing_action_ = i;
          capture_append_ = false;
          kb->BeginKeyCapture();
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("+ Anadir")) {
          capturing_action_ = i;
          capture_append_ = true;
          kb->BeginKeyCapture();
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Borrar")) {
          std::lock_guard lock(Mutex());
          cfg.input.players[0].guitar_keys[i].clear();
          controls_dirty_ = true;
        }
      }
      ImGui::TableNextColumn();
      ImGui::TextDisabled("%s", pad_names[i]);
      ImGui::PopID();
    }
    ImGui::EndTable();
  }

  if (ImGui::Button("Guardar")) {
    if (Save()) status_ = "Controles guardados en " + ConfigPath().filename().string();
    controls_dirty_ = false;
  }
  ImGui::SameLine();
  if (ImGui::Button("Restaurar teclas por defecto")) {
    std::lock_guard lock(Mutex());
    ResetKeysToDefaults(cfg.input);
    ResetPlayerKeys(cfg.input.players[0], Instrument::kGuitar);
    controls_dirty_ = true;
  }
  ImGui::SameLine();
  if (ImGui::Button("Abrir re_wor_de.toml")) OpenInExplorer(ConfigPath());

  ImGui::Separator();
  DrawPlayersSection();
  DrawDevicesSection();
}

// Jugadores: que teclado, mando y microfono usa cada uno (los mismos datos que Options > Controls del juego)
void DePanelDialog::DrawPlayersSection() {
  using namespace re_wor::input;
  auto* kb = KeyboardGuitarDriver::Instance();
  auto& cfg = Get();
  if (ImGui::Button("Abrir Opciones > Controls en el juego (desde un menu)")) {
    controls::RequestOpen();
  }
  const double now = ImGui::GetTime();
  if (now - devices_listed_at_ > 3.0) {
    keyboards_ = ListKeyboards();
    mics_ = ListMics();
    pads_ = ListPads();
    devices_listed_at_ = now;
  }
  bool changed = false;
  if (detecting_player_ >= 0 && kb) {
    const std::string path = kb->PollDetectedKeyboard();
    if (!path.empty()) {
      std::lock_guard lock(Mutex());
      auto& p = cfg.input.players[detecting_player_];
      p.keyboard = path;
      p.keyboard_name = KeyboardNameForPath(path);
      detecting_player_ = -1;
      changed = true;
    }
  }
  ImGui::Separator();
  ImGui::Text("Jugadores: teclado, mando y microfono de cada uno");
  if (ImGui::BeginTable("##players", 5, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchSame)) {
    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 110.f);
    for (int i = 0; i < kMaxPlayers; i++) ImGui::TableSetupColumn(("Jugador " + std::to_string(i + 1)).c_str());
    ImGui::TableHeadersRow();
    std::lock_guard lock(Mutex());
    auto label = [](const char* t) {
      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0);
      ImGui::TextDisabled("%s", t);
    };
    // teclado
    label("Teclado");
    for (int i = 0; i < kMaxPlayers; i++) {
      ImGui::TableSetColumnIndex(i + 1);
      auto& p = cfg.input.players[i];
      ImGui::PushID(i);
      const std::string cur = p.keyboard.empty() ? "Ninguno"
                              : p.keyboard == "*" ? "Cualquiera libre"
                                                  : (p.keyboard_name.empty() ? KeyboardNameForPath(p.keyboard) : p.keyboard_name);
      ImGui::SetNextItemWidth(-1);
      if (ImGui::BeginCombo("##kb", cur.c_str())) {
        if (ImGui::Selectable("Ninguno", p.keyboard.empty())) { p.keyboard.clear(); changed = true; }
        if (ImGui::Selectable("Cualquiera libre", p.keyboard == "*")) { p.keyboard = "*"; p.keyboard_name.clear(); changed = true; }
        for (auto& k : keyboards_) {
          if (ImGui::Selectable(k.name.c_str(), p.keyboard == k.path)) {
            p.keyboard = k.path;
            p.keyboard_name = k.name;
            changed = true;
          }
        }
        ImGui::EndCombo();
      }
      if (detecting_player_ == i) {
        ImGui::TextColored(ImVec4(1, 0.8f, 0.2f, 1), "Pulsa una tecla en ese teclado...");
        if (ImGui::SmallButton("Cancelar")) { detecting_player_ = -1; if (kb) kb->CancelKeyboardDetect(); }
      } else if (ImGui::SmallButton("Detectar pulsando una tecla") && kb) {
        detecting_player_ = i;
        kb->BeginKeyboardDetect();
      }
      ImGui::PopID();
    }
    // modo
    label("Instrumento");
    for (int i = 0; i < kMaxPlayers; i++) {
      ImGui::TableSetColumnIndex(i + 1);
      auto& p = cfg.input.players[i];
      ImGui::PushID(100 + i);
      int inst = static_cast<int>(InstrumentFromId(p.instrument));
      const char* names[] = {"Guitarra", "Bajo", "Bateria", "Voz (mando + microfono)"};
      ImGui::SetNextItemWidth(-1);
      if (ImGui::Combo("##inst", &inst, names, 4)) {
        p.instrument = InstrumentId(static_cast<Instrument>(inst));
        changed = true;
      }
      ImGui::PopID();
    }
    // mando
    label("Mando");
    for (int i = 0; i < kMaxPlayers; i++) {
      ImGui::TableSetColumnIndex(i + 1);
      auto& p = cfg.input.players[i];
      ImGui::PushID(200 + i);
      ImGui::SetNextItemWidth(-1);
      if (ImGui::BeginCombo("##pad", p.pad.empty() ? "Automatico" : p.pad_name.c_str())) {
        if (ImGui::Selectable("Automatico (orden de conexion)", p.pad.empty())) { p.pad.clear(); p.pad_name.clear(); changed = true; }
        for (auto& d : pads_) {
          if (ImGui::Selectable(d.name.c_str(), p.pad == d.guid)) { p.pad = d.guid; p.pad_name = d.name; changed = true; }
        }
        ImGui::EndCombo();
      }
      ImGui::PopID();
    }
    // microfono
    label("Microfono");
    for (int i = 0; i < kMaxPlayers; i++) {
      ImGui::TableSetColumnIndex(i + 1);
      auto& p = cfg.input.players[i];
      ImGui::PushID(300 + i);
      const std::string cur = p.mic.empty() ? "Ninguno" : p.mic == "default" ? "Predeterminado de Windows" : p.mic_name;
      ImGui::SetNextItemWidth(-1);
      if (ImGui::BeginCombo("##mic", cur.c_str())) {
        if (ImGui::Selectable("Ninguno", p.mic.empty())) { p.mic.clear(); p.mic_name.clear(); changed = true; }
        if (ImGui::Selectable("Predeterminado de Windows", p.mic == "default")) {
          p.mic = "default";
          p.mic_name = "Predeterminado de Windows";
          changed = true;
        }
        for (auto& m : mics_) {
          if (ImGui::Selectable(m.name.c_str(), p.mic == m.id)) { p.mic = m.id; p.mic_name = m.name; changed = true; }
        }
        ImGui::EndCombo();
      }
      if (!p.mic.empty()) {
        ImGui::SetNextItemWidth(-1);
        if (ImGui::SliderFloat("##gain", &p.mic_gain, 0.25f, 4.f, "Ganancia %.2fx")) changed = true;
        ImGui::ProgressBar(MicLevel(i), ImVec2(-1, 8), "");
        const std::string st = MicStatus(i);
        if (!st.empty()) ImGui::TextDisabled("%s", st.c_str());
      }
      ImGui::PopID();
    }
    ImGui::EndTable();
  }
  Hint("Cada jugador es un mando del juego (Jugador 1 = mando 1). El microfono va con el mando de su jugador: "
       "quien canta puede usar otro teclado en modo Mando para moverse por los menus.");
  if (changed) {
    controls_dirty_ = true;
    if (kb) kb->ReloadBindings();
    ApplyMicConfig();
  }
}

void DePanelDialog::DrawDevicesSection() {
  ImGui::Text("Jugadores conectados (XInput / DirectInput / HID / teclado):");
  auto* in = runtime_ ? dynamic_cast<rex::input::InputSystem*>(runtime_->input_system()) : nullptr;
  if (!in) {
    ImGui::TextDisabled("Sistema de entrada no disponible");
    return;
  }
  auto users = in->GetConnectedUsers();
  bool any = false;
  for (uint32_t u = 0; u < rex::input::kMaxGuestUsers; u++) {
    if (!users.test(u)) continue;
    any = true;
    rex::input::X_INPUT_CAPABILITIES caps{};
    rex::input::X_INPUT_STATE state{};
    in->GetCapabilities(u, 0, &caps);
    in->GetStateForUI(u, &state);
    uint16_t b = state.gamepad.buttons;
    ImGui::Text("Jugador %u: %s (subtipo %u)", u + 1, SubtypeName(caps.sub_type), caps.sub_type);
    ImGui::SameLine(300);
    const uint16_t fret_masks[] = {rex::input::X_INPUT_GAMEPAD_A, rex::input::X_INPUT_GAMEPAD_B,
                                   rex::input::X_INPUT_GAMEPAD_Y, rex::input::X_INPUT_GAMEPAD_X,
                                   rex::input::X_INPUT_GAMEPAD_LEFT_SHOULDER};
    for (int i = 0; i < 5; i++) {
      ImGui::SameLine();
      ImGui::TextColored((b & fret_masks[i]) ? kFretColors[i] : ImVec4(0.3f, 0.3f, 0.3f, 1), "#");
    }
    ImGui::SameLine();
    ImGui::TextDisabled(" btn=0x%04X  RX=%6d  RY=%6d", b, int(state.gamepad.thumb_rx),
                        int(state.gamepad.thumb_ry));
  }
  if (!any) ImGui::TextDisabled("Ninguno (conecta una guitarra/mando o activa el teclado)");
}

// -----------------------------------------------------------------------------
// Mods
// -----------------------------------------------------------------------------

void DePanelDialog::DrawModsTab() {
  auto& mm = ModManager::Instance();
  if (ImGui::Button("Abrir carpeta mods")) OpenInExplorer(mm.root());
  ImGui::SameLine();
  if (ImGui::Button("Volver a escanear")) {
    mm.Rescan();
    RefreshMods();
  }
  ImGui::SameLine();
  ImGui::BeginDisabled(!mods_dirty_);
  if (ImGui::Button("Guardar orden y estado")) {
    mm.Persist();
    mods_dirty_ = false;
    restart_needed_ = true;
    status_ = "Lista de mods guardada";
  }
  ImGui::EndDisabled();

  ImGui::TextDisabled("Arriba = mayor prioridad. Cada mod es una carpeta mods/<Nombre>/ con mod.toml y data/...");

  if (ImGui::BeginTable("mods", 6,
                        ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                            ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable,
                        ImVec2(0, 260))) {
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("On", ImGuiTableColumnFlags_WidthFixed, 30);
    ImGui::TableSetupColumn("Mod");
    ImGui::TableSetupColumn("Tipo", ImGuiTableColumnFlags_WidthFixed, 80);
    ImGui::TableSetupColumn("Archivos", ImGuiTableColumnFlags_WidthFixed, 120);
    ImGui::TableSetupColumn("Aplicado", ImGuiTableColumnFlags_WidthFixed, 110);
    ImGui::TableSetupColumn("Orden", ImGuiTableColumnFlags_WidthFixed, 60);
    ImGui::TableHeadersRow();
    for (size_t i = 0; i < mods_.size(); i++) {
      auto& m = mods_[i];
      ImGui::PushID(static_cast<int>(i));
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      bool en = m.enabled;
      if (ImGui::Checkbox("##en", &en)) {
        mm.SetEnabled(m.folder, en);
        m.enabled = en;
        mods_dirty_ = true;
      }
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(m.name.c_str());
      if (!m.author.empty() || !m.version.empty()) {
        ImGui::SameLine();
        ImGui::TextDisabled("%s %s", m.version.c_str(), m.author.empty() ? "" : ("por " + m.author).c_str());
      }
      if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Carpeta: %s\n%s", m.path.string().c_str(), m.description.c_str());
      }
      if (!m.description.empty()) ImGui::TextDisabled("%s", m.description.c_str());
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(m.type.c_str());
      ImGui::TableNextColumn();
      ImGui::Text("%zu (%s)", m.file_count, HumanBytes(m.total_bytes).c_str());
      ImGui::TableNextColumn();
      if (m.file_count == 0) {
        ImGui::TextColored(ImVec4(1, 0.6f, 0.2f, 1), "vacio");
      } else {
        ImGui::Text("%zu reemp.", m.overrides);
        if (m.added) ImGui::TextDisabled("%zu nuevos", m.added);
      }
      ImGui::TableNextColumn();
      if (ImGui::ArrowButton("##up", ImGuiDir_Up) && i > 0) {
        mm.Move(m.folder, -1);
        mods_dirty_ = true;
        RefreshMods();
      }
      ImGui::SameLine();
      if (ImGui::ArrowButton("##dn", ImGuiDir_Down) && i + 1 < mods_.size()) {
        mm.Move(m.folder, +1);
        mods_dirty_ = true;
        RefreshMods();
      }
      ImGui::PopID();
    }
    if (mods_.empty()) {
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::TableNextColumn();
      ImGui::TextDisabled("No hay mods. Crea uno abajo o copia una carpeta de mod en mods/.");
    }
    ImGui::EndTable();
  }

  ImGui::Separator();
  ImGui::Text("Crear mod nuevo (plantilla con mod.toml y carpetas):");
  ImGui::SetNextItemWidth(220);
  ImGui::InputTextWithHint("##newmod", "NombreDelMod", new_mod_name_, sizeof(new_mod_name_));
  ImGui::SameLine();
  ImGui::SetNextItemWidth(230);
  ImGui::Combo("##newtype", &new_mod_type_, kModTypeLabels, IM_ARRAYSIZE(kModTypeLabels));
  ImGui::SameLine();
  if (ImGui::Button("Crear")) {
    std::string err;
    if (mm.CreateTemplate(new_mod_name_, kModTypes[new_mod_type_], &err)) {
      status_ = std::string("Mod creado: mods/") + new_mod_name_;
      OpenInExplorer(mm.root() / new_mod_name_);
      new_mod_name_[0] = 0;
      mods_dirty_ = true;
      RefreshMods();
    } else {
      status_ = "Error: " + err;
    }
  }

  if (ImGui::CollapsingHeader("Archivos aplicados en esta sesion")) {
    auto injected = mm.Injected();
    ImGui::BeginChild("inj", ImVec2(0, 160), true);
    if (injected.empty()) ImGui::TextDisabled("Ningun archivo de mods aplicado.");
    for (auto& r : injected) {
      ImGui::TextColored(r.replaced ? ImVec4(0.5f, 0.9f, 0.5f, 1) : ImVec4(0.9f, 0.8f, 0.4f, 1), "%s",
                         r.replaced ? "REEMPLAZA" : "NUEVO    ");
      ImGui::SameLine();
      ImGui::Text("%s  <- [%s]", r.guest_path.c_str(), r.mod.c_str());
    }
    ImGui::EndChild();
    ImGui::TextDisabled("NUEVO = no existe en el juego original: solo se carga si su carpeta tampoco existe.");
  }
}

void DePanelDialog::DrawContentTab(const char* type_a, const char* type_b, const char* help) {
  ImGui::TextWrapped("%s", help);
  ImGui::TextDisabled("Si el original esta en data/compressed/ o en compress.toc, el reemplazo debe ir comprimido (CHNK).");
  ImGui::Spacing();
  bool any = false;
  for (auto& m : mods_) {
    if (m.type != type_a && m.type != type_b &&
        !(std::strcmp(type_a, "skin") == 0 && (m.type == "character" || m.type == "ui"))) {
      continue;
    }
    any = true;
    ImGui::BulletText("%s%s  -  %zu archivo(s), %zu reemplazo(s)%s", m.name.c_str(),
                      m.enabled ? "" : " (desactivado)", m.file_count, m.overrides,
                      m.version.empty() ? "" : ("  v" + m.version).c_str());
  }
  if (!any) ImGui::TextDisabled("No hay mods de este tipo todavia.");
  ImGui::Spacing();
  ImGui::SetNextItemWidth(220);
  ImGui::InputTextWithHint("##quickname", "NombreDelMod", new_mod_name_, sizeof(new_mod_name_));
  ImGui::SameLine();
  if (ImGui::Button("Crear plantilla y abrir carpeta")) {
    std::string err;
    auto& mm = ModManager::Instance();
    if (mm.CreateTemplate(new_mod_name_, type_a, &err)) {
      status_ = std::string("Mod creado: mods/") + new_mod_name_;
      OpenInExplorer(mm.root() / new_mod_name_);
      new_mod_name_[0] = 0;
      mods_dirty_ = true;
      RefreshMods();
    } else {
      status_ = "Error: " + err;
    }
  }
  ImGui::TextDisabled("Despues de copiar los archivos: pestana Mods -> Volver a escanear -> Guardar -> reiniciar.");
}

// -----------------------------------------------------------------------------
// Dev
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
// Estilo comun de las pestanas Dev y Graficos
// -----------------------------------------------------------------------------
namespace {
const ImVec4 kAccent(0.35f, 0.75f, 1.0f, 1.0f);
const ImVec4 kOk(0.45f, 0.85f, 0.50f, 1.0f);
const ImVec4 kWarn(1.0f, 0.72f, 0.25f, 1.0f);
const ImVec4 kErr(1.0f, 0.42f, 0.42f, 1.0f);
const ImVec4 kDim(0.62f, 0.64f, 0.70f, 1.0f);

void Section(const char* title) {
  ImGui::Spacing();
  ImGui::PushStyleColor(ImGuiCol_Text, kAccent);
  ImGui::SeparatorText(title);
  ImGui::PopStyleColor();
}

// etiqueta de estado con fondo (una "pastilla")
void Chip(const char* text, const ImVec4& col) {
  const ImVec4 bg(col.x * 0.25f, col.y * 0.25f, col.z * 0.25f, 1);
  ImGui::PushStyleColor(ImGuiCol_Button, bg);
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, bg);
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, bg);
  ImGui::PushStyleColor(ImGuiCol_Text, col);
  ImGui::SmallButton(text);
  ImGui::PopStyleColor(4);
}

void Hint(const char* text) {
  ImGui::SameLine();
  ImGui::TextDisabled("(?)");
  if (ImGui::BeginItemTooltip()) {
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 32);
    ImGui::TextUnformatted(text);
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
  }
}

// fila "etiqueta | control": etiqueta a la izquierda, control alineado en columna
void Row(const char* label, float width = 260) {
  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted(label);
  ImGui::SameLine(220);
  ImGui::SetNextItemWidth(width);
}

void RestartTag(bool pending) {
  ImGui::SameLine();
  if (pending) ImGui::TextColored(kWarn, "reinicio pendiente");
  else ImGui::TextDisabled("al reiniciar");
}
}  // namespace

// -----------------------------------------------------------------------------
// Dev: puente con el motor
// -----------------------------------------------------------------------------
void DePanelDialog::RunConsoleLine(const std::string& line) {
  if (line.empty()) return;
  if (history_.empty() || history_.back() != line) history_.push_back(line);
  if (history_.size() > 24) history_.erase(history_.begin());
  qb_bridge::Submit(line);
  scroll_log_ = true;
}

void DePanelDialog::DrawDevTab() {
  auto& mm = ModManager::Instance();
  auto& cfg = Get();
  uint8_t* membase = runtime_ ? runtime_->virtual_membase() : nullptr;

  // ---- barra de estado ----
  const bool boot = BootDone();
  Chip(boot ? "Juego: en el menu" : "Juego: arrancando", boot ? kOk : kWarn);
  ImGui::SameLine();
  if (qb_bridge::ServerPort()) {
    char b[48];
    std::snprintf(b, sizeof b, "Puente 127.0.0.1:%d", qb_bridge::ServerPort());
    Chip(b, kOk);
  } else {
    Chip("Puente local apagado", kDim);
  }
  ImGui::SameLine();
  Chip(qb_bridge::IsDebugBuild() ? "DebugBuild 1" : "DebugBuild 0", qb_bridge::IsDebugBuild() ? kAccent : kDim);
  ImGui::SameLine();
  ImGui::TextDisabled("Consola del juego: qb <orden>");

  if (!ImGui::BeginTabBar("dev_tabs")) return;

  // ---------------------------------------------------------------- Menus de Neversoft
  if (ImGui::BeginTabItem("Menus")) {
    Section("Menu de depuracion original");
    ImGui::TextWrapped("Pantallas de depuracion que Neversoft dejo dentro del juego. Se abren desde el menu "
                       "principal: no durante el arranque ni dentro de una cancion.");
    ImGui::BeginDisabled(!boot);
    const auto& screens = qb_bridge::DebugScreens();
    for (size_t i = 0; i < screens.size(); ++i) {
      if (i % 3) ImGui::SameLine();
      ImGui::PushID(static_cast<int>(i));
      if (ImGui::Button(screens[i].title, ImVec2(230, 30))) RunConsoleLine(std::string("state ") + screens[i].state);
      ImGui::PopID();
    }
    if (ImGui::Button("Depurador del microfono", ImVec2(230, 30))) RunConsoleLine("run debug_menu_mic_debugger");
    ImGui::EndDisabled();
    if (!boot) ImGui::TextColored(kWarn, "Espera a que aparezca el menu principal.");

    Section("Scripts");
    bool dbg = qb_bridge::IsDebugBuild();
    if (ImGui::Checkbox("Responder DebugBuild = 1 a los scripts", &dbg)) {
      qb_bridge::SetDebugBuild(dbg);
      std::lock_guard lock(Mutex());
      cfg.dev.debug_build = dbg;
      controls_dirty_ = true;
    }
    Hint("En la version de tienda DebugBuild() siempre responde 0. Algunos scripts muestran opciones extra con 1.");

    Section("Partida guardada");
    ImGui::Checkbox("Entiendo que esto cambia mi partida", &allow_unlock_);
    ImGui::SameLine();
    ImGui::BeginDisabled(!allow_unlock_);
    if (ImGui::Button("Desbloquear canciones y carrera")) {
      qb_bridge::UnlockEverything();
      allow_unlock_ = false;
    }
    ImGui::EndDisabled();
    Hint("Usa los scripts de desbloqueo del menu de depuracion. Haz antes una copia de la carpeta user.");
    ImGui::EndTabItem();
  }

  // ---------------------------------------------------------------- Consola
  if (ImGui::BeginTabItem("Consola")) {
    auto logs = qb_bridge::GetRecentLogs();
    const float input_h = ImGui::GetFrameHeightWithSpacing() * 3.2f;
    ImGui::BeginChild("qb_log", ImVec2(0, -input_h), ImGuiChildFlags_Borders);
    if (logs.empty()) {
      ImGui::TextDisabled("Escribe una orden abajo. Ejemplos:");
      ImGui::TextDisabled("  run ui_event event=menu_change data={state=UIstate_debug}");
      ImGui::TextDisabled("  get controller_unplugged_frame_count     set <global> <valor>     exists <nombre>");
    }
    for (const auto& e : logs) {
      ImGui::TextDisabled("%7.1f", e.timestamp);
      ImGui::SameLine();
      ImGui::PushStyleColor(ImGuiCol_Text, e.success ? ImGui::GetStyleColorVec4(ImGuiCol_Text) : kErr);
      ImGui::TextWrapped("%s", e.message.c_str());
      ImGui::PopStyleColor();
    }
    if (scroll_log_ || ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4) ImGui::SetScrollHereY(1.0f);
    scroll_log_ = false;
    ImGui::EndChild();

    // tipo del simbolo que se esta escribiendo
    std::string text(script_input_), head = text.substr(0, text.find_first_of(" \t")), sym = head;
    if (head == "run" || head == "exists" || head == "get" || head == "set" || head == "state") {
      const std::string rest = text.substr(head.size());
      const size_t a = rest.find_first_not_of(" \t");
      sym = a == std::string::npos ? "" : rest.substr(a, rest.find_first_of(" \t", a) - a);
    }
    const int type = sym.empty() || head == "state" ? -1 : qb_bridge::SymbolType(qb_bridge::CalculateQbKey(sym), membase);
    ImGui::SetNextItemWidth(-140);
    if (focus_console_) {
      ImGui::SetKeyboardFocusHere();
      focus_console_ = false;
    }
    const bool enter = ImGui::InputTextWithHint("##qb_cmd", "orden (run, get, set, exists, state, debugmenu...)",
                                                script_input_, sizeof(script_input_),
                                                ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    if ((ImGui::Button("Enviar", ImVec2(62, 0)) || enter) && script_input_[0]) {
      RunConsoleLine(script_input_);
      script_input_[0] = 0;
      focus_console_ = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Copiar", ImVec2(62, 0))) {
      std::string all;
      for (const auto& e : logs) all += e.message + "\n";
      ImGui::SetClipboardText(all.c_str());
    }
    if (type >= 0) {
      ImGui::TextColored(type == 0 ? kWarn : kOk, "%s: %s", sym.c_str(), qb_bridge::SymbolTypeName(type));
    } else {
      ImGui::TextDisabled("Parametros como en QB: clave=valor, \"texto\", 1.5, nombre, {anidado}");
    }
    if (!history_.empty()) {
      ImGui::TextDisabled("Recientes:");
      for (int i = static_cast<int>(history_.size()) - 1, n = 0; i >= 0 && n < 5; --i, ++n) {
        ImGui::SameLine();
        ImGui::PushID(i);
        const std::string label = history_[i].size() > 30 ? history_[i].substr(0, 28) + ".." : history_[i];
        if (ImGui::SmallButton(label.c_str())) {
          std::snprintf(script_input_, sizeof(script_input_), "%s", history_[i].c_str());
          focus_console_ = true;
        }
        ImGui::PopID();
      }
    }
    ImGui::EndTabItem();
  }

  // ---------------------------------------------------------------- Globales vigiladas
  if (ImGui::BeginTabItem("Globales")) {
    ImGui::TextWrapped("Variables globales del motor en vivo. Se pueden cambiar las enteras, las reales y los nombres.");
    ImGui::SetNextItemWidth(300);
    const bool add = ImGui::InputTextWithHint("##watch_add", "nombre de la global (ej. is_in_debug)", global_input_,
                                              sizeof(global_input_), ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    if ((ImGui::Button("Vigilar") || add) && global_input_[0]) {
      watches_.push_back(Watch{global_input_, {}});
      global_input_[0] = 0;
    }
    if (ImGui::BeginTable("watch", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
      ImGui::TableSetupColumn("Global", ImGuiTableColumnFlags_WidthStretch, 1.2f);
      ImGui::TableSetupColumn("Valor actual", ImGuiTableColumnFlags_WidthStretch, 1.0f);
      ImGui::TableSetupColumn("Nuevo valor", ImGuiTableColumnFlags_WidthFixed, 200);
      ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 30);
      ImGui::TableHeadersRow();
      int remove = -1;
      for (size_t i = 0; i < watches_.size(); ++i) {
        auto& w = watches_[i];
        ImGui::PushID(static_cast<int>(i));
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(w.name.c_str());
        ImGui::TableNextColumn();
        const std::string v = qb_bridge::PeekGlobal(w.name, membase);
        if (v.empty()) ImGui::TextColored(kWarn, "no cargada");
        else ImGui::TextUnformatted(v.c_str());
        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(120);
        const bool set = ImGui::InputText("##v", w.edit, sizeof(w.edit), ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SameLine();
        if ((ImGui::SmallButton("Aplicar") || set) && w.edit[0]) RunConsoleLine("set " + w.name + " " + w.edit);
        ImGui::TableNextColumn();
        if (ImGui::SmallButton("x")) remove = static_cast<int>(i);
        ImGui::PopID();
      }
      ImGui::EndTable();
      if (remove >= 0) watches_.erase(watches_.begin() + remove);
    }
    if (watches_.empty()) {
      ImGui::TextDisabled("Sugerencias:");
      const char* sug[] = {"is_in_debug", "controller_unplugged_frame_count"};
      for (const char* s : sug) {
        ImGui::SameLine();
        if (ImGui::SmallButton(s)) watches_.push_back(Watch{s, {}});
      }
    }
    ImGui::TextDisabled("%zu simbolos cargados en la tabla global", qb_bridge::GetLoadedScriptCount(membase));
    ImGui::EndTabItem();
  }

  // ---------------------------------------------------------------- Trucos
  if (ImGui::BeginTabItem("Trucos")) {
    ImGui::TextWrapped("Lanzan el script del truco si existe en esta version del juego; si no existe, la consola "
                       "lo dice. Pensados para usarse dentro de una cancion.");
    struct C {
      const char* label;
      qb_bridge::BuiltinCheat c;
    };
    const C cheats[] = {{"Hyperspeed", qb_bridge::BuiltinCheat::kHyperspeed},
                        {"No fallar", qb_bridge::BuiltinCheat::kInvincible},
                        {"Slide siempre", qb_bridge::BuiltinCheat::kAlwaysSlide},
                        {"Modo rendimiento", qb_bridge::BuiltinCheat::kPerformanceMode},
                        {"Autoplay", qb_bridge::BuiltinCheat::kAutoPlay},
                        {"Ocultar HUD", qb_bridge::BuiltinCheat::kHideHud},
                        {"Bateria libre", qb_bridge::BuiltinCheat::kFreeDrum}};
    int n = 0;
    for (const auto& c : cheats) {
      if (n++ % 4) ImGui::SameLine();
      if (ImGui::Button(c.label, ImVec2(170, 28))) qb_bridge::TriggerCheat(c.c);
    }
    ImGui::EndTabItem();
  }

  // ---------------------------------------------------------------- Motor
  if (ImGui::BeginTabItem("Motor")) {
    std::lock_guard lock(Mutex());
    Section("Arranque y proceso");
    if (ImGui::Checkbox("Saltar la pantalla legal y los videos del arranque", &cfg.dev.auto_skip_intro)) controls_dirty_ = true;
    if (ImGui::Checkbox("Prioridad del proceso por encima de lo normal", &cfg.perf.above_normal_priority)) controls_dirty_ = true;
    if (ImGui::Checkbox("Registrar cada archivo abierto en re_wor_trace.log", &cfg.dev.log_file_opens)) {
      SetLogFileOpens(cfg.dev.log_file_opens);
      controls_dirty_ = true;
    }
    Section("Puente local");
    Row("Puerto (0 = apagado)", 120);
    if (ImGui::InputInt("##bridge_port", &cfg.dev.bridge_port, 0)) {
      cfg.dev.bridge_port = std::clamp(cfg.dev.bridge_port, 0, 65535);
      controls_dirty_ = true;
    }
    RestartTag(cfg.dev.bridge_port != qb_bridge::ServerPort());
    ImGui::TextDisabled("Solo escucha en este PC. La clave cambia en cada arranque (dev_bridge.json).");
    ImGui::EndTabItem();
  }

  // ---------------------------------------------------------------- Archivos
  if (ImGui::BeginTabItem("Archivos")) {
    if (ImGui::Button("Abrir carpeta del juego")) OpenInExplorer(re_wor::core::get_base_directory());
    ImGui::SameLine();
    if (ImGui::Button("Abrir assets")) OpenInExplorer(re_wor::core::get_assets_directory());
    ImGui::SameLine();
    ImGui::Text("Abiertos: %" PRIu64 "   fallidos: %" PRIu64, mm.total_opens(), mm.failed_opens());
    ImGui::SetNextItemWidth(260);
    ImGui::InputTextWithHint("##flt", "filtrar (ej. songs, .pak)", open_filter_, sizeof(open_filter_));
    ImGui::SameLine();
    ImGui::Checkbox("Solo fallidos", &only_failed_);
    ImGui::SameLine();
    if (ImGui::Button("Limpiar")) mm.ClearRecentOpens();
    auto opens = mm.RecentOpens();
    ImGui::BeginChild("opens", ImVec2(0, 0), ImGuiChildFlags_Borders);
    std::string lf = open_filter_;
    for (auto& c : lf) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    for (auto it = opens.rbegin(); it != opens.rend(); ++it) {
      if (only_failed_ && it->ok) continue;
      if (!lf.empty()) {
        std::string lp = it->path;
        for (auto& c : lp) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (lp.find(lf) == std::string::npos) continue;
      }
      ImGui::TextColored(it->ok ? kOk : kErr, "%s", it->ok ? "OK   " : "FALLA");
      ImGui::SameLine();
      ImGui::TextUnformatted(it->path.c_str());
      ImGui::SameLine();
      ImGui::TextDisabled("flags=0x%X", it->flags);
    }
    ImGui::EndChild();
    ImGui::EndTabItem();
  }

  // ---------------------------------------------------------------- Memoria
  if (ImGui::BeginTabItem("Memoria")) {
    ImGui::SetNextItemWidth(120);
    ImGui::InputText("Direccion (hex)", peek_addr_, sizeof(peek_addr_), ImGuiInputTextFlags_CharsHexadecimal);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120);
    ImGui::SliderInt("Filas", &peek_rows_, 1, 32);
    const uint32_t addr = static_cast<uint32_t>(std::strtoul(peek_addr_, nullptr, 16)) & ~0xFu;
    if (!membase) {
      ImGui::TextDisabled("Memoria no disponible");
    } else if (addr < 0x40000000u) {
      ImGui::TextDisabled("Solo direcciones >= 0x40000000 (montones e imagen del juego)");
    } else {
      ImGui::BeginChild("mem", ImVec2(0, 0), ImGuiChildFlags_Borders);
      for (int r = 0; r < peek_rows_; r++) {
        const uint32_t a = addr + r * 16;
        uint8_t row[16];
        if (!SafeRead(membase + a, row, sizeof(row))) {
          ImGui::TextColored(kErr, "%08X: <no mapeado>", a);
          continue;
        }
        char line[128], ascii[17] = {};
        int n = std::snprintf(line, sizeof(line), "%08X: ", a);
        for (int c = 0; c < 16; c++) {
          n += std::snprintf(line + n, sizeof(line) - n, "%02X ", row[c]);
          ascii[c] = (row[c] >= 32 && row[c] < 127) ? static_cast<char>(row[c]) : '.';
        }
        ImGui::Text("%s %s", line, ascii);
      }
      ImGui::EndChild();
    }
    ImGui::EndTabItem();
  }

  // ---------------------------------------------------------------- Atajos
  if (ImGui::BeginTabItem("Atajos")) {
    const auto& hk = Get().hotkeys;
    if (ImGui::BeginTable("hk", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) {
      auto row = [](const std::string& k, const char* what) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextColored(kAccent, "%s", k.c_str());
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(what);
      };
      row(HotkeyOr(hk.panel, "F8"), "este panel");
      row(HotkeyOr(hk.debug_menu, "F6"), "menu de depuracion de Neversoft");
      row(HotkeyOr(hk.perf_overlay, "F9"), "rendimiento (SDK)");
      row(HotkeyOr(hk.settings, "F10"), "ajustes avanzados / cvars (SDK)");
      row(HotkeyOr(hk.console, "Backtick"), "consola (SDK): qb <orden>");
      row(HotkeyOr(hk.achievements, "F7"), "logros (SDK)");
      row(HotkeyOr(hk.fullscreen, "F11"), "pantalla completa");
      row(HotkeyOr(hk.skip_intro, "F"), "saltar los videos del arranque");
      ImGui::EndTable();
    }
    ImGui::TextDisabled("Las teclas se cambian en re_wor_de.toml ([hotkeys]).");
    ImGui::EndTabItem();
  }
  ImGui::EndTabBar();
}

// -----------------------------------------------------------------------------
// Graficos
// -----------------------------------------------------------------------------
void DePanelDialog::DrawGraphicsTab() {
  auto& cfg = Get();
  std::lock_guard lock(Mutex());
  GraphicsConfig& g = cfg.graphics;
  const GraphicsConfig& b = BootGraphics();
  bool changed = false;

  ImGui::BeginChild("gfx_scroll", ImVec2(0, -ImGui::GetFrameHeightWithSpacing() * 1.6f));

  // ---------------------------------------------------------------- Idioma (textos del juego)
  Section("Idioma");
  {
    static std::string boot_lang = cfg.game.language;   // el de este arranque
    const char* labels[] = {"English", "Espanol", "Francais", "Deutsch", "Italiano"};
    const char* codes[] = {"en", "es", "fr", "de", "it"};
    int idx = 0;
    for (int i = 0; i < 5; ++i) if (cfg.game.language == codes[i]) idx = i;
    Row("Idioma del juego");
    if (ImGui::Combo("##lang", &idx, labels, 5)) { cfg.game.language = codes[idx]; controls_dirty_ = true; }
    RestartTag(cfg.game.language != boot_lang);
    Hint("Los textos traducidos que trae el juego. La partida guardada es la misma en todos los idiomas.");
  }

  // ---------------------------------------------------------------- Calidad general
  Section("Preset y API");
  {
    const char* labels[] = {"Low", "Medium", "High", "Ultra", "Custom"};
    int idx = std::clamp(g.preset, 0, 4);
    Row("Preset de calidad");
    if (ImGui::Combo("##preset", &idx, labels, 5) && idx < 4) {
      g.ApplyPreset(idx);
      ApplyLiveDisplayOptions(g);
      changed = true;
    }
    Hint("Cambia antialiasing, filtrado, post-proceso y cache de texturas. No toca la resolucion.");
    const char* apis[] = {"Direct3D 12", "Vulkan", "Direct3D 11 (Legacy / APU)"};
    int aidx = g.backend == "vulkan" ? 1 : g.backend == "d3d11" ? 2 : 0;
    Row("API grafica");
    if (ImGui::Combo("##api", &aidx, apis, 3)) {
      g.backend = aidx == 1 ? "vulkan" : aidx == 2 ? "d3d11" : "d3d12";
      changed = true;
    }
    RestartTag(g.backend != b.backend);
    Hint("Direct3D 11 es el motor nativo completo y optimizado (Fase 4). Direct3D 12 y Vulkan estan en desarrollo.");

    const char* pipelines[] = {"Nativo Completo (Fase 4: 100% Nativo PC)", "Hibrido (Fase 3: Escenario 3D)", "Emulador Xenos (Clasico)"};
    int pidx = g.pipeline_mode == "hybrid" ? 1 : g.pipeline_mode == "emulated" ? 2 : 0;
    Row("Pipeline de Render");
    if (ImGui::Combo("##pipelinemode", &pidx, pipelines, 3)) {
      g.pipeline_mode = pidx == 1 ? "hybrid" : pidx == 2 ? "emulated" : "full";
      changed = true;
    }
    RestartTag(g.pipeline_mode != b.pipeline_mode);
    Hint("El modo Nativo Completo (Fase 4) elimina la emulacion de EDRAM y traslada el 100% del render a la GPU.");
  }

  // ---------------------------------------------------------------- Pantalla
  Section("Pantalla");
  {
    struct DisplayResItem {
      std::string val;
      std::string label;
    };

    static std::vector<DisplayResItem> s_res_cached;
    if (s_res_cached.empty()) {
      s_res_cached.push_back({"", "Nativa de la pantalla / escritorio (Auto)"});
      std::vector<std::pair<uint32_t, uint32_t>> res_pairs;
#ifdef _WIN32
      DEVMODEA dm = {};
      dm.dmSize = sizeof(dm);

      for (DWORD i = 0; EnumDisplaySettingsA(nullptr, i, &dm); ++i) {
        if (dm.dmBitsPerPel != 32) continue;
        if (dm.dmPelsWidth < 800 || dm.dmPelsHeight < 600) continue;

        bool exists = false;
        for (const auto& p : res_pairs) {
          if (p.first == dm.dmPelsWidth && p.second == dm.dmPelsHeight) {
            exists = true;
            break;
          }
        }
        if (!exists) {
          res_pairs.push_back({dm.dmPelsWidth, dm.dmPelsHeight});
        }
      }

      std::sort(res_pairs.begin(), res_pairs.end(), [](const auto& a, const auto& b) {
        if (a.first != b.first) return a.first < b.first;
        return a.second < b.second;
      });
#endif
      if (res_pairs.empty()) {
        res_pairs = {
            {1280, 720}, {1366, 768}, {1600, 900}, {1920, 1080},
            {1920, 1200}, {2560, 1080}, {2560, 1440}, {2560, 1600},
            {3440, 1440}, {3840, 2160}
        };
      }

      for (const auto& [w, h] : res_pairs) {
        uint32_t gcd_val = std::gcd(w, h);
        uint32_t rw = w / (gcd_val ? gcd_val : 1);
        uint32_t rh = h / (gcd_val ? gcd_val : 1);
        if (rw == 8 && rh == 5) { rw = 16; rh = 10; }
        else if (rw == 64 && rh == 27) { rw = 21; rh = 9; }
        else if (rw == 43 && rh == 18) { rw = 21; rh = 9; }

        char val_buf[32];
        std::snprintf(val_buf, sizeof(val_buf), "%ux%u", w, h);

        char label_buf[96];
        const char* standard_tag = "";
        if (w == 1280 && h == 720) standard_tag = " - 720p HD";
        else if (w == 1366 && h == 768) standard_tag = " - Portatiles";
        else if (w == 1600 && h == 900) standard_tag = " - 900p HD+";
        else if (w == 1920 && h == 1080) standard_tag = " - 1080p Full HD";
        else if (w == 1920 && h == 1200) standard_tag = " - 1200p (16:10)";
        else if (w == 2560 && h == 1440) standard_tag = " - 1440p 2K QHD";
        else if (w == 2560 && h == 1600) standard_tag = " - 1600p (16:10)";
        else if (w == 3440 && h == 1440) standard_tag = " - Ultrawide UWQHD";
        else if (w == 3840 && h == 2160) standard_tag = " - 4K Ultra HD";

        std::snprintf(label_buf, sizeof(label_buf), "%u x %u (%u:%u%s)", w, h, rw, rh, standard_tag);
        s_res_cached.push_back({val_buf, label_buf});
      }
    }

    int idx = 0;
    for (size_t i = 0; i < s_res_cached.size(); ++i) {
      if (g.resolution == s_res_cached[i].val ||
          (g.resolution == "1080p" && s_res_cached[i].val == "1920x1080") ||
          (g.resolution == "720p" && s_res_cached[i].val == "1280x720") ||
          (g.resolution == "1440p" && s_res_cached[i].val == "2560x1440") ||
          (g.resolution == "4k" && s_res_cached[i].val == "3840x2160") ||
          (g.resolution == "900p" && s_res_cached[i].val == "1600x900") ||
          (g.resolution == "768p" && s_res_cached[i].val == "1366x768")) {
        idx = static_cast<int>(i);
        break;
      }
    }

    Row("Resolucion");
    if (ImGui::BeginCombo("##res", s_res_cached[idx].label.c_str())) {
      for (size_t i = 0; i < s_res_cached.size(); ++i) {
        bool is_selected = (idx == static_cast<int>(i));
        if (ImGui::Selectable(s_res_cached[i].label.c_str(), is_selected)) {
          RequestDisplayChange(g.resolution, g.window_mode, s_res_cached[i].val, g.window_mode);
        }
        if (is_selected) ImGui::SetItemDefaultFocus();
      }
      ImGui::EndCombo();
    }
    Hint("Detecta automaticamente todas las resoluciones y proporciones (16:9, 16:10, 21:9) de tu monitor o pantalla.");
  }
  {
    const char* labels[] = {"Ventana", "Pantalla completa (sin bordes)", "Pantalla completa exclusiva"};
    Row("Modo de pantalla");
    int mode = g.window_mode;
    if (ImGui::Combo("##wmode", &mode, labels, 3)) RequestDisplayChange(g.resolution, g.window_mode, g.resolution, mode);
    Hint("F11 alterna ventana y pantalla completa al momento.");
  }
  {
    const int hz[] = {60, 75, 120, 144, 165, 240};
    const char* labels[] = {"60 Hz (como en la consola)", "75 Hz", "120 Hz", "144 Hz", "165 Hz", "240 Hz"};
    int idx = 0;
    for (int i = 0; i < 6; ++i) if (g.refresh_hz == hz[i]) idx = i;
    Row("Frecuencia");
    if (ImGui::Combo("##hz", &idx, labels, 6)) {
      g.refresh_hz = hz[idx];
      ApplyLiveDisplayOptions(g);
      changed = true;
    }
    if (g.refresh_hz > 60) {
      ImGui::TextColored(kWarn, "Experimental: el juego se hizo para 60 Hz. Si notas animaciones o audio raros, vuelve a 60.");
    }
  }
  Row("VSync del monitor");
  if (ImGui::Checkbox("##vsync", &g.vsync)) {
    ApplyLiveDisplayOptions(g);
    changed = true;
  }
  ImGui::SameLine();
  ImGui::TextDisabled("al momento");
  Row("Contador de FPS");
  if (ImGui::Checkbox("##showfps", &g.show_fps)) changed = true;
  ImGui::SameLine();
  if (ImGui::Checkbox("Detallado (tiempos y esperas a la GPU)##pdet", &g.perf_detail)) changed = true;
  Row("Franjas si no es 16:9");
  if (ImGui::Checkbox("##lbox", &g.letterbox)) {
    ApplyLiveDisplayOptions(g);
    changed = true;
  }
  ImGui::SameLine();
  ImGui::TextDisabled("al momento");

  // ---------------------------------------------------------------- Calidad
  Section("Calidad");
  Row("Antialiasing");
  if (ImGui::Checkbox("MSAA 2x nativo##msaa", &g.msaa2x)) changed = true;
  RestartTag(g.msaa2x != b.msaa2x);
  ImGui::SameLine();
  if (ImGui::Checkbox("FXAA##fxaa", &g.fxaa)) {
    ApplyLiveDisplayOptions(g);
    changed = true;
  }
  {
    const char* labels[] = {"Trilineal", "Anisotropico 2x", "Anisotropico 4x", "Anisotropico 8x",
                            "Anisotropico 16x"};
    Row("Filtrado de texturas");
    if (ImGui::Combo("##aniso", &g.aniso, labels, 5)) changed = true;
    RestartTag(g.aniso != b.aniso);
  }
  Row("Post-proceso");
  if (ImGui::Checkbox("Efectos de imagen (seccion Imagen)##pfx", &g.post_fx)) changed = true;
  {
    const char* labels[] = {"Ninguno", "Bajo (filas 3D de delante)", "Medio (publico 2D del fondo)", "Alto (original)"};
    Row("Publico");
    if (ImGui::Combo("##crowd", &g.crowd, labels, 4)) changed = true;
    Hint("Se aplica al momento durante la cancion.");
  }
  {
    const char* labels[] = {"Optimizado (Alto Rendimiento / Culling)", "Medio (Balanceado)", "Alto (Original 360)", "Ultra (Sin Culling)"};
    Row("Humo y niebla");
    if (ImGui::Combo("##particles", &g.particles, labels, 4)) changed = true;
    Hint("Optimiza el humo, niebla y efectos del escenario para evitar caidas en planos abiertos.");
  }
  {
    const char* labels[] = {"Bilineal (suave)", "AMD CAS (nitidez)", "AMD FSR 1 (reescalado)"};
    const char* vals[] = {"bilinear", "cas", "fsr"};
    int idx = 0;
    for (int i = 0; i < 3; ++i) if (g.upscale == vals[i]) idx = i;
    Row("Escalado a pantalla");
    if (ImGui::Combo("##up", &idx, labels, 3)) { g.upscale = vals[idx]; changed = true; }
    RestartTag(g.upscale != b.upscale);
  }
  ImGui::BeginDisabled(g.upscale == "bilinear");
  Row("Nitidez");
  if (ImGui::SliderFloat("##sharp", &g.sharpness, 0.f, 1.f, "%.2f")) changed = true;
  RestartTag(g.sharpness != b.sharpness);
  ImGui::EndDisabled();
  if (ImGui::TreeNode("Avanzado")) {
    const char* paths[] = {"RTV (rapido, recomendado)", "ROV (mas exacto, mas lento)"};
    int pidx = g.render_target_path == "rov" ? 1 : 0;
    Row("Render targets");
    if (ImGui::Combo("##rtp", &pidx, paths, 2)) { g.render_target_path = pidx ? "rov" : "rtv"; changed = true; }
    RestartTag(g.render_target_path != b.render_target_path);
    const int caches[] = {512, 1024, 2048};
    const char* clabels[] = {"512 MB", "1024 MB (recomendado)", "2048 MB"};
    int cidx = 1;
    for (int i = 0; i < 3; ++i) if (g.texture_cache_mb == caches[i]) cidx = i;
    Row("Cache de texturas");
    if (ImGui::Combo("##tc", &cidx, clabels, 3)) { g.texture_cache_mb = caches[cidx]; changed = true; }
    RestartTag(g.texture_cache_mb != b.texture_cache_mb);
    Row("Shaders");
    if (ImGui::Checkbox("Compilar en paralelo (menos tirones)##async", &g.async_pipeline_compile)) changed = true;
    RestartTag(g.async_pipeline_compile != b.async_pipeline_compile);
    ImGui::TreePop();
  }

  // ---------------------------------------------------------------- Imagen
  Section("Imagen");
  if (!edfx::SongPlaying()) {
    ImGui::TextColored(kDim, "Se aplica al escenario durante las canciones; la autopista y el HUD no se tocan.");
  } else if (sandbox::Mode() == sandbox::NativeMode::kSandbox) {
    ImGui::TextColored(kOk, "Activo en esta cancion.");
  } else {
    ImGui::TextColored(kWarn, "Este escenario no deja separar el fondo del HUD: aqui no se aplica.");
  }
  {
    struct Preset {
      const char* name;
      float bloom, glow, br, co, sa, vi, gr;
    };
    const Preset presets[] = {{"Original", 0, 0, 0, 0, 0, 0, 0},
                              {"Vivo", .25f, 0, .05f, .15f, .25f, 0, 0},
                              {"Concierto", .55f, .35f, .05f, .2f, .15f, .25f, 0},
                              {"Cine", .2f, .1f, -.05f, .25f, -.15f, .45f, .25f},
                              {"Suave", .1f, .45f, .05f, -.1f, -.05f, .1f, 0}};
    for (size_t i = 0; i < 5; ++i) {
      if (i) ImGui::SameLine();
      if (ImGui::Button(presets[i].name, ImVec2(110, 0))) {
        const auto& p = presets[i];
        g.bloom = p.bloom;
        g.glow = p.glow;
        g.brightness = p.br;
        g.contrast = p.co;
        g.saturation = p.sa;
        g.vignette = p.vi;
        g.grain = p.gr;
        changed = true;
      }
    }
  }
  struct S {
    const char* label;
    float* v;
    float lo, hi;
    const char* hint;
  };
  const S sliders[] = {
      {"Bloom", &g.bloom, 0, 1, "Resplandor alrededor de las luces mas fuertes (focos, pirotecnia)."},
      {"Glow", &g.glow, 0, 1, "Resplandor suave de toda la zona iluminada."},
      {"Blur", &g.blur, 0, 1, "Desenfoque suave del escenario."},
      {"Brillo", &g.brightness, -1, 1, nullptr},
      {"Contraste", &g.contrast, -1, 1, nullptr},
      {"Saturacion", &g.saturation, -1, 1, "-1 = blanco y negro."},
      {"Vineta", &g.vignette, 0, 1, "Oscurece los bordes de la imagen."},
      {"Grano", &g.grain, 0, 1, "Grano de pelicula."},
  };
  for (const auto& s : sliders) {
    Row(s.label);
    if (ImGui::SliderFloat((std::string("##") + s.label).c_str(), s.v, s.lo, s.hi, "%.2f")) changed = true;
    if (s.hint) Hint(s.hint);
  }
  ImGui::EndChild();

  // ---------------------------------------------------------------- pie
  ImGui::Separator();
  if (ImGui::Button("Valores por defecto")) {
    const std::string res = g.resolution;
    const int mode = g.window_mode;
    g = GraphicsConfig{};
    g.resolution = res;   // la resolucion y el modo de pantalla se cambian arriba, con confirmacion
    g.window_mode = mode;
    ApplyLiveDisplayOptions(g);
    changed = true;
  }
  ImGui::SameLine();
  const bool pending = NeedsRestart(g) || g.render_target_path != b.render_target_path ||
                       g.async_pipeline_compile != b.async_pipeline_compile;
  if (pending) ImGui::TextColored(kWarn, "Hay cambios que se aplican al reiniciar el juego.");
  else ImGui::TextDisabled("Se guarda en re_wor_de.toml ([graphics]).");
  if (changed) {
    g.preset = g.MatchPreset();
    controls_dirty_ = true;
  }
}

}  // namespace re_wor::de
