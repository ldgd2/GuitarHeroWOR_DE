// nativegfx/backend/d3d11/d3d11_renderer.cpp
// Implementacion del renderizador nativo Direct3D 11.
#ifdef _WIN32
#include "nativegfx/backend/d3d11/d3d11_renderer.hpp"

#include <windows.h>
#include <vector>

namespace re_wor::nativegfx {

using Microsoft::WRL::ComPtr;

namespace {


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
  texture_cache_.BeginFrame(frame_counter_++);
  std::memset(uploaded_pages_mask_, 0, sizeof(uploaded_pages_mask_));
  last_blend_state_ = nullptr;
  std::memset(last_blend_factor_, 0, sizeof(last_blend_factor_));
  last_ds_state_ = nullptr;
  last_stencil_ref_ = 0xFFFFFFFF;
  last_rs_state_ = nullptr;
  last_vs_ = nullptr;
  last_ps_ = nullptr;
  last_topo_ = D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;
  std::memset(last_bound_srv_, 0, sizeof(last_bound_srv_));
  std::memset(last_bound_sampler_, 0, sizeof(last_bound_sampler_));
  std::memset(last_tex_hash_, 0, sizeof(last_tex_hash_));
  std::memset(last_cached_srv_, 0, sizeof(last_cached_srv_));
  std::memset(last_cached_smp_, 0, sizeof(last_cached_smp_));
  guest_srv_bound_ = false;
  last_gs_ = nullptr;
  std::memset(&last_scissor_rect_, 0, sizeof(last_scissor_rect_));
  std::memset(last_cb_version_, 0, sizeof(last_cb_version_));
  last_draw_fetch_version_ = 0xFFFFFFFF;
  last_ib_addr_ = 0;
  last_ib_count_ = 0;
  last_ib_format_ = IndexFormat::Index16;

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

