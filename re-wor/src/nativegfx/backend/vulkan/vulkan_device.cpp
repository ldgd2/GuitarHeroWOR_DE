// nativegfx/backend/vulkan/vulkan_device.cpp
// Implementacion del dispositivo y colas Vulkan con carga dinamica.
#include "nativegfx/backend/vulkan/vulkan_device.hpp"

#ifdef _WIN32
#include <windows.h>
#include <vulkan/vulkan_win32.h>
#endif

#include <algorithm>
#include <cstring>

namespace re_wor::nativegfx {

VulkanDeviceContext::VulkanDeviceContext() = default;

VulkanDeviceContext::~VulkanDeviceContext() {
  Shutdown();
}

bool VulkanDeviceContext::LoadVulkanLibrary() {
#ifdef _WIN32
  HMODULE mod = LoadLibraryW(L"vulkan-1.dll");
  if (!mod) return false;
  vulkan_module_ = mod;

  auto gipa = reinterpret_cast<PFN_vkGetInstanceProcAddr>(GetProcAddress(mod, "vkGetInstanceProcAddr"));
  if (!gipa) return false;

  vk_.vkCreateInstance = reinterpret_cast<PFN_vkCreateInstance>(gipa(nullptr, "vkCreateInstance"));
  return vk_.vkCreateInstance != nullptr;
#else
  return false;
#endif
}

bool VulkanDeviceContext::Initialize(void* hwnd, uint32_t width, uint32_t height) {
  hwnd_ = hwnd;
  width_ = width ? width : 1920;
  height_ = height ? height : 1080;

  if (!LoadVulkanLibrary()) return false;
  if (!CreateInstance()) return false;

#ifdef _WIN32
  if (hwnd) {
    HMODULE mod = static_cast<HMODULE>(vulkan_module_);
    auto gipa = reinterpret_cast<PFN_vkGetInstanceProcAddr>(GetProcAddress(mod, "vkGetInstanceProcAddr"));
    auto vkCreateWin32SurfaceKHR = reinterpret_cast<PFN_vkCreateWin32SurfaceKHR>(
        gipa(instance_, "vkCreateWin32SurfaceKHR"));

    if (vkCreateWin32SurfaceKHR) {
      VkWin32SurfaceCreateInfoKHR sci = {};
      sci.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
      sci.hinstance = GetModuleHandle(nullptr);
      sci.hwnd = static_cast<HWND>(hwnd);
      vkCreateWin32SurfaceKHR(instance_, &sci, nullptr, &surface_);
    }
  }
#endif

  if (!SelectPhysicalDevice()) return false;
  if (!CreateLogicalDevice()) return false;
  if (!CreateRenderPass()) return false;

  // Crear Command Pool y Command Buffers
  VkCommandPoolCreateInfo cp_info = {};
  cp_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  cp_info.queueFamilyIndex = graphics_family_index_;
  cp_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  if (vk_.vkCreateCommandPool(device_, &cp_info, nullptr, &command_pool_) != VK_SUCCESS) {
    return false;
  }

  command_buffers_.resize(kMaxFramesInFlight);
  VkCommandBufferAllocateInfo cb_info = {};
  cb_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  cb_info.commandPool = command_pool_;
  cb_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  cb_info.commandBufferCount = kMaxFramesInFlight;
  if (vk_.vkAllocateCommandBuffers(device_, &cb_info, command_buffers_.data()) != VK_SUCCESS) {
    return false;
  }

  // Crear semaforos y fences de sincronizacion
  VkSemaphoreCreateInfo sem_info = {VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
  VkFenceCreateInfo fence_info = {VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, nullptr, VK_FENCE_CREATE_SIGNALED_BIT};

  for (uint32_t i = 0; i < kMaxFramesInFlight; ++i) {
    vk_.vkCreateSemaphore(device_, &sem_info, nullptr, &image_available_semaphores_[i]);
    vk_.vkCreateSemaphore(device_, &sem_info, nullptr, &render_finished_semaphores_[i]);
    vk_.vkCreateFence(device_, &fence_info, nullptr, &in_flight_fences_[i]);
  }

  return CreateSwapchainResources(width_, height_);
}

bool VulkanDeviceContext::CreateInstance() {
  HMODULE mod = static_cast<HMODULE>(vulkan_module_);
  auto gipa = reinterpret_cast<PFN_vkGetInstanceProcAddr>(GetProcAddress(mod, "vkGetInstanceProcAddr"));

  VkApplicationInfo app_info = {};
  app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  app_info.pApplicationName = "Guitar Hero Warriors of Rock";
  app_info.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
  app_info.pEngineName = "NativeGFX";
  app_info.engineVersion = VK_MAKE_VERSION(1, 0, 0);
  app_info.apiVersion = VK_API_VERSION_1_2;   // Vulkan 1.2

  std::vector<const char*> extensions;
  if (hwnd_) {
    extensions.push_back("VK_KHR_surface");
#ifdef _WIN32
    extensions.push_back("VK_KHR_win32_surface");
#endif
  }

  VkInstanceCreateInfo ci = {};
  ci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  ci.pApplicationInfo = &app_info;
  ci.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
  ci.ppEnabledExtensionNames = extensions.empty() ? nullptr : extensions.data();

  if (vk_.vkCreateInstance(&ci, nullptr, &instance_) != VK_SUCCESS) {
    return false;
  }

  // Cargar funciones de instancia
#define LOAD_INST(fn) vk_.fn = reinterpret_cast<PFN_##fn>(gipa(instance_, #fn));
  LOAD_INST(vkDestroyInstance);
  LOAD_INST(vkEnumeratePhysicalDevices);
  LOAD_INST(vkGetPhysicalDeviceProperties);
  LOAD_INST(vkGetPhysicalDeviceQueueFamilyProperties);
  LOAD_INST(vkCreateDevice);
  LOAD_INST(vkGetDeviceProcAddr);
#undef LOAD_INST

  return true;
}

bool VulkanDeviceContext::SelectPhysicalDevice() {
  uint32_t device_count = 0;
  vk_.vkEnumeratePhysicalDevices(instance_, &device_count, nullptr);
  if (device_count == 0) return false;

  std::vector<VkPhysicalDevice> devices(device_count);
  vk_.vkEnumeratePhysicalDevices(instance_, &device_count, devices.data());

  for (const auto& dev : devices) {
    VkPhysicalDeviceProperties props;
    vk_.vkGetPhysicalDeviceProperties(dev, &props);
    if (props.apiVersion >= VK_API_VERSION_1_2) {
      physical_device_ = dev;
      if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) break;
    }
  }

  if (physical_device_ == VK_NULL_HANDLE) {
    physical_device_ = devices[0];
  }

  uint32_t queue_family_count = 0;
  vk_.vkGetPhysicalDeviceQueueFamilyProperties(physical_device_, &queue_family_count, nullptr);
  std::vector<VkQueueFamilyProperties> queue_families(queue_family_count);
  vk_.vkGetPhysicalDeviceQueueFamilyProperties(physical_device_, &queue_family_count, queue_families.data());

  for (uint32_t i = 0; i < queue_family_count; ++i) {
    if (queue_families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
      graphics_family_index_ = i;
      break;
    }
  }

  return true;
}

bool VulkanDeviceContext::CreateLogicalDevice() {
  float priority = 1.0f;
  VkDeviceQueueCreateInfo queue_ci = {};
  queue_ci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
  queue_ci.queueFamilyIndex = graphics_family_index_;
  queue_ci.queueCount = 1;
  queue_ci.pQueuePriorities = &priority;

  std::vector<const char*> device_extensions;
  if (surface_ != VK_NULL_HANDLE) {
    device_extensions.push_back("VK_KHR_swapchain");
  }

  VkDeviceCreateInfo device_ci = {};
  device_ci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  device_ci.queueCreateInfoCount = 1;
  device_ci.pQueueCreateInfos = &queue_ci;
  device_ci.enabledExtensionCount = static_cast<uint32_t>(device_extensions.size());
  device_ci.ppEnabledExtensionNames = device_extensions.empty() ? nullptr : device_extensions.data();

  if (vk_.vkCreateDevice(physical_device_, &device_ci, nullptr, &device_) != VK_SUCCESS) {
    return false;
  }

  // Cargar funciones de dispositivo
#define LOAD_DEV(fn) vk_.fn = reinterpret_cast<PFN_##fn>(vk_.vkGetDeviceProcAddr(device_, #fn));
  LOAD_DEV(vkDestroyDevice);
  LOAD_DEV(vkGetDeviceQueue);
  LOAD_DEV(vkDeviceWaitIdle);
  LOAD_DEV(vkQueueSubmit);
  LOAD_DEV(vkQueueWaitIdle);
  LOAD_DEV(vkCreateCommandPool);
  LOAD_DEV(vkDestroyCommandPool);
  LOAD_DEV(vkAllocateCommandBuffers);
  LOAD_DEV(vkResetCommandBuffer);
  LOAD_DEV(vkBeginCommandBuffer);
  LOAD_DEV(vkEndCommandBuffer);
  LOAD_DEV(vkCmdBeginRenderPass);
  LOAD_DEV(vkCmdEndRenderPass);
  LOAD_DEV(vkCmdSetViewport);
  LOAD_DEV(vkCmdSetScissor);
  LOAD_DEV(vkCmdDraw);
  LOAD_DEV(vkCmdDrawIndexed);
  LOAD_DEV(vkCreateRenderPass);
  LOAD_DEV(vkDestroyRenderPass);
  LOAD_DEV(vkCreateFramebuffer);
  LOAD_DEV(vkDestroyFramebuffer);
  LOAD_DEV(vkCreateImageView);
  LOAD_DEV(vkDestroyImageView);
  LOAD_DEV(vkCreateSemaphore);
  LOAD_DEV(vkDestroySemaphore);
  LOAD_DEV(vkCreateFence);
  LOAD_DEV(vkDestroyFence);
  LOAD_DEV(vkWaitForFences);
  LOAD_DEV(vkResetFences);
  LOAD_DEV(vkCreateDescriptorSetLayout);
  LOAD_DEV(vkDestroyDescriptorSetLayout);
  LOAD_DEV(vkCreatePipelineLayout);
  LOAD_DEV(vkDestroyPipelineLayout);
  LOAD_DEV(vkCreateShaderModule);
  LOAD_DEV(vkDestroyShaderModule);
  LOAD_DEV(vkCreateGraphicsPipelines);
  LOAD_DEV(vkDestroyPipeline);
  LOAD_DEV(vkCmdBindPipeline);
  LOAD_DEV(vkCmdBindDescriptorSets);
  LOAD_DEV(vkCreateSwapchainKHR);
  LOAD_DEV(vkDestroySwapchainKHR);
  LOAD_DEV(vkGetSwapchainImagesKHR);
  LOAD_DEV(vkQueuePresentKHR);
#undef LOAD_DEV

  vk_.vkGetDeviceQueue(device_, graphics_family_index_, 0, &graphics_queue_);
  return true;
}

bool VulkanDeviceContext::CreateRenderPass() {
  VkAttachmentDescription color_attachment = {};
  color_attachment.format = VK_FORMAT_B8G8R8A8_UNORM;
  color_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
  color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  color_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  color_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  color_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  color_attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

  VkAttachmentReference color_ref = {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};

  VkSubpassDescription subpass = {};
  subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  subpass.colorAttachmentCount = 1;
  subpass.pColorAttachments = &color_ref;

  VkRenderPassCreateInfo rp_info = {};
  rp_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  rp_info.attachmentCount = 1;
  rp_info.pAttachments = &color_attachment;
  rp_info.subpassCount = 1;
  rp_info.pSubpasses = &subpass;

  return vk_.vkCreateRenderPass(device_, &rp_info, nullptr, &render_pass_) == VK_SUCCESS;
}

bool VulkanDeviceContext::CreateSwapchainResources(uint32_t width, uint32_t height) {
  if (surface_ != VK_NULL_HANDLE && vk_.vkCreateSwapchainKHR) {
    VkSwapchainCreateInfoKHR sci = {};
    sci.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    sci.surface = surface_;
    sci.minImageCount = kMaxFramesInFlight;
    sci.imageFormat = VK_FORMAT_B8G8R8A8_UNORM;
    sci.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    sci.imageExtent = {width, height};
    sci.imageArrayLayers = 1;
    sci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    sci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    sci.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    sci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    sci.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    sci.clipped = VK_TRUE;

    if (vk_.vkCreateSwapchainKHR(device_, &sci, nullptr, &swapchain_) != VK_SUCCESS) {
      return false;
    }

    uint32_t image_count = 0;
    vk_.vkGetSwapchainImagesKHR(device_, swapchain_, &image_count, nullptr);
    swapchain_images_.resize(image_count);
    vk_.vkGetSwapchainImagesKHR(device_, swapchain_, &image_count, swapchain_images_.data());

    swapchain_views_.resize(image_count);
    framebuffers_.resize(image_count);

    for (uint32_t i = 0; i < image_count; ++i) {
      VkImageViewCreateInfo iv_info = {};
      iv_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
      iv_info.image = swapchain_images_[i];
      iv_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
      iv_info.format = VK_FORMAT_B8G8R8A8_UNORM;
      iv_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
      iv_info.subresourceRange.levelCount = 1;
      iv_info.subresourceRange.layerCount = 1;

      if (vk_.vkCreateImageView(device_, &iv_info, nullptr, &swapchain_views_[i]) != VK_SUCCESS) return false;

      VkFramebufferCreateInfo fb_info = {};
      fb_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
      fb_info.renderPass = render_pass_;
      fb_info.attachmentCount = 1;
      fb_info.pAttachments = &swapchain_views_[i];
      fb_info.width = width;
      fb_info.height = height;
      fb_info.layers = 1;

      if (vk_.vkCreateFramebuffer(device_, &fb_info, nullptr, &framebuffers_[i]) != VK_SUCCESS) return false;
    }
  }
  return true;
}

VkFramebuffer VulkanDeviceContext::GetCurrentFramebuffer() const {
  if (image_index_ < framebuffers_.size()) {
    return framebuffers_[image_index_];
  }
  return VK_NULL_HANDLE;
}

void VulkanDeviceContext::WaitForFences() {
  if (device_ && vk_.vkWaitForFences && in_flight_fences_[current_frame_] != VK_NULL_HANDLE) {
    vk_.vkWaitForFences(device_, 1, &in_flight_fences_[current_frame_], VK_TRUE, UINT64_MAX);
    vk_.vkResetFences(device_, 1, &in_flight_fences_[current_frame_]);
  }
}

void VulkanDeviceContext::Present(bool vsync) {
  if (swapchain_ != VK_NULL_HANDLE && vk_.vkQueuePresentKHR) {
    VkPresentInfoKHR present_info = {};
    present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present_info.waitSemaphoreCount = 1;
    present_info.pWaitSemaphores = &render_finished_semaphores_[current_frame_];
    present_info.swapchainCount = 1;
    present_info.pSwapchains = &swapchain_;
    present_info.pImageIndices = &image_index_;

    vk_.vkQueuePresentKHR(graphics_queue_, &present_info);
  }
  current_frame_ = (current_frame_ + 1) % kMaxFramesInFlight;
}

void VulkanDeviceContext::Resize(uint32_t width, uint32_t height) {
  if (width == 0 || height == 0 || (width == width_ && height == height_)) return;
  if (device_ && vk_.vkDeviceWaitIdle) vk_.vkDeviceWaitIdle(device_);
  width_ = width;
  height_ = height;
  for (auto fb : framebuffers_) if (vk_.vkDestroyFramebuffer) vk_.vkDestroyFramebuffer(device_, fb, nullptr);
  for (auto iv : swapchain_views_) if (vk_.vkDestroyImageView) vk_.vkDestroyImageView(device_, iv, nullptr);
  framebuffers_.clear();
  swapchain_views_.clear();
  if (swapchain_ && vk_.vkDestroySwapchainKHR) vk_.vkDestroySwapchainKHR(device_, swapchain_, nullptr);
  CreateSwapchainResources(width_, height_);
}

void VulkanDeviceContext::Shutdown() {
  if (device_) {
    if (vk_.vkDeviceWaitIdle) vk_.vkDeviceWaitIdle(device_);
    for (uint32_t i = 0; i < kMaxFramesInFlight; ++i) {
      if (image_available_semaphores_[i] && vk_.vkDestroySemaphore) vk_.vkDestroySemaphore(device_, image_available_semaphores_[i], nullptr);
      if (render_finished_semaphores_[i] && vk_.vkDestroySemaphore) vk_.vkDestroySemaphore(device_, render_finished_semaphores_[i], nullptr);
      if (in_flight_fences_[i] && vk_.vkDestroyFence) vk_.vkDestroyFence(device_, in_flight_fences_[i], nullptr);
    }
    for (auto fb : framebuffers_) if (vk_.vkDestroyFramebuffer) vk_.vkDestroyFramebuffer(device_, fb, nullptr);
    for (auto iv : swapchain_views_) if (vk_.vkDestroyImageView) vk_.vkDestroyImageView(device_, iv, nullptr);
    framebuffers_.clear();
    swapchain_views_.clear();
    if (command_pool_ && vk_.vkDestroyCommandPool) vk_.vkDestroyCommandPool(device_, command_pool_, nullptr);
    if (render_pass_ && vk_.vkDestroyRenderPass) vk_.vkDestroyRenderPass(device_, render_pass_, nullptr);
    if (swapchain_ && vk_.vkDestroySwapchainKHR) vk_.vkDestroySwapchainKHR(device_, swapchain_, nullptr);
    if (vk_.vkDestroyDevice) vk_.vkDestroyDevice(device_, nullptr);
    device_ = VK_NULL_HANDLE;
  }
  if (instance_) {
    if (surface_) {
      HMODULE mod = static_cast<HMODULE>(vulkan_module_);
      auto gipa = reinterpret_cast<PFN_vkGetInstanceProcAddr>(GetProcAddress(mod, "vkGetInstanceProcAddr"));
      auto vkDestroySurfaceKHR = reinterpret_cast<PFN_vkDestroySurfaceKHR>(gipa(instance_, "vkDestroySurfaceKHR"));
      if (vkDestroySurfaceKHR) vkDestroySurfaceKHR(instance_, surface_, nullptr);
      surface_ = VK_NULL_HANDLE;
    }
    if (vk_.vkDestroyInstance) vk_.vkDestroyInstance(instance_, nullptr);
    instance_ = VK_NULL_HANDLE;
  }
#ifdef _WIN32
  if (vulkan_module_) {
    FreeLibrary(static_cast<HMODULE>(vulkan_module_));
    vulkan_module_ = nullptr;
  }
#endif
}

}  // namespace re_wor::nativegfx
