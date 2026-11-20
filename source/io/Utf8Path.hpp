#pragma once

#include <filesystem>
#include <string>

namespace omniframe::io {

/**
 * Converte uma string UTF-8 em std::filesystem::path preservando caracteres
 * Unicode no Windows (CMD, PowerShell e diálogos UTF-16) e POSIX.
 */
inline std::filesystem::path pathFromUtf8(const std::string& utf8Path) {
    const std::u8string u8(reinterpret_cast<const char8_t*>(utf8Path.data()), utf8Path.size());
    return std::filesystem::path(u8);
}

} // namespace omniframe::io
