// nativegfx/backend/d3d12/d3d12_device.cpp
// Implementacion del dispositivo y cola de comandos en Direct3D 12.
#ifdef _WIN32
#include "nativegfx/backend/d3d12/d3d12_device.hpp"

#include <algorithm>

namespace re_wor::nativegfx {

using Microsoft::WRL::ComPtr;

D3D12DeviceContext::D3D12DeviceContext() = default;

D3D12DeviceContext::~D3D12DeviceContext() {
  Shutdown();
}

bool D3D12DeviceContext::Initialize(void* hwnd, uint32_t width, uint32_t height) {
  hwnd_ = hwnd;
  width_ = width ? width : 1920;
  height_ = height ? height : 1080;

  // 1. Crear el dispositivo D3D12
  HRESULT hr = D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device_));
  if (FAILED(hr)) return false;

  // 2. Crear la cola de comandos directa
  D3D12_COMMAND_QUEUE_DESC queue_desc = {};
  queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
  queue_desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
  hr = device_->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&command_queue_));
  if (FAILED(hr)) return false;

  // 3. Crear montículos de descriptores RTV y DSV
  D3D12_DESCRIPTOR_HEAP_DESC rtv_heap_desc = {};
  rtv_heap_desc.NumDescriptors = kFrameBufferCount;
  rtv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
  rtv_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
  hr = device_->CreateDescriptorHeap(&rtv_heap_desc, IID_PPV_ARGS(&rtv_heap_));
  if (FAILED(hr)) return false;

  rtv_descriptor_size_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

  D3D12_DESCRIPTOR_HEAP_DESC dsv_heap_desc = {};
  dsv_heap_desc.NumDescriptors = 1;
  dsv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
  dsv_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
  hr = device_->CreateDescriptorHeap(&dsv_heap_desc, IID_PPV_ARGS(&dsv_heap_));
  if (FAILED(hr)) return false;

  // 4. Crear los Command Allocators y la lista de comandos
  for (uint32_t i = 0; i < kFrameBufferCount; ++i) {
    hr = device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&command_allocators_[i]));
    if (FAILED(hr)) return false;
  }

  hr = device_->CreateCommandList(
      0, D3D12_COMMAND_LIST_TYPE_DIRECT, command_allocators_[0].Get(), nullptr, IID_PPV_ARGS(&command_list_));
  if (FAILED(hr)) return false;
  command_list_->Close();

  // 5. Crear Valla de sincronizacion (Fence)
  hr = device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_));
  if (FAILED(hr)) return false;
  fence_event_ = CreateEvent(nullptr, FALSE, FALSE, nullptr);
  if (!fence_event_) return false;

  // 6. Swap chain si hay ventana
  if (hwnd) {
    ComPtr<IDXGIFactory4> factory;
    if (SUCCEEDED(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)))) {
      DXGI_SWAP_CHAIN_DESC1 scd = {};
      scd.BufferCount = kFrameBufferCount;
      scd.Width = width_;
      scd.Height = height_;
      scd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
      scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
      scd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
      scd.SampleDesc.Count = 1;

      ComPtr<IDXGISwapChain1> sc;
      if (SUCCEEDED(factory->CreateSwapChainForHwnd(
              command_queue_.Get(), static_cast<HWND>(hwnd), &scd, nullptr, nullptr, &sc))) {
        sc.As(&swap_chain_);
        frame_index_ = swap_chain_->GetCurrentBackBufferIndex();
      }
    }
  }

  return CreateSwapchainResources(width_, height_);
}

