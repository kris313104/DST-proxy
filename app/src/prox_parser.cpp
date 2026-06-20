// prox_parser.cpp — implementacja parsera linii [PROX] / [PROX-IDS].
#include "prox_parser.h"

#include <cctype>
#include <cstdlib>

namespace prox {

namespace {

bool is_key_char(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

// Czyta wartość w cudzysłowie zaczynającą się od s[i]=='"'. Po powrocie i wskazuje
// znak za zamykającym cudzysłowem. Obsługuje escape: \" \\ oraz \<newline> (Lua %q).
std::string read_quoted(const std::string& s, size_t& i) {
    std::string out;
    ++i;  // pomiń otwierający "
    while (i < s.size()) {
        char c = s[i];
        if (c == '\\' && i + 1 < s.size()) {
            char n = s[i + 1];
            if (n == 'n') out.push_back('\n');
            else out.push_back(n);  // \" -> "  \\ -> \  \<nl> -> <nl>
            i += 2;
            continue;
        }
        if (c == '"') {
            ++i;  // pomiń zamykający "
            return out;
        }
        out.push_back(c);
        ++i;
    }
    return out;  // brak zamknięcia — zwróć co mamy
}

// Czyta surowy token (liczba) do najbliższego białego znaku.
std::string read_bare(const std::string& s, size_t& i) {
    size_t start = i;
    while (i < s.size() && !std::isspace(static_cast<unsigned char>(s[i]))) ++i;
    return s.substr(start, i - start);
}

double to_double(const std::unordered_map<std::string, std::string>& kv,
                 const char* key, double def) {
    auto it = kv.find(key);
    if (it == kv.end() || it->second.empty()) return def;
    char* end = nullptr;
    double v = std::strtod(it->second.c_str(), &end);
    if (end == it->second.c_str()) return def;
    return v;
}

int to_int(const std::unordered_map<std::string, std::string>& kv,
           const char* key, int def) {
    auto it = kv.find(key);
    if (it == kv.end() || it->second.empty()) return def;
    return static_cast<int>(std::strtol(it->second.c_str(), nullptr, 10));
}

std::string to_str(const std::unordered_map<std::string, std::string>& kv,
                   const char* key) {
    auto it = kv.find(key);
    return it == kv.end() ? std::string() : it->second;
}

}  // namespace

std::unordered_map<std::string, std::string> parse_kv(const std::string& s) {
    std::unordered_map<std::string, std::string> kv;
    size_t i = 0;
    while (i < s.size()) {
        // pomiń białe znaki
        if (std::isspace(static_cast<unsigned char>(s[i]))) { ++i; continue; }

        // klucz
        size_t kstart = i;
        while (i < s.size() && is_key_char(s[i])) ++i;
        if (i == kstart) { ++i; continue; }  // nie-kluczowy znak, pomiń
        std::string key = s.substr(kstart, i - kstart);

        if (i >= s.size() || s[i] != '=') continue;  // klucz bez '=' — ignoruj
        ++i;  // pomiń '='

        std::string value;
        if (i < s.size() && s[i] == '"') {
            value = read_quoted(s, i);
        } else {
            value = read_bare(s, i);
        }
        kv[key] = value;
    }
    return kv;
}

LineKind parse_line(const std::string& line, Update& out_update, Ids& out_ids) {
    // [PROX-IDS] sprawdzamy przed [PROX] (bo "[PROX-IDS]" zawiera "[PROX").
    const std::string ids_marker = "[PROX-IDS]";
    const std::string upd_marker = "[PROX]";

    size_t p = line.find(ids_marker);
    if (p != std::string::npos) {
        auto kv = parse_kv(line.substr(p + ids_marker.size()));
        out_ids.sid = to_str(kv, "sid");
        out_ids.sid2 = to_str(kv, "sid2");
        out_ids.srv = to_str(kv, "srv");
        out_ids.chosen_room = to_str(kv, "chosen_room");
        return LineKind::Ids;
    }

    p = line.find(upd_marker);
    if (p != std::string::npos) {
        auto kv = parse_kv(line.substr(p + upd_marker.size()));
        out_update.version = to_int(kv, "v", 0);
        out_update.t = to_double(kv, "t", 0.0);
        out_update.uid = to_str(kv, "uid");
        out_update.name = to_str(kv, "name");
        out_update.x = to_double(kv, "x", 0.0);
        out_update.z = to_double(kv, "z", 0.0);
        out_update.heading = to_double(kv, "h", 0.0);
        out_update.room = to_str(kv, "room");
        return LineKind::Update;
    }

    return LineKind::None;
}

}  // namespace prox
