#pragma once
#include "mforce/util/fft.h"
#include <vector>
#include <complex>
#include <cmath>
#include <algorithm>

namespace mforce {

// Per-render signal statistics. Cheap to compute, useful for filtering
// batch-rendered sound exploration runs.
struct SignalStats {
    float peak              {0.0f};   // max |x|
    float rms               {0.0f};   // sqrt(mean(x^2))
    float zeroCrossingRate  {0.0f};   // crossings per second
    float spectralCentroid  {0.0f};   // Hz — brightness proxy (mag-weighted mean bin freq)
    float spectralFlatness  {0.0f};   // 0..1 — 0 = pure tone, 1 = white noise (geo-mean / arith-mean of mags)
};

// Time-domain stats. Cheap O(N).
inline void compute_time_stats(const float* samples, int n, int sampleRate,
                               SignalStats& out) {
    if (n <= 0) return;

    double sumSq = 0.0;
    float  peak  = 0.0f;
    int    zc    = 0;
    for (int i = 0; i < n; ++i) {
        float s = samples[i];
        float a = std::fabs(s);
        if (a > peak) peak = a;
        sumSq += double(s) * double(s);
        if (i > 0) {
            // Sign change counts as a zero crossing
            if ((samples[i - 1] >= 0.0f) != (s >= 0.0f)) ++zc;
        }
    }
    out.peak = peak;
    out.rms  = float(std::sqrt(sumSq / double(n)));
    out.zeroCrossingRate = float(zc) * float(sampleRate) / float(n);
}

// Frequency-domain stats. Pulls a Hann-windowed slice from the middle of
// the buffer (skipping attack/release transients), FFTs, computes centroid
// and flatness from magnitudes. Window size is the largest power of 2
// that fits, capped at fftMax (default 8192 ≈ 5.9 Hz bin width at 48k).
inline void compute_spectral_stats(const float* samples, int n, int sampleRate,
                                   SignalStats& out, int fftMax = 8192) {
    int N = fftMax;
    while (N > n) N >>= 1;
    if (N < 256) return;  // too short for meaningful spectrum

    int mid    = n / 2;
    int offset = std::max(0, mid - N / 2);
    if (offset + N > n) offset = n - N;

    std::vector<std::complex<float>> buf(N);
    const float twoPi = 6.28318530718f;
    for (int i = 0; i < N; ++i) {
        float w = 0.5f * (1.0f - std::cos(twoPi * float(i) / float(N - 1)));
        buf[i] = std::complex<float>(samples[offset + i] * w, 0.0f);
    }
    fft_inplace(buf);

    // Single-sided magnitude spectrum
    int Nover2 = N / 2;
    double magSum     = 0.0;   // ∑|X[k]|
    double weightedSum= 0.0;   // ∑k·|X[k]|
    double logSum     = 0.0;   // ∑log|X[k]|  (for geometric mean)
    int    nonzero    = 0;
    const float floorMag = 1e-12f;
    for (int k = 1; k < Nover2; ++k) {  // skip DC
        float mag = std::abs(buf[k]);
        if (mag < floorMag) mag = floorMag;
        magSum      += mag;
        weightedSum += double(k) * mag;
        logSum      += std::log(double(mag));
        ++nonzero;
    }
    if (magSum > 0.0) {
        float binHz = float(sampleRate) / float(N);
        out.spectralCentroid = float((weightedSum / magSum) * binHz);
        // Spectral flatness = geomean / arithmean
        double geoMean   = std::exp(logSum / double(nonzero));
        double arithMean = magSum / double(nonzero);
        out.spectralFlatness = float(geoMean / arithMean);
    }
}

inline SignalStats compute_all_stats(const float* samples, int n, int sampleRate) {
    SignalStats s;
    compute_time_stats(samples, n, sampleRate, s);
    compute_spectral_stats(samples, n, sampleRate, s);
    return s;
}

} // namespace mforce
