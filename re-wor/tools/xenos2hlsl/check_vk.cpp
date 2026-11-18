// Backend Vulkan de xenos2hlsl_check. Ver check_vk.h.
//
// Bindings (set 0), los mismos que pone validate.py al compilar a SPIR-V:
//   0-3 cbuffers b0-b3 | 16 xe_mem (t0) | 17-48 Texture2D (t1-t32) | 49-80 Texture3D (t33-t64)
//   81-112 TextureCube (t65-t96) | 128-159 samplers (s0-s31) | 200 salida del geometry shader | 201 entrada de pixeles
//
// Vertices: detras del shader va un geometry shader que copia posicion e interpoladores (sin interpolar, antes del
// recorte) a un buffer; la rasterizacion esta desactivada. Pixeles: como en D3D11, un punto de 1 pixel por caso con
// los interpoladores fijos, 4 destinos de color y profundidad borrada a 0.25 (si queda 0.25, el pixel se descarto).
#define VK_NO_PROTOTYPES
#include <windows.h>
#include <vulkan/vulkan.h>
#include <unknwn.h>   // IUnknown / IStream: el proyecto compila con WIN32_LEAN_AND_MEAN
#include <objidl.h>
#include <dxcapi.h>
#include <wrl/client.h>

#include <cstring>

#include <fmt/format.h>

#include "check_vk.h"

namespace re_wor::xenos2hlsl::vkcheck {
namespace {

using Microsoft::WRL::ComPtr;

#define XE_VK_INSTANCE_FNS(X)                                                                                         \
  X(vkDestroyInstance) X(vkEnumeratePhysicalDevices) X(vkGetPhysicalDeviceProperties)                               \
  X(vkGetPhysicalDeviceProperties2) X(vkGetPhysicalDeviceFeatures) X(vkGetPhysicalDeviceQueueFamilyProperties)      \
  X(vkGetPhysicalDeviceMemoryProperties) X(vkCreateDevice) X(vkGetDeviceProcAddr)

#define XE_VK_DEVICE_FNS(X)                                                                                           \
  X(vkDestroyDevice) X(vkGetDeviceQueue) X(vkDeviceWaitIdle) X(vkQueueSubmit) X(vkQueueWaitIdle)                    \
  X(vkCreateBuffer) X(vkGetBufferMemoryRequirements) X(vkBindBufferMemory) X(vkAllocateMemory) X(vkMapMemory)        \
  X(vkCreateImage) X(vkGetImageMemoryRequirements) X(vkBindImageMemory) X(vkCreateImageView) X(vkCreateSampler)     \
  X(vkCreateDescriptorSetLayout) X(vkCreatePipelineLayout) X(vkCreateDescriptorPool) X(vkAllocateDescriptorSets)    \
  X(vkUpdateDescriptorSets) X(vkCreateShaderModule) X(vkDestroyShaderModule) X(vkCreateGraphicsPipelines)           \
  X(vkDestroyPipeline) X(vkCreateRenderPass) X(vkCreateFramebuffer) X(vkCreateCommandPool)                         \
  X(vkAllocateCommandBuffers) X(vkBeginCommandBuffer) X(vkEndCommandBuffer) X(vkCmdBeginRenderPass)                 \
  X(vkCmdEndRenderPass) X(vkCmdBindPipeline) X(vkCmdBindDescriptorSets) X(vkCmdDraw) X(vkCmdPipelineBarrier)        \
  X(vkCmdCopyBufferToImage) X(vkCmdCopyImageToBuffer)

#define VKC(x)                                                       \
  do {                                                               \
    const VkResult r_ = (x);                                         \
    if (r_ != VK_SUCCESS) {                                          \
      err = fmt::format("{} fallo ({})", #x, int(r_));               \
      return false;                                                  \
    }                                                                \
  } while (0)

constexpr uint32_t kBindMem = 16, kBindTex2D = 17, kBindTex3D = 49, kBindTexCube = 81, kBindSampler = 128;
constexpr uint32_t kBindGsOut = 200, kBindPsIn = 201;
constexpr VkFormat kColorFormat = VK_FORMAT_R32G32B32A32_SFLOAT;
constexpr float kDepthClear = 0.25f;
constexpr float kSentinel = -7777.0f;

// geometry shader de captura: ve cada vertice antes del recorte y lo copia tal cual
const char* kCaptureGs = R"(
struct XeVaryings { float4 pos : SV_Position; float4 o[16] : TEXCOORD0; };
struct XeNone { float4 pos : SV_Position; };
[[vk::binding(200, 0)]] RWStructuredBuffer<float4> xe_out;
[maxvertexcount(1)]
void main(point XeVaryings v[1], uint id : SV_PrimitiveID, inout PointStream<XeNone> s) {
  xe_out[id * 17u] = v[0].pos;
  [unroll] for (uint i = 0u; i < 16u; ++i) xe_out[id * 17u + 1u + i] = v[0].o[i];
}
)";

// vertice de paso para los pixeles: un punto por pixel con sus 16 interpoladores
const char* kPassVs = R"(
struct XeOut {
  float4 pos : SV_Position;
  float4 o[16] : TEXCOORD0;
  [[vk::builtin("PointSize")]] float size : PSIZE;
};
[[vk::binding(201, 0)]] StructuredBuffer<float4> xe_in;
XeOut main(uint id : SV_VertexID) {
  XeOut v;
  v.pos = float4((float(id) + 0.5) / XE_CASES * 2.0 - 1.0, 0.0, 0.5, 1.0);
  [unroll] for (uint i = 0u; i < 16u; ++i) v.o[i] = xe_in[id * 16u + i];
  v.size = 1.0;
  return v;
}
)";

struct Buf {
  VkBuffer buf = VK_NULL_HANDLE;
  VkDeviceMemory mem = VK_NULL_HANDLE;
  void* ptr = nullptr;
  VkDeviceSize size = 0;
};

struct Img {
  VkImage img = VK_NULL_HANDLE;
  VkDeviceMemory mem = VK_NULL_HANDLE;
  VkImageView view = VK_NULL_HANDLE;
};

std::wstring Widen(const std::string& s) {
  std::wstring w(s.size(), L'\0');
  w.resize(MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), w.data(), int(w.size())));
  return w;
}

