// tools/nativegfx_test/main.cpp
// Test de verificación automatizado para la Fase 2 y Fase 3 del Render Nativo (nativegfx).
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "nativegfx/backend/d3d11/d3d11_renderer.hpp"
#include "nativegfx/common/register_map.hpp"
#include "nativegfx/common/types.hpp"
#include "nativegfx/device/device_reader.hpp"
#include "nativegfx/device/device_tracker.hpp"
#include "nativegfx/full/full_pipeline.hpp"
#include "nativegfx/full/gpu_decoupler.hpp"
#include "nativegfx/full/surface_manager.hpp"
#include "nativegfx/full/vblank_generator.hpp"
#include "nativegfx/hybrid/hybrid_pipeline.hpp"
#include "nativegfx/hybrid/scene_classifier.hpp"
#include "nativegfx/state/texture_desc.hpp"

using namespace re_wor::nativegfx;

// Listener de prueba para verificar DeviceTracker
class MockDeviceListener : public IDeviceEventListener {
 public:
  uint32_t draw_count = 0;
  uint32_t resolve_count = 0;
  uint32_t swap_count = 0;
  DrawCommand last_draw;

  void OnDraw(const DrawCommand& cmd) override {
    ++draw_count;
    last_draw = cmd;
  }

  void OnResolve(const ResolveCommand& cmd) override {
    ++resolve_count;
  }

  void OnSwap() override {
    ++swap_count;
  }
};

void TestEndianAndMath() {
  printf("[TEST] Comprobando utilidades de endianness y tipos matematicos... ");

  // Swap32
  uint32_t v = 0x12345678;
  assert(Swap32(v) == 0x78563412);

  // Rect2D
  Rect2D r = {10, 20, 110, 120};
  assert(r.Width() == 100);
  assert(r.Height() == 100);
  assert(!r.IsEmpty());

  Rect2D empty_r = {100, 100, 50, 50};
  assert(empty_r.IsEmpty());

  printf("OK\n");
}

void TestTextureDecoding() {
  printf("[TEST] Comprobando decodificacion de texturas (TextureDesc)... ");

  // Simular una constante de fetch para una textura 2D DXT5 de 512x512
  uint32_t fc[6] = {};
  fc[0] = 1 | (2 << 10) | (2 << 13) | (16 << 22) | (1u << 31);   // 2D, ClampToEdge, pitch=512, Tiled
  fc[1] = 0x00010000;                                             // Base addr
  fc[2] = 511 | (511 << 13);                                      // 512 x 512
  fc[3] = (0 | (1 << 3) | (2 << 6) | (3 << 9)) | (20 << 19);     // RGBA swizzle, fmt=20 (DXT5)
  fc[4] = 0 | (8 << 4);                                           // 9 mip levels (0..8)
  fc[5] = 0;

  TextureDesc desc = DecodeTextureFetchConstant(fc);
  assert(desc.dimension == TextureDimension::TwoD);
  assert(desc.width == 512);
  assert(desc.height == 512);
  assert(desc.depth == 1);
  assert(desc.format_id == 20);
  assert(desc.format_info.is_compressed == true);
  assert(strcmp(desc.format_info.name, "DXT4_5") == 0);
  assert(desc.is_tiled == true);
  assert(desc.swizzle[0] == 0 && desc.swizzle[1] == 1 && desc.swizzle[2] == 2 && desc.swizzle[3] == 3);

  printf("OK (textura DXT5 512x512 verificada)\n");
}

