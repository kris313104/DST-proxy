// main_voice.cpp — the proximity voice app.
//
// Intended end-user experience: just run prox_voice.exe and play. On startup it:
//   1. auto-installs the bundled mod into your DST (Steam) install,
//   2. connects to the built-in signaling server,
//   3. waits until you're in a DST world, then auto-joins that world's voice room,
//   4. plays everyone's voice with volume based on in-game distance.
// The only manual step (Klei limitation) is enabling the mod once: DST -> Mods -> Client Mods.
//
// Dev/test flags still exist: --sim (no game), --verbose, --signal, --name, range/PTT, etc.
#include "audio_io.h"
#include "log_tailer.h"
#include "mod_installer.h"
#include "prox_parser.h"
#include "voice_client.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <system_error>
#include <thread>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace {

// >>> Hosted signaling server. Replace with your public server for zero-config end users,
//     e.g. "wss://prox.zenx.today". Overridable via PROX_SIGNAL env or --signal.
const char* kDefaultSignaling = "ws://127.0.0.1:8080";

std::string argval(int argc, char** argv, const char* key, const std::string& def) {
    for (int i = 1; i < argc - 1; ++i)
        if (std::strcmp(argv[i], key) == 0) return argv[i + 1];
    return def;
}
bool hasflag(int argc, char** argv, const char* key) {
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], key) == 0) return true;
    return false;
}

}  // namespace

