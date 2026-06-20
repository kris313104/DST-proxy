// falloff.cpp — implementation of proximity volume + stereo pan.
#include "falloff.h"

#include <algorithm>
#include <cmath>

namespace prox {

namespace {
constexpr double kPi = 3.14159265358979323846;
}

double gain_for_distance(double dist, const FalloffConfig& cfg) {
    if (dist <= cfg.near_r) return 1.0;
    if (dist >= cfg.far_r) return 0.0;
    const double span = cfg.far_r - cfg.near_r;
    if (span <= 0.0) return 0.0;
    const double linear = (cfg.far_r - dist) / span;  // 1 at near_r, 0 at far_r
    const double e = (cfg.exponent <= 0.0) ? 1.0 : cfg.exponent;
    return std::pow(std::clamp(linear, 0.0, 1.0), e);
}

double gain_for_delta(double dx, double dz, const FalloffConfig& cfg) {
    return gain_for_distance(std::sqrt(dx * dx + dz * dz), cfg);
}

double stereo_pan(double dx, double dz, double heading_deg) {
    if (dx == 0.0 && dz == 0.0) return 0.0;
    const double world_ang = std::atan2(dx, dz);          // angle of source vector
    const double facing = heading_deg * kPi / 180.0;      // listener facing
    const double rel = world_ang - facing;
    return std::clamp(std::sin(rel), -1.0, 1.0);
}

}  // namespace prox
