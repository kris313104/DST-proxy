// gui_win.h — native Win32 GUI for the proximity voice app.
//
// Provides a small window with: live status, a Mute/Unmute button, a microphone
// dropdown, and sliders for mic volume, hearing range, and audio direction (pan).
// It owns the full runtime (mod auto-install, audio I/O, log tailing, WebRTC mesh),
// so prox_voice.exe launched without dev flags just shows this window.
#pragma once

#include <string>

#include "voice_client.h"

namespace prox {

// Run the GUI to completion (blocks until the window is closed). Returns the process exit code.
// nameOverride: optional display name (empty = auto from the DST log).
// micIndex: initial capture device index (-1 = system default).
int run_gui(VoiceConfig cfg, const std::string& nameOverride, int micIndex);

}  // namespace prox
