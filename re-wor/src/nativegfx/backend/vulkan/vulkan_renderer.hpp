// nativegfx/backend/vulkan/vulkan_renderer.hpp
// Implementacion completa del renderizador nativo Vulkan 1.2+ (Fase 3 y 4).
#pragma once

#include <vulkan/vulkan.h>
#include <memory>
#include <vector>
#include <unordered_map>
#include <string>
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

  void SetVSync(bool vsync) override { vsync_ = vsync; }
  bool GetVSync() const override { return vsync_; }

  void SetAnisotropy(uint32_t level) override { aniso_ = level; }
  uint32_t GetAnisotropy() const override { return aniso_; }

 private:
  bool CreateDescriptorLayouts();
  bool CreatePipelineLayout();
  VkPipeline GetOrCreatePipeline(const DrawCommand& cmd);

  VulkanDeviceContext device_ctx_;

  VkDescriptorSetLayout descriptor_set_layout_ = VK_NULL_HANDLE;
  VkPipelineLayout pipeline_layout_            = VK_NULL_HANDLE;

  std::unordered_map<std::string, VkPipeline> pipeline_cache_;
  VkPipeline last_pipeline_ = VK_NULL_HANDLE;
  VkPipeline last_bound_pipe_ = VK_NULL_HANDLE;
  uint64_t last_vs_hash_ = 0;
  uint64_t last_ps_hash_ = 0;
  uint32_t last_topo_ = 0;

  bool is_frame_active_ = false;
  bool vsync_ = false;
  uint32_t aniso_ = 16;
};

}  // namespace re_wor::nativegfx
