// Lectura de las caches de shaders del SDK (cache/shaders/shareable/*.xsh): cabecera 'XESH' + version, y por
// shader uint64 hash, uint32 (palabras | tipo << 31) y el microcodigo en big endian.
#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <vector>

namespace re_wor::xenos2hlsl {

struct XshEntry {
  uint64_t hash;
  bool pixel;
  std::vector<uint32_t> dwords;
};

inline bool ReadXsh(const std::filesystem::path& p, std::vector<XshEntry>& out) {
  std::ifstream f(p, std::ios::binary);
  if (!f) return false;
  std::vector<uint8_t> b((std::istreambuf_iterator<char>(f)), {});
  if (b.size() < 8 || std::memcmp(b.data(), "XESH", 4) != 0) {
    std::fprintf(stderr, "%s: no es una cache de shaders del SDK\n", p.string().c_str());
    return false;
  }
  size_t o = 8;
  while (o + 12 <= b.size()) {
    XshEntry e;
    uint32_t w;
    std::memcpy(&e.hash, &b[o], 8);
    std::memcpy(&w, &b[o + 8], 4);
    const uint32_t n = w & 0x7FFFFFFFu;
    e.pixel = (w >> 31) != 0;
    o += 12;
    if (o + size_t(n) * 4 > b.size()) break;
    e.dwords.resize(n);
    std::memcpy(e.dwords.data(), &b[o], size_t(n) * 4);
    o += size_t(n) * 4;
    out.push_back(std::move(e));
  }
  return true;
}

// Todas las caches de un archivo o carpeta, sin repetir shaders (el mismo puede estar en varias).
inline std::vector<XshEntry> ReadXshInput(const std::filesystem::path& in) {
  std::vector<std::filesystem::path> files;
  if (std::filesystem::is_directory(in)) {
    for (const auto& de : std::filesystem::directory_iterator(in))
      if (de.path().extension() == ".xsh") files.push_back(de.path());
  } else {
    files.push_back(in);
  }
  std::vector<XshEntry> all, out;
  for (const auto& f : files) ReadXsh(f, all);
  std::map<std::pair<uint64_t, bool>, bool> seen;
  for (auto& e : all)
    if (seen.emplace(std::make_pair(e.hash, e.pixel), true).second) out.push_back(std::move(e));
  return out;
}

}  // namespace re_wor::xenos2hlsl
