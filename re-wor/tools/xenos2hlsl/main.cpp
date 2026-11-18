// xenos2hlsl: traduce fuera de linea los shaders de la 360 capturados por el SDK (cache/shaders/shareable/*.xsh)
// a HLSL para el render nativo (docs/RENDER_NATIVO.md, fase 1).
//
// Uso: xenos2hlsl <entrada.xsh | carpeta> <carpeta de salida>
// Salida: <hash>.vs.hlsl / <hash>.ps.hlsl, xe_common.hlsli y manifest.json. Lo generado sale del juego: no va a git.
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include <fmt/format.h>

#include <rex/graphics/flags.h>
#include <rex/graphics/pipeline/shader/shader.h>
#include <rex/string/buffer.h>

#include "hlsl_translator.h"
#include "xsh.h"

// El analizador del SDK solo lee este cvar (graphics/flags.cpp arrastra RenderDoc y el resto de la GPU).
REXCVAR_DEFINE_STRING(dump_shaders, "", "GPU", "Path to dump shaders to");

namespace fs = std::filesystem;
using namespace rex::graphics;

namespace {

std::string JsonEscape(const std::string& s) {
  std::string o;
  for (char c : s) {
    if (c == '"' || c == '\\') { o += '\\'; o += c; }
    else if (c == '\n') o += "\\n";
    else if (uint8_t(c) < 0x20) o += fmt::format("\\u{:04x}", uint8_t(c));
    else o += c;
  }
  return o;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 3) {
    fmt::print(stderr, "uso: xenos2hlsl <entrada.xsh | carpeta> <carpeta de salida>\n");
    return 2;
  }
  const fs::path in = argv[1], out = argv[2];
  const auto entries = re_wor::xenos2hlsl::ReadXshInput(in);
  fs::create_directories(out);
  {
    std::ofstream h(out / "xe_common.hlsli", std::ios::binary);
    h << re_wor::xenos2hlsl::HlslTranslator::CommonHeader();
  }

  re_wor::xenos2hlsl::HlslTranslator translator;
  rex::string::StringBuffer disasm;
  std::string manifest = "{\n \"shaders\": [\n";
  uint32_t ok = 0, failed = 0, first = 1;
  for (const auto& e : entries) {
    Shader shader(e.pixel ? xenos::ShaderType::kPixel : xenos::ShaderType::kVertex, e.hash, e.dwords.data(),
                  e.dwords.size());
    shader.AnalyzeUcode(disasm);
    Shader::Translation* t = shader.GetOrCreateTranslation(0);
    const bool good = translator.TranslateAnalyzedShader(*t) && t->is_valid();
    const auto& rep = translator.report();
    const std::string name = fmt::format("{:016X}.{}.hlsl", e.hash, e.pixel ? "ps" : "vs");
    std::string errs;
    for (const auto& er : t->errors()) errs += (errs.empty() ? "\"" : ", \"") + JsonEscape(er.message) + "\"";
    for (const auto& n : rep.notes) errs += (errs.empty() ? "\"" : ", \"") + JsonEscape(n) + "\"";
    if (good && !rep.uses_calls) {
      std::ofstream o(out / name, std::ios::binary);
      const auto& bin = t->translated_binary();
      o.write(reinterpret_cast<const char*>(bin.data()), std::streamsize(bin.size()));
      // desensamblado de la 360 al lado, para comparar a mano
      std::ofstream d(out / fmt::format("{:016X}.{}.ucode.txt", e.hash, e.pixel ? "ps" : "vs"), std::ios::binary);
      d << shader.ucode_disassembly();
      ++ok;
    } else {
      ++failed;
    }
    std::string tex;
    for (const auto& b : shader.texture_bindings())
      tex += fmt::format("{}[{}, {}]", tex.empty() ? "" : ", ", b.fetch_constant, uint32_t(b.fetch_instr.dimension));
    std::string vtx;
    for (const auto& b : shader.vertex_bindings())
      vtx += fmt::format("{}{{\"fc\": {}, \"stride\": {}, \"attrs\": {}}}", vtx.empty() ? "" : ", ",
                         b.fetch_constant, b.stride_words, b.attributes.size());
    manifest += fmt::format(
        "{}  {{\"hash\": \"{:016X}\", \"type\": \"{}\", \"file\": \"{}\", \"ok\": {}, \"dwords\": {}, "
        "\"registers\": {}, \"dynamic_registers\": {}, \"textures\": [{}], \"vertex_bindings\": [{}], "
        "\"color_targets\": {}, \"writes_depth\": {}, \"kills\": {}, \"switch\": {}, \"loops\": {}, \"calls\": {}, "
        "\"memexport\": {}, \"messages\": [{}]}}",
        first ? "" : ",\n", e.hash, e.pixel ? "ps" : "vs", good ? name : "", good && !rep.uses_calls ? "true" : "false",
        e.dwords.size(), shader.register_static_address_bound(),
        shader.uses_register_dynamic_addressing() ? "true" : "false", tex, vtx, shader.writes_color_targets(),
        shader.writes_depth() ? "true" : "false", shader.kills_pixels() ? "true" : "false",
        rep.uses_switch ? "true" : "false", rep.uses_loops ? "true" : "false", rep.uses_calls ? "true" : "false",
        rep.uses_memexport ? "true" : "false", errs);
    first = 0;
  }
  manifest += fmt::format("\n ],\n \"translated\": {},\n \"failed\": {}\n}}\n", ok, failed);
  std::ofstream(out / "manifest.json", std::ios::binary) << manifest;
  fmt::print("shaders: {} traducidos, {} fallidos -> {}\n", ok, failed, out.string());
  return failed ? 1 : 0;
}
