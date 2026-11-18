// nativegfx/backend/d3d11/d3d11_texture_cache.cpp
// Implementacion del cache de texturas en Direct3D 11.
#ifdef _WIN32
#include "nativegfx/backend/d3d11/d3d11_texture_cache.hpp"

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

uint64_t TextureHash(const TextureDesc& d) {
  uint64_t h = 1469598103934665603ull;
  auto add = [&](uint64_t v) { h = (h ^ v) * 1099511628211ull; };
  add(d.guest_physical_address);
  add(d.width);
  add(d.height);
  add(d.format_id);
  add(d.is_tiled ? 1 : 0);
  add(d.endianness);
  return h;
}

}  // namespace

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
    return it->second.srv.Get();
  }

  // Mapear formato a DXGI
  DXGI_FORMAT dxgi_fmt = DXGI_FORMAT_R8G8B8A8_UNORM;
  if (desc.format_id == 18) {
    dxgi_fmt = DXGI_FORMAT_BC1_UNORM;
  } else if (desc.format_id == 19) {
    dxgi_fmt = DXGI_FORMAT_BC2_UNORM;
  } else if (desc.format_id == 20) {
    dxgi_fmt = DXGI_FORMAT_BC3_UNORM;
  }

  TextureResource res;
  res.width = desc.width;
  res.height = desc.height;
  res.format = desc.format_id;

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
      memory_texture_cache_[key] = {nullptr, dummycube_};
      return dummycube_.Get();
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC sd = {};
    sd.Format = dxgi_fmt;
    sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE;
    sd.TextureCube.MipLevels = td.MipLevels;
    hr = device_->CreateShaderResourceView(res.texture.Get(), &sd, &res.srv);
    if (FAILED(hr)) {
      memory_texture_cache_[key] = {nullptr, dummycube_};
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
      memory_texture_cache_[key] = {nullptr, dummy2d_};
      return dummy2d_.Get();
    }

    hr = device_->CreateShaderResourceView(res.texture.Get(), nullptr, &res.srv);
    if (FAILED(hr)) {
      memory_texture_cache_[key] = {nullptr, dummy2d_};
      return dummy2d_.Get();
    }

    // Si disponemos de memoria huésped, subir datos lineales de forma segura
    if (guest_memory && desc.guest_physical_address >= 0x00010000 && desc.guest_physical_address < (512u << 20)) {
      const uint8_t* src_bytes = guest_memory + desc.guest_physical_address;
      uint32_t row_pitch = desc.pitch_bytes ? desc.pitch_bytes : (desc.width * 4);
      size_t total_bytes = static_cast<size_t>(row_pitch) * desc.height;
      if (total_bytes > 0 && total_bytes <= 64 * 1024 * 1024) {
        size_t safe_bytes = GetReadableBytes(src_bytes, total_bytes);
        if (safe_bytes >= total_bytes) {
          std::vector<uint8_t> temp(total_bytes);
          if (SafeMemoryCopy(temp.data(), src_bytes, total_bytes)) {
            context_->UpdateSubresource(res.texture.Get(), 0, nullptr, temp.data(), row_pitch, 0);
          }
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
