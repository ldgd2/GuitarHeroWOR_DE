// nativegfx/backend/d3d11/d3d11_device.hpp
// Manejo del dispositivo Direct3D 11, contexto inmediato, swap chain y render targets nativos.
#pragma once

#ifdef _WIN32
#include <d3d11.h>
#include <dxgi.h>
#include <wrl/client.h>

#include <cstdint>

namespace re_wor::nativegfx {

class D3D11DeviceContext {
 public:
  D3D11DeviceContext();
  ~D3D11DeviceContext();

  bool Initialize(void* hwnd, uint32_t width, uint32_t height);
  void Shutdown();
  void Resize(uint32_t width, uint32_t height);
  void Present(bool vsync = true);

  ID3D11Device* GetDevice() const { return device_.Get(); }
  ID3D11DeviceContext* GetContext() const { return context_.Get(); }
  ID3D11RenderTargetView* GetBackBufferRTV() const { return back_buffer_rtv_.Get(); }
  ID3D11DepthStencilView* GetDefaultDSV() const { return default_dsv_.Get(); }

  uint32_t GetWidth() const { return width_; }
  uint32_t GetHeight() const { return height_; }
  float GetResolutionScale() const { return float(height_) / 720.0f; }

 private:
  bool CreateSwapchainResources(uint32_t width, uint32_t height);

  uint32_t width_  = 1920;
  uint32_t height_ = 1080;
  void* hwnd_      = nullptr;

  Microsoft::WRL::ComPtr<ID3D11Device> device_;
  Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
  Microsoft::WRL::ComPtr<IDXGISwapChain> swap_chain_;
  Microsoft::WRL::ComPtr<ID3D11RenderTargetView> back_buffer_rtv_;
  Microsoft::WRL::ComPtr<ID3D11Texture2D> depth_stencil_texture_;
  Microsoft::WRL::ComPtr<ID3D11DepthStencilView> default_dsv_;
};

}  // namespace re_wor::nativegfx
#endif
