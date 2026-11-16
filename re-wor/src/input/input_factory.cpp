#include "input/input_factory.hpp"

#include <rex/cvar.h>
#include <rex/input/device_assignment.h>
#include <rex/input/flags.h>
#include <rex/input/input_system.h>
#include <rex/input/mnk/mnk_input_driver.h>
#include <rex/input/nop/nop_input_driver.h>
#include <rex/input/sdl/sdl_input_driver.h>
#if defined(_WIN32)
#include <rex/input/xinput/xinput_input_driver.h>
#endif

#include "core/crash_handler.hpp"
#include "de/de_config.hpp"
#include "input/keyboard_guitar_driver.hpp"
#include "input/devices.hpp"
#include "input/mic_capture.hpp"

REXCVAR_DECLARE(bool, mnk_mode);

namespace re_wor::input {

extern rex::input::InputDriver* g_pad_driver;   // devices.cpp (lista de mandos de la pantalla Controles)

namespace {
// Reparto de dispositivos a los 4 mandos del juego segun [[input.players]]:
//  - teclado de un jugador (guid re-wor-kbd-p<N>) -> mando N
//  - mando fijado en un jugador (pad = guid) -> ese mando
//  - el resto de mandos, por orden de conexion (como antes)
//  - lo demas sintetico (teclado-raton MnK, el mando vacio) -> mando 1
class PlayerAssignment;
PlayerAssignment* g_assignment = nullptr;

class PlayerAssignment final : public rex::input::DeviceAssignment {
 public:
  PlayerAssignment() { g_assignment = this; }
  ~PlayerAssignment() override {
    if (g_assignment == this) g_assignment = nullptr;
  }
  // algun mando fisico (no el teclado) va al jugador `user`
  bool UserHasPhysical(uint32_t user) const {
    std::vector<rex::input::DeviceId> ids;
    DevicesForUser(user, ids);
    std::lock_guard lock(mutex_);
    for (const auto& d : devices_) {
      if (d.synthetic || d.guid.rfind("re-wor-kbd-p", 0) == 0) continue;
      for (auto id : ids)
        if (id == d.id) return true;
    }
    return false;
  }
  void OnDevicesChanged(const std::vector<rex::input::DeviceInfo>& devices) override {
    std::lock_guard lock(mutex_);
    devices_ = devices;
  }
  void DevicesForUser(uint32_t user, std::vector<rex::input::DeviceId>& out) const override {
    out.clear();
    if (user >= rex::input::kMaxGuestUsers) return;
    std::string pinned[re_wor::de::kMaxPlayers];
    {
      std::lock_guard cfg(re_wor::de::Mutex());
      for (int i = 0; i < re_wor::de::kMaxPlayers; i++) pinned[i] = re_wor::de::Get().input.players[i].pad;
    }
    std::lock_guard lock(mutex_);
    for (const auto& d : devices_) {
      if (d.guid.rfind("re-wor-kbd-p", 0) == 0) {
        if (static_cast<uint32_t>(d.guid.back() - '0') == user) out.push_back(d.id);
        continue;
      }
      if (d.synthetic) {
        if (user == 0) out.push_back(d.id);
        continue;
      }
      int pin = -1;
      for (int i = 0; i < re_wor::de::kMaxPlayers; i++) {
        if (!pinned[i].empty() && (pinned[i] == d.guid || pinned[i] == d.name)) pin = i;
      }
      if (pin >= 0 ? static_cast<uint32_t>(pin) == user : d.ordinal == user) out.push_back(d.id);
    }
  }