void TestDeviceReaderState() {
  printf("[TEST] Comprobando extraccion de estado en DeviceReader... ");

  // Crear un buffer simulado de memoria huésped para el dispositivo D3D
  std::vector<uint8_t> dummy_dev(16384, 0);

  auto write_be32 = [&](uint32_t offset, uint32_t val) {
    uint32_t swapped = Swap32(val);
    std::memcpy(dummy_dev.data() + offset, &swapped, 4);
  };

  // 1. Configurar RB_SURFACE_INFO (dev + 10368 = 0x2000)
  write_be32(10368, 1280 | (1 << 16));   // pitch 1280, 2x MSAA

  // 2. Configurar RB_COLOR_INFO (dev + 10368 + 4 = 0x2001)
  write_be32(10372, 0x100 | (6 << 16));   // color_base=0x100, format=6 (8_8_8_8)

  // 3. Configurar RB_COLOR_MASK (dev + 10444 + 4*4 = 0x2104)
  write_be32(10460, 0xF);   // color write mask RGBA en RT0

  // 4. Configurar RB_DEPTHCONTROL (dev + 10548 = 0x2200)
  write_be32(10548, 0x33);

  // 5. Configurar Scissor (0x2081 en dev + 10432 + 4 = 10436, 0x2082 en dev + 10440)
  write_be32(10436, 0 | (0 << 16) | (1u << 31));   // (0, 0), window_offset_disable
  write_be32(10440, 1280 | (720 << 16));            // (1280, 720)

  // 6. Configurar Constantes Float de VS (dev + 1920 = 0x4000)
  float test_floats[4] = {1.5f, 2.5f, 3.5f, 4.5f};
  for (int i = 0; i < 4; ++i) {
    uint32_t raw;
    std::memcpy(&raw, &test_floats[i], 4);
    write_be32(1920 + 4 * i, raw);
  }

  // Ejecutar el DeviceReader
  DeviceReader reader;
  reader.UpdateFromDevice(dummy_dev.data(), 0, true);

  PipelineState state;
  bool ok = reader.ExtractPipelineState(state, 1.0f);
  assert(ok);

  assert(state.render_targets.surface_pitch == 1280);
  assert(state.render_targets.color_targets[0].enabled == true);
  assert(state.render_targets.color_targets[0].base_address == 0x100);
  assert(state.render_targets.color_targets[0].format == 6);
  assert(state.render_targets.color_targets[0].write_mask == 0xF);

  assert(state.depth_stencil.depth_enable == true);
  assert(state.depth_stencil.depth_write_enable == true);
  assert(state.depth_stencil.depth_func == ComparisonFunc::LessEqual);

  assert(state.rasterizer.scissor_rect.left == 0);
  assert(state.rasterizer.scissor_rect.top == 0);
  assert(state.rasterizer.scissor_rect.right == 1280);
  assert(state.rasterizer.scissor_rect.bottom == 720);

  assert(state.constants.vs_float[0][0] == 1.5f);
  assert(state.constants.vs_float[0][1] == 2.5f);
  assert(state.constants.vs_float[0][2] == 3.5f);
  assert(state.constants.vs_float[0][3] == 4.5f);

  printf("OK\n");
}

void TestDeviceTrackerPipeline() {
  printf("[TEST] Comprobando pipeline de eventos de DeviceTracker... ");

  MockDeviceListener listener;
  DeviceTracker& tracker = DeviceTracker::Instance();
  tracker.AddListener(&listener);
  tracker.SetEnabled(true);

  std::vector<uint8_t> dummy_dev(16384, 0);

  uint32_t args[8] = {
      0,           // r3 = dev
      4,           // r4 = prim_type (TriangleList = 4)
      300,         // r5 = count
      0x20000000,  // r6 = index buffer address
  };

  tracker.OnD3DCall(dummy_dev.data(), 0x822D18A0, 0, args);
  assert(listener.draw_count == 1);
  assert(listener.last_draw.topology == PrimitiveTopology::TriangleList);
  assert(listener.last_draw.primitive_count == 300);

  tracker.OnD3DCall(dummy_dev.data(), 0x822E4538, 0, args);
  assert(listener.resolve_count == 1);

  tracker.OnSwap();
  assert(listener.swap_count == 1);

  tracker.RemoveListener(&listener);
  printf("OK\n");
}

void TestSceneClassifier() {
  printf("[TEST] Comprobando clasificador de fases del render hibrido... ");

  SceneClassifier classifier;
  classifier.OnBeginFrame();
  assert(classifier.IsInStage3D());

  DrawCommand cmd1;
  cmd1.source_function = 0x822D18A0;
  RenderPhase p1 = classifier.ClassifyDraw(cmd1);
  assert(p1 == RenderPhase::Stage3D);
  assert(classifier.IsInStage3D());
  assert(classifier.GetStage3DDrawCount() == 1);

  // Simular el Resolve a pantalla completa del escenario 3D
  ResolveCommand res;
  res.width = 1280;
  res.height = 720;
  classifier.OnResolve(res);
  assert(!classifier.IsInStage3D());
  assert(classifier.GetCurrentPhase() == RenderPhase::SceneResolve);

  // Draws posteriores (HUD / Autopista de notas)
  DrawCommand cmd2;
  RenderPhase p2 = classifier.ClassifyDraw(cmd2);
  assert(p2 == RenderPhase::HighwayHUD);
  assert(classifier.GetHighwayHUDDrawCount() == 1);

  // Al comenzar nuevo frame, debe resetearse a Stage3D
  classifier.OnBeginFrame();
  assert(classifier.IsInStage3D());
  assert(classifier.GetStage3DDrawCount() == 0);

  printf("OK (Stage3D -> Resolve -> HighwayHUD verificado)\n");
}

