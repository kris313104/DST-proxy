// mod_installer.cpp — find the DST install and write the bundled mod into it.
#include "mod_installer.h"

#include "mod_assets.h"

#include <fstream>
#include <iterator>
#include <regex>
#include <system_error>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace prox {
namespace {

#ifdef _WIN32
std::string regString(HKEY root, const char* subkey, const char* value) {
    char buf[2048];
    DWORD len = sizeof(buf);
    if (RegGetValueA(root, subkey, value, RRF_RT_REG_SZ, nullptr, buf, &len) == ERROR_SUCCESS)
        return std::string(buf, (len > 0) ? len - 1 : 0);
    return {};
}
#endif

std::vector<fs::path> steamLibraries() {
    std::vector<fs::path> libs;
#ifdef _WIN32
    std::string steam = regString(HKEY_CURRENT_USER, "Software\\Valve\\Steam", "SteamPath");
    if (steam.empty())
        steam = regString(HKEY_LOCAL_MACHINE, "SOFTWARE\\WOW6432Node\\Valve\\Steam", "InstallPath");
    if (steam.empty()) return libs;

    fs::path sp(steam);
    libs.push_back(sp);

    std::ifstream f(sp / "steamapps" / "libraryfolders.vdf");
    if (f) {
        std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        std::regex re("\"path\"\\s*\"([^\"]*)\"");
        for (std::sregex_iterator it(content.begin(), content.end(), re), end; it != end; ++it) {
            std::string p = (*it)[1].str();
            std::string unescaped;  // vdf escapes backslashes as "\\"
            for (size_t i = 0; i < p.size(); ++i) {
                if (p[i] == '\\' && i + 1 < p.size() && p[i + 1] == '\\') {
                    unescaped.push_back('\\');
                    ++i;
                } else {
                    unescaped.push_back(p[i]);
                }
            }
            libs.push_back(fs::path(unescaped));
        }
    }
#endif
    return libs;
}

}  // namespace

std::optional<fs::path> find_dst_mods_dir() {
    std::error_code ec;
    for (auto& lib : steamLibraries()) {
        fs::path dst = lib / "steamapps" / "common" / "Don't Starve Together";
        if (fs::is_directory(dst, ec)) return dst / "mods";
    }
    return std::nullopt;
}

bool install_mod(const fs::path& modsDir, std::string& outMsg) {
    std::error_code ec;
    fs::path root = modsDir / "proximity_voice_export";
    fs::create_directories(root / "scripts", ec);

    auto write = [](const fs::path& p, const char* content) -> bool {
        std::ofstream o(p, std::ios::binary | std::ios::trunc);
        if (!o) return false;
        o << content;
        return o.good();
    };

    const bool ok = write(root / "modinfo.lua", MODINFO_LUA) &&
                    write(root / "modmain.lua", MODMAIN_LUA) &&
                    write(root / "scripts" / "proximity_export.lua", EXPORT_LUA);

    outMsg = ok ? ("mod ready at " + root.string())
                : ("could not write mod to " + root.string());
    return ok;
}

bool auto_install_mod(std::string& outMsg) {
    auto modsDir = find_dst_mods_dir();
    if (!modsDir) {
        outMsg = "DST install not found (copy the mod folder into <DST>/mods manually)";
        return false;
    }
    return install_mod(*modsDir, outMsg);
}

}  // namespace prox
