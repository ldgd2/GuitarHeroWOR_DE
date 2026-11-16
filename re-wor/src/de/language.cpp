// Idioma del juego: [game] language de re_wor_de.toml (en, es, fr, de, it).
//
// WoR ya trae los textos traducidos: pak/qs.pak (ingles), qs_s (espanol), qs_f (frances), qs_g (aleman) y
// qs_i (italiano), mas pantallas de carga con texto (*_s.img...). El motor guarda el idioma como una clave interna
// en 0x82860430 y de ahi saca el sufijo de los archivos:
//   - sub_82403230 devuelve el sufijo ("_s", "_f", "_g", "_i"; ingles sin sufijo)
//   - sub_82403310 devuelve el nombre del idioma
//   - sub_824AD288 lo pone al arrancar y SetConfig (CFunc 0x824FFAB0) lo cambia desde los scripts
// Claves internas (de SetConfig): ingles 0x77B12AAB, espanol 0x6F2ADD9F, frances 0x4C9908C7, aleman 0x41D88638,
// italiano 0x0C8B4164.
//
// Aqui se fija esa clave al idioma elegido cada vez que el motor la consulta o la cambia. Ademas el SDK informa ese
// idioma por XGetLanguage / XConfig (cvar user_language, ver re_wor_app.cpp). Requiere reiniciar: los textos se
// cargan al arrancar.
#include <cstdint>
#include <string>

#include "core/crash_handler.hpp"
#include "de/de_config.hpp"
#include "generated/re_wor_pch.h"

namespace {
constexpr uint32_t kLanguageAddr = 0x82860430;

uint32_t WantedKey() {
  static const uint32_t key = [] {
    const std::string l = re_wor::de::Get().game.language;
    return l == "es" ? 0x6F2ADD9Fu : l == "fr" ? 0x4C9908C7u : l == "de" ? 0x41D88638u : l == "it" ? 0x0C8B4164u : 0u;
  }();
  return key;   // 0 = ingles: no se toca nada
}

void Enforce(uint8_t* base) {
  const uint32_t want = WantedKey();
  if (!want) return;
  if (REX_LOAD_U32(kLanguageAddr) != want) {
    static bool logged = false;
    if (!logged) {
      logged = true;
      TRACE("[Idioma] clave del motor 0x" << std::hex << REX_LOAD_U32(kLanguageAddr) << " -> 0x" << want << std::dec);
    }
    REX_STORE_U32(kLanguageAddr, want);
  }
}
}  // namespace

extern "C" void __imp__sub_82403230(PPCContext& __restrict ctx, uint8_t* base);
extern "C" REX_FUNC(sub_82403230) {   // sufijo de idioma de los archivos
  Enforce(base);
  __imp__sub_82403230(ctx, base);
}

extern "C" void __imp__sub_82403310(PPCContext& __restrict ctx, uint8_t* base);
extern "C" REX_FUNC(sub_82403310) {   // nombre del idioma
  Enforce(base);
  __imp__sub_82403310(ctx, base);
}

extern "C" void __imp__sub_824AD288(PPCContext& __restrict ctx, uint8_t* base);
extern "C" REX_FUNC(sub_824AD288) {   // idioma al arrancar
  __imp__sub_824AD288(ctx, base);
  Enforce(base);
}

extern "C" void __imp__sub_824FFAB0(PPCContext& __restrict ctx, uint8_t* base);
extern "C" REX_FUNC(sub_824FFAB0) {   // SetConfig (scripts)
  __imp__sub_824FFAB0(ctx, base);
  Enforce(base);
}
