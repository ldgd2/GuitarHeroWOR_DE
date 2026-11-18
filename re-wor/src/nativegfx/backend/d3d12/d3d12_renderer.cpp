// nativegfx/backend/d3d12/d3d12_renderer.cpp
// Implementacion del renderizador nativo Direct3D 12.
#ifdef _WIN32
#include "nativegfx/backend/d3d12/d3d12_renderer.hpp"

namespace re_wor::nativegfx {

using Microsoft::WRL::ComPtr;

namespace {

D3D12_PRIMITIVE_TOPOLOGY ToD3D12Topology(PrimitiveTopology topo) {
  switch (topo) {
    case PrimitiveTopology::PointList:     return D3D_PRIMITIVE_TOPOLOGY_POINTLIST;
    case PrimitiveTopology::LineList:      return D3D_PRIMITIVE_TOPOLOGY_LINELIST;
    case PrimitiveTopology::LineStrip:     return D3D_PRIMITIVE_TOPOLOGY_LINESTRIP;
    case PrimitiveTopology::TriangleList:  return D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    case PrimitiveTopology::TriangleStrip: return D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
    case PrimitiveTopology::RectangleList: return D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    default:                               return D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
  }
}

}  // namespace

D3D12Renderer::D3D12Renderer() = default;

D3D12Renderer::~D3D12Renderer() {
  Shutdown();
}

bool D3D12Renderer::Initialize(void* native_window_handle, uint32_t width, uint32_t height) {
  if (!device_ctx_.Initialize(native_window_handle, width, height)) {
    return false;
  }

  if (!CreateRootSignature()) {
    return false;
  }

  if (!CreateUploadBuffers()) {
    return false;
  }

  return true;
}

bool D3D12Renderer::CreateRootSignature() {
  ID3D12Device* dev = device_ctx_.GetDevice();
  if (!dev) return false;

  // Root Parameters:
  // 0: CBV b0 (VS float)
  // 1: CBV b0 (PS float)
  // 2: CBV b1 (bool/loop)
  // 3: CBV b2 (fetch)
  // 4: CBV b3 (sys)
  // 5: SRV t0 (xe_mem raw buffer)
  D3D12_ROOT_PARAMETER root_params[6] = {};

  for (int i = 0; i < 5; ++i) {
    root_params[i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    root_params[i].Descriptor.ShaderRegister = i;
    root_params[i].Descriptor.RegisterSpace = 0;
    root_params[i].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
  }

  // t0 raw ByteAddressBuffer
  root_params[5].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
  root_params[5].Descriptor.ShaderRegister = 0;
  root_params[5].Descriptor.RegisterSpace = 0;
  root_params[5].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

  D3D12_ROOT_SIGNATURE_DESC rs_desc = {};
  rs_desc.NumParameters = 6;
  rs_desc.pParameters = root_params;
  rs_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

  ComPtr<ID3DBlob> signature_blob;
  ComPtr<ID3DBlob> error_blob;
  HRESULT hr = D3D12SerializeRootSignature(&rs_desc, D3D_ROOT_SIGNATURE_VERSION_1, &signature_blob, &error_blob);
  if (FAILED(hr)) return false;

  hr = dev->CreateRootSignature(
      0, signature_blob->GetBufferPointer(), signature_blob->GetBufferSize(), IID_PPV_ARGS(&root_signature_));
  return SUCCEEDED(hr);
}

bool D3D12Renderer::CreateUploadBuffers() {
  ID3D12Device* dev = device_ctx_.GetDevice();
  if (!dev) return false;

  constexpr uint32_t kUploadBufferSize = 4u << 20;   // 4 MB ring buffer de constantes

  D3D12_HEAP_PROPERTIES hp = {};
  hp.Type = D3D12_HEAP_TYPE_UPLOAD;

  D3D12_RESOURCE_DESC rd = {};
  rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  rd.Width = kUploadBufferSize;
  rd.Height = 1;
  rd.DepthOrArraySize = 1;
  rd.MipLevels = 1;
  rd.SampleDesc.Count = 1;
  rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

  HRESULT hr = dev->CreateCommittedResource(
      &hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
      IID_PPV_ARGS(&constant_upload_buffer_));
  if (FAILED(hr)) return false;

  D3D12_RANGE read_range = {0, 0};
  hr = constant_upload_buffer_->Map(0, &read_range, reinterpret_cast<void**>(&constant_upload_ptr_));
  return SUCCEEDED(hr);
}

void D3D12Renderer::BeginFrame() {
  is_frame_active_ = true;
  constant_offset_ = 0;

  ID3D12CommandAllocator* allocator = device_ctx_.GetCurrentAllocator();
  ID3D12GraphicsCommandList* list = device_ctx_.GetCommandList();
  if (!allocator || !list) return;

  allocator->Reset();
  list->Reset(allocator, nullptr);

  D3D12_CPU_DESCRIPTOR_HANDLE rtv = device_ctx_.GetCurrentRtvHandle();
  D3D12_CPU_DESCRIPTOR_HANDLE dsv = device_ctx_.GetDsvHandle();

  const float clear_color[4] = {0.0f, 0.0f, 0.0f, 1.0f};
  list->ClearRenderTargetView(rtv, clear_color, 0, nullptr);
  list->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

  list->OMSetRenderTargets(1, &rtv, FALSE, &dsv);

  D3D12_VIEWPORT vp = {0.0f, 0.0f, float(device_ctx_.GetWidth()), float(device_ctx_.GetHeight()), 0.0f, 1.0f};
  D3D12_RECT sc = {0, 0, LONG(device_ctx_.GetWidth()), LONG(device_ctx_.GetHeight())};
  list->RSSetViewports(1, &vp);
  list->RSSetScissorRects(1, &sc);

  list->SetGraphicsRootSignature(root_signature_.Get());
}

void D3D12Renderer::RenderDraw(const DrawCommand& cmd, const uint8_t* guest_memory_base) {
  ID3D12GraphicsCommandList* list = device_ctx_.GetCommandList();
  if (!list || !constant_upload_ptr_) return;

  // Actualizar constantes en el ring buffer de upload
  constexpr uint32_t kBlockSize = 256;
  auto alloc_block = [&](const void* data, uint32_t size) -> D3D12_GPU_VIRTUAL_ADDRESS {
    uint32_t aligned = (size + 255) & ~255u;
    if (constant_offset_ + aligned > (4u << 20)) constant_offset_ = 0;
    uint32_t cur = constant_offset_;
    std::memcpy(constant_upload_ptr_ + cur, data, size);
    constant_offset_ += aligned;
    return constant_upload_buffer_->GetGPUVirtualAddress() + cur;
  };

  list->SetGraphicsRootConstantBufferView(0, alloc_block(cmd.state.constants.vs_float, sizeof(cmd.state.constants.vs_float)));
  list->SetGraphicsRootConstantBufferView(1, alloc_block(cmd.state.constants.ps_float, sizeof(cmd.state.constants.ps_float)));

  uint8_t bl_data[160] = {};
  std::memcpy(bl_data, cmd.state.constants.bool_bits, sizeof(cmd.state.constants.bool_bits));
  std::memcpy(bl_data + 32, cmd.state.constants.loop_consts, sizeof(cmd.state.constants.loop_consts));
  list->SetGraphicsRootConstantBufferView(2, alloc_block(bl_data, sizeof(bl_data)));

  list->SetGraphicsRootConstantBufferView(3, alloc_block(cmd.state.constants.fetch_consts, sizeof(cmd.state.constants.fetch_consts)));
  list->SetGraphicsRootConstantBufferView(4, alloc_block(&cmd.state.constants.system, sizeof(cmd.state.constants.system)));

  D3D12_PRIMITIVE_TOPOLOGY topo = ToD3D12Topology(cmd.topology);
  list->IASetPrimitiveTopology(topo);

  if (cmd.is_indexed && cmd.index_count > 0) {
    list->DrawIndexedInstanced(cmd.index_count, 1, 0, cmd.base_vertex, 0);
  } else if (cmd.vertex_count > 0) {
    list->DrawInstanced(cmd.vertex_count, 1, 0, 0);
  }
}

void D3D12Renderer::RenderResolve(const ResolveCommand& cmd) {
  // Manejo de resolves en D3D12
}

void D3D12Renderer::EndFrame() {
  ID3D12GraphicsCommandList* list = device_ctx_.GetCommandList();
  if (list) {
    list->Close();
    ID3D12CommandList* lists[] = {list};
    device_ctx_.GetCommandQueue()->ExecuteCommandLists(1, lists);
  }
  is_frame_active_ = false;
}

void D3D12Renderer::Present() {
  device_ctx_.Present(true);
}

void D3D12Renderer::Resize(uint32_t width, uint32_t height) {
  device_ctx_.Resize(width, height);
}

void D3D12Renderer::Shutdown() {
  if (constant_upload_buffer_) {
    D3D12_RANGE empty_range = {0, 0};
    constant_upload_buffer_->Unmap(0, &empty_range);
    constant_upload_buffer_.Reset();
  }
  root_signature_.Reset();
  guest_memory_buffer_.Reset();
  device_ctx_.Shutdown();
}

}  // namespace re_wor::nativegfx
#endif
