#pragma once
#include <cmath>

namespace mforce {

// Narrow-knee soft clipper. Identity below THR; asymptotic to CEIL.
// HARD is a belt-and-suspenders clamp so downstream volume scaling can't
// leak past int16 saturation.
inline float soft_clip(float x) {
    constexpr float THR  = 0.95f;
    constexpr float CEIL = 0.99f;
    constexpr float HARD = 0.999f;

    float ax = std::fabs(x);
    if (ax < THR) return x;

    float sign = (x < 0) ? -1.0f : 1.0f;
    float over = ax - THR;
    float room = CEIL - THR;
    float shaped = THR + room * std::tanh(over / room);
    float y = sign * shaped;

    if (y >  HARD) y =  HARD;
    if (y < -HARD) y = -HARD;
    return y;
}

} // namespace mforce
