#pragma once

#include <rex/rex_app.h>
#include <rex/runtime.h>
#include <memory>

class ReWorApp : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;

  ~ReWorApp() override;

  void OnConfigurePaths(rex::PathConfig& paths) override;

  std::optional<rex::PathConfig> OnFinalizePaths(
      const rex::PathConfig& defaults,
      std::function<void(rex::PathConfig)> resume) override;

  void OnPreSetup(rex::RuntimeConfig& config) override;

  void OnPostSetup() override;

  void OnCreateDialogs(rex::ui::ImGuiDrawer* imgui) override;

  void OnGuestThreadExit(rex::system::XThread* thread) override;

  void OnShutdown() override;

  static std::unique_ptr<rex::ui::WindowedApp> Create(
      rex::ui::WindowedAppContext& ctx);
};