void TestD3D11RendererOffscreen() {
  printf("[TEST] Comprobando inicializacion y ciclo de D3D11Renderer (Offscreen)... ");

#ifdef _WIN32
  D3D11Renderer renderer;
  bool init_ok = renderer.Initialize(nullptr, 1920, 1080);
  assert(init_ok);
  assert(renderer.GetNativeWidth() == 1920);
  assert(renderer.GetNativeHeight() == 1080);
  assert(renderer.GetResolutionScale() == 1.5f);   // 1080 / 720 = 1.5x

  renderer.BeginFrame();

  DrawCommand cmd;
  cmd.topology = PrimitiveTopology::TriangleList;
  cmd.vertex_count = 3;
  std::vector<uint8_t> dummy_mem(1024, 0);

  renderer.RenderDraw(cmd, dummy_mem.data());
  renderer.EndFrame();
  renderer.Shutdown();
  printf("OK (D3D11 Hardware Device 1080p verificado)\n");
#else
  printf("SKIPPED (no es Windows)\n");
#endif
}

void TestBackendFactoryAndD3D12() {
  printf("[TEST] Comprobando BackendFactory, D3D12 y Vulkan... ");

  assert(BackendFactory::ParseBackendType("d3d11") == GraphicsBackendType::D3D11);
  assert(BackendFactory::ParseBackendType("d3d12") == GraphicsBackendType::D3D12);
  assert(BackendFactory::ParseBackendType("vulkan") == GraphicsBackendType::Vulkan);

#ifdef _WIN32
  // Test D3D12 Renderer Offscreen
  auto d3d12_renderer = BackendFactory::CreateRenderer(GraphicsBackendType::D3D12);
  assert(d3d12_renderer != nullptr);

  bool d3d12_ok = d3d12_renderer->Initialize(nullptr, 1920, 1080);
  assert(d3d12_ok);
  assert(d3d12_renderer->GetNativeWidth() == 1920);
  assert(d3d12_renderer->GetNativeHeight() == 1080);

  d3d12_renderer->BeginFrame();

  DrawCommand cmd;
  cmd.topology = PrimitiveTopology::TriangleList;
  cmd.vertex_count = 3;
  std::vector<uint8_t> dummy_mem(1024, 0);

  d3d12_renderer->RenderDraw(cmd, dummy_mem.data());
  d3d12_renderer->EndFrame();
  d3d12_renderer->Shutdown();
#endif

  // Test Vulkan Renderer Creation via Factory
  auto vk_renderer = BackendFactory::CreateRenderer(GraphicsBackendType::Vulkan);
  assert(vk_renderer != nullptr);

  printf("OK (Factory, D3D12 1080p y Vulkan validados)\n");
}

void TestPhase4SurfaceManager() {
  printf("[TEST] Comprobando SurfaceManager sin EDRAM (Fase 4)... ");

  SurfaceManager& sm = SurfaceManager::Instance();
  sm.Initialize(1920, 1080);

  // 1. Crear / obtener render target virtual
  NativeSurface* rt = sm.GetOrCreateRenderTarget(0x00000000, 1920, 1080, SurfaceFormat::k_8_8_8_8, false);
  assert(rt != nullptr);
  assert(rt->width == 1920);
  assert(rt->height == 1080);
  assert(rt->is_swap_chain_target == true);

  // 2. Procesar resolve de textura intermedia (ej. shadow map o textura de reflejo)
  ResolveCommand res_tex;
  res_tex.source_surface_base = 0x00100000;
  res_tex.dest_texture_address = 0xA0001000;
  res_tex.width = 512;
  res_tex.height = 512;
  res_tex.format = static_cast<uint32_t>(SurfaceFormat::k_8_8_8_8);
  res_tex.is_depth = false;

  bool ok_res = sm.ProcessResolve(res_tex, 1);
  assert(ok_res);
  assert(sm.HasResolvedTexture(0xA0001000));
  assert(sm.GetEdramBytesSaved() > 0);

  const NativeSurface* found = sm.FindResolvedTexture(0xA0001000);
  assert(found != nullptr);
  assert(found->width == 512);
  assert(found->height == 512);

  // 3. Procesar resolve a pantalla completa (salida a presentacion)
  ResolveCommand res_full;
  res_full.source_surface_base = 0x00000000;
  res_full.dest_texture_address = 0x82000000;
  res_full.width = 1920;
  res_full.height = 1080;
  assert(sm.ProcessResolve(res_full, 1));

  sm.Shutdown();
  printf("OK (Zero-EDRAM, Texture Aliasing y Resolve Bypassed verificados)\n");
}

