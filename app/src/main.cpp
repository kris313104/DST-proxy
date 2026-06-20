// main.cpp — Phase 1: console viewer for data from client_log.txt.
//
// Tails the DST log, parses [PROX]/[PROX-IDS] lines and prints the LOCAL player's
// position plus candidate room identifiers.
//
// NOTE: a client's log contains ONLY its own position (the mod exports just the local
// player). A multi-player view appears in Phase 2, once apps exchange positions over
// the network. This tool validates the data path and supports the 2-client test
// (run it on both machines and compare room/sid).
//
// Usage:
//   prox_companion [path_to_client_log.txt]
// With no argument it tries to locate the log automatically.

#include "log_tailer.h"
#include "prox_parser.h"

#include <chrono>
#include <cmath>
#include <iostream>
#include <string>
#include <thread>

int main(int argc, char** argv) {
    std::optional<std::filesystem::path> log_path;
    if (argc > 1) {
        log_path = std::filesystem::path(argv[1]);
    } else {
        log_path = prox::find_client_log();
    }

    if (!log_path) {
        std::cerr << "client_log.txt not found. Pass the path as an argument:\n"
                  << "  prox_companion \"%USERPROFILE%\\Documents\\Klei\\DoNotStarveTogether\\client_log.txt\"\n";
        return 1;
    }

    std::cout << "Tailing: " << log_path->string() << "\n";
    std::cout << "Waiting for [PROX] lines (join a game with the mod enabled)...\n\n";

    prox::LogTailer tailer(*log_path);

    using namespace std::chrono;
    using clock = steady_clock;

    prox::Update latest;
    bool pending = false;       // a new update arrived since the last [local] print
    bool had_data = false;
    bool idle_notified = false;
    auto last_print = clock::now();
    auto last_data = clock::now();

    std::string last_uid, last_room;
    double last_x = 0, last_z = 0, last_h = 0;

    auto changed_since_print = [&](const prox::Update& u) {
        return u.uid != last_uid || u.room != last_room ||
               std::fabs(u.x - last_x) > 0.01 || std::fabs(u.z - last_z) > 0.01 ||
               std::fabs(u.heading - last_h) > 0.1;
    };

    for (;;) {
        for (const auto& line : tailer.poll()) {
            prox::Update upd;
            prox::Ids ids;
            switch (prox::parse_line(line, upd, ids)) {
                case prox::LineKind::Update:
                    latest = upd;
                    pending = true;
                    had_data = true;
                    idle_notified = false;
                    last_data = clock::now();
                    break;
                case prox::LineKind::Ids:
                    std::cout << "[ids]   sid=\"" << ids.sid << "\" sid2=\"" << ids.sid2
                              << "\" srv=\"" << ids.srv << "\" chosen=\"" << ids.chosen_room
                              << "\"" << std::endl;
                    break;
                case prox::LineKind::None:
                    break;
            }
        }

        auto now = clock::now();

        // Print position only on real change (throttled to ~3/s) or as a 3 s heartbeat.
        if (pending) {
            bool changed = changed_since_print(latest);
            if ((changed && now - last_print >= milliseconds(300)) ||
                now - last_print >= seconds(3)) {
                std::cout << "[local] room=\"" << latest.room << "\" uid=\"" << latest.uid
                          << "\" name=\"" << latest.name << "\" pos=(" << latest.x << ", "
                          << latest.z << ") heading=" << latest.heading << " t=" << latest.t
                          << std::endl;
                last_print = now;
                last_uid = latest.uid;
                last_room = latest.room;
                last_x = latest.x;
                last_z = latest.z;
                last_h = latest.heading;
            }
            pending = false;
        }

        // One-shot notice when the mod stops emitting (player left the game / closed it).
        if (had_data && !idle_notified && now - last_data > seconds(2)) {
            std::cout << "[idle]  no new positions (player left the game?)" << std::endl;
            idle_notified = true;
        }

        std::this_thread::sleep_for(milliseconds(100));
    }

    return 0;
}
