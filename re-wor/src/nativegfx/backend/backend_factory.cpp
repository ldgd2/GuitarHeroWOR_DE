// nativegfx/backend/backend_factory.cpp
// Implementacion de la fabrica de backends nativos.
#include "nativegfx/backend/backend_factory.hpp"

#ifdef _WIN32
#include "nativegfx/backend/d3d11/d3d11_renderer.hpp"
#include "nativegfx/backend/d3d12/d3d12_renderer.hpp"
#endif
#include "nativegfx/backend/vulkan/vulkan_renderer.hpp"

#include <algorithm>

namespace re_wor::nativegfx {

std::unique_ptr<INativeRenderer> BackendFactory::CreateRenderer(GraphicsBackendType type) {
  switch (type) {
#ifdef _WIN32
    case GraphicsBackendType::D3D11:
      return std::make_unique<D3D11Renderer>();
    case GraphicsBackendType::D3D12:
      return std::make_unique<D3D12Renderer>();
#endif
    case GraphicsBackendType::Vulkan:
      return std::make_unique<VulkanRenderer>();
    default:
#ifdef _WIN32
      return std::make_unique<D3D11Renderer>();
#else
      return std::make_unique<VulkanRenderer>();
#endif
  }
}

GraphicsBackendType BackendFactory::ParseBackendType(const std::string& name) {
  std::string lower = name;
  std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

  if (lower == "d3d12" || lower == "dx12" || lower == "directx12") {
    return GraphicsBackendType::D3D12;
  }
  if (lower == "vulkan" || lower == "vk") {
    return GraphicsBackendType::Vulkan;
  }
  return GraphicsBackendType::D3D11;
}

const char* BackendFactory::GetBackendName(GraphicsBackendType type) {
  switch (type) {
    case GraphicsBackendType::D3D11:  return "DirectX 11 (Legacy / Potato PC)";
    case GraphicsBackendType::D3D12:  return "DirectX 12 (Modern Windows)";
    case GraphicsBackendType::Vulkan: return "Vulkan 1.2+ (AMD / Linux / Cross-Platform)";
    default:                          return "Desconocido";
  }
}

}  // namespace re_wor::nativegfx
