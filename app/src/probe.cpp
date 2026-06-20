// probe.cpp — link/smoke check for the voice toolchain (libdatachannel + opus + miniaudio).
// Proves all three libraries compile, link, and run basic calls. Not part of the app.
#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>

#include <opus.h>
#include <rtc/rtc.hpp>

#include <iostream>

int main() {
    std::cout << "opus version: " << opus_get_version_string() << "\n";

    // libdatachannel: creating a PeerConnection proves the lib links and initializes.
    rtc::Configuration config;
    rtc::PeerConnection pc(config);
    std::cout << "libdatachannel: PeerConnection created, state="
              << static_cast<int>(pc.state()) << "\n";

    // miniaudio: enumerate devices.
    ma_context context;
    if (ma_context_init(nullptr, 0, nullptr, &context) == MA_SUCCESS) {
        ma_device_info* playback = nullptr;
        ma_uint32 playbackCount = 0;
        ma_device_info* capture = nullptr;
        ma_uint32 captureCount = 0;
        if (ma_context_get_devices(&context, &playback, &playbackCount, &capture,
                                   &captureCount) == MA_SUCCESS) {
            std::cout << "miniaudio: " << playbackCount << " playback, " << captureCount
                      << " capture device(s)\n";
        }
        ma_context_uninit(&context);
    } else {
        std::cout << "miniaudio: context init failed\n";
    }

    std::cout << "PROBE OK\n";
    return 0;
}
