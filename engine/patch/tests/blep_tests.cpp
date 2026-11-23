#include "mforce/testing/check.h"
#include "core/tests/fixtures.h"
#include "mforce/core/dsp/blep.h"
#include "mforce/core/pins.h"
#include "mforce/patch/node.h"
#include "mforce/source/oscillator/pulse_source.h"
#include "mforce/source/oscillator/saw_source.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>

using namespace mforce;


static bool same_bits(float a, float b) {
    return std::bit_cast<uint32_t>(a) == std::bit_cast<uint32_t>(b);
}
namespace {
// PulseSource::get_blep as it stood (SawSource's inline body is the same two polynomials).
float blep_reference(float pos, float phaseIncr) {
    if (pos < phaseIncr) {
        float t = pos / phaseIncr;
        return t + t - t * t - 1.0f;
    } else if (pos > 1.0f - phaseIncr) {
        float t = (pos - 1.0f) / phaseIncr;
        return t * t + t + t + 1.0f;
    }
    return 0.0f;
}
// The old SawSource::wave and PulseSource::wave (duty 0.5, bend 0: the defaults).
float saw_reference(float pos, float incr) {
    float val = 2.0f * pos - 1.0f;
    float blep = 0.0f;
    if (pos < incr) { float t = pos / incr; blep = t + t - t * t - 1.0f; }
    else if (pos > 1.0f - incr) { float t = (pos - 1.0f) / incr; blep = t * t + t + t + 1.0f; }
    return std::clamp(val - blep, -1.0f, 1.0f);
}
float pulse_reference(float pos, float incr) {
    const float duty = 0.5f, b = 0.0f;
    float val = 0.0f;
    if (pos < duty) val = 1.0f - (1.0f - pos / duty) * b;
    else            val = -1.0f - (duty - pos) * 2.0f * b;
    return val + blep_reference(pos, incr) - blep_reference(std::fmod(pos + (1.0f - duty), 1.0f), incr);
}
// WaveSource's accumulator, as compute() drives it with phase 0: pos starts at
// 0, wave() reads it, then it advances by the increment modulo 1.
template <class Node, class Ref>
int wave_mismatches(Node& node, Ref ref, int n) {
    RenderContext ctx{48000};
    node.prepare(ctx);
    node.start(n);
    const float incr = 440.0f / 48000.0f;
    float pos = 0.0f;
    int bad = 0;
    for (int i = 0; i < n; ++i) {
        const float expect = ref(pos, incr) * 1.0f;
        if (!same_bits(node.next(), expect)) ++bad;
        pos = std::fmod(pos + incr, 1.0f);
        if (pos < 0.0f) pos += 1.0f;
    }
    return bad;
}
}  // namespace

void run_dsp_blep_tests() {
    auto sig = twin_signal(0xB1EFu, 4000);
    const float incs[] = {1e-4f, 0.01f, 0.1f, 0.45f, 0.5f};
    int bad = 0;
    for (float inc : incs) {
        for (float s : sig) {
            const float pos = 0.5f * (s + 1.0f);          // anywhere in [0, 1)
            const float lo = pos * inc, hi = 1.0f - pos * inc;   // inside each window
            if (!same_bits(blep_reference(pos, inc), dsp::blep(pos, inc))) ++bad;
            if (!same_bits(blep_reference(lo, inc), dsp::blep(lo, inc))) ++bad;
            if (!same_bits(blep_reference(hi, inc), dsp::blep(hi, inc))) ++bad;
        }
    }
    CHECK(bad == 0);
    CHECK(same_bits(dsp::blep(0.0f, 0.01f), -1.0f));      // t = 0 at the step
    CHECK(same_bits(dsp::blep(0.5f, 0.01f), 0.0f));       // outside both windows
    SawSource saw;
    CHECK(wave_mismatches(saw, saw_reference, 4800) == 0);
    PulseSource pulse;
    CHECK(wave_mismatches(pulse, pulse_reference, 4800) == 0);
}
