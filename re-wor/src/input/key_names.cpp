#include "input/key_names.hpp"

#include <cctype>
#include <cstdio>

namespace re_wor::input {

namespace {

struct NamedKey {
  const char* name;
  uint16_t vk;
};

// Nombres compatibles con los keybinds del SDK (rex MnK) cuando es posible.
constexpr NamedKey kNamedKeys[] = {
    {"Backspace", 0x08}, {"Tab", 0x09},       {"Return", 0x0D},     {"Enter", 0x0D},
    {"Shift", 0x10},     {"Control", 0x11},   {"Ctrl", 0x11},       {"Alt", 0x12},
    {"Pause", 0x13},     {"CapsLock", 0x14},  {"Escape", 0x1B},     {"Esc", 0x1B},
    {"Space", 0x20},     {"PageUp", 0x21},    {"PageDown", 0x22},   {"End", 0x23},
    {"Home", 0x24},      {"Left", 0x25},      {"Up", 0x26},         {"Right", 0x27},
    {"Down", 0x28},      {"Insert", 0x2D},    {"Delete", 0x2E},     {"Numpad0", 0x60},
    {"Numpad1", 0x61},   {"Numpad2", 0x62},   {"Numpad3", 0x63},    {"Numpad4", 0x64},
    {"Numpad5", 0x65},   {"Numpad6", 0x66},   {"Numpad7", 0x67},    {"Numpad8", 0x68},
    {"Numpad9", 0x69},   {"Multiply", 0x6A},  {"Add", 0x6B},        {"Subtract", 0x6D},
    {"Decimal", 0x6E},   {"Divide", 0x6F},    {"LShift", 0xA0},     {"RShift", 0xA1},
    {"LControl", 0xA2},  {"RControl", 0xA3},  {"LAlt", 0xA4},       {"RAlt", 0xA5},
    {"Semicolon", 0xBA}, {"Equals", 0xBB},    {"Comma", 0xBC},      {"Minus", 0xBD},
    {"Period", 0xBE},    {"Slash", 0xBF},     {"Backtick", 0xC0},   {"LBracket", 0xDB},
    {"Backslash", 0xDC}, {"RBracket", 0xDD},  {"Quote", 0xDE},
    // Resto del teclado (ISO / espanol: la tecla "< >" es Oem102).
    {"Oem102", 0xE2},    {"NumLock", 0x90},   {"ScrollLock", 0x91}, {"PrintScreen", 0x2C},
    {"Apps", 0x5D},      {"Clear", 0x0C},     {"Separator", 0x6C},  {"NumpadEquals", 0x92},
    {"LWin", 0x5B},      {"RWin", 0x5C},
};

bool EqualsNoCase(std::string_view a, std::string_view b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); i++) {
    if (std::tolower(static_cast<unsigned char>(a[i])) !=
        std::tolower(static_cast<unsigned char>(b[i]))) {
      return false;
    }
  }
  return true;
}

std::string_view Trim(std::string_view s) {
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
  return s;
}

}  // namespace

uint16_t KeyNameToVk(std::string_view name) {
  name = Trim(name);
  if (name.empty()) return 0;
  // Letra o digito.
  if (name.size() == 1) {
    char c = static_cast<char>(std::toupper(static_cast<unsigned char>(name[0])));
    if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) return static_cast<uint16_t>(c);
  }
  // F1..F24
  if ((name[0] == 'F' || name[0] == 'f') && name.size() <= 3) {
    int n = 0;
    for (size_t i = 1; i < name.size(); i++) {
      if (!std::isdigit(static_cast<unsigned char>(name[i]))) {
        n = -1;
        break;
      }
      n = n * 10 + (name[i] - '0');
    }
    if (n >= 1 && n <= 24) return static_cast<uint16_t>(0x70 + n - 1);
  }
  for (const auto& k : kNamedKeys) {
    if (EqualsNoCase(name, k.name)) return k.vk;
  }
  return 0;
}

std::string VkToKeyName(uint16_t vk) {
  if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9')) {
    return std::string(1, static_cast<char>(vk));
  }
  if (vk >= 0x70 && vk <= 0x87) {
    return "F" + std::to_string(vk - 0x70 + 1);
  }
  for (const auto& k : kNamedKeys) {
    // Saltar alias (Enter/Ctrl/Esc) para devolver el nombre canonico.
    if (k.vk == vk) return k.name;
  }
  char buf[16];
  std::snprintf(buf, sizeof(buf), "VK_0x%02X", vk);
  return buf;
}

std::vector<uint16_t> ParseKeyList(std::string_view list) {
  std::vector<uint16_t> out;
  while (!list.empty()) {
    size_t comma = list.find(',');
    std::string_view token = list.substr(0, comma);
    if (uint16_t vk = KeyNameToVk(token)) out.push_back(vk);
    if (comma == std::string_view::npos) break;
    list.remove_prefix(comma + 1);
  }
  return out;
}

}  // namespace re_wor::input
