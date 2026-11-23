#pragma once

// Flush-to-zero + denormals-are-zero for the calling thread. High-Q filter
// states decay into subnormal-float range after excitation dies and denormal
// arithmetic runs ~100x slower (profiled: the hammer bank alone cost 1.1s
// of a 3.9s render crawling through subnormals). Call once at the top of
// every thread that renders DSP; the command-line tool calls it in main.
#if defined(_MSC_VER) || defined(__SSE2__)
#include <immintrin.h>
#endif

namespace mforce {

inline void enable_flush_denormals() {
#if defined(_MSC_VER) || defined(__SSE2__)
    _MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON);
    _MM_SET_DENORMALS_ZERO_MODE(_MM_DENORMALS_ZERO_ON);
#endif
}

} // namespace mforce
