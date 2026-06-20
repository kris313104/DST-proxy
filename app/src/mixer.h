// mixer.h — mixing a mono source into a stereo output with gain + pan (pure, testable).
#pragma once

#include <cstddef>
#include <cstdint>

namespace prox {

// Mix a mono int16 source into an interleaved stereo float accumulator.
//   out:    2*frames floats (L,R,L,R,...); ACCUMULATED into (caller zeroes first).
//   gain:   linear volume (typically falloff(distance), 0..1).
//   pan:    -1 = full left, 0 = center, +1 = full right (constant-power).
void mix_mono_into_stereo(float* out, std::size_t frames, const int16_t* mono,
                          double gain, double pan);

}  // namespace prox
