#include "app/re_wor_app.hpp"
#include "core/path_resolver.hpp"
#include "core/crash_handler.hpp"
#include "core/timer_resolution.hpp"
#include "hooks/init/crt_init.hpp"
#include "integrations/discord/discord_rpc.hpp"
#include "mods/mod_vfs.hpp"
#include "mods/vfs_mod_loader.hpp"
#include "memory/memory_heaps.hpp"
#include "input/input_manager.hpp"
#include "input/input_factory.hpp"
#include "de/de_config.hpp"
#include "de/de_hooks.hpp"
#include "de/de_panel.hpp"
#include "de/qb_bridge.hpp"
#include "de/gfx_options.hpp"
#include "de/edfx.hpp"
#include "de/render_sandbox.hpp"
#include "diagnostics/stubs.hpp"
#include "generated/re_wor_init.h"
#include "generated/re_wor_pch.h"

#include <rex/audio/sdl/sdl_audio_system.h>
#include <rex/input/input_system.h>
#include <rex/ui/keybinds.h>
#include <rex/cvar.h>
#include <rex/system/xmemory.h>
#include <rex/filesystem.h>
#include <rex/chrono/clock.h>

#ifdef _WIN32
#include <windows.h>
#include <set>
#include <thread>
#endif

#ifdef REWOR_DEV
#include "diagnostics/stub_tracker.hpp"
#endif

ReWorApp::~ReWorApp() {
  re_wor::integrations::discord::shutdown();
}

void ReWorApp::OnConfigurePaths(rex::PathConfig& paths) {
  TRACE("[ReWorApp::OnConfigurePaths] Resolving application paths dynamically...");

  auto base_dir = re_wor::core::get_base_directory();
  auto assets_dir = re_wor::core::get_assets_directory();
  auto user_dir = re_wor::core::get_user_directory();
  auto cache_dir = re_wor::core::get_cache_directory();

  std::error_code ec;
  std::filesystem::create_directories(user_dir, ec);
  std::filesystem::create_directories(cache_dir, ec);

  paths.game_data_root = assets_dir;
  paths.user_data_root = user_dir;
  paths.cache_root = cache_dir;

  TRACE("[ReWorApp::OnConfigurePaths] Base Directory: " << base_dir.string());
  TRACE("[ReWorApp::OnConfigurePaths] Game Data Root: " << paths.game_data_root.string());
  TRACE("[ReWorApp::OnConfigurePaths] User Data Root: " << paths.user_data_root.string());
  TRACE("[ReWorApp::OnConfigurePaths] Cache Root:     " << paths.cache_root.string());
}

std::optional<rex::PathConfig> ReWorApp::OnFinalizePaths(
    const rex::PathConfig& defaults,
    std::function<void(rex::PathConfig)> resume) {
  (void)resume;
  rex::PathConfig paths = defaults;

  // Garantizar que game_data_root NUNCA esté vacío
  if (paths.game_data_root.empty() || !std::filesystem::is_directory(paths.game_data_root)) {
    paths.game_data_root = re_wor::core::get_assets_directory();
    std::error_code ec;
    if (!std::filesystem::is_directory(paths.game_data_root, ec)) {
      std::filesystem::create_directories(paths.game_data_root, ec);
    }
  }

  if (paths.user_data_root.empty()) {
    paths.user_data_root = re_wor::core::get_user_directory();
  }
  if (paths.cache_root.empty()) {
    paths.cache_root = re_wor::core::get_cache_directory();
  }

  TRACE("[ReWorApp::OnFinalizePaths] Final Game Root: " << paths.game_data_root.string());
  return paths;
}

