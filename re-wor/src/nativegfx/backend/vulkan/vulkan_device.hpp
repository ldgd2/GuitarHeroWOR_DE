// nativegfx/backend/vulkan/vulkan_device.hpp
// Manejo del dispositivo Vulkan, colas y swapchain con carga dinamica de vulkan-1.dll.
#pragma once

#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <cstdint>
#include <vector>

namespace re_wor::nativegfx {

// Punteros a funciones de la API Vulkan cargados dinamicamente
struct VulkanDispatch {
  PFN_vkCreateInstance vkCreateInstance = nullptr;
  PFN_vkDestroyInstance vkDestroyInstance = nullptr;
  PFN_vkEnumeratePhysicalDevices vkEnumeratePhysicalDevices = nullptr;
  PFN_vkGetPhysicalDeviceProperties vkGetPhysicalDeviceProperties = nullptr;
  PFN_vkGetPhysicalDeviceQueueFamilyProperties vkGetPhysicalDeviceQueueFamilyProperties = nullptr;
  PFN_vkCreateDevice vkCreateDevice = nullptr;
  PFN_vkGetDeviceProcAddr vkGetDeviceProcAddr = nullptr;

  PFN_vkDestroyDevice vkDestroyDevice = nullptr;
  PFN_vkGetDeviceQueue vkGetDeviceQueue = nullptr;
  PFN_vkDeviceWaitIdle vkDeviceWaitIdle = nullptr;
  PFN_vkQueueSubmit vkQueueSubmit = nullptr;
  PFN_vkQueueWaitIdle vkQueueWaitIdle = nullptr;

  PFN_vkCreateCommandPool vkCreateCommandPool = nullptr;
  PFN_vkDestroyCommandPool vkDestroyCommandPool = nullptr;
  PFN_vkAllocateCommandBuffers vkAllocateCommandBuffers = nullptr;
  PFN_vkResetCommandBuffer vkResetCommandBuffer = nullptr;
  PFN_vkBeginCommandBuffer vkBeginCommandBuffer = nullptr;
  PFN_vkEndCommandBuffer vkEndCommandBuffer = nullptr;

  PFN_vkCmdBeginRenderPass vkCmdBeginRenderPass = nullptr;
  PFN_vkCmdEndRenderPass vkCmdEndRenderPass = nullptr;
  PFN_vkCmdSetViewport vkCmdSetViewport = nullptr;
  PFN_vkCmdSetScissor vkCmdSetScissor = nullptr;
  PFN_vkCmdDraw vkCmdDraw = nullptr;
  PFN_vkCmdDrawIndexed vkCmdDrawIndexed = nullptr;

  PFN_vkCreateRenderPass vkCreateRenderPass = nullptr;
  PFN_vkDestroyRenderPass vkDestroyRenderPass = nullptr;
  PFN_vkCreateFramebuffer vkCreateFramebuffer = nullptr;
  PFN_vkDestroyFramebuffer vkDestroyFramebuffer = nullptr;
  PFN_vkCreateImageView vkCreateImageView = nullptr;
  PFN_vkDestroyImageView vkDestroyImageView = nullptr;

  PFN_vkCreateSemaphore vkCreateSemaphore = nullptr;
  PFN_vkDestroySemaphore vkDestroySemaphore = nullptr;
  PFN_vkCreateFence vkCreateFence = nullptr;
  PFN_vkDestroyFence vkDestroyFence = nullptr;
  PFN_vkWaitForFences vkWaitForFences = nullptr;
  PFN_vkResetFences vkResetFences = nullptr;

  PFN_vkCreateDescriptorSetLayout vkCreateDescriptorSetLayout = nullptr;
  PFN_vkDestroyDescriptorSetLayout vkDestroyDescriptorSetLayout = nullptr;
  PFN_vkCreatePipelineLayout vkCreatePipelineLayout = nullptr;
  PFN_vkDestroyPipelineLayout vkDestroyPipelineLayout = nullptr;

  PFN_vkCreateSwapchainKHR vkCreateSwapchainKHR = nullptr;
  PFN_vkDestroySwapchainKHR vkDestroySwapchainKHR = nullptr;
  PFN_vkGetSwapchainImagesKHR vkGetSwapchainImagesKHR = nullptr;
  PFN_vkQueuePresentKHR vkQueuePresentKHR = nullptr;
};

class VulkanDeviceContext {
 public:
  VulkanDeviceContext();
  ~VulkanDeviceContext();

  bool Initialize(void* hwnd, uint32_t width, uint32_t height);
  void Shutdown();
  void Resize(uint32_t width, uint32_t height);
  void Present(bool vsync = true);

  VkDevice GetDevice() const { return device_; }
  VkPhysicalDevice GetPhysicalDevice() const { return physical_device_; }
  VkQueue GetQueue() const { return graphics_queue_; }
  VkCommandBuffer GetCurrentCommandBuffer() const { return command_buffers_[current_frame_]; }
  VkRenderPass GetRenderPass() const { return render_pass_; }
  VkFramebuffer GetCurrentFramebuffer() const;
  const VulkanDispatch& GetDispatch() const { return vk_; }

  uint32_t GetWidth() const { return width_; }
  uint32_t GetHeight() const { return height_; }
  float GetResolutionScale() const { return float(height_) / 720.0f; }
  uint32_t GetCurrentFrameIndex() const { return current_frame_; }

  void WaitForFences();

 private:
  bool LoadVulkanLibrary();
  bool CreateInstance();
  bool SelectPhysicalDevice();
  bool CreateLogicalDevice();
  bool CreateRenderPass();
  bool CreateSwapchainResources(uint32_t width, uint32_t height);

  static constexpr uint32_t kMaxFramesInFlight = 2;

  void* vulkan_module_ = nullptr;
  VulkanDispatch vk_;

  uint32_t width_  = 1920;
  uint32_t height_ = 1080;
  void* hwnd_      = nullptr;
  uint32_t current_frame_ = 0;
  uint32_t image_index_   = 0;

  VkInstance instance_               = VK_NULL_HANDLE;
  VkSurfaceKHR surface_              = VK_NULL_HANDLE;
  VkPhysicalDevice physical_device_  = VK_NULL_HANDLE;
  VkDevice device_                   = VK_NULL_HANDLE;
  VkQueue graphics_queue_            = VK_NULL_HANDLE;
  uint32_t graphics_family_index_    = 0;

  VkSwapchainKHR swapchain_          = VK_NULL_HANDLE;
  std::vector<VkImage> swapchain_images_;
  std::vector<VkImageView> swapchain_views_;
  std::vector<VkFramebuffer> framebuffers_;

  VkRenderPass render_pass_          = VK_NULL_HANDLE;
  VkCommandPool command_pool_        = VK_NULL_HANDLE;
  std::vector<VkCommandBuffer> command_buffers_;

  VkSemaphore image_available_semaphores_[kMaxFramesInFlight] = {};
  VkSemaphore render_finished_semaphores_[kMaxFramesInFlight] = {};
  VkFence in_flight_fences_[kMaxFramesInFlight] = {};
};

}  // namespace re_wor::nativegfx
