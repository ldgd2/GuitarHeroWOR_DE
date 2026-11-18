// nativegfx/backend/d3d11/d3d11_device.cpp
// Implementacion del dispositivo D3D11 nativo.
#ifdef _WIN32
#include "nativegfx/backend/d3d11/d3d11_device.hpp"

namespace re_wor::nativegfx {

using Microsoft::WRL::ComPtr;

D3D11DeviceContext::D3D11DeviceContext() = default;

D3D11DeviceContext::~D3D11DeviceContext() {
  Shutdown();
}

bool D3D11DeviceContext::Initialize(void* hwnd, uint32_t width, uint32_t height) {
  hwnd_ = hwnd;
  width_ = width ? width : 1920;
  height_ = height ? height : 1080;

  UINT create_flags = 0;
#ifdef _DEBUG
  create_flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

  D3D_FEATURE_LEVEL feature_levels[] = {
      D3D_FEATURE_LEVEL_11_1,
      D3D_FEATURE_LEVEL_11_0,
  };
  D3D_FEATURE_LEVEL obtained_level;

  if (hwnd) {
    DXGI_SWAP_CHAIN_DESC scd = {};
    scd.BufferCount = 2;
    scd.BufferDesc.Width = width_;
    scd.BufferDesc.Height = height_;
    scd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    scd.BufferDesc.RefreshRate.Numerator = 60;
    scd.BufferDesc.RefreshRate.Denominator = 1;
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.OutputWindow = static_cast<HWND>(hwnd);
    scd.SampleDesc.Count = 1;
    scd.SampleDesc.Quality = 0;
    scd.Windowed = TRUE;
    scd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        create_flags,
        feature_levels,
        2,
        D3D11_SDK_VERSION,
        &scd,
        &swap_chain_,
        &device_,
        &obtained_level,
        &context_);

    if (FAILED(hr)) {
      // Fallback a nivel de características 11.0 estándar
      hr = D3D11CreateDeviceAndSwapChain(
          nullptr,
          D3D_DRIVER_TYPE_HARDWARE,
          nullptr,
          0,
          &feature_levels[1],
          1,
          D3D11_SDK_VERSION,
          &scd,
          &swap_chain_,
          &device_,
          &obtained_level,
          &context_);
      if (FAILED(hr)) return false;
    }
  } else {
    // Modo offscreen (headless / render target en memoria)
    HRESULT hr = D3D11CreateDevice(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        create_flags,
        feature_levels,
        2,
        D3D11_SDK_VERSION,
        &device_,
        &obtained_level,
        &context_);
    if (FAILED(hr)) return false;
  }

  return CreateSwapchainResources(width_, height_);
}

bool D3D11DeviceContext::CreateSwapchainResources(uint32_t width, uint32_t height) {
  if (!device_) return false;

  back_buffer_rtv_.Reset();
  depth_stencil_texture_.Reset();
  default_dsv_.Reset();

  if (swap_chain_) {
    ComPtr<ID3D11Texture2D> back_buffer;
    HRESULT hr = swap_chain_->GetBuffer(0, IID_PPV_ARGS(&back_buffer));
    if (FAILED(hr)) return false;

    hr = device_->CreateRenderTargetView(back_buffer.Get(), nullptr, &back_buffer_rtv_);
    if (FAILED(hr)) return false;
  } else {
    // Offscreen render target
    D3D11_TEXTURE2D_DESC td = {};
    td.Width = width;
    td.Height = height;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

    ComPtr<ID3D11Texture2D> tex;
    HRESULT hr = device_->CreateTexture2D(&td, nullptr, &tex);
    if (FAILED(hr)) return false;

    hr = device_->CreateRenderTargetView(tex.Get(), nullptr, &back_buffer_rtv_);
    if (FAILED(hr)) return false;
  }

  // Crear buffer de profundidad / plantilla de alta resolución
  D3D11_TEXTURE2D_DESC dd = {};
  dd.Width = width;
  dd.Height = height;
  dd.MipLevels = 1;
  dd.ArraySize = 1;
  dd.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
  dd.SampleDesc.Count = 1;
  dd.Usage = D3D11_USAGE_DEFAULT;
  dd.BindFlags = D3D11_BIND_DEPTH_STENCIL;

  HRESULT hr = device_->CreateTexture2D(&dd, nullptr, &depth_stencil_texture_);
  if (FAILED(hr)) return false;

  hr = device_->CreateDepthStencilView(depth_stencil_texture_.Get(), nullptr, &default_dsv_);
  return SUCCEEDED(hr);
}

void D3D11DeviceContext::Resize(uint32_t width, uint32_t height) {
  if (width == 0 || height == 0 || (width == width_ && height == height_)) return;

  width_ = width;
  height_ = height;

  back_buffer_rtv_.Reset();
  depth_stencil_texture_.Reset();
  default_dsv_.Reset();

  if (swap_chain_) {
    swap_chain_->ResizeBuffers(0, width_, height_, DXGI_FORMAT_UNKNOWN, 0);
  }

  CreateSwapchainResources(width_, height_);
}

void D3D11DeviceContext::Present(bool vsync) {
  if (swap_chain_) {
    swap_chain_->Present(vsync ? 1 : 0, 0);
  }
}

void D3D11DeviceContext::Shutdown() {
  back_buffer_rtv_.Reset();
  depth_stencil_texture_.Reset();
  default_dsv_.Reset();
  swap_chain_.Reset();
  if (context_) {
    context_->ClearState();
    context_.Reset();
  }
  device_.Reset();
}

}  // namespace re_wor::nativegfx
#endif
