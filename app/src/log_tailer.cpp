// log_tailer.cpp — implementacja namierzania i tailowania client_log.txt.
#include "log_tailer.h"

#include <cstdlib>
#include <fstream>
#include <system_error>

namespace fs = std::filesystem;

namespace prox {

namespace {

fs::path dst_dir() {
    // Windows: %USERPROFILE%\Documents\Klei\DoNotStarveTogether
    const char* up = std::getenv("USERPROFILE");
    if (up == nullptr) up = std::getenv("HOME");  // awaryjnie (inne OS)
    fs::path base = up ? fs::path(up) : fs::current_path();
    return base / "Documents" / "Klei" / "DoNotStarveTogether";
}

}  // namespace

std::optional<fs::path> find_client_log() {
    std::error_code ec;
    fs::path dir = dst_dir();

    fs::path direct = dir / "client_log.txt";
    if (fs::exists(direct, ec)) return direct;

    if (!fs::is_directory(dir, ec)) return std::nullopt;

    // Szukaj w podfolderach (np. numerycznych = Steam ID) najnowszego client_log.txt.
    std::optional<fs::path> best;
    fs::file_time_type best_time{};
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        if (ec) break;
        if (!entry.is_directory()) continue;
        fs::path cand = entry.path() / "client_log.txt";
        if (!fs::exists(cand, ec)) continue;
        auto mt = fs::last_write_time(cand, ec);
        if (ec) continue;
        if (!best || mt > best_time) {
            best = cand;
            best_time = mt;
        }
    }
    return best;
}

LogTailer::LogTailer(fs::path path) : path_(std::move(path)) {}

std::vector<std::string> LogTailer::poll() {
    std::vector<std::string> lines;

    std::error_code ec;
    std::uintmax_t size = fs::file_size(path_, ec);
    if (ec) return lines;  // plik chwilowo niedostępny

    if (size < offset_) {
        // Plik się skurczył (rotacja/restart) — zacznij od początku.
        offset_ = 0;
        partial_.clear();
    }
    if (size == offset_) return lines;  // nic nowego

    std::ifstream in(path_, std::ios::binary);
    if (!in) return lines;
    in.seekg(static_cast<std::streamoff>(offset_), std::ios::beg);

    std::string chunk;
    chunk.resize(static_cast<size_t>(size - offset_));
    in.read(&chunk[0], static_cast<std::streamsize>(chunk.size()));
    chunk.resize(static_cast<size_t>(in.gcount()));
    offset_ += chunk.size();

    partial_ += chunk;

    // Podziel na kompletne linie; ostatni niedokończony fragment zachowaj.
    size_t start = 0;
    for (size_t i = 0; i < partial_.size(); ++i) {
        if (partial_[i] == '\n') {
            std::string line = partial_.substr(start, i - start);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            lines.push_back(std::move(line));
            start = i + 1;
        }
    }
    partial_.erase(0, start);

    return lines;
}

}  // namespace prox
