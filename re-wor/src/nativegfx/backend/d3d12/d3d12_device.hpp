// nativegfx/backend/d3d12/d3d12_device.hpp
// Manejo del dispositivo Direct3D 12, colas de comandos, descriptores y swap chain nativa.
#pragma once

#ifdef _WIN32
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <cstdint>

namespace re_wor::nativegfx {

class D3D12DeviceContext {
 public:
  D3D12DeviceContext();
  ~D3D12DeviceContext();

  bool Initialize(void* hwnd, uint32_t width, uint32_t height);
  void Shutdown();
  void Resize(uint32_t width, uint32_t height);
  void Present(bool vsync = true);

  void WaitForGpu();

  ID3D12Device* GetDevice() const { return device_.Get(); }
  ID3D12CommandQueue* GetCommandQueue() const { return command_queue_.Get(); }
  ID3D12GraphicsCommandList* GetCommandList() const { return command_list_.Get(); }
  ID3D12CommandAllocator* GetCurrentAllocator() const { return command_allocators_[frame_index_].Get(); }

  D3D12_CPU_DESCRIPTOR_HANDLE GetCurrentRtvHandle() const;
  D3D12_CPU_DESCRIPTOR_HANDLE GetDsvHandle() const;
  ID3D12Resource* GetCurrentRenderTarget() const { return render_targets_[frame_index_].Get(); }
  IDXGISwapChain3* GetSwapChain() const { return swap_chain_.Get(); }

  uint32_t GetWidth() const { return width_; }
  uint32_t GetHeight() const { return height_; }
  float GetResolutionScale() const { return float(height_) / 720.0f; }
  uint32_t GetFrameIndex() const { return frame_index_; }

 private:
  bool CreateSwapchainResources(uint32_t width, uint32_t height);

  static constexpr uint32_t kFrameBufferCount = 2;

  uint32_t width_  = 1920;
  uint32_t height_ = 1080;
  void* hwnd_      = nullptr;
  uint32_t frame_index_ = 0;

  Microsoft::WRL::ComPtr<ID3D12Device> device_;
  Microsoft::WRL::ComPtr<ID3D12CommandQueue> command_queue_;
  Microsoft::WRL::ComPtr<IDXGISwapChain3> swap_chain_;

  Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtv_heap_;
  Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> dsv_heap_;
  uint32_t rtv_descriptor_size_ = 0;

  Microsoft::WRL::ComPtr<ID3D12Resource> render_targets_[kFrameBufferCount];
  Microsoft::WRL::ComPtr<ID3D12Resource> depth_stencil_;

  Microsoft::WRL::ComPtr<ID3D12CommandAllocator> command_allocators_[kFrameBufferCount];
  Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> command_list_;

  // Sincronizacion por valla (Fence)
  Microsoft::WRL::ComPtr<ID3D12Fence> fence_;
  uint64_t fence_values_[kFrameBufferCount] = {};
  void* fence_event_ = nullptr;
};

}  // namespace re_wor::nativegfx
#endif
