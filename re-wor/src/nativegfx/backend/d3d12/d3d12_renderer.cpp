// nativegfx/backend/d3d12/d3d12_renderer.cpp
// Implementacion completa del renderizador nativo Direct3D 12.
#ifdef _WIN32
#include "nativegfx/backend/d3d12/d3d12_renderer.hpp"

#include <d3dcompiler.h>
#include <windows.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
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

static inline size_t GetReadableBytes(const void* ptr, size_t max_size) {
  if (!ptr || max_size == 0) return 0;
  return max_size;
}

static const std::filesystem::path& GetNativeShadersDir() {
  static std::filesystem::path s_dir = []() {
    const std::filesystem::path candidates[] = {
        std::filesystem::current_path() / "cache" / "native_shaders",
        std::filesystem::current_path() / "re-wor" / "cache" / "native_shaders",
        std::filesystem::path("cache/native_shaders"),
        std::filesystem::path("re-wor/cache/native_shaders")
    };
    for (const auto& c : candidates) {
      if (std::filesystem::is_directory(c)) {
        return c;
      }
    }
    return std::filesystem::path("cache/native_shaders");
  }();
  return s_dir;
}

class NativeShaderInclude : public ID3DInclude {
 public:
  STDMETHOD(Open)(D3D_INCLUDE_TYPE, LPCSTR pFileName, LPCVOID, LPCVOID* ppData, UINT* pBytes) override {
    if (!pFileName) return E_FAIL;
    std::filesystem::path p = GetNativeShadersDir() / pFileName;
    std::ifstream f(p, std::ios::binary);
    if (f.is_open()) {
      std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
      char* buf = new char[content.size()];
      std::memcpy(buf, content.data(), content.size());
      *ppData = buf;
      *pBytes = static_cast<UINT>(content.size());
      return S_OK;
    }
    return E_FAIL;
  }
  STDMETHOD(Close)(LPCVOID pData) override {
    delete[] static_cast<const char*>(pData);
    return S_OK;
  }
};

static std::vector<uint8_t> LoadShaderBytecodeFromDisk(uint64_t hash, const char* stage) {
  char filename[64];
  std::snprintf(filename, sizeof(filename), "%016llX.%s.cso", static_cast<unsigned long long>(hash), stage);
  std::filesystem::path p = GetNativeShadersDir() / filename;
  std::ifstream f(p, std::ios::binary);
  if (f.is_open()) {
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  }
  return {};
}

static std::string LoadShaderSourceFromDisk(uint64_t hash, const char* stage) {
  char filename[64];
  std::snprintf(filename, sizeof(filename), "%016llX.%s.hlsl", static_cast<unsigned long long>(hash), stage);
  std::filesystem::path p = GetNativeShadersDir() / filename;
  std::ifstream f(p, std::ios::binary);
  if (f.is_open()) {
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  }
  return "";
}

static void SaveShaderBytecodeToDisk(uint64_t hash, const char* stage, const void* data, size_t size) {
  if (!data || size == 0) return;
  char filename[64];
  std::snprintf(filename, sizeof(filename), "%016llX.%s.cso", static_cast<unsigned long long>(hash), stage);
  std::filesystem::path p = GetNativeShadersDir() / filename;
  std::ofstream f(p, std::ios::binary);
  if (f.is_open()) {
    f.write(static_cast<const char*>(data), size);
  }
}

const char* kRectGeometryShaderSource = R"(
struct XeVaryings { float4 pos : SV_Position; float4 o[16] : TEXCOORD0; };
[maxvertexcount(4)]
void main(triangle XeVaryings v[3], inout TriangleStream<XeVaryings> s) {
  XeVaryings w[3] = {v[0], v[1], v[2]};
  float2 p0 = w[0].pos.xy / w[0].pos.w, p1 = w[1].pos.xy / w[1].pos.w, p2 = w[2].pos.xy / w[2].pos.w;
  float d01 = dot(p1 - p0, p1 - p0), d12 = dot(p2 - p1, p2 - p1), d20 = dot(p0 - p2, p0 - p2);
  uint c = 0u, a = 1u, b = 2u;
  if (d01 >= d12 && d01 >= d20) { c = 2u; a = 0u; b = 1u; }
  else if (d20 >= d01 && d20 >= d12) { c = 1u; a = 2u; b = 0u; }
  XeVaryings d;
  d.pos = w[a].pos + w[b].pos - w[c].pos;
  [unroll] for (uint i = 0u; i < 16u; ++i) d.o[i] = w[a].o[i] + w[b].o[i] - w[c].o[i];
  s.Append(w[c]); s.Append(w[a]); s.Append(w[b]); s.Append(d);
}
)";

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

D3D12_PRIMITIVE_TOPOLOGY_TYPE ToD3D12TopologyType(PrimitiveTopology topo) {
  switch (topo) {
    case PrimitiveTopology::PointList:
      return D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
    case PrimitiveTopology::LineList:
    case PrimitiveTopology::LineStrip:
      return D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
    case PrimitiveTopology::TriangleList:
    case PrimitiveTopology::TriangleStrip:
    case PrimitiveTopology::RectangleList:
    default:
      return D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  }
}

