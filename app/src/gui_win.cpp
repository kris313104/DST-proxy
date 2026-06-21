// gui_win.cpp — native Win32 GUI for the proximity voice app (no extra deps).
//
// Owns the whole runtime: auto-installs the mod, opens audio, tails the DST log to
// auto-join/leave the voice room, and exposes live controls (mute, mic select, mic
// volume, hearing range, audio direction). All controls drive the already-existing
// VoiceClient/AudioIO hooks; sliders use the thread-safe live setters on VoiceClient.
#include "gui_win.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commctrl.h>

#include "audio_io.h"
#include "log_tailer.h"
#include "mod_installer.h"
#include "prox_parser.h"

#pragma comment(lib, "comctl32.lib")

namespace prox {
namespace {

enum : int {
    IDC_MUTE = 1001,
    IDC_COMBO = 1002,       // microphone (input)
    IDC_GAIN = 1003,
    IDC_RANGE = 1004,
    IDC_PAN = 1005,
    IDC_COMBO_OUT = 1006,   // speakers (output)
    IDT_STATUS = 1,  // refresh timer
};

// Posted by the log thread when the in-game mute hotkey fires; wParam = muted (0/1).
constexpr UINT WM_APP_MUTE = WM_APP + 1;

constexpr int kPanSpan = 180;  // pan slider runs 0..360, displayed as -180..+180

struct Gui {
    VoiceConfig cfg;
    std::string nameOverride;
    int initialMic = -1;

    std::unique_ptr<VoiceClient> voice;
    std::unique_ptr<AudioIO> io;
    std::thread logThread;

    std::atomic<bool> running{true};
    std::atomic<bool> joined{false};
    std::atomic<bool> muted{false};
    bool audioOk = false;

    std::vector<std::string> devices;     // capture devices (combo item i+1 -> devices[i])
    std::vector<std::string> outDevices;  // playback devices (combo item i+1 -> outDevices[i])

