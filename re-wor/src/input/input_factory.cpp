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

REXCVAR_DECLARE(bool, mnk_mode);

namespace re_wor::input {

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
  input->SetDeviceAssignment(std::make_unique<rex::input::SlotAssignment>());
  return input;
}

}  // namespace re_wor::input
