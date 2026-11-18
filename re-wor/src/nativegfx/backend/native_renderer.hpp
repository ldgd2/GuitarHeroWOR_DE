// nativegfx/backend/native_renderer.hpp
// Interfaz abstracta del Renderizador Nativo (Fase 3 y 4).
// Permite desacoplar el pipeline de renderizado de la API grafica subyacente (D3D11, D3D12, Vulkan).
#pragma once

#include <cstdint>
#include "nativegfx/device/draw_command.hpp"
#include "nativegfx/device/resolve_command.hpp"

namespace re_wor::nativegfx {

class INativeRenderer {
 public:
  virtual ~INativeRenderer() = default;

  // Inicializacion del dispositivo grafico nativo asociado a una ventana
  virtual bool Initialize(void* native_window_handle, uint32_t width, uint32_t height) = 0;

  // Liberacion de recursos
  virtual void Shutdown() = 0;

  // Control de frame
  virtual void BeginFrame() = 0;
  virtual void EndFrame() = 0;
  virtual void Present() = 0;

  // Ejecucion de comandos desacoplados
  virtual void RenderDraw(const DrawCommand& cmd, const uint8_t* guest_memory_base) = 0;
  virtual void RenderResolve(const ResolveCommand& cmd) = 0;

  // Redimensionado de framebuffer nativo (1080p, 1440p, 4K)
  virtual void Resize(uint32_t width, uint32_t height) = 0;

  // Resolucion nativa actual
  virtual uint32_t GetNativeWidth() const = 0;
  virtual uint32_t GetNativeHeight() const = 0;
  virtual float GetResolutionScale() const = 0;

  // Opciones graficas en caliente
  virtual void SetVSync(bool vsync) {}
  virtual bool GetVSync() const { return false; }
  virtual void SetAnisotropy(uint32_t level) {}
  virtual uint32_t GetAnisotropy() const { return 1; }
};

}  // namespace re_wor::nativegfx
