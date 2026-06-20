// opus_codec.h — thin RAII wrapper around an Opus encoder+decoder for voice.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

// Opaque Opus handle types (defined in <opus.h>); forward-declared in the GLOBAL namespace
// so member pointers match the C API exactly.
struct OpusEncoder;
struct OpusDecoder;

namespace prox {

// 48 kHz mono, 20 ms frames (960 samples) by default — standard Opus VoIP setup.
class OpusCodec {
public:
    explicit OpusCodec(int sample_rate = 48000, int frame_size = 960);
    ~OpusCodec();

    OpusCodec(const OpusCodec&) = delete;
    OpusCodec& operator=(const OpusCodec&) = delete;

    bool ok() const { return enc_ != nullptr && dec_ != nullptr; }
    int frame_size() const { return frame_size_; }

    // Encode one frame (frame_size mono int16 samples) -> Opus bytes. Empty on error.
    std::vector<unsigned char> encode(const int16_t* pcm);

    // Decode Opus bytes -> mono int16 (out_pcm must hold frame_size samples).
    // Pass data=nullptr/len=0 for packet-loss concealment. Returns samples decoded, <0 on error.
    int decode(const unsigned char* data, std::size_t len, int16_t* out_pcm);

private:
    int sample_rate_;
    int frame_size_;
    OpusEncoder* enc_ = nullptr;
    OpusDecoder* dec_ = nullptr;
};

}  // namespace prox
