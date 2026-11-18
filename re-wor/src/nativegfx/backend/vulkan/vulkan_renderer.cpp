// nativegfx/backend/vulkan/vulkan_renderer.cpp
// Implementacion del renderizador nativo Vulkan 1.2+.
#include "nativegfx/backend/vulkan/vulkan_renderer.hpp"

#include <cstring>

namespace re_wor::nativegfx {

VulkanRenderer::VulkanRenderer() = default;

VulkanRenderer::~VulkanRenderer() {
  Shutdown();
}

bool VulkanRenderer::Initialize(void* native_window_handle, uint32_t width, uint32_t height) {
  if (!device_ctx_.Initialize(native_window_handle, width, height)) {
    return false;
  }

  if (!CreateDescriptorLayouts()) {
    return false;
  }

  if (!CreatePipelineLayout()) {
    return false;
  }

  return true;
}

bool VulkanRenderer::CreateDescriptorLayouts() {
  VkDevice dev = device_ctx_.GetDevice();
  const auto& vk = device_ctx_.GetDispatch();
  if (!dev || !vk.vkCreateDescriptorSetLayout) return false;

  std::vector<VkDescriptorSetLayoutBinding> bindings;

  for (uint32_t i = 0; i < 4; ++i) {
    VkDescriptorSetLayoutBinding b = {};
    b.binding = i;
    b.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    b.descriptorCount = 1;
    b.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings.push_back(b);
  }

  // Binding 16: xe_mem
  VkDescriptorSetLayoutBinding mem_b = {};
  mem_b.binding = 16;
  mem_b.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  mem_b.descriptorCount = 1;
  mem_b.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
  bindings.push_back(mem_b);

  VkDescriptorSetLayoutCreateInfo layout_ci = {};
  layout_ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  layout_ci.bindingCount = static_cast<uint32_t>(bindings.size());
  layout_ci.pBindings = bindings.data();

  return vk.vkCreateDescriptorSetLayout(dev, &layout_ci, nullptr, &descriptor_set_layout_) == VK_SUCCESS;
}

bool VulkanRenderer::CreatePipelineLayout() {
  VkDevice dev = device_ctx_.GetDevice();
  const auto& vk = device_ctx_.GetDispatch();
  if (!dev || !vk.vkCreatePipelineLayout || descriptor_set_layout_ == VK_NULL_HANDLE) return false;

  VkPipelineLayoutCreateInfo pl_ci = {};
  pl_ci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pl_ci.setLayoutCount = 1;
  pl_ci.pSetLayouts = &descriptor_set_layout_;

  return vk.vkCreatePipelineLayout(dev, &pl_ci, nullptr, &pipeline_layout_) == VK_SUCCESS;
}

void VulkanRenderer::BeginFrame() {
  is_frame_active_ = true;
  device_ctx_.WaitForFences();

  const auto& vk = device_ctx_.GetDispatch();
  VkCommandBuffer cb = device_ctx_.GetCurrentCommandBuffer();
  if (!cb || !vk.vkResetCommandBuffer || !vk.vkBeginCommandBuffer) return;

  vk.vkResetCommandBuffer(cb, 0);

  VkCommandBufferBeginInfo begin_info = {};
  begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  vk.vkBeginCommandBuffer(cb, &begin_info);

  VkFramebuffer fb = device_ctx_.GetCurrentFramebuffer();
  if (fb != VK_NULL_HANDLE && vk.vkCmdBeginRenderPass) {
    VkClearValue clear_color = {{{0.0f, 0.0f, 0.0f, 1.0f}}};
    VkRenderPassBeginInfo rp_begin = {};
    rp_begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rp_begin.renderPass = device_ctx_.GetRenderPass();
    rp_begin.framebuffer = fb;
    rp_begin.renderArea.offset = {0, 0};
    rp_begin.renderArea.extent = {device_ctx_.GetWidth(), device_ctx_.GetHeight()};
    rp_begin.clearValueCount = 1;
    rp_begin.pClearValues = &clear_color;

    vk.vkCmdBeginRenderPass(cb, &rp_begin, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport vp = {0.0f, 0.0f, float(device_ctx_.GetWidth()), float(device_ctx_.GetHeight()), 0.0f, 1.0f};
    VkRect2D sc = {{0, 0}, {device_ctx_.GetWidth(), device_ctx_.GetHeight()}};
    if (vk.vkCmdSetViewport) vk.vkCmdSetViewport(cb, 0, 1, &vp);
    if (vk.vkCmdSetScissor) vk.vkCmdSetScissor(cb, 0, 1, &sc);
  }
}

void VulkanRenderer::RenderDraw(const DrawCommand& cmd, const uint8_t* guest_memory_base) {
  const auto& vk = device_ctx_.GetDispatch();
  VkCommandBuffer cb = device_ctx_.GetCurrentCommandBuffer();
  if (!cb) return;

  if (cmd.is_indexed && cmd.index_count > 0) {
    if (vk.vkCmdDrawIndexed) vk.vkCmdDrawIndexed(cb, cmd.index_count, 1, 0, cmd.base_vertex, 0);
  } else if (cmd.vertex_count > 0) {
    if (vk.vkCmdDraw) vk.vkCmdDraw(cb, cmd.vertex_count, 1, 0, 0);
  }
}

void VulkanRenderer::RenderResolve(const ResolveCommand& cmd) {
  // Manejo de resolves en Vulkan
}

void VulkanRenderer::EndFrame() {
  const auto& vk = device_ctx_.GetDispatch();
  VkCommandBuffer cb = device_ctx_.GetCurrentCommandBuffer();
  if (cb) {
    if (device_ctx_.GetCurrentFramebuffer() != VK_NULL_HANDLE && vk.vkCmdEndRenderPass) {
      vk.vkCmdEndRenderPass(cb);
    }
    if (vk.vkEndCommandBuffer) vk.vkEndCommandBuffer(cb);

    VkSubmitInfo submit_info = {};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &cb;

    if (vk.vkQueueSubmit) vk.vkQueueSubmit(device_ctx_.GetQueue(), 1, &submit_info, VK_NULL_HANDLE);
  }
  is_frame_active_ = false;
}

void VulkanRenderer::Present() {
  device_ctx_.Present(true);
}

void VulkanRenderer::Resize(uint32_t width, uint32_t height) {
  device_ctx_.Resize(width, height);
}

void VulkanRenderer::Shutdown() {
  VkDevice dev = device_ctx_.GetDevice();
  const auto& vk = device_ctx_.GetDispatch();
  if (dev) {
    if (pipeline_layout_ != VK_NULL_HANDLE && vk.vkDestroyPipelineLayout) {
      vk.vkDestroyPipelineLayout(dev, pipeline_layout_, nullptr);
      pipeline_layout_ = VK_NULL_HANDLE;
    }
    if (descriptor_set_layout_ != VK_NULL_HANDLE && vk.vkDestroyDescriptorSetLayout) {
      vk.vkDestroyDescriptorSetLayout(dev, descriptor_set_layout_, nullptr);
      descriptor_set_layout_ = VK_NULL_HANDLE;
    }
  }
  device_ctx_.Shutdown();
}

}  // namespace re_wor::nativegfx
