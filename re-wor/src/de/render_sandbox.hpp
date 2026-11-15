// Sandbox de render: pasada propia en la GPU con los efectos del editor (ver render_sandbox.cpp).
#pragma once

namespace re_wor::de::sandbox {
void Install();        // registra los ganchos de GPU (OnPostSetup; se reintenta desde OnLogicFrame)
void OnLogicFrame();   // desde el frame logico del juego

// Que debe hacer native_fx con el post-proceso del motor
enum class NativeMode {
  kNormal,       // como siempre (sin sandbox)
  kMarkerOnly,   // calibrando: solo el marcador neutro encendido
  kAllOff,       // calibrando: todo apagado
  kSandbox,      // listo: marcador neutro y efectos en la pasada sandbox (si DrawsImage())
};
NativeMode Mode();
// true si la pasada sandbox dibuja los efectos de imagen: native_fx y la capa ImGui no deben duplicarlos
bool DrawsImage();
}  // namespace re_wor::de::sandbox