bool D3D12DeviceContext::CreateSwapchainResources(uint32_t width, uint32_t height) {
  if (!device_ || !rtv_heap_) return false;

  D3D12_CPU_DESCRIPTOR_HANDLE rtv_handle = rtv_heap_->GetCPUDescriptorHandleForHeapStart();

  for (uint32_t i = 0; i < kFrameBufferCount; ++i) {
    render_targets_[i].Reset();
    if (swap_chain_) {
      if (FAILED(swap_chain_->GetBuffer(i, IID_PPV_ARGS(&render_targets_[i])))) return false;
    } else {
      // Offscreen render target
      D3D12_HEAP_PROPERTIES hp = {};
      hp.Type = D3D12_HEAP_TYPE_DEFAULT;

      D3D12_RESOURCE_DESC rd = {};
      rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
      rd.Width = width;
      rd.Height = height;
      rd.DepthOrArraySize = 1;
      rd.MipLevels = 1;
      rd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
      rd.SampleDesc.Count = 1;
      rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

      D3D12_CLEAR_VALUE cv = {};
      cv.Format = rd.Format;

      if (FAILED(device_->CreateCommittedResource(
              &hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_RENDER_TARGET, &cv,
              IID_PPV_ARGS(&render_targets_[i])))) {
        return false;
      }
    }
    device_->CreateRenderTargetView(render_targets_[i].Get(), nullptr, rtv_handle);
    rtv_handle.ptr += rtv_descriptor_size_;
  }

  // Buffer de profundidad D24S8
  depth_stencil_.Reset();
  D3D12_HEAP_PROPERTIES dhp = {};
  dhp.Type = D3D12_HEAP_TYPE_DEFAULT;

  D3D12_RESOURCE_DESC dsd = {};
  dsd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  dsd.Width = width;
  dsd.Height = height;
  dsd.DepthOrArraySize = 1;
  dsd.MipLevels = 1;
  dsd.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
  dsd.SampleDesc.Count = 1;
  dsd.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

  D3D12_CLEAR_VALUE dcv = {};
  dcv.Format = dsd.Format;
  dcv.DepthStencil.Depth = 1.0f;
  dcv.DepthStencil.Stencil = 0;

  if (SUCCEEDED(device_->CreateCommittedResource(
          &dhp, D3D12_HEAP_FLAG_NONE, &dsd, D3D12_RESOURCE_STATE_DEPTH_WRITE, &dcv,
          IID_PPV_ARGS(&depth_stencil_)))) {
    device_->CreateDepthStencilView(depth_stencil_.Get(), nullptr, dsv_heap_->GetCPUDescriptorHandleForHeapStart());
  }

  return true;
}

D3D12_CPU_DESCRIPTOR_HANDLE D3D12DeviceContext::GetCurrentRtvHandle() const {
  D3D12_CPU_DESCRIPTOR_HANDLE handle = rtv_heap_->GetCPUDescriptorHandleForHeapStart();
  handle.ptr += frame_index_ * rtv_descriptor_size_;
  return handle;
}

D3D12_CPU_DESCRIPTOR_HANDLE D3D12DeviceContext::GetDsvHandle() const {
  return dsv_heap_->GetCPUDescriptorHandleForHeapStart();
}

void D3D12DeviceContext::WaitForGpu() {
  if (!command_queue_ || !fence_ || !fence_event_) return;

  uint64_t current_fence = fence_values_[frame_index_] + 1;
  command_queue_->Signal(fence_.Get(), current_fence);
  fence_values_[frame_index_] = current_fence;

  if (fence_->GetCompletedValue() < current_fence) {
    fence_->SetEventOnCompletion(current_fence, fence_event_);
    WaitForSingleObject(fence_event_, INFINITE);
  }
}

void D3D12DeviceContext::Present(bool vsync) {
  if (swap_chain_) {
    swap_chain_->Present(vsync ? 1 : 0, 0);
    WaitForGpu();
    frame_index_ = swap_chain_->GetCurrentBackBufferIndex();
  } else {
    WaitForGpu();
    frame_index_ = (frame_index_ + 1) % kFrameBufferCount;
  }
}

void D3D12DeviceContext::Resize(uint32_t width, uint32_t height) {
  if (width == 0 || height == 0 || (width == width_ && height == height_)) return;

  WaitForGpu();
  width_ = width;
  height_ = height;

  for (uint32_t i = 0; i < kFrameBufferCount; ++i) {
    render_targets_[i].Reset();
  }
  depth_stencil_.Reset();

  if (swap_chain_) {
    swap_chain_->ResizeBuffers(kFrameBufferCount, width_, height_, DXGI_FORMAT_R8G8B8A8_UNORM, 0);
    frame_index_ = swap_chain_->GetCurrentBackBufferIndex();
  }

  CreateSwapchainResources(width_, height_);
}

void D3D12DeviceContext::Shutdown() {
  WaitForGpu();
  if (fence_event_) {
    CloseHandle(fence_event_);
    fence_event_ = nullptr;
  }
  for (uint32_t i = 0; i < kFrameBufferCount; ++i) {
    render_targets_[i].Reset();
    command_allocators_[i].Reset();
  }
  depth_stencil_.Reset();
  fence_.Reset();
  command_list_.Reset();
  rtv_heap_.Reset();
  dsv_heap_.Reset();
  swap_chain_.Reset();
  command_queue_.Reset();
  device_.Reset();
}

}  // namespace re_wor::nativegfx
#endif