class Impl final : public Backend {
 public:
  ~Impl() override {
    // herramienta de una sola pasada: basta con soltar el dispositivo y la instancia
    if (dev_) {
      vkDeviceWaitIdle(dev_);
      vkDestroyDevice(dev_, nullptr);
    }
    if (inst_) vkDestroyInstance(inst_, nullptr);
  }

  bool Init(uint32_t cases, uint32_t mem_size, const float (*tex_colors)[4], const std::string& dxc_dll,
            std::string& err) {
    cases_ = cases;
    mem_size_ = mem_size;
    if (!InitDxc(dxc_dll, err) || !InitDevice(err) || !InitResources(tex_colors, err)) return false;
    std::string pass = kPassVs;
    pass.insert(0, fmt::format("#define XE_CASES {}.0\n", cases_));
    std::vector<uint32_t> code;
    if (!CompileProfile(pass, L"vs_6_0", code, err) || !MakeModule(code, pass_vs_, err)) {
      err = "vertice de paso: " + err;
      return false;
    }
    if (!CompileProfile(kCaptureGs, L"gs_6_0", code, err) || !MakeModule(code, capture_gs_, err)) {
      err = "geometry shader de captura: " + err;
      return false;
    }
    return true;
  }

  const std::string& device_name() const override { return name_; }

  bool Compile(const std::string& src, bool pixel, std::vector<uint32_t>& spirv, std::string& err) override {
    return CompileProfile(src, pixel ? L"ps_6_0" : L"vs_6_0", spirv, err);
  }

