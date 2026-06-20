// mod_installer.h — auto-install the bundled DST mod into the Steam install.
#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace prox {

// Locate the DST "mods" directory inside the Steam install (Windows). nullopt if not found.
std::optional<std::filesystem::path> find_dst_mods_dir();

// Write the bundled mod into <modsDir>/proximity_voice_export. outMsg gets a human message.
bool install_mod(const std::filesystem::path& modsDir, std::string& outMsg);

// Convenience: find DST + install. Returns true on success.
bool auto_install_mod(std::string& outMsg);

}  // namespace prox
