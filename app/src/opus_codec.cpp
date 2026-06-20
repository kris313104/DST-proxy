// opus_codec.cpp — Opus encoder/decoder wrapper.
#include "opus_codec.h"

#include <opus.h>

namespace prox {

OpusCodec::OpusCodec(int sample_rate, int frame_size)
    : sample_rate_(sample_rate), frame_size_(frame_size) {
    int err = OPUS_OK;
    enc_ = opus_encoder_create(sample_rate_, 1, OPUS_APPLICATION_VOIP, &err);
    if (err != OPUS_OK) enc_ = nullptr;
    if (enc_ != nullptr) {
        opus_encoder_ctl(enc_, OPUS_SET_BITRATE(24000));
        opus_encoder_ctl(enc_, OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));
        opus_encoder_ctl(enc_, OPUS_SET_INBAND_FEC(1));
        opus_encoder_ctl(enc_, OPUS_SET_PACKET_LOSS_PERC(10));
    }

    err = OPUS_OK;
    dec_ = opus_decoder_create(sample_rate_, 1, &err);
    if (err != OPUS_OK) dec_ = nullptr;
}

OpusCodec::~OpusCodec() {
    if (enc_ != nullptr) opus_encoder_destroy(enc_);
    if (dec_ != nullptr) opus_decoder_destroy(dec_);
}

std::vector<unsigned char> OpusCodec::encode(const int16_t* pcm) {
    if (enc_ == nullptr) return {};
    std::vector<unsigned char> out(4000);
    const int n = opus_encode(enc_, pcm, frame_size_, out.data(),
                              static_cast<opus_int32>(out.size()));
    if (n < 0) return {};
    out.resize(static_cast<std::size_t>(n));
    return out;
}

int OpusCodec::decode(const unsigned char* data, std::size_t len, int16_t* out_pcm) {
    if (dec_ == nullptr) return -1;
    return opus_decode(dec_, data, static_cast<opus_int32>(len), out_pcm, frame_size_, 0);
}

}  // namespace prox