void TestPhase4VBlankAndDecoupler() {
  printf("[TEST] Comprobando VBlankGenerator y GpuDecoupler (Fase 4)... ");

  // 1. GpuDecoupler
  GpuDecoupler& dec = GpuDecoupler::Instance();
  dec.SetMode(PipelineMode::FullNative);
  assert(dec.IsFullNative());
  assert(dec.ShouldBypassPm4Processing());

  dec.RecordBypassedPacket(0x2D, 16);
  assert(dec.GetTotalPacketsBypassed() == 1);
  assert(dec.GetTotalDwordsBypassed() == 16);

  dec.SetMode(PipelineMode::Hybrid);
  assert(dec.IsHybrid());
  dec.SetMode(PipelineMode::FullNative);

  // 2. VBlankGenerator
  VBlankGenerator& vb = VBlankGenerator::Instance();
  vb.Initialize(60);
  assert(vb.GetTargetRefreshRate() == 60);

  uint64_t cb_ticks = 0;
  vb.SetVBlankCallback([&](uint64_t count) {
    cb_ticks = count;
  });

  vb.OnSwap();
  assert(vb.GetTotalVBlanks() == 1);
  assert(cb_ticks == 1);

  vb.Shutdown();
  printf("OK (60Hz VBlank Autonomo y Decoupler validados)\n");
}

void TestPhase4FullPipeline() {
  printf("[TEST] Comprobando FullPipeline (100%% draws nativos + Swap)... ");

#ifdef _WIN32
  FullPipeline& fp = FullPipeline::Instance();
  bool init_ok = fp.Initialize(nullptr, GraphicsBackendType::D3D11, 1920, 1080);
  assert(init_ok);
  assert(fp.IsEnabled());
  assert(DeviceTracker::Instance().GetResolutionScale() == 1.5f);

  if (auto* ren = fp.GetRenderer()) {
    ren->SetVSync(true);
    assert(ren->GetVSync() == true);
    ren->SetVSync(false);
    assert(ren->GetVSync() == false);

    ren->SetAnisotropy(16);
    assert(ren->GetAnisotropy() == 16);
    ren->SetAnisotropy(1);
    assert(ren->GetAnisotropy() == 1);
  }

  std::vector<uint8_t> dummy_mem(4096, 0);
  fp.SetGuestMemoryBase(dummy_mem.data());

  // Draw 1: Escenario 3D
  DrawCommand cmd3d;
  cmd3d.topology = PrimitiveTopology::TriangleList;
  cmd3d.vertex_count = 3;
  fp.OnDraw(cmd3d);

  // Draw 2: Autopista de notas
  DrawCommand cmd_highway;
  cmd_highway.topology = PrimitiveTopology::TriangleList;
  cmd_highway.vertex_count = 6;
  fp.OnDraw(cmd_highway);

  // Draw 3: HUD 2D
  DrawCommand cmd_hud;
  cmd_hud.topology = PrimitiveTopology::TriangleStrip;
  cmd_hud.vertex_count = 4;
  fp.OnDraw(cmd_hud);

  assert(fp.GetTotalNativeDraws() == 3);

  // Resolve final a pantalla completa
  ResolveCommand res_final;
  res_final.width = 1920;
  res_final.height = 1080;
  fp.OnResolve(res_final);
  assert(fp.GetTotalNativeResolves() == 1);

  // Swap
  fp.OnSwap();
  assert(fp.GetTotalFramesRendered() == 1);

  fp.Shutdown();
  assert(!fp.IsEnabled());
  printf("OK (FullPipeline D3D11 1080p, 100%% draws y ciclo Swap verificados)\n");
#else
  printf("SKIPPED (no es Windows)\n");
#endif
}

int main() {
  printf("========================================================\n");
  printf(" Native GFX Phase 2, 3 & 4 - Verification Test Suite\n");
  printf("========================================================\n");

  TestEndianAndMath();
  TestTextureDecoding();
  TestDeviceReaderState();
  TestDeviceTrackerPipeline();
  TestSceneClassifier();
  TestD3D11RendererOffscreen();
  TestBackendFactoryAndD3D12();

  // Tests Fase 4
  TestPhase4SurfaceManager();
  TestPhase4VBlankAndDecoupler();
  TestPhase4FullPipeline();

  printf("========================================================\n");
  printf(" [EXITO] Todos los tests de la Fase 2, 3 y 4 pasaron al 100%%\n");
  printf("========================================================\n");
  return 0;
}