    HWND hwnd = nullptr;
    HWND hMod = nullptr, hStatus = nullptr, hMute = nullptr, hCombo = nullptr, hComboOut = nullptr;
    HWND hGain = nullptr, hGainVal = nullptr, hRange = nullptr, hRangeVal = nullptr;
    HWND hPan = nullptr, hPanVal = nullptr;
    HFONT font = nullptr;
};

HWND makeLabel(HWND parent, const char* text, int x, int y, int w, int h, HFONT font) {
    HWND s = CreateWindowExA(0, "STATIC", text, WS_CHILD | WS_VISIBLE, x, y, w, h, parent, nullptr,
                             nullptr, nullptr);
    SendMessageA(s, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    return s;
}

HWND makeTrackbar(HWND parent, int id, int x, int y, int w, int h, int lo, int hi, int pos,
                  HFONT font) {
    HWND t = CreateWindowExA(0, TRACKBAR_CLASSA, "",
                             WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_NOTICKS, x, y, w, h, parent,
                             reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), nullptr, nullptr);
    SendMessageA(t, TBM_SETRANGE, TRUE, MAKELPARAM(lo, hi));
    SendMessageA(t, TBM_SETPOS, TRUE, pos);
    SendMessageA(t, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    return t;
}

void updateSliderLabels(Gui* g) {
    char buf[64];
    int gain = static_cast<int>(SendMessageA(g->hGain, TBM_GETPOS, 0, 0));
    std::snprintf(buf, sizeof(buf), "%d%%", gain);
    SetWindowTextA(g->hGainVal, buf);

    int range = static_cast<int>(SendMessageA(g->hRange, TBM_GETPOS, 0, 0));
    std::snprintf(buf, sizeof(buf), "%d", range);
    SetWindowTextA(g->hRangeVal, buf);

    int pan = static_cast<int>(SendMessageA(g->hPan, TBM_GETPOS, 0, 0)) - kPanSpan;
    std::snprintf(buf, sizeof(buf), "%d deg", pan);
    SetWindowTextA(g->hPanVal, buf);
}

void applySliders(Gui* g) {
    if (!g->voice) return;
    int gain = static_cast<int>(SendMessageA(g->hGain, TBM_GETPOS, 0, 0));
    int range = static_cast<int>(SendMessageA(g->hRange, TBM_GETPOS, 0, 0));
    int pan = static_cast<int>(SendMessageA(g->hPan, TBM_GETPOS, 0, 0)) - kPanSpan;
    g->voice->setMicGain(gain / 100.0);
    g->voice->setFar(static_cast<double>(range));
    g->voice->setPanOffset(static_cast<double>(pan));
    updateSliderLabels(g);
}

// Apply a mute state. UI-thread only (touches the button text). Used by the Mute
// button and, via WM_APP_MUTE, by the in-game hotkey relayed from the log thread.
void applyMute(Gui* g, bool m) {
    g->muted.store(m);
    if (g->voice) g->voice->setTransmitting(!m);
    SetWindowTextA(g->hMute, m ? "Unmute" : "Mute");
}

// Restart audio with the devices currently selected in both combos (item 0 = default = -1).
void restartAudio(Gui* g) {
    int cap = static_cast<int>(SendMessageA(g->hCombo, CB_GETCURSEL, 0, 0)) - 1;
    int play = static_cast<int>(SendMessageA(g->hComboOut, CB_GETCURSEL, 0, 0)) - 1;
    g->io->stop();
    g->io->set_capture_device(cap);
    g->io->set_playback_device(play);
    g->audioOk = g->io->start();
}

void startLogThread(Gui* g) {
    g->logThread = std::thread([g]() {
        std::optional<std::filesystem::path> logPath = find_client_log();
        if (!logPath) return;  // status stays "waiting"; nothing to tail yet
        LogTailer tailer(*logPath, /*from_end=*/true);
        std::string curRoom;
        auto lastProx = std::chrono::steady_clock::now();
        bool inWorld = false;
        while (g->running.load()) {
            for (auto& line : tailer.poll()) {
                Command cmd;
                if (parse_command(line, cmd)) {
                    if (cmd.mute >= 0)  // relay to the UI thread (button text + transmit)
                        PostMessageA(g->hwnd, WM_APP_MUTE, cmd.mute != 0 ? 1 : 0, 0);
                    continue;
                }
                Update u;
                Ids ids;
                if (parse_line(line, u, ids) == LineKind::Update && !u.uid.empty() &&
                    !u.room.empty()) {
                    g->voice->setLocalPosition(u);
                    lastProx = std::chrono::steady_clock::now();
                    if (u.room != curRoom) {  // entered / switched world -> (re)join
                        if (!curRoom.empty()) g->voice->stop();
                        const std::string nm =
                            !g->nameOverride.empty()
                                ? g->nameOverride
                                : (u.name.empty() ? std::string("Player") : u.name);
                        g->voice->start(u.room, u.uid, nm);
                        // Re-apply mute state in case the user toggled it while disconnected.
                        g->voice->setTransmitting(!g->muted.load());
                        curRoom = u.room;
                        g->joined.store(true);
                        inWorld = true;
                    }
                }
            }
            if (inWorld &&
                std::chrono::steady_clock::now() - lastProx > std::chrono::seconds(3)) {
                g->voice->stop();
                curRoom.clear();
                g->joined.store(false);
                inWorld = false;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    });
}

void buildControls(HWND hwnd, Gui* g) {
    g->font = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    const int M = 14;       // left margin
    const int W = 452;      // inner content width
    const int SLIDER = 300;
    int y = 12;

    g->hMod = makeLabel(hwnd, "Mod: checking...", M, y, W, 34, g->font);
    y += 40;
    g->hStatus = makeLabel(hwnd, "Starting...", M, y, W, 22, g->font);
    y += 32;

    g->hMute = CreateWindowExA(0, "BUTTON", "Mute", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, M, y, 150,
                               32, hwnd, reinterpret_cast<HMENU>(IDC_MUTE), nullptr, nullptr);
    SendMessageA(g->hMute, WM_SETFONT, reinterpret_cast<WPARAM>(g->font), TRUE);
    y += 46;

    makeLabel(hwnd, "Microphone:", M, y + 4, 90, 20, g->font);
    g->hCombo = CreateWindowExA(0, "COMBOBOX", "",
                                WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL, M + 96, y,
                                W - 96, 240, hwnd, reinterpret_cast<HMENU>(IDC_COMBO), nullptr,
                                nullptr);
    SendMessageA(g->hCombo, WM_SETFONT, reinterpret_cast<WPARAM>(g->font), TRUE);
    SendMessageA(g->hCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>("Default microphone"));
    for (auto& d : g->devices)
        SendMessageA(g->hCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(d.c_str()));
    int sel = (g->initialMic >= 0 && g->initialMic < static_cast<int>(g->devices.size()))
                  ? g->initialMic + 1
                  : 0;
    SendMessageA(g->hCombo, CB_SETCURSEL, sel, 0);
    y += 40;

    makeLabel(hwnd, "Speakers:", M, y + 4, 90, 20, g->font);
    g->hComboOut = CreateWindowExA(0, "COMBOBOX", "",
                                   WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL, M + 96, y,
                                   W - 96, 240, hwnd, reinterpret_cast<HMENU>(IDC_COMBO_OUT), nullptr,
                                   nullptr);
    SendMessageA(g->hComboOut, WM_SETFONT, reinterpret_cast<WPARAM>(g->font), TRUE);
    SendMessageA(g->hComboOut, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>("Default speakers"));
    for (auto& d : g->outDevices)
        SendMessageA(g->hComboOut, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(d.c_str()));
    SendMessageA(g->hComboOut, CB_SETCURSEL, 0, 0);
    y += 40;

    makeLabel(hwnd, "Mic volume", M, y, 110, 20, g->font);
    g->hGainVal = makeLabel(hwnd, "", M + SLIDER + 16, y, 80, 20, g->font);
    y += 20;
    g->hGain = makeTrackbar(hwnd, IDC_GAIN, M, y, SLIDER, 30, 0, 200,
                            static_cast<int>(g->cfg.mic_gain * 100.0 + 0.5), g->font);
    y += 40;

    makeLabel(hwnd, "Hearing range", M, y, 110, 20, g->font);
    g->hRangeVal = makeLabel(hwnd, "", M + SLIDER + 16, y, 80, 20, g->font);
    y += 20;
    g->hRange = makeTrackbar(hwnd, IDC_RANGE, M, y, SLIDER, 30, 5, 80,
                             static_cast<int>(g->cfg.falloff.far_r + 0.5), g->font);
    y += 40;

    makeLabel(hwnd, "Audio direction", M, y, 110, 20, g->font);
    g->hPanVal = makeLabel(hwnd, "", M + SLIDER + 16, y, 80, 20, g->font);
    y += 20;
    g->hPan = makeTrackbar(hwnd, IDC_PAN, M, y, SLIDER, 30, 0, 2 * kPanSpan,
                           static_cast<int>(g->cfg.pan_offset + 0.5) + kPanSpan, g->font);

    updateSliderLabels(g);
}

void refreshStatus(Gui* g) {
    if (!g->audioOk) {
        SetWindowTextA(g->hStatus, "Microphone or speakers unavailable - check your audio devices.");
        return;
    }
    if (!g->joined.load()) {
        SetWindowTextA(g->hStatus, "Waiting for you to join a DST world...");
        return;
    }
    char buf[96];
    std::snprintf(buf, sizeof(buf), "In voice. Players connected nearby: %d",
                  g->voice->connectedCount());
    SetWindowTextA(g->hStatus, buf);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    Gui* g = reinterpret_cast<Gui*>(GetWindowLongPtrA(hwnd, GWLP_USERDATA));
    switch (msg) {
        case WM_COMMAND: {
            if (!g) break;
            const int id = LOWORD(wParam);
            if (id == IDC_MUTE) {
                applyMute(g, !g->muted.load());
            } else if ((id == IDC_COMBO || id == IDC_COMBO_OUT) &&
                       HIWORD(wParam) == CBN_SELCHANGE && g->io) {
                restartAudio(g);  // re-open audio with both selected devices
                refreshStatus(g);
            }
            return 0;
        }
        case WM_HSCROLL: {
            if (g) applySliders(g);
            return 0;
        }
        case WM_APP_MUTE: {  // in-game mute hotkey relayed from the log thread
            if (g) applyMute(g, wParam != 0);
            return 0;
        }
        case WM_TIMER: {
            if (g && wParam == IDT_STATUS) refreshStatus(g);
            return 0;
        }
        case WM_CTLCOLORSTATIC: {
            // Transparent label backgrounds over the dialog face.
            SetBkMode(reinterpret_cast<HDC>(wParam), TRANSPARENT);
            return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_BTNFACE));
        }
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY: {
            if (g) {
                KillTimer(hwnd, IDT_STATUS);
                g->running.store(false);
                if (g->logThread.joinable()) g->logThread.join();
                if (g->io) g->io->stop();
                if (g->voice) g->voice->stop();
            }
            PostQuitMessage(0);
            return 0;
        }
    }
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

}  // namespace

int run_gui(VoiceConfig cfg, const std::string& nameOverride, int micIndex) {
    auto g = std::make_unique<Gui>();
    g->cfg = std::move(cfg);
    g->nameOverride = nameOverride;
    g->initialMic = micIndex;
    g->devices = AudioIO::list_capture_devices();
    g->outDevices = AudioIO::list_playback_devices();

    // Auto-install the bundled mod (best-effort) so we can show the result in the UI.
    std::string modMsg;
    const bool modOk = auto_install_mod(modMsg);

    // Hide the console window the (console-subsystem) exe spawns on a double-click launch,
    // but NOT a shared terminal (if a dev runs it from cmd/powershell, >1 process attached).
    {
        DWORD pids[2];
        if (GetConsoleProcessList(pids, 2) <= 1) {
            if (HWND con = GetConsoleWindow()) ShowWindow(con, SW_HIDE);
        }
    }

    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_BAR_CLASSES | ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&icc);

    const char* kClass = "ProxVoiceWindow";
    WNDCLASSEXA wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandleA(nullptr);
    wc.hCursor = LoadCursorA(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    wc.lpszClassName = kClass;
    wc.hIcon = LoadIconA(nullptr, IDI_APPLICATION);
    RegisterClassExA(&wc);

    const DWORD style = (WS_OVERLAPPEDWINDOW & ~(WS_THICKFRAME | WS_MAXIMIZEBOX)) | WS_VISIBLE;
    RECT rc{0, 0, 500, 474};
    AdjustWindowRect(&rc, style, FALSE);
    HWND hwnd = CreateWindowExA(0, kClass, "zenx proximity voice", style, CW_USEDEFAULT,
                                CW_USEDEFAULT, rc.right - rc.left, rc.bottom - rc.top, nullptr,
                                nullptr, wc.hInstance, nullptr);
    if (!hwnd) return 1;
    g->hwnd = hwnd;
    SetWindowLongPtrA(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(g.get()));

    buildControls(hwnd, g.get());
    SetWindowTextA(g->hMod,
                   modOk ? (modMsg + "  Enable it once: DST -> Mods -> Client Mods.").c_str()
                         : ("Mod: " + modMsg).c_str());

    // Wire and start audio.
    g->voice = std::make_unique<VoiceClient>(g->cfg);
    g->io = std::make_unique<AudioIO>(g->cfg.sample_rate, g->cfg.frame_size);
    VoiceClient* vc = g->voice.get();
    g->io->set_on_frame([vc](const int16_t* pcm, int n) { vc->onCaptureFrame(pcm, n); });
    g->io->set_on_render([vc](float* out, int n) { vc->render(out, n); });
    if (micIndex >= 0) g->io->set_capture_device(micIndex);
    g->audioOk = g->io->start();
    applySliders(g.get());  // push initial slider values into the voice client

    startLogThread(g.get());

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);
    SetTimer(hwnd, IDT_STATUS, 500, nullptr);
    refreshStatus(g.get());

    MSG m;
    while (GetMessageA(&m, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageA(hwnd, &m)) {
            TranslateMessage(&m);
            DispatchMessageA(&m);
        }
    }

    // Safety net: WM_DESTROY normally tears down, but if the loop exits otherwise,
    // make sure the log thread is stopped before Gui is destroyed (joinable thread = terminate).
    g->running.store(false);
    if (g->logThread.joinable()) g->logThread.join();
    return 0;
}

}  // namespace prox
