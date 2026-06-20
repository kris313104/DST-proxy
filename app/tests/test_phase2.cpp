// test_phase2.cpp — tests for proximity math (falloff) and peer position protocol.
#include "falloff.h"
#include "mixer.h"
#include "peer_proto.h"
#include "prox_parser.h"

#include <cmath>
#include <iostream>

static int g_failures = 0;

#define CHECK(cond)                                                      \
    do {                                                                 \
        if (!(cond)) {                                                   \
            std::cerr << "FAIL @" << __LINE__ << ": " << #cond << "\n";  \
            ++g_failures;                                                \
        }                                                                \
    } while (0)

static bool near(double a, double b, double eps = 1e-6) { return std::fabs(a - b) < eps; }

int main() {
    using namespace prox;

    // --- gain_for_distance ---
    {
        FalloffConfig c;  // near=8, far=30, linear
        CHECK(near(gain_for_distance(0.0, c), 1.0));
        CHECK(near(gain_for_distance(8.0, c), 1.0));     // at near_r still full
        CHECK(near(gain_for_distance(5.0, c), 1.0));     // inside near
        CHECK(near(gain_for_distance(30.0, c), 0.0));    // at far_r silent
        CHECK(near(gain_for_distance(100.0, c), 0.0));   // beyond far silent
        // midpoint of [8,30] = 19 -> linear 0.5
        CHECK(near(gain_for_distance(19.0, c), 0.5, 1e-9));
        // monotonic decreasing across the ramp
        double prev = 1.0;
        for (double d = 8.0; d <= 30.0; d += 1.0) {
            double g = gain_for_distance(d, c);
            CHECK(g <= prev + 1e-9);
            prev = g;
        }
    }

    // --- exponent shaping ---
    {
        FalloffConfig faster; faster.exponent = 2.0;  // drops faster
        FalloffConfig linear;
        double d = 19.0;  // midpoint
        CHECK(gain_for_distance(d, faster) < gain_for_distance(d, linear));
    }

    // --- gain_for_delta matches distance ---
    {
        FalloffConfig c;
        CHECK(near(gain_for_delta(3.0, 4.0, c), gain_for_distance(5.0, c)));  // 3-4-5
    }

    // --- stereo_pan ---
    {
        // odd in dx: mirroring left/right flips sign
        double p = stereo_pan(5.0, 5.0, 0.0);
        double pm = stereo_pan(-5.0, 5.0, 0.0);
        CHECK(near(p, -pm));
        CHECK(p >= -1.0 && p <= 1.0);
        CHECK(near(stereo_pan(0.0, 0.0, 45.0), 0.0));  // coincident -> centered
    }

    // --- peer_proto round-trip (name with space + quote) ---
    {
        Update u;
        u.uid = "KU_abc";
        u.name = "Bob \"the\" Builder";
        u.x = -12.34;
        u.z = 56.78;
        u.heading = 135.0;
        u.t = 42.5;

        std::string wire = encode_position(u);
        Update r;
        CHECK(decode_position(wire, r));
        CHECK(r.uid == u.uid);
        CHECK(r.name == u.name);
        CHECK(near(r.x, u.x, 1e-2));
        CHECK(near(r.z, u.z, 1e-2));
        CHECK(near(r.heading, u.heading, 1e-1));
        CHECK(near(r.t, u.t, 1e-2));
    }

    // --- decode rejects junk ---
    {
        Update r;
        CHECK(!decode_position("hello world no fields", r));
    }

    // --- mixer: mono -> stereo with gain + pan ---
    {
        const int16_t mono[1] = {16384};  // 0.5 in float

        // center, gain 1 -> equal L/R at -3 dB (cos(pi/4) ~ 0.7071)
        float out[2] = {0.0f, 0.0f};
        mix_mono_into_stereo(out, 1, mono, 1.0, 0.0);
        CHECK(near(out[0], out[1], 1e-5));
        CHECK(near(out[0], 0.5f * 0.70710678f, 1e-4));

        // full left
        out[0] = out[1] = 0.0f;
        mix_mono_into_stereo(out, 1, mono, 1.0, -1.0);
        CHECK(near(out[0], 0.5f, 1e-4));
        CHECK(near(out[1], 0.0f, 1e-4));

        // full right
        out[0] = out[1] = 0.0f;
        mix_mono_into_stereo(out, 1, mono, 1.0, 1.0);
        CHECK(near(out[0], 0.0f, 1e-4));
        CHECK(near(out[1], 0.5f, 1e-4));

        // gain 0 -> no change
        out[0] = out[1] = 0.0f;
        mix_mono_into_stereo(out, 1, mono, 0.0, 0.0);
        CHECK(near(out[0], 0.0f) && near(out[1], 0.0f));

        // accumulation: two full-left calls add up
        out[0] = out[1] = 0.0f;
        mix_mono_into_stereo(out, 1, mono, 1.0, -1.0);
        mix_mono_into_stereo(out, 1, mono, 1.0, -1.0);
        CHECK(near(out[0], 1.0f, 1e-4));
    }

    if (g_failures == 0) {
        std::cout << "OK: phase 2 math + proto tests passed\n";
        return 0;
    }
    std::cerr << g_failures << " test(s) failed\n";
    return 1;
}
