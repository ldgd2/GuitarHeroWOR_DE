#include "de/de_panel.hpp"

#include <rex/input/input.h>
#include <rex/input/input_system.h>
#include <rex/runtime.h>

#include <imgui.h>

#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "core/crash_handler.hpp"
#include "core/path_resolver.hpp"
#include "de/de_config.hpp"
#include "de/de_hooks.hpp"
#include "input/key_names.hpp"
#include "input/keyboard_guitar_driver.hpp"

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif

namespace re_wor::de {

namespace {

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

void DePanelDialog::Toggle(rex::ui::ImGuiDrawer* drawer, rex::Runtime* runtime) {
  if (g_panel) {
    delete g_panel;  // el destructor quita el dialogo del drawer
    return;
  }
  if (drawer) new DePanelDialog(drawer, runtime);
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
    if (ImGui::BeginTabItem("Controles")) {
      DrawControlsTab();
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Mods")) {
      DrawModsTab();
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Musica custom")) {
      DrawContentTab("music", "song",
                     "Reemplaza canciones existentes: el audio va en data/music/<cancion>_1.fsb.xen "
                     "(tambien _2, _3 y _preview) y el chart en data/songs/<Cancion>_song.pak.xen.\n"
                     "Usa el MISMO nombre de archivo que la cancion original que quieres sustituir. "
                     "Los archivos se generan con herramientas de la comunidad (p. ej. Onyx).");
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Skins")) {
      DrawContentTab("skin", "highway",
                     "Skins de highway, gemas, personajes o interfaz: copia el .pak.xen / .img.xen "
                     "modificado con la misma ruta que el original (data/pak/..., data/zones/..., "
                     "data/ui_images/...).\nTambien se aceptan los tipos 'character' y 'ui'.");
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Dev")) {
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
      std::string& keys = cfg.input.keys[capturing_action_];
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
    if (ImGui::Checkbox("Usar el teclado como guitarra", &cfg.input.keyboard_guitar)) {
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
        ImGui::TextUnformatted(cfg.input.keys[i].empty() ? "(sin asignar)" : cfg.input.keys[i].c_str());
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
          cfg.input.keys[i].clear();
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
    controls_dirty_ = true;
  }
  ImGui::SameLine();
  if (ImGui::Button("Abrir re_wor_de.toml")) OpenInExplorer(ConfigPath());

  ImGui::Separator();
  DrawDevicesSection();
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

void DePanelDialog::DrawDevTab() {
  auto& mm = ModManager::Instance();
  auto& cfg = Get();

  {
    std::lock_guard lock(Mutex());
    if (ImGui::Checkbox("Registrar cada archivo abierto en re_wor_trace.log", &cfg.dev.log_file_opens)) {
      SetLogFileOpens(cfg.dev.log_file_opens);
      controls_dirty_ = true;
    }
  }
  ImGui::SameLine();
  if (ImGui::Button("Abrir carpeta del juego")) OpenInExplorer(re_wor::core::get_base_directory());
  ImGui::SameLine();
  if (ImGui::Button("Abrir assets")) OpenInExplorer(re_wor::core::get_assets_directory());

  ImGui::Text("Archivos abiertos por el motor: %" PRIu64 "  (fallidos: %" PRIu64 ")", mm.total_opens(),
              mm.failed_opens());
  ImGui::SetNextItemWidth(260);
  ImGui::InputTextWithHint("##flt", "filtrar (ej. songs, .pak)", open_filter_, sizeof(open_filter_));
  ImGui::SameLine();
  ImGui::Checkbox("Solo fallidos", &only_failed_);
  ImGui::SameLine();
  if (ImGui::Button("Limpiar")) mm.ClearRecentOpens();

  auto opens = mm.RecentOpens();
  ImGui::BeginChild("opens", ImVec2(0, 200), true);
  std::string flt = open_filter_;
  for (auto it = opens.rbegin(); it != opens.rend(); ++it) {
    if (only_failed_ && it->ok) continue;
    if (!flt.empty()) {
      std::string lp = it->path, lf = flt;
      for (auto& c : lp) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      for (auto& c : lf) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      if (lp.find(lf) == std::string::npos) continue;
    }
    ImGui::TextColored(it->ok ? ImVec4(0.6f, 0.9f, 0.6f, 1) : ImVec4(1, 0.4f, 0.4f, 1), "%s",
                       it->ok ? "OK   " : "FALLA");
    ImGui::SameLine();
    ImGui::Text("%s", it->path.c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("flags=0x%X", it->flags);
  }
  ImGui::EndChild();

  if (ImGui::CollapsingHeader("Visor de memoria del guest (solo lectura)")) {
    ImGui::SetNextItemWidth(120);
    ImGui::InputText("Direccion (hex)", peek_addr_, sizeof(peek_addr_), ImGuiInputTextFlags_CharsHexadecimal);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(100);
    ImGui::SliderInt("Filas", &peek_rows_, 1, 32);
    uint32_t addr = static_cast<uint32_t>(std::strtoul(peek_addr_, nullptr, 16)) & ~0xFu;
    uint8_t* membase = runtime_ ? runtime_->virtual_membase() : nullptr;
    if (!membase) {
      ImGui::TextDisabled("Memoria no disponible");
    } else if (addr < 0x40000000u) {
      ImGui::TextDisabled("Solo se permiten direcciones >= 0x40000000 (heaps/imagen del juego)");
    } else {
      ImGui::BeginChild("mem", ImVec2(0, 18.0f * peek_rows_ + 10), true);
      for (int r = 0; r < peek_rows_; r++) {
        uint32_t a = addr + r * 16;
        char line[128];
        int n = std::snprintf(line, sizeof(line), "%08X: ", a);
        char ascii[17] = {};
        uint8_t row[16];
        if (!SafeRead(membase + a, row, sizeof(row))) {
          ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%08X: <no mapeado>", a);
          continue;
        }
        for (int c = 0; c < 16; c++) {
          uint8_t v = row[c];
          n += std::snprintf(line + n, sizeof(line) - n, "%02X ", v);
          ascii[c] = (v >= 32 && v < 127) ? static_cast<char>(v) : '.';
        }
        ImGui::Text("%s %s", line, ascii);
      }
      ImGui::EndChild();
    }
  }

  if (ImGui::CollapsingHeader("Atajos")) {
    const auto& hk = Get().hotkeys;
    ImGui::BulletText("%s - este panel", HotkeyOr(hk.panel, "F8").c_str());
    ImGui::BulletText("%s - overlay de rendimiento (SDK)", HotkeyOr(hk.perf_overlay, "F9").c_str());
    ImGui::BulletText("%s - ajustes avanzados / cvars (SDK)", HotkeyOr(hk.settings, "F10").c_str());
    ImGui::BulletText("%s - consola (SDK)", HotkeyOr(hk.console, "Backtick").c_str());
    ImGui::BulletText("%s - logros (SDK)", HotkeyOr(hk.achievements, "F7").c_str());
    ImGui::BulletText("%s - pantalla completa", HotkeyOr(hk.fullscreen, "F11").c_str());
    ImGui::TextDisabled("Las teclas se cambian en el lanzador (pestana Jugar > Atajos del juego).");
    ImGui::TextDisabled("F1-F5 estan reservadas para los trastes.");
    ImGui::BulletText("REWOR_HANG_DUMP_SECONDS=N - volcar pilas de hilos tras N s (diagnostico)");
  }
}

}  // namespace re_wor::de
