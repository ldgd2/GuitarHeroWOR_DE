#pragma once
// Conversion entre nombres de tecla legibles ("A", "Space", "F5", "Up")
// y codigos virtual-key de Windows (los que usa rex::ui::VirtualKey).

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace re_wor::input {

// Devuelve 0 si el nombre no se reconoce. No distingue mayusculas.
uint16_t KeyNameToVk(std::string_view name);
// Nombre canonico de la tecla, o "VK_0x.." si no hay nombre.
std::string VkToKeyName(uint16_t vk);
// "A,1, Space" -> {0x41, 0x31, 0x20}. Ignora nombres invalidos.
std::vector<uint16_t> ParseKeyList(std::string_view list);

}  // namespace re_wor::input
