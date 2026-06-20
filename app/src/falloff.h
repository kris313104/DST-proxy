// falloff.h — proximity volume + stereo pan math (pure, unit-tested).
#pragma once

namespace prox {

struct FalloffConfig {
    double near_r = 8.0;     // full volume within this radius (DST world units)
    double far_r = 30.0;     // zero volume beyond this radius
    double exponent = 1.0;   // curve shaping on the [0,1] linear ramp:
                             //   1.0 = linear, >1 = drops faster, <1 = stays louder longer
};

// Volume gain in [0,1] for a given distance.
double gain_for_distance(double dist, const FalloffConfig& cfg);

// Convenience: gain from a delta vector on the world (x,z) plane.
double gain_for_delta(double dx, double dz, const FalloffConfig& cfg);

// Stereo pan in [-1,1] (left..right) for a source offset (dx,dz) relative to a listener
// facing `heading_deg`. Odd in dx (mirroring left/right). The exact axis/sign convention
// must be calibrated against the game in Phase 3.
double stereo_pan(double dx, double dz, double heading_deg);

}  // namespace prox
