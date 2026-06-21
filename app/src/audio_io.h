// audio_io.h — microphone capture + speaker playback via miniaudio (PIMPL).
//
// Capture: mono int16 @ sample_rate, delivered in fixed `capture_frame` chunks (for Opus).
// Playback: interleaved stereo float @ sample_rate; the render callback fills it.
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace prox {

struct AudioIOImpl;  // defined in audio_io.cpp (keeps miniaudio out of this header)

class AudioIO {
public:
    using FrameCallback = std::function<void(const int16_t* mono, int frames)>;
    using RenderCallback = std::function<void(float* stereo, int frames)>;

    explicit AudioIO(int sample_rate = 48000, int capture_frame = 960);
    ~AudioIO();

    AudioIO(const AudioIO&) = delete;
    AudioIO& operator=(const AudioIO&) = delete;

    void set_on_frame(FrameCallback cb);    // called per capture_frame of mic audio
    void set_on_render(RenderCallback cb);  // called to fill the playback buffer

    // Choose the capture (microphone) device by index (see list_capture_devices). Call before start().
    void set_capture_device(int index);
    // Choose the playback (speakers) device by index (see list_playback_devices). Call before start().
    void set_playback_device(int index);

    // Names of available devices, index-aligned with set_capture_device / set_playback_device.
    static std::vector<std::string> list_capture_devices();
    static std::vector<std::string> list_playback_devices();

    bool start();  // false if no usable device started
    void stop();

    bool capture_running() const;
    bool playback_running() const;
    int sample_rate() const;
    int capture_frame() const;

private:
    std::unique_ptr<AudioIOImpl> impl_;
};

}  // namespace prox
