// mixer.cpp — mono->stereo mixing with gain and constant-power pan.
#include "mixer.h"

#include <algorithm>
#include <cmath>

namespace prox {

namespace {
constexpr double kPi = 3.14159265358979323846;
}

void mix_mono_into_stereo(float* out, std::size_t frames, const int16_t* mono,
                          double gain, double pan) {
    if (gain <= 0.0) return;
    pan = std::clamp(pan, -1.0, 1.0);
    // Constant-power pan: theta in [0, pi/2]; center => equal -3dB on both sides.
    const double theta = (pan + 1.0) * 0.25 * kPi;
    const float lg = static_cast<float>(std::cos(theta) * gain);
    const float rg = static_cast<float>(std::sin(theta) * gain);
    for (std::size_t i = 0; i < frames; ++i) {
        const float s = mono[i] / 32768.0f;
        out[2 * i] += s * lg;
        out[2 * i + 1] += s * rg;
    }
}

}  // namespace prox