void ReWorApp::OnPreSetup(rex::RuntimeConfig& config) {
  TRACE("[ReWorApp::OnPreSetup] Initializing audio and input systems");
  config.audio_factory = REX_AUDIO_BACKEND(rex::audio::sdl::SDLAudioSystem);

  // Configuracion de la Definitive Edition (controles, mods, dev).
  re_wor::de::Load();
  if (!std::filesystem::exists(re_wor::de::ConfigPath())) {
    re_wor::de::Save();  // crear el archivo con los valores por defecto
  }
  re_wor::de::SetLogFileOpens(re_wor::de::Get().dev.log_file_opens);
  re_wor::de::SetTraceProbes(re_wor::de::Get().perf.trace_probes);
#ifdef _WIN32
  // Por encima de lo normal (no "alta"): el juego no pierde CPU frente a programas en segundo
  // plano, sin quitarsela al audio ni a la entrada del sistema.
  if (re_wor::de::Get().perf.above_normal_priority) {
    SetPriorityClass(GetCurrentProcess(), ABOVE_NORMAL_PRIORITY_CLASS);
    TRACE("[Perf] Prioridad del proceso: por encima de lo normal");
  }
#endif

  // Entrada: SDL3 (XInput + DirectInput + HID) o XInput, guitarra por teclado,
  // teclado como mando (opcional) y NOP.
  config.input_factory = REX_INPUT_BACKEND(re_wor::input::CreateDefinitiveInputSystem);

  // rexglue-sdk 0.10 carga la GPU como plugin (rexgpu-<nombre>.dll) segun la
  // cvar `gpu_plugin`, que por defecto esta VACIA = sin emulacion de GPU.
  // Sin GPU no hay imagen y nunca llegan las interrupciones de VBlank, por lo
  // que el hilo de render del juego se queda esperando para siempre (pantalla
  // negra "colgada" durante la carga). El SDK viejo que usaba el release
  // enlazaba rex::graphics directamente, por eso alli si funcionaba.
  // Se respeta un valor explicito pasado por linea de comandos/TOML.
  if (!config.graphics && config.gpu_plugin.empty()) {
    config.gpu_plugin = "xenos";
    TRACE("[ReWorApp::OnPreSetup] gpu_plugin vacio -> usando 'xenos'");
  }

  // Optimizaciones de traduccion grafica para PC (Direct3D 12 - Hardware RTV & Cache):
  // RTV es el camino mas rapido y compatible para Nvidia, AMD e Intel (evita el costo de ROV).
  const auto& gfx = re_wor::de::Get().graphics;
  if (!gfx.render_target_path.empty()) {
    rex::cvar::SetFlagByName("render_target_path_d3d12", gfx.render_target_path);
  } else {
    rex::cvar::SetFlagByName("render_target_path_d3d12", "rtv");
  }
  if (gfx.resolution_scale > 0) {
    rex::cvar::SetFlagByName("draw_resolution_scale_x", std::to_string(gfx.resolution_scale));
    rex::cvar::SetFlagByName("draw_resolution_scale_y", std::to_string(gfx.resolution_scale));
  }
  int cache_mb = gfx.texture_cache_mb > 0 ? gfx.texture_cache_mb : 1024;
  rex::cvar::SetFlagByName("texture_cache_memory_limit_soft", std::to_string(cache_mb));
  rex::cvar::SetFlagByName("texture_cache_memory_limit_hard", std::to_string(cache_mb * 2));
  if (gfx.async_pipeline_compile) {
    rex::cvar::SetFlagByName("d3d12_pipeline_creation_threads", "-1");
  }
  // Opciones graficas del panel (pestana Graficos): todas son de arranque en el SDK
  re_wor::de::ApplyDisplayOptions(gfx);
  rex::cvar::SetFlagByName("d3d12_readback_resolve", "false");
  rex::cvar::SetFlagByName("d3d12_submit_on_primary_buffer_end", "true");
}

