// nativegfx/backend/d3d11/d3d11_renderer.hpp
// Implementacion completa del renderizador nativo Direct3D 11 (Fase 3).
#pragma once

#ifdef _WIN32
#include <d3d11.h>
#include <wrl/client.h>

#include <memory>
#include <vector>
#include "nativegfx/backend/native_renderer.hpp"
#include "nativegfx/backend/d3d11/d3d11_device.hpp"
#include "nativegfx/backend/d3d11/d3d11_shader_cache.hpp"
#include "nativegfx/backend/d3d11/d3d11_state_cache.hpp"
#include "nativegfx/backend/d3d11/d3d11_texture_cache.hpp"

namespace re_wor::nativegfx {

class D3D11Renderer : public INativeRenderer {
 public:
  D3D11Renderer();
  ~D3D11Renderer() override;

  bool Initialize(void* native_window_handle, uint32_t width, uint32_t height) override;
  void Shutdown() override;

  void BeginFrame() override;
  void EndFrame() override;
  void Present() override;

  void RenderDraw(const DrawCommand& cmd, const uint8_t* guest_memory_base) override;
  void RenderResolve(const ResolveCommand& cmd) override;

  void Resize(uint32_t width, uint32_t height) override;

  void SetVSync(bool vsync) override { vsync_ = vsync; }
  bool GetVSync() const override { return vsync_; }
  void SetAnisotropy(uint32_t level) override { state_cache_.SetAnisotropy(level); }
  uint32_t GetAnisotropy() const override { return state_cache_.GetAnisotropy(); }

  uint32_t GetNativeWidth() const override { return device_ctx_.GetWidth(); }
  uint32_t GetNativeHeight() const override { return device_ctx_.GetHeight(); }
  float GetResolutionScale() const override { return device_ctx_.GetResolutionScale(); }

  ID3D11Device* GetD3D11Device() const { return device_ctx_.GetDevice(); }
  ID3D11DeviceContext* GetD3D11Context() const { return device_ctx_.GetContext(); }

 private:
  void CreateConstantBuffers();
  void UpdateConstantBuffers(const ShaderConstants& constants);
  void UpdateGuestMemoryBuffer(const uint8_t* guest_memory_base);

  D3D11DeviceContext device_ctx_;
  D3D11StateCache state_cache_;
  D3D11ShaderCache shader_cache_;
  D3D11TextureCache texture_cache_;

  // Buffer de constantes b0 (VS float4), b0 (PS float4), b1 (bool/loop), b2 (fetch), b3 (sys)
  Microsoft::WRL::ComPtr<ID3D11Buffer> cb_vs_c_;
  Microsoft::WRL::ComPtr<ID3D11Buffer> cb_ps_c_;
  Microsoft::WRL::ComPtr<ID3D11Buffer> cb_bl_;
  Microsoft::WRL::ComPtr<ID3D11Buffer> cb_fc_;
  Microsoft::WRL::ComPtr<ID3D11Buffer> cb_sys_;

  // Buffer de memoria raw (t0: ByteAddressBuffer xe_mem para lecturas de vfetch)
  Microsoft::WRL::ComPtr<ID3D11Buffer> guest_memory_buffer_;
  Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> guest_memory_srv_;

  // Buffer de indices dinamico
  Microsoft::WRL::ComPtr<ID3D11Buffer> dynamic_ib_;
  uint32_t dynamic_ib_size_ = 0;

  // Mascara de paginas de 64 KB subidas a guest_memory_buffer_ en el cuadro actual (512 MB = 8192 paginas = 128 uint64_t)
  uint64_t uploaded_pages_mask_[128] = {};
  std::vector<uint8_t> temp_upload_;

  bool is_frame_active_ = false;
  bool vsync_ = false;
};

}  // namespace re_wor::nativegfx
#endif