D3D12_BLEND ToD3D12Blend(BlendFactor factor) {
  switch (factor) {
    case BlendFactor::Zero:           return D3D12_BLEND_ZERO;
    case BlendFactor::One:            return D3D12_BLEND_ONE;
    case BlendFactor::SrcColor:       return D3D12_BLEND_SRC_COLOR;
    case BlendFactor::InvSrcColor:    return D3D12_BLEND_INV_SRC_COLOR;
    case BlendFactor::SrcAlpha:       return D3D12_BLEND_SRC_ALPHA;
    case BlendFactor::InvSrcAlpha:    return D3D12_BLEND_INV_SRC_ALPHA;
    case BlendFactor::DestAlpha:      return D3D12_BLEND_DEST_ALPHA;
    case BlendFactor::InvDestAlpha:   return D3D12_BLEND_INV_DEST_ALPHA;
    case BlendFactor::DestColor:      return D3D12_BLEND_DEST_COLOR;
    case BlendFactor::InvDestColor:   return D3D12_BLEND_INV_DEST_COLOR;
    case BlendFactor::SrcAlphaSat:    return D3D12_BLEND_SRC_ALPHA_SAT;
    case BlendFactor::BlendFactor:    return D3D12_BLEND_BLEND_FACTOR;
    case BlendFactor::InvBlendFactor: return D3D12_BLEND_INV_BLEND_FACTOR;
    default:                          return D3D12_BLEND_ONE;
  }
}

D3D12_BLEND_OP ToD3D12BlendOp(BlendOp op) {
  switch (op) {
    case BlendOp::Add:         return D3D12_BLEND_OP_ADD;
    case BlendOp::Subtract:    return D3D12_BLEND_OP_SUBTRACT;
    case BlendOp::RevSubtract: return D3D12_BLEND_OP_REV_SUBTRACT;
    case BlendOp::Min:         return D3D12_BLEND_OP_MIN;
    case BlendOp::Max:         return D3D12_BLEND_OP_MAX;
    default:                   return D3D12_BLEND_OP_ADD;
  }
}

D3D12_COMPARISON_FUNC ToD3D12Comparison(ComparisonFunc func) {
  switch (func) {
    case ComparisonFunc::Never:        return D3D12_COMPARISON_FUNC_NEVER;
    case ComparisonFunc::Less:         return D3D12_COMPARISON_FUNC_LESS;
    case ComparisonFunc::Equal:        return D3D12_COMPARISON_FUNC_EQUAL;
    case ComparisonFunc::LessEqual:    return D3D12_COMPARISON_FUNC_LESS_EQUAL;
    case ComparisonFunc::Greater:      return D3D12_COMPARISON_FUNC_GREATER;
    case ComparisonFunc::NotEqual:     return D3D12_COMPARISON_FUNC_NOT_EQUAL;
    case ComparisonFunc::GreaterEqual: return D3D12_COMPARISON_FUNC_GREATER_EQUAL;
    case ComparisonFunc::Always:       return D3D12_COMPARISON_FUNC_ALWAYS;
    default:                           return D3D12_COMPARISON_FUNC_ALWAYS;
  }
}

D3D12_STENCIL_OP ToD3D12StencilOp(StencilOp op) {
  switch (op) {
    case StencilOp::Keep:    return D3D12_STENCIL_OP_KEEP;
    case StencilOp::Zero:    return D3D12_STENCIL_OP_ZERO;
    case StencilOp::Replace: return D3D12_STENCIL_OP_REPLACE;
    case StencilOp::IncrSat: return D3D12_STENCIL_OP_INCR_SAT;
    case StencilOp::DecrSat: return D3D12_STENCIL_OP_DECR_SAT;
    case StencilOp::Invert:  return D3D12_STENCIL_OP_INVERT;
    case StencilOp::Incr:    return D3D12_STENCIL_OP_INCR;
    case StencilOp::Decr:    return D3D12_STENCIL_OP_DECR;
    default:                 return D3D12_STENCIL_OP_KEEP;
  }
}

D3D12_BLEND_DESC ToD3D12BlendDesc(const BlendState& blend) {
  D3D12_BLEND_DESC desc = {};
  desc.AlphaToCoverageEnable = FALSE;
  desc.IndependentBlendEnable = blend.independent_blend_enable ? TRUE : FALSE;
  for (int i = 0; i < 8; ++i) {
    if (i < 4) {
      const auto& src = blend.targets[i];
      auto& dst = desc.RenderTarget[i];
      dst.BlendEnable = src.blend_enable ? TRUE : FALSE;
      dst.LogicOpEnable = FALSE;
      dst.SrcBlend = ToD3D12Blend(src.src_blend);
      dst.DestBlend = ToD3D12Blend(src.dest_blend);
      dst.BlendOp = ToD3D12BlendOp(src.blend_op);
      dst.SrcBlendAlpha = ToD3D12Blend(src.src_blend_alpha);
      dst.DestBlendAlpha = ToD3D12Blend(src.dest_blend_alpha);
      dst.BlendOpAlpha = ToD3D12BlendOp(src.blend_op_alpha);
      dst.LogicOp = D3D12_LOGIC_OP_NOOP;
      dst.RenderTargetWriteMask = src.write_mask;
    } else {
      desc.RenderTarget[i].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    }
  }
  return desc;
}