void ReWorApp::OnPostSetup() {
  TRACE("[ReWorApp::OnPostSetup] XEX loaded, runtime ready");

  // RELOJ DEL GUEST (causa de la "camara lenta"):
  // El exe enlaza su PROPIA copia de rexcore (clock.cpp) y la DLL del runtime
  // otra. El runtime fija la frecuencia del guest a 50 MHz solo en la copia de
  // la DLL (KeQueryPerformanceFrequency devuelve 50 MHz), pero el codigo
  // recompilado lee `mftb` (REX_QUERY_TIMEBASE) de la copia del exe, que se
  // quedaba en la frecuencia del host (10 MHz). Resultado: el temporizador del
  // motor (Tmr) iba 5 veces mas lento que el audio/VBlank. Se sincroniza aqui
  // la copia del exe con el valor del runtime.
  {
    const uint64_t guest_hz = 50000000ull;
    rex::chrono::Clock::set_guest_tick_frequency(guest_hz);
    auto ratio = rex::chrono::Clock::guest_tick_ratio();
    TRACE("[ReWorApp::OnPostSetup] Reloj del guest (exe) = " << guest_hz << " Hz, ratio host->guest "
          << ratio.first << "/" << ratio.second);
  }

  // Diagnostico: buscar cabeceras FSB descifradas en la memoria del guest.
  // REWOR_SCAN_FSB=<carpeta>  -> cada 10 s vuelca 8 KB de cada "FSB4" encontrado.
  if (const char* scan_dir = std::getenv("REWOR_SCAN_FSB")) {
    uint8_t* membase = runtime() ? runtime()->virtual_membase() : nullptr;
    std::string dir = scan_dir;
    if (membase) {
      std::thread([membase, dir]() {
        std::set<std::string> seen;
        for (int pass = 0; pass < 30; ++pass) {
          std::this_thread::sleep_for(std::chrono::seconds(10));
          int found = 0;
          uint8_t* p = membase;
          uint8_t* end = membase + 0x100000000ull;
          MEMORY_BASIC_INFORMATION mbi;
          while (p < end && VirtualQuery(p, &mbi, sizeof(mbi))) {
            uint8_t* rb = static_cast<uint8_t*>(mbi.BaseAddress);
            uint8_t* re = rb + mbi.RegionSize;
            if (mbi.State == MEM_COMMIT && (mbi.Protect & (PAGE_READWRITE | PAGE_READONLY)) &&
                !(mbi.Protect & PAGE_GUARD)) {
              for (uint8_t* q = rb; q + 8192 <= re; q += 16) {
                if (q[0] == 'F' && q[1] == 'S' && q[2] == 'B' && q[3] == '4') {
                  std::string nm(reinterpret_cast<const char*>(q + 50), 30);
                  nm = nm.c_str();
                  if (!seen.insert(nm).second) continue;
                  uint32_t guest = static_cast<uint32_t>(q - membase);
                  char name[512];
                  std::snprintf(name, sizeof(name), "%s\\fsb_p%02d_%08X.bin", dir.c_str(), pass, guest);
                  if (FILE* f = std::fopen(name, "wb")) {
                    std::fwrite(q, 1, 8192, f);
                    std::fclose(f);
                  }
                  ++found;
                }
              }
            }
            p = re;
          }
          TRACE("[ScanFSB] pasada " << pass << ": " << found << " cabeceras FSB4");
        }
      }).detach();
    }
  }

  // Diagnostico: volcar la imagen del XEX cargada (0x82000000..) a disco.
  if (const char* dump = std::getenv("REWOR_DUMP_IMAGE")) {
    uint8_t* membase = runtime() ? runtime()->virtual_membase() : nullptr;
    if (membase) {
      std::string path = dump;
      int delay = 0;
      if (const char* d = std::getenv("REWOR_DUMP_DELAY")) delay = std::atoi(d);
      std::thread([membase, path, delay]() {
        std::this_thread::sleep_for(std::chrono::seconds(delay));
        if (FILE* f = std::fopen(path.c_str(), "wb")) {
          std::fwrite(membase + 0x82000000u, 1, 0x00C00000u, f);
          std::fclose(f);
        }
      }).detach();
    }
  }

  uint8_t* base = runtime()->virtual_membase();

  // Rellenar entradas nulas en la tabla de despacho
  re_wor::hooks::init::fill_dispatch_table(base);

  // Inicializar VFS Multi-Root
  re_wor::mods::initialize_mod_vfs(runtime());

  // Inicializar cargador VFS 5-tier y expansión de memoria a 4GB
  re_wor::mods::ModLoaderConfig mod_cfg;
  auto base_dir = re_wor::core::get_base_directory();
  mod_cfg.baked_data_path = base_dir / "mods" / "baked" / "data";
  mod_cfg.raw_data_path = base_dir / "mods" / "raw" / "data";
  mod_cfg.song_mods_path = base_dir / "mods" / "song";
  mod_cfg.base_mods_path = base_dir / "mods";
  mod_cfg.assets_data_path = re_wor::core::get_assets_directory() / "data";
  mod_cfg.assets_path = re_wor::core::get_assets_directory();

  re_wor::mods::InitializeVFSModLoader(runtime(), mod_cfg);

  // Inicializar orquestador de Entrada unificada (Guitarras, Baterías, Bajo, Micrófono, Gamepad)
  re_wor::input::InputManager::Instance().Initialize(runtime());

  // Discord Rich Presence
  re_wor::integrations::discord::initialize();
  re_wor::integrations::discord::update();

  // Pantalla completa (tecla configurable en [hotkeys], por defecto F11)
  rex::ui::RegisterBind("fullscreen_toggle", re_wor::de::HotkeyOr(re_wor::de::Get().hotkeys.fullscreen, "F11"),
                        "Toggle fullscreen", [this]() {
    window()->SetFullscreen(!window()->IsFullscreen());
  });

  // Saltar la intro (videos del arranque) con F ([hotkeys].skip_intro)
  rex::ui::RegisterBind("bind_skip_intro", re_wor::de::HotkeyOr(re_wor::de::Get().hotkeys.skip_intro, "F"),
                        "Saltar intro", []() { re_wor::de::RequestSkipIntro(); });

  // Inicializar estado de DebugBuild y rendimiento desde configuracion
  re_wor::de::qb_bridge::SetDebugBuild(re_wor::de::Get().dev.debug_build);
  re_wor::de::qb_bridge::StartServer(re_wor::de::Get().dev.bridge_port);   // puerto local para el editor
  // memoria del juego para los scripts propios (qb/*.qb.xen); se reserva una vez y no se libera
  re_wor::de::qb_bridge::SetGuestAllocator([rt = runtime()](uint32_t size) -> uint32_t {
    return rt && rt->memory() ? rt->memory()->SystemHeapAlloc(size, 0x20) : 0;
  });
  // 'vsync' del SDK es el reloj de VBlank del juego (no el del monitor): apagado gira a 1000 Hz y da
  // tirones, asi que se deja siempre encendido. La frecuencia del juego es [graphics] refresh_hz
  // (video_mode_refresh_rate, en ApplyDisplayOptions) y el VSync del monitor es [graphics] vsync.
  rex::cvar::SetFlagByName("vsync", "true");

#ifdef REWOR_DEV
  re_wor::diagnostics::StubTracker::instance().start_reporter(5);
#endif

  re_wor::de::sandbox::Install();

  TRACE("[ReWorApp::OnPostSetup] Setup completed successfully");
}

