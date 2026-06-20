// test_voice.cpp — Opus codec round-trip (requires PROX_WITH_VOICE / opus).
#include "opus_codec.h"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <vector>

static int g_failures = 0;
#define CHECK(cond)                                                      \
    do {                                                                 \
        if (!(cond)) {                                                   \
            std::cerr << "FAIL @" << __LINE__ << ": " << #cond << "\n";  \
            ++g_failures;                                                \
        }                                                                \
    } while (0)

int main() {
    using namespace prox;

    OpusCodec codec;  // 48k mono, 960
    CHECK(codec.ok());
    CHECK(codec.frame_size() == 960);

    // Generate a 440 Hz sine, one 20 ms frame.
    const int N = codec.frame_size();
    std::vector<int16_t> in(N);
    double energy_in = 0.0;
    for (int i = 0; i < N; ++i) {
        double s = std::sin(2.0 * 3.14159265358979 * 440.0 * i / 48000.0);
        in[i] = static_cast<int16_t>(s * 12000.0);
        energy_in += static_cast<double>(in[i]) * in[i];
    }

    // Encode -> non-empty and smaller than raw PCM.
    auto pkt = codec.encode(in.data());
    CHECK(!pkt.empty());
    CHECK(pkt.size() < static_cast<size_t>(N) * sizeof(int16_t));

    // Decode -> full frame, non-silent, energy in a sane range (lossy, so approximate).
    std::vector<int16_t> out(N);
    int n = codec.decode(pkt.data(), pkt.size(), out.data());
    CHECK(n == N);
    double energy_out = 0.0;
    for (int i = 0; i < N; ++i) energy_out += static_cast<double>(out[i]) * out[i];
    CHECK(energy_out > 0.0);                       // not silent
    CHECK(energy_out > energy_in * 0.25);          // roughly preserved
    CHECK(energy_out < energy_in * 4.0);

    // Packet-loss concealment: decode(null) yields a full frame without crashing.
    int nplc = codec.decode(nullptr, 0, out.data());
    CHECK(nplc == N);

    if (g_failures == 0) {
        std::cout << "OK: opus codec tests passed (packet " << pkt.size() << " bytes)\n";
        return 0;
    }
    std::cerr << g_failures << " test(s) failed\n";
    return 1;
}
