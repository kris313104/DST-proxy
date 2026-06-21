// prox_parser.h
// Parser linii [PROX] / [PROX-IDS] z client_log.txt (Faza 1).
// Czysta logika, bez I/O — łatwa do testów jednostkowych.
#pragma once

#include <string>
#include <unordered_map>

namespace prox {

// Pozycja lokalnego gracza wyeksportowana przez mod (linia [PROX]).
struct Update {
    int version = 0;
    double t = 0.0;          // czas gry (GetTime) — do porządkowania/interpolacji
    std::string uid;         // KU_... tożsamość gracza
    std::string name;        // nazwa wyświetlana
    double x = 0.0;          // pozycja na płaszczyźnie świata
    double z = 0.0;
    double heading = 0.0;    // heading kamery w stopniach
    std::string room;        // wybrany identyfikator pokoju do parowania
};

// Diagnostyka kandydatów na identyfikator pokoju (linia [PROX-IDS]).
struct Ids {
    std::string sid;          // TheNet:GetSessionIdentifier()
    std::string sid2;         // TheWorld.meta.session_identifier
    std::string srv;          // TheNet:GetServerName()
    std::string chosen_room;  // pierwszy niepusty z powyższych
};

// Komenda sterująca z moda do apki (linia [PROX-CMD]) — kanał gra -> aplikacja.
struct Command {
    int version = 0;
    int mute = -1;  // -1 = nieustawione, 0 = mów (live), 1 = wycisz (muted)
};

enum class LineKind { None, Update, Ids };

// Parsuje pojedynczą linię logu. Toleruje dowolny prefiks (np. znacznik czasu DST
// "[00:01:23]: "), szukając markera [PROX-IDS] / [PROX] gdziekolwiek w linii.
// Zwraca rodzaj i wypełnia odpowiednią strukturę.
LineKind parse_line(const std::string& line, Update& out_update, Ids& out_ids);

// Parsuje linię [PROX-CMD] (kanał komend mod -> apka). Zwraca true, jeśli to była
// taka linia (i wypełnia out). Niezależne od parse_line (markery się nie kolidują).
bool parse_command(const std::string& line, Command& out);

// Pomocniczo (wystawione do testów): wyciąga pary klucz=wartość z fragmentu
// "k1=v1 k2=\"v2\" ...". Wartości w cudzysłowach są odescapowane (\" \\ \<nl>).
std::unordered_map<std::string, std::string> parse_kv(const std::string& s);

}  // namespace prox
