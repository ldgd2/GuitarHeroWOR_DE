// Ganchos del juego en el procesador de comandos de GPU (re-wor "sandbox de render").
// El SDK avisa de cada draw (con sus shaders y render targets ya preparados), resolve (copia de EDRAM a memoria) y
// swap. Si el gancho de draw devuelve kDrawHookPassAfter, justo despues de ese draw (antes del siguiente draw,
// resolve o swap) el SDK ejecuta la PASADA SANDBOX sobre su render target de color 0:
//   A) pantalla completa: PSMain(VSMain) lee una copia de la imagen (t0) y escribe el resultado.
//   B) opcional, instanciada con mezcla alfa: VSPart/PSPart, `particle_instances` instancias de 6 vertices.
// El HLSL lo da el juego (hlsl + hlsl_version; se recompila al cambiar la version) y los parametros van en
// `constants` (b0, hasta 256 floats). Asi el juego puede meter cualquier efecto de imagen sin tocar el SDK.
#pragma once
#include <cstdint>

struct ID3D12Resource;

namespace rex::graphics {

struct DrawHookInfo {
  uint64_t vs_hash = 0, ps_hash = 0;   // ucode_data_hash de los shaders del juego (0 = sin pixel shader)
  uint32_t rt_key[4] = {};             // RenderTargetKey de los 4 color RT (0 = no usado)
  uint32_t depth_key = 0;
  ID3D12Resource* rt_resource[4] = {}; // texturas host de esos RT (camino host render targets)
  uint32_t index_count = 0, prim_type = 0;
  uint32_t used_textures = 0;          // mascara de fetch constants usadas por el pixel shader
  uint32_t window_scissor_br = 0;      // PA_SC_WINDOW_SCISSOR_BR (x | y << 16)
};

struct ResolveHookInfo {   // copia de EDRAM a memoria (el Xenos "resuelve" la imagen dibujada)
  uint32_t dest_base = 0, dest_pitch = 0, dest_height = 0;   // destino: direccion, ancho y alto en pixeles
  uint32_t src_select = 0;   // 0..3 color RT, 4 profundidad
  uint32_t dest_format = 0;  // xenos::ColorFormat del destino
  uint32_t draws_before = 0; // draws desde el resolve anterior
};

enum : uint32_t {
  kDrawHookSkip = 1,       // no emitir el draw del juego
  kDrawHookPassAfter = 2,  // ejecutar la pasada sandbox despues de este draw
};

using DrawHookFn = uint32_t (*)(const DrawHookInfo& info);
// Si devuelve kDrawHookPassAfter, la pasada sandbox se ejecuta sobre el RT de origen JUSTO ANTES de esta copia
// (lo que se copia ya lleva el efecto; lo que el juego dibuje despues, como el HUD, no).
using ResolveHookFn = uint32_t (*)(const ResolveHookInfo& info);
using SwapHookFn = void (*)();
using LogHookFn = void (*)(const char* text);

struct SandboxPassInfo {   // lo rellena el SDK en cada pasada (para el juego)
  uint32_t width = 0, height = 0;      // zona dibujada en pixeles del host
  uint32_t dxgi_format = 0;
  uint32_t scale_x = 1, scale_y = 1;   // escala de resolucion del host
  uint32_t tex_width = 0, tex_height = 0;   // tamano real de la textura del RT (>= width/height: pitch de EDRAM)
};

struct DrawHooks {
  DrawHookFn draw = nullptr;
  ResolveHookFn resolve = nullptr;
  SwapHookFn swap = nullptr;
  LogHookFn log = nullptr;
  // pasada sandbox (todo lo pone el juego; el SDK solo lee)
  const char* hlsl = nullptr;          // fuente con VSMain/PSMain y, si hay particulas, VSPart/PSPart
  uint32_t hlsl_version = 0;           // cambiarla fuerza recompilar
  const float* constants = nullptr;    // b0
  uint32_t constant_count = 0;         // <= 256
  uint32_t particle_instances = 0;
  void (*before_pass)(const SandboxPassInfo& info) = nullptr;   // ultima oportunidad de ajustar constants
};
DrawHooks& GetDrawHooks();

}  // namespace rex::graphics
