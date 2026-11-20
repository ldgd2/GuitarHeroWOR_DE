// nativegfx/backend/d3d11/d3d11_texture_cache.cpp
// Implementacion del cache de texturas en Direct3D 11.
#ifdef _WIN32
#include "nativegfx/backend/d3d11/d3d11_texture_cache.hpp"

#include <windows.h>
#include <vector>
#include <algorithm>

namespace re_wor::nativegfx {

using Microsoft::WRL::ComPtr;


bool D3D11TextureCache::Initialize(ID3D11Device* device, ID3D11DeviceContext* context) {
  device_ = device;
  context_ = context;
  if (!device_ || !context_) return false;

  uint32_t pixel = 0x00000000;

  // 1. Textura dummy 2D (1x1 transparente)
  {
    D3D11_TEXTURE2D_DESC td = {};
    td.Width = 1;
    td.Height = 1;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_IMMUTABLE;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA init = {&pixel, 4, 0};
    ComPtr<ID3D11Texture2D> tex2d;
    if (SUCCEEDED(device_->CreateTexture2D(&td, &init, &tex2d))) {
      device_->CreateShaderResourceView(tex2d.Get(), nullptr, &dummy2d_);
    }
  }

  // 2. Textura dummy 3D (1x1x1 transparente)
  {
    D3D11_TEXTURE3D_DESC td3 = {};
    td3.Width = 1;
    td3.Height = 1;
    td3.Depth = 1;
    td3.MipLevels = 1;
    td3.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td3.Usage = D3D11_USAGE_IMMUTABLE;
    td3.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA init3 = {&pixel, 4, 4};
    ComPtr<ID3D11Texture3D> tex3d;
    if (SUCCEEDED(device_->CreateTexture3D(&td3, &init3, &tex3d))) {
      device_->CreateShaderResourceView(tex3d.Get(), nullptr, &dummy3d_);
    }
  }

  // 3. Textura dummy Cube (1x1x6 transparente)
  {
    D3D11_TEXTURE2D_DESC tdc = {};
    tdc.Width = 1;
    tdc.Height = 1;
    tdc.MipLevels = 1;
    tdc.ArraySize = 6;
    tdc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    tdc.SampleDesc.Count = 1;
    tdc.Usage = D3D11_USAGE_IMMUTABLE;
    tdc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    tdc.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE;

    D3D11_SUBRESOURCE_DATA cube_init[6];
    for (int f = 0; f < 6; ++f) {
      cube_init[f] = {&pixel, 4, 0};
    }
    ComPtr<ID3D11Texture2D> texcube;
    if (SUCCEEDED(device_->CreateTexture2D(&tdc, cube_init, &texcube))) {
      D3D11_SHADER_RESOURCE_VIEW_DESC srvd = {};
      srvd.Format = tdc.Format;
      srvd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE;
      srvd.TextureCube.MipLevels = 1;
      device_->CreateShaderResourceView(texcube.Get(), &srvd, &dummycube_);
    }
  }

  return true;
}

ID3D11ShaderResourceView* D3D11TextureCache::GetDummySRV(TextureDimension dim) const {
  if (dim == TextureDimension::ThreeD && dummy3d_) return dummy3d_.Get();
  if (dim == TextureDimension::Cube && dummycube_) return dummycube_.Get();
  return dummy2d_.Get();
}

void D3D11TextureCache::BeginFrame(uint64_t frame_index) {
  current_frame_ = frame_index;

  // Limpieza periódica cada 120 cuadros con presupuesto ampliado para evitar tirones (texture thrashing)
  if ((frame_index % 120) == 0 && memory_texture_cache_.size() > 512) {
    for (auto it = memory_texture_cache_.begin(); it != memory_texture_cache_.end(); ) {
      // Si la textura no ha sido usada en los ultimos 600 cuadros (10s a 60fps), desalojar
      if (frame_index > it->second.last_frame_used && (frame_index - it->second.last_frame_used) > 600) {
        it = memory_texture_cache_.erase(it);
      } else {
        ++it;
      }
    }

    // Si aun supera 2048 texturas en memoria, recortar a las 1024 mas recientes
    if (memory_texture_cache_.size() > 2048) {
      std::vector<std::pair<uint64_t, uint64_t>> entries; // frame, key
      entries.reserve(memory_texture_cache_.size());
      for (const auto& [k, res] : memory_texture_cache_) {
        entries.emplace_back(res.last_frame_used, k);
      }
      std::sort(entries.begin(), entries.end());
      size_t to_remove = memory_texture_cache_.size() - 1024;
      for (size_t i = 0; i < to_remove; ++i) {
        memory_texture_cache_.erase(entries[i].second);
      }
    }
  }
}

ID3D11ShaderResourceView* D3D11TextureCache::GetOrCreateTexture(const TextureDesc& desc, const uint8_t* guest_memory) {
  if (!device_ || !context_ || !desc.IsValid()) {
    return GetDummySRV(desc.dimension);
  }

  // Comprobar si la textura proviene de un resolve previo
  auto res_it = resolve_texture_cache_.find(desc.guest_physical_address);
  if (res_it != resolve_texture_cache_.end()) {
    return res_it->second.srv.Get();
  }

  uint64_t key = TextureHash(desc);
  auto it = memory_texture_cache_.find(key);
  if (it != memory_texture_cache_.end()) {
    it->second.last_frame_used = current_frame_;
    return it->second.srv.Get();
  }

  // Mapear formato Xbox 360 a DXGI
  DXGI_FORMAT dxgi_fmt = DXGI_FORMAT_R8G8B8A8_UNORM;
  switch (desc.format_id) {
    case 18:
    case 51:
    case 58:
    case 59:
    case 60:
      dxgi_fmt = DXGI_FORMAT_BC1_UNORM;
      break;
    case 19:
    case 52:
      dxgi_fmt = DXGI_FORMAT_BC2_UNORM;
      break;
    case 20:
    case 53:
      dxgi_fmt = DXGI_FORMAT_BC3_UNORM;
      break;
    case 49: // DXN / ATI2N (Normal maps en Warriors of Rock)
      dxgi_fmt = DXGI_FORMAT_BC5_UNORM;
      break;
    case 2:
    case 8:
    case 9:
      dxgi_fmt = DXGI_FORMAT_R8_UNORM;
      break;
    case 10:
      dxgi_fmt = DXGI_FORMAT_R8G8_UNORM;
      break;
    case 4:
      dxgi_fmt = DXGI_FORMAT_B5G6R5_UNORM;
      break;
    case 3:
      dxgi_fmt = DXGI_FORMAT_B5G5R5A1_UNORM;
      break;
    case 30:
      dxgi_fmt = DXGI_FORMAT_R16_FLOAT;
      break;
    case 31:
      dxgi_fmt = DXGI_FORMAT_R16G16_FLOAT;
      break;
    case 32:
      dxgi_fmt = DXGI_FORMAT_R16G16B16A16_FLOAT;
      break;
    case 7:
    case 54:
      dxgi_fmt = DXGI_FORMAT_R10G10B10A2_UNORM;
      break;
    default:
      dxgi_fmt = DXGI_FORMAT_R8G8B8A8_UNORM;
      break;
  }

  TextureResource res;
  res.width = desc.width;
  res.height = desc.height;
  res.format = desc.format_id;
  res.last_frame_used = current_frame_;

  if (desc.dimension == TextureDimension::Cube) {
    D3D11_TEXTURE2D_DESC td = {};
    td.Width = desc.width;
    td.Height = desc.height;
    td.MipLevels = desc.mip_levels ? desc.mip_levels : 1;
    td.ArraySize = 6;
    td.Format = dxgi_fmt;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    td.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE;

    HRESULT hr = device_->CreateTexture2D(&td, nullptr, &res.texture);
    if (FAILED(hr)) {
      memory_texture_cache_[key] = {nullptr, dummycube_, 0, 0, 0, current_frame_};
      return dummycube_.Get();
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC sd = {};
    sd.Format = dxgi_fmt;
    sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE;
    sd.TextureCube.MipLevels = td.MipLevels;
    hr = device_->CreateShaderResourceView(res.texture.Get(), &sd, &res.srv);
    if (FAILED(hr)) {
      memory_texture_cache_[key] = {nullptr, dummycube_, 0, 0, 0, current_frame_};
      return dummycube_.Get();
    }
  } else {
    // 2D estándar o 1D
    D3D11_TEXTURE2D_DESC td = {};
    td.Width = desc.width;
    td.Height = desc.height;
    td.MipLevels = desc.mip_levels ? desc.mip_levels : 1;
    td.ArraySize = 1;
    td.Format = dxgi_fmt;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    HRESULT hr = device_->CreateTexture2D(&td, nullptr, &res.texture);
    if (FAILED(hr)) {
      memory_texture_cache_[key] = {nullptr, dummy2d_, 0, 0, 0, current_frame_};
      return dummy2d_.Get();
    }

    hr = device_->CreateShaderResourceView(res.texture.Get(), nullptr, &res.srv);
    if (FAILED(hr)) {
      memory_texture_cache_[key] = {nullptr, dummy2d_, 0, 0, 0, current_frame_};
      return dummy2d_.Get();
    }

    // Si disponemos de memoria huésped, subir datos de forma segura
    if (guest_memory && desc.guest_physical_address >= 0x00010000 && desc.guest_physical_address < (512u << 20)) {
      const uint8_t* src_bytes = guest_memory + desc.guest_physical_address;
      uint32_t row_pitch = desc.pitch_bytes;
      uint32_t num_rows = desc.height;
      if (desc.format_info.is_compressed) {
        uint32_t blocks_x = (desc.width + 3) / 4;
        uint32_t blocks_y = (desc.height + 3) / 4;
        uint32_t bytes_per_block = desc.format_info.bytes_per_block ? desc.format_info.bytes_per_block : 16;
        if (!row_pitch) row_pitch = blocks_x * bytes_per_block;
        num_rows = blocks_y;
      } else {
        uint32_t bpp = desc.format_info.bytes_per_block ? desc.format_info.bytes_per_block : 4;
        if (!row_pitch) row_pitch = desc.width * bpp;
      }
      size_t total_bytes = static_cast<size_t>(row_pitch) * num_rows;
      if (total_bytes > 0 && total_bytes <= 64 * 1024 * 1024) {
        if (desc.guest_physical_address >= 0x00010000 && (desc.guest_physical_address + total_bytes <= (512u << 20))) {
          context_->UpdateSubresource(res.texture.Get(), 0, nullptr, src_bytes, row_pitch, 0);
        }
      }
    }
  }

  memory_texture_cache_[key] = res;
  return res.srv.Get();
}

ID3D11ShaderResourceView* D3D11TextureCache::RegisterResolveTarget(uint32_t address, ID3D11Texture2D* texture,
                                                                  ID3D11ShaderResourceView* srv) {
  if (!address || !srv) return nullptr;

  TextureResource res;
  res.texture = texture;
  res.srv = srv;
  resolve_texture_cache_[address] = res;
  return srv;
}

ID3D11Texture2D* D3D11TextureCache::GetOrCreateResolveTexture(uint32_t address, uint32_t width, uint32_t height, DXGI_FORMAT format) {
  if (!address || !device_) return nullptr;
  auto it = resolve_texture_cache_.find(address);
  if (it != resolve_texture_cache_.end()) {
    if (it->second.width == width && it->second.height == height && it->second.texture) {
      it->second.last_frame_used = current_frame_;
      return it->second.texture.Get();
    }
  }

  D3D11_TEXTURE2D_DESC td = {};
  td.Width = width;
  td.Height = height;
  td.MipLevels = 1;
  td.ArraySize = 1;
  td.Format = format;
  td.SampleDesc.Count = 1;
  td.Usage = D3D11_USAGE_DEFAULT;
  td.BindFlags = D3D11_BIND_SHADER_RESOURCE;

  Microsoft::WRL::ComPtr<ID3D11Texture2D> tex;
  if (FAILED(device_->CreateTexture2D(&td, nullptr, &tex))) {
    return nullptr;
  }

  Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv;
  if (FAILED(device_->CreateShaderResourceView(tex.Get(), nullptr, &srv))) {
    return nullptr;
  }

  TextureResource res;
  res.texture = tex;
  res.srv = srv;
  res.width = width;
  res.height = height;
  res.format = static_cast<uint32_t>(format);
  res.last_frame_used = current_frame_;
  resolve_texture_cache_[address] = res;
  return tex.Get();
}

void D3D11TextureCache::Shutdown() {
  memory_texture_cache_.clear();
  resolve_texture_cache_.clear();
  dummy2d_.Reset();
  dummy3d_.Reset();
  dummycube_.Reset();
  device_ = nullptr;
  context_ = nullptr;
}

}  // namespace re_wor::nativegfx
#endif
