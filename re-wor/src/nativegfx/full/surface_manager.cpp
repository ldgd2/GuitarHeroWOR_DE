// nativegfx/full/surface_manager.cpp
// Implementacion del administrador de superficies sin EDRAM (Fase 4: Completo).
#include "nativegfx/full/surface_manager.hpp"

#include <algorithm>

namespace re_wor::nativegfx {

SurfaceManager& SurfaceManager::Instance() {
  static SurfaceManager s_instance;
  return s_instance;
}

void SurfaceManager::Initialize(uint32_t native_width, uint32_t native_height) {
  native_width_  = native_width;
  native_height_ = native_height;
  current_frame_ = 0;
  total_resolves_bypassed_ = 0;
  edram_bytes_saved_ = 0;
  surfaces_.clear();
  guest_addr_to_surface_id_.clear();
}

void SurfaceManager::Shutdown() {
  surfaces_.clear();
  guest_addr_to_surface_id_.clear();
}

void SurfaceManager::BeginFrame(uint64_t frame_index) {
  current_frame_ = frame_index;

  // Limpiar referencias caducadas que no se hayan usado en más de 120 frames
  if (surfaces_.size() > 64) {
    for (auto it = surfaces_.begin(); it != surfaces_.end();) {
      if (!it->second.is_swap_chain_target && current_frame_ > it->second.last_frame_used + 120) {
        guest_addr_to_surface_id_.erase(it->second.guest_address);
        it = surfaces_.erase(it);
      } else {
        ++it;
      }
    }
  }
}

void SurfaceManager::EndFrame() {
  // Sincronizacion de fin de cuadro de superficies
}

NativeSurface* SurfaceManager::GetOrCreateRenderTarget(uint32_t surface_base, uint32_t width, uint32_t height,
                                                       SurfaceFormat format, bool is_depth) {
  auto it = surfaces_.find(surface_base);
  if (it != surfaces_.end()) {
    it->second.last_frame_used = current_frame_;
    return &it->second;
  }

  // Registrar nueva superficie nativa virtual
  NativeSurface surf;
  surf.id = next_surface_id_++;
  surf.guest_address = surface_base;
  surf.width = (width > 0) ? width : native_width_;
  surf.height = (height > 0) ? height : native_height_;
  surf.format = format;
  surf.is_depth = is_depth;
  surf.last_frame_used = current_frame_;
  surf.is_swap_chain_target = (surf.width == native_width_ && surf.height == native_height_ && !is_depth);

  surfaces_[surface_base] = surf;
  return &surfaces_[surface_base];
}

bool SurfaceManager::ProcessResolve(const ResolveCommand& cmd, uint64_t current_frame) {
  ++total_resolves_bypassed_;

  // Ahorro estimado de ancho de banda y memoria al no pasar por des-tiling en CPU/EDRAM
  // En Xbox 360, un resolve 720p copia ~3.6 MB de EDRAM tileada a memoria lineal.
  uint64_t resolve_bytes = static_cast<uint64_t>(cmd.width) * cmd.height * 4;
  edram_bytes_saved_ += resolve_bytes;

  // Identificar si este resolve es la salida a pantalla (framebuffer final)
  bool is_fullscreen = (cmd.width >= 1280 && cmd.height >= 720);

  // Registrar la superficie nativa asociada
  NativeSurface surf;
  surf.id = next_surface_id_++;
  surf.guest_address = cmd.dest_texture_address;
  surf.width = (cmd.width > 0) ? cmd.width : native_width_;
  surf.height = (cmd.height > 0) ? cmd.height : native_height_;
  surf.format = static_cast<SurfaceFormat>(cmd.format);
  surf.is_depth = cmd.is_depth;
  surf.is_swap_chain_target = is_fullscreen;
  surf.last_frame_used = current_frame;

  surfaces_[cmd.source_surface_base] = surf;

  if (cmd.dest_texture_address != 0) {
    guest_addr_to_surface_id_[cmd.dest_texture_address] = surf.id;
  }

  return true;
}

bool SurfaceManager::HasResolvedTexture(uint32_t guest_address) const {
  return guest_addr_to_surface_id_.find(guest_address) != guest_addr_to_surface_id_.end();
}

const NativeSurface* SurfaceManager::FindResolvedTexture(uint32_t guest_address) const {
  auto it = guest_addr_to_surface_id_.find(guest_address);
  if (it == guest_addr_to_surface_id_.end()) return nullptr;

  for (const auto& [base, surf] : surfaces_) {
    if (surf.id == it->second) {
      return &surf;
    }
  }
  return nullptr;
}

}  // namespace re_wor::nativegfx
