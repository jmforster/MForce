#pragma once

namespace mforce::dsp {

// The one float every node's pi literal rounds to: 3.14159265f, 3.1415926536f
// and 3.14159265358979323846f are the same float (0x40490FDB), so a formula
// written with any of them computes the same bits with this constant.
inline constexpr float kPi = 3.14159265358979323846f;

} // namespace mforce::dsp