  // Vincular ranuras fijas de buffers de constantes una vez por cuadro
  ID3D11Buffer* vcb[4] = {cb_vs_c_.Get(), cb_bl_.Get(), cb_fc_.Get(), cb_sys_.Get()};
  ID3D11Buffer* pcb[4] = {cb_ps_c_.Get(), cb_bl_.Get(), cb_fc_.Get(), cb_sys_.Get()};
  ctx->VSSetConstantBuffers(0, 4, vcb);
  ctx->PSSetConstantBuffers(0, 4, pcb);
}

void D3D11Renderer::UpdateConstantBuffers(const ShaderConstants& constants) {
  ID3D11DeviceContext* ctx = device_ctx_.GetContext();
  if (!ctx) return;

  if (constants.vs_version != last_cb_version_[0]) {
    ctx->UpdateSubresource(cb_vs_c_.Get(), 0, nullptr, constants.vs_float, 0, 0);
    last_cb_version_[0] = constants.vs_version;
  }

  if (constants.ps_version != last_cb_version_[1]) {
    ctx->UpdateSubresource(cb_ps_c_.Get(), 0, nullptr, constants.ps_float, 0, 0);
    last_cb_version_[1] = constants.ps_version;
  }

  if (constants.bool_version != last_cb_version_[2]) {
    uint8_t bl_data[160] = {};
    std::memcpy(bl_data, constants.bool_bits, sizeof(constants.bool_bits));
    std::memcpy(bl_data + 32, constants.loop_consts, sizeof(constants.loop_consts));
    ctx->UpdateSubresource(cb_bl_.Get(), 0, nullptr, bl_data, 0, 0);
    last_cb_version_[2] = constants.bool_version;
  }

  if (constants.fetch_version != last_cb_version_[3]) {
    ctx->UpdateSubresource(cb_fc_.Get(), 0, nullptr, constants.fetch_consts, 0, 0);
    last_cb_version_[3] = constants.fetch_version;
  }

  if (constants.system_version != last_cb_version_[4]) {
    ctx->UpdateSubresource(cb_sys_.Get(), 0, nullptr, &constants.system, 0, 0);
    last_cb_version_[4] = constants.system_version;
  }
}

void D3D11Renderer::RenderDraw(const DrawCommand& cmd, const uint8_t* guest_memory_base) {
  ID3D11DeviceContext* ctx = device_ctx_.GetContext();
  if (!ctx) return;

  // 1. Estados de pipeline con filtro de redundancia
  ID3D11BlendState* bs = state_cache_.GetOrCreateBlendState(cmd.state.blend);
  if (bs != last_blend_state_ || std::memcmp(last_blend_factor_, &cmd.state.blend.blend_constant.x, sizeof(last_blend_factor_)) != 0) {
    ctx->OMSetBlendState(bs, &cmd.state.blend.blend_constant.x, 0xFFFFFFFF);
    last_blend_state_ = bs;
    std::memcpy(last_blend_factor_, &cmd.state.blend.blend_constant.x, sizeof(last_blend_factor_));
  }

  ID3D11DepthStencilState* dss = state_cache_.GetOrCreateDepthStencilState(cmd.state.depth_stencil);
  if (dss != last_ds_state_ || cmd.state.depth_stencil.stencil_ref != last_stencil_ref_) {
    ctx->OMSetDepthStencilState(dss, cmd.state.depth_stencil.stencil_ref);
    last_ds_state_ = dss;
    last_stencil_ref_ = cmd.state.depth_stencil.stencil_ref;
  }

  ID3D11RasterizerState* rs = state_cache_.GetOrCreateRasterizerState(cmd.state.rasterizer);
  if (rs != last_rs_state_) {
    ctx->RSSetState(rs);
    last_rs_state_ = rs;
  }

  if (cmd.state.rasterizer.scissor_enable) {
    const Rect2D& r = cmd.state.rasterizer.scissor_rect;
    D3D11_RECT d3d_rect = {
        std::max(0L, static_cast<LONG>(r.left)),
        std::max(0L, static_cast<LONG>(r.top)),
        std::min(static_cast<LONG>(device_ctx_.GetWidth()), static_cast<LONG>(r.right)),
        std::min(static_cast<LONG>(device_ctx_.GetHeight()), static_cast<LONG>(r.bottom))
    };
    if (d3d_rect.right > d3d_rect.left && d3d_rect.bottom > d3d_rect.top) {
      if (std::memcmp(&d3d_rect, &last_scissor_rect_, sizeof(D3D11_RECT)) != 0) {
        ctx->RSSetScissorRects(1, &d3d_rect);
        last_scissor_rect_ = d3d_rect;
      }
    }
  }

  // 2. Shaders con filtro de redundancia
  const auto& vs_info = cmd.state.vertex_shader;
  const auto& ps_info = cmd.state.pixel_shader;

  ID3D11VertexShader* vs = shader_cache_.GetOrCreateVertexShader(vs_info.hash, "");
  ID3D11PixelShader* ps  = shader_cache_.GetOrCreatePixelShader(ps_info.hash, "");

  // Direct3D 11 requiere obligatoriamente un Vertex Shader para emitir geometría
  if (!vs) return;

  if (vs != last_vs_) {
    ctx->VSSetShader(vs, nullptr, 0);
    last_vs_ = vs;
  }
  if (ps != last_ps_) {
    ctx->PSSetShader(ps, nullptr, 0);
    last_ps_ = ps;
  }

  // Geometria especial de rectangulos (con filtro de redundancia de Geometry Shader)
  ID3D11GeometryShader* desired_gs = (cmd.topology == PrimitiveTopology::RectangleList) ? shader_cache_.GetRectangleGeometryShader() : nullptr;
  if (desired_gs != last_gs_) {
    ctx->GSSetShader(desired_gs, nullptr, 0);
    last_gs_ = desired_gs;
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
          size_t safe_bytes = (phys_addr >= 0x00010000 && (phys_addr + upload_size <= kRawMemBytes)) ? upload_size : 0;
          if (safe_bytes >= 64) {
            D3D11_BOX box = {};
            box.left = phys_addr;
            box.right = phys_addr + static_cast<UINT>(safe_bytes);
            box.top = 0;
            box.bottom = 1;
            box.front = 0;
            box.back = 1;
            ctx->UpdateSubresource(guest_memory_buffer_.Get(), 0, &box, src_ptr, 0, 0);

            for (uint32_t p = page_start; p <= page_end; ++p) {
              uploaded_pages_mask_[p >> 6] |= (1ULL << (p & 63));
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

    if (!guest_srv_bound_) {
      ctx->VSSetShaderResources(0, 1, guest_memory_srv_.GetAddressOf());
      ctx->PSSetShaderResources(0, 1, guest_memory_srv_.GetAddressOf());
      guest_srv_bound_ = true;
    }
  }

  // 5. Texturas y Samplers con cache de ranura y filtro de redundancia
  if (cmd.state.constants.fetch_version != last_draw_fetch_version_) {
    for (uint32_t i = 0; i < 32; ++i) {
      const auto& tex_desc = cmd.state.bound_textures[i];
      if (tex_desc.width > 0 && tex_desc.IsValid()) {
        uint64_t thash = TextureHash(tex_desc);
        ID3D11ShaderResourceView* srv = nullptr;
        ID3D11SamplerState* smp = nullptr;

        if (thash == last_tex_hash_[i] && last_cached_srv_[i]) {
          srv = last_cached_srv_[i];
          smp = last_cached_smp_[i];
        } else {
          srv = texture_cache_.GetOrCreateTexture(tex_desc, guest_memory_base);
          smp = (i < 16) ? state_cache_.GetOrCreateSamplerState(tex_desc) : nullptr;
          last_tex_hash_[i] = thash;
          last_cached_srv_[i] = srv;
          last_cached_smp_[i] = smp;
        }

        if (srv) {
          UINT slot = 1 + i;
          if (tex_desc.dimension == TextureDimension::ThreeD) {
            slot = 33 + i;
          } else if (tex_desc.dimension == TextureDimension::Cube) {
            slot = 65 + i;
          }
          if (srv != last_bound_srv_[slot]) {
            ctx->PSSetShaderResources(slot, 1, &srv);
            last_bound_srv_[slot] = srv;
          }
        }

        // D3D11 limita estrictamente los samplers a [0..15]
        if (i < 16 && smp) {
          if (smp != last_bound_sampler_[i]) {
            ctx->PSSetSamplers(i, 1, &smp);
            last_bound_sampler_[i] = smp;
          }
        }
      }
    }
    last_draw_fetch_version_ = cmd.state.constants.fetch_version;
  }

  // 6. Ejecutar Draw con filtro de redundancia de topologia
  D3D11_PRIMITIVE_TOPOLOGY topo = ToD3DTopology(cmd.topology);
  if (topo != last_topo_) {
    ctx->IASetPrimitiveTopology(topo);
    last_topo_ = topo;
  }

  if (cmd.is_indexed && cmd.index_count > 0) {
    // Si los índices están en memoria del juego
    if (cmd.index_buffer_address && guest_memory_base) {
      uint32_t idx_addr = cmd.index_buffer_address & 0x1FFFFFFFu;
      if (idx_addr >= 0x00010000 && idx_addr < (512u << 20)) {
        if (idx_addr == last_ib_addr_ && cmd.index_count == last_ib_count_ && cmd.index_format == last_ib_format_ && dynamic_ib_) {
          // Omitir Map/memcpy/IASetIndexBuffer redundante si es el mismo buffer de indices que el draw previo
          ctx->DrawIndexed(cmd.index_count, 0, cmd.base_vertex);
        } else {
          const uint8_t* idx_bytes = guest_memory_base + idx_addr;
          DXGI_FORMAT ifmt = (cmd.index_format == IndexFormat::Index32) ? DXGI_FORMAT_R32_UINT : DXGI_FORMAT_R16_UINT;
          uint32_t idx_stride = (cmd.index_format == IndexFormat::Index32) ? 4 : 2;
          uint32_t total_bytes = cmd.index_count * idx_stride;

          size_t safe_ib_bytes = (idx_addr >= 0x00010000 && (idx_addr + total_bytes <= (512u << 20))) ? total_bytes : 0;
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
                std::memcpy(mapped.pData, idx_bytes, total_bytes);
                ctx->Unmap(dynamic_ib_.Get(), 0);
                ctx->IASetIndexBuffer(dynamic_ib_.Get(), ifmt, 0);
                ctx->DrawIndexed(cmd.index_count, 0, cmd.base_vertex);
                last_ib_addr_ = idx_addr;
                last_ib_count_ = cmd.index_count;
                last_ib_format_ = cmd.index_format;
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
  if (cmd.dest_texture_address == 0) return;
  last_draw_fetch_version_ = 0xFFFFFFFF;
  ID3D11DeviceContext* ctx = device_ctx_.GetContext();
  ID3D11Texture2D* src_tex = device_ctx_.GetBackBufferTexture();
  if (!ctx || !src_tex) return;

  D3D11_TEXTURE2D_DESC sd;
  src_tex->GetDesc(&sd);

  uint32_t w = (cmd.width > 0 && cmd.width <= sd.Width) ? cmd.width : sd.Width;
  uint32_t h = (cmd.height > 0 && cmd.height <= sd.Height) ? cmd.height : sd.Height;

  ID3D11Texture2D* resolved_tex = texture_cache_.GetOrCreateResolveTexture(cmd.dest_texture_address, w, h, sd.Format);
  if (resolved_tex) {
    D3D11_BOX box = {};
    box.right = w;
    box.bottom = h;
    box.back = 1;
    ctx->CopySubresourceRegion(resolved_tex, 0, 0, 0, 0, src_tex, 0, &box);
  }
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