int main(int argc, char** argv) {
    const bool sim = hasflag(argc, argv, "--sim");
    const bool verbose = hasflag(argc, argv, "--verbose");
    const int runSecs = std::atoi(argval(argc, argv, "--seconds", "0").c_str());
    const std::string nameArg = argval(argc, argv, "--name", "");

    prox::VoiceConfig cfg;
    const char* envSig = std::getenv("PROX_SIGNAL");
    cfg.signaling_url =
        argval(argc, argv, "--signal", envSig ? std::string(envSig) : std::string(kDefaultSignaling));
    cfg.falloff.near_r = std::atof(argval(argc, argv, "--near", "8").c_str());
    cfg.falloff.far_r = std::atof(argval(argc, argv, "--far", "30").c_str());
    cfg.falloff.exponent = std::atof(argval(argc, argv, "--exp", "1").c_str());
    cfg.gate_rms = std::atof(argval(argc, argv, "--gate", "0").c_str());
    cfg.pan_amount = std::atof(argval(argc, argv, "--pan", "1").c_str());
    cfg.join_token = argval(argc, argv, "--token", "");
    if (hasflag(argc, argv, "--no-denoise")) cfg.denoise = false;
    if (hasflag(argc, argv, "--no-agc")) cfg.agc = false;
    if (hasflag(argc, argv, "--no-skip-far")) cfg.skip_far_audio = false;

    // Use cacert.pem shipped next to the exe to TLS-verify wss:// signaling (if present).
#ifdef _WIN32
    {
        char buf[MAX_PATH];
        DWORD n = GetModuleFileNameA(nullptr, buf, MAX_PATH);
        if (n > 0 && n < MAX_PATH) {
            std::filesystem::path ca =
                std::filesystem::path(std::string(buf, n)).parent_path() / "cacert.pem";
            std::error_code ec;
            if (std::filesystem::exists(ca, ec)) cfg.ca_bundle_file = ca.string();
        }
    }
#endif

    // 1. Auto-install the mod (skip in --sim dev mode).
    if (!sim) {
        std::string msg;
        const bool ok = prox::auto_install_mod(msg);
        std::cout << "[mod] " << msg << "\n";
        if (ok)
            std::cout << "[mod] Enable it once: DST -> Mods -> Client Mods -> "
                         "\"Proximity Voice Export\" -> Enable.\n";
    }
    std::cout << "[net] signaling: " << cfg.signaling_url << "\n";
    std::cout << "Tip: use headphones to avoid echo.\n\n";

    prox::VoiceClient voice(cfg);

    prox::AudioIO io(cfg.sample_rate, cfg.frame_size);
    io.set_on_frame([&](const int16_t* pcm, int n) { voice.onCaptureFrame(pcm, n); });
    io.set_on_render([&](float* out, int n) { voice.render(out, n); });
    if (!io.start()) std::cerr << "warning: audio devices failed to start\n";

    std::atomic<bool> joined{false};
    std::atomic<bool> running{true};
    std::thread logThread;

    if (sim) {
        const std::string room = argval(argc, argv, "--room", "testroom");
        const std::string uid = argval(argc, argv, "--uid", "sim");
        prox::Update u;
        u.uid = uid;
        u.name = nameArg.empty() ? "SimPlayer" : nameArg;
        u.room = room;
        u.x = std::atof(argval(argc, argv, "--x", "0").c_str());
        u.z = std::atof(argval(argc, argv, "--z", "0").c_str());
        voice.setLocalPosition(u);
        voice.start(room, uid, u.name);
        joined = true;
        std::cout << "[sim] uid=" << uid << " room=" << room << " pos=(" << u.x << ", " << u.z << ")\n";
    } else {
        const std::string logArg = argval(argc, argv, "--log", "");
        std::optional<std::filesystem::path> logPath =
            logArg.empty() ? prox::find_client_log()
                           : std::optional<std::filesystem::path>(logArg);
        if (!logPath) {
            std::cerr << "client_log.txt not found; start DST once, or pass --log <path>.\n";
        } else {
            logThread = std::thread([&, lp = *logPath]() {
                prox::LogTailer tailer(lp);
                std::string curRoom;
                while (running) {
                    for (auto& line : tailer.poll()) {
                        prox::Update u;
                        prox::Ids ids;
                        if (prox::parse_line(line, u, ids) == prox::LineKind::Update &&
                            !u.uid.empty() && !u.room.empty()) {
                            voice.setLocalPosition(u);
                            if (u.room != curRoom) {  // entered / switched world -> (re)join
                                if (!curRoom.empty()) voice.stop();
                                const std::string nm =
                                    !nameArg.empty() ? nameArg
                                                     : (u.name.empty() ? std::string("Player") : u.name);
                                voice.start(u.room, u.uid, nm);
                                curRoom = u.room;
                                joined = true;
                                std::cout << "[voice] joined your world (room "
                                          << curRoom.substr(0, std::min<size_t>(8, curRoom.size()))
                                          << "...)\n";
                            }
                        }
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
            });
        }
    }

#ifdef _WIN32
    std::thread pttThread;
    {
        const std::string p = argval(argc, argv, "--ptt", "");
        int pttVk = 0;
        if (p == "SPACE") pttVk = VK_SPACE;
        else if (p == "CTRL") pttVk = VK_CONTROL;
        else if (p == "SHIFT") pttVk = VK_SHIFT;
        else if (p.size() == 1) pttVk = std::toupper(static_cast<unsigned char>(p[0]));
        if (pttVk != 0) {
            voice.setTransmitting(false);
            std::cout << "[ptt] hold '" << p << "' to talk\n";
            pttThread = std::thread([&voice, &running, pttVk]() {
                while (running) {
                    voice.setTransmitting((GetAsyncKeyState(pttVk) & 0x8000) != 0);
                    std::this_thread::sleep_for(std::chrono::milliseconds(20));
                }
            });
        }
    }
#endif

    std::thread inputThread;
    if (runSecs <= 0) {
        std::cout << "Running. Press Enter to quit.\n";
        inputThread = std::thread([&]() {
            std::cin.get();
            running = false;
        });
    }

    const auto t0 = std::chrono::steady_clock::now();
    auto lastStatus = t0 - std::chrono::seconds(10);
    while (running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        const auto now = std::chrono::steady_clock::now();
        if (now - lastStatus >= std::chrono::seconds(verbose ? 1 : 2)) {
            lastStatus = now;
            if (verbose) {
                std::cout << voice.statsLine() << std::endl;
            } else if (!joined) {
                std::cout << "Waiting for you to join a DST world (enable the mod in Mods)..."
                          << std::endl;
            } else {
                std::cout << "In voice. Players connected nearby: " << voice.connectedCount()
                          << std::endl;
            }
        }
        if (runSecs > 0 &&
            std::chrono::steady_clock::now() - t0 >= std::chrono::seconds(runSecs)) {
            running = false;
        }
    }

    // Stop the position feed first (so it can't (re)start voice during teardown), then audio, then mesh.
    if (logThread.joinable()) logThread.join();
#ifdef _WIN32
    if (pttThread.joinable()) pttThread.join();
#endif
    io.stop();
    voice.stop();
    if (inputThread.joinable()) inputThread.detach();
    std::cout << "bye\n";
    return 0;
}
