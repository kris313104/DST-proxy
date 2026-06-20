// log_tailer.h
// Namierzenie i tailowanie client_log.txt DST (Faza 1).
#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace prox {

// Próbuje znaleźć client_log.txt:
//   %USERPROFILE%/Documents/Klei/DoNotStarveTogether/client_log.txt
// a jeśli nie ma — w podfolderach z numerem (Steam ID), wybierając najnowszy.
// Zwraca pustą wartość, jeśli nie znaleziono.
std::optional<std::filesystem::path> find_client_log();

// Tailer: zwraca nowe, kompletne linie dopisane do pliku od ostatniego poll().
// Obsługuje obcięcie/rotację pliku (restart gry) — resetuje pozycję, gdy plik się skurczy.
class LogTailer {
public:
    explicit LogTailer(std::filesystem::path path);

    std::vector<std::string> poll();

    const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
    std::uintmax_t offset_ = 0;  // ile bajtów już skonsumowano
    std::string partial_;        // niedokończona ostatnia linia
};

}  // namespace prox
