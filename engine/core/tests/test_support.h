#pragma once
// Helpers the core tests share, and the suites one file runs from another.
#include "mforce/testing/check.h"
#include <bit>
#include <cstdint>
#include <cstring>
#include <vector>

using namespace mforce;


inline bool same_bits(float a, float b) {
    return std::bit_cast<uint32_t>(a) == std::bit_cast<uint32_t>(b);
}
// Equal as values: the same bits, or a zero of either sign on both sides.
inline bool same_value(float a, float b) {
    return same_bits(a, b) || (a == 0.0f && b == 0.0f);
}

inline bool same_bits(const std::vector<float>& a, const std::vector<float>& b) {
    return a.size() == b.size() &&
           std::memcmp(a.data(), b.data(), a.size() * sizeof(float)) == 0;
}