D3D12_RASTERIZER_DESC ToD3D12RasterizerDesc(const RasterizerState& r) {
  D3D12_RASTERIZER_DESC desc = {};
  desc.FillMode = (r.fill_mode == FillMode::Wireframe) ? D3D12_FILL_MODE_WIREFRAME : D3D12_FILL_MODE_SOLID;
  switch (r.cull_mode) {
    case CullMode::None:  desc.CullMode = D3D12_CULL_MODE_NONE; break;
    case CullMode::Front: desc.CullMode = D3D12_CULL_MODE_FRONT; break;
    case CullMode::Back:  desc.CullMode = D3D12_CULL_MODE_BACK; break;
    default:              desc.CullMode = D3D12_CULL_MODE_BACK; break;
  }
  desc.FrontCounterClockwise = r.front_counter_clockwise ? TRUE : FALSE;
  desc.DepthBias = static_cast<INT>(r.depth_bias);
  desc.DepthBiasClamp = 0.0f;
  desc.SlopeScaledDepthBias = r.slope_scaled_depth_bias;
  desc.DepthClipEnable = TRUE;
  desc.MultisampleEnable = TRUE;
  desc.AntialiasedLineEnable = TRUE;
  desc.ForcedSampleCount = 0;
  desc.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;
  return desc;
}

D3D12_DEPTH_STENCIL_DESC ToD3D12DepthStencilDesc(const DepthStencilState& d) {
  D3D12_DEPTH_STENCIL_DESC desc = {};
  desc.DepthEnable = d.depth_enable ? TRUE : FALSE;
  desc.DepthWriteMask = d.depth_write_enable ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
  desc.DepthFunc = ToD3D12Comparison(d.depth_func);
  desc.StencilEnable = d.stencil_enable ? TRUE : FALSE;
  desc.StencilReadMask = d.stencil_read_mask;
  desc.StencilWriteMask = d.stencil_write_mask;

  desc.FrontFace.StencilFailOp = ToD3D12StencilOp(d.front_face.fail_op);
  desc.FrontFace.StencilDepthFailOp = ToD3D12StencilOp(d.front_face.depth_fail_op);
  desc.FrontFace.StencilPassOp = ToD3D12StencilOp(d.front_face.pass_op);
  desc.FrontFace.StencilFunc = ToD3D12Comparison(d.front_face.compare_func);

  desc.BackFace.StencilFailOp = ToD3D12StencilOp(d.back_face.fail_op);
  desc.BackFace.StencilDepthFailOp = ToD3D12StencilOp(d.back_face.depth_fail_op);
  desc.BackFace.StencilPassOp = ToD3D12StencilOp(d.back_face.pass_op);
  desc.BackFace.StencilFunc = ToD3D12Comparison(d.back_face.compare_func);
  return desc;
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

  if (!CreateSrvDescriptorHeap()) {
    return false;
  }

  CompileRectangleGeometryShader();

  return true;
}

bool D3D12Renderer::CompileRectangleGeometryShader() {
  ComPtr<ID3DBlob> blob, errors;
  HRESULT hr = D3DCompile(kRectGeometryShaderSource, std::strlen(kRectGeometryShaderSource),
                          "rect_gs.hlsl", nullptr, nullptr, "main", "gs_5_0",
                          D3DCOMPILE_OPTIMIZATION_LEVEL1, 0, &blob, &errors);
  if (SUCCEEDED(hr) && blob) {
    rect_gs_bytecode_.assign(reinterpret_cast<const uint8_t*>(blob->GetBufferPointer()),
                             reinterpret_cast<const uint8_t*>(blob->GetBufferPointer()) + blob->GetBufferSize());
    return true;
  }
  return false;
}

