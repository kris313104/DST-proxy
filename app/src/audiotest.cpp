// audiotest.cpp — local loopback: mic -> Opus encode -> decode -> mixer -> speakers.
// Verifies the full local audio + codec pipeline on one machine (you should hear your mic
// echoed back). Network is simulated by the immediate encode/decode round-trip.
//
// Usage: prox_audiotest [seconds]   (default 6)
#include "audio_io.h"
#include "mixer.h"
#include "opus_codec.h"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <deque>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

int main(int argc, char** argv) {
    const int seconds = (argc > 1) ? std::atoi(argv[1]) : 6;

    prox::OpusCodec codec;
    if (!codec.ok()) {
        std::cerr << "opus init failed\n";
        return 1;
    }

    std::mutex m;
    std::deque<int16_t> fifo;  // mono samples after the codec round-trip
    std::atomic<long> framesIn{0};
    std::atomic<long> renderCalls{0};

    prox::AudioIO io(48000, codec.frame_size());

    io.set_on_frame([&](const int16_t* pcm, int n) {
        auto pkt = codec.encode(pcm);
        std::vector<int16_t> dec(n);
        int got = codec.decode(pkt.data(), pkt.size(), dec.data());
        if (got > 0) {
            std::lock_guard<std::mutex> lk(m);
            for (int i = 0; i < got; ++i) fifo.push_back(dec[i]);
            // cap latency: drop if backlog grows too large (~0.5s)
            while (fifo.size() > 24000) fifo.pop_front();
        }
        ++framesIn;
    });

    io.set_on_render([&](float* out, int frames) {
        std::vector<int16_t> tmp(static_cast<size_t>(frames), 0);
        {
            std::lock_guard<std::mutex> lk(m);
            for (int i = 0; i < frames; ++i) {
                if (!fifo.empty()) { tmp[i] = fifo.front(); fifo.pop_front(); }
            }
        }
        prox::mix_mono_into_stereo(out, static_cast<size_t>(frames), tmp.data(), 1.0, 0.0);
        ++renderCalls;
    });

    if (!io.start()) {
        std::cerr << "audio start failed (no usable devices?)\n";
        return 2;
    }

    std::cout << "Loopback running for " << seconds << "s — talk into the mic...\n";
    std::cout << "capture=" << (io.capture_running() ? "on" : "OFF")
              << " playback=" << (io.playback_running() ? "on" : "OFF") << "\n";
    std::this_thread::sleep_for(std::chrono::seconds(seconds));
    io.stop();

    std::cout << "done. capture frames=" << framesIn.load()
              << " render callbacks=" << renderCalls.load() << "\n";
    return 0;
}
