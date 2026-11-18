// nativegfx/backend/d3d12/d3d12_renderer.hpp
// Implementacion completa del renderizador nativo Direct3D 12 (Fase 3 y 4).
#pragma once

#ifdef _WIN32
#include <d3d12.h>
#include <wrl/client.h>

#include <memory>
#include <vector>
#include "nativegfx/backend/native_renderer.hpp"
#include "nativegfx/backend/d3d12/d3d12_device.hpp"

namespace re_wor::nativegfx {

class D3D12Renderer : public INativeRenderer {
 public:
  D3D12Renderer();
  ~D3D12Renderer() override;

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
  bool CreateRootSignature();
  bool CreateUploadBuffers();

  D3D12DeviceContext device_ctx_;

  Microsoft::WRL::ComPtr<ID3D12RootSignature> root_signature_;
  Microsoft::WRL::ComPtr<ID3D12Resource> constant_upload_buffer_;
  uint8_t* constant_upload_ptr_ = nullptr;
  uint32_t constant_offset_ = 0;

  // Buffer raw para t0 (xe_mem)
  Microsoft::WRL::ComPtr<ID3D12Resource> guest_memory_buffer_;

  bool is_frame_active_ = false;
};

}  // namespace re_wor::nativegfx
#endif