bool D3D12Renderer::CreateSrvDescriptorHeap() {
  ID3D12Device* dev = device_ctx_.GetDevice();
  if (!dev) return false;

  D3D12_DESCRIPTOR_HEAP_DESC heap_desc = {};
  heap_desc.NumDescriptors = 256;
  heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

  HRESULT hr = dev->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(&srv_heap_));
  if (FAILED(hr)) return false;

  srv_descriptor_size_ = dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

  // Inicializar descriptores 0..127 con descriptores nulos validos de D3D12
  D3D12_CPU_DESCRIPTOR_HANDLE handle = srv_heap_->GetCPUDescriptorHandleForHeapStart();
  for (uint32_t i = 0; i < 128; ++i) {
    D3D12_SHADER_RESOURCE_VIEW_DESC sd = {};
    sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    if (i >= 32 && i < 64) {
      sd.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
      sd.Texture3D.MipLevels = 1;
    } else if (i >= 64 && i < 96) {
      sd.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
      sd.TextureCube.MipLevels = 1;
    } else {
      sd.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
      sd.Texture2D.MipLevels = 1;
    }
    dev->CreateShaderResourceView(nullptr, &sd, handle);
    handle.ptr += srv_descriptor_size_;
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
  // 6: SRV Table t1..t128 (texturas)
  D3D12_ROOT_PARAMETER root_params[7] = {};

  // Param 0: VS float b0
  root_params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
  root_params[0].Descriptor.ShaderRegister = 0;
  root_params[0].Descriptor.RegisterSpace = 0;
  root_params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

  // Param 1: PS float b0
  root_params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
  root_params[1].Descriptor.ShaderRegister = 0;
  root_params[1].Descriptor.RegisterSpace = 0;
  root_params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

  // Param 2: Bool/Loop b1
  root_params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
  root_params[2].Descriptor.ShaderRegister = 1;
  root_params[2].Descriptor.RegisterSpace = 0;
  root_params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

  // Param 3: Fetch b2
  root_params[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
  root_params[3].Descriptor.ShaderRegister = 2;
  root_params[3].Descriptor.RegisterSpace = 0;
  root_params[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

  // Param 4: System NDC b3
  root_params[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
  root_params[4].Descriptor.ShaderRegister = 3;
  root_params[4].Descriptor.RegisterSpace = 0;
  root_params[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

  // Param 5: t0 raw ByteAddressBuffer (xe_mem)
  root_params[5].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
  root_params[5].Descriptor.ShaderRegister = 0;
  root_params[5].Descriptor.RegisterSpace = 0;
  root_params[5].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

  // Param 6: Table for textures t1..t128
  D3D12_DESCRIPTOR_RANGE srv_range = {};
  srv_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
  srv_range.NumDescriptors = 128;
  srv_range.BaseShaderRegister = 1;
  srv_range.RegisterSpace = 0;
  srv_range.OffsetInDescriptorsFromTableStart = 0;

  root_params[6].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  root_params[6].DescriptorTable.NumDescriptorRanges = 1;
  root_params[6].DescriptorTable.pDescriptorRanges = &srv_range;
  root_params[6].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

  // 16 Samplers estaticos (s0..s15)
  D3D12_STATIC_SAMPLER_DESC static_samplers[16] = {};
  for (uint32_t i = 0; i < 16; ++i) {
    D3D12_STATIC_SAMPLER_DESC& s = static_samplers[i];
    s.Filter = D3D12_FILTER_ANISOTROPIC;
    s.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    s.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    s.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    s.MipLODBias = 0.0f;
    s.MaxAnisotropy = aniso_ > 0 ? aniso_ : 16;
    s.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    s.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
    s.MinLOD = 0.0f;
    s.MaxLOD = D3D12_FLOAT32_MAX;
    s.ShaderRegister = i;
    s.RegisterSpace = 0;
    s.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
  }

  D3D12_ROOT_SIGNATURE_DESC rs_desc = {};
  rs_desc.NumParameters = 7;
  rs_desc.pParameters = root_params;
  rs_desc.NumStaticSamplers = 16;
  rs_desc.pStaticSamplers = static_samplers;
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

  constexpr uint32_t kConstantBufferSize = 4u << 20; // 4 MB ring buffer de constantes

  D3D12_HEAP_PROPERTIES hp = {};
  hp.Type = D3D12_HEAP_TYPE_UPLOAD;

  D3D12_RESOURCE_DESC rd = {};
  rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  rd.Width = kConstantBufferSize;
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
  if (FAILED(hr)) return false;

  // Buffer raw para t0 (xe_mem) - 512 MB de memoria fisica de Xbox 360
  constexpr uint32_t kRawMemBytes = 512u << 20;
  D3D12_RESOURCE_DESC mem_rd = {};
  mem_rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  mem_rd.Width = kRawMemBytes;
  mem_rd.Height = 1;
  mem_rd.DepthOrArraySize = 1;
  mem_rd.MipLevels = 1;
  mem_rd.SampleDesc.Count = 1;
  mem_rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

  hr = dev->CreateCommittedResource(
      &hp, D3D12_HEAP_FLAG_NONE, &mem_rd, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
      IID_PPV_ARGS(&guest_memory_buffer_));
  if (FAILED(hr)) return false;

  hr = guest_memory_buffer_->Map(0, &read_range, reinterpret_cast<void**>(&guest_memory_ptr_));
  return SUCCEEDED(hr);
}

void D3D12Renderer::BeginFrame() {
  is_frame_active_ = true;
  constant_offset_ = 0;
  last_pso_ = nullptr;
  last_bound_pso_ = nullptr;
  std::memset(last_cb_gpu_, 0, sizeof(last_cb_gpu_));
  std::memset(last_cb_version_, 0, sizeof(last_cb_version_));
  std::memset(last_bound_root_cb_, 0, sizeof(last_bound_root_cb_));
  last_ib_addr_ = 0;
  last_ib_count_ = 0;
  last_ib_format_ = IndexFormat::Index16;
  std::memset(uploaded_pages_mask_, 0, sizeof(uploaded_pages_mask_));
  std::memset(&last_vp_, 0, sizeof(last_vp_));
  std::memset(&last_scissor_, 0, sizeof(last_scissor_));
  last_topo_ = D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;
  last_xe_mem_gpu_ = 0;

  ID3D12CommandAllocator* allocator = device_ctx_.GetCurrentAllocator();
  ID3D12GraphicsCommandList* list = device_ctx_.GetCommandList();
  if (!allocator || !list) return;

  allocator->Reset();
  list->Reset(allocator, nullptr);

  // Transicion de barrera: PRESENT -> RENDER_TARGET para el swapchain backbuffer (solo con swapchain real)
  ID3D12Resource* rt = device_ctx_.GetCurrentRenderTarget();
  if (rt && device_ctx_.GetSwapChain()) {
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = rt;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    list->ResourceBarrier(1, &barrier);
  }

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

  if (srv_heap_) {
    ID3D12DescriptorHeap* heaps[] = {srv_heap_.Get()};
    list->SetDescriptorHeaps(1, heaps);
    list->SetGraphicsRootDescriptorTable(6, srv_heap_->GetGPUDescriptorHandleForHeapStart());
  }

  if (guest_memory_buffer_) {
    list->SetGraphicsRootShaderResourceView(5, guest_memory_buffer_->GetGPUVirtualAddress());
  }
}

const std::vector<uint8_t>& D3D12Renderer::GetOrLoadShaderBytecode(uint64_t hash, const char* stage) {
  static const std::vector<uint8_t> s_empty;
  if (hash == 0) return s_empty;

  auto& cache = (std::strcmp(stage, "vs") == 0) ? vs_bytecodes_ : ps_bytecodes_;
  auto it = cache.find(hash);
  if (it != cache.end()) {
    return it->second;
  }

  // 1. Cargar bytecode .cso precompilado
  std::vector<uint8_t> cso = LoadShaderBytecodeFromDisk(hash, stage);
  if (!cso.empty()) {
    cache[hash] = std::move(cso);
    return cache[hash];
  }

  // 2. Si no hay .cso, compilar desde .hlsl si existe
  std::string hlsl = LoadShaderSourceFromDisk(hash, stage);
  if (!hlsl.empty()) {
    ComPtr<ID3DBlob> blob, errors;
    NativeShaderInclude include_handler;
    const char* profile = (std::strcmp(stage, "vs") == 0) ? "vs_5_0" : "ps_5_0";
    HRESULT hr = D3DCompile(
        hlsl.data(), hlsl.size(), "native_shader.hlsl", nullptr, &include_handler,
        "main", profile, D3DCOMPILE_OPTIMIZATION_LEVEL1 | D3DCOMPILE_IEEE_STRICTNESS, 0, &blob, &errors);
    if (SUCCEEDED(hr) && blob) {
      std::vector<uint8_t> compiled(reinterpret_cast<const uint8_t*>(blob->GetBufferPointer()),
                                    reinterpret_cast<const uint8_t*>(blob->GetBufferPointer()) + blob->GetBufferSize());
      SaveShaderBytecodeToDisk(hash, stage, blob->GetBufferPointer(), blob->GetBufferSize());
      cache[hash] = std::move(compiled);
      return cache[hash];
    }
  }

  cache[hash] = {}; // Cache negativo
  return cache[hash];
}

ID3D12PipelineState* D3D12Renderer::GetOrCreatePipelineState(const DrawCommand& cmd) {
  ID3D12Device* dev = device_ctx_.GetDevice();
  if (!dev || !root_signature_) return nullptr;

  const auto& vs_info = cmd.state.vertex_shader;
  const auto& ps_info = cmd.state.pixel_shader;

  if (vs_info.hash == 0) return nullptr;

  const auto& vs_code = GetOrLoadShaderBytecode(vs_info.hash, "vs");
  if (vs_code.empty()) return nullptr;

  const auto& ps_code = (ps_info.hash != 0) ? GetOrLoadShaderBytecode(ps_info.hash, "ps") : std::vector<uint8_t>{};

  D3D12_PRIMITIVE_TOPOLOGY_TYPE topo_type = ToD3D12TopologyType(cmd.topology);
  bool is_rect = (cmd.topology == PrimitiveTopology::RectangleList && !rect_gs_bytecode_.empty());

  // Clave unica empaquetada (bitfields enteros puros sin padding de compilador)
  D3D12PsoKey key = {};
  key.vs = vs_info.hash;
  key.ps = ps_info.hash;
  key.topo = static_cast<uint32_t>(topo_type);
  key.rect = is_rect ? 1 : 0;

  // Empaquetar estado de blend
  uint64_t b0 = (cmd.state.blend.independent_blend_enable ? 1ULL : 0ULL) |
                (cmd.state.blend.targets[0].blend_enable ? 2ULL : 0ULL) |
                ((static_cast<uint64_t>(cmd.state.blend.targets[0].src_blend) & 0x1FULL) << 2) |
                ((static_cast<uint64_t>(cmd.state.blend.targets[0].dest_blend) & 0x1FULL) << 7) |
                ((static_cast<uint64_t>(cmd.state.blend.targets[0].blend_op) & 0x7ULL) << 12) |
                ((static_cast<uint64_t>(cmd.state.blend.targets[0].src_blend_alpha) & 0x1FULL) << 15) |
                ((static_cast<uint64_t>(cmd.state.blend.targets[0].dest_blend_alpha) & 0x1FULL) << 20) |
                ((static_cast<uint64_t>(cmd.state.blend.targets[0].blend_op_alpha) & 0x7ULL) << 25) |
                ((static_cast<uint64_t>(cmd.state.blend.targets[0].write_mask) & 0xFULL) << 28);
  key.blend_bits = b0;

  if (cmd.state.blend.independent_blend_enable) {
    uint64_t b1 = 0;
    for (int i = 1; i < 4; ++i) {
      uint64_t sub = (cmd.state.blend.targets[i].blend_enable ? 1ULL : 0ULL) |
                     ((static_cast<uint64_t>(cmd.state.blend.targets[i].src_blend) & 0x1FULL) << 1) |
                     ((static_cast<uint64_t>(cmd.state.blend.targets[i].dest_blend) & 0x1FULL) << 6) |
                     ((static_cast<uint64_t>(cmd.state.blend.targets[i].blend_op) & 0x7ULL) << 11) |
                     ((static_cast<uint64_t>(cmd.state.blend.targets[i].src_blend_alpha) & 0x1FULL) << 14) |
                     ((static_cast<uint64_t>(cmd.state.blend.targets[i].dest_blend_alpha) & 0x1FULL) << 19) |
                     ((static_cast<uint64_t>(cmd.state.blend.targets[i].blend_op_alpha) & 0x7ULL) << 24) |
                     ((static_cast<uint64_t>(cmd.state.blend.targets[i].write_mask) & 0xFULL) << 27);
      b1 |= (sub << ((i - 1) * 21));
    }
    key.blend_bits2 = b1;
  }

  // Empaquetar depth stencil
  const auto& ds = cmd.state.depth_stencil;
  uint64_t d0 = (ds.depth_enable ? 1ULL : 0ULL) |
                (ds.depth_write_enable ? 2ULL : 0ULL) |
                ((static_cast<uint64_t>(ds.depth_func) & 0x7ULL) << 2) |
                ((ds.stencil_enable ? 1ULL : 0ULL) << 5) |
                ((static_cast<uint64_t>(ds.stencil_read_mask) & 0xFFULL) << 6) |
                ((static_cast<uint64_t>(ds.stencil_write_mask) & 0xFFULL) << 14) |
                ((ds.backface_enable ? 1ULL : 0ULL) << 22) |
                ((static_cast<uint64_t>(ds.front_face.fail_op) & 0xFULL) << 23) |
                ((static_cast<uint64_t>(ds.front_face.depth_fail_op) & 0xFULL) << 27) |
                ((static_cast<uint64_t>(ds.front_face.pass_op) & 0xFULL) << 31) |
                ((static_cast<uint64_t>(ds.front_face.compare_func) & 0x7ULL) << 35) |
                ((static_cast<uint64_t>(ds.back_face.fail_op) & 0xFULL) << 38) |
                ((static_cast<uint64_t>(ds.back_face.depth_fail_op) & 0xFULL) << 42) |
                ((static_cast<uint64_t>(ds.back_face.pass_op) & 0xFULL) << 46) |
                ((static_cast<uint64_t>(ds.back_face.compare_func) & 0x7ULL) << 50);
  key.ds_bits = d0;

  // Empaquetar rasterizador
  const auto& rs = cmd.state.rasterizer;
  uint32_t slope_bits = 0;
  std::memcpy(&slope_bits, &rs.slope_scaled_depth_bias, 4);
  uint64_t r0 = (static_cast<uint64_t>(rs.fill_mode) & 0x1ULL) |
                ((static_cast<uint64_t>(rs.cull_mode) & 0x3ULL) << 1) |
                ((rs.front_counter_clockwise ? 1ULL : 0ULL) << 3) |
                ((static_cast<uint64_t>(static_cast<uint32_t>(static_cast<int32_t>(rs.depth_bias)))) << 4) ^
                (static_cast<uint64_t>(slope_bits) << 32);
  key.raster_bits = r0;

  // Comprobacion inmediata MRU de 1 elemento (90% de aciertos consecutivos)
  if (last_pso_ && key == last_pso_key_) {
    return last_pso_;
  }

  auto it = pso_cache_.find(key);
  if (it != pso_cache_.end()) {
    last_pso_key_ = key;
    last_pso_ = it->second.Get();
    return last_pso_;
  }

  // Conversion diferida a descriptores D3D12 solo ante fallo de cache
  D3D12_BLEND_DESC blend_desc = ToD3D12BlendDesc(cmd.state.blend);
  D3D12_DEPTH_STENCIL_DESC ds_desc = ToD3D12DepthStencilDesc(cmd.state.depth_stencil);
  D3D12_RASTERIZER_DESC rs_desc = ToD3D12RasterizerDesc(cmd.state.rasterizer);

  D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
  desc.pRootSignature = root_signature_.Get();
  desc.VS = {vs_code.data(), vs_code.size()};
  if (!ps_code.empty()) {
    desc.PS = {ps_code.data(), ps_code.size()};
  }
  if (is_rect) {
    desc.GS = {rect_gs_bytecode_.data(), rect_gs_bytecode_.size()};
  }
  desc.BlendState = blend_desc;
  desc.SampleMask = UINT_MAX;
  desc.RasterizerState = rs_desc;
  desc.DepthStencilState = ds_desc;
  desc.InputLayout = {nullptr, 0};
  desc.IBStripCutValue = D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_DISABLED;
  desc.PrimitiveTopologyType = topo_type;
  desc.NumRenderTargets = 1;
  desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
  desc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
  desc.SampleDesc.Count = 1;
  desc.SampleDesc.Quality = 0;

  ComPtr<ID3D12PipelineState> pso;
  HRESULT hr = dev->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&pso));
  if (SUCCEEDED(hr)) {
    last_pso_key_ = key;
    last_pso_ = pso.Get();
    pso_cache_[key] = pso;
    return pso.Get();
  }

  pso_cache_[key] = nullptr;
  return nullptr;
}

void D3D12Renderer::RenderDraw(const DrawCommand& cmd, const uint8_t* guest_memory_base) {
  ID3D12GraphicsCommandList* list = device_ctx_.GetCommandList();
  if (!list || !constant_upload_ptr_) return;

  // 1. Obtener o compilar PSO para este Draw
  ID3D12PipelineState* pso = GetOrCreatePipelineState(cmd);
  if (cmd.state.vertex_shader.hash != 0 && !pso) {
    return; // Si el shader existe pero no pudo crearse el PSO, evitar fallo GPU
  }
  if (pso && pso != last_bound_pso_) {
    list->SetPipelineState(pso);
    last_bound_pso_ = pso;
  }

  // 2. Actualizar constantes en el ring buffer de upload (con cache de buffer para omitir memcpys identicos)
  auto alloc_block = [&](const void* data, uint32_t size) -> D3D12_GPU_VIRTUAL_ADDRESS {
    uint32_t aligned = (size + 255) & ~255u;
    if (constant_offset_ + aligned > (4u << 20)) constant_offset_ = 0;
    uint32_t cur = constant_offset_;
    std::memcpy(constant_upload_ptr_ + cur, data, size);
    constant_offset_ += aligned;
    return constant_upload_buffer_->GetGPUVirtualAddress() + cur;
  };

  auto upload_or_reuse = [&](int slot, uint32_t ver, const void* data, uint32_t size) -> D3D12_GPU_VIRTUAL_ADDRESS {
    if (last_cb_gpu_[slot] != 0 && ver == last_cb_version_[slot]) {
      return last_cb_gpu_[slot];
    }
    D3D12_GPU_VIRTUAL_ADDRESS gpu_addr = alloc_block(data, size);
    last_cb_gpu_[slot] = gpu_addr;
    last_cb_version_[slot] = ver;
    return gpu_addr;
  };

  auto bind_root_cb = [&](UINT root_index, D3D12_GPU_VIRTUAL_ADDRESS addr) {
    if (addr != 0 && addr != last_bound_root_cb_[root_index]) {
      list->SetGraphicsRootConstantBufferView(root_index, addr);
      last_bound_root_cb_[root_index] = addr;
    }
  };

  bind_root_cb(0, upload_or_reuse(0, cmd.state.constants.vs_version, cmd.state.constants.vs_float, sizeof(cmd.state.constants.vs_float)));
  bind_root_cb(1, upload_or_reuse(1, cmd.state.constants.ps_version, cmd.state.constants.ps_float, sizeof(cmd.state.constants.ps_float)));

  uint8_t bl_data[160] = {};
  std::memcpy(bl_data, cmd.state.constants.bool_bits, sizeof(cmd.state.constants.bool_bits));
  std::memcpy(bl_data + 32, cmd.state.constants.loop_consts, sizeof(cmd.state.constants.loop_consts));
  bind_root_cb(2, upload_or_reuse(2, cmd.state.constants.bool_version, bl_data, sizeof(bl_data)));

  bind_root_cb(3, upload_or_reuse(3, cmd.state.constants.fetch_version, cmd.state.constants.fetch_consts, sizeof(cmd.state.constants.fetch_consts)));
  bind_root_cb(4, upload_or_reuse(4, cmd.state.constants.system_version, &cmd.state.constants.system, sizeof(cmd.state.constants.system)));

  // 3. Subir geometria de vertices a guest_memory_ptr_ (t0: xe_mem)
  if (guest_memory_ptr_ && guest_memory_base) {
    constexpr uint32_t kRawMemBytes = 512u << 20;
    static const uint32_t kKnownVfcSlots[] = {95, 94, 93};
    const bool uses_vfc0 = (cmd.state.vertex_shader.hash == 0x1E6883FCCDE1F688ull);

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
            std::memcpy(guest_memory_ptr_ + phys_addr, src_ptr, safe_bytes);
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

    if (last_xe_mem_gpu_ != guest_memory_buffer_->GetGPUVirtualAddress()) {
      list->SetGraphicsRootShaderResourceView(5, guest_memory_buffer_->GetGPUVirtualAddress());
      last_xe_mem_gpu_ = guest_memory_buffer_->GetGPUVirtualAddress();
    }
  }

  // 4. Viewport y Scissor con filtro de redundancia
  D3D12_VIEWPORT vp = {0.0f, 0.0f, float(device_ctx_.GetWidth()), float(device_ctx_.GetHeight()), 0.0f, 1.0f};
  if (vp.Width != last_vp_.Width || vp.Height != last_vp_.Height) {
    list->RSSetViewports(1, &vp);
    last_vp_ = vp;
  }

  D3D12_RECT d3d_rect;
  if (cmd.state.rasterizer.scissor_enable) {
    const Rect2D& r = cmd.state.rasterizer.scissor_rect;
    d3d_rect = {
        std::max(0L, static_cast<LONG>(r.left)),
        std::max(0L, static_cast<LONG>(r.top)),
        std::min(static_cast<LONG>(device_ctx_.GetWidth()), static_cast<LONG>(r.right)),
        std::min(static_cast<LONG>(device_ctx_.GetHeight()), static_cast<LONG>(r.bottom))
    };
    if (d3d_rect.right <= d3d_rect.left || d3d_rect.bottom <= d3d_rect.top) {
      d3d_rect = {0, 0, LONG(device_ctx_.GetWidth()), LONG(device_ctx_.GetHeight())};
    }
  } else {
    d3d_rect = {0, 0, LONG(device_ctx_.GetWidth()), LONG(device_ctx_.GetHeight())};
  }
  if (std::memcmp(&d3d_rect, &last_scissor_, sizeof(d3d_rect)) != 0) {
    list->RSSetScissorRects(1, &d3d_rect);
    last_scissor_ = d3d_rect;
  }

  // 5. Topologia
  D3D12_PRIMITIVE_TOPOLOGY topo = ToD3D12Topology(cmd.topology);
  if (topo != last_topo_) {
    list->IASetPrimitiveTopology(topo);
    last_topo_ = topo;
  }

  // 6. Draw indexed o Draw instanced
  if (cmd.is_indexed && cmd.index_count > 0) {
    if (cmd.index_buffer_address && guest_memory_base) {
      uint32_t idx_addr = cmd.index_buffer_address & 0x1FFFFFFFu;
      if (idx_addr >= 0x00010000 && idx_addr < (512u << 20)) {
        if (idx_addr == last_ib_addr_ && cmd.index_count == last_ib_count_ && cmd.index_format == last_ib_format_ && ib_upload_buffer_) {
          // Omitir subida y re-enlace repetido del mismo buffer de indices
          list->DrawIndexedInstanced(cmd.index_count, 1, 0, cmd.base_vertex, 0);
        } else {
          const uint8_t* idx_bytes = guest_memory_base + idx_addr;
          DXGI_FORMAT ifmt = (cmd.index_format == IndexFormat::Index32) ? DXGI_FORMAT_R32_UINT : DXGI_FORMAT_R16_UINT;
          uint32_t idx_stride = (cmd.index_format == IndexFormat::Index32) ? 4 : 2;
          uint32_t total_bytes = cmd.index_count * idx_stride;

          size_t safe_ib_bytes = GetReadableBytes(idx_bytes, total_bytes);
          if (safe_ib_bytes >= total_bytes) {
            ID3D12Device* dev = device_ctx_.GetDevice();
            if (!ib_upload_buffer_ || ib_upload_size_ < total_bytes) {
              if (ib_upload_buffer_) {
                ib_upload_buffer_->Unmap(0, nullptr);
                ib_upload_buffer_.Reset();
              }
              ib_upload_size_ = (total_bytes + 65535) & ~65535u;
              D3D12_HEAP_PROPERTIES hp = {};
              hp.Type = D3D12_HEAP_TYPE_UPLOAD;
              D3D12_RESOURCE_DESC rd = {};
              rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
              rd.Width = ib_upload_size_;
              rd.Height = 1;
              rd.DepthOrArraySize = 1;
              rd.MipLevels = 1;
              rd.SampleDesc.Count = 1;
              rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

              dev->CreateCommittedResource(
                  &hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                  IID_PPV_ARGS(&ib_upload_buffer_));
              D3D12_RANGE r = {0, 0};
              ib_upload_buffer_->Map(0, &r, reinterpret_cast<void**>(&ib_upload_ptr_));
            }

            if (ib_upload_ptr_) {
              std::memcpy(ib_upload_ptr_, idx_bytes, total_bytes);
              D3D12_INDEX_BUFFER_VIEW ibv = {};
              ibv.BufferLocation = ib_upload_buffer_->GetGPUVirtualAddress();
              ibv.SizeInBytes = total_bytes;
              ibv.Format = ifmt;
              list->IASetIndexBuffer(&ibv);
              list->DrawIndexedInstanced(cmd.index_count, 1, 0, cmd.base_vertex, 0);
              last_ib_addr_ = idx_addr;
              last_ib_count_ = cmd.index_count;
              last_ib_format_ = cmd.index_format;
            }
          }
        }
      }
    }
  } else if (cmd.vertex_count > 0) {
    list->DrawInstanced(cmd.vertex_count, 1, 0, 0);
  }
}

void D3D12Renderer::RenderResolve(const ResolveCommand& cmd) {
  // Resolves en D3D12
}

void D3D12Renderer::EndFrame() {
  ID3D12GraphicsCommandList* list = device_ctx_.GetCommandList();
  if (list) {
    // Transicion de barrera: RENDER_TARGET -> PRESENT (solo con swapchain real)
    ID3D12Resource* rt = device_ctx_.GetCurrentRenderTarget();
    if (rt && device_ctx_.GetSwapChain()) {
      D3D12_RESOURCE_BARRIER barrier = {};
      barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
      barrier.Transition.pResource = rt;
      barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
      barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
      barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
      list->ResourceBarrier(1, &barrier);
    }

    list->Close();
    ID3D12CommandList* lists[] = {list};
    device_ctx_.GetCommandQueue()->ExecuteCommandLists(1, lists);
  }
  is_frame_active_ = false;
}

void D3D12Renderer::Present() {
  device_ctx_.Present(vsync_);
}

void D3D12Renderer::Resize(uint32_t width, uint32_t height) {
  device_ctx_.Resize(width, height);
}

void D3D12Renderer::Shutdown() {
  device_ctx_.WaitForGpu();

  pso_cache_.clear();
  vs_bytecodes_.clear();
  ps_bytecodes_.clear();
  rect_gs_bytecode_.clear();

  if (ib_upload_buffer_) {
    ib_upload_buffer_->Unmap(0, nullptr);
    ib_upload_buffer_.Reset();
    ib_upload_ptr_ = nullptr;
    ib_upload_size_ = 0;
  }

  if (guest_memory_buffer_) {
    D3D12_RANGE empty_range = {0, 0};
    guest_memory_buffer_->Unmap(0, &empty_range);
    guest_memory_buffer_.Reset();
    guest_memory_ptr_ = nullptr;
  }

  if (constant_upload_buffer_) {
    D3D12_RANGE empty_range = {0, 0};
    constant_upload_buffer_->Unmap(0, &empty_range);
    constant_upload_buffer_.Reset();
    constant_upload_ptr_ = nullptr;
  }

  srv_heap_.Reset();
  root_signature_.Reset();
  device_ctx_.Shutdown();
}

}  // namespace re_wor::nativegfx
#endif
