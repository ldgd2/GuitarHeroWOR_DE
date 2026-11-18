// nativegfx/backend/d3d11/d3d11_renderer.cpp
// Implementacion del renderizador nativo Direct3D 11.
#ifdef _WIN32
#include "nativegfx/backend/d3d11/d3d11_renderer.hpp"

#include <windows.h>
#include <vector>

namespace re_wor::nativegfx {

using Microsoft::WRL::ComPtr;

namespace {

static bool SafeMemoryCopy(void* dst, const void* src, size_t size) {
#ifdef _WIN32
  __try {
    std::memcpy(dst, src, size);
    return true;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
#else
  std::memcpy(dst, src, size);
  return true;
#endif
}

static size_t GetReadableBytes(const void* ptr, size_t max_size) {
  if (!ptr || max_size == 0) return 0;
  const uint8_t* p = static_cast<const uint8_t*>(ptr);
  MEMORY_BASIC_INFORMATION mbi;
  if (!VirtualQuery(p, &mbi, sizeof(mbi))) return 0;
  if (mbi.State != MEM_COMMIT) return 0;
  if ((mbi.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE)) == 0) return 0;
  if (mbi.Protect & PAGE_GUARD) return 0;
  size_t available = mbi.RegionSize - (p - static_cast<const uint8_t*>(mbi.BaseAddress));
  return std::min(available, max_size);
}

ComPtr<ID3D11Buffer> CreateConstantBuffer(ID3D11Device* dev, UINT size) {
  D3D11_BUFFER_DESC desc = {};
  desc.ByteWidth = (size + 15) & ~15u;
  desc.Usage = D3D11_USAGE_DEFAULT;
  desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
  ComPtr<ID3D11Buffer> buf;
  dev->CreateBuffer(&desc, nullptr, &buf);
  return buf;
}

D3D11_PRIMITIVE_TOPOLOGY ToD3DTopology(PrimitiveTopology topo) {
  switch (topo) {
    case PrimitiveTopology::PointList:     return D3D11_PRIMITIVE_TOPOLOGY_POINTLIST;
    case PrimitiveTopology::LineList:      return D3D11_PRIMITIVE_TOPOLOGY_LINELIST;
    case PrimitiveTopology::LineStrip:     return D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP;
    case PrimitiveTopology::TriangleList:  return D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    case PrimitiveTopology::TriangleStrip: return D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
    case PrimitiveTopology::RectangleList: return D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    default:                               return D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
  }
}

}  // namespace

D3D11Renderer::D3D11Renderer() = default;

D3D11Renderer::~D3D11Renderer() {
  Shutdown();
}

bool D3D11Renderer::Initialize(void* native_window_handle, uint32_t width, uint32_t height) {
  if (!device_ctx_.Initialize(native_window_handle, width, height)) {
    return false;
  }

  ID3D11Device* dev = device_ctx_.GetDevice();
  ID3D11DeviceContext* ctx = device_ctx_.GetContext();

  state_cache_.Initialize(dev);
  shader_cache_.Initialize(dev);
  texture_cache_.Initialize(dev, ctx);

  CreateConstantBuffers();

  // Buffer de memoria fisica raw para lecturas de shaders (t0: xe_mem)
  constexpr uint32_t kRawMemBytes = 512u << 20;   // 512 MB de memoria física de la 360
  D3D11_BUFFER_DESC md = {};
  md.ByteWidth = kRawMemBytes;
  md.Usage = D3D11_USAGE_DEFAULT;
  md.BindFlags = D3D11_BIND_SHADER_RESOURCE;
  md.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;

  if (SUCCEEDED(dev->CreateBuffer(&md, nullptr, &guest_memory_buffer_))) {
    D3D11_SHADER_RESOURCE_VIEW_DESC sd = {};
    sd.Format = DXGI_FORMAT_R32_TYPELESS;
    sd.ViewDimension = D3D11_SRV_DIMENSION_BUFFEREX;
    sd.BufferEx.NumElements = kRawMemBytes / 4;
    sd.BufferEx.Flags = D3D11_BUFFEREX_SRV_FLAG_RAW;
    dev->CreateShaderResourceView(guest_memory_buffer_.Get(), &sd, &guest_memory_srv_);
  }

  return true;
}

void D3D11Renderer::CreateConstantBuffers() {
  ID3D11Device* dev = device_ctx_.GetDevice();
  if (!dev) return;

  cb_vs_c_ = CreateConstantBuffer(dev, 4096);                // 256 x float4
  cb_ps_c_ = CreateConstantBuffer(dev, 4096);                // 256 x float4
  cb_bl_   = CreateConstantBuffer(dev, 160);                 // 8 bools + 32 loops
  cb_fc_   = CreateConstantBuffer(dev, 768);                 // 192 fetch constants
  cb_sys_  = CreateConstantBuffer(dev, sizeof(SystemNdcConstants));
}

void D3D11Renderer::BeginFrame() {
  ID3D11DeviceContext* ctx = device_ctx_.GetContext();
  if (!ctx) return;

  is_frame_active_ = true;
  std::memset(uploaded_pages_mask_, 0, sizeof(uploaded_pages_mask_));

  ID3D11RenderTargetView* rtv = device_ctx_.GetBackBufferRTV();
  ID3D11DepthStencilView* dsv = device_ctx_.GetDefaultDSV();

  const float clear_color[4] = {0.0f, 0.0f, 0.0f, 1.0f};
  if (rtv) ctx->ClearRenderTargetView(rtv, clear_color);
  if (dsv) ctx->ClearDepthStencilView(dsv, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);

  ctx->OMSetRenderTargets(1, &rtv, dsv);

  D3D11_VIEWPORT vp = {};
  vp.Width    = float(device_ctx_.GetWidth());
  vp.Height   = float(device_ctx_.GetHeight());
  vp.MinDepth = 0.0f;
  vp.MaxDepth = 1.0f;
  ctx->RSSetViewports(1, &vp);
}

void D3D11Renderer::UpdateConstantBuffers(const ShaderConstants& constants) {
  ID3D11DeviceContext* ctx = device_ctx_.GetContext();
  if (!ctx) return;

  ctx->UpdateSubresource(cb_vs_c_.Get(), 0, nullptr, constants.vs_float, 0, 0);
  ctx->UpdateSubresource(cb_ps_c_.Get(), 0, nullptr, constants.ps_float, 0, 0);

  // Buffer de Bools y Loops combinados (160 bytes)
  uint8_t bl_data[160] = {};
  std::memcpy(bl_data, constants.bool_bits, sizeof(constants.bool_bits));
  std::memcpy(bl_data + 32, constants.loop_consts, sizeof(constants.loop_consts));
  ctx->UpdateSubresource(cb_bl_.Get(), 0, nullptr, bl_data, 0, 0);

  ctx->UpdateSubresource(cb_fc_.Get(), 0, nullptr, constants.fetch_consts, 0, 0);
  ctx->UpdateSubresource(cb_sys_.Get(), 0, nullptr, &constants.system, 0, 0);

  ID3D11Buffer* vcb[4] = {cb_vs_c_.Get(), cb_bl_.Get(), cb_fc_.Get(), cb_sys_.Get()};
  ID3D11Buffer* pcb[4] = {cb_ps_c_.Get(), cb_bl_.Get(), cb_fc_.Get(), cb_sys_.Get()};

  ctx->VSSetConstantBuffers(0, 4, vcb);
  ctx->PSSetConstantBuffers(0, 4, pcb);
}

void D3D11Renderer::RenderDraw(const DrawCommand& cmd, const uint8_t* guest_memory_base) {
  ID3D11DeviceContext* ctx = device_ctx_.GetContext();
  if (!ctx) return;

  // 1. Estados de pipeline
  ID3D11BlendState* bs = state_cache_.GetOrCreateBlendState(cmd.state.blend);
  ctx->OMSetBlendState(bs, &cmd.state.blend.blend_constant.x, 0xFFFFFFFF);

  ID3D11DepthStencilState* dss = state_cache_.GetOrCreateDepthStencilState(cmd.state.depth_stencil);
  ctx->OMSetDepthStencilState(dss, cmd.state.depth_stencil.stencil_ref);

  ID3D11RasterizerState* rs = state_cache_.GetOrCreateRasterizerState(cmd.state.rasterizer);
  ctx->RSSetState(rs);

  if (cmd.state.rasterizer.scissor_enable) {
    const Rect2D& r = cmd.state.rasterizer.scissor_rect;
    D3D11_RECT d3d_rect = {
        std::max(0L, static_cast<LONG>(r.left)),
        std::max(0L, static_cast<LONG>(r.top)),
        std::min(static_cast<LONG>(device_ctx_.GetWidth()), static_cast<LONG>(r.right)),
        std::min(static_cast<LONG>(device_ctx_.GetHeight()), static_cast<LONG>(r.bottom))
    };
    if (d3d_rect.right > d3d_rect.left && d3d_rect.bottom > d3d_rect.top) {
      ctx->RSSetScissorRects(1, &d3d_rect);
    }
  }

  // 2. Shaders
  const auto& vs_info = cmd.state.vertex_shader;
  const auto& ps_info = cmd.state.pixel_shader;

  ID3D11VertexShader* vs = shader_cache_.GetOrCreateVertexShader(vs_info.hash, "");
  ID3D11PixelShader* ps  = shader_cache_.GetOrCreatePixelShader(ps_info.hash, "");

  // Direct3D 11 requiere obligatoriamente un Vertex Shader para emitir geometría
  if (!vs) return;

  ctx->VSSetShader(vs, nullptr, 0);
  ctx->PSSetShader(ps, nullptr, 0);

  // Geometria especial de rectangulos
  if (cmd.topology == PrimitiveTopology::RectangleList) {
    ctx->GSSetShader(shader_cache_.GetRectangleGeometryShader(), nullptr, 0);
  } else {
    ctx->GSSetShader(nullptr, nullptr, 0);
  }

  // 3. Constantes
  UpdateConstantBuffers(cmd.state.constants);

  // 4. Memoria física xe_mem en t0 (vfetch de vértices)
  if (guest_memory_srv_ && guest_memory_base) {
    constexpr uint32_t kRawMemBytes = 512u << 20;
    // Slots de fetch de vértices reales de la 360 (95, 94, 93; slot 0 solo si el shader lo pide)
    static const uint32_t kKnownVfcSlots[] = {95, 94, 93};
    const bool uses_vfc0 = (vs_info.hash == 0x1E6883FCCDE1F688ull);

    auto upload_slot = [&](uint32_t slot) {
      uint32_t vaddr = cmd.state.constants.fetch_consts[slot * 2 + 0];
      uint32_t phys_addr = (vaddr & 0x1FFFFFFFu) & ~63u;

      if (phys_addr >= 0x00010000 && phys_addr < kRawMemBytes) {
        uint32_t count = cmd.is_indexed ? (cmd.index_count > 0 ? cmd.index_count : 1024u)
                                        : (cmd.vertex_count > 0 ? cmd.vertex_count : 1024u);
        uint32_t upload_size = std::min(kRawMemBytes - phys_addr, std::max(count * 64u, 4096u));
        upload_size = std::min(upload_size, 262144u);

        uint32_t page_start = phys_addr >> 16;
        uint32_t page_end = (phys_addr + upload_size - 1) >> 16;
        if (page_end >= 8192) page_end = 8191;

        bool all_uploaded = true;
        for (uint32_t p = page_start; p <= page_end; ++p) {
          if ((uploaded_pages_mask_[p >> 6] & (1ULL << (p & 63))) == 0) {
            all_uploaded = false;
            break;
          }
        }

        if (!all_uploaded) {
          const uint8_t* src_ptr = guest_memory_base + phys_addr;
          size_t safe_bytes = GetReadableBytes(src_ptr, upload_size);
          if (safe_bytes >= 64) {
            if (temp_upload_.size() < safe_bytes) temp_upload_.resize(safe_bytes);
            if (SafeMemoryCopy(temp_upload_.data(), src_ptr, safe_bytes)) {
              D3D11_BOX box = {};
              box.left = phys_addr;
              box.right = phys_addr + static_cast<UINT>(safe_bytes);
              box.top = 0;
              box.bottom = 1;
              box.front = 0;
              box.back = 1;
              ctx->UpdateSubresource(guest_memory_buffer_.Get(), 0, &box, temp_upload_.data(), 0, 0);

              for (uint32_t p = page_start; p <= page_end; ++p) {
                uploaded_pages_mask_[p >> 6] |= (1ULL << (p & 63));
              }
            }
          }
        }
      }
    };

    for (uint32_t slot : kKnownVfcSlots) {
      upload_slot(slot);
    }
    if (uses_vfc0) {
      upload_slot(0);
    }

    ctx->VSSetShaderResources(0, 1, guest_memory_srv_.GetAddressOf());
    ctx->PSSetShaderResources(0, 1, guest_memory_srv_.GetAddressOf());
  }

  // 5. Texturas y Samplers
  for (uint32_t i = 0; i < 32; ++i) {
    const auto& tex_desc = cmd.state.bound_textures[i];
    if (tex_desc.IsValid()) {
      ID3D11ShaderResourceView* srv = texture_cache_.GetOrCreateTexture(tex_desc, guest_memory_base);
      if (srv) {
        UINT slot = 1 + i;
        if (tex_desc.dimension == TextureDimension::ThreeD) {
          slot = 33 + i;
        } else if (tex_desc.dimension == TextureDimension::Cube) {
          slot = 65 + i;
        }
        ctx->PSSetShaderResources(slot, 1, &srv);
      }

      // D3D11 limita estrictamente los samplers a [0..15] (D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT = 16)
      if (i < 16) {
        ID3D11SamplerState* smp = state_cache_.GetOrCreateSamplerState(tex_desc);
        if (smp) {
          ctx->PSSetSamplers(i, 1, &smp);
        }
      }
    }
  }

  // 6. Ejecutar Draw
  D3D11_PRIMITIVE_TOPOLOGY topo = ToD3DTopology(cmd.topology);
  ctx->IASetPrimitiveTopology(topo);

  if (cmd.is_indexed && cmd.index_count > 0) {
    // Si los índices están en memoria del juego
    if (cmd.index_buffer_address && guest_memory_base) {
      uint32_t idx_addr = cmd.index_buffer_address & 0x1FFFFFFFu;
      if (idx_addr >= 0x00010000 && idx_addr < (512u << 20)) {
        const uint8_t* idx_bytes = guest_memory_base + idx_addr;
        DXGI_FORMAT ifmt = (cmd.index_format == IndexFormat::Index32) ? DXGI_FORMAT_R32_UINT : DXGI_FORMAT_R16_UINT;
        uint32_t idx_stride = (cmd.index_format == IndexFormat::Index32) ? 4 : 2;
        uint32_t total_bytes = cmd.index_count * idx_stride;

        size_t safe_ib_bytes = GetReadableBytes(idx_bytes, total_bytes);
        if (safe_ib_bytes >= total_bytes) {
          ID3D11Device* dev = device_ctx_.GetDevice();
          if (!dynamic_ib_ || dynamic_ib_size_ < total_bytes) {
            dynamic_ib_.Reset();
            D3D11_BUFFER_DESC ibd = {};
            ibd.ByteWidth = (total_bytes + 65535) & ~65535u;
            ibd.Usage = D3D11_USAGE_DYNAMIC;
            ibd.BindFlags = D3D11_BIND_INDEX_BUFFER;
            ibd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            dev->CreateBuffer(&ibd, nullptr, &dynamic_ib_);
            dynamic_ib_size_ = ibd.ByteWidth;
          }

          if (dynamic_ib_) {
            D3D11_MAPPED_SUBRESOURCE mapped;
            if (SUCCEEDED(ctx->Map(dynamic_ib_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
              if (SafeMemoryCopy(mapped.pData, idx_bytes, total_bytes)) {
                ctx->Unmap(dynamic_ib_.Get(), 0);
                ctx->IASetIndexBuffer(dynamic_ib_.Get(), ifmt, 0);
                ctx->DrawIndexed(cmd.index_count, 0, cmd.base_vertex);
              } else {
                ctx->Unmap(dynamic_ib_.Get(), 0);
              }
            }
          }
        }
      }
    }
  } else if (cmd.vertex_count > 0) {
    ctx->Draw(cmd.vertex_count, 0);
  }
}

void D3D11Renderer::RenderResolve(const ResolveCommand& cmd) {
  // En la Fase 3, los resolves del escenario preparan las texturas que consumirán los pases posteriores
}

void D3D11Renderer::EndFrame() {
  is_frame_active_ = false;
}

void D3D11Renderer::Present() {
  device_ctx_.Present(vsync_);
}

void D3D11Renderer::Resize(uint32_t width, uint32_t height) {
  device_ctx_.Resize(width, height);
}

void D3D11Renderer::Shutdown() {
  cb_vs_c_.Reset();
  cb_ps_c_.Reset();
  cb_bl_.Reset();
  cb_fc_.Reset();
  cb_sys_.Reset();
  guest_memory_srv_.Reset();
  guest_memory_buffer_.Reset();
  dynamic_ib_.Reset();

  texture_cache_.Shutdown();
  shader_cache_.Shutdown();
  state_cache_.Clear();
  device_ctx_.Shutdown();
}

}  // namespace re_wor::nativegfx
#endif
