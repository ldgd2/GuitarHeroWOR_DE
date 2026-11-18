// nativegfx/backend/backend_factory.hpp
// Fabrica para instanciar el backend nativo correspondiente (D3D11, D3D12 o Vulkan).
#pragma once

#include <memory>
#include <string>
#include "nativegfx/backend/native_renderer.hpp"

namespace re_wor::nativegfx {

enum class GraphicsBackendType {
  D3D11 = 0,   // DirectX 11 (Max compatibilidad, APUs e integradas antiguas)
  D3D12,       // DirectX 12 (Nativo de Windows 10/11)
  Vulkan,      // Vulkan 1.2+ (APUs AMD, Linux, Steam Deck y PCs modernas)
};

class BackendFactory {
 public:
  static std::unique_ptr<INativeRenderer> CreateRenderer(GraphicsBackendType type);
  static GraphicsBackendType ParseBackendType(const std::string& name);
  static const char* GetBackendName(GraphicsBackendType type);
};

}  // namespace re_wor::nativegfx
