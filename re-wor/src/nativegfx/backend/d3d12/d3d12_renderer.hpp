// nativegfx/backend/d3d12/d3d12_renderer.hpp
// Implementacion completa del renderizador nativo Direct3D 12 (Fase 3 y 4).
#pragma once

#ifdef _WIN32
#include <d3d12.h>
#include <wrl/client.h>

#include <memory>
#include <string>
#include <unordered_map>
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

  void SetVSync(bool vsync) override { vsync_ = vsync; }
  bool GetVSync() const override { return vsync_; }

  void SetAnisotropy(uint32_t level) override { aniso_ = level; }
  uint32_t GetAnisotropy() const override { return aniso_; }

 private:
  bool CreateRootSignature();
  bool CreateUploadBuffers();
  bool CreateSrvDescriptorHeap();
  bool CompileRectangleGeometryShader();

  ID3D12PipelineState* GetOrCreatePipelineState(const DrawCommand& cmd);
  const std::vector<uint8_t>& GetOrLoadShaderBytecode(uint64_t hash, const char* stage);

  D3D12DeviceContext device_ctx_;

  Microsoft::WRL::ComPtr<ID3D12RootSignature> root_signature_;
  Microsoft::WRL::ComPtr<ID3D12Resource> constant_upload_buffer_;
  uint8_t* constant_upload_ptr_ = nullptr;
  uint32_t constant_offset_ = 0;

  // Buffer raw para t0 (xe_mem) - 512 MB de memoria física 360
  Microsoft::WRL::ComPtr<ID3D12Resource> guest_memory_buffer_;
  uint8_t* guest_memory_ptr_ = nullptr;
  uint64_t uploaded_pages_mask_[128] = {};
  std::vector<uint8_t> temp_upload_;

  // Buffer de upload dinámico para índices
  Microsoft::WRL::ComPtr<ID3D12Resource> ib_upload_buffer_;
  uint8_t* ib_upload_ptr_ = nullptr;
  uint32_t ib_upload_size_ = 0;

  // Montículo de descriptores SRV para texturas
  Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> srv_heap_;
  uint32_t srv_descriptor_size_ = 0;

  // Cache de bytecodes de shaders cargados/compilados
  std::unordered_map<uint64_t, std::vector<uint8_t>> vs_bytecodes_;
  std::unordered_map<uint64_t, std::vector<uint8_t>> ps_bytecodes_;
  std::vector<uint8_t> rect_gs_bytecode_;

  struct D3D12PsoKey {
    uint64_t vs = 0;
    uint64_t ps = 0;
    uint64_t blend_bits = 0;
    uint64_t blend_bits2 = 0;
    uint64_t ds_bits = 0;
    uint64_t raster_bits = 0;
    uint32_t topo = 0;
    uint32_t rect = 0;

    bool operator==(const D3D12PsoKey& o) const noexcept {
      return vs == o.vs && ps == o.ps &&
             blend_bits == o.blend_bits && blend_bits2 == o.blend_bits2 &&
             ds_bits == o.ds_bits && raster_bits == o.raster_bits &&
             topo == o.topo && rect == o.rect;
    }
  };

  struct D3D12PsoKeyHash {
    size_t operator()(const D3D12PsoKey& k) const noexcept {
      uint64_t h = k.vs ^ (k.ps * 1099511628211ull);
      h = (h ^ k.blend_bits) * 1099511628211ull;
      h = (h ^ k.blend_bits2) * 1099511628211ull;
      h = (h ^ k.ds_bits) * 1099511628211ull;
      h = (h ^ k.raster_bits) * 1099511628211ull;
      h = (h ^ (static_cast<uint64_t>(k.topo) | (static_cast<uint64_t>(k.rect) << 16))) * 1099511628211ull;
      return static_cast<size_t>(h);
    }
  };

  // Cache de PSOs sin asignacion dinamica
  std::unordered_map<D3D12PsoKey, Microsoft::WRL::ComPtr<ID3D12PipelineState>, D3D12PsoKeyHash> pso_cache_;
  D3D12PsoKey last_pso_key_ = {};
  ID3D12PipelineState* last_pso_ = nullptr;
  ID3D12PipelineState* last_bound_pso_ = nullptr;

  // Cache de constantes en GPU para evitar subidas redundantes
  D3D12_GPU_VIRTUAL_ADDRESS last_cb_gpu_[5] = {};
  uint32_t last_cb_version_[5] = {};
  D3D12_GPU_VIRTUAL_ADDRESS last_bound_root_cb_[5] = {};

  uint32_t last_ib_addr_ = 0;
  uint32_t last_ib_count_ = 0;
  IndexFormat last_ib_format_ = IndexFormat::Index16;

  D3D12_VIEWPORT last_vp_ = {};
  D3D12_RECT last_scissor_ = {};
  D3D12_PRIMITIVE_TOPOLOGY last_topo_ = D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;
  D3D12_GPU_VIRTUAL_ADDRESS last_xe_mem_gpu_ = 0;

  bool is_frame_active_ = false;
  bool vsync_ = false;
  uint32_t aniso_ = 16;
};

}  // namespace re_wor::nativegfx
#endif