 private:
  mutable std::mutex mutex_;
  std::vector<rex::input::DeviceInfo> devices_;
};
}  // namespace

bool UpdatePlayerOneFallback(bool* nobody, bool apply) {
  auto* kb = KeyboardGuitarDriver::Instance();
  if (nobody) *nobody = false;
  if (!kb) return false;
  re_wor::de::PlayerSlotConfig pl[re_wor::de::kMaxPlayers];
  {
    std::lock_guard lock(re_wor::de::Mutex());
    for (int i = 0; i < re_wor::de::kMaxPlayers; i++) pl[i] = re_wor::de::Get().input.players[i];
  }
  std::vector<KeyboardInfo> kbs;
  bool listed = false;
  auto keyboard_ok = [&](const std::string& k) {
    if (k.empty()) return false;
    if (k == "*") return true;
    if (!listed) { kbs = ListKeyboards(); listed = true; }
    for (const auto& info : kbs)
      if (info.path == k) return true;
    return false;
  };
  // jugador 1: su teclado enchufado o un mando fisico que le toque
  bool p1 = keyboard_ok(pl[0].keyboard) || (g_assignment && g_assignment->UserHasPhysical(0));
  // alguien: cualquier mando conectado o cualquier teclado configurado y enchufado
  bool any = p1 || !ListPads().empty();
  for (int i = 1; i < re_wor::de::kMaxPlayers && !any; i++) any = keyboard_ok(pl[i].keyboard);
  if (apply) kb->SetMenuFallback(!p1);
  if (nobody) *nobody = !any;
  return !p1;
}

std::unique_ptr<rex::system::IInputSystem> CreateDefinitiveInputSystem(bool tool_mode) {
  auto input = std::make_unique<rex::input::InputSystem>(nullptr);

  re_wor::de::InputConfig cfg;
  {
    std::lock_guard lock(re_wor::de::Mutex());
    cfg = re_wor::de::Get().input;
  }

  if (!tool_mode) {
    bool real_driver = false;
#if defined(_WIN32)
    if (cfg.backend == "xinput") {
      auto xinput = std::make_unique<rex::input::xinput::XinputInputDriver>(nullptr, 0);
      if (xinput->Setup() == X_STATUS_SUCCESS) {
        g_pad_driver = xinput.get();
        input->AddDriver(std::move(xinput));
        real_driver = true;
        TRACE("[Input] Backend XInput nativo activo");
      }
    }
#endif
    if (!real_driver) {
      // SDL3 cubre XInput, DirectInput, HID y la mayoria de guitarras USB/adaptadores.
      REXCVAR_SET(input_backend, std::string("sdl"));
      auto sdl = std::make_unique<rex::input::sdl::SDLInputDriver>(nullptr, 0);
      if (sdl->Setup() == X_STATUS_SUCCESS) {
        g_pad_driver = sdl.get();
        input->AddDriver(std::move(sdl));
        TRACE("[Input] Backend SDL3 activo (XInput + DirectInput + HID)");
      } else {
        TRACE("[Input] ERROR: SDL3 no pudo iniciar el subsistema de mandos");
      }
    }

    // La guitarra por teclado siempre se registra; se activa/desactiva en vivo.
    auto kb_guitar = std::make_unique<KeyboardGuitarDriver>(nullptr, 0);
    if (kb_guitar->Setup() == X_STATUS_SUCCESS) {
      input->AddDriver(std::move(kb_guitar));
    }

    REXCVAR_SET(mnk_mode, cfg.keyboard_gamepad);
    auto mnk = std::make_unique<rex::input::mnk::MnkInputDriver>(nullptr, 0);
    if (mnk->Setup() == X_STATUS_SUCCESS) {
      input->AddDriver(std::move(mnk));
    }
  }

  input->AddDriver(std::make_unique<rex::input::nop::NopInputDriver>(nullptr, tool_mode ? 0 : 1));
  input->SetDeviceAssignment(std::make_unique<PlayerAssignment>());
  if (!tool_mode) {
    for (const auto& k : ListKeyboards()) TRACE("[Input] teclado: " << k.name << "  (" << k.path << ")");
    for (const auto& m : ListMics()) TRACE("[Input] microfono: " << m.name << (m.is_default ? "  (predeterminado)" : ""));
    ApplyMicConfig();   // microfonos de los jugadores (voz)
    UpdatePlayerOneFallback(nullptr, true);   // antes de que el juego vea los mandos
  }
  return input;
}

}  // namespace re_wor::input
