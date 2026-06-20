// audio_dsp.h — mic preprocessing (noise suppression + AGC) via speexdsp.
#pragma once

#include <cstdint>

namespace prox {

class AudioPreprocessor {
public:
    AudioPreprocessor(int frame_size, int sample_rate, bool denoise = true, bool agc = true);
    ~AudioPreprocessor();

    AudioPreprocessor(const AudioPreprocessor&) = delete;
    AudioPreprocessor& operator=(const AudioPreprocessor&) = delete;

    bool ok() const { return st_ != nullptr; }

    // Process one frame_size int16 frame IN PLACE. Returns true if speech is detected.
    bool process(int16_t* pcm);

private:
    void* st_ = nullptr;  // SpeexPreprocessState (kept opaque to avoid leaking the header)
    int frame_size_;
};

}  // namespace prox
