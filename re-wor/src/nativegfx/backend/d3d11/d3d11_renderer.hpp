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
  uint64_t frame_counter_ = 0;

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

  // Cache de versión de buffers de constantes para omitir UpdateSubresource idénticos sin coste de CPU
  uint32_t last_cb_version_[5] = {};
  uint32_t last_draw_fetch_version_ = 0xFFFFFFFF;
  uint32_t last_ib_addr_ = 0;
  uint32_t last_ib_count_ = 0;
  IndexFormat last_ib_format_ = IndexFormat::Index16;

  // Redundancy filters para estados de pipeline de D3D11
  ID3D11BlendState* last_blend_state_ = nullptr;
  float last_blend_factor_[4] = {0, 0, 0, 0};
  ID3D11DepthStencilState* last_ds_state_ = nullptr;
  uint32_t last_stencil_ref_ = 0xFFFFFFFF;
  ID3D11RasterizerState* last_rs_state_ = nullptr;
  ID3D11VertexShader* last_vs_ = nullptr;
  ID3D11PixelShader* last_ps_ = nullptr;
  D3D11_PRIMITIVE_TOPOLOGY last_topo_ = D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;

  // Filtro de redundancia para texturas y samplers
  ID3D11ShaderResourceView* last_bound_srv_[128] = {};
  ID3D11SamplerState* last_bound_sampler_[16] = {};
  uint64_t last_tex_hash_[32] = {};
  ID3D11ShaderResourceView* last_cached_srv_[32] = {};
  ID3D11SamplerState* last_cached_smp_[32] = {};

  bool guest_srv_bound_ = false;
  ID3D11GeometryShader* last_gs_ = nullptr;
  D3D11_RECT last_scissor_rect_ = {};

  bool is_frame_active_ = false;
  bool vsync_ = false;
};

}  // namespace re_wor::nativegfx
#endif
