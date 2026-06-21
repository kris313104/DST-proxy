// audio_io.cpp — miniaudio capture/playback implementation.
#include "audio_io.h"

#include <miniaudio.h>  // declarations only; implementation lives in miniaudio_impl.cpp

#include <vector>

namespace prox {

struct AudioIOImpl {
    int sample_rate;
    int capture_frame;
    AudioIO::FrameCallback on_frame;
    AudioIO::RenderCallback on_render;

    ma_context context{};
    bool contextOk = false;
    ma_device capture{};
    ma_device playback{};
    bool capture_ok = false;
    bool playback_ok = false;

    int captureIndex = -1;       // -1 = default device
    ma_device_id captureId{};
    bool haveCaptureId = false;

    int playbackIndex = -1;      // -1 = default device
    ma_device_id playbackId{};
    bool havePlaybackId = false;

    std::vector<int16_t> accum;  // accumulates mic samples up to capture_frame
    int accum_n = 0;

    AudioIOImpl(int sr, int cf) : sample_rate(sr), capture_frame(cf), accum(cf) {}
};

namespace {

void capture_cb(ma_device* dev, void* /*out*/, const void* input, ma_uint32 frameCount) {
    auto* impl = static_cast<AudioIOImpl*>(dev->pUserData);
    if (impl->on_frame == nullptr || input == nullptr) return;
    const int16_t* in = static_cast<const int16_t*>(input);
    for (ma_uint32 i = 0; i < frameCount; ++i) {
        impl->accum[impl->accum_n++] = in[i];
        if (impl->accum_n >= impl->capture_frame) {
            impl->on_frame(impl->accum.data(), impl->capture_frame);
            impl->accum_n = 0;
        }
    }
}

void playback_cb(ma_device* dev, void* output, const void* /*input*/, ma_uint32 frameCount) {
    auto* impl = static_cast<AudioIOImpl*>(dev->pUserData);
    float* out = static_cast<float*>(output);
    for (ma_uint32 i = 0; i < frameCount * 2; ++i) out[i] = 0.0f;  // stereo, zeroed
    if (impl->on_render != nullptr) impl->on_render(out, static_cast<int>(frameCount));
}

}  // namespace

AudioIO::AudioIO(int sr, int cf) : impl_(std::make_unique<AudioIOImpl>(sr, cf)) {}
AudioIO::~AudioIO() { stop(); }

void AudioIO::set_on_frame(FrameCallback cb) { impl_->on_frame = std::move(cb); }
void AudioIO::set_on_render(RenderCallback cb) { impl_->on_render = std::move(cb); }
void AudioIO::set_capture_device(int index) { impl_->captureIndex = index; }
void AudioIO::set_playback_device(int index) { impl_->playbackIndex = index; }

namespace {
// Enumerate device names for one direction (playback=false -> capture).
std::vector<std::string> list_devices(bool playback) {
    std::vector<std::string> names;
    ma_context ctx;
    if (ma_context_init(nullptr, 0, nullptr, &ctx) != MA_SUCCESS) return names;
    ma_device_info* play = nullptr;
    ma_uint32 nplay = 0;
    ma_device_info* cap = nullptr;
    ma_uint32 ncap = 0;
    if (ma_context_get_devices(&ctx, &play, &nplay, &cap, &ncap) == MA_SUCCESS) {
        if (playback)
            for (ma_uint32 i = 0; i < nplay; ++i) names.emplace_back(play[i].name);
        else
            for (ma_uint32 i = 0; i < ncap; ++i) names.emplace_back(cap[i].name);
    }
    ma_context_uninit(&ctx);
    return names;
}
}  // namespace

std::vector<std::string> AudioIO::list_capture_devices() { return list_devices(false); }
std::vector<std::string> AudioIO::list_playback_devices() { return list_devices(true); }
int AudioIO::sample_rate() const { return impl_->sample_rate; }
int AudioIO::capture_frame() const { return impl_->capture_frame; }
bool AudioIO::capture_running() const { return impl_->capture_ok; }
bool AudioIO::playback_running() const { return impl_->playback_ok; }

bool AudioIO::start() {
    ma_context* ctx = nullptr;
    if (ma_context_init(nullptr, 0, nullptr, &impl_->context) == MA_SUCCESS) {
        impl_->contextOk = true;
        ctx = &impl_->context;
    }

    // Resolve the chosen capture device by index (if any). Reset first so a switch back to
    // the default device (index < 0) doesn't keep reusing a previously resolved device id.
    impl_->haveCaptureId = false;
    if (impl_->contextOk && impl_->captureIndex >= 0) {
        ma_device_info* play = nullptr;
        ma_uint32 nplay = 0;
        ma_device_info* cap = nullptr;
        ma_uint32 ncap = 0;
        if (ma_context_get_devices(&impl_->context, &play, &nplay, &cap, &ncap) == MA_SUCCESS &&
            static_cast<ma_uint32>(impl_->captureIndex) < ncap) {
            impl_->captureId = cap[impl_->captureIndex].id;
            impl_->haveCaptureId = true;
        }
    }

    // Resolve the chosen playback device by index (symmetric to capture).
    impl_->havePlaybackId = false;
    if (impl_->contextOk && impl_->playbackIndex >= 0) {
        ma_device_info* play = nullptr;
        ma_uint32 nplay = 0;
        ma_device_info* cap = nullptr;
        ma_uint32 ncap = 0;
        if (ma_context_get_devices(&impl_->context, &play, &nplay, &cap, &ncap) == MA_SUCCESS &&
            static_cast<ma_uint32>(impl_->playbackIndex) < nplay) {
            impl_->playbackId = play[impl_->playbackIndex].id;
            impl_->havePlaybackId = true;
        }
    }

    ma_device_config cc = ma_device_config_init(ma_device_type_capture);
    cc.capture.format = ma_format_s16;
    cc.capture.channels = 1;
    cc.sampleRate = static_cast<ma_uint32>(impl_->sample_rate);
    cc.dataCallback = capture_cb;
    cc.pUserData = impl_.get();
    if (impl_->haveCaptureId) cc.capture.pDeviceID = &impl_->captureId;
    if (ma_device_init(ctx, &cc, &impl_->capture) == MA_SUCCESS) {
        if (ma_device_start(&impl_->capture) == MA_SUCCESS) impl_->capture_ok = true;
        else ma_device_uninit(&impl_->capture);
    }

    ma_device_config pc = ma_device_config_init(ma_device_type_playback);
    pc.playback.format = ma_format_f32;
    pc.playback.channels = 2;
    pc.sampleRate = static_cast<ma_uint32>(impl_->sample_rate);
    pc.dataCallback = playback_cb;
    pc.pUserData = impl_.get();
    if (impl_->havePlaybackId) pc.playback.pDeviceID = &impl_->playbackId;
    if (ma_device_init(ctx, &pc, &impl_->playback) == MA_SUCCESS) {
        if (ma_device_start(&impl_->playback) == MA_SUCCESS) impl_->playback_ok = true;
        else ma_device_uninit(&impl_->playback);
    }

    return impl_->capture_ok || impl_->playback_ok;
}

void AudioIO::stop() {
    if (!impl_) return;
    if (impl_->capture_ok) {
        ma_device_uninit(&impl_->capture);
        impl_->capture_ok = false;
    }
    if (impl_->playback_ok) {
        ma_device_uninit(&impl_->playback);
        impl_->playback_ok = false;
    }
    if (impl_->contextOk) {
        ma_context_uninit(&impl_->context);
        impl_->contextOk = false;
    }
}

}  // namespace prox