  bool RunVertex(const std::vector<uint32_t>& spirv, const Frame& f, std::vector<float>& out,
                 std::string& err) override {
    Upload(f);
    std::memset(gs_out_.ptr, 0, size_t(gs_out_.size));
    VkShaderModule vs;
    if (!MakeModule(spirv, vs, err)) return false;
    VkPipelineShaderStageCreateInfo st[2] = {Stage(VK_SHADER_STAGE_VERTEX_BIT, vs),
                                             Stage(VK_SHADER_STAGE_GEOMETRY_BIT, capture_gs_)};
    VkPipeline pipe;
    const bool ok = MakePipeline(st, rp_vs_, 0, true, pipe, err);
    vkDestroyShaderModule(dev_, vs, nullptr);
    if (!ok) return false;

    if (!Begin(err)) return false;
    VkRenderPassBeginInfo rb = {VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    rb.renderPass = rp_vs_;
    rb.framebuffer = fb_vs_;
    rb.renderArea.extent = {cases_, 1};
    vkCmdBeginRenderPass(cmd_, &rb, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(cmd_, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
    vkCmdBindDescriptorSets(cmd_, VK_PIPELINE_BIND_POINT_GRAPHICS, layout_, 0, 1, &set_, 0, nullptr);
    vkCmdDraw(cmd_, cases_, 1, 0, 0);
    vkCmdEndRenderPass(cmd_);
    HostBarrier(VK_PIPELINE_STAGE_GEOMETRY_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT);
    const bool ran = Submit(err);
    vkDestroyPipeline(dev_, pipe, nullptr);
    if (!ran) return false;
    out.assign(size_t(cases_) * 17 * 4, 0.0f);
    std::memcpy(out.data(), gs_out_.ptr, out.size() * sizeof(float));
    return true;
  }

  bool RunPixel(const std::vector<uint32_t>& spirv, const Frame& f, std::vector<float>& colors,
                std::vector<float>& depth, std::string& err) override {
    Upload(f);
    std::memcpy(ps_in_.ptr, f.ps_in, size_t(cases_) * 16 * 16);
    VkShaderModule ps;
    if (!MakeModule(spirv, ps, err)) return false;
    VkPipelineShaderStageCreateInfo st[2] = {Stage(VK_SHADER_STAGE_VERTEX_BIT, pass_vs_),
                                             Stage(VK_SHADER_STAGE_FRAGMENT_BIT, ps)};
    VkPipeline pipe;
    const bool ok = MakePipeline(st, rp_ps_, 4, false, pipe, err);
    vkDestroyShaderModule(dev_, ps, nullptr);
    if (!ok) return false;

    if (!Begin(err)) return false;
    VkClearValue clears[5];
    for (int i = 0; i < 4; ++i)
      for (float& c : clears[i].color.float32) c = kSentinel;
    clears[4].depthStencil = {kDepthClear, 0};
    VkRenderPassBeginInfo rb = {VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    rb.renderPass = rp_ps_;
    rb.framebuffer = fb_ps_;
    rb.renderArea.extent = {cases_, 1};
    rb.clearValueCount = 5;
    rb.pClearValues = clears;
    vkCmdBeginRenderPass(cmd_, &rb, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(cmd_, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
    vkCmdBindDescriptorSets(cmd_, VK_PIPELINE_BIND_POINT_GRAPHICS, layout_, 0, 1, &set_, 0, nullptr);
    vkCmdDraw(cmd_, cases_, 1, 0, 0);
    vkCmdEndRenderPass(cmd_);
    // el render pass deja las imagenes en TRANSFER_SRC (con su dependencia): copiarlas al buffer de lectura
    VkBufferImageCopy cp = {};
    cp.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    cp.imageExtent = {cases_, 1, 1};
    for (uint32_t i = 0; i < 4; ++i) {
      cp.bufferOffset = VkDeviceSize(i) * cases_ * 16;
      vkCmdCopyImageToBuffer(cmd_, rt_[i].img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback_.buf, 1, &cp);
    }
    cp.bufferOffset = VkDeviceSize(4) * cases_ * 16;
    cp.imageSubresource.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    vkCmdCopyImageToBuffer(cmd_, ds_.img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback_.buf, 1, &cp);
    HostBarrier(VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
    const bool ran = Submit(err);
    vkDestroyPipeline(dev_, pipe, nullptr);
    if (!ran) return false;
    const float* p = static_cast<const float*>(readback_.ptr);
    colors.assign(size_t(cases_) * 16, 0.0f);
    for (uint32_t c = 0; c < cases_; ++c)
      for (uint32_t t = 0; t < 4; ++t) std::memcpy(&colors[(size_t(c) * 4 + t) * 4], p + (size_t(t) * cases_ + c) * 4, 16);
    depth.assign(p + size_t(cases_) * 16, p + size_t(cases_) * 17);
    return true;
  }

 private:
#define XE_DECL(n) PFN_##n n = nullptr;
  XE_VK_INSTANCE_FNS(XE_DECL)
  XE_VK_DEVICE_FNS(XE_DECL)
#undef XE_DECL

  uint32_t cases_ = 64, mem_size_ = 0;
  std::string name_;
  HMODULE dxc_dll_ = nullptr;
  ComPtr<IDxcCompiler3> dxc_;
  VkInstance inst_ = VK_NULL_HANDLE;
  VkPhysicalDevice pd_ = VK_NULL_HANDLE;
  VkPhysicalDeviceMemoryProperties memprops_ = {};
  VkDevice dev_ = VK_NULL_HANDLE;
  VkQueue queue_ = VK_NULL_HANDLE;
  uint32_t qfam_ = 0;
  Buf cb_[4], mem_, gs_out_, ps_in_, readback_;
  Img tex_[3][32], rt_[4], ds_;
  VkSampler sampler_ = VK_NULL_HANDLE;
  VkDescriptorSetLayout dsl_ = VK_NULL_HANDLE;
  VkPipelineLayout layout_ = VK_NULL_HANDLE;
  VkDescriptorPool pool_ = VK_NULL_HANDLE;
  VkDescriptorSet set_ = VK_NULL_HANDLE;
  VkRenderPass rp_vs_ = VK_NULL_HANDLE, rp_ps_ = VK_NULL_HANDLE;
  VkFramebuffer fb_vs_ = VK_NULL_HANDLE, fb_ps_ = VK_NULL_HANDLE;
  VkCommandPool cpool_ = VK_NULL_HANDLE;
  VkCommandBuffer cmd_ = VK_NULL_HANDLE;
  VkShaderModule pass_vs_ = VK_NULL_HANDLE, capture_gs_ = VK_NULL_HANDLE;

  // ------------------------------------------------------------------------------------------ dxc
  bool InitDxc(const std::string& path, std::string& err) {
    dxc_dll_ = LoadLibraryExW(Widen(path).c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!dxc_dll_) {
      err = "no se pudo cargar " + path;
      return false;
    }
    auto create = reinterpret_cast<DxcCreateInstanceProc>(GetProcAddress(dxc_dll_, "DxcCreateInstance"));
    if (!create || FAILED(create(CLSID_DxcCompiler, IID_PPV_ARGS(&dxc_)))) {
      err = path + " no es una dxcompiler.dll valida";
      return false;
    }
    return true;
  }

  bool CompileProfile(const std::string& src, const wchar_t* profile, std::vector<uint32_t>& spirv, std::string& err) {
    DxcBuffer in = {src.data(), src.size(), DXC_CP_UTF8};
    // mismas opciones que validate.py
    const wchar_t* args[] = {L"-spirv", L"-T", profile, L"-E", L"main", L"-O3", L"-Gis",
                             L"-fspv-target-env=vulkan1.2", L"-fvk-use-dx-layout", L"-fvk-t-shift", L"16", L"0",
                             L"-fvk-s-shift", L"128", L"0"};
    ComPtr<IDxcResult> res;
    if (FAILED(dxc_->Compile(&in, args, UINT32(std::size(args)), nullptr, IID_PPV_ARGS(&res)))) {
      err = "dxc no arranco";
      return false;
    }
    HRESULT status = E_FAIL;
    res->GetStatus(&status);
    if (FAILED(status)) {
      ComPtr<IDxcBlobUtf8> e;
      res->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&e), nullptr);
      err = e && e->GetStringLength() ? std::string(e->GetStringPointer(), e->GetStringLength()) : "dxc fallo";
      return false;
    }
    ComPtr<IDxcBlob> obj;
    res->GetResult(&obj);
    spirv.resize(obj->GetBufferSize() / 4);
    std::memcpy(spirv.data(), obj->GetBufferPointer(), spirv.size() * 4);
    return true;
  }

  // ------------------------------------------------------------------------------------------ dispositivo
  bool InitDevice(std::string& err) {
    HMODULE vk = LoadLibraryW(L"vulkan-1.dll");
    if (!vk) {
      err = "no hay vulkan-1.dll (driver sin Vulkan)";
      return false;
    }
    auto gipa = reinterpret_cast<PFN_vkGetInstanceProcAddr>(GetProcAddress(vk, "vkGetInstanceProcAddr"));
    auto create_instance = reinterpret_cast<PFN_vkCreateInstance>(gipa(nullptr, "vkCreateInstance"));
    VkApplicationInfo app = {VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "xenos2hlsl_check";
    app.apiVersion = VK_API_VERSION_1_2;
    VkInstanceCreateInfo ici = {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ici.pApplicationInfo = &app;
    VKC(create_instance(&ici, nullptr, &inst_));
#define XE_LOAD(n) n = reinterpret_cast<PFN_##n>(gipa(inst_, #n));
    XE_VK_INSTANCE_FNS(XE_LOAD)
#undef XE_LOAD

    uint32_t n = 0;
    vkEnumeratePhysicalDevices(inst_, &n, nullptr);
    std::vector<VkPhysicalDevice> pds(n);
    vkEnumeratePhysicalDevices(inst_, &n, pds.data());
    int best = -1, discrete = -1;   // la primera GPU dedicada con Vulkan 1.2; si no hay, la primera con 1.2
    for (uint32_t i = 0; i < n; ++i) {
      VkPhysicalDeviceProperties p;
      vkGetPhysicalDeviceProperties(pds[i], &p);
      if (p.apiVersion < VK_API_VERSION_1_2) continue;
      if (best < 0) best = int(i);
      if (discrete < 0 && p.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) discrete = int(i);
    }
    if (discrete >= 0) best = discrete;
    if (best < 0) {
      err = "ninguna GPU con Vulkan 1.2";
      return false;
    }
    pd_ = pds[best];
    VkPhysicalDeviceVulkan12Properties p12 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_PROPERTIES};
    VkPhysicalDeviceProperties2 p2 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, &p12};
    vkGetPhysicalDeviceProperties2(pd_, &p2);
    name_ = p2.properties.deviceName;
    if (!p12.shaderSignedZeroInfNanPreserveFloat32)
      name_ += ", sin SignedZeroInfNanPreserve: los NaN pueden no respetarse";
    VkPhysicalDeviceFeatures have;
    vkGetPhysicalDeviceFeatures(pd_, &have);
    if (!have.geometryShader || !have.vertexPipelineStoresAndAtomics) {
      err = name_ + ": sin geometry shaders o sin escrituras en buffers desde vertices";
      return false;
    }
    vkGetPhysicalDeviceMemoryProperties(pd_, &memprops_);

    vkGetPhysicalDeviceQueueFamilyProperties(pd_, &n, nullptr);
    std::vector<VkQueueFamilyProperties> qf(n);
    vkGetPhysicalDeviceQueueFamilyProperties(pd_, &n, qf.data());
    qfam_ = UINT32_MAX;
    for (uint32_t i = 0; i < n && qfam_ == UINT32_MAX; ++i)
      if (qf[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) qfam_ = i;
    if (qfam_ == UINT32_MAX) {
      err = "sin cola grafica";
      return false;
    }
    const float prio = 1.0f;
    VkDeviceQueueCreateInfo qci = {VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    qci.queueFamilyIndex = qfam_;
    qci.queueCount = 1;
    qci.pQueuePriorities = &prio;
    VkPhysicalDeviceFeatures want = {};
    want.geometryShader = VK_TRUE;
    want.vertexPipelineStoresAndAtomics = VK_TRUE;
    VkDeviceCreateInfo dci = {VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &qci;
    dci.pEnabledFeatures = &want;
    VKC(vkCreateDevice(pd_, &dci, nullptr, &dev_));
#define XE_LOAD(n) n = reinterpret_cast<PFN_##n>(vkGetDeviceProcAddr(dev_, #n));
    XE_VK_DEVICE_FNS(XE_LOAD)
#undef XE_LOAD
    vkGetDeviceQueue(dev_, qfam_, 0, &queue_);
    VkCommandPoolCreateInfo cpi = {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    cpi.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    cpi.queueFamilyIndex = qfam_;
    VKC(vkCreateCommandPool(dev_, &cpi, nullptr, &cpool_));
    VkCommandBufferAllocateInfo cai = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cai.commandPool = cpool_;
    cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cai.commandBufferCount = 1;
    VKC(vkAllocateCommandBuffers(dev_, &cai, &cmd_));
    return true;
  }

  bool Alloc(const VkMemoryRequirements& req, VkMemoryPropertyFlags want, VkDeviceMemory& mem, std::string& err) {
    for (int pass = 0; pass < 2; ++pass) {   // primero con las propiedades pedidas; si no, cualquiera valida
      for (uint32_t i = 0; i < memprops_.memoryTypeCount; ++i) {
        if (!(req.memoryTypeBits & (1u << i))) continue;
        if (pass == 0 && (memprops_.memoryTypes[i].propertyFlags & want) != want) continue;
        VkMemoryAllocateInfo ai = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        ai.allocationSize = req.size;
        ai.memoryTypeIndex = i;
        VKC(vkAllocateMemory(dev_, &ai, nullptr, &mem));
        return true;
      }
      if (want & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) break;   // la memoria visible desde la CPU no es opcional
    }
    err = "sin tipo de memoria adecuado";
    return false;
  }

  bool MakeBuffer(VkDeviceSize size, VkBufferUsageFlags usage, Buf& b, std::string& err) {
    VkBufferCreateInfo bi = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bi.size = size;
    bi.usage = usage;
    VKC(vkCreateBuffer(dev_, &bi, nullptr, &b.buf));
    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(dev_, b.buf, &req);
    if (!Alloc(req, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, b.mem, err))
      return false;
    VKC(vkBindBufferMemory(dev_, b.buf, b.mem, 0));
    VKC(vkMapMemory(dev_, b.mem, 0, VK_WHOLE_SIZE, 0, &b.ptr));
    b.size = size;
    return true;
  }

  bool MakeImage(VkImageType type, VkImageViewType view_type, VkFormat format, uint32_t width, uint32_t layers,
                 VkImageUsageFlags usage, VkImageAspectFlags aspect, Img& im, std::string& err) {
    VkImageCreateInfo ii = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ii.flags = view_type == VK_IMAGE_VIEW_TYPE_CUBE ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0;
    ii.imageType = type;
    ii.format = format;
    ii.extent = {width, 1, 1};
    ii.mipLevels = 1;
    ii.arrayLayers = layers;
    ii.samples = VK_SAMPLE_COUNT_1_BIT;
    ii.tiling = VK_IMAGE_TILING_OPTIMAL;
    ii.usage = usage;
    ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VKC(vkCreateImage(dev_, &ii, nullptr, &im.img));
    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(dev_, im.img, &req);
    if (!Alloc(req, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, im.mem, err)) return false;
    VKC(vkBindImageMemory(dev_, im.img, im.mem, 0));
    VkImageViewCreateInfo vi = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vi.image = im.img;
    vi.viewType = view_type;
    vi.format = format;
    vi.subresourceRange = {aspect, 0, 1, 0, layers};
    VKC(vkCreateImageView(dev_, &vi, nullptr, &im.view));
    return true;
  }

  bool InitResources(const float (*tex_colors)[4], std::string& err) {
    const VkDeviceSize cb_sizes[4] = {4096, 160, 768, 64};
    for (int i = 0; i < 4; ++i)
      if (!MakeBuffer(cb_sizes[i], VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, cb_[i], err)) return false;
    if (!MakeBuffer(mem_size_, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, mem_, err) ||
        !MakeBuffer(VkDeviceSize(cases_) * 17 * 16, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, gs_out_, err) ||
        !MakeBuffer(VkDeviceSize(cases_) * 16 * 16, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, ps_in_, err) ||
        !MakeBuffer(VkDeviceSize(cases_) * 17 * 4 * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT, readback_, err))
      return false;

    // texturas de un texel, como en D3D11: el color de su constante de fetch sea cual sea la coordenada
    Buf staging;
    if (!MakeBuffer(32 * 6 * 16, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, staging, err)) return false;
    for (uint32_t n = 0; n < 32; ++n)
      for (uint32_t f = 0; f < 6; ++f) std::memcpy(static_cast<uint8_t*>(staging.ptr) + (n * 6 + f) * 16, tex_colors[n], 16);
    const VkImageUsageFlags tex_usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    for (uint32_t n = 0; n < 32; ++n) {
      if (!MakeImage(VK_IMAGE_TYPE_2D, VK_IMAGE_VIEW_TYPE_2D, kColorFormat, 1, 1, tex_usage,
                     VK_IMAGE_ASPECT_COLOR_BIT, tex_[0][n], err) ||
          !MakeImage(VK_IMAGE_TYPE_3D, VK_IMAGE_VIEW_TYPE_3D, kColorFormat, 1, 1, tex_usage,
                     VK_IMAGE_ASPECT_COLOR_BIT, tex_[1][n], err) ||
          !MakeImage(VK_IMAGE_TYPE_2D, VK_IMAGE_VIEW_TYPE_CUBE, kColorFormat, 1, 6, tex_usage,
                     VK_IMAGE_ASPECT_COLOR_BIT, tex_[2][n], err))
        return false;
    }
    if (!Begin(err)) return false;
    for (uint32_t k = 0; k < 3; ++k) {
      for (uint32_t n = 0; n < 32; ++n) {
        const uint32_t layers = k == 2 ? 6 : 1;
        ImageBarrier(tex_[k][n].img, layers, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                     VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
        VkBufferImageCopy cp = {};
        cp.bufferOffset = VkDeviceSize(n) * 6 * 16;
        cp.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, layers};
        cp.imageExtent = {1, 1, 1};
        vkCmdCopyBufferToImage(cmd_, staging.buf, tex_[k][n].img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &cp);
        ImageBarrier(tex_[k][n].img, layers, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_TRANSFER_BIT,
                     VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT, VK_ACCESS_SHADER_READ_BIT);
      }
    }
    if (!Submit(err)) return false;

    VkSamplerCreateInfo si = {VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    si.magFilter = si.minFilter = VK_FILTER_NEAREST;
    si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    si.addressModeU = si.addressModeV = si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.maxLod = VK_LOD_CLAMP_NONE;
    VKC(vkCreateSampler(dev_, &si, nullptr, &sampler_));

    // destinos de los pixeles
    for (auto& r : rt_)
      if (!MakeImage(VK_IMAGE_TYPE_2D, VK_IMAGE_VIEW_TYPE_2D, kColorFormat, cases_, 1,
                     VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, VK_IMAGE_ASPECT_COLOR_BIT, r,
                     err))
        return false;
    if (!MakeImage(VK_IMAGE_TYPE_2D, VK_IMAGE_VIEW_TYPE_2D, VK_FORMAT_D32_SFLOAT, cases_, 1,
                   VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                   VK_IMAGE_ASPECT_DEPTH_BIT, ds_, err))
      return false;

    return InitDescriptors(err) && InitRenderPasses(err);
  }

  bool InitDescriptors(std::string& err) {
    std::vector<VkDescriptorSetLayoutBinding> b;
    auto add = [&](uint32_t binding, VkDescriptorType type) {
      b.push_back({binding, type, 1, VK_SHADER_STAGE_ALL_GRAPHICS, nullptr});
    };
    for (uint32_t i = 0; i < 4; ++i) add(i, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
    add(kBindMem, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    for (uint32_t n = 0; n < 32; ++n) {
      add(kBindTex2D + n, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE);
      add(kBindTex3D + n, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE);
      add(kBindTexCube + n, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE);
      add(kBindSampler + n, VK_DESCRIPTOR_TYPE_SAMPLER);
    }
    add(kBindGsOut, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    add(kBindPsIn, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    VkDescriptorSetLayoutCreateInfo li = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    li.bindingCount = uint32_t(b.size());
    li.pBindings = b.data();
    VKC(vkCreateDescriptorSetLayout(dev_, &li, nullptr, &dsl_));
    VkPipelineLayoutCreateInfo pli = {VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pli.setLayoutCount = 1;
    pli.pSetLayouts = &dsl_;
    VKC(vkCreatePipelineLayout(dev_, &pli, nullptr, &layout_));
    const VkDescriptorPoolSize sizes[] = {{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 4},
                                          {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 3},
                                          {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 96},
                                          {VK_DESCRIPTOR_TYPE_SAMPLER, 32}};
    VkDescriptorPoolCreateInfo pi = {VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pi.maxSets = 1;
    pi.poolSizeCount = uint32_t(std::size(sizes));
    pi.pPoolSizes = sizes;
    VKC(vkCreateDescriptorPool(dev_, &pi, nullptr, &pool_));
    VkDescriptorSetAllocateInfo ai = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    ai.descriptorPool = pool_;
    ai.descriptorSetCount = 1;
    ai.pSetLayouts = &dsl_;
    VKC(vkAllocateDescriptorSets(dev_, &ai, &set_));

    std::vector<VkDescriptorBufferInfo> bufs;
    std::vector<VkDescriptorImageInfo> imgs;
    bufs.reserve(8);
    imgs.reserve(128);
    std::vector<VkWriteDescriptorSet> w;
    auto write_buf = [&](uint32_t binding, VkDescriptorType type, const Buf& bf) {
      bufs.push_back({bf.buf, 0, VK_WHOLE_SIZE});
      VkWriteDescriptorSet x = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
      x.dstSet = set_;
      x.dstBinding = binding;
      x.descriptorCount = 1;
      x.descriptorType = type;
      x.pBufferInfo = &bufs.back();
      w.push_back(x);
    };
    auto write_img = [&](uint32_t binding, VkDescriptorType type, VkImageView view, VkSampler s) {
      imgs.push_back({s, view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL});
      VkWriteDescriptorSet x = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
      x.dstSet = set_;
      x.dstBinding = binding;
      x.descriptorCount = 1;
      x.descriptorType = type;
      x.pImageInfo = &imgs.back();
      w.push_back(x);
    };
    for (uint32_t i = 0; i < 4; ++i) write_buf(i, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, cb_[i]);
    write_buf(kBindMem, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, mem_);
    write_buf(kBindGsOut, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, gs_out_);
    write_buf(kBindPsIn, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, ps_in_);
    for (uint32_t n = 0; n < 32; ++n) {
      write_img(kBindTex2D + n, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, tex_[0][n].view, VK_NULL_HANDLE);
      write_img(kBindTex3D + n, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, tex_[1][n].view, VK_NULL_HANDLE);
      write_img(kBindTexCube + n, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, tex_[2][n].view, VK_NULL_HANDLE);
      write_img(kBindSampler + n, VK_DESCRIPTOR_TYPE_SAMPLER, VK_NULL_HANDLE, sampler_);
    }
    vkUpdateDescriptorSets(dev_, uint32_t(w.size()), w.data(), 0, nullptr);
    return true;
  }

  bool InitRenderPasses(std::string& err) {
    // vertices: sin adjuntos, la rasterizacion esta desactivada
    VkSubpassDescription sub = {};
    sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    VkRenderPassCreateInfo ri = {VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    ri.subpassCount = 1;
    ri.pSubpasses = &sub;
    VKC(vkCreateRenderPass(dev_, &ri, nullptr, &rp_vs_));
    VkFramebufferCreateInfo fi = {VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    fi.renderPass = rp_vs_;
    fi.width = cases_;
    fi.height = 1;
    fi.layers = 1;
    VKC(vkCreateFramebuffer(dev_, &fi, nullptr, &fb_vs_));

    // pixeles: 4 colores + profundidad, que acaban listos para copiarse
    VkAttachmentDescription att[5] = {};
    for (int i = 0; i < 5; ++i) {
      att[i].format = i < 4 ? kColorFormat : VK_FORMAT_D32_SFLOAT;
      att[i].samples = VK_SAMPLE_COUNT_1_BIT;
      att[i].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
      att[i].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
      att[i].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
      att[i].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
      att[i].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
      att[i].finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    }
    VkAttachmentReference colors[4];
    for (uint32_t i = 0; i < 4; ++i) colors[i] = {i, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkAttachmentReference depth = {4, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    VkSubpassDescription ps = {};
    ps.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    ps.colorAttachmentCount = 4;
    ps.pColorAttachments = colors;
    ps.pDepthStencilAttachment = &depth;
    VkSubpassDependency dep = {};
    dep.srcSubpass = 0;
    dep.dstSubpass = VK_SUBPASS_EXTERNAL;
    dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dep.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dep.dstStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
    dep.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    VkRenderPassCreateInfo rpi = {VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    rpi.attachmentCount = 5;
    rpi.pAttachments = att;
    rpi.subpassCount = 1;
    rpi.pSubpasses = &ps;
    rpi.dependencyCount = 1;
    rpi.pDependencies = &dep;
    VKC(vkCreateRenderPass(dev_, &rpi, nullptr, &rp_ps_));
    VkImageView views[5] = {rt_[0].view, rt_[1].view, rt_[2].view, rt_[3].view, ds_.view};
    fi.renderPass = rp_ps_;
    fi.attachmentCount = 5;
    fi.pAttachments = views;
    VKC(vkCreateFramebuffer(dev_, &fi, nullptr, &fb_ps_));
    return true;
  }

  // ------------------------------------------------------------------------------------------ ejecucion
  bool MakeModule(const std::vector<uint32_t>& code, VkShaderModule& m, std::string& err) {
    VkShaderModuleCreateInfo mi = {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    mi.codeSize = code.size() * 4;
    mi.pCode = code.data();
    VKC(vkCreateShaderModule(dev_, &mi, nullptr, &m));
    return true;
  }

  static VkPipelineShaderStageCreateInfo Stage(VkShaderStageFlagBits stage, VkShaderModule m) {
    VkPipelineShaderStageCreateInfo s = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    s.stage = stage;
    s.module = m;
    s.pName = "main";
    return s;
  }

  bool MakePipeline(const VkPipelineShaderStageCreateInfo* stages, VkRenderPass rp, uint32_t color_count,
                    bool discard, VkPipeline& pipe, std::string& err) {
    VkPipelineVertexInputStateCreateInfo vin = {VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo ia = {VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
    VkViewport vp = {0.0f, 0.0f, float(cases_), 1.0f, 0.0f, 1.0f};
    VkRect2D sc = {{0, 0}, {cases_, 1}};
    VkPipelineViewportStateCreateInfo vs = {VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vs.viewportCount = 1;
    vs.pViewports = &vp;
    vs.scissorCount = 1;
    vs.pScissors = &sc;
    VkPipelineRasterizationStateCreateInfo rs = {VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rs.rasterizerDiscardEnable = discard ? VK_TRUE : VK_FALSE;
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo ms = {VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo ds = {VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    ds.depthTestEnable = color_count ? VK_TRUE : VK_FALSE;
    ds.depthWriteEnable = color_count ? VK_TRUE : VK_FALSE;
    ds.depthCompareOp = VK_COMPARE_OP_ALWAYS;
    VkPipelineColorBlendAttachmentState blend[4] = {};
    for (auto& b : blend) b.colorWriteMask = 0xF;
    VkPipelineColorBlendStateCreateInfo cb = {VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    cb.attachmentCount = color_count;
    cb.pAttachments = blend;
    VkGraphicsPipelineCreateInfo gi = {VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    gi.stageCount = 2;
    gi.pStages = stages;
    gi.pVertexInputState = &vin;
    gi.pInputAssemblyState = &ia;
    gi.pViewportState = &vs;
    gi.pRasterizationState = &rs;
    gi.pMultisampleState = &ms;
    gi.pDepthStencilState = &ds;
    gi.pColorBlendState = &cb;
    gi.layout = layout_;
    gi.renderPass = rp;
    VKC(vkCreateGraphicsPipelines(dev_, VK_NULL_HANDLE, 1, &gi, nullptr, &pipe));
    return true;
  }

  void Upload(const Frame& f) {
    std::memcpy(cb_[0].ptr, f.c, 4096);
    std::memcpy(cb_[1].ptr, f.bl, 160);
    std::memcpy(cb_[2].ptr, f.fc, 768);
    std::memcpy(cb_[3].ptr, f.sys, 64);
    std::memcpy(mem_.ptr, f.mem, mem_size_);
  }

  bool Begin(std::string& err) {
    VkCommandBufferBeginInfo bi = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VKC(vkBeginCommandBuffer(cmd_, &bi));
    return true;
  }

  bool Submit(std::string& err) {
    VKC(vkEndCommandBuffer(cmd_));
    VkSubmitInfo si = {VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd_;
    VKC(vkQueueSubmit(queue_, 1, &si, VK_NULL_HANDLE));
    VKC(vkQueueWaitIdle(queue_));
    return true;
  }

  void HostBarrier(VkPipelineStageFlags src_stage, VkAccessFlags src_access) {   // -> lectura desde la CPU
    VkMemoryBarrier mb = {VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    mb.srcAccessMask = src_access;
    mb.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    vkCmdPipelineBarrier(cmd_, src_stage, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &mb, 0, nullptr, 0, nullptr);
  }

  void ImageBarrier(VkImage img, uint32_t layers, VkImageLayout from, VkImageLayout to, VkPipelineStageFlags src_stage,
                    VkAccessFlags src_access, VkPipelineStageFlags dst_stage, VkAccessFlags dst_access) {
    VkImageMemoryBarrier ib = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    ib.srcAccessMask = src_access;
    ib.dstAccessMask = dst_access;
    ib.oldLayout = from;
    ib.newLayout = to;
    ib.srcQueueFamilyIndex = ib.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    ib.image = img;
    ib.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, layers};
    vkCmdPipelineBarrier(cmd_, src_stage, dst_stage, 0, 0, nullptr, 0, nullptr, 1, &ib);
  }
};

}  // namespace

std::unique_ptr<Backend> Backend::Create(uint32_t cases, uint32_t mem_size, const float (*tex_colors)[4],
                                         const std::string& dxc_dll, std::string& err) {
  auto b = std::make_unique<Impl>();
  if (!b->Init(cases, mem_size, tex_colors, dxc_dll, err)) return nullptr;
  return b;
}

}  // namespace re_wor::xenos2hlsl::vkcheck
