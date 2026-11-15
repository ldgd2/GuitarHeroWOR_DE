#pragma once
// Panel "Definitive Edition" (F8 por defecto, configurable): controles, mods, musica/skins custom y dev.

#include <rex/ui/imgui_dialog.h>

#include <string>
#include <vector>

#include "de/mod_manager.hpp"

namespace rex {
class Runtime;
}

namespace re_wor::de {

class DePanelDialog final : public rex::ui::ImGuiDialog {
 public:
  DePanelDialog(rex::ui::ImGuiDrawer* drawer, rex::Runtime* runtime);
  ~DePanelDialog() override;

  // Abre o cierra el panel (F8 por defecto, configurable). El dialogo se borra solo al cerrarse.
  static void Toggle(rex::ui::ImGuiDrawer* drawer, rex::Runtime* runtime, int select_tab = -1);
  static bool IsOpen();

 protected:
  void OnDraw(ImGuiIO& io) override;

 private:
  void DrawControlsTab();
  void DrawDevicesSection();
  void DrawModsTab();
  void DrawContentTab(const char* type_filter_a, const char* type_filter_b, const char* help);
  void DrawDevTab();
  void DrawGraphicsTab();
  void RunConsoleLine(const std::string& line);

  void RefreshMods();

  rex::Runtime* runtime_ = nullptr;
  bool blocking_input_ = false;

  // Reasignacion de teclas
  int capturing_action_ = -1;
  bool capture_append_ = false;
  bool controls_dirty_ = false;

  // Mods
  std::vector<ModInfo> mods_;
  bool mods_dirty_ = false;
  bool restart_needed_ = false;
  char new_mod_name_[64] = {};
  int new_mod_type_ = 0;
  std::string status_;

  // Dev
  int selected_tab_ = -1;
  char open_filter_[96] = {};
  bool only_failed_ = false;
  char peek_addr_[16] = "82000000";
  int peek_rows_ = 8;
  char script_input_[256] = "";
  char global_input_[96] = "";
  char value_input_[64] = "";
  bool allow_unlock_ = false;
  char script_filter_[64] = "";
  bool symbols_scanned_ = false;
  std::vector<std::string> history_;   // ordenes recientes de la consola
  bool scroll_log_ = false;
  bool focus_console_ = false;
  struct Watch {
    std::string name;
    char edit[48];
  };
  std::vector<Watch> watches_;         // globales vigiladas
};

}  // namespace re_wor::de