void ReWorApp::OnCreateDialogs(rex::ui::ImGuiDrawer* imgui) {
  re_wor::de::CreateFpsCounter(imgui);   // [graphics] show_fps (Options > Graphics > Show FPS)
  // Panel de la Definitive Edition (controles, mods, musica/skins, dev). Tecla en [hotkeys].panel.
  const auto& hk = re_wor::de::Get().hotkeys;
  rex::ui::RegisterBind("bind_de_panel", re_wor::de::HotkeyOr(hk.panel, "F8"), "Panel Definitive Edition",
                        [this, imgui]() { re_wor::de::DePanelDialog::Toggle(imgui, runtime()); });

  // F6: abre el menu de depuracion original de Neversoft y la pestana Dev del panel
  rex::ui::RegisterBind("bind_debug_menu", re_wor::de::HotkeyOr(hk.debug_menu, "F6"),
                        "Menu de depuracion", [this, imgui]() {
    re_wor::de::qb_bridge::OpenDebugMenu();
    re_wor::de::DePanelDialog::Toggle(imgui, runtime(), 4);
  });
  // Capa de efectos del editor de canciones (EDFX): siempre presente, solo dibuja durante las canciones que
  // traen un .edfx. Se borra sola con el drawer.
  new re_wor::de::edfx::EdfxOverlay(imgui);
  // Atajos del SDK (ya registrados con sus teclas de fabrica): se aplican las del toml.
  const std::pair<const char*, std::string> sdk_binds[] = {
      {"bind_debug_overlay", re_wor::de::HotkeyOr(hk.perf_overlay, "F9")},
      {"bind_settings", re_wor::de::HotkeyOr(hk.settings, "F10")},
      {"bind_console", re_wor::de::HotkeyOr(hk.console, "Backtick")},
      {"bind_achievements", re_wor::de::HotkeyOr(hk.achievements, "F7")}};
  for (const auto& [name, key] : sdk_binds) {
    if (!rex::cvar::SetFlagByName(name, key)) {
      TRACE("[ReWorApp] No se pudo asignar " << name << " = " << key);
    }
  }
#ifdef _WIN32
  HWND hwnd = static_cast<HWND>(window()->GetNativeWindowHandle());
  if (hwnd) {
    HICON icon = LoadIconA(GetModuleHandleA(NULL), "IDI_ICON1");
    if (icon) {
      SendMessageA(hwnd, WM_SETICON, ICON_BIG, (LPARAM)icon);
      SendMessageA(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)icon);
    }
  }
#endif
  // Arrancar en modo ventana por defecto (permite ver la consola y evita pantalla negra fija)
  window()->SetFullscreen(false);
}

void ReWorApp::OnGuestThreadExit(rex::system::XThread* thread) {
  uint32_t thread_id = thread ? thread->thread_id() : 0;
  TRACE("[ReWorApp::OnGuestThreadExit] Guest thread 0x" << std::hex << thread_id << " exited");
  re_wor::core::flush_trace();
}

void ReWorApp::OnShutdown() {
  TRACE("[ReWorApp::OnShutdown] Application shutting down");
  re_wor::core::flush_trace();
  re_wor::core::disable_high_resolution_timer();
}

std::unique_ptr<rex::ui::WindowedApp> ReWorApp::Create(
    rex::ui::WindowedAppContext& ctx) {
  re_wor::core::install_crash_handlers();
  // Antes que cualquier hilo del juego: sin esto todo el audio va lento.
  re_wor::core::enable_high_resolution_timer();

  TRACE("[ReWorApp::Create] Constructing ReWorApp");
  auto app = std::unique_ptr<ReWorApp>(new ReWorApp(
      ctx, "Guitar Hero Warriors of Rock",
      {REX_CODE_BASE, REX_CODE_SIZE, REX_IMAGE_BASE,
       REX_IMAGE_SIZE, PPCFuncMappings}));
  TRACE("[ReWorApp::Create] Done");
  return app;
}

