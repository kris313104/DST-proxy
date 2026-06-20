// audio_dsp.cpp — speexdsp preprocessor (noise suppression + AGC).
#include "audio_dsp.h"

#include <speex/speex_preprocess.h>

namespace prox {

AudioPreprocessor::AudioPreprocessor(int frame_size, int sample_rate, bool denoise, bool agc)
    : frame_size_(frame_size) {
    SpeexPreprocessState* st = speex_preprocess_state_init(frame_size, sample_rate);
    if (st != nullptr) {
        int on;
        on = denoise ? 1 : 0;
        speex_preprocess_ctl(st, SPEEX_PREPROCESS_SET_DENOISE, &on);
        on = agc ? 1 : 0;
        speex_preprocess_ctl(st, SPEEX_PREPROCESS_SET_AGC, &on);
        if (agc) {
            float level = 18000.0f;  // target output level
            speex_preprocess_ctl(st, SPEEX_PREPROCESS_SET_AGC_LEVEL, &level);
        }
    }
    st_ = st;
}

AudioPreprocessor::~AudioPreprocessor() {
    if (st_ != nullptr) speex_preprocess_state_destroy(static_cast<SpeexPreprocessState*>(st_));
}

bool AudioPreprocessor::process(int16_t* pcm) {
    if (st_ == nullptr) return true;
    return speex_preprocess_run(static_cast<SpeexPreprocessState*>(st_),
                                reinterpret_cast<spx_int16_t*>(pcm)) != 0;
}

}  // namespace prox
