// nativegfx/backend/vulkan/vulkan_renderer.hpp
// Implementacion completa del renderizador nativo Vulkan 1.2+ (Fase 3 y 4).
#pragma once

#include <vulkan/vulkan.h>
#include <memory>
#include <vector>
#include "nativegfx/backend/native_renderer.hpp"
#include "nativegfx/backend/vulkan/vulkan_device.hpp"

namespace re_wor::nativegfx {

class VulkanRenderer : public INativeRenderer {
 public:
  VulkanRenderer();
  ~VulkanRenderer() override;

  bool Initialize(void* native_window_handle, uint32_t width, uint32_t height) override;
  void Shutdown() override;

  void BeginFrame() override;
  void EndFrame() override;
  void Present() override;

  void RenderDraw(const DrawCommand& cmd, const uint8_t* guest_memory_base) override;
  void RenderResolve(const ResolveCommand& cmd) override;

  void Resize(uint32_t width, uint32_t height) override;

  uint32_t GetNativeWidth() const override { return device_ctx_.GetWidth(); }
  uint32_t GetNativeHeight() const override { return device_ctx_.GetHeight(); }
  float GetResolutionScale() const override { return device_ctx_.GetResolutionScale(); }

 private:
  bool CreateDescriptorLayouts();
  bool CreatePipelineLayout();

  VulkanDeviceContext device_ctx_;

  VkDescriptorSetLayout descriptor_set_layout_ = VK_NULL_HANDLE;
  VkPipelineLayout pipeline_layout_            = VK_NULL_HANDLE;

  bool is_frame_active_ = false;
};

}  // namespace re_wor::nativegfx
