#pragma once
#include <complex>
#include <vector>
#include <cmath>

namespace mforce {

// In-place Cooley-Tukey FFT. Size of x must be a power of two.
// Extracted from tools/mforce_ui/main.cpp so other tools (explore mode,
// future analyzers) can share one implementation.
inline void fft_inplace(std::vector<std::complex<float>>& x) {
    int n = (int)x.size();
    // Bit-reversal permutation
    for (int i = 1, j = 0; i < n; ++i) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(x[i], x[j]);
    }
    // Cooley-Tukey
    for (int len = 2; len <= n; len <<= 1) {
        float ang = -2.0f * 3.14159265358979f / float(len);
        std::complex<float> wlen(std::cos(ang), std::sin(ang));
        for (int i = 0; i < n; i += len) {
            std::complex<float> w(1.0f, 0.0f);
            for (int j = 0; j < len / 2; ++j) {
                std::complex<float> u = x[i + j];
                std::complex<float> v = x[i + j + len / 2] * w;
                x[i + j] = u + v;
                x[i + j + len / 2] = u - v;
                w *= wlen;
            }
        }
    }
}

} // namespace mforce
