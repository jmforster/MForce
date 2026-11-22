// Assert-style engine unit tests (PerformSource P1, plan_perform_source_p1.md).
// House testing is render-based; this exe covers the value-level semantics
// renders can't isolate (curve interp identity, envelope mapping, adapters).
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cmath>
#include <cstdlib>
#include "alloc_probe.h"
#include "mforce/source/output/sound_channel.h"
#include "mforce/source/output/stereo_mixer.h"
#include "mforce/source/modulator/perform_node.h"

static int g_checks = 0, g_fails = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_fails; \
  std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_NEAR(a, b, eps) do { ++g_checks; float _a=(a), _b=(b); \
  if (std::fabs(_a-_b) > (eps)) { ++g_fails; \
  std::printf("FAIL %s:%d  %s=%g vs %s=%g\n", __FILE__, __LINE__, #a, _a, #b, _b); } } while (0)

#include "mforce/source/modulator/curve_node.h"
#include <memory>
using namespace mforce;

// ---------------------------------------------------------------------------
// The twin-test scaffolding for the core/dsp primitives: a merged site is
// compared bit for bit with the lines it replaced, over a deterministic
// signal that carries exact zeros of both signs.
#include "mforce/core/dsp/constants.h"
#include "mforce/core/randomizer.h"
#include <bit>
#include <cstdint>
#include <vector>

static bool same_bits(float a, float b) {
    return std::bit_cast<uint32_t>(a) == std::bit_cast<uint32_t>(b);
}
// Equal as values: the same bits, or a zero of either sign on both sides.
static bool same_value(float a, float b) {
    return same_bits(a, b) || (a == 0.0f && b == 0.0f);
}
// n samples in (-1, 1) from the engine's own generator, with an exact zero at
// every 97th index and a negative zero at every 101st, so a comparison meets
// both.
static std::vector<float> twin_signal(uint32_t seed, int n) {
    Randomizer rng(seed);
    std::vector<float> v(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) {
        float x = rng.valuePN();
        if (i % 97 == 0) x = 0.0f;
        if (i % 101 == 0) x = -0.0f;
        v[size_t(i)] = x;
    }
    return v;
}

static void run_dsp_scaffold_tests() {
    CHECK(same_value(-0.0f, 0.0f));
    CHECK(!same_bits(-0.0f, 0.0f));
    CHECK(same_bits(0.1f, 0.1f) && !same_value(0.1f, 0.1000001f));
    auto v = twin_signal(0x5EEDu, 1000);
    CHECK(v.size() == 1000);
    CHECK(same_bits(v[0], -0.0f));            // index 0 is both a 97th and a 101st: the last rule wins
    CHECK(same_bits(v[97], 0.0f) && !std::signbit(v[97]));
    CHECK(same_bits(v[101], -0.0f) && std::signbit(v[101]));
    CHECK(v[1] != 0.0f && v[1] > -1.0f && v[1] < 1.0f);
    auto w = twin_signal(0x5EEDu, 1000);
    CHECK(same_bits(v[500], w[500]));         // deterministic per seed
    // The three pi literals the nodes wrote are one float.
    CHECK(same_bits(dsp::kPi, 3.14159265f));
    CHECK(same_bits(dsp::kPi, 3.1415926536f));
    CHECK(same_bits(dsp::kPi, 3.14159265358979323846f));
    CHECK(same_bits(2.0f * dsp::kPi, 6.28318530717958647692f));
}

// ---------------------------------------------------------------------------
// blep: the polynomial band-limited step, against the lines it replaced.
#include "mforce/core/dsp/blep.h"
#include "mforce/source/oscillator/saw_source.h"
#include "mforce/source/oscillator/pulse_source.h"

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
}

static void run_dsp_blep_tests() {
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

// ---------------------------------------------------------------------------
// DcBlocker: KSString's and the resonator's excitation blockers, against the
// lines they replaced (r = 1 - 2*pi*20/sr; y = r*y1 + x - x1).
#include "mforce/core/dsp/dc_blocker.h"

static void run_dsp_dc_blocker_tests() {
    for (float sr : {44100.0f, 48000.0f, 96000.0f}) {
        auto sig = twin_signal(0xDC0u + uint32_t(sr), 10000);
        float dcR = 1.0f - 2.0f * 3.14159265f * 20.0f / sr;   // the deleted line, 20 Hz
        float dcX = 0.0f, dcY = 0.0f;
        dsp::DcBlocker dc;
        dc.set(20.0f, sr);
        int bad = 0;
        for (float exc : sig) {
            float x = dcR * dcY + exc - dcX;                   // the deleted line
            dcX = exc; dcY = x;
            if (!same_bits(dc.process(exc), x)) ++bad;
        }
        CHECK(bad == 0);
    }
    dsp::DcBlocker dc;
    dc.set(20.0f, 48000.0f);
    dc.process(0.7f);
    dc.process(-0.2f);
    dc.reset();
    CHECK(same_bits(dc.process(0.5f), 0.5f));   // r*0 + 0.5 - 0: both memories cleared
    dsp::DcBlocker fresh;                        // a reset blocker is a fresh one from here on
    fresh.set(20.0f, 48000.0f);
    fresh.process(0.5f);
    int bad = 0;
    for (float x : twin_signal(0xDC1u, 200)) if (!same_bits(dc.process(x), fresh.process(x))) ++bad;
    CHECK(bad == 0);
}

// ---------------------------------------------------------------------------
// TuningAllpass: the wavetable's, the resonator's and EKS's first-order
// allpass, against the line they shared (y = C*x + x1 - C*y1).
#include "mforce/core/dsp/tuning_allpass.h"

static void run_dsp_tuning_allpass_tests() {
    for (float C : {0.2f, 0.5f, 0.9f, 0.0f}) {
        auto sig = twin_signal(0x7A11u + uint32_t(C * 100.0f), 10000);
        float prevIn = 0.0f, state = 0.0f;
        dsp::TuningAllpass ap;
        ap.set_coefficient(C);
        int bad = 0;
        for (float x : sig) {
            float y = C * x + prevIn - C * state;    // the deleted line
            prevIn = x;
            state = y;
            if (!same_bits(ap.process(x), y)) ++bad;
        }
        CHECK(bad == 0);
    }
    // The fraction-to-coefficient line, as the wavetable and the resonator wrote it.
    int badFrac = 0;
    for (int i = 0; i < 20; ++i) {
        const float frac = 0.05f * float(i) + 0.0137f;
        dsp::TuningAllpass ap;
        ap.set_fraction(frac);
        if (!same_bits(ap.coefficient(), (1.0f - frac) / (1.0f + frac))) ++badFrac;
    }
    CHECK(badFrac == 0);
}

// Across a note boundary EKS cleared y1 and kept x1; reset() clears both. With
// a coefficient the two runs differ from the first sample after the boundary
// by a difference that never grows and dies away. With coefficient 0 the
// section is a unit delay, which is why its owners skip it then (the
// wavetable and EKS test the coefficient before calling it).
static void run_dsp_tuning_allpass_boundary_tests() {
    {
        dsp::TuningAllpass unit;
        unit.set_coefficient(0.0f);
        CHECK(same_bits(unit.process(0.3f), 0.0f));
        CHECK(same_bits(unit.process(0.1f), 0.3f));
        CHECK(!(unit.coefficient() > 0.0f));      // EKS's guard
        CHECK(!(unit.coefficient() != 0.0f));     // the wavetable's guard
    }
    for (float C : {0.6f, 0.25f}) {
        auto sig = twin_signal(0xE45u, 400);
        float prevIn = 0.0f, state = 0.0f;
        dsp::TuningAllpass ap;
        ap.set_coefficient(C);
        for (int i = 0; i < 200; ++i) {           // the first note
            float y = C * sig[i] + prevIn - C * state; prevIn = sig[i]; state = y;
            ap.process(sig[i]);
        }
        state = 0.0f;                              // EKS's old adjust(): y1 only
        ap.reset();                                // both
        float lastDiff = 0.0f;
        bool firstDiffers = false;
        int grew = 0, same = 0;
        for (int i = 200; i < 400; ++i) {
            float y = C * sig[i] + prevIn - C * state; prevIn = sig[i]; state = y;
            float yn = ap.process(sig[i]);
            float diff = std::fabs(yn - y);
            if (i == 200) firstDiffers = !same_bits(yn, y);
            if (i > 200 && diff > lastDiff) ++grew;
            if (same_bits(yn, y)) ++same;
            lastDiff = diff;
        }
        CHECK(firstDiffers);
        CHECK(grew == 0);
        CHECK(lastDiff < 1e-6f);
        CHECK(same < 200);
    }
}

// ---------------------------------------------------------------------------
// OnePole and the pole formulas: KSString's comb damping, the resonator's
// damping and the bowed string's line filters (y += a*(x - y)); the
// time-constant and corner-frequency lines of the hammer bank, the limiter,
// the performer, the instrument's ring-out and KSString's inharmonic highpass.
#include "mforce/core/dsp/one_pole.h"

static void run_dsp_one_pole_tests() {
    for (float c : {0.05f, 0.6f, 1.0f}) {
        auto sig = twin_signal(0x1E01u + uint32_t(c * 100.0f), 10000);
        float lp = 0.0f;
        dsp::OnePole pole;
        int bad = 0;
        for (float v : sig) {
            lp += c * (v - lp);                    // the deleted line
            if (!same_bits(pole.process(v, c), lp)) ++bad;
            if (!same_bits(pole.value(), lp)) ++bad;
        }
        CHECK(bad == 0);
    }
    dsp::OnePole pole;
    pole.process(0.9f, 0.5f);
    pole.reset();
    CHECK(same_bits(pole.value(), 0.0f));
    CHECK(same_bits(pole.process(0.4f, 0.25f), 0.25f * (0.4f - 0.0f)));
    // The hammer bank's and the instrument's lines: exp(-1 / (seconds * rate)).
    const float sr = 48000.0f;
    CHECK(same_bits(dsp::pole_for_seconds(0.05f, sr), std::exp(-1.0f / (0.05f * sr))));
    CHECK(same_bits(dsp::pole_for_seconds(0.37f, 44100.0f), std::exp(-1.0f / (0.37f * 44100.0f))));
    // The performer's smoother: 1 - exp(-1 / (0.010 * rate)).
    CHECK(same_bits(1.0f - dsp::pole_for_seconds(0.010f, sr), 1.0f - std::exp(-1.0f / (0.010f * sr))));
    // The limiter's line, with its floor of one sample applied by the caller.
    const float relSec = 0.2f;
    CHECK(same_bits(dsp::pole_for_samples(std::max(1.0f, relSec * sr)),
                    std::exp(-1.0f / std::max(1.0f, relSec * sr))));
    CHECK(same_bits(dsp::pole_for_samples(1.0f), std::exp(-1.0f)));
    // KSString's inharmonic highpass pole: exp(-2*pi*hz / rate).
    CHECK(same_bits(dsp::pole_for_cutoff_hz(150.0f, sr), std::exp(-2.0f * 3.14159265f * 150.0f / sr)));
    CHECK(same_bits(dsp::pole_for_cutoff_hz(5000.0f, 96000.0f), std::exp(-2.0f * 3.14159265f * 5000.0f / 96000.0f)));
}

// ---------------------------------------------------------------------------
// bow_friction: the bow table's, KSString's and the bowed string's Friedlander
// curves, against the three lines they replaced.
#include "mforce/core/dsp/bow_friction.h"

namespace {
float bow_table_reference(float dv, float slope, float offset, float minOut, float maxOut) {
    const float s = std::fabs((dv + offset) * slope) + 0.75f;
    float rc = std::pow(s, -4.0f);
    rc = std::clamp(rc, minOut, maxOut);
    return rc;
}
float ks_string_reference(float vRel, float gain) {
    float bt = std::fabs(gain * vRel) + 0.75f;
    float fricCoef = std::pow(bt, -4.0f);
    if (fricCoef > 0.98f) fricCoef = 0.98f;
    return fricCoef;
}
float bowed_string_reference(float vRel, float gain) {
    float bt = std::abs(gain * vRel) + 0.75f;
    float fricCoef = std::pow(bt, -4.0f);
    if (fricCoef > 0.98f) fricCoef = 0.98f;
    else if (fricCoef < 0.0f) fricCoef = 0.0f;
    return fricCoef;
}
}

static void run_dsp_bow_friction_tests() {
    auto sig = twin_signal(0xB0B0u, 6000);
    int badTable = 0, badKs = 0, badBowed = 0;
    for (float slope : {0.05f, 3.0f, 4.0f, 20.0f}) {
        for (float offset : {0.0f, 0.3f}) {
            for (float s : sig) {
                const float dv = 3.0f * s;
                const float t = bow_table_reference(dv, slope, offset, 0.01f, 0.98f);
                if (!same_bits(dsp::bow_friction(dv, slope, offset, 0.01f, 0.98f), t)) ++badTable;
                if (offset != 0.0f) continue;
                const float k = ks_string_reference(dv, slope);
                const float b = bowed_string_reference(dv, slope);
                const float rc = dsp::bow_friction(dv, slope, 0.0f, 0.0f, 0.98f);
                if (!same_bits(rc, k)) ++badKs;
                if (!same_bits(rc, b)) ++badBowed;
            }
        }
    }
    CHECK(badTable == 0);
    CHECK(badKs == 0);
    CHECK(badBowed == 0);
    CHECK(same_bits(dsp::bow_friction(0.0f, 4.0f, 0.0f, 0.0f, 0.98f), 0.98f));     // stuck: the curve's top, clamped
    CHECK(dsp::bow_friction(2.0f, 4.0f, 0.0f, 0.0f, 0.98f) < 0.001f);               // slipping: the curve's tail
    CHECK(same_bits(dsp::bow_friction(2.0f, 4.0f, 0.0f, 0.01f, 0.98f), 0.01f));     // the table's floor
}

// ---------------------------------------------------------------------------
// Biquad: the five sites' per-sample lines against BiquadCoefficients::process,
// and the recipes against the coefficient blocks they replaced.
#include "mforce/core/dsp/biquad.h"
#include "mforce/source/filter/biquad_source.h"

namespace {
struct RawRef { float b0, b1, b2, a1, a2, x1{0}, x2{0}, y1{0}, y2{0};
    float tick(float x) {                       // BiquadSource raw, the brass lip
        const float y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
        x2 = x1; x1 = x; y2 = y1; y1 = y; return y;
    }
};
struct DoublePoleRef { float x1{0}, x2{0}, y1{0}, y2{0};
    float tick(float x, float a) {              // KSString::biquad_ap, the resonator's stiffness
        float y = a * a * x - 2.0f * a * x1 + x2 + 2.0f * a * y1 - a * a * y2;
        x2 = x1; x1 = x; y2 = y1; y1 = y; return y;
    }
};
struct ChafeRef { float s[4]{};
    float tick(float x, float a1, float a2) {   // Mesh2D::allpass
        const float y = a2 * x + a1 * s[0] + s[1] - a1 * s[2] - a2 * s[3];
        s[1] = s[0]; s[0] = x; s[3] = s[2]; s[2] = y; return y;
    }
};
struct ResonanceRef { float y1{0}, y2{0};
    float tick(float x, float b0, float a1, float a2) {   // BiquadSource resonance mode
        const float y = b0 * x - a1 * y1 - a2 * y2;
        y2 = y1; y1 = y; return y;
    }
};
}

static void run_dsp_biquad_tests() {
    auto sig = twin_signal(0xB1Au, 10000);
    const float sets[3][5] = {{0.2f, 0.1f, 0.05f, -1.2f, 0.5f}, {1.0f, 0.0f, 0.0f, 0.0f, 0.0f}, {0.3f, -0.6f, 0.3f, -1.7f, 0.8f}};
    for (const auto& c : sets) {
        RawRef ref{c[0], c[1], c[2], c[3], c[4]};
        dsp::Biquad bq;
        bq.coefficients.set_raw(c[0], c[1], c[2], c[3], c[4]);
        int bad = 0;
        for (float x : sig) if (!same_bits(bq.process(x), ref.tick(x))) ++bad;
        CHECK(bad == 0);
    }
    for (float a : {0.12f, 0.5f, 0.95f}) {
        DoublePoleRef ref;
        dsp::Biquad bq;
        bq.coefficients.set_allpass_double_pole(a);
        int bad = 0;
        for (float x : sig) if (!same_bits(bq.process(x), ref.tick(x, a))) ++bad;
        CHECK(bad == 0);
    }
    {
        const float fc = 1575.0f, R = 0.75f, sr = 48000.0f;
        const float a1 = -2.0f * R * std::cos(6.28318530717958647692f * fc / sr), a2 = R * R;   // the mesh's lines
        ChafeRef ref;
        dsp::BiquadCoefficients edge;
        edge.set_allpass_chafe(fc, R, sr);
        dsp::BiquadState st;
        int bad = 0;
        for (float x : sig) if (!same_bits(edge.process(st, x), ref.tick(x, a1, a2))) ++bad;
        CHECK(bad == 0);
        CHECK(same_bits(edge.a1(), a1) && same_bits(edge.a2(), a2) && same_bits(edge.b0(), a2) && same_bits(edge.b1(), a1) && same_bits(edge.b2(), 1.0f));
    }
    {   // The resonance line has no input memory; process() adds 0*x1 + 0*x2, which
        // can differ only in the sign of a zero when b0*x is a negative zero.
        const float b0 = 0.05f, r = 0.997f, f = 440.0f, sr = 48000.0f;
        const float a1 = -2.0f * r * std::cos(2.0f * 3.14159265358979323846f * f / sr), a2 = r * r;
        ResonanceRef ref;
        dsp::Biquad bq;
        bq.coefficients.set_resonance(f, r, sr, b0);
        CHECK(same_bits(bq.coefficients.a1(), a1) && same_bits(bq.coefficients.a2(), a2));
        int badValue = 0, badBits = 0, negZero = 0;
        for (float x : sig) {
            const float y = bq.process(x), e = ref.tick(x, b0, a1, a2);
            if (!same_value(y, e)) ++badValue;
            if (!same_bits(y, e)) ++badBits;
            if (same_bits(b0 * x, -0.0f)) ++negZero;
        }
        CHECK(badValue == 0);
        CHECK(badBits <= negZero);
    }
}

static void run_dsp_biquad_recipe_tests() {
    for (float q : {0.5f, 2.0f, 15.0f}) {
        for (float cutoff : {20.0f, 528.0f, 7000.0f}) {
            const float omega = 2.0f * 3.1415926536f * cutoff / 48000.0f;   // the brass lines
            const float alpha = std::sin(omega) / (2.0f * q);
            const float cosOm = std::cos(omega);
            const float a0 = 1.0f + alpha;
            const float c = 1.0f - cosOm;
            const float scale = 1.0f / (a0 * std::max(1.0f, q));
            dsp::BiquadCoefficients lip;
            lip.set_resonant_lowpass(omega, q);
            CHECK(same_bits(lip.b0(), 0.5f * c * scale) && same_bits(lip.b1(), c * scale) && same_bits(lip.b2(), 0.5f * c * scale));
            CHECK(same_bits(lip.a1(), -2.0f * cosOm / a0) && same_bits(lip.a2(), (1.0f - alpha) / a0));
        }
    }
    // phase_delay_at: the closed form BiquadSource carried, retyped, against the
    // coefficients' — two sets, one with a negative b0 for the polarity fold.
    auto reference = [](float b0, float b1, float b2, float a1, float a2, float hz, float sr) {
        if (hz <= 0.0f) return 0.0f;
        constexpr float pi = 3.14159265358979323846f;
        const float w = 2.0f * pi * hz / sr;
        const float c1 = std::cos(w), s1 = std::sin(w);
        const float c2 = std::cos(2.0f * w), s2 = std::sin(2.0f * w);
        const float nr = b0 + b1 * c1 + b2 * c2;
        const float ni = -(b1 * s1 + b2 * s2);
        const float dr = 1.0f + a1 * c1 + a2 * c2;
        const float di = -(a1 * s1 + a2 * s2);
        float ph = std::atan2(ni, nr) - std::atan2(di, dr);
        while (ph >  0.5f * pi) ph -= pi;
        while (ph <= -0.5f * pi) ph += pi;
        return -ph / w;
    };
    const float sets[2][5] = {{0.3f, -0.6f, 0.3f, -1.7f, 0.8f}, {-0.4f, 0.2f, 0.1f, -0.5f, 0.3f}};
    for (const auto& k : sets) {
        dsp::BiquadCoefficients cc;
        cc.set_raw(k[0], k[1], k[2], k[3], k[4]);
        for (float hz : {55.0f, 440.0f, 3000.0f, 12000.0f})
            CHECK(same_bits(cc.phase_delay_at(hz, 48000.0f), reference(k[0], k[1], k[2], k[3], k[4], hz, 48000.0f)));
    }
    BiquadSource node;
    node.b0.value = 0.3f; node.b1.value = -0.6f; node.b2.value = 0.3f; node.a1.value = -1.7f; node.a2.value = 0.8f;
    RenderContext ctx{48000};
    node.prepare(ctx);
    node.start(100);
    dsp::BiquadCoefficients c;
    c.set_raw(0.3f, -0.6f, 0.3f, -1.7f, 0.8f);
    for (float hz : {55.0f, 440.0f, 3000.0f, 12000.0f})
        CHECK(same_bits(node.phase_delay_at(hz), c.phase_delay_at(hz, 48000.0f)));
    CHECK(same_bits(c.phase_delay_at(0.0f, 48000.0f), 0.0f));
}

// ---------------------------------------------------------------------------
// ButterworthSection: the two section kinds of the Butterworth filters,
// against the FIR/IIR-vector sections they replaced, with their operation order.
#include "mforce/core/dsp/butterworth_section.h"
#include "mforce/source/filter/bw_lowpass_filter.h"
#include "mforce/source/filter/bw_highpass_filter.h"
#include "mforce/source/filter/bw_bandpass_filter.h"
#include "mforce/source/noise/white_noise_source.h"
#include "mforce/source/noise/shaped_draw.h"

namespace {
// BWSection, BWLPSection and BWHPSection as they stood.
struct BwRef {
    std::vector<float> fir = std::vector<float>(3, 0.0f), iir = std::vector<float>(2, 0.0f);
    float a[3]{}, b[2]{};
    float gain{0.0f}, zeta{0.0f}, rate{48000.0f};
    bool highpass{false};
    BwRef(bool hp, float k, float n, float sampleRate) : rate(sampleRate), highpass(hp) {
        zeta = float(-std::cos(3.14159265358979 * (2.0 * k + n - 1.0) / (2.0 * n)));
    }
    void update(float cutoff) {
        if (!highpass) {
            float omegac = float(2.0 * rate * std::tan(3.14159265358979 * cutoff / rate));
            a[0] = omegac * omegac; a[1] = 2.0f * omegac * omegac; a[2] = omegac * omegac;
            float b0 = (4.0f * rate * rate) + (4.0f * rate * zeta * omegac) + (omegac * omegac);
            b[0] = ((2.0f * omegac * omegac) - (8.0f * rate * rate)) / (-b0);
            b[1] = ((4.0f * rate * rate) - (4.0f * rate * zeta * omegac) + (omegac * omegac)) / (-b0);
            gain = 1.0f / b0;
        } else {
            float omegac = float(1.0 / (2.0 * rate * std::tan(3.14159265358979 * cutoff / rate)));
            a[0] = 4.0f * rate * rate; a[1] = -8.0f * rate * rate; a[2] = 4.0f * rate * rate;
            float b0 = (4.0f * rate * rate) + (4.0f * rate * zeta / omegac) + (1.0f / (omegac * omegac));
            b[0] = ((2.0f / (omegac * omegac)) - (8.0f * rate * rate)) / (-b0);
            b[1] = ((4.0f * rate * rate) - (4.0f * rate * zeta / omegac) + (1.0f / (omegac * omegac))) / (-b0);
            gain = 1.0f / b0;
        }
    }
    float process(float input) {
        float in = gain * input;
        for (int i = 2; i > 0; --i) fir[i] = fir[i - 1];
        fir[0] = in;
        float value = 0.0f;
        for (int i = 0; i < 3; ++i) value += a[i] * fir[i];
        float out = value;
        for (int i = 0; i < 2; ++i) out += b[i] * iir[i];
        for (int i = 1; i > 0; --i) iir[i] = iir[i - 1];
        iir[0] = out;
        return out;
    }
};
}

static void run_dsp_butterworth_tests() {
    auto sig = twin_signal(0xB077u, 10000);
    for (bool hp : {false, true}) {
        for (float cutoff : {100.0f, 1000.0f, 12000.0f}) {
            for (int k = 1; k <= 2; ++k) {
                BwRef ref(hp, float(k), 4.0f, 48000.0f);
                ref.update(cutoff);
                dsp::ButterworthSection sec(hp ? dsp::ButterworthSection::Kind::Highpass
                                               : dsp::ButterworthSection::Kind::Lowpass,
                                            k, 4, 48000.0f);
                sec.update(cutoff);
                int bad = 0;
                for (float x : sig) if (!same_bits(sec.process(x), ref.process(x))) ++bad;
                CHECK(bad == 0);
            }
        }
    }
    // The two-section filters on white noise, against a cascade of the old sections.
    for (bool hp : {false, true}) {
        RenderContext ctx{48000};
        auto white = std::make_shared<WhiteNoiseSource>(0xB0B1u);
        auto whiteRef = std::make_shared<WhiteNoiseSource>(0xB0B1u);
        std::unique_ptr<ValueSource> node;
        if (hp) node = std::make_unique<BWHighpassFilter>(); else node = std::make_unique<BWLowpassFilter>();
        node->set_param("source", white);
        node->prepare(ctx); node->start(4800);
        whiteRef->prepare(ctx); whiteRef->start(4800);
        BwRef s1(hp, 1.0f, 4.0f, 48000.0f), s2(hp, 2.0f, 4.0f, 48000.0f);
        s1.update(1000.0f); s2.update(1000.0f);          // the pin's default cutoff
        int bad = 0;
        for (int i = 0; i < 4800; ++i) {
            const float expect = s2.process(s1.process(whiteRef->next()));
            if (!same_bits(node->next(), expect)) ++bad;
        }
        CHECK(bad == 0);
    }
    dsp::ButterworthSection sec(dsp::ButterworthSection::Kind::Lowpass, 1, 2, 48000.0f);
    sec.update(500.0f);
    sec.process(0.8f); sec.process(-0.3f);
    sec.reset();
    dsp::ButterworthSection fresh(dsp::ButterworthSection::Kind::Lowpass, 1, 2, 48000.0f);
    fresh.update(500.0f);
    CHECK(same_bits(sec.process(0.25f), fresh.process(0.25f)));
}

// ---------------------------------------------------------------------------
// A reset primitive is a fresh one: each stateful primitive driven past its
// memory depth, reset, and compared with a fresh instance over 200 samples.
// DcBlocker::set at a second corner against its formula.
#include "mforce/render/perform_source.h"

namespace {
template <class P>
int reset_mismatches(P& used, P& fresh, const std::vector<float>& sig) {
    for (int i = 0; i < 5; ++i) used.process(sig[size_t(i)]);
    used.reset();
    int bad = 0;
    for (size_t i = 5; i < sig.size(); ++i)
        if (!same_bits(used.process(sig[i]), fresh.process(sig[i]))) ++bad;
    return bad;
}
}

static void run_dsp_reset_tests() {
    auto sig = twin_signal(0x2E5E7u, 205);
    {
        dsp::DcBlocker a, b;
        a.set(60.0f, 44100.0f); b.set(60.0f, 44100.0f);
        CHECK(reset_mismatches(a, b, sig) == 0);
        dsp::DcBlocker c;
        c.set(60.0f, 44100.0f);
        const float r = 1.0f - 2.0f * 3.14159265f * 60.0f / 44100.0f;   // the deleted line at another corner
        c.process(1.0f);
        CHECK(same_bits(c.process(0.0f), r * 1.0f + 0.0f - 1.0f));
    }
    {
        dsp::TuningAllpass a, b;
        a.set_coefficient(0.4f); b.set_coefficient(0.4f);
        CHECK(reset_mismatches(a, b, sig) == 0);
    }
    {
        dsp::Biquad a, b;
        a.coefficients.set_raw(0.3f, -0.6f, 0.3f, -1.7f, 0.8f);
        b.coefficients.set_raw(0.3f, -0.6f, 0.3f, -1.7f, 0.8f);
        CHECK(reset_mismatches(a, b, sig) == 0);
    }
    {
        dsp::ButterworthSection a(dsp::ButterworthSection::Kind::Highpass, 1, 2, 48000.0f);
        dsp::ButterworthSection b(dsp::ButterworthSection::Kind::Highpass, 1, 2, 48000.0f);
        a.update(800.0f); b.update(800.0f);
        CHECK(reset_mismatches(a, b, sig) == 0);
    }
    {
        dsp::OnePole a, b;
        for (int i = 0; i < 5; ++i) a.process(sig[size_t(i)], 0.3f);
        a.reset();
        int bad = 0;
        for (size_t i = 5; i < sig.size(); ++i)
            if (!same_bits(a.process(sig[i], 0.3f), b.process(sig[i], 0.3f))) ++bad;
        CHECK(bad == 0);
    }
}

// The performer's wheel and pressure smoothers, against the lines they replaced.
static void run_dsp_performer_smoother_tests() {
    RenderContext ctx{48000};
    PerformSource ps;
    auto is = std::make_shared<InstrumentState>();
    ps.set_instrument_state(is);
    ps.prepare(ctx);
    const float a = 1.0f - std::exp(-1.0f / (0.010f * 48000.0f));   // the deleted coefficient line
    float wheel = 0.0f, press = 0.0f;
    auto sig = twin_signal(0x5E00u, 300);
    int bad = 0;
    for (float sv : sig) {
        const float w = 0.5f * (sv + 1.0f), pr = 0.25f * (sv + 1.0f);
        is->wheel.store(w);
        is->pressure.store(pr);
        ps.tick();
        wheel += a * (w - wheel);          // the deleted lines
        press += a * (pr - press);
        if (!same_bits(ps.wheel(), wheel) || !same_bits(ps.pressure(), press)) ++bad;
    }
    CHECK(bad == 0);
}

using CurveKnots = std::vector<std::pair<float, float>>;
using CurveRows = std::vector<CurveNode::ExprKnot>;

static void run_curve_node_tests() {
    // Linear (vcurve semantics): clamp + linear interp
    CurveNode lin;
    lin.set_knots(CurveKnots{{0.0f, 0.3f}, {0.2f, 0.3f}, {0.85f, 1.0f}, {1.0f, 1.6f}});
    CHECK_NEAR(lin.map(-1.0f), 0.3f, 1e-6f);   // clamp low
    CHECK_NEAR(lin.map(0.1f),  0.3f, 1e-6f);   // flat segment
    CHECK_NEAR(lin.map(1.0f),  1.6f, 1e-6f);   // clamp high
    CHECK_NEAR(lin.map(0.925f), 1.3f, 1e-3f);  // midpoint of last segment

    // LogX (a frequency curve's usual interp): value linear in log(x)
    CurveNode logx; logx.interp.value = int(CurveNode::CurveInterp::LogX);
    logx.set_knots(CurveKnots{{100.0f, 0.0f}, {10000.0f, 2.0f}});
    CHECK_NEAR(logx.map(1000.0f), 1.0f, 1e-4f);  // geometric midpoint

    // LogLog: 2 knots == exact power law (y = x^2 through (10,100),(100,10000))
    CurveNode ll; ll.interp.value = int(CurveNode::CurveInterp::LogLog);
    ll.set_knots(CurveKnots{{10.0f, 100.0f}, {100.0f, 10000.0f}});
    CHECK_NEAR(ll.map(31.6227766f), 1000.0f, 0.5f);

    // Pull path: source wired, evaluates on change
    auto cs = std::make_shared<ConstantSource>(0.5f);
    CurveNode wired;
    wired.set_knots(CurveKnots{{0.0f, 0.0f}, {1.0f, 10.0f}});
    wired.set_param("source", cs);
    wired.next();
    CHECK_NEAR(wired.current(), 5.0f, 1e-6f);

    // Empty knots = identity
    CurveNode ident;
    CHECK_NEAR(ident.map(123.0f), 123.0f, 1e-6f);
}

static void run_curve_expr_tests() {
    using EK = CurveNode::ExprKnot;
    using KF = CurveNode::KnotForm;
    // One Linear knot = global formula with live extrapolation on both sides
    // (the keytrack case: cutoff = 8*f0 everywhere, no 16000-knot needed).
    CurveNode kt; kt.mode.value = 1;
    kt.set_expr_knots(CurveRows{{440.0f, KF::Linear, 8.0f, 0.0f}});
    CHECK_NEAR(kt.map(100.0f),  800.0f,   1e-3f);
    CHECK_NEAR(kt.map(440.0f),  3520.0f,  1e-2f);
    CHECK_NEAR(kt.map(5000.0f), 40000.0f, 1e-1f);

    // Matt's 3-point example: flat 0.1 low, 0.1 - x/50000 high, smooth
    // morph between; equal formulas on the top knots pin the formula exactly
    // over [3200,16000] and keep extrapolating above.
    CurveNode m; m.mode.value = 1;
    m.set_expr_knots(CurveRows{{20.0f,    KF::Linear, 0.0f,      0.1f},
                               {3200.0f,  KF::Linear, -2e-5f,    0.1f},
                               {16000.0f, KF::Linear, -2e-5f,    0.1f}});
    CHECK_NEAR(m.map(10.0f),     0.1f,    1e-6f);   // edge: constant holds
    CHECK_NEAR(m.map(8000.0f),  -0.06f,   1e-5f);   // formula exact mid-span
    CHECK_NEAR(m.map(20000.0f), -0.3f,    1e-5f);   // extrapolates past last
    // Blend region: t = (1610-20)/3180 = 0.5, v0 = 0.1, v1 = 0.0678
    CHECK_NEAR(m.map(1610.0f),   0.0839f, 1e-4f);

    // Power form: y = 2 * x^0.5
    CurveNode pw; pw.mode.value = 1;
    pw.set_expr_knots(CurveRows{{100.0f, KF::Power, 2.0f, 0.5f}});
    CHECK_NEAR(pw.map(100.0f),  20.0f, 1e-4f);
    CHECK_NEAR(pw.map(2500.0f), 100.0f, 1e-3f);

    // Points mode leaves points behavior untouched even with expression rows set
    CurveNode off;
    off.set_expr_knots(CurveRows{{440.0f, KF::Linear, 8.0f, 0.0f}});
    off.set_knots(CurveKnots{{0.0f, 0.0f}, {1.0f, 10.0f}});
    CHECK_NEAR(off.map(0.5f), 5.0f, 1e-6f);
}

#include "mforce/source/envelope/envelope.h"

static void run_envelope_range_tests() {
    RenderContext ctx{48000};
    auto mk = []() {
        auto env = std::make_shared<Envelope>();
        env->add_stage({{0.0f, 1.0f, RampType::Linear, 0.0f}, 0.5f, 0.0f, 0.0f});
        env->add_stage({{1.0f, 1.0f, RampType::Linear, 0.0f}, 0.5f, 0.0f, 0.0f});
        return env;
    };
    // Default (no min/max): final value 1.0 — unchanged behavior
    auto e1 = mk(); e1->prepare(ctx); e1->start(4800);
    float last = 0; for (int i = 0; i < 4300; ++i) last = e1->next();
    CHECK_NEAR(last, 1.0f, 1e-3f);

    // minValue 80, maxValue 500 (constants): early sample near 80, late 500
    auto e2 = mk();
    e2->set_param("minValue", std::make_shared<ConstantSource>(80.0f));
    e2->set_param("maxValue", std::make_shared<ConstantSource>(500.0f));
    e2->prepare(ctx); e2->start(4800);
    float first = e2->next();
    CHECK(first < 120.0f && first >= 80.0f);
    for (int i = 1; i < 4300; ++i) last = e2->next();
    CHECK_NEAR(last, 500.0f, 1.0f);

    // get_param roundtrip
    CHECK(e2->get_param("maxValue") != nullptr);
    CHECK(e1->get_param("maxValue") == nullptr);
}

static void run_stage_nominal_tests() {
    RenderContext ctx{48000};
    auto env = std::make_shared<Envelope>();
    // 0->1 attack with nominal 0.25 s (percent tiny), then gated expand hold.
    Envelope::Stage a{{0.0f, 1.0f, RampType::Linear, 0.0f}, 0.001f, 0.0f, 0.0f};
    a.nominal = 0.25f;
    env->add_stage(a);
    env->add_stage({{1.0f, 1.0f, RampType::Linear, 0.0f}, 0.0f, 0.0f, 0.0f});
    env->set_gated(true);
    env->prepare(ctx); env->start(48000);  // nominal note duration 1 s
    // Nominal honored: attack = 12000 samples; value at sample 6000 ~ 0.5.
    // Without nominal the 0.001-pct attack finishes in ~48 samples.
    float v = 0; for (int i = 0; i < 6000; ++i) v = env->next();
    CHECK(v > 0.3f && v < 0.7f);

    // NOT gated: nominal ignored, tiny attack — value ~1.0 by sample 6000.
    auto env2 = std::make_shared<Envelope>();
    Envelope::Stage a2{{0.0f, 1.0f, RampType::Linear, 0.0f}, 0.001f, 0.0f, 0.0f};
    a2.nominal = 0.25f;
    env2->add_stage(a2);
    env2->add_stage({{1.0f, 1.0f, RampType::Linear, 0.0f}, 0.0f, 0.0f, 0.0f});
    env2->prepare(ctx); env2->start(48000);
    float v2 = 0; for (int i = 0; i < 6000; ++i) v2 = env2->next();
    CHECK_NEAR(v2, 1.0f, 1e-3f);
}

#include "mforce/render/perform_source.h"

static void run_perform_source_tests() {
    auto ps = std::make_shared<PerformSource>();
    ps->set_note(220.0f, 0.9f, 48000, 1.0f);
    PerformOut f(ps, PerformOut::Field::Frequency);
    PerformOut v(ps, PerformOut::Field::Velocity);
    PerformOut d(ps, PerformOut::Field::Duration);
    f.next(); v.next(); d.next();
    CHECK_NEAR(f.current(), 220.0f, 1e-6f);
    CHECK_NEAR(v.current(), 0.9f, 1e-6f);
    CHECK_NEAR(d.current(), 1.0f, 1e-6f);
    ps->set_note(440.0f, 0.5f, 24000, 0.5f); // re-strike: adapters follow
    f.next(); v.next(); d.next();
    CHECK_NEAR(f.current(), 440.0f, 1e-6f);
    CHECK_NEAR(v.current(), 0.5f, 1e-6f);
    CHECK_NEAR(d.current(), 0.5f, 1e-6f);   // backlog 64 Tier 1
    CHECK(ps->note().durSamples == 24000);

    // Chain: PerformOut freq -> LogX CurveNode, re-evaluates on re-strike
    auto fo = std::make_shared<PerformOut>(ps, PerformOut::Field::Frequency);
    CurveNode cn; cn.interp.value = int(CurveNode::CurveInterp::LogX);
    cn.set_knots(CurveKnots{{100.0f, 0.0f}, {10000.0f, 2.0f}});
    cn.set_param("source", fo);
    cn.next();
    CHECK_NEAR(cn.current(), cn.map(440.0f), 1e-6f);
    ps->set_note(1000.0f, 0.5f, 24000);
    cn.next();
    CHECK_NEAR(cn.current(), 1.0f, 1e-4f);
}

static void run_tap_guard_tests() {
    // Clamp to +-8
    auto c = std::make_shared<ConstantSource>(42.0f);
    c->next();
    TapSource tap(c);
    CHECK_NEAR(tap.next(), 8.0f, 1e-6f);
    c->set(-100.0f); c->next();
    CHECK_NEAR(tap.next(), -8.0f, 1e-6f);
    c->set(3.5f); c->next();
    CHECK_NEAR(tap.next(), 3.5f, 1e-6f);

    // NaN/inf scrub to 0
    c->set(std::nanf("")); c->next();
    CHECK_NEAR(tap.next(), 0.0f, 1e-6f);
    c->set(INFINITY); c->next();
    CHECK_NEAR(tap.next(), 0.0f, 1e-6f);

    // Null source still reads 0 (pass-2 placeholder state)
    TapSource empty;
    CHECK_NEAR(empty.next(), 0.0f, 1e-6f);
}

#include "mforce/source/combiner/combined_source.h"
#include "mforce/source/combiner/crossfade_source.h"
#include "mforce/source/filter/distorted_source.h"
#include "mforce/source/utility/static_var_source.h"
#include "mforce/source/utility/static_range_source.h"

static void run_tap_cycle_tests() {
    // The tap edge's one-sample delay with no host clock: every pull computes
    // and the tap reads its source's present value, which for a tap evaluated
    // before its source each tick is last tick's value (under a clock the
    // delay is the rule: run_tap_reading_tests). Self-referencing counter:
    // out = 1 + tap(out)
    // ramps 1, 2, 3... and the guard ceiling (not inf) catches the runaway.
    auto counter = std::make_shared<CombinedSource>(
        std::make_shared<ConstantSource>(1.0f),
        std::make_shared<ConstantSource>(0.0f),   // replaced by the tap below
        CombineOp::Sum, 0.0f);                    // Sum: true addition (Add averages)
    auto tap = std::make_shared<TapSource>(counter);
    counter->set_param("source2", tap);

    CHECK_NEAR(counter->next(), 1.0f, 1e-6f);
    CHECK_NEAR(counter->next(), 2.0f, 1e-6f);
    CHECK_NEAR(counter->next(), 3.0f, 1e-6f);
    for (int i = 0; i < 20; ++i) counter->next();
    // Equilibrium, not inf: the guard caps the FEEDBACK at 8, so the
    // counter settles at 1 + 8 = 9.
    CHECK_NEAR(counter->current(), 9.0f, 1e-6f);
}

#include "mforce/source/physical/delay_line_source.h"

static void run_delay_line_tests() {
    RenderContext ctx{48000};
    // sr 48000, freq 480 -> exactly 100 samples of delay
    auto dl = std::make_shared<DelayLineSource>();
    dl->set_param("frequency", std::make_shared<ConstantSource>(480.0f));
    auto in = std::make_shared<ConstantSource>(0.0f);
    dl->set_param("source", in);
    dl->prepare(ctx); dl->start(48000);

    in->set(1.0f);
    float first = dl->next();          // impulse enters at tick 1
    in->set(0.0f);
    CHECK_NEAR(first, 0.0f, 1e-6f);    // empty line
    float out = 0.0f;
    for (int i = 0; i < 99; ++i) out = dl->next();
    CHECK_NEAR(out, 0.0f, 1e-6f);      // still inside the line (tick 100)
    out = dl->next();                  // tick 101: impulse emerges
    CHECK_NEAR(out, 1.0f, 1e-3f);
    out = dl->next();
    CHECK_NEAR(out, 0.0f, 1e-3f);      // and passes

    // ratio doubles the period: 200 samples
    auto dl2 = std::make_shared<DelayLineSource>();
    dl2->set_param("frequency", std::make_shared<ConstantSource>(480.0f));
    dl2->set_param("ratio", std::make_shared<ConstantSource>(2.0f));
    auto in2 = std::make_shared<ConstantSource>(0.0f);
    dl2->set_param("source", in2);
    dl2->prepare(ctx); dl2->start(48000);
    in2->set(1.0f); dl2->next(); in2->set(0.0f);
    for (int i = 0; i < 199; ++i) out = dl2->next();
    out = dl2->next();
    CHECK_NEAR(out, 1.0f, 1e-3f);

    // prepare() clears the line: no ring-over between notes
    dl2->prepare(ctx); dl2->start(48000);
    for (int i = 0; i < 300; ++i) CHECK(dl2->next() == 0.0f);
}

#include "mforce/source/filter/svf_source.h"

// Measured phase delay: drive the filter with a unit sine at hz, project the
// steady-state output onto sin/cos at the same index (I/Q). y = A sin(wn+p)
// gives p = atan2(sum y*cos, sum y*sin); delay in samples = -p/w.
static float measure_phase_delay(SVFSource& f, ConstantSource& in,
                                 float hz, int sr) {
    const double w = 2.0 * 3.141592653589793 * hz / sr;
    const int settle = sr / 2, N = sr;
    double si = 0.0, co = 0.0;
    for (int n = 0; n < settle + N; ++n) {
        in.set(float(std::sin(w * n)));
        const float y = f.next();
        if (n >= settle) {
            si += y * std::sin(w * n);
            co += y * std::cos(w * n);
        }
    }
    return float(-std::atan2(co, si) / w);
}

static void run_phase_delay_tests() {
    RenderContext ctx{48000};
    // Closed-form phase_delay_at must match the filter actually measured,
    // across modes and both sides of cutoff.
    struct Case { int mode; float fc, res, hz; };
    const Case cases[] = {
        {SVFSource::kLowpass,   1000.0f, 1.0f, 200.0f},
        {SVFSource::kLowpass,   1000.0f, 1.0f, 1000.0f},  // at fc: (pi/2)/w
        {SVFSource::kLowpass,   4000.0f, 2.0f, 500.0f},
        {SVFSource::kBandpass,  1000.0f, 4.0f, 300.0f},   // below fc: lead
        {SVFSource::kLowpass1P, 2000.0f, 1.0f, 400.0f},
    };
    for (const auto& c : cases) {
        auto in = std::make_shared<ConstantSource>(0.0f);
        SVFSource f;
        f.set_setting("mode", float(c.mode));
        f.set_param("source", in);
        f.set_param("cutoffFreq", std::make_shared<ConstantSource>(c.fc));
        f.set_param("resonance", std::make_shared<ConstantSource>(c.res));
        f.prepare(ctx); f.start(48000);
        const float meas = measure_phase_delay(f, *in, c.hz, 48000);
        const float pred = f.phase_delay_at(c.hz);
        CHECK_NEAR(pred, meas, 0.15f);
    }
    // At fc the 2-pole LP delay is exactly (pi/2)/w regardless of res.
    {
        auto in = std::make_shared<ConstantSource>(0.0f);
        SVFSource f;
        f.set_param("source", in);
        f.set_param("cutoffFreq", std::make_shared<ConstantSource>(1000.0f));
        f.prepare(ctx); f.start(8);
        f.next();
        const float w = 2.0f * 3.14159265f * 1000.0f / 48000.0f;
        CHECK_NEAR(f.phase_delay_at(1000.0f), (0.5f * 3.14159265f) / w, 1e-2f);
    }
}

static void run_loop_compensation_tests() {
    RenderContext ctx{48000};
    // Bare tap-closed loop: delay <- sum(excite, tap(delay)). The cycle is
    // len + 1 ticks (the closing tap's one sample); compensate shortens len
    // so the cycle lands exactly on sr/f.
    auto run_bare = [&](bool comp) {
        auto dl = std::make_shared<DelayLineSource>();
        dl->set_param("frequency", std::make_shared<ConstantSource>(480.0f));
        auto ex = std::make_shared<ConstantSource>(0.0f);
        auto sum = std::make_shared<CombinedSource>(
            ex, std::make_shared<ConstantSource>(0.0f), CombineOp::Sum, 0.0f);
        sum->set_param("source2", std::make_shared<TapSource>(dl));
        dl->set_param("source", sum);
        if (comp) dl->set_setting("compensate", 1.0f);
        dl->prepare(ctx); dl->start(48000);
        ex->set(1.0f); dl->next(); ex->set(0.0f);
        int first = 0, second = 0;
        for (int t = 2; t <= 400 && !second; ++t) {
            if (std::fabs(dl->next()) > 0.5f) {
                if (!first) first = t; else if (t > first + 4) second = t;
            }
        }
        return std::pair<int, int>(first, second - first);
    };
    auto off = run_bare(false);
    CHECK(off.first == 101 && off.second == 101);   // sr/f + 1: rings flat
    auto on = run_bare(true);
    CHECK(on.first == 100 && on.second == 100);     // exactly sr/f

    // Loop with an SVF inside: sum -> svf -> delay. The walk must find the
    // filter and subtract its (current) phase delay; the echo period lands
    // on sr/f instead of sr/f + 1 + ~7.8 samples of filter lag.
    auto run_svf = [&](bool comp) {
        auto dl = std::make_shared<DelayLineSource>();
        dl->set_param("frequency", std::make_shared<ConstantSource>(200.0f));
        auto ex = std::make_shared<ConstantSource>(0.0f);
        auto sum = std::make_shared<CombinedSource>(
            ex, std::make_shared<ConstantSource>(0.0f), CombineOp::Sum, 0.0f);
        sum->set_param("source2", std::make_shared<TapSource>(dl));
        // fc between mode 1 (200 Hz) and mode 2 (~400 Hz): only the
        // fundamental sees |H| > 1 and grows, so the settled tail is a
        // near-sine at the loop's mode-1 frequency. (With a higher cutoff
        // several modes grow at once and the STRETCHED upper modes — loop
        // lag varies with frequency, exactly like KS partials — dominate
        // the autocorrelation.) fc 300 also makes the lag big: ~33 samples,
        // 14% of the period, so uncompensated flatness is unmistakable.
        auto svf = std::make_shared<SVFSource>();
        svf->set_param("source", sum);
        svf->set_param("cutoffFreq", std::make_shared<ConstantSource>(300.0f));
        svf->set_param("resonance", std::make_shared<ConstantSource>(1.0f));
        dl->set_param("source", svf);
        if (comp) dl->set_setting("compensate", 1.0f);
        dl->prepare(ctx); dl->start(48000);
        ex->set(1.0f); dl->next(); ex->set(0.0f);
        // The recirculating impulse smears into a multi-harmonic tone, so
        // measure the loop period by autocorrelation of the settled tail
        // (lag peak + parabolic refinement), not by threshold crossings.
        for (int t = 0; t < 3000; ++t) dl->next();
        std::vector<float> tail(4800);
        for (auto& v : tail) v = dl->next();
        // Normalized (correlation-coefficient) form: the tail grows a few
        // percent per cycle, and unnormalized autocorrelation tilts its peak
        // under an exponential envelope.
        auto ac = [&](int L) {
            double r = 0.0, e0 = 0.0, eL = 0.0;
            for (size_t i = 0; i + L < tail.size(); ++i) {
                r  += double(tail[i]) * double(tail[i + L]);
                e0 += double(tail[i]) * double(tail[i]);
                eL += double(tail[i + L]) * double(tail[i + L]);
            }
            return (e0 > 0.0 && eL > 0.0) ? r / std::sqrt(e0 * eL) : 0.0;
        };
        int bestL = 200;
        double bestR = ac(200);
        for (int L = 201; L <= 280; ++L) {
            const double r = ac(L);
            if (r > bestR) { bestR = r; bestL = L; }
        }
        const double r0 = ac(bestL - 1), r1 = ac(bestL), r2 = ac(bestL + 1);
        const double den = r0 - 2.0 * r1 + r2;
        const double frac = (den != 0.0) ? 0.5 * (r0 - r2) / den : 0.0;
        return float(bestL + frac);
    };
    const float periodOff = run_svf(false);
    const float periodOn = run_svf(true);
    CHECK(periodOff > 265.0f);                      // ~273: flat by a third of a semitone x4
    CHECK_NEAR(periodOn, 240.0f, 0.5f);             // on pitch
}

#include "mforce/source/filter/shaper_source.h"

static void run_shaper_tests() {
    RenderContext ctx{48000};
    // Default identity curve passes input through
    auto sh = std::make_shared<ShaperSource>();
    auto in = std::make_shared<ConstantSource>(0.5f);
    sh->set_param("source", in);
    sh->prepare(ctx); sh->start(100);
    CHECK_NEAR(sh->next(), 0.5f, 1e-3f);

    // Clamp past the drawn ends
    in->set(3.0f); in->next();
    CHECK_NEAR(sh->next(), 1.0f, 1e-3f);
    in->set(-3.0f); in->next();
    CHECK_NEAR(sh->next(), -1.0f, 1e-3f);

    // drive scales input BEFORE the lookup: drive 2 pushes 0.4 to 0.8
    sh->set_param("drive", std::make_shared<ConstantSource>(2.0f));
    in->set(0.4f); in->next();
    CHECK_NEAR(sh->next(), 0.8f, 1e-3f);

    // A drawn dead-zone curve: flat 0 across [-0.5, 0.5], ramps outside.
    // smoothness 0.5 = linear segments (0 = hold-v1 step, 1 = sine).
    auto dz = std::make_shared<ShaperSource>();
    dz->set_array("values", {-1.0f, -1.0f, -0.5f, 0.0f, 0.5f, 0.0f, 1.0f, 1.0f});
    dz->set_param("smoothness", std::make_shared<ConstantSource>(0.5f));
    auto in2 = std::make_shared<ConstantSource>(0.2f);
    dz->set_param("source", in2);
    dz->prepare(ctx); dz->start(100);
    float mid = dz->next();
    CHECK(std::fabs(mid) < 0.05f);           // inside the dead zone
    in2->set(0.75f); in2->next();
    float hi = dz->next();
    CHECK_NEAR(hi, 0.5f, 1e-3f);             // linear outer ramp midpoint

    // smoothness 0 steps: same input holds the segment's START value (0)
    dz->set_param("smoothness", std::make_shared<ConstantSource>(0.0f));
    CHECK_NEAR(dz->next(), 0.0f, 1e-6f);

    // values round-trip via get_array (UI re-pull path)
    auto back = dz->get_array("values");
    CHECK(back.size() == 8 && back[2] == -0.5f);
}

#include "mforce/core/curve.h"

// Shaper per-segment overrides (curve-morph plan Task 5). "segs" array:
// flat [typeIdx, power] per segment; typeIdx 0 = default (smoothness),
// else 1 + int(RampType).
static void run_shaper_seg_tests() {
    RenderContext ctx{48000};
    ShaperSource sh;
    sh.set_array("values", {-1.0f,-1.0f, 0.0f,0.0f, 1.0f,1.0f});
    sh.set_param("smoothness", std::make_shared<ConstantSource>(0.5f));
    sh.set_param("drive", std::make_shared<ConstantSource>(1.0f));
    sh.set_param("source", std::make_shared<ConstantSource>(0.0f));
    sh.prepare(ctx); sh.start(8);
    sh.next();
    // Segment 1 (0->1) overridden to Hold (typeIdx 5): y stays 0 across it.
    sh.set_array("segs", {0.0f,0.0f, 5.0f,0.0f});
    CHECK_NEAR(sh.map(0.5f), 0.0f, 1e-9f);
    // Segment 0 default: smoothness 0.5 == lerp
    CHECK_NEAR(sh.map(-0.5f), -0.5f, 1e-6f);
    // Expo override with power 2 on segment 1 (typeIdx 2 = 1 + Expo)
    sh.set_array("segs", {0.0f,0.0f, 2.0f,2.0f});
    CHECK_NEAR(sh.map(0.5f), 0.25f, 1e-6f);   // t^2 at t=0.5
    // Round-trip: get_array returns what was set (Expo override live)
    auto back = sh.get_array("segs");
    CHECK(back.size() == 4);
    // A fresh node has no segs and an all-default set encodes to empty
    ShaperSource fresh;
    CHECK(fresh.get_array("segs").empty());
    sh.set_array("segs", {0.0f,0.0f, 0.0f,0.0f});
    CHECK(sh.get_array("segs").empty());
    // Smoothness must NOT bend an overridden Linear segment (typeIdx 1)
    sh.set_array("segs", {0.0f,0.0f, 1.0f,0.0f});
    sh.set_param("smoothness", std::make_shared<ConstantSource>(1.0f));
    sh.next();
    CHECK_NEAR(sh.map(0.5f), 0.5f, 1e-6f);
}

#include "mforce/source/utility/wormhole_source.h"

// Wormhole: pure pass-through, including chained pairs (out-half refs
// in-half — the hidden-wire configuration the UI creates).
static void run_wormhole_tests() {
    RenderContext ctx{48000};
    auto in = std::make_shared<ConstantSource>(0.42f);
    WormholeSource wh;
    wh.set_param("source", in);
    wh.prepare(ctx); wh.start(8);
    CHECK_NEAR(wh.next(), 0.42f, 1e-9f);
    auto whIn = std::make_shared<WormholeSource>();
    whIn->set_param("source", in);
    WormholeSource whOut;
    whOut.set_param("source", whIn);
    whOut.prepare(ctx); whOut.start(8);
    CHECK_NEAR(whOut.next(), 0.42f, 1e-9f);
    // Unwired = silence, not garbage
    WormholeSource bare;
    bare.prepare(ctx); bare.start(8);
    CHECK_NEAR(bare.next(), 0.0f, 1e-9f);
}

// Shaper morph pin (curve-morph plan Task 8): point-space A/B blend.
static void run_shaper_morph_tests() {
    RenderContext ctx{48000};
    ShaperSource sh;
    sh.set_array("values",  {-1.0f,-1.0f, 0.4f,0.2f, 1.0f,1.0f});
    sh.set_array("values2", {-1.0f,-1.0f, 0.1f,0.6f, 1.0f,1.0f});
    auto morph = std::make_shared<ConstantSource>(0.0f);
    sh.set_param("morph", morph);
    sh.set_param("source", std::make_shared<ConstantSource>(0.0f));
    sh.set_param("drive", std::make_shared<ConstantSource>(1.0f));
    sh.set_param("smoothness", std::make_shared<ConstantSource>(0.5f));
    sh.prepare(ctx); sh.start(8);
    sh.next();
    // m=0: exactly curve A (knee at 0.4)
    CHECK_NEAR(sh.map(0.4f), 0.2f, 1e-7f);
    // m=1: exactly curve B (knee at 0.1)
    morph->set(1.0f); morph->next(); sh.next();
    CHECK_NEAR(sh.map(0.1f), 0.6f, 1e-7f);
    // m=0.5: the knee SLIDES to (0.25, 0.4) — point-space, not output blend
    morph->set(0.5f); morph->next(); sh.next();
    CHECK_NEAR(sh.map(0.25f), 0.4f, 1e-6f);
    // Segment powers lerp when both sides override the same segment
    sh.set_array("segs",  {0.0f,0.0f, 2.0f,1.0f});   // seg1 Expo p1
    sh.set_array("segs2", {0.0f,0.0f, 2.0f,3.0f});   // seg1 Expo p3
    // blended seg1: Expo p2 between blended points (0.25,0.4)->(1,1):
    // at x=0.625 (t=0.5): 0.4 + 0.6*0.25 = 0.55
    CHECK_NEAR(sh.map(0.625f), 0.55f, 1e-5f);
    // Signed-curvature lerp across the Expo/InverseExpo axis: A Expo p2
    // (bulge down) vs B InverseExpo p2 (bulge up) at m=0.5 cancels to
    // linear — segment midpoint lands on the chord midpoint.
    sh.set_array("segs",  {0.0f,0.0f, 2.0f,2.0f});
    sh.set_array("segs2", {0.0f,0.0f, 3.0f,2.0f});
    CHECK_NEAR(sh.map(0.625f), 0.7f, 1e-5f);
    // Mismatched point count: morph ignored (B degenerates to A)
    sh.set_array("segs", {}); sh.set_array("segs2", {});
    sh.set_array("values2", {-1.0f,-1.0f, 1.0f,1.0f});
    CHECK_NEAR(sh.map(0.4f), 0.2f, 1e-6f);
    // No values2 at all: byte-identity path
    ShaperSource plain;
    plain.set_array("values", {-1.0f,-1.0f, 1.0f,1.0f});
    CHECK_NEAR(plain.map(0.3f), 0.3f, 1e-7f);
}

// RampType::Hold + holdPct removal (curve-morph plan Task 4).
static void run_ramp_hold_tests() {
    Ramp h{0.7f, 0.2f, RampType::Hold, 0.0f};
    CHECK_NEAR(h.value(0.0f), 0.7f, 1e-9f);
    CHECK_NEAR(h.value(0.5f), 0.7f, 1e-9f);
    CHECK_NEAR(h.value(1.0f), 0.7f, 1e-9f);
    // Byte-identity guard: every type returns startVal at exactly pos==0
    // (the old holdPct==0 branch did this; Expo with power 0 would
    // otherwise flip to endVal at pos 0).
    Ramp e{0.3f, 0.9f, RampType::Expo, 0.0f};
    CHECK_NEAR(e.value(0.0f), 0.3f, 1e-9f);
    Ramp s{0.3f, 0.9f, RampType::Sine, 0.0f};
    CHECK_NEAR(s.value(0.0f), 0.3f, 1e-6f);
}

// Shared Curve evaluator parity (2026-09-05 curve-morph plan Task 2):
// Curve::eval must reproduce CurveNode::map (all three domains, smoothness
// 0.5 == exact lerp) and ShaperSource::map (SmoothnessInterpolator path)
// before either host delegates to it.
static void run_curve_shared_tests() {
    RenderContext ctx{48000};

    // Parity vs CurveNode::map — Linear domain
    CurveNode lin;
    lin.set_knots(CurveKnots{{0.0f, 0.3f}, {0.2f, 0.3f}, {0.85f, 1.0f}, {1.0f, 1.6f}});
    for (float x : {-1.0f, 0.0f, 0.1f, 0.2f, 0.5f, 0.925f, 1.0f, 2.0f})
        CHECK_NEAR(Curve::eval(lin.knots(), {}, Curve::Domain::Linear, 0.5f, x),
                   lin.map(x), 1e-7f);

    // Parity vs CurveNode::map — LogX and LogLog domains
    CurveNode logx; logx.interp.value = int(CurveNode::CurveInterp::LogX);
    logx.set_knots(CurveKnots{{100.0f, 0.0f}, {10000.0f, 2.0f}});
    CHECK_NEAR(Curve::eval(logx.knots(), {}, Curve::Domain::LogX, 0.5f, 1000.0f),
               logx.map(1000.0f), 1e-6f);
    CurveNode ll; ll.interp.value = int(CurveNode::CurveInterp::LogLog);
    ll.set_knots(CurveKnots{{10.0f, 100.0f}, {100.0f, 10000.0f}});
    CHECK_NEAR(Curve::eval(ll.knots(), {}, Curve::Domain::LogLog, 0.5f,
                           31.6227766f),
               ll.map(31.6227766f), 0.5f);

    // Parity vs ShaperSource::map — smoothness 0.6, asymmetric curve
    ShaperSource sh;
    sh.set_array("values", {-1.0f,-0.76f, -0.87f,-0.73f, -0.1f,-0.2f,
                             0.0f,0.0f, 0.11f,0.19f, 0.78f,0.57f,
                             0.93f,0.66f});
    sh.set_param("smoothness", std::make_shared<ConstantSource>(0.6f));
    sh.set_param("drive", std::make_shared<ConstantSource>(1.0f));
    sh.set_param("source", std::make_shared<ConstantSource>(0.0f));
    sh.prepare(ctx); sh.start(8);
    sh.next();  // latches smoothness into the evaluator
    auto vals = sh.get_array("values");
    for (float x : {-1.5f, -0.9f, -0.3f, 0.0f, 0.05f, 0.5f, 0.9f, 1.5f})
        CHECK_NEAR(Curve::eval_flat(vals, {}, Curve::Domain::Linear, 0.6f, x),
                   sh.map(x), 1e-6f);

    // Identity on empty
    CHECK_NEAR(Curve::eval({}, {}, Curve::Domain::Linear, 0.5f, 123.0f),
               123.0f, 1e-9f);
}

#include "mforce/source/physical/pierce_filter.h"
#include <vector>
#include <random>

// ---------------------------------------------------------------------------
// HARD GATE for the Pierce/Van Duyne passive nonlinear filter (round 2): the
// whole point of this structure is that it cannot create energy, so nothing it
// terminates can run away and it can be driven at full scale.  Drives the
// SHIPPED PierceFilterSource (not a transcription) open loop and requires the
// cumulative output energy never to exceed the cumulative input energy.
//
// Probes: white-noise burst, continuous white noise, a 20 Hz - 20 kHz sweep, a
// full-scale square, an impulse, and DC — across eight coefficient pairs from
// near-linear to the most asymmetric the clamp allows.  The literal recurrence
// from the patent/Faust fails this at 2.43; see
// docs/research/stk_port/PIERCE_PASSIVE_NOTES.md.
// ---------------------------------------------------------------------------
namespace {

struct Playback final : mforce::Generator {
    const std::vector<float>* buf{nullptr};
    size_t i{0};
    const char* type_name() const override { return "Playback"; }
    void on_start(int) override { i = 0; }
    float compute() override { return (buf && i < buf->size()) ? (*buf)[i++] : 0.0f; }
};

double worst_energy_ratio(const std::vector<float>& x, float aNeg, float aPos) {
    RenderContext ctx{48000};
    auto src = std::make_shared<Playback>();
    src->buf = &x;
    PierceFilterSource f;
    f.set_param("source", src);
    f.set_param("coefNeg", std::make_shared<ConstantSource>(aNeg));
    f.set_param("coefPos", std::make_shared<ConstantSource>(aPos));
    f.prepare(ctx); f.start(int(x.size()));
    double cx = 0.0, cy = 0.0, worst = 0.0;
    for (float s : x) {
        const double y = f.next();
        cx += double(s) * double(s);
        cy += y * y;
        if (cx > 1e-12) worst = std::fmax(worst, cy / cx);
    }
    return worst;
}

} // namespace

static void run_pierce_passivity_tests() {
    const int N = 48000;
    const double sr = 48000.0;
    std::vector<std::pair<const char*, std::vector<float>>> probes;
    std::mt19937 rng(7);
    std::normal_distribution<float> gauss(0.0f, 1.0f);

    std::vector<float> burst(N, 0.0f);
    for (int n = 0; n < N / 5; ++n) burst[n] = gauss(rng);
    probes.emplace_back("noiseburst", burst);

    std::vector<float> noise(N);
    for (int n = 0; n < N; ++n) noise[n] = gauss(rng);
    probes.emplace_back("noise", noise);

    std::vector<float> sweep(N);
    for (int n = 0; n < N; ++n) {
        const double t = n / sr;
        sweep[n] = float(std::sin(2.0 * 3.14159265358979 *
                                  (20.0 * t + (19980.0 / 2.0) * t * t)));
    }
    probes.emplace_back("sweep", sweep);

    std::vector<float> square(N);
    for (int n = 0; n < N; ++n)
        square[n] = std::sin(2.0 * 3.14159265358979 * 220.0 * n / sr) >= 0.0
                        ? 1.0f : -1.0f;
    probes.emplace_back("square_fullscale", square);

    std::vector<float> imp(N, 0.0f);
    imp[0] = 1.0f;
    probes.emplace_back("impulse", imp);
    probes.emplace_back("dc", std::vector<float>(N, 1.0f));

    const float pairs[][2] = {{0.5f, -0.5f},  {0.9f, -0.9f}, {0.99f, -0.99f},
                              {0.9f, 0.1f},   {-0.9f, 0.3f}, {0.0f, 0.8f},
                              {0.75f, -0.25f}, {0.999f, -0.999f}};
    double worst = 0.0;
    const char* worstName = "";
    for (const auto& p : probes)
        for (const auto& c : pairs) {
            const double r = worst_energy_ratio(p.second, c[0], c[1]);
            CHECK(r <= 1.0 + 1e-6);
            if (r > worst) { worst = r; worstName = p.first; }
        }
    std::printf("pierce passivity: worst cumulative out/in energy = %.9f "
                "(%s) over %d probes\n", worst, worstName,
                int(probes.size() * 8));

    // coefNeg == coefPos must be an exactly linear first-order allpass:
    // unity magnitude, so total output energy equals total input energy to
    // within the stored energy left in the state.
    const double lin = worst_energy_ratio(noise, 0.7f, 0.7f);
    CHECK(lin <= 1.0 + 1e-6);
    CHECK(lin > 0.999);
    std::printf("pierce linear check (coefNeg == coefPos == 0.7): "
                "energy ratio %.9f\n", lin);
}

#include "mforce/music/parse_util.h"

static void run_passage_parse_tests() {
    // No bars: every note starts its own phrase (compat: one-note phrases).
    auto plain = parse_passage("Cq Dq Eq", 4, 60.0f);
    CHECK(plain.size() == 3);
    for (auto& n : plain) CHECK(n.phraseStart);

    // Bars group; leading/doubled/trailing bars are no-ops.
    auto p = parse_passage("| Cq Dq | | Eq Fq Gq |", 4, 60.0f);
    CHECK(p.size() == 5);
    CHECK(p[0].phraseStart);  CHECK(!p[1].phraseStart);
    CHECK(p[2].phraseStart);  CHECK(!p[3].phraseStart);  CHECK(!p[4].phraseStart);

    // A rest ends the phrase: the note after it starts fresh (spec §2 v1
    // rule — silence breaks the breath; `|` is the explicit marker).
    // Grouping is active because the string contains a bar.
    auto r = parse_passage("| Cq Dq Rq Eq", 4, 60.0f);
    CHECK(r.size() == 4);
    CHECK(r[0].phraseStart);  CHECK(!r[1].phraseStart);
    CHECK(r[2].noteNumber == kRestNote);
    CHECK(r[3].phraseStart);
}

#include "mforce/render/perform_source.h"

static void run_onset_field_tests() {
    auto ps = std::make_shared<PerformSource>();
    PerformOut out(ps, PerformOut::Field::Onset);
    CHECK(out.current() == 0.0f);                          // no note yet
    ps->set_note(440.0f, 0.8f, 48000, 1.0f, nullptr, 2.0f);
    CHECK(out.current() == 2.0f);
    CHECK(out.next() == 2.0f);
    ps->set_note(440.0f, 0.8f, 48000, 1.0f, nullptr);      // default arg = none
    CHECK(out.current() == 0.0f);
}

#include "mforce/render/instrument.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <sstream>
#include <cstring>

// ---------------------------------------------------------------------------
// The tests' one door to a patch: load_inst reads JSON text through the codec
// and builds the instrument; load_sound builds the file at a path as a sound;
// load_refused says what a read or a build of a text refuses with.
#include "mforce/patch/patch_json.h"
#include "mforce/patch/retired_spellings.h"
#include "mforce/patch/table_rule.h"
#include "mforce/render/build_instrument.h"
#include "mforce/render/build_sound.h"
#include <set>
#include <string_view>

namespace patch_door {
static bool same_samples(const std::vector<float>& a, const std::vector<float>& b) {
    return a.size() == b.size() && (a.empty() || std::memcmp(a.data(), b.data(), a.size() * sizeof(float)) == 0);
}
// The first index where two renders differ; their common length when one is a prefix of the other.
static size_t first_difference(const std::vector<float>& a, const std::vector<float>& b) {
    size_t i = 0;
    while (i < a.size() && i < b.size() && std::memcmp(&a[i], &b[i], sizeof(float)) == 0) ++i;
    return i;
}
static std::string difference(const std::vector<float>& a, const std::vector<float>& b) {
    const size_t i = first_difference(a, b);
    char text[160];
    std::snprintf(text, sizeof text, "first at sample %zu of %zu and %zu: %.9g %.9g", i, a.size(), b.size(),
                  i < a.size() ? double(a[i]) : 0.0, i < b.size() ? double(b[i]) : 0.0);
    return text;
}
// A C4 of `seconds` on the live path: one prepared voice pulled sample by
// sample, its tails after it, until the voice's life ends.
static std::vector<float> live_c4(PitchedInstrument& pi, float seconds = 0.25f) {
    auto v = pi.prepare_voice(60.0f, 0.8f, seconds);
    std::vector<float> out(size_t(v.durSamples));
    for (int i = 0; i < v.durSamples; ++i) {
        v.tick();
        out[size_t(i)] = v.source->next() * v.gain;
        for (auto& a : v.advanceList) a->next();
    }
    return out;
}
// A sound's first `frames` (all of it for -1) through its mixer, stereo interleaved.
static std::vector<float> render_sound(BuiltSound& s, int frames = -1) {
    const int n = frames < 0 ? s.frames : std::min(frames, s.frames);
    std::vector<float> buf(2 * size_t(n), 0.0f);
    s.mixer->render(buf.data(), n);
    return buf;
}
static mforce::Patch read_text(const std::string& text) {
    mforce::Patch p;
    from_json(nlohmann::json::parse(text), p);
    return p;
}
}  // namespace patch_door

// An instrument as the tests hold it: the build, the patch's rate and what
// the build had to say.
struct LoadedInstrument {
    std::unique_ptr<PitchedInstrument> instrument;
    int sampleRate{48000};
    std::vector<std::string> warnings;
};

// The instrument `json` describes, read through the codec and built with at
// least `minPolyphony` voices.
static LoadedInstrument load_inst(const std::string& json, int minPolyphony = 0) {
    const mforce::Patch patch = patch_door::read_text(json);
    LoadedInstrument loaded;
    loaded.instrument = build_instrument(patch, minPolyphony, &loaded.warnings);
    loaded.sampleRate = patch.sampleRate;
    return loaded;
}
// The sound the patch file at `path` describes.
static BuiltSound load_sound(const std::string& path) {
    return build_sound(load_patch(path));
}
// The sentence a read or a build of `json` refuses with: an instrument as
// load_inst builds it, a plain sound as a sound; empty when it loads.
static std::string load_refused(const std::string& json) {
    try {
        const mforce::Patch patch = patch_door::read_text(json);
        if (patch.instrument) (void)build_instrument(patch);
        else (void)build_sound(patch);
    } catch (const std::exception& e) { return e.what(); }
    return {};
}
// Whether a refusal says `sentence`: the module's refusal begins with the
// path to what it refuses, then the sentence. An empty sentence wants none.
static bool refusal_says(const std::string& refusal, const std::string& sentence) {
    return sentence.empty() ? refusal.empty() : refusal.ends_with(sentence);
}

// Load a baseline patch with its score block stripped (the tests schedule
// their own notes). Run engine_tests from the repo root.
static LoadedInstrument load_scoreless(const char* path) {
    std::ifstream f(path);
    std::stringstream ss; ss << f.rdbuf();
    std::string txt = ss.str();
    auto j = nlohmann::json::parse(txt);
    j.erase("score");
    return load_inst(j.dump());
}

// A minimal sustaining patch: percent-mode adsr (attack 5%, decay 10%,
// expand, release 25%) into a sine whose frequency is WIRED to the Note
// node. The wire matters: a value pushed once at Setup (a dynamic pin)
// would freeze a glide at its first value — only pulled frequency chains
// (what every taught wind patch uses) see the ramp. %s = the instrument
// block's glideMs.
static std::string hold_patch_json(float glideMs) {
    char buf[1400];
    std::snprintf(buf, sizeof(buf), R"({
      "sampleRate": 48000,
      "instrument": { "polyphony": 2, "glideMs": %.1f },
      "graph": {
        "output": "sine1",
        "nodes": [
          { "id": "pf", "type": "PerformNode",
            "params": { "field": "frequency" } },
          { "id": "env1", "type": "Envelope",
            "params": {
              "startVal": [0.0, 1.0, 0.7, 0.7], "endVal": [1.0, 0.7, 0.7, 0.0],
              "power": [0.0, 0.0, 0.0, 0.0], "type": ["Linear", "Linear", "Linear", "Linear"],
              "percent": [0.05, 0.1, 0.0, 0.25], "minSec": [0.0, 0.0, 0.0, 0.0],
              "maxSec": [0.0, 0.0, 0.0, 0.0], "nominal": [0.0, 0.0, 0.0, 0.0] } },
          { "id": "sine1", "type": "SineSource",
            "params": { "frequency": { "ref": "pf" },
                        "amplitude": { "ref": "env1" }, "phase": 0.0 } }
        ]
      }
    })", glideMs);
    return std::string(buf);
}

// Per-note delivery with hold (spec 2026-09-20-note-onsets-v2 §4/§5/§7).
static void run_hold_delivery_tests() {
    const int sr = 48000;
    auto rms_of = [](const std::vector<float>& b, float fromSec, float toSec) {
        int a = int(fromSec * 48000), z = int(toSec * 48000);
        a = std::max(0, a); z = std::min(z, int(b.size()));
        double s = 0.0;
        for (int i = a; i < z; ++i) s += double(b[size_t(i)]) * b[size_t(i)];
        return float(std::sqrt(s / double(std::max(1, z - a))));
    };
    auto render_all = [](LoadedInstrument& ip, int n) {
        std::vector<float> b(size_t(n), 0.0f);
        ip.instrument->render(b.data(), n);
        return b;
    };

    // (a) The classic path is deterministic and unchanged in shape: the
    //     same hold:false PerformedNote renders the same bytes twice.
    //     (The null gate over 79 patches is the real referee for "equals
    //     yesterday".)
    {
        const int N = int(1.6f * sr);
        auto a = load_scoreless("patches/baselines/BaselineSIN.json");
        auto* pa = dynamic_cast<PitchedInstrument*>(a.instrument.get());
        CHECK(pa != nullptr);
        pa->play_note({.start = 0.0f, .duration = 1.0f, .noteNumber = 60.0f, .velocity = 0.8f});
        auto bufA = render_all(a, N);

        auto b = load_scoreless("patches/baselines/BaselineSIN.json");
        auto* pb = dynamic_cast<PitchedInstrument*>(b.instrument.get());
        pb->play_note({.start = 0.0f, .duration = 1.0f, .noteNumber = 60.0f, .velocity = 0.8f, .onset = 0, .hold = false});
        auto bufB = render_all(b, N);
        CHECK(std::memcmp(bufA.data(), bufB.data(),
                          size_t(N) * sizeof(float)) == 0);
    }

    // (b) Hold mechanics: quarter {hold:true} + quarter {hold:false},
    //     same pitch. The gate stays open across 0.4 s — no release dip —
    //     and the voice releases after 0.8 s.
    {
        const int N = int(2.0f * sr);
        auto ip = load_inst(hold_patch_json(15.0f));
        auto* pi = dynamic_cast<PitchedInstrument*>(ip.instrument.get());
        CHECK(pi != nullptr);
        pi->play_note({.start = 0.0f, .duration = 0.4f, .noteNumber = 60.0f, .velocity = 0.8f, .onset = 0, .hold = true});
        pi->play_note({.start = 0.4f, .duration = 0.4f, .noteNumber = 60.0f, .velocity = 0.8f, .onset = 0, .hold = false});
        auto b = render_all(ip, N);
        float sustain  = rms_of(b, 0.30f, 0.34f);
        float boundary = rms_of(b, 0.38f, 0.42f);
        CHECK(sustain > 0.1f);
        CHECK(std::fabs(boundary - sustain) < 0.10f * sustain);
        CHECK(rms_of(b, 0.70f, 0.78f) > 0.1f);        // still sounding
        CHECK(rms_of(b, 1.10f, 1.40f) < 0.01f * sustain);  // and released
    }

    // (c) Release re-layout end to end (§5): quarter {hold:true} + half
    //     {hold:false}. The release is 25% of the HALF note (0.2 s), not
    //     of the quarter that prepared the voice (which would be 0.1 s).
    {
        const int N = int(2.4f * sr);
        auto ip = load_inst(hold_patch_json(15.0f));
        auto* pi = dynamic_cast<PitchedInstrument*>(ip.instrument.get());
        pi->play_note({.start = 0.0f, .duration = 0.4f, .noteNumber = 60.0f, .velocity = 0.8f, .onset = 0, .hold = true});
        pi->play_note({.start = 0.4f, .duration = 0.8f, .noteNumber = 60.0f, .velocity = 0.8f, .onset = 0, .hold = false});
        auto b = render_all(ip, N);
        float peak = 0.0f;
        for (float v : b) peak = std::max(peak, std::fabs(v));
        const float thresh = peak * 0.01f;            // -40 dB below peak
        int last = 0;
        for (int i = 0; i < N; ++i)
            if (std::fabs(b[size_t(i)]) > thresh) last = i;
        float lastSec = float(last) / float(sr);
        CHECK(lastSec > 1.34f && lastSec < 1.46f);    // 1.2 s + ~0.2 s
    }

    // (d) Glide (§4.1): C4 {hold:true} then G4 {hold:false} with
    //     glideMs 15. The pitch ARRIVES at G4 within the glide, and the
    //     boundary's per-sample step is smaller than an instant retarget's.
    {
        const int N = int(1.6f * sr);
        auto glided = load_inst(hold_patch_json(15.0f));
        auto* pg = dynamic_cast<PitchedInstrument*>(glided.instrument.get());
        pg->play_note({.start = 0.0f, .duration = 0.4f, .noteNumber = 60.0f, .velocity = 0.8f, .onset = 0, .hold = true});
        pg->play_note({.start = 0.4f, .duration = 0.4f, .noteNumber = 67.0f, .velocity = 0.8f, .onset = 0, .hold = false});
        auto bg = render_all(glided, N);

        auto instant = load_inst(hold_patch_json(0.0f));
        auto* pn = dynamic_cast<PitchedInstrument*>(instant.instrument.get());
        pn->play_note({.start = 0.0f, .duration = 0.4f, .noteNumber = 60.0f, .velocity = 0.8f, .onset = 0, .hold = true});
        pn->play_note({.start = 0.4f, .duration = 0.4f, .noteNumber = 67.0f, .velocity = 0.8f, .onset = 0, .hold = false});
        auto bn = render_all(instant, N);

        // f0 from INTERPOLATED rising zero crossings (a raw crossing count
        // over 30 ms quantizes to ~70 cents — far too coarse for a
        // 30-cent gate).
        auto f0 = [&](const std::vector<float>& b, float fromSec, float toSec) {
            int a = int(fromSec * sr), z = int(toSec * sr);
            double first = -1.0, last = -1.0;
            int cycles = -1;
            for (int i = a + 1; i < z; ++i) {
                float p = b[size_t(i-1)], c = b[size_t(i)];
                if (p < 0.0f && c >= 0.0f) {
                    double t = double(i - 1) + double(-p) / double(c - p);
                    if (first < 0.0) { first = t; cycles = 0; }
                    else { last = t; ++cycles; }
                }
            }
            if (cycles <= 0) return 0.0f;
            return float(double(cycles) * double(sr) / (last - first));
        };
        const float g4 = 391.995f;
        float cents = 1200.0f * std::log2(f0(bg, 0.415f, 0.445f) / g4);
        CHECK(std::fabs(cents) < 30.0f);
        // Boundary smoothness over the first 5 ms of the glide.
        auto maxStep = [&](const std::vector<float>& b) {
            float m = 0.0f;
            for (int i = int(0.400f * sr) + 1; i < int(0.405f * sr); ++i)
                m = std::max(m, std::fabs(b[size_t(i)] - b[size_t(i-1)]));
            return m;
        };
        CHECK(maxStep(bg) < maxStep(bn));
    }

    // (e) finish_open_lines (§4.3): a line left open at score end is
    //     released, not leaked — the output decays away.
    {
        const int N = int(2.0f * sr);
        auto ip = load_inst(hold_patch_json(15.0f));
        auto* pi = dynamic_cast<PitchedInstrument*>(ip.instrument.get());
        pi->play_note({.start = 0.0f, .duration = 0.5f, .noteNumber = 60.0f, .velocity = 0.8f, .onset = 0, .hold = true});
        pi->finish_open_lines();
        auto b = render_all(ip, N);
        float peak = 0.0f;
        for (float v : b) peak = std::max(peak, std::fabs(v));
        CHECK(peak > 0.1f);
        CHECK(rms_of(b, 1.20f, 1.60f) < 0.001f * peak);   // < -60 dBFS
    }

    // (f) Mid-line retune still re-drives the living voice: the second
    //     half of a C4 -> G4 line oscillates ~1.5x faster (the v1
    //     assertion, re-expressed per note).
    {
        const int N = int(1.6f * sr);
        auto c = load_scoreless("patches/baselines/BaselineSIN.json");
        auto* pc = dynamic_cast<PitchedInstrument*>(c.instrument.get());
        pc->play_note({.start = 0.0f, .duration = 0.5f, .noteNumber = 60.0f, .velocity = 0.8f, .onset = 0, .hold = true});
        pc->play_note({.start = 0.5f, .duration = 0.5f, .noteNumber = 67.0f, .velocity = 0.8f, .onset = 0, .hold = false});
        auto bufC = render_all(c, N);
        auto zc = [&](int from, int to) {
            int n = 0;
            for (int i = from + 1; i < to; ++i)
                if ((bufC[size_t(i)] >= 0) != (bufC[size_t(i-1)] >= 0)) ++n;
            return n;
        };
        int half = 24000;
        float ratio = float(zc(half, 2 * half)) / float(std::max(1, zc(0, half)));
        CHECK(ratio > 1.35f && ratio < 1.65f);   // 392/261.6 = 1.498
    }
}

// Renders notes 60 and 72 of an instrument patch whose Note node drives `osc`'s
// frequency: the load must succeed, the output be finite and audible, and the
// octave double the zero crossings, so the note's pitch reached the oscillator.
static void check_note_pitch_reaches_osc(const std::string& patch) {
    const int n = 12000;
    auto render = [&](float midi) {
        auto ip = load_inst(patch);
        auto* pi = dynamic_cast<PitchedInstrument*>(ip.instrument.get());
        pi->play_note({.start = 0.0f, .duration = 0.4f, .noteNumber = midi, .velocity = 0.8f});
        std::vector<float> b(size_t(n), 0.0f);
        ip.instrument->render(b.data(), n);
        return b;
    };
    std::vector<float> lo, hi;
    bool loaded = true;
    try { lo = render(60.0f); hi = render(72.0f); }
    catch (const std::exception&) { loaded = false; }
    CHECK(loaded);
    if (!loaded) return;

    bool finite = true;
    float peak = 0.0f;
    for (float v : lo) { finite = finite && std::isfinite(v); peak = std::max(peak, std::fabs(v)); }
    CHECK(finite);
    CHECK(peak > 0.1f);
    auto crossings = [](const std::vector<float>& b) {
        int c = 0;
        for (size_t i = 1; i < b.size(); ++i) c += (b[i] >= 0.0f) != (b[i - 1] >= 0.0f);
        return c;
    };
    const float ratio = float(crossings(hi)) / float(std::max(1, crossings(lo)));
    CHECK(ratio > 1.8f && ratio < 2.2f);       // an octave up doubles the frequency
}

// A Note node wired to an oscillator's frequency carries the note's pitch to
// it; and the same wire inside a MultiplexSource's template, where each of
// the three clones builds its own copy of the Note node, which reads the
// voice's pitch, so the pitch reaches every clone.
static void run_note_wire_tests() {
    check_note_pitch_reaches_osc(R"({
      "sampleRate": 48000,
      "instrument": { "polyphony": 2 },
      "graph": { "output": "osc", "nodes": [
        { "id": "pf", "type": "PerformNode", "params": { "field": "frequency" } },
        { "id": "osc", "type": "SineSource", "params": { "frequency": { "ref": "pf" }, "phase": 0.0 } } ] }
    })");
    check_note_pitch_reaches_osc(R"({
      "sampleRate": 48000,
      "instrument": { "polyphony": 2 },
      "graph": { "output": "mux", "nodes": [
        { "id": "pf", "type": "PerformNode", "params": { "field": "frequency" } },
        { "id": "osc", "type": "SineSource", "params": { "frequency": { "ref": "pf" }, "phase": 0.0 } },
        { "id": "mux", "type": "MultiplexSource",
          "params": { "count": 3, "source": { "ref": "osc" } } } ] }
    })");
}

// A Multiplex voice follows a bend: the same three-clone template as above, one
// note rendered plain and once carrying a PitchCurve on its PerformedNote (the
// field the Conductor fills for a bend). The bend holds +7 semitones for the
// whole note, so every clone's Note node reads the bent frequency and the zero
// crossings rise by 2^(7/12) = 1.498.
static void run_multiplex_bend_tests() {
    std::printf("a Multiplex voice follows a bend\n");
    const char* patch = R"({
      "sampleRate": 48000,
      "instrument": { "polyphony": 2 },
      "graph": { "output": "mux", "nodes": [
        { "id": "pf", "type": "PerformNode", "params": { "field": "frequency" } },
        { "id": "osc", "type": "SineSource", "params": { "frequency": { "ref": "pf" }, "phase": 0.0 } },
        { "id": "mux", "type": "MultiplexSource",
          "params": { "count": 3, "source": { "ref": "osc" } } } ] }
    })";
    PitchCurve up7;
    up7.semi = {7.0f};
    up7.dur = {0.0f};                  // one hold that fills the note
    const int n = 12000;               // inside the 0.4 s note
    auto render = [&](std::optional<PitchCurve> curve) {
        auto ip = load_inst(patch);
        auto* pi = dynamic_cast<PitchedInstrument*>(ip.instrument.get());
        pi->play_note({.start = 0.0f, .duration = 0.4f, .noteNumber = 60.0f, .velocity = 0.8f, .onset = 0, .hold = false, .curve = curve});
        std::vector<float> b(size_t(n), 0.0f);
        ip.instrument->render(b.data(), n);
        return b;
    };
    std::vector<float> plain, bent;
    bool loaded = true;
    try { plain = render(std::nullopt); bent = render(up7); }
    catch (const std::exception& e) { loaded = false; std::printf("  load failed: %s\n", e.what()); }
    CHECK(loaded);
    if (!loaded) return;

    CHECK(plain != bent);
    float peak = 0.0f;
    for (float v : bent) peak = std::max(peak, std::fabs(v));
    CHECK(peak > 0.1f);
    auto crossings = [](const std::vector<float>& b) {
        int c = 0;
        for (size_t i = 1; i < b.size(); ++i) c += (b[i] >= 0.0f) != (b[i - 1] >= 0.0f);
        return c;
    };
    const float ratio = float(crossings(bent)) / float(std::max(1, crossings(plain)));
    std::printf("  zero crossings plain %d, bent %d, ratio %.3f\n", crossings(plain), crossings(bent), ratio);
    CHECK(ratio > 1.40f && ratio < 1.60f);    // 2^(7/12) = 1.498, upward
}

static void run_envelope_retrigger_tests() {
    // Seconds-mode gesture: 10ms 1.0->0.2, 20ms 0.2->1.0, expand@1.0.
    const int sr = 48000;
    Envelope env;
    env.set_setting("timeMode", 1.0f);
    env.add_stage({{1.0f, 0.2f, RampType::Linear, 0.0f}, 0.010f, 0.0f, 0.0f});
    env.add_stage({{0.2f, 1.0f, RampType::Linear, 0.0f}, 0.020f, 0.0f, 0.0f});
    env.add_stage({{1.0f, 1.0f, RampType::Linear, 0.0f}, 0.0f,   0.0f, 0.0f});
    env.prepare(RenderContext{sr}); env.start(sr);   // 1.0 s

    float last = 0.0f;
    for (int i = 0; i < sr / 2; ++i) last = env.next();
    CHECK_NEAR(last, 1.0f, 1e-4f);        // settled on expand

    env.retrigger();
    float first = env.next();
    CHECK(std::fabs(first - last) < 0.02f);   // no step at the restart
    float minSeen = first;
    for (int i = 1; i < int(0.010f * sr) + 4; ++i)
        minSeen = std::min(minSeen, env.next());
    CHECK_NEAR(minSeen, 0.2f, 0.02f);     // the dip fired
    float v = 0.0f;
    for (int i = 0; i < int(0.030f * sr); ++i) v = env.next();
    CHECK_NEAR(v, 1.0f, 1e-3f);           // recovered to neutral
    // Holds neutral through what remains of the prepared second.
    for (int i = 0; i < sr / 4; ++i) v = env.next();
    CHECK_NEAR(v, 1.0f, 1e-3f);
}

// Release re-layout (spec 2026-09-20-note-onsets-v2 §5): the release
// stage's sample count was computed at voice birth against note 1's
// duration; at gate_release the caller may re-reference it to the
// RELEASING note's duration.
static void run_release_relayout_tests() {
    const int sr = 48000;
    // adsr-ish: attack 10%, decay 10%, expand, release 25% (percent mode)
    Envelope env;
    env.add_stage({{0.0f, 1.0f, RampType::Linear, 0.0f}, 0.10f, 0.0f, 0.0f});
    env.add_stage({{1.0f, 0.7f, RampType::Linear, 0.0f}, 0.10f, 0.0f, 0.0f});
    env.add_stage({{0.7f, 0.7f, RampType::Linear, 0.0f}, 0.0f,  0.0f, 0.0f});
    env.add_stage({{0.7f, 0.0f, RampType::Linear, 0.0f}, 0.25f, 0.0f, 0.0f});
    env.set_gated(true);
    env.prepare(RenderContext{sr}); env.start(int(0.4f * sr));      // quarter note
    for (int i = 0; i < int(0.35f * sr); ++i) env.next(); // into the hold
    // Release re-referenced to a HALF note: 25% of 0.8 s = 0.2 s.
    int rem = env.gate_release(int(0.8f * sr));
    CHECK(std::abs(rem - int(0.25f * 0.8f * sr)) <= 2);
    // Default arg keeps old behavior: fresh envelope, no ref. The
    // prepare-time layout resolves percent against the LAYOUT window,
    // i.e. the note minus the engine-wide reflection allowance — that
    // 10 ms is why the expectation is not a flat 25% of 0.4 s.
    Envelope e2;
    e2.add_stage({{0.0f, 1.0f, RampType::Linear, 0.0f}, 0.10f, 0.0f, 0.0f});
    e2.add_stage({{1.0f, 1.0f, RampType::Linear, 0.0f}, 0.0f,  0.0f, 0.0f});
    e2.add_stage({{1.0f, 0.0f, RampType::Linear, 0.0f}, 0.25f, 0.0f, 0.0f});
    e2.set_gated(true);
    e2.prepare(RenderContext{sr}); e2.start(int(0.4f * sr));
    for (int i = 0; i < int(0.2f * sr); ++i) e2.next();
    int rem2 = e2.gate_release();
    CHECK(std::abs(rem2 - int(0.25f * (0.4f - Envelope::kReflectionAllowanceSec)
                              * sr)) <= 2);
}

#include "mforce/source/modulator/name_gate.h"

static void run_name_gate_tests() {
    auto ps = std::make_shared<PerformSource>();
    auto in = std::make_shared<PerformOut>(ps, PerformOut::Field::Onset);
    auto ng = std::make_shared<NameGate>();
    ng->set_param("in", in);
    ng->targetId = 2.0f;
    ps->set_note(440.0f, 0.8f, 100, 0.0f, nullptr, 2.0f);
    CHECK(ng->current() == 1.0f);
    CHECK(ng->next() == 1.0f);
    ps->set_note(440.0f, 0.8f, 100, 0.0f, nullptr, 1.0f);
    CHECK(ng->current() == 0.0f);
    ng->targetId = -1.0f;                    // unresolved never matches
    ps->set_note(440.0f, 0.8f, 100, 0.0f, nullptr, 0.0f);
    CHECK(ng->current() == 0.0f);
}

static void run_onset_trigger_tests() {
    // Minimal taught patch: sine * gesture envelope; gesture's trigger
    // wired Note.onset -> NameGate("tongue") -> Envelope.trigger.
    // Spec §6 end to end: the dip fires on the tongued note only.
    const char* kJson = R"({
      "sampleRate": 48000,
      "instrument": { "polyphony": 1, "onsets": ["tongue"] },
      "graph": {
        "output": "sine1",
        "nodes": [
          { "id": "perf1", "type": "PerformNode",
            "params": { "field": "onset" } },
          { "id": "ng1", "type": "NameGate",
            "params": { "name": "tongue", "in": { "ref": "perf1" } } },
          { "id": "gest1", "type": "Envelope",
            "params": { "timeMode": "seconds",
                        "trigger": { "ref": "ng1" },
              "startVal": [1.0, 0.1, 1.0], "endVal": [0.1, 1.0, 1.0],
              "power": [0.0, 0.0, 0.0], "type": ["Linear", "Linear", "Linear"],
              "percent": [0.01, 0.02, 0.0], "minSec": [0.0, 0.0, 0.0],
              "maxSec": [0.0, 0.0, 0.0], "nominal": [0.0, 0.0, 0.0] } },
          { "id": "sine1", "type": "SineSource",
            "params": { "frequency": 220.0, "amplitude": { "ref": "gest1" },
                        "phase": 0.0 } }
        ]
      }
    })";
    auto ip = load_inst(kJson);
    auto* pi = dynamic_cast<PitchedInstrument*>(ip.instrument.get());
    CHECK(pi != nullptr);
    pi->play_note({.start = 0.0f, .duration = 0.5f, .noteNumber = 57.0f, .velocity = 0.8f, .onset = 0, .hold = true});
    pi->play_note({.start = 0.5f, .duration = 0.5f, .noteNumber = 57.0f, .velocity = 0.8f, .onset = pi->instrument_class().onset_id("tongue"), .hold = false});
    const int N = int(1.2f * 48000);
    std::vector<float> buf(size_t(N), 0.0f);
    RenderContext ctx{48000};
    ip.instrument->render(buf.data(), N);

    auto rms = [&](float fromSec, float toSec) {
        int a = int(fromSec * 48000), b = int(toSec * 48000);
        double s = 0.0;
        for (int i = a; i < b; ++i) s += double(buf[size_t(i)]) * buf[size_t(i)];
        return float(std::sqrt(s / double(b - a)));
    };
    float before = rms(0.45f, 0.49f);          // settled, pre-boundary
    float dip    = rms(0.507f, 0.513f);        // around the dip minimum
                                               // (10ms fall + start of rise)
    float after  = rms(0.60f, 0.90f);          // recovered sustain
    CHECK(dip < 0.5f * before);                // the consonant fired
    CHECK(after > 0.9f * before);              // and got out of the way
    // Control: same line, no onset name — no dip.
    auto ip2 = load_inst(kJson);
    auto* pi2 = dynamic_cast<PitchedInstrument*>(ip2.instrument.get());
    pi2->play_note({.start = 0.0f, .duration = 0.5f, .noteNumber = 57.0f, .velocity = 0.8f, .onset = 0, .hold = true});
    pi2->play_note({.start = 0.5f, .duration = 0.5f, .noteNumber = 57.0f, .velocity = 0.8f, .onset = 0, .hold = false});
    std::vector<float> buf2(size_t(N), 0.0f);
    ip2.instrument->render(buf2.data(), N);
    double s = 0.0;
    int a = int(0.500f * 48000), b = int(0.508f * 48000);
    for (int i = a; i < b; ++i) s += double(buf2[size_t(i)]) * buf2[size_t(i)];
    float dip2 = float(std::sqrt(s / double(b - a)));
    CHECK(dip2 > 0.9f * before);
}

static void run_note_reseed_tests() {
    // Per-note determinism (onsets-v2 addendum): an in-line note's Setup
    // re-anchors stochastic draws, so a held-line note's noise realization
    // equals a fresh note's. Patch: white noise * gated sustain envelope.
    // Property: with reseed, note 2's samples in the flat-sustain window
    // REPLAY note 1's (same draw index from anchor, same sustain level);
    // free-running draws would diverge at every sample.
    const char* kJson = R"({
      "sampleRate": 48000,
      "instrument": { "polyphony": 1, "sustaining": true },
      "graph": {
        "output": "wn1",
        "nodes": [
          { "id": "env1", "type": "Envelope",
            "params": {
              "startVal": [0.0, 1.0, 0.7, 0.7], "endVal": [1.0, 0.7, 0.7, 0.0],
              "power": [0.0, 0.0, 0.0, 0.0], "type": ["Linear", "Linear", "Linear", "Linear"],
              "percent": [0.1, 0.1, 0.0, 0.15], "minSec": [0.0, 0.0, 0.0, 0.0],
              "maxSec": [0.0, 0.0, 0.0, 0.0], "nominal": [0.0, 0.0, 0.0, 0.0] } },
          { "id": "wn1", "type": "WhiteNoiseSource",
            "params": { "amplitude": { "ref": "env1" } } }
        ]
      }
    })";
    auto ip = load_inst(kJson);
    auto* pi = dynamic_cast<PitchedInstrument*>(ip.instrument.get());
    CHECK(pi != nullptr);
    pi->play_note({.start = 0.0f, .duration = 0.5f, .noteNumber = 57.0f, .velocity = 0.8f, .onset = 0, .hold = true, .curve = std::nullopt});
    pi->play_note({.start = 0.5f, .duration = 0.5f, .noteNumber = 57.0f, .velocity = 0.8f, .onset = 0, .hold = false, .curve = std::nullopt});
    const int N = int(0.5f * 48000);
    std::vector<float> buf(size_t(int(1.6f * 48000)), 0.0f);
    RenderContext ctx{48000};
    ip.instrument->render(buf.data(), int(buf.size()));
    // Flat-sustain overlap: note 1 is at sustain from 25% on; note 2 is
    // HELD at sustain throughout (no re-attack). Skip the last 10% for
    // note 2's release re-layout territory.
    int from = int(0.25f * N), to = int(0.85f * N);
    int same = 0, total = 0;
    for (int k = from; k < to; ++k) {
        ++total;
        if (buf[size_t(k)] == buf[size_t(N + k)]) ++same;
    }
    CHECK(same == total);   // exact replay — draws re-anchored
}

#include "mforce/render/embedded_score.h"
#include <filesystem>

// A patch file in the scratch folder for the tests that read a file: written when made (the folder too, since a fresh clone has
// none), removed when it goes out of scope.
struct ScratchPatch {
    std::string path;
    ScratchPatch(const char* p, const std::string& text) : path(p) {
        std::filesystem::create_directories("renders/scratch");
        std::ofstream f(path);
        f << text;
    }
    // The error_code overload: a destructor must not throw, and a file that
    // will not go away is not this test's failure.
    ~ScratchPatch() { std::error_code ec; std::filesystem::remove(path, ec); }
    ScratchPatch(const ScratchPatch&) = delete;
    ScratchPatch& operator=(const ScratchPatch&) = delete;
};

// The voice count of the one instrument channel build_sound builds from a
// patch file; -1, with the reason printed, when the load throws or the
// mixer is not that shape.
static int file_voice_count(const char* path, const std::string& text) {
    ScratchPatch file(path, text);
    int voices = -1;
    try {
        auto built = load_sound(file.path);
        if (built.mixer && built.mixer->channels.size() == 1)
            if (auto* pi = dynamic_cast<PitchedInstrument*>(built.mixer->channels[0].source.get()))
                voices = int(pi->voicePool.size());
    }
    catch (const std::exception& e) { std::printf("  load failed: %s\n", e.what()); }
    return voices;
}

// The voice pool's size: the file's "polyphony" (4 when the block has none),
// floored by the caller's minimum; build_sound's floor is 0. The
// rate is the file's "sampleRate", 48000 when it has none.
static void run_voice_pool_tests() {
    std::printf("the voice pool: the file's polyphony, the default, the floor; the default rate\n");
    const char* two = R"({"sampleRate":48000,"instrument":{"polyphony":2},"graph":{"output":"osc","nodes":[
      {"id":"osc","type":"SineSource","params":{"frequency":220.0}}]}})";
    const char* six = R"({"sampleRate":48000,"instrument":{"polyphony":6},"graph":{"output":"osc","nodes":[
      {"id":"osc","type":"SineSource","params":{"frequency":220.0}}]}})";
    const char* unstated = R"({"sampleRate":48000,"instrument":{},"graph":{"output":"osc","nodes":[
      {"id":"osc","type":"SineSource","params":{"frequency":220.0}}]}})";
    const char* noRate = R"({"instrument":{"polyphony":1},"graph":{"output":"osc","nodes":[
      {"id":"osc","type":"SineSource","params":{"frequency":220.0}}]}})";
    const char* none = R"({"sampleRate":48000,"instrument":{"polyphony":0},"graph":{"output":"osc","nodes":[
      {"id":"osc","type":"SineSource","params":{"frequency":220.0}}]}})";
    CHECK(load_inst(two, 5).instrument->voicePool.size() == 5);
    CHECK(load_inst(two, 0).instrument->voicePool.size() == 2);
    CHECK(load_inst(six, 3).instrument->voicePool.size() == 6);   // a floor below is no cut
    CHECK(load_inst(unstated, 0).instrument->voicePool.size() == 4);
    auto defaultRate = load_inst(noRate);
    CHECK(defaultRate.sampleRate == 48000);
    CHECK(defaultRate.instrument->render_context().sampleRate == 48000);
    CHECK(file_voice_count("renders/scratch/_voice_pool_two.json", two) == 2);
    // A file asking for no voices gets none: the standalone floor is 0, not 1.
    CHECK(file_voice_count("renders/scratch/_voice_pool_none.json", none) == 0);
}

// The streaming voice's gain is the note's velocity times the instrument's
// pre-clip volume: a streaming caller never passes through render(), so the
// volume rides along on the voice.
static void run_voice_gain_tests() {
    std::printf("the streaming voice's gain: velocity times the instrument's volume\n");
    const char* one = R"({"sampleRate":48000,"instrument":{"polyphony":1},"graph":{"output":"osc","nodes":[
      {"id":"osc","type":"SineSource","params":{"frequency":220.0}}]}})";
    auto ip = load_inst(one);
    ip.instrument->volume = 0.5f;
    CHECK(ip.instrument->prepare_voice_at(0, 60.0f, 0.8f, 0.1f).gain == 0.8f * 0.5f);
    ip.instrument->volume = 1.0f;
    CHECK(ip.instrument->prepare_voice_at(0, 60.0f, 0.8f, 0.1f).gain == 0.8f);
}

// The frame count build_sound gives a patch file; -1, with the
// reason printed, when the load throws.
static int frames_of(const char* path, const char* text) {
    ScratchPatch file(path, text);
    int frames = -1;
    try { frames = load_sound(file.path).frames; }
    catch (const std::exception& e) { std::printf("  load failed: %s\n", e.what()); }
    return frames;
}

// The score a patch file carries under "score": performed at load and its end
// returned in seconds; build_sound turns the end into the frame count.
static void run_embedded_score_tests() {
    std::printf("the embedded score: its end, hold, the phrased refusal, the frames\n");
    const char* plain = R"({"sampleRate":48000,"instrument":{"polyphony":1},"graph":{"output":"osc","nodes":[
      {"id":"osc","type":"SineSource","params":{"frequency":220.0}}]}})";
    const char* sustaining = R"({"sampleRate":48000,"instrument":{"polyphony":1,"sustaining":true},"graph":{"output":"osc","nodes":[
      {"id":"osc","type":"SineSource","params":{"frequency":220.0}}]}})";
    // A fresh instrument per score.
    auto end_of = [](const char* patch, const char* score) {
        auto ip = load_inst(patch);
        return play_embedded_score(*ip.instrument, score);
    };
    // A note with no "time" starts where its predecessor ended.
    CHECK(end_of(plain, R"([{"note":60,"time":0.5,"duration":1.0},{"note":62,"duration":0.25}])") == 1.75);
    // The end is the maximum, not the last note's.
    CHECK(end_of(plain, R"([{"note":60,"time":0,"duration":3},{"note":62,"time":1,"duration":0.5}])") == 3.0);
    CHECK(end_of(plain, "[]") == 0.0);
    // "hold" on a non-sustaining instrument is ignored and the end still
    // counted: no line is open after the first note, so the second takes its
    // articulation. The same score on a sustaining instrument is refused,
    // because there the line is open.
    {
        const char* heldThenArticulated = R"([{"note":60,"time":0,"duration":1,"hold":true},
          {"note":62,"duration":1,"articulation":{"type":"Staccato"}}])";
        bool threw = false;
        double end = -1.0;
        try { end = end_of(plain, heldThenArticulated); }
        catch (const std::exception&) { threw = true; }
        CHECK(!threw);
        CHECK(end == 2.0);
        bool named = false;
        try { (void)end_of(sustaining, heldThenArticulated); }
        catch (const std::exception& e) { named = std::string(e.what()).find("phrased") != std::string::npos; }
        CHECK(named);
    }
    // Articulation on a phrased note is refused, naming "phrased".
    {
        bool named = false;
        try { (void)end_of(sustaining, R"([{"note":60,"time":0,"duration":1,"hold":true,"articulation":"staccato"}])"); }
        catch (const std::exception& e) { named = std::string(e.what()).find("phrased") != std::string::npos; }
        CHECK(named);
    }
    // build_sound's frame arithmetic: the render covers at least the score, a
    // larger "seconds" extends it.
    CHECK(frames_of("renders/scratch/_embedded_score_noseconds.json",
        R"({"sampleRate":48000,"instrument":{"polyphony":1},"graph":{"output":"osc","nodes":[
          {"id":"osc","type":"SineSource","params":{"frequency":220.0}}]},
          "score":[{"note":60,"time":0.5,"duration":1.0},{"note":62,"duration":0.25}]})") == 84000);
    CHECK(frames_of("renders/scratch/_embedded_score_seconds5.json",
        R"({"sampleRate":48000,"seconds":5,"instrument":{"polyphony":1},"graph":{"output":"osc","nodes":[
          {"id":"osc","type":"SineSource","params":{"frequency":220.0}}]},
          "score":[{"note":60,"time":0.5,"duration":1.0},{"note":62,"duration":0.25}]})") == 240000);
    CHECK(frames_of("renders/scratch/_embedded_score_seconds1.json",
        R"({"sampleRate":48000,"seconds":1,"instrument":{"polyphony":1},"graph":{"output":"osc","nodes":[
          {"id":"osc","type":"SineSource","params":{"frequency":220.0}}]},
          "score":[{"note":60,"time":0.5,"duration":1.0},{"note":62,"duration":0.25}]})") == 84000);
}

// The score's end is double arithmetic on the JSON values, and build_sound
// rounds it to frames; a file with neither a score nor "seconds" renders five
// seconds.
static void run_embedded_score_end_tests() {
    std::printf("the embedded score's end: double, rounded to frames, the five-second default\n");
    const char* plain = R"({"sampleRate":48000,"instrument":{"polyphony":1},"graph":{"output":"osc","nodes":[
      {"id":"osc","type":"SineSource","params":{"frequency":220.0}}]}})";
    {
        auto ip = load_inst(plain);
        // 0.1 + 0.2 in double is not 0.1f + 0.2f widened.
        CHECK(play_embedded_score(*ip.instrument, R"([{"note":60,"time":0.1,"duration":0.2}])") == 0.1 + 0.2);
    }
    // 1.0000125 s at 48000 is 48000.6 frames: rounded, not truncated.
    CHECK(frames_of("renders/scratch/_embedded_score_rounding.json",
        R"({"sampleRate":48000,"instrument":{"polyphony":1},"graph":{"output":"osc","nodes":[
          {"id":"osc","type":"SineSource","params":{"frequency":220.0}}]},
          "score":[{"note":60,"time":0,"duration":1.0000125}]})") == 48001);
    CHECK(frames_of("renders/scratch/_embedded_score_none.json", plain) == 240000);
}

// What the embedded score delivers to the instrument, observed on it: where
// each note starts, the velocity a note without one gets, which notes are
// phrased, and the open line finished at the end.
static void run_embedded_score_delivery_tests() {
    std::printf("the embedded score's delivery: starts, the velocity default, onset-only, the open line\n");
    const char* plain = R"({"sampleRate":48000,"instrument":{"polyphony":3},"graph":{"output":"osc","nodes":[
      {"id":"osc","type":"SineSource","params":{"frequency":220.0}}]}})";
    // A note with no "time" starts where its predecessor ended.
    {
        auto ip = load_inst(plain);
        (void)play_embedded_score(*ip.instrument,
            R"([{"note":60,"time":0.25,"duration":0.5},{"note":62,"duration":0.25},{"note":64,"duration":0.5}])");
        const auto& notes = ip.instrument->renderedNotes;
        const int sr = ip.instrument->render_context().sampleRate;
        CHECK(notes.size() == 3);
        CHECK(notes.size() == 3 && notes[0].startSample == int(0.25 * sr)
              && notes[1].startSample == int(0.75 * sr) && notes[2].startSample == int(1.0 * sr));
    }
    // A note with no "velocity" is played at 0.8.
    {
        auto delivered = [&](const char* score) {
            auto ip = load_inst(plain);
            (void)play_embedded_score(*ip.instrument, score);
            const auto& notes = ip.instrument->renderedNotes;
            return notes.size() == 1 ? notes[0].samples : std::vector<float>{};
        };
        const auto unstated = delivered(R"([{"note":60,"time":0,"duration":0.25}])");
        CHECK(!unstated.empty());
        CHECK(unstated == delivered(R"([{"note":60,"time":0,"duration":0.25,"velocity":0.8}])"));
        CHECK(unstated != delivered(R"([{"note":60,"time":0,"duration":0.25,"velocity":0.4}])"));
    }
    // A note with an onset and no hold is phrased: articulation is refused on
    // it, and without the articulation it is delivered.
    {
        const char* plucked = R"({"sampleRate":48000,"instrument":{"polyphony":1,"onsets":["pluck"]},"graph":{"output":"osc","nodes":[
          {"id":"osc","type":"SineSource","params":{"frequency":220.0}}]}})";
        bool named = false;
        try {
            auto ip = load_inst(plucked);
            (void)play_embedded_score(*ip.instrument,
                R"([{"note":60,"time":0,"duration":0.5,"onset":"pluck","articulation":{"type":"Staccato"}}])");
        }
        catch (const std::exception& e) { named = std::string(e.what()).find("phrased") != std::string::npos; }
        CHECK(named);
        auto ip = load_inst(plucked);
        (void)play_embedded_score(*ip.instrument, R"([{"note":60,"time":0,"duration":0.5,"onset":"pluck"}])");
        CHECK(ip.instrument->renderedNotes.size() == 1);
    }
    // A score ending on a held note leaves no line open: the line reaches the
    // timeline at the end.
    {
        const char* sustaining = R"({"sampleRate":48000,"instrument":{"polyphony":1,"sustaining":true},"graph":{"output":"osc","nodes":[
          {"id":"env","type":"Envelope","params":{"startVal":[0.0,1.0,1.0],"endVal":[1.0,1.0,0.0],
            "power":[0.0,0.0,0.0],"type":["Linear","Linear","Linear"],"percent":[0.05,0.0,0.25],
            "minSec":[0.0,0.0,0.0],"maxSec":[0.0,0.0,0.0],"nominal":[0.0,0.0,0.0]}},
          {"id":"osc","type":"SineSource","params":{"frequency":220.0,"amplitude":{"ref":"env"}}}]}})";
        auto ip = load_inst(sustaining);
        (void)play_embedded_score(*ip.instrument,
            R"([{"note":60,"time":0,"duration":0.5},{"note":62,"duration":0.5,"hold":true}])");
        CHECK(ip.instrument->renderedNotes.size() == 2);
    }
}

// The codec hands the score over as text. Every score block in
// patches/library and patches/baselines reads back from its text to the same
// JSON, the same text, the same float and double of each number and the same
// number type.
static void run_embedded_score_text_tests() {
    std::printf("the embedded score as text: every score in the measurement scope reads back the same\n");
    int checked = 0;
    for (const char* dir : {"patches/library", "patches/baselines"}) {
        for (const auto& entry : std::filesystem::recursive_directory_iterator(dir)) {
            if (!entry.is_regular_file() || entry.path().extension() != ".json") continue;
            std::ifstream f(entry.path());
            std::stringstream ss; ss << f.rdbuf();
            nlohmann::json root = nlohmann::json::parse(ss.str());
            if (!root.is_object() || !root.contains("score")) continue;
            const nlohmann::json& score = root.at("score");
            nlohmann::json re = nlohmann::json::parse(score.dump());
            bool same = re == score && re.dump() == score.dump() && re.size() == score.size();
            for (size_t i = 0; same && i < score.size(); ++i) {
                for (const char* key : {"note", "velocity", "duration", "time"}) {
                    if (!score[i].is_object() || !score[i].contains(key)) continue;
                    const auto& a = score[i].at(key);
                    const auto& b = re[i].at(key);
                    same = same
                        && std::bit_cast<uint32_t>(a.get<float>()) == std::bit_cast<uint32_t>(b.get<float>())
                        && std::bit_cast<uint64_t>(a.get<double>()) == std::bit_cast<uint64_t>(b.get<double>())
                        && a.is_number_integer() == b.is_number_integer()
                        && a.is_number_float() == b.is_number_float();
                }
            }
            if (!same) std::printf("  score text differs: %s\n", entry.path().string().c_str());
            CHECK(same);
            ++checked;
        }
    }
    std::printf("  score-bearing files checked: %d\n", checked);
}

#include "mforce/music/passage_melody.h"
#include "mforce/music/templates_json.h"
#include <filesystem>

static void run_passage_melody_tests() {
    using namespace mforce;
    Scale c = Scale::get("C", "Major");
    // House convention: E5 = 64, D5 = 62, C5 = 60, G5 = 67.
    CHECK(scale_steps_between(64.0f, 62.0f, c) == -1);   // E -> D
    CHECK(scale_steps_between(62.0f, 60.0f, c) == -1);   // D -> C
    CHECK(scale_steps_between(64.0f, 67.0f, c) ==  2);   // E -> G (E F G)
    CHECK(scale_steps_between(64.0f, 64.0f, c) ==  0);
    CHECK(scale_steps_between(60.0f, 72.0f, c) ==  7);   // C5 -> C6, full octave
    CHECK(scale_steps_between(72.0f, 59.0f, c) == -8);   // C6 -> B4, crosses octave
    // Non-scale tone throws (crawl material must be diatonic).
    bool threw = false;
    try { scale_steps_between(60.0f, 61.0f, c); } catch (const std::exception&) { threw = true; }
    CHECK(threw);
    // Non-C tonic: G Major, F#5 = 66 is a scale tone, F5 = 65 is not.
    Scale g = Scale::get("G", "Major");
    CHECK(scale_steps_between(67.0f, 66.0f, g) == -1);   // G -> F#
    threw = false;
    try { scale_grid_index(65.0f, g); } catch (const std::exception&) { threw = true; }
    CHECK(threw);

    const std::string mary =
        "Eq Dq Cq Dq Eq Eq Eh Dq Dq Dh Eq Gq Gh | "
        "Eq Dq Cq Dq Eq Eq Eq Eq Dq Dq Eq Dq Cw";
    auto phrases = phrases_from_passage(mary, 5, c, 4.0f);
    CHECK(phrases.size() == 2);

    // Phrase 1: 4 figures, DENSE connectors (one per figure, [0] a dummy —
    // composer.h:1471 reads connectors[i] as the lead INTO figure i),
    // starts E5.
    const auto& p1 = phrases[0];
    CHECK(p1.figures.size() == 4);
    CHECK(p1.connectors.size() == 4);
    CHECK(p1.startingPitch && int(p1.startingPitch->note_number()) == 64);
    CHECK(p1.cadenceType == 0);
    // Every figure Locked with a lockedFigure present.
    for (const auto& ft : p1.figures) {
        CHECK(ft.source == FigureSource::Locked);
        CHECK(ft.lockedFigure.has_value());
    }
    // fig1 = E D C D: durations 1,1,1,1; steps 0,-1,-1,+1.
    {
        const auto& u = p1.figures[0].lockedFigure->units;
        CHECK(u.size() == 4);
        CHECK(u[0].duration == 1.0f && u[0].step == 0);
        CHECK(u[1].step == -1 && u[2].step == -1 && u[3].step == 1);
    }
    // fig4 = E G Gh: durations 1,1,2; steps 0,+2,0.
    {
        const auto& u = p1.figures[3].lockedFigure->units;
        CHECK(u.size() == 3);
        CHECK(u[2].duration == 2.0f);
        CHECK(u[0].step == 0 && u[1].step == 2 && u[2].step == 0);
    }
    // Connectors: [0] dummy 0, then +1 (D->E), -1 (E->D), +1 (D->E).
    CHECK(p1.connectors[0] && p1.connectors[0]->leadStep == 0);
    CHECK(p1.connectors[1] && p1.connectors[1]->leadStep == 1);
    CHECK(p1.connectors[2] && p1.connectors[2]->leadStep == -1);
    CHECK(p1.connectors[3] && p1.connectors[3]->leadStep == 1);

    // Phrase 2: last figure is the whole-note C alone in its bar.
    const auto& p2 = phrases[1];
    CHECK(p2.figures.size() == 4);
    CHECK(p2.connectors.size() == 4);
    CHECK(p2.startingPitch && int(p2.startingPitch->note_number()) == 64);
    {
        const auto& u = p2.figures[3].lockedFigure->units;
        CHECK(u.size() == 1 && u[0].duration == 4.0f && u[0].step == 0);
    }
    // fig3 = D D E D: steps 0,0,+1,-1; its lead connector is -1 (E->D).
    CHECK(p2.figures[2].lockedFigure->units[2].step == 1);
    CHECK(p2.connectors[2] && p2.connectors[2]->leadStep == -1);
    // fig4's lead is -1 (D->C).
    CHECK(p2.connectors[3] && p2.connectors[3]->leadStep == -1);

    // A '|'-free string is one phrase (parse marks every note phraseStart
    // only when grouping is active — verify we don't fragment).
    auto one = phrases_from_passage("Cq Dq Eq Fq", 5, c, 4.0f);
    CHECK(one.size() == 1 && one[0].figures.size() == 1);

    // Rests refuse loudly (parse's rest-ends-phrase rule would corrupt
    // structural grouping; revisit when a crawl tune needs rests).
    threw = false;
    try { phrases_from_passage("Cq Rq Eq", 5, c, 4.0f); }
    catch (const std::exception&) { threw = true; }
    CHECK(threw);

    // Schema + apply: template with a melodyPassageFile gets its phrases
    // derived; the two new fields round-trip; a missing file throws.
    {
        // Write a scratch .psg next to the test's CWD-independent temp dir.
        const std::string psgPath = "renders/scratch/_pm_test.psg";
        std::filesystem::create_directories("renders/scratch");
        { std::ofstream f(psgPath); f << "Eq Dq Cq Dq | Cq Cq Ch"; }

        json tj = json::parse(R"({
          "keyName": "C", "scaleName": "Major", "bpm": 80,
          "sections": [{"name": "Main", "beats": 8}],
          "parts": [{
            "name": "melody", "role": "melody",
            "passages": { "Main": {
              "melodyPassageFile": "renders/scratch/_pm_test.psg",
              "melodyOctave": 5, "phrases": [] } }
          }]
        })");
        PieceTemplate tmpl;
        from_json(tj, tmpl);
        CHECK(tmpl.parts[0].passages.at("Main").melodyPassageFile
              == "renders/scratch/_pm_test.psg");
        CHECK(tmpl.parts[0].passages.at("Main").melodyOctave == 5);

        apply_passage_melodies(tmpl);
        const auto& pass = tmpl.parts[0].passages.at("Main");
        CHECK(pass.phrases.size() == 2);
        CHECK(pass.phrases[0].figures.size() == 1);   // one bar
        CHECK(pass.phrases[1].figures.size() == 1);

        // Round-trip keeps the fields.
        json out; to_json(out, tmpl.parts[0].passages.at("Main"));
        CHECK(out.value("melodyPassageFile", std::string())
              == "renders/scratch/_pm_test.psg");
        CHECK(out.value("melodyOctave", 0) == 5);

        // Missing file names the path.
        tmpl.parts[0].passages.at("Main").melodyPassageFile = "no/such.psg";
        threw = false;
        try { apply_passage_melodies(tmpl); }
        catch (const std::exception& e) {
            threw = std::string(e.what()).find("no/such.psg") != std::string::npos;
        }
        CHECK(threw);
        std::filesystem::remove(psgPath);
    }
}

#include "mforce/music/anchor_selector.h"
#include "mforce/music/classical_composer.h"

static void run_walk1_tests() {
    using namespace mforce;
    Scale c = Scale::get("C", "Major");
    MelodyProfile prof = MelodyProfile::load_by_name("nursery_v1");

    // --- Complexify: target note count, duration preserved, deterministic,
    //     JSON name round-trips.
    {
        MelodicFigure rep3;
        rep3.units.push_back({1.0f, 0});
        rep3.units.push_back({1.0f, 0});
        rep3.units.push_back({2.0f, 0});
        auto a1 = figure_transforms::apply(rep3, TransformOp::Complexify, 4, 77u);
        CHECK(a1.note_count() == 4);
        CHECK(std::fabs(a1.total_duration() - 4.0f) < 1e-5f);
        auto a2 = figure_transforms::apply(rep3, TransformOp::Complexify, 4, 77u);
        bool same = a1.note_count() == a2.note_count();
        for (int k = 0; same && k < a1.note_count(); ++k)
            same = a1.units[k].duration == a2.units[k].duration
                && a1.units[k].step == a2.units[k].step;
        CHECK(same);
        json jt; to_json(jt, TransformOp::Complexify);
        CHECK(jt.get<std::string>() == std::string("complexify"));
        TransformOp back; from_json(jt, back);
        CHECK(back == TransformOp::Complexify);
    }

    // --- Derived motifs: chains resolve parent-first; missing parent throws
    //     by name; generationSeed makes a random op deterministic.
    {
        Motif a; a.name = "a"; a.userProvided = true;
        {
            MelodicFigure fa;
            fa.units.push_back({1.0f, 0});
            fa.units.push_back({1.0f, 1});
            a.content = fa;
        }
        Motif b; b.name = "b"; b.derivedFrom = "a";
        b.transform = TransformOp::Invert; b.content = MelodicFigure{};
        Motif d; d.name = "d"; d.derivedFrom = "b";
        d.transform = TransformOp::Reverse; d.content = MelodicFigure{};
        PieceTemplate tmpl; tmpl.motifs = {a, b, d};
        Randomizer rng(1u);
        realize_motifs(tmpl, rng);
        CHECK(tmpl.realizedMotifs.count("b") == 1);
        CHECK(tmpl.realizedMotifs.count("d") == 1);
        CHECK(tmpl.realizedMotifs.at("b").units[1].step == -1);

        PieceTemplate bad;
        Motif z; z.name = "z"; z.derivedFrom = "ghost";
        z.content = MelodicFigure{};
        bad.motifs = {z};
        bool threw = false;
        try { Randomizer r2(1u); realize_motifs(bad, r2); }
        catch (const std::exception& e) {
            threw = std::string(e.what()).find("ghost") != std::string::npos;
        }
        CHECK(threw);

        Motif v; v.name = "v"; v.derivedFrom = "a";
        v.transform = TransformOp::VarySteps; v.transformParam = 1;
        v.generationSeed = 42u; v.content = MelodicFigure{};
        PieceTemplate t2; t2.motifs = {a, v};
        PieceTemplate t3; t3.motifs = {a, v};
        Randomizer r3(9u);    realize_motifs(t2, r3);
        Randomizer r4(1234u); realize_motifs(t3, r4);
        const auto& u2 = t2.realizedMotifs.at("v").units;
        const auto& u3 = t3.realizedMotifs.at("v").units;
        bool sameV = u2.size() == u3.size();
        for (size_t k = 0; sameV && k < u2.size(); ++k)
            sameV = u2[k].step == u3[k].step;
        CHECK(sameV);
    }

    // --- Anchor selector: R1/R2/R3 hold across seeds; dense connectors;
    //     the low-scoring start is rarely chosen; parallel pinning wins.
    {
        HarmonyTimeline tl;
        ChordProgression prog;
        ScaleChord c1; c1.degree = 0; c1.quality = &ChordDef::get("Major");
        ScaleChord g7; g7.degree = 4; g7.quality = &ChordDef::get("7");
        prog.add(c1, 4.0f);
        prog.add(g7, 2.0f);
        prog.add(c1, 2.0f);
        tl.set_segment(0.0f, 8.0f, prog, "test");

        MelodicFigure fA;   // 4 beats, net -1
        fA.units.push_back({1.0f, 0});
        fA.units.push_back({1.0f, -1});
        fA.units.push_back({2.0f, 0});
        MelodicFigure fB;   // 4 beats, notes at beats 4,5,6 (G7,G7,C)
        fB.units.push_back({1.0f, 0});
        fB.units.push_back({1.0f, 1});
        fB.units.push_back({2.0f, 0});
        std::vector<const MelodicFigure*> figs = {&fA, &fB};
        const Pitch reg = Pitch::from_note_number(64.0f);   // E5 register

        for (uint32_t s = 1; s <= 30; ++s) {
            Randomizer r(s);
            PhraseTemplate local; local.name = "t";
            select_anchors(local, figs, tl, c, 0.0f, 4.0f, 1.0f,
                           /*isPassageFinal*/ true, std::nullopt, reg, r, prof, {}, 0.0f, false);
            CHECK(local.startingPitch.has_value());
            CHECK(local.connectors.size() == 2);
            CHECK(!local.connectors[0].has_value());
            CHECK(local.connectors[1].has_value());
            const int pc0 =
                ((int(local.startingPitch->note_number()) % 12) + 12) % 12;
            CHECK(pc0 == 0 || pc0 == 4 || pc0 == 7);        // R1
            const int g0 =
                scale_grid_index(local.startingPitch->note_number(), c);
            const int g1 = g0 + (-1) + local.connectors[1]->leadStep;
            const int last = g1 + 1;                        // fB net to last
            CHECK(((last % 7) + 7) % 7 == 0);               // R3 (tonic)
        }

        // Parallel pinning: a pinned start is taken verbatim.
        {
            Randomizer r(5u);
            PhraseTemplate local; local.name = "t2";
            select_anchors(local, figs, tl, c, 0.0f, 4.0f, 1.0f, true,
                           Pitch::from_note_number(67.0f), reg, r, prof, {}, 0.0f, false);
            CHECK(local.startingPitch
                  && int(local.startingPitch->note_number()) == 67);
        }

        // Weighting: single-figure phrase where anchors E/G keep both notes
        // chord tones and anchor C leaves its second (long, figure-final)
        // note off-chord — C should be a rare pick.
        {
            MelodicFigure fW;   // 4 beats: [0, -2], second note long+final
            fW.units.push_back({2.0f, 0});
            fW.units.push_back({2.0f, -2});
            std::vector<const MelodicFigure*> one = {&fW};
            int cPicks = 0;
            for (uint32_t s = 1; s <= 100; ++s) {
                Randomizer r(s);
                PhraseTemplate local; local.name = "w";
                select_anchors(local, one, tl, c, 0.0f, 4.0f, 1.0f,
                               /*isPassageFinal*/ false, std::nullopt, reg, r, prof, {}, 0.0f, false);
                const int pc0 =
                    ((int(local.startingPitch->note_number()) % 12) + 12) % 12;
                if (pc0 == 0) ++cPicks;
            }
            CHECK(cPicks < 15);
        }

        // Empty legal set throws with the phrase name: force R3 onto a
        // timeline whose final chord contains no tonic (B diminished-ish via
        // G7 only) — no chain can end on degree 1 as a chord tone.
        {
            HarmonyTimeline tl2;
            ChordProgression p2;
            p2.add(g7, 8.0f);
            tl2.set_segment(0.0f, 8.0f, p2, "test");
            Randomizer r(3u);
            PhraseTemplate local; local.name = "impossible";
            bool threw = false;
            try {
                select_anchors(local, figs, tl2, c, 0.0f, 4.0f, 1.0f,
                               true, std::nullopt, reg, r, prof, {}, 0.0f, false);
            } catch (const std::exception& e) {
                threw = std::string(e.what()).find("impossible")
                        != std::string::npos;
            }
            CHECK(threw);
        }
    }

    // --- End to end: harmonic anchorMode through ClassicalComposer.
    {
        json tj = json::parse(R"({
          "keyName": "C", "scaleName": "Major", "bpm": 80, "masterSeed": 7,
          "sections": [{"name": "Main", "beats": 8,
            "chordProgression": [
              {"degree": 0, "beats": 4},
              {"degree": 4, "quality": "7", "beats": 2},
              {"degree": 0, "beats": 2}]}],
          "motifs": [
            {"name": "m1", "userProvided": true, "figure": {"units": [
              {"duration": 1.0, "step": 0}, {"duration": 1.0, "step": -1},
              {"duration": 2.0, "step": 0}]}},
            {"name": "m2", "userProvided": true, "figure": {"units": [
              {"duration": 1.0, "step": 0}, {"duration": 1.0, "step": 1},
              {"duration": 2.0, "step": 0}]}}
          ],
          "parts": [{"name": "melody", "role": "melody", "passages": {
            "Main": {"anchorMode": "harmonic", "melodyProfile": "nursery_v1",
                     "startingPitch": {"octave": 5, "pitch": "E"},
                     "phrases": [{
                        "name": "p1",
                        "startingPitch": {"octave": 5, "pitch": "E"},
                        "figures": [
                          {"source": "reference", "motifName": "m1"},
                          {"source": "reference", "motifName": "m2"}]}]}}}]
        })");
        PieceTemplate tmpl; from_json(tj, tmpl);
        Piece piece;
        ClassicalComposer composer(tmpl.masterSeed);
        composer.compose(piece, tmpl);
        CHECK(!piece.parts.empty());
        CHECK(piece.parts[0].elementSequence.size() == 6);
        // First event: R1 pitch class; last event: tonic (R3).
        const auto& els = piece.parts[0].elementSequence.elements;
        const int pcF = ((int(els.front().note().noteNumber) % 12) + 12) % 12;
        const int pcL = ((int(els.back().note().noteNumber) % 12) + 12) % 12;
        CHECK(pcF == 0 || pcF == 4 || pcF == 7);
        CHECK(pcL == 0);
    }
}

static void run_walk2_tests() {
    using namespace mforce;
    Scale c = Scale::get("C", "Major");

    // --- elaborate(): rung 1 always splits the long note evenly; totals
    //     preserved; nothing below a sixteenth; late-placement bias; fine
    //     splits rare and mostly dotted.
    {
        MelodicFigure rep3;
        rep3.units.push_back({1.0f, 0});
        rep3.units.push_back({1.0f, 0});
        rep3.units.push_back({2.0f, 0});
        for (uint32_t s = 1; s <= 20; ++s) {
            Randomizer r(s);
            auto e4 = figure_transforms::elaborate(rep3, r, 4);
            CHECK(e4.note_count() == 4);
            bool allQ = true;
            for (auto& u : e4.units) if (u.duration != 1.0f) allQ = false;
            CHECK(allQ);                       // h -> q q, Mary's own move
            CHECK(e4.net_step() == 0);
        }
        int fineCount = 0, dottedCount = 0;
        double smallStartSum = 0.0;
        int smallCount = 0;
        for (uint32_t s = 1; s <= 400; ++s) {
            Randomizer r(s);
            auto e5 = figure_transforms::elaborate(rep3, r, 5);
            CHECK(std::fabs(e5.total_duration() - 4.0f) < 1e-4f);
            float minDur = 99.0f, minStart = 0.0f, start = 0.0f;
            bool hasDotted = false, hasFine = false;
            for (auto& u : e5.units) {
                CHECK(u.duration >= 0.25f - 1e-6f);
                if (u.duration < minDur) { minDur = u.duration; minStart = start; }
                if (u.duration == 0.75f) hasDotted = true;
                if (u.duration <= 0.25f + 1e-6f) hasFine = true;
                start += u.duration;
            }
            if (hasFine || hasDotted) { ++fineCount; if (hasDotted) ++dottedCount; }
            if (minDur < 1.0f) { smallStartSum += minStart; ++smallCount; }
        }
        // Depth policy: fine splits rare (~20% of the ONE post-rung split).
        CHECK(fineCount > 20 && fineCount < 160);
        // Dotted dominates fine (~80%).
        CHECK(dottedCount * 2 > fineCount);
        // Late-placement: mean start beat of the smallest unit is past the
        // bar's first half (uniform would be ~1.5 on a 4-beat figure).
        CHECK(smallCount > 0 && smallStartSum / smallCount > 1.6);
    }

    // --- Selector R4 (cadential register memory) + regression plumbing:
    //     final note must be a visited pitch when a visited set exists.
    {
        HarmonyTimeline tl;
        ChordProgression prog;
        ScaleChord c1; c1.degree = 0; c1.quality = &ChordDef::get("Major");
        prog.add(c1, 4.0f);
        tl.set_segment(0.0f, 4.0f, prog, "test");
        MelodicFigure whole;
        whole.units.push_back({4.0f, 0});
        std::vector<const MelodicFigure*> figs = {&whole};
        const Pitch reg = Pitch::from_note_number(64.0f);
        MelodyProfile prof = MelodyProfile::load_by_name("nursery_v1");
        // Prior track: C5 only (a whole note in the bar before).
        const std::vector<TrackNote> prior = {{scale_grid_index(60.0f, c), -4.0f, 4.0f}};
        for (uint32_t s = 1; s <= 20; ++s) {
            Randomizer r(s);
            PhraseTemplate local; local.name = "r4";
            auto res = select_anchors(local, figs, tl, c, 0.0f, 4.0f, 1.0f,
                           true, std::nullopt, reg, r, prof, prior, 4.0f, false);
            CHECK(local.startingPitch
                  && int(local.startingPitch->note_number()) == 60);
            CHECK(res.notes.size() == 1);      // the chain's notes, returned
        }
        // Without any visited set, C5 and C6 both stay legal (guard).
        bool saw60 = false, saw72 = false;
        for (uint32_t s = 1; s <= 60; ++s) {
            Randomizer r(s);
            PhraseTemplate local; local.name = "r4b";
            select_anchors(local, figs, tl, c, 0.0f, 4.0f, 1.0f,
                           true, std::nullopt, reg, r, prof, {}, 0.0f, false);
            const int nn = int(local.startingPitch->note_number());
            CHECK(nn == 60 || nn == 72);
            if (nn == 60) saw60 = true;
            if (nn == 72) saw72 = true;
        }
        CHECK(saw60);
        (void)saw72;   // 72 may be rare; only legality is asserted
    }
}

#include "mforce/music/melody_profile.h"
#include "mforce/music/style_table.h"
#include "mforce/music/note_map.h"
#include "mforce/music/phrase_critic.h"
#include <set>

static void run_walk3_tests() {
    using namespace mforce;
    // --- vary_steps reaches Mary's exact head_a from head ([0,-1,-1,+1] ->
    //     [0,0,+1,-1]) once the final step is perturbable (spec §4).
    {
        MelodicFigure head;
        head.units.push_back({1.0f, 0});
        head.units.push_back({1.0f, -1});
        head.units.push_back({1.0f, -1});
        head.units.push_back({1.0f, 1});
        bool reached = false;
        bool step0Touched = false;
        for (uint32_t s = 1; s <= 20000 && !reached; ++s) {
            auto f = figure_transforms::apply(head, TransformOp::VarySteps, 3, s);
            if (f.units[0].step != 0) step0Touched = true;
            if (f.units[1].step == 0 && f.units[2].step == 1
                && f.units[3].step == -1) reached = true;
        }
        CHECK(reached);
        CHECK(!step0Touched);
    }

    // --- MelodyProfile: strict parse of the shipped NRS v1 file.
    {
        MelodyProfile p = MelodyProfile::load_by_name("nursery_v1");
        CHECK(p.name == "nursery_v1");
        CHECK(p.tendencies.count("V7:7") == 1);
        CHECK(std::fabs(p.tendencies.at("V7:7").odds_per_100("step_down") - 96.0) < 1e-9);
        CHECK(std::fabs(p.tendencies.at("V7:7").odds_per_100("leap_up") - 4.0) < 1e-9); // "other"
        CHECK(std::fabs(p.nct.appoggiatura - 3.0) < 1e-9);
        CHECK(p.critic.departureBudget == 1);
        CHECK(p.search.phraseCandidates == 10 && p.search.topK == 3
              && p.search.passageCandidates == 10);
        // Missing key -> throws naming it.
        json j = json::parse(R"({"melody": {"tendencies": {}}})");
        bool threw = false;
        try { MelodyProfile::parse_json(j); }
        catch (const std::exception& e) {
            threw = std::string(e.what()).find("nct") != std::string::npos;
        }
        CHECK(threw);
        // StyleTable still parses a file carrying a "melody" block.
        StyleTable st = StyleTable::load_by_name("nursery_v1");
        CHECK(st.transitions.count("I") == 1);
        // Template round trip keeps melodyProfile.
        PassageTemplate pt; pt.melodyProfile = "nursery_v1";
        json pj; to_json(pj, pt);
        PassageTemplate back; from_json(pj, back);
        CHECK(back.melodyProfile == "nursery_v1");
    }

    // --- note_map: classification cases from Matt's annotations.
    {
        Scale c = Scale::get("C", "Major");
        MelodyProfile prof = MelodyProfile::load_by_name("nursery_v1");
        ScaleChord cI;  cI.degree = 0; cI.quality = &ChordDef::get("Major");
        ScaleChord g7;  g7.degree = 4; g7.quality = &ChordDef::get("7");
        auto timeline = [&](std::vector<std::pair<ScaleChord, float>> segs) {
            HarmonyTimeline tl; ChordProgression pr; float tot = 0;
            for (auto& [sc, b] : segs) { pr.add(sc, b); tot += b; }
            tl.set_segment(0.0f, tot, pr, "t");
            return tl;
        };
        auto G = [&](float nn) { return scale_grid_index(nn, c); };
        // Quarter-note track from note numbers, starting at beat 0.
        auto track = [&](std::vector<float> nns) {
            std::vector<TrackNote> t; float b = 0;
            for (float nn : nns) { t.push_back({G(nn), b, 1.0f}); b += 1.0f; }
            return t;
        };
        auto kinds = [&](const NoteMapResult& r) {
            std::vector<std::string> k;
            for (auto& v : r.verdicts) k.push_back(v.kind);
            return k;
        };
        // Mary bar 7 over G7 then C: D D E D | C -> E is an upper NEIGHBOR (licensed).
        {
            HarmonyTimeline tl = timeline({{g7, 4}, {cI, 4}});
            ChordLookup cl(tl, c);
            auto r = evaluate_note_map(track({74, 74, 76, 74, 72}), 0, cl, c, 4.0f, prof, true);
            auto k = kinds(r);
            CHECK(std::find(k.begin(), k.end(), "neighbor") != k.end());
            CHECK(r.departures == 0);
        }
        // D D D E | C -> E leaves by leap: ESCAPE tone (departure).
        {
            HarmonyTimeline tl = timeline({{g7, 4}, {cI, 4}});
            ChordLookup cl(tl, c);
            auto r = evaluate_note_map(track({74, 74, 74, 76, 72}), 0, cl, c, 4.0f, prof, true);
            auto k = kinds(r);
            CHECK(std::find(k.begin(), k.end(), "escape") != k.end());
            CHECK(r.departures == 1);
        }
        // Passing tone: E D C over C -> D passing, odds 100, contributes 0.
        {
            HarmonyTimeline tl = timeline({{cI, 4}});
            ChordLookup cl(tl, c);
            auto r = evaluate_note_map(track({76, 74, 72, 72}), 0, cl, c, 4.0f, prof, true);
            CHECK(kinds(r).size() >= 1 && kinds(r)[0] == "passing");
            CHECK(std::fabs(r.logScore) < 1e-9);
        }
        // Suspension (walk1 s101): F F F F over G7 | F E over C -> F held into C,
        // resolves down by step = suspension; F G instead = unresolved (departure).
        {
            HarmonyTimeline tl = timeline({{g7, 4}, {cI, 4}});
            ChordLookup cl(tl, c);
            auto good = evaluate_note_map(track({77, 77, 77, 77, 77, 76, 76, 76}), 0, cl, c, 4.0f, prof, true);
            auto kg = kinds(good);
            CHECK(std::find(kg.begin(), kg.end(), "suspension") != kg.end());
            CHECK(good.departures == 0);
            auto bad = evaluate_note_map(track({77, 77, 77, 77, 77, 79, 79, 79}), 0, cl, c, 4.0f, prof, true);
            auto kb = kinds(bad);
            CHECK(std::find(kb.begin(), kb.end(), "unresolvedSuspension") != kb.end());
            CHECK(bad.departures == 1);
        }
        // Tendency: 7th of V7 (F) stepping down vs up -> 96:4 = ln ratio ln(24).
        {
            HarmonyTimeline tl = timeline({{g7, 4}});
            ChordLookup cl(tl, c);
            auto down = evaluate_note_map(track({77, 76}), 0, cl, c, 4.0f, prof, true);
            auto up   = evaluate_note_map(track({77, 79}), 0, cl, c, 4.0f, prof, true);
            // F->E: E is an NCT over G7 left with no next -> unscored; only F's
            // tendency differs between the two tracks.
            CHECK(std::fabs((down.logScore - up.logScore) - std::log(24.0)) < 1e-6);
            CHECK(up.departures == 1 && down.departures == 0);
        }
        // Leading tone B over G7 leaping down to F (walk2 s101): tendency
        // "other" (1/100) -> departure.
        {
            HarmonyTimeline tl = timeline({{g7, 4}, {cI, 4}});
            ChordLookup cl(tl, c);
            auto r = evaluate_note_map(track({71, 71, 71, 71, 77, 79}), 0, cl, c, 4.0f, prof, true);
            CHECK(r.departures >= 2);   // B leap-down + F appoggiatura over C
        }
        // Accented modifier: neighbor on the bar downbeat.
        {
            HarmonyTimeline tl = timeline({{cI, 4}, {cI, 4}});
            ChordLookup cl(tl, c);
            // beats 0..: C E G G | A G ... -> A at beat 4 (downbeat), step in, step out opposite
            auto r = evaluate_note_map(track({72, 76, 79, 79, 81, 79}), 0, cl, c, 4.0f, prof, true);
            bool found = false;
            for (auto& v : r.verdicts)
                if (v.kind == "neighbor" && std::fabs(v.odds - 30.0) < 1e-9) found = true;
            CHECK(found);
        }
        // Long NCT: E held 2 beats over G7 -> odds 1.
        {
            HarmonyTimeline tl = timeline({{g7, 4}, {cI, 4}});
            ChordLookup cl(tl, c);
            std::vector<TrackNote> t = {{G(74), 0, 1}, {G(76), 1, 2}, {G(74), 3, 1}, {G(72), 4, 4}};
            auto r = evaluate_note_map(t, 0, cl, c, 4.0f, prof, true);
            bool found = false;
            for (auto& v : r.verdicts) if (std::fabs(v.odds - 1.0) < 1e-9) found = true;
            CHECK(found);
        }
        // Scope: firstScoredNote excludes earlier fully-determined events.
        {
            HarmonyTimeline tl = timeline({{cI, 8}});
            ChordLookup cl(tl, c);
            auto all  = evaluate_note_map(track({76, 74, 72, 74, 76, 74, 72}), 0, cl, c, 4.0f, prof, true);
            auto tail = evaluate_note_map(track({76, 74, 72, 74, 76, 74, 72}), 4, cl, c, 4.0f, prof, true);
            CHECK(all.verdicts.size() > tail.verdicts.size());
        }
    }

    // --- phrase critic + top-k.
    {
        Scale c = Scale::get("C", "Major");
        MelodyProfile prof = MelodyProfile::load_by_name("nursery_v1");
        ScaleChord cI; cI.degree = 0; cI.quality = &ChordDef::get("Major");
        HarmonyTimeline tl; ChordProgression pr; pr.add(cI, 16.0f);
        tl.set_segment(0.0f, 16.0f, pr, "t");
        ChordLookup cl(tl, c);
        auto ph = [&](std::vector<int> grids) {
            std::vector<PhraseNote> v; float b = 0;
            for (int g : grids) { v.push_back({g, b, 1.0f, 0}); b += 1.0f; }
            return v;
        };
        // Range: span 4 costs nothing; span 10 costs 1.5 * 3.
        auto small = score_phrase_critic(ph({35, 36, 37, 38, 39}), {}, {}, false, 0.0f, cl, prof.critic);
        auto wide  = score_phrase_critic(ph({30, 32, 34, 36, 38, 40}), {}, {}, false, 0.0f, cl, prof.critic);
        CHECK(std::fabs(small.range) < 1e-9);
        CHECK(std::fabs(wide.range - (-4.5)) < 1e-9);
        // Motion: 3 repeats of 4 moves (0.75) -> -4 * 0.25 = -1.
        auto rep = score_phrase_critic(ph({35, 35, 35, 35, 36}), {}, {}, false, 0.0f, cl, prof.critic);
        CHECK(std::fabs(rep.motion - (-1.0)) < 1e-9);
        // Gap-fill: leap up 3 then step down -> +1.5.
        auto gap = score_phrase_critic(ph({35, 38, 37}), {}, {}, false, 0.0f, cl, prof.critic);
        CHECK(std::fabs(gap.gap - 1.5) < 1e-9);
        // pick_top_k: in-budget always outranks over-budget; k bounds the pool.
        std::vector<RankItem> items = {{false, 100.0}, {true, 1.0}, {true, 0.5}, {true, -50.0}};
        for (uint32_t s = 1; s <= 200; ++s) {
            Randomizer r(s);
            size_t p = pick_top_k(items, 2, r);
            CHECK(p == 1 || p == 2);
        }
        std::vector<RankItem> none = {{false, 3.0}, {false, 1.0}};
        Randomizer r1(1u);
        CHECK(pick_top_k(none, 1, r1) == 0);
    }

    // --- select_anchors: the 7th of V7 now resolves down ~24:1 (was ~400:1),
    //     measured end to end on a 2-figure phrase over G7 -> C.
    {
        Scale c = Scale::get("C", "Major");
        MelodyProfile prof = MelodyProfile::load_by_name("nursery_v1");
        ScaleChord cI; cI.degree = 0; cI.quality = &ChordDef::get("Major");
        ScaleChord g7; g7.degree = 4; g7.quality = &ChordDef::get("7");
        HarmonyTimeline tl; ChordProgression pr;
        pr.add(g7, 2.0f); pr.add(cI, 2.0f);
        tl.set_segment(0.0f, 4.0f, pr, "t");
        MelodicFigure a; a.units.push_back({2.0f, 0});          // over G7
        MelodicFigure b; b.units.push_back({2.0f, 0});          // over C
        std::vector<const MelodicFigure*> figs = {&a, &b};
        const Pitch reg = Pitch::from_note_number(77.0f);       // F5
        int down = 0, up = 0;
        for (uint32_t s = 1; s <= 400; ++s) {
            Randomizer r(s);
            PhraseTemplate local; local.name = "sev";
            auto res = select_anchors(local, figs, tl, c, 0.0f, 4.0f, 1.0f, false,
                                      Pitch::from_note_number(77.0f), reg, r,
                                      prof, {}, 0.0f, false);
            (void)res;
            const int lead = local.connectors[1]->leadStep;
            if (lead == -1) ++down;
            if (lead > 0) ++up;
        }
        CHECK(down > up);     // the 96-row dominates; exact ratio is mixed with placement
        // Decision log names a departure when the chosen chain takes one:
        // the 7th of V7 stepping UP (4/100, below departureBelow 25).
        bool named = false;
        for (uint32_t s = 1; s <= 400 && !named; ++s) {
            Randomizer r(s);
            PhraseTemplate local; local.name = "sev";
            auto res = select_anchors(local, figs, tl, c, 0.0f, 4.0f, 1.0f, false,
                                      Pitch::from_note_number(77.0f), reg, r,
                                      prof, {}, 0.0f, true);
            if (local.connectors[1]->leadStep == 1) {
                CHECK(res.departures == 1);
                named = res.log.find("[departure] phrase 'sev' bar 1 beat 1 nn 77: "
                                     "tendency V7:7 step_up odds 4.0") != std::string::npos;
            }
        }
        CHECK(named);
    }

    // --- best-of-N end to end on the Mary walk template: deterministic per
    //     seed; legality (R3); variety across seeds.
    {
        std::ifstream f("scores/baselines/template_mary_walk.json");
        json base = json::parse(f);
        auto melody = [&](uint32_t seed) {
            json tj = base; tj["masterSeed"] = seed;
            PieceTemplate tmpl; from_json(tj, tmpl);
            Piece piece; ClassicalComposer composer(tmpl.masterSeed);
            composer.compose(piece, tmpl);
            std::vector<int> nns;
            for (auto& part : piece.parts) {
                if (part.name != "melody") continue;
                for (auto& el : part.elementSequence.elements)
                    nns.push_back(int(el.note().noteNumber));
            }
            return nns;
        };
        auto a = melody(100), b = melody(100);
        CHECK(a == b);                          // deterministic
        CHECK(!a.empty() && (a.back() % 12) == 0);   // R3
        std::set<std::vector<int>> distinct;
        for (uint32_t s = 100; s < 110; ++s) distinct.insert(melody(s));
        CHECK(distinct.size() >= 3);            // search didn't collapse to one tune
    }
}

#include "mforce/core/component.h"
#include <string>

// Checkpoint 2.1: a pin is a self-registering member. Declaration order is
// registry order; the base serves descriptors and by-name access from it.
namespace {
struct PinProbe : mforce::Component {
  mforce::Param freq{this, "frequency", 440.0f, 0.01f, 20000.0f, "hz"};
  mforce::Param amp{this, "amplitude", 1.0f, 0.0f, 10.0f};
  mforce::Input in{this, "source", mforce::Input::Optional};
  mforce::Setting<int> mode{this, "mode", 2, 0, 3};
  mforce::Array table{this, "weights", nullptr, 0.5f, 0.0f, 1.0f};
  const char* type_name() const override { return "PinProbe"; }
};
}

static void run_pin_tests() {
    PinProbe p;
    auto pd = p.param_descriptors();
    CHECK(pd.size() == 2);
    CHECK(std::string(pd[0].name) == "frequency");
    CHECK_NEAR(pd[0].default_value, 440.0f, 1e-6f);
    CHECK(std::string(pd[0].hint) == "hz");
    CHECK(p.input_descriptors().size() == 1);
    CHECK(p.setting_descriptors().size() == 1);
    CHECK(p.array_descriptors().size() == 1);
    CHECK(p.pins().size() == 5);
    // an unwired param reads its constant; a wired one reads the source
    CHECK_NEAR(p.freq.current(), 440.0f, 1e-6f);
    CHECK(p.get_param("frequency") == nullptr);
    p.set_param("frequency", std::make_shared<mforce::ConstantSource>(220.0f));
    p.freq.source->next();
    CHECK_NEAR(p.freq.current(), 220.0f, 1e-6f);
    CHECK(p.get_param("frequency") != nullptr);
    CHECK(p.get_param("nosuch") == nullptr);
    p.clear_param("frequency");
    CHECK_NEAR(p.freq.current(), 440.0f, 1e-6f);
    // settings and arrays by name; the descriptor keeps the declared default
    p.set_setting("mode", 3.0f);
    CHECK(p.mode.value == 3);
    CHECK_NEAR(p.get_setting("mode"), 3.0f, 1e-6f);
    CHECK_NEAR(p.setting_descriptors()[0].default_value, 2.0f, 1e-6f);
    CHECK(p.setting_descriptors()[0].type == mforce::SettingType::Int);
    CHECK_NEAR(p.get_setting("nosuch"), 0.0f, 1e-6f);
    p.set_array("weights", {0.1f, 0.2f});
    CHECK(p.get_array("weights").size() == 2);
    CHECK(p.table.values.size() == 2);
    // an optional input unwired reads 0; an input is reachable by name too
    CHECK_NEAR(p.in.current(), 0.0f, 1e-6f);
    p.set_param("source", std::make_shared<mforce::ConstantSource>(3.0f));
    CHECK(p.get_param("source") != nullptr);
    CHECK(p.in.source != nullptr);
}

// Checkpoint 2.1: Generator and Processor under ValueSource. next() advances
// every pin in declaration order, then compute() does the node's work.
namespace {
struct Doubler final : mforce::Processor {
  float compute() override { return 2.0f * input.current(); }
  const char* type_name() const override { return "Doubler"; }
};
struct UnitRamp final : mforce::Generator {
  float v_{0.0f};
  float compute() override { return v_ += 1.0f; }
  Range range() const override { return Range::Unipolar; }
  const char* type_name() const override { return "UnitRamp"; }
};
}

static void run_base_tests() {
    auto ramp = std::make_shared<UnitRamp>();
    Doubler d;
    d.set_param("source", ramp);              // Processor's input is the pin named "source"
    CHECK(d.input_descriptors().size() == 1);
    mforce::RenderContext ctx{48000};
    d.prepare(ctx); d.start(100);
    CHECK_NEAR(d.next(), 2.0f, 1e-6f);        // the input advanced (ramp -> 1), then 2 * 1
    CHECK_NEAR(d.next(), 4.0f, 1e-6f);
    CHECK_NEAR(d.current(), 4.0f, 1e-6f);
    CHECK(ramp->range() == mforce::Generator::Range::Unipolar);
    // a constant is a Generator that computes its value
    mforce::ConstantSource c(0.25f);
    CHECK_NEAR(c.current(), 0.25f, 1e-6f);
    c.set(0.5f);
    CHECK_NEAR(c.next(), 0.5f, 1e-6f);
}

#include "mforce/source/oscillator/sine_source.h"
#include "mforce/source/oscillator/fm_source.h"

// A Manual pin is driven by its node: next() pulls the source and returns
// its value, or the pin's fallback (the constant; 0 for an input) unwired.
static void run_manual_pin_next_tests() {
    PinProbe p;
    CHECK_NEAR(p.freq.next(), 440.0f, 1e-6f);        // unwired: the constant
    CHECK_NEAR(p.in.next(), 0.0f, 1e-6f);            // unwired input: 0
    auto ramp = std::make_shared<UnitRamp>();
    p.set_param("frequency", ramp);
    p.set_param("source", ramp);
    CHECK_NEAR(p.freq.next(), 1.0f, 1e-6f);          // pulls the source once
    CHECK_NEAR(p.in.next(), 2.0f, 1e-6f);            // the same source, pulled again
    CHECK_NEAR(p.freq.current(), 2.0f, 1e-6f);          // current() does not pull
}

// The base does not clamp a setting (min/max are advisory); a node that
// enforces its declared range calls clamp() in on_setting_changed.
static void run_setting_clamp_tests() {
    PinProbe p;
    p.set_setting("mode", 7.0f);
    CHECK(p.mode.value == 7);
    p.mode.clamp();
    CHECK(p.mode.value == 3);
    p.set_setting("mode", -5.0f);
    p.mode.clamp();
    CHECK(p.mode.value == 0);
    p.set_setting("mode", 1.0f);
    p.mode.clamp();
    CHECK(p.mode.value == 1);
    p.set_setting("mode", 2.7f);                  // an int setting truncates
    CHECK(p.mode.value == 2);
}

// A Manual pin is prepared by its node, not by the base: the base's
// prepare skips it, and the node calls the pin's own prepare at the point
// it chooses.
namespace {
struct PrepCounter final : mforce::Generator {
  int starts{0};
  void on_start(int) override { ++starts; }
  float compute() override { return 0.0f; }
  const char* type_name() const override { return "PrepCounter"; }
};
struct ManualHolder final : mforce::Generator {
  mforce::Param gate{this, "gate", 0.0f, 0.0f, 1.0f, nullptr, mforce::Pull::Manual};
  mforce::Param gain{this, "gain", 1.0f, 0.0f, 1.0f};
  float compute() override { return gain.current(); }
  const char* type_name() const override { return "ManualHolder"; }
};
}

static void run_manual_pin_prepare_tests() {
    auto a = std::make_shared<PrepCounter>();
    auto b = std::make_shared<PrepCounter>();
    ManualHolder h;
    h.set_param("gate", a);
    h.set_param("gain", b);
    mforce::RenderContext ctx{48000};
    h.prepare(ctx); h.start(8);
    CHECK(a->starts == 0);                      // Manual: the base leaves it alone
    CHECK(b->starts == 1);                      // Auto: the base prepared it
    h.gate.prepare(ctx); h.gate.start(8);                       // the node prepares it where it chooses
    CHECK(a->starts == 1);
}

// A Processor may own its primary input: Processor(Pull::Manual) keeps
// `source` out of the sweep so the node pulls it where the old code did.
namespace {
struct ManualDoubler final : mforce::Processor {
  ManualDoubler() : Processor(mforce::Pull::Manual) {}
  float compute() override { return 2.0f * input.next(); }
  const char* type_name() const override { return "ManualDoubler"; }
};
}

namespace {
struct ManualIdle final : mforce::Processor {  // owns its input and never pulls it
  ManualIdle() : Processor(mforce::Pull::Manual) {}
  float compute() override { return 0.0f; }
  const char* type_name() const override { return "ManualIdle"; }
};
}

static void run_processor_manual_input_tests() {
    auto ramp = std::make_shared<UnitRamp>();
    ManualDoubler d;
    d.set_param("source", ramp);
    CHECK(d.input_descriptors().size() == 1);  // the pin is still declared
    mforce::RenderContext ctx{48000};
    d.prepare(ctx); d.start(100);
    CHECK_NEAR(d.next(), 2.0f, 1e-6f);         // one pull per sample, by the node
    CHECK_NEAR(d.next(), 4.0f, 1e-6f);
    ManualIdle idle;
    idle.set_param("source", ramp);
    idle.next();
    CHECK_NEAR(ramp->current(), 2.0f, 1e-6f);  // the base advanced nothing
    Doubler a;
    a.set_param("source", ramp);
    a.next();
    CHECK_NEAR(ramp->current(), 3.0f, 1e-6f);  // the default stays Auto
}

// Checkpoint 2.1: WaveSource under Generator with its three pins declared
// once, in the order the old next() advanced them (amplitude, frequency,
// phase); an unwired pin reads its constant and has no source.
static void run_wave_source_tests() {
    SineSource s;
    auto pd = s.param_descriptors();
    CHECK(pd.size() == 3);
    CHECK(std::string(pd[0].name) == "amplitude");
    CHECK(std::string(pd[1].name) == "frequency");
    CHECK(std::string(pd[2].name) == "phase");
    CHECK(s.get_param("phase") == nullptr);
    CHECK(s.range() == mforce::Generator::Range::Bipolar);
    s.set_param("frequency", std::make_shared<ConstantSource>(12000.0f));
    s.prepare(RenderContext{48000}); s.start(8);
    CHECK_NEAR(s.next(), 0.0f, 1e-6f);           // pos 0 -> sin(0)
    CHECK_NEAR(s.next(), 1.0f, 1e-6f);           // a quarter cycle later
    CHECK(s.get_param("frequency") != nullptr);
}

// The per-tick memo: one compute per tick however many pulls, in whatever
// order; step() advances now; no clock means every pull computes; a cycle
// with no tap is finite.
namespace {
struct TestHost { SampleClock clock; RenderContext ctx{48000, &clock}; };
struct Counter final : Generator {
    int computes{0};
    float compute() override { return float(++computes); }
    const char* type_name() const override { return "Counter"; }
};
}

// A cycle with no tap, wired after the host calls (which walk the pins):
// illegal, and what the re-entry guards must keep finite.
static std::shared_ptr<CombinedSource> tapless_cycle(TestHost& h) {
    auto loop = std::make_shared<CombinedSource>(std::make_shared<ConstantSource>(1.0f),
                                                 std::make_shared<ConstantSource>(0.0f),
                                                 CombineOp::Sum, 0.0f);
    loop->prepare(h.ctx); loop->start(16);
    loop->set_param("source2", loop);
    return loop;
}

static void run_memo_tests() {
    TestHost h;
    auto c = std::make_shared<Counter>();
    auto sum = std::make_shared<CombinedSource>(c, c, CombineOp::Sum, 0.0f);   // one node on both pins, no wrapper
    sum->prepare(h.ctx); sum->start(16);
    h.clock.tick();
    CHECK_NEAR(sum->next(), 2.0f, 1e-6f);     // computed once, served once
    CHECK(c->computes == 1);
    CHECK_NEAR(c->next(), 1.0f, 1e-6f);       // a third pull this tick: the memo
    CHECK(c->computes == 1);
    h.clock.tick();
    CHECK_NEAR(sum->next(), 4.0f, 1e-6f);
    CHECK(c->computes == 2);
    h.clock.tick();                           // order independence: the leaf first
    CHECK_NEAR(c->next(), 3.0f, 1e-6f);
    CHECK_NEAR(sum->next(), 6.0f, 1e-6f);
    CHECK(c->computes == 3);
    h.clock.tick();                           // step(): one sample now, whatever the tick
    CHECK_NEAR(c->step(), 4.0f, 1e-6f);
    CHECK_NEAR(c->step(), 5.0f, 1e-6f);
    CHECK_NEAR(c->next(), 5.0f, 1e-6f);       // the memo holds the last step
    CHECK(c->computes == 5);
    h.clock.tick();                           // step() is recursive through Auto pins
    auto chain = std::make_shared<CombinedSource>(c, std::make_shared<ConstantSource>(0.0f),
                                                  CombineOp::Sum, 0.0f);
    chain->prepare(h.ctx); chain->start(16);
    CHECK_NEAR(chain->step(), 6.0f, 1e-6f);
    CHECK_NEAR(chain->step(), 7.0f, 1e-6f);
    CHECK(c->computes == 7);
    CHECK_NEAR(sum->step(), 18.0f, 1e-6f);    // a node on two pins of a stepped parent steps twice, and
                                              // both pins read it after the sweep: 9 + 9
    CHECK(c->computes == 9);
    auto bare = std::make_shared<Counter>();  // no clock: every pull computes
    bare->prepare(RenderContext{48000}); bare->start(16);
    bare->next(); bare->next(); bare->next();
    CHECK(bare->computes == 3);
    auto loop = tapless_cycle(h);             // illegal, must be finite
    h.clock.tick();
    CHECK_NEAR(loop->next(), 1.0f, 1e-6f);    // 1 + the previous value (0)
    h.clock.tick();
    CHECK_NEAR(loop->next(), 2.0f, 1e-6f);    // 1 + 1
    loop->clear_param("source2");             // break the ownership cycle
}

static void run_previous_tests() {
    TestHost h;
    auto c = std::make_shared<Counter>();
    c->prepare(h.ctx); c->start(16);
    CHECK_NEAR(c->previous(), 0.0f, 1e-6f);   // nothing computed yet: cur_
    h.clock.tick();
    CHECK_NEAR(c->previous(), 0.0f, 1e-6f);   // this tick not yet claimed: cur_, still last tick's
    CHECK_NEAR(c->next(), 1.0f, 1e-6f);
    CHECK_NEAR(c->previous(), 0.0f, 1e-6f);   // claimed and computed: what cur_ replaced
    CHECK_NEAR(c->current(), 1.0f, 1e-6f);
    h.clock.tick();
    CHECK_NEAR(c->previous(), 1.0f, 1e-6f);   // a new tick, not yet claimed: cur_
    CHECK_NEAR(c->next(), 2.0f, 1e-6f);
    CHECK_NEAR(c->previous(), 1.0f, 1e-6f);
    h.clock.tick();                           // step() moves the value the same way
    CHECK_NEAR(c->step(), 3.0f, 1e-6f);
    CHECK_NEAR(c->previous(), 2.0f, 1e-6f);
    CHECK_NEAR(c->step(), 4.0f, 1e-6f);
    CHECK_NEAR(c->previous(), 3.0f, 1e-6f);   // before the latest step
    h.clock.tick(); h.clock.tick();           // not pulled for two ticks
    CHECK_NEAR(c->previous(), 4.0f, 1e-6f);   // its last value
    auto mid = std::make_shared<CombinedSource>(std::make_shared<ConstantSource>(1.0f),
                                                std::make_shared<ConstantSource>(0.0f),
                                                CombineOp::Sum, 0.0f);
    struct Probe final : Generator {            // reads its target's previous() from inside the target's compute
        ValueSource* target{nullptr};
        float seen{-1.0f};
        float compute() override { seen = target->previous(); return 0.0f; }
        const char* type_name() const override { return "Probe"; }
    };
    auto probe = std::make_shared<Probe>(); probe->target = mid.get();
    mid->prepare(h.ctx); mid->start(16); probe->prepare(h.ctx); probe->start(16);
    mid->set_param("source2", probe);         // mid's sweep pulls the probe: a read mid-compute
    h.clock.tick();
    CHECK_NEAR(mid->next(), 1.0f, 1e-6f);
    CHECK_NEAR(probe->seen, 0.0f, 1e-6f);     // mid-compute: last tick's value (0), not two ticks old
    h.clock.tick();
    CHECK_NEAR(mid->next(), 1.0f, 1e-6f);
    CHECK_NEAR(probe->seen, 1.0f, 1e-6f);
    auto bare = std::make_shared<Counter>();  // no clock: every pull computes, previous() is the present value
    bare->prepare(RenderContext{48000}); bare->start(16);
    bare->next(); bare->next();
    CHECK_NEAR(bare->previous(), 2.0f, 1e-6f);
}

static void run_tap_reading_tests() {
    TestHost h;
    // What a consumer gets. A node reads its pins after its sweep through
    // current(), so the tap's current() is the delivery: sum = c + tap(c),
    // with c computed first in the tick, reads c's previous value through the
    // tap — 1 + 0, 2 + 1, 3 + 2 (reading A gave 2, 4, 6).
    auto c = std::make_shared<Counter>();
    auto tap = std::make_shared<TapSource>(c);
    auto sum = std::make_shared<CombinedSource>(c, tap, CombineOp::Sum, 0.0f);
    sum->prepare(h.ctx); sum->start(16);
    h.clock.tick(); CHECK_NEAR(sum->next(), 1.0f, 1e-6f);
    h.clock.tick(); CHECK_NEAR(sum->next(), 3.0f, 1e-6f);
    h.clock.tick(); CHECK_NEAR(sum->next(), 5.0f, 1e-6f);
    // The tap alone, read position by read position.
    c = std::make_shared<Counter>();
    tap = std::make_shared<TapSource>(c);
    c->prepare(h.ctx); c->start(16); tap->prepare(h.ctx); tap->start(16);
    h.clock.tick();
    CHECK_NEAR(c->next(), 1.0f, 1e-6f);       // the source computes first this tick
    CHECK_NEAR(tap->next(), 0.0f, 1e-6f);     // the tap is one sample late anyway (reading A gave 1)
    h.clock.tick();
    CHECK_NEAR(tap->next(), 1.0f, 1e-6f);     // the tap first: the source has not claimed this tick
    CHECK_NEAR(c->next(), 2.0f, 1e-6f);
    CHECK_NEAR(tap->next(), 1.0f, 1e-6f);     // the tap's own memo: pulled again this tick, the same value
    CHECK_NEAR(tap->current(), 1.0f, 1e-6f);  // pulled this tick: current() is the memo, what a consumer reads after its sweep
    h.clock.tick();
    CHECK_NEAR(c->next(), 3.0f, 1e-6f);
    CHECK_NEAR(tap->current(), 3.0f, 1e-6f);  // not pulled this tick: live — between ticks, the value the next tick's tap delivers
    CHECK_NEAR(tap->next(), 2.0f, 1e-6f);
    CHECK_NEAR(tap->current(), 2.0f, 1e-6f);  // and the memo again once pulled
    h.clock.tick();                           // inside a fill: one step late
    CHECK_NEAR(c->step(), 4.0f, 1e-6f);
    CHECK_NEAR(c->step(), 5.0f, 1e-6f);
    CHECK_NEAR(tap->step(), 4.0f, 1e-6f);
    h.clock.tick(); h.clock.tick();           // a source not pulled this tick: its last value
    CHECK_NEAR(tap->next(), 5.0f, 1e-6f);
    // Between ticks (a Setup-time read) the clock holds at the last rendered
    // tick: a tap pulled in that tick still answers with its memo — one sample
    // older than what the coming tick delivers — and a Setup-time step()
    // through it reads the same memo. A stated limit, not a goal.
    h.clock.tick();
    CHECK_NEAR(c->next(), 6.0f, 1e-6f);
    CHECK_NEAR(tap->next(), 5.0f, 1e-6f);     // pulled this tick
    CHECK_NEAR(tap->current(), 5.0f, 1e-6f);  // "between ticks" now: the memo, though the coming tick delivers 6
    CHECK_NEAR(tap->step(), 5.0f, 1e-6f);     // a Setup-time step() through the pulled tap: the same memo
    // Inside a fill, a tap stepped before its source reads two steps late: the
    // source re-claims the same tick, so previous() cannot tell a fresh step
    // from an earlier one. A stated limit of step().
    h.clock.tick();
    CHECK_NEAR(tap->step(), 6.0f, 1e-6f);     // draw 1: the source has not claimed this tick — its last value
    CHECK_NEAR(c->step(), 7.0f, 1e-6f);
    CHECK_NEAR(tap->step(), 6.0f, 1e-6f);     // draw 2: the value before the source's latest step, not the latest
    CHECK_NEAR(c->step(), 8.0f, 1e-6f);
    CHECK_NEAR(tap->step(), 7.0f, 1e-6f);     // draw 3: 7, not 8
    // The cycle skeleton under a clock: out = 1 + tap(out) counts 1, 2, 3 — the
    // tap reads its source mid-compute, and readings A and B agree there.
    auto counter = std::make_shared<CombinedSource>(std::make_shared<ConstantSource>(1.0f),
                                                    std::make_shared<ConstantSource>(0.0f),
                                                    CombineOp::Sum, 0.0f);
    auto self = std::make_shared<TapSource>(counter);
    counter->prepare(h.ctx); counter->start(16);
    counter->set_param("source2", self);      // after the host calls, which walk the pins
    self->prepare(h.ctx); self->start(16);
    h.clock.tick(); CHECK_NEAR(counter->next(), 1.0f, 1e-6f);
    h.clock.tick(); CHECK_NEAR(counter->next(), 2.0f, 1e-6f);
    h.clock.tick(); CHECK_NEAR(counter->next(), 3.0f, 1e-6f);
    counter->clear_param("source2");          // break the ownership cycle
    auto bare = std::make_shared<Counter>();  // no clock: positional, as every pull computes
    auto bareTap = std::make_shared<TapSource>(bare);
    bare->prepare(RenderContext{48000}); bare->start(16);
    bareTap->prepare(RenderContext{48000}); bareTap->start(16);
    bare->next();
    CHECK_NEAR(bareTap->next(), 1.0f, 1e-6f);
}

#include "mforce/source/envelope/envelope.h"

// The sweep follows every door that changes a pin's source: a source wired
// after prepare and start is pulled on the next tick, and one cleared is
// not.
static void run_sweep_follows_wiring_tests() {
    TestHost h;
    auto c = std::make_shared<Counter>();
    auto p = std::make_shared<CombinedSource>(std::make_shared<ConstantSource>(1.0f),
                                              std::make_shared<ConstantSource>(0.0f),
                                              CombineOp::Sum, 0.0f);
    p->prepare(h.ctx); p->start(16);
    p->set_param("source2", c);               // wired after the host calls
    h.clock.tick();
    CHECK_NEAR(p->next(), 2.0f, 1e-6f);       // 1 + the counter's first value
    CHECK(c->computes == 1);
    p->set_param("source2", nullptr);         // cleared: back to the pin's fallback (0)
    h.clock.tick();
    CHECK_NEAR(p->next(), 1.0f, 1e-6f);
    CHECK(c->computes == 1);                  // not pulled any more
    auto d = std::make_shared<Counter>();
    CHECK(p->param("source1") == nullptr);    // no Param of that name: source1 is an Input
    p->set_param("source1", d);
    h.clock.tick();
    CHECK_NEAR(p->next(), 1.0f, 1e-6f);       // d's first value + 0
    CHECK(d->computes == 1);

    // A node assigning itself (an envelope taking a new layout through
    // replace_stages) keeps its wires in the sweep: the range source is still
    // pulled afterwards.
    auto r = std::make_shared<Counter>();
    auto env = Envelope::make_ar(0.2f);
    env->set_param("maxValue", r);
    env->prepare(h.ctx); env->start(4800);
    for (int i = 0; i < 3; ++i) { h.clock.tick(); env->next(); }
    CHECK(r->computes == 3);
    env->replace_stages(std::move(*Envelope::make_ar(0.3f)));   // the stages are replaced, the wire must stay swept
    for (int i = 0; i < 3; ++i) { h.clock.tick(); env->next(); }
    CHECK(r->computes == 6);
    CHECK(env->get_param("maxValue").get() == r.get());
}

// The tap edge reads its source's memo live: a Setup-time read between
// ticks sees the source's present value without the tap being pulled.
static void run_tap_reads_live_tests() {
    TestHost h;
    auto c = std::make_shared<Counter>();
    auto tap = std::make_shared<TapSource>(c);
    c->prepare(h.ctx); c->start(16);
    tap->prepare(h.ctx); tap->start(16);
    h.clock.tick(); c->next();
    CHECK_NEAR(tap->current(), 1.0f, 1e-6f);  // never pulled, reads live
    h.clock.tick(); c->next();
    CHECK_NEAR(tap->current(), 2.0f, 1e-6f);
    CHECK_NEAR(tap->next(), 1.0f, 1e-6f);     // pulled: the previous tick's value (the source computed this tick already)
    h.clock.tick();
    CHECK_NEAR(tap->next(), 2.0f, 1e-6f);     // the source not yet computed this tick: its value is still last tick's
    c->next();
    CHECK_NEAR(tap->current(), 2.0f, 1e-6f);  // pulled this tick: the memo, not live — what a consumer reads after its sweep
}

// A source wired to two pins is the same object on both pins: no wrapper, no
// advancer, no wiring order. A consumer the file lists before the advancer
// used to read a wrapper a sample late; it reads this sample's value now, so
// the shared patch renders exactly as the same patch with the envelope
// duplicated, however the file orders the consumers.
static std::string shared_env_patch_json(bool duplicated) {
    using nlohmann::json;
    const json table = {
        {"startVal", {0.0, 1.0, 0.7, 0.7}}, {"endVal", {1.0, 0.7, 0.7, 0.0}},
        {"power", {0.0, 0.0, 0.0, 0.0}}, {"type", {"Linear", "Linear", "Linear", "Linear"}},
        {"percent", {0.05, 0.1, 0.0, 0.25}}, {"minSec", {0.0, 0.0, 0.0, 0.0}},
        {"maxSec", {0.0, 0.0, 0.0, 0.0}}, {"nominal", {0.0, 0.0, 0.0, 0.0}}};
    auto env = [&](const char* id) {
        return json{{"id", id}, {"type", "Envelope"}, {"params", table}};
    };
    auto sine = [](const char* id, const char* envId) {
        return json{{"id", id}, {"type", "SineSource"},
                    {"params", {{"frequency", 220.0}, {"amplitude", {{"ref", envId}}}, {"phase", 0.0}}}};
    };
    json nodes = json::array();
    nodes.push_back(env("env"));
    if (duplicated) nodes.push_back(env("env2"));
    nodes.push_back(sine("sine2", "env"));                            // listed first: the file's advancer
    nodes.push_back(sine("sine1", duplicated ? "env2" : "env"));      // pulled first by the mix
    nodes.push_back(json{{"id", "mix"}, {"type", "CombinedSource"},
                         {"params", {{"operation", 3}, {"gainAdj", 0.0},
                                     {"source1", {{"ref", "sine1"}}}, {"source2", {{"ref", "sine2"}}}}}});
    const json root = {{"sampleRate", 48000}, {"instrument", {{"polyphony", 1}}},
                       {"graph", {{"output", "mix"}, {"nodes", nodes}}}};
    return root.dump();
}

static void run_shared_source_tests() {
    auto render_all = [](LoadedInstrument& p, int n) {
        std::vector<float> b(size_t(n), 0.0f);
        p.instrument->render(b.data(), n);
        return b;
    };
    auto rms_of = [](const std::vector<float>& b, float fromSec, float toSec) {
        int a = int(fromSec * 48000), z = int(toSec * 48000);
        a = std::max(0, a); z = std::min(z, int(b.size()));
        double s = 0.0;
        for (int i = a; i < z; ++i) s += double(b[size_t(i)]) * b[size_t(i)];
        return float(std::sqrt(s / double(std::max(1, z - a))));
    };
    auto ip = load_inst(shared_env_patch_json(false));
    auto* pi = dynamic_cast<PitchedInstrument*>(ip.instrument.get());
    CHECK(pi != nullptr && !pi->voicePool.empty());
    auto& vg = pi->voicePool[0];
    auto env = vg.nodesById.at("env");
    CHECK(vg.nodesById.at("sine2")->get_param("amplitude").get() == env.get());
    CHECK(vg.nodesById.at("sine1")->get_param("amplitude").get() == env.get());   // the second consumer holds the node itself
    const int N = 24000;
    pi->play_note({.start = 0.0f, .duration = 0.25f, .noteNumber = 60.0f, .velocity = 0.8f, .onset = 0, .hold = false});
    auto shared = render_all(ip, N);
    auto dupIp = load_inst(shared_env_patch_json(true));
    auto* dp = dynamic_cast<PitchedInstrument*>(dupIp.instrument.get());
    dp->play_note({.start = 0.0f, .duration = 0.25f, .noteNumber = 60.0f, .velocity = 0.8f, .onset = 0, .hold = false});
    auto dup = render_all(dupIp, N);
    CHECK(shared.size() == dup.size() &&
          std::memcmp(shared.data(), dup.data(), shared.size() * sizeof(float)) == 0);
    CHECK(rms_of(shared, 0.05f, 0.20f) > 0.01f);                                   // and it sounds
}

// Checkpoint 2.1: the core family (envelopes, range/var, multi, name gate)
// under Generator/Processor with its pins declared once.
#include "mforce/source/envelope/envelope.h"
#include "mforce/source/combiner/multi_source.h"
#include "mforce/source/modulator/range_source.h"
#include "mforce/source/modulator/var_source.h"

namespace {
// Counts how often a pin's owner prepared and advanced it.
struct PinCounter final : mforce::Generator {
  int starts{0}, nexts{0}, rate{0}, ctxPrepares{0};
  float level{1.0f};
  void on_prepare(const mforce::RenderContext& c) override { rate = c.sampleRate; ++ctxPrepares; }
  void on_start(int) override { ++starts; }
  float compute() override { ++nexts; return level; }
  const char* type_name() const override { return "PinCounter"; }
};

bool has_name(std::span<const SettingDescriptor> d, const char* n) {
  for (const auto& x : d) if (std::string(x.name) == n) return true;
  return false;
}
}

static void run_range_var_tests() {
    RenderContext ctx{48000};

    // RangeSource: the declared default of `normalized` is false; the
    // setting turns it on.
    {
        auto zero = std::make_shared<ConstantSource>(0.0f);
        RangeSource a;
        a.set_param("min", std::make_shared<ConstantSource>(10.0f));
        a.set_param("max", std::make_shared<ConstantSource>(20.0f));
        a.set_param("var", zero);
        CHECK(a.normalized.value == false);
        CHECK(!a.setting_descriptors().empty());
        CHECK_NEAR(a.setting_descriptors()[0].default_value, 0.0f, 1e-6f);
        a.prepare(ctx); a.start(16);
        CHECK_NEAR(a.next(), 15.0f, 1e-5f);        // var 0 remapped (0+1)/2
        RangeSource b;
        b.set_param("min", std::make_shared<ConstantSource>(10.0f));
        b.set_param("max", std::make_shared<ConstantSource>(20.0f));
        b.set_param("var", zero);
        b.set_setting("normalized", 1.0f);
        b.prepare(ctx); b.start(16);
        CHECK_NEAR(b.next(), 10.0f, 1e-5f);        // already normalized
        CHECK(a.param_descriptors().size() == 3);
        CHECK(std::string(a.param_descriptors()[2].name) == "var");
        CHECK(a.get_param("max") != nullptr);
        for (int i = 0; i < 10; ++i) CHECK(std::isfinite(a.next()));
    }

    // VarSource: absolute and relative modes; wired by name, default
    // absolute.
    {
        VarSource v;
        v.set_param("val", std::make_shared<ConstantSource>(2.0f));
        v.set_param("var", std::make_shared<ConstantSource>(0.5f));
        v.set_param("varPct", std::make_shared<ConstantSource>(0.5f));
        CHECK(v.absolute.value == true);
        v.prepare(ctx); v.start(16);
        CHECK_NEAR(v.next(), 2.5f, 1e-5f);         // 2 * (1 + 0.5 * 0.5)
        v.set_setting("absolute", 0.0f);
        CHECK_NEAR(v.next(), 2.5f + (2.0f - 2.5f) * 1.25f, 1e-5f);
        for (int i = 0; i < 10; ++i) CHECK(std::isfinite(v.next()));
        CHECK(v.param_descriptors().size() == 3);
    }
}

static void run_multi_name_gate_tests() {
    RenderContext ctx{48000};

    // MultiSource: `source` is a declared list with its weights and delays
    // beside it, by position; clear_param empties it.
    {
        MultiSource m;
        m.set_param("source", std::make_shared<ConstantSource>(1.0f));
        m.add_param("source", std::make_shared<ConstantSource>(2.0f));
        CHECK(m.source.sources.size() == 2);
        CHECK(m.input_descriptors().size() == 1);
        CHECK(m.input_descriptors()[0].multi);
        CHECK(m.pins().size() == 3);               // the list and the two arrays
        CHECK(m.get_param("source") == nullptr);
        m.set_array("weights", {1.0f, 0.5f});
        m.set_array("delays", {0.0f, 1.0f});
        m.prepare(ctx); m.start(16);
        CHECK_NEAR(m.next(), 1.0f, 1e-6f);         // the delayed feed is not yet in
        CHECK_NEAR(m.next(), 2.0f, 1e-6f);         // 1 + 2 * 0.5
        m.clear_param("source");
        CHECK(m.source.sources.empty());
    }

    // NameGate: `in` is an optional signal input, advanced once per sample.
    {
        NameGate ng;
        CHECK(ng.param_descriptors().empty());
        CHECK(ng.input_descriptors().size() == 1);
        CHECK(std::string(ng.input_descriptors()[0].name) == "in");
        ng.targetId = 0.0f;
        CHECK(ng.next() == 0.0f);                  // unwired never matches, even id 0
        auto c = std::make_shared<PinCounter>();
        c->level = 3.0f;
        ng.set_param("in", c);
        ng.targetId = 3.0f;
        ng.prepare(ctx); ng.start(16);
        CHECK(c->starts == 1);
        CHECK(ng.next() == 1.0f);
        CHECK(ng.next() == 1.0f);
        CHECK(c->nexts == 2);
    }
}

static void run_envelope_pin_tests() {
    RenderContext ctx{48000};

    // Envelope: minValue/maxValue are pulled and prepared by the base;
    // `trigger` is Manual, so it is never prepared or advanced.
    {
        auto env = std::make_shared<Envelope>();
        env->add_stage({{0.0f, 1.0f, RampType::Linear, 0.0f}, 0.5f, 0.0f, 0.0f});
        env->add_stage({{1.0f, 1.0f, RampType::Linear, 0.0f}, 0.5f, 0.0f, 0.0f});
        auto lo = std::make_shared<PinCounter>();
        auto hi = std::make_shared<PinCounter>();
        auto trig = std::make_shared<PinCounter>();
        env->set_param("minValue", lo);
        env->set_param("maxValue", hi);
        env->set_param("trigger", trig);
        env->prepare(ctx); env->start(4800);
        for (int i = 0; i < 5; ++i) env->next();
        CHECK(lo->starts == 1 && hi->starts == 1 && trig->starts == 0);
        CHECK(lo->nexts == 5 && hi->nexts == 5 && trig->nexts == 0);
        CHECK(env->trigger.source == trig);
        CHECK(env->range() == Generator::Range::Unipolar);
        CHECK(env->param_descriptors().size() == 3);
        CHECK(std::string(env->param_descriptors()[2].name) == "trigger");
    }

    // Envelope settings: the knobs clamp; sustainLevel is a live view of the
    // four-slot ADSR shape and reads 0 on any other layout.
    {
        auto adsr = Envelope::make_adsr(0.1f, 0.1f, 0.4f, 0.2f);
        CHECK_NEAR(adsr->get_setting("sustainLevel"), 0.4f, 1e-6f);
        adsr->set_setting("sustainLevel", 0.2f);
        CHECK_NEAR(adsr->get_setting("sustainLevel"), 0.2f, 1e-6f);
        CHECK_NEAR(adsr->stage(3).ramp.startVal, 0.2f, 1e-6f);
        adsr->set_setting("stage_accuracy", 7.0f);
        CHECK_NEAR(adsr->get_setting("stage_accuracy"), 1.0f, 1e-6f);
        adsr->set_setting("timeScale", 0.0f);
        CHECK_NEAR(adsr->get_setting("timeScale"), 0.01f, 1e-6f);
        auto ar = Envelope::make_ar(0.2f);
        ar->set_setting("sustainLevel", 0.9f);
        CHECK_NEAR(ar->get_setting("sustainLevel"), 0.0f, 1e-6f);
        CHECK_NEAR(ar->stage(1).ramp.startVal, 1.0f, 1e-6f);   // not rewritten
    }
}

// replace_stages assigns a freshly laid-out envelope over this one (the
// editor's preset combo). The pin registry belongs to the object, not to the
// assignment, so the pins stay the same and a wired one is still prepared
// and advanced afterwards.
static void run_envelope_rebuild_pin_tests() {
    RenderContext ctx{48000};
    auto env = Envelope::make_adsr(0.2f, 0.1f, 0.7f, 0.1f);
    const std::vector<PinBase*> pinsBefore = env->pins();
    auto lo = std::make_shared<PinCounter>();
    auto hi = std::make_shared<PinCounter>();
    env->set_param("minValue", lo);
    env->set_param("maxValue", hi);
    env->set_setting("stage_accuracy", 0.5f);
    env->replace_stages(std::move(*Envelope::make_adsr(0.2f, 0.1f, 0.3f, 0.25f)));   // each of these replaces the layout
    env->replace_stages(std::move(*Envelope::make_as(0.05f)));
    CHECK_NEAR(env->get_setting("sustainLevel"), 0.0f, 1e-6f);        // the view follows the layout: no four-slot shape
    env->replace_stages(std::move(*Envelope::make_adsr(0.2f, 0.1f, 0.7f, 0.0f)));
    CHECK_NEAR(env->get_setting("sustainLevel"), 0.7f, 1e-6f);        // and reads the new layout's slot (what the editor re-pulls)
    CHECK_NEAR(env->stage(2).ramp.startVal, 0.7f, 1e-6f);
    env->replace_stages(std::move(*Envelope::make_as(0.05f)));
    CHECK(env->pins() == pinsBefore);
    CHECK(env->get_param("minValue") == lo && env->get_param("maxValue") == hi);
    CHECK_NEAR(env->get_setting("stage_accuracy"), 0.5f, 1e-6f);      // a knob outside the layout survives
    CHECK(env->stage_count() == 2);

    env->prepare(ctx); env->start(4800);
    const int n = 25;
    for (int i = 0; i < n; ++i) env->next();
    CHECK(lo->starts == 1 && hi->starts == 1);
    CHECK(lo->nexts == n && hi->nexts == n);
}
// Checkpoint 2.1: the noise family with its pins declared once.
#include "mforce/source/noise/layered_red_noise_source.h"
#include "mforce/source/noise/blue_noise_source.h"
#include "mforce/source/noise/crackle_noise_source.h"
#include "mforce/source/noise/murmuration_noise_source.h"
#include "mforce/source/noise/perlin_noise_source.h"
#include "mforce/source/noise/velvet_noise_source.h"
#include "mforce/source/noise/violet_noise_source.h"
#include "mforce/source/noise/pink_noise_source.h"
#include "mforce/source/noise/red_noise_source.h"
#include "mforce/source/noise/wander_noise_source.h"
#include "mforce/source/noise/wander_noise2_source.h"
#include "mforce/source/noise/wander_noise3_source.h"
#include "mforce/source/noise/white_noise_source.h"

namespace {
std::string param_names(const mforce::Component& c) {
    std::string out;
    for (const auto& d : c.param_descriptors()) { out += d.name; out += ','; }
    return out;
}
float param_default(const mforce::Component& c, const char* name) {
    for (const auto& d : c.param_descriptors())
        if (std::string(d.name) == name) return d.default_value;
    return -999.0f;
}
}

// Every type prepares and renders finite values, and says which pins it
// declares, in the order they are advanced.
static void run_noise_pin_tests() {
    RenderContext ctx{48000};

    auto smoke = [&](ValueSource& s) {
        s.prepare(ctx); s.start(4800);
        for (int i = 0; i < 10; ++i) CHECK(std::isfinite(s.next()));
    };
    WhiteNoiseSource white;           smoke(white);
    PinkNoiseSource pink;             smoke(pink);
    BlueNoiseSource blue;             smoke(blue);
    VioletNoiseSource violet;         smoke(violet);
    VelvetNoiseSource velvet;         smoke(velvet);
    PerlinNoiseSource perlin;         smoke(perlin);
    CrackleNoiseSource crackle;       smoke(crackle);
    MurmurationNoiseSource murmur;    smoke(murmur);
    WanderNoiseSource wander;     smoke(wander);
    WanderNoise2Source wander2;   smoke(wander2);
    WanderNoise3Source wander3;   smoke(wander3);
    RedNoiseSource red;           smoke(red);
    LayeredRedNoiseSource layered; smoke(layered);

    CHECK(param_names(white) == "amplitude,density,boost,continuity,zeroCrossTendency,");
    CHECK(param_names(pink) == "amplitude,");
    CHECK(param_names(blue) == "amplitude,");
    CHECK(param_names(violet) == "amplitude,");
    CHECK(param_names(velvet) == "density,amplitude,");
    CHECK(param_names(perlin) == "speed,octaves,persistence,lacunarity,");
    CHECK(param_names(crackle) == "chaos,");
    CHECK(param_names(murmur) == "count,cohesion,alignment,separation,chaos,speed,");
    CHECK(param_names(wander) == "amplitude,speed,deltaSpeed,slopeLimit,");
    CHECK(param_names(wander2) == "amplitude,minSpeed,maxSpeed,reverseProb,retraceProb,retracePct,");
    CHECK(param_names(wander3) == "amplitude,speed,deltaSpeed,slopeLimit,");
    CHECK(param_names(red) ==
          "amplitude,frequency,phase,density,smoothness,rampVariation,boost,continuity,zeroCrossTendency,");
    CHECK(param_names(layered).empty());
    CHECK(layered.input_descriptors().empty());

    CHECK(white.range() == Generator::Range::Bipolar);
    CHECK(red.range() == Generator::Range::Bipolar);
    CHECK(blue.range() == Generator::Range::None);
    CHECK(murmur.range() == Generator::Range::None);
    CHECK(layered.range() == Generator::Range::None);
}

// WhiteNoiseSource: at the inert defaults one draw per sample, exactly
// the historical sequence; a wired pin is prepared and advanced once
// per sample.
static void run_noise_white_tests() {
    RenderContext ctx{48000};

    WhiteNoiseSource w(777u);
    Randomizer ref(777u);
    w.prepare(ctx); w.start(16);
    for (int i = 0; i < 5; ++i) CHECK(w.next() == ref.valuePN());
    auto dens = std::make_shared<PinCounter>();
    dens->level = 1.0f;
    w.set_param("density", dens);
    w.prepare(ctx); w.start(16);
    for (int i = 0; i < 5; ++i) w.next();
    CHECK(dens->starts == 1 && dens->nexts == 5);
    CHECK(w.get_param("density") == dens);
    CHECK(w.get_param("boost") == nullptr);
}

// Three declared defaults: an unwired node reads the descriptor's value, so
// the CLI and the UI agree.
static void run_noise_default_tests() {
    RenderContext ctx{48000};

    RedNoiseSource red;
    CHECK_NEAR(red.density.current(), 0.5f, 1e-6f);
    CHECK_NEAR(red.smoothness.current(), 0.5f, 1e-6f);
    CHECK_NEAR(red.rampVariation.current(), 0.0f, 1e-6f);
    CHECK_NEAR(param_default(red, "density"), 0.5f, 1e-6f);
    CHECK_NEAR(param_default(red, "rampVariation"), 0.0f, 1e-6f);

    WanderNoise2Source w2;
    CHECK_NEAR(w2.minSpeed.current(), 0.01f, 1e-6f);
    CHECK_NEAR(w2.maxSpeed.current(), 0.1f, 1e-6f);
    w2.prepare(ctx); w2.start(4800);
    float last = w2.next();
    bool steady = true;
    for (int i = 0; i < 2000; ++i) {              // a walk, not rail to rail
        const float v = w2.next();
        steady = steady && std::fabs(v - last) <= 0.1f + 1e-6f;
        last = v;
    }
    CHECK(steady);
}

// RedNoiseSource keeps WaveSource's three pins and adds its own after
// them; its pins are reached by name, base and own alike.
static void run_noise_red_pin_tests() {
    RenderContext ctx{48000};

    RedNoiseSource red(99u);
    auto lfoDepth = std::make_shared<PinCounter>();
    auto zct = std::make_shared<PinCounter>();
    red.set_param("amplitude", lfoDepth);
    red.set_param("zeroCrossTendency", zct);
    CHECK(red.amplitude.source == lfoDepth);
    CHECK(red.zeroCrossTendency.source == zct);
    red.set_param("frequency", std::make_shared<ConstantSource>(100.0f));
    red.prepare(ctx); red.start(4800);
    for (int i = 0; i < 7; ++i) red.next();
    CHECK(lfoDepth->starts == 1 && lfoDepth->nexts == 7);
    CHECK(zct->starts == 1 && zct->nexts == 7);
    red.clear_param("zeroCrossTendency");
    CHECK(red.get_param("zeroCrossTendency") == nullptr);
}

// The walks that never apply `amplitude` leave it unpulled (Manual): it
// is prepared once and never advanced. WanderNoise3 does apply it.
static void run_noise_manual_amplitude_tests() {
    RenderContext ctx{48000};

    auto amp1 = std::make_shared<PinCounter>();
    auto amp2 = std::make_shared<PinCounter>();
    auto amp3 = std::make_shared<PinCounter>();
    auto spd = std::make_shared<PinCounter>();
    WanderNoiseSource w1;
    WanderNoise2Source w2;
    WanderNoise3Source w3;
    w1.set_param("amplitude", amp1);
    w1.set_param("speed", spd);
    w2.set_param("amplitude", amp2);
    w3.set_param("amplitude", amp3);
    w1.prepare(ctx); w1.start(4800);
    w2.prepare(ctx); w2.start(4800);
    w3.prepare(ctx); w3.start(4800);
    for (int i = 0; i < 5; ++i) { w1.next(); w2.next(); w3.next(); }
    CHECK(amp1->starts == 1 && amp1->nexts == 0);
    CHECK(amp2->starts == 1 && amp2->nexts == 0);
    CHECK(amp3->starts == 1 && amp3->nexts == 5);
    CHECK(spd->starts == 1 && spd->nexts == 5);
    // The Manual pins were prepared, not only started: they know the rate.
    CHECK(amp1->rate == 48000 && amp2->rate == 48000);
    CHECK(amp3->rate == 48000 && spd->rate == 48000);
}

// LayeredRedNoiseSource: the layer table is two arrays, the layer count a
// setting that follows them.
static void run_noise_layered_tests() {
    RenderContext ctx{48000};

    LayeredRedNoiseSource l;
    CHECK(l.setting_descriptors().size() == 1);
    CHECK_NEAR(l.setting_descriptors()[0].default_value, 3.0f, 1e-6f);
    CHECK(l.array_descriptors().size() == 2);
    CHECK_NEAR(l.array_descriptors()[0].default_value, 7.0f, 1e-6f);
    CHECK_NEAR(l.array_descriptors()[1].default_value, 0.05f, 1e-6f);
    CHECK(l.get_array("frequency") == std::vector<float>({0.5f, 2.0f, 8.0f}));
    CHECK(l.get_array("amplitude") == std::vector<float>({1.0f, 1.0f, 1.0f}));
    CHECK_NEAR(l.get_setting("count"), 3.0f, 1e-6f);

    l.set_setting("count", 5.0f);                   // new rows take the declared defaults
    CHECK_NEAR(l.get_setting("count"), 5.0f, 1e-6f);
    CHECK(l.get_array("frequency") == std::vector<float>({0.5f, 2.0f, 8.0f, 7.0f, 7.0f}));
    CHECK(l.get_array("amplitude") == std::vector<float>({1.0f, 1.0f, 1.0f, 0.05f, 0.05f}));
    l.set_setting("count", 99.0f);
    CHECK_NEAR(l.get_setting("count"), 16.0f, 1e-6f);
    CHECK(l.get_array("frequency").size() == 16);
    l.set_setting("count", 0.0f);
    CHECK_NEAR(l.get_setting("count"), 1.0f, 1e-6f);
    CHECK(l.get_array("amplitude").size() == 1);

    l.set_array("frequency", {1.0f, 2.0f});          // the arrays set the count
    CHECK_NEAR(l.get_setting("count"), 2.0f, 1e-6f);
    l.set_array("amplitude", {0.5f, 0.25f});
    CHECK_NEAR(l.get_setting("count"), 2.0f, 1e-6f);
    CHECK(l.get_array("amplitude") == std::vector<float>({0.5f, 0.25f}));
    l.prepare(ctx); l.start(4800);                           // the dirty layers rebuild here
    float sum = 0.0f;
    for (int i = 0; i < 2000; ++i) sum += std::fabs(l.next());
    CHECK(sum > 0.0f);
}

// The shaped-noise draw white and red share: two verbatim copies of the
// deleted call-site blocks against the helper, bit-for-bit over a grid, on
// three identically seeded Randomizers (same seed, same draws).
static void run_shaped_draw_twin_tests() {
    auto white_block = [](Randomizer& rng_, float zct, float bst, float cont,
                          float lastVal_, float& lastSign_) {
        float v;
        if (rng_.decide(zct)) {
            lastSign_ = -lastSign_;
        } else {
            lastSign_ = float(rng_.sign());
            if (lastSign_ == 0.0f) lastSign_ = 1.0f;
        }
        const float b = std::clamp(bst, 0.0f, 1.0f);
        v = rng_.range(b, 1.0f) * lastSign_;
        if (cont != 0.0f) {
            const float influence = std::min(cont, 0.999f);
            v = rng_.range(lastVal_, v, lastVal_, influence);
        }
        return v;
    };
    auto red_block = [](Randomizer& rng_, float zct, float boostIn, float contIn,
                        float lastValue_, float& lastSign_) {
        float nextValue_;
        if (rng_.decide(zct)) {
            lastSign_ = -lastSign_;
        } else {
            lastSign_ = float(rng_.sign());
            if (lastSign_ == 0.0f) lastSign_ = 1.0f;
        }
        float b = boostIn;
        b = std::clamp(b, 0.0f, 1.0f);
        nextValue_ = rng_.range(b, 1.0f) * lastSign_;
        float cont = contIn;
        if (cont != 0.0f) {
            float influence = std::min(cont, 0.999f);
            nextValue_ = rng_.range(lastValue_, nextValue_, lastValue_, influence);
        }
        return nextValue_;
    };

    const float grid[] = {0.0f, 0.2f, 0.5f, 0.999f, 1.0f, -0.3f, 1.7f};
    int bad = 0, rows = 0;
    uint32_t seed = 0x5A9Du;
    for (float zct : grid) for (float bst : grid) for (float cont : grid) for (float last : grid) {
        for (float sign0 : {-1.0f, 1.0f}) {
            ++seed; ++rows;
            Randomizer ra(seed), rb(seed), rc(seed);
            float sa = sign0, sb = sign0, sc = sign0;
            for (int k = 0; k < 4; ++k) {          // chain: a reordered draw desyncs later rows
                float a = white_block(ra, zct, bst, cont, last, sa);
                float b = red_block(rb, zct, bst, cont, last, sb);
                float c = shaped_draw(rc, zct, bst, cont, last, sc);
                if (!same_bits(a, b) || !same_bits(a, c)) ++bad;
                if (!same_bits(sa, sb) || !same_bits(sa, sc)) ++bad;
            }
            if (!same_bits(ra.valuePN(), rc.valuePN())) ++bad;   // the draw count agrees too
        }
    }
    CHECK(rows == 7 * 7 * 7 * 7 * 2);
    CHECK(bad == 0);
}

// Checkpoint 2.1: the wavetable, the hybrid KS and the evolution holders.
#include "mforce/source/wavetable/hybrid_ks_source.h"
#include "mforce/source/wavetable/wavetable_source.h"
#include "mforce/source/wavetable/wave_evolution.h"
#include "mforce/source/wavetable/averaging_evolution.h"
#include "mforce/source/wavetable/bezier_pull_evolution.h"
#include "mforce/source/wavetable/bit_rotate_evolution.h"
#include "mforce/source/wavetable/bowed_string_evolution.h"
#include "mforce/source/wavetable/brass_evolution.h"
#include "mforce/source/wavetable/cellular_automaton_evolution.h"
#include "mforce/source/wavetable/eks_evolution.h"
#include "mforce/source/wavetable/histogram_equalize_evolution.h"
#include "mforce/source/wavetable/pluck_evolution.h"
#include "mforce/source/wavetable/reaction_diffusion_evolution.h"
#include "mforce/source/wavetable/reed_evolution.h"
#include "mforce/source/wavetable/sort_erosion_evolution.h"
#include "mforce/source/wavetable/target_evolution.h"

namespace {
std::string input_names(const mforce::Component& c) {
    std::string out;
    for (const auto& d : c.input_descriptors()) { out += d.name; out += ','; }
    return out;
}
std::string setting_names(const mforce::Component& c) {
    std::string out;
    for (const auto& d : c.setting_descriptors()) { out += d.name; out += ','; }
    return out;
}
// Logs the order in which its consumers advance it, and counts prepares.
struct OrderProbe final : mforce::Generator {
    OrderProbe(std::vector<int>* l, int i) : log(l), id(i) {}
    std::vector<int>* log;
    int id;
    int starts{0};
    void on_start(int) override { ++starts; }
    float compute() override { log->push_back(id); return 1.0f; }
    const char* type_name() const override { return "OrderProbe"; }
};
}

// WavetableSource: WaveSource's three pins, then speedFactor, inputSource and
// evolution, which the node advances itself (Manual): inputSource while a
// note's table is filled, speedFactor only in the interpolating read.
static void run_wavetable_pin_tests() {
    RenderContext ctx{48000};

    WavetableSource w;
    CHECK(param_names(w) == "amplitude,frequency,phase,speedFactor,");
    CHECK(input_names(w) == "inputSource,evolution,");
    CHECK(setting_names(w) == "interpolate,");
    CHECK(w.setting_descriptors()[0].type == SettingType::Bool);
    CHECK(w.range() == Generator::Range::Bipolar);
    CHECK(w.get_param("inputSource") != nullptr);          // the seed noise
    CHECK(w.get_param("evolution") == nullptr);
    CHECK(w.get_param("speedFactor") == nullptr);
    CHECK_NEAR(w.speedFactor.current(), 1.0f, 1e-6f);
    w.prepare(ctx); w.start(4800);
    for (int i = 0; i < 10; ++i) CHECK(std::isfinite(w.next()));

    WavetableSource f;  // 480 Hz: a table of 100
    auto seed = std::make_shared<PinCounter>();
    f.set_param("inputSource", seed);
    f.set_param("frequency", std::make_shared<ConstantSource>(480.0f));
    f.prepare(ctx); f.start(4800);
    for (int i = 0; i < 250; ++i) f.next();
    CHECK(seed->starts == 1 && seed->nexts == 100);
    CHECK(seed->rate == 48000);                      // prepared, not only started

    for (bool interp : {false, true}) {
        WavetableSource s;
        auto sf = std::make_shared<PinCounter>();
        s.set_param("speedFactor", sf);
        s.set_setting("interpolate", interp ? 1.0f : 0.0f);
        CHECK(s.get_setting("interpolate") == (interp ? 1.0f : 0.0f));
        s.prepare(ctx); s.start(4800);
        for (int i = 0; i < 20; ++i) CHECK(std::isfinite(s.next()));
        CHECK(sf->starts == 1);
        CHECK(sf->rate == 48000);
        CHECK(sf->nexts == (interp ? 20 : 0));
    }
}

// The evolution slot holds the evolution; the node prepares and starts it
// through the family's base and evolves once per sample.
static void run_wavetable_evolution_tests() {
    RenderContext ctx{48000};

    WavetableSource w;
    auto reed = std::make_shared<ReedEvolution>();
    auto breath = std::make_shared<PinCounter>();
    reed->set_param("breath", breath);
    w.set_param("evolution", reed);
    CHECK(w.get_part("evolution") == reed);
    CHECK(w.evolution.get() == reed.get());
    w.prepare(ctx); w.start(4800);
    CHECK(breath->starts == 1);                          // wavetable -> evolution -> pin
    for (int i = 0; i < 10; ++i) CHECK(std::isfinite(w.next()));
    CHECK(breath->nexts == 10);
    w.clear_param("evolution");                            // no evolution, no more pulls
    CHECK(w.get_part("evolution") == nullptr);
    for (int i = 0; i < 10; ++i) w.next();
    CHECK(breath->nexts == 10);

    WavetableSource n;  // a signal is not an evolution: the slot refuses it, the node renders with no evolution
    CHECK(!n.set_param("evolution", std::make_shared<ConstantSource>(0.5f)));
    CHECK(n.get_part("evolution") == nullptr);
    n.prepare(ctx); n.start(4800);
    for (int i = 0; i < 10; ++i) CHECK(std::isfinite(n.next()));

    WavetableSource o;  // a pluck in the slot renders a non-zero table
    o.set_param("evolution", std::make_shared<PluckEvolution>(1u));
    o.prepare(ctx); o.start(4800);
    float peak = 0.0f;
    for (int i = 0; i < 2000; ++i) peak = std::max(peak, std::fabs(o.next()));
    CHECK(peak > 0.0f);
}

// HybridKSSource: WaveSource's three pins, the seed waveform (Manual: pulled
// while a note's table is filled) and three settings.
static void run_hybrid_ks_tests() {
    RenderContext ctx{48000};

    HybridKSSource h;
    CHECK(param_names(h) == "amplitude,frequency,phase,");
    CHECK(input_names(h) == "inputSource,");
    CHECK(setting_names(h) == "holdCycles,morphDuration,numPartials,");
    CHECK_NEAR(h.get_setting("holdCycles"), 5.0f, 1e-6f);
    CHECK_NEAR(h.get_setting("morphDuration"), 0.5f, 1e-6f);
    CHECK_NEAR(h.get_setting("numPartials"), 30.0f, 1e-6f);
    CHECK(h.range() == Generator::Range::Bipolar);
    CHECK(h.get_param("inputSource") != nullptr);
    h.prepare(ctx); h.start(4800);
    for (int i = 0; i < 10; ++i) CHECK(std::isfinite(h.next()));

    HybridKSSource k;  // 480 Hz: a table of 100
    auto seed = std::make_shared<PinCounter>();
    k.set_param("inputSource", seed);
    k.set_param("frequency", std::make_shared<ConstantSource>(480.0f));
    k.set_setting("holdCycles", 2.0f);
    k.set_setting("numPartials", 4.0f);
    k.set_setting("morphDuration", 0.01f);
    CHECK_NEAR(k.get_setting("holdCycles"), 2.0f, 1e-6f);
    k.prepare(ctx); k.start(4800);
    bool finite = true;
    for (int i = 0; i < 1000; ++i) finite = finite && std::isfinite(k.next());   // through the switch
    CHECK(finite);
    CHECK(seed->starts == 1 && seed->nexts == 100);
}

// All twelve evolutions: the settings (and pins) each declares, in order, and
// the declared default is what an untouched evolution reports.
static void run_evolution_shape_tests() {
    PluckEvolution pluck;
    AveragingEvolution avg;
    EKSEvolution eks;
    ReedEvolution reed;
    BowedStringEvolution bowed;
    BrassEvolution brass;
    ReactionDiffusionEvolution rd;
    SortErosionEvolution sort;
    CellularAutomatonEvolution ca;
    HistogramEqualizeEvolution he;
    BezierPullEvolution bez;
    BitRotateEvolution rot;

    CHECK(setting_names(pluck) == "muting,");
    CHECK(setting_names(avg) == "sampleCount,speed,decayFactor,leading,autoAdjust,");
    CHECK(setting_names(eks) == "pickPosition,pickDirection,stiffness,decayStretch,drumBlend,");
    CHECK(setting_names(reed) == "tubeLoss,loopFilter,");
    CHECK(setting_names(bowed) == "tubeLoss,bowSpeed,bowPosition,brightness,");
    CHECK(setting_names(brass) == "tubeLoss,lipTension,lipFreqRatio,lipQ,");
    CHECK(setting_names(rd) == "feed,kill,diffU,diffV,dt,subSteps,");
    CHECK(setting_names(sort) == "swapsPerSample,descending,");
    CHECK(setting_names(ca) == "rule,threshold,levelHi,levelLo,stepEvery,");
    CHECK(setting_names(he) == "bins,rate,");
    CHECK(setting_names(bez) == "p0,p1,p2,p3,rate,");
    CHECK(setting_names(rot).empty());

    CHECK(param_names(reed) == "reedStiffness," && input_names(reed) == "breath,");
    CHECK(param_names(bowed) == "frictionGain," && input_names(bowed) == "bow,");
    CHECK(param_names(brass) == "brassiness," && input_names(brass) == "breath,");
    CHECK(param_names(rot) == "shiftBits,stepEvery," && input_names(rot).empty());
    CHECK_NEAR(param_default(reed, "reedStiffness"), 1.5f, 1e-6f);
    CHECK_NEAR(param_default(bowed, "frictionGain"), 4.0f, 1e-6f);
    CHECK_NEAR(param_default(brass, "brassiness"), 1.5f, 1e-6f);
    CHECK_NEAR(param_default(rot, "shiftBits"), 3.0f, 1e-6f);
    CHECK_NEAR(param_default(rot, "stepEvery"), 4.0f, 1e-6f);
    CHECK(param_names(pluck).empty() && input_names(pluck).empty());

    const mforce::Component* all[] = {&pluck, &avg, &eks, &reed, &bowed, &brass, &rd, &sort, &ca, &he, &bez, &rot};
    for (const auto* c : all)
        for (const auto& d : c->setting_descriptors())
            CHECK_NEAR(c->get_setting(d.name), d.default_value, 1e-9f);
}

// An evolution's settings read back as set, and an evolution reads its own
// live value.
static void run_evolution_setting_tests() {
    ReactionDiffusionEvolution rd;
    rd.set_setting("subSteps", 3.0f);
    CHECK_NEAR(rd.get_setting("subSteps"), 3.0f, 1e-6f);

    SortErosionEvolution sort;
    sort.set_setting("descending", 1.0f);
    CHECK_NEAR(sort.get_setting("descending"), 1.0f, 1e-6f);

    CellularAutomatonEvolution ca;
    ca.set_setting("stepEvery", 8.0f);                     // the evolution reads the live level
    std::vector<float> table(8, 0.5f);
    ca.shape_excitation(table);
    ca.evolve(table, 0);
    CHECK_NEAR(table[0], 0.8f, 1e-6f);
    ca.set_setting("levelHi", 0.3f);
    ca.evolve(table, 0);
    CHECK_NEAR(table[0], 0.3f, 1e-6f);

    PluckEvolution pluck;
    pluck.set_setting("muting", 0.5f);
    CHECK_NEAR(pluck.get_setting("muting"), 0.5f, 1e-6f);
    AveragingEvolution avg;
    avg.set_setting("leading", 1.0f);
    avg.set_setting("speed", 2.5f);
    CHECK_NEAR(avg.get_setting("leading"), 1.0f, 1e-6f);
    CHECK_NEAR(avg.get_setting("speed"), 2.5f, 1e-6f);
    CHECK_NEAR(avg.get_setting("decayFactor"), 0.999f, 1e-6f);
}

// The four evolutions with wired pins (reed, bowed, brass, bit rotate): the
// pins are Manual. The base prepares and starts them once; the evolution
// advances each once per sample in its own order.
static void run_evolution_manual_pin_tests() {
    RenderContext ctx{48000};

    auto run = [&](std::shared_ptr<Component> evo, const char* first, const char* second) {
        std::vector<int> log;
        auto a = std::make_shared<OrderProbe>(&log, 1);
        auto b = std::make_shared<OrderProbe>(&log, 2);
        evo->set_param(first, a);
        evo->set_param(second, b);
        WavetableSource w;
        w.set_param("evolution", evo);
        w.prepare(ctx); w.start(4800);
        for (int i = 0; i < 4; ++i) CHECK(std::isfinite(w.next()));
        CHECK(a->starts == 1 && b->starts == 1);
        CHECK(log == std::vector<int>({1, 2, 1, 2, 1, 2, 1, 2}));
    };
    run(std::make_shared<ReedEvolution>(), "reedStiffness", "breath");
    run(std::make_shared<BowedStringEvolution>(), "bow", "frictionGain");
    run(std::make_shared<BrassEvolution>(), "brassiness", "breath");
    run(std::make_shared<BitRotateEvolution>(), "stepEvery", "shiftBits");
}

// The family's base prepares and starts every pin of an evolution once,
// through the wavetable that holds it; a second prepare reaches the pins
// again (prepare is idempotent) and a Manual pin is advanced only by evolve.
static void run_evolution_lifecycle_tests() {
    RenderContext ctx{48000};
    auto run = [&](std::shared_ptr<WaveEvolution> evo, const char* pin) {
        auto probe = std::make_shared<PinCounter>();
        evo->set_param(pin, probe);
        WavetableSource w;
        w.set_param("evolution", evo);
        CHECK(probe->ctxPrepares == 0 && probe->starts == 0);
        w.prepare(ctx);
        CHECK(probe->rate == 48000 && probe->ctxPrepares == 1 && probe->starts == 0);
        w.start(4800);
        CHECK(probe->starts == 1 && probe->nexts == 0);
        bool finite = true;
        for (int i = 0; i < 64; ++i) finite = finite && std::isfinite(w.next());
        CHECK(finite);
        w.prepare(ctx);
        CHECK(probe->ctxPrepares == 2 && probe->starts == 1);
    };
    run(std::make_shared<ReedEvolution>(), "breath");
    run(std::make_shared<BrassEvolution>(), "brassiness");
    run(std::make_shared<BowedStringEvolution>(), "bow");
    run(std::make_shared<BitRotateEvolution>(), "shiftBits");
}

// A setting is read where it is used, after construction: Averaging's
// direction and window decide the next evolve; Pluck's draw stream is the
// constructor's seed whatever settings are set afterwards.
static void run_evolution_live_read_tests() {
    auto averaged = [](bool leading) {
        AveragingEvolution a;
        a.set_setting("sampleCount", 2.0f);
        a.set_setting("speed", 1.0f);
        a.set_setting("decayFactor", 0.5f);
        a.set_setting("leading", leading ? 1.0f : 0.0f);
        std::vector<float> t = {1.0f, 2.0f, 3.0f, 4.0f};
        a.adjust(440.0f);
        a.evolve(t, 0);
        return t[0];
    };
    CHECK_NEAR(averaged(false), 1.25f, 1e-6f);   // (1 + 4) / 2 * 0.5: trailing reads index 0 and the wrap
    CHECK_NEAR(averaged(true), 0.75f, 1e-6f);    // (1 + 2) / 2 * 0.5

    RenderContext ctx{48000};
    auto render = [&](uint32_t seed, bool setMuting) {
        auto p = std::make_shared<PluckEvolution>(seed);
        if (setMuting) p->set_setting("muting", 0.0f);     // the default's value, set after construction
        WavetableSource w;
        w.set_param("evolution", p);
        w.prepare(ctx); w.start(4800);
        std::vector<float> out;
        for (int i = 0; i < 2048; ++i) out.push_back(w.next());
        return out;
    };
    const auto a = render(7u, true), b = render(7u, false), c = render(0xDEAD'BEEFu, false);
    bool ab = true, bc = true;
    for (size_t i = 0; i < a.size(); ++i) { ab = ab && same_bits(a[i], b[i]); bc = bc && same_bits(b[i], c[i]); }
    CHECK(ab);                                            // setting muting did not restart or reseed the stream
    CHECK(!bc);                                           // the seed is the stream

    // autoAdjust: the per-note decay and window follow the frequency
    auto averaging_at = [](bool autoAdjust, float sampleCount, float decayFactor, float hz, std::vector<float> t) {
        AveragingEvolution a;
        a.set_setting("sampleCount", sampleCount);
        a.set_setting("speed", 1.0f);
        a.set_setting("decayFactor", decayFactor);
        a.set_setting("autoAdjust", autoAdjust ? 1.0f : 0.0f);
        a.adjust(hz);
        for (int i = 0; i < int(t.size()); ++i) a.evolve(t, i);
        return t;
    };
    const auto ones = averaging_at(true, 1.0f, 0.5f, 880.0f, std::vector<float>(8, 1.0f));
    CHECK_NEAR(ones[0], 0.875f, 1e-6f);                  // 1 - (1 - 0.5) * (1 - (880 - 220) / 880): the decay follows the pitch
    std::vector<float> ramp(16);
    for (int i = 0; i < 16; ++i) ramp[size_t(i)] = float(i) / 16.0f;
    const float window = float(2.0f - ((2.0f - 1.0f) - (2.0f - 1.0f) * std::exp(-0.0008 * double(880.0f - 440.0f))));
    const auto adjusted = averaging_at(true, 2.0f, 1.0f, 880.0f, ramp);
    const auto stated = averaging_at(false, window, 1.0f, 880.0f, ramp);   // the same window, stated rather than derived
    const auto plain = averaging_at(false, 2.0f, 1.0f, 880.0f, ramp);
    bool as = true, ap = true;
    for (size_t i = 0; i < 16; ++i) { as = as && same_bits(adjusted[i], stated[i]); ap = ap && same_bits(adjusted[i], plain[i]); }
    CHECK(as);                                            // above 440 Hz the window shrinks to this float, and it is the one evolve draws against
    CHECK(!ap);
}

// One cycle at full morph over a zero table: the table becomes the target
// resampled to `len`.
static std::vector<float> target_one_cycle(const RenderContext& ctx, TargetEvolution& e, int len) {
    e.set_setting("holdCycles", 0.0f);
    e.set_setting("morphRate", 1.0f);
    e.set_setting("decayFactor", 1.0f);
    e.prepare(ctx, 64);
    std::vector<float> v(size_t(len), 0.0f);
    e.adjust(440.0f);
    e.shape_excitation(v);
    for (int i = 0; i < len; ++i) e.evolve(v, i);
    return v;
}

// TargetEvolution: a registered part whose target is synthesised at prepare
// from harmonic amplitudes and peak-normalised; with no hold and a full morph
// step the table becomes the target in one cycle; an empty target keeps plain
// averaging; a wavetable renders it.
static void run_target_evolution_tests() {
    RenderContext ctx{48000};
    TargetEvolution t;
    CHECK(setting_names(t) == "targetLength,morphRate,holdCycles,decayFactor,");
    CHECK(param_names(t).empty() && input_names(t).empty());
    CHECK(t.get_array("targetPartials") == std::vector<float>{1.0f});
    CHECK_NEAR(t.get_setting("targetLength"), 1024.0f, 1e-6f);
    CHECK_NEAR(t.get_setting("morphRate"), 0.001f, 1e-9f);
    CHECK_NEAR(t.get_setting("holdCycles"), 3.0f, 1e-6f);
    CHECK_NEAR(t.get_setting("decayFactor"), 0.998f, 1e-9f);

    t.set_setting("targetLength", 8.0f);
    const auto sine = target_one_cycle(ctx, t, 8);
    CHECK_NEAR(sine[2], 1.0f, 1e-6f);  CHECK_NEAR(sine[6], -1.0f, 1e-6f);
    CHECK_NEAR(sine[0], 0.0f, 1e-6f);  CHECK_NEAR(sine[4], 0.0f, 1e-6f);
    TargetEvolution half;                                  // half amplitude in, unit peak out
    half.set_array("targetPartials", {0.5f});
    half.set_setting("targetLength", 8.0f);
    CHECK_NEAR(target_one_cycle(ctx, half, 8)[2], 1.0f, 1e-6f);
    TargetEvolution two;                                   // two partials: a different shape, still a unit peak
    two.set_array("targetPartials", {1.0f, 0.5f});
    two.set_setting("targetLength", 64.0f);
    const auto shape = target_one_cycle(ctx, two, 64);
    float peak = 0.0f; bool differs = false;
    for (int i = 0; i < 64; ++i) { peak = std::max(peak, std::fabs(shape[size_t(i)])); differs = differs || std::fabs(shape[size_t(i)] - std::sin(float(i) / 64.0f * 2.0f * 3.14159265f)) > 1e-3f; }
    CHECK_NEAR(peak, 1.0f, 1e-6f);
    CHECK(differs);

    TargetEvolution none;                                  // no partials: plain averaging with the decay, no division by zero
    none.set_array("targetPartials", {});
    none.set_setting("holdCycles", 0.0f);
    none.set_setting("decayFactor", 0.5f);
    none.prepare(ctx, 64);
    std::vector<float> flat(8, 0.5f);
    none.adjust(440.0f);
    none.evolve(flat, 0);
    CHECK_NEAR(flat[0], 0.25f, 1e-6f);

    WavetableSource w;                                     // under a wavetable: finite and sounding
    w.set_param("evolution", std::make_shared<TargetEvolution>());
    w.prepare(ctx); w.start(4800);
    float wpeak = 0.0f; bool finite = true;
    for (int i = 0; i < 4800; ++i) { const float s = w.next(); finite = finite && std::isfinite(s); wpeak = std::max(wpeak, std::fabs(s)); }
    CHECK(finite && wpeak > 0.0f);
}

// TargetEvolution's morph over a non-zero table (half morphed and decayed in
// one step, saturated the next) and its resample of an 8-sample target onto a
// 12-sample table.
static void run_target_evolution_morph_tests() {
    RenderContext ctx{48000};
    // the morph over a non-zero table: one cycle at morphRate 0.5 and decay 0.9
    TargetEvolution morph;
    morph.set_setting("targetLength", 8.0f);
    morph.set_setting("holdCycles", 0.0f);
    morph.set_setting("morphRate", 0.5f);
    morph.set_setting("decayFactor", 0.9f);
    morph.prepare(ctx, 64);
    std::vector<float> table(8, 0.5f);
    morph.adjust(440.0f);
    for (int i = 0; i < 8; ++i) morph.evolve(table, i);
    const float s = std::sin(0.125f * (2.0f * dsp::kPi) * 1.0f);  // the synthesised target's second sample
    CHECK_NEAR(table[0], 0.2375f, 1e-6f);                  // (0.5 * (1 - 0.5) + 0 * 0.5) * (1 - 0.1 * 0.5): half morphed, decayed
    CHECK_NEAR(table[1], s, 1e-6f);                         // morphAmount saturates at 1: the target, undecayed
    CHECK_NEAR(table[2], 1.0f, 1e-6f);
    CHECK_NEAR(table[7], -s, 1e-6f);
    // the resample: a 12-sample table from an 8-sample target interpolates between neighbours
    TargetEvolution twelve;
    twelve.set_setting("targetLength", 8.0f);
    const auto r = target_one_cycle(ctx, twelve, 12);
    CHECK_NEAR(r[1], s * (2.0f / 3.0f), 1e-5f);           // position 2/3 of the way from t[0] = 0 to t[1] = s
    CHECK_NEAR(r[3], 1.0f, 1e-6f);                         // position 2: t[2]
    CHECK_NEAR(r[6], 0.0f, 1e-6f);                         // position 4: t[4]
    CHECK_NEAR(r[9], -1.0f, 1e-6f);                        // position 6: t[6]
}

// Checkpoint 2.1: the additive family (sources, partials, formants).
#include "mforce/source/additive/formant.h"
#include "mforce/source/additive/formant_peaks.h"
#include "mforce/source/additive/formant_bands.h"
#include "mforce/source/additive/formant_sequence.h"
#include "mforce/source/additive/additive_source.h"
#include "mforce/source/additive/partial_set.h"
#include "mforce/core/driven_part.h"
#include "mforce/source/additive/partials.h"
#include "mforce/source/additive/coherent_walk.h"
#include "mforce/source/additive/pair_trade.h"
#include "mforce/source/additive/bandwidth_noise.h"
#include "mforce/source/additive/onset_gate.h"
#include "mforce/source/additive/decay_law.h"
#include "mforce/source/additive/partial_layout.h"
#include "mforce/core/dsp/segment_walk.h"
#include "mforce/source/additive/full_partials.h"
#include "mforce/source/additive/sequence_partials.h"
#include "mforce/source/additive/explicit_partials.h"
#include "mforce/source/additive/composite_partials.h"
#include "mforce/source/additive/expand_rule_node.h"
#include "mforce/source/additive/expand_rule.h"

namespace {
std::string array_names(const mforce::Component& c) {
    std::string out;
    for (const auto& d : c.array_descriptors()) { out += d.name; out += ','; }
    return out;
}
// The eight envelope pins of every Partials, in the order next() advances them.
const char* const kPartialsEnvNames[8] = {"multEnv", "amplEnv", "poEnv", "roEnv",
                                          "dtEnv", "bwEnv", "motionEnv", "shimmerEnv"};
}

// The source declares the WaveSource trio and then its own pins, in the
// order wave() advances them; the structural inputs are named but never
// pulled.
static void run_additive_source_pin_tests() {
    AdditiveSource full;
    CHECK(param_names(full) == "amplitude,frequency,phase,formantWeight,formantFloor,");
    CHECK(input_names(full) == "formant,partials,");
    CHECK(setting_names(full) == "noiseBedLevel,noiseBedFreq,noiseBedWidth,noiseBedDelay,"
                                 "noiseBedFadePow,noiseBedFade,");
    CHECK_NEAR(param_default(full, "formantWeight"), 0.0f, 1e-6f);
    CHECK_NEAR(param_default(full, "formantFloor"), 1.0f, 1e-6f);
    CHECK_NEAR(full.get_setting("noiseBedFreq"), 2900.0f, 1e-3f);
    CHECK_NEAR(full.get_setting("noiseBedFade"), 0.05f, 1e-6f);
    CHECK(std::string(full.type_name()) == "AdditiveSource");
    CHECK(full.get_param("formantFloor") == nullptr);
    CHECK(full.get_param("formant") == nullptr);
}

// A one-row FormantPeaks: the peak the split tests shape.
static std::shared_ptr<FormantPeaks> make_one_peak(float hz, float width = 500.0f, float gain = 1.0f, float power = 2.0f) {
    auto pk = std::make_shared<FormantPeaks>();
    pk->set_array("frequency", {hz});
    pk->set_array("gain", {gain});
    pk->set_array("width", {width});
    pk->set_array("power", {power});
    return pk;
}

// The structural types: the pins they declare, in descriptor order.
static void run_additive_structural_pin_tests() {
    FormantBands band;
    CHECK(param_names(band) == "startFreq,freqIncrement,");
    CHECK(array_names(band) == "gains,");
    band.set_array("gains", {0.0f, 1.0f, 0.0f});
    CHECK_NEAR(band.gain(1.0f), 1.0f, 1e-6f);
    CHECK(band.gain(1.5f) > 0.0f && band.gain(2.5f) == 0.0f);                 // the span ends at startFreq + 2

    FormantPeaks peaks;
    CHECK(param_names(peaks).empty());
    CHECK(array_names(peaks) == "frequency,gain,width,power,");
    for (const auto& d : peaks.array_descriptors()) CHECK(std::string(d.groupName) == "peaks");
    CHECK_NEAR(peaks.array_descriptors()[0].default_value, 1000.0f, 1e-3f);
    CHECK_NEAR(peaks.array_descriptors()[2].default_value, 500.0f, 1e-3f);
    CHECK_NEAR(peaks.array_descriptors()[3].min_value, 0.01f, 1e-6f);
    CHECK(peaks.peak_count() == 0);

    FormantSequence seq;
    CHECK(param_names(seq) == "blend,");
    CHECK(input_names(seq) == "spectra,");
    CHECK(seq.input_descriptors()[0].multi);
    CompositePartials comp;
    CHECK(param_names(comp).empty());
    CHECK(input_names(comp) == "partials,");
    CHECK(comp.input_descriptors()[0].multi);

    ExpandRuleNode rule;
    CHECK(param_names(rule) == "spacing1,spacing2,dt1,dt2,loPct1,loPct2,power1,power2,po1,po2,");
    CHECK(setting_names(rule) == "count,recurse,");
    CHECK(rule.to_struct().count == 2 && rule.to_struct().recurse == 0);
    CHECK_NEAR(rule.to_struct().spacing1, 0.5f, 1e-6f);
    CHECK_NEAR(rule.to_struct().dt2, 0.01f, 1e-6f);
    rule.set_setting("count", 4.0f);
    rule.set_param("po2", std::make_shared<ConstantSource>(0.25f));
    CHECK(rule.to_struct().count == 4);
    CHECK_NEAR(rule.to_struct().po2, 0.25f, 1e-6f);
}

// Every Partials declares the same eight envelope pins and the shared
// settings; a subclass adds its own settings after them.
static void run_additive_partials_pin_tests() {
    FullPartials full;
    SequencePartials seq;
    ExplicitPartials expl;
    std::string envs = "multEnv,amplEnv,poEnv,roEnv,dtEnv,bwEnv,motionEnv,shimmerEnv,";
    CHECK(param_names(full) == envs);
    CHECK(param_names(seq) == envs);
    CHECK(param_names(expl) == envs);
    CHECK(input_names(full) == "expandRule," && input_names(seq) == "expandRule," && input_names(expl) == "expandRule,");
    CHECK(array_names(full).empty() && array_names(seq).empty());
    CHECK(array_names(expl) == "mult1,ampl1,mult2,ampl2,");
    for (const auto& d : expl.array_descriptors()) CHECK(std::string(d.groupName) == "partials");

    const std::string shared = "rolloff1,rolloff2,detune1,detune2,bandwidth1,bandwidth2,bandwidthHz,";
    CHECK(setting_names(full).rfind(shared, 0) == 0);
    CHECK(setting_names(full).find("maxPartials,minMult,evenWeight1,evenWeight2,oddWeight1,oddWeight2,"
                                   "unitPO1,unitPO2,") != std::string::npos);
    CHECK(setting_names(seq).find("maxPartials,minMult1,minMult2,incr1,incr2,unitPO1,unitPO2,")
          != std::string::npos);
    CHECK(setting_names(expl).find("maxPartials,evolve,unitPO1,unitPO2,") != std::string::npos);
    CHECK(has_name(full.setting_descriptors(), "inharmonicity"));
    CHECK(has_name(seq.setting_descriptors(), "onsetFade"));
    CHECK_NEAR(full.get_setting("maxPartials"), 30.0f, 1e-6f);
    CHECK_NEAR(seq.get_setting("maxPartials"), 30.0f, 1e-6f);
    CHECK_NEAR(expl.get_setting("maxPartials"), 16.0f, 1e-6f);
    CHECK_NEAR(full.get_setting("motionHz"), 4.0f, 1e-6f);
    CHECK_NEAR(full.get_setting("onsetFade"), 0.03f, 1e-6f);
    CHECK_NEAR(full.get_setting("bandwidthHz"), 30.0f, 1e-6f);
    CHECK(expl.get_setting("evolve") == 1.0f);
    for (const auto& d : expl.setting_descriptors())
        if (std::string(d.name) == "evolve") CHECK(d.type == SettingType::Bool);
    CHECK(full.partial_count() == 30 && seq.partial_count() == 30 && expl.partial_count() == 16);
}

// Every type prepares and renders finite values; a wired source plays.
static void run_additive_smoke_tests() {
    RenderContext ctx{48000};
    auto smoke = [&](ValueSource& s) {
        s.prepare(ctx); s.start(4800);
        for (int i = 0; i < 10; ++i) CHECK(std::isfinite(s.next()));
    };
    AdditiveSource bare;     smoke(bare);                 // no partials: silent
    // A formant part is driven by its owner; its gain is what it answers.
    auto smokeFormant = [&](Formant& f) {
        f.prepare(ctx); f.start(4800);
        for (int i = 0; i < 10; ++i) { f.next(); CHECK(std::isfinite(f.gain(1000.0f))); }
    };
    FormantBands band;                  smokeFormant(band);
    FormantPeaks peaks;                 peaks.set_array("frequency", {1000.0f}); peaks.set_array("gain", {1.0f});
                                        peaks.set_array("width", {500.0f}); peaks.set_array("power", {2.0f}); smokeFormant(peaks);
    FormantSequence seq;                smokeFormant(seq);
    // A partials part is driven by its owner; its sum is what it answers.
    auto smokePartials = [&](PartialSet& p) {
        p.prepare(ctx); p.start(4800);
        for (int i = 0; i < 10; ++i) { p.next(); CHECK(std::isfinite(p.sum_partials(0.0f, 1.0f, 220.0f, 0.0f, nullptr, 0.0f, 1.0f))); }
    };
    CompositePartials comp;             smokePartials(comp);
    ExpandRuleNode rule;                CHECK(rule.to_struct().count == 2);   // a block of fields, no lifecycle
    FullPartials full;                  smokePartials(full);
    SequencePartials sequence;          smokePartials(sequence);
    ExplicitPartials expl;              smokePartials(expl);

    AdditiveSource f;
    f.set_param("partials", std::make_shared<FullPartials>());
    f.set_param("formant", make_one_peak(1000.0f));
    f.set_param("formantWeight", std::make_shared<ConstantSource>(0.5f));
    f.prepare(ctx); f.start(4800);
    float sum = 0.0f;
    for (int i = 0; i < 400; ++i) sum += std::fabs(f.next());
    CHECK(sum > 0.0f);
}

// The pins the additive source drives through the partials and formant
// interfaces are Manual: prepared where the old code prepared them, advanced
// once per call, in declaration order, and not at all by next() on the part.
static void run_additive_manual_pin_tests() {
    RenderContext ctx{48000};

    std::vector<int> log;
    std::vector<std::shared_ptr<OrderProbe>> probes;
    FullPartials fp;
    for (int i = 0; i < 8; ++i) {
        probes.push_back(std::make_shared<OrderProbe>(&log, i + 1));
        fp.set_param(kPartialsEnvNames[i], probes.back());
    }
    fp.prepare(ctx);
    fp.start(4800);
    CHECK(log.empty());
    for (const auto& p : probes) CHECK(p->starts == 1);
    fp.next();
    CHECK(log == std::vector<int>({1, 2, 3, 4, 5, 6, 7, 8}));
    log.clear();
    fp.next();
    CHECK(log.size() == 8);
    fp.prepare(ctx); fp.start(4800);                                 // a second prepare and start: the pins start again
    for (const auto& p : probes) CHECK(p->starts == 2);

    FormantBands band;
    FormantSequence seq;
    auto b1 = std::make_shared<PinCounter>(), b2 = std::make_shared<PinCounter>();
    band.set_param("startFreq", b1);
    seq.set_param("blend", b2);
    band.prepare(ctx); band.start(4800); band.next();
    seq.prepare(ctx);  seq.start(4800);  seq.next();
    CHECK(b1->starts == 1 && b1->nexts == 1);
    CHECK(b2->starts == 1 && b2->nexts == 1);

    ExpandRuleNode rule;                                   // nothing pulls its pins: the host snapshots them
    auto r1 = std::make_shared<PinCounter>();
    rule.set_param("spacing1", r1);
    for (int i = 0; i < 3; ++i) (void)rule.to_struct();
    CHECK(r1->starts == 0 && r1->nexts == 0);
}

// AdditiveSource reads the formant weight and floor only while a formant
// is wired and partials are set, and prepares them either way.
static void run_additive_formant_pin_tests() {
    RenderContext ctx{48000};
    AdditiveSource f;
    auto w = std::make_shared<PinCounter>(), fl = std::make_shared<PinCounter>();
    f.set_param("formantWeight", w);
    f.set_param("formantFloor", fl);
    f.set_param("partials", std::make_shared<FullPartials>());
    f.prepare(ctx); f.start(4800);
    for (int i = 0; i < 5; ++i) f.next();
    CHECK(w->starts == 1 && fl->starts == 1);
    CHECK(w->nexts == 0 && fl->nexts == 0);          // no formant: never advanced

    auto formant = std::make_shared<FormantBands>();
    auto freq = std::make_shared<PinCounter>();
    formant->set_param("startFreq", freq);
    f.set_param("formant", formant);
    f.prepare(ctx); f.start(4800);
    for (int i = 0; i < 5; ++i) f.next();
    CHECK(w->starts == 2 && fl->starts == 2 && freq->starts == 1);
    CHECK(w->nexts == 5 && fl->nexts == 5 && freq->nexts == 5);

    AdditiveSource g;                           // no partials: nothing advances
    auto gw = std::make_shared<PinCounter>();
    g.set_param("formantWeight", gw);
    g.set_param("formant", formant);
    g.prepare(ctx); g.start(4800);
    for (int i = 0; i < 5; ++i) g.next();
    CHECK(gw->starts == 1 && gw->nexts == 0);
}

// The structural inputs are type-checked when wired and keep a typed view.
static void run_additive_wiring_tests() {
    AdditiveSource f;
    auto fp = std::make_shared<FullPartials>();
    auto fm = make_one_peak(800.0f);
    auto other = std::make_shared<ConstantSource>(1.0f);
    f.set_param("partials", fp);
    f.set_param("formant", fm);
    CHECK(f.get_part("partials") == fp && f.get_part("formant") == fm);
    CHECK(!f.set_param("partials", other));                // not a PartialSet: refused
    CHECK(!f.set_param("formant", other));                 // not a Formant: refused, the slot keeps its part
    CHECK(f.get_part("partials") == fp && f.get_part("formant") == fm);
    f.set_param("formant", nullptr);                       // null clears the slot
    CHECK(f.get_part("formant") == nullptr);

    FormantSequence seq;
    auto s1 = make_one_peak(500.0f), s2 = make_one_peak(1500.0f);
    seq.set_param("spectra", s1);
    CHECK(seq.spectra.views.size() == 1);
    seq.add_param("spectra", s2);
    CHECK(seq.spectra.views.size() == 2);
    seq.set_param("spectra", s2);                          // set replaces, add appends
    CHECK(seq.spectra.views.size() == 1);
    seq.add_param("spectra", other);                       // not a Formant: refused
    CHECK(seq.spectra.views.size() == 1);
    seq.clear_param("spectra");
    CHECK(seq.spectra.views.empty());
    seq.set_param("blend", other);                         // the pin is served by the base
    CHECK(seq.get_param("blend") == other && seq.get_param("spectra") == nullptr);

    CompositePartials comp;
    auto p2 = std::make_shared<ExplicitPartials>();
    comp.add_param("partials", fp);
    comp.add_param("partials", p2);                        // add appends
    CHECK(!comp.set_param("partials", other));             // not a PartialSet: refused, the slot keeps its parts
    CHECK(comp.partial_count() == 30 + 16);
    comp.clear_param("partials");
    CHECK(comp.partial_count() == 0);
}

// The settings that reshape the arrays rebuild them; the clamps hold.
static void run_additive_setting_hook_tests() {
    FullPartials full;
    full.set_setting("maxPartials", 12.0f);
    CHECK(full.get_array("mult1").size() == 12);           // get_array rebuilds a dirty set
    full.set_setting("minMult", 3.0f);
    CHECK_NEAR(full.get_array("mult1")[0], 3.0f, 1e-6f);
    full.set_setting("maxPartials", 0.0f);
    CHECK_NEAR(full.get_setting("maxPartials"), 1.0f, 1e-6f);
    full.set_setting("minMult", -4.0f);
    CHECK_NEAR(full.get_setting("minMult"), 1.0f, 1e-6f);
    full.set_setting("bandwidthHz", 0.2f);
    CHECK_NEAR(full.get_setting("bandwidthHz"), 1.0f, 1e-6f);
    full.set_setting("maxPartials", 4.0f);
    full.set_setting("evenWeight1", 0.25f);
    CHECK_NEAR(full.get_array("ampl1")[1], 0.25f, 1e-6f);  // partial 2 is even

    SequencePartials seq;
    seq.set_setting("maxPartials", 5.0f);
    seq.set_setting("incr1", 2.0f);
    auto m1 = seq.get_array("mult1");
    CHECK(m1.size() == 5);
    CHECK_NEAR(m1[2], 5.0f, 1e-6f);                        // 1 + 2 * 2
    seq.set_setting("maxPartials", -3.0f);
    CHECK_NEAR(seq.get_setting("maxPartials"), 1.0f, 1e-6f);

    ExplicitPartials expl;
    expl.set_setting("maxPartials", 5.0f);
    CHECK(expl.get_array("mult1").size() == 5);            // the working defaults
    CHECK_NEAR(expl.get_array("mult1")[4], 5.0f, 1e-6f);
    expl.set_setting("maxPartials", 0.0f);
    CHECK_NEAR(expl.get_setting("maxPartials"), 1.0f, 1e-6f);
}

// ExplicitPartials keeps the four user arrays equal and in step with
// maxPartials, and mirrors _1 into _2 while evolve is off.
static void run_additive_explicit_array_tests() {
    // The user's copies arrive in any order; mult1 last keeps each rebuild in range.
    ExplicitPartials e;
    e.set_array("mult2", {1.0f, 2.1f, 3.2f});
    e.set_array("ampl2", {0.5f, 0.4f, 0.3f});
    e.set_array("ampl1", {1.0f, 0.5f, 0.25f});
    e.set_array("mult1", {1.0f, 2.0f, 3.0f});
    CHECK(e.get_array("mult2") == std::vector<float>({1.0f, 2.1f, 3.2f}));
    CHECK(e.get_array("ampl1") == std::vector<float>({1.0f, 0.5f, 0.25f}));
    CHECK_NEAR(e.get_setting("maxPartials"), 3.0f, 1e-6f);
    CHECK(e.partial_count() == 3);

    e.set_setting("evolve", 0.0f);                         // turning evolve off mirrors _1 into _2
    CHECK(e.get_array("mult2") == e.get_array("mult1"));
    CHECK(e.get_array("ampl2") == e.get_array("ampl1"));
    e.set_array("mult1", {4.0f, 5.0f});
    CHECK(e.get_array("mult2") == std::vector<float>({4.0f, 5.0f}));
    CHECK_NEAR(e.get_setting("maxPartials"), 2.0f, 1e-6f);
    e.set_array("ampl1", {0.3f, 0.2f});
    CHECK(e.get_array("ampl2") == std::vector<float>({0.3f, 0.2f}));
    CHECK(e.get_array("nosuch").empty());

    ExplicitPartials fresh;                                // nothing set: the working defaults
    CHECK(fresh.get_array("mult1").size() == 16);
    CHECK(fresh.get_array("ampl2").size() == 16);
    CHECK(fresh.mult1.values.empty());

    RenderContext ctx{48000};
    auto g = std::make_shared<ExplicitPartials>();
    g->set_array("mult2", {1.0f, 2.0f, 3.1f});
    g->set_array("ampl2", {1.0f, 0.5f, 0.2f});
    g->set_array("ampl1", {1.0f, 0.4f, 0.3f});
    g->set_array("mult1", {1.0f, 2.0f, 3.0f});
    AdditiveSource f;
    f.set_param("partials", g);
    f.prepare(ctx); f.start(4800);
    float sum = 0.0f;
    for (int i = 0; i < 400; ++i) { float v = f.next(); CHECK(std::isfinite(v)); sum += std::fabs(v); }
    CHECK(sum > 0.0f);
}

// Checkpoint 2.1: the physical models (delay line, KS string, allpass
// resonator, bow table, 2D mesh, Pierce filter), all Processors: the signal
// input is the inherited `source`, declared first.
#include "mforce/source/physical/allpass_resonator.h"
#include "mforce/source/physical/bow_table_source.h"
#include "mforce/source/physical/ks_string.h"
#include "mforce/source/physical/mesh2d_source.h"

namespace {
// The connectable pins of a node in registration order, which is the order
// the base advances them.
std::string pin_order(const mforce::Component& c) {
    std::string out;
    for (const auto* p : c.pins())
        if (p->kind == mforce::PinKind::Param || p->kind == mforce::PinKind::Input) {
            out += p->name;
            out += ',';
        }
    return out;
}
}

// Every type prepares and renders finite values, declares its pins in the
// order they are advanced, and keeps its defaults.
static void run_physical_pin_shape_tests() {
    RenderContext ctx{48000};
    auto smoke = [&](ValueSource& s) {
        s.prepare(ctx); s.start(4800);
        for (int i = 0; i < 10; ++i) CHECK(std::isfinite(s.next()));
    };
    AllpassResonator ar;          smoke(ar);
    BowTableSource bt;            smoke(bt);
    DelayLineSource dl;           smoke(dl);
    KSString ks;                  smoke(ks);
    Mesh2DSource mesh;            smoke(mesh);
    PierceFilterSource pierce;    smoke(pierce);

    CHECK(pin_order(ar) == "source,frequency,amplitude,");
    CHECK(pin_order(bt) == "source,slope,offset,");
    CHECK(pin_order(dl) == "source,frequency,ratio,amplitude,");
    CHECK(pin_order(ks) == "source,frequency,amplitude,damper,bow,");
    CHECK(pin_order(mesh) ==
          "source,inX,inY,outX,outY,decay,edgeFc,edgeR,coefNeg,coefPos,");
    CHECK(pin_order(pierce) == "source,coefNeg,coefPos,");

    CHECK(input_names(ar) == "source," && input_names(bt) == "source,");
    CHECK(input_names(ks) == "source,damper,bow,");
    CHECK(param_names(ks) == "frequency,amplitude,");
    CHECK(param_names(pierce) == "coefNeg,coefPos,");
    CHECK(setting_names(ar) == "feedback,damping,stiffness,tension,exciteGain,direct,");
    CHECK(setting_names(bt) == "minOutput,maxOutput,");
    CHECK(setting_names(dl) == "compensate,");
    CHECK(setting_names(mesh) == "cols,rows,edgeMode,");
    CHECK(setting_names(pierce).empty());
    CHECK(setting_names(ks) ==
          "numCombs,detune,spreadJitter,excStagger,t60,brightness,dispersion,"
          "inharmGain,inharmFb,inharmHp,ap1,ap2,ap3,fbCoeff,releaseFb,damperNoise,"
          "exciteGain,direct,bowSpeed,frictionGain,bowGain,");

    CHECK(!ks.tracks_frequency_live());
    CHECK(dl.tracks_frequency_live() && mesh.tracks_frequency_live());

    // a declared default wins over anything the constructor once set
    CHECK_NEAR(ks.get_setting("detune"), 0.75f, 1e-6f);
    CHECK_NEAR(ks.get_setting("numCombs"), 3.0f, 1e-6f);
    CHECK_NEAR(param_default(ks, "frequency"), 220.0f, 1e-6f);
    CHECK_NEAR(param_default(mesh, "edgeFc"), 1575.0f, 1e-6f);
    CHECK_NEAR(param_default(pierce, "coefPos"), -0.5f, 1e-6f);
    CHECK_NEAR(mesh.get_setting("cols"), 12.0f, 1e-6f);
    CHECK_NEAR(dl.get_setting("compensate"), 0.0f, 1e-6f);
    for (const auto& d : mesh.setting_descriptors())
        if (std::string(d.name) == "edgeMode") {
            CHECK(d.enum_labels != nullptr && std::string(d.enum_labels[2]) == "pierce");
            CHECK(d.enum_labels[3] == nullptr);
        }
}

// The base prepares and advances every wired pin once per note and once per
// sample, so a node reads each pin's current value in compute().
static void run_physical_auto_pin_tests() {
    RenderContext ctx{48000};
    auto count = [&](ValueSource& s, std::initializer_list<const char*> names) {
        std::vector<std::shared_ptr<PinCounter>> probes;
        for (const char* n : names) {
            probes.push_back(std::make_shared<PinCounter>());
            s.set_param(n, probes.back());
        }
        s.prepare(ctx); s.start(4800);
        for (int i = 0; i < 7; ++i) CHECK(std::isfinite(s.next()));
        for (const auto& p : probes) CHECK(p->starts == 1 && p->nexts == 7);
    };
    AllpassResonator ar;
    count(ar, {"source", "frequency", "amplitude"});
    BowTableSource bt;
    count(bt, {"source", "slope", "offset"});
    DelayLineSource dl;
    count(dl, {"source", "frequency", "ratio", "amplitude"});
    KSString ks;
    count(ks, {"source", "frequency", "amplitude", "damper", "bow"});
    Mesh2DSource mesh;
    count(mesh, {"source", "inX", "inY", "outX", "outY", "decay", "edgeFc", "edgeR",
                 "coefNeg", "coefPos"});
    PierceFilterSource pierce;
    count(pierce, {"source", "coefNeg", "coefPos"});

    // an unwired pin reads its declared constant and is not a source
    CHECK(ks.get_param("damper") != nullptr && ks.get_param("bow") != nullptr);
    KSString bare;
    CHECK(bare.get_param("source") == nullptr && bare.get_param("frequency") == nullptr);
    bare.clear_param("nosuch");
    CHECK(bare.get_param("nosuch") == nullptr);
}

// The base advances the pins in declaration order, the inherited `source`
// first.
static void run_physical_advance_order_tests() {
    RenderContext ctx{48000};
    auto order = [&](ValueSource& s, std::initializer_list<const char*> names) {
        std::vector<int> log;
        std::vector<std::shared_ptr<OrderProbe>> probes;
        std::vector<int> expect;
        int id = 0;
        for (const char* n : names) {
            probes.push_back(std::make_shared<OrderProbe>(&log, ++id));
            s.set_param(n, probes.back());
            expect.push_back(id);
        }
        s.prepare(ctx); s.start(4800);
        s.next();
        CHECK(log == expect);
    };
    AllpassResonator ar;
    order(ar, {"source", "frequency", "amplitude"});
    BowTableSource bt;
    order(bt, {"source", "slope", "offset"});
    DelayLineSource dl;
    order(dl, {"source", "frequency", "ratio", "amplitude"});
    KSString ks;
    order(ks, {"source", "frequency", "amplitude", "damper", "bow"});
    Mesh2DSource mesh;
    order(mesh, {"source", "inX", "inY", "outX", "outY", "decay", "edgeFc", "edgeR",
                 "coefNeg", "coefPos"});
    PierceFilterSource pierce;
    order(pierce, {"source", "coefNeg", "coefPos"});
}

// Settings the old set_setting clamped are still clamped; the others are not.
static void run_physical_setting_hook_tests() {
    KSString ks;
    ks.set_setting("numCombs", 40.0f);   CHECK_NEAR(ks.get_setting("numCombs"), 10.0f, 1e-6f);
    ks.set_setting("numCombs", 0.0f);    CHECK_NEAR(ks.get_setting("numCombs"), 1.0f, 1e-6f);
    ks.set_setting("t60", 0.001f);       CHECK_NEAR(ks.get_setting("t60"), 0.05f, 1e-6f);
    ks.set_setting("t60", 600.0f);       CHECK_NEAR(ks.get_setting("t60"), 600.0f, 1e-6f);
    ks.set_setting("ap1", 5.0f);         CHECK_NEAR(ks.get_setting("ap1"), 0.95f, 1e-6f);
    ks.set_setting("ap3", -5.0f);        CHECK_NEAR(ks.get_setting("ap3"), -0.95f, 1e-6f);
    ks.set_setting("brightness", 9.0f);  CHECK_NEAR(ks.get_setting("brightness"), 1.0f, 1e-6f);
    ks.set_setting("releaseFb", -2.0f);  CHECK_NEAR(ks.get_setting("releaseFb"), 0.0f, 1e-6f);
    ks.set_setting("damperNoise", 9.0f); CHECK_NEAR(ks.get_setting("damperNoise"), 2.0f, 1e-6f);
    ks.set_setting("excStagger", 1.0f);  CHECK_NEAR(ks.get_setting("excStagger"), 0.02f, 1e-6f);
    ks.set_setting("detune", 99999.0f);  CHECK_NEAR(ks.get_setting("detune"), 99999.0f, 1e-3f);
    ks.set_setting("inharmGain", -3.0f); CHECK_NEAR(ks.get_setting("inharmGain"), -3.0f, 1e-6f);
    ks.set_setting("nosuch", 1.0f);      CHECK_NEAR(ks.get_setting("nosuch"), 0.0f, 1e-6f);

    AllpassResonator ar;
    ar.set_setting("feedback", 5.0f);    CHECK_NEAR(ar.get_setting("feedback"), 0.9999f, 1e-6f);
    ar.set_setting("damping", -1.0f);    CHECK_NEAR(ar.get_setting("damping"), 0.05f, 1e-6f);
    ar.set_setting("stiffness", 9.0f);   CHECK_NEAR(ar.get_setting("stiffness"), 0.95f, 1e-6f);
    ar.set_setting("tension", -2.0f);    CHECK_NEAR(ar.get_setting("tension"), 0.0f, 1e-6f);
    ar.set_setting("direct", 7.0f);      CHECK_NEAR(ar.get_setting("direct"), 7.0f, 1e-6f);

    DelayLineSource dl;
    dl.set_setting("compensate", 0.4f);  CHECK_NEAR(dl.get_setting("compensate"), 1.0f, 1e-6f);
    dl.set_setting("compensate", 0.0f);  CHECK_NEAR(dl.get_setting("compensate"), 0.0f, 1e-6f);

    BowTableSource bt;
    bt.set_setting("minOutput", 0.2f);   CHECK_NEAR(bt.get_setting("minOutput"), 0.2f, 1e-6f);
    bt.set_setting("maxOutput", 7.0f);   CHECK_NEAR(bt.get_setting("maxOutput"), 7.0f, 1e-6f);
}

// The mesh size and the edge mode are clamped, and each change clears the
// mesh: a ringing mesh with no excitation falls silent at once.
static void run_physical_mesh_hook_tests() {
    Mesh2DSource mesh;
    mesh.set_setting("cols", 500.0f);    CHECK_NEAR(mesh.get_setting("cols"), 64.0f, 1e-6f);
    mesh.set_setting("rows", 1.0f);      CHECK_NEAR(mesh.get_setting("rows"), 2.0f, 1e-6f);
    mesh.set_setting("edgeMode", 9.0f);  CHECK_NEAR(mesh.get_setting("edgeMode"), 2.0f, 1e-6f);
    mesh.set_setting("edgeMode", -3.0f); CHECK_NEAR(mesh.get_setting("edgeMode"), 0.0f, 1e-6f);

    RenderContext ctx{48000};
    auto in = std::make_shared<ConstantSource>(0.0f);
    Mesh2DSource ring;
    ring.set_param("source", in);
    ring.set_setting("cols", 8.0f);
    ring.set_setting("rows", 6.0f);
    ring.prepare(ctx); ring.start(4800);
    in->set(1.0f); ring.next(); in->set(0.0f);
    float energy = 0.0f;
    for (int i = 0; i < 60; ++i) energy += std::fabs(ring.next());
    CHECK(energy > 0.0f);
    ring.set_setting("edgeMode", 1.0f);
    for (int i = 0; i < 20; ++i) CHECK(ring.next() == 0.0f);
}

// The pins are read in compute(): the bow table's curve, KSString's bow mode.
static void run_physical_behavior_tests() {
    RenderContext ctx{48000};
    BowTableSource bt;
    bt.set_param("source", std::make_shared<ConstantSource>(1.0f));
    bt.prepare(ctx); bt.start(4800);
    CHECK_NEAR(bt.next(), 0.01f, 1e-6f);           // rc clamped up to minOutput
    bt.set_param("slope", std::make_shared<ConstantSource>(0.5f));
    CHECK_NEAR(bt.next(), std::pow(1.25f, -4.0f), 1e-5f);
    bt.set_param("offset", std::make_shared<ConstantSource>(1.0f));
    CHECK_NEAR(bt.next(), std::pow(1.75f, -4.0f), 1e-5f);

    // the bow pin switches the bow junction on only while it is wired
    auto strike = [&](bool bowed) {
        auto in = std::make_shared<ConstantSource>(0.0f);
        KSString ks;
        ks.set_param("source", in);
        if (bowed) ks.set_param("bow", std::make_shared<ConstantSource>(0.8f));
        ks.prepare(ctx); ks.start(4800);
        in->set(1.0f); ks.next(); in->set(0.0f);
        float sum = 0.0f;
        for (int i = 0; i < 2000; ++i) sum += ks.next();
        return sum;
    };
    const float plucked = strike(false), bowed = strike(true);
    CHECK(std::isfinite(plucked) && std::isfinite(bowed));
    CHECK(plucked != bowed);
}

// Checkpoint 2.1: the filters (Butterworth sections, delay, hammer bank,
// limiter, reverb, state-variable filter, biquad) and the vibrato modulator.
// Every filter is a Processor: the signal input is the inherited `source`,
// declared first. The vibrato has no signal input and is a Generator.
#include "mforce/source/filter/biquad_source.h"
#include "mforce/source/filter/bw_lowpass_filter.h"
#include "mforce/source/filter/bw_highpass_filter.h"
#include "mforce/source/filter/bw_bandpass_filter.h"
#include "mforce/source/filter/delay_filter.h"
#include "mforce/source/filter/hammer_bank.h"
#include "mforce/source/filter/limiter.h"
#include "mforce/source/filter/reverb.h"
#include "mforce/source/envelope/vibrato.h"

static_assert(std::is_base_of_v<Processor, BiquadSource> &&
              std::is_base_of_v<Processor, BWLowpassFilter> &&
              std::is_base_of_v<Processor, BWHighpassFilter> &&
              std::is_base_of_v<Processor, BWBandpassFilter> &&
              std::is_base_of_v<Processor, DelayFilter> &&
              std::is_base_of_v<Processor, HammerBank> &&
              std::is_base_of_v<Processor, Limiter> &&
              std::is_base_of_v<Processor, Reverb> &&
              std::is_base_of_v<Processor, SVFSource>);
static_assert(std::is_base_of_v<Generator, Vibrato> && !std::is_base_of_v<Processor, Vibrato>);

// Every type prepares and renders finite values, and declares its pins in
// the order they are advanced.
static void run_filter_pin_shape_tests() {
    RenderContext ctx{48000};
    auto smoke = [&](ValueSource& s) {
        s.prepare(ctx); s.start(4800);
        for (int i = 0; i < 10; ++i) CHECK(std::isfinite(s.next()));
    };
    BiquadSource bq;         smoke(bq);
    BWLowpassFilter lp;      smoke(lp);
    BWHighpassFilter hp;     smoke(hp);
    BWBandpassFilter bp;     smoke(bp);
    DelayFilter dly;         smoke(dly);
    HammerBank hammer;       smoke(hammer);
    Limiter lim;             smoke(lim);
    Reverb rev;              smoke(rev);
    SVFSource svf;           smoke(svf);
    Vibrato vib;             smoke(vib);

    CHECK(pin_order(bq) == "source,frequency,radius,");
    CHECK(pin_order(lp) == "source,cutoffFreq,");
    CHECK(pin_order(hp) == "source,cutoffFreq,");
    CHECK(pin_order(bp) == "source,lowCutoff,highCutoff,");
    CHECK(pin_order(dly) == "source,delayTime,delayLevel,feedback,");
    CHECK(pin_order(hammer) == "source,frequency,");
    CHECK(pin_order(lim) == "source,threshold,release,");
    CHECK(pin_order(rev) == "source,roomSize,damping,wet,dry,");
    CHECK(pin_order(svf) == "source,cutoffFreq,resonance,");
    CHECK(pin_order(vib) == "frequency,");

    CHECK(input_names(bq) == "source," && input_names(rev) == "source,");
    CHECK(input_names(vib).empty());
    CHECK(param_names(bp) == "lowCutoff,highCutoff,");
    CHECK(param_names(vib) == "frequency,");
    CHECK(setting_names(bq) == "mode,b0,b1,b2,a1,a2,");
    CHECK(setting_names(svf) == "mode,normalize,");
    CHECK(setting_names(hammer) ==
          "numBands,harm1,harm2,harm3,harm4,resStart,resEnd,resDecay,bandTilt,direct,gain,");
    CHECK(setting_names(vib) ==
          "speed,depth,attack,threshold,speedVar,depthVar,zeroCrossTendency,");
    CHECK(setting_names(lp) == "sections," && setting_names(bp) == "sections," && setting_names(dly).empty());
    CHECK(setting_names(lim).empty() && setting_names(rev).empty());

    CHECK(vib.range() == Generator::Range::None);
}

// The descriptors declare each filter's documented defaults, and the enum
// settings keep their labels and their types.
static void run_filter_default_tests() {
    BiquadSource bq;
    CHECK_NEAR(param_default(bq, "frequency"), 220.0f, 1e-6f);
    CHECK_NEAR(param_default(bq, "radius"), 0.997f, 1e-6f);
    CHECK_NEAR(bq.get_setting("b0"), 1.0f, 1e-6f);
    CHECK_NEAR(bq.get_setting("a1"), 0.0f, 1e-6f);
    BWBandpassFilter bp;
    CHECK_NEAR(param_default(bp, "lowCutoff"), 100.0f, 1e-6f);
    CHECK_NEAR(param_default(bp, "highCutoff"), 5000.0f, 1e-6f);
    DelayFilter dly;
    CHECK_NEAR(param_default(dly, "delayTime"), 0.01f, 1e-6f);
    CHECK_NEAR(param_default(dly, "delayLevel"), 0.5f, 1e-6f);
    CHECK_NEAR(param_default(dly, "feedback"), 0.3f, 1e-6f);
    Limiter lim;
    CHECK_NEAR(param_default(lim, "threshold"), 0.95f, 1e-6f);
    CHECK_NEAR(param_default(lim, "release"), 0.05f, 1e-6f);
    Reverb rev;
    CHECK_NEAR(param_default(rev, "wet"), 0.33f, 1e-6f);
    CHECK_NEAR(param_default(rev, "dry"), 0.4f, 1e-6f);
    HammerBank hammer;
    CHECK_NEAR(hammer.get_setting("numBands"), 4.0f, 1e-6f);
    CHECK_NEAR(hammer.get_setting("resDecay"), 0.010f, 1e-6f);
    CHECK_NEAR(hammer.get_setting("bandTilt"), -0.5f, 1e-6f);
    Vibrato vib;
    CHECK_NEAR(param_default(vib, "frequency"), 440.0f, 1e-6f);
    CHECK_NEAR(vib.get_setting("speed"), 5.0f, 1e-6f);
    CHECK_NEAR(vib.get_setting("zeroCrossTendency"), 1.0f, 1e-6f);
    // set_setting moves the value, not the declared default
    Vibrato custom;
    custom.set_setting("speed", 6.5f);
    custom.set_setting("depthVar", 0.4f);
    custom.set_setting("zeroCrossTendency", 0.0f);
    CHECK_NEAR(custom.get_setting("speed"), 6.5f, 1e-6f);
    CHECK_NEAR(custom.get_setting("depthVar"), 0.4f, 1e-6f);
    CHECK_NEAR(custom.get_setting("zeroCrossTendency"), 0.0f, 1e-6f);
    for (const auto& d : custom.setting_descriptors())
        if (std::string(d.name) == "speed") CHECK_NEAR(d.default_value, 5.0f, 1e-6f);

    for (const auto& d : bq.setting_descriptors())
        if (std::string(d.name) == "mode") {
            CHECK(d.type == SettingType::Int && d.enum_labels != nullptr);
            CHECK(std::string(d.enum_labels[0]) == "Raw");
            CHECK(std::string(d.enum_labels[1]) == "Resonance" && d.enum_labels[2] == nullptr);
        }
    SVFSource svf;
    for (const auto& d : svf.setting_descriptors()) {
        if (std::string(d.name) == "mode") {
            CHECK(d.type == SettingType::Int && d.enum_labels != nullptr);
            CHECK(std::string(d.enum_labels[0]) == "Lowpass");
            CHECK(std::string(d.enum_labels[4]) == "Highpass1P" && d.enum_labels[5] == nullptr);
            CHECK_NEAR(d.max_value, 4.0f, 1e-6f);
        }
        if (std::string(d.name) == "normalize") CHECK(d.type == SettingType::Bool);
    }
}

// The base prepares and advances every wired Auto pin once per note and once
// per sample. The biquad's frequency and radius are Manual: the node prepares
// them every note and pulls them only in resonance mode.
static void run_filter_auto_pin_tests() {
    RenderContext ctx{48000};
    auto wire = [&](ValueSource& s, std::initializer_list<const char*> names) {
        std::vector<std::shared_ptr<PinCounter>> probes;
        for (const char* n : names) {
            probes.push_back(std::make_shared<PinCounter>());
            s.set_param(n, probes.back());
        }
        s.prepare(ctx); s.start(4800);
        for (int i = 0; i < 7; ++i) CHECK(std::isfinite(s.next()));
        return probes;
    };
    auto count = [&](ValueSource& s, std::initializer_list<const char*> names) {
        for (const auto& p : wire(s, names)) CHECK(p->starts == 1 && p->nexts == 7);
    };
    BWLowpassFilter lp;      count(lp, {"source", "cutoffFreq"});
    BWHighpassFilter hp;     count(hp, {"source", "cutoffFreq"});
    BWBandpassFilter bp;     count(bp, {"source", "lowCutoff", "highCutoff"});
    DelayFilter dly;         count(dly, {"source", "delayTime", "delayLevel", "feedback"});
    HammerBank hammer;       count(hammer, {"source", "frequency"});
    Limiter lim;             count(lim, {"source", "threshold", "release"});
    Reverb rev;              count(rev, {"source", "roomSize", "damping", "wet", "dry"});
    SVFSource svf;           count(svf, {"source", "cutoffFreq", "resonance"});
    Vibrato vib;             count(vib, {"frequency"});

    BiquadSource raw;
    auto rp = wire(raw, {"source", "frequency", "radius"});
    CHECK(rp[0]->starts == 1 && rp[0]->nexts == 7);
    CHECK(rp[1]->starts == 1 && rp[1]->nexts == 0);
    CHECK(rp[2]->starts == 1 && rp[2]->nexts == 0);
    BiquadSource res;
    res.set_setting("mode", 1.0f);
    auto sp = wire(res, {"source", "frequency", "radius"});
    for (const auto& p : sp) CHECK(p->starts == 1 && p->nexts == 7);

    // an unwired pin reads its declared constant and is not a source
    CHECK(lp.get_param("source") != nullptr && lp.get_param("nosuch") == nullptr);
    DelayFilter bare;
    CHECK(bare.get_param("source") == nullptr && bare.get_param("delayTime") == nullptr);
    bare.clear_param("nosuch");
    CHECK(bare.get_param("nosuch") == nullptr);
}

// The base advances the pins in declaration order, the inherited `source`
// first; the biquad pulls frequency then radius itself, in resonance mode.
static void run_filter_advance_order_tests() {
    RenderContext ctx{48000};
    auto order = [&](ValueSource& s, std::initializer_list<const char*> names) {
        std::vector<int> log;
        std::vector<std::shared_ptr<OrderProbe>> probes;
        std::vector<int> expect;
        int id = 0;
        for (const char* n : names) {
            probes.push_back(std::make_shared<OrderProbe>(&log, ++id));
            s.set_param(n, probes.back());
            expect.push_back(id);
        }
        s.prepare(ctx); s.start(4800);
        s.next();
        CHECK(log == expect);
    };
    BWLowpassFilter lp;      order(lp, {"source", "cutoffFreq"});
    BWBandpassFilter bp;     order(bp, {"source", "lowCutoff", "highCutoff"});
    DelayFilter dly;         order(dly, {"source", "delayTime", "delayLevel", "feedback"});
    HammerBank hammer;       order(hammer, {"source", "frequency"});
    Limiter lim;             order(lim, {"source", "threshold", "release"});
    Reverb rev;              order(rev, {"source", "roomSize", "damping", "wet", "dry"});
    SVFSource svf;           order(svf, {"source", "cutoffFreq", "resonance"});
    BiquadSource bq;
    bq.set_setting("mode", 1.0f);
    order(bq, {"source", "frequency", "radius"});
}

// The setting hooks clamp the hammer bank's band count and resonance floors
// and the SVF's mode range. The others are not clamped.
static void run_filter_setting_hook_tests() {
    HammerBank hb;
    hb.set_setting("numBands", 9.0f);   CHECK_NEAR(hb.get_setting("numBands"), 4.0f, 1e-6f);
    hb.set_setting("numBands", 0.0f);   CHECK_NEAR(hb.get_setting("numBands"), 1.0f, 1e-6f);
    hb.set_setting("numBands", 2.0f);   CHECK_NEAR(hb.get_setting("numBands"), 2.0f, 1e-6f);
    hb.set_setting("resEnd", 0.1f);     CHECK_NEAR(hb.get_setting("resEnd"), 0.5f, 1e-6f);
    hb.set_setting("resEnd", 99.0f);    CHECK_NEAR(hb.get_setting("resEnd"), 99.0f, 1e-6f);
    hb.set_setting("resDecay", 0.0f);   CHECK_NEAR(hb.get_setting("resDecay"), 1e-4f, 1e-9f);
    hb.set_setting("resDecay", 2.0f);   CHECK_NEAR(hb.get_setting("resDecay"), 2.0f, 1e-6f);
    hb.set_setting("harm3", 40.0f);     CHECK_NEAR(hb.get_setting("harm3"), 40.0f, 1e-6f);
    hb.set_setting("gain", 20.0f);      CHECK_NEAR(hb.get_setting("gain"), 20.0f, 1e-6f);
    hb.set_setting("nosuch", 1.0f);     CHECK_NEAR(hb.get_setting("nosuch"), 0.0f, 1e-6f);

    SVFSource svf;
    svf.set_setting("mode", 9.0f);      CHECK_NEAR(svf.get_setting("mode"), 4.0f, 1e-6f);
    svf.set_setting("mode", -3.0f);     CHECK_NEAR(svf.get_setting("mode"), 0.0f, 1e-6f);
    svf.set_setting("mode", 2.0f);      CHECK_NEAR(svf.get_setting("mode"), 2.0f, 1e-6f);
    svf.set_setting("normalize", 0.4f); CHECK_NEAR(svf.get_setting("normalize"), 1.0f, 1e-6f);
    svf.set_setting("normalize", 0.0f); CHECK_NEAR(svf.get_setting("normalize"), 0.0f, 1e-6f);

    BiquadSource bq;
    bq.set_setting("mode", 1.0f);       CHECK_NEAR(bq.get_setting("mode"), 1.0f, 1e-6f);
    bq.set_setting("b0", 99.0f);        CHECK_NEAR(bq.get_setting("b0"), 99.0f, 1e-6f);
    bq.set_setting("a2", -5.0f);        CHECK_NEAR(bq.get_setting("a2"), -5.0f, 1e-6f);

    // the vibrato rebuilds its LFO at prepare from whatever the settings hold
    Vibrato vib;
    vib.set_setting("threshold", 0.5f); CHECK_NEAR(vib.get_setting("threshold"), 0.5f, 1e-6f);
    vib.set_setting("depth", 0.3f);     CHECK_NEAR(vib.get_setting("depth"), 0.3f, 1e-6f);
}

// The pins are read in compute(): the biquad's two modes, the delay's echo,
// the limiter's ceiling, the reverb's dry path, the hammer bank's direct path.
static void run_filter_behavior_tests() {
    RenderContext ctx{48000};
    auto in = std::make_shared<ConstantSource>(1.0f);

    // raw mode: y = b0 x on the first sample (no history)
    BiquadSource raw;
    raw.set_param("source", in);
    raw.set_setting("b0", 0.5f);
    raw.prepare(ctx); raw.start(4800);
    CHECK_NEAR(raw.next(), 0.5f, 1e-6f);

    // resonance mode derives a1/a2 from the frequency and radius pins, and
    // reads their declared constants when nothing is wired
    BiquadSource res;
    res.set_param("source", in);
    res.set_setting("mode", 1.0f);
    res.set_param("frequency", std::make_shared<ConstantSource>(1000.0f));
    res.set_param("radius", std::make_shared<ConstantSource>(0.9f));
    res.prepare(ctx); res.start(4800);
    res.next();
    CHECK_NEAR(res.get_setting("a2"), 0.81f, 1e-6f);
    CHECK_NEAR(res.get_setting("a1"),
               -2.0f * 0.9f * std::cos(2.0f * 3.14159265f * 1000.0f / 48000.0f), 1e-5f);
    BiquadSource dflt;
    dflt.set_param("source", in);
    dflt.set_setting("mode", 1.0f);
    dflt.prepare(ctx); dflt.start(4800);
    dflt.next();
    CHECK_NEAR(dflt.get_setting("a2"), 0.997f * 0.997f, 1e-6f);

    // delay: an impulse comes back 0.01 s later at level/(1+level)
    auto imp = std::make_shared<ConstantSource>(1.0f);
    DelayFilter dly;
    dly.set_param("source", imp);
    dly.prepare(ctx); dly.start(4800);
    CHECK_NEAR(dly.next(), 1.0f / 1.5f, 1e-5f);
    imp->set(0.0f);
    float echo = 0.0f;
    for (int i = 1; i <= 480; ++i) echo = dly.next();
    CHECK_NEAR(echo, (0.5f / 1.15f) / 1.5f, 1e-3f);

    // limiter: a constant 2.0 settles at the threshold once the lookahead fills
    auto loud = std::make_shared<ConstantSource>(2.0f);
    Limiter lim;
    lim.set_param("source", loud);
    lim.set_param("threshold", std::make_shared<ConstantSource>(0.5f));
    lim.prepare(ctx); lim.start(4800);
    float last = 0.0f;
    for (int i = 0; i < 1000; ++i) last = lim.next();
    CHECK_NEAR(last, 0.5f, 1e-3f);

    // reverb: wet 0, dry 1 passes the input through
    Reverb rev;
    rev.set_param("source", in);
    rev.set_param("wet", std::make_shared<ConstantSource>(0.0f));
    rev.set_param("dry", std::make_shared<ConstantSource>(1.0f));
    rev.prepare(ctx); rev.start(4800);
    CHECK_NEAR(rev.next(), 1.0f, 1e-6f);

    // hammer bank: gain 0 and direct 1 leaves the input
    HammerBank hammer;
    hammer.set_param("source", std::make_shared<ConstantSource>(0.7f));
    hammer.set_setting("gain", 0.0f);
    hammer.set_setting("direct", 1.0f);
    hammer.prepare(ctx); hammer.start(4800);
    CHECK_NEAR(hammer.next(), 0.7f, 1e-6f);
}

// Butterworth sections: a constant passes the lowpass, is blocked by the
// highpass and by a bandpass whose low edge is above DC.
static void run_filter_butterworth_tests() {
    RenderContext ctx{48000};
    auto dc = std::make_shared<ConstantSource>(1.0f);
    BWLowpassFilter lp;
    BWHighpassFilter hp;
    BWBandpassFilter bp;
    lp.set_param("source", dc);
    hp.set_param("source", dc);
    bp.set_param("source", dc);
    lp.prepare(ctx); lp.start(48000);
    hp.prepare(ctx); hp.start(48000);
    bp.prepare(ctx); bp.start(48000);
    float l = 0.0f, h = 1.0f, b = 1.0f;
    for (int i = 0; i < 4800; ++i) { l = lp.next(); h = hp.next(); b = bp.next(); }
    CHECK_NEAR(l, 1.0f, 1e-2f);
    CHECK_NEAR(h, 0.0f, 1e-2f);
    CHECK_NEAR(b, 0.0f, 1e-2f);

    // the cutoff pin moves the corner: a 100 Hz lowpass blocks a 5 kHz tone
    auto tone = std::make_shared<ConstantSource>(0.0f);
    BWLowpassFilter low;
    low.set_param("source", tone);
    low.set_param("cutoffFreq", std::make_shared<ConstantSource>(100.0f));
    low.prepare(ctx); low.start(48000);
    float peak = 0.0f;
    for (int n = 0; n < 4800; ++n) {
        tone->set(std::sin(2.0f * 3.14159265f * 5000.0f * float(n) / 48000.0f));
        const float y = low.next();
        if (n > 2400) peak = std::max(peak, std::fabs(y));
    }
    CHECK(peak < 0.01f);
}

// The vibrato passes the frequency through when the note is shorter than its
// threshold, and wobbles it when the note is long enough.
static void run_vibrato_tests() {
    RenderContext ctx{48000};
    Vibrato off;
    off.set_param("frequency", std::make_shared<ConstantSource>(300.0f));
    off.set_setting("threshold", 1.0f);
    off.prepare(ctx); off.start(4800);                          // 0.1 s: below the threshold
    for (int i = 0; i < 200; ++i) CHECK_NEAR(off.next(), 300.0f, 1e-6f);

    Vibrato on;
    on.set_setting("depth", 0.05f);
    on.set_setting("attack", 0.1f);
    on.set_param("frequency", std::make_shared<ConstantSource>(300.0f));
    on.prepare(ctx); on.start(48000);
    float lo = 1e9f, hi = -1e9f, y = 0.0f;
    bool finite = true;
    for (int i = 0; i < 48000; ++i) {
        y = on.next();
        finite = finite && std::isfinite(y);
        lo = std::min(lo, y); hi = std::max(hi, y);
    }
    CHECK(finite);
    CHECK(hi - lo > 1.0f && lo > 300.0f * 0.9f && hi < 300.0f * 1.1f);
    CHECK(on.current() == y);
}

// The filter family, in registration order.
static void run_filter_tests() {
    run_filter_pin_shape_tests();
    run_filter_default_tests();
    run_filter_auto_pin_tests();
    run_filter_advance_order_tests();
    run_filter_setting_hook_tests();
    run_filter_behavior_tests();
    run_filter_butterworth_tests();
    run_vibrato_tests();
}

// Checkpoint 2.1: the combiners, modulators and the other sources in source/.
// The distorted source, the shaper, the wormhole, the multiplex and the
// repeating source have one signal input and are Processors (the shaper, the
// multiplex and the repeating source pull it themselves:
// Processor(Pull::Manual)); the phased, segment and
// static sources have none and are Generators. The combiners have two signal
// inputs and no `source`, and stay ValueSources with their categories.
#include "mforce/core/source_registry.h"
#include "mforce/source/combiner/multiplex_source.h"
#include "mforce/source/envelope/phased_value_source.h"
#include "mforce/source/modulator/repeating_source.h"
#include "mforce/source/envelope/segment_source.h"

static_assert(std::is_base_of_v<Processor, DistortedSource> &&
              std::is_base_of_v<Processor, ShaperSource> &&
              std::is_base_of_v<Processor, WormholeSource> &&
              std::is_base_of_v<Processor, MultiplexSource> &&
              std::is_base_of_v<Processor, RepeatingSource>);
// A Phased node filled by name: its overlap, a feed per row and the four
// columns.
struct PhasedRow { std::shared_ptr<ValueSource> source; float percent, minSec, maxSec, gain; };
static void fill_phased(PhasedValueSource& p, float overlap, std::initializer_list<PhasedRow> rows) {
    p.set_setting("overlap", overlap);
    std::vector<float> percent, minSec, maxSec, gain;
    for (const auto& r : rows) {
        p.add_param("sources", r.source);
        percent.push_back(r.percent); minSec.push_back(r.minSec);
        maxSec.push_back(r.maxSec);   gain.push_back(r.gain);
    }
    p.set_array("percent", percent); p.set_array("minSec", minSec);
    p.set_array("maxSec", maxSec);   p.set_array("gain", gain);
}

static_assert(std::is_base_of_v<Generator, PhasedValueSource> &&
              std::is_base_of_v<Generator, SegmentSource> &&
              std::is_base_of_v<Generator, StaticVarSource> &&
              std::is_base_of_v<Generator, StaticRangeSource>);
static_assert(!std::is_base_of_v<Processor, CombinedSource> &&
              !std::is_base_of_v<Processor, CrossfadeSource> &&
              !std::is_base_of_v<Generator, CombinedSource> &&
              !std::is_base_of_v<Generator, CrossfadeSource>);

namespace {
const SettingDescriptor* find_setting(const Component& c, const char* name) {
    for (const auto& d : c.setting_descriptors())
        if (std::string(d.name) == name) return &d;
    return nullptr;
}
// Counts prepares and advances and remembers the length it was last prepared for.
struct LengthProbe final : Generator {
    int starts{0}, nexts{0}, lastFrames{0};
    void on_start(int frames) override { ++starts; lastFrames = frames; }
    float compute() override { ++nexts; return 1.0f; }
    const char* type_name() const override { return "LengthProbe"; }
};
std::shared_ptr<ConstantSource> konst(float v) { return std::make_shared<ConstantSource>(v); }
}

// Every type prepares and renders finite values, and declares its pins in the
// order they are advanced.
static void run_combine_mod_pin_shape_tests() {
    RenderContext ctx{48000};
    auto smoke = [&](ValueSource& s) {
        s.prepare(ctx); s.start(4800);
        bool finite = true;
        for (int i = 0; i < 10; ++i) finite = finite && std::isfinite(s.next());
        CHECK(finite);
    };
    CombinedSource comb(konst(1.0f), konst(2.0f), CombineOp::Add);   smoke(comb);
    CrossfadeSource xfade;
    xfade.set_param("source1", konst(1.0f));
    xfade.set_param("source2", konst(2.0f));                     smoke(xfade);
    DistortedSource dist;                                    smoke(dist);
    StaticVarSource sv;
    sv.set_setting("varPct", 0.1f);                              smoke(sv);
    StaticRangeSource sr;                                    smoke(sr);
    MultiplexSource mux;                                     smoke(mux);
    PhasedValueSource phased;
    fill_phased(phased, 0.05f, {{konst(1.0f), 0.5f, 0.0f, 0.0f, 1.0f},
                                {konst(2.0f), 0.0f, 0.0f, 0.0f, 1.0f}});   smoke(phased);
    RepeatingSource rep;
    rep.set_param("source", konst(1.0f));                        smoke(rep);
    SegmentSource seg;
    seg.set_array("values", {2.0f, 1.0f, 2.0f, -1.0f});          smoke(seg);
    ShaperSource shaper;                                     smoke(shaper);
    WormholeSource wh;                                       smoke(wh);

    CHECK(pin_order(comb) == "source1,source2,");
    CHECK(pin_order(xfade) == "amplitude,source1,source2,");
    CHECK(pin_order(dist) == "source,density,gain,shift,amplitude,");
    CHECK(pin_order(sv).empty() && pin_order(sr).empty());
    CHECK(pin_order(mux) == "source," && pin_order(rep) == "source,");
    CHECK(pin_order(phased) == "amplitude,");
    CHECK(pin_order(seg) == "amplitude,smoothness,width,gap,gapVarPct,widthVarPct,valVarPct,");
    CHECK(pin_order(shaper) == "source,drive,smoothness,morph,breakaway,capture,");
    CHECK(pin_order(wh) == "source,");

    CHECK(input_names(comb) == "source1,source2," && input_names(xfade) == "source1,source2,");
    CHECK(input_names(dist) == "source," && input_names(mux) == "source,");
    CHECK(input_names(phased) == "sources," && input_names(seg).empty());
    CHECK(param_names(comb).empty() && param_names(xfade) == "amplitude,");
    CHECK(param_names(dist) == "density,gain,shift,amplitude,");
    CHECK(param_names(shaper) == "drive,smoothness,morph,breakaway,capture,");
    CHECK(param_names(seg) == "amplitude,smoothness,width,gap,gapVarPct,widthVarPct,valVarPct,");
    CHECK(setting_names(comb) == "operation,gainAdj," && setting_names(xfade) == "ratio,overlap,gainAdj,");
    CHECK(setting_names(mux) == "count," && setting_names(shaper) == "hysteresis,");
    CHECK(setting_names(rep) == "duration,durVarPct,gapDuration,gapVarPct,");
    CHECK(setting_names(seg) == "oneShot,timeMode,");
    CHECK(setting_names(dist).empty() && setting_names(wh).empty() && setting_names(phased) == "overlap,");
    CHECK(array_names(phased) == "percent,minSec,maxSec,gain,");
    CHECK(array_names(seg) == "values," && array_names(shaper) == "values,segs,values2,segs2,");
    CHECK(array_names(comb).empty() && array_names(mux).empty());
    // the two Generators among them declare no range: a shape, not a signal
    CHECK(phased.range() == Generator::Range::None && seg.range() == Generator::Range::None);
}

// A declared default is what an unwired pin reads; the enum settings keep
// their labels and their types; constructor arguments set values, not the
// declared defaults.
static void run_combine_mod_default_tests() {
    CrossfadeSource xf;
    CHECK_NEAR(param_default(xf, "amplitude"), 1.0f, 1e-6f);
    CHECK_NEAR(xf.get_setting("ratio"), 0.5f, 1e-6f);
    CHECK_NEAR(xf.get_setting("overlap"), 0.1f, 1e-6f);
    CHECK_NEAR(xf.get_setting("gainAdj"), 0.0f, 1e-6f);
    DistortedSource dist;
    CHECK_NEAR(param_default(dist, "amplitude"), 1.0f, 1e-6f);
    CHECK_NEAR(param_default(dist, "density"), 1.0f, 1e-6f);
    CHECK_NEAR(param_default(dist, "gain"), 1.0f, 1e-6f);
    CHECK_NEAR(param_default(dist, "shift"), 0.0f, 1e-6f);
    ShaperSource sh;
    CHECK_NEAR(param_default(sh, "drive"), 1.0f, 1e-6f);
    CHECK_NEAR(param_default(sh, "smoothness"), 0.5f, 1e-6f);
    CHECK_NEAR(param_default(sh, "morph"), 0.0f, 1e-6f);
    CHECK_NEAR(param_default(sh, "breakaway"), 0.6f, 1e-6f);
    CHECK_NEAR(param_default(sh, "capture"), 0.0f, 1e-6f);
    SegmentSource seg;
    CHECK_NEAR(param_default(seg, "smoothness"), 0.5f, 1e-6f);
    CHECK_NEAR(param_default(seg, "width"), 1.0f, 1e-6f);
    CHECK_NEAR(param_default(seg, "gap"), 0.0f, 1e-6f);
    PhasedValueSource phased;
    CHECK_NEAR(param_default(phased, "amplitude"), 1.0f, 1e-6f);
    CHECK_NEAR(phased.get_setting("overlap"), 0.05f, 1e-6f);
    MultiplexSource mux;
    CHECK_NEAR(mux.get_setting("count"), 10.0f, 1e-6f);
    RepeatingSource rep;
    CHECK_NEAR(rep.get_setting("duration"), 0.5f, 1e-6f);
    CHECK_NEAR(rep.get_setting("gapDuration"), 0.2f, 1e-6f);

    CombinedSource comb(nullptr, nullptr, CombineOp::Multiply, 0.5f);
    CHECK_NEAR(comb.get_setting("operation"), 1.0f, 1e-6f);
    CHECK_NEAR(comb.get_setting("gainAdj"), 0.5f, 1e-6f);
    CHECK_NEAR(find_setting(comb, "operation")->default_value, 0.0f, 1e-6f);
    CHECK_NEAR(find_setting(comb, "gainAdj")->default_value, 0.0f, 1e-6f);
    SegmentSource once;
    once.set_array("values", {4.0f, 1.0f});
    once.set_setting("oneShot", 1.0f);
    CHECK_NEAR(once.get_setting("oneShot"), 1.0f, 1e-6f);
    CHECK_NEAR(find_setting(once, "oneShot")->default_value, 0.0f, 1e-6f);

    const auto* op = find_setting(comb, "operation");
    CHECK(op->type == SettingType::Int && op->enum_labels != nullptr && op->max_value == 3.0f);
    CHECK(std::string(op->enum_labels[0]) == "Mix" && std::string(op->enum_labels[3]) == "Sum");
    CHECK(std::string(op->enum_labels[1]) == "Multiply" && std::string(op->enum_labels[2]) == "Fade");
    CHECK(op->enum_labels[4] == nullptr);
    const auto* tm = find_setting(seg, "timeMode");
    CHECK(tm->type == SettingType::Int && tm->max_value == 2.0f);
    CHECK(std::string(tm->enum_labels[0]) == "auto" && std::string(tm->enum_labels[2]) == "seconds");
    CHECK(tm->enum_labels[3] == nullptr);
    CHECK(find_setting(seg, "oneShot")->type == SettingType::Bool);
    CHECK(find_setting(sh, "hysteresis")->type == SettingType::Bool);
    CHECK(find_setting(mux, "count")->type == SettingType::Int);
    CHECK(find_setting(rep, "duration")->type == SettingType::Float);

    // a fresh shaper is the identity curve with no segment overrides
    CHECK((sh.get_array("values") == std::vector<float>{-1.0f, -1.0f, 1.0f, 1.0f}));
    CHECK(sh.get_array("segs").empty() && sh.get_array("values2").empty());
    CHECK(sh.get_array("segs2").empty() && seg.get_array("values").empty());
}

// The base prepares and advances every wired Auto pin once per note and once
// per sample.
static void run_combine_mod_auto_pin_tests() {
    RenderContext ctx{48000};
    auto count = [&](ValueSource& s, std::initializer_list<const char*> names) {
        std::vector<std::shared_ptr<PinCounter>> probes;
        for (const char* n : names) {
            probes.push_back(std::make_shared<PinCounter>());
            s.set_param(n, probes.back());
        }
        s.prepare(ctx); s.start(4800);
        bool finite = true;
        for (int i = 0; i < 7; ++i) finite = finite && std::isfinite(s.next());
        bool each = true;
        for (const auto& p : probes) each = each && p->starts == 1 && p->nexts == 7;
        CHECK(finite && each);
    };
    CombinedSource comb(nullptr, nullptr, CombineOp::Add);
    count(comb, {"source1", "source2"});
    CrossfadeSource xfade;
    count(xfade, {"amplitude"});
    DistortedSource dist;
    count(dist, {"source", "density", "gain", "shift", "amplitude"});
    PhasedValueSource phased;
    fill_phased(phased, 0.05f, {{konst(1.0f), 0.5f, 0.0f, 0.0f, 1.0f},
                                {konst(2.0f), 0.0f, 0.0f, 0.0f, 1.0f}});
    count(phased, {"amplitude"});
    SegmentSource seg;
    seg.set_array("values", {2.0f, 1.0f});
    count(seg, {"amplitude", "smoothness", "width", "gap", "gapVarPct", "widthVarPct", "valVarPct"});
    WormholeSource wh;
    count(wh, {"source"});
    // breakaway and capture are read only in hysteresis mode, but the base
    // pulls them in both
    const auto shaperPins = {"source", "drive", "smoothness", "morph", "breakaway", "capture"};
    ShaperSource shaper;
    count(shaper, shaperPins);
    ShaperSource hyst;
    hyst.set_setting("hysteresis", 1.0f);
    count(hyst, shaperPins);

    // an unwired pin reads its declared constant and is not a source
    CHECK(dist.get_param("source") != nullptr && dist.get_param("nosuch") == nullptr);
    DistortedSource bare;
    CHECK(bare.get_param("source") == nullptr && bare.get_param("gain") == nullptr);
    bare.clear_param("nosuch");
    CHECK(bare.get_param("nosuch") == nullptr);
}

// The Manual pins are the node's: the crossfade's sources are prepared for
// their own part of the note and pulled only in it; the repeating source (its
// inherited input) only while a repetition plays; the multiplex source
// (likewise) only while there are no clones.
static void run_combine_mod_manual_pin_tests() {
    RenderContext ctx{48000};
    // crossfade: 100 samples at ratio 0.5, overlap 0.1 -> each source is
    // prepared for 55 samples and pulled for 55 (45 alone + the 10 overlap)
    CrossfadeSource xf;
    auto s1 = std::make_shared<LengthProbe>(), s2 = std::make_shared<LengthProbe>();
    auto amp = std::make_shared<PinCounter>();
    xf.set_param("source1", s1);
    xf.set_param("source2", s2);
    xf.set_param("amplitude", amp);
    xf.prepare(ctx); xf.start(100);
    for (int i = 0; i < 100; ++i) xf.next();
    CHECK(amp->starts == 1 && amp->nexts == 100);
    CHECK(s1->starts == 1 && s1->lastFrames == 55 && s1->nexts == 55);
    CHECK(s2->starts == 1 && s2->lastFrames == 55 && s2->nexts == 55);

    // repeating: 10 samples of source, 6 of silence, per cycle (sr 1000);
    // the source is prepared again for each repetition and not pulled in a gap
    RepeatingSource rep;
    auto rsrc = std::make_shared<LengthProbe>();
    rep.set_param("source", rsrc);
    rep.set_setting("duration", 0.01f);
    rep.set_setting("gapDuration", 0.005f);
    rep.prepare(RenderContext{1000}); rep.start(100);
    CHECK(rsrc->starts == 1 && rsrc->lastFrames == 10);
    std::vector<float> out;
    for (int i = 0; i < 32; ++i) out.push_back(rep.next());
    bool pattern = true;
    for (int i = 0; i < 32; ++i) pattern = pattern && (out[i] == ((i % 16) < 10 ? 1.0f : 0.0f));
    CHECK(pattern);
    CHECK(rsrc->nexts == 20 && rsrc->starts == 3 && rsrc->lastFrames == 10);

    // multiplex: the wired source plays solo until the clones exist
    MultiplexSource solo;
    auto wired = std::make_shared<LengthProbe>();
    solo.set_param("source", wired);
    solo.prepare(ctx); solo.start(100);
    for (int i = 0; i < 5; ++i) solo.next();
    CHECK(wired->starts == 1 && wired->nexts == 5);
    CHECK_NEAR(solo.current(), 1.0f, 1e-6f);
    MultiplexSource cloned;
    auto unused = std::make_shared<LengthProbe>();
    cloned.set_param("source", unused);
    cloned.set_setting("count", 3.0f);
    cloned.set_template([](int) {
        return std::shared_ptr<ValueSource>(konst(2.0f));
    });
    cloned.prepare(ctx); cloned.start(100);
    for (int i = 0; i < 5; ++i) cloned.next();
    CHECK(unused->starts == 0 && unused->nexts == 0);
    CHECK_NEAR(cloned.current(), 2.0f, 1e-6f);        // the mean of the three clones
}

// A node shared by the shaper's source pin and its drive chain computes once
// per tick, so the source and the drive chain read the same sample's value of
// it: nothing is a sample late.
static void run_combine_mod_shaper_timing_tests() {
    RenderContext ctx{48000};
    TestHost h;
    auto ramp = std::make_shared<UnitRamp>();
    auto doubled = std::make_shared<Doubler>();
    doubled->set_param("source", ramp);                   // the drive chain reads the ramp too
    ShaperSource sh;
    sh.set_array("values", {-100.0f, -100.0f, 100.0f, 100.0f});   // identity over the test range
    sh.set_param("source", ramp);
    sh.set_param("drive", doubled);
    sh.prepare(h.ctx); sh.start(100);
    h.clock.tick(); CHECK_NEAR(sh.next(), 2.0f, 1e-3f);   // 1 * 2: this sample's ramp on both pins
    h.clock.tick(); CHECK_NEAR(sh.next(), 8.0f, 1e-3f);   // 2 * 4
    h.clock.tick(); CHECK_NEAR(sh.next(), 18.0f, 1e-3f);  // 3 * 6

    // every wired pin is prepared once per note and pulled once per sample
    // (four of the six are wired here; the auto-pin test wires all six)
    ShaperSource full;
    std::vector<std::shared_ptr<PinCounter>> probes;
    for (const char* n : {"source", "drive", "smoothness", "morph"}) {
        probes.push_back(std::make_shared<PinCounter>());
        full.set_param(n, probes.back());
    }
    full.prepare(ctx); full.start(100);
    for (int i = 0; i < 7; ++i) full.next();
    bool each = true;
    for (const auto& p : probes) each = each && p->starts == 1 && p->nexts == 7;
    CHECK(each);
}

// The base advances the pins in declaration order, which is the order the
// descriptors list them in, the shaper's six included; a source wired to both
// inputs of a combiner is advanced once, by the first.
static void run_combine_mod_advance_order_tests() {
    RenderContext ctx{48000};
    auto order = [&](ValueSource& s, std::initializer_list<const char*> names, int skip = 0) {
        std::vector<int> log;
        std::vector<std::shared_ptr<OrderProbe>> probes;
        std::vector<int> expect;
        int id = 0;
        for (const char* n : names) {
            probes.push_back(std::make_shared<OrderProbe>(&log, ++id));
            s.set_param(n, probes.back());
            expect.push_back(id);
        }
        s.prepare(ctx); s.start(100);
        for (int i = 0; i < skip; ++i) s.next();
        log.clear();
        s.next();
        CHECK(log == expect);
    };
    CombinedSource comb(nullptr, nullptr, CombineOp::Add);
    order(comb, {"source1", "source2"});
    DistortedSource dist;
    order(dist, {"source", "density", "gain", "shift", "amplitude"});
    SegmentSource seg;
    seg.set_array("values", {2.0f, 1.0f});
    order(seg, {"amplitude", "smoothness", "width", "gap", "gapVarPct", "widthVarPct", "valVarPct"});
    ShaperSource shaper;
    order(shaper, {"source", "drive", "smoothness", "morph", "breakaway", "capture"});
    CrossfadeSource xfade;           // sample 45 of 100 is in the overlap: both sources play
    order(xfade, {"amplitude", "source1", "source2"}, 45);

    auto ramp = std::make_shared<UnitRamp>();
    CombinedSource sq(ramp, ramp, CombineOp::Multiply);   // the square through sharing is the memo at work
    TestHost h;
    sq.prepare(h.ctx); sq.start(100);
    h.clock.tick(); CHECK_NEAR(sq.next(), 1.0f, 1e-6f);
    h.clock.tick(); CHECK_NEAR(sq.next(), 4.0f, 1e-6f);
    h.clock.tick(); CHECK_NEAR(sq.next(), 9.0f, 1e-6f);
}

// The setting and array hooks: the multiplex count is clamped and rebuilds
// the clones only when it changes; the segment source re-derives its units
// from its time mode and its first width; the shaper refuses a malformed
// curve and keeps its segment arrays normalised.
static void run_combine_mod_setting_hook_tests() {
    RenderContext ctx{48000};
    MultiplexSource mux;
    mux.set_setting("count", 99.0f);  CHECK_NEAR(mux.get_setting("count"), 50.0f, 1e-6f);
    mux.set_setting("count", 0.0f);   CHECK_NEAR(mux.get_setting("count"), 1.0f, 1e-6f);
    mux.set_setting("count", 4.0f);   CHECK_NEAR(mux.get_setting("count"), 4.0f, 1e-6f);
    int builds = 0;
    mux.set_template([&builds](int) {
        ++builds;
        return std::shared_ptr<ValueSource>(konst(1.0f));
    });
    mux.prepare(ctx); mux.start(8);                                   CHECK(builds == 4);
    mux.prepare(ctx); mux.start(8);                                   CHECK(builds == 4);
    mux.set_setting("count", 4.0f);  mux.prepare(ctx); mux.start(8);  CHECK(builds == 4);
    mux.set_setting("count", 6.0f);  mux.prepare(ctx); mux.start(8);  CHECK(builds == 10);
    mux.mark_dirty();                mux.prepare(ctx); mux.start(8);  CHECK(builds == 16);

    // a 4-sample ramp: the first width is inferred to be seconds (< 1) at sr 1000
    auto ramp4 = [&](SegmentSource& s) {
        s.prepare(RenderContext{1000}); s.start(100);
        std::vector<float> out;
        for (int i = 0; i < 6; ++i) out.push_back(s.next());
        return out;
    };
    SegmentSource seg;
    seg.set_array("values", {4.0f, 1.0f});
    seg.set_setting("oneShot", 1.0f);
    seg.set_array("values", {0.004f, 1.0f});
    auto secs = ramp4(seg);
    CHECK_NEAR(secs[0], 0.25f, 1e-5f);
    CHECK_NEAR(secs[3], 1.0f, 1e-5f);
    CHECK(secs[4] == 0.0f && secs[5] == 0.0f);
    seg.set_setting("timeMode", 1.0f);                     // samples: 0.004 rounds to no width
    auto samples = ramp4(seg);
    CHECK(samples[0] == 0.0f && samples[3] == 0.0f);
    seg.set_setting("timeMode", 2.0f);
    CHECK_NEAR(ramp4(seg)[3], 1.0f, 1e-5f);
    seg.set_setting("timeMode", 9.0f);  CHECK_NEAR(seg.get_setting("timeMode"), 2.0f, 1e-6f);
    seg.set_setting("timeMode", -3.0f); CHECK_NEAR(seg.get_setting("timeMode"), 0.0f, 1e-6f);
    seg.set_setting("oneShot", 0.0f);
    CHECK(seg.get_array("values").size() == 2);            // a setting edit keeps the shape

    ShaperSource sh;
    const std::vector<float> identity{-1.0f, -1.0f, 1.0f, 1.0f};
    sh.set_array("values", {0.0f, 0.0f, 1.0f});            // odd length: refused
    CHECK(sh.get_array("values") == identity);
    sh.set_array("values", {0.0f, 0.0f});                  // under two points: refused
    CHECK(sh.get_array("values") == identity);
    sh.set_array("values", {0.0f, 0.0f, 1.0f, 2.0f});
    CHECK(sh.get_array("values").size() == 4 && sh.get_array("values")[3] == 2.0f);
    sh.set_array("values2", {0.0f, 0.0f, 1.0f, 1.0f});
    sh.set_array("values2", {1.0f, 2.0f, 3.0f});           // refused: curve B stays
    CHECK(sh.get_array("values2").size() == 4);
    sh.set_array("values2", {});                           // empty clears curve B
    CHECK(sh.get_array("values2").empty());
    sh.set_array("segs", {0.0f, 0.0f, 9.0f, 5.0f});        // no valid override: normalises to empty
    CHECK(sh.get_array("segs").empty());
    sh.set_array("segs2", {0.0f, 0.0f, 2.0f, 2.0f});
    CHECK(sh.get_array("segs2").size() == 4);
}

// The pins are read in compute(): the combiner's four operations, the
// crossfade's three regions, the distortion's clip and density, the static
// sources' held value, the one-shot segment, the phased first stage.
static void run_combine_mod_behavior_tests() {
    RenderContext ctx{48000};
    CombinedSource c(konst(0.5f), konst(0.25f), CombineOp::Add, 0.2f);
    c.prepare(ctx); c.start(100);
    CHECK_NEAR(c.next(), (0.5f + 0.25f * 1.2f) * 0.5f, 1e-6f);
    c.set_setting("operation", 1.0f);
    CHECK_NEAR(c.next(), 0.5f * 0.25f * 1.2f, 1e-6f);
    c.set_setting("operation", 3.0f);
    CHECK_NEAR(c.next(), 0.5f + 0.25f * 1.2f, 1e-6f);
    c.set_setting("operation", 2.0f);
    c.prepare(ctx); c.start(100);
    CHECK_NEAR(c.next(), 0.5f, 1e-6f);                                     // fade starts at source1
    CHECK_NEAR(c.next(), 0.5f * 0.99f + 0.25f * 1.2f * 0.01f, 1e-6f);

    // crossfade: source1 alone, an overlap where both play, source2 alone
    CrossfadeSource xf;
    xf.set_param("source1", konst(1.0f));
    xf.set_param("source2", konst(2.0f));
    xf.set_param("amplitude", konst(0.5f));
    xf.prepare(ctx); xf.start(100);
    std::vector<float> out;
    for (int i = 0; i < 100; ++i) out.push_back(xf.next());
    CHECK_NEAR(out[0], 0.5f, 1e-6f);
    CHECK_NEAR(out[50], 0.75f, 1e-6f);
    CHECK_NEAR(out[99], 1.0f, 1e-6f);

    // distortion: gain scales and clips; density 0 is silence
    auto in = konst(0.3f);
    DistortedSource d;
    d.set_param("source", in);
    d.set_param("gain", konst(0.0f));
    CHECK_NEAR(d.next(), 0.3f, 1e-6f);
    d.set_param("gain", konst(1.0f));
    CHECK_NEAR(d.next(), 0.6f, 1e-6f);
    in->set(-0.8f);
    CHECK_NEAR(d.next(), -1.0f, 1e-6f);
    d.set_param("density", konst(0.0f));
    bool silent = true;
    for (int i = 0; i < 20; ++i) silent = silent && d.next() == 0.0f;
    CHECK(silent);

    // the static sources hold one value per note, from prepare on
    StaticVarSource sv;
    sv.set_setting("baseValue", 2.0f);
    sv.prepare(ctx); sv.start(10);
    CHECK_NEAR(sv.current(), 2.0f, 1e-6f);
    CHECK_NEAR(sv.next(), 2.0f, 1e-6f);
    StaticRangeSource sr;
    sr.set_setting("min", 0.2f);
    sr.set_setting("max", 0.4f);
    sr.set_setting("bias", 0.3f);
    sr.prepare(ctx); sr.start(10);
    const float held = sr.current();
    CHECK(held >= 0.2f && held <= 0.4f && sr.next() == held && sr.next() == held);

    // a one-shot segment ramps over its width, scaled by amplitude, then stops
    SegmentSource once;
    once.set_array("values", {4.0f, 1.0f});
    once.set_setting("oneShot", 1.0f);
    once.set_param("amplitude", konst(2.0f));
    once.prepare(ctx); once.start(100);
    CHECK_NEAR(once.next(), 0.5f, 1e-5f);
    CHECK_NEAR(once.next(), 1.0f, 1e-5f);
    CHECK_NEAR(once.next(), 1.5f, 1e-5f);
    CHECK_NEAR(once.next(), 2.0f, 1e-5f);
    CHECK(once.next() == 0.0f);

    // the phased source plays its first stage at its gain and amplitude
    PhasedValueSource phased;
    fill_phased(phased, 0.05f, {{konst(1.0f), 0.5f, 0.0f, 0.0f, 1.0f},
                                {konst(2.0f), 0.0f, 0.0f, 0.0f, 1.0f}});
    phased.set_param("amplitude", konst(0.5f));
    phased.prepare(ctx); phased.start(4800);
    CHECK_NEAR(phased.next(), 0.5f, 1e-6f);
}

// The shaper sticks until the input passes the breakaway, slips along the
// second curve, and sticks again on a sign reversal.
static void run_combine_mod_hysteresis_tests() {
    RenderContext ctx{48000};
    ShaperSource sh;
    auto in = konst(0.2f);
    sh.set_param("source", in);
    sh.set_array("values2", {-1.0f, 0.0f, 1.0f, 0.0f});    // the slip curve: flat zero
    sh.set_setting("hysteresis", 1.0f);
    sh.prepare(ctx); sh.start(100);
    CHECK_NEAR(sh.next(), 0.2f, 1e-5f);                    // under the 0.6 breakaway: curve A
    in->set(0.9f);
    CHECK_NEAR(sh.next(), 0.0f, 1e-5f);                    // past it: curve B
    in->set(0.5f);
    CHECK_NEAR(sh.next(), 0.0f, 1e-5f);                    // still slipping
    in->set(-0.5f);
    CHECK_NEAR(sh.next(), -0.5f, 1e-5f);                   // sign reversal: stuck again
}

// The combiner/modulator family, in registration order.
static void run_combine_mod_tests() {
    run_combine_mod_pin_shape_tests();
    run_combine_mod_default_tests();
    run_combine_mod_auto_pin_tests();
    run_combine_mod_manual_pin_tests();
    run_combine_mod_shaper_timing_tests();
    run_combine_mod_advance_order_tests();
    run_combine_mod_setting_hook_tests();
    run_combine_mod_behavior_tests();
    run_combine_mod_hysteresis_tests();
}

// ---------------------------------------------------------------------------
// prepare(ctx) / start(frames): the two host calls
// ---------------------------------------------------------------------------
#include <functional>

// The split is exact when a node that is prepared once and started per note
// renders the same bytes as one that is prepared and started before every
// note (what the old prepare(ctx, frames) did). Both arms throw or neither;
// a comparison of silence proves nothing, so the result says whether the
// render had any signal in it.
struct SplitResult { bool equal; bool threw; bool nonzero; };

static SplitResult split_compare(const std::function<std::shared_ptr<ValueSource>()>& make,
                                 int frames = 2048, int notes = 3, int sampleRate = 48000) {
    RenderContext ctx{sampleRate};
    auto a = make(), b = make();
    std::vector<float> ra, rb;
    auto render = [&](ValueSource& s, std::vector<float>& out, bool prepareEachNote) {
        if (!prepareEachNote) s.prepare(ctx);
        for (int n = 0; n < notes; ++n) {
            if (prepareEachNote) s.prepare(ctx);
            s.start(frames);
            for (int i = 0; i < frames; ++i) out.push_back(s.next());
        }
    };
    bool threwA = false, threwB = false;
    try { render(*a, ra, true);  } catch (...) { threwA = true; }
    try { render(*b, rb, false); } catch (...) { threwB = true; }
    if (threwA || threwB) return {threwA == threwB, true, false};
    const bool equal = ra.size() == rb.size() &&
                       std::memcmp(ra.data(), rb.data(), ra.size() * sizeof(float)) == 0;
    const bool nonzero = std::any_of(rb.begin(), rb.end(), [](float v) { return v != 0.0f; });
    return {equal, false, nonzero};
}

static bool split_equivalent(const std::function<std::shared_ptr<ValueSource>()>& make,
                             int frames = 2048, int notes = 3, int sampleRate = 48000) {
    return split_compare(make, frames, notes, sampleRate).equal;
}

// The registered types that are parts, not signals: each implements its
// interface and nothing of ValueSource. The list grows as the families leave
// ValueSource and is the audit's oracle at the close.
static const std::set<std::string> kPartTypes = {
    "FormantPeaks", "FormantBands", "FormantSequence",
    "FullPartials", "SequencePartials", "ExplicitPartials", "CompositePartials", "ExpandRule",
    "PluckEvolution", "AveragingEvolution", "EKSEvolution", "ReedEvolution", "BowedStringEvolution",
    "BrassEvolution", "ReactionDiffusionEvolution", "SortErosionEvolution", "CellularAutomatonEvolution",
    "HistogramEqualizeEvolution", "BezierPullEvolution", "BitRotateEvolution", "TargetEvolution",
    "SoundChannel", "StereoMixer",
};

static void run_split_equivalence_tests() {
    register_all_sources();
    auto& reg = SourceRegistry::instance();
    std::set<std::string> parts;   // registered types that are not signals: the split has nothing to compare
    for (const auto& entry : reg.registered_types()) {
        const std::string n = entry.name;
        if (!std::dynamic_pointer_cast<ValueSource>(reg.create(n))) { parts.insert(n); continue; }
        const SplitResult r = split_compare([&] { return std::dynamic_pointer_cast<ValueSource>(reg.create(n)); });
        if (!r.equal) std::printf("  split differs: %s\n", n.c_str());
        CHECK(r.equal);
        if (r.threw) std::printf("  (both arms threw at the defaults: %s)\n", n.c_str());
        // A sound-making type at its defaults makes sound; the comparison above
        // was of a real signal for these. Processors and parts read 0 unwired;
        // the physical models need an excitation and the envelopes a note, and
        // those have wired cases in their family's tests.
        const Family f = entry.tag.family;
        if (f == Family::Oscillator || f == Family::Noise || f == Family::Wavetable) {
            if (!r.threw && !r.nonzero) std::printf("  silent at its defaults: %s\n", n.c_str());
            CHECK(r.threw || r.nonzero);
        }
    }
    CHECK(parts == kPartTypes);   // exactly the parts are not signals
    if (parts != kPartTypes) for (const auto& n : parts) std::printf("  not a signal: %s\n", n.c_str());
    // A node knows no rate until a host prepares it.
    struct Probe final : Generator {
        float compute() override { return 0.0f; }
        bool p() const { return prepared(); }
        int  r() const { return sample_rate(); }
    };
    Probe p;
    CHECK(!p.p());
    CHECK(p.r() == 0);
    p.prepare(RenderContext{22050});
    CHECK(p.p());
    CHECK(p.r() == 22050);
    // The context the host gave an object survives assignment, like the
    // registry: a node assigning itself assigns its own state only.
    Probe q;
    q = p;
    CHECK(q.r() == 0);
    p = q;
    CHECK(p.r() == 22050);
}

// ---------------------------------------------------------------------------
// Core family: the rate arrives through prepare(ctx), the note through
// start(frames) (WaveSource, Envelope and presets, FM, Multi).
// ---------------------------------------------------------------------------
static std::vector<float> render_samples(ValueSource& s, int n) {
    std::vector<float> out;
    out.reserve(size_t(n));
    for (int i = 0; i < n; ++i) out.push_back(s.next());
    return out;
}

static bool same_bits(const std::vector<float>& a, const std::vector<float>& b) {
    return a.size() == b.size() &&
           std::memcmp(a.data(), b.data(), a.size() * sizeof(float)) == 0;
}

static void run_core_split_envelope_tests() {
    // An envelope keeps the context it was prepared with through a new
    // layout: replace_stages assigns a fresh, unprepared envelope over it.
    {
        auto e = Envelope::make_ar(0.2f);
        e->prepare(RenderContext{22050});
        e->replace_stages(std::move(*Envelope::make_ar(0.5f)));
        e->start(22050);
        auto f = Envelope::make_ar(0.5f);
        f->prepare(RenderContext{22050});
        f->start(22050);
        const auto ra = render_samples(*e, 22050), rb = render_samples(*f, 22050);
        CHECK(same_bits(ra, rb));
        CHECK(ra[5000] > 0.0f);                      // rendered, not the silent unprepared path
    }
    // prepare(ctx) twice with the same context changes nothing a render sees.
    {
        auto a = Envelope::make_adsr(0.2f, 0.1f, 0.7f, 0.0f);
        auto b = Envelope::make_adsr(0.2f, 0.1f, 0.7f, 0.0f);
        a->prepare(RenderContext{48000});
        a->prepare(RenderContext{48000});
        a->start(4800);
        b->prepare(RenderContext{48000});
        b->start(4800);
        CHECK(same_bits(render_samples(*a, 4800), render_samples(*b, 4800)));
    }
    // A stage added after prepare is counted by the next start with no
    // second prepare; a removed one is dropped from the counts.
    {
        auto grown = std::make_shared<Envelope>();
        grown->add_stage({{0.0f, 1.0f, RampType::Linear, 0.0f}, 0.5f, 0.0f, 0.0f});
        grown->prepare(RenderContext{48000});
        grown->add_stage({{1.0f, 0.0f, RampType::Linear, 0.0f}, 0.0f, 0.0f, 0.0f});
        grown->start(2400);
        auto fresh = std::make_shared<Envelope>();
        fresh->add_stage({{0.0f, 1.0f, RampType::Linear, 0.0f}, 0.5f, 0.0f, 0.0f});
        fresh->add_stage({{1.0f, 0.0f, RampType::Linear, 0.0f}, 0.0f, 0.0f, 0.0f});
        fresh->prepare(RenderContext{48000});
        fresh->start(2400);
        CHECK(same_bits(render_samples(*grown, 2400), render_samples(*fresh, 2400)));
        grown->remove_stage(1);
        grown->start(2400);
        auto one = std::make_shared<Envelope>();
        one->add_stage({{0.0f, 1.0f, RampType::Linear, 0.0f}, 0.5f, 0.0f, 0.0f});
        one->prepare(RenderContext{48000});
        one->start(2400);
        CHECK(same_bits(render_samples(*grown, 2400), render_samples(*one, 2400)));
    }
    // prepare(ctx) again on a node that has already played a note leaves its
    // next note as it would have been: nothing per-note lives in on_prepare.
    {
        auto a = Envelope::make_adsr(0.2f, 0.1f, 0.7f, 0.0f);
        auto b = Envelope::make_adsr(0.2f, 0.1f, 0.7f, 0.0f);
        a->prepare(RenderContext{48000});
        b->prepare(RenderContext{48000});
        a->start(4800); render_samples(*a, 1000);
        b->start(4800); render_samples(*b, 1000);
        a->prepare(RenderContext{48000});
        a->start(4800);
        b->start(4800);
        CHECK(same_bits(render_samples(*a, 4800), render_samples(*b, 4800)));
    }
}

static void run_core_split_tests() {
    run_core_split_envelope_tests();
    // FM with oversampling: the decimation sections are built at prepare and
    // reset per note; the sweep covers the default (oversample 1).
    CHECK(split_equivalent([] {
        auto f = std::make_shared<FMSource>();
        f->set_setting("oversample", 2.0f);
        return f;
    }));
    // The sections are fresh at every note, as the per-note rebuild left
    // them: a reused node's second note is a fresh node's first note.
    {
        FMSource reused, fresh;
        reused.set_setting("oversample", 2.0f);
        fresh.set_setting("oversample", 2.0f);
        reused.prepare(RenderContext{48000});
        fresh.prepare(RenderContext{48000});
        reused.start(1024); render_samples(reused, 777);
        reused.start(1024);
        fresh.start(1024);
        CHECK(same_bits(render_samples(reused, 1024), render_samples(fresh, 1024)));
    }
    // A setting changed after prepare rebuilds the sections; the node keeps
    // its context.
    {
        FMSource a;
        a.prepare(RenderContext{48000});
        a.set_setting("oversample", 4.0f);
        a.start(1024);
        FMSource b;
        b.set_setting("oversample", 4.0f);
        b.prepare(RenderContext{48000});
        b.start(1024);
        CHECK(same_bits(render_samples(a, 1024), render_samples(b, 1024)));
    }
    // MultiSource: an oscillator and an envelope, one delayed.
    CHECK(split_equivalent([] {
        auto m = std::make_shared<MultiSource>();
        m->add_param("source", std::make_shared<SineSource>());
        m->add_param("source", Envelope::make_ar(0.3f));
        m->set_array("weights", {1.0f, 0.5f});
        m->set_array("delays", {0.0f, 100.0f});
        return m;
    }));
    // The oscillators read the rate from the context: the same frequency at
    // half the rate advances the phase twice as far per sample.
    {
        SineSource lo, hi;
        lo.prepare(RenderContext{24000});
        hi.prepare(RenderContext{48000});
        lo.start(8);
        hi.start(8);
        const auto a = render_samples(lo, 8), b = render_samples(hi, 8);
        CHECK(!same_bits(a, b));
        CHECK_NEAR(a[1], std::sin(2.0f * 3.14159265358979f * 440.0f / 24000.0f), 1e-4f);
        CHECK_NEAR(b[1], std::sin(2.0f * 3.14159265358979f * 440.0f / 48000.0f), 1e-4f);
    }
}

// ---------------------------------------------------------------------------
// Noise family: constructors lose the rate; RedNoise::reset() is the
// constructed state (Vibrato's per-note fresh chain).
// ---------------------------------------------------------------------------
static void run_noise_split_tests() {
    // reset() returns a played RedNoise to the state of a new one, draw
    // stream included. Every ramp carries a value and the continuity and
    // sign rules are live, so each field reset() touches reaches the output;
    // two rounds at different play lengths so the state exposed differs.
    {
        auto make_red = [] {
            auto r = std::make_shared<RedNoiseSource>(0x51EEDu);
            r->density.set(std::make_shared<ConstantSource>(1.0f));
            r->zeroCrossTendency.set(std::make_shared<ConstantSource>(0.5f));
            r->continuity.set(std::make_shared<ConstantSource>(0.5f));
            r->prepare(RenderContext{48000});
            return r;
        };
        auto used = make_red();
        for (int played : {50, 173}) {
            used->start(100);
            render_samples(*used, played);
            used->reset();
            used->start(100);
            auto fresh = make_red();
            fresh->start(100);
            const auto a = render_samples(*used, 400), b = render_samples(*fresh, 400);
            CHECK(same_bits(a, b));
            int nonzero = 0;
            for (float v : a) nonzero += (v != 0.0f);
            CHECK(nonzero > 300);                 // the comparison is of a real signal
        }
    }
    // The walks leave `amplitude` Manual: the node prepares and starts it.
    CHECK(split_equivalent([] {
        auto w = std::make_shared<WanderNoiseSource>();
        w->amplitude.set(std::make_shared<ConstantSource>(0.5f));
        return w;
    }));
    CHECK(split_equivalent([] {
        auto w = std::make_shared<WanderNoise2Source>();
        w->amplitude.set(std::make_shared<ConstantSource>(0.5f));
        return w;
    }));
}

// ---------------------------------------------------------------------------
// Wavetable family: the seed noise, the evolution and the speed pin are
// Manual, so the node prepares and starts them; HybridKS sizes its target
// array in prepare and again when numPartials changes afterwards.
// ---------------------------------------------------------------------------
static void run_wavetable_split_tests() {
    // A patch's shape: seed noise in inputSource, an evolution in the
    // evolution slot; then the interpolating read; then evolutions with Manual pins.
    for (bool interp : {false, true}) {
        CHECK(split_equivalent([interp] {
            auto w = std::make_shared<WavetableSource>();
            w->set_param("inputSource", std::make_shared<WhiteNoiseSource>(0x1234u));
            w->set_param("evolution", std::make_shared<PluckEvolution>());
            w->set_setting("interpolate", interp ? 1.0f : 0.0f);
            return w;
        }));
    }
    CHECK(split_equivalent([] {
        auto w = std::make_shared<WavetableSource>();
        auto reed = std::make_shared<ReedEvolution>();
        reed->set_param("breath", std::make_shared<ConstantSource>(0.6f));
        w->set_param("evolution", reed);
        w->set_param("speedFactor", std::make_shared<ConstantSource>(1.5f));
        return w;
    }));
    CHECK(split_equivalent([] {
        auto w = std::make_shared<WavetableSource>();
        auto bit = std::make_shared<BitRotateEvolution>();
        bit->set_param("shiftBits", std::make_shared<ConstantSource>(2.0f));
        w->set_param("evolution", bit);
        return w;
    }));
    // An evolution that sizes its lines from the rate learns it through the
    // node's prepare: the same patch at half the rate is a different render.
    {
        auto bowed_at = [](int rate) {
            WavetableSource w;
            auto bow = std::make_shared<BowedStringEvolution>();
            bow->set_param("bow", std::make_shared<ConstantSource>(0.5f));
            w.set_param("evolution", bow);
            w.set_param("inputSource", std::make_shared<WhiteNoiseSource>(0x77u));
            w.prepare(RenderContext{rate});
            w.start(2048);
            return render_samples(w, 2048);
        };
        const auto lo = bowed_at(24000), hi = bowed_at(48000);
        CHECK(!same_bits(lo, hi));
        CHECK(std::all_of(hi.begin(), hi.end(), [](float v) { return std::isfinite(v); }));
    }
}

static void run_hybrid_ks_split_tests() {
    // HybridKS: numPartials changed after prepare must reach the target array
    // (12 shrinks the loop; 40 outgrows the 20 targets set below).
    for (float partials : {12.0f, 40.0f}) {
        auto make = [] {
            auto h = std::make_shared<HybridKSSource>();
            h->set_setting("holdCycles", 2.0f);
            h->set_setting("morphDuration", 0.02f);
            h->set_array("targetPartials", std::vector<float>(20, 0.05f));
            return h;
        };
        const RenderContext ctx{48000};
        auto late = make();
        late->prepare(ctx);
        late->set_setting("numPartials", partials);
        CHECK(late->target_count() >= int(partials));   // the setting hook grew the array
        auto early = make();
        early->set_setting("numPartials", partials);
        early->prepare(ctx);
        std::vector<float> ra, rb;
        for (int note = 0; note < 2; ++note) {
            late->start(4800);
            early->start(4800);
            const auto a = render_samples(*late, 4800), b = render_samples(*early, 4800);
            ra.insert(ra.end(), a.begin(), a.end());
            rb.insert(rb.end(), b.begin(), b.end());
        }
        CHECK(same_bits(ra, rb));
        CHECK(std::all_of(ra.begin(), ra.end(), [](float v) { return std::isfinite(v); }));
    }
    // And the plain split, with the default numPartials.
    CHECK(split_equivalent([] {
        auto h = std::make_shared<HybridKSSource>();
        h->set_setting("holdCycles", 2.0f);
        return h;
    }));
}

// ---------------------------------------------------------------------------
// Additive family: the rate reaches the parts through their prepare; the note
// counter, the per-note streams and the envelope layouts run in their start.
// ---------------------------------------------------------------------------
static std::shared_ptr<ExplicitPartials> make_split_partials() {
    auto p = std::make_shared<ExplicitPartials>();
    p->set_array("mult2", {1.0f, 2.1f, 3.2f});
    p->set_array("ampl2", {0.5f, 0.4f, 0.3f});
    p->set_array("ampl1", {1.0f, 0.5f, 0.25f});
    p->set_array("mult1", {1.0f, 2.0f, 3.0f});
    p->set_setting("bandwidth1", 0.4f);
    p->set_setting("bandwidthHz", 2.0f);        // the per-note noise streams
    p->set_setting("motionDepth1", 6.0f);       // the walks are sized from the rate
    p->set_param("multEnv", Envelope::make_ar(0.3f));
    return p;
}

static std::shared_ptr<AdditiveSource> make_split_full(std::shared_ptr<Component> formant) {
    auto f = std::make_shared<AdditiveSource>();
    f->set_param("partials", make_split_partials());
    if (formant) {
        f->set_param("formant", std::move(formant));
        f->set_param("formantWeight", std::make_shared<ConstantSource>(0.7f));
    }
    f->set_setting("noiseBedLevel", 0.3f);      // the bed's coefficients come from the rate
    f->set_setting("noiseBedDelay", 0.01f);
    return f;
}

static void run_additive_split_tests() {
    // The note counter advances at start, never at prepare: a node prepared
    // before every note renders what one prepared once does, across four notes.
    CHECK(split_equivalent([] { return make_split_full(nullptr); }, 2048, 4));
    CHECK(split_equivalent([] { return make_split_full(make_one_peak(800.0f, 900.0f)); }, 2048, 4));
    CHECK(split_equivalent([] {
        auto spec = std::make_shared<FormantPeaks>();
        spec->set_array("frequency", {500.0f, 1500.0f});
        spec->set_array("gain", {1.0f, 1.0f});
        spec->set_array("width", {900.0f, 900.0f});
        spec->set_array("power", {2.0f, 2.0f});
        return make_split_full(spec);
    }, 2048, 4));
    CHECK(split_equivalent([] {
        auto seq = std::make_shared<FormantSequence>();
        seq->set_param("blend", Envelope::make_ar(0.5f));
        for (float hz : {400.0f, 1200.0f})
            seq->add_param("spectra", make_one_peak(hz, 900.0f));
        return make_split_full(seq);
    }, 2048, 4));
    CHECK(split_equivalent([] {
        auto comp = std::make_shared<CompositePartials>();
        comp->add_param("partials", make_split_partials());
        comp->add_param("partials", std::make_shared<FullPartials>(0x51u));
        auto f = std::make_shared<AdditiveSource>();
        f->set_param("partials", comp);
        return f;
    }, 2048, 4));

    // The rate reaches the partials and the bed only through prepare(ctx).
    auto at = [](int rate) {
        auto f = make_split_full(make_one_peak(800.0f, 900.0f));
        f->prepare(RenderContext{rate});
        f->start(2048);
        return render_samples(*f, 2048);
    };
    const auto lo = at(24000), hi = at(48000);
    CHECK(!same_bits(lo, hi));
    CHECK(std::all_of(hi.begin(), hi.end(), [](float v) { return std::isfinite(v); }));
    CHECK(std::any_of(hi.begin(), hi.end(), [](float v) { return v != 0.0f; }));
}

// ---------------------------------------------------------------------------
// Physical models: the rate arrives through prepare(ctx), the note through
// start(frames). A constant on a pin cannot show a missing call, so the
// shaper's pins here are PinCounters that record the rate and the starts they
// were given.
// ---------------------------------------------------------------------------
static void run_shaper_pin_split_tests() {
    ShaperSource sh;
    const char* names[] = {"source", "drive", "smoothness", "morph", "breakaway", "capture"};
    std::vector<std::shared_ptr<PinCounter>> pins;
    for (const char* n : names) {
        pins.push_back(std::make_shared<PinCounter>());
        sh.set_param(n, pins.back());
    }
    sh.prepare(RenderContext{48000});
    for (const auto& p : pins) CHECK(p->rate == 48000 && p->starts == 0);   // prepared, not started
    sh.start(64);
    for (const auto& p : pins) CHECK(p->rate == 48000 && p->starts == 1);   // started once
    // A second prepare is idempotent and starts nothing; each start reaches each pin.
    sh.prepare(RenderContext{48000});
    sh.start(64);
    for (const auto& p : pins) CHECK(p->rate == 48000 && p->starts == 2);
    // The shaper itself, with the drive and the input wired to real sources.
    CHECK(split_equivalent([] {
        auto s = std::make_shared<ShaperSource>();
        s->set_param("source", std::make_shared<SineSource>());
        s->set_param("drive", std::make_shared<ConstantSource>(2.0f));
        return s;
    }));
}

// A read position a hair under the write index rounds up to the ring length
// in float: the read wraps to slot 0, the ring's first write, on every lap.
static void run_delay_line_wrap_tests() {
    DelayLineSource dl;
    dl.set_param("source", std::make_shared<Counter>());   // 1, 2, 3, ...: consecutive samples differ, so a wrong fraction shows
    dl.set_param("frequency", std::make_shared<ConstantSource>(std::nextafter(1470.0f, 0.0f)));
    dl.prepare(RenderContext{22050});
    dl.start(64);
    const int ring = dl.capacity();
    const auto out = render_samples(dl, 32);
    CHECK_NEAR(out[15], 1.0f, 1e-4f);    // write index 15, delay 15.000001: slot 0, the first write (1)
    CHECK(out[14] == 0.0f);              // still the empty ring
    CHECK_NEAR(out[16], 2.0f, 1e-3f);    // one past the wrap: between slot 0 and slot 1, the second write (2)
    CHECK(std::all_of(out.begin(), out.end(), [](float v) { return std::isfinite(v); }));
    // Past one full ring the write index has wrapped; the read at the same
    // phase wraps to slot 0 again, which now holds the second lap's first
    // write (sample `ring`: the Counter at ring + 1).
    const auto more = render_samples(dl, ring);      // samples 32 .. 32 + ring
    CHECK_NEAR(more[size_t(ring + 15 - 32)], float(ring + 1), 1e-2f);
    CHECK_NEAR(more[size_t(ring + 16 - 32)], float(ring + 2), 1e-2f);
}

static void run_physical_split_tests() {
    const auto noise = [] { return std::make_shared<WhiteNoiseSource>(0x5EEDu); };
    CHECK(split_equivalent([&] {
        auto k = std::make_shared<KSString>();
        k->set_param("source", noise());
        return k;
    }));
    {   // the registry's string is the same type; excited, it sounds
        const SplitResult r = split_compare([&] {
            auto k = std::dynamic_pointer_cast<ValueSource>(SourceRegistry::instance().create("KSString"));
            k->set_param("source", noise());
            return k;
        });
        CHECK(r.equal && r.nonzero);
    }
    CHECK(split_equivalent([&] {
        auto a = std::make_shared<AllpassResonator>();
        a->set_param("source", noise());
        return a;
    }));
    CHECK(split_equivalent([] {
        auto d = std::make_shared<DelayLineSource>();
        d->set_param("source", std::make_shared<SineSource>());
        return d;
    }));
    CHECK(split_equivalent([] {
        auto m = std::make_shared<Mesh2DSource>();
        m->set_param("source", std::make_shared<SineSource>());
        m->set_setting("edgeMode", 1.0f);
        return m;
    }));
    for (const char* type : {"PierceFilter", "BowTable", "Wormhole"}) {
        CHECK(split_equivalent([&] {
            auto s = std::dynamic_pointer_cast<ValueSource>(SourceRegistry::instance().create(type));
            s->set_param("source", std::make_shared<SineSource>());
            return s;
        }));
    }
    run_shaper_pin_split_tests();
    run_delay_line_wrap_tests();

    // The delay line follows the rate its host prepared it at: the same
    // rate again changes nothing, a new rate re-sizes it.
    DelayLineSource dl;
    dl.prepare(RenderContext{22050});
    CHECK(dl.capacity() == 22050 / 20 + 4);
    dl.prepare(RenderContext{22050});
    CHECK(dl.capacity() == 22050 / 20 + 4);
    dl.prepare(RenderContext{48000});
    CHECK(dl.capacity() == 48000 / 20 + 4);

    // The strings' comb lengths come from the rate in init_note; Mesh2D's
    // boundary filter from the rate it was prepared with.
    auto at = [&](int rate, auto make) {
        auto s = make();
        s->prepare(RenderContext{rate});
        s->start(2048);
        return render_samples(*s, 2048);
    };
    const auto ks = [&] {
        auto k = std::make_shared<KSString>();
        k->set_param("source", noise());
        return k;
    };
    const auto lo = at(24000, ks), hi = at(48000, ks);
    CHECK(!same_bits(lo, hi));
    CHECK(std::all_of(hi.begin(), hi.end(), [](float v) { return std::isfinite(v); }));
    CHECK(std::any_of(hi.begin(), hi.end(), [](float v) { return v != 0.0f; }));
    const auto mesh = [] {
        auto m = std::make_shared<Mesh2DSource>();
        m->set_param("source", std::make_shared<SineSource>());
        m->set_setting("edgeMode", 1.0f);
        return m;
    };
    CHECK(!same_bits(at(24000, mesh), at(48000, mesh)));
}

// ---------------------------------------------------------------------------
// Filters and Vibrato: the rate arrives through prepare(ctx), the note through
// start(frames). Every filter is built here by name with `in` on its input.
// A rate-dependence check feeds DC, because a Sine would differ between two
// rates by itself and hide a coefficient that ignored the rate.
// ---------------------------------------------------------------------------
static const char* const kFilterNames[] = {
    "BWLowpass", "BWHighpass", "BWBandpass", "SVF", "SVF1P", "Delay", "Limiter",
    "Reverb", "Hammer", "BiquadRes", "BiquadRaw"};

static std::shared_ptr<ValueSource> make_filter(const std::string& name,
                                                std::shared_ptr<ValueSource> in) {
    std::shared_ptr<ValueSource> n;
    if (name == "BWLowpass")       n = std::make_shared<BWLowpassFilter>();
    else if (name == "BWHighpass") n = std::make_shared<BWHighpassFilter>();
    else if (name == "BWBandpass") n = std::make_shared<BWBandpassFilter>();
    else if (name == "Delay")      n = std::make_shared<DelayFilter>();
    else if (name == "Limiter")    n = std::make_shared<Limiter>();
    else if (name == "Reverb")     n = std::make_shared<Reverb>();
    else if (name == "Hammer")     n = std::make_shared<HammerBank>();
    else if (name == "SVF" || name == "SVF1P") {
        n = std::make_shared<SVFSource>();
        if (name == "SVF1P") n->set_setting("mode", float(SVFSource::kLowpass1P));
    } else {
        n = std::make_shared<BiquadSource>();
        if (name == "BiquadRes") {
            n->set_setting("mode", float(BiquadSource::kResonance));
            n->set_param("frequency", std::make_shared<ConstantSource>(1000.0f));
            n->set_param("radius", std::make_shared<ConstantSource>(0.9f));
        } else {
            n->set_setting("b0", 0.5f);
            n->set_setting("b1", 0.3f);
            n->set_setting("a1", -0.2f);
        }
    }
    n->set_param("source", std::move(in));
    return n;
}

// Rendered at `rate` with DC in: whatever differs between two rates came from
// the node's own use of the rate.
static std::vector<float> filter_at(const std::string& name, int rate) {
    auto f = make_filter(name, std::make_shared<ConstantSource>(1.0f));
    f->prepare(RenderContext{rate});
    f->start(4096);
    return render_samples(*f, 4096);
}

// A node used at one rate and then prepared at another renders what a node
// prepared at the second rate renders: nothing sized or derived from the
// first rate survives.
static bool follows_rate(const std::function<std::shared_ptr<ValueSource>()>& make,
                         int first, int second) {
    auto used = make(), fresh = make();
    used->prepare(RenderContext{first});
    used->start(4096);
    render_samples(*used, 500);
    used->prepare(RenderContext{second});
    used->start(4096);
    fresh->prepare(RenderContext{second});
    fresh->start(4096);
    return same_bits(render_samples(*used, 4096), render_samples(*fresh, 4096));
}

static void run_filter_rate_tests() {
    for (const std::string name : kFilterNames) {
        if (name != "BiquadRaw") {   // a raw biquad's coefficients are settings, not the rate
            const auto lo = filter_at(name, 24000), hi = filter_at(name, 48000);
            if (same_bits(lo, hi)) std::printf("  rate ignored: %s\n", name.c_str());
            CHECK(!same_bits(lo, hi));
            CHECK(std::all_of(hi.begin(), hi.end(), [](float v) { return std::isfinite(v); }));
            CHECK(std::any_of(hi.begin(), hi.end(), [](float v) { return v != 0.0f; }));
        }
        const auto make = [&] { return make_filter(name, std::make_shared<ConstantSource>(1.0f)); };
        const bool down = follows_rate(make, 48000, 22050), up = follows_rate(make, 22050, 48000);
        if (!down || !up) std::printf("  rate not followed: %s\n", name.c_str());
        CHECK(down);
        CHECK(up);
    }
}

// The pins the node prepares and starts itself, the buffers the rate sizes,
// and the filters whose history is not a per-note state.
static void run_filter_pin_split_tests() {
    BiquadSource bq;
    bq.set_setting("mode", float(BiquadSource::kResonance));
    auto freq = std::make_shared<PinCounter>(), rad = std::make_shared<PinCounter>();
    bq.set_param("frequency", freq);
    bq.set_param("radius", rad);
    bq.prepare(RenderContext{48000});
    CHECK(freq->rate == 48000 && rad->rate == 48000);
    CHECK(freq->starts == 0 && rad->starts == 0);        // prepared, not started
    bq.start(64);
    CHECK(freq->starts == 1 && rad->starts == 1);        // started once
    bq.prepare(RenderContext{48000});
    bq.start(64);
    CHECK(freq->starts == 2 && rad->starts == 2);

    DelayFilter dly;
    dly.prepare(RenderContext{22050});
    CHECK(dly.capacity() == 22050);
    dly.prepare(RenderContext{22050});
    CHECK(dly.capacity() == 22050);
    dly.prepare(RenderContext{48000});
    CHECK(dly.capacity() == 48000);
    Limiter lim;
    lim.prepare(RenderContext{22050});
    CHECK(lim.capacity() == 110);
    lim.prepare(RenderContext{22050});
    CHECK(lim.capacity() == 110);
    lim.prepare(RenderContext{48000});
    CHECK(lim.capacity() == 240);

    // The Butterworth histories carry across notes (no per-note reset): after
    // a settled DC note, the next note's first sample is still on the plateau.
    for (const char* name : {"BWLowpass", "BWHighpass"}) {
        auto f = make_filter(name, std::make_shared<ConstantSource>(1.0f));
        f->prepare(RenderContext{48000});
        f->start(4800);
        const float settled = render_samples(*f, 4800).back();
        f->start(4800);
        const float firstOfNext = f->next();
        CHECK_NEAR(firstOfNext, settled, 1e-3f);
    }
}

// A started note carries nothing of the note before it: the second note of a
// used node renders what a fresh node's first note renders. (The Butterworth
// filters are the exception: their histories run on across notes.)
static bool restarts_clean(const std::string& name) {
    const auto make = [&] { return make_filter(name, std::make_shared<SineSource>()); };
    auto used = make(), fresh = make();
    used->prepare(RenderContext{48000});
    used->start(50000);
    render_samples(*used, 49000);          // past one lap of the longest buffer (the delay's, one second)
    used->start(4096);
    fresh->prepare(RenderContext{48000});
    fresh->start(4096);
    return same_bits(render_samples(*used, 4096), render_samples(*fresh, 4096));
}

static void run_filter_split_tests() {
    for (const std::string name : kFilterNames) {
        const bool ok = split_equivalent(
            [&] { return make_filter(name, std::make_shared<SineSource>()); });
        if (!ok) std::printf("  split differs: %s\n", name.c_str());
        CHECK(ok);
        if (name.compare(0, 2, "BW") == 0) continue;
        const bool clean = restarts_clean(name);
        if (!clean) std::printf("  note not clean: %s\n", name.c_str());
        CHECK(clean);
    }
    run_filter_rate_tests();
    run_filter_pin_split_tests();
}

static std::shared_ptr<Vibrato> make_vibrato(float speed) {
    auto v = std::make_shared<Vibrato>();
    v->set_param("frequency", std::make_shared<ConstantSource>(440.0f));
    v->set_setting("depth", 0.1f);
    v->set_setting("speed", speed);
    return v;
}

// A Vibrato builds its LFO once and resets it per note, which has to be the
// chain a note used to get by building it fresh.
static void run_vibrato_split_tests() {
    constexpr int kNote = 24000;
    {
        auto reused = make_vibrato(6.0f), fresh = make_vibrato(6.0f);
        reused->prepare(RenderContext{48000});
        reused->start(kNote);
        render_samples(*reused, 1000);        // mid-ramp: the noise has drawn, the envelopes have moved
        reused->start(kNote);
        fresh->prepare(RenderContext{48000});
        fresh->start(kNote);
        const auto a = render_samples(*reused, kNote), b = render_samples(*fresh, kNote);
        CHECK(same_bits(a, b));
        const auto [lo, hi] = std::minmax_element(b.begin(), b.end());
        CHECK(*hi - *lo > 1.0f);              // the LFO is audible, so the comparison means something
    }
    CHECK(split_equivalent([] { return make_vibrato(6.0f); }, kNote, 5));

    // A setting changed after prepare rebuilds the chain under the context
    // the node holds: the same bytes as the setting made before prepare.
    {
        auto late = make_vibrato(6.0f), early = make_vibrato(7.0f);
        late->prepare(RenderContext{48000});
        late->set_setting("speed", 7.0f);
        late->start(kNote);
        early->prepare(RenderContext{48000});
        early->start(kNote);
        CHECK(same_bits(render_samples(*late, kNote), render_samples(*early, kNote)));
    }

    // A note below the threshold passes the frequency through and leaves the
    // chain alone; the next long note is a fresh-chain note.
    {
        auto v = make_vibrato(6.0f), fresh = make_vibrato(6.0f);
        v->set_setting("threshold", 0.2f);
        fresh->set_setting("threshold", 0.2f);
        v->prepare(RenderContext{48000});
        v->start(4800);                       // 0.1 s: off
        for (float y : render_samples(*v, 200)) CHECK(y == 440.0f);
        v->start(kNote);
        fresh->prepare(RenderContext{48000});
        fresh->start(kNote);
        CHECK(same_bits(render_samples(*v, kNote), render_samples(*fresh, kNote)));
    }
    CHECK(follows_rate([] { return make_vibrato(6.0f); }, 48000, 24000));
    CHECK(follows_rate([] { return make_vibrato(6.0f); }, 24000, 48000));
}

// ---------------------------------------------------------------------------
// Combiners and modulators: the rate arrives through prepare(ctx), the note
// through start(frames). The crossfade, repeating and phased sources own their
// sub-sources and prepare and start each themselves, and the multiplex
// prepares its clones; a constant on a pin or a stage cannot show a missing
// call, so those are PinCounters that record the rate and the calls.
// ---------------------------------------------------------------------------
static std::shared_ptr<SineSource> sine_at(float hz) {
    auto s = std::make_shared<SineSource>();
    s->set_param("frequency", std::make_shared<ConstantSource>(hz));
    return s;
}

// The voice source of a loaded patch whose output is a three-clone multiplex
// over a two-node template (a sine under an AR envelope).
static std::shared_ptr<ValueSource> make_multiplex_voice() {
    auto ip = load_inst(R"({
      "sampleRate": 48000,
      "instrument": { "polyphony": 1 },
      "graph": { "output": "mux", "nodes": [
        { "id": "env", "type": "Envelope", "params": { "preset": "ar", "attack": 0.2 } },
        { "id": "osc", "type": "SineSource",
          "params": { "frequency": 220.0, "amplitude": { "ref": "env" } } },
        { "id": "mux", "type": "MultiplexSource",
          "params": { "count": 3, "source": { "ref": "osc" } } } ] }
    })");
    auto* pi = dynamic_cast<PitchedInstrument*>(ip.instrument.get());
    return pi->voicePool.at(0).source;
}

static void run_combine_mod_pin_split_tests() {
    const RenderContext ctx{48000};

    CrossfadeSource xf;
    auto s1 = std::make_shared<PinCounter>(), s2 = std::make_shared<PinCounter>();
    xf.set_param("source1", s1);
    xf.set_param("source2", s2);
    xf.prepare(ctx);
    CHECK(s1->rate == 48000 && s2->rate == 48000);
    CHECK(s1->starts == 0 && s2->starts == 0);           // prepared, not started
    xf.start(100);
    CHECK(s1->starts == 1 && s2->starts == 1);           // started once
    CHECK(s1->ctxPrepares == 1 && s2->ctxPrepares == 1);

    RepeatingSource rep;
    rep.set_setting("duration", 0.05f);
    rep.set_setting("gapDuration", 0.01f);
    auto kid = std::make_shared<PinCounter>();
    rep.set_param("source", kid);
    rep.prepare(ctx);
    CHECK(kid->rate == 48000 && kid->starts == 0);
    rep.start(9600);
    CHECK(kid->starts == 1);                               // the first repetition
    render_samples(rep, 9600);
    // A repetition boundary starts the child and never prepares it.
    CHECK(kid->ctxPrepares == 1);
    CHECK(kid->starts == 4);
    CHECK(kid->nexts > 3 * 2400 && kid->nexts < 4 * 2400);

    PhasedValueSource ph;
    auto st1 = std::make_shared<PinCounter>(), st2 = std::make_shared<PinCounter>();
    fill_phased(ph, 0.05f, {{st1, 0.5f, 0.0f, 0.0f, 1.0f}, {st2, 0.0f, 0.0f, 0.0f, 1.0f}});
    ph.prepare(ctx);
    CHECK(st1->rate == 48000 && st2->rate == 48000);
    CHECK(st1->starts == 0 && st2->starts == 0);
    ph.start(4800);
    CHECK(st1->starts == 1 && st2->starts == 1);
    CHECK(st1->ctxPrepares == 1 && st2->ctxPrepares == 1);

    // The multiplex prepares the clones its builder returns unprepared, builds
    // them once, and starts them per note; unbuilt, it prepares its input.
    MultiplexSource mux;
    mux.set_setting("count", 3.0f);
    std::vector<std::shared_ptr<PinCounter>> clones;
    mux.set_template([&clones](int) {
        clones.push_back(std::make_shared<PinCounter>());
        return std::shared_ptr<ValueSource>(clones.back());
    });
    mux.prepare(ctx);
    CHECK(clones.size() == 3);
    for (const auto& c : clones) CHECK(c->rate == 48000 && c->starts == 0);
    mux.start(64);
    mux.prepare(ctx);
    mux.start(64);
    CHECK(clones.size() == 3);
    for (const auto& c : clones) CHECK(c->starts == 2);
    MultiplexSource solo;
    auto wired = std::make_shared<PinCounter>();
    solo.set_param("source", wired);
    solo.prepare(ctx);
    CHECK(wired->rate == 48000 && wired->starts == 0);
    solo.start(64);
    CHECK(wired->starts == 1);
}

// Each repetition restarts the child by start(); the repeater's output is the
// child's own note, over and over, with the gap between.
static void run_repeating_restart_tests() {
    const RenderContext ctx{48000};
    const auto make = [] {
        auto r = std::make_shared<RepeatingSource>();
        r->set_setting("duration", 0.05f);
        r->set_setting("gapDuration", 0.01f);
        r->set_param("source", Envelope::make_ar(0.3f));
        return r;
    };
    auto once = make(), shim = make();
    once->prepare(ctx);
    once->start(4800);
    shim->prepare(ctx); shim->start(4800);
    const auto out = render_samples(*once, 9600);
    CHECK(same_bits(out, render_samples(*shim, 9600)));

    auto ref = Envelope::make_ar(0.3f);
    ref->prepare(ctx);
    ref->start(2400);
    const auto note = render_samples(*ref, 2400);
    constexpr int kPeriod = 2400 + 1 + 480;                  // the note, then the gap
    CHECK(std::equal(note.begin(), note.end(), out.begin()));
    CHECK(std::all_of(out.begin() + 2400, out.begin() + kPeriod, [](float v) { return v == 0.0f; }));
    CHECK(std::equal(note.begin(), note.end(), out.begin() + kPeriod));
    CHECK(std::equal(note.begin(), note.end(), out.begin() + 2 * kPeriod));
    CHECK(*std::max_element(note.begin(), note.end()) > 0.5f);   // the envelope is audible
}

static std::shared_ptr<RepeatingSource> make_repeater(float varPct = 0.3f) {
    auto r = std::make_shared<RepeatingSource>();
    r->set_setting("duration", 0.02f);
    r->set_setting("durVarPct", varPct);
    r->set_setting("gapDuration", 0.01f);
    r->set_setting("gapVarPct", varPct);
    r->set_param("source", Envelope::make_ar(0.3f));
    return r;
}

static std::shared_ptr<PhasedValueSource> make_phased() {
    auto p = std::make_shared<PhasedValueSource>();
    fill_phased(*p, 0.01f, {{sine_at(220.0f), 0.5f, 0.0f, 0.0f, 1.0f},
                            {sine_at(330.0f), 0.0f, 0.0f, 0.0f, 1.0f}});
    return p;
}

static std::shared_ptr<SegmentSource> make_segment(bool seconds, float varPct = 0.2f) {
    auto g = std::make_shared<SegmentSource>();
    g->set_array("values", seconds ? std::vector<float>{0.0004f, 1.0f, 0.0006f, -1.0f}
                                   : std::vector<float>{40.0f, 1.0f, 60.0f, -1.0f});
    g->set_param("widthVarPct", std::make_shared<ConstantSource>(varPct));
    g->set_param("valVarPct", std::make_shared<ConstantSource>(varPct / 2.0f));
    return g;
}

static void run_combine_mod_split_tests() {
    CHECK(split_equivalent([] { return make_repeater(); }));
    CHECK(split_equivalent([] {
        auto x = std::make_shared<CrossfadeSource>();
        x->set_param("source1", sine_at(220.0f));
        x->set_param("source2", sine_at(330.0f));
        return x;
    }));
    CHECK(split_equivalent([] { return make_phased(); }));
    CHECK(split_equivalent([] { return make_segment(false); }));
    CHECK(split_equivalent([] { return make_segment(true); }));
    CHECK(split_equivalent([] { return make_multiplex_voice(); }));
    CHECK(split_equivalent([] {
        return std::make_shared<CombinedSource>(sine_at(220.0f), sine_at(330.0f), CombineOp::Fade);
    }));
    CHECK(split_equivalent([] {
        auto s = std::make_shared<StaticVarSource>();
        s->set_setting("baseValue", 2.0f);
        s->set_setting("varPct", 0.5f);
        s->set_setting("bias", 0.1f);
        return s;
    }));
    CHECK(split_equivalent([] {
        auto s = std::make_shared<StaticRangeSource>();
        s->set_setting("min", 0.2f);
        s->set_setting("max", 0.4f);
        s->set_setting("bias", 0.3f);
        return s;
    }));

    // The rate is the host's: a node prepared at one rate and then another is
    // the node prepared at the second. Both draw from a stream that runs on
    // across notes, so the comparison leaves the variation out.
    CHECK(follows_rate([] { return make_repeater(0.0f); }, 48000, 22050));
    CHECK(follows_rate([] { return make_repeater(0.0f); }, 22050, 48000));
    CHECK(follows_rate([] { return make_segment(true, 0.0f); }, 48000, 22050));
    CHECK(follows_rate([] { return make_segment(true, 0.0f); }, 22050, 48000));
    CHECK(follows_rate([] { return make_phased(); }, 48000, 22050));
    CHECK(follows_rate([] { return make_phased(); }, 22050, 48000));
    run_combine_mod_pin_split_tests();
    run_repeating_restart_tests();
}

// Wired cases for the types whose defaults read 0: a brass evolution under a
// wavetable (its rate capture), a distorter with a signal, a sequence of
// partials in an additive source.
static void run_wired_split_tests() {
    CHECK(split_equivalent([] {
        auto w = std::make_shared<WavetableSource>();
        auto brass = std::make_shared<BrassEvolution>();
        brass->set_param("breath", std::make_shared<ConstantSource>(0.6f));
        w->set_param("evolution", brass);
        w->set_param("inputSource", std::make_shared<WhiteNoiseSource>(0x8Au));
        return w;
    }, 2048, 3));
    {
        auto brass_at = [](int rate) {
            WavetableSource w;
            auto brass = std::make_shared<BrassEvolution>();
            brass->set_param("breath", std::make_shared<ConstantSource>(0.6f));
            w.set_param("evolution", brass);
            w.set_param("inputSource", std::make_shared<WhiteNoiseSource>(0x8Au));
            w.prepare(RenderContext{rate});
            w.start(2048);
            return render_samples(w, 2048);
        };
        CHECK(!same_bits(brass_at(24000), brass_at(48000)));   // the lip filter is laid out from the rate
    }
    {   // an averaging evolution is a part: it sounds under a wavetable
        const SplitResult r = split_compare([] {
            auto w = std::make_shared<WavetableSource>();
            w->set_param("evolution", std::make_shared<AveragingEvolution>());
            w->set_param("inputSource", std::make_shared<WhiteNoiseSource>(0x5A5Au));
            return w;
        });
        CHECK(r.equal && r.nonzero);
    }
    {
        const SplitResult r = split_compare([] {
            auto d = std::make_shared<DistortedSource>(0x51u);
            d->set_param("source", std::make_shared<SineSource>());
            d->set_param("density", std::make_shared<ConstantSource>(0.5f));
            return d;
        });
        CHECK(r.equal && r.nonzero);
    }
    {
        const SplitResult r = split_compare([] {
            auto f = std::make_shared<AdditiveSource>();
            auto seq = std::make_shared<SequencePartials>();
            seq->set_setting("maxPartials", 12.0f);
            seq->set_setting("unitPO1", 0.3f);
            f->set_param("partials", seq);
            return f;
        }, 2048, 4);
        CHECK(r.equal && r.nonzero);
    }
}

// A setting changed on a Vibrato that is playing a note: the note keeps the
// chain it started with, and the next start plays the new one.
static void run_vibrato_late_setting_tests() {
    auto make = [] {
        auto v = std::make_shared<Vibrato>();
        v->set_param("frequency", std::make_shared<ConstantSource>(440.0f));
        v->set_setting("depth", 0.1f);
        v->set_setting("speed", 6.0f);
        v->prepare(RenderContext{48000});
        return v;
    };
    auto edited = make(), control = make();
    edited->start(4096); control->start(4096);
    const auto e1 = render_samples(*edited, 1000), c1 = render_samples(*control, 1000);
    edited->set_setting("speed", 7.0f);                 // mid-note
    const auto e2 = render_samples(*edited, 1000), c2 = render_samples(*control, 1000);
    CHECK(same_bits(e1, c1) && same_bits(e2, c2));      // the playing note is untouched
    edited->start(4096);
    auto fresh = std::make_shared<Vibrato>();
    fresh->set_param("frequency", std::make_shared<ConstantSource>(440.0f));
    fresh->set_setting("depth", 0.1f);
    fresh->set_setting("speed", 7.0f);
    fresh->prepare(RenderContext{48000});
    fresh->start(4096);
    CHECK(same_bits(render_samples(*edited, 4096), render_samples(*fresh, 4096)));   // the next note is the new chain
}

// The hosts: a loaded instrument is prepared by its
// build and only started per note; render takes no context.
static void run_host_split_tests() {
    const int n = 48000;
    auto render_twice = [&](const char* path) {
        std::vector<float> a(size_t(n), 0.0f), b(size_t(n), 0.0f);
        for (auto* out : {&a, &b}) {
            auto ip = load_scoreless(path);
            auto* inst = dynamic_cast<PitchedInstrument*>(ip.instrument.get());
            inst->play_note({.start = 0.0f, .duration = 0.4f, .noteNumber = 60.0f, .velocity = 0.8f, .onset = 0, .hold = false, .curve = std::nullopt});
            inst->play_note({.start = 0.5f, .duration = 0.4f, .noteNumber = 64.0f, .velocity = 0.8f, .onset = 0, .hold = false, .curve = std::nullopt});
            ip.instrument->render(out->data(), n);
        }
        CHECK(same_bits(a, b));
        float peak = 0.0f;
        for (float v : a) peak = std::max(peak, std::fabs(v));
        CHECK(peak > 0.01f);
    };
    render_twice("patches/baselines/BaselineSIN.json");
    // PerformSource learns the rate through prepare: the wheel smoother's
    // first step is 1 - exp(-1 / (0.010 s * rate)).
    {
        PerformSource ps;
        auto is = std::make_shared<InstrumentState>();
        is->wheel.store(1.0f);
        ps.set_instrument_state(is);
        ps.prepare(RenderContext{48000});
        ps.tick();
        CHECK_NEAR(ps.wheel(), 1.0f - std::exp(-1.0f / (0.010f * 48000.0f)), 1e-6f);
    }
}

// The two-call contract: the sweep over every type, then one group per family.
static void run_prepare_start_tests() {
    run_split_equivalence_tests();
    run_core_split_tests();
    run_noise_split_tests();
    run_wavetable_split_tests();
    run_hybrid_ks_split_tests();
    run_additive_split_tests();
    run_physical_split_tests();
    run_filter_split_tests();
    run_vibrato_split_tests();
    run_combine_mod_split_tests();
    run_wired_split_tests();
    run_vibrato_late_setting_tests();
    run_host_split_tests();
}

// start() and next() never throw. An invalid runtime value
// is clamped to the narrowest value that makes the old throw's condition
// false and the node reports a fault bit; the host reads the bits after a
// render. prepare() may throw, and does on a host error.
static void run_no_throw_tests() {
    TestHost h;
    // A frequency driven to zero mid-note renders with the pin's minimum and
    // reports, instead of terminating the process from the audio thread.
    auto f = std::make_shared<ConstantSource>(440.0f);
    auto osc = std::make_shared<SineSource>();
    osc->set_param("frequency", f);
    osc->prepare(h.ctx); osc->start(48000);
    h.clock.tick(); osc->next();
    CHECK(osc->faults() == 0);
    f->set(0.0f);
    h.clock.tick();
    float v = 0.0f;
    bool threw = false;
    try { v = osc->next(); } catch (...) { threw = true; }
    CHECK(!threw);
    CHECK(std::isfinite(v));
    CHECK((osc->faults() & fault_bit(Fault::FrequencyClamped)) != 0);
    f->set(440.0f);
    h.clock.tick(); osc->next();                      // computes normally again
    osc->clear_faults();
    CHECK(osc->faults() == 0);
    // A table fill at a frequency above the rate fills one sample and reports.
    auto wt = std::make_shared<WavetableSource>();
    wt->set_param("frequency", std::make_shared<ConstantSource>(96000.0f));
    wt->set_param("inputSource", std::make_shared<ConstantSource>(0.5f));
    wt->prepare(h.ctx); wt->start(4800);
    h.clock.tick();
    threw = false;
    try { wt->next(); } catch (...) { threw = true; }
    CHECK(!threw);
    CHECK((wt->faults() & fault_bit(Fault::TableClamped)) != 0);
#ifdef NDEBUG
    // A node no host prepared: start() reports and skips on_start (nothing
    // divides by zero there); a bare node's next() computes as it always has
    // and reports the same fault. (In a Debug build the assert on start()
    // is the contract and fires first.)
    auto dl = std::make_shared<DelayLineSource>();
    dl->set_param("source", std::make_shared<ConstantSource>(1.0f));
    dl->start(4800);
    CHECK((dl->faults() & fault_bit(Fault::NeverPrepared)) != 0);
#else
    auto dl = std::make_shared<DelayLineSource>();
#endif
    auto bareC = std::make_shared<Counter>();
    CHECK_NEAR(bareC->next(), 1.0f, 1e-6f);            // computed, as a bare node always has
    CHECK((bareC->faults() & fault_bit(Fault::NeverPrepared)) != 0);
    // prepare() with no rate is a host error and throws where throwing is allowed.
    threw = false;
    try { dl->prepare(RenderContext{0}); } catch (const std::invalid_argument&) { threw = true; }
    CHECK(threw);
    CHECK(std::string(fault_name(Fault::FrequencyClamped)) == "frequency clamped");
}

// The fault bits reach the host. A cycle with no tap is a
// bit, not a print, through next() and through the host calls; a clockless
// pull reports; the instrument collects by node id and clears as it reads.
static void run_fault_tests() {
    TestHost h;
    auto loop = tapless_cycle(h);
    h.clock.tick(); CHECK_NEAR(loop->next(), 1.0f, 1e-6f);
    CHECK((loop->faults() & fault_bit(Fault::CycleWithoutTap)) != 0);
    loop->clear_faults();
    loop->prepare(h.ctx);                              // re-entered through the cycle: returns, no overflow
    CHECK((loop->faults() & fault_bit(Fault::CycleWithoutTap)) != 0);
    loop->clear_faults();
    loop->start(16);
    CHECK((loop->faults() & fault_bit(Fault::CycleWithoutTap)) != 0);
    loop->clear_param("source2");
    // A clockless pull on a prepared node reports; the host is the only reader.
    auto bare = std::make_shared<Counter>();
    bare->prepare(RenderContext{48000}); bare->start(16);
    bare->next();
    CHECK((bare->faults() & fault_bit(Fault::Clockless)) != 0);
    CHECK(bare->take_faults() == fault_bit(Fault::Clockless));   // read and cleared in one step
    CHECK(bare->faults() == 0);
    // A throw out of on_prepare (prepare may throw) leaves the node usable:
    // the next prepare runs on_prepare, and next() reports no cycle.
    struct ThrowingPrepare final : Generator {
        bool armed{true};
        int prepares{0};
        void on_prepare(const RenderContext&) override {
            if (armed) { armed = false; throw std::runtime_error("on_prepare failed"); }
            ++prepares;
        }
        float compute() override { return 1.0f; }
        const char* type_name() const override { return "ThrowingPrepare"; }
    };
    auto tp = std::make_shared<ThrowingPrepare>();
    bool threw = false;
    try { tp->prepare(h.ctx); } catch (const std::runtime_error&) { threw = true; }
    CHECK(threw);
    tp->prepare(h.ctx);
    CHECK(tp->prepares == 1);
    tp->start(16); h.clock.tick();
    CHECK_NEAR(tp->next(), 1.0f, 1e-6f);
    CHECK((tp->faults() & fault_bit(Fault::CycleWithoutTap)) == 0);
    // The host collects by node id, once per id across the pool, in id order,
    // and clears as it reads; a prepare after the render clears nothing.
    const char* json = R"({"sampleRate":48000,"instrument":{"polyphony":2},
      "graph":{"output":"sum","nodes":[
        {"id":"b","type":"SineSource","params":{"frequency":0.0,"amplitude":1.0}},
        {"id":"a","type":"SineSource","params":{"frequency":0.0,"amplitude":1.0}},
        {"id":"sum","type":"CombinedSource","params":{"source1":{"ref":"b"},"source2":{"ref":"a"},"operation":3}}]}})";
    auto ip = load_inst(json);
    auto* pi = dynamic_cast<PitchedInstrument*>(ip.instrument.get());
    CHECK(pi != nullptr);
    pi->play_note({.start = 0.0f, .duration = 0.1f, .noteNumber = 60.0f, .velocity = 0.8f, .onset = 0, .hold = false});
    std::vector<float> out(4800);
    ip.instrument->render(out.data(), int(out.size()));
    ip.instrument->prepare(RenderContext{48000});      // a scored patch is pre-rendered at load, then prepared again
    std::vector<FaultReport> faults;
    ip.instrument->collect_faults(faults);
    CHECK(faults.size() == 2);
    CHECK(faults.size() == 2 && faults[0].nodeId == "a" && faults[1].nodeId == "b");
    CHECK(faults.size() == 2 && (faults[0].faults & fault_bit(Fault::FrequencyClamped)) != 0
                             && (faults[1].faults & fault_bit(Fault::FrequencyClamped)) != 0);
    faults.clear();
    ip.instrument->collect_faults(faults);
    CHECK(faults.empty());                             // cleared as read
}

// A rejected wire says so. set_param answers false for a pin
// the node does not have or a part of the wrong kind; the build turns that,
// and an unknown key, into a load failure naming node, pin and reason.
static void run_wire_rejection_tests() {
    auto osc = std::make_shared<SineSource>();
    CHECK(osc->set_param("frequency", std::make_shared<ConstantSource>(1.0f)));
    CHECK(!osc->set_param("freq", std::make_shared<ConstantSource>(1.0f)));         // no such pin
    auto add = std::make_shared<AdditiveSource>();                              // registry name "AdditiveSource"
    CHECK(!add->set_param("partials", std::make_shared<SineSource>()));              // wrong kind for the slot
    CHECK(add->set_param("partials", std::make_shared<FullPartials>()));
    const char* bad_key = R"({"sampleRate":48000,"instrument":{"polyphony":1},"graph":{"output":"osc","nodes":[
        {"id":"osc","type":"SineSource","params":{"frequncy":440.0}}]}})";
    std::string msg;
    msg = load_refused(bad_key);
    CHECK(msg.find("node 'osc' (SineSource): unknown key 'frequncy'") != std::string::npos);
    const char* bad_kind = R"({"sampleRate":48000,"instrument":{"polyphony":1},"graph":{"output":"add","nodes":[
        {"id":"s","type":"SineSource","params":{"frequency":440.0}},
        {"id":"add","type":"AdditiveSource","params":{"frequency":220.0,"partials":{"ref":"s"}}}]}})";
    msg.clear();
    msg = load_refused(bad_kind);
    CHECK(msg.find("node 'add' (AdditiveSource): 'partials' does not accept a SineSource") != std::string::npos);
    // A key a hand-built node's branch never read (the VarSource case the lint
    // found in the bwg patches) fails the load too: the check runs per node.
    const char* hand_built = R"({"sampleRate":48000,"instrument":{"polyphony":1},"graph":{"output":"v","nodes":[
        {"id":"v","type":"VarSource","params":{"value":0.0}}]}})";
    msg.clear();
    msg = load_refused(hand_built);
    CHECK(msg.find("node 'v' (VarSource): unknown key 'value'") != std::string::npos);
}

// A string where a pin takes a value fails the load naming the node, the pin
// and the string; the wavetable's old string-typed evolution also names the
// tool that rewrites it as a node, and the node form of the same patch loads.
static void run_string_pin_refusal_tests() {
    const char* evo = R"({"sampleRate":48000,"instrument":{"polyphony":1},"graph":{"output":"wt","nodes":[
        {"id":"wt","type":"WavetableSource","params":{"frequency":330.0,"evolution":"pluck","muting":0.0,"evolutionSeed":42}}]}})";
    std::string msg;
    msg = load_refused(evo);
    CHECK(msg.find("node 'wt' (WavetableSource): 'evolution' is the string \"pluck\"") != std::string::npos);
    CHECK(msg.find("tools/convert_patch.py") != std::string::npos);
    CHECK(msg.find("a slot takes {\"ref\": id}") != std::string::npos);       // a slot, so no number is offered
    const char* freq = R"({"sampleRate":48000,"instrument":{"polyphony":1},"graph":{"output":"osc","nodes":[
        {"id":"osc","type":"SineSource","params":{"frequency":"440"}}]}})";
    msg.clear();
    msg = load_refused(freq);
    CHECK(msg.find("node 'osc' (SineSource): 'frequency' is the string \"440\"") != std::string::npos);
    CHECK(msg.find("convert_patch") == std::string::npos);
    CHECK(msg.find("a pin takes a number") != std::string::npos);
    const char* node = R"({"sampleRate":48000,"instrument":{"polyphony":1},"graph":{"output":"wt","nodes":[
        {"id":"wt_evolution","type":"PluckEvolution","params":{"muting":0.0,"seed":42}},
        {"id":"wt","type":"WavetableSource","params":{"frequency":330.0,"evolution":{"ref":"wt_evolution"}}}]}})";
    auto ip = load_inst(node);
    CHECK(ip.instrument != nullptr);
}

#include "mforce/patch/envelope_preset.h"

// ---------------------------------------------------------------------------
// The preset form against the preset classes: for each of the six layouts a
// class built through set_setting (what the registry path's generic settings
// pass does) lays out the same stages as the form built from the same keys
// at once.
// ---------------------------------------------------------------------------
static bool same_layout(const Envelope& a, const Envelope& b) {
    if (a.stage_count() != b.stage_count()) return false;
    for (int i = 0; i < a.stage_count(); ++i) {
        const auto& s = a.stage(i);
        const auto& t = b.stage(i);
        if (s.percent != t.percent || s.minSec != t.minSec || s.maxSec != t.maxSec || s.nominal != t.nominal)
            return false;
        if (s.ramp.startVal != t.ramp.startVal || s.ramp.endVal != t.ramp.endVal
            || s.ramp.type != t.ramp.type || s.ramp.power != t.ramp.power)
            return false;
    }
    return a.absolute_time() == b.absolute_time() && a.has_adsr_shape_() == b.has_adsr_shape_();
}

// The four layout factories, stage by stage: the form shapes the curved
// stages afterwards from its keys, so these are the factories' own values,
// which a direct caller (Vibrato's ramps) gets.
struct StageRow { float start, end; RampType type; float power, percent, minSec, maxSec; };
static bool stages_are(const Envelope& e, std::initializer_list<StageRow> rows) {
    if (e.stage_count() != int(rows.size())) return false;
    int i = 0;
    for (const auto& r : rows) {
        const auto& s = e.stage(i++);
        if (s.ramp.startVal != r.start || s.ramp.endVal != r.end || s.ramp.type != r.type || s.ramp.power != r.power
            || s.percent != r.percent || s.minSec != r.minSec || s.maxSec != r.maxSec || s.nominal != 0.0f)
            return false;
    }
    return true;
}

static void run_envelope_factory_tests() {
    const RampType L = RampType::Linear, S = RampType::Sine;
    CHECK(stages_are(*Envelope::make_as(0.3f),
                     {{0.0f, 1.0f, L, 0.0f, 0.3f, 0.0f, 1.0f}, {1.0f, 1.0f, L, 0.0f, 0.0f, 0.0f, 0.0f}}));
    CHECK(stages_are(*Envelope::make_as(0.3f, 0.6f, true),
                     {{0.6f, 0.0f, L, 0.0f, 0.3f, 0.0f, 1.0f}, {0.0f, 0.0f, L, 0.0f, 0.0f, 0.0f, 0.0f}}));
    CHECK(stages_are(*Envelope::make_asr(0.15f, 0.4f, 0.3f),
                     {{0.0f, 0.4f, L, 0.0f, 0.15f, 0.0f, 1.0f}, {0.4f, 0.4f, L, 0.0f, 0.0f, 0.0f, 0.0f},
                      {0.4f, 0.0f, S, 0.0f, 0.3f, 0.0f, 0.0f}}));
    CHECK(stages_are(*Envelope::make_ads(0.1f, 0.25f, 0.3f),
                     {{0.0f, 1.0f, L, 0.0f, 0.1f, 0.0f, 1.0f}, {1.0f, 0.3f, L, 0.0f, 0.25f, 0.0f, 0.5f},
                      {0.3f, 0.3f, L, 0.0f, 0.0f, 0.0f, 0.0f}}));
    CHECK(stages_are(*Envelope::make_adr(0.1f, 0.2f, 0.55f),
                     {{0.0f, 1.0f, L, 0.0f, 0.1f, 0.0f, 1.0f}, {1.0f, 0.55f, L, 0.0f, 0.2f, 0.0f, 0.5f},
                      {0.55f, 0.0f, S, 0.0f, 0.0f, 0.0f, 0.0f}}));
    for (const auto& e : {Envelope::make_as(0.3f), Envelope::make_asr(0.15f, 0.4f, 0.3f),
                          Envelope::make_ads(0.1f, 0.25f, 0.3f), Envelope::make_adr(0.1f, 0.2f, 0.55f)})
        CHECK(!e->has_adsr_shape_() && !e->absolute_time());      // none is the four-slot shape, none in seconds
}

// The six preset forms, stage by stage, at their defaults and with every
// knob off its default. The defaults are the deleted classes' own, but one:
// a bare adsr form releases over 0.0 where the class released over 0.1 (the
// migration writes the key).
static void run_envelope_form_tests() {
    const RampType L = RampType::Linear, E = RampType::Expo, I = RampType::InverseExpo, S = RampType::Sine;
    using J = nlohmann::json;
    CHECK(stages_are(*envelope_from_preset_json(J{{"preset", "ar"}}),
                     {{0.0f, 1.0f, L, 0.0f, 0.2f, 0.0f, 1.0f}, {1.0f, 0.0f, S, 0.0f, 0.0f, 0.0f, 0.0f}}));
    CHECK(stages_are(*envelope_from_preset_json(J::parse(R"({"preset":"ar","attack":0.4,"attackCurve":2,"attackPower":1.2,"releaseCurve":0,"releasePower":3.0})")),
                     {{0.0f, 1.0f, I, 1.2f, 0.4f, 0.0f, 1.0f}, {1.0f, 0.0f, L, 3.0f, 0.0f, 0.0f, 0.0f}}));
    CHECK(stages_are(*envelope_from_preset_json(J{{"preset", "as"}}),
                     {{0.0f, 1.0f, L, 0.0f, 0.2f, 0.0f, 1.0f}, {1.0f, 1.0f, L, 0.0f, 0.0f, 0.0f, 0.0f}}));
    CHECK(stages_are(*envelope_from_preset_json(J::parse(R"({"preset":"as","attack":0.35,"attackCurve":1,"attackPower":2.5,"reverse":true,"sustainLevel":0.6})")),
                     {{0.6f, 0.0f, E, 2.5f, 0.35f, 0.0f, 1.0f}, {0.0f, 0.0f, L, 0.0f, 0.0f, 0.0f, 0.0f}}));
    CHECK(stages_are(*envelope_from_preset_json(J{{"preset", "asr"}}),
                     {{0.0f, 1.0f, L, 0.0f, 0.2f, 0.0f, 1.0f}, {1.0f, 1.0f, L, 0.0f, 0.0f, 0.0f, 0.0f},
                      {1.0f, 0.0f, S, 0.0f, 0.1f, 0.0f, 0.0f}}));
    CHECK(stages_are(*envelope_from_preset_json(J::parse(R"({"preset":"asr","attack":0.15,"attackCurve":2,"attackPower":1.5,"release":0.3,"releaseCurve":2,"releasePower":0.5,"sustainLevel":0.4})")),
                     {{0.0f, 0.4f, I, 1.5f, 0.15f, 0.0f, 1.0f}, {0.4f, 0.4f, L, 0.0f, 0.0f, 0.0f, 0.0f},
                      {0.4f, 0.0f, I, 0.5f, 0.3f, 0.0f, 0.0f}}));
    CHECK(stages_are(*envelope_from_preset_json(J{{"preset", "ads"}}),
                     {{0.0f, 1.0f, L, 0.0f, 0.2f, 0.0f, 1.0f}, {1.0f, 0.0f, L, 0.0f, 0.1f, 0.0f, 0.5f},
                      {0.0f, 0.0f, L, 0.0f, 0.0f, 0.0f, 0.0f}}));
    CHECK(stages_are(*envelope_from_preset_json(J::parse(R"({"preset":"ads","attack":0.1,"attackCurve":3,"decay":0.25,"decayCurve":1,"decayPower":2.0,"sustainLevel":0.3})")),
                     {{0.0f, 1.0f, S, 0.0f, 0.1f, 0.0f, 1.0f}, {1.0f, 0.3f, E, 2.0f, 0.25f, 0.0f, 0.5f},
                      {0.3f, 0.3f, L, 0.0f, 0.0f, 0.0f, 0.0f}}));
    CHECK(stages_are(*envelope_from_preset_json(J{{"preset", "adr"}}),
                     {{0.0f, 1.0f, L, 0.0f, 0.2f, 0.0f, 1.0f}, {1.0f, 0.7f, L, 0.0f, 0.1f, 0.0f, 0.5f},
                      {0.7f, 0.0f, S, 0.0f, 0.0f, 0.0f, 0.0f}}));
    CHECK(stages_are(*envelope_from_preset_json(J::parse(R"({"preset":"adr","attack":0.1,"attackPower":0.7,"decay":0.2,"decayCurve":3,"decayLevel":0.55,"releaseCurve":1,"releasePower":1.5})")),
                     {{0.0f, 1.0f, L, 0.7f, 0.1f, 0.0f, 1.0f}, {1.0f, 0.55f, S, 0.0f, 0.2f, 0.0f, 0.5f},
                      {0.55f, 0.0f, E, 1.5f, 0.0f, 0.0f, 0.0f}}));
    CHECK(stages_are(*envelope_from_preset_json(J{{"preset", "adsr"}}),
                     {{0.0f, 1.0f, L, 0.0f, 0.2f, 0.0f, 1.0f}, {1.0f, 0.7f, L, 0.0f, 0.1f, 0.0f, 0.5f},
                      {0.7f, 0.7f, L, 0.0f, 0.0f, 0.0f, 0.0f}, {0.7f, 0.0f, S, 0.0f, 0.0f, 0.0f, 0.0f}}));
    CHECK(stages_are(*envelope_from_preset_json(J::parse(R"({"preset":"adsr","attack":0.12,"attackCurve":1,"attackPower":0.8,"decay":0.2,"decayCurve":2,"decayPower":1.1,"sustainLevel":0.5,"release":0.25,"releaseCurve":0,"releasePower":2.0})")),
                     {{0.0f, 1.0f, E, 0.8f, 0.12f, 0.0f, 1.0f}, {1.0f, 0.5f, I, 1.1f, 0.2f, 0.0f, 0.5f},
                      {0.5f, 0.5f, L, 0.0f, 0.0f, 0.0f, 0.0f}, {0.5f, 0.0f, L, 2.0f, 0.25f, 0.0f, 0.0f}}));
    // adsr is the four-slot shape (the live sustain rewrite applies); the
    // others are not, and report the four-slot view of sustainLevel, 0; none
    // is in seconds.
    CHECK(envelope_from_preset_json(J{{"preset", "adsr"}})->has_adsr_shape_());
    for (const char* n : {"ar", "as", "asr", "ads", "adr"}) {
        auto e = envelope_from_preset_json(J{{"preset", n}});
        CHECK(!e->has_adsr_shape_() && !e->absolute_time());
        CHECK_NEAR(e->get_setting("sustainLevel"), 0.0f, 1e-6f);
    }
    // A curve outside 0..3 and an unknown preset are refused by name.
    std::string msg;
    try { envelope_from_preset_json(J{{"preset", "as"}, {"attackCurve", 4}}); } catch (const std::runtime_error& e) { msg = e.what(); }
    CHECK(msg.find("attackCurve is 4") != std::string::npos);
    msg.clear();
    try { envelope_from_preset_json(J{{"preset", "adsx"}}); } catch (const std::runtime_error& e) { msg = e.what(); }
    CHECK(msg.find("Unknown envelope preset: adsx") != std::string::npos);
    // The key list is what the builders read; the table is the six in menu order.
    const auto& keys = envelope_preset_keys();
    CHECK(std::find(keys.begin(), keys.end(), "reverse") != keys.end());
    CHECK(std::find(keys.begin(), keys.end(), "decayLevel") != keys.end());
    CHECK(std::find(keys.begin(), keys.end(), "stages") == keys.end());
    CHECK(std::size(kEnvelopePresets) == 6 && std::string(kEnvelopePresets[0].name) == "ar"
          && std::string(kEnvelopePresets[5].name) == "adsr");
}

// Through a load: a form node and the stage table it lays out, given as
// columns, render the same bytes; a seed and the accuracy knobs reach the
// form node as they reach the table node.
static std::vector<float> render_loaded_note(const char* json, int n) {
    auto ip = load_inst(json);
    auto* pi = dynamic_cast<PitchedInstrument*>(ip.instrument.get());
    pi->play_note({.start = 0.0f, .duration = 0.2f, .noteNumber = 60.0f, .velocity = 0.8f, .onset = 0, .hold = false});
    std::vector<float> out(static_cast<size_t>(n));
    ip.instrument->render(out.data(), n);
    return out;
}

static void run_envelope_form_loader_tests() {
    const char* form = R"({"sampleRate":48000,"instrument":{"polyphony":1},"graph":{"output":"osc","nodes":[
        {"id":"Env","type":"Envelope","params":{"preset":"as","attack":0.3,"attackCurve":1,"attackPower":2.0,"reverse":true,"sustainLevel":0.6,"stage_accuracy":0.9,"ramp_accuracy":0.95,"seed":77,"minValue":0.0,"maxValue":0.8}},
        {"id":"osc","type":"SineSource","params":{"frequency":220.0,"amplitude":{"ref":"Env"}}}]}})";
    const char* stages = R"({"sampleRate":48000,"instrument":{"polyphony":1},"graph":{"output":"osc","nodes":[
        {"id":"Env","type":"Envelope","params":{"startVal":[0.6,0.0],"endVal":[0.0,0.0],"power":[2.0,0.0],"type":["Expo","Linear"],"percent":[0.3,0.0],"minSec":[0.0,0.0],"maxSec":[1.0,0.0],"nominal":[0.0,0.0],"stage_accuracy":0.9,"ramp_accuracy":0.95,"seed":77,"minValue":0.0,"maxValue":0.8}},
        {"id":"osc","type":"SineSource","params":{"frequency":220.0,"amplitude":{"ref":"Env"}}}]}})";
    const auto a = render_loaded_note(form, 9600), b = render_loaded_note(stages, 9600);
    CHECK(same_bits(a, b));
    CHECK(a[1000] != 0.0f);                       // inside the attack: reversed, the shape falls to 0 and holds there
    auto env_of = [](const char* json) {
        auto ip = load_inst(json);
        auto* pi = dynamic_cast<PitchedInstrument*>(ip.instrument.get());
        return std::dynamic_pointer_cast<Envelope>(pi->voicePool.at(0).nodesById.at("Env"));
    };
    auto ef = env_of(form), es = env_of(stages);
    CHECK(ef && es && same_layout(*ef, *es));
    CHECK(ef->get_seed() == es->get_seed() && ef->get_seed() != 0u);
    CHECK_NEAR(ef->get_setting("stage_accuracy"), 0.9f, 1e-6f);
    CHECK_NEAR(ef->get_setting("ramp_accuracy"), 0.95f, 1e-6f);
}

// ---------------------------------------------------------------------------
// The Envelope's stage table: the eight columns of the group "stages" are what
// the node holds, and the stage layout the render walks is derived from them.
// ---------------------------------------------------------------------------
static const char* const kStageColumns[] = {"startVal", "endVal", "power", "type",
                                            "percent", "minSec", "maxSec", "nominal"};

// Field `c` of a stage, in the columns' order; the type as its index.
static float stage_field(const Envelope::Stage& s, int c) {
    switch (c) {
        case 0:  return s.ramp.startVal;
        case 1:  return s.ramp.endVal;
        case 2:  return s.ramp.power;
        case 3:  return float(int(s.ramp.type));
        case 4:  return s.percent;
        case 5:  return s.minSec;
        case 6:  return s.maxSec;
        default: return s.nominal;
    }
}

static void run_envelope_table_tests() {
    std::printf("the Envelope's stage table: the twin, the doors, the order, timeMode, the mapping, the load\n");
    using Stage = Envelope::Stage;
    using J = nlohmann::json;
    const RampType L = RampType::Linear, E = RampType::Expo, I = RampType::InverseExpo,
                   S = RampType::Sine, H = RampType::Hold;

    // Stage count and every field of every stage, the floats by their bits.
    auto same_layout = [](const Envelope& a, const Envelope& b) {
        if (a.stage_count() != b.stage_count()) return false;
        for (int i = 0; i < a.stage_count(); ++i) {
            const Stage& s = a.stage(i);
            const Stage& t = b.stage(i);
            if (!same_bits(s.ramp.startVal, t.ramp.startVal) || !same_bits(s.ramp.endVal, t.ramp.endVal)
                || s.ramp.type != t.ramp.type || !same_bits(s.ramp.power, t.ramp.power)
                || !same_bits(s.percent, t.percent) || !same_bits(s.minSec, t.minSec)
                || !same_bits(s.maxSec, t.maxSec) || !same_bits(s.nominal, t.nominal))
                return false;
        }
        return true;
    };
    // Each of the eight columns equals the layout field for field.
    auto columns_match_layout = [](const Envelope& e) {
        for (int c = 0; c < 8; ++c) {
            const std::vector<float> col = e.get_array(kStageColumns[c]);
            if (int(col.size()) != e.stage_count()) return false;
            for (int i = 0; i < e.stage_count(); ++i)
                if (!same_bits(col[size_t(i)], stage_field(e.stage(i), c))) return false;
        }
        return true;
    };
    auto rows_of = [](const Envelope& e) {
        std::vector<Stage> rows;
        for (int i = 0; i < e.stage_count(); ++i) rows.push_back(e.stage(i));
        return rows;
    };
    auto by_add = [](const std::vector<Stage>& rows, float timeMode) {
        auto e = std::make_shared<Envelope>();
        e->set_setting("timeMode", timeMode);
        for (const Stage& s : rows) e->add_stage(s);
        return e;
    };
    // The same rows written as the eight columns, in declaration order or reversed.
    auto by_columns = [](const std::vector<Stage>& rows, float timeMode, bool reverse) {
        auto e = std::make_shared<Envelope>();
        e->set_setting("timeMode", timeMode);
        for (int k = 0; k < 8; ++k) {
            const int c = reverse ? 7 - k : k;
            std::vector<float> col;
            for (const Stage& s : rows) col.push_back(stage_field(s, c));
            e->set_array(kStageColumns[c], col);
        }
        return e;
    };
    auto note = [](Envelope& e, int n) {
        e.prepare(RenderContext{48000});
        e.start(n);
        return render_samples(e, n);
    };

    // (1) The twin for the stage layout: add_stage and set_array lay out the
    // same stages and render the same samples.
    std::vector<std::pair<std::vector<Stage>, float>> lists;
    for (float a : {0.0f, 0.15f, 0.4f})
        for (float k : {0.0f, 0.3f, 1.0f}) {
            lists.push_back({rows_of(*Envelope::make_ar(a, k * 0.1f, 1.0f - k * 0.5f)), 0.0f});
            lists.push_back({rows_of(*Envelope::make_as(a, k + 0.2f, k > 0.5f)), 0.0f});
            lists.push_back({rows_of(*Envelope::make_asr(a, k + 0.2f, 0.3f - a * 0.5f)), 0.0f});
            lists.push_back({rows_of(*Envelope::make_ads(a, k * 0.2f, k)), 0.0f});
            lists.push_back({rows_of(*Envelope::make_adr(a, k * 0.2f, k)), 0.0f});
            lists.push_back({rows_of(*Envelope::make_adsr(a, k * 0.2f, k, 0.3f - a * 0.5f)), 0.0f});
            lists.push_back({rows_of(*Envelope::make_adsr_abs(a * 0.1f, k * 0.05f, k, 0.02f)), 1.0f});
        }
    lists.push_back({{{{0.0f, 1.0f, L, 0.0f}, 0.1f, 0.0f, 0.5f},
                      {{1.0f, 0.4f, E, 2.0f}, 0.15f, 0.01f, 0.2f},
                      {{0.4f, 0.9f, I, 2.0f}, 0.1f, 0.0f, 0.0f},
                      {{0.9f, 0.5f, S, 0.0f}, 0.0f, 0.0f, 0.0f},
                      {{0.5f, 0.1f, S, 3.0f}, 0.2f, 0.0f, 1.0f},
                      {{0.1f, 0.7f, H, 0.0f}, 0.1f, 0.0f, 0.0f, 0.05f}}, 0.0f});
    lists.push_back({{{{-0.5f, 1.5f, E, 0.5f}, 0.05f, 0.0f, 0.0f}, {{1.5f, -0.25f, I, 3.0f}, 0.0f, 0.0f, 0.0f},
                      {{-0.25f, 0.0f, H, 0.0f}, 0.03f, 0.0f, 0.0f, 0.2f}}, 1.0f});
    lists.push_back({{}, 0.0f});
    lists.push_back({{Stage{}, Stage{}, Stage{}}, 0.0f});
    for (const auto& [rows, timeMode] : lists) {
        auto a = by_add(rows, timeMode);
        auto b = by_columns(rows, timeMode, false);
        auto c = by_columns(rows, timeMode, true);
        CHECK(same_layout(*a, *b) && same_layout(*a, *c));
        CHECK(a->stage_count() == int(rows.size()));
        CHECK(a->absolute_time() == b->absolute_time() && a->absolute_time() == c->absolute_time());
        const auto ra = note(*a, 4800), rb = note(*b, 4800), rc = note(*c, 4800);
        CHECK(same_bits(ra, rb) && same_bits(ra, rc));
    }
    // A factory and the add_stage build of its rows render alike (the factory
    // writes its columns by the same door).
    {
        auto f = Envelope::make_adsr_abs(0.01f, 0.05f, 0.6f, 0.02f);
        auto a = by_add(rows_of(*f), 1.0f);
        CHECK(same_layout(*f, *a) && same_bits(note(*f, 4800), note(*a, 4800)));
    }

    // (2) The columns follow every door.
    {
        auto e = Envelope::make_adsr(0.1f, 0.2f, 0.6f, 0.25f);
        CHECK(columns_match_layout(*e));
        e->add_stage({{0.0f, 0.3f, E, 1.5f}, 0.1f, 0.01f, 0.4f, 0.02f});
        CHECK(e->stage_count() == 5 && columns_match_layout(*e));
        e->remove_stage(4);
        CHECK(e->stage_count() == 4 && columns_match_layout(*e));
        e->remove_stage(0);
        CHECK(e->stage_count() == 3 && columns_match_layout(*e));
        e->remove_stage(7);                       // out of range: nothing moves
        CHECK(e->stage_count() == 3 && columns_match_layout(*e));
        e->set_curve(1, I, 2.5f);
        CHECK(e->stage(1).ramp.type == I && e->stage(1).ramp.power == 2.5f && columns_match_layout(*e));
        e->set_curve(9, S, 1.0f);                 // out of range: nothing moves
        CHECK(columns_match_layout(*e));
        e->replace_stages(std::move(*Envelope::make_asr(0.1f, 0.5f, 0.2f)));
        CHECK(e->stage_count() == 3 && columns_match_layout(*e));
        e->replace_stages(std::move(*Envelope::make_adsr(0.1f, 0.2f, 0.6f, 0.25f)));
        e->set_setting("sustainLevel", 0.35f);
        CHECK(same_bits(e->stage(2).ramp.startVal, 0.35f) && columns_match_layout(*e));
        for (const auto& f : {Envelope::make_ar(0.2f), Envelope::make_as(0.3f), Envelope::make_asr(0.15f, 0.4f, 0.3f),
                              Envelope::make_ads(0.1f, 0.25f, 0.3f), Envelope::make_adr(0.1f, 0.2f, 0.55f),
                              Envelope::make_adsr(0.1f, 0.2f, 0.6f, 0.25f),
                              Envelope::make_adsr_abs(0.01f, 0.05f, 0.6f, 0.02f)})
            CHECK(columns_match_layout(*f));
        auto shaped = envelope_from_preset_json(J::parse(R"({"preset":"adsr","attackCurve":1,"attackPower":0.8,"releaseCurve":2,"releasePower":1.5})"));
        CHECK(shaped->stage(0).ramp.type == E && shaped->stage(3).ramp.type == I && columns_match_layout(*shaped));
    }

    // (3) The order fact: sustainLevel set before the columns leaves the
    // table as written; set after, it rewrites the four slots.
    {
        const auto slots09 = rows_of(*Envelope::make_adsr(0.1f, 0.2f, 0.9f, 0.25f));
        auto slots_are = [](const Envelope& e, float v) {
            return same_bits(e.stage(1).ramp.endVal, v) && same_bits(e.stage(2).ramp.startVal, v)
                && same_bits(e.stage(2).ramp.endVal, v) && same_bits(e.stage(3).ramp.startVal, v);
        };
        Envelope before;
        before.set_setting("sustainLevel", 0.3f);
        for (int c = 0; c < 8; ++c) {
            std::vector<float> col;
            for (const Stage& s : slots09) col.push_back(stage_field(s, c));
            before.set_array(kStageColumns[c], col);
        }
        CHECK(slots_are(before, 0.9f));
        CHECK(same_bits(before.get_setting("sustainLevel"), 0.9f));
        Envelope after;
        for (int c = 0; c < 8; ++c) {
            std::vector<float> col;
            for (const Stage& s : slots09) col.push_back(stage_field(s, c));
            after.set_array(kStageColumns[c], col);
        }
        after.set_setting("sustainLevel", 0.3f);
        CHECK(slots_are(after, 0.3f) && columns_match_layout(after));
        CHECK(after.get_array("endVal")[1] == 0.3f && after.get_array("startVal")[2] == 0.3f
              && after.get_array("endVal")[2] == 0.3f && after.get_array("startVal")[3] == 0.3f);
        auto three = by_columns(rows_of(*Envelope::make_asr(0.15f, 0.4f, 0.3f)), 0.0f, false);
        const auto threeRows = rows_of(*three);
        three->set_setting("sustainLevel", 0.3f);
        CHECK(same_layout(*three, *by_add(threeRows, 0.0f)) && columns_match_layout(*three));
    }

    // (4) timeMode is the one state behind absolute_time().
    {
        Envelope e;
        CHECK(!e.absolute_time());
        e.set_setting("timeMode", 1.0f);
        CHECK(e.absolute_time());
        e.set_setting("timeMode", 0.0f);
        CHECK(!e.absolute_time());
        auto abs = Envelope::make_adsr_abs(0.01f, 0.05f, 0.6f, 0.02f);
        CHECK(abs->absolute_time() && abs->get_setting("timeMode") == 1.0f);
        CHECK(!Envelope::make_adsr(0.1f, 0.2f, 0.6f, 0.25f)->absolute_time());
    }

    // (5) The mapping applies always: with nothing wired 0 + (1 - 0) * raw is
    // raw, bit for bit, but for a raw -0.0, which leaves as +0.0.
    {
        auto hold_at = [&](float v, bool wired) {
            Envelope e;
            e.add_stage({{v, v, H, 0.0f}, 0.0f, 0.0f, 0.0f});
            if (wired) {
                e.set_param("minValue", std::make_shared<ConstantSource>(0.25f));
                e.set_param("maxValue", std::make_shared<ConstantSource>(0.75f));
            }
            e.prepare(RenderContext{48000});
            e.start(4800);
            return e.next();
        };
        const float denormal = std::numeric_limits<float>::denorm_min() * 7.0f;
        for (float v : {0.0f, 1.0f, 0.5f, -0.3f, -1.0f, -250.0f, 1.25f, 2.6f, 40000.0f, denormal, -denormal,
                        0.1f, 0.7f, 1e-30f})
        {
            CHECK(same_bits(hold_at(v, false), v));
            const float lo = 0.25f, hi = 0.75f;
            CHECK(same_bits(hold_at(v, true), lo + (hi - lo) * v));
        }
        const float negZero = hold_at(-0.0f, false);
        CHECK(negZero == 0.0f && !std::signbit(negZero));
    }

    // (6) Both adsr factories have the four-slot shape by the invariant alone.
    for (float s : {0.0f, 0.3f, 1.0f, 2.6f})
        for (float d : {0.0f, 0.2f})
            for (float r : {0.0f, 0.25f})
                for (const auto& e : {Envelope::make_adsr(0.1f, d, s, r), Envelope::make_adsr_abs(0.01f, d * 0.1f, s, r * 0.1f)}) {
                    CHECK(e->has_adsr_shape_());
                    CHECK(same_bits(e->get_setting("sustainLevel"), s));
                    e->set_setting("sustainLevel", 0.45f);
                    CHECK(same_bits(e->get_setting("sustainLevel"), 0.45f));
                    CHECK(same_bits(e->stage(1).ramp.endVal, 0.45f) && same_bits(e->stage(2).ramp.startVal, 0.45f)
                          && same_bits(e->stage(2).ramp.endVal, 0.45f) && same_bits(e->stage(3).ramp.startVal, 0.45f));
                    CHECK(columns_match_layout(*e));
                }

    // (7) The load: the table as columns; labels exactly (another case
    // refused) and indices; the refusals; an empty table; timeMode on either form.
    {
        auto patch = [](const std::string& envParams) {
            return std::string(R"({"sampleRate":48000,"instrument":{"polyphony":1},"graph":{"output":"osc","nodes":[
                {"id":"Env","type":"Envelope","params":{)") + envParams + R"(}},
                {"id":"osc","type":"SineSource","params":{"frequency":220.0,"amplitude":{"ref":"Env"}}}]}})";
        };
        auto env_of = [](const std::string& json) {
            auto ip = load_inst(json);
            auto* pi = dynamic_cast<PitchedInstrument*>(ip.instrument.get());
            return std::dynamic_pointer_cast<Envelope>(pi->voicePool.at(0).nodesById.at("Env"));
        };
        auto refusal = [](const std::string& json) {
            return load_refused(json);
        };
        const std::string table = R"("startVal":[0.0,1.0,0.2],"endVal":[1.0,0.2,0.0],"power":[2.0,0.0,0.0],)"
                                  R"("type":["Expo","Sine","Hold"],"percent":[0.3,0.0,0.2],"minSec":[0.0,0.0,0.0],)"
                                  R"("maxSec":[1.0,0.0,0.0],"nominal":[0.0,0.0,0.1])";
        const auto fromTable = env_of(patch(table));
        const std::string cased = refusal(patch(R"("startVal":[0.0,1.0,0.2],"endVal":[1.0,0.2,0.0],"power":[2.0,0.0,0.0],)"
                                                R"("type":["expo","SINE","Hold"],"percent":[0.3,0.0,0.2],"minSec":[0.0,0.0,0.0],)"
                                                R"("maxSec":[1.0,0.0,0.0],"nominal":[0.0,0.0,0.1])"));
        CHECK(cased.find(R"('type' holds "expo", which is not one of its labels)") != std::string::npos);
        CHECK(fromTable && fromTable->stage_count() == 3);
        CHECK(fromTable->stage(0).ramp.type == E && fromTable->stage(1).ramp.type == S && fromTable->stage(2).ramp.type == H);
        const std::string indices = R"("startVal":[0.0,1.0,0.2],"endVal":[1.0,0.2,0.0],"power":[2.0,0.0,0.0],)"
                                    R"("type":[1,3,4],"percent":[0.3,0.0,0.2],"minSec":[0.0,0.0,0.0],)"
                                    R"("maxSec":[1.0,0.0,0.0],"nominal":[0.0,0.0,0.1])";
        CHECK(same_layout(*fromTable, *env_of(patch(indices))));

        const std::string unknown = refusal(patch(R"("startVal":[0.0],"endVal":[1.0],"power":[0.0],"type":["Squiggle"],)"
                                                  R"("percent":[0.0],"minSec":[0.0],"maxSec":[0.0],"nominal":[0.0])"));
        CHECK(unknown.find("node 'Env' (Envelope): 'type' holds \"Squiggle\"") != std::string::npos);
        const std::string presetAndColumn = refusal(patch(R"("preset":"ar","type":["Linear"])"));
        CHECK(presetAndColumn.find("node 'Env' (Envelope): holds both 'preset' and 'type'") != std::string::npos);
        const std::string ragged = refusal(patch(R"("startVal":[0.0],"endVal":[1.0],"power":[0.0],"type":["Linear"],)"
                                                 R"("percent":[0.0],"minSec":[0.0],"maxSec":[0.0])"));
        CHECK(ragged.find("node 'Env' (Envelope): table 'stages' takes its columns at one length, all present or all absent")
              != std::string::npos);
        CHECK(ragged.find("nominal absent") != std::string::npos);

        const auto empty = env_of(patch(R"("startVal":[],"endVal":[],"power":[],"type":[],"percent":[],"minSec":[],"maxSec":[],"nominal":[])"));
        CHECK(empty && empty->stage_count() == 0);
        CHECK(env_of(patch(table + R"(,"timeMode":"seconds")"))->absolute_time());
        CHECK(!env_of(patch(table))->absolute_time());
        CHECK(env_of(patch(R"("preset":"adsr","timeMode":"seconds")"))->absolute_time());

        // The order of a load: the settings pass runs before the array pass,
        // so a sustainLevel beside a four-slot table leaves the table as the
        // file wrote it.
        const std::string slots09 = R"("startVal":[0.0,1.0,0.9,0.9],"endVal":[1.0,0.9,0.9,0.0],"power":[0.0,0.0,0.0,0.0],)"
                                    R"("type":["Linear","Linear","Linear","Sine"],"percent":[0.2,0.1,0.0,0.2],)"
                                    R"("minSec":[0.0,0.0,0.0,0.0],"maxSec":[1.0,0.5,0.0,0.0],"nominal":[0.0,0.0,0.0,0.0],)"
                                    R"("sustainLevel":0.3)";
        const auto ordered = env_of(patch(slots09));
        const std::vector<float> start = ordered->get_array("startVal"), end = ordered->get_array("endVal");
        CHECK(start.size() == 4 && end.size() == 4);
        if (start.size() == 4 && end.size() == 4)
            CHECK(end[1] == 0.9f && start[2] == 0.9f && end[2] == 0.9f && start[3] == 0.9f);
        CHECK(ordered->get_setting("sustainLevel") == 0.9f);
    }

    // (8) A rebuild sizes the layout by the shortest column and never writes
    // a column: startVal at three cells and endVal at two leave no stage
    // while the default's six other columns are empty, and two stages once
    // those six hold two cells each; neither startVal nor endVal is resized.
    {
        Envelope e;
        e.set_array("startVal", {0.0f, 1.0f, 2.0f});
        e.set_array("endVal", {1.0f, 0.0f});
        CHECK(e.stage_count() == 0);
        for (const char* c : {"power", "type", "percent", "minSec", "maxSec", "nominal"})
            e.set_array(c, {0.0f, 0.0f});
        CHECK(e.stage_count() == 2);
        CHECK(e.get_array("startVal").size() == 3);
        CHECK(e.get_array("endVal").size() == 2);
    }

    // (9) A table set shorter under a sounding note keeps the walk inside the
    // layout. The note is in its last stage (the adsr's release, from 0.8)
    // when the table becomes two Sine rows between -1 and -3, which stay in
    // their range at any position; past the last stage the walk reads 0. So
    // every value must be finite and in [-3, 0]. What this can see: a walk
    // that reads a stage past the layout through the cells the shrink left in
    // the vectors' capacity, the old release's positive values. What it
    // cannot: a read past the end whose value happens to fall in the range.
    {
        auto e = Envelope::make_adsr(0.1f, 0.1f, 0.8f, 0.2f);
        e->prepare(RenderContext{48000});
        e->start(4800);
        for (int i = 0; i < 3500; ++i) e->next();
        e->set_array("startVal", {-1.0f, -2.0f});
        e->set_array("endVal", {-2.0f, -3.0f});
        e->set_array("power", {0.0f, 0.0f});
        e->set_array("type", {float(int(S)), float(int(S))});
        e->set_array("percent", {0.2f, 0.2f});
        e->set_array("minSec", {0.0f, 0.0f});
        e->set_array("maxSec", {0.0f, 0.0f});
        e->set_array("nominal", {0.0f, 0.0f});
        CHECK(e->stage_count() == 2);
        int outside = 0;
        for (int i = 3500; i < 4800; ++i) {
            const float v = e->next();
            if (!std::isfinite(v) || v < -3.0f || v > 0.0f) ++outside;
        }
        if (outside) std::printf("  shorter table under a note: %d of 1300 values outside [-3, 0]\n", outside);
        CHECK(outside == 0);
    }
}

// The curves of the Curve's twin test, each under one interp in one mode, and
// the grid of x it is read over: below the first knot, at each knot, between,
// above the last, 0 and a negative x.
struct CurveCase {
    const char* name;
    int interp;                                    // CurveNode::CurveInterp
    bool expr;                                     // expressions mode
    CurveKnots pts;
    CurveRows rows;
};
static std::vector<CurveCase> curve_cases() {
    using KF = CurveNode::KnotForm;
    const CurveKnots p2 = {{100.0f, 0.5f}, {1000.0f, 2.0f}};
    const CurveKnots p5 = {{20.0f, 0.1f}, {200.0f, 0.8f}, {1000.0f, 0.3f}, {5000.0f, 1.5f}, {16000.0f, 4.0f}};
    const CurveKnots p1 = {{440.0f, 3.0f}};
    const CurveKnots pz = {{100.0f, 0.0f}, {1000.0f, 1.0f}, {10000.0f, 4.0f}};
    const CurveRows e1 = {{440.0f, KF::Linear, 8.0f, 0.0f}};
    const CurveRows e4 = {{0.0f, KF::Linear, 0.0f, 0.1f},
                          {200.0f, KF::Power, 0.01f, 1.5f},
                          {2000.0f, KF::Linear, -2e-5f, 0.5f},
                          {12000.0f, KF::Power, 3.0f, 0.25f}};
    static const char* const in[] = {"linear", "logx", "loglog"};
    std::vector<CurveCase> cases;
    for (int i = 0; i < 3; ++i) {
        cases.push_back({in[i], i, false, p2, {}});
        cases.push_back({in[i], i, false, p5, {}});
        cases.push_back({in[i], i, false, p1, {}});
        cases.push_back({in[i], i, false, {}, {}});
    }
    cases.push_back({"loglog, a y of 0", 2, false, pz, {}});
    cases.push_back({"linear", 0, true, {}, e1});
    for (int i = 0; i < 3; ++i) cases.push_back({in[i], i, true, {}, e4});
    cases.push_back({"linear", 0, true, {}, {}});
    return cases;
}
static constexpr float kCurveGrid[] = {-50.0f, 0.0f, 10.0f, 20.0f, 50.0f, 100.0f, 150.0f, 200.0f, 440.0f,
                                       500.0f, 1000.0f, 1500.0f, 2000.0f, 5000.0f, 7500.0f, 10000.0f,
                                       12000.0f, 16000.0f, 20000.0f};
// map(x) over kCurveGrid for each of curve_cases(), as float bits, recorded
// from the node that held its knots as public vectors.
static constexpr uint32_t kCurveBaseMap[][std::size(kCurveGrid)] = {
    {0x3F000000u, 0x3F000000u, 0x3F000000u, 0x3F000000u, 0x3F000000u, 0x3F000000u, 0x3F155555u, 0x3F2AAAABu, 0x3F888888u, 0x3F955556u, 0x40000000u, 0x40000000u, 0x40000000u, 0x40000000u, 0x40000000u, 0x40000000u, 0x40000000u, 0x40000000u, 0x40000000u},  // 0: points, linear, 2 knots, 0 rows
    {0x3DCCCCCDu, 0x3DCCCCCDu, 0x3DCCCCCDu, 0x3DCCCCCDu, 0x3E5DDDDEu, 0x3ED27D28u, 0x3F1B05B0u, 0x3F4CCCCDu, 0x3F266666u, 0x3F1CCCCDu, 0x3E99999Au, 0x3EE66667u, 0x3F19999Au, 0x3FC00000u, 0x40045D17u, 0x4028BA2Eu, 0x4045D174u, 0x40800000u, 0x40800000u},  // 1: points, linear, 5 knots, 0 rows
    {0x40400000u, 0x40400000u, 0x40400000u, 0x40400000u, 0x40400000u, 0x40400000u, 0x40400000u, 0x40400000u, 0x40400000u, 0x40400000u, 0x40400000u, 0x40400000u, 0x40400000u, 0x40400000u, 0x40400000u, 0x40400000u, 0x40400000u, 0x40400000u, 0x40400000u},  // 2: points, linear, 1 knots, 0 rows
    {0xC2480000u, 0x00000000u, 0x41200000u, 0x41A00000u, 0x42480000u, 0x42C80000u, 0x43160000u, 0x43480000u, 0x43DC0000u, 0x43FA0000u, 0x447A0000u, 0x44BB8000u, 0x44FA0000u, 0x459C4000u, 0x45EA6000u, 0x461C4000u, 0x463B8000u, 0x467A0000u, 0x469C4000u},  // 3: points, linear, 0 knots, 0 rows
    {0x3F000000u, 0x3F000000u, 0x3F000000u, 0x3F000000u, 0x3F000000u, 0x3F000000u, 0x3F439E7Au, 0x3F739874u, 0x3FBB8AFDu, 0x3FC633C6u, 0x40000000u, 0x40000000u, 0x40000000u, 0x40000000u, 0x40000000u, 0x40000000u, 0x40000000u, 0x40000000u, 0x40000000u},  // 4: points, logx, 2 knots, 0 rows
    {0x3DCCCCCDu, 0x3DCCCCCDu, 0x3DCCCCCDu, 0x3DCCCCCDu, 0x3EC1D25Bu, 0x3F16DAFDu, 0x3F366936u, 0x3F4CCCCDu, 0x3F0E17E2u, 0x3F03ED36u, 0x3E99999Au, 0x3F1A315Au, 0x3F511A96u, 0x3FC00000u, 0x4017C654u, 0x403F58F8u, 0x40586D5Eu, 0x40800000u, 0x40800000u},  // 5: points, logx, 5 knots, 0 rows
    {0x40400000u, 0x40400000u, 0x40400000u, 0x40400000u, 0x40400000u, 0x40400000u, 0x40400000u, 0x40400000u, 0x40400000u, 0x40400000u, 0x40400000u, 0x40400000u, 0x40400000u, 0x40400000u, 0x40400000u, 0x40400000u, 0x40400000u, 0x40400000u, 0x40400000u},  // 6: points, logx, 1 knots, 0 rows
    {0xC2480000u, 0x00000000u, 0x41200000u, 0x41A00000u, 0x42480000u, 0x42C80000u, 0x43160000u, 0x43480000u, 0x43DC0000u, 0x43FA0000u, 0x447A0000u, 0x44BB8000u, 0x44FA0000u, 0x459C4000u, 0x45EA6000u, 0x461C4000u, 0x463B8000u, 0x467A0000u, 0x469C4000u},  // 7: points, logx, 0 knots, 0 rows
    {0x3F000000u, 0x3F000000u, 0x3F000000u, 0x3F000000u, 0x3F000000u, 0x3F000000u, 0x3F236408u, 0x3F4249F8u, 0x3F9C29A2u, 0x3FA8A7F1u, 0x40000000u, 0x40000000u, 0x40000000u, 0x40000000u, 0x40000000u, 0x40000000u, 0x40000000u, 0x40000000u, 0x40000000u},  // 8: points, loglog, 2 knots, 0 rows
    {0x3DCCCCCDu, 0x3DCCCCCDu, 0x3DCCCCCDu, 0x3DCCCCCDu, 0x3E6A3F73u, 0x3EDB0793u, 0x3F1DF149u, 0x3F4CCCCDu, 0x3EFD536Bu, 0x3EEA56FAu, 0x3E99999Au, 0x3EE66666u, 0x3F19999Au, 0x3FC00000u, 0x400721FAu, 0x402C3B8Du, 0x4048DB2Cu, 0x40800000u, 0x40800000u},  // 9: points, loglog, 5 knots, 0 rows
    {0x40400000u, 0x40400000u, 0x40400000u, 0x40400000u, 0x40400000u, 0x40400000u, 0x40400000u, 0x40400000u, 0x40400000u, 0x40400000u, 0x40400000u, 0x40400000u, 0x40400000u, 0x40400000u, 0x40400000u, 0x40400000u, 0x40400000u, 0x40400000u, 0x40400000u},  // 10: points, loglog, 1 knots, 0 rows
    {0xC2480000u, 0x00000000u, 0x41200000u, 0x41A00000u, 0x42480000u, 0x42C80000u, 0x43160000u, 0x43480000u, 0x43DC0000u, 0x43FA0000u, 0x447A0000u, 0x44BB8000u, 0x44FA0000u, 0x459C4000u, 0x45EA6000u, 0x461C4000u, 0x463B8000u, 0x467A0000u, 0x469C4000u},  // 11: points, loglog, 0 knots, 0 rows
    {0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x3E345144u, 0x3E9A209Au, 0x3F24B951u, 0x3F32EFB3u, 0x3F800000u, 0x3FA36408u, 0x3FC249F8u, 0x4028A7F1u, 0x405749ABu, 0x40800000u, 0x40800000u, 0x40800000u, 0x40800000u},  // 12: points, loglog, a y of 0, 3 knots, 0 rows
    {0xC3C80000u, 0x00000000u, 0x42A00000u, 0x43200000u, 0x43C80000u, 0x44480000u, 0x44960000u, 0x44C80000u, 0x455C0000u, 0x457A0000u, 0x45FA0000u, 0x463B8000u, 0x467A0000u, 0x471C4000u, 0x476A6000u, 0x479C4000u, 0x47BB8000u, 0x47FA0000u, 0x481C4000u},  // 13: expressions, linear, 0 knots, 1 rows
    {0x3DCCCCCDu, 0x3DCCCCCDu, 0x3DE2F115u, 0x3E37BFD5u, 0x3F757964u, 0x40A19999u, 0x415CDAA4u, 0x41E24630u, 0x42A01BFAu, 0x42BA8098u, 0x432FE53Au, 0x4321B6B8u, 0x3EEB8800u, 0x40FB2360u, 0x4178331Eu, 0x41C07AE1u, 0x41FB3144u, 0x4206F640u, 0x420EB471u},  // 14: expressions, linear, 0 knots, 4 rows
    {0x3DCCCCCDu, 0x3DCCCCCDu, 0x3DE2F115u, 0x3E37BFD5u, 0x3F757964u, 0x40A19999u, 0x415CDAA4u, 0x41E24630u, 0x4273700Au, 0x428703C2u, 0x42BF0F20u, 0x4291FCFCu, 0x3EEB8800u, 0x41518A4Cu, 0x41A57E3Au, 0x41D7D2A7u, 0x41FB3144u, 0x4206F640u, 0x420EB471u},  // 15: expressions, logx, 0 knots, 4 rows
    {0x3DCCCCCDu, 0x3DCCCCCDu, 0x3DD8EF67u, 0x3DFEF7A0u, 0x3E79B27Du, 0x3F800000u, 0x409FAE47u, 0x41E24630u, 0x4175D7ADu, 0x414E1FD9u, 0x4058BF1Bu, 0x3F926D7Eu, 0x3EEB851Fu, 0x40552143u, 0x410D9F69u, 0x419635DCu, 0x41FB3144u, 0x4206F640u, 0x420EB471u},  // 16: expressions, loglog, 0 knots, 4 rows
    {0xC2480000u, 0x00000000u, 0x41200000u, 0x41A00000u, 0x42480000u, 0x42C80000u, 0x43160000u, 0x43480000u, 0x43DC0000u, 0x43FA0000u, 0x447A0000u, 0x44BB8000u, 0x44FA0000u, 0x459C4000u, 0x45EA6000u, 0x461C4000u, 0x463B8000u, 0x467A0000u, 0x469C4000u},  // 17: expressions, linear, 0 knots, 0 rows
};

static void run_curve_table_tests() {
    std::printf("the Curve's tables: the twin for the knot lookup, the columns, interp and mode, the load\n");
    using KF = CurveNode::KnotForm;
    static const char* const kColumns[] = {"x", "y", "exprX", "exprForm", "exprA", "exprB"};
    auto expr_cell = [](const CurveNode::ExprKnot& e, int c) {
        return c == 0 ? e.x : c == 1 ? float(int(e.form)) : c == 2 ? e.a : e.b;
    };
    // Column c of a case's rows: 0 and 1 the knots, 2 to 5 the expression rows.
    auto column = [&](const CurveCase& k, int c) {
        std::vector<float> col;
        if (c < 2) for (const auto& p : k.pts) col.push_back(c == 0 ? p.first : p.second);
        else       for (const auto& e : k.rows) col.push_back(expr_cell(e, c - 2));
        return col;
    };
    auto by_setters = [](const CurveCase& k) {
        auto n = std::make_shared<CurveNode>();
        n->set_setting("interp", float(k.interp));
        n->set_setting("mode", k.expr ? 1.0f : 0.0f);
        n->set_knots(k.pts);
        n->set_expr_knots(k.rows);
        return n;
    };
    // The same curve written as the six columns, in declaration order or reversed.
    auto by_columns = [&](const CurveCase& k, bool reverse) {
        auto n = std::make_shared<CurveNode>();
        n->set_setting("interp", float(k.interp));
        n->set_setting("mode", k.expr ? 1.0f : 0.0f);
        for (int j = 0; j < 6; ++j) {
            const int c = reverse ? 5 - j : j;
            n->set_array(kColumns[c], column(k, c));
        }
        return n;
    };
    auto same_knots = [](const CurveKnots& a, const CurveKnots& b) {
        if (a.size() != b.size()) return false;
        for (size_t i = 0; i < a.size(); ++i)
            if (!same_bits(a[i].first, b[i].first) || !same_bits(a[i].second, b[i].second)) return false;
        return true;
    };
    auto same_rows = [](const CurveRows& a, const CurveRows& b) {
        if (a.size() != b.size()) return false;
        for (size_t i = 0; i < a.size(); ++i)
            if (!same_bits(a[i].x, b[i].x) || a[i].form != b[i].form || !same_bits(a[i].a, b[i].a)
                || !same_bits(a[i].b, b[i].b))
                return false;
        return true;
    };
    auto same_map = [](const CurveNode& a, const CurveNode& b) {
        for (float x : kCurveGrid)
            if (!same_bits(a.map(x), b.map(x))) return false;
        return true;
    };

    // (1) The twin for the knot lookup: the setters and set_array give the
    // recorded map(x), bit for bit, and next() through a wired constant agrees.
    const auto cases = curve_cases();
    CHECK(cases.size() == std::size(kCurveBaseMap));
    for (size_t c = 0; c < cases.size() && c < std::size(kCurveBaseMap); ++c) {
        const auto a = by_setters(cases[c]), b = by_columns(cases[c], false), r = by_columns(cases[c], true);
        int bad = 0;
        for (size_t i = 0; i < std::size(kCurveGrid); ++i) {
            const float x = kCurveGrid[i];
            const float v = a->map(x);
            if (std::bit_cast<uint32_t>(v) != kCurveBaseMap[c][i]) ++bad;
            if (!same_bits(v, b->map(x)) || !same_bits(v, r->map(x))) ++bad;
            auto wired = by_columns(cases[c], false);
            wired->set_param("source", std::make_shared<ConstantSource>(x));
            wired->next();
            if (!same_bits(wired->current(), v)) ++bad;
        }
        if (bad) std::printf("  curve case %zu (%s, %s): %d of %zu differ\n", c,
                             cases[c].expr ? "expressions" : "points", cases[c].name, bad, std::size(kCurveGrid));
        CHECK(bad == 0);
    }

    // (2) The columns follow the setters, and the rows follow the columns.
    for (const CurveCase& k : cases) {
        const auto a = by_setters(k);
        bool ok = true;
        for (int c = 0; c < 6; ++c) {
            const std::vector<float> want = column(k, c), got = a->get_array(kColumns[c]);
            ok = ok && want.size() == got.size();
            for (size_t i = 0; ok && i < want.size(); ++i) ok = same_bits(want[i], got[i]);
        }
        CHECK(ok);
        CHECK(same_knots(a->knots(), k.pts) && same_rows(a->expr_knots(), k.rows));
        const auto b = by_columns(k, true);
        CHECK(same_knots(b->knots(), k.pts) && same_rows(b->expr_knots(), k.rows));
    }
    {
        // A form cell is truncated and clamped: 1 and above Power, below 1 Linear.
        CurveNode n;
        n.set_array("exprX", {0.0f, 1.0f, 2.0f, 3.0f, 4.0f, 5.0f});
        n.set_array("exprForm", {1.0f, 0.0f, 2.7f, -1.0f, 0.9f, 1.5f});
        n.set_array("exprA", {1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f});
        n.set_array("exprB", {2.0f, 2.0f, 2.0f, 2.0f, 2.0f, 2.0f});
        const auto& rows = n.expr_knots();
        CHECK(rows.size() == 6);
        if (rows.size() == 6)
            CHECK(rows[0].form == KF::Power && rows[1].form == KF::Linear && rows[2].form == KF::Power
                  && rows[3].form == KF::Linear && rows[4].form == KF::Linear && rows[5].form == KF::Power);
        // A group's rows are as many as its shortest column has cells.
        CurveNode m;
        m.set_array("x", {1.0f, 2.0f, 3.0f});
        m.set_array("y", {4.0f, 5.0f});
        CHECK(same_knots(m.knots(), CurveKnots{{1.0f, 4.0f}, {2.0f, 5.0f}}));
        m.set_array("exprX", {1.0f, 2.0f});
        CHECK(m.expr_knots().empty());
        // The setters replace the columns whole.
        m.set_knots(CurveKnots{{7.0f, 8.0f}});
        CHECK(m.get_array("x") == std::vector<float>{7.0f} && m.get_array("y") == std::vector<float>{8.0f});
        m.set_knots(CurveKnots{});
        CHECK(m.knots().empty() && m.get_array("x").empty() && m.get_array("y").empty());
    }

    // (3) interp and mode are settings; their labels are the words the files hold.
    {
        CurveNode n;
        CHECK(n.interp.value == int(CurveNode::CurveInterp::Linear) && n.mode.value == 0);
        n.set_setting("interp", 2.0f);
        CHECK(n.interp.value == int(CurveNode::CurveInterp::LogLog));
        n.set_setting("mode", 1.0f);
        CHECK(n.mode.value == 1 && n.get_setting("mode") == 1.0f);
        auto labels_are = [](const char* const* got, std::initializer_list<const char*> want) {
            if (!got) return false;
            size_t i = 0;
            for (const char* w : want) {
                if (!got[i] || std::strcmp(got[i], w) != 0) return false;
                ++i;
            }
            return got[i] == nullptr;
        };
        int found = 0;
        for (const auto& d : n.setting_descriptors()) {
            if (std::strcmp(d.name, "interp") == 0) { ++found; CHECK(labels_are(d.enum_labels, {"linear", "logx", "loglog"})); }
            if (std::strcmp(d.name, "mode") == 0)   { ++found; CHECK(labels_are(d.enum_labels, {"points", "expressions"})); }
        }
        CHECK(found == 2);
        std::string arrays;
        for (const auto& d : n.array_descriptors()) {
            arrays += std::string(arrays.empty() ? "" : " ") + d.name + "/" + (d.groupName ? d.groupName : "-");
            if (std::strcmp(d.name, "exprForm") == 0) CHECK(labels_are(d.enum_labels, {"linear", "power"}));
            else CHECK(d.enum_labels == nullptr);
        }
        CHECK(arrays == "x/knots y/knots exprX/exprKnots exprForm/exprKnots exprA/exprKnots exprB/exprKnots");

        // An interp outside the labels' range reads as linear in both modes.
        for (const CurveCase& k : cases) {
            if (k.interp != 0) continue;
            const auto linear = by_setters(k);
            auto five = by_setters(k);
            five->set_setting("interp", 5.0f);
            CHECK(five->interp.value == 5);
            CHECK(same_map(*five, *linear));
        }
    }

    // (4) The load: each table as its columns; labels exactly, another case refused; the refusals.
    {
        auto patch = [](const std::string& curveParams) {
            return std::string(R"({"sampleRate":48000,"instrument":{"polyphony":1},"graph":{"output":"osc","nodes":[
                {"id":"pf","type":"PerformNode","params":{"field":"frequency"}},
                {"id":"cv","type":"CurveNode","params":{"source":{"ref":"pf"},)") + curveParams + R"(}},
                {"id":"osc","type":"SineSource","params":{"frequency":220.0,"amplitude":{"ref":"cv"}}}]}})";
        };
        auto curve_of = [](const std::string& json) {
            auto ip = load_inst(json);
            auto* pi = dynamic_cast<PitchedInstrument*>(ip.instrument.get());
            return std::dynamic_pointer_cast<CurveNode>(pi->voicePool.at(0).nodesById.at("cv"));
        };
        auto refusal = [](const std::string& json) {
            return load_refused(json);
        };
        const std::string xy = R"("interp":"logx","x":[100.0,1000.0,5000.0],"y":[0.002,0.2,0.05])";
        const auto fromXY = curve_of(patch(xy));
        CHECK(fromXY && fromXY->knots().size() == 3);
        CHECK(same_knots(fromXY->knots(), CurveKnots{{100.0f, 0.002f}, {1000.0f, 0.2f}, {5000.0f, 0.05f}}));
        CHECK(fromXY->interp.value == int(CurveNode::CurveInterp::LogX) && fromXY->mode.value == 0);

        const std::string labels = R"("mode":"expressions","interp":"loglog","exprX":[0.0,200.0,2000.0,12000.0],)"
                                   R"("exprForm":["linear","power","linear","power"],"exprA":[0.0,0.0001,-2e-5,0.03],)"
                                   R"("exprB":[0.001,1.5,0.05,0.25])";
        const std::string indices = R"("mode":"expressions","interp":"loglog","exprX":[0.0,200.0,2000.0,12000.0],)"
                                    R"("exprForm":[0,1,0,1],"exprA":[0.0,0.0001,-2e-5,0.03],"exprB":[0.001,1.5,0.05,0.25])";
        const auto fromLabels = curve_of(patch(labels)), fromIndices = curve_of(patch(indices));
        CHECK(fromLabels && fromIndices && fromLabels->expr_knots().size() == 4);
        CHECK(same_rows(fromLabels->expr_knots(), fromIndices->expr_knots()));
        CHECK(same_map(*fromLabels, *fromIndices));
        CHECK(fromLabels->mode.value == 1 && fromLabels->interp.value == int(CurveNode::CurveInterp::LogLog));
        CHECK(fromLabels->expr_knots().size() == 4 && fromLabels->expr_knots()[2].form == KF::Linear
              && fromLabels->expr_knots()[1].form == KF::Power);

        // a column's label, interp and mode in another case are refused, and so is a word that matches no label
        const std::string casedForm = refusal(patch(R"("mode":"expressions","interp":"loglog","exprX":[0.0,200.0],)"
                                                    R"("exprForm":["linear","POWER"],"exprA":[0.0,0.0001],"exprB":[0.001,1.5])"));
        CHECK(casedForm.find(R"('exprForm' holds "POWER", which is not one of its labels)") != std::string::npos);
        const std::string cased = refusal(patch(R"("interp":"LogLog","mode":"EXPRESSIONS")"));
        CHECK(cased.find(R"('interp' holds "LogLog", which is not one of its labels)") != std::string::npos);
        const std::string unknownSetting = refusal(patch(R"("interp":"cubic","mode":"spline")"));
        CHECK(unknownSetting.find(R"('interp' holds "cubic", which is not one of its labels)") != std::string::npos);
        const auto absent = curve_of(patch(R"("x":[0.0,1.0],"y":[0.0,1.0])"));
        CHECK(absent->interp.value == 0 && absent->mode.value == 0);

        // A table missing a column is ragged.
        const std::string ragged = refusal(patch(R"("x":[100.0,1000.0])"));
        CHECK(ragged.find("node 'cv' (CurveNode): table 'knots' takes its columns at one length, all present or all absent")
              != std::string::npos);
        CHECK(ragged.find("y absent") != std::string::npos);
        // An exprForm label outside the two is refused.
        const std::string badForm = refusal(patch(R"("exprX":[0.0],"exprForm":["cubic"],"exprA":[1.0],"exprB":[0.0])"));
        CHECK(badForm.find("node 'cv' (CurveNode): 'exprForm' holds \"cubic\"") != std::string::npos);
    }
}

// A type this engine no longer has fails the load naming the tool that
// rewrites it; a name it never had does not.
static void run_retired_type_message_tests() {
    auto message_for = [](const char* type) {
        const std::string json = std::string(R"({"sampleRate":48000,"instrument":{"polyphony":1},"graph":{"output":"n","nodes":[
            {"id":"n","type":")") + type + R"(","params":{}}]}})";
        return load_refused(json);
    };
    for (const char* t : {"ASEnvelope", "ADSREnvelope", "KSPianoString"}) {
        const std::string msg = message_for(t);
        CHECK(msg.find(std::string("Unknown node type: ") + t) != std::string::npos);
        CHECK(msg.find("tools/convert_patch.py") != std::string::npos);
    }
    const std::string none = message_for("NoSuchType");
    CHECK(none.find("Unknown node type: NoSuchType") != std::string::npos);
    CHECK(none.find("convert_patch") == std::string::npos);
    CHECK(retired_type_hint("Envelope") == nullptr && retired_type_hint("ASREnvelope") != nullptr);
    CHECK(retired_type_hint("AdditiveSource2") != nullptr && retired_type_hint("BasicAdditiveSource") != nullptr);
    CHECK(std::string(retired_type_hint("AdditiveSource2")).find("author an AdditiveSource") != std::string::npos);
    for (const char* t : {"Formant", "FormantSpectrum"})
        CHECK(std::string(retired_type_hint(t)).find("rows of a FormantPeaks") != std::string::npos);
    CHECK(std::string(retired_type_hint("FixedSpectrum")).find("FormantBands or FormantPeaks") != std::string::npos);
    CHECK(message_for("FormantSpectrum").find("convert_patch.py") != std::string::npos);
    CHECK(std::string(retired_type_hint("BandSpectrum")).find("renamed FormantBands") != std::string::npos);
    CHECK(message_for("BandSpectrum").find("Unknown node type: BandSpectrum") != std::string::npos);
}

// The allocation probe: counts only while armed, keeps the first armed
// allocation's stack, and resets to nothing.
static void run_alloc_probe_tests() {
    alloc_probe::reset();
    { std::vector<int> v(100); (void)v; }                 // unarmed: not counted
    CHECK(alloc_probe::count() == 0);
    alloc_probe::arm();
    { std::vector<int> v(100); (void)v; }                 // one allocation of 400 bytes
    alloc_probe::disarm();
    CHECK(alloc_probe::count() == 1);
    CHECK(alloc_probe::bytes() >= 400);
    int depth = 0;
    const void* const* stack = alloc_probe::first_stack(depth);
    CHECK(stack != nullptr);
#ifdef _WIN32
    CHECK(depth > 0);
#endif
    { std::vector<int> w(10); (void)w; }                  // disarmed again: still 1
    CHECK(alloc_probe::count() == 1);
    alloc_probe::reset();
    CHECK(alloc_probe::count() == 0 && alloc_probe::bytes() == 0);
    alloc_probe::first_stack(depth);
    CHECK(depth == 0);
}

// Allocation-free start() and next(): one note through the real host's live
// path — a pool slot, prepare_voice_at (the note's Setup, then start on the
// voice and its tails), one pull per sample for the note's life — armed from
// note-on to the last sample, must count nothing. Returns the count.
static std::size_t allocations_in_one_note(const char* patchJson, float noteNumber = 60.0f) {
    auto ip = load_inst(patchJson);
    PitchedInstrument& inst = *ip.instrument;
    inst.release_voice(inst.acquire_voice());        // the slot table is sized on first use
    alloc_probe::reset();
    alloc_probe::arm();
    const int slot = std::max(0, inst.acquire_voice());
    PitchedInstrument::StreamingVoice sv = inst.prepare_voice_at(slot, noteNumber, 0.8f, 0.1f);
    for (int i = 0; i < sv.durSamples; ++i) {
        sv.tick();
        (void)sv.source->next();
        for (auto& a : sv.advanceList) a->next();
    }
    inst.release_voice(slot);
    alloc_probe::disarm();
    if (alloc_probe::count())
        std::printf("  [alloc] %zu allocation(s), %zu bytes, in one note of the patch beginning %.60s\n",
                    alloc_probe::count(), alloc_probe::bytes(), patchJson);
    return alloc_probe::count();
}

// One note through the live path, its samples returned: what a listener gets.
static std::vector<float> render_one_live_note(PitchedInstrument& inst, float noteNumber) {
    inst.release_voice(inst.acquire_voice());
    const int slot = std::max(0, inst.acquire_voice());
    PitchedInstrument::StreamingVoice sv = inst.prepare_voice_at(slot, noteNumber, 0.8f, 0.1f);
    std::vector<float> out(size_t(sv.durSamples), 0.0f);
    for (int i = 0; i < sv.durSamples; ++i) {
        sv.tick();
        out[size_t(i)] = sv.source->next() * sv.gain;
        for (auto& a : sv.advanceList) a->next();
    }
    inst.release_voice(slot);
    return out;
}

static void run_allocation_tests_partials() {
    // Partials: a plain FullPartials, and one with every per-partial feature on
    // (decay, bandwidth noise, motion, shimmer, trade, onset spread, an expand
    // rule) so every array the Partials' start sizes is exercised; two pitches on
    // the pool's two voices.
    const char* additive = R"({"sampleRate":48000,"instrument":{"polyphony":2},"graph":{"output":"add","nodes":[
      {"id":"parts","type":"FullPartials","params":{"maxPartials":16}},
      {"id":"add","type":"AdditiveSource","params":{"frequency":220.0,"amplitude":0.5,"partials":{"ref":"parts"},"formantWeight":0.0}}]}})";
    CHECK(allocations_in_one_note(additive) == 0);
    CHECK(allocations_in_one_note(additive, 72.0f) == 0);
    const char* everything = R"({"sampleRate":48000,"instrument":{"polyphony":2},"graph":{"output":"add","nodes":[
      {"id":"rule","type":"ExpandRule","params":{"count":1,"recurse":1,"spacing1":0.3,"spacing2":0.3}},
      {"id":"parts","type":"FullPartials","params":{"maxPartials":8,"decayRate":1.0,"bandwidthHz":30.0,
         "motionDepth1":10.0,"shimmerDepth1":0.2,"tradeDepth":0.2,"onsetSpread":0.1,"expandRule":{"ref":"rule"}}},
      {"id":"add","type":"AdditiveSource","params":{"frequency":220.0,"amplitude":0.5,"partials":{"ref":"parts"},"formantWeight":0.0}}]}})";
    CHECK(allocations_in_one_note(everything) == 0);
    CHECK(allocations_in_one_note(everything, 48.0f) == 0);
    // A second prepare after a note — the UI prepares every node again at
    // every Play — changes nothing a render can see: the expand rule starts
    // each note from the same originals (one voice, so the second note lands
    // on the voice the first note expanded).
    {
        const char* expanded = R"({"sampleRate":48000,"instrument":{"polyphony":1},"graph":{"output":"add","nodes":[
          {"id":"rule","type":"ExpandRule","params":{"count":1,"recurse":0,"spacing1":0.3,"spacing2":0.3}},
          {"id":"parts","type":"FullPartials","params":{"maxPartials":4,"expandRule":{"ref":"rule"}}},
          {"id":"add","type":"AdditiveSource","params":{"frequency":220.0,"amplitude":0.5,"partials":{"ref":"parts"},"formantWeight":0.0}}]}})";
        auto once = load_inst(expanded);
        (void)render_one_live_note(*once.instrument, 60.0f);
        const auto second = render_one_live_note(*once.instrument, 60.0f);
        auto again = load_inst(expanded);
        (void)render_one_live_note(*again.instrument, 60.0f);
        again.instrument->prepare(RenderContext{48000});
        const auto secondAfterPrepare = render_one_live_note(*again.instrument, 60.0f);
        CHECK(same_bits(second, secondAfterPrepare));
    }
    // An ExplicitPartials row edited after the first prepare (the UI's path:
    // set_array, then Play prepares again) sounds on the next note with an
    // expand rule: the originals the rule starts from follow every rebuild.
    {
        const char* rows = R"({"sampleRate":48000,"instrument":{"polyphony":1},"graph":{"output":"add","nodes":[
          {"id":"rule","type":"ExpandRule","params":{"count":1,"recurse":0,"spacing1":0.3,"spacing2":0.3}},
          {"id":"parts","type":"ExplicitPartials","params":{"mult1":[1.0,2.0,3.0,4.0],"ampl1":[1.0,0.5,0.25,0.125],"mult2":[1.0,2.0,3.0,4.0],"ampl2":[1.0,0.5,0.25,0.125],"evolve":false,
             "expandRule":{"ref":"rule"}}},
          {"id":"add","type":"AdditiveSource","params":{"frequency":220.0,"amplitude":0.5,"partials":{"ref":"parts"},"formantWeight":0.0}}]}})";
        const char* editedRows = R"({"sampleRate":48000,"instrument":{"polyphony":1},"graph":{"output":"add","nodes":[
          {"id":"rule","type":"ExpandRule","params":{"count":1,"recurse":0,"spacing1":0.3,"spacing2":0.3}},
          {"id":"parts","type":"ExplicitPartials","params":{"mult1":[1.0,2.0,3.0,4.0],"ampl1":[1.0,0.1,0.1,0.1],"mult2":[1.0,2.0,3.0,4.0],"ampl2":[1.0,0.1,0.1,0.1],"evolve":false,
             "expandRule":{"ref":"rule"}}},
          {"id":"add","type":"AdditiveSource","params":{"frequency":220.0,"amplitude":0.5,"partials":{"ref":"parts"},"formantWeight":0.0}}]}})";
        auto edited = load_inst(rows);
        (void)render_one_live_note(*edited.instrument, 60.0f);
        edited.instrument->voicePool[0].nodesById.at("parts")->set_array("ampl1", {1.0f, 0.1f, 0.1f, 0.1f});
        edited.instrument->prepare(RenderContext{48000});
        const auto afterEdit = render_one_live_note(*edited.instrument, 60.0f);
        auto loaded = load_inst(editedRows);
        (void)render_one_live_note(*loaded.instrument, 60.0f);
        const auto asLoaded = render_one_live_note(*loaded.instrument, 60.0f);
        CHECK(same_bits(afterEdit, asLoaded));
    }
}

// The frequency-dependent tables and their evolutions.
static void run_allocation_tests_tables() {
    // WavetableSource: the table is reserved at prepare for the floor frequency
    // (kMinTableHz); a note at the floor, below it, and at the pin's maximum
    // fit, and each evolution's buffers are reserved with it.
    const char* wavetable = R"({"sampleRate":48000,"instrument":{"polyphony":2},"graph":{"output":"src","nodes":[{"id":"__perf_frequency","type":"PerformNode","params":{"field":"frequency"}},
      {"id":"src","type":"WavetableSource","params":{"seed":12345,"frequency":{"ref":"__perf_frequency"},"amplitude":0.5}}]}})";
    CHECK(allocations_in_one_note(wavetable) == 0);
    CHECK(allocations_in_one_note(wavetable, 15.49f) == 0);     // 20 Hz: the floor's length
    CHECK(allocations_in_one_note(wavetable, 0.0f) == 0);       // 8 Hz: below the floor, the floor's length
    CHECK(allocations_in_one_note(wavetable, 135.08f) == 0);    // 20 kHz: the pin's maximum, a two-sample table
    const auto withEvolution = [](const char* evolutionNodes) {
        return std::string(R"({"sampleRate":48000,"instrument":{"polyphony":2},"graph":{"output":"src","nodes":[{"id":"__perf_frequency","type":"PerformNode","params":{"field":"frequency"}},)")
             + evolutionNodes
             + R"(,{"id":"src","type":"WavetableSource","params":{"seed":12345,"frequency":{"ref":"__perf_frequency"},"amplitude":0.5,"evolution":{"ref":"ev"}}}]}})";
    };
    const char* evolutions[] = {
        R"({"id":"ev","type":"HistogramEqualizeEvolution","params":{"bins":64,"rate":5e-05}})",
        R"({"id":"ev","type":"ReactionDiffusionEvolution","params":{"feed":0.0367,"kill":0.0649,"diffU":0.16,"diffV":0.08,"dt":1.0,"subSteps":1}})",
        R"({"id":"ev","type":"CellularAutomatonEvolution","params":{"rule":110,"threshold":0.0,"levelHi":0.6,"levelLo":-0.6,"stepEvery":12}})",
        R"({"id":"ev","type":"BezierPullEvolution","params":{"p0":-1.0,"p1":0.8,"p2":-0.8,"p3":1.0,"rate":5e-05}})",
        R"({"id":"ev","type":"SortErosionEvolution","params":{"swapsPerSample":4,"descending":0}})",
        R"({"id":"bow","type":"RedNoiseSource","params":{"amplitude":1.0,"frequency":350.0,"density":0.5,"smoothness":0.5,"rampVariation":0.2}},
      {"id":"ev","type":"BowedStringEvolution","params":{"bow":{"ref":"bow"},"bowPosition":0.14,"bowSpeed":0.3,"frictionGain":4.0,"tubeLoss":0.005}})",
        R"({"id":"ev","type":"BrassEvolution","params":{"brassiness":1.5,"breath":0.8,"tubeLoss":0.01,"lipTension":0.92,"lipFreqRatio":1.2,"lipQ":3.0}})",
        R"({"id":"ev","type":"ReedEvolution","params":{"breath":0.5,"reedStiffness":1.8,"tubeLoss":0.01,"loopFilter":0.35}})",
        R"({"id":"ev","type":"EKSEvolution","params":{"pickPosition":0.5,"pickDirection":0.3,"stiffness":0.1,"decayStretch":0.5,"drumBlend":1.0}})",
    };
    for (const char* ev : evolutions) {
        const std::string json = withEvolution(ev);
        CHECK(allocations_in_one_note(json.c_str()) == 0);
        CHECK(allocations_in_one_note(json.c_str(), 36.0f) == 0);   // a second, lower pitch on the other voice
    }
    // The pluck under a wavetable, its seed given on the node.
    const char* pluck = R"({"sampleRate":48000,"instrument":{"polyphony":2},"graph":{"output":"src","nodes":[{"id":"__perf_frequency","type":"PerformNode","params":{"field":"frequency"}},
      {"id":"ev","type":"PluckEvolution","params":{"muting":0.0,"seed":42}},
      {"id":"src","type":"WavetableSource","params":{"seed":12345,"frequency":{"ref":"__perf_frequency"},"amplitude":0.5,"evolution":{"ref":"ev"}}}]}})";
    CHECK(allocations_in_one_note(pluck) == 0);
    // The morph target: synthesised at prepare, resampled per note within the reserve.
    const char* target = R"({"sampleRate":48000,"instrument":{"polyphony":2},"graph":{"output":"src","nodes":[{"id":"__perf_frequency","type":"PerformNode","params":{"field":"frequency"}},
      {"id":"ev","type":"TargetEvolution","params":{"morphRate":0.0005,"holdCycles":5,"decayFactor":0.999,"targetPartials":[1.0,0.5,0.33,0.25],"targetLength":2048}},
      {"id":"src","type":"WavetableSource","params":{"seed":12345,"frequency":{"ref":"__perf_frequency"},"amplitude":0.5,"evolution":{"ref":"ev"}}}]}})";
    CHECK(allocations_in_one_note(target) == 0);
    CHECK(allocations_in_one_note(target, 36.0f) == 0);
}

// The remaining families: the hybrid table, the stage, segment and basic
// additive nodes, a Multiplex voice whose clones read the voice's pitch.
static void run_allocation_tests_nodes() {
    // HybridKSSource: the table is reserved at prepare for the floor, the
    // partial arrays sized with the targets; a short hold so the transition
    // (the DFT) happens inside the note's life.
    const char* hybrid = R"({"sampleRate":48000,"instrument":{"polyphony":2},"graph":{"output":"hks","nodes":[{"id":"__perf_frequency","type":"PerformNode","params":{"field":"frequency"}},
      {"id":"hks","type":"HybridKSSource","params":{"seed":12345,"frequency":{"ref":"__perf_frequency"},"amplitude":0.5,"holdCycles":2,"morphDuration":0.05,"numPartials":30,
         "targetPartials":[0.4,1.0,0.75,0.45,0.28,0.15,0.1,0.05,0.02,0.01]}}]}})";
    CHECK(allocations_in_one_note(hybrid) == 0);
    CHECK(allocations_in_one_note(hybrid, 15.49f) == 0);    // the floor
    CHECK(allocations_in_one_note(hybrid, 36.0f) == 0);     // a second pitch on the other voice

    // PhasedValueSource: the stage counters and boundaries are sized at prepare.
    const char* phased = R"({"sampleRate":48000,"instrument":{"polyphony":2},"graph":{"output":"phased","nodes":[{"id":"__perf_frequency","type":"PerformNode","params":{"field":"frequency"}},
      {"id":"saw","type":"SawSource","params":{"frequency":{"ref":"__perf_frequency"},"amplitude":1.0}},
      {"id":"pulse","type":"PulseSource","params":{"frequency":{"ref":"__perf_frequency"},"amplitude":1.0,"dutyCycle":0.3,"bend":0.4}},
      {"id":"sin","type":"SineSource","params":{"frequency":{"ref":"__perf_frequency"},"amplitude":1.0}},
      {"id":"phased","type":"PhasedValueSource","params":{"overlap":0.15,"amplitude":0.8,
        "sources":[{"ref":"saw"},{"ref":"pulse"},{"ref":"sin"}],
        "percent":[0.33,0.33,0.0],"minSec":[0.5,0.5,0.0],"maxSec":[3.0,3.0,0.0],"gain":[1.0,1.0,1.0]}}]}})";
    CHECK(allocations_in_one_note(phased) == 0);
    CHECK(allocations_in_one_note(phased, 48.0f) == 0);
    // SegmentSource: the segment list is reserved at prepare for the shape's size.
    const char* segment = R"({"sampleRate":48000,"instrument":{"polyphony":2},"graph":{"output":"seg","nodes":[
      {"id":"seg","type":"SegmentSource","params":{"values":[8.0,0.65,8.0,-0.54,8.0,0.74,9.0,0.2],"oneShot":false,"seed":42,"smoothness":0.5,"gap":0.0}}]}})";
    CHECK(allocations_in_one_note(segment) == 0);
    CHECK(allocations_in_one_note(segment, 48.0f) == 0);

    // A note on a Multiplex voice whose clones read the voice's pitch (each
    // clone's Note node wired to Var1.val) allocates nothing.
    const char* multiplex = R"({"sampleRate":48000,"instrument":{"polyphony":2},"graph":{"output":"Multiplex1","nodes":[
      {"id":"__perf_freq","type":"PerformNode","params":{"field":"frequency"}},
      {"id":"Var1","type":"VarSource","params":{"absolute":true,"val":{"ref":"__perf_freq"},"varPct":0.0}},
      {"id":"Triangle1","type":"TriangleSource","params":{"amplitude":0.5,"frequency":{"ref":"Var1"}}},
      {"id":"Multiplex1","type":"MultiplexSource","params":{"count":4,"source":{"ref":"Triangle1"}}}]}})";
    CHECK(allocations_in_one_note(multiplex) == 0);
    CHECK(allocations_in_one_note(multiplex, 48.0f) == 0);
}

// The paths no library or baselines patch drives: a repetition boundary, a
// held note's release, a drum hit.
static void run_allocation_tests_pins() {
    // A RepeatingSource boundary inside next() restarts its child — a
    // Partials-bearing one — and allocates nothing; the note's life holds
    // several repetitions.
    const char* repeating = R"({"sampleRate":48000,"instrument":{"polyphony":2},"graph":{"output":"rep","nodes":[{"id":"__perf_frequency","type":"PerformNode","params":{"field":"frequency"}},
      {"id":"parts","type":"FullPartials","params":{"maxPartials":8}},
      {"id":"add","type":"AdditiveSource","params":{"frequency":{"ref":"__perf_frequency"},"amplitude":0.5,"partials":{"ref":"parts"},"formantWeight":0.0}},
      {"id":"rep","type":"RepeatingSource","params":{"seed":77,"source":{"ref":"add"},"duration":0.05,"gapDuration":0.01}}]}})";
    CHECK(allocations_in_one_note(repeating) == 0);

    // A held note's release — the envelopes gated at note-on, released
    // mid-life with the releasing note's length as the reference, the way a
    // line's end does — re-lays the release stage in place and allocates
    // nothing.
    {
        const char* held = R"({"sampleRate":48000,"instrument":{"polyphony":2,"sustaining":true},"graph":{"output":"osc","nodes":[{"id":"__perf_frequency","type":"PerformNode","params":{"field":"frequency"}},
          {"id":"env","type":"Envelope","params":{"preset":"adsr","attack":0.05,"decay":0.1,"sustainLevel":0.8,"release":0.2}},
          {"id":"osc","type":"SineSource","params":{"frequency":{"ref":"__perf_frequency"},"amplitude":{"ref":"env"}}}]}})";
        auto ip = load_inst(held);
        PitchedInstrument& inst = *ip.instrument;
        inst.release_voice(inst.acquire_voice());
        const int slot = std::max(0, inst.acquire_voice());
        auto& vg = inst.voicePool[size_t(slot)];
        CHECK(!vg.allEnvelopes.empty());
        for (auto* e : vg.allEnvelopes) e->set_gated(true);
        alloc_probe::reset();
        alloc_probe::arm();
        PitchedInstrument::StreamingVoice sv = inst.prepare_voice_at(slot, 60.0f, 0.8f, 0.5f);
        const auto pull = [&](int n) {
            for (int i = 0; i < n; ++i) { sv.tick(); (void)sv.source->next(); for (auto& a : sv.advanceList) a->next(); }
        };
        pull(sv.durSamples / 2);
        for (auto* e : vg.allEnvelopes) e->gate_release(int(0.5f * 48000.0f));   // the releasing note's length: the release is re-laid against it
        pull(sv.durSamples / 2);
        alloc_probe::disarm();
        CHECK(alloc_probe::count() == 0);
        inst.release_voice(slot);
    }

    // The drum kit has no library or baselines patch: one hit's engine path
    // through a kit built by hand from a loaded voice — the drum source's
    // start and every next on the kit's clock. The kit adds only play_hit's
    // pre-render buffer, the offline host's, outside the arm.
    {
        const char* drum = R"({"sampleRate":48000,"instrument":{"polyphony":1},"graph":{"output":"osc","nodes":[
          {"id":"env","type":"Envelope","params":{"preset":"adsr","attack":0.001,"decay":0.1,"sustainLevel":0.0,"release":0.05}},
          {"id":"osc","type":"SineSource","params":{"frequency":180.0,"amplitude":{"ref":"env"}}}]}})";
        auto ip = load_inst(drum);
        DrumKit kit;
        DrumKit::DrumSource ds;
        ds.source = ip.instrument->voicePool[0].source;
        kit.sources.push_back(std::move(ds));
        kit.prepare(RenderContext{48000});
        auto& d = kit.sources[0];
        alloc_probe::reset();
        alloc_probe::arm();
        d.source->start(4800);
        for (int i = 0; i < 4800; ++i) { d.clock->tick(); (void)d.source->next(); }
        alloc_probe::disarm();
        CHECK(alloc_probe::count() == 0);
    }
}

// A structural setting — one the node sizes or reshapes itself by: every int
// and bool, and a float the node declares so — takes no per-note push. The
// pin and its descriptor say so, and one changed after a note is honoured by
// the next prepare.
static void run_structural_setting_tests() {
    std::printf("structural settings: the mark, and the rebuild at prepare\n");
    {
        FullPartials full;
        CHECK(full.structural_setting("maxPartials"));      // an int: structural by type
        CHECK(full.structural_setting("evenWeight1"));      // a float the node declares
        CHECK(!full.structural_setting("rolloff1"));        // a value setting
        CHECK(!full.structural_setting("noSuchSetting"));
        bool declared = false, plain = false;
        for (const auto& d : full.setting_descriptors()) {
            if (std::string_view(d.name) == "unitPO2")   declared = d.structural;
            if (std::string_view(d.name) == "decayRate") plain = !d.structural;
        }
        CHECK(declared);
        CHECK(plain);
        SequencePartials seq;
        CHECK(seq.structural_setting("incr1"));
        CHECK(seq.structural_setting("minMult2"));
        CHECK(!seq.structural_setting("rolloff2"));
    }
    // A shaping float set after a note (the UI's edit, then Play prepares
    // again) rebuilds the arrays at that prepare: the next note is the one a
    // patch loaded with the value renders.
    {
        const char* even = R"({"sampleRate":48000,"instrument":{"polyphony":1},"graph":{"output":"add","nodes":[
          {"id":"parts","type":"FullPartials","params":{"maxPartials":4}},
          {"id":"add","type":"AdditiveSource","params":{"frequency":220.0,"amplitude":0.5,"partials":{"ref":"parts"},"formantWeight":0.0}}]}})";
        const char* odd = R"({"sampleRate":48000,"instrument":{"polyphony":1},"graph":{"output":"add","nodes":[
          {"id":"parts","type":"FullPartials","params":{"maxPartials":4,"evenWeight1":0.0}},
          {"id":"add","type":"AdditiveSource","params":{"frequency":220.0,"amplitude":0.5,"partials":{"ref":"parts"},"formantWeight":0.0}}]}})";
        auto edited = load_inst(even);
        const auto before = render_one_live_note(*edited.instrument, 60.0f);
        edited.instrument->voicePool[0].nodesById.at("parts")->set_setting("evenWeight1", 0.0f);
        edited.instrument->prepare(RenderContext{48000});
        const auto after = render_one_live_note(*edited.instrument, 60.0f);
        auto loaded = load_inst(odd);
        (void)render_one_live_note(*loaded.instrument, 60.0f);
        const auto asLoaded = render_one_live_note(*loaded.instrument, 60.0f);
        CHECK(!same_bits(before, after));
        CHECK(same_bits(after, asLoaded));
    }
}

// A load refuses a per-note push into a structural setting — the wiring
// format's dynamic pin — naming the node, the setting and the reason; a value
// setting still takes one.
// A load that fails for the reason the needle names; another reason is
// printed, so a test that expected one refusal and got another says so.
static bool load_fails_naming(const char* patch, const char* needle) {
    const std::string msg = load_refused(patch);
    if (msg.empty()) return false;
    if (msg.find(needle) == std::string::npos)
        std::printf("  load failed for another reason: %s\n", msg.c_str());
    return msg.find(needle) != std::string::npos;
}

static void run_structural_push_refusal_tests() {
    std::printf("structural settings: the load refuses the wire\n");
    auto loads = [](const char* patch) {
        try { (void)load_inst(patch); return true; }
        catch (const std::exception& e) { std::printf("  load failed: %s\n", e.what()); return false; }
    };
    // The wiring format's dynamic pin into a shaping float (refused), an int
    // (refused: structural by type) and a value float (taken).
    const char* pinShaping = R"({"sampleRate":48000,"instrument":{"polyphony":1},"graph":{"output":"add","nodes":[
      {"id":"pf","type":"PerformNode","params":{"field":"frequency"}},
      {"id":"parts","type":"SequencePartials","params":{"maxPartials":4},"dynamicPins":{"incr1":{"ref":"pf"}}},
      {"id":"add","type":"AdditiveSource","params":{"frequency":{"ref":"pf"},"amplitude":0.5,"partials":{"ref":"parts"},"formantWeight":0.0}}]}})";
    CHECK(load_fails_naming(pinShaping, "graph.nodes[1].dynamicPins.incr1: node 'parts' (SequencePartials)"));
    CHECK(load_fails_naming(pinShaping, "structural"));
    const char* pinCount = R"({"sampleRate":48000,"instrument":{"polyphony":1},"graph":{"output":"add","nodes":[
      {"id":"pf","type":"PerformNode","params":{"field":"frequency"}},
      {"id":"parts","type":"FullPartials","params":{"maxPartials":4},"dynamicPins":{"maxPartials":{"ref":"pf"}}},
      {"id":"add","type":"AdditiveSource","params":{"frequency":{"ref":"pf"},"amplitude":0.5,"partials":{"ref":"parts"},"formantWeight":0.0}}]}})";
    CHECK(load_fails_naming(pinCount, "structural"));
    const char* pinValue = R"({"sampleRate":48000,"instrument":{"polyphony":1},"graph":{"output":"add","nodes":[
      {"id":"pf","type":"PerformNode","params":{"field":"frequency"}},
      {"id":"parts","type":"FullPartials","params":{"maxPartials":4},"dynamicPins":{"decayRate":{"ref":"pf"}}},
      {"id":"add","type":"AdditiveSource","params":{"frequency":{"ref":"pf"},"amplitude":0.5,"partials":{"ref":"parts"},"formantWeight":0.0}}]}})";
    CHECK(loads(pinValue));
}

// An instrument block that carries a paramMap is refused by name, through the
// text entry point and the file entry point alike, and the message names the
// converter that rewrites it, in the one sentence kParamMapRefusal holds; an
// empty map and a null one are refused too. The same document without the key
// loads.
static void run_param_map_refusal_tests() {
    std::printf("instrument.paramMap: refused by name, naming the converter\n");
    const char* withMap = R"({"sampleRate":48000,"instrument":{"polyphony":1,"paramMap":{"frequency":"osc.frequency"}},"graph":{"output":"osc","nodes":[
      {"id":"pf","type":"PerformNode","params":{"field":"frequency"}},
      {"id":"osc","type":"SineSource","params":{"amplitude":0.5}}]}})";
    const char* emptyMap = R"({"sampleRate":48000,"instrument":{"polyphony":1,"paramMap":{}},"graph":{"output":"osc","nodes":[
      {"id":"pf","type":"PerformNode","params":{"field":"frequency"}},
      {"id":"osc","type":"SineSource","params":{"frequency":{"ref":"pf"},"amplitude":0.5}}]}})";
    const char* nullMap = R"({"sampleRate":48000,"instrument":{"polyphony":1,"paramMap":null},"graph":{"output":"osc","nodes":[
      {"id":"pf","type":"PerformNode","params":{"field":"frequency"}},
      {"id":"osc","type":"SineSource","params":{"frequency":{"ref":"pf"},"amplitude":0.5}}]}})";
    const char* without = R"({"sampleRate":48000,"instrument":{"polyphony":1},"graph":{"output":"osc","nodes":[
      {"id":"pf","type":"PerformNode","params":{"field":"frequency"}},
      {"id":"osc","type":"SineSource","params":{"frequency":{"ref":"pf"},"amplitude":0.5}}]}})";
    CHECK(load_fails_naming(withMap, "paramMap"));
    CHECK(load_fails_naming(withMap, "tools/convert_patch.py"));
    CHECK(load_fails_naming(emptyMap, "paramMap"));
    CHECK(load_fails_naming(emptyMap, "tools/convert_patch.py"));
    CHECK(load_fails_naming(nullMap, "paramMap"));
    CHECK(load_fails_naming(nullMap, "tools/convert_patch.py"));
    {
        const std::string msg = load_refused(withMap);
        if (!refusal_says(msg, kParamMapRefusal)) std::printf("  paramMap: %s\n", msg.c_str());
        CHECK(refusal_says(msg, kParamMapRefusal));
    }
    {
        ScratchPatch file("renders/scratch/_param_map_refusal.json", withMap);
        std::string msg;
        try { (void)load_sound(file.path); }
        catch (const std::exception& e) { msg = e.what(); }
        CHECK(msg.find("paramMap") != std::string::npos);
        CHECK(msg.find("tools/convert_patch.py") != std::string::npos);
    }
    bool loaded = true;
    try { (void)load_inst(without); }
    catch (const std::exception& e) { loaded = false; std::printf("  load failed: %s\n", e.what()); }
    CHECK(loaded);
}

// A key of a table's row form — an Envelope's stages, a CurveNode's knots and
// exprKnots — fails the load through the text entry point and the file entry
// point alike, naming the node, the key and the converter: the message is the
// node's prefix and the sentence retired_key_refusal holds for the pair. The
// refusal is per type and key, and the table forms of the three load.
static void run_retired_key_refusal_tests() {
    std::printf("retired keys: an Envelope's stages, a CurveNode's knots and exprKnots refused by name\n");
    auto patch = [](const std::string& node) {
        return std::string(R"({"sampleRate":48000,"instrument":{"polyphony":1},"graph":{"output":"osc","nodes":[
      {"id":"pf","type":"PerformNode","params":{"field":"frequency"}},
      )") + node + R"(,
      {"id":"osc","type":"SineSource","params":{"frequency":{"ref":"pf"},"amplitude":{"ref":"n"}}}]}})";
    };
    auto text_refusal = [](const std::string& json) {
        return load_refused(json);
    };
    auto file_refusal = [](const char* path, const std::string& json) {
        ScratchPatch file(path, json);
        try { (void)load_sound(file.path); } catch (const std::exception& e) { return std::string(e.what()); }
        return std::string();
    };
    auto loads = [](const std::string& json) {
        try { (void)load_inst(json); return true; }
        catch (const std::exception& e) { std::printf("  load failed: %s\n", e.what()); return false; }
    };

    struct Retired { const char* type; const char* key; const char* file; std::string node; };
    const Retired retired[] = {
        {"Envelope", "stages", "renders/scratch/_retired_stages.json",
         R"({"id":"n","type":"Envelope","params":{"stages":[{"startVal":0.0,"endVal":1.0,"type":"Linear","percent":0.5},)"
         R"({"startVal":1.0,"endVal":0.0,"type":"Linear","percent":0.5}]}})"},
        {"CurveNode", "knots", "renders/scratch/_retired_knots.json",
         R"({"id":"n","type":"CurveNode","params":{"source":{"ref":"pf"},"knots":[[100.0,0.2],[1000.0,0.8]]}})"},
        {"CurveNode", "exprKnots", "renders/scratch/_retired_expr_knots.json",
         R"({"id":"n","type":"CurveNode","params":{"source":{"ref":"pf"},"mode":"expressions",)"
         R"("exprKnots":[{"x":440.0,"form":"linear","a":0.001,"b":0.0}]}})"},
    };
    for (const auto& r : retired) {
        const std::string sentence = retired_key_refusal(r.type, r.key);
        const std::string want = std::string("node 'n' (") + r.type + "): " + sentence;
        for (const std::string& msg : {text_refusal(patch(r.node)), file_refusal(r.file, patch(r.node))}) {
            if (!refusal_says(msg, want)) std::printf("  %s.%s: got \"%s\"\n", r.type, r.key, msg.c_str());
            CHECK(msg.find(std::string("node 'n' (") + r.type + ")") != std::string::npos);
            CHECK(msg.find(std::string("'") + r.key + "'") != std::string::npos);
            CHECK(msg.find("no longer read") != std::string::npos);
            CHECK(msg.find("python tools/convert_patch.py") != std::string::npos);
            CHECK(msg.size() >= sentence.size() && msg.compare(msg.size() - sentence.size(), sentence.size(), sentence) == 0);
            CHECK(refusal_says(msg, want));
        }
    }
    // The sentence for each retired pair, and none for any other pair.
    CHECK(retired_key_refusal("Envelope", "stages")
          == "'stages' is no longer read: python tools/convert_patch.py <patch> rewrites it as the eight stage columns");
    CHECK(retired_key_refusal("CurveNode", "knots")
          == "'knots' is no longer read: python tools/convert_patch.py <patch> rewrites it as the columns x and y");
    CHECK(retired_key_refusal("CurveNode", "exprKnots")
          == "'exprKnots' is no longer read: python tools/convert_patch.py <patch> rewrites it as the columns exprX, exprForm, exprA and exprB");
    CHECK(retired_key_refusal("Envelope", "knots").empty());
    CHECK(retired_key_refusal("CurveNode", "stages").empty());
    CHECK(retired_key_refusal("Envelope", "startVal").empty());

    // The table forms beside them load.
    CHECK(loads(patch(R"({"id":"n","type":"Envelope","params":{"startVal":[0.0,1.0],"endVal":[1.0,0.0],"power":[0.0,0.0],)"
                      R"("type":["Linear","Linear"],"percent":[0.5,0.5],"minSec":[0.0,0.0],"maxSec":[0.0,0.0],"nominal":[0.0,0.0]}})")));
    CHECK(loads(patch(R"({"id":"n","type":"CurveNode","params":{"source":{"ref":"pf"},"x":[100.0,1000.0],"y":[0.2,0.8]}})")));
    CHECK(loads(patch(R"({"id":"n","type":"CurveNode","params":{"source":{"ref":"pf"},"mode":"expressions",)"
                      R"("exprX":[440.0],"exprForm":["linear"],"exprA":[0.001],"exprB":[0.0]}})")));

    // A stage list beside a preset gets the retired key's sentence, not the
    // two forms'; a preset beside a stage column gets the two forms'.
    const std::string listAndPreset = text_refusal(patch(
        R"({"id":"n","type":"Envelope","params":{"preset":"adsr","stages":[{"startVal":0.0,"endVal":1.0,"type":"Linear","percent":0.5}]}})"));
    CHECK(refusal_says(listAndPreset, "node 'n' (Envelope): " + retired_key_refusal("Envelope", "stages")));
    CHECK(listAndPreset.find("one form") == std::string::npos);
    const std::string presetAndColumn = text_refusal(patch(R"({"id":"n","type":"Envelope","params":{"preset":"ar","type":["Linear"]}})"));
    CHECK(presetAndColumn.find("node 'n' (Envelope): holds both 'preset' and 'type'; an Envelope takes one form: "
                               "the stage columns or a preset") != std::string::npos);
}

// A dynamic pin: a setting the patch drives once per note, here an envelope's
// timeScale through a CurveNode reading the note's frequency. The setting takes
// the curve's value at the note's frequency at Setup; a held line's
// continuation leaves it at the line's first note; and the curve, which nothing
// but the binding reaches, reports no fault after a note.
static std::string dynamic_pin_patch_json(int polyphony) {
    return R"({"sampleRate":48000,"instrument":{"polyphony":)" + std::to_string(polyphony)
         + R"(,"sustaining":true,"glideMs":15.0},"graph":{"output":"sine1","nodes":[
      {"id":"pf","type":"PerformNode","params":{"field":"frequency"}},
      {"id":"ts","type":"CurveNode","params":{"source":{"ref":"pf"},"interp":"logx","x":[100.0,1000.0],"y":[0.5,2.0]}},
      {"id":"env1","type":"Envelope","params":{"startVal":[0.0,1.0,0.7,0.7],"endVal":[1.0,0.7,0.7,0.0],
        "power":[0.0,0.0,0.0,0.0],"type":["Linear","Linear","Linear","Linear"],"percent":[0.05,0.1,0.0,0.25],
        "minSec":[0.0,0.0,0.0,0.0],"maxSec":[0.0,0.0,0.0,0.0],"nominal":[0.0,0.0,0.0,0.0]},
       "dynamicPins":{"timeScale":{"ref":"ts"}}},
      {"id":"sine1","type":"SineSource","params":{"frequency":{"ref":"pf"},"amplitude":{"ref":"env1"},"phase":0.0}}]}})";
}

static void run_dynamic_pin_tests() {
    std::printf("dynamic pins: the note's value at Setup, held across a continuation, no fault\n");
    CurveNode curve; curve.interp.value = int(CurveNode::CurveInterp::LogX);
    curve.set_knots(CurveKnots{{100.0f, 0.5f}, {1000.0f, 2.0f}});
    const float at60 = curve.map(note_to_freq(60.0f));
    const float at72 = curve.map(note_to_freq(72.0f));
    CHECK(std::fabs(at60 - at72) > 0.1f);              // the two notes give two values
    auto timescale = [](PitchedInstrument& pi, size_t voice = 0) {
        return pi.voicePool[voice].nodesById.at("env1")->get_setting("timeScale");
    };
    // 1. Each fresh note sets the setting to the curve at its own frequency,
    //    on its own voice: with two voices, play_note's rotation hands the
    //    first fresh note voice 0 and the second voice 1.
    {
        auto ip = load_inst(dynamic_pin_patch_json(2));
        PitchedInstrument& pi = *ip.instrument;
        CHECK(pi.voicePool.size() == 2);
        pi.play_note({.start = 0.0f, .duration = 0.4f, .noteNumber = 60.0f, .velocity = 0.8f, .onset = 0, .hold = false});
        CHECK_NEAR(timescale(pi, 0), at60, 1e-5f);
        pi.play_note({.start = 1.0f, .duration = 0.4f, .noteNumber = 72.0f, .velocity = 0.8f, .onset = 0, .hold = false});
        CHECK_NEAR(timescale(pi, 1), at72, 1e-5f);
        CHECK_NEAR(timescale(pi, 0), at60, 1e-5f);     // the first voice keeps its note's value
        // 3. Each voice's curve was stepped at Setup with that voice's context:
        //    no fault on any voice (collect_faults walks the whole pool).
        std::vector<FaultReport> faults;
        pi.collect_faults(faults);
        bool curveFault = false;
        for (const auto& f : faults)
            if (f.nodeId == "ts") { curveFault = true; std::printf("  ts faults: %u\n", unsigned(f.faults)); }
        CHECK(!curveFault);
    }
    // 2. A held line: the second note continues the first, and the setting
    //    keeps the first note's value.
    {
        auto ip = load_inst(dynamic_pin_patch_json(1));
        PitchedInstrument& pi = *ip.instrument;
        pi.play_note({.start = 0.0f, .duration = 0.4f, .noteNumber = 60.0f, .velocity = 0.8f, .onset = 0, .hold = true});
        CHECK_NEAR(timescale(pi), at60, 1e-5f);
        pi.play_note({.start = 0.4f, .duration = 0.4f, .noteNumber = 72.0f, .velocity = 0.8f, .onset = 0, .hold = false});
        CHECK_NEAR(timescale(pi), at60, 1e-5f);
    }
}

// One wiring door for every pin kind: a Param or Input takes a signal and
// refuses a part, a Slot takes its interface and refuses a signal; get_param
// serves the signal pins, get_part the slots. The registry hands out
// Components. A part named where a signal is expected fails the load naming
// the node, the pin and the kind; the voice keeps a list of its signal nodes.
struct Plug final : Component {   // a bare part: a Component that is no signal
    const char* type_name() const override { return "Plug"; }
};
static void run_wiring_door_tests() {
    std::printf("the wiring door: one call for every pin kind\n");
    AdditiveSource add;
    CHECK(add.set_param("frequency", std::make_shared<ConstantSource>(220.0f)));   // a signal into a Param
    CHECK(!add.set_param("frequency", std::make_shared<Plug>()));                  // a part into a Param: refused
    CHECK(add.set_param("partials", std::make_shared<FullPartials>()));            // a part into its slot
    CHECK(!add.set_param("partials", std::make_shared<SineSource>()));             // a signal into a slot: refused
    CHECK(!add.set_param("partials", std::make_shared<Plug>()));                   // the wrong part: refused
    CHECK(add.get_part("frequency") == nullptr);                                   // a Param serves no part
    CHECK(add.get_param("nothing") == nullptr && add.get_part("nothing") == nullptr);
    CHECK(add.get_part("partials") != nullptr);
    CHECK(add.get_param("partials") == nullptr);                                   // a slot serves no signal
    register_all_sources();
    auto& reg = SourceRegistry::instance();
    std::shared_ptr<Component> c = reg.create("SineSource");
    CHECK(std::dynamic_pointer_cast<ValueSource>(c) != nullptr);
    CHECK(std::dynamic_pointer_cast<ValueSource>(reg.create("FullPartials")) == nullptr);   // a part
    // a part where a signal is expected fails the load naming node, pin and kind
    const char* partAsSignal = R"({"sampleRate":48000,"instrument":{"polyphony":1},"graph":{"output":"add","nodes":[
      {"id":"parts","type":"FullPartials","params":{"maxPartials":4}},
      {"id":"fm","type":"FormantPeaks","params":{"frequency":[440.0],"gain":[1.0],"width":[300.0],"power":[2.0]}},
      {"id":"add","type":"AdditiveSource","params":{"frequency":{"ref":"fm"},"amplitude":0.5,"partials":{"ref":"parts"}}}]}})";
    CHECK(load_fails_naming(partAsSignal, "add"));
    CHECK(load_fails_naming(partAsSignal, "frequency"));
    CHECK(load_fails_naming(partAsSignal, "not a signal"));
    const char* additive = R"({"sampleRate":48000,"instrument":{"polyphony":1},"graph":{"output":"add","nodes":[
      {"id":"parts","type":"FullPartials","params":{"maxPartials":4}},
      {"id":"add","type":"AdditiveSource","params":{"frequency":220.0,"amplitude":0.5,"partials":{"ref":"parts"},"formantWeight":0.0}}]}})";
    auto ip = load_inst(additive);
    const auto& vg = ip.instrument->voicePool[0];
    CHECK(vg.signals.size() == 1 && vg.signals[0].id == "add");   // the part is no signal
    CHECK(vg.nodesById.count("parts") == 1 && vg.nodesById.count("add") == 1);
}

// The formant family are parts: a FormantSequence holds its formants in a
// multi slot that clears and refills, set replaces and add appends, a signal
// is refused, no formant type is a ValueSource, and a formant edited on a
// loaded instrument reaches the next note through the owner's prepare.
static void run_formant_part_tests() {
    std::printf("formants: parts in slots\n");
    {
        FormantSequence seq;
        auto f1 = make_one_peak(500.0f), f2 = make_one_peak(2000.0f);
        CHECK(seq.add_param("spectra", f1) && seq.add_param("spectra", f2));
        CHECK(seq.clear_param("spectra"));
        CHECK(seq.add_param("spectra", f2));
        seq.prepare(RenderContext{48000}); seq.start(4800); seq.next();
        CHECK(seq.gain(500.0f) == 0.0f && seq.gain(2000.0f) > 0.0f);                 // exactly the refilled set
        CHECK(!seq.add_param("spectra", std::make_shared<SineSource>()));             // a signal into the slot: refused
        CHECK(seq.set_param("spectra", f1));                                          // set replaces on a multi slot
        CHECK(seq.get_part("spectra") == f1);
        CHECK(seq.spectra.views.size() == 1);
        register_all_sources();
        for (const char* type : {"FormantPeaks", "FormantBands", "FormantSequence"})
            CHECK(std::dynamic_pointer_cast<ValueSource>(SourceRegistry::instance().create(type)) == nullptr);
    }
    // a peak's gain changed on a loaded instrument, then prepare: the next note hears it
    {
        const char* shaped = R"({"sampleRate":48000,"instrument":{"polyphony":1},"graph":{"output":"add","nodes":[
          {"id":"parts","type":"FullPartials","params":{"maxPartials":8}},
          {"id":"fm","type":"FormantPeaks","params":{"frequency":[440.0],"gain":[1.0],"width":[300.0],"power":[2.0]}},
          {"id":"add","type":"AdditiveSource","params":{"frequency":220.0,"amplitude":0.5,"partials":{"ref":"parts"},"formant":{"ref":"fm"},"formantWeight":1.0}}]}})";
        auto ip = load_inst(shaped);
        const auto before = render_one_live_note(*ip.instrument, 60.0f);
        ip.instrument->voicePool[0].nodesById.at("fm")->set_array("gain", {0.0f});
        ip.instrument->prepare(RenderContext{48000});
        const auto after = render_one_live_note(*ip.instrument, 60.0f);
        CHECK(!same_bits(before, after));
    }
}

// The segment walk, the coherent walk and the pair trade against the blocks
// they replaced: the deleted walk functions retyped here as they stood in
// Partials, both sides driven by identically seeded Randomizers, every
// advance compared bit for bit.
namespace partials_twin {
struct W { float cur{0.0f}, target{0.0f}; int pos{0}, len{1}; };
inline int seg_len(int baseLen, float evolve, Randomizer& rng) {
    float f = (evolve <= 0.0f) ? 1.0f
                               : std::pow(1.0f + 3.0f * evolve, rng.valuePN());
    int len = int(float(baseLen) * f);
    return len < 1 ? 1 : len;
}
inline void init(W& w, int baseLen, float evolve, Randomizer& rng) {
    w.cur = rng.valuePN();
    w.target = rng.valuePN();
    w.len = seg_len(baseLen, evolve, rng);
    w.pos = int(rng.range(0.0f, float(w.len)));
}
inline float advance(W& w, int baseLen, float evolve, Randomizer& rng) {
    if (w.pos >= w.len) {
        w.cur = w.target;
        float amp = 1.0f + 0.7f * evolve * rng.valuePN();
        w.target = rng.valuePN() * amp;
        w.len = seg_len(baseLen, evolve, rng);
        w.pos = 0;
    }
    float u = float(w.pos) / float(w.len);
    float s = u * u * (3.0f - 2.0f * u);
    ++w.pos;
    return w.cur + (w.target - w.cur) * s;
}
}  // namespace partials_twin

static void run_segment_walk_twin_tests() {
    std::printf("segment walk: the primitive against the deleted walk\n");
    int bad = 0, rows = 0;
    for (int baseLen : {1, 7, 1600, 48000})
        for (float evolve : {0.0f, 0.1f, 0.3f, 1.0f})
            for (uint32_t seed = 1; seed <= 8; ++seed) {
                Randomizer ra(seed), rb(seed);
                partials_twin::W a;
                dsp::SegmentWalk b;
                partials_twin::init(a, baseLen, evolve, ra);
                b.init(baseLen, evolve, rb);
                if (!same_bits(a.cur, b.cur) || !same_bits(a.target, b.target) || a.pos != b.pos || a.len != b.len) ++bad;
                for (int k = 0; k < 500; ++k) {
                    float x = partials_twin::advance(a, baseLen, evolve, ra);
                    float y = b.advance(baseLen, evolve, rb);
                    ++rows;
                    if (!same_bits(x, y)) ++bad;
                }
            }
    CHECK(bad == 0);
    CHECK(rows == 4 * 4 * 8 * 500);
}

static void run_coherent_walk_twin_tests() {
    std::printf("coherent walk: the motion block against the layer\n");
    int bad = 0, rows = 0;
    for (auto rh : {std::pair<float, float>{48000.0f, 4.0f}, std::pair<float, float>{44100.0f, 7.3f}})
    for (int n : {1, 8, 61})
        for (float coh : {0.0f, 0.4f, 0.5f, 0.59f, 1.0f})
            for (float evolve : {0.0f, 0.3f})
                for (uint32_t noteBase : {0x1234u, 0xBEEFu}) {
                    const float rate = rh.first, hz = rh.second;
                    // the deleted block, as Partials::start and next ran it
                    int moLen = std::max(1, int(rate / std::max(0.01f, hz)));
                    Randomizer moSharedRng(stream_seed(noteBase, 0, kRngMotionShared));
                    partials_twin::W moShared;
                    partials_twin::init(moShared, moLen, evolve, moSharedRng);
                    std::vector<partials_twin::W> moWalks(static_cast<size_t>(n));
                    std::vector<Randomizer> moRng(size_t(n), Randomizer(0u));
                    for (int i = 0; i < n; ++i) {
                        moRng[size_t(i)] = Randomizer(stream_seed(noteBase, uint32_t(i), kRngMotionInd));
                        partials_twin::init(moWalks[size_t(i)], moLen, evolve, moRng[size_t(i)]);
                    }
                    std::vector<float> moVals(size_t(n), 0.0f);
                    CoherentWalk layer;
                    layer.prepare(n);
                    layer.start(n, noteBase, kRngMotionShared, kRngMotionInd, hz, evolve, rate);
                    for (int k = 0; k < 300; ++k) {
                        float shared = partials_twin::advance(moShared, moLen, evolve, moSharedRng);
                        for (size_t i = 0; i < moWalks.size(); ++i) {
                            float ind = partials_twin::advance(moWalks[i], moLen, evolve, moRng[i]);
                            moVals[i] = coh * shared + (1.0f - coh) * ind;
                        }
                        layer.advance(evolve, coh);
                        for (int i = 0; i < n; ++i) {
                            ++rows;
                            if (!same_bits(moVals[size_t(i)], layer.value(i))) ++bad;
                        }
                    }
                }
    CHECK(bad == 0);
    CHECK(rows == 2 * (1 + 8 + 61) * 5 * 2 * 2 * 300);
}

static void run_pair_trade_twin_tests() {
    std::printf("pair trade: the trade block against the layer\n");
    int bad = 0, rows = 0;
    const float rate = 48000.0f;
    for (float hz : {2.0f, 0.37f, 7.3f})
    for (int n : {1, 2, 7, 24})
        for (uint32_t noteBase : {0x1234u, 0xBEEFu}) {
            int trLen = std::max(1, int(rate / std::max(0.01f, hz)));
            Randomizer trRng(stream_seed(noteBase, 0, kRngTrade));
            std::vector<partials_twin::W> trWalks(size_t((n + 1) / 2));
            for (auto& w : trWalks) partials_twin::init(w, trLen, 0.0f, trRng);
            std::vector<float> trVals(size_t((n + 1) / 2), 0.0f);
            PairTrade layer;
            layer.prepare(n);
            layer.start(n, noteBase, kRngTrade, hz, rate);
            for (int k = 0; k < 300; ++k) {
                for (size_t i = 0; i < trWalks.size(); ++i)
                    trVals[i] = partials_twin::advance(trWalks[i], trLen, 0.0f, trRng);
                layer.advance();
                for (size_t i = 0; i < trVals.size(); ++i) {
                    ++rows;
                    if (!same_bits(trVals[i], layer.value(int(i)))) ++bad;
                }
            }
        }
    CHECK(bad == 0);
    CHECK(rows == 3 * (1 + 1 + 4 + 12) * 2 * 300);
}

// The bandwidth noise, the onset gate and the decay law against the blocks
// they replaced: each block retyped here as Partials ran it, both sides on
// identically seeded streams, every output compared bit for bit (a layer's
// state is private and is read through its outputs) — the onset rows with
// t <= 0 included, where an assignment and a
// multiply by zero differ in the zero's sign.
static void run_bandwidth_noise_twin_tests() {
    std::printf("bandwidth noise: the per-partial walk against the layer\n");
    int bad = 0, rows = 0;
    const float rate = 48000.0f;
    for (int n : {1, 5, 40})
        for (float hz : {0.5f, 30.0f, 2000.0f})
            for (uint32_t noteBase : {0x1234u, 0xBEEFu}) {
                // the start block
                int bwLen = std::max(1, int(rate / (hz < 1.0f ? 1.0f : hz)));
                std::vector<float> bwCur(size_t(n), 0.0f), bwTarget(size_t(n), 0.0f);
                std::vector<int> bwPos(size_t(n), 0);
                std::vector<Randomizer> bwRng(size_t(n), Randomizer(0u));
                for (int i = 0; i < n; ++i) {
                    bwRng[size_t(i)]    = Randomizer(stream_seed(noteBase, uint32_t(i), kRngBandwidth));
                    bwCur[size_t(i)]    = bwRng[size_t(i)].valuePN();
                    bwTarget[size_t(i)] = bwRng[size_t(i)].valuePN();
                    bwPos[size_t(i)]    = int(bwRng[size_t(i)].range(0.0f, float(bwLen)));
                }
                BandwidthNoise layer;
                layer.prepare(n);
                layer.start(n, noteBase, kRngBandwidth, hz, rate);
                const float dry = std::sqrt(1.0f - 0.37f), wet = std::sqrt(0.37f);
                for (int k = 0; k < 400; ++k)
                    for (int index = 0; index < n; ++index) {
                        // the body's block
                        if (bwPos[size_t(index)] >= bwLen) {
                            bwCur[size_t(index)]    = bwTarget[size_t(index)];
                            bwTarget[size_t(index)] = bwRng[size_t(index)].valuePN();
                            bwPos[size_t(index)]    = 0;
                        }
                        float u = float(bwPos[size_t(index)]) / float(bwLen);
                        float s = u * u * (3.0f - 2.0f * u);
                        float noise = bwCur[size_t(index)] + (bwTarget[size_t(index)] - bwCur[size_t(index)]) * s;
                        ++bwPos[size_t(index)];
                        float expected = dry + wet * noise;
                        float got = layer.factor(index, dry, wet);
                        ++rows;
                        if (!same_bits(expected, got)) ++bad;
                    }
            }
    CHECK(bad == 0);
    CHECK(rows == (1 + 5 + 40) * 3 * 2 * 400);
}

static void run_onset_gate_twin_tests() {
    std::printf("onset gate: the delays and the fade against the layer\n");
    int bad = 0, rows = 0, zeros = 0;
    const float rate = 48000.0f;
    for (int n : {1, 6, 33})
        for (float tilt : {-1.0f, -0.5f, 0.0f, 0.5f, 1.0f})
            for (float spread : {0.0f, 0.01f, 0.2f})
                for (uint32_t noteBase : {0x1234u, 0xBEEFu}) {
                    const float fade = 0.0005f;
                    // the start block
                    Randomizer onsetRng(stream_seed(noteBase, 0, kRngOnset));
                    std::vector<float> onsetDelay(static_cast<size_t>(n));
                    float at = std::fabs(tilt);
                    for (int i = 0; i < n; ++i) {
                        float u = onsetRng.value();
                        float r = (n > 1) ? float(i) / float(n - 1) : 0.0f;
                        float ordered = (tilt >= 0.0f) ? r : 1.0f - r;
                        onsetDelay[size_t(i)] = ((1.0f - at) * u + at * ordered) * spread * rate;
                    }
                    float onsetFadeSamples = std::max(1.0f, fade * rate);
                    OnsetGate layer;
                    layer.prepare(n);
                    layer.reset();
                    layer.start(n, noteBase, kRngOnset, spread, tilt, fade, rate);
                    int sampleIdx = 0;
                    for (int k = 0; k < 120; ++k) {
                        ++sampleIdx;
                        layer.advance();
                        for (int index = 0; index < n; ++index) {
                            const float in = (index & 1) ? -0.75f : 0.75f;   // both signs: the zero's sign is the point
                            // the body's block
                            float pampl = in;
                            float t = (float(sampleIdx) - onsetDelay[size_t(index)]) / onsetFadeSamples;
                            if (t <= 0.0f) pampl = 0.0f;
                            else if (t < 1.0f) pampl *= t * t * (3.0f - 2.0f * t);
                            float got = layer.apply(index, in);
                            ++rows;
                            if (t <= 0.0f) ++zeros;
                            if (!same_bits(pampl, got)) ++bad;
                        }
                    }
                }
    CHECK(bad == 0);
    CHECK(zeros > 0);                       // the assignment rows were exercised
    CHECK(rows == (1 + 6 + 33) * 5 * 3 * 2 * 120);
}

static void run_decay_law_twin_tests() {
    std::printf("decay law: the factor and the running gain against the layer\n");
    int bad = 0, rows = 0;
    const float pmults[] = {1.0f, 2.5f, 17.0f, 61.3f};
    for (float rate : {48000.0f, 44100.0f})
    for (float decayRate : {0.0f, 1.0f, 40.0f, 400.0f})
        for (float decayExp : {-2.0f, 0.0f, 0.6f, 4.0f}) {
            const int n = 4;
            std::vector<float> decayGain(n, 1.0f), decayState(n, 1.0f);
            for (int i = 0; i < n; ++i) {
                const double r = double(decayRate) * std::pow(double(pmults[i]), double(decayExp));
                decayGain[size_t(i)] = float(std::pow(10.0, -r / (20.0 * double(rate))));
            }
            DecayLaw layer;
            layer.prepare(n);
            layer.start(n);
            for (int i = 0; i < n; ++i) layer.set_gain(i, pmults[i], decayRate, decayExp, rate);
            for (int k = 0; k < 2000; ++k) {
                for (size_t i = 0; i < size_t(n); ++i) decayState[i] *= decayGain[i];
                layer.advance();
                for (int i = 0; i < n; ++i) {
                    ++rows;
                    if (!same_bits(decayState[size_t(i)], layer.gain(i))) ++bad;
                }
            }
        }
    CHECK(bad == 0);
    CHECK(rows == 2 * 4 * 4 * 2000 * 4);
}

// The expansion against the deleted apply_expand_rule: the block retyped here
// over six local arrays as Partials ran it, the layout on the same originals
// and an identically seeded stream, all six arrays compared bit for bit; a
// restore after an expansion gives the snapshot back; a reserved layout
// expands without allocating.
static void run_partial_layout_twin_tests() {
    std::printf("partial layout: the expansion against the deleted block\n");
    int bad = 0, rows = 0;
    for (int n : {1, 4, 16})
        for (int count : {1, 2, 3})
            for (int recurse : {0, 1, 2})
                for (uint32_t seed : {0x51u, 0xA7u}) {
                    ExpandRule er;
                    er.count = count; er.recurse = recurse;
                    er.spacing1 = 0.3f; er.spacing2 = 0.4f; er.dt1 = 0.02f; er.dt2 = 0.03f;
                    er.loPct1 = 0.2f; er.loPct2 = 0.25f; er.power1 = 1.5f; er.power2 = 0.7f;
                    er.po1 = 0.25f; er.po2 = 0.5f;
                    // the originals: a deterministic layout
                    std::vector<float> mult1_(static_cast<size_t>(n)), mult2_(static_cast<size_t>(n)), ampl1_(static_cast<size_t>(n)), ampl2_(static_cast<size_t>(n)), po1_(static_cast<size_t>(n)), po2_(static_cast<size_t>(n));
                    for (int i = 0; i < n; ++i) {
                        mult1_[size_t(i)] = float(i + 1) * 1.01f; mult2_[size_t(i)] = float(i + 1);
                        ampl1_[size_t(i)] = 1.0f / float(i + 1);  ampl2_[size_t(i)] = 0.5f / float(i + 1);
                        po1_[size_t(i)] = 0.1f * float(i) - 0.25f; po2_[size_t(i)] = 0.07f * float(i) - 0.1f;   // negative for the low partials
                    }
                    PartialLayout layout;
                    layout.mult1 = mult1_; layout.mult2 = mult2_; layout.ampl1 = ampl1_; layout.ampl2 = ampl2_;
                    layout.po1 = po1_; layout.po2 = po2_;
                    layout.snapshot();
                    const std::vector<float> oM1 = mult1_, oM2 = mult2_, oA1 = ampl1_, oA2 = ampl2_, oP1 = po1_, oP2 = po2_;
                    const int expanded = layout.reserve_expanded(er);
                    layout.restore();   // the note's path: the originals back, then the expansion
                    std::vector<float> exM1_, exM2_, exA1_, exA2_, exP1_, exP2_;
                    Randomizer expandRng_(seed), layoutRng(seed);
                    for (int r = 0; r <= er.recurse; ++r) {
                        // the deleted block, verbatim over the local arrays
                        int origN = int(mult1_.size());
                        int newN = origN * (er.count * 2 + 1);
                        auto& m1 = exM1_; auto& m2 = exM2_; auto& a1 = exA1_;
                        auto& a2 = exA2_; auto& p1 = exP1_; auto& p2 = exP2_;
                        for (auto* v : {&m1, &m2, &a1, &a2, &p1, &p2}) v->assign(size_t(newN), 0.0f);
                        for (int i = 0; i < origN; ++i) {
                            int base = i * (er.count * 2 + 1);
                            for (int j = 0; j < er.count; ++j) {
                                int idx = base + j;
                                float semis1 = float(er.count - j) * er.spacing1;
                                float semis2 = float(er.count - j) * er.spacing2;
                                m1[idx] = mult1_[i] / std::pow(2.0f, semis1 / 12.0f) * (1.0f + expandRng_.valuePN() * er.dt1);
                                m2[idx] = mult2_[i] / std::pow(2.0f, semis2 / 12.0f) * (1.0f + expandRng_.valuePN() * er.dt2);
                                float t = float(j) / float(er.count);
                                a1[idx] = ampl1_[i] * er.loPct1 + ampl1_[i] * (1.0f - er.loPct1) * std::pow(t, er.power1);
                                a2[idx] = ampl2_[i] * er.loPct2 + ampl2_[i] * (1.0f - er.loPct2) * std::pow(t, er.power2);
                                p1[idx] = std::fmod(po1_[i] + float(j + 1) / float(er.count) * er.po1, 1.0f);
                                p2[idx] = std::fmod(po2_[i] + float(j + 1) / float(er.count) * er.po2, 1.0f);
                            }
                            int cIdx = base + er.count;
                            m1[cIdx] = mult1_[i]; m2[cIdx] = mult2_[i];
                            a1[cIdx] = ampl1_[i]; a2[cIdx] = ampl2_[i];
                            p1[cIdx] = po1_[i];   p2[cIdx] = po2_[i];
                            for (int j = 0; j < er.count; ++j) {
                                int idx = base + er.count + 1 + j;
                                float semis1 = float(j + 1) * er.spacing1;
                                float semis2 = float(j + 1) * er.spacing2;
                                m1[idx] = mult1_[i] * std::pow(2.0f, semis1 / 12.0f);
                                m2[idx] = mult2_[i] * std::pow(2.0f, semis2 / 12.0f);
                                float t = float(er.count - j - 1) / float(er.count);
                                a1[idx] = ampl1_[i] * er.loPct1 + ampl1_[i] * (1.0f - er.loPct1) * std::pow(t, er.power1);
                                a2[idx] = ampl2_[i] * er.loPct2 + ampl2_[i] * (1.0f - er.loPct2) * std::pow(t, er.power2);
                                p1[idx] = std::fmod(po1_[i] + float(er.count - j) / float(er.count) * er.po1, 1.0f);
                                p2[idx] = std::fmod(po2_[i] + float(er.count - j) / float(er.count) * er.po2, 1.0f);
                            }
                        }
                        mult1_.assign(m1.begin(), m1.end()); mult2_.assign(m2.begin(), m2.end());
                        ampl1_.assign(a1.begin(), a1.end()); ampl2_.assign(a2.begin(), a2.end());
                        po1_.assign(p1.begin(), p1.end());   po2_.assign(p2.begin(), p2.end());
                        layout.expand(er, layoutRng);
                    }
                    CHECK(layout.count() == expanded && int(mult1_.size()) == expanded);
                    const std::vector<float>* mine[] = {&mult1_, &mult2_, &ampl1_, &ampl2_, &po1_, &po2_};
                    const std::vector<float>* theirs[] = {&layout.mult1, &layout.mult2, &layout.ampl1, &layout.ampl2, &layout.po1, &layout.po2};
                    for (int a = 0; a < 6; ++a)
                        for (size_t i = 0; i < mine[a]->size(); ++i) {
                            ++rows;
                            if (!same_bits((*mine[a])[i], (*theirs[a])[i])) ++bad;
                        }
                    // restore gives the snapshot back, all six arrays
                    layout.restore();
                    CHECK(layout.count() == n);
                    auto bitsEqual = [](const std::vector<float>& a, const std::vector<float>& b) {
                        if (a.size() != b.size()) return false;
                        for (size_t i = 0; i < a.size(); ++i) if (!same_bits(a[i], b[i])) return false;
                        return true;
                    };
                    CHECK(bitsEqual(layout.mult1, oM1) && bitsEqual(layout.mult2, oM2) && bitsEqual(layout.ampl1, oA1)
                          && bitsEqual(layout.ampl2, oA2) && bitsEqual(layout.po1, oP1) && bitsEqual(layout.po2, oP2));
                }
    CHECK(bad == 0);
    CHECK(rows > 0);
    // a reserved layout expands without allocating
    {
        ExpandRule er;
        er.count = 2; er.recurse = 1;
        PartialLayout layout;
        for (int i = 0; i < 8; ++i) {
            layout.mult1.push_back(float(i + 1)); layout.mult2.push_back(float(i + 1));
            layout.ampl1.push_back(1.0f); layout.ampl2.push_back(1.0f);
            layout.po1.push_back(0.0f); layout.po2.push_back(0.0f);
        }
        layout.snapshot();
        layout.reserve_expanded(er);
        Randomizer rng(7u);
        alloc_probe::reset();
        alloc_probe::arm();
        layout.restore();
        for (int r = 0; r <= er.recurse; ++r) layout.expand(er, rng);
        alloc_probe::disarm();
        CHECK(alloc_probe::count() == 0);
        CHECK(layout.count() == 8 * 5 * 5);
    }
}

// The base drives a set's pins: each prepared once, started once and
// advanced once per next(), in declaration order; a composite forwards each
// verb to its sets and advances no pin of its own.
struct OrderProbe final : Generator {
    std::vector<std::string>* log{nullptr};
    std::string tag;
    float compute() override { if (log) log->push_back(tag); return 0.0f; }
    const char* type_name() const override { return "OrderProbe"; }
};
static void run_partial_set_lifecycle_tests() {
    std::printf("partial set: the base drives the pins\n");
    RenderContext ctx{48000};
    const char* names[] = {"multEnv", "amplEnv", "poEnv", "roEnv", "dtEnv", "bwEnv", "motionEnv", "shimmerEnv"};
    {
        FullPartials full;
        std::vector<std::shared_ptr<PinCounter>> probes;
        for (const char* n : names) {
            probes.push_back(std::make_shared<PinCounter>());
            CHECK(full.set_param(n, probes.back()));
        }
        PartialSet& set = full;
        set.prepare(ctx); set.start(4800);
        for (int i = 0; i < 7; ++i) set.next();
        for (const auto& p : probes) CHECK(p->ctxPrepares == 1 && p->starts == 1 && p->nexts == 7);
    }
    {
        FullPartials full;
        std::vector<std::string> log;
        for (const char* n : names) {
            auto o = std::make_shared<OrderProbe>();
            o->log = &log; o->tag = n;
            CHECK(full.set_param(n, o));
        }
        full.prepare(ctx); full.start(4800); full.next();
        std::string seen;
        for (const auto& s : log) seen += s + ",";
        CHECK(seen == "multEnv,amplEnv,poEnv,roEnv,dtEnv,bwEnv,motionEnv,shimmerEnv,");
    }
    {
        CompositePartials comp;
        auto a = std::make_shared<FullPartials>();
        auto b = std::make_shared<SequencePartials>();
        auto pa = std::make_shared<PinCounter>();
        auto pb = std::make_shared<PinCounter>();
        CHECK(a->set_param("multEnv", pa) && b->set_param("amplEnv", pb));
        CHECK(comp.add_param("partials", a) && comp.add_param("partials", b));
        PartialSet& set = comp;
        set.prepare(ctx); set.start(4800);
        for (int i = 0; i < 7; ++i) set.next();
        CHECK(pa->ctxPrepares == 1 && pa->starts == 1 && pa->nexts == 7);
        CHECK(pb->ctxPrepares == 1 && pb->starts == 1 && pb->nexts == 7);
        CHECK(comp.partial_count() == 60);
    }
}

// The owner-driven part's lifecycle lives once, in DrivenPart: every pin prepared
// and started once by the base, every Param pin advanced once per next() in
// declaration order, then the hook; a Setting between two Params is neither
// prepared nor advanced. The part here is test-local; PartialSet and the
// formant base derive the same way.
namespace {
struct TwoPinPart final : DrivenPart {
    Param a{this, "a", 1.0f, 0.0f, 10.0f, nullptr, Pull::Manual};
    Setting<int> n{this, "n", 3, 1, 8};
    Param b{this, "b", 2.0f, 0.0f, 10.0f, nullptr, Pull::Manual};
    int prepares{0}, starts{0}, nexts{0}, rate{0};
    const char* type_name() const override { return "TwoPinPart"; }
protected:
    void on_prepare(const RenderContext& ctx) override { rate = ctx.sampleRate; ++prepares; }
    void on_start(int) override { ++starts; }
    void on_next() override { ++nexts; }
};
}
static void run_driven_part_lifecycle_tests() {
    std::printf("driven part: the base drives the pins\n");
    RenderContext ctx{48000};
    {
        TwoPinPart part;
        auto pa = std::make_shared<PinCounter>(), pb = std::make_shared<PinCounter>();
        CHECK(part.set_param("a", pa) && part.set_param("b", pb));
        part.prepare(ctx); part.start(4800);
        for (int i = 0; i < 5; ++i) part.next();
        CHECK(pa->ctxPrepares == 1 && pa->starts == 1 && pa->nexts == 5);
        CHECK(pb->ctxPrepares == 1 && pb->starts == 1 && pb->nexts == 5);
        CHECK(pa->rate == 48000 && pb->rate == 48000);
        CHECK(part.prepares == 1 && part.starts == 1 && part.nexts == 5 && part.rate == 48000);
        part.prepare(ctx); part.start(4800); part.next();        // a second prepare relists the pins: nothing doubles
        CHECK(pa->ctxPrepares == 2 && pa->starts == 2 && pa->nexts == 6);
        CHECK(pb->ctxPrepares == 2 && pb->starts == 2 && pb->nexts == 6);
        CHECK(part.prepares == 2 && part.starts == 2 && part.nexts == 6);
    }
    {
        TwoPinPart part;
        std::vector<std::string> log;
        auto oa = std::make_shared<OrderProbe>(); oa->log = &log; oa->tag = "a";
        auto ob = std::make_shared<OrderProbe>(); ob->log = &log; ob->tag = "b";
        CHECK(part.set_param("a", oa) && part.set_param("b", ob));
        part.prepare(ctx); part.start(4800);
        CHECK(log.empty());
        part.next();
        CHECK(log == std::vector<std::string>({"a", "b"}));      // declaration order, the setting skipped
        part.set_setting("n", 5.0f);
        part.next();
        CHECK(log.size() == 4 && part.n.value == 5);
    }
    {
        TwoPinPart part;                                          // unwired: the constants, no source to call
        part.prepare(ctx); part.start(4800);
        for (int i = 0; i < 3; ++i) part.next();
        CHECK(part.nexts == 3);
        CHECK_NEAR(part.a.current(), 1.0f, 1e-6f);
        CHECK_NEAR(part.b.current(), 2.0f, 1e-6f);
    }
}

// The 2,000-point log grid from 20 Hz to 20 kHz the formant twins share.
static std::vector<float> formant_log_grid() {
    std::vector<float> freqs;
    const float lo = std::log10(20.0f), hi = std::log10(20000.0f);
    for (int i = 0; i < 2000; ++i)
        freqs.push_back(std::pow(10.0f, lo + (hi - lo) * float(i) / 1999.0f));
    return freqs;
}

// FormantPeaks against the peak node and the spectrum it replaces: the peak's
// get_gain (band edges from frequency +/- width/2, the two power ramps) and
// the spectrum's get_gain (the max over the containing children, 0 when none
// contains) retyped here over rows as they stood, compared bit for bit with
// FormantPeaks over rows x frequencies: 2,000 log-spaced from 20 Hz to
// 20 kHz plus every row's exact band edges, the float just inside each edge
// and the centre; a single row, overlapping rows, a gain-0 row, three powers,
// two equal rows, a vowel's five. The old contains() is implied: the gain is
// compared both as the old spectrum gave it and gated to 0 where the old
// predicate was false, and the two agree.
namespace formant_peaks_twin {
struct Row { float f, g, w, p; };
static bool old_peak_contains(const Row& r, float freq) {
    const float lo = r.f - r.w * 0.5f;
    const float hi = r.f + r.w * 0.5f;
    return freq > lo && freq < hi;
}
static float old_peak_gain(const Row& r, float freq) {
    const float lo = r.f - r.w * 0.5f;
    const float hi = r.f + r.w * 0.5f;
    if (freq <= lo || freq >= hi) return 0.0f;
    if (freq < r.f)
        return std::pow((freq - lo) / (r.f - lo), r.p) * r.g;
    return std::pow((hi - freq) / (hi - r.f), r.p) * r.g;
}
static bool old_spectrum_contains(const std::vector<Row>& rows, float freq) {
    for (const auto& r : rows)
        if (old_peak_contains(r, freq)) return true;
    return false;
}
static float old_spectrum_gain(const std::vector<Row>& rows, float freq) {
    float g = 0.0f;
    for (const auto& r : rows)
        if (old_peak_contains(r, freq))
            g = std::max(g, old_peak_gain(r, freq));
    return g;
}
static const std::vector<std::vector<Row>> kSets = {
    {{1000.0f, 1.0f, 500.0f, 2.0f}},                                                    // one row
    {{700.0f, 1.0f, 600.0f, 0.5f}, {900.0f, 0.0f, 500.0f, 2.0f}, {1100.0f, 0.8f, 800.0f, 6.0f}},   // overlapping; a gain-0 row; three powers
    {{800.0f, 1.0f, 640.0f, 2.0f}, {1150.0f, 0.631f, 720.0f, 2.0f}, {2800.0f, 0.1f, 960.0f, 2.0f},
     {3500.0f, 0.0158f, 1040.0f, 2.0f}, {4950.0f, 0.001f, 1120.0f, 2.0f}},              // a vowel's five
    {{300.0f, 2.5f, 1000.0f, 1.0f}, {300.0f, 2.5f, 1000.0f, 1.0f}},                     // two equal rows
    {{220.0f, 0.7f, 400.0f, 1.0f}, {850.0f, 1.0f, 900.0f, 2.0f}, {1850.0f, 0.6f, 1200.0f, 1.0f}, {3100.0f, 0.65f, 1000.0f, 1.5f}},
};
// The frequency grid for a row set: log-spaced, then each row's edges, the
// floats just inside them, and the centre.
static std::vector<float> grid(const std::vector<Row>& set) {
    std::vector<float> freqs = formant_log_grid();
    for (const auto& r : set) {
        const float a = r.f - r.w * 0.5f, b = r.f + r.w * 0.5f;
        freqs.push_back(a); freqs.push_back(b); freqs.push_back(r.f);
        freqs.push_back(std::nextafter(a, 1.0e9f)); freqs.push_back(std::nextafter(b, 0.0f));
    }
    return freqs;
}
static void fill(FormantPeaks& peaks, const std::vector<Row>& set) {
    std::vector<float> f, g, w, p;
    for (const auto& r : set) { f.push_back(r.f); g.push_back(r.g); w.push_back(r.w); p.push_back(r.p); }
    peaks.set_array("frequency", f);
    peaks.set_array("gain", g);
    peaks.set_array("width", w);
    peaks.set_array("power", p);
}
}
static void run_formant_peaks_twin_tests() {
    using namespace formant_peaks_twin;
    std::printf("formant peaks twin: the peak node and the spectrum, as rows\n");
    int rows = 0, bad = 0, inBand = 0;
    for (const auto& set : kSets) {
        FormantPeaks peaks;
        fill(peaks, set);
        peaks.prepare(RenderContext{48000}); peaks.start(4800); peaks.next();
        CHECK(peaks.peak_count() == int(set.size()));
        for (float freq : grid(set)) {
            ++rows;
            const bool oc = old_spectrum_contains(set, freq);
            const float og = old_spectrum_gain(set, freq);
            inBand += oc;
            if (!same_bits(peaks.gain(freq), og)) ++bad;
            if (!same_bits(peaks.gain(freq), oc ? og : 0.0f)) ++bad;          // 0 outside every row: the old gate is implied
        }
    }
    CHECK(bad == 0);
    CHECK(rows == 2000 * 5 + 5 * (1 + 3 + 5 + 2 + 4));
    CHECK(inBand > 0 && inBand < rows);
    // the edges follow an array change (the editor's path), not only prepare
    {
        FormantPeaks peaks;
        fill(peaks, kSets[0]);
        peaks.prepare(RenderContext{48000}); peaks.start(4800);
        CHECK(same_bits(peaks.gain(1000.0f), 1.0f) && peaks.gain(1100.0f) > 0.0f);
        peaks.set_array("width", {100.0f});
        CHECK(same_bits(peaks.gain(1100.0f), 0.0f));
        peaks.set_array("frequency", {2000.0f});
        CHECK(same_bits(peaks.gain(2000.0f), 1.0f));
    }
}

// A FormantPeaks loads from four arrays of one length and renders; arrays of
// unequal length fail the load naming the node; an edited row reaches the
// next note through the owner's prepare, as the peak node's pin did.
static void run_formant_peaks_load_tests() {
    std::printf("formant peaks: the load\n");
    const char* ragged = R"({"sampleRate":48000,"instrument":{"polyphony":1},"graph":{"output":"add","nodes":[
      {"id":"parts","type":"FullPartials","params":{"maxPartials":8}},
      {"id":"pk","type":"FormantPeaks","params":{"frequency":[500.0,1500.0],"gain":[1.0],"width":[300.0,400.0],"power":[2.0,2.0]}},
      {"id":"add","type":"AdditiveSource","params":{"frequency":220.0,"amplitude":0.5,"partials":{"ref":"parts"},"formant":{"ref":"pk"},"formantWeight":1.0}}]}})";
    CHECK(load_fails_naming(ragged, "node 'pk' (FormantPeaks)"));
    CHECK(load_fails_naming(ragged, "table 'peaks' takes its columns at one length"));
    CHECK(load_fails_naming(ragged, "frequency 2, gain 1, width 2, power 2"));
    const char* missing = R"({"sampleRate":48000,"instrument":{"polyphony":1},"graph":{"output":"add","nodes":[
      {"id":"parts","type":"FullPartials","params":{"maxPartials":8}},
      {"id":"pk","type":"FormantPeaks","params":{"frequency":[500.0],"gain":[1.0],"width":[300.0]}},
      {"id":"add","type":"AdditiveSource","params":{"frequency":220.0,"amplitude":0.5,"partials":{"ref":"parts"},"formant":{"ref":"pk"},"formantWeight":1.0}}]}})";
    CHECK(load_fails_naming(missing, "node 'pk' (FormantPeaks)"));            // a missing array is a length of none
    const char* rows = R"({"sampleRate":48000,"instrument":{"polyphony":1},"graph":{"output":"add","nodes":[
      {"id":"parts","type":"FullPartials","params":{"maxPartials":8}},
      {"id":"pk","type":"FormantPeaks","params":{"frequency":[440.0,1320.0],"gain":[1.0,0.5],"width":[300.0,400.0],"power":[2.0,2.0]}},
      {"id":"add","type":"AdditiveSource","params":{"frequency":220.0,"amplitude":0.5,"partials":{"ref":"parts"},"formant":{"ref":"pk"},"formantWeight":1.0,"formantFloor":0.0}}]}})";
    auto ip = load_inst(rows);
    auto* pk = dynamic_cast<FormantPeaks*>(ip.instrument->voicePool[0].nodesById.at("pk").get());
    CHECK(pk != nullptr && pk->peak_count() == 2);
    const auto before = render_one_live_note(*ip.instrument, 60.0f);
    CHECK(std::any_of(before.begin(), before.end(), [](float v) { return v != 0.0f; }));
    pk->set_array("gain", {0.0f, 0.5f});
    ip.instrument->prepare(RenderContext{48000});
    const auto after = render_one_live_note(*ip.instrument, 60.0f);
    CHECK(!same_bits(before, after));
    const char* none = R"({"sampleRate":48000,"instrument":{"polyphony":1},"graph":{"output":"add","nodes":[
      {"id":"parts","type":"FullPartials","params":{"maxPartials":8}},
      {"id":"pk","type":"FormantPeaks"},
      {"id":"add","type":"AdditiveSource","params":{"frequency":220.0,"amplitude":0.5,"partials":{"ref":"parts"},"formant":{"ref":"pk"},"formantWeight":1.0}}]}})";
    auto empty = load_inst(none);                            // no rows: a formant that gains nothing
    auto* pk0 = dynamic_cast<FormantPeaks*>(empty.instrument->voicePool[0].nodesById.at("pk").get());
    CHECK(pk0 != nullptr && pk0->peak_count() == 0 && same_bits(pk0->gain(1000.0f), 0.0f));
}

// The enum column and the table rule. A test-local node declares a plain
// column and an enum column in one group, and an enum setting over the same
// labels; the registry builds it under a name no patch uses.
namespace enum_column {
static constexpr const char* kKinds[] = {"Linear", "Expo", "Hold", nullptr};
struct EnumTableProbe final : Generator {
    Setting<int> shape{this, "shape", 0, 0, 2, kKinds};
    Array level{this, "level", "rows", 0.0f, -10.0f, 10.0f};
    Array kind {this, "kind",  "rows", 0.0f, 0.0f,   2.0f, kKinds};
    float compute() override { return 0.0f; }
    const char* type_name() const override { return "EnumTableProbe"; }
};
// A one-node graph: `node` of `type` with `params`, beside a signal for the output.
static std::string one_node(const char* type, const nlohmann::json& params) {
    nlohmann::json n = {{"id", "n"}, {"type", type}};
    if (!params.is_null()) n["params"] = params;
    nlohmann::json doc = {{"sampleRate", 48000}, {"instrument", {{"polyphony", 1}}},
                          {"graph", {{"output", "out"}, {"nodes", nlohmann::json::array({
                              nlohmann::json{{"id", "out"}, {"type", "SineSource"}}, n})}}}};
    return doc.dump();
}
// The node `n` of a loaded one-node graph, or null with the refusal printed.
static std::shared_ptr<Component> load_node(const char* type, const nlohmann::json& params, LoadedInstrument& ip) {
    try { ip = load_inst(one_node(type, params)); }
    catch (const std::exception& e) { std::printf("  %s %s: %s\n", type, params.dump().c_str(), e.what()); return nullptr; }
    return ip.instrument->voicePool[0].nodesById.at("n");
}
static float load_setting(const char* type, const char* key, const nlohmann::json& value) {
    LoadedInstrument ip;
    auto n = load_node(type, nlohmann::json{{key, value}}, ip);
    return n ? n->get_setting(key) : -1.0f;
}
// The probe's `kind` column after a load of `params`, or {-1} on a refusal.
static std::vector<float> load_kind(const nlohmann::json& params) {
    LoadedInstrument ip;
    auto n = load_node("EnumTableProbe", params, ip);
    return n ? n->get_array("kind") : std::vector<float>{-1.0f};
}
// Whether the probe's load of `params` is refused with every one of `needles`.
static bool probe_fails_naming(const nlohmann::json& params, std::initializer_list<const char*> needles) {
    if (const std::string msg = load_refused(one_node("EnumTableProbe", params)); !msg.empty()) {
        bool all = true;
        for (const char* n : needles) all = all && msg.find(n) != std::string::npos;
        if (!all) std::printf("  refused for another reason: %s\n", msg.c_str());
        return all;
    }
    std::printf("  loaded: %s\n", params.dump().c_str());
    return false;
}
}
static void run_enum_column_tests() {
    using namespace enum_column;
    std::printf("the enum column: labels on disk, indices in memory; a ragged table refused\n");
    register_all_sources();
    auto& reg = SourceRegistry::instance();
    if (!reg.has("EnumTableProbe"))
        reg.register_type("EnumTableProbe", {.family = Family::Utility, .shelf = Shelf::Experimental}, [](auto) { return std::make_shared<EnumTableProbe>(); });
    // the one label match: case-insensitive, whole-string; null outside the labels
    CHECK(enum_index(kKinds, "Expo") == 1 && enum_index(kKinds, "expo") == 1 && enum_index(kKinds, "EXPO") == 1);
    CHECK(enum_index(kKinds, "Linear") == 0 && enum_index(kKinds, "hold") == 2);
    CHECK(enum_index(kKinds, "Lin") == -1 && enum_index(kKinds, "Linears") == -1 && enum_index(kKinds, "") == -1);
    CHECK(enum_index(kKinds, "Sine") == -1 && enum_index(nullptr, "Linear") == -1);
    CHECK(std::string(enum_label(kKinds, 0)) == "Linear" && std::string(enum_label(kKinds, 1)) == "Expo"
          && std::string(enum_label(kKinds, 2)) == "Hold");
    CHECK(enum_label(kKinds, -1) == nullptr && enum_label(kKinds, 3) == nullptr && enum_label(nullptr, 0) == nullptr);
    // the column read through a load: labels, indices and a mix load to the same indices
    {
        const auto byLabel = load_kind({{"level", {0.1, 0.2, 0.3}}, {"kind", {"Linear", "Expo", "Hold"}}});
        const auto byIndex = load_kind({{"level", {0.1, 0.2, 0.3}}, {"kind", {0, 1, 2}}});
        const auto mixed   = load_kind({{"level", {0.1, 0.2, 0.3}}, {"kind", {"Expo", 0, "Hold"}}});
        CHECK((byLabel == std::vector<float>{0.0f, 1.0f, 2.0f}));
        CHECK(byIndex == byLabel);
        CHECK((mixed == std::vector<float>{1.0f, 0.0f, 2.0f}));
        CHECK((load_kind({{"level", nlohmann::json::array({0.5})}, {"kind", nlohmann::json::array({2.0})}}) == std::vector<float>{2.0f}));   // a whole float is an index
    }
    // refused, naming the node, the column, the value and the labels
    {
        const nlohmann::json bad[] = {"Sine", 3, -1, 1.5, true, nullptr, nlohmann::json::object()};
        const char* shown[] = {"\"Sine\"", "holds 3,", "holds -1,", "holds 1.5,", "holds true,", "holds null,", "holds {},"};
        for (size_t k = 0; k < std::size(bad); ++k)
            CHECK(probe_fails_naming({{"level", {0.1, 0.2}}, {"kind", {"Linear", bad[k]}}},
                                     {"node 'n' (EnumTableProbe)", "'kind'", shown[k], "(Linear, Expo, Hold)"}));
    }
    // a ragged table refused for every type, naming the node, the group and each column's length
    {
        CHECK(probe_fails_naming({{"level", {0.1, 0.2, 0.3}}, {"kind", {0, 1}}},
                                 {"node 'n' (EnumTableProbe)", "table 'rows'", "(got level 3, kind 2)"}));
        CHECK(probe_fails_naming({{"level", {0.1, 0.2}}}, {"node 'n' (EnumTableProbe)", "table 'rows'", "(got level 2, kind absent)"}));
        CHECK(probe_fails_naming({{"kind", nlohmann::json::array({"Expo"})}}, {"table 'rows'", "(got level absent, kind 1)"}));
        CHECK(probe_fails_naming({{"level", 0.5}, {"kind", nlohmann::json::array({"Expo"})}}, {"node 'n' (EnumTableProbe): 'level' takes a list of numbers, not 0.5"}));   // not an array: refused
        CHECK(probe_fails_naming({{"level", nlohmann::json::array()}, {"kind", nlohmann::json::array({0})}}, {"(got level 0, kind 1)"}));
        CHECK(load_kind(nlohmann::json::object()).empty());                                 // both absent
        CHECK(load_kind({{"level", nlohmann::json::array()}, {"kind", nlohmann::json::array()}}).empty());   // both empty
        CHECK((load_kind({{"level", {0.1, 0.2}}, {"kind", {"Hold", 1}}}) == std::vector<float>{2.0f, 1.0f}));   // one length
    }
    // CombinedSource reads its own labels and the ordinals; a label in another
    // case and "add", a word that is no label, are refused
    {
        for (const char* word : {"mix", "MULTIPLY", "add"}) {
            const std::string msg = load_refused(one_node("CombinedSource", {{"operation", word}}));
            const bool named = msg.find(std::string("'operation' holds \"") + word + "\", which is not one of its labels") != std::string::npos;
            if (!named) std::printf("  CombinedSource operation %s: %s\n", word, msg.c_str());
            CHECK(named);
        }
        const std::pair<nlohmann::json, int> ops[] = {{"Mix", 0}, {"Multiply", 1}, {"Fade", 2}, {"Sum", 3},
                                                      {0, 0}, {1, 1}, {2, 2}, {3, 3}};
        for (const auto& [v, want] : ops) {
            const float got = load_setting("CombinedSource", "operation", v);
            if (got != float(want)) std::printf("  CombinedSource operation %s: got %g, want %d\n", v.dump().c_str(), got, want);
            CHECK(got == float(want));
        }
    }
    // every enum setting of every registered type loads from each of its own labels
    {
        int walked = 0;
        std::vector<std::string> skipped;
        for (const auto& e : reg.registered_types()) {
            if (e.name == "PerformNode") { skipped.push_back(e.name + " (loads as the voice's adapter, which declares no setting; run_perform_node_field_tests loads each label)"); continue; }
            std::shared_ptr<Component> s;
            try { s = reg.create(e.name); } catch (const std::exception&) { skipped.push_back(e.name + " (the registry cannot build it alone)"); continue; }
            if (!s) { skipped.push_back(e.name + " (the registry cannot build it alone)"); continue; }
            for (const auto& d : s->setting_descriptors()) {
                if (!d.enum_labels || d.type == SettingType::String) continue;   // a string setting takes no label
                for (int i = 0; d.enum_labels[i]; ++i) {
                    const float got = load_setting(e.name.c_str(), d.name, d.enum_labels[i]);
                    if (got != float(i)) std::printf("  %s.%s \"%s\": got %g, want %d\n", e.name.c_str(), d.name, d.enum_labels[i], got, i);
                    CHECK(got == float(i));
                    ++walked;
                }
            }
        }
        std::printf("  enum labels walked: %d; skipped: %zu\n", walked, skipped.size());
        for (const auto& s : skipped) std::printf("  skipped: %s\n", s.c_str());
        CHECK(walked > 0);
    }
}

// The three kinds on test-local nodes: a string setting, a list of signal
// inputs, and a node's own check of itself. The registry builds each under a
// name no patch uses; the load cases reach them through an instrument's
// build and a patch file's.
namespace three_kinds {
struct StringProbe final : Generator {
    Setting<float> gain{this, "gain", 1.0f, 0.0f, 2.0f};
    StringSetting label{this, "label", "x"};
    std::vector<std::string> changed;                 // each name on_setting_changed saw, in order
    static inline std::string lastLabel;              // the label after the last change, for a load whose node the test cannot reach
    void on_setting_changed(std::string_view n) override { changed.emplace_back(n); lastLabel = label.value; }
    float compute() override { return 0.0f; }
    const char* type_name() const override { return "StringProbe"; }
};
struct ListProbe final : Generator {
    InputList feeds{this, "feeds"};
    int hooks{0};                                     // on_input_list_changed calls for "feeds"
    void on_input_list_changed(std::string_view n) override { if (n == "feeds") ++hooks; }
    float compute() override {
        float sum = 0.0f;
        for (const auto& f : feeds.sources) sum += f->current();
        return sum;
    }
    const char* type_name() const override { return "ListProbe"; }
};
// Counts its prepares, starts and computes: a feed the owner's sweep must not reach.
struct CountingFeed final : Generator {
    int prepares{0}, starts{0}, nexts{0};
    void on_prepare(const RenderContext&) override { ++prepares; }
    void on_start(int) override { ++starts; }
    float compute() override { ++nexts; return 1.0f; }
    const char* type_name() const override { return "CountingFeed"; }
};
// Refuses itself when `refuse` is on, or when `refuseSeeded` is on and the
// registry built it with a seed (a Multiplex clone always is); two warnings
// when `warn` is on. Counts the calls of both.
struct CheckProbe final : Generator {
    Setting<bool> refuse{this, "refuse", false, false, true};
    Setting<bool> refuseSeeded{this, "refuseSeeded", false, false, true};
    Setting<bool> warn{this, "warn", false, false, true};
    bool seeded{false};
    static inline int validated{0}, warned{0};
    std::string validate() const override {
        ++validated;
        return refuse.value || (refuseSeeded.value && seeded) ? "probe says no" : "";
    }
    std::vector<std::string> warnings() const override {
        ++warned;
        if (!warn.value) return {};
        return {"probe warns once", "probe warns twice"};
    }
    float compute() override { return 0.0f; }
    const char* type_name() const override { return "CheckProbe"; }
};
// Refuses itself as built, whatever the file gives it.
struct RefusingProbe final : Generator {
    std::string validate() const override { return "probe refuses as built"; }
    float compute() override { return 0.0f; }
    const char* type_name() const override { return "RefusingProbe"; }
};
static void register_probes() {
    register_all_sources();
    auto& reg = SourceRegistry::instance();
    const TypeTag tag{.family = Family::Utility, .shelf = Shelf::Experimental};
    if (!reg.has("StringProbe")) reg.register_type("StringProbe", tag, [](auto) { return std::make_shared<StringProbe>(); });
    if (!reg.has("ListProbe"))   reg.register_type("ListProbe", tag, [](auto) { return std::make_shared<ListProbe>(); });
    if (!reg.has("CheckProbe"))
        reg.register_type("CheckProbe", tag, [](std::optional<uint32_t> seed) {
            auto p = std::make_shared<CheckProbe>();
            p->seeded = seed.has_value();
            return p;
        });
    if (!reg.has("RefusingProbe")) reg.register_type("RefusingProbe", tag, [](auto) { return std::make_shared<RefusingProbe>(); });
}
// The one-node instrument document as a plain sound: no instrument block.
static std::string plain(const std::string& instrumentDoc) {
    auto doc = nlohmann::json::parse(instrumentDoc);
    doc.erase("instrument");
    doc["seconds"] = 0.05;
    return doc.dump();
}
// Loads `text` as a patch file built as a sound (file) or as an instrument.
static void load(const std::string& text, bool file) {
    if (!file) { (void)load_inst(text); return; }
    ScratchPatch patch("renders/scratch/engine_tests_three_kinds.json", text);
    (void)load_sound(patch.path);
}
// Whether the load of `text` is refused with every one of `needles`.
static bool refused_naming(const std::string& text, bool file, std::initializer_list<const char*> needles) {
    try { load(text, file); }
    catch (const std::exception& e) {
        const std::string msg = e.what();
        bool all = true;
        for (const char* n : needles) all = all && msg.find(n) != std::string::npos;
        if (!all) std::printf("  refused for another reason: %s\n", msg.c_str());
        return all;
    }
    std::printf("  loaded: %s\n", text.c_str());
    return false;
}
static bool loads(const std::string& text, bool file) {
    try { load(text, file); return true; }
    catch (const std::exception& e) { std::printf("  load failed: %s\n", e.what()); return false; }
}
// A voice graph of three nodes: two sines `a` and `b` beside the node `n`.
static std::string list_doc(const nlohmann::json& feeds, const char* extraType = nullptr) {
    nlohmann::json nodes = nlohmann::json::array({
        nlohmann::json{{"id", "a"}, {"type", "SineSource"}},
        nlohmann::json{{"id", "b"}, {"type", "SineSource"}},
        nlohmann::json{{"id", "n"}, {"type", "ListProbe"}, {"params", {{"feeds", feeds}}}}});
    if (extraType) nodes.insert(nodes.begin(), nlohmann::json{{"id", "pk"}, {"type", extraType}});
    return nlohmann::json{{"sampleRate", 48000}, {"instrument", {{"polyphony", 1}}},
                          {"graph", {{"output", "a"}, {"nodes", nodes}}}}.dump();
}
}

static void run_string_setting_tests() {
    using namespace three_kinds;
    std::printf("the string setting: by name, structural, untouched by the float door\n");
    StringProbe p;
    const auto sd = p.setting_descriptors();
    CHECK(sd.size() == 2);
    CHECK(std::string(sd[0].name) == "gain" && sd[0].type == SettingType::Float && !sd[0].structural);
    CHECK(std::string(sd[1].name) == "label" && sd[1].type == SettingType::String && sd[1].structural);
    CHECK(sd[1].default_value == 0.0f && sd[1].min_value == 0.0f && sd[1].max_value == 0.0f && sd[1].enum_labels == nullptr);
    CHECK(p.get_string("label") == "x");
    p.set_string("label", "y");
    CHECK(p.label.value == "y" && p.get_string("label") == "y");
    CHECK((p.changed == std::vector<std::string>{"label"}));
    p.set_setting("label", 1.0f);                                  // the float door finds no string
    CHECK(p.label.value == "y" && p.get_setting("label") == 0.0f && p.changed.size() == 1);
    CHECK(!p.structural_setting("label"));                         // the float door's question; the descriptor says structural
    CHECK(p.get_string("nosuch").empty() && p.get_string("gain").empty());
    p.set_string("nosuch", "z");
    p.set_string("gain", "z");
    CHECK(p.changed.size() == 1 && p.get_setting("gain") == 1.0f);
    p.set_setting("gain", 0.5f);
    CHECK((p.changed == std::vector<std::string>{"label", "gain"}) && p.label.value == "y");
}

static void run_string_setting_loader_tests() {
    using namespace three_kinds;
    std::printf("the string setting through a load: a string read, a number and a pin refused\n");
    register_probes();
    {
        LoadedInstrument ip;
        auto n = enum_column::load_node("StringProbe", {{"label", "hello"}}, ip);
        CHECK(n != nullptr && n->get_string("label") == "hello");
        LoadedInstrument absent;
        auto d = enum_column::load_node("StringProbe", nlohmann::json::object(), absent);
        CHECK(d != nullptr && d->get_string("label") == "x");      // absent: the default
        StringProbe::lastLabel.clear();
        CHECK(loads(plain(enum_column::one_node("StringProbe", {{"label", "plain"}})), true));
        CHECK(StringProbe::lastLabel == "plain");
    }
    for (const bool file : {false, true}) {
        const std::string doc = enum_column::one_node("StringProbe", {{"label", 3}});
        CHECK(refused_naming(file ? plain(doc) : doc, file, {"node 'n' (StringProbe): 'label' takes a string, not 3"}));
        const std::string flag = enum_column::one_node("StringProbe", {{"label", true}});
        CHECK(refused_naming(file ? plain(flag) : flag, file, {"node 'n' (StringProbe): 'label' takes a string, not true"}));
    }
    {
        auto doc = nlohmann::json::parse(enum_column::one_node("StringProbe", nlohmann::json::object()));
        doc["graph"]["nodes"][1]["dynamicPins"] = {{"label", {{"ref", "out"}}}};
        for (const bool file : {false, true})
            CHECK(refused_naming(doc.dump(), file, {"graph.nodes[1].dynamicPins.label: node 'n' (StringProbe): 'label' is a structural setting on StringProbe"}));
    }
}

static void run_input_list_tests() {
    using namespace three_kinds;
    std::printf("the list of signal inputs: set replaces, add appends, clear empties, a part refused\n");
    ListProbe p;
    const auto id = p.input_descriptors();
    CHECK(id.size() == 1 && std::string(id[0].name) == "feeds" && id[0].multi && !id[0].slot);
    CHECK(p.param_descriptors().empty() && p.setting_descriptors().empty());
    CHECK(p.pins().size() == 1 && p.pins()[0]->pull == Pull::Manual);
    CrossfadeSource xf;                                                    // a single Input is not a list
    CHECK(!xf.input_descriptors().empty() && !xf.input_descriptors()[0].multi);
    auto a = std::make_shared<CountingFeed>(), b = std::make_shared<CountingFeed>(), c = std::make_shared<CountingFeed>();
    using Feeds = std::vector<std::shared_ptr<ValueSource>>;
    CHECK(p.set_param("feeds", a));
    CHECK((p.feeds.sources == Feeds{a}) && p.hooks == 1);
    CHECK(p.add_param("feeds", b) && p.add_param("feeds", c));
    CHECK((p.feeds.sources == Feeds{a, b, c}) && p.hooks == 3);
    CHECK(p.set_param("feeds", b));
    CHECK((p.feeds.sources == Feeds{b}) && p.hooks == 4);
    CHECK(p.set_param("feeds", nullptr));
    CHECK(p.feeds.sources.empty() && p.hooks == 5);
    CHECK(p.add_param("feeds", a) && p.add_param("feeds", nullptr));
    CHECK(p.feeds.sources.empty() && p.hooks == 7);
    CHECK(p.add_param("feeds", a) && p.clear_param("feeds"));
    CHECK(p.feeds.sources.empty() && p.hooks == 9);
    CHECK(p.add_param("feeds", a) && p.hooks == 10);
    auto part = std::make_shared<FormantPeaks>();
    CHECK(!p.set_param("feeds", part));
    CHECK(!p.add_param("feeds", part));
    CHECK((p.feeds.sources == Feeds{a}) && p.hooks == 10);
    CHECK(p.get_param("feeds") == nullptr && p.get_part("feeds") == nullptr);
    // the owner's sweep does not prepare, start or pull a list's feeds
    CHECK(p.add_param("feeds", b));
    p.prepare(RenderContext{48000});
    p.start(16);
    for (int i = 0; i < 8; ++i) (void)p.next();
    CHECK(a->nexts == 0 && b->nexts == 0 && a->prepares == 0 && b->prepares == 0 && a->starts == 0 && b->starts == 0);
}

static void run_input_list_loader_tests() {
    using namespace three_kinds;
    std::printf("the list of signal inputs through a load: refs and taps in file order\n");
    register_probes();
    auto feeds_of = [](const std::string& doc, LoadedInstrument& ip) -> ListProbe* {
        try { ip = load_inst(doc); }
        catch (const std::exception& e) { std::printf("  load failed: %s\n", e.what()); return nullptr; }
        return dynamic_cast<ListProbe*>(ip.instrument->voicePool[0].nodesById.at("n").get());
    };
    {
        LoadedInstrument ip;
        auto* n = feeds_of(list_doc(nlohmann::json::array({{{"ref", "a"}}, {{"tap", "b"}}, {{"ref", "b"}}})), ip);
        CHECK(n != nullptr);
        if (n) {
            const auto& nodes = ip.instrument->voicePool[0].nodesById;
            CHECK(n->feeds.sources.size() == 3);
            auto* tap = n->feeds.sources.size() == 3 ? dynamic_cast<TapSource*>(n->feeds.sources[1].get()) : nullptr;
            CHECK(n->feeds.sources.size() == 3 && n->feeds.sources[0] == nodes.at("a") && n->feeds.sources[2] == nodes.at("b"));
            CHECK(tap != nullptr && tap->source == nodes.at("b"));
        }
    }
    {
        LoadedInstrument ip;
        auto* n = feeds_of(list_doc({{"ref", "b"}}), ip);
        CHECK(n != nullptr && n->feeds.sources.size() == 1);
        if (n && n->feeds.sources.size() == 1) CHECK(n->feeds.sources[0] == ip.instrument->voicePool[0].nodesById.at("b"));
    }
    for (const bool file : {false, true}) {
        const std::string inList = list_doc(nlohmann::json::array({{{"ref", "a"}}, {{"ref", "pk"}}}), "FormantPeaks");
        const std::string alone = list_doc({{"ref", "pk"}}, "FormantPeaks");
        CHECK(refused_naming(inList, file, {"node 'n' (ListProbe): 'feeds' does not accept a FormantPeaks, a part, not a signal"}));
        CHECK(refused_naming(alone, file, {"node 'n' (ListProbe): 'feeds' does not accept a FormantPeaks, a part, not a signal"}));
    }
}

static void run_validate_tests() {
    using namespace three_kinds;
    std::printf("a node's own check: a refusal fails the load naming the node, a warning does not\n");
    register_probes();
    CheckProbe p;
    CHECK(p.validate().empty() && p.warnings().empty());
    p.refuse.value = true;
    p.warn.value = true;
    CHECK(p.validate() == "probe says no");
    CHECK((p.warnings() == std::vector<std::string>{"probe warns once", "probe warns twice"}));
    // a voice's node and a plain sound's node
    for (const bool file : {false, true}) {
        const std::string doc = enum_column::one_node("CheckProbe", {{"refuse", true}});
        CHECK(refused_naming(file ? plain(doc) : doc, file, {"node 'n' (CheckProbe): probe says no"}));
        const std::string warned = enum_column::one_node("CheckProbe", {{"warn", true}});
        CHECK(loads(file ? plain(warned) : warned, file));
        CHECK(loads(file ? plain(enum_column::one_node("CheckProbe", nullptr)) : enum_column::one_node("CheckProbe", nullptr), file));
        // a node with no params key is asked too
        const std::string bare = enum_column::one_node("RefusingProbe", nullptr);
        CHECK(nlohmann::json::parse(bare)["graph"]["nodes"][1].count("params") == 0);
        CHECK(refused_naming(file ? plain(bare) : bare, file, {"node 'n' (RefusingProbe): probe refuses as built"}));
    }
    // asked once per built node: one voice builds the probe once
    const auto module_builds = [](const std::string& doc) {
        try { (void)build_instrument(patch_door::read_text(doc)); return true; }
        catch (const std::exception& e) { std::printf("  load failed: %s\n", e.what()); return false; }
    };
    {
        const std::string doc = enum_column::one_node("CheckProbe", {{"warn", true}});
        const int v0 = CheckProbe::validated, w0 = CheckProbe::warned;
        CHECK(module_builds(doc));
        CHECK(CheckProbe::validated - v0 == 1 && CheckProbe::warned - w0 == 1);
    }
    // inside a Multiplex template: each clone's build asks its own node
    auto mux_doc = [](const nlohmann::json& params) {
        return nlohmann::json{{"sampleRate", 48000}, {"instrument", {{"polyphony", 1}}},
            {"graph", {{"output", "mux"}, {"nodes", nlohmann::json::array({
                nlohmann::json{{"id", "n"}, {"type", "CheckProbe"}, {"params", params}},
                nlohmann::json{{"id", "mux"}, {"type", "MultiplexSource"},
                               {"params", {{"count", 3}, {"source", {{"ref", "n"}}}}}}})}}}}.dump();
    };
    {
        const int v0 = CheckProbe::validated, w0 = CheckProbe::warned;
        CHECK(module_builds(mux_doc({{"warn", true}})));
        if (CheckProbe::validated - v0 != 4) std::printf("  validated %d times, want 4\n", CheckProbe::validated - v0);
        CHECK(CheckProbe::validated - v0 == 4 && CheckProbe::warned - w0 == 4);   // the template's node, then three clones
    }
    for (const bool file : {false, true}) {
        const std::string refused = mux_doc({{"refuse", true}}), seeded = mux_doc({{"refuseSeeded", true}});
        CHECK(refused_naming(file ? plain(refused) : refused, file, {"node 'n' (CheckProbe): probe says no"}));
        // seeded only in a clone: the main pass passes, the clone's build refuses
        const std::string alone = enum_column::one_node("CheckProbe", {{"refuseSeeded", true}});
        CHECK(loads(file ? plain(alone) : alone, file));
        CHECK(refused_naming(file ? plain(seeded) : seeded, file, {"node 'n' (CheckProbe): probe says no"}));
    }
}

// FormantBands against the band spectrum it was: the old contains() and
// get_gain() retyped over (startFreq, freqIncrement, gains) as they stood,
// and the consumer's gate `contains ? get_gain : 0` compared bit for bit with
// the predicate-folded gain over the frequencies where they could differ — the sub-bin
// below startFreq (where the old get_gain alone was NOT 0: a negative position
// truncates to bin 0 and interpolates with a negative fraction), startFreq
// itself and the floats beside it, every bin edge, the span's end and the
// floats beside it, one bin beyond — and a log grid; the two pins moved
// between samples by a stepping source.
namespace formant_bands_twin {
static bool old_contains(float sf, float fi, const std::vector<float>& gains, float freq) {
    return freq >= sf && freq <= sf + fi * float(int(gains.size()) - 1);
}
static float old_get_gain(float sf, float fi, const std::vector<float>& gains, float freq) {
    if (fi <= 0.0f) return 0.0f;

    float pos = (freq - sf) / fi;
    int idx = int(pos);
    if (idx < 0 || idx + 1 >= int(gains.size())) return 0.0f;

    float frac = pos - float(idx);
    return gains[idx] + (gains[idx + 1] - gains[idx]) * frac;
}
}
// A Manual pin's source that steps through a list, one value per next().
struct StepSource final : Generator {
    std::vector<float> values;
    size_t at{0};
    float compute() override { const float v = values[at % values.size()]; ++at; return v; }
    const char* type_name() const override { return "StepSource"; }
};
static void run_formant_bands_twin_tests() {
    using namespace formant_bands_twin;
    std::printf("formant bands twin: the predicate folded into the gain\n");
    const std::vector<float> gains = {0.1f, 0.9f, 0.3f, 1.0f, 0.5f, 0.8f, 0.2f, 0.7f};
    const std::vector<float> sfs = {300.0f, 100.0f, 250.5f, 1000.0f, 440.0f};
    const std::vector<float> fis = {150.0f, 50.0f, 37.25f, 0.0f, 33.3f};    // fi <= 0 gains nothing; 33.3 makes the span's end inexact
    FormantBands band;
    band.set_array("gains", gains);
    auto sf = std::make_shared<StepSource>(); sf->values = sfs;
    auto fi = std::make_shared<StepSource>(); fi->values = fis;
    CHECK(band.set_param("startFreq", sf) && band.set_param("freqIncrement", fi));
    band.prepare(RenderContext{48000}); band.start(4800);
    int rows = 0, bad = 0, subBinNonZero = 0, inBand = 0;
    for (size_t k = 0; k < sfs.size(); ++k) {
        band.next();                                                        // both pins step together
        const float s = sfs[k], d = fis[k];
        const float end = s + d * float(int(gains.size()) - 1);
        if (k == 4) CHECK(old_get_gain(s, d, gains, end) != 0.0f);         // the inexact increment lands the end inside the last bin, so the end comparison is pinned
        std::vector<float> freqs = formant_log_grid();
        for (float f : {s - d * 0.5f, s - d * 0.01f, std::nextafter(s, 0.0f), s, std::nextafter(s, 1.0e9f),
                        s + d * 0.5f, end - d * 0.5f, std::nextafter(end, 0.0f), end, std::nextafter(end, 1.0e9f),
                        end + d * 0.5f, end + d, end + d * 1.5f})
            freqs.push_back(f);
        for (int j = 0; j < int(gains.size()); ++j) freqs.push_back(s + d * float(j));
        for (float f : freqs) {
            ++rows;
            const bool oc = old_contains(s, d, gains, f);
            const float og = old_get_gain(s, d, gains, f);
            inBand += oc;
            if (!oc && og != 0.0f) ++subBinNonZero;                         // the sub-bin: the case the fold keeps
            if (!same_bits(band.gain(f), oc ? og : 0.0f)) ++bad;
        }
    }
    CHECK(bad == 0);
    CHECK(subBinNonZero > 0);                                               // the fold is load-bearing
    CHECK(inBand > 0 && inBand < rows);
    CHECK(rows == 5 * (2000 + 13 + 8));
    // an empty table: nothing contains, nothing gains
    FormantBands empty;
    empty.prepare(RenderContext{48000}); empty.start(4800); empty.next();
    CHECK(same_bits(empty.gain(0.0f), 0.0f) && same_bits(empty.gain(100.0f), 0.0f));
}

// FormantSequence against the crossfade it was: fPtr and fWeight as the old
// next() computed them, each neighbour gated by its own old contains(), over
// children of two kinds and blends 0, 0.37, 0.5, 1 (two children) and
// 0.25, 0.5, 0.75, 1 (three); where only one neighbour contains, the other
// contributes the 0 the old gate gave it.
static void run_formant_sequence_twin_tests() {
    using formant_peaks_twin::Row;
    std::printf("formant sequence twin: the crossfade over gated children\n");
    const std::vector<Row>& vowel = formant_peaks_twin::kSets[2];
    const std::vector<Row>& three = formant_peaks_twin::kSets[1];
    const std::vector<float> gains = {0.1f, 0.9f, 0.3f, 1.0f, 0.5f, 0.8f, 0.2f, 0.7f};
    const float bsf = 300.0f, bfi = 150.0f;
    auto old_child = [&](int which, float f) -> float {                     // 0: vowel rows, 1: bands, 2: three rows
        if (which == 1)
            return formant_bands_twin::old_contains(bsf, bfi, gains, f) ? formant_bands_twin::old_get_gain(bsf, bfi, gains, f) : 0.0f;
        const std::vector<Row>& rows = which == 0 ? vowel : three;
        return formant_peaks_twin::old_spectrum_contains(rows, f) ? formant_peaks_twin::old_spectrum_gain(rows, f) : 0.0f;
    };
    auto make_child = [&](int which) -> std::shared_ptr<Component> {
        if (which == 1) {
            auto b = std::make_shared<FormantBands>();
            b->set_param("startFreq", std::make_shared<ConstantSource>(bsf));
            b->set_param("freqIncrement", std::make_shared<ConstantSource>(bfi));
            b->set_array("gains", gains);
            return b;
        }
        auto p = std::make_shared<FormantPeaks>();
        formant_peaks_twin::fill(*p, which == 0 ? vowel : three);
        return p;
    };
    int rows = 0, bad = 0, oneSided = 0;
    for (const auto& kinds : std::vector<std::vector<int>>{{0, 1}, {1, 0, 2}}) {
        const std::vector<float> blends = kinds.size() == 2 ? std::vector<float>{0.0f, 0.37f, 0.5f, 1.0f}
                                                             : std::vector<float>{0.25f, 0.5f, 0.75f, 1.0f};
        for (float b : blends) {
            FormantSequence seq;
            seq.set_param("blend", std::make_shared<ConstantSource>(b));
            for (int k : kinds) CHECK(seq.add_param("spectra", make_child(k)));
            seq.prepare(RenderContext{48000}); seq.start(4800); seq.next();
            const int n = int(kinds.size());
            const float bb = std::clamp(b, 0.0f, 1.0f) * float(n - 1);
            const int fPtr = std::min(int(bb), n - 2);
            const float w = bb - float(fPtr);
            const int hi = std::min(fPtr + 1, n - 1);
            for (float f : formant_log_grid()) {
                ++rows;
                const float g0 = old_child(kinds[size_t(fPtr)], f);
                const float g1 = old_child(kinds[size_t(hi)], f);
                if ((g0 == 0.0f) != (g1 == 0.0f)) ++oneSided;
                if (!same_bits(seq.gain(f), g0 * (1.0f - w) + g1 * w)) ++bad;
            }
        }
    }
    CHECK(bad == 0);
    CHECK(oneSided > 0);
    CHECK(rows == 8 * 2000);
    // fewer than two children: the first alone, or nothing
    {
        FormantSequence one;
        one.set_param("blend", std::make_shared<ConstantSource>(0.7f));
        CHECK(one.add_param("spectra", make_child(1)));
        one.prepare(RenderContext{48000}); one.start(4800); one.next();
        for (float f : {250.0f, 300.0f, 600.0f, 1350.0f, 1500.0f})
            CHECK(same_bits(one.gain(f), old_child(1, f)));
        FormantSequence none;
        none.prepare(RenderContext{48000}); none.start(4800); none.next();
        CHECK(same_bits(none.gain(600.0f), 0.0f));
    }
}

// The consumer's factor `floor + weight × gain` against the gated form it
// replaces, `contains ? floor + weight × gain : floor`, over floor in
// {+0, −0, 0.2, 1}, weight in {0, 0.5, 1}, and (gain, contains) in {(0, in),
// (0.3, in), (1, in), (0, out)}: identical except the (−0, out-of-band) rows,
// one per weight, which differ in the sign of the zero only — the one
// departure, counted here.
static void run_formant_factor_twin_tests() {
    std::printf("formant factor twin: floor + weight x gain against the gated form\n");
    std::vector<float> floors = {0.0f, -0.0f, 0.2f, 1.0f};
    std::vector<float> weights = {0.0f, 0.5f, 1.0f};
    std::vector<std::pair<float, bool>> gains = {{0.0f, true}, {0.3f, true}, {1.0f, true}, {0.0f, false}};
    int rows = 0, differ = 0, signOnly = 0, negZeroOut = 0;
    for (float fl : floors)
        for (float wt : weights)
            for (const auto& [g, in] : gains) {
                ++rows;
                const float old = in ? fl + wt * g : fl;
                const float now = fl + wt * g;
                if (!same_bits(old, now)) {
                    ++differ;
                    if (same_value(old, now)) ++signOnly;
                    if (std::signbit(fl) && fl == 0.0f && !in) ++negZeroOut;
                }
            }
    CHECK(rows == 48);
    CHECK(differ == signOnly);                                              // never a value, only a sign
    CHECK(differ == 3 && negZeroOut == 3);                                  // −0 floor, out of band, each weight
}

// The base drives a formant's pins: FormantBands' two prepared and started
// once and advanced once per next() in declaration order; FormantPeaks has
// no Param pin and its hook caches the rows; a FormantSequence's blend and
// its child's pins through the hooks.
static void run_formant_lifecycle_tests() {
    std::printf("formants: the base drives the pins\n");
    RenderContext ctx{48000};
    {
        FormantBands band;
        auto a = std::make_shared<PinCounter>(), b = std::make_shared<PinCounter>();
        CHECK(band.set_param("startFreq", a) && band.set_param("freqIncrement", b));
        Formant& f = band;
        f.prepare(ctx); f.start(4800);
        for (int i = 0; i < 5; ++i) f.next();
        CHECK(a->ctxPrepares == 1 && a->starts == 1 && a->nexts == 5 && a->rate == 48000);
        CHECK(b->ctxPrepares == 1 && b->starts == 1 && b->nexts == 5 && b->rate == 48000);
    }
    {
        FormantBands band;
        std::vector<std::string> log;
        auto oa = std::make_shared<OrderProbe>(); oa->log = &log; oa->tag = "startFreq";
        auto ob = std::make_shared<OrderProbe>(); ob->log = &log; ob->tag = "freqIncrement";
        CHECK(band.set_param("startFreq", oa) && band.set_param("freqIncrement", ob));
        band.prepare(ctx); band.start(4800); band.next();
        CHECK(log == std::vector<std::string>({"startFreq", "freqIncrement"}));
    }
    {
        FormantPeaks peaks;
        peaks.set_array("frequency", {1000.0f}); peaks.set_array("gain", {1.0f});
        peaks.set_array("width", {500.0f}); peaks.set_array("power", {2.0f});
        CHECK(peaks.param_descriptors().empty());
        Formant& f = peaks;
        f.prepare(ctx); f.start(4800); f.next();
        CHECK(peaks.peak_count() == 1 && same_bits(f.gain(1000.0f), 1.0f));
    }
    {
        FormantSequence seq;
        auto bl = std::make_shared<PinCounter>(), c = std::make_shared<PinCounter>();
        auto band = std::make_shared<FormantBands>();
        CHECK(band->set_param("startFreq", c));
        CHECK(seq.set_param("blend", bl) && seq.add_param("spectra", band));
        seq.prepare(ctx); seq.start(4800);
        for (int i = 0; i < 3; ++i) seq.next();
        CHECK(bl->ctxPrepares == 1 && bl->starts == 1 && bl->nexts == 3);
        CHECK(c->ctxPrepares == 1 && c->starts == 1 && c->nexts == 3);
    }
}

// The partials family are parts: a CompositePartials holds its sets in a multi
// slot (add appends, set replaces, a signal is refused) and no partials type
// is a ValueSource.
static void run_partials_part_tests() {
    std::printf("partials: parts in slots\n");
    CompositePartials comp;
    auto fp = std::make_shared<FullPartials>();
    auto ep = std::make_shared<ExplicitPartials>();
    CHECK(comp.add_param("partials", fp) && comp.add_param("partials", ep));
    CHECK(comp.partial_count() == 30 + 16);
    CHECK(!comp.add_param("partials", std::make_shared<SineSource>()));          // a signal into the slot: refused
    CHECK(comp.set_param("partials", ep));                                        // set replaces
    CHECK(comp.partial_count() == 16 && comp.get_part("partials") == ep);
    register_all_sources();
    for (const char* type : {"FullPartials", "SequencePartials", "ExplicitPartials", "CompositePartials", "ExpandRule"})
        CHECK(std::dynamic_pointer_cast<ValueSource>(SourceRegistry::instance().create(type)) == nullptr);
    // the evolutions are parts too
    for (const char* type : {"PluckEvolution", "ReedEvolution", "BitRotateEvolution"})
        CHECK(std::dynamic_pointer_cast<ValueSource>(SourceRegistry::instance().create(type)) == nullptr);
}

// TargetEvolution is registered as a part: created by name, an evolution and
// not a signal, on the Experimental shelf of the Wavetable family.
static void run_target_evolution_registry_tests() {
    register_all_sources();
    auto& reg = SourceRegistry::instance();
    CHECK(std::dynamic_pointer_cast<ValueSource>(reg.create("TargetEvolution")) == nullptr);
    CHECK(std::dynamic_pointer_cast<WaveEvolution>(reg.create("TargetEvolution")) != nullptr);
    CHECK(reg.family("TargetEvolution") == Family::Wavetable && reg.shelf("TargetEvolution") == Shelf::Experimental);
    CHECK(reg.label("TargetEvolution") == "Target Evolution");
}

// The audit: every registered type is created once; exactly the named parts
// are not signals, every other type is. The twenty-three parts are the four
// Partials types and the expand rule, the three formant types, the
// thirteen evolutions and the two output parts.
static void run_component_root_tests() {
    std::printf("the root: parts and signals\n");
    register_all_sources();
    auto& reg = SourceRegistry::instance();
    CHECK(kPartTypes.size() == 23);
    int seen = 0, parts = 0;
    for (const auto& entry : reg.registered_types()) {
        const std::string& name = entry.name;
        auto c = reg.create(name);
        const bool isPart = std::dynamic_pointer_cast<ValueSource>(c) == nullptr;
        if (isPart != (kPartTypes.count(name) == 1))
            std::printf("  %s: %s\n", name.c_str(), isPart ? "a part the list lacks" : "a signal the list names");
        CHECK(isPart == (kPartTypes.count(name) == 1));
        ++seen; parts += isPart;
    }
    CHECK(parts == 23);
    CHECK(seen == int(reg.registered_types().size()));
}

// A node that counts the reseeds the live note-on delivers: the voice's reseed
// walk must reach every signal, on the audio thread, with no cast.
struct ReseedProbe final : Generator {
    int reseeds{0};
    float compute() override { return 1.0f; }
    void reseed() override { ++reseeds; }
    const char* type_name() const override { return "ReseedProbe"; }
};

// Whether build_sound refuses a patch file with a message holding
// `needle`; a refusal for another reason is printed.
static bool file_fails_naming(const char* path, const char* text, const char* needle) {
    ScratchPatch file(path, text);
    bool named = false;
    try { (void)load_sound(file.path); }
    catch (const std::exception& e) {
        const std::string msg = e.what();
        if (msg.find(needle) == std::string::npos) std::printf("  load failed for another reason: %s\n", msg.c_str());
        named = msg.find(needle) != std::string::npos;
    }
    return named;
}

// The review's gaps in a load: a refused slot wire says what the slot
// takes; the deleted branch's alias key is refused by name; a part in every
// signal position fails the load with its path's message.
static void run_review_gap_tests() {
    std::printf("the review's gaps: messages, the alias, the output positions\n");
    // a signal into a slot: the refusal names what the slot takes
    const char* sineIntoPartials = R"({"sampleRate":48000,"instrument":{"polyphony":1},"graph":{"output":"add","nodes":[
      {"id":"osc","type":"SineSource","params":{"frequency":220.0}},
      {"id":"add","type":"AdditiveSource","params":{"frequency":220.0,"amplitude":0.5,"partials":{"ref":"osc"}}}]}})";
    CHECK(load_fails_naming(sineIntoPartials, "does not accept a SineSource"));
    CHECK(load_fails_naming(sineIntoPartials, "takes a PartialSet part"));
    // a deleted additive type fails the load by name, with the hint
    const char* deletedType = R"({"sampleRate":48000,"instrument":{"polyphony":1},"graph":{"output":"as2","nodes":[
      {"id":"as2","type":"AdditiveSource2","params":{"frequency":220.0}}]}})";
    CHECK(load_fails_naming(deletedType, "Unknown node type: AdditiveSource2"));
    CHECK(load_fails_naming(deletedType, "author an AdditiveSource"));
    // a FormantSequence keyed "formants" (the alias the deleted branch read) is refused by name
    const char* seqAlias = R"({"sampleRate":48000,"instrument":{"polyphony":1},"graph":{"output":"add","nodes":[
      {"id":"parts","type":"FullPartials","params":{"maxPartials":4}},
      {"id":"spec","type":"FormantPeaks","params":{"frequency":[500.0],"gain":[1.0],"width":[500.0],"power":[2.0]}},
      {"id":"seq","type":"FormantSequence","params":{"blend":0.5,"formants":[{"ref":"spec"}]}},
      {"id":"add","type":"AdditiveSource","params":{"frequency":220.0,"amplitude":0.5,"partials":{"ref":"parts"},"formant":{"ref":"seq"},"formantWeight":1.0}}]}})";
    CHECK(load_fails_naming(seqAlias, "node 'seq' (FormantSequence): unknown key 'formants'"));
    // a part as the instrument's output, as a mixer-mode output, as a channel's source
    const char* partAsVoiceOutput = R"({"sampleRate":48000,"instrument":{"polyphony":1},"graph":{"output":"parts","nodes":[
      {"id":"parts","type":"FullPartials","params":{"maxPartials":4}}]}})";
    CHECK(load_fails_naming(partAsVoiceOutput, "not a signal"));
    CHECK(file_fails_naming("renders/scratch/_part_as_output.json",
        R"({"sampleRate":48000,"seconds":0.1,"graph":{"output":"parts","nodes":[{"id":"parts","type":"FullPartials","params":{"maxPartials":4}}]}})",
        "'parts' is a FullPartials, a part, not a signal"));
    CHECK(file_fails_naming("renders/scratch/_part_as_channel.json",
        R"({"sampleRate":48000,"seconds":0.1,"graph":{"output":"mix","nodes":[
          {"id":"parts","type":"FullPartials","params":{"maxPartials":4}},
          {"id":"ch1","type":"SoundChannel","params":{"source":{"ref":"parts"},"volume":1.0,"pan":0.0}},
          {"id":"mix","type":"StereoMixer","params":{"channels":[{"ref":"ch1"}],"gainL":1.0,"gainR":1.0}}]}})",
        "node 'ch1' (SoundChannel): 'source' does not accept a FullPartials, a part, not a signal"));
    // a channel as the output, and a channel whose source names no node
    CHECK(file_fails_naming("renders/scratch/_channel_as_output.json",
        R"({"sampleRate":48000,"seconds":0.1,"graph":{"output":"ch1","nodes":[
          {"id":"osc","type":"SineSource","params":{"frequency":220.0}},
          {"id":"ch1","type":"SoundChannel","params":{"source":{"ref":"osc"},"volume":1.0,"pan":0.0}}]}})",
        "'ch1' is a SoundChannel, a part, not a signal"));
    CHECK(file_fails_naming("renders/scratch/_channel_source_missing.json",
        R"({"sampleRate":48000,"seconds":0.1,"graph":{"output":"mix","nodes":[
          {"id":"ch1","type":"SoundChannel","params":{"source":{"ref":"nowhere"},"volume":1.0,"pan":0.0}},
          {"id":"mix","type":"StereoMixer","params":{"channels":[{"ref":"ch1"}],"gainL":1.0,"gainR":1.0}}]}})",
        "node 'ch1' (SoundChannel): 'source' reads 'nowhere', which names no node"));
}

// A patch file in the scratch folder built as a sound, rendered for
// its own frame count into `buf`. False, with the reason printed, when the
// load or the render throws.
static bool render_scratch_patch(const char* path, const std::string& text, std::vector<float>& buf) {
    ScratchPatch file(path, text);
    bool rendered = false;
    try {
        auto p = load_sound(file.path);
        buf.assign(2 * size_t(p.frames), 0.0f);
        p.mixer->render(buf.data(), p.frames);
        rendered = true;
    }
    catch (const std::exception& e) { std::printf("  load failed: %s\n", e.what()); }
    return rendered;
}

// A channel's source is any signal the graph builds: each of six signal types
// feeds the one SoundChannel of a StereoMixer and renders, every sample finite.
static void run_channel_source_tests() {
    std::printf("channel sources: six signal types feed a SoundChannel and render\n");
    auto through_channel = [](const char* node, const char* id) {
        return std::string(R"({"sampleRate":48000,"seconds":0.05,"graph":{"output":"mix","nodes":[)") + node
            + R"(,{"id":"ch1","type":"SoundChannel","params":{"source":{"ref":")" + id
            + R"("},"volume":1.0,"pan":0.0}},)"
            + R"({"id":"mix","type":"StereoMixer","params":{"channels":[{"ref":"ch1"}],"gainL":1.0,"gainR":1.0}}]}})";
    };
    auto all_finite = [](const std::vector<float>& b) {
        for (float s : b) if (!std::isfinite(s)) return false;
        return !b.empty();
    };
    struct Case { const char* path; const char* node; const char* id; };
    const Case cases[] = {
        {"renders/scratch/_channel_envelope.json",
         R"({"id":"env","type":"Envelope","params":{"preset":"ar","attack":0.2}})", "env"},
        {"renders/scratch/_channel_curve.json",
         R"({"id":"cv","type":"CurveNode","params":{"x":[0.0,1.0],"y":[0.25,0.75],"source":0.5}})", "cv"},
        {"renders/scratch/_channel_range.json",
         R"({"id":"rng","type":"RangeSource","params":{"min":0.0,"max":1.0,"var":0.5}})", "rng"},
        {"renders/scratch/_channel_vibrato.json",
         R"({"id":"vib","type":"Vibrato","params":{"frequency":220.0}})", "vib"},
        // No vocabulary in a mixer-mode patch: the gate never fires.
        {"renders/scratch/_channel_name_gate.json",
         R"({"id":"gate","type":"NameGate","params":{"name":"pluck"}})", "gate"},
    };
    for (const auto& c : cases) {
        std::vector<float> buf;
        CHECK(render_scratch_patch(c.path, through_channel(c.node, c.id), buf));
        CHECK(all_finite(buf));
    }
    // A constant signal renders as one non-zero value on every left sample:
    // the adapter really pulls the node.
    std::vector<float> buf;
    CHECK(render_scratch_patch("renders/scratch/_channel_var.json",
        through_channel(R"({"id":"var","type":"VarSource","params":{"val":0.5,"var":0.0}})", "var"), buf));
    CHECK(all_finite(buf));
    bool constant = !buf.empty() && buf[0] != 0.0f;
    for (size_t i = 0; i < buf.size(); i += 2) constant = constant && buf[i] == buf[0];
    CHECK(constant);
}

// The channel adapter is its graph's pull loop: it gives the graph a clock, so
// a node two consumers pull computes once a sample, and it starts the graph
// over the buffer. Two channels cannot share a source.
static void run_channel_clock_tests() {
    std::printf("the channel adapter: its clock, its start, one source per channel\n");
    auto product_of = [](const char* nodes, const char* a, const char* b) {
        return std::string(R"({"sampleRate":48000,"seconds":0.05,"graph":{"output":"mix","nodes":[)") + nodes
            + R"(,{"id":"prod","type":"CombinedSource","params":{"operation":1,"gainAdj":0.0,"source1":{"ref":")" + a
            + R"("},"source2":{"ref":")" + b + R"("}}},)"
            + R"({"id":"ch1","type":"SoundChannel","params":{"source":{"ref":"prod"},"volume":1.0,"pan":0.0}},)"
            + R"({"id":"mix","type":"StereoMixer","params":{"channels":[{"ref":"ch1"}],"gainL":1.0,"gainR":1.0}}]}})";
    };
    // One envelope on both inputs renders as two identical envelopes do.
    std::vector<float> shared, twin;
    CHECK(render_scratch_patch("renders/scratch/_channel_shared_env.json",
        product_of(R"({"id":"env","type":"Envelope","params":{"preset":"ar","attack":0.2}})", "env", "env"), shared));
    CHECK(render_scratch_patch("renders/scratch/_channel_twin_env.json",
        product_of(R"({"id":"env1","type":"Envelope","params":{"preset":"ar","attack":0.2}},)"
                   R"({"id":"env2","type":"Envelope","params":{"preset":"ar","attack":0.2}})", "env1", "env2"), twin));
    CHECK(!shared.empty() && shared.size() == twin.size()
          && std::memcmp(shared.data(), twin.data(), shared.size() * sizeof(float)) == 0);
    // The envelope moves across the buffer: it was started over it.
    bool moves = false;
    for (size_t i = 2; i < twin.size(); i += 2) moves = moves || twin[i] != twin[0];
    CHECK(moves);
    // Two channels naming one source are refused.
    CHECK(file_fails_naming("renders/scratch/_channels_one_source.json",
        R"({"sampleRate":48000,"seconds":0.1,"graph":{"output":"mix","nodes":[
          {"id":"osc","type":"SineSource","params":{"frequency":220.0}},
          {"id":"ch1","type":"SoundChannel","params":{"source":{"ref":"osc"},"volume":1.0,"pan":0.0}},
          {"id":"ch2","type":"SoundChannel","params":{"source":{"ref":"osc"},"volume":1.0,"pan":0.0}},
          {"id":"mix","type":"StereoMixer","params":{"channels":[{"ref":"ch1"},{"ref":"ch2"}],"gainL":1.0,"gainR":1.0}}]}})",
        "'osc' is reached from channel 'ch1' and from channel 'ch2'"));
}

// Whether a mixer signal is a constant of `v` (prepared, not yet rendered:
// its current value is the constant's).
static bool is_constant_of(const std::shared_ptr<ValueSource>& s, float v) {
    return s && std::string(s->type_name()) == "Constant" && s->current() == v;
}

// The instrument form's mixer is one unity channel: a node named "mix" that
// nothing consumes puts no gain on it.
static void run_instrument_mixer_unity_tests() {
    std::printf("the instrument form's mixer: unity, whatever a node named mix says\n");
    const std::string head = R"({"sampleRate":48000,"instrument":{"polyphony":1},
      "score":[{"note":60,"time":0,"duration":0.05}],"graph":{"output":"osc","nodes":[
      {"id":"osc","type":"SineSource","params":{"frequency":220.0,"amplitude":0.5}})";
    std::vector<float> plain, withMix;
    CHECK(render_scratch_patch("renders/scratch/_instrument_no_mix.json", head + "]}}", plain));
    CHECK(render_scratch_patch("renders/scratch/_instrument_quarter_mix.json",
        head + R"(,{"id":"mix","type":"StereoMixer","params":{"channels":[],"gainL":0.25,"gainR":0.25}}]}})",
        withMix));
    CHECK(!plain.empty() && plain.size() == withMix.size()
          && std::memcmp(plain.data(), withMix.data(), plain.size() * sizeof(float)) == 0);
    bool silent = true;
    for (float s : plain) if (s != 0.0f) silent = false;
    CHECK(!silent);
    // The mixer itself: one channel, unity gains and volume, centre pan.
    ScratchPatch file("renders/scratch/_instrument_unity.json", head + "]}}");
    bool unity = false;
    try {
        auto built = load_sound(file.path);
        const auto& m = *built.mixer;
        unity = m.channels.size() == 1
             && is_constant_of(m.gainL, 1.0f) && is_constant_of(m.gainR, 1.0f)
             && is_constant_of(m.channels[0].volume, 1.0f) && is_constant_of(m.channels[0].pan, 0.0f);
    }
    catch (const std::exception& e) { std::printf("  load failed: %s\n", e.what()); }
    CHECK(unity);
}

// The review's gaps, the lifecycle's: parts nested three deep are prepared and
// started through their owners; the live note-on's reseed reaches a signal.
static void run_review_gap_lifecycle_tests() {
    std::printf("the review's gaps: nesting, the live reseed\n");
    // parts three deep: the owner's prepare, start and next reach the innermost pins
    {
        RenderContext ctx{48000};
        AdditiveSource f;
        auto fm = std::make_shared<FormantBands>();
        auto freq = std::make_shared<PinCounter>();
        fm->set_param("startFreq", freq);
        auto inner = std::make_shared<FormantSequence>();                     // a sequence is a Formant: it nests
        CHECK(inner->add_param("spectra", fm));
        auto seq = std::make_shared<FormantSequence>();
        CHECK(seq->add_param("spectra", inner));
        CHECK(f.set_param("partials", std::make_shared<FullPartials>()));
        CHECK(f.set_param("formant", seq));
        f.set_param("formantWeight", std::make_shared<ConstantSource>(1.0f));
        f.prepare(ctx); f.start(4800);
        for (int i = 0; i < 3; ++i) f.next();
        CHECK(freq->rate == 48000 && freq->starts == 1 && freq->nexts == 3);
    }
    // the live note-on's reseed reaches the voice's signals
    {
        register_all_sources();
        auto& reg = SourceRegistry::instance();
        if (!reg.has("ReseedProbe"))
            reg.register_type("ReseedProbe", {.family = Family::Utility, .shelf = Shelf::Experimental}, [](auto) { return std::make_shared<ReseedProbe>(); });
        const char* probed = R"({"sampleRate":48000,"instrument":{"polyphony":1},"graph":{"output":"p","nodes":[
          {"id":"parts","type":"FullPartials","params":{"maxPartials":4}},
          {"id":"p","type":"ReseedProbe"}]}})";
        auto ip = load_inst(probed);
        PitchedInstrument& inst = *ip.instrument;
        inst.release_voice(inst.acquire_voice());
        const int slot = std::max(0, inst.acquire_voice());
        (void)inst.prepare_voice_at(slot, 60.0f, 0.8f, 0.1f);
        inst.continue_voice_live(slot, 62.0f, 0.8f, 0.1f, 261.6f, 0.0f);
        auto* probe = dynamic_cast<ReseedProbe*>(inst.voicePool[size_t(slot)].nodesById.at("p").get());
        CHECK(probe != nullptr && probe->reseeds == 1);
        inst.release_voice(slot);
    }
}

// The one classification, declared where a type is registered: every type's
// family, label and shelf (the families table in docs/architecture/part3-nodes.md;
// the shelf is today's menu — everything not in a main family menu is Experimental,
// and a type the host makes by an item of its own is on no shelf), the label
// derived from the type name where today's menu had none, the loading alias
// still building its type, and labels unique within a family and shelf.
struct FamilyRow { const char* name; Family family; const char* label; Shelf shelf; };
static const FamilyRow kFamilyTable[] = {
    {"SineSource", Family::Oscillator, "Sine", Shelf::Main},
    {"SawSource", Family::Oscillator, "Saw", Shelf::Main},
    {"PulseSource", Family::Oscillator, "Pulse", Shelf::Main},
    {"TriangleSource", Family::Oscillator, "Triangle", Shelf::Main},
    {"FMSource", Family::Oscillator, "FM", Shelf::Main},
    {"RedNoiseSource", Family::Noise, "Red", Shelf::Main},
    {"PinkNoiseSource", Family::Noise, "Pink", Shelf::Main},
    {"WhiteNoiseSource", Family::Noise, "White", Shelf::Main},
    {"BlueNoiseSource", Family::Noise, "Blue", Shelf::Main},
    {"VioletNoiseSource", Family::Noise, "Violet", Shelf::Main},
    {"VelvetNoiseSource", Family::Noise, "Velvet", Shelf::Main},
    {"PerlinNoiseSource", Family::Noise, "Perlin", Shelf::Main},
    {"CrackleNoiseSource", Family::Noise, "Crackle", Shelf::Main},
    {"WavetableSource", Family::Wavetable, "Wavetable", Shelf::Main},
    {"HybridKSSource", Family::Wavetable, "Hybrid KS", Shelf::Main},
    {"EKSEvolution", Family::Wavetable, "EKS Evolution", Shelf::Main},
    {"PluckEvolution", Family::Wavetable, "Pluck Evolution", Shelf::Main},
    {"AveragingEvolution", Family::Wavetable, "Averaging Evolution", Shelf::Main},
    {"AdditiveSource", Family::Additive, "Additive", Shelf::Main},
    {"FullPartials", Family::Additive, "Full Partials", Shelf::Main},
    {"SequencePartials", Family::Additive, "Sequence Partials", Shelf::Main},
    {"ExplicitPartials", Family::Additive, "Explicit Partials", Shelf::Main},
    {"CompositePartials", Family::Additive, "Composite Partials", Shelf::Main},
    {"ExpandRule", Family::Additive, "Partials Expand Rule", Shelf::Main},
    {"FormantPeaks", Family::Additive, "Formant Peaks", Shelf::Main},
    {"FormantBands", Family::Additive, "Formant Bands", Shelf::Main},
    {"FormantSequence", Family::Additive, "Formant Sequence", Shelf::Main},
    {"KSString", Family::Physical, "KS String", Shelf::Main},
    {"AllpassResonator", Family::Physical, "Allpass Resonator", Shelf::Main},
    {"DelayLine", Family::Physical, "Delay Line", Shelf::Main},
    {"DelayFilter", Family::Filter, "Delay", Shelf::Main},
    {"Reverb", Family::Filter, "Reverb", Shelf::Main},
    {"Limiter", Family::Filter, "Limiter", Shelf::Main},
    {"HammerBank", Family::Filter, "Hammer Bank", Shelf::Main},
    {"DistortedSource", Family::Filter, "Distortion", Shelf::Main},
    {"BWBandpassFilter", Family::Filter, "BW Bandpass", Shelf::Main},
    {"BWLowpassFilter", Family::Filter, "BW Lowpass", Shelf::Main},
    {"BWHighpassFilter", Family::Filter, "BW Highpass", Shelf::Main},
    {"SVFSource", Family::Filter, "SVF (State Variable)", Shelf::Main},
    {"Shaper", Family::Filter, "Shaper", Shelf::Main},
    {"Envelope", Family::Envelope, "Envelope", Shelf::Main},
    {"Vibrato", Family::Envelope, "Vibrato", Shelf::Main},
    {"PhasedValueSource", Family::Envelope, "Phased", Shelf::Main},
    {"SegmentSource", Family::Envelope, "Segment", Shelf::Main},
    {"RepeatingSource", Family::Modulator, "Repeating", Shelf::Main},
    {"VarSource", Family::Modulator, "Var", Shelf::Main},
    {"RangeSource", Family::Modulator, "Range", Shelf::Main},
    {"CurveNode", Family::Modulator, "Curve", Shelf::Main},
    {"CombinedSource", Family::Combiner, "Combined", Shelf::Main},
    {"CrossfadeSource", Family::Combiner, "Crossfade", Shelf::Main},
    {"MultiSource", Family::Combiner, "Multi", Shelf::Main},
    {"MultiplexSource", Family::Combiner, "Multiplex", Shelf::Main},
    {"Wormhole", Family::Utility, "Wormhole", Shelf::Main},
    {"LayeredRedNoiseSource", Family::Noise, "Layered Red", Shelf::Experimental},
    {"MurmurationNoiseSource", Family::Noise, "Murmuration", Shelf::Experimental},
    {"WanderNoiseSource", Family::Noise, "Wander 1", Shelf::Experimental},
    {"WanderNoise2Source", Family::Noise, "Wander 2", Shelf::Experimental},
    {"WanderNoise3Source", Family::Noise, "Wander 3", Shelf::Experimental},
    {"BowedStringEvolution", Family::Wavetable, "Bowed String Evolution", Shelf::Experimental},
    {"ReedEvolution", Family::Wavetable, "Reed Evolution", Shelf::Experimental},
    {"BrassEvolution", Family::Wavetable, "Brass Evolution", Shelf::Experimental},
    {"ReactionDiffusionEvolution", Family::Wavetable, "Reaction-Diffusion (Gray-Scott)", Shelf::Experimental},
    {"SortErosionEvolution", Family::Wavetable, "Sort Erosion (->saw)", Shelf::Experimental},
    {"CellularAutomatonEvolution", Family::Wavetable, "Cellular Automaton (Wolfram)", Shelf::Experimental},
    {"HistogramEqualizeEvolution", Family::Wavetable, "Histogram Equalize", Shelf::Experimental},
    {"BezierPullEvolution", Family::Wavetable, "Bezier Pull (->curve)", Shelf::Experimental},
    {"BitRotateEvolution", Family::Wavetable, "Bit Rotate (glitch)", Shelf::Experimental},
    {"TargetEvolution", Family::Wavetable, "Target Evolution", Shelf::Experimental},
    {"Mesh2D", Family::Physical, "Mesh 2D", Shelf::Experimental},
    {"PierceFilter", Family::Physical, "Pierce Filter", Shelf::Experimental},
    {"BowTable", Family::Physical, "Bow Table", Shelf::Experimental},
    {"Biquad", Family::Filter, "Biquad", Shelf::Experimental},
    {"NameGate", Family::Modulator, "Name Gate", Shelf::Experimental},
    {"StaticVarSource", Family::Utility, "Static Var", Shelf::Experimental},
    {"StaticRangeSource", Family::Utility, "Static Range", Shelf::Experimental},
    {"PerformNode", Family::Modulator, "Note Field", Shelf::None},
    {"SoundChannel", Family::Output, "Channel", Shelf::None},
    {"StereoMixer", Family::Output, "Mixer", Shelf::None},
};

// The registry refuses a name it does not know, naming it; label() and shelf()
// share family()'s lookup, so the one path stands for the three.
static bool throws_with(const SourceRegistry& reg, const char* name) {
    try { (void)reg.family(name); } catch (const std::exception& e) {
        return std::string(e.what()).find(name) != std::string::npos;
    }
    return false;
}

static void run_registry_family_tests() {
    std::printf("the registry: families, labels, shelves\n");
    register_all_sources();
    auto& reg = SourceRegistry::instance();
    int main = 0, experimental = 0, none = 0;
    for (const auto& row : kFamilyTable) {
        const TypeTag* tag = reg.find(row.name);
        if (!tag) { std::printf("  in kFamilyTable but not registered: %s\n", row.name); CHECK(tag != nullptr); continue; }
        const std::string label = reg.label(row.name);
        if (tag->family != row.family || label != row.label || tag->shelf != row.shelf)
            std::printf("  %s: %s / %s / %d\n", row.name, family_name(tag->family), label.c_str(), int(tag->shelf));
        CHECK(tag->family == row.family);
        CHECK(label == row.label);
        CHECK(tag->shelf == row.shelf);
        (row.shelf == Shelf::Main ? main : row.shelf == Shelf::Experimental ? experimental : none)++;
    }
    CHECK(main == 53 && experimental == 22 && none == 3);
    for (const auto& e : reg.registered_types()) {                             // every registered type is in the table
        bool inTable = false;
        for (const auto& row : kFamilyTable) inTable = inTable || e.name == row.name;
        if (!inTable) std::printf("  registered but not in kFamilyTable: %s\n", e.name.c_str());
        CHECK(inTable);
    }
    // I11-style guard: a label is given only where it differs from the derivation
    for (const auto& e : reg.registered_types()) {
        if (e.tag.label && std::string(e.tag.label) == derived_label(e.name))
            std::printf("  redundant label on %s: %s\n", e.name.c_str(), e.tag.label);
        CHECK(!e.tag.label || std::string(e.tag.label) != derived_label(e.name));
    }
    // the derivation: the suffix off, a space before each word, acronyms kept
    CHECK(derived_label("HybridKSSource") == "Hybrid KS" && derived_label("BWLowpassFilter") == "BW Lowpass");
    CHECK(derived_label("Mesh2D") == "Mesh 2D" && derived_label("PierceFilterSource") == "Pierce Filter");
    CHECK(derived_label("Envelope") == "Envelope" && derived_label("CurveNode") == "Curve");
    CHECK(derived_label("WanderNoise2Source") == "Wander Noise 2" && derived_label("StaticVarSource") == "Static Var");
    // the alias still builds its type, on the Experimental shelf of its family
    // labels are non-empty and unique within a family and shelf; types_in walks every type once
    int walked = 0;
    for (Family f : kFamilyOrder)
        for (Shelf s : {Shelf::Main, Shelf::Experimental, Shelf::None}) {
            std::set<std::string> labels;
            for (const auto& e : reg.types_in(f, s)) {
                CHECK(e.tag.family == f);
                CHECK(!reg.label(e.name).empty());
                CHECK(labels.insert(reg.label(e.name)).second);
                ++walked;
            }
        }
    CHECK(walked == 78);
    CHECK(std::string(family_name(Family::Oscillator)) == "Oscillators" && std::string(family_name(Family::Noise)) == "Noise"
          && std::string(family_name(Family::Filter)) == "Filters" && std::string(family_name(Family::Utility)) == "Utility");
    CHECK(reg.find("NoSuchType") == nullptr && reg.find("SineSource") != nullptr);
    CHECK(reg.find("SineSource")->family == Family::Oscillator);
    CHECK(throws_with(reg, "NoSuchType"));                                      // unknown: no invented answer
    // the Envelope family is what the editor's envelope views read: both shelves, exactly these
    std::set<std::string> envelopes;
    for (Shelf s : {Shelf::Main, Shelf::Experimental})
        for (const auto& e : reg.types_in(Family::Envelope, s)) envelopes.insert(e.name);
    CHECK((envelopes == std::set<std::string>{"Envelope", "PhasedValueSource", "SegmentSource", "Vibrato"}));
}

// The menu's content is the registry's: for each family and shelf, the types
// in the order the menu lists them — today's menu order, which registration
// order now carries. This is what both the create menu and the Replace-with
// menu draw.
struct MenuRow { Family family; Shelf shelf; std::vector<const char*> names; };
static const MenuRow kMenuTable[] = {
    {Family::Oscillator, Shelf::Main, {"SineSource", "SawSource", "PulseSource", "TriangleSource", "FMSource"}},
    {Family::Noise, Shelf::Main, {"RedNoiseSource", "PinkNoiseSource", "WhiteNoiseSource", "BlueNoiseSource", "VioletNoiseSource", "VelvetNoiseSource", "PerlinNoiseSource", "CrackleNoiseSource"}},
    {Family::Noise, Shelf::Experimental, {"LayeredRedNoiseSource", "MurmurationNoiseSource", "WanderNoiseSource", "WanderNoise2Source", "WanderNoise3Source"}},
    {Family::Wavetable, Shelf::Main, {"WavetableSource", "HybridKSSource", "EKSEvolution", "PluckEvolution", "AveragingEvolution"}},
    {Family::Wavetable, Shelf::Experimental, {"BowedStringEvolution", "ReedEvolution", "BrassEvolution", "ReactionDiffusionEvolution", "SortErosionEvolution", "CellularAutomatonEvolution", "HistogramEqualizeEvolution", "BezierPullEvolution", "BitRotateEvolution", "TargetEvolution"}},
    {Family::Additive, Shelf::Main, {"AdditiveSource", "FullPartials", "SequencePartials", "ExplicitPartials", "CompositePartials", "ExpandRule", "FormantPeaks", "FormantBands", "FormantSequence"}},
    {Family::Physical, Shelf::Main, {"KSString", "AllpassResonator", "DelayLine"}},
    {Family::Physical, Shelf::Experimental, {"Mesh2D", "PierceFilter", "BowTable"}},
    {Family::Filter, Shelf::Main, {"DelayFilter", "Reverb", "Limiter", "HammerBank", "DistortedSource", "BWBandpassFilter", "BWLowpassFilter", "BWHighpassFilter", "SVFSource", "Shaper"}},
    {Family::Filter, Shelf::Experimental, {"Biquad"}},
    {Family::Envelope, Shelf::Main, {"Envelope", "Vibrato", "PhasedValueSource", "SegmentSource"}},
    {Family::Modulator, Shelf::Main, {"RepeatingSource", "VarSource", "RangeSource", "CurveNode"}},
    {Family::Modulator, Shelf::Experimental, {"NameGate"}},
    {Family::Combiner, Shelf::Main, {"CombinedSource", "CrossfadeSource", "MultiSource", "MultiplexSource"}},
    {Family::Utility, Shelf::Main, {"Wormhole"}},
    {Family::Utility, Shelf::Experimental, {"StaticVarSource", "StaticRangeSource"}},
};

static void run_menu_content_tests() {
    std::printf("the menu: each family's types in menu order\n");
    register_all_sources();
    auto& reg = SourceRegistry::instance();
    for (const auto& row : kMenuTable) {
        std::vector<std::string> got;
        for (const auto& e : reg.types_in(row.family, row.shelf)) got.push_back(e.name);
        std::vector<std::string> want(row.names.begin(), row.names.end());
        if (got != want) {
            std::printf("  %s/%s: got", family_name(row.family), row.shelf == Shelf::Main ? "main" : "experimental");
            for (const auto& n : got) std::printf(" %s", n.c_str());
            std::printf("\n");
        }
        CHECK(got == want);
    }
    // a family with nothing on a shelf draws nothing: no placeholder
    CHECK(reg.types_in(Family::Oscillator, Shelf::Experimental).empty());
    CHECK(reg.types_in(Family::Additive, Shelf::Experimental).empty());
}

// The third shelf: the three types a host makes by an item of its own sit on
// no generated menu's shelf. Main and Experimental hold exactly the 75 types
// they held; Output's two and the note field are on None alone.
static void run_registry_shelf_none_tests() {
    std::printf("the registry: the types no generated menu offers\n");
    register_all_sources();
    auto& reg = SourceRegistry::instance();
    const std::set<std::string> none = {"PerformNode", "SoundChannel", "StereoMixer"};
    int shelved = 0;
    for (Family f : kFamilyOrder)
        for (Shelf s : {Shelf::Main, Shelf::Experimental})
            for (const auto& e : reg.types_in(f, s)) {
                CHECK(none.count(e.name) == 0);
                ++shelved;
            }
    CHECK(shelved == 75);
    auto names = [&](Family f, Shelf s) {
        std::vector<std::string> out;
        for (const auto& e : reg.types_in(f, s)) out.push_back(e.name);
        return out;
    };
    CHECK((names(Family::Output, Shelf::None) == std::vector<std::string>{"SoundChannel", "StereoMixer"}));
    CHECK((names(Family::Modulator, Shelf::None) == std::vector<std::string>{"PerformNode"}));
    CHECK(names(Family::Output, Shelf::Main).empty() && names(Family::Output, Shelf::Experimental).empty());
    int onNone = 0;
    for (Family f : kFamilyOrder) onNone += int(names(f, Shelf::None).size());
    CHECK(onNone == 3);
    CHECK(std::string(family_name(Family::Output)) == "Output");
    CHECK(kFamilyOrder[kFamilyCount - 1] == Family::Output);
}

// The twins of the nodes that declare the keys a patch gives them. Each case is
// driven the same way under the old constructors and under the declared
// pins; the 64-bit FNV-1a of its samples' float bits under the old
// constructors is held here as a literal.
namespace node_twins {
static uint64_t fnv(const std::vector<float>& samples) {
    uint64_t h = 14695981039346656037ull;
    for (float v : samples) {
        const uint32_t b = std::bit_cast<uint32_t>(v);
        for (int k = 0; k < 4; ++k) { h ^= (b >> (8 * k)) & 0xFFu; h *= 1099511628211ull; }
    }
    return h;
}
// A seeded white noise in [-1, 1].
struct NoiseFeed final : Generator {
    Randomizer rng;
    explicit NoiseFeed(uint32_t seed) : rng(seed) {}
    float compute() override { return rng.valuePN(); }
    const char* type_name() const override { return "NoiseFeed"; }
};
// A line from a to b over n samples of each note, then held at b.
struct LineFeed final : Generator {
    float a, b;
    int n, k{0};
    LineFeed(float a0, float b0, int n0) : a(a0), b(b0), n(n0) {}
    void on_start(int) override { k = 0; }
    float compute() override { const float t = k < n ? float(k) / float(n) : 1.0f; ++k; return a + (b - a) * t; }
    const char* type_name() const override { return "LineFeed"; }
};
static std::shared_ptr<ValueSource> c(float v) { return std::make_shared<ConstantSource>(v); }
static std::shared_ptr<ValueSource> line(float a, float b) { return std::make_shared<LineFeed>(a, b, 4800); }
static std::shared_ptr<ValueSource> noise(uint32_t seed) { return std::make_shared<NoiseFeed>(seed); }
// `notes` notes of `frames` samples each: prepare at 48 kHz, then start and pull.
static uint64_t render(ValueSource& n, int frames, int notes = 1) {
    std::vector<float> out;
    out.reserve(size_t(frames) * size_t(notes));
    n.prepare(RenderContext{48000});
    for (int k = 0; k < notes; ++k) {
        n.start(frames);
        for (int i = 0; i < frames; ++i) out.push_back(n.next());
    }
    return fnv(out);
}
static constexpr const char* kFilterKinds[] = {"lp", "hp", "bp"};
static constexpr int kSectionCounts[] = {1, 2, 3, 5};
// The filter `f` of kind 0 (lowpass), 1 (highpass) or 2 (bandpass), fed a
// fresh seeded noise, its cutoff(s) fixed or moving, 4,800 samples.
static uint64_t filter_hash(ValueSource& f, int kind, bool moving) {
    f.set_param("source", noise(0xB0B0u));
    if (kind == 2) {
        f.set_param("lowCutoff",  moving ? line(100.0f, 800.0f) : c(300.0f));
        f.set_param("highCutoff", moving ? line(2000.0f, 6000.0f) : c(3000.0f));
    } else if (kind == 0) {
        f.set_param("cutoffFreq", moving ? line(200.0f, 8000.0f) : c(1200.0f));
    } else {
        f.set_param("cutoffFreq", moving ? line(3000.0f, 100.0f) : c(800.0f));
    }
    return render(f, 4800);
}
// The hybrid's target spectrum of n values.
static std::vector<float> targets(int n) {
    std::vector<float> t;
    for (int k = 0; k < n; ++k) t.push_back((k % 3 == 1 ? 0.5f : 1.0f) / float(k + 1));
    return t;
}
static constexpr int kTargetCounts[] = {10, 30, 40};
// A 220 Hz note of 9,600 samples: the attack and well into the additive phase.
static uint64_t hybrid_hash(ValueSource& h) {
    h.set_param("frequency", c(220.0f));
    return render(h, 9600);
}
// Each Static node's three numbers, a grid of four, and two seeds.
static constexpr float kStaticVarGrid[4][3]   = {{1.0f, 0.0f, 0.0f}, {2.0f, 0.5f, 0.0f}, {2.0f, 0.5f, 0.3f}, {-3.0f, 0.2f, -0.5f}};
static constexpr float kStaticRangeGrid[4][3] = {{0.0f, 1.0f, 0.0f}, {0.2f, 0.4f, 0.3f}, {-5.0f, 5.0f, -0.7f}, {100.0f, 200.0f, 0.0f}};
static constexpr uint32_t kStaticVarSeeds[]   = {0x57A70001u, 12345u};
static constexpr uint32_t kStaticRangeSeeds[] = {0x57A70002u, 12345u};
// Four notes of two samples: the value drawn at each start.
static uint64_t static_hash(ValueSource& s) { return render(s, 2, 4); }
// Vibrato's seven settings over six points; the last holds every note
// under its threshold.
static constexpr const char* kVibratoKeys[7] = {"speed", "depth", "attack", "threshold", "speedVar", "depthVar", "zeroCrossTendency"};
static constexpr float kVibratoGrid[6][7] = {
    {5.0f, 0.02f, 0.3f, 0.0f,  0.0f, 0.0f, 1.0f},
    {6.0f, 0.05f, 0.2f, 0.0f,  0.3f, 0.2f, 1.0f},
    {3.5f, 0.1f,  0.5f, 0.05f, 0.0f, 0.5f, 0.3f},
    {8.0f, 0.01f, 0.0f, 0.0f,  0.8f, 0.0f, 0.0f},
    {5.0f, 0.03f, 1.0f, 0.08f, 0.1f, 0.1f, 0.7f},
    {5.0f, 0.05f, 0.3f, 0.5f,  0.0f, 0.0f, 1.0f}};
static constexpr uint32_t kVibratoSeeds[] = {0xF1B00000u, 777u};
// Two notes of 4,800 samples on a 440 Hz carrier.
static uint64_t vibrato_hash(ValueSource& v, const float (&point)[7]) {
    for (int k = 0; k < 7; ++k) v.set_setting(kVibratoKeys[k], point[k]);
    v.set_param("frequency", c(440.0f));
    return render(v, 4800, 2);
}
static const std::vector<float> kSegmentValues = {0.01f, 1.0f, 0.02f, -0.5f, 0.005f, 0.0f};
// Two notes of 2,400 samples, the widths and values varied so the seed is heard.
static uint64_t segment_hash(ValueSource& s) {
    s.set_param("widthVarPct", c(0.3f));
    s.set_param("valVarPct", c(0.2f));
    return render(s, 2400, 2);
}
}  // namespace node_twins

namespace node_twins {
// The old constructors' answers, one per case, recorded from a build of the
// old headers.
static const std::pair<const char*, uint64_t> kBase[] = {
    {"bw lp s1 fixed", 0x7DA6617F2EF07EEDull},
    {"bw lp s1 moving", 0x1CF748556F117F90ull},
    {"bw lp s2 fixed", 0xE54E45AD12B6F94Eull},
    {"bw lp s2 moving", 0xD4ED49DF426328A5ull},
    {"bw lp s3 fixed", 0x9B04DD220CFCC334ull},
    {"bw lp s3 moving", 0x49BFF5C476E951FDull},
    {"bw lp s5 fixed", 0x1D495CEDFA615EF9ull},
    {"bw lp s5 moving", 0x8CA09047A4C97532ull},
    {"bw hp s1 fixed", 0x6C60F48C27D74030ull},
    {"bw hp s1 moving", 0x957B8416812577B5ull},
    {"bw hp s2 fixed", 0x878C7C87AFF7806Dull},
    {"bw hp s2 moving", 0x69D1F954EFB71716ull},
    {"bw hp s3 fixed", 0x0697BF2AA3F664CCull},
    {"bw hp s3 moving", 0x0F698B70757C41E7ull},
    {"bw hp s5 fixed", 0x7DFAD8E6FBB7717Aull},
    {"bw hp s5 moving", 0x6560F5D68C9F5863ull},
    {"bw bp s1 fixed", 0x2EB9D888CF17F7D3ull},
    {"bw bp s1 moving", 0x6CF772B4F98311B1ull},
    {"bw bp s2 fixed", 0xC63BA71D32C64700ull},
    {"bw bp s2 moving", 0x3AF89144A020CEBBull},
    {"bw bp s3 fixed", 0x74716CD94469318Bull},
    {"bw bp s3 moving", 0x2C3E846CA3BE0B40ull},
    {"bw bp s5 fixed", 0x554736ADFCD23C42ull},
    {"bw bp s5 moving", 0x58D552AC9CE75A09ull},
    {"hybrid t10", 0x65BF1FE0CB7E09FBull},
    {"hybrid t30", 0x59BB69A73FC03443ull},
    {"hybrid t40", 0x59BB69A73FC03443ull},
    {"hybrid t10 after prepare", 0x65BF1FE0CB7E09FBull},
    {"staticvar g0 seed0", 0xEB87129436C8DA65ull},
    {"staticrange g0 seed0", 0x0E9E2A5D8A488161ull},
    {"staticvar g0 seed1", 0xEB87129436C8DA65ull},
    {"staticrange g0 seed1", 0x68C88F41F56F6279ull},
    {"staticvar g1 seed0", 0xB5EAB293CFA4921Dull},
    {"staticrange g1 seed0", 0x7E0E21A9625BE551ull},
    {"staticvar g1 seed1", 0x4675EA76AD111F95ull},
    {"staticrange g1 seed1", 0xF9F8EB0EBC06BF9Dull},
    {"staticvar g2 seed0", 0xF0EF10E2BFF19241ull},
    {"staticrange g2 seed0", 0x47A55EC549607C71ull},
    {"staticvar g2 seed1", 0x21082F231FD6FDEDull},
    {"staticrange g2 seed1", 0xABF1E39866FD7BC1ull},
    {"staticvar g3 seed0", 0x3971B82E9C983CADull},
    {"staticrange g3 seed0", 0xD8D7B92104A117ADull},
    {"staticvar g3 seed1", 0xFEC36C0DEE946D69ull},
    {"staticrange g3 seed1", 0x855842CC60294771ull},
    {"vibrato g0 seed0", 0x16C06C18A210D1D1ull},
    {"vibrato g0 seed1", 0x4E91E896AB169CE9ull},
    {"vibrato g1 seed0", 0x0BC202720C42535Dull},
    {"vibrato g1 seed1", 0x23D18A0253408425ull},
    {"vibrato g2 seed0", 0xF10E1653FF9DF66Dull},
    {"vibrato g2 seed1", 0x5359D3A7ED07F895ull},
    {"vibrato g3 seed0", 0xA426CE46B48C4405ull},
    {"vibrato g3 seed1", 0x0EAD61D1DA83A411ull},
    {"vibrato g4 seed0", 0xB1B2AD3ADDF6DC49ull},
    {"vibrato g4 seed1", 0xE2AEAB8B0387E331ull},
    {"vibrato g5 seed0", 0x48A6C9FCB7685F25ull},
    {"vibrato g5 seed1", 0x48A6C9FCB7685F25ull},
    {"var absolute", 0xFA747A5C678A7F0Full},
    {"var relative", 0xDA52ED834A7DED6Bull},
    {"range normalized", 0x04F516117C2EF73Full},
    {"range raw", 0x1B0EBD249763BBD4ull},
    {"segment default seed", 0xF3965F2D63FBA2D5ull},
    {"segment seed 99", 0xB311CFFF6E6BF28Full},
    {"segment default seed oneShot", 0xECAD53185C918959ull},
    {"segment seed 99 oneShot", 0x6A67A1EF442F948Aull},
};
static uint64_t base(const std::string& name) {
    for (const auto& [n, h] : kBase) if (name == n) return h;
    std::printf("  no recorded case '%s'\n", name.c_str());
    return 0;
}
static std::string filter_case(int kind, int n, bool moving) {
    return std::string("bw ") + kFilterKinds[kind] + " s" + std::to_string(n) + (moving ? " moving" : " fixed");
}
static std::shared_ptr<ValueSource> bw_filter(int kind) {
    if (kind == 0) return std::make_shared<BWLowpassFilter>();
    if (kind == 1) return std::make_shared<BWHighpassFilter>();
    return std::make_shared<BWBandpassFilter>();
}
static std::shared_ptr<ValueSource> create(const char* type, std::optional<uint32_t> seed = std::nullopt) {
    register_all_sources();
    return std::dynamic_pointer_cast<ValueSource>(SourceRegistry::instance().create(type, seed));
}
}  // namespace node_twins

static void run_bw_section_twin_tests() {
    using namespace node_twins;
    std::printf("the Butterworth section count: the setting against the old constructor\n");
    int rows = 0;
    for (int kind = 0; kind < 3; ++kind)
        for (int n : kSectionCounts)
            for (bool moving : {false, true}) {
                auto f = bw_filter(kind);
                f->set_setting("sections", float(n));
                CHECK(filter_hash(*f, kind, moving) == base(filter_case(kind, n, moving)));
                ++rows;
            }
    CHECK(rows == 24);
    for (int kind = 0; kind < 3; ++kind) {
        auto f = bw_filter(kind);
        CHECK(f->get_setting("sections") == 2.0f);
        CHECK(filter_hash(*f, kind, true) == base(filter_case(kind, 2, true)));
        // The count changed between two prepares: the rebuild starts from
        // clear histories, so fresh feeds give the fresh three-section render.
        f->set_setting("sections", 3.0f);
        CHECK(filter_hash(*f, kind, true) == base(filter_case(kind, 3, true)));
        // The same rate and count again: the histories carry on.
        CHECK(filter_hash(*f, kind, true) != base(filter_case(kind, 3, true)));
    }
    BWBandpassFilter bp;
    const auto* d = find_setting(bp, "sections");
    CHECK(d != nullptr && d->type == SettingType::Int && d->structural);
    CHECK(d && d->default_value == 2.0f && d->min_value == 1.0f && d->max_value == 8.0f);
}

static void run_hybrid_target_twin_tests() {
    using namespace node_twins;
    std::printf("the hybrid's targets: the array against set_target_partials\n");
    for (int n : kTargetCounts) {
        HybridKSSource h;
        h.set_array("targetPartials", targets(n));
        CHECK(hybrid_hash(h) == base("hybrid t" + std::to_string(n)));
        CHECK(h.get_array("targetPartials") == targets(n));      // the store keeps what was set
        CHECK(h.target_count() == std::max(n, 30));               // the working copy is padded to numPartials
    }
    HybridKSSource late;
    late.prepare(RenderContext{48000});
    late.set_array("targetPartials", targets(10));
    CHECK(late.target_count() == 30);                             // sized at once on a prepared node
    CHECK(hybrid_hash(late) == base("hybrid t10 after prepare"));
    CHECK(base("hybrid t10") != base("hybrid t30"));              // the targets reach the render
    bool declared = false;
    for (const auto& d : late.array_descriptors())
        declared = declared || std::string(d.name) == "targetPartials";
    CHECK(declared);
}

static void run_static_pair_twin_tests() {
    using namespace node_twins;
    std::printf("the Static pair: the settings against the old constructors\n");
    for (int g = 0; g < 4; ++g)
        for (int s = 0; s < 2; ++s) {
            const std::string at = " g" + std::to_string(g) + " seed" + std::to_string(s);
            const auto& p = kStaticVarGrid[g];
            StaticVarSource sv(kStaticVarSeeds[s]);
            sv.set_setting("baseValue", p[0]);
            sv.set_setting("varPct", p[1]);
            sv.set_setting("bias", p[2]);
            CHECK(static_hash(sv) == base("staticvar" + at));
            const auto& q = kStaticRangeGrid[g];
            StaticRangeSource sr(kStaticRangeSeeds[s]);
            sr.set_setting("min", q[0]);
            sr.set_setting("max", q[1]);
            sr.set_setting("bias", q[2]);
            CHECK(static_hash(sr) == base("staticrange" + at));
        }
    // The default seeds are the old ones.
    StaticVarSource dv;
    dv.set_setting("baseValue", 2.0f);
    dv.set_setting("varPct", 0.5f);
    CHECK(static_hash(dv) == base("staticvar g1 seed0"));
    StaticRangeSource dr;
    CHECK(static_hash(dr) == base("staticrange g0 seed0"));
    StaticVarSource fv;
    StaticRangeSource fr;
    CHECK(fv.get_setting("baseValue") == 1.0f && fv.get_setting("varPct") == 0.0f && fv.get_setting("bias") == 0.0f);
    CHECK(fr.get_setting("min") == 0.0f && fr.get_setting("max") == 1.0f && fr.get_setting("bias") == 0.0f);
    CHECK(fv.setting_descriptors().size() == 3 && fr.setting_descriptors().size() == 3);
    const auto* b = find_setting(fv, "bias");
    CHECK(b && b->type == SettingType::Float && b->min_value == -1.0f && b->max_value == 1.0f);
    const auto* mx = find_setting(fr, "max");
    CHECK(mx && mx->default_value == 1.0f && mx->min_value == -10000.0f && mx->max_value == 10000.0f);
}

static void run_vibrato_twin_tests() {
    using namespace node_twins;
    std::printf("Vibrato: its LFO's two ranges built by their pins against the old constructor\n");
    int rows = 0;
    for (int g = 0; g < 6; ++g)
        for (int s = 0; s < 2; ++s) {
            Vibrato v(kVibratoSeeds[s]);
            CHECK(vibrato_hash(v, kVibratoGrid[g]) == base("vibrato g" + std::to_string(g) + " seed" + std::to_string(s)));
            ++rows;
        }
    CHECK(rows == 12);
    CHECK(base("vibrato g1 seed0") != base("vibrato g1 seed1"));  // the seed reaches the render
}

static void run_registry_built_twin_tests() {
    using namespace node_twins;
    std::printf("Var, Range and Segment built by the registry and filled by name\n");
    for (bool abs : {true, false}) {
        auto v = create("VarSource");
        v->set_param("val", line(1.0f, 3.0f));
        v->set_param("var", noise(7u));
        v->set_param("varPct", c(0.25f));
        v->set_setting("absolute", abs ? 1.0f : 0.0f);
        CHECK(render(*v, 4800) == base(std::string("var ") + (abs ? "absolute" : "relative")));
    }
    for (bool norm : {true, false}) {
        auto r = create("RangeSource");
        r->set_param("min", c(10.0f));
        r->set_param("max", c(20.0f));
        r->set_param("var", noise(9u));
        r->set_setting("normalized", norm ? 1.0f : 0.0f);
        CHECK(render(*r, 4800) == base(std::string("range ") + (norm ? "normalized" : "raw")));
    }
    for (bool once : {false, true}) {
        auto a = create("SegmentSource");
        auto b = create("SegmentSource", 99u);
        for (auto* s : {a.get(), b.get()}) {
            s->set_array("values", kSegmentValues);
            s->set_setting("oneShot", once ? 1.0f : 0.0f);
        }
        CHECK(segment_hash(*a) == base(std::string("segment default seed") + (once ? " oneShot" : "")));
        CHECK(segment_hash(*b) == base(std::string("segment seed 99") + (once ? " oneShot" : "")));
    }
    // The registry's own nodes: nothing wired, each pin at its declared constant.
    auto v = create("VarSource");
    CHECK(!v->get_param("val") && !v->get_param("var") && !v->get_param("varPct"));
    auto* vs = dynamic_cast<VarSource*>(v.get());
    CHECK(vs && vs->val.constant == 1.0f && vs->absolute.value);
    (void)render(*v, 4);
    CHECK(v->current() == 1.0f);                                  // 1 * (1 + 0 * 0)
    auto r = create("RangeSource");
    CHECK(r->get_setting("normalized") == 0.0f);
    CHECK(!r->get_param("min") && !r->get_param("max") && !r->get_param("var"));
}

static void run_name_gate_name_tests() {
    std::printf("NameGate: the name is a declared string, resolved against the onsets at build\n");
    NameGate ng;
    const auto sd = ng.setting_descriptors();
    CHECK(sd.size() == 1);
    CHECK(sd.size() == 1 && std::string(sd[0].name) == "name" && sd[0].type == SettingType::String && sd[0].structural);
    CHECK(ng.get_string("name").empty());
    ng.set_string("name", "tongue");
    CHECK(ng.name.value == "tongue" && ng.get_string("name") == "tongue");
    CHECK(ng.targetId == -1.0f);                                  // a set resolves nothing; the builder does
    const auto gate_doc = [](const nlohmann::json& name) {
        auto doc = nlohmann::json::parse(enum_column::one_node("NameGate", {{"name", name}}));
        doc["instrument"]["onsets"] = {"tongue", "pluck"};
        return doc.dump();
    };
    for (const auto& [name, id] : {std::pair<const char*, float>{"tongue", 1.0f}, {"pluck", 2.0f}, {"slur", -1.0f}}) {
        LoadedInstrument ip;
        bool loaded = true;
        try { ip = load_inst(gate_doc(name)); }
        catch (const std::exception& e) { std::printf("  %s: %s\n", name, e.what()); loaded = false; }
        CHECK(loaded);
        if (!loaded) continue;
        auto* g = dynamic_cast<NameGate*>(ip.instrument->voicePool[0].nodesById.at("n").get());
        CHECK(g && g->get_string("name") == name && g->targetId == id);   // 1-based; outside the vocabulary -1
    }
    CHECK(three_kinds::refused_naming(gate_doc(3), false, {"node 'n' (NameGate): 'name' takes a string, not 3"}));
    // A plain sound has no vocabulary: the gate loads and never matches.
    CHECK(three_kinds::loads(three_kinds::plain(gate_doc("tongue")), true));
}

static void run_shaper_validate_tests() {
    std::printf("the Shaper checks itself: validate() on its arrays, warnings() under hysteresis\n");
    using Arrays = std::vector<std::pair<const char*, std::vector<float>>>;
    const auto check = [](const Arrays& arrays) {
        ShaperSource s;
        for (const auto& [n, v] : arrays) s.set_array(n, v);
        return s.validate();
    };
    const std::vector<float> two = {-1.0f, -1.0f, 1.0f, 1.0f}, three = {-1.0f, -1.0f, 0.0f, 0.5f, 1.0f, 1.0f};
    CHECK(check({}).empty());
    CHECK(check({{"values", two}, {"values2", three}}) == "values2 length must match values");
    CHECK(check({{"values", three}, {"values2", three}}).empty());
    CHECK(check({{"segs", {0.0f, 0.0f, 2.0f, 1.0f}}, {"segs2", {2.0f, 1.0f}}}) == "segs2 length must match segs");
    CHECK(check({{"segs", {0.0f, 0.0f, 2.0f, 1.0f}}, {"segs2", {0.0f, 0.0f, 1.0f, 1.0f}}}) == "segs2 segment types must match segs");
    CHECK(check({{"segs", {0.0f, 0.0f, 2.0f, 2.0f}}, {"segs2", {0.0f, 0.0f, 3.0f, 2.0f}}}).empty());   // Expo and InverseExpo
    CHECK(check({{"segs", {0.0f, 0.0f, 3.0f, 2.0f}}, {"segs2", {0.0f, 0.0f, 2.0f, 1.0f}}}).empty());
    CHECK(check({{"segs2", {0.0f, 0.0f, 2.0f, 3.0f}}}).empty());                                       // no segs beside segs2
    CHECK(check({{"segs", {0.0f, 0.0f, 2.0f, 1.0f}}}).empty());
    // A list in which no segment is overridden is held empty, and the check
    // reads the decoded lists, which keep their length: an all-default list
    // is compared as the file's.
    CHECK(check({{"segs", {0.0f, 0.0f, 0.0f, 0.0f}}, {"segs2", {0.0f, 0.0f, 2.0f, 1.0f, 0.0f, 0.0f}}}) == "segs2 length must match segs");
    CHECK(check({{"segs", {0.0f, 0.0f, 0.0f, 0.0f}}, {"segs2", {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f}}}) == "segs2 length must match segs");
    CHECK(check({{"segs", {0.0f, 0.0f, 0.0f, 0.0f}}, {"segs2", {0.0f, 0.0f, 2.0f, 1.0f}}}) == "segs2 segment types must match segs");
    CHECK(check({{"segs", {0.0f, 0.0f, 2.0f, 1.0f, 0.0f, 0.0f}}, {"segs2", {0.0f, 0.0f, 0.0f, 0.0f}}}) == "segs2 length must match segs");
    CHECK(check({{"segs", {0.0f, 0.0f, 2.0f, 1.0f}}, {"segs2", {0.0f, 0.0f, 0.0f, 0.0f}}}) == "segs2 segment types must match segs");
    CHECK(check({{"segs", {0.0f, 0.0f, 0.0f, 0.0f}}, {"segs2", {0.0f, 0.0f, 0.0f, 0.0f}}}).empty());
    // values2 beside no values: four floats match the default curve the node
    // holds and load; any other length is refused.
    CHECK(check({{"values2", two}}).empty());
    CHECK(check({{"values2", three}}) == "values2 length must match values");
    // Through a load: the node, its type and the words.
    nlohmann::json bad = {{"values", two}, {"values2", three}};
    CHECK(three_kinds::refused_naming(enum_column::one_node("Shaper", bad), false,
                                      {"node 'n' (Shaper): values2 length must match values"}));
    nlohmann::json types = {{"segs", {0.0f, 0.0f, 4.0f, 1.0f}}, {"segs2", {0.0f, 0.0f, 2.0f, 1.0f}}};
    CHECK(three_kinds::refused_naming(enum_column::one_node("Shaper", types), true,
                                      {"node 'n' (Shaper): segs2 segment types must match segs"}));
    nlohmann::json pair = {{"segs", {0.0f, 0.0f, 2.0f, 1.0f}}, {"segs2", {0.0f, 0.0f, 3.0f, 1.0f}}};
    CHECK(three_kinds::loads(enum_column::one_node("Shaper", pair), false));
    for (const bool file : {false, true}) {
        const auto doc = [file](const nlohmann::json& params) {
            const std::string d = enum_column::one_node("Shaper", params);
            return file ? three_kinds::plain(d) : d;
        };
        CHECK(three_kinds::refused_naming(doc({{"segs", {0.0f, 0.0f, 0.0f, 0.0f}}, {"segs2", {0.0f, 0.0f, 2.0f, 1.0f, 0.0f, 0.0f}}}),
                                          file, {"node 'n' (Shaper): segs2 length must match segs"}));
        CHECK(three_kinds::refused_naming(doc({{"segs", {0.0f, 0.0f, 0.0f, 0.0f}}, {"segs2", {0.0f, 0.0f, 2.0f, 1.0f}}}),
                                          file, {"node 'n' (Shaper): segs2 segment types must match segs"}));
        CHECK(three_kinds::refused_naming(doc({{"segs", {0.0f, 0.0f, 2.0f, 1.0f, 0.0f, 0.0f}}, {"segs2", {0.0f, 0.0f, 0.0f, 0.0f}}}),
                                          file, {"node 'n' (Shaper): segs2 length must match segs"}));
        CHECK(three_kinds::refused_naming(doc({{"segs", {0.0f, 0.0f, 2.0f, 1.0f}}, {"segs2", {0.0f, 0.0f, 0.0f, 0.0f}}}),
                                          file, {"node 'n' (Shaper): segs2 segment types must match segs"}));
        CHECK(three_kinds::loads(doc({{"segs2", {0.0f, 0.0f, 2.0f, 1.0f}}}), file));
        CHECK(three_kinds::loads(doc({{"values2", two}}), file));
        CHECK(three_kinds::refused_naming(doc({{"values2", three}}), file, {"node 'n' (Shaper): values2 length must match values"}));
    }
    // warnings(): one line under hysteresis without a slip curve, none otherwise.
    ShaperSource h;
    CHECK(h.warnings().empty());
    h.set_setting("hysteresis", 1.0f);
    const auto w = h.warnings();
    CHECK(w.size() == 1 && w[0] == "hysteresis without values2 is inert (no slip curve)");
    h.set_array("values2", two);
    CHECK(h.warnings().empty());
}

static void run_declared_default_load_tests() {
    using namespace node_twins;
    std::printf("the five deleted branches' types load at their declared defaults\n");
    // Settings, Params and arrays against the registry's own node of the type.
    const auto at_defaults = [](const Component& got, const Component& fresh) {
        bool ok = true;
        for (const auto& d : fresh.setting_descriptors())
            ok = ok && got.get_setting(d.name) == d.default_value && got.get_setting(d.name) == fresh.get_setting(d.name);
        for (const auto& d : fresh.param_descriptors())
            ok = ok && !got.get_param(d.name) && !fresh.get_param(d.name);
        for (const auto& d : fresh.array_descriptors())
            ok = ok && got.get_array(d.name) == fresh.get_array(d.name);
        return ok;
    };
    for (const char* type : {"RangeSource", "SegmentSource", "Vibrato", "BWLowpassFilter",
                             "BWHighpassFilter", "BWBandpassFilter", "HybridKSSource"})
        for (bool withParams : {true, false}) {
            LoadedInstrument ip;
            auto n = enum_column::load_node(type, withParams ? nlohmann::json::object() : nlohmann::json(), ip);
            CHECK(n != nullptr);
            if (!n) continue;
            const bool ok = at_defaults(*n, *create(type));
            if (!ok) std::printf("  %s %s: not at its declared defaults\n", type, withParams ? "{}" : "(no params)");
            CHECK(ok);
        }
    // A RepeatingSource's gapDuration holds what the file gives, or its default.
    for (const auto& [params, want] : std::initializer_list<std::pair<const char*, float>>{
             {R"({"gapDuration":0.5})", 0.5f}, {R"({})", 0.2f}}) {
        LoadedInstrument rip;
        auto r = enum_column::load_node("RepeatingSource", nlohmann::json::parse(params), rip);
        CHECK(r && r->get_setting("gapDuration") == want);
    }
}

namespace list_twins {
using node_twins::c;
using node_twins::fnv;
using node_twins::noise;
constexpr float kAbsent = std::numeric_limits<float>::quiet_NaN();
// One feed of a case: a constant, a seeded noise, or a sine at a frequency,
// with its weight and delay, either of which may be absent.
struct Feed {
    int kind;                                   // 0 constant, 1 noise (seed), 2 sine (Hz)
    float value;
    float weight = kAbsent;
    float delay = kAbsent;
};
static std::shared_ptr<ValueSource> signal(int kind, float value) {
    if (kind == 0) return c(value);
    if (kind == 1) return noise(uint32_t(value));
    auto s = std::make_shared<SineSource>();
    s->set_param("frequency", c(value));
    return s;
}
struct MultiCase { const char* name; std::vector<Feed> feeds; };
static const std::vector<MultiCase> kMultiCases = {
    {"multi one",            {{0, 0.3f}}},
    {"multi two all",        {{0, 0.3f, 0.5f}, {1, 11.0f, 2.0f, 1.0f}}},
    {"multi three some",     {{0, 0.3f, 0.5f}, {0, -0.7f}, {1, 11.0f, 0.25f}}},
    {"multi four delays",    {{0, 0.3f}, {1, 11.0f, kAbsent, 100.0f}, {1, 23.0f, kAbsent, 1.0f}, {0, 0.6f}}},
    {"multi five all",       {{0, 0.1f, 0.5f, 100.0f}, {0, 0.2f, 0.25f}, {1, 11.0f, 1.5f, 1.0f},
                              {2, 440.0f, 0.8f}, {0, 0.4f, -1.0f, 100.0f}}},
    {"multi three delayed",  {{2, 440.0f, kAbsent, 100.0f}, {1, 23.0f, kAbsent, 100.0f}, {0, 0.2f, kAbsent, 100.0f}}},
    // "multi five all" with its arrays cut to two weights and one delay, the
    // defaults written out past the cut.
    {"multi five defaults",  {{0, 0.1f, 0.5f, 100.0f}, {0, 0.2f, 0.25f, 0.0f}, {1, 11.0f, 1.0f, 0.0f},
                              {2, 440.0f, 1.0f, 0.0f}, {0, 0.4f, 1.0f, 0.0f}}},
};
// A case's weights or delays as the node's array: one per feed, the default
// where a feed has none; empty when no feed has one.
static std::vector<float> column(const std::vector<Feed>& feeds, float Feed::*field, float def) {
    bool any = false;
    for (const auto& f : feeds) any = any || !std::isnan(f.*field);
    std::vector<float> out;
    if (any)
        for (const auto& f : feeds) out.push_back(std::isnan(f.*field) ? def : f.*field);
    return out;
}
constexpr int kMultiFrames = 4800;

// One stage of a Phased case: its signal and its four numbers.
struct Row { int kind; float value; float percent, minSec, maxSec, gain; };
struct PhasedCase { const char* name; std::vector<Row> rows; };
static const std::vector<PhasedCase> kPhasedCases = {
    {"expand first",  {{2, 220.0f, 0.0f, 0.0f, 0.0f, 1.0f}, {1, 31.0f, 0.3f, 0.0f, 0.0f, 1.0f}, {2, 550.0f, 0.25f, 0.0f, 0.0f, 1.0f}}},
    {"expand middle", {{2, 220.0f, 0.3f, 0.0f, 0.0f, 1.0f}, {1, 31.0f, 0.0f, 0.0f, 0.0f, 1.0f}, {2, 550.0f, 0.25f, 0.0f, 0.0f, 1.0f}}},
    {"expand last",   {{2, 220.0f, 0.3f, 0.0f, 0.0f, 1.0f}, {1, 31.0f, 0.25f, 0.0f, 0.0f, 1.0f}, {2, 550.0f, 0.0f, 0.0f, 0.0f, 1.0f}}},
    {"clamped",       {{2, 220.0f, 0.5f, 0.01f, 0.02f, 1.0f}, {1, 31.0f, 0.0f, 0.0f, 0.0f, 1.0f}, {2, 550.0f, 0.2f, 0.03f, 0.04f, 1.0f}}},
    {"gain",          {{2, 220.0f, 0.3f, 0.0f, 0.0f, 0.5f}, {1, 31.0f, 0.0f, 0.0f, 0.0f, 1.0f}, {2, 550.0f, 0.25f, 0.0f, 0.0f, 0.5f}}},
};
static constexpr float kOverlaps[] = {0.0f, 0.05f, 0.2f};
static constexpr int kNoteFrames[] = {9600, 48000};
static std::string phased_name(const PhasedCase& pc, float overlap, int frames) {
    char buf[96];
    std::snprintf(buf, sizeof buf, "phased %s ov%.2f n%d", pc.name, overlap, frames);
    return buf;
}
static uint64_t zeros(int frames) { return fnv(std::vector<float>(size_t(frames), 0.0f)); }

// The rule of twelve values, every one off its default, and the host it
// expands: a FullPartials under an AdditiveSource.
static ExpandRule rule_values(int recurse) {
    ExpandRule r;
    r.count = 3; r.recurse = recurse;
    r.spacing1 = 0.3f; r.spacing2 = 0.7f; r.dt1 = 0.05f; r.dt2 = 0.02f;
    r.loPct1 = 0.2f; r.loPct2 = 0.3f; r.power1 = 2.0f; r.power2 = 1.5f;
    r.po1 = 0.1f; r.po2 = 0.25f;
    return r;
}
static std::shared_ptr<FullPartials> host_partials() {
    auto p = std::make_shared<FullPartials>(0x3902u);
    p->set_setting("maxPartials", 6.0f);
    return p;
}
static std::shared_ptr<AdditiveSource> host(std::shared_ptr<FullPartials> parts) {
    auto a = std::make_shared<AdditiveSource>();
    a->set_param("frequency", c(220.0f));
    a->set_param("partials", std::move(parts));
    return a;
}
constexpr int kRuleFrames = 4800, kRuleNotes = 2;
// Notes on a node already prepared: start and pull, no prepare.
static uint64_t render_prepared(ValueSource& n, int frames, int notes) {
    std::vector<float> out;
    out.reserve(size_t(frames) * size_t(notes));
    for (int k = 0; k < notes; ++k) {
        n.start(frames);
        for (int i = 0; i < frames; ++i) out.push_back(n.next());
    }
    return fnv(out);
}
}  // namespace list_twins

namespace list_twins {
// The old fill's answers (add with a weight and a delay, add_stage, the
// struct handed to set_expand_rule), one per case, recorded from a build of
// the old headers.
static const std::pair<const char*, uint64_t> kBase[] = {
    {"multi one", 0x5A7D7C04F2769CA5ull},
    {"multi two all", 0x88045BDA2BF5BDE7ull},
    {"multi three some", 0x110682BC88AFB980ull},
    {"multi four delays", 0xCA65543BF7A95376ull},
    {"multi five all", 0x816A53E6C9ADEC03ull},
    {"multi three delayed", 0x6BC2C0800CFB6075ull},
    {"multi five defaults", 0x306EBA191BF9E239ull},
    {"phased expand first ov0.00 n9600", 0x3452D7490CAE6CBEull},
    {"phased expand first ov0.00 n48000", 0x0A8F84C9382AC0FAull},
    {"phased expand first ov0.05 n9600", 0xC5ABB93F4E52304Bull},
    {"phased expand first ov0.05 n48000", 0x52648E35AC5E4CC1ull},
    {"phased expand first ov0.20 n9600", 0x5BA977C1A46B91FBull},
    {"phased expand first ov0.20 n48000", 0x2D8C356CBE7DA3FBull},
    {"phased expand middle ov0.00 n9600", 0xD1B46C44DA175B3Aull},
    {"phased expand middle ov0.00 n48000", 0xD7FEBB876E0211ADull},
    {"phased expand middle ov0.05 n9600", 0xDAA3AE9FA76DC163ull},
    {"phased expand middle ov0.05 n48000", 0x20F654881D2C5835ull},
    {"phased expand middle ov0.20 n9600", 0x30BEEF910CE130A4ull},
    {"phased expand middle ov0.20 n48000", 0x1C16DEAA0BA7B3DCull},
    {"phased expand last ov0.00 n9600", 0x7200B562F373D661ull},
    {"phased expand last ov0.00 n48000", 0x9B2CC538F2522A07ull},
    {"phased expand last ov0.05 n9600", 0x70E42EE60889830Full},
    {"phased expand last ov0.05 n48000", 0x398DE6EC34BEC562ull},
    {"phased expand last ov0.20 n9600", 0xB836D8CC0AAE5A24ull},
    {"phased expand last ov0.20 n48000", 0x2C49EBB889185456ull},
    {"phased clamped ov0.00 n9600", 0x599D3203CD1D728Aull},
    {"phased clamped ov0.00 n48000", 0xE394F9ED5BFC9096ull},
    {"phased clamped ov0.05 n9600", 0x8A4BBA7F04A10D92ull},
    {"phased clamped ov0.05 n48000", 0xE9EED33E1FFD8554ull},
    {"phased clamped ov0.20 n9600", 0xF865214F16F3313Eull},
    {"phased clamped ov0.20 n48000", 0x2252C62EEC19AE0Eull},
    {"phased gain ov0.00 n9600", 0xF0C512D67C2C0D6Full},
    {"phased gain ov0.00 n48000", 0xF23E6BBB266C4F43ull},
    {"phased gain ov0.05 n9600", 0x6CC31B7FD7FCBA17ull},
    {"phased gain ov0.05 n48000", 0x5E8B76AB7519107Bull},
    {"phased gain ov0.20 n9600", 0x30BEEF910CE130A4ull},
    {"phased gain ov0.20 n48000", 0x48A26FB28712CBD6ull},
    {"rule recurse0", 0x42208CB55733894Bull},
    {"rule recurse1", 0xE2A810BB90A821E7ull},
    {"rule none", 0xC3B3C2CA5142A1A5ull},
};
static uint64_t base(const std::string& name) {
    for (const auto& [n, h] : kBase) if (name == n) return h;
    std::printf("  no recorded case '%s'\n", name.c_str());
    return 0;
}
static const MultiCase& multi_case(const char* name) {
    for (const auto& mc : kMultiCases) if (std::string(mc.name) == name) return mc;
    std::printf("  no multi case '%s'\n", name);
    return kMultiCases.front();
}
static const PhasedCase& phased_case(const char* name) {
    for (const auto& pc : kPhasedCases) if (std::string(pc.name) == name) return pc;
    std::printf("  no phased case '%s'\n", name);
    return kPhasedCases.front();
}
static void add_feeds(MultiSource& m, const std::vector<Feed>& feeds) {
    for (const auto& f : feeds) CHECK(m.add_param("source", signal(f.kind, f.value)));
}
// The case's arrays, set only where a feed holds a value.
static void set_columns(MultiSource& m, const std::vector<Feed>& feeds) {
    if (auto w = column(feeds, &Feed::weight, 1.0f); !w.empty()) m.set_array("weights", w);
    if (auto d = column(feeds, &Feed::delay, 0.0f); !d.empty()) m.set_array("delays", d);
}
// The first `n` rows of the case's table, column by column.
static void set_table(PhasedValueSource& p, const std::vector<Row>& rows, size_t n) {
    std::vector<float> percent, minSec, maxSec, gain;
    for (size_t i = 0; i < n && i < rows.size(); ++i) {
        percent.push_back(rows[i].percent); minSec.push_back(rows[i].minSec);
        maxSec.push_back(rows[i].maxSec);   gain.push_back(rows[i].gain);
    }
    p.set_array("percent", percent); p.set_array("minSec", minSec);
    p.set_array("maxSec", maxSec);   p.set_array("gain", gain);
}
static void add_stages(PhasedValueSource& p, const std::vector<Row>& rows) {
    for (const auto& r : rows) CHECK(p.add_param("sources", signal(r.kind, r.value)));
}
// The rule of `rule_values(recurse)` as a node, its pins wired to constants
// as a load wires them.
static std::shared_ptr<ExpandRuleNode> rule_node(int recurse) {
    const ExpandRule r = rule_values(recurse);
    auto n = std::make_shared<ExpandRuleNode>();
    n->set_setting("count", float(r.count));
    n->set_setting("recurse", float(r.recurse));
    const std::pair<const char*, float> pins[] = {
        {"spacing1", r.spacing1}, {"spacing2", r.spacing2}, {"dt1", r.dt1}, {"dt2", r.dt2},
        {"loPct1", r.loPct1}, {"loPct2", r.loPct2}, {"power1", r.power1}, {"power2", r.power2},
        {"po1", r.po1}, {"po2", r.po2}};
    for (const auto& [name, v] : pins) CHECK(n->set_param(name, c(v)));
    return n;
}
// A voice graph of two sines, `a` at 220 Hz and `b` at 330, then `nodes`.
static std::string voice_doc(const nlohmann::json& nodes) {
    nlohmann::json all = nlohmann::json::array({
        nlohmann::json{{"id", "a"}, {"type", "SineSource"}, {"params", {{"frequency", 220.0}}}},
        nlohmann::json{{"id", "b"}, {"type", "SineSource"}, {"params", {{"frequency", 330.0}}}}});
    for (const auto& n : nodes) all.push_back(n);
    return nlohmann::json{{"sampleRate", 48000}, {"instrument", {{"polyphony", 1}}},
                          {"graph", {{"output", "a"}, {"nodes", all}}}}.dump();
}
// The render of node `id` of a loaded voice graph, or 0 with the refusal printed.
static uint64_t loaded_render(const std::string& doc, const char* id, std::vector<float>* arrays = nullptr,
                              std::initializer_list<const char*> names = {}) {
    try {
        LoadedInstrument ip = load_inst(doc);
        auto n = ip.instrument->voicePool[0].nodesById.at(id);
        if (arrays) for (const char* a : names) for (float v : n->get_array(a)) arrays->push_back(v);
        return node_twins::render(*std::dynamic_pointer_cast<ValueSource>(n), 4800, 2);
    } catch (const std::exception& e) {
        std::printf("  load failed: %s\n", e.what());
        return 0;
    }
}
}  // namespace list_twins

static void run_multi_list_twin_tests() {
    using namespace list_twins;
    std::printf("MultiSource: the list and its two arrays against add(src, weight, delay)\n");
    int rows = 0;
    for (const auto& mc : kMultiCases)
        for (bool arraysFirst : {true, false}) {
            MultiSource m;
            if (arraysFirst) set_columns(m, mc.feeds);
            add_feeds(m, mc.feeds);
            if (!arraysFirst) set_columns(m, mc.feeds);
            CHECK(m.source.sources.size() == mc.feeds.size());
            CHECK(node_twins::render(m, kMultiFrames) == base(mc.name));
            ++rows;
        }
    CHECK(rows == 14);
    const auto& five = multi_case("multi five all");
    {   // Arrays shorter than the feeds: each feed past an end takes the default.
        MultiSource m;
        add_feeds(m, five.feeds);
        m.set_array("weights", {0.5f, 0.25f});
        m.set_array("delays", {100.0f});
        CHECK(node_twins::render(m, kMultiFrames) == base("multi five defaults"));
    }
    {   // Arrays longer than the feeds: the entries past the last feed are not read.
        MultiSource m;
        add_feeds(m, multi_case("multi two all").feeds);
        m.set_array("weights", {0.5f, 2.0f, 7.0f, 7.0f});
        m.set_array("delays", {0.0f, 1.0f, 50.0f});
        CHECK(node_twins::render(m, kMultiFrames) == base("multi two all"));
    }
    {   // clear_param empties the list: silence, with the arrays kept.
        MultiSource m;
        add_feeds(m, five.feeds);
        set_columns(m, five.feeds);
        CHECK(m.clear_param("source"));
        CHECK(m.source.sources.empty() && m.get_array("weights").size() == 5);
        CHECK(node_twins::render(m, kMultiFrames) == zeros(kMultiFrames));
    }
    {   // set_param replaces the list with its one feed.
        MultiSource m;
        add_feeds(m, multi_case("multi three some").feeds);
        CHECK(m.set_param("source", signal(0, 0.3f)));
        CHECK(m.source.sources.size() == 1);
        CHECK(node_twins::render(m, kMultiFrames) == base("multi one"));
    }
}

static void run_phased_list_twin_tests() {
    using namespace list_twins;
    std::printf("PhasedValueSource: the list and its table against add_stage\n");
    int rows = 0;
    for (const auto& pc : kPhasedCases)
        for (float ov : kOverlaps)
            for (int frames : kNoteFrames)
                for (bool tableFirst : {true, false}) {
                    PhasedValueSource p;
                    p.set_setting("overlap", ov);
                    if (tableFirst) set_table(p, pc.rows, pc.rows.size());
                    add_stages(p, pc.rows);
                    if (!tableFirst) set_table(p, pc.rows, pc.rows.size());
                    CHECK(node_twins::render(p, frames) == base(phased_name(pc, ov, frames)));
                    ++rows;
                }
    CHECK(rows == 60);
    {   // No stage: silence.
        PhasedValueSource p;
        CHECK(node_twins::render(p, 4800) == zeros(4800));
    }
    {   // A table of two rows under three feeds: the third takes 0, 0, 0, 1,
        // which is the last row of "expand last".
        const auto& last = phased_case("expand last");
        PhasedValueSource p;
        add_stages(p, last.rows);
        set_table(p, last.rows, 2);
        CHECK(node_twins::render(p, 9600) == base(phased_name(last, 0.05f, 9600)));
    }
    const auto& middle = phased_case("expand middle");
    {   // The list emptied after a prepare and a start: zeros, inside the list.
        PhasedValueSource p;
        add_stages(p, middle.rows);
        set_table(p, middle.rows, 3);
        p.prepare(RenderContext{48000});
        p.start(9600);
        for (int i = 0; i < 100; ++i) (void)p.next();
        CHECK(p.clear_param("sources"));
        bool silent = true;
        for (int i = 0; i < 9600; ++i) silent = silent && p.next() == 0.0f;
        p.start(9600);
        for (int i = 0; i < 100; ++i) silent = silent && p.next() == 0.0f;
        CHECK(silent);
    }
    {   // Three stages cut to one mid-note: the walk reads the one stage.
        PhasedValueSource p;
        add_stages(p, middle.rows);
        set_table(p, middle.rows, 3);
        p.prepare(RenderContext{48000});
        p.start(9600);
        for (int i = 0; i < 5000; ++i) (void)p.next();
        CHECK(p.set_param("sources", c(0.5f)));
        CHECK(p.sources.sources.size() == 1);
        bool one = true;
        for (int i = 0; i < 6000; ++i) one = one && p.next() == 0.5f;
        CHECK(one);
    }
    // A list or table change never moves the walk a note laid out.
    const auto pull = [](PhasedValueSource& p, int n, std::vector<float>& out) {
        for (int i = 0; i < n; ++i) out.push_back(p.next());
    };
    const auto untouched = [&](const std::vector<Row>& rows, float overlap, int frames) {
        PhasedValueSource p;
        p.set_setting("overlap", overlap);
        add_stages(p, rows);
        set_table(p, rows, rows.size());
        std::vector<float> out;
        p.prepare(RenderContext{48000});
        p.start(frames);
        pull(p, frames, out);
        return out;
    };
    const auto feeds_of = [](const std::vector<Row>& rows) {
        std::vector<std::shared_ptr<ValueSource>> feeds;
        for (const auto& r : rows) feeds.push_back(signal(r.kind, r.value));
        return feeds;
    };
    const std::vector<float> twin = untouched(middle.rows, 0.05f, 9600);
    for (int k : {100, 3000, 9000}) {   // in the first stage, the expand stage, and its fade into the last
        // The list cleared and refilled with the same feeds mid-note, as the editor's rewire does.
        PhasedValueSource p;
        p.set_setting("overlap", 0.05f);
        const auto feeds = feeds_of(middle.rows);
        for (const auto& f : feeds) CHECK(p.add_param("sources", f));
        set_table(p, middle.rows, 3);
        std::vector<float> out;
        p.prepare(RenderContext{48000});
        p.start(9600);
        pull(p, k, out);
        CHECK(p.clear_param("sources"));
        for (const auto& f : feeds) CHECK(p.add_param("sources", f));
        pull(p, 9600 - k, out);
        if (out != twin) std::printf("  refilled list at %d: the walk moved\n", k);
        CHECK(out == twin);
        // The four columns set again to the same values mid-note.
        PhasedValueSource q;
        q.set_setting("overlap", 0.05f);
        add_stages(q, middle.rows);
        set_table(q, middle.rows, 3);
        std::vector<float> outQ;
        q.prepare(RenderContext{48000});
        q.start(9600);
        pull(q, k, outQ);
        set_table(q, middle.rows, 3);
        pull(q, 9600 - k, outQ);
        if (outQ != twin) std::printf("  table set again at %d: the walk moved\n", k);
        CHECK(outQ == twin);
    }
    {   // Prepared and started with no feed, then a feed: zeros until the
        // next start, then the feed plays as in a node that held it throughout.
        PhasedValueSource p;
        p.prepare(RenderContext{48000});
        p.start(4800);
        std::vector<float> out;
        pull(p, 100, out);
        auto feed = signal(2, 220.0f);
        feed->prepare(RenderContext{48000});              // prepared here: the walk prepares nothing mid-note
        CHECK(p.add_param("sources", feed));
        pull(p, 4700, out);
        CHECK(fnv(out) == zeros(4800));
        p.start(4800);
        std::vector<float> played;
        pull(p, 4800, played);
        CHECK(played == untouched({{2, 220.0f, 0.0f, 0.0f, 0.0f, 1.0f}}, 0.05f, 4800));
    }
    {   // Three stages cut to two mid-note (cleared, then two added): the walk
        // keeps the boundaries the note laid out (2400, 4800, 9600) and reads
        // the two stages, each sample against the hand-computed walk.
        PhasedValueSource p;
        p.set_setting("overlap", 0.01f);
        p.set_array("percent", {0.25f, 0.25f, 0.5f});
        auto one = c(1.0f), two = c(2.0f), four = c(4.0f);
        for (const auto& f : {one, two, four}) CHECK(p.add_param("sources", f));
        std::vector<float> out;
        p.prepare(RenderContext{48000});
        p.start(9600);
        pull(p, 1000, out);
        CHECK(p.clear_param("sources"));
        CHECK(p.add_param("sources", one) && p.add_param("sources", two));
        pull(p, 8600, out);
        const int halfOl = int(0.01f * float(48000)) / 2;
        const auto fade = [&](int pos) {            // stage 1 into stage 2 over the half overlap
            const float t = float(pos) / float(halfOl);
            return (1.0f * (1.0f - t) + 2.0f * 1.0f * t) * 1.0f;
        };
        const auto want = [&](int ptr) {
            if (ptr < 2400) return 2400 - ptr <= halfOl ? fade(halfOl - (2400 - ptr)) : 1.0f;
            const int pos = ptr - (ptr < 4800 ? 2400 : 4800);   // past the last boundary walked, the last stage
            return pos < halfOl ? fade(pos) : 2.0f;
        };
        bool finite = true, walked = true;
        int firstOff = -1;
        for (int i = 0; i < int(out.size()); ++i) {
            finite = finite && std::isfinite(out[size_t(i)]);
            const bool same = std::fabs(out[size_t(i)] - want(i)) <= 1e-6f;
            if (!same && firstOff < 0) firstOff = i;
            walked = walked && same;
        }
        if (firstOff >= 0) std::printf("  cut to two: sample %d is %g, want %g\n", firstOff, out[size_t(firstOff)], want(firstOff));
        CHECK(out.size() == 9600 && finite && walked);
    }
    {   // The next start after the list grew allocates nothing: the counters
        // and boundaries were reserved for the list when it changed.
        PhasedValueSource p;
        add_stages(p, {middle.rows[0]});
        p.prepare(RenderContext{48000});
        const auto later = feeds_of({middle.rows[1], middle.rows[2]});
        for (const auto& f : later) {
            f->prepare(RenderContext{48000});
            CHECK(p.add_param("sources", f));
        }
        alloc_probe::reset();
        alloc_probe::arm();
        p.start(9600);
        for (int i = 0; i < 9600; ++i) (void)p.next();
        alloc_probe::disarm();
        if (alloc_probe::count()) std::printf("  the start after the list grew: %zu allocation(s)\n", alloc_probe::count());
        CHECK(alloc_probe::count() == 0);
        alloc_probe::reset();
    }
}

static void run_expand_rule_slot_tests() {
    using namespace list_twins;
    std::printf("Partials: the expandRule slot against set_expand_rule\n");
    for (int recurse : {0, 1}) {
        auto parts = host_partials();
        CHECK(parts->set_param("expandRule", rule_node(recurse)));
        auto a = host(parts);
        CHECK(node_twins::render(*a, kRuleFrames, kRuleNotes) == base("rule recurse" + std::to_string(recurse)));
    }
    {   // The slot emptied: no expansion, by clear_param and by a null.
        auto parts = host_partials();
        CHECK(parts->set_param("expandRule", rule_node(0)));
        CHECK(parts->clear_param("expandRule"));
        auto a = host(parts);
        CHECK(node_twins::render(*a, kRuleFrames, kRuleNotes) == base("rule none"));
        auto parts2 = host_partials();
        CHECK(parts2->set_param("expandRule", rule_node(0)));
        CHECK(parts2->set_param("expandRule", nullptr));
        auto a2 = host(parts2);
        CHECK(node_twins::render(*a2, kRuleFrames, kRuleNotes) == base("rule none"));
    }
    {   // A signal is refused and the rule stays.
        auto parts = host_partials();
        CHECK(parts->set_param("expandRule", rule_node(0)));
        CHECK(!parts->set_param("expandRule", c(1.0f)));
        CHECK(parts->expandRule.get() != nullptr);
        auto a = host(parts);
        CHECK(node_twins::render(*a, kRuleFrames, kRuleNotes) == base("rule recurse0"));
    }
    {   // A rule wired after the host's prepare: the note arrays are sized again.
        auto parts = host_partials();
        auto a = host(parts);
        a->prepare(RenderContext{48000});
        CHECK(parts->set_param("expandRule", rule_node(0)));
        CHECK(render_prepared(*a, kRuleFrames, kRuleNotes) == base("rule recurse0"));
    }
    {   // The slot emptied after a note: the next notes play the unexpanded
        // partials, as a host that never had the rule plays its second and third.
        auto parts = host_partials();
        CHECK(parts->set_param("expandRule", rule_node(0)));
        auto a = host(parts);
        a->prepare(RenderContext{48000});
        (void)render_prepared(*a, kRuleFrames, 1);
        CHECK(parts->clear_param("expandRule"));
        auto never = host(host_partials());
        never->prepare(RenderContext{48000});
        (void)render_prepared(*never, kRuleFrames, 1);
        CHECK(render_prepared(*a, kRuleFrames, kRuleNotes) == render_prepared(*never, kRuleFrames, kRuleNotes));
    }
    {   // The rule is read when the slot changes: a later edit to the node is not seen.
        auto parts = host_partials();
        auto rule = rule_node(0);
        CHECK(parts->set_param("expandRule", rule));
        rule->set_setting("count", 5.0f);
        auto a = host(parts);
        CHECK(node_twins::render(*a, kRuleFrames, kRuleNotes) == base("rule recurse0"));
    }
}

static void run_list_node_descriptor_tests() {
    std::printf("the list nodes' and the slot's descriptors\n");
    MultiSource m;
    CHECK(m.input_descriptors().size() == 1 && std::string(m.input_descriptors()[0].name) == "source");
    CHECK(m.input_descriptors()[0].multi && !m.input_descriptors()[0].slot);
    const auto ma = m.array_descriptors();
    CHECK(ma.size() == 2);
    CHECK(ma.size() == 2 && std::string(ma[0].name) == "weights" && !ma[0].groupName && ma[0].default_value == 1.0f);
    CHECK(ma.size() == 2 && std::string(ma[1].name) == "delays" && !ma[1].groupName && ma[1].default_value == 0.0f);
    CHECK(m.param_descriptors().empty() && m.setting_descriptors().empty());
    CHECK(m.pins().size() == 3);
    PhasedValueSource p;
    CHECK(p.input_descriptors().size() == 1 && std::string(p.input_descriptors()[0].name) == "sources");
    CHECK(p.input_descriptors()[0].multi && !p.input_descriptors()[0].slot);
    CHECK(p.setting_descriptors().size() == 1 && std::string(p.setting_descriptors()[0].name) == "overlap");
    CHECK(p.setting_descriptors()[0].type == SettingType::Float && p.setting_descriptors()[0].default_value == 0.05f);
    const auto pa = p.array_descriptors();
    const char* columns[] = {"percent", "minSec", "maxSec", "gain"};
    const float defaults[] = {0.0f, 0.0f, 0.0f, 1.0f};
    CHECK(pa.size() == 4);
    for (size_t k = 0; k < 4 && k < pa.size(); ++k) {
        CHECK(std::string(pa[k].name) == columns[k]);
        CHECK(pa[k].groupName && std::string(pa[k].groupName) == "stages");
        CHECK(pa[k].default_value == defaults[k]);
    }
    CHECK(p.param_descriptors().size() == 1 && std::string(p.param_descriptors()[0].name) == "amplitude");
    const std::shared_ptr<Component> hosts[] = {std::make_shared<FullPartials>(), std::make_shared<SequencePartials>(),
                                                std::make_shared<ExplicitPartials>()};
    for (const auto& h : hosts) {
        int found = 0;
        for (const auto& d : h->input_descriptors())
            if (std::string(d.name) == "expandRule") {
                ++found;
                CHECK(d.slot && !d.multi && d.hint && std::string(d.hint) == "an ExpandRule part");
            }
        CHECK(found == 1);
    }
}

static void run_list_node_loader_tests() {
    using namespace list_twins;
    using nlohmann::json;
    std::printf("the list nodes load their feeds and arrays from the file\n");
    std::vector<float> newArrays;
    const json columns = {{"id", "n"}, {"type", "MultiSource"}, {"params", {
        {"source", json::array({json{{"ref", "a"}}, json{{"ref", "b"}}})},
        {"weights", {0.5, 1.0}}, {"delays", {0.0, 100.0}}}}};
    const json plainFeeds = {{"id", "n"}, {"type", "MultiSource"}, {"params", {
        {"source", json::array({json{{"ref", "a"}}, json{{"ref", "b"}}})}}}};
    const uint64_t mNew = loaded_render(voice_doc(json::array({columns})), "n", &newArrays, {"weights", "delays"});
    CHECK(mNew != 0 && newArrays == std::vector<float>({0.5f, 1.0f, 0.0f, 100.0f}));
    CHECK(mNew != loaded_render(voice_doc(json::array({plainFeeds})), "n"));
    newArrays.clear();
    const json table = {{"id", "n"}, {"type", "PhasedValueSource"}, {"params", {{"overlap", 0.01},
        {"sources", json::array({json{{"ref", "a"}}, json{{"ref", "b"}}})},
        {"percent", {0.3, 0.0}}, {"minSec", {0.01, 0.0}}, {"maxSec", {0.02, 0.0}}, {"gain", {0.5, 1.0}}}}};
    const char* cols[] = {"percent", "minSec", "maxSec", "gain"};
    const uint64_t pNew = loaded_render(voice_doc(json::array({table})), "n", &newArrays, {cols[0], cols[1], cols[2], cols[3]});
    CHECK(pNew != 0 && newArrays == std::vector<float>({0.3f, 0.0f, 0.01f, 0.0f, 0.02f, 0.0f, 0.5f, 1.0f}));
    json ragged = table;
    ragged["params"].erase("gain");
    CHECK(three_kinds::refused_naming(voice_doc(json::array({ragged})), false,
        {"node 'n' (PhasedValueSource): table 'stages' takes its columns at one length, all present or all absent",
         "gain absent"}));
}

static void run_expand_rule_loader_tests() {
    using namespace list_twins;
    using nlohmann::json;
    std::printf("the expand rule loads as a node by ref; a ref that is not a rule fails\n");
    const json rule = {{"count", 3}, {"spacing1", 0.3}, {"spacing2", 0.7}, {"dt1", 0.05}, {"power1", 2.0}, {"po2", 0.25}};
    const auto partials_doc = [&](const json& expandRule, bool withNode) {
        json nodes = json::array();
        if (withNode) nodes.push_back(json{{"id", "rule"}, {"type", "ExpandRule"}, {"params", rule}});
        json p = {{"id", "p"}, {"type", "FullPartials"}, {"params", {{"seed", 3902}, {"maxPartials", 6}}}};
        if (!expandRule.is_null()) p["params"]["expandRule"] = expandRule;
        nodes.push_back(p);
        nodes.push_back(json{{"id", "n"}, {"type", "AdditiveSource"},
                             {"params", {{"frequency", 220.0}, {"partials", {{"ref", "p"}}}}}});
        return voice_doc(nodes);
    };
    const uint64_t refRule = loaded_render(partials_doc(json{{"ref", "rule"}}, true), "n");
    CHECK(refRule != 0 && refRule != loaded_render(partials_doc(json(), false), "n"));
    // A ref to no node, and to a node that is not an ExpandRule.
    CHECK(three_kinds::refused_naming(partials_doc(json{{"ref", "nope"}}, false), false, {"nope"}));
    CHECK(three_kinds::refused_naming(partials_doc(json{{"ref", "a"}}, false), false,
        {"node 'p' (FullPartials): 'expandRule' does not accept a SineSource; 'expandRule' takes an ExpandRule part"}));
}

namespace retired_forms {
// The message the load of `text` fails with, as an instrument or, as a plain
// sound, a patch file built as a sound; empty when it loads.
static std::string refusal(const std::string& text, bool file) {
    try { three_kinds::load(file ? three_kinds::plain(text) : text, file); }
    catch (const std::exception& e) { return e.what(); }
    return {};
}
// The voice graph of the two sines `a` and `b` and the nodes of `nodes`, a JSON array.
static std::string doc(const std::string& nodes) {
    return list_twins::voice_doc(nlohmann::json::parse(nodes));
}
// One node `n` of `type` holding `params`, as a JSON array of one.
static std::string node(const char* type, const std::string& params) {
    return std::string(R"([{"id":"n","type":")") + type + R"(","params":)" + params + "}]";
}
// Whether the load of `nodes` fails through both entry points with `want` after its path.
static bool refused_with(const std::string& nodes, const std::string& want) {
    bool ok = true;
    for (bool file : {false, true}) {
        const std::string msg = refusal(doc(nodes), file);
        if (!refusal_says(msg, want)) std::printf("  %s: got \"%s\"\n  want \"%s\"\n", file ? "file" : "text", msg.c_str(), want.c_str());
        ok = ok && refusal_says(msg, want);
    }
    return ok;
}
// Whether `nodes` loads through both entry points.
static bool loads_both(const std::string& nodes) {
    bool ok = true;
    for (bool file : {false, true}) {
        const std::string msg = refusal(doc(nodes), file);
        if (!msg.empty()) std::printf("  %s: load failed: %s\n", file ? "file" : "text", msg.c_str());
        ok = ok && msg.empty();
    }
    return ok;
}
const std::string kConv = "is no longer read: python tools/convert_patch.py <patch> rewrites it as ";
}  // namespace retired_forms

static void run_retired_form_table_tests() {
    std::printf("retired forms: one table, by type, key and where the key sat\n");
    const std::string conv = "python tools/convert_patch.py <patch> rewrites it as ";
    struct Row { const char* type; const char* key; RetiredAt at; std::string want; };
    const Row rows[] = {
        {"Envelope", "stages", RetiredAt::Key, "'stages' is no longer read: " + conv + "the eight stage columns"},
        {"CurveNode", "knots", RetiredAt::Key, "'knots' is no longer read: " + conv + "the columns x and y"},
        {"CurveNode", "exprKnots", RetiredAt::Key,
         "'exprKnots' is no longer read: " + conv + "the columns exprX, exprForm, exprA and exprB"},
        {"PhasedValueSource", "stages", RetiredAt::Key,
         "'stages' is no longer read: " + conv + "sources and the columns percent, minSec, maxSec and gain"},
        {"BWLowpassFilter", "cutoff", RetiredAt::Key, "'cutoff' is no longer read: " + conv + "cutoffFreq"},
        {"BWHighpassFilter", "cutoff", RetiredAt::Key, "'cutoff' is no longer read: " + conv + "cutoffFreq"},
        {"SVFSource", "cutoff", RetiredAt::Key, "'cutoff' is no longer read: " + conv + "cutoffFreq"},
        {"RepeatingSource", "gap", RetiredAt::Key, "'gap' is no longer read: " + conv + "gapDuration"},
        {"MultiSource", "weight", RetiredAt::FeedKey, "'weight' on a feed is no longer read: " + conv + "the array weights"},
        {"MultiSource", "delay", RetiredAt::FeedKey, "'delay' on a feed is no longer read: " + conv + "the array delays"},
        {"FullPartials", "expandRule", RetiredAt::InlineSlot,
         "'expandRule' as an inline object is no longer read: " + conv + "an ExpandRule node wired by ref"},
        {"SequencePartials", "expandRule", RetiredAt::InlineSlot,
         "'expandRule' as an inline object is no longer read: " + conv + "an ExpandRule node wired by ref"},
        {"ExplicitPartials", "expandRule", RetiredAt::InlineSlot,
         "'expandRule' as an inline object is no longer read: " + conv + "an ExpandRule node wired by ref"},
    };
    for (const auto& r : rows) {
        const std::string got = retired_key_refusal(r.type, r.key, r.at);
        if (got != r.want) std::printf("  %s.%s: got \"%s\"\n", r.type, r.key, got.c_str());
        CHECK(got == r.want);
    }
    // The default place is a key of the node's params.
    CHECK(retired_key_refusal("BWLowpassFilter", "cutoff") == rows[4].want);
    // The same key at another place gives nothing.
    CHECK(retired_key_refusal("MultiSource", "weight").empty());
    CHECK(retired_key_refusal("MultiSource", "delay", RetiredAt::InlineSlot).empty());
    CHECK(retired_key_refusal("BWLowpassFilter", "cutoff", RetiredAt::FeedKey).empty());
    CHECK(retired_key_refusal("FullPartials", "expandRule").empty());
    CHECK(retired_key_refusal("Envelope", "stages", RetiredAt::FeedKey).empty());
    // A near miss gives nothing.
    CHECK(retired_key_refusal("BWBandpassFilter", "cutoff").empty());
    CHECK(retired_key_refusal("SegmentSource", "gap").empty());
    CHECK(retired_key_refusal("MultiSource", "stages").empty());
    CHECK(retired_key_refusal("SineSource", "weight", RetiredAt::FeedKey).empty());
    CHECK(retired_key_refusal("AdditiveSource", "formant", RetiredAt::InlineSlot).empty());
}

static void run_retired_form_load_tests() {
    using namespace retired_forms;
    std::printf("retired forms: each fails through both entry points with its sentence; its new form loads\n");
    struct Form { std::string oldNodes, newNodes, want; };
    std::vector<Form> forms = {
        {node("PhasedValueSource", R"({"stages":[{"source":{"ref":"a"},"percent":0.5},{"source":{"ref":"b"}}]})"),
         node("PhasedValueSource", R"({"sources":[{"ref":"a"},{"ref":"b"}],"percent":[0.5,0.0],"minSec":[0.0,0.0],)"
                                   R"("maxSec":[0.0,0.0],"gain":[1.0,1.0]})"),
         "node 'n' (PhasedValueSource): 'stages' " + kConv + "sources and the columns percent, minSec, maxSec and gain"},
        {node("RepeatingSource", R"({"source":{"ref":"a"},"gap":0.1})"),
         node("RepeatingSource", R"({"source":{"ref":"a"},"gapDuration":0.1})"),
         "node 'n' (RepeatingSource): 'gap' " + kConv + "gapDuration"},
        {node("MultiSource", R"({"source":[{"ref":"a","weight":0.5},{"ref":"b"}]})"),
         node("MultiSource", R"({"source":[{"ref":"a"},{"ref":"b"}],"weights":[0.5,1.0]})"),
         "node 'n' (MultiSource): 'source': 'weight' on a feed " + kConv + "the array weights"},
        {node("MultiSource", R"({"source":{"ref":"a","weight":0.5}})"),
         node("MultiSource", R"({"source":{"ref":"a"},"weights":[0.5]})"),
         "node 'n' (MultiSource): 'source': 'weight' on a feed " + kConv + "the array weights"},
        {node("MultiSource", R"({"source":[{"ref":"a"},{"ref":"b","delay":100}]})"),
         node("MultiSource", R"({"source":[{"ref":"a"},{"ref":"b"}],"delays":[0.0,100.0]})"),
         "node 'n' (MultiSource): 'source': 'delay' on a feed " + kConv + "the array delays"},
        {node("MultiSource", R"({"source":{"ref":"a","delay":100}})"),
         node("MultiSource", R"({"source":{"ref":"a"},"delays":[100.0]})"),
         "node 'n' (MultiSource): 'source': 'delay' on a feed " + kConv + "the array delays"},
    };
    for (const char* type : {"BWLowpassFilter", "BWHighpassFilter", "SVFSource"})
        forms.push_back({node(type, R"({"source":{"ref":"a"},"cutoff":500.0})"),
                         node(type, R"({"source":{"ref":"a"},"cutoffFreq":500.0})"),
                         "node 'n' (" + std::string(type) + "): 'cutoff' " + kConv + "cutoffFreq"});
    for (const char* type : {"FullPartials", "SequencePartials", "ExplicitPartials"}) {
        const std::string host = node(type, R"({"expandRule":{"ref":"r"}})");
        forms.push_back({node(type, R"({"expandRule":{"count":3,"spacing1":0.3}})"),
                         R"([{"id":"r","type":"ExpandRule","params":{"count":3,"spacing1":0.3}},)" + host.substr(1),
                         "node 'n' (" + std::string(type) + "): 'expandRule' as an inline object " + kConv
                             + "an ExpandRule node wired by ref"});
    }
    CHECK(forms.size() == 12);
    for (const auto& f : forms) {
        CHECK(refused_with(f.oldNodes, f.want));
        CHECK(loads_both(f.newNodes));
    }
    // A cutoff on the Butterworth bandpass is a key no reader has: unknown.
    CHECK(refused_with(node("BWBandpassFilter", R"({"source":{"ref":"a"},"cutoff":500.0})"),
                       "node 'n' (BWBandpassFilter): unknown key 'cutoff'"));
}

static void run_feed_shape_refusal_tests() {
    using namespace retired_forms;
    std::printf("a feed holds 'ref' or 'tap' alone; a slot takes a feed, not an object\n");
    // A key beside a feed's ref or tap that the table has no row for.
    CHECK(refused_with(node("MultiSource", R"({"source":[{"ref":"a"},{"ref":"b","foo":1}]})"),
                       "node 'n' (MultiSource): 'source': a feed holds 'ref' or 'tap' alone, not 'foo'"));
    CHECK(refused_with(node("SineSource", R"({"amplitude":{"ref":"a","foo":1}})"),
                       "node 'n' (SineSource): 'amplitude': a feed holds 'ref' or 'tap' alone, not 'foo'"));
    CHECK(refused_with(node("SineSource", R"({"amplitude":{"tap":"b","foo":1}})"),
                       "node 'n' (SineSource): 'amplitude': a feed holds 'ref' or 'tap' alone, not 'foo'"));
    // MultiSource's rows are its own: a weight on another node's feed is the general case.
    CHECK(refused_with(node("SineSource", R"({"amplitude":{"ref":"a","weight":0.5}})"),
                       "node 'n' (SineSource): 'amplitude': a feed holds 'ref' or 'tap' alone, not 'weight'"));
    CHECK(refused_with(node("CombinedSource", R"({"source1":{"ref":"a","delay":2}})"),
                       "node 'n' (CombinedSource): 'source1': a feed holds 'ref' or 'tap' alone, not 'delay'"));
    // Both ref and tap.
    CHECK(refused_with(node("SineSource", R"({"amplitude":{"ref":"a","tap":"b"}})"),
                       "node 'n' (SineSource): 'amplitude': a feed holds 'ref' or 'tap', not both"));
    CHECK(refused_with(node("MultiSource", R"({"source":[{"ref":"a","tap":"b"}]})"),
                       "node 'n' (MultiSource): 'source': a feed holds 'ref' or 'tap', not both"));
    // An object with neither on a slot the table has no row for.
    CHECK(refused_with(node("AdditiveSource", R"({"formant":{"foo":1}})"),
                       "node 'n' (AdditiveSource): 'formant' holds an object where a slot takes {\"ref\": id}"));
    // On a signal pin, the same object reaches the resolver's own sentence.
    CHECK(refused_with(node("SineSource", R"({"amplitude":{"foo":1}})"),
                       "node 'n' (SineSource): 'amplitude' takes a number, {\"ref\": id} or {\"tap\": id}, not {\"foo\":1}"));
    // A plain feed and a tap load.
    CHECK(loads_both(node("SineSource", R"({"amplitude":{"ref":"a"},"frequency":{"tap":"b"}})")));
}

static void run_declared_reader_tests() {
    using enum_column::load_node;
    using enum_column::load_setting;
    std::printf("the deleted readers' keys, read by the declarations alone\n");
    // A VarSource with empty params and with none: nothing wired, val 1.
    for (bool withParams : {true, false}) {
        LoadedInstrument ip;
        auto n = load_node("VarSource", withParams ? nlohmann::json::object() : nlohmann::json(), ip);
        auto* v = dynamic_cast<VarSource*>(n.get());
        CHECK(v && !v->get_param("val") && !v->get_param("var") && !v->get_param("varPct"));
        CHECK(v && v->val.current() == 1.0f && v->get_setting("absolute") == 1.0f);
        if (v) { v->prepare(RenderContext{48000}); v->start(4); CHECK(v->next() == 1.0f); }
    }
    // CombinedSource's operation by its labels and indices; "add", which
    // matches no label, and a label in another case are refused.
    for (const char* word : {"add", "mix", "fade", "sum"}) {
        const std::string msg = load_refused(enum_column::one_node("CombinedSource", {{"operation", word}}));
        const bool named = msg.find(std::string("'operation' holds \"") + word + "\", which is not one of its labels") != std::string::npos;
        if (!named) std::printf("  operation %s: %s\n", word, msg.c_str());
        CHECK(named);
    }
    const std::pair<nlohmann::json, float> ops[] = {
        {"Mix", 0.0f}, {"Fade", 2.0f}, {"Multiply", 1.0f}, {"Sum", 3.0f},
        {0, 0.0f}, {1, 1.0f}, {2, 2.0f}, {3, 3.0f}};
    for (const auto& [op, want] : ops) {
        const float got = load_setting("CombinedSource", "operation", op);
        if (got != want) std::printf("  operation %s: %g\n", op.dump().c_str(), got);
        CHECK(got == want);
    }
    {
        LoadedInstrument ip;
        auto c = load_node("CombinedSource", nlohmann::json{{"operation", "Multiply"}}, ip);
        CHECK(c && c->get_setting("gainAdj") == 0.0f && c->get_setting("operation") == 1.0f);
    }
    CHECK(load_setting("RepeatingSource", "gapDuration", 0.5) == 0.5f);
    // A CrossfadeSource and a RepeatingSource with no settings hold the declared defaults.
    for (const char* type : {"CrossfadeSource", "RepeatingSource"})
        for (bool withParams : {true, false}) {
            LoadedInstrument ip;
            auto n = load_node(type, withParams ? nlohmann::json::object() : nlohmann::json(), ip);
            CHECK(n != nullptr);
            if (!n) continue;
            CHECK(!n->setting_descriptors().empty());
            for (const auto& d : n->setting_descriptors()) CHECK(n->get_setting(d.name) == d.default_value);
        }
}

static void run_branch_key_tests() {
    using namespace retired_forms;
    std::printf("the declarations hold no key a deleted reader alone read\n");
    // Each key that left the list, on a type that declares none of them.
    for (const char* key : {"weight", "delay", "sections", "targetPartials", "oneShot", "normalized",
                            "absolute", "baseValue", "bias", "varPct", "name"})
        CHECK(refused_with(node("SineSource", std::string("{\"") + key + "\":1}"),
                           std::string("node 'n' (SineSource): unknown key '") + key + "'"));
    CHECK(refused_with(node("MultiSource", R"({"source":[{"ref":"a"}],"delay":2})"), "node 'n' (MultiSource): unknown key 'delay'"));
    // Where a type declares the key, it loads.
    CHECK(loads_both(node("BWLowpassFilter", R"({"source":{"ref":"a"},"sections":3})")));
    CHECK(loads_both(node("MultiSource", R"({"source":[{"ref":"a"},{"ref":"b"}],"weights":[0.5,0.25]})")));
    CHECK(loads_both(node("NameGate", R"({"name":"tongue"})")));
    CHECK(loads_both(node("SegmentSource", R"({"values":[0.0,1.0],"oneShot":true})")));
}

// The plain sounds the build of the two output parts is compared on, each
// part's feeds in its `params` as {"ref": id}. Their stereo output's 64-bit
// FNV-1a is held as a literal.
namespace plain_parts {
// One channel: its id, the id of the node its source holds, and the params
// beside the source (null: none).
struct Chan { std::string id, source; nlohmann::json params; };
struct Case { const char* name; nlohmann::json signals; std::vector<Chan> chans; nlohmann::json mix; };
static nlohmann::json node(const char* id, const char* type, nlohmann::json params) {
    return nlohmann::json{{"id", id}, {"type", type}, {"params", std::move(params)}};
}
static nlohmann::json ref(const char* id) { return nlohmann::json{{"ref", id}}; }
// A plain sound of 0.25 s at 48 kHz: the signals, each channel, then the
// mixer `mix` (`mixParams` null: no gains) as the output.
static std::string sound(const nlohmann::json& signals, const std::vector<Chan>& chans,
                         const nlohmann::json& mixParams) {
    nlohmann::json nodes = signals;
    nlohmann::json ids = nlohmann::json::array();
    for (const auto& c : chans) {
        nlohmann::json p = c.params.is_null() ? nlohmann::json::object() : c.params;
        p["source"] = nlohmann::json{{"ref", c.source}};
        nodes.push_back(nlohmann::json{{"id", c.id}, {"type", "SoundChannel"}, {"params", p}});
        ids.push_back(nlohmann::json{{"ref", c.id}});
    }
    nlohmann::json mp = mixParams.is_null() ? nlohmann::json::object() : mixParams;
    mp["channels"] = ids;
    nodes.push_back(nlohmann::json{{"id", "mix"}, {"type", "StereoMixer"}, {"params", mp}});
    return nlohmann::json{{"sampleRate", 48000}, {"seconds", 0.25},
                          {"graph", {{"output", "mix"}, {"nodes", nodes}}}}.dump();
}
static std::vector<Case> cases() {
    const auto osc   = node("osc", "SineSource", {{"frequency", 220.0}, {"amplitude", 0.5}});
    const auto saw   = node("saw", "SawSource", {{"frequency", 330.0}, {"amplitude", 0.4}});
    const auto tri   = node("tri", "TriangleSource", {{"frequency", 440.0}, {"amplitude", 0.3}});
    const auto noise = node("noise", "WhiteNoiseSource", {{"amplitude", 0.2}, {"seed", 12345}});
    const auto lfo   = node("lfo", "SineSource", {{"frequency", 3.0}, {"amplitude", 0.4}});
    using J = nlohmann::json;
    return {
        {"one channel, all four numbers", J::array({osc}), {{"ch1", "osc", {{"volume", 0.7}, {"pan", -0.3}}}},
         {{"gainL", 0.9}, {"gainR", 0.8}}},
        {"no numbers on either part", J::array({osc}), {{"ch1", "osc", J()}}, J()},
        {"two channels", J::array({osc, noise}),
         {{"chA", "osc", {{"volume", 0.3}, {"pan", -0.7}}}, {"chB", "noise", {{"volume", 0.9}, {"pan", 0.4}}}},
         {{"gainL", 0.8}, {"gainR", 0.6}}},
        {"three channels", J::array({osc, saw, tri}),
         {{"chA", "osc", {{"volume", 0.6}, {"pan", -0.5}}}, {"chB", "saw", {{"volume", 0.7}, {"pan", 0.5}}}, {"chC", "tri", J()}},
         {{"gainL", 0.9}, {"gainR", 0.7}}},
        {"volume wired", J::array({lfo, osc}), {{"ch1", "osc", {{"volume", ref("lfo")}, {"pan", 0.2}}}},
         {{"gainL", 1.0}, {"gainR", 1.0}}},
        {"pan wired", J::array({lfo, osc}), {{"ch1", "osc", {{"volume", 0.8}, {"pan", ref("lfo")}}}},
         {{"gainL", 1.0}, {"gainR", 1.0}}},
        {"gainL wired", J::array({lfo, osc}), {{"ch1", "osc", {{"volume", 0.8}, {"pan", 0.0}}}},
         {{"gainL", ref("lfo")}, {"gainR", 0.7}}},
    };
}
// The FNV-1a of a sound's stereo render; 0 when the load or the render throws.
static uint64_t render_hash(const std::string& text) {
    std::vector<float> buf;
    if (!render_scratch_patch("renders/scratch/_plain_parts.json", text, buf)) return 0;
    return node_twins::fnv(buf);
}
}

// The two output parts as declared: each pin with its default, and the
// wiring door — a mixer's channels take a SoundChannel and nothing else, a
// channel's source a signal and nothing else.
static void run_output_part_tests() {
    std::printf("the output parts: their pins and the wiring door\n");
    register_all_sources();
    auto& reg = SourceRegistry::instance();
    auto ch = reg.has("SoundChannel") ? std::dynamic_pointer_cast<SoundChannel>(reg.create("SoundChannel")) : nullptr;
    auto mix = reg.has("StereoMixer") ? std::dynamic_pointer_cast<StereoMixer>(reg.create("StereoMixer")) : nullptr;
    CHECK(ch != nullptr && mix != nullptr);
    if (!ch || !mix) return;
    CHECK(dynamic_cast<ValueSource*>(static_cast<Component*>(ch.get())) == nullptr);   // neither is a signal
    CHECK(dynamic_cast<ValueSource*>(static_cast<Component*>(mix.get())) == nullptr);
    auto param_is = [](const ParamDescriptor& d, const char* name, float def, float mn, float mx) {
        return std::string(d.name) == name && d.default_value == def && d.min_value == mn && d.max_value == mx;
    };
    const auto chParams = ch->param_descriptors();
    CHECK(chParams.size() == 2 && param_is(chParams[0], "volume", 1.0f, 0.0f, 4.0f) && param_is(chParams[1], "pan", 0.0f, -1.0f, 1.0f));
    const auto chInputs = ch->input_descriptors();
    CHECK(chInputs.size() == 1 && std::string(chInputs[0].name) == "source" && !chInputs[0].multi && !chInputs[0].slot);
    const auto mixParams = mix->param_descriptors();
    CHECK(mixParams.size() == 2 && param_is(mixParams[0], "gainL", 1.0f, 0.0f, 4.0f) && param_is(mixParams[1], "gainR", 1.0f, 0.0f, 4.0f));
    const auto mixInputs = mix->input_descriptors();
    CHECK(mixInputs.size() == 1 && std::string(mixInputs[0].name) == "channels" && mixInputs[0].multi && mixInputs[0].slot
          && mixInputs[0].hint && std::string(mixInputs[0].hint) == "a SoundChannel");
    CHECK(ch->setting_descriptors().empty() && ch->array_descriptors().empty());
    CHECK(mix->setting_descriptors().empty() && mix->array_descriptors().empty());
    // channels: a channel by set_param, a second appended, a signal and another part refused
    auto ch2 = std::make_shared<SoundChannel>();
    auto sine = std::make_shared<SineSource>();
    auto peaks = std::make_shared<FormantPeaks>();
    CHECK(mix->set_param("channels", ch));
    CHECK(mix->add_param("channels", ch2));
    auto holds_both = [&] {
        return mix->channels.views.size() == 2 && mix->channels.views[0] == ch.get() && mix->channels.views[1] == ch2.get();
    };
    CHECK(holds_both());
    CHECK(!mix->set_param("channels", sine) && holds_both());
    CHECK(!mix->add_param("channels", sine) && holds_both());
    CHECK(!mix->add_param("channels", peaks) && holds_both());
    // source: a signal taken, a part refused
    CHECK(ch->set_param("source", sine) && ch->source.source == sine);
    CHECK(!ch->set_param("source", peaks) && ch->source.source == sine);
}

namespace plain_parts {
// The refusal of a patch file read and built as a sound, after the path its
// message begins with; empty when it loads.
static std::string file_refusal(const char* path, const std::string& text) {
    ScratchPatch file(path, text);
    try { (void)load_sound(file.path); }
    catch (const std::exception& e) {
        std::string msg = e.what();
        if (msg.rfind(file.path + ": ", 0) == 0) msg.erase(0, file.path.size() + 2);
        return msg;
    }
    return {};
}
// The mixer build_sound builds from a patch file; null, with the
// refusal printed, when the load throws.
static std::unique_ptr<Mixer> built_mixer(const std::string& text) {
    ScratchPatch file("renders/scratch/_plain_built.json", text);
    try { return std::move(load_sound(file.path).mixer); }
    catch (const std::exception& e) { std::printf("  load failed: %s\n", e.what()); }
    return nullptr;
}
// The same sound with the mixer's channel list reversed, the file's node order kept.
static std::string reversed_channels(const std::string& text) {
    auto doc = nlohmann::json::parse(text);
    for (auto& n : doc["graph"]["nodes"]) {
        if (n["type"] != "StereoMixer") continue;
        auto& list = n["params"]["channels"];
        std::reverse(list.begin(), list.end());
    }
    return doc.dump();
}
}

// The twin: each plain sound renders the base's bytes.
static void run_plain_part_twin_tests() {
    using namespace plain_parts;
    std::printf("the plain-sound build: each sound against the base's render\n");
    // Each sound's hash, held.
    const uint64_t kBase[] = {
        0xbd803f232d11c7d6ull,   // one channel, all four numbers
        0x428f79ca36025021ull,   // no numbers on either part
        0x6a4b98ef60321f25ull,   // two channels
        0x6da350faa0c174a3ull,   // three channels
        0x8f96948dd16fb3c3ull,   // volume wired
        0xbe932bc542335611ull,   // pan wired
        0x94a4b14bfce0b145ull,   // gainL wired
    };
    const auto all = cases();
    CHECK(all.size() == std::size(kBase));
    for (size_t k = 0; k < all.size() && k < std::size(kBase); ++k) {
        const uint64_t h = render_hash(sound(all[k].signals, all[k].chans, all[k].mix));
        if (h != kBase[k]) std::printf("  %s: 0x%016llx\n", all[k].name, (unsigned long long)h);
        CHECK(h == kBase[k]);
    }
}

// What the build reads from the parts: the channels in the mixer's order, a
// number on a pin as given, a pin the file leaves out at its declared
// constant; an empty channel list renders silence.
static void run_plain_part_build_tests() {
    using namespace plain_parts;
    std::printf("the plain-sound build: the channels' order, each pin's number or default\n");
    const auto all = cases();
    const auto three = sound(all[3].signals, all[3].chans, all[3].mix);
    auto m = built_mixer(three);
    CHECK(m && m->channels.size() == 3);
    if (m && m->channels.size() == 3)
        CHECK(is_constant_of(m->channels[0].volume, 0.6f) && is_constant_of(m->channels[1].volume, 0.7f)
              && is_constant_of(m->channels[2].volume, 1.0f) && is_constant_of(m->channels[0].pan, -0.5f)
              && is_constant_of(m->channels[1].pan, 0.5f) && is_constant_of(m->channels[2].pan, 0.0f)
              && is_constant_of(m->gainL, 0.9f) && is_constant_of(m->gainR, 0.7f));
    auto r = built_mixer(reversed_channels(three));                        // the mixer's list, not the file's node order
    CHECK(r && r->channels.size() == 3);
    if (r && r->channels.size() == 3)
        CHECK(is_constant_of(r->channels[0].volume, 1.0f) && is_constant_of(r->channels[1].volume, 0.7f)
              && is_constant_of(r->channels[2].volume, 0.6f));
    auto bare = built_mixer(sound(all[1].signals, all[1].chans, all[1].mix));
    CHECK(bare && bare->channels.size() == 1);
    if (bare && bare->channels.size() == 1)
        CHECK(is_constant_of(bare->gainL, 1.0f) && is_constant_of(bare->gainR, 1.0f)
              && is_constant_of(bare->channels[0].volume, 1.0f) && is_constant_of(bare->channels[0].pan, 0.0f));
    std::vector<float> buf;
    CHECK(render_scratch_patch("renders/scratch/_plain_empty.json", sound(all[1].signals, {}, nlohmann::json()), buf));
    bool silent = !buf.empty();
    for (float s : buf) silent = silent && s == 0.0f;
    CHECK(silent);
}

// An instrument may hold a mixer and a channel beside its voice: they are
// built and play no part. A mixer as the instrument's output is a part where
// a signal is required.
static void run_instrument_holding_parts_tests() {
    std::printf("the output parts in an instrument: built beside the voice, not its output\n");
    const std::string head = R"({"sampleRate":48000,"instrument":{"polyphony":1},
      "score":[{"note":60,"time":0,"duration":0.05}],"graph":{"output":"osc","nodes":[
      {"id":"osc","type":"SineSource","params":{"frequency":220.0,"amplitude":0.5}})";
    std::vector<float> plain, withParts;
    CHECK(render_scratch_patch("renders/scratch/_instrument_alone.json", head + "]}}", plain));
    CHECK(render_scratch_patch("renders/scratch/_instrument_parts_params.json", head
        + R"(,{"id":"ch1","type":"SoundChannel","params":{"source":{"ref":"osc"},"volume":0.5,"pan":0.3}},)"
        + R"({"id":"mix","type":"StereoMixer","params":{"channels":[{"ref":"ch1"}],"gainL":0.25,"gainR":0.5}}]}})", withParts));
    CHECK(!plain.empty() && plain.size() == withParts.size()
          && std::memcmp(plain.data(), withParts.data(), plain.size() * sizeof(float)) == 0);
    CHECK(load_fails_naming(R"({"sampleRate":48000,"instrument":{"polyphony":1},"graph":{"output":"mix","nodes":[
      {"id":"osc","type":"SineSource","params":{"frequency":220.0}},
      {"id":"ch1","type":"SoundChannel","params":{"source":{"ref":"osc"}}},
      {"id":"mix","type":"StereoMixer","params":{"channels":[{"ref":"ch1"}]}}]}})", "'mix' is a StereoMixer, a part, not a signal"));
}

// The plain sound's refusals, each with its sentence.
static void run_plain_part_refusal_tests() {
    using plain_parts::file_refusal;
    std::printf("the plain-sound build: its refusals\n");
    const std::string osc = R"({"sampleRate":48000,"seconds":0.1,"graph":{"output":"mix","nodes":[
      {"id":"osc","type":"SineSource","params":{"frequency":220.0}},)";
    CHECK(file_refusal("renders/scratch/_plain_no_source.json", osc
        + R"({"id":"ch1","type":"SoundChannel","params":{"volume":1.0}},
             {"id":"mix","type":"StereoMixer","params":{"channels":[{"ref":"ch1"}]}}]}})").ends_with("node 'ch1' (SoundChannel): 'source' is not wired; a channel the mixer lists needs a source"));
    CHECK(file_refusal("renders/scratch/_plain_signal_channel.json",
                       osc + R"({"id":"mix","type":"StereoMixer","params":{"channels":[{"ref":"osc"}]}}]}})").ends_with("node 'mix' (StereoMixer): 'channels' does not accept a SineSource; 'channels' takes a SoundChannel"));
    CHECK(file_fails_naming("renders/scratch/_plain_part_source.json",
        R"({"sampleRate":48000,"seconds":0.1,"graph":{"output":"mix","nodes":[
          {"id":"parts","type":"FullPartials","params":{"maxPartials":4}},
          {"id":"ch1","type":"SoundChannel","params":{"source":{"ref":"parts"}}},
          {"id":"mix","type":"StereoMixer","params":{"channels":[{"ref":"ch1"}]}}]}})",
        "node 'ch1' (SoundChannel): 'source' does not accept a FullPartials, a part, not a signal"));
    CHECK(file_refusal("renders/scratch/_plain_source_nowhere.json", osc
        + R"({"id":"ch1","type":"SoundChannel","params":{"source":{"ref":"nowhere"}}},
             {"id":"mix","type":"StereoMixer","params":{"channels":[{"ref":"ch1"}]}}]}})").ends_with("node 'ch1' (SoundChannel): 'source' reads 'nowhere', which names no node"));
    CHECK(file_refusal("renders/scratch/_plain_channel_output.json",
        R"({"sampleRate":48000,"seconds":0.1,"graph":{"output":"ch1","nodes":[
          {"id":"osc","type":"SineSource","params":{"frequency":220.0}},
          {"id":"ch1","type":"SoundChannel","params":{"source":{"ref":"osc"}}}]}})").ends_with("'ch1' is a SoundChannel, a part, not a signal"));
    CHECK(file_refusal("renders/scratch/_plain_channel_key.json", osc
        + R"({"id":"ch1","type":"SoundChannel","params":{"source":{"ref":"osc"},"bogus":1}},
             {"id":"mix","type":"StereoMixer","params":{"channels":[{"ref":"ch1"}]}}]}})").ends_with("node 'ch1' (SoundChannel): unknown key 'bogus'"));
    CHECK(file_refusal("renders/scratch/_plain_mixer_key.json", osc
        + R"({"id":"ch1","type":"SoundChannel","params":{"source":{"ref":"osc"}}},
             {"id":"mix","type":"StereoMixer","params":{"channels":[{"ref":"ch1"}],"bogus":1}}]}})").ends_with("node 'mix' (StereoMixer): unknown key 'bogus'"));
}

// A node two of a plain sound's pull loops reach is refused, naming the node
// and both loops in the mixer's channel order, the mixer's own loop last.
namespace plain_parts {
// A plain sound whose pull loops share a node, and the sentence that refuses it.
struct Shared { std::string what, text, sentence; };
static std::vector<Shared> shared_cases() {
    using J = nlohmann::json;
    const std::string tail = ": each pulls its nodes on its own clock, so a node belongs to one";
    struct Shape { const char* what; J signals; std::vector<Chan> chans; std::string sentence; };
    const Shape shapes[] = {
        {"two channels on one source", J::array({node("osc", "SineSource", {{"frequency", 220.0}})}),
         {{"ch1", "osc", J()}, {"ch2", "osc", J()}}, "'osc' is reached from channel 'ch1' and from channel 'ch2'" + tail},
        {"one node two levels down", J::array({node("lfo", "SineSource", {{"frequency", 2.0}}),
                                               node("mid", "SineSource", {{"frequency", 5.0}, {"amplitude", ref("lfo")}}),
                                               node("srcA", "SineSource", {{"frequency", 220.0}, {"amplitude", ref("mid")}}),
                                               node("srcB", "SawSource", {{"frequency", 330.0}, {"amplitude", ref("lfo")}})}),
         {{"chA", "srcA", J()}, {"chB", "srcB", J()}}, "'lfo' is reached from channel 'chA' and from channel 'chB'" + tail},
        {"a source and a pan on one node", J::array({node("X", "SineSource", {{"frequency", 0.5}}),
                                                     node("src", "SineSource", {{"frequency", 220.0}, {"amplitude", ref("X")}})}),
         {{"ch1", "src", {{"pan", ref("X")}}}}, "'X' is reached from channel 'ch1' and from the mixer's gains, volumes and pans" + tail},
        {"through a list pin", J::array({node("s1", "SineSource", {{"frequency", 220.0}}), node("s2", "SawSource", {{"frequency", 330.0}}),
                                         node("multi", "MultiSource", {{"source", J::array({ref("s1"), ref("s2")})}})}),
         {{"chA", "multi", J()}, {"chB", "s2", J()}}, "'s2' is reached from channel 'chA' and from channel 'chB'" + tail},
    };
    std::vector<Shared> out;
    for (const auto& s : shapes) out.push_back({s.what, sound(s.signals, s.chans, J()), s.sentence});
    // the loops in the mixer's channel order, not the file's node order
    out.push_back({"the first, its channels reversed", reversed_channels(out[0].text),
                   "'osc' is reached from channel 'ch2' and from channel 'ch1'" + tail});
    // a gain on a node a channel's source reads: the mixer's loop is named last
    out.push_back({"a gain on the source's node", sound(shapes[2].signals, {{"ch1", "src", J()}}, J{{"gainL", ref("X")}}),
                   "'X' is reached from channel 'ch1' and from the mixer's gains, volumes and pans" + tail});
    return out;
}
// Plain sounds whose pull loops share no node: two channels on two sources,
// a wired pan on a node of its own, and a tap from one loop into another.
static std::vector<std::string> sharing_sounds_that_load() {
    using J = nlohmann::json;
    const auto all = cases();
    const J tapAcross = J::array({node("oscA", "SineSource", {{"frequency", 220.0}, {"amplitude", 0.4}}),
                                  node("oscB", "SawSource", {{"frequency", 330.0}, {"amplitude", 0.4}}),
                                  node("comb", "CombinedSource", {{"operation", 0}, {"gainAdj", 0.0}, {"source1", ref("oscA")},
                                                                  {"source2", J{{"tap", "oscB"}}}})});
    return {sound(all[2].signals, all[2].chans, all[2].mix), sound(all[5].signals, all[5].chans, all[5].mix),
            sound(tapAcross, {{"chA", "comb", J()}, {"chB", "oscB", J()}}, J())};
}
}  // namespace plain_parts

static void run_plain_sharing_refusal_tests() {
    using namespace plain_parts;
    std::printf("the plain sound's pull loops: a node two loops reach is refused\n");
    for (const auto& c : shared_cases()) {
        const std::string got = file_refusal("renders/scratch/_plain_shared.json", c.text);
        if (got != c.sentence) std::printf("  %s: %s\n", c.what.c_str(), got.c_str());
        CHECK(got == c.sentence);
    }
}
static void run_plain_sharing_load_tests() {
    using namespace plain_parts;
    std::printf("the plain sound's pull loops: what loads\n");
    for (const auto& t : sharing_sounds_that_load()) {
        std::vector<float> buf;
        CHECK(render_scratch_patch("renders/scratch/_plain_loads.json", t, buf));
        bool sound_ = false;
        for (float s : buf) sound_ = sound_ || s != 0.0f;
        CHECK(sound_);
    }
}
static void run_plain_sharing_multiplex_tests() {
    using namespace plain_parts;
    using J = nlohmann::json;
    std::printf("the plain sound's pull loops: a Multiplex's template is not walked\n");
    const std::string tail = ": each pulls its nodes on its own clock, so a node belongs to one";
    const auto mux = [](const char* id, int count) {
        return node(id, "MultiplexSource", {{"count", count}, {"source", ref("t")}});
    };
    const J lfo = node("lfo", "SineSource", {{"frequency", 2.0}, {"amplitude", 0.5}});
    const J saw = node("b", "SawSource", {{"frequency", 330.0}, {"amplitude", ref("lfo")}});
    const std::string texts[] = {
        sound(J::array({node("t", "SineSource", {{"frequency", 220.0}, {"amplitude", 0.3}}), mux("m1", 2), mux("m2", 3)}),
              {{"c1", "m1", J()}, {"c2", "m2", J()}}, J()),
        sound(J::array({lfo, node("t", "SineSource", {{"frequency", 220.0}, {"amplitude", ref("lfo")}}), mux("m1", 2), saw}),
              {{"c1", "m1", J()}, {"c2", "b", J()}}, J()),
    };
    for (const auto& t : texts) {
        std::vector<float> buf;
        CHECK(render_scratch_patch("renders/scratch/_plain_mux_loads.json", t, buf));
        bool sound_ = false;
        for (float s : buf) sound_ = sound_ || s != 0.0f;
        CHECK(sound_);
    }
    const J comb = node("comb", "CombinedSource", {{"operation", 0}, {"gainAdj", 0.0}, {"source1", ref("m1")},
                                                   {"source2", ref("lfo")}});
    const std::string got = file_refusal("renders/scratch/_plain_mux_shared.json",
        sound(J::array({lfo, node("t", "SineSource", {{"frequency", 220.0}}), mux("m1", 2), comb, saw}),
              {{"c1", "comb", J()}, {"c2", "b", J()}}, J()));
    if (got != "'lfo' is reached from channel 'c1' and from channel 'c2'" + tail) std::printf("  beside a Multiplex: %s\n", got.c_str());
    CHECK(got == "'lfo' is reached from channel 'c1' and from channel 'c2'" + tail);
}

// The table's rows for a node's own keys and for the document's: each gives
// its sentence at its place and nothing at another; a row with no type
// answers for every type.
static void run_retired_spelling_table_tests() {
    std::printf("retired spellings: a node's keys and the document's, in the one table\n");
    const std::string conv = "is no longer read: python tools/convert_patch.py <patch> rewrites it as ";
    struct Row { const char* type; const char* key; RetiredAt at; std::string want; };
    const Row rows[] = {
        {"SoundChannel", "inputs", RetiredAt::NodeKey, "'inputs' beside params " + conv + "the feed 'source' in params"},
        {"StereoMixer", "inputs", RetiredAt::NodeKey, "'inputs' beside params " + conv + "the feeds 'channels' in params"},
        {"", "ui", RetiredAt::TopKey, "'ui' at the top of the file " + conv + "layout"},
        {"", "groups", RetiredAt::TopKey, "'groups' at the top of the file " + conv + "layout.groups"},
        {"", "_comment", RetiredAt::TopKey, "'_comment' at the top of the file " + conv + "comment"},
        {"", "_provenance", RetiredAt::TopKey, "'_provenance' at the top of the file " + conv + "comment"},
    };
    for (const auto& r : rows) {
        const std::string got = retired_key_refusal(r.type, r.key, r.at);
        if (got != r.want) std::printf("  '%s'.%s: got \"%s\"\n", r.type, r.key, got.c_str());
        CHECK(got == r.want);
    }
    // A row with no type answers whatever type is asked.
    CHECK(retired_key_refusal("SoundChannel", "ui", RetiredAt::TopKey) == rows[2].want);
    CHECK(retired_key_refusal("SineSource", "_provenance", RetiredAt::TopKey) == rows[5].want);
    // The same key at another place gives nothing.
    for (RetiredAt at : {RetiredAt::Key, RetiredAt::FeedKey, RetiredAt::InlineSlot, RetiredAt::TopKey}) {
        CHECK(retired_key_refusal("SoundChannel", "inputs", at).empty());
        CHECK(retired_key_refusal("StereoMixer", "inputs", at).empty());
    }
    for (const char* key : {"ui", "groups", "_comment", "_provenance"})
        for (RetiredAt at : {RetiredAt::Key, RetiredAt::FeedKey, RetiredAt::InlineSlot, RetiredAt::NodeKey})
            CHECK(retired_key_refusal("", key, at).empty());
    // 'inputs' on another type, and on none.
    for (const char* type : {"SineSource", "MultiSource", "PerformNode", ""})
        CHECK(retired_key_refusal(type, "inputs", RetiredAt::NodeKey).empty());
    // Another key of a node, and another key of the document.
    for (const char* key : {"id", "type", "params", "dynamicPins", "comment"})
        CHECK(retired_key_refusal("SoundChannel", key, RetiredAt::NodeKey).empty());
    for (const char* key : {"sampleRate", "seconds", "instrument", "score", "graph", "layout", "comment", "inputs"})
        CHECK(retired_key_refusal("", key, RetiredAt::TopKey).empty());
}

// A channel's or a mixer's node-level `inputs` fails the load of a plain sound
// and of an instrument, with the table's sentence after the node's id and
// type; the same parts with their feeds in params load.
static void run_retired_spelling_load_tests() {
    using plain_parts::file_refusal;
    std::printf("retired spellings: a channel's and a mixer's 'inputs' refused in a file and an instrument\n");
    const std::string conv = "is no longer read: python tools/convert_patch.py <patch> rewrites it as ";
    const std::string osc = R"({"id":"osc","type":"SineSource","params":{"frequency":220.0,"amplitude":0.5}})";
    const std::string chNew = R"({"id":"ch1","type":"SoundChannel","params":{"source":{"ref":"osc"},"volume":0.5}})";
    const std::string chOld = R"({"id":"ch1","type":"SoundChannel","inputs":{"source":"osc"},"params":{"volume":0.5}})";
    const std::string mixNew = R"({"id":"mix","type":"StereoMixer","params":{"channels":[{"ref":"ch1"}],"gainL":0.5}})";
    const std::string mixOld = R"({"id":"mix","type":"StereoMixer","inputs":{"channels":["ch1"]},"params":{"gainL":0.5}})";
    const std::string chWant = "node 'ch1' (SoundChannel): 'inputs' beside params " + conv + "the feed 'source' in params";
    const std::string mixWant = "node 'mix' (StereoMixer): 'inputs' beside params " + conv + "the feeds 'channels' in params";
    const std::string none;
    auto nodes = [&](const std::string& ch, const std::string& mix) { return osc + "," + ch + "," + mix + "]}}"; };
    auto instrument_refusal = [&](const std::string& ch, const std::string& mix) -> std::string {
        return load_refused(R"({"sampleRate":48000,"instrument":{"polyphony":1},"graph":{"output":"osc","nodes":[)" + nodes(ch, mix));
    };
    struct Case { const char* what; const std::string& ch; const std::string& mix; const std::string& want; };
    const Case cases[] = {{"the channel's", chOld, mixNew, chWant}, {"the mixer's", chNew, mixOld, mixWant},
                          {"both, the channel first", chOld, mixOld, chWant}, {"neither", chNew, mixNew, none}};
    for (const auto& c : cases) {
        const std::string inFile = file_refusal("renders/scratch/_retired_inputs.json",
            R"({"sampleRate":48000,"seconds":0.05,"graph":{"output":"mix","nodes":[)" + nodes(c.ch, c.mix));
        const std::string inVoice = instrument_refusal(c.ch, c.mix);
        if (!refusal_says(inFile, c.want) || !refusal_says(inVoice, c.want))
            std::printf("  %s: file \"%s\", instrument \"%s\"\n", c.what, inFile.c_str(), inVoice.c_str());
        CHECK(refusal_says(inFile, c.want));
        CHECK(refusal_says(inVoice, c.want));
    }
}

// Which of the voice's six adapters a PerformNode `f` loads as: its index in
// the labels' order, -1 for none, -2 with the refusal when the load throws.
static int perform_field_of(const nlohmann::json& perfNode, std::string& refusal) {
    using J = nlohmann::json;
    const J osc = {{"id", "osc"}, {"type", "SineSource"}, {"params", J{{"frequency", J{{"ref", "f"}}}}}};
    const J doc = {{"sampleRate", 48000}, {"instrument", J{{"polyphony", 1}}},
                   {"graph", J{{"output", "osc"}, {"nodes", J::array({perfNode, osc})}}}};
    try {
        auto ip = load_inst(doc.dump());
        const auto& v = ip.instrument->voicePool[0];
        const Component* f = v.nodesById.at("f").get();
        const Component* six[] = {v.freqOut.get(), v.velOut.get(), v.wheelOut.get(),
                                  v.pressOut.get(), v.durOut.get(), v.onsetOut.get()};
        for (int k = 0; k < 6; ++k)
            if (f == six[k]) return k;
        return -1;
    }
    catch (const std::exception& e) { refusal = e.what(); }
    return -2;
}

// A PerformNode's field is read against the type's declared labels: a label
// exactly (the label in upper case is refused), an index, absent as the
// first; the adapter is the voice's own.
static void run_perform_node_field_tests() {
    using J = nlohmann::json;
    std::printf("PerformNode: the field against its declared labels\n");
    auto field_node = [](const J& v) { return J{{"id", "f"}, {"type", "PerformNode"}, {"params", J{{"field", v}}}}; };
    std::string refusal;
    for (int k = 0; k < 6; ++k) {
        const std::string label = PerformNode::kFieldLabels[k];
        std::string upper = label;
        for (auto& c : upper) c = char(std::toupper(static_cast<unsigned char>(c)));
        CHECK(perform_field_of(field_node(label), refusal) == k);
        refusal.clear();
        CHECK(perform_field_of(field_node(upper), refusal) == -2);
        CHECK(refusal.find("'field' holds \"" + upper + "\", which is not one of its labels") != std::string::npos);
        CHECK(perform_field_of(field_node(k), refusal) == k);
    }
    CHECK(perform_field_of(J{{"id", "f"}, {"type", "PerformNode"}}, refusal) == 0);
    CHECK(perform_field_of(J{{"id", "f"}, {"type", "PerformNode"}, {"params", J::object()}}, refusal) == 0);
    for (const J& bad : {J("bogus"), J(6), J(2.5), J(true)}) {
        refusal.clear();
        CHECK(perform_field_of(field_node(bad), refusal) == -2);
        const std::string want = "node 'f' (PerformNode): 'field' holds " + bad.dump()
                               + ", which is not one of its labels (frequency, velocity, wheel, pressure, duration, onset) or an index into them";
        if (!refusal_says(refusal, want)) std::printf("  %s: %s\n", bad.dump().c_str(), refusal.c_str());
        CHECK(refusal_says(refusal, want));
    }
    // the bend check reads the field the same way: an index loads beside a node that reads frequency once
    bool loaded = true;
    try {
        (void)load_inst(R"({"sampleRate":48000,"instrument":{"polyphony":1},"graph":{"output":"ks","nodes":[
          {"id":"f","type":"PerformNode","params":{"field":0}},
          {"id":"ks","type":"KSString","params":{"frequency":{"ref":"f"}}}]}})");
    } catch (const std::exception& e) { std::printf("  load failed: %s\n", e.what()); loaded = false; }
    CHECK(loaded);
    CHECK(file_fails_naming("renders/scratch/_plain_perform.json",
        R"({"sampleRate":48000,"seconds":0.1,"graph":{"output":"osc","nodes":[
          {"id":"f","type":"PerformNode","params":{"field":"velocity"}},
          {"id":"osc","type":"SineSource","params":{"frequency":220.0,"amplitude":{"ref":"f"}}}]}})", "node 'f' (PerformNode): a plain sound has no note to read"));
}

// The registered type: a Generator with one enum setting, the six labels.
static void run_perform_node_type_tests() {
    std::printf("PerformNode: the registered declaration\n");
    register_all_sources();
    auto& reg = SourceRegistry::instance();
    auto n = reg.has("PerformNode") ? reg.create("PerformNode") : nullptr;
    CHECK(std::dynamic_pointer_cast<Generator>(n) != nullptr);
    if (!n) return;
    const auto settings = n->setting_descriptors();
    CHECK(settings.size() == 1);
    if (settings.size() == 1) {
        const auto& d = settings[0];
        CHECK(std::string(d.name) == "field" && d.type == SettingType::Int && d.default_value == 0.0f
              && d.min_value == 0.0f && d.max_value == 5.0f && d.enum_labels != nullptr);
        const char* want[] = {"frequency", "velocity", "wheel", "pressure", "duration", "onset"};
        bool same = d.enum_labels != nullptr;
        for (int k = 0; same && k < 6; ++k) same = d.enum_labels[k] && std::string(d.enum_labels[k]) == want[k];
        CHECK(same && d.enum_labels[6] == nullptr);
    }
    CHECK(n->param_descriptors().empty() && n->input_descriptors().empty() && n->array_descriptors().empty());
    PerformNode p;
    CHECK(p.compute() == 0.0f);
}

// The preset form's curve keys: a label of the stage type, in any case, reads
// as its index; Hold, any other word and an index outside 0 to 3 are refused.
static void run_preset_curve_label_tests() {
    using J = nlohmann::json;
    std::printf("the preset form: a curve key as a label or as its index\n");
    auto columns = [](const J& p) {
        std::vector<std::vector<float>> out;
        try {
            auto env = envelope_from_preset_json(p);
            for (const auto& d : env->array_descriptors())
                if (d.groupName && std::string_view(d.groupName) == "stages") out.push_back(env->get_array(d.name));
        } catch (const std::exception& e) { std::printf("  %s: %s\n", p.dump().c_str(), e.what()); }
        return out;
    };
    struct Preset { const char* name; std::vector<std::string> keys; };
    const Preset presets[] = {{"ar", {"attack", "release"}}, {"as", {"attack"}}, {"asr", {"attack", "release"}},
                              {"ads", {"attack", "decay"}}, {"adr", {"attack", "decay", "release"}},
                              {"adsr", {"attack", "decay", "release"}}};
    for (const auto& pr : presets)
        for (const auto& key : pr.keys)
            for (int i = 0; i < 4; ++i) {
                const std::string label = Envelope::kStageTypeLabels[i];
                std::string lower = label;
                for (auto& c : lower) c = char(std::tolower(static_cast<unsigned char>(c)));
                auto with = [&](const J& v) { return J{{"preset", pr.name}, {key + "Curve", v}, {key + "Power", 0.5}}; };
                const auto byIndex = columns(with(i));
                CHECK(byIndex.size() == 8);
                CHECK(columns(with(label)) == byIndex);
                CHECK(columns(with(lower)) == byIndex);
            }
    CHECK(columns(J{{"preset", "ar"}, {"attackCurve", "Expo"}}) != columns(J{{"preset", "ar"}, {"attackCurve", "Linear"}}));
    auto refusal = [](const J& v) {
        try { (void)envelope_from_preset_json(J{{"preset", "ar"}, {"attackCurve", v}}); }
        catch (const std::exception& e) { return std::string(e.what()); }
        return std::string();
    };
    for (const char* word : {"Hold", "bogus"})
        CHECK(refusal(word) == "envelope preset: attackCurve is \"" + std::string(word)
                               + "\"; a curve is Linear, Expo, InverseExpo or Sine, or its index 0 to 3");
    CHECK(refusal(4).find("attackCurve is 4; a curve is 0 (Linear)") != std::string::npos);
    CHECK(refusal(-1).find("attackCurve is -1; a curve is 0 (Linear)") != std::string::npos);
}

// A combiner's gainAdj scales its second operand by 1 + gainAdj, and its
// declared range takes a signed scale.
static void run_gain_adj_range_tests() {
    std::printf("CombinedSource.gainAdj: declared from -100 to 100\n");
    CombinedSource comb(nullptr, nullptr, CombineOp::Sum);
    bool found = false;
    for (const auto& d : comb.setting_descriptors()) {
        if (std::string_view(d.name) != "gainAdj") continue;
        found = true;
        CHECK(d.min_value == -100.0f);
        CHECK(d.max_value == 100.0f);
    }
    CHECK(found);
}

// The four evolutions take an int setting outside its declared range as
// given: it reads back so, and the evolution, hosted in a wavetable, renders
// a thousand finite samples on it with no fault.
static void run_evolution_unclamped_setting_tests() {
    std::printf("the four evolutions: an int setting outside its range is used as given\n");
    struct Case { const char* type; const char* setting; float value; };
    const Case cases[] = {
        {"CellularAutomatonEvolution", "rule", 300.0f},
        {"CellularAutomatonEvolution", "rule", -1.0f},
        {"CellularAutomatonEvolution", "stepEvery", 0.0f},
        {"ReactionDiffusionEvolution", "subSteps", 0.0f},
        {"HistogramEqualizeEvolution", "bins", 3.0f},
        {"HistogramEqualizeEvolution", "bins", 300.0f},
        {"SortErosionEvolution", "swapsPerSample", 0.0f},
    };
    auto make = [](std::string_view type) -> std::shared_ptr<Component> {
        if (type == "CellularAutomatonEvolution") return std::make_shared<CellularAutomatonEvolution>();
        if (type == "ReactionDiffusionEvolution") return std::make_shared<ReactionDiffusionEvolution>();
        if (type == "HistogramEqualizeEvolution") return std::make_shared<HistogramEqualizeEvolution>();
        return std::make_shared<SortErosionEvolution>();
    };
    for (const auto& c : cases) {
        auto evo = make(c.type);
        evo->set_setting(c.setting, c.value);
        const bool readBack = evo->get_setting(c.setting) == c.value;
        TestHost h;
        WavetableSource w;
        w.set_param("evolution", evo);
        w.prepare(h.ctx); w.start(4800);
        bool finite = true;
        for (int i = 0; i < 1000; ++i) { h.clock.tick(); finite = finite && std::isfinite(w.next()); }
        const unsigned char faults = w.take_faults();
        if (!readBack || !finite || faults != 0)
            std::printf("  %s.%s = %g: read back %g, finite %d, faults %d\n", c.type, c.setting, c.value,
                        evo->get_setting(c.setting), int(finite), int(faults));
        CHECK(readBack);
        CHECK(finite);
        CHECK(faults == 0);
    }
}

// A held line of three notes on an instrument whose amplitude Envelope holds
// `trigger` as given, each note shorter than the Envelope's first stage so it
// is mid-flight at every Setup.
static std::string trigger_line_patch(int trigger) {
    return std::string(R"({"sampleRate":48000,"instrument":{"polyphony":1,"sustaining":true},
      "graph":{"output":"sine1","nodes":[
        {"id":"env1","type":"Envelope","params":{"timeMode":"seconds","trigger":)")
         + std::to_string(trigger) + R"(,
          "startVal":[1.0,0.1,1.0],"endVal":[0.1,1.0,1.0],"power":[0.0,0.0,0.0],
          "type":["Linear","Linear","Linear"],"percent":[0.15,0.15,0.0],"minSec":[0.0,0.0,0.0],
          "maxSec":[0.0,0.0,0.0],"nominal":[0.0,0.0,0.0]}},
        {"id":"sine1","type":"SineSource","params":{"frequency":220.0,"amplitude":{"ref":"env1"},"phase":0.0}}]}})";
}

// Loads trigger_line_patch(trigger) and renders the line. A `constant` of 0
// or more first unwires every voice's env1.trigger and sets the pin's own
// constant to it. `listed` says whether env1 is in every voice's trigger
// bindings with no wire on its trigger: a number is the pin's own constant.
static std::vector<float> render_trigger_line(int trigger, int constant, bool& listed) {
    auto ip = load_inst(trigger_line_patch(trigger));
    auto* pi = dynamic_cast<PitchedInstrument*>(ip.instrument.get());
    listed = pi != nullptr && !pi->voicePool.empty();
    if (!pi) return {};
    for (auto& vg : pi->voicePool) {
        auto it = vg.nodesById.find("env1");
        auto env = it == vg.nodesById.end() ? nullptr : std::dynamic_pointer_cast<Envelope>(it->second);
        if (!env) { listed = false; continue; }
        if (constant >= 0) {
            env->clear_param("trigger");
            env->trigger.constant = float(constant);
        }
        listed = listed && env->trigger.source == nullptr
              && std::find(vg.triggerBindings.begin(), vg.triggerBindings.end(), env.get()) != vg.triggerBindings.end();
    }
    pi->play_note({.start = 0.0f, .duration = 0.1f, .noteNumber = 57.0f, .velocity = 0.8f, .onset = 0, .hold = true});
    pi->play_note({.start = 0.1f, .duration = 0.1f, .noteNumber = 59.0f, .velocity = 0.8f, .onset = 0, .hold = true});
    pi->play_note({.start = 0.2f, .duration = 0.1f, .noteNumber = 60.0f, .velocity = 0.8f, .onset = 0, .hold = false});
    std::vector<float> buf(size_t(0.5f * 48000), 0.0f);
    ip.instrument->render(buf.data(), int(buf.size()));
    return buf;
}

// fire_triggers asks the trigger's value: the pin's own constant 1 restarts
// the Envelope, as loaded or set by hand, and a constant 0 does not.
static void run_trigger_value_tests() {
    std::printf("fire_triggers: an Envelope restarts on its trigger's value, wired or not\n");
    bool listedA = false, listedB = false, listedC = false, listedD = false;
    const auto a = render_trigger_line(1, -1, listedA);   // as loaded: the trigger the pin's own constant 1
    const auto b = render_trigger_line(1, 1, listedB);    // unwired, the pin's constant 1
    const auto c = render_trigger_line(1, 0, listedC);    // unwired, the pin's constant 0
    const auto d = render_trigger_line(0, 0, listedD);    // "trigger": 0, the pin's own 0, which binds nothing
    CHECK(listedA && listedB && listedC);
    CHECK(!listedD);
    CHECK(!a.empty() && a == b);
    CHECK(c != a);
    CHECK(c == d);
}

// A Multiplex given a one-argument builder calls it once per clone, with the
// clone's index, at every rebuild, and emits the clones' mean; a clone's inner
// node that only the root's pin holds lives as long as the Multiplex.
static void run_multiplex_builder_tests() {
    std::printf("MultiplexSource: a builder that returns the clone's root\n");
    std::vector<int> calls;
    std::vector<std::weak_ptr<ValueSource>> inner;
    auto allAlive = [&inner] {
        bool alive = !inner.empty();
        for (const auto& w : inner) alive = alive && !w.expired();
        return alive;
    };
    MultiplexSource mux;
    mux.set_setting("count", 3.0f);
    mux.set_template([&calls, &inner](int i) -> std::shared_ptr<ValueSource> {
        calls.push_back(i);
        auto leaf = konst(float(i + 1));
        inner.push_back(leaf);
        return std::make_shared<CombinedSource>(leaf, konst(0.0f), CombineOp::Sum);
    });
    TestHost h;
    mux.prepare(h.ctx);
    CHECK(calls == std::vector<int>({0, 1, 2}));
    CHECK(allAlive());
    mux.start(64); h.clock.tick();
    CHECK_NEAR(mux.next(), 2.0f, 1e-6f);                  // (1 + 2 + 3) / 3
    calls.clear(); inner.clear();
    mux.mark_dirty();
    mux.prepare(h.ctx);
    CHECK(calls == std::vector<int>({0, 1, 2}));
    CHECK(allAlive());
    calls.clear(); inner.clear();
    mux.set_setting("count", 5.0f);
    mux.prepare(h.ctx);
    CHECK(calls == std::vector<int>({0, 1, 2, 3, 4}));
    CHECK(allAlive());
    mux.start(64); h.clock.tick();
    CHECK_NEAR(mux.next(), 3.0f, 1e-6f);                  // (1 + 2 + 3 + 4 + 5) / 5
}

// A builder given by set_template is the one the next prepare() asks: a
// Multiplex prepared and pulled with builder A, then given builder B and
// prepared and pulled again, asks B for every clone and emits B's mean.
static void run_multiplex_template_change_tests() {
    std::printf("MultiplexSource: the next prepare asks the builder set_template gave it\n");
    int askedA = 0, askedB = 0;
    MultiplexSource mux;
    mux.set_setting("count", 2.0f);
    mux.set_template([&askedA](int) -> std::shared_ptr<ValueSource> { ++askedA; return konst(1.0f); });
    TestHost h;
    mux.prepare(h.ctx);
    mux.start(64); h.clock.tick();
    CHECK_NEAR(mux.next(), 1.0f, 1e-6f);
    mux.set_template([&askedB](int i) -> std::shared_ptr<ValueSource> { ++askedB; return konst(float(10 + i)); });
    mux.prepare(h.ctx);
    mux.start(64); h.clock.tick();
    CHECK(askedA == 2 && askedB == 2);
    CHECK_NEAR(mux.next(), 10.5f, 1e-6f);                 // (10 + 11) / 2
}

// The mixer's listed signals report under their ids after the channels'
// reports, each once, as their bits are taken; with none listed the mixer
// reports what its channels report.
static void run_mixer_signal_fault_tests() {
    std::printf("Mixer: the listed signals report under their ids, after the channels\n");
    struct OnceFaultSource final : MonoSource {
        bool reported{false};
        void prepare(const RenderContext&) override {}
        void render(float* out, int frames) override { std::fill(out, out + frames, 0.0f); }
        void collect_faults(std::vector<FaultReport>& out) override {
            if (!reported) out.push_back({"ch", "OnceFault", fault_bit(Fault::TableClamped)});
            reported = true;
        }
    };
    TestHost h;
    auto a = std::make_shared<SineSource>();
    a->set_param("frequency", konst(440.0f));
    auto b = std::make_shared<SineSource>();
    b->set_param("frequency", konst(0.0f));               // clamped: a fault
    a->prepare(h.ctx); b->prepare(h.ctx);
    a->start(16); b->start(16);
    for (int i = 0; i < 4; ++i) { h.clock.tick(); a->next(); b->next(); }
    Mixer m;
    m.channels.push_back(Channel{std::make_unique<OnceFaultSource>(), nullptr, nullptr});
    m.signals = {{"a", a}, {"b", b}};
    std::vector<FaultReport> got;
    m.collect_faults(got);
    CHECK(got.size() == 2);
    CHECK(got.size() == 2 && got[0].nodeId == "ch" && got[1].nodeId == "b");
    CHECK(got.size() == 2 && std::string(got[1].type) == b->type_name()
          && got[1].faults == fault_bit(Fault::FrequencyClamped));
    got.clear();
    m.collect_faults(got);
    CHECK(got.empty());                                   // the bits were taken
    Mixer plain;
    plain.channels.push_back(Channel{std::make_unique<OnceFaultSource>(), nullptr, nullptr});
    plain.collect_faults(got);
    CHECK(got.size() == 1 && got[0].nodeId == "ch" && got[0].faults == fault_bit(Fault::TableClamped));
}

// ---------------------------------------------------------------------------
// The patch module's records, its declarations table and Patch::check().
#include "mforce/patch/fragment.h"
#include "mforce/patch/patch.h"
#include "mforce/patch/table_rule.h"
#include "mforce/patch/type_declarations.h"
#include <chrono>

namespace patch_records {
// A Patch with every member of every record set away from its default.
static Patch full_patch() {
    Node a;
    a.id = "osc"; a.type = "SineSource"; a.seed = 7u;
    a.values["phase"] = 0.25f;
    a.wires["amplitude"] = {Wire{"env", false}, Wire{"lfo", true}};
    a.settings["mode"] = 1.0f;
    a.strings["name"] = "tongue";
    a.arrays["col"] = {0.5f, 1.0f};
    a.dynamicPins["t60"] = "curve";
    a.comment = "the tone";
    Patch p;
    p.graph.nodes = {a, Node{"env", "Envelope"}};
    p.graph.output = "osc";
    p.instrument = InstrumentBlock{2, 0.5f, true, {"tongue", "slur"}, 20.0f};
    p.layout.positions["osc"] = Vec2{1, 2};
    p.layout.outputPos = Vec2{3, 4};
    p.layout.panning = Vec2{5, 6};
    p.layout.groups = {Group{"Voice", {"osc", "env"}, Vec2{7, 8}}};
    p.layout.faces = {NoteFace{"Note", {{"frequency", "freq"}}, Vec2{9, 10}}};
    p.sampleRate = 44100;
    p.seconds = 2.5;
    p.score = "[]";
    p.comment = "a patch";
    return p;
}
static Fragment full_fragment() {
    const Patch p = full_patch();
    return Fragment{p.graph.nodes, p.layout.positions, p.layout.groups, p.layout.faces};
}
struct PatchCase { const char* member; void (*change)(Patch&); };
static std::vector<PatchCase> patch_cases() {
    return {
        {"Wire.source", [](Patch& p) { p.graph.nodes[0].wires["amplitude"][1].source = "lfo2"; }},
        {"Wire.tap", [](Patch& p) { p.graph.nodes[0].wires["amplitude"][1].tap = false; }},
        {"Node.id", [](Patch& p) { p.graph.nodes[0].id = "osc2"; }},
        {"Node.type", [](Patch& p) { p.graph.nodes[0].type = "SawSource"; }},
        {"Node.seed", [](Patch& p) { p.graph.nodes[0].seed = 8u; }},
        {"Node.seed (absent)", [](Patch& p) { p.graph.nodes[0].seed.reset(); }},
        {"Node.values", [](Patch& p) { p.graph.nodes[0].values["phase"] = 0.5f; }},
        {"Node.wires", [](Patch& p) { p.graph.nodes[0].wires["phase"] = {Wire{"env"}}; }},
        {"Node.settings", [](Patch& p) { p.graph.nodes[0].settings["mode"] = 2.0f; }},
        {"Node.strings", [](Patch& p) { p.graph.nodes[0].strings["name"] = "slur"; }},
        {"Node.arrays", [](Patch& p) { p.graph.nodes[0].arrays["col"][1] = 2.0f; }},
        {"Node.dynamicPins", [](Patch& p) { p.graph.nodes[0].dynamicPins["t60"] = "curve2"; }},
        {"Node.comment", [](Patch& p) { p.graph.nodes[0].comment = "another"; }},
        {"Graph.nodes", [](Patch& p) { p.graph.nodes.pop_back(); }},
        {"Graph.output", [](Patch& p) { p.graph.output = "env"; }},
        {"InstrumentBlock.polyphony", [](Patch& p) { p.instrument->polyphony = 3; }},
        {"InstrumentBlock.volume", [](Patch& p) { p.instrument->volume = 0.25f; }},
        {"InstrumentBlock.sustaining", [](Patch& p) { p.instrument->sustaining = false; }},
        {"InstrumentBlock.onsets", [](Patch& p) { p.instrument->onsets.pop_back(); }},
        {"InstrumentBlock.glideMs", [](Patch& p) { p.instrument->glideMs = 15.0f; }},
        {"Vec2.x", [](Patch& p) { p.layout.positions["osc"].x = 1.5f; }},
        {"Vec2.y", [](Patch& p) { p.layout.positions["osc"].y = 2.5f; }},
        {"Group.name", [](Patch& p) { p.layout.groups[0].name = "Body"; }},
        {"Group.members", [](Patch& p) { p.layout.groups[0].members.pop_back(); }},
        {"Group.pos", [](Patch& p) { p.layout.groups[0].pos.y = 0.0f; }},
        {"NoteFace.label", [](Patch& p) { p.layout.faces[0].label = "Note2"; }},
        {"NoteFace.fields", [](Patch& p) { p.layout.faces[0].fields["velocity"] = "vel"; }},
        {"NoteFace.pos", [](Patch& p) { p.layout.faces[0].pos.reset(); }},
        {"Layout.positions", [](Patch& p) { p.layout.positions["env"] = Vec2{}; }},
        {"Layout.outputPos", [](Patch& p) { p.layout.outputPos->x = 0.0f; }},
        {"Layout.panning", [](Patch& p) { p.layout.panning.reset(); }},
        {"Layout.groups", [](Patch& p) { p.layout.groups.push_back(Group{"Body"}); }},
        {"Layout.faces", [](Patch& p) { p.layout.faces.clear(); }},
        {"Patch.graph", [](Patch& p) { p.graph = Graph{}; }},
        {"Patch.instrument", [](Patch& p) { p.instrument.reset(); }},
        {"Patch.layout", [](Patch& p) { p.layout = Layout{}; }},
        {"Patch.sampleRate", [](Patch& p) { p.sampleRate = 48000; }},
        {"Patch.seconds", [](Patch& p) { p.seconds = 3.0; }},
        {"Patch.score", [](Patch& p) { p.score = ""; }},
        {"Patch.comment", [](Patch& p) { p.comment = ""; }},
    };
}
struct FragmentCase { const char* member; void (*change)(Fragment&); };
static std::vector<FragmentCase> fragment_cases() {
    return {
        {"Fragment.nodes", [](Fragment& f) { f.nodes[1].type = "SawSource"; }},
        {"Fragment.positions", [](Fragment& f) { f.positions["env"] = Vec2{}; }},
        {"Fragment.groups", [](Fragment& f) { f.groups[0].members.push_back("lfo"); }},
        {"Fragment.faces", [](Fragment& f) { f.faces[0].fields.clear(); }},
    };
}
}  // namespace patch_records

// The records are plain values: a copy compares equal, and a change to any
// one member of any record, at its own depth, compares unequal.
static void run_patch_record_tests() {
    using namespace patch_records;
    std::printf("the patch records: a copy equal, a change to any member unequal\n");
    const Patch whole = full_patch();
    Patch copy = whole;
    CHECK(copy == whole);
    for (const auto& c : patch_cases()) {
        Patch changed = whole;
        c.change(changed);
        const bool unequal = !(changed == whole);
        if (!unequal) std::printf("  %s: the change compares equal\n", c.member);
        CHECK(unequal);
    }
    const Fragment fragment = full_fragment();
    Fragment fragmentCopy = fragment;
    CHECK(fragmentCopy == fragment);
    for (const auto& c : fragment_cases()) {
        Fragment changed = fragment;
        c.change(changed);
        const bool unequal = !(changed == fragment);
        if (!unequal) std::printf("  %s: the change compares equal\n", c.member);
        CHECK(unequal);
    }
    // What == on a float cannot see: a negative zero compares equal to a
    // zero, so two records that would be written as "-0" and "0" are equal.
    Patch zero = whole, negativeZero = whole;
    zero.graph.nodes[0].values["phase"] = 0.0f;
    negativeZero.graph.nodes[0].values["phase"] = -0.0f;
    CHECK(zero == negativeZero);
    // What == on a float sees oddly: a NaN compares unequal to itself, so a
    // record holding one is unequal to its own copy.
    Patch nan = whole;
    nan.graph.nodes[0].values["phase"] = std::numeric_limits<float>::quiet_NaN();
    const Patch nanCopy = nan;
    CHECK(!(nanCopy == nan));
}

namespace patch_table {
static std::vector<std::string> labels(const char* const* l) {
    std::vector<std::string> out;
    for (int i = 0; l && l[i]; ++i) out.emplace_back(l[i]);
    return out;
}
static std::string text(const char* s) { return s ? s : ""; }
// Every field of every descriptor of `fresh` against the entry `d`, in
// declaration order; the first difference is printed.
static bool same_declarations(const Component& fresh, const TypeDeclarations& d) {
    const auto differ = [&](const char* what, const char* name) {
        std::printf("  %s: %s '%s' differs from a fresh instance's\n", d.type.c_str(), what, name);
        return false;
    };
    const auto ps = fresh.param_descriptors();
    if (ps.size() != d.params.size()) return differ("the param count", "");
    for (size_t i = 0; i < ps.size(); ++i) {
        const auto& e = d.params[i];
        if (e.name != ps[i].name || e.defaultValue != ps[i].default_value || e.min != ps[i].min_value ||
            e.max != ps[i].max_value || e.hint != text(ps[i].hint)) return differ("param", ps[i].name);
    }
    const auto is = fresh.input_descriptors();
    if (is.size() != d.inputs.size()) return differ("the input count", "");
    for (size_t i = 0; i < is.size(); ++i) {
        const auto& e = d.inputs[i];
        if (e.name != is[i].name || e.list != is[i].multi || e.slot != is[i].slot ||
            e.hint != text(is[i].hint) || (!e.slot && !e.accepts.empty())) return differ("input", is[i].name);
    }
    const auto ss = fresh.setting_descriptors();
    if (ss.size() != d.settings.size()) return differ("the setting count", "");
    for (size_t i = 0; i < ss.size(); ++i) {
        const auto& e = d.settings[i];
        const std::string defaultText = ss[i].type == SettingType::String ? fresh.get_string(ss[i].name) : "";
        if (e.name != ss[i].name || e.type != ss[i].type || e.defaultValue != ss[i].default_value ||
            e.min != ss[i].min_value || e.max != ss[i].max_value || e.defaultText != defaultText ||
            e.labels != labels(ss[i].enum_labels) || e.structural != ss[i].structural)
            return differ("setting", ss[i].name);
    }
    const auto as = fresh.array_descriptors();
    if (as.size() != d.arrays.size()) return differ("the array count", "");
    for (size_t i = 0; i < as.size(); ++i) {
        const auto& e = d.arrays[i];
        if (e.name != as[i].name || e.group != text(as[i].groupName) || e.rowDefault != as[i].default_value ||
            e.min != as[i].min_value || e.max != as[i].max_value || e.labels != labels(as[i].enum_labels))
            return differ("array", as[i].name);
    }
    return true;
}
// A probe registered only after the table has been asked.
struct LateProbe final : Generator {
    Param level{this, "level", 0.5f, 0.0f, 1.0f};
    float compute() override { return level.current(); }
    const char* type_name() const override { return "LateDeclarationProbe"; }
};
}  // namespace patch_table

// The table's first ask, its pointers' lifetime, and an unknown name. The
// whole table's build time is printed.
static void run_type_declaration_identity_tests() {
    std::printf("the declarations table: read a type at a time, pointers stable, unknown names null\n");
    const auto start = std::chrono::steady_clock::now();
    const TypeDeclarations* sine = type_declarations("SineSource");
    const Component* sineProto = prototype("SineSource");
    CHECK(sine != nullptr);
    CHECK(sineProto != nullptr);
    const auto types = declared_types();
    for (const auto& t : types) CHECK(type_declarations(t) != nullptr);
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    std::printf("  %zu types declared; the table read whole in %.1f ms\n", types.size(), ms);
    CHECK(type_declarations("SineSource") == sine);
    CHECK(prototype("SineSource") == sineProto);
    CHECK(std::find(types.begin(), types.end(), "SineSource") != types.end());
    CHECK(sine && sine->type == "SineSource" && sine->signal);
    if (sine) {
        CHECK(sine->param("frequency") && sine->param("frequency")->name == "frequency");
        CHECK(sine->param("nope") == nullptr);
        CHECK(sine->input("nope") == nullptr && sine->setting("nope") == nullptr && sine->array("nope") == nullptr);
    }
    if (const auto* env = type_declarations("Envelope")) {
        CHECK(env->setting("timeMode") && env->setting("timeMode")->labels ==
                                              std::vector<std::string>({"fraction", "seconds"}));
        CHECK(env->array("type") && env->array("type")->group == "stages" && env->array("type")->labels.size() == 5);
        CHECK(env->input("trigger") == nullptr && env->param("trigger") != nullptr);
    }
    if (const auto* gate = type_declarations("NameGate"))
        CHECK(gate->setting("name") && gate->setting("name")->type == SettingType::String &&
              gate->setting("name")->structural && gate->input("in") != nullptr);
    CHECK(type_declarations("NoSuchSource") == nullptr);
    CHECK(prototype("NoSuchSource") == nullptr);
    const auto again = declared_types();
    CHECK(std::find(again.begin(), again.end(), "NoSuchSource") == again.end());
    CHECK(again == types);
}

// Every field of every descriptor of a fresh instance made through the
// registry, the string settings' defaults, and whether the type is a signal.
static void run_type_declaration_field_tests() {
    using namespace patch_table;
    std::printf("the declarations table: every descriptor field, as a fresh instance says\n");
    const auto& reg = SourceRegistry::instance();
    int types = 0, strings = 0;
    for (const auto& t : declared_types()) {
        const TypeDeclarations* d = type_declarations(t);
        CHECK(d != nullptr);
        if (!d) continue;
        ++types;
        const auto fresh = reg.create(t);
        CHECK(d->type == t);
        CHECK(same_declarations(*fresh, *d));
        const bool signal = dynamic_cast<ValueSource*>(fresh.get()) != nullptr;
        if (d->signal != signal) std::printf("  %s: signal is %d, the cast says %d\n", t.c_str(), d->signal, signal);
        CHECK(d->signal == signal);
        for (const auto& s : d->settings) strings += s.type == SettingType::String;
    }
    std::printf("  %d types compared; %d string settings among them\n", types, strings);
    const auto* gate = type_declarations("NameGate");
    CHECK(gate && gate->setting("name") && gate->setting("name")->defaultText == "");
    const auto* mixer = type_declarations("StereoMixer");
    CHECK(mixer && !mixer->signal);
    const auto* channel = type_declarations("SoundChannel");
    CHECK(channel && !channel->signal);
}

// A slot's accepted types against the door: for every slot of every type and
// every registered type, listed exactly when a fresh owner's set_param takes
// a fresh instance; and three slots by name.
static void run_type_declaration_accept_tests() {
    std::printf("the declarations table: a slot accepts what the door takes\n");
    const auto& reg = SourceRegistry::instance();
    const auto types = declared_types();
    int slots = 0, probes = 0, disagreements = 0;
    for (const auto& owner : types) {
        const TypeDeclarations* d = type_declarations(owner);
        if (!d) continue;
        for (const auto& in : d->inputs) {
            if (!in.slot) continue;
            ++slots;
            for (const auto& candidate : types) {
                ++probes;
                const bool door = reg.create(owner)->set_param(in.name, reg.create(candidate));
                const bool listed = std::find(in.accepts.begin(), in.accepts.end(), candidate) != in.accepts.end();
                if (door == listed) continue;
                ++disagreements;
                std::printf("  %s.%s and %s: the door says %d, the table %d\n",
                            owner.c_str(), in.name.c_str(), candidate.c_str(), door, listed);
            }
        }
    }
    std::printf("  %d slots, %d probes\n", slots, probes);
    CHECK(slots > 0);
    CHECK(disagreements == 0);
    const auto accepts = [](const char* type, const char* slot) {
        const auto* d = type_declarations(type);
        const auto* in = d ? d->input(slot) : nullptr;
        return in ? in->accepts : std::vector<std::string>{"(no such slot)"};
    };
    CHECK(accepts("StereoMixer", "channels") == std::vector<std::string>({"SoundChannel"}));
    CHECK(accepts("AdditiveSource", "partials") ==
          std::vector<std::string>({"FullPartials", "SequencePartials", "ExplicitPartials", "CompositePartials"}));
    CHECK(accepts("WavetableSource", "evolution") ==
          std::vector<std::string>({"EKSEvolution", "PluckEvolution", "AveragingEvolution", "BowedStringEvolution",
                                    "ReedEvolution", "BrassEvolution", "ReactionDiffusionEvolution",
                                    "SortErosionEvolution", "CellularAutomatonEvolution",
                                    "HistogramEqualizeEvolution", "BezierPullEvolution", "BitRotateEvolution",
                                    "TargetEvolution"}));
    const auto* sine = type_declarations("SineSource");
    CHECK(sine && sine->input("frequency") == nullptr && sine->param("frequency") != nullptr);
}

// A fresh instance holds its declared defaults: every Param's constant and
// every scalar setting's stored value. Envelope.sustainLevel is the one
// exception: its get_setting answers from the stage table.
static void run_type_declaration_default_tests() {
    std::printf("the declarations table: a fresh instance holds its declared defaults\n");
    const auto& reg = SourceRegistry::instance();
    int differing = 0, compared = 0;
    for (const auto& t : declared_types()) {
        const TypeDeclarations* d = type_declarations(t);
        if (!d) continue;
        const auto fresh = reg.create(t);
        for (const auto& p : d->params) {
            ++compared;
            const Param* pin = fresh->param(p.name);
            if (pin && pin->constant == p.defaultValue) continue;
            ++differing;
            std::printf("  %s.%s: the constant is %g, declared %g\n", t.c_str(), p.name.c_str(),
                        pin ? pin->constant : -1.0f, p.defaultValue);
        }
        for (const auto& s : d->settings) {
            if (s.type == SettingType::String) continue;
            const float got = fresh->get_setting(s.name);
            if (t == "Envelope" && s.name == "sustainLevel") {
                std::printf("  Envelope.sustainLevel (the view): reads %g, declared %g\n", got, s.defaultValue);
                continue;
            }
            ++compared;
            if (got == s.defaultValue) continue;
            ++differing;
            std::printf("  %s.%s: holds %g, declared %g\n", t.c_str(), s.name.c_str(), got, s.defaultValue);
        }
    }
    std::printf("  %d values compared, %d differ\n", compared, differing);
    CHECK(compared > 0);
    CHECK(differing == 0);
}

// The table is read from each type's prototype and asks slot acceptance of a
// scratch owner, so no slot of any type's prototype holds a part.
static void run_type_declaration_prototype_tests() {
    std::printf("the declarations table: no slot of any prototype holds a part\n");
    int slots = 0, holding = 0;
    for (const auto& t : declared_types()) {
        const TypeDeclarations* d = type_declarations(t);
        const Component* proto = prototype(t);
        if (!d || !proto) continue;
        for (const auto& in : d->inputs) {
            if (!in.slot) continue;
            ++slots;
            const auto part = proto->get_part(in.name);
            if (!part) continue;
            ++holding;
            std::printf("  %s.%s holds a %s\n", t.c_str(), in.name.c_str(), part->type_name());
        }
    }
    std::printf("  %d slots over every type's prototype, %d hold a part\n", slots, holding);
    CHECK(slots > 0);
    CHECK(holding == 0);
}

// A name the registry does not have caches nothing: registered after it was
// asked, and after the table was first read, it is found.
static void run_type_declaration_late_tests() {
    using namespace patch_table;
    std::printf("the declarations table: a type registered after the first ask is found\n");
    auto& reg = SourceRegistry::instance();
    CHECK(type_declarations("SineSource") != nullptr);
    if (!reg.has("LateDeclarationProbe")) {
        CHECK(type_declarations("LateDeclarationProbe") == nullptr);
        CHECK(prototype("LateDeclarationProbe") == nullptr);
        reg.register_type("LateDeclarationProbe", {.family = Family::Utility, .shelf = Shelf::Experimental},
                          [](auto) { return std::make_shared<LateProbe>(); });
    }
    const auto* late = type_declarations("LateDeclarationProbe");
    CHECK(late && late->signal && late->params.size() == 1 && late->param("level") &&
          late->param("level")->defaultValue == 0.5f);
    CHECK(prototype("LateDeclarationProbe") != nullptr);
    const auto types = declared_types();
    CHECK(!types.empty() && types.back() == "LateDeclarationProbe");
}

namespace patch_check {
static Node node(const char* id, const char* type) {
    Node n;
    n.id = id;
    n.type = type;
    return n;
}
static Node& at(Patch& p, const char* id) {
    for (auto& n : p.graph.nodes)
        if (n.id == id) return n;
    std::printf("  no node '%s' in the case's patch\n", id);
    std::abort();
}
// A three-node instrument: a note's frequency and an Envelope into a sine,
// the Envelope's timeScale pushed from the frequency per note, with a
// position per node, a group and a note face.
static Patch instrument() {
    Patch p;
    Node freq = node("freq", "PerformNode");
    freq.settings["field"] = 0.0f;
    Node env = node("env", "Envelope");
    env.values["maxValue"] = 0.8f;
    env.settings["timeMode"] = 1.0f;
    env.dynamicPins["timeScale"] = "freq";
    Node osc = node("osc", "SineSource");
    osc.wires["frequency"] = {Wire{"freq"}};
    osc.wires["amplitude"] = {Wire{"env"}};
    p.graph.nodes = {freq, env, osc};
    p.graph.output = "osc";
    p.instrument = InstrumentBlock{};
    p.layout.positions = {{"freq", Vec2{0, 0}}, {"env", Vec2{0, 100}}, {"osc", Vec2{200, 50}}};
    p.layout.groups = {Group{"Voice", {"osc", "env", "Note"}, Vec2{100, 50}}};
    p.layout.faces = {NoteFace{"Note", {{"frequency", "freq"}}, Vec2{-100, 0}}};
    return p;
}
// A two-channel plain sound: a sine and a saw, each on a channel of the mixer.
static Patch plain_sound() {
    Patch p;
    Node ch1 = node("ch1", "SoundChannel");
    ch1.wires["source"] = {Wire{"a"}};
    Node ch2 = node("ch2", "SoundChannel");
    ch2.wires["source"] = {Wire{"b"}};
    ch2.values["pan"] = -0.5f;
    Node mix = node("mix", "StereoMixer");
    mix.wires["channels"] = {Wire{"ch1"}, Wire{"ch2"}};
    p.graph.nodes = {node("a", "SineSource"), node("b", "SawSource"), ch1, ch2, mix};
    p.graph.output = "mix";
    p.seconds = 1.0;
    return p;
}
// One break: the patch it starts from, the change, and the one line check() gives.
struct Break { const char* name; bool plain; void (*change)(Patch&); const char* line; };
static void run_breaks(const std::vector<Break>& breaks) {
    for (const auto& b : breaks) {
        Patch p = b.plain ? plain_sound() : instrument();
        b.change(p);
        const auto got = p.check();
        const std::vector<std::string> want = *b.line ? std::vector<std::string>{b.line} : std::vector<std::string>{};
        if (got != want) {
            std::printf("  %s: check() gave %zu lines\n", b.name, got.size());
            for (const auto& l : got) std::printf("    %s\n", l.c_str());
        }
        CHECK(got == want);
    }
}
}  // namespace patch_check

namespace patch_check {
constexpr const char* kShared = "; ids, group names and face labels share one namespace";
static std::string shared(const char* head) { return std::string(head) + kShared; }
// Every stage column of `env` at two rows, the second stage of type `type`.
static void stages(Node& env, float type) {
    for (const char* column : {"startVal", "endVal", "power", "percent", "minSec", "maxSec", "nominal"})
        env.arrays[column] = {0.0f, 1.0f};
    env.arrays["type"] = {0.0f, type};
}
// Patches that hold, and the ids, the namespace and the output.
static std::vector<Break> name_breaks() {
    static const std::string twoNodes = shared("graph.nodes[3].id: 'osc' names two things: a node, a node");
    static const std::string groupNode = shared("layout.groups[1].name: 'env' names two things: a node, a group");
    static const std::string faceGroup = shared("layout.faces[1].label: 'Voice' names two things: a group, a face");
    static const std::string threeOnce = shared("layout.groups[1].name: 'osc' names two things: a node, a group");
    return {
        {"the instrument holds", false, [](Patch&) {}, ""},
        {"the plain sound holds", true, [](Patch&) {}, ""},
        {"a tap closes a loop", false, [](Patch& p) { at(p, "env").wires["minValue"] = {Wire{"osc", true}}; }, ""},
        {"an empty output", false, [](Patch& p) { p.graph.output.clear(); }, ""},
        {"a member naming a group", false, [](Patch& p) { p.layout.groups.push_back(Group{"Outer", {"Voice"}}); }, ""},
        {"a full stage table", false, [](Patch& p) { stages(at(p, "env"), 4.0f); }, ""},
        {"an empty id", false, [](Patch& p) { p.graph.nodes.push_back(node("", "SineSource")); },
         "graph.nodes[3].id: an id is empty"},
        {"two nodes under one id", false, [](Patch& p) { p.graph.nodes.push_back(node("osc", "SawSource")); },
         twoNodes.c_str()},
        {"a group named as a node", false, [](Patch& p) { p.layout.groups.push_back(Group{"env"}); },
         groupNode.c_str()},
        {"a face labelled as a group", false, [](Patch& p) { p.layout.faces.push_back(NoteFace{"Voice"}); },
         faceGroup.c_str()},
        {"three things under one name, once", false,
         [](Patch& p) { p.layout.groups.push_back(Group{"osc"}); p.layout.faces.push_back(NoteFace{"osc"}); },
         threeOnce.c_str()},
        {"the output naming no node", false, [](Patch& p) { p.graph.output = "nope"; },
         "graph.output: 'nope' names no node"},
        {"the output a part", false,
         [](Patch& p) {
             Node ch = node("ch", "SoundChannel");
             ch.wires["source"] = {Wire{"osc"}};
             p.graph.nodes.push_back(ch);
             p.graph.output = "ch";
         },
         "graph.output: 'ch' is a SoundChannel, a part, not a signal"},
    };
}
// A node's type, the names of its maps, and the shape of its pins.
static std::vector<Break> declaration_breaks() {
    return {
        {"an unknown type", false, [](Patch& p) { p.graph.nodes.push_back(node("x", "NoSuchSource")); },
         "graph.nodes[3].type: node 'x' (NoSuchSource): Unknown node type: NoSuchSource"},
        {"a retired type", false, [](Patch& p) { p.graph.nodes.push_back(node("x", "AREnvelope")); },
         "graph.nodes[3].type: node 'x' (AREnvelope): Unknown node type: AREnvelope; "
         "python tools/convert_patch.py <patch> rewrites a type this engine no longer has"},
        {"an unknown type checked no further", false,
         [](Patch& p) {
             Node x = node("x", "NoSuchSource");
             x.values["frequncy"] = 1.0f;
             x.wires["in"] = {Wire{"ghost"}};
             p.graph.nodes.push_back(x);
         },
         "graph.nodes[3].type: node 'x' (NoSuchSource): Unknown node type: NoSuchSource"},
        {"a number under an undeclared name", false, [](Patch& p) { at(p, "osc").values["frequncy"] = 1.0f; },
         "graph.nodes[2].params.frequncy: node 'osc' (SineSource): 'frequncy' is not a pin of SineSource"},
        {"a feed under an undeclared name", false, [](Patch& p) { at(p, "osc").wires["source"] = {Wire{"env"}}; },
         "graph.nodes[2].params.source: node 'osc' (SineSource): 'source' is not a pin of SineSource"},
        {"a setting under a pin's name", false, [](Patch& p) { at(p, "env").settings["maxValue"] = 1.0f; },
         "graph.nodes[1].params.maxValue: node 'env' (Envelope): 'maxValue' is not a setting of Envelope"},
        {"a number under a string setting's name", false,
         [](Patch& p) { Node g = node("g", "NameGate"); g.settings["name"] = 1.0f; p.graph.nodes.push_back(g); },
         "graph.nodes[3].params.name: node 'g' (NameGate): 'name' is not a setting of NameGate"},
        {"a string under a float setting's name", false, [](Patch& p) { at(p, "env").strings["timeScale"] = "fast"; },
         "graph.nodes[1].params.timeScale: node 'env' (Envelope): 'timeScale' is not a string setting of Envelope"},
        {"an array under an undeclared name", false, [](Patch& p) { at(p, "osc").arrays["startVal"] = {0.0f}; },
         "graph.nodes[2].params.startVal: node 'osc' (SineSource): 'startVal' is not an array of SineSource"},
        {"a number and a feed on one pin", false, [](Patch& p) { at(p, "osc").values["frequency"] = 440.0f; },
         "graph.nodes[2].params.frequency: node 'osc' (SineSource): 'frequency' holds a number and a feed: a pin takes one"},
        {"an empty feed list", false, [](Patch& p) { at(p, "env").wires["minValue"] = {}; },
         "graph.nodes[1].params.minValue: node 'env' (Envelope): 'minValue' holds an empty feed list: "
         "a pin with no feed has no entry"},
        {"two feeds on a single pin", false, [](Patch& p) { at(p, "osc").wires["amplitude"].push_back(Wire{"freq"}); },
         "graph.nodes[2].params.amplitude: node 'osc' (SineSource): 'amplitude' takes one feed, and holds 2"},
        {"a number on a slot", false,
         [](Patch& p) { Node w = node("wt", "WavetableSource"); w.values["evolution"] = 1.0f; p.graph.nodes.push_back(w); },
         "graph.nodes[3].params.evolution: node 'wt' (WavetableSource): 'evolution' is a slot: it takes a part, not a number"},
        {"a tap on a slot", true, [](Patch& p) { at(p, "mix").wires["channels"][1].tap = true; },
         "graph.nodes[4].params.channels: node 'mix' (StereoMixer): 'channels' is a slot: it takes a part by ref, "
         "not through a tap"},
    };
}
// Feeds, settings and columns against the declarations.
static std::vector<Break> value_breaks() {
    return {
        {"a feed naming no node", false, [](Patch& p) { at(p, "osc").wires["phase"] = {Wire{"ghost"}}; },
         "graph.nodes[2].params.phase: node 'osc' (SineSource): 'phase' reads 'ghost', which names no node"},
        {"a tap naming no node", false, [](Patch& p) { at(p, "env").wires["minValue"] = {Wire{"ghost", true}}; },
         "graph.nodes[1].params.minValue: node 'env' (Envelope): 'minValue' reads 'ghost', which names no node"},
        {"a signal on a slot", true, [](Patch& p) { at(p, "mix").wires["channels"].push_back(Wire{"a"}); },
         "graph.nodes[4].params.channels: node 'mix' (StereoMixer): 'channels' does not accept a SineSource; "
         "'channels' takes a SoundChannel"},
        {"a wrong part on a slot", true,
         [](Patch& p) { p.graph.nodes.push_back(node("fp", "FullPartials")); at(p, "mix").wires["channels"].push_back(Wire{"fp"}); },
         "graph.nodes[4].params.channels: node 'mix' (StereoMixer): 'channels' does not accept a FullPartials, "
         "a part, not a signal; 'channels' takes a SoundChannel"},
        {"a part on a Param", false,
         [](Patch& p) { p.graph.nodes.push_back(node("fp", "FullPartials")); at(p, "osc").wires["phase"] = {Wire{"fp"}}; },
         "graph.nodes[2].params.phase: node 'osc' (SineSource): 'phase' does not accept a FullPartials, a part, not a signal"},
        {"a part on an Input", true, [](Patch& p) { at(p, "ch1").wires["source"] = {Wire{"ch2"}}; },
         "graph.nodes[2].params.source: node 'ch1' (SoundChannel): 'source' does not accept a SoundChannel, "
         "a part, not a signal"},
        {"a float setting above its range", false, [](Patch& p) { at(p, "env").settings["timeScale"] = 20.0f; },
         "graph.nodes[1].params.timeScale: node 'env' (Envelope): 'timeScale' is 20, outside its range 0.01 to 10"},
        {"a float setting below its range", false, [](Patch& p) { at(p, "env").settings["stage_accuracy"] = -0.5f; },
         "graph.nodes[1].params.stage_accuracy: node 'env' (Envelope): 'stage_accuracy' is -0.5, outside its range 0 to 1"},
        {"an int setting above its range", false,
         [](Patch& p) { Node e = node("ep", "ExplicitPartials"); e.settings["maxPartials"] = 201.0f; p.graph.nodes.push_back(e); },
         "graph.nodes[3].params.maxPartials: node 'ep' (ExplicitPartials): 'maxPartials' is 201, outside its range 1 to 200"},
        {"an int setting holding a fraction", false,
         [](Patch& p) { Node e = node("ep", "ExplicitPartials"); e.settings["maxPartials"] = 2.5f; p.graph.nodes.push_back(e); },
         "graph.nodes[3].params.maxPartials: node 'ep' (ExplicitPartials): 'maxPartials' is 2.5: an int setting holds "
         "a whole number"},
        {"a bool setting holding 2", false,
         [](Patch& p) { Node e = node("ep", "ExplicitPartials"); e.settings["evolve"] = 2.0f; p.graph.nodes.push_back(e); },
         "graph.nodes[3].params.evolve: node 'ep' (ExplicitPartials): 'evolve' is 2: a bool setting holds 0 or 1"},
        {"an enum outside its labels", false, [](Patch& p) { at(p, "env").settings["timeMode"] = 2.0f; },
         "graph.nodes[1].params.timeMode: node 'env' (Envelope): 'timeMode' is 2, which is not an index into its "
         "labels (fraction, seconds)"},
        {"a labelled column outside its labels", false, [](Patch& p) { stages(at(p, "env"), 7.0f); },
         "graph.nodes[1].params.type: node 'env' (Envelope): 'type' row 1 is 7, which is not an index into its "
         "labels (Linear, Expo, InverseExpo, Sine, Hold)"},
        {"a ragged table", false, [](Patch& p) { at(p, "env").arrays["startVal"] = {0.0f, 1.0f}; },
         "graph.nodes[1].params: node 'env' (Envelope): table 'stages' takes its columns at one length, all present "
         "or all absent (got startVal 2, endVal absent, power absent, type absent, percent absent, minSec absent, "
         "maxSec absent, nominal absent)"},
    };
}
// Dynamic pins, cycles, the plain sound and the layout.
static std::vector<Break> graph_breaks() {
    return {
        {"a dynamic pin on a pin", false, [](Patch& p) { at(p, "env").dynamicPins["trigger"] = "freq"; },
         "graph.nodes[1].dynamicPins.trigger: node 'env' (Envelope): 'trigger' is not a setting on Envelope "
         "(fixed pins belong in \"params\")"},
        {"a dynamic pin on a structural setting", false, [](Patch& p) { at(p, "env").dynamicPins["timeMode"] = "freq"; },
         "graph.nodes[1].dynamicPins.timeMode: node 'env' (Envelope): 'timeMode' is a structural setting on Envelope "
         "(it sizes or reshapes the node at prepare), so it takes no pin"},
        {"a dynamic pin naming no node", false, [](Patch& p) { at(p, "env").dynamicPins["timeScale"] = "ghost"; },
         "graph.nodes[1].dynamicPins.timeScale: node 'env' (Envelope): 'timeScale' reads 'ghost', which names no node"},
        {"a dynamic pin read from a part", false,
         [](Patch& p) { p.graph.nodes.push_back(node("fp", "FullPartials")); at(p, "env").dynamicPins["timeScale"] = "fp"; },
         "graph.nodes[1].dynamicPins.timeScale: node 'env' (Envelope): 'timeScale' is a FullPartials, a part, not a signal"},
        {"a cycle of two", false, [](Patch& p) { at(p, "env").wires["minValue"] = {Wire{"osc"}}; },
         "graph: a cycle with no tap in it: env -> osc -> env"},
        {"a node that reads itself", false, [](Patch& p) { at(p, "osc").wires["phase"] = {Wire{"osc"}}; },
         "graph: a cycle with no tap in it: osc -> osc"},
        {"a cycle through a dynamic pin", false, [](Patch& p) { at(p, "env").dynamicPins["timeScale"] = "osc"; },
         "graph: a cycle with no tap in it: env -> osc -> env"},
        {"a PerformNode in a plain sound", true, [](Patch& p) { p.graph.nodes.push_back(node("pf", "PerformNode")); },
         "graph.nodes[5].type: node 'pf' (PerformNode): a plain sound has no note to read"},
        {"a dynamic pin in a plain sound", true,
         [](Patch& p) { Node e = node("env", "Envelope"); e.dynamicPins["timeScale"] = "a"; p.graph.nodes.push_back(e); },
         "graph.nodes[5].dynamicPins.timeScale: node 'env' (Envelope): a plain sound has no note, so nothing drives "
         "a dynamic pin"},
        {"a position naming no node", false, [](Patch& p) { p.layout.positions["ghost"] = Vec2{}; },
         "layout.positions.ghost: 'ghost' names no node"},
        {"a member naming nothing", false, [](Patch& p) { p.layout.groups[0].members.push_back("ghost"); },
         "layout.groups[0].members[3]: 'ghost' names no node, group or face"},
        {"a face field naming no node", false, [](Patch& p) { p.layout.faces[0].fields["velocity"] = "ghost"; },
         "layout.faces[0].fields.velocity: 'ghost' names no node"},
    };
}
}  // namespace patch_check

// Patch::check() against the eight invariants a patch keeps, each case one
// minimal break of a valid patch giving exactly its one line, path and all:
//  ids non-empty and unique, one namespace with group names and face labels:
//    "an empty id", "two nodes under one id", "a group named as a node",
//    "a face labelled as a group", "three things under one name, once";
//  the output empty or a node that is a signal or the mixer part:
//    "an empty output", "the output naming no node", "the output a part",
//    "the plain sound holds";
//  every feed, tap, dynamic pin's source, member, face field and position
//    naming a node (a member also a group or a face): "a feed naming no
//    node", "a tap naming no node", "a dynamic pin naming no node", "a
//    position naming no node", "a member naming nothing", "a face field
//    naming no node", "a member naming a group";
//  a pin a number or feeds, one feed unless a list, no tap on a slot: "a
//    number and a feed on one pin", "an empty feed list", "two feeds on a
//    single pin", "a number on a slot", "a tap on a slot";
//  every name declared for its kind, the type registered, a dynamic pin on a
//    setting that is not structural, a slot's part of an accepted type (and a
//    signal pin's feed a signal): "an unknown type", "a retired type", "an
//    unknown type checked no further", the five "... under ... name" cases,
//    "a dynamic pin on a pin", "a dynamic pin on a structural setting", "a
//    dynamic pin read from a part", "a signal on a slot", "a wrong part on a
//    slot", "a part on a Param", "a part on an Input";
//  a table's columns at one length, a setting inside its declaration: "a
//    ragged table", "a full stage table", the four range, fraction and bool
//    cases, "an enum outside its labels", "a labelled column outside its
//    labels";
//  no cycle without a tap: "a cycle of two", "a node that reads itself", "a
//    cycle through a dynamic pin", "a tap closes a loop";
//  a plain sound with no PerformNode and no dynamic pin: "a PerformNode in a
//    plain sound", "a dynamic pin in a plain sound".
static void run_patch_check_tests() {
    using namespace patch_check;
    std::printf("Patch::check(): each invariant broken once, each line with its path\n");
    run_breaks(name_breaks());
    run_breaks(declaration_breaks());
    run_breaks(value_breaks());
    run_breaks(graph_breaks());
    // Three unrelated breaks, made in the reverse of check()'s order, come
    // back in check()'s order: the output, the node, the layout.
    Patch p = instrument();
    p.layout.positions["ghost"] = Vec2{};
    at(p, "osc").values["frequncy"] = 1.0f;
    p.graph.output = "nope";
    const std::vector<std::string> want = {
        "graph.output: 'nope' names no node",
        "graph.nodes[2].params.frequncy: node 'osc' (SineSource): 'frequncy' is not a pin of SineSource",
        "layout.positions.ghost: 'ghost' names no node"};
    const auto got = p.check();
    if (got != want)
        for (const auto& l : got) std::printf("  three breaks gave: %s\n", l.c_str());
    CHECK(got == want);
    // check() reads the record and builds nothing: the patch is as it was.
    const Patch before = instrument();
    Patch after = before;
    (void)after.check();
    CHECK(after == before);
}

// ---------------------------------------------------------------------------
// The codec: patch_json's one reader and one writer of a patch file.
#include "mforce/patch/patch_json.h"
#include <random>

namespace patch_codec {
using J = nlohmann::json;
// A patch read from JSON text through the codec; throws as from_json does.
static Patch load_text(const std::string& text) { return patch_door::read_text(text); }
// The refusal reading `text` gives, whole; empty when it reads.
static std::string refusal(const std::string& text) {
    try { (void)load_text(text); } catch (const std::exception& e) { return e.what(); }
    return {};
}
// A node read on its own.
static Node read_node(const std::string& text) {
    Node n;
    from_json(J::parse(text), n);
    return n;
}
// An instrument of two nodes: a sine 'o' (the output) and `node`, graph.nodes[1].
static std::string with_node(const std::string& node) {
    return R"J({"instrument":{},"graph":{"output":"o","nodes":[{"id":"o","type":"SineSource"},)J" + node + "]}}";
}
// `params` as the bag of a node 'id' of `type`, as with_node's graph.nodes[1].
static std::string with_params(const char* id, const char* type, const std::string& params) {
    return with_node(std::string(R"J({"id":")J") + id + R"J(","type":")J" + type + R"J(","params":)J" + params + "}");
}
// Every stage column of an Envelope at two rows, with `type` as the type column.
static std::string stage_columns(const std::string& type) {
    return R"J("startVal":[0,1],"endVal":[1,0],"power":[0,0],"percent":[0.2,0],"minSec":[0,0],"maxSec":[1,0],"nominal":[0,0],"type":)J" + type;
}
static std::string text_of(const std::filesystem::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}
// Runs one family and prints its check count; a throw it lets out fails the
// family by name, where an uncaught throw would end the whole run with
// nothing printed.
static void guarded(const char* family, void (*run)()) {
    const int before = g_checks;
    try { run(); } catch (const std::exception& e) { std::printf("  %s threw: %s\n", family, e.what()); CHECK(false); }
    std::printf("  %s: %d checks\n", family, g_checks - before);
}
// One refusal case: the text read and the whole message it must give.
struct Refused { const char* name; std::string text; std::string want; };
static void run_refusals(const std::vector<Refused>& cases) {
    for (const auto& c : cases) {
        const std::string got = refusal(c.text);
        if (got != c.want) std::printf("  %s:\n    got  %s\n    want %s\n", c.name, got.c_str(), c.want.c_str());
        CHECK(got == c.want);
    }
}
}  // namespace patch_codec

// Every patch of patches/library and patches/baselines, walked as it stands:
// load_patch reads it, a write and a read give the same record, the saved
// text ends in one newline, and the text of a save read and saved again is
// byte-equal.
static void run_codec_identity_tests() {
    using namespace patch_codec;
    std::printf("the codec: every patch of library and baselines reads, writes and reads back the same\n");
    const std::string scratch = (std::filesystem::temp_directory_path() / "mforce_codec_identity.json").string();
    int files = 0, refused = 0, unequal = 0, resaved = 0;
    for (const char* dir : {"patches/library", "patches/baselines"}) {
        std::vector<std::filesystem::path> paths;
        for (const auto& e : std::filesystem::recursive_directory_iterator(dir))
            if (e.is_regular_file() && e.path().extension() == ".json") paths.push_back(e.path());
        std::sort(paths.begin(), paths.end());
        for (const auto& path : paths) {
            ++files;
            try {
                const Patch p = load_patch(path.string());
                Patch back;
                from_json(J(p), back);
                const bool same = back == p;
                if (!same) { ++unequal; std::printf("  a write and a read differ: %s\n", path.string().c_str()); }
                CHECK(same);
                save_patch(scratch, p);
                const std::string first = text_of(scratch);
                const bool oneNewline = first.size() >= 2 && first.back() == '\n' && first[first.size() - 2] != '\n';
                if (!oneNewline) std::printf("  the saved text does not end in one newline: %s\n", path.string().c_str());
                CHECK(oneNewline);
                const Patch again = load_patch(scratch);
                save_patch(scratch, again);
                const bool equal = text_of(scratch) == first && again == p;
                if (!equal) { ++resaved; std::printf("  a second save differs: %s\n", path.string().c_str()); }
                CHECK(equal);
            } catch (const std::exception& e) {
                ++refused;
                std::printf("  refused: %s\n", e.what());
                CHECK(false);
            }
        }
    }
    std::filesystem::remove(scratch);
    std::printf("  %d files: %d refused, %d unequal after a write and a read, %d whose second save differs\n",
                files, refused, unequal, resaved);
    CHECK(files > 0);
}

namespace patch_codec {
// A node of `type` read from `params` holds each declared Param in values at
// `param(p)`, each scalar setting at `setting(s)`, each string setting at
// `text(s)`, and nothing else.
template <class P, class S, class T>
static bool holds(const Node& n, const TypeDeclarations& d, P param, S setting, T text) {
    if (!n.wires.empty() || !n.arrays.empty() || n.seed || !n.dynamicPins.empty()) return false;
    size_t scalars = 0, strings = 0;
    if (n.values.size() != d.params.size()) return false;
    for (const auto& p : d.params) {
        auto it = n.values.find(p.name);
        if (it == n.values.end() || std::bit_cast<uint32_t>(it->second) != std::bit_cast<uint32_t>(param(p))) return false;
    }
    for (const auto& s : d.settings) {
        if (s.type == SettingType::String) {
            ++strings;
            auto it = n.strings.find(s.name);
            if (it == n.strings.end() || it->second != text(s)) return false;
        } else {
            ++scalars;
            auto it = n.settings.find(s.name);
            if (it == n.settings.end() || std::bit_cast<uint32_t>(it->second) != std::bit_cast<uint32_t>(setting(s))) return false;
        }
    }
    return n.settings.size() == scalars && n.strings.size() == strings;
}
// A bag giving every declared Param and setting of `d` a value off its default.
static J everything(const TypeDeclarations& d) {
    J params = J::object();
    for (const auto& p : d.params) params[p.name] = p.max;
    for (const auto& s : d.settings) {
        if (s.type == SettingType::String) params[s.name] = "given";
        else if (s.type == SettingType::Bool) params[s.name] = s.defaultValue == 0.0f;
        else if (!s.labels.empty()) params[s.name] = s.labels.back();
        else params[s.name] = s.max;
    }
    return params;
}
static float given_setting(const TypeDeclarations::Setting& s) {
    if (s.type == SettingType::Bool) return s.defaultValue == 0.0f ? 1.0f : 0.0f;
    if (!s.labels.empty()) return float(s.labels.size() - 1);
    return s.max;
}
}  // namespace patch_codec

// The model is complete in pins and settings and not in arrays: for every
// registered type but the Envelope (whose bag with no stage column is the
// preset form), a node given nothing holds every Param and setting at its
// declared default and no array; a node given everything holds what it was
// given, and writes and reads back the same.
static void run_codec_complete_model_tests() {
    using namespace patch_codec;
    std::printf("the codec: the model complete in pins and settings, not in arrays\n");
    int types = 0;
    for (const auto& t : declared_types()) {
        const TypeDeclarations* d = type_declarations(t);
        if (!d || t == "Envelope") continue;
        ++types;
        try {
            const Node bare = read_node(J{{"id", "n"}, {"type", t}}.dump());
            const bool defaults = holds(bare, *d, [](const auto& p) { return p.defaultValue; },
                                        [](const auto& s) { return s.defaultValue; },
                                        [](const auto& s) { return s.defaultText; });
            if (!defaults) std::printf("  %s given nothing does not hold its defaults\n", t.c_str());
            CHECK(defaults);
            const Node full = read_node(J{{"id", "n"}, {"type", t}, {"params", everything(*d)}}.dump());
            const bool given = holds(full, *d, [](const auto& p) { return p.max; },
                                     [](const auto& s) { return given_setting(s); },
                                     [](const auto&) { return std::string("given"); });
            if (!given) std::printf("  %s given everything does not hold what it was given\n", t.c_str());
            CHECK(given);
            Node back;
            from_json(J(full), back);
            CHECK(back == full);
        } catch (const std::exception& e) {
            std::printf("  %s: %s\n", t.c_str(), e.what());
            CHECK(false);
        }
    }
    std::printf("  %d types read given nothing and given everything\n", types);
    CHECK(types > 0);
}

// A float's text reads back bit for bit: hand-picked values on a Param and
// on a Float setting, then a hundred thousand random finite bit patterns
// from a fixed seed through an array.
static void run_codec_float_tests() {
    using namespace patch_codec;
    std::printf("the codec: a float written as its shortest text reads back bit for bit\n");
    const float picked[] = {0.1f, 0.6f, 1e-7f, 16777216.0f, -0.0f, 0.0f, 1.0f / 3.0f,
                            std::numeric_limits<float>::max(), std::numeric_limits<float>::lowest(),
                            std::numeric_limits<float>::min(), std::numeric_limits<float>::denorm_min(),
                            -std::numeric_limits<float>::denorm_min(),
                            // the two whose shortest text names a double that narrows to the next float
                            std::bit_cast<float>(uint32_t(0x15ae43fdu)), std::bit_cast<float>(uint32_t(0x95ae43fdu))};
    for (float v : picked) {
        Node n{"x", "Biquad"};
        n.values["frequency"] = v;
        n.settings["b0"] = v;
        const Node back = read_node(J(n).dump());
        const bool same = std::bit_cast<uint32_t>(back.values.at("frequency")) == std::bit_cast<uint32_t>(v) &&
                          std::bit_cast<uint32_t>(back.settings.at("b0")) == std::bit_cast<uint32_t>(v);
        if (!same) std::printf("  %.9g does not read back\n", v);
        CHECK(same);
    }
    Node short_text{"x", "SineSource"};
    short_text.values["phase"] = 0.6f;
    short_text.values["amplitude"] = 0.1f;
    const std::string text = J(short_text).dump();
    CHECK(text.find("\"phase\":0.6") != std::string::npos && text.find("\"amplitude\":0.1") != std::string::npos);
    std::mt19937 rng(46);
    int drawn = 0, differing = 0;
    while (drawn < 100000) {
        Node n{"s", "SegmentSource"};
        auto& rows = n.arrays["values"];
        while (rows.size() < 1000) {
            const float v = std::bit_cast<float>(uint32_t(rng()));
            if (std::isfinite(v)) rows.push_back(v);
        }
        drawn += int(rows.size());
        const Node back = read_node(J(n).dump());
        const auto& got = back.arrays.at("values");
        for (size_t i = 0; i < rows.size(); ++i)
            differing += std::bit_cast<uint32_t>(got[i]) != std::bit_cast<uint32_t>(rows[i]);
    }
    std::printf("  %d random finite floats written and read: %d differ\n", drawn, differing);
    CHECK(differing == 0);
}

// Each kind is written in its one form and read in its forms: an int as an
// integer, a bool as true or false (read also as 0 or 1), an enum and a
// labelled column as labels (read also as indices, never as a label in
// another case), a single pin's feed as one object and a list pin's as a
// list even when it holds one.
static void run_codec_kind_tests() {
    using namespace patch_codec;
    std::printf("the codec: ints, bools, enums, labelled columns and feeds, by kind\n");
    const Node f = read_node(R"J({"id":"f","type":"BWLowpassFilter","params":{"sections":3}})J");
    CHECK(f.settings.at("sections") == 3.0f);
    CHECK(J(f)["params"]["sections"].is_number_integer() && J(f)["params"]["sections"] == 3);
    const Node s = read_node(R"J({"id":"s","type":"SVFSource","params":{"mode":"Highpass","normalize":true}})J");
    CHECK(s.settings.at("mode") == 1.0f && s.settings.at("normalize") == 1.0f);
    CHECK(J(s)["params"]["mode"] == "Highpass" && J(s)["params"]["normalize"] == true);
    CHECK(read_node(R"J({"id":"s","type":"SVFSource","params":{"mode":1}})J").settings.at("mode") == 1.0f);
    CHECK(read_node(R"J({"id":"s","type":"SVFSource","params":{"mode":4.0}})J").settings.at("mode") == 4.0f);
    for (const char* b : {"0", "1", "false", "true"}) {
        const Node n = read_node(std::string(R"J({"id":"s","type":"SVFSource","params":{"normalize":)J") + b + "}}");
        const float want = (std::string(b) == "1" || std::string(b) == "true") ? 1.0f : 0.0f;
        CHECK(n.settings.at("normalize") == want);
        CHECK(J(n)["params"]["normalize"] == (want == 1.0f));
    }
    CHECK(refusal(with_params("s", "SVFSource", R"J({"mode":"highpass"})J")) ==
          "graph.nodes[1].params.mode: node 's' (SVFSource): 'mode' holds \"highpass\", which is not one of its "
          "labels (Lowpass, Highpass, Bandpass, Lowpass1P, Highpass1P) or an index into them");
    const Node e = read_node("{\"id\":\"e\",\"type\":\"Envelope\",\"params\":{" + stage_columns(R"J(["Linear",3]})J") + "}");
    CHECK(e.arrays.at("type") == std::vector<float>({0.0f, 3.0f}));
    CHECK(J(e)["params"]["type"] == J::parse(R"J(["Linear","Sine"])J"));
    const Node m = read_node(R"J({"id":"m","type":"MultiSource","params":{"source":{"ref":"o"}}})J");
    CHECK(m.wires.at("source") == std::vector<Wire>({Wire{"o", false}}));
    CHECK(J(m)["params"]["source"] == J::parse(R"J([{"ref":"o"}])J"));
    const Node two = read_node(R"J({"id":"m","type":"MultiSource","params":{"source":[{"ref":"a"},{"tap":"b"}]}})J");
    CHECK(two.wires.at("source") == std::vector<Wire>({Wire{"a", false}, Wire{"b", true}}));
    CHECK(read_node(R"J({"id":"m","type":"MultiSource","params":{"source":[]}})J").wires.empty());
    CHECK(read_node(R"J({"id":"m","type":"MultiSource","params":{"source":0.5}})J").values.at("source") == 0.5f);
    const Node one = read_node(R"J({"id":"x","type":"SineSource","params":{"amplitude":{"tap":"o"}}})J");
    CHECK(one.wires.at("amplitude") == std::vector<Wire>({Wire{"o", true}}));
    CHECK(J(one)["params"]["amplitude"] == J::parse(R"J({"tap":"o"})J"));
    CHECK(one.values.count("amplitude") == 0 && one.values.count("frequency") == 1);
    const Node mix = read_node(R"J({"id":"mx","type":"StereoMixer","params":{"channels":{"ref":"c"}}})J");
    CHECK(J(mix)["params"]["channels"] == J::parse(R"J([{"ref":"c"}])J"));
    const Node w = read_node(R"J({"id":"w","type":"WavetableSource","params":{"evolution":{"ref":"ev"}}})J");
    CHECK(J(w)["params"]["evolution"] == J::parse(R"J({"ref":"ev"})J"));
    // A name the type does not declare is written by the map it sits in.
    Node odd{"x", "SineSource"};
    odd.settings["level"] = 2.0f;
    odd.wires["source"] = {Wire{"o"}};
    odd.strings["name"] = "n";
    const J oddJson = J(odd);
    CHECK(oddJson["params"]["level"] == 2.0 && oddJson["params"]["source"] == J::parse(R"J([{"ref":"o"}])J") &&
          oddJson["params"]["name"] == "n");
    Node lost{"x", "NoSuchSource"};
    lost.values["a"] = 1.0f;
    lost.arrays["b"] = {1.0f};
    CHECK(J(lost)["params"] == J::parse(R"J({"a":1.0,"b":[1.0]})J"));
}

namespace patch_codec {
// The preset cases: each of the six bare and with every knob it reads,
// the adsr in seconds, an as that says seconds, and the load test's node.
static const char* const kPresetCases[] = {
    R"J({"preset":"ar"})J",
    R"J({"preset":"ar","attack":0.4,"attackCurve":2,"attackPower":1.2,"releaseCurve":0,"releasePower":3.0,"attackMin":0.1,"attackMax":0.9})J",
    R"J({"preset":"as"})J",
    R"J({"preset":"as","attack":0.35,"attackCurve":1,"attackPower":2.5,"reverse":true,"sustainLevel":0.6})J",
    R"J({"preset":"asr"})J",
    R"J({"preset":"asr","attack":0.15,"attackCurve":2,"attackPower":1.5,"release":0.3,"releaseCurve":2,"releasePower":0.5,"sustainLevel":0.4})J",
    R"J({"preset":"ads"})J",
    R"J({"preset":"ads","attack":0.1,"attackCurve":3,"decay":0.25,"decayCurve":1,"decayPower":2.0,"sustainLevel":0.3})J",
    R"J({"preset":"adr"})J",
    R"J({"preset":"adr","attack":0.1,"attackPower":0.7,"decay":0.2,"decayCurve":3,"decayLevel":0.55,"releaseCurve":1,"releasePower":1.5})J",
    R"J({"preset":"adsr"})J",
    R"J({"preset":"adsr","attack":0.12,"attackCurve":1,"attackPower":0.8,"decay":0.2,"decayCurve":2,"decayPower":1.1,"sustainLevel":0.5,"release":0.25,"releaseCurve":0,"releasePower":2.0,"attackMin":0.05,"attackMax":0.9,"decayMin":0.01,"decayMax":0.4,"releaseMin":0.0,"releaseMax":0.3})J",
    R"J({"preset":"adsr","timeMode":"seconds","attack":0.05,"decay":0.1,"sustainLevel":0.6,"release":0.2,"attackCurve":"Expo"})J",
    R"J({"preset":"as","timeMode":"seconds","attack":0.3})J",
    R"J({"preset":"as","attack":0.3,"attackCurve":1,"attackPower":2.0,"reverse":true,"sustainLevel":0.6,"stage_accuracy":0.9,"ramp_accuracy":0.95,"seed":77,"minValue":0.0,"maxValue":0.8})J",
};
static const char* const kStageColumnNames[] = {"startVal", "endVal", "power", "type", "percent", "minSec", "maxSec", "nominal"};
// The node read from `params` holds the eight columns envelope_from_preset_json
// lays out from the same params, and nothing more in arrays.
static bool preset_columns(const Node& n, const J& params) {
    const auto env = envelope_from_preset_json(params);
    if (n.arrays.size() != std::size(kStageColumnNames)) return false;
    for (const char* c : kStageColumnNames)
        if (!n.arrays.count(c) || n.arrays.at(c) != env->get_array(c)) return false;
    return true;
}
}  // namespace patch_codec

// The preset form: its eight columns are envelope_from_preset_json's, timeMode
// and sustainLevel are settings as the file gives them or at their defaults,
// a node with neither form is an ar, both forms and an unknown preset are
// refused naming the node.
static void run_codec_preset_tests() {
    using namespace patch_codec;
    std::printf("the codec: the Envelope's preset form read into its eight columns\n");
    for (const char* c : kPresetCases) {
        const J params = J::parse(c);
        const Node n = read_node(J{{"id", "e"}, {"type", "Envelope"}, {"params", params}}.dump());
        const bool columns = preset_columns(n, params);
        if (!columns) std::printf("  %s: the columns differ from the preset's\n", c);
        CHECK(columns);
        const float timeMode = params.value("timeMode", std::string("fraction")) == "seconds" ? 1.0f : 0.0f;
        CHECK(n.settings.at("timeMode") == timeMode);
        CHECK(n.settings.at("sustainLevel") == params.value("sustainLevel", 0.7f));
        CHECK(n.settings.at("stage_accuracy") == params.value("stage_accuracy", 1.0f));
        CHECK(n.seed == (params.contains("seed") ? std::optional<uint32_t>(77u) : std::optional<uint32_t>()));
        Node back;
        from_json(J(n), back);
        CHECK(back == n);
    }
    const Node bare = read_node(R"J({"id":"e","type":"Envelope"})J");
    const Node empty = read_node(R"J({"id":"e","type":"Envelope","params":{}})J");
    CHECK(preset_columns(bare, J::parse(R"J({"preset":"ar"})J")) && empty == bare);
    const auto ar = Envelope::make_ar(0.2f);
    for (const char* c : kStageColumnNames) CHECK(bare.arrays.at(c) == ar->get_array(c));
    CHECK(refusal(with_params("e", "Envelope", "{\"preset\":\"ar\"," + stage_columns("[0,0]") + "}")) ==
          "graph.nodes[1].params.preset: node 'e' (Envelope): holds both 'preset' and 'startVal'; an Envelope "
          "takes one form: the stage columns or a preset");
    CHECK(refusal(with_params("e", "Envelope", R"J({"preset":"adsx"})J")) ==
          "graph.nodes[1].params: node 'e' (Envelope): Unknown envelope preset: adsx");
    CHECK(refusal(with_params("e", "Envelope", R"J({"preset":"as","attackCurve":7})J")) ==
          "graph.nodes[1].params: node 'e' (Envelope): envelope preset: attackCurve is 7; a curve is 0 (Linear), "
          "1 (Expo), 2 (InverseExpo) or 3 (Sine)");
    CHECK(refusal(with_params("e", "Envelope", R"J({"preset":"ar","bogus":1})J")) ==
          "graph.nodes[1].params.bogus: node 'e' (Envelope): unknown key 'bogus'");
    CHECK(refusal(with_params("e", "Envelope", "{\"attack\":0.1," + stage_columns("[0,0]") + "}")) ==
          "graph.nodes[1].params.attack: node 'e' (Envelope): unknown key 'attack'");
}

// The passengers: the score's text is its block's dump(), [] kept apart from
// no score; a comment at both levels; a seed; seconds absent and present;
// sampleRate; an empty document an empty Patch.
static void run_codec_passenger_tests() {
    using namespace patch_codec;
    std::printf("the codec: the score, the comments, a seed, seconds and the sample rate\n");
    const std::string score = R"J([{"note":60,"velocity":0.8,"duration":0.5,"time":0.0,"articulation":"staccato"}])J";
    const std::string node = R"J({"id":"x","type":"SineSource","params":{"seed":4294967295},"comment":"the tone"})J";
    const std::string text = R"J({"sampleRate":22050,"seconds":2.5,"comment":"a patch","score":)J" + score +
                             R"J(,"instrument":{},"graph":{"output":"x","nodes":[)J" + node + "]}}";
    const Patch p = load_text(text);
    CHECK(p.score == J::parse(score).dump());
    CHECK(p.comment == "a patch" && p.graph.nodes.at(0).comment == "the tone");
    CHECK(p.graph.nodes.at(0).seed == std::optional<uint32_t>(4294967295u));
    CHECK(p.seconds == std::optional<double>(2.5) && p.sampleRate == 22050);
    const J written = J(p);
    CHECK(written["score"] == J::parse(score) && written["comment"] == "a patch" && written["seconds"] == 2.5);
    CHECK(written["graph"]["nodes"][0]["comment"] == "the tone");
    CHECK(written["graph"]["nodes"][0]["params"]["seed"].is_number_unsigned() &&
          written["graph"]["nodes"][0]["params"]["seed"] == 4294967295u);
    const Patch emptyScore = load_text(R"J({"score":[],"instrument":{},"graph":{"output":"x","nodes":[{"id":"x","type":"SineSource"}]}})J");
    const Patch noScore = load_text(R"J({"instrument":{},"graph":{"output":"x","nodes":[{"id":"x","type":"SineSource"}]}})J");
    CHECK(emptyScore.score == "[]" && noScore.score.empty() && !(emptyScore == noScore));
    CHECK(J(emptyScore)["score"] == J::array() && !J(noScore).contains("score"));
    CHECK(!noScore.seconds && noScore.sampleRate == 48000 && noScore.comment.empty());
    CHECK(!J(noScore).contains("seconds") && !J(noScore).contains("comment") && !J(noScore).contains("layout"));
    CHECK(!J(noScore)["graph"]["nodes"][0].contains("comment") && !J(noScore)["graph"]["nodes"][0]["params"].contains("seed"));
    CHECK(noScore.instrument == std::optional<InstrumentBlock>(InstrumentBlock{}));
    CHECK(J(noScore)["instrument"] == J::parse(R"J({"polyphony":4,"volume":1.0,"sustaining":false,"onsets":[],"glideMs":15.0})J"));
    const Patch block = load_text(R"J({"instrument":{"polyphony":2,"volume":0.5,"sustaining":true,"onsets":["tongue"],"glideMs":20},"graph":{"output":"x","nodes":[{"id":"x","type":"SineSource"}]}})J");
    CHECK(block.instrument == std::optional<InstrumentBlock>(InstrumentBlock{2, 0.5f, true, {"tongue"}, 20.0f}));
    CHECK(load_text("{}") == Patch{});
    CHECK(J(Patch{}) == J::parse(R"J({"sampleRate":48000,"graph":{"output":"","nodes":[]}})J"));
}

// The layout and the fragment: each read, written and read back the same; a
// fragment's feed may name a node it does not hold.
static void run_codec_layout_fragment_tests() {
    using namespace patch_codec;
    std::printf("the codec: the layout and the fragment\n");
    const std::string layout = R"J({"positions":{"x":[1.5,-2.0],"y":[0.0,3.25]},"output":[10.0,20.0],"panning":[-5.0,6.0],)J"
                               R"J("groups":[{"name":"Voice","members":["x","Note"],"pos":[7.0,8.0]}],)J"
                               R"J("faces":[{"label":"Note","fields":{"frequency":"x"},"pos":[-1.0,1.0]},{"label":"Bare","fields":{}}]})J";
    const Patch p = load_text(R"J({"instrument":{},"graph":{"output":"x","nodes":[{"id":"x","type":"SineSource"},{"id":"y","type":"SawSource"}]},"layout":)J" +
                              layout + "}");
    CHECK(p.layout.positions.at("x") == (Vec2{1.5f, -2.0f}) && p.layout.outputPos == std::optional<Vec2>(Vec2{10, 20}));
    CHECK(p.layout.panning == std::optional<Vec2>(Vec2{-5, 6}) && p.layout.groups.size() == 1 && p.layout.faces.size() == 2);
    CHECK(p.layout.faces[0].pos == std::optional<Vec2>(Vec2{-1, 1}) && !p.layout.faces[1].pos);
    CHECK(J(p)["layout"] == J::parse(layout));
    Patch back;
    from_json(J(p), back);
    CHECK(back == p);
    Layout l;
    from_json(J::parse(layout), l);
    CHECK(l == p.layout && J(l) == J::parse(layout));
    CHECK(J(Layout{}) == J::object());
    const std::string fragment = R"J({"nodes":[{"id":"f","type":"SVFSource","params":{"source":{"ref":"elsewhere"},"cutoffFreq":{"tap":"g"}}},)J"
                                 R"J({"id":"g","type":"SineSource"}],"positions":{"f":[1.0,2.0]},)J"
                                 R"J("groups":[{"name":"G","members":["f","g"],"pos":[0.0,0.0]}],"faces":[{"label":"N","fields":{"frequency":"g"}}]})J";
    Fragment fr;
    from_json(J::parse(fragment), fr);
    CHECK(fr.nodes.size() == 2 && fr.nodes[0].wires.at("source") == std::vector<Wire>({Wire{"elsewhere", false}}));
    CHECK(fr.positions.at("f") == (Vec2{1, 2}) && fr.groups.size() == 1 && fr.faces.size() == 1);
    Fragment frBack;
    from_json(J(fr), frBack);
    CHECK(frBack == fr);
    CHECK(J(Fragment{}) == J::parse(R"J({"nodes":[]})J"));
}

namespace patch_codec {
// An instrument whose top level, besides the graph of one sine, holds `extra`.
static std::string top(const std::string& extra) {
    return "{" + extra + R"J(,"instrument":{},"graph":{"output":"o","nodes":[{"id":"o","type":"SineSource"}]}})J";
}
// An instrument of one sine whose block is `block`.
static std::string block(const std::string& b) {
    return R"J({"instrument":)J" + b + R"J(,"graph":{"output":"o","nodes":[{"id":"o","type":"SineSource"}]}})J";
}
// An instrument of one sine whose layout is `layout`.
static std::string layout(const std::string& l) {
    return R"J({"instrument":{},"graph":{"output":"o","nodes":[{"id":"o","type":"SineSource"}]},"layout":)J" + l + "}";
}
static const std::string kFeedTakes = R"J({"ref": id} or {"tap": id})J";
// The document's levels: the top, the block, the graph and the layout.
static std::vector<Refused> document_refusals() {
    return {
        {"a top-level frames", top(R"J("frames":96000)J"), "frames: unknown key 'frames'"},
        {"a stray key at the top", top(R"J("author":"M")J"), "author: unknown key 'author'"},
        {"sampleRate with a fraction", top(R"J("sampleRate":44100.5)J"), "sampleRate: 'sampleRate' takes a whole number, not 44100.5"},
        {"seconds as a string", top(R"J("seconds":"2")J"), "seconds: 'seconds' takes a number, not \"2\""},
        {"a comment that is not a string", top(R"J("comment":5)J"), "comment: 'comment' takes a string, not 5"},
        {"a comment as a list", top(R"J("comment":["a"])J"), "comment: 'comment' takes a string, not [\"a\"]"},
        {"a graph that is not an object", R"J({"graph":[]})J", "graph: 'graph' takes an object, not []"},
        {"nodes that are not a list", R"J({"graph":{"nodes":{}}})J", "graph.nodes: 'nodes' takes a list, not {}"},
        {"an output that is not a string", R"J({"graph":{"output":3}})J", "graph.output: 'output' takes a string, not 3"},
        {"a stray key in the graph", R"J({"graph":{"extras":1}})J", "graph.extras: unknown key 'extras'"},
        {"a node that is not an object", with_node("5"), "graph.nodes[1]: 'nodes[1]' takes an object, not 5"},
        {"an instrument that is not an object", R"J({"instrument":true})J", "instrument: 'instrument' takes an object, not true"},
        {"instrument.release", block(R"J({"release":0.3})J"), "instrument.release: unknown key 'release'"},
        {"instrument.paramMap", block(R"J({"paramMap":{}})J"),
         "instrument.paramMap: instrument.paramMap is no longer read: python tools/convert_patch.py <patch> rewrites it as wiring"},
        {"polyphony as a string", block(R"J({"polyphony":"4"})J"), "instrument.polyphony: 'polyphony' takes a whole number, not \"4\""},
        {"volume as a bool", block(R"J({"volume":true})J"), "instrument.volume: 'volume' takes a number, not true"},
        {"glideMs as null", block(R"J({"glideMs":null})J"), "instrument.glideMs: 'glideMs' takes a number, not null"},
        {"glideMs as a bool", block(R"J({"glideMs":true})J"), "instrument.glideMs: 'glideMs' takes a number, not true"},
        {"polyphony with a fraction", block(R"J({"polyphony":1.5})J"),
         "instrument.polyphony: 'polyphony' takes a whole number, not 1.5"},
        {"sustaining as 1", block(R"J({"sustaining":1})J"), "instrument.sustaining: 'sustaining' takes true or false, not 1"},
        {"onsets holding a number", block(R"J({"onsets":[1]})J"), "instrument.onsets: 'onsets' takes a list of strings, not [1]"},
        {"a layout that is not an object", R"J({"layout":[]})J", "layout: 'layout' takes an object, not []"},
        {"a stray key in the layout", layout(R"J({"zoom":2})J"), "layout.zoom: unknown key 'zoom'"},
        {"a position of one number", layout(R"J({"positions":{"o":[1]}})J"), "layout.positions.o: 'o' takes [x, y], not [1]"},
        {"panning as a string", layout(R"J({"panning":"x"})J"), "layout.panning: 'panning' takes [x, y], not \"x\""},
        {"groups that are not a list", layout(R"J({"groups":{}})J"), "layout.groups: 'groups' takes a list, not {}"},
        {"a group's stray key", layout(R"J({"groups":[{"name":"G","color":1}]})J"), "layout.groups[0].color: unknown key 'color'"},
        {"a group's pos of three", layout(R"J({"groups":[{"name":"G","pos":[1,2,3]}]})J"),
         "layout.groups[0].pos: 'pos' takes [x, y], not [1,2,3]"},
        {"a group's member that is a number", layout(R"J({"groups":[{"name":"G","members":["o",1]}]})J"),
         "layout.groups[0].members: 'members' takes a list of strings, not [\"o\",1]"},
        {"a face's stray key", layout(R"J({"faces":[{"label":"F","colour":1}]})J"), "layout.faces[0].colour: unknown key 'colour'"},
        {"a face's field that is a number", layout(R"J({"faces":[{"label":"F","fields":{"frequency":3}}]})J"),
         "layout.faces[0].fields.frequency: 'frequency' takes a string, not 3"},
    };
}
// The nineteen rows of retired_key_refusal's table, each at its own place.
static std::vector<Refused> retired_refusals() {
    const std::string tail = " is no longer read: python tools/convert_patch.py <patch> rewrites it as ";
    return {
        {"Envelope.stages", with_params("e", "Envelope", R"J({"stages":[]})J"),
         "graph.nodes[1].params.stages: node 'e' (Envelope): 'stages'" + tail + "the eight stage columns"},
        {"CurveNode.knots", with_params("c", "CurveNode", R"J({"knots":[]})J"),
         "graph.nodes[1].params.knots: node 'c' (CurveNode): 'knots'" + tail + "the columns x and y"},
        {"CurveNode.exprKnots", with_params("c", "CurveNode", R"J({"exprKnots":[]})J"),
         "graph.nodes[1].params.exprKnots: node 'c' (CurveNode): 'exprKnots'" + tail + "the columns exprX, exprForm, exprA and exprB"},
        {"PhasedValueSource.stages", with_params("v", "PhasedValueSource", R"J({"stages":[]})J"),
         "graph.nodes[1].params.stages: node 'v' (PhasedValueSource): 'stages'" + tail + "sources and the columns percent, minSec, maxSec and gain"},
        {"BWLowpassFilter.cutoff", with_params("f", "BWLowpassFilter", R"J({"cutoff":800})J"),
         "graph.nodes[1].params.cutoff: node 'f' (BWLowpassFilter): 'cutoff'" + tail + "cutoffFreq"},
        {"BWHighpassFilter.cutoff", with_params("f", "BWHighpassFilter", R"J({"cutoff":800})J"),
         "graph.nodes[1].params.cutoff: node 'f' (BWHighpassFilter): 'cutoff'" + tail + "cutoffFreq"},
        {"SVFSource.cutoff", with_params("f", "SVFSource", R"J({"cutoff":800})J"),
         "graph.nodes[1].params.cutoff: node 'f' (SVFSource): 'cutoff'" + tail + "cutoffFreq"},
        {"RepeatingSource.gap", with_params("r", "RepeatingSource", R"J({"gap":0.2})J"),
         "graph.nodes[1].params.gap: node 'r' (RepeatingSource): 'gap'" + tail + "gapDuration"},
        {"MultiSource feed weight", with_params("m", "MultiSource", R"J({"source":[{"ref":"o","weight":0.5}]})J"),
         "graph.nodes[1].params.source[0]: node 'm' (MultiSource): 'source': 'weight' on a feed" + tail + "the array weights"},
        {"MultiSource feed delay", with_params("m", "MultiSource", R"J({"source":[{"ref":"o","delay":0.5}]})J"),
         "graph.nodes[1].params.source[0]: node 'm' (MultiSource): 'source': 'delay' on a feed" + tail + "the array delays"},
        {"FullPartials inline expandRule", with_params("p", "FullPartials", R"J({"expandRule":{"recurse":1}})J"),
         "graph.nodes[1].params.expandRule: node 'p' (FullPartials): 'expandRule' as an inline object" + tail + "an ExpandRule node wired by ref"},
        {"SequencePartials inline expandRule", with_params("p", "SequencePartials", R"J({"expandRule":{"recurse":1}})J"),
         "graph.nodes[1].params.expandRule: node 'p' (SequencePartials): 'expandRule' as an inline object" + tail + "an ExpandRule node wired by ref"},
        {"ExplicitPartials inline expandRule", with_params("p", "ExplicitPartials", R"J({"expandRule":{"recurse":1}})J"),
         "graph.nodes[1].params.expandRule: node 'p' (ExplicitPartials): 'expandRule' as an inline object" + tail + "an ExpandRule node wired by ref"},
        {"SoundChannel inputs", with_node(R"J({"id":"c","type":"SoundChannel","inputs":{}})J"),
         "graph.nodes[1].inputs: node 'c' (SoundChannel): 'inputs' beside params" + tail + "the feed 'source' in params"},
        {"StereoMixer inputs", with_node(R"J({"id":"x","type":"StereoMixer","inputs":{}})J"),
         "graph.nodes[1].inputs: node 'x' (StereoMixer): 'inputs' beside params" + tail + "the feeds 'channels' in params"},
        {"the top's ui", top(R"J("ui":{})J"), "ui: 'ui' at the top of the file" + tail + "layout"},
        {"the top's groups", top(R"J("groups":[])J"), "groups: 'groups' at the top of the file" + tail + "layout.groups"},
        {"the top's _comment", top(R"J("_comment":"c")J"), "_comment: '_comment' at the top of the file" + tail + "comment"},
        {"the top's _provenance", top(R"J("_provenance":{})J"), "_provenance: '_provenance' at the top of the file" + tail + "comment"},
    };
}
}  // namespace patch_codec

namespace patch_codec {
// A node's own keys, its type, its dynamic pins and its seed.
static std::vector<Refused> node_refusals() {
    const std::string seed = "graph.nodes[1].params.seed: node 'x' (SineSource): 'seed' takes a whole number from 0 to 4294967295, not ";
    const std::string pin = "graph.nodes[1].dynamicPins.timeScale: node 'e' (Envelope): 'timeScale' in dynamicPins takes {\"ref\": id}, not ";
    return {
        {"a stray key on a node", with_node(R"J({"id":"x","type":"SineSource","label":"L"})J"),
         "graph.nodes[1].label: node 'x' (SineSource): unknown key 'label'"},
        {"an id that is not a string", with_node(R"J({"id":5,"type":"SineSource"})J"), "graph.nodes[1].id: 'id' takes a string, not 5"},
        {"a type that is not a string", with_node(R"J({"id":"x","type":[]})J"), "graph.nodes[1].type: 'type' takes a string, not []"},
        {"a node comment that is not a string", with_node(R"J({"id":"x","type":"SineSource","comment":5})J"),
         "graph.nodes[1].comment: node 'x' (SineSource): 'comment' takes a string, not 5"},
        {"a node comment as a list", with_node(R"J({"id":"x","type":"SineSource","comment":["a","b"]})J"),
         "graph.nodes[1].comment: node 'x' (SineSource): 'comment' takes a string, not [\"a\",\"b\"]"},
        {"params that are not an object", with_node(R"J({"id":"x","type":"SineSource","params":[]})J"),
         "graph.nodes[1].params: node 'x' (SineSource): 'params' takes an object, not []"},
        {"an unknown type", with_node(R"J({"id":"x","type":"NoSuchSource"})J"),
         "graph.nodes[1].type: node 'x' (NoSuchSource): Unknown node type: NoSuchSource"},
        {"a retired type", with_node(R"J({"id":"x","type":"AREnvelope"})J"),
         "graph.nodes[1].type: node 'x' (AREnvelope): Unknown node type: AREnvelope; python tools/convert_patch.py <patch> "
         "rewrites a type this engine no longer has"},
        {"frequency on a WhiteNoiseSource", with_params("n", "WhiteNoiseSource", R"J({"frequency":440})J"),
         "graph.nodes[1].params.frequency: node 'n' (WhiteNoiseSource): unknown key 'frequency'"},
        {"a null dynamic pin", with_node(R"J({"id":"e","type":"Envelope","dynamicPins":{"timeScale":null}})J"), pin + "null"},
        {"a tap dynamic pin", with_node(R"J({"id":"e","type":"Envelope","dynamicPins":{"timeScale":{"tap":"o"}}})J"),
         pin + "{\"tap\":\"o\"}"},
        {"a dynamic pin with an extra key", with_node(R"J({"id":"e","type":"Envelope","dynamicPins":{"timeScale":{"ref":"o","x":1}}})J"),
         pin + "{\"ref\":\"o\",\"x\":1}"},
        {"dynamicPins that are not an object", with_node(R"J({"id":"e","type":"Envelope","dynamicPins":3})J"),
         "graph.nodes[1].dynamicPins: node 'e' (Envelope): 'dynamicPins' takes an object, not 3"},
        {"dynamicPins as null", with_node(R"J({"id":"e","type":"Envelope","dynamicPins":null})J"),
         "graph.nodes[1].dynamicPins: node 'e' (Envelope): 'dynamicPins' takes an object, not null"},
        {"a negative seed", with_params("x", "SineSource", R"J({"seed":-5})J"), seed + "-5"},
        {"a fractional seed", with_params("x", "SineSource", R"J({"seed":7.5})J"), seed + "7.5"},
        {"a seed past 32 bits", with_params("x", "SineSource", R"J({"seed":4294967296})J"), seed + "4294967296"},
        {"a seed as a string", with_params("x", "SineSource", R"J({"seed":"7"})J"), seed + "\"7\""},
        {"a seed as a bool", with_params("x", "SineSource", R"J({"seed":true})J"), seed + "true"},
    };
}
// A pin, a list pin and a slot given what they do not take.
static std::vector<Refused> pin_refusals() {
    const std::string sine = "graph.nodes[1].params.frequency: node 'x' (SineSource): 'frequency'";
    const std::string multi = "graph.nodes[1].params.source";
    const std::string wave = "graph.nodes[1].params.evolution: node 'w' (WavetableSource): 'evolution'";
    return {
        {"a string on a pin", with_params("x", "SineSource", R"J({"frequency":"440"})J"),
         sine + " is the string \"440\"; a pin takes a number, " + kFeedTakes},
        {"a list on a pin that is not one", with_params("x", "SineSource", R"J({"frequency":[{"ref":"o"}]})J"),
         sine + " takes one feed, not a list"},
        {"a feed with both keys", with_params("x", "SineSource", R"J({"frequency":{"ref":"o","tap":"o"}})J"),
         sine + ": a feed holds 'ref' or 'tap', not both"},
        {"a key beside a feed's", with_params("x", "SineSource", R"J({"frequency":{"ref":"o","gain":2}})J"),
         sine + ": a feed holds 'ref' or 'tap' alone, not 'gain'"},
        {"a bool on a pin", with_params("x", "SineSource", R"J({"frequency":true})J"), sine + " takes a number, " + kFeedTakes + ", not true"},
        {"null on a pin", with_params("x", "SineSource", R"J({"frequency":null})J"), sine + " takes a number, " + kFeedTakes + ", not null"},
        {"an object that is no feed on a pin", with_params("x", "SineSource", R"J({"frequency":{"value":3}})J"),
         sine + " takes a number, " + kFeedTakes + ", not {\"value\":3}"},
        {"a ref that is not a string", with_params("x", "SineSource", R"J({"frequency":{"ref":3}})J"),
         sine + " takes a number, " + kFeedTakes + ", not {\"ref\":3}"},
        {"a number in a list of feeds", with_params("m", "MultiSource", R"J({"source":[{"ref":"o"},0.25]})J"),
         multi + "[1]: node 'm' (MultiSource): 'source' holds 0.25, where a list holds " + kFeedTakes},
        {"a string in a list of feeds", with_params("m", "MultiSource", R"J({"source":["o"]})J"),
         multi + "[0]: node 'm' (MultiSource): 'source' holds \"o\", where a list holds " + kFeedTakes},
        {"a string on a list pin", with_params("m", "MultiSource", R"J({"source":"o"})J"),
         multi + ": node 'm' (MultiSource): 'source' is the string \"o\"; a pin takes a number, " + kFeedTakes},
        {"a tap on a slot", with_params("w", "WavetableSource", R"J({"evolution":{"tap":"o"}})J"),
         wave + " is a slot: it takes {\"ref\": id}, not {\"tap\":\"o\"}"},
        {"a number on a slot", with_params("w", "WavetableSource", R"J({"evolution":3})J"), wave + " is a slot: it takes {\"ref\": id}, not 3"},
        {"an object that is no feed on a slot", with_params("w", "WavetableSource", R"J({"evolution":{"type":"PluckEvolution"}})J"),
         wave + " holds an object where a slot takes {\"ref\": id}"},
        {"a string on a slot", with_params("w", "WavetableSource", R"J({"evolution":"pluck"})J"),
         wave + " is the string \"pluck\"; a slot takes {\"ref\": id}; the string-typed evolution is gone, python "
                "tools/convert_patch.py <patch> rewrites it as an evolution node"},
        {"a list on a single slot", with_params("w", "WavetableSource", R"J({"evolution":[{"ref":"o"}]})J"), wave + " takes one feed, not a list"},
        {"a tap in a multi slot's list", with_params("mx", "StereoMixer", R"J({"channels":[{"tap":"o"}]})J"),
         "graph.nodes[1].params.channels[0]: node 'mx' (StereoMixer): 'channels' is a slot: it takes {\"ref\": id}, not {\"tap\":\"o\"}"},
    };
}
}  // namespace patch_codec

namespace patch_codec {
// Settings and arrays given what their kind does not take; and the
// refusals check() makes once the read is whole.
static std::vector<Refused> value_refusals() {
    const std::string svf = "graph.nodes[1].params.mode: node 's' (SVFSource): 'mode' holds ";
    const std::string labels = ", which is not one of its labels (Lowpass, Highpass, Bandpass, Lowpass1P, Highpass1P) or an index into them";
    const std::string column = "graph.nodes[1].params.type: node 'e' (Envelope): 'type' holds ";
    const std::string stageLabels = ", which is not one of its labels (Linear, Expo, InverseExpo, Sine, Hold) or an index into them";
    return {
        {"a string on a float setting", with_params("b", "Biquad", R"J({"b0":"1.5"})J"),
         "graph.nodes[1].params.b0: node 'b' (Biquad): 'b0' takes a number, not \"1.5\""},
        {"a bool on a float setting", with_params("b", "Biquad", R"J({"b0":true})J"),
         "graph.nodes[1].params.b0: node 'b' (Biquad): 'b0' takes a number, not true"},
        {"null on a float setting", with_params("b", "Biquad", R"J({"b0":null})J"),
         "graph.nodes[1].params.b0: node 'b' (Biquad): 'b0' takes a number, not null"},
        {"a list on a float setting", with_params("b", "Biquad", R"J({"b0":[1.5]})J"),
         "graph.nodes[1].params.b0: node 'b' (Biquad): 'b0' takes a number, not [1.5]"},
        {"a feed on a float setting", with_params("b", "Biquad", R"J({"b1":{"ref":"o"}})J"),
         "graph.nodes[1].params.b1: node 'b' (Biquad): 'b1' takes a number, not {\"ref\":\"o\"}"},
        {"an int given a fraction", with_params("f", "BWLowpassFilter", R"J({"sections":2.5})J"),
         "graph.nodes[1].params.sections: node 'f' (BWLowpassFilter): 'sections' takes a whole number, not 2.5"},
        {"a bool given 2", with_params("s", "SVFSource", R"J({"normalize":2})J"),
         "graph.nodes[1].params.normalize: node 's' (SVFSource): 'normalize' takes true or false, not 2"},
        {"a bool given a string", with_params("s", "SVFSource", R"J({"normalize":"true"})J"),
         "graph.nodes[1].params.normalize: node 's' (SVFSource): 'normalize' takes true or false, not \"true\""},
        {"an enum word that is no label", with_params("s", "SVFSource", R"J({"mode":"Bandstop"})J"), svf + "\"Bandstop\"" + labels},
        {"an enum label in another case", with_params("s", "SVFSource", R"J({"mode":"highpass"})J"), svf + "\"highpass\"" + labels},
        {"an enum index outside its labels", with_params("s", "SVFSource", R"J({"mode":5})J"), svf + "5" + labels},
        {"an enum given a fraction", with_params("s", "SVFSource", R"J({"mode":1.5})J"), svf + "1.5" + labels},
        {"an enum given a bool", with_params("s", "SVFSource", R"J({"mode":true})J"), svf + "true" + labels},
        {"a number on a string setting", with_params("g", "NameGate", R"J({"name":3})J"),
         "graph.nodes[1].params.name: node 'g' (NameGate): 'name' takes a string, not 3"},
        {"a number on an array", with_params("sg", "SegmentSource", R"J({"values":0.5})J"),
         "graph.nodes[1].params.values: node 'sg' (SegmentSource): 'values' takes a list of numbers, not 0.5"},
        {"a string in an array", with_params("sg", "SegmentSource", R"J({"values":[1,"a"]})J"),
         "graph.nodes[1].params.values: node 'sg' (SegmentSource): 'values' takes a list of numbers, not [1,\"a\"]"},
        {"a bool in an array", with_params("sg", "SegmentSource", R"J({"values":[1,true]})J"),
         "graph.nodes[1].params.values: node 'sg' (SegmentSource): 'values' takes a list of numbers, not [1,true]"},
        {"a labelled column holding no label", with_params("e", "Envelope", "{" + stage_columns(R"J(["Linear","Bogus"])J") + "}"),
         column + "\"Bogus\"" + stageLabels},
        {"a labelled column's label in another case", with_params("e", "Envelope", "{" + stage_columns(R"J(["linear",0])J") + "}"),
         column + "\"linear\"" + stageLabels},
        {"a float setting outside its range", with_params("b", "Biquad", R"J({"b0":12})J"),
         "graph.nodes[1].params.b0: node 'b' (Biquad): 'b0' is 12, outside its range -10 to 10"},
        {"an int setting outside its range", with_params("c", "CellularAutomatonEvolution", R"J({"rule":300})J"),
         "graph.nodes[1].params.rule: node 'c' (CellularAutomatonEvolution): 'rule' is 300, outside its range 0 to 255"},
        {"two violations, one per line", with_params("b", "Biquad", R"J({"b0":12,"a2":-3})J"),
         "graph.nodes[1].params.a2: node 'b' (Biquad): 'a2' is -3, outside its range -1 to 1\n"
         "graph.nodes[1].params.b0: node 'b' (Biquad): 'b0' is 12, outside its range -10 to 10"},
        {"a dynamic pin in a plain sound",
         R"J({"seconds":1,"graph":{"output":"mx","nodes":[{"id":"o","type":"SineSource"},)J"
         R"J({"id":"b","type":"Biquad","params":{"source":{"ref":"o"}},"dynamicPins":{"b0":{"ref":"o"}}},)J"
         R"J({"id":"ch","type":"SoundChannel","params":{"source":{"ref":"b"}}},)J"
         R"J({"id":"mx","type":"StereoMixer","params":{"channels":[{"ref":"ch"}]}}]}})J",
         "graph.nodes[1].dynamicPins.b0: node 'b' (Biquad): a plain sound has no note, so nothing drives a dynamic pin"},
        {"a document that is not an object", "5", "a patch takes an object, not 5"},
    };
}
}  // namespace patch_codec

// Every refusal of the reader, each through load_text and compared whole:
// the document's levels, the nineteen rows of retired_key_refusal at their places
// (TopKey among them), a node's keys, its pins, its settings and arrays, and
// what check() refuses once the read is whole.
static void run_codec_refusal_tests() {
    using namespace patch_codec;
    std::printf("the codec: every refusal with its path and its sentence\n");
    run_refusals(document_refusals());
    run_refusals(retired_refusals());
    run_refusals(node_refusals());
    run_refusals(pin_refusals());
    run_refusals(value_refusals());
    std::printf("  %zu document, %zu retired, %zu node, %zu pin and %zu value refusals\n", document_refusals().size(),
                retired_refusals().size(), node_refusals().size(), pin_refusals().size(), value_refusals().size());
}

// A read that fails leaves its target as it was: a Patch, a Node, a Graph, a
// block, a layout and a fragment, each given a record first.
static void run_codec_untouched_tests() {
    using namespace patch_codec;
    std::printf("the codec: a read that fails leaves its target as it was\n");
    const Patch given = load_text(with_params("x", "SawSource", R"J({"frequency":330})J"));
    for (const std::string& bad : {with_params("x", "SawSource", R"J({"frequency":"330"})J"),
                                   with_params("b", "Biquad", R"J({"b0":12})J"), top(R"J("frames":1)J"),
                                   std::string(R"J({"graph":{"output":"o","nodes":[{"id":"o","type":"SineSource"},5]}})J")}) {
        Patch target = given;
        try { from_json(J::parse(bad), target); CHECK(false); } catch (const std::runtime_error&) {}
        CHECK(target == given);
    }
    const Node node = read_node(R"J({"id":"x","type":"SawSource","params":{"frequency":330}})J");
    Node nodeTarget = node;
    try { from_json(J::parse(R"J({"id":"y","type":"SawSource","params":{"bogus":1}})J"), nodeTarget); CHECK(false); }
    catch (const std::runtime_error&) {}
    CHECK(nodeTarget == node);
    Graph graph = given.graph, graphTarget = given.graph;
    try { from_json(J::parse(R"J({"output":"z","nodes":[{"id":"z","type":"NoSuchSource"}]})J"), graphTarget); CHECK(false); }
    catch (const std::runtime_error&) {}
    CHECK(graphTarget == graph);
    const InstrumentBlock blockGiven{2, 0.5f, true, {"tongue"}, 20.0f};
    InstrumentBlock blockTarget = blockGiven;
    try { from_json(J::parse(R"J({"polyphony":3,"release":1})J"), blockTarget); CHECK(false); } catch (const std::runtime_error&) {}
    CHECK(blockTarget == blockGiven);
    Layout layoutGiven;
    layoutGiven.positions["x"] = Vec2{1, 2};
    Layout layoutTarget = layoutGiven;
    try { from_json(J::parse(R"J({"positions":{"y":[3,4]},"zoom":1})J"), layoutTarget); CHECK(false); } catch (const std::runtime_error&) {}
    CHECK(layoutTarget == layoutGiven);
    const Fragment fragmentGiven{{node}, {}, {}, {}};
    Fragment fragmentTarget = fragmentGiven;
    try { from_json(J::parse(R"J({"nodes":[],"faces":[3]})J"), fragmentTarget); CHECK(false); } catch (const std::runtime_error&) {}
    CHECK(fragmentTarget == fragmentGiven);
}

// ---------------------------------------------------------------------------
// The builder: build_graph over a Patch's graph, build_instrument and
// build_sound, each against the renders and refusals its tests hold.
#include "mforce/patch/build_graph.h"
#include "mforce/render/build_instrument.h"
#include "mforce/render/build_sound.h"
#include "mforce/source/combiner/multi_source.h"
#include "mforce/source/combiner/multiplex_source.h"
#include "mforce/source/modulator/name_gate.h"

namespace build_tests {
using patch_codec::load_text;
using J = nlohmann::json;
using patch_door::first_difference;
using patch_door::live_c4;
using patch_door::render_sound;
using patch_door::same_samples;

// The six note signals as the constants 1 to 6, each its own object.
static NoteSignals constant_note() {
    const auto c = [](float v) { return std::make_shared<ConstantSource>(v); };
    return NoteSignals{c(1.0f), c(2.0f), c(3.0f), c(4.0f), c(5.0f), c(6.0f)};
}
// An instrument around `nodes` (the inside of a JSON array), `output` its output.
static std::string inst(const std::string& nodes, const char* output,
                        const std::string& block = R"({"polyphony":1})") {
    return R"({"instrument":)" + block + R"(,"graph":{"output":")" + output + R"(","nodes":[)" + nodes + "]}}";
}
// A plain sound around `nodes`, `output` its output, a quarter second long.
static std::string plain(const std::string& nodes, const char* output) {
    return std::string(R"({"seconds":0.25,"graph":{"output":")") + output + R"(","nodes":[)" + nodes + "]}}";
}
// `text` read through the codec, its graph built with the constant note and the patch's block.
static BuiltGraph built(const std::string& text) {
    const Patch p = load_text(text);
    const NoteSignals note = constant_note();
    return build_graph(p.graph, &note, p.instrument ? &*p.instrument : nullptr);
}
// The refusal a graph's build gives, whole; empty when it builds.
static std::string build_refusal(const Graph& g, bool withNote = true, const InstrumentBlock* block = nullptr) {
    const NoteSignals note = constant_note();
    try { (void)build_graph(g, withNote ? &note : nullptr, block); } catch (const std::exception& e) { return e.what(); }
    return {};
}
static std::string text_refusal(const std::string& text) { return build_refusal(load_text(text).graph); }
// One feed by ref, one by tap.
static std::vector<Wire> ref(const char* id) { return {Wire{id, false}}; }
static std::vector<Wire> tap(const char* id) { return {Wire{id, true}}; }
// A node of the record made by hand: one pin's feeds, or none.
static Node node(const char* id, const char* type, const char* pin = nullptr, std::vector<Wire> feeds = {}) {
    Node n;
    n.id = id;
    n.type = type;
    if (pin) n.wires[pin] = std::move(feeds);
    return n;
}
static Graph graph_of(std::vector<Node> nodes, const char* output) {
    Graph g;
    g.nodes = std::move(nodes);
    g.output = output;
    return g;
}
// A Shaper whose values2 is not as long as its values, which validate()
// refuses; `source` names the node it reads, or none.
static std::string bad_shaper(const char* id, const char* source = nullptr) {
    return std::string(R"({"id":")") + id + R"(","type":"Shaper","params":{"values2":[-1,-1,0,0,1,1])" +
           (source ? std::string(R"(,"source":{"ref":")") + source + R"("})" : std::string()) + "}}";
}
const std::string kShaperRefusal = "values2 length must match values";
// `src` rendered for `n` samples on a clock of its own at 48 kHz.
static std::vector<float> render_signal(ValueSource& src, int n) {
    SampleClock clock;
    src.prepare(RenderContext{48000}.with_clock(&clock));
    src.start(n);
    std::vector<float> out(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) { clock.tick(); out[size_t(i)] = src.next(); }
    return out;
}
// Three notes on the offline path: a held one, the note that ends its line, and one alone.
static std::vector<float> play_three(PitchedInstrument& pi, int onset = 0) {
    pi.play_note({.start = 0.0f, .duration = 0.4f, .noteNumber = 48.0f, .velocity = 0.9f, .onset = onset, .hold = true});
    pi.play_note({.start = 0.4f, .duration = 0.4f, .noteNumber = 60.0f, .velocity = 0.4f, .onset = onset, .hold = false});
    pi.play_note({.start = 1.0f, .duration = 0.4f, .noteNumber = 67.0f, .velocity = 0.9f, .onset = onset, .hold = false});
    std::vector<float> b(size_t(2 * 48000), 0.0f);
    pi.render(b.data(), int(b.size()));
    return b;
}
}  // namespace build_tests

// The order of construction: the file's, where every feed comes first; a
// node a feed names later is built before its reader, the rest in the file's
// order; a dynamic pin's later source builds; a cycle with no tap, and a node
// reading itself, are refused naming their nodes; a tap closes a loop.
static void run_build_sort_tests() {
    using namespace build_tests;
    std::printf("the builder's order: the file's where feeds come first; cycles refused by name\n");
    CHECK(text_refusal(inst(bad_shaper("A") + "," + bad_shaper("B"), "A")) == "node 'A' (Shaper): " + kShaperRefusal);
    CHECK(text_refusal(inst(bad_shaper("B") + "," + bad_shaper("A"), "A")) == "node 'B' (Shaper): " + kShaperRefusal);
    CHECK(text_refusal(inst(bad_shaper("A", "C") + "," + bad_shaper("C"), "A")) == "node 'C' (Shaper): " + kShaperRefusal);
    CHECK(text_refusal(inst(bad_shaper("A", "C") + "," + bad_shaper("B") + R"(,{"id":"C","type":"SineSource"})", "A")) ==
          "node 'B' (Shaper): " + kShaperRefusal);
    const BuiltGraph pinned = built(inst(
        R"({"id":"env","type":"Envelope","dynamicPins":{"timeScale":{"ref":"late"}}},)"
        R"({"id":"out","type":"SawSource","params":{"amplitude":{"ref":"env"}}},{"id":"late","type":"SineSource"})", "out"));
    CHECK(pinned.dynamicPins.size() == 1 && pinned.dynamicPins[0].source.get() == pinned.nodes.at("late").get());
    const Graph cycle = graph_of({node("A", "SineSource", "amplitude", ref("B")), node("B", "SineSource", "amplitude", ref("A"))}, "A");
    CHECK(build_refusal(cycle) == "a cycle with no tap in it: A -> B -> A");
    CHECK(build_refusal(graph_of({node("A", "SineSource", "amplitude", ref("A"))}, "A")) == "a cycle with no tap in it: A -> A");
    Graph loop = cycle;
    loop.nodes[1].wires["amplitude"] = tap("A");
    CHECK(build_refusal(loop).empty());
    CHECK(build_refusal(graph_of({node("A", "SineSource", "amplitude", ref("ghost"))}, "A")) == "Unresolved node ref: ghost");
    CHECK(build_refusal(graph_of({node("A", "SineSource", "amplitude", tap("ghost"))}, "A")) == "Unresolved tap target: ghost");
    CHECK(build_refusal(graph_of({node("A", "SineSource")}, "")) == "the patch has no output");
    CHECK(build_refusal(graph_of({node("A", "NoSuchSource")}, "A")) == "Unknown node type: NoSuchSource");
}

// The fill: a Param's number its own constant with nothing wired, an Input's
// a wired constant; a list's feeds in order; a slot's part; a setting, a
// string, an array, a labelled column by index; settings before arrays.
static void run_build_fill_tests() {
    using namespace build_tests;
    std::printf("the builder's fill: a Param's number, an Input's, feeds in order, settings before arrays\n");
    const BuiltGraph g = built(inst(
        R"({"id":"a","type":"SineSource","params":{"amplitude":0.3,"frequency":220}},{"id":"b","type":"SawSource"},)"
        R"({"id":"sum","type":"CombinedSource","params":{"source1":{"ref":"a"},"source2":0.5}},)"
        R"({"id":"list","type":"MultiSource","params":{"source":[{"ref":"b"},{"ref":"a"}]}},)"
        R"({"id":"parts","type":"FullPartials","params":{"maxPartials":4}},)"
        R"({"id":"add","type":"AdditiveSource","params":{"partials":{"ref":"parts"}}},)"
        R"({"id":"svf","type":"SVFSource","params":{"source":{"ref":"b"},"mode":"Highpass"}},)"
        R"({"id":"gate","type":"NameGate","params":{"name":"tongue"}},{"id":"seg","type":"SegmentSource","params":{"values":[0,1,0.5]}},)"
        R"({"id":"env","type":"Envelope","params":{)" + patch_codec::stage_columns("[0,3]") + "}}", "sum"));
    Param* amp = g.nodes.at("a")->param("amplitude");
    CHECK(amp && amp->source == nullptr && amp->constant == 0.3f);
    const auto s2 = g.nodes.at("sum")->get_param("source2");
    const auto* k = dynamic_cast<const ConstantSource*>(s2.get());
    CHECK(k && k->current() == 0.5f);
    CHECK(g.nodes.at("sum")->get_param("source1").get() == g.nodes.at("a").get());
    const auto* list = dynamic_cast<const MultiSource*>(g.nodes.at("list").get());
    CHECK(list && list->source.sources.size() == 2 && list->source.sources[0].get() == g.nodes.at("b").get() &&
          list->source.sources[1].get() == g.nodes.at("a").get());
    CHECK(g.nodes.at("add")->get_part("partials") == g.nodes.at("parts"));
    const auto& modes = type_declarations("SVFSource")->setting("mode")->labels;
    CHECK(g.nodes.at("svf")->get_setting("mode") == float(std::find(modes.begin(), modes.end(), "Highpass") - modes.begin()));
    CHECK(g.nodes.at("gate")->get_string("name") == "tongue");
    CHECK(g.nodes.at("seg")->get_array("values") == std::vector<float>({0.0f, 1.0f, 0.5f}));
    CHECK(g.nodes.at("env")->get_array("type") == std::vector<float>({0.0f, 3.0f}));
    // Settings before arrays: a sustainLevel beside four-stage columns leaves the file's cells.
    const BuiltGraph four = built(inst(R"({"id":"env","type":"Envelope","params":{"sustainLevel":0.9,)"
        R"("startVal":[0,1,0.5,0.5],"endVal":[1,0.5,0.5,0],"power":[0,0,0,0],"type":[0,0,0,0],)"
        R"("percent":[0.05,0.1,0,0.25],"minSec":[0,0,0,0],"maxSec":[0,0,0,0],"nominal":[0,0,0,0]}})", "env"));
    CHECK(four.nodes.at("env")->get_array("startVal") == std::vector<float>({0.0f, 1.0f, 0.5f, 0.5f}));
    CHECK(four.nodes.at("env")->get_setting("sustainLevel") == 0.5f);
    // The door's refusals, on records the codec would not have read.
    Patch p = load_text(inst(R"({"id":"a","type":"SineSource"},{"id":"parts","type":"FullPartials"},)"
                             R"({"id":"add","type":"AdditiveSource"})", "a"));
    Graph partOnSignal = p.graph;
    partOnSignal.nodes[0].wires["amplitude"] = ref("parts");
    CHECK(build_refusal(partOnSignal) == "node 'a' (SineSource): 'amplitude' does not accept a FullPartials, a part, not a signal");
    Graph signalOnSlot = p.graph;
    signalOnSlot.nodes[2].wires["partials"] = ref("a");
    CHECK(build_refusal(signalOnSlot) == "node 'add' (AdditiveSource): 'partials' does not accept a SineSource; 'partials' takes " +
                                             type_declarations("AdditiveSource")->input("partials")->hint);
    CHECK(text_refusal(inst(bad_shaper("sh"), "sh")) == "node 'sh' (Shaper): " + kShaperRefusal);
    // The table rule on a record made in memory and never checked.
    Graph ragged = graph_of({node("env", "Envelope")}, "env");
    ragged.nodes[0].arrays["startVal"] = {0.0f, 1.0f};
    CHECK(build_refusal(ragged) == "node 'env' (Envelope): table 'stages' takes its columns at one length, all present or "
                                   "all absent (got startVal 2, endVal absent, power absent, type absent, percent absent, "
                                   "minSec absent, maxSec absent, nominal absent)");
}

// The three names: a PerformNode is the note's signal its field names, and
// refused with no note; a NameGate's name finds its 1-based place among the
// onsets, and one outside them, or any name with no block, is one warning.
static void run_build_named_tests() {
    using namespace build_tests;
    std::printf("the builder's three names: the note's fields, the onsets, the template\n");
    std::string fields;
    for (int k = 0; k < 6; ++k)
        fields += std::string(R"({"id":"f)") + char('0' + k) + R"(","type":"PerformNode","params":{"field":")" +
                  PerformNode::kFieldLabels[k] + R"("}},)";
    const Patch p = load_text(inst(fields + R"({"id":"out","type":"SineSource","params":{"frequency":{"ref":"f0"}}})", "out"));
    const NoteSignals note = constant_note();
    const BuiltGraph g = build_graph(p.graph, &note, nullptr);
    for (int k = 0; k < 6; ++k) CHECK(g.nodes.at(std::string("f") + char('0' + k)).get() == note.field(k).get());
    CHECK(note.field(0) == note.frequency && note.field(1) == note.velocity && note.field(2) == note.wheel &&
          note.field(3) == note.pressure && note.field(4) == note.duration && note.field(5) == note.onset);
    CHECK(build_refusal(p.graph, false) == "node 'f0' (PerformNode): a plain sound has no note to read");
    const Patch gp = load_text(inst(R"({"id":"g1","type":"NameGate","params":{"name":"bow"}},)"
                                    R"({"id":"g2","type":"NameGate","params":{"name":"tongue"}},)"
                                    R"({"id":"g3","type":"NameGate","params":{"name":"slap"}},)"
                                    R"({"id":"g4","type":"NameGate"},{"id":"out","type":"SineSource"})",
                                    "out", R"({"polyphony":1,"onsets":["tongue","bow"]})"));
    const BuiltGraph gg = build_graph(gp.graph, &note, &*gp.instrument);
    const auto target = [&](const BuiltGraph& b, const char* id) { return static_cast<NameGate&>(*b.nodes.at(id)).targetId; };
    CHECK(target(gg, "g1") == 2.0f && target(gg, "g2") == 1.0f && target(gg, "g3") < 0.0f && target(gg, "g4") < 0.0f);
    CHECK(gg.warnings == std::vector<std::string>(
        {"node 'g3' (NameGate): the name 'slap' is not one of the instrument's onsets, so the gate never fires"}));
    const BuiltGraph none = build_graph(gp.graph, &note, nullptr);
    CHECK(none.warnings.size() == 3 && target(none, "g2") < 0.0f);
    if (!none.warnings.empty())
        CHECK(none.warnings[0] == "node 'g1' (NameGate): the name 'bow' finds no onsets: the graph is built without an "
                                  "instrument block, so the gate never fires");
}

namespace build_tests {
// Instruments around a MultiplexSource, a1 to a7 (the synthetic set's files of
// those names, without their layout, score and length), and a NameGate inside
// a Multiplex template.
struct Named { const char* name; const char* text; };
static const Named kMultiplexTexts[] = {
    {"a1", R"J({"graph":{"nodes":[{"id":"pf","type":"PerformNode","params":{"field":"frequency"}},{"id":"amp","type":"Envelope","params":{"preset":"adsr","attack":0.02,"decay":0.1,"release":0.1,"sustainLevel":0.7}},{"id":"N","type":"WhiteNoiseSource","params":{"amplitude":0.3,"seed":1101}},{"id":"S","type":"SawSource","params":{"amplitude":0.3,"frequency":{"ref":"pf"},"phase":0.0}},{"id":"F","type":"SVFSource","params":{"source":{"ref":"S"},"cutoffFreq":1500.0,"resonance":0.3,"mode":"Lowpass"}},{"id":"R","type":"CombinedSource","params":{"source1":{"ref":"N"},"source2":{"ref":"F"},"operation":"Sum","gainAdj":0.0,"seed":1201}},{"id":"mux","type":"MultiplexSource","params":{"count":5,"source":{"ref":"R"}}},{"id":"out","type":"CombinedSource","params":{"source1":{"ref":"mux"},"source2":{"ref":"amp"},"operation":"Multiply","gainAdj":0.0}}],"output":"out"},"instrument":{"polyphony":1,"volume":0.5},"sampleRate":48000})J"},
    {"a2", R"J({"graph":{"nodes":[{"id":"pfF","type":"PerformNode","params":{"field":"frequency"}},{"id":"pfV","type":"PerformNode","params":{"field":"velocity"}},{"id":"amp","type":"Envelope","params":{"preset":"adsr","attack":0.02,"decay":0.1,"release":0.1,"sustainLevel":0.7}},{"id":"S","type":"SawSource","params":{"amplitude":0.4,"frequency":{"ref":"pfF"},"phase":0.0}},{"id":"N","type":"WhiteNoiseSource","params":{"amplitude":0.1,"seed":1301}},{"id":"SN","type":"CombinedSource","params":{"source1":{"ref":"S"},"source2":{"ref":"N"},"operation":"Sum","gainAdj":0.0}},{"id":"R","type":"CombinedSource","params":{"source1":{"ref":"SN"},"source2":{"ref":"pfV"},"operation":"Multiply","gainAdj":0.0}},{"id":"mux","type":"MultiplexSource","params":{"count":3,"source":{"ref":"R"}}},{"id":"out","type":"CombinedSource","params":{"source1":{"ref":"mux"},"source2":{"ref":"amp"},"operation":"Multiply","gainAdj":0.0}}],"output":"out"},"instrument":{"polyphony":1,"volume":0.5},"sampleRate":48000})J"},
    {"a3", R"J({"graph":{"nodes":[{"id":"pf","type":"PerformNode","params":{"field":"frequency"}},{"id":"amp","type":"Envelope","params":{"preset":"adsr","attack":0.02,"decay":0.1,"release":0.1,"sustainLevel":0.7}},{"id":"N","type":"WhiteNoiseSource","params":{"amplitude":0.2,"seed":1401}},{"id":"S","type":"SawSource","params":{"amplitude":0.3,"frequency":{"ref":"pf"},"phase":0.0}},{"id":"R2","type":"CombinedSource","params":{"source1":{"ref":"N"},"source2":{"ref":"S"},"operation":"Sum","gainAdj":0.0}},{"id":"inner","type":"MultiplexSource","params":{"count":2,"source":{"ref":"R2"}}},{"id":"F","type":"SVFSource","params":{"source":{"ref":"inner"},"cutoffFreq":2000.0,"resonance":0.3,"mode":"Lowpass"}},{"id":"outer","type":"MultiplexSource","params":{"count":3,"source":{"ref":"F"}}},{"id":"out","type":"CombinedSource","params":{"source1":{"ref":"outer"},"source2":{"ref":"amp"},"operation":"Multiply","gainAdj":0.0}}],"output":"out"},"instrument":{"polyphony":1,"volume":0.5},"sampleRate":48000})J"},
    {"a4", R"J({"graph":{"nodes":[{"id":"pf","type":"PerformNode","params":{"field":"frequency"}},{"id":"E","type":"Envelope","params":{"preset":"adsr","attack":0.02,"decay":0.1,"release":0.1,"sustainLevel":0.7,"stage_accuracy":0.6}},{"id":"R","type":"SawSource","params":{"amplitude":{"ref":"E"},"frequency":{"ref":"pf"},"phase":0.0}},{"id":"mux","type":"MultiplexSource","params":{"count":4,"source":{"ref":"R"}}}],"output":"mux"},"instrument":{"polyphony":1,"volume":0.5},"sampleRate":48000})J"},
    {"a5", R"J({"graph":{"nodes":[{"id":"pf","type":"PerformNode","params":{"field":"frequency"}},{"id":"amp","type":"Envelope","params":{"preset":"adsr","attack":0.02,"decay":0.1,"release":0.1,"sustainLevel":0.7}},{"id":"N","type":"WhiteNoiseSource","params":{"amplitude":0.3,"seed":1501}},{"id":"F","type":"SVFSource","params":{"source":{"ref":"N"},"cutoffFreq":800.0,"resonance":0.6,"mode":"Lowpass"}},{"id":"mux","type":"MultiplexSource","params":{"count":3,"source":{"ref":"F"}}},{"id":"S","type":"SawSource","params":{"amplitude":0.2,"frequency":{"ref":"pf"},"phase":0.0}},{"id":"sum","type":"CombinedSource","params":{"source1":{"ref":"mux"},"source2":{"ref":"S"},"operation":"Sum","gainAdj":0.0}},{"id":"out","type":"CombinedSource","params":{"source1":{"ref":"sum"},"source2":{"ref":"amp"},"operation":"Multiply","gainAdj":0.0}}],"output":"out"},"instrument":{"polyphony":1,"volume":0.5},"sampleRate":48000})J"},
    {"a6a", R"J({"graph":{"nodes":[{"id":"pf","type":"PerformNode","params":{"field":"frequency"}},{"id":"amp","type":"Envelope","params":{"preset":"adsr","attack":0.02,"decay":0.1,"release":0.1,"sustainLevel":0.7}},{"id":"T","type":"SawSource","params":{"amplitude":0.3,"frequency":{"ref":"pf"},"phase":0.0}},{"id":"mux","type":"MultiplexSource","params":{"count":3,"source":{"tap":"T"}}},{"id":"S0","type":"SineSource","params":{"amplitude":0.3,"frequency":{"ref":"pf"},"phase":0.0}},{"id":"sum","type":"CombinedSource","params":{"source1":{"ref":"S0"},"source2":{"ref":"mux"},"operation":"Sum","gainAdj":0.0}},{"id":"out","type":"CombinedSource","params":{"source1":{"ref":"sum"},"source2":{"ref":"amp"},"operation":"Multiply","gainAdj":0.0}}],"output":"out"},"instrument":{"polyphony":1,"volume":0.5},"sampleRate":48000})J"},
    {"a6b", R"J({"graph":{"nodes":[{"id":"pf","type":"PerformNode","params":{"field":"frequency"}},{"id":"amp","type":"Envelope","params":{"preset":"adsr","attack":0.02,"decay":0.1,"release":0.1,"sustainLevel":0.7}},{"id":"mux","type":"MultiplexSource","params":{"count":3}},{"id":"S0","type":"SineSource","params":{"amplitude":0.3,"frequency":{"ref":"pf"},"phase":0.0}},{"id":"sum","type":"CombinedSource","params":{"source1":{"ref":"S0"},"source2":{"ref":"mux"},"operation":"Sum","gainAdj":0.0}},{"id":"out","type":"CombinedSource","params":{"source1":{"ref":"sum"},"source2":{"ref":"amp"},"operation":"Multiply","gainAdj":0.0}}],"output":"out"},"instrument":{"polyphony":1,"volume":0.5},"sampleRate":48000})J"},
    {"a7", R"J({"graph":{"nodes":[{"id":"pf","type":"PerformNode","params":{"field":"frequency"}},{"id":"amp","type":"Envelope","params":{"preset":"adsr","attack":0.02,"decay":0.1,"release":0.1,"sustainLevel":0.7}},{"id":"X","type":"SawSource","params":{"amplitude":0.3,"frequency":{"ref":"pf"},"phase":0.0}},{"id":"N","type":"WhiteNoiseSource","params":{"amplitude":0.05,"seed":1701}},{"id":"XN","type":"CombinedSource","params":{"source1":{"ref":"X"},"source2":{"ref":"N"},"operation":"Sum","gainAdj":0.0}},{"id":"Y","type":"CombinedSource","params":{"source1":{"tap":"XN"},"source2":0.5,"operation":"Multiply","gainAdj":0.0}},{"id":"R","type":"CombinedSource","params":{"source1":{"ref":"XN"},"source2":{"ref":"Y"},"operation":"Sum","gainAdj":0.0}},{"id":"mux","type":"MultiplexSource","params":{"count":3,"source":{"ref":"R"}}},{"id":"out","type":"CombinedSource","params":{"source1":{"ref":"mux"},"source2":{"ref":"amp"},"operation":"Multiply","gainAdj":0.0}}],"output":"out"},"instrument":{"polyphony":1,"volume":0.5},"sampleRate":48000})J"},
};
static const char* const kGateInTemplate = R"J({"graph":{"nodes":[{"id":"pfF","type":"PerformNode","params":{"field":"frequency"}},{"id":"pfO","type":"PerformNode","params":{"field":"onset"}},{"id":"ng","type":"NameGate","params":{"name":"tongue","in":{"ref":"pfO"}}},{"id":"gest","type":"Envelope","params":{"minValue":0.0,"maxValue":1.0,"ramp_accuracy":1.0,"stage_accuracy":1.0,"timeMode":"seconds","timeScale":1.0,"startVal":[1.0,0.1,1.0],"endVal":[0.1,1.0,1.0],"power":[0.0,0.0,0.0],"type":["Linear","Linear","Linear"],"percent":[0.01,0.02,0.0],"minSec":[0.0,0.0,0.0],"maxSec":[0.0,0.0,0.0],"nominal":[0.0,0.0,0.0],"trigger":{"ref":"ng"}}},{"id":"lift","type":"CombinedSource","params":{"source1":{"ref":"ng"},"source2":1.0,"operation":"Sum","gainAdj":0.0}},{"id":"amp2","type":"CombinedSource","params":{"source1":{"ref":"gest"},"source2":{"ref":"lift"},"operation":"Multiply","gainAdj":0.0}},{"id":"R","type":"SawSource","params":{"amplitude":{"ref":"amp2"},"frequency":{"ref":"pfF"},"phase":0.0}},{"id":"mux","type":"MultiplexSource","params":{"count":2,"source":{"ref":"R"}}}],"output":"mux"},"instrument":{"polyphony":1,"volume":0.5,"onsets":["tongue"],"sustaining":true},"sampleRate":48000})J";
}  // namespace build_tests

// The template on the Multiplex texts: each plays something. A NameGate
// inside a template resolves against the block's onsets: it fires on a name
// the block holds and not on one it lacks.
static void run_build_template_reader_tests() {
    using namespace build_tests;
    std::printf("the template: the Multiplex files play; a gate in the template fires on the block's onset\n");
    for (const auto& t : kMultiplexTexts) {
        auto mod = build_instrument(load_text(t.text));
        const auto b = play_three(*mod);
        const bool audible = std::any_of(b.begin(), b.end(), [](float s) { return s != 0.0f; });
        if (!audible) std::printf("  %s: silent\n", t.name);
        CHECK(audible);
    }
    std::string outside = kGateInTemplate;
    outside.replace(outside.find(R"("name":"tongue")"), 15, R"("name":"slap")");
    auto mod = build_instrument(load_text(kGateInTemplate));
    auto modOutside = build_instrument(load_text(outside));
    const auto b = play_three(*mod, 1), c = play_three(*modOutside, 1);
    CHECK(!same_samples(b, c));                  // the clone's gate fires on the block's onset, not on a name it lacks
}

namespace build_tests {
// One node of `type` holding `params`, as text.
static std::string node_text(const char* id, const char* type, const std::string& params) {
    return std::string(R"({"id":")") + id + R"(","type":")" + type + R"(","params":)" + params + "}";
}
// A white noise of amplitude 0.3 and `seed`, rendered alone for `n` samples.
static std::vector<float> noise_alone(uint32_t seed, int n) {
    Patch p = load_text(plain(node_text("N", "WhiteNoiseSource", R"({"amplitude":0.3})"), "N"));
    p.graph.nodes[0].seed = seed;
    BuiltGraph g = build_graph(p.graph, nullptr, nullptr);
    return render_signal(*std::dynamic_pointer_cast<ValueSource>(g.output), n);
}
// The mean of renders, summed in order and divided as a MultiplexSource divides.
static std::vector<float> mean_of(const std::vector<std::vector<float>>& renders) {
    std::vector<float> out(renders.front().size());
    for (size_t s = 0; s < out.size(); ++s) {
        float sum = 0.0f;
        for (const auto& r : renders) sum += r[s];
        out[s] = sum / float(renders.size());
    }
    return out;
}
}  // namespace build_tests

// The seed rule, read back through each clone's noise: clone i's every node's
// seed is its own XOR-ed with base ^ (i * 0x9E3779B9), base the root's seed,
// and that value where a node has none; a template inside a template applies
// the rule again over the seeds its clone was given.
static void run_build_template_seed_tests() {
    using namespace build_tests;
    std::printf("the template's seed rule: each clone's seeds, nested included\n");
    const int n = 4800;
    const std::string three = node_text("N", "WhiteNoiseSource", R"({"amplitude":0.3,"seed":1101})") + "," +
                              node_text("M", "WhiteNoiseSource", R"({"amplitude":0.3})") + "," +
                              node_text("R", "CombinedSource", R"({"source1":{"ref":"N"},"source2":{"ref":"M"},"operation":"Sum","seed":77})");
    BuiltGraph flat = built(plain(three + "," + node_text("mux", "MultiplexSource", R"({"count":3,"source":{"ref":"R"}})"), "mux"));
    std::vector<std::vector<float>> clones;
    for (uint32_t i = 0; i < 3; ++i) {
        const uint32_t pert = 77u ^ (i * 0x9E3779B9u);
        Patch p = load_text(plain(three, "R"));
        p.graph.nodes[0].seed = 1101u ^ pert;
        p.graph.nodes[1].seed = pert;
        p.graph.nodes[2].seed = 77u ^ pert;
        BuiltGraph g = build_graph(p.graph, nullptr, nullptr);
        clones.push_back(render_signal(*std::dynamic_pointer_cast<ValueSource>(g.output), n));
    }
    CHECK(!same_samples(clones[0], clones[1]));
    CHECK(same_samples(render_signal(*std::dynamic_pointer_cast<ValueSource>(flat.output), n), mean_of(clones)));
    BuiltGraph nested = built(plain(node_text("N", "WhiteNoiseSource", R"({"amplitude":0.3,"seed":1101})") + "," +
                                    node_text("inner", "MultiplexSource", R"({"count":2,"source":{"ref":"N"},"seed":55})") + "," +
                                    node_text("outer", "MultiplexSource", R"({"count":2,"source":{"ref":"inner"}})"), "outer"));
    std::vector<std::vector<float>> outerClones;
    for (uint32_t i = 0; i < 2; ++i) {
        const uint32_t p = 55u ^ (i * 0x9E3779B9u);           // the outer root, `inner`, holds 55
        const uint32_t noiseSeed = 1101u ^ p;                 // N in outer clone i, the inner template's root
        std::vector<std::vector<float>> innerClones;
        for (uint32_t j = 0; j < 2; ++j) innerClones.push_back(noise_alone(noiseSeed ^ (noiseSeed ^ (j * 0x9E3779B9u)), n));
        outerClones.push_back(mean_of(innerClones));
    }
    CHECK(same_samples(render_signal(*std::dynamic_pointer_cast<ValueSource>(nested.output), n), mean_of(outerClones)));
}

// The closure holds copies: an instrument's Multiplex rebuilt after the
// patch, its graph and the note's holder that made it are gone renders as a
// fresh one. A dynamic pin and a tap-only tail inside a template are refused.
static void run_build_template_closure_tests() {
    using namespace build_tests;
    std::printf("the template's closure outlives its patch; the two refusals inside a template\n");
    std::unique_ptr<PitchedInstrument> kept;
    {
        const Patch p = load_text(kMultiplexTexts[0].text);
        kept = build_instrument(p);
    }
    static_cast<MultiplexSource&>(*kept->voicePool[0].nodesById.at("mux")).mark_dirty();
    kept->prepare(RenderContext{48000});
    auto fresh = build_instrument(load_text(kMultiplexTexts[0].text));
    CHECK(same_samples(play_three(*kept), play_three(*fresh)));
    CHECK(text_refusal(inst(R"({"id":"pf","type":"PerformNode"},{"id":"c","type":"SineSource"},)"
                            R"({"id":"env","type":"Envelope","dynamicPins":{"timeScale":{"ref":"c"}}},)"
                            R"({"id":"R","type":"SawSource","params":{"amplitude":{"ref":"env"},"frequency":{"ref":"pf"}}},)"
                            R"({"id":"mux","type":"MultiplexSource","params":{"source":{"ref":"R"}}})", "mux")) ==
          "node 'env' (Envelope): a dynamic pin inside the template of 'mux' is not supported: each clone would need its own");
    CHECK(text_refusal(inst(R"({"id":"T","type":"SawSource"},{"id":"Y","type":"CombinedSource","params":{"source1":{"tap":"T"}}},)"
                            R"({"id":"mux","type":"MultiplexSource","params":{"source":{"ref":"Y"}}})", "mux")) ==
          "node 'T' (SawSource): inside the template of 'mux' only a tap reads it, and nothing advances a clone's tail");
}

// A record with two nodes under one id never passed check(): each builder
// refuses it naming the second node, and none crashes.
static void run_build_duplicate_id_tests() {
    using namespace build_tests;
    std::printf("the builder refuses two nodes under one id\n");
    const auto refused = [](const auto& build) {
        try { build(); } catch (const std::exception& e) { return std::string(e.what()); }
        return std::string();
    };
    Patch env = load_text(inst(R"({"id":"x","type":"Envelope"},{"id":"y","type":"SineSource"})", "x"));
    env.graph.nodes[1].id = "x";
    const std::string want = "node 'x' (SineSource): the id 'x' names an earlier node too; an id names one node";
    CHECK(build_refusal(env.graph) == want);
    CHECK(refused([&] { (void)build_instrument(env); }) == want);
    Patch mix = load_text(plain(R"({"id":"o","type":"SineSource"},{"id":"ch","type":"SoundChannel","params":{"source":{"ref":"o"}}},)"
                                R"({"id":"m","type":"StereoMixer","params":{"channels":[{"ref":"ch"}]}},{"id":"s","type":"SawSource"})", "m"));
    mix.graph.nodes[3].id = "m";
    CHECK(refused([&] { (void)build_sound(mix); }) == "node 'm' (SawSource): the id 'm' names an earlier node too; an id names one node");
}

// The lists: the tails in the graph's order, a dynamic pin's read not a read;
// the dynamic pins in the graph's order, a node's by setting; the signals in
// id order, a PerformNode's id among them; the bend's warning.
static void run_build_list_tests() {
    using namespace build_tests;
    std::printf("the builder's lists: tails, dynamic pins, signals, the bend's warning\n");
    const auto tails = [](const std::string& nodes, const char* output) { return advance_ids(load_text(inst(nodes, output)).graph); };
    using Ids = std::vector<std::string>;
    const std::string four = R"({"id":"D0","type":"SawSource"},{"id":"D1","type":"SawSource"},{"id":"D2","type":"SawSource"},)"
        R"({"id":"D3","type":"SawSource"},{"id":"out","type":"MultiSource","params":{"source":[{"tap":"D3"},{"tap":"D2"},{"tap":"D1"},{"tap":"D0"}]}})";
    CHECK(tails(four, "out") == Ids({"D0", "D1", "D2", "D3"}));
    const BuiltGraph g4 = built(inst(four, "out"));
    CHECK(g4.advance.size() == 4);
    for (size_t k = 0; k < g4.advance.size() && k < 4; ++k) CHECK(g4.advance[k].get() == g4.nodes.at("D" + std::to_string(k)).get());
    const std::string one = R"({"id":"T","type":"SawSource"},{"id":"out","type":"CombinedSource","params":{"source1":{"tap":"T"}}})";
    CHECK(tails(one, "out") == Ids({"T"}));
    // T1, first in the file, reads L, listed after T2: T2 is built before T1,
    // and advance still holds T1 then T2.
    const BuiltGraph late = built(inst(R"({"id":"T1","type":"SawSource","params":{"amplitude":{"ref":"L"}}},)"
        R"({"id":"T2","type":"SawSource"},{"id":"L","type":"SineSource"},)"
        R"({"id":"out","type":"MultiSource","params":{"source":[{"tap":"T2"},{"tap":"T1"}]}})", "out"));
    CHECK(late.advance.size() == 2 && late.advance[0].get() == late.nodes.at("T1").get() &&
          late.advance[1].get() == late.nodes.at("T2").get());
    CHECK(tails(R"({"id":"out","type":"SawSource"})", "out").empty());
    CHECK(tails(R"({"id":"T","type":"SawSource","params":{"amplitude":{"tap":"T"}}})", "T").empty());
    CHECK(tails(R"({"id":"T","type":"SawSource"},{"id":"env","type":"Envelope","dynamicPins":{"timeScale":{"ref":"T"}}},)"
                R"({"id":"out","type":"CombinedSource","params":{"source1":{"tap":"T"},"source2":{"ref":"env"}}})", "out") == Ids({"T"}));
    const BuiltGraph pins = built(inst(
        R"({"id":"eB","type":"Envelope","dynamicPins":{"timeScale":{"ref":"late"}}},{"id":"s","type":"SineSource"},)"
        R"({"id":"eA","type":"Envelope","dynamicPins":{"timeScale":{"ref":"s"},"sustainLevel":{"ref":"s"}}},)"
        R"({"id":"late","type":"SineSource"},{"id":"out","type":"CombinedSource","params":{"source1":{"ref":"eA"},"source2":{"ref":"eB"}}})", "out"));
    const auto pin_is = [&](size_t k, const char* id, const char* setting, const char* source) {
        return k < pins.dynamicPins.size() && pins.dynamicPins[k].node == pins.nodes.at(id) && pins.dynamicPins[k].setting == setting &&
               pins.dynamicPins[k].source.get() == pins.nodes.at(source).get();
    };
    CHECK(pins.dynamicPins.size() == 3 && pin_is(0, "eB", "timeScale", "late") && pin_is(1, "eA", "sustainLevel", "s") &&
          pin_is(2, "eA", "timeScale", "s"));
    const BuiltGraph sig = built(inst(R"({"id":"z","type":"SineSource","params":{"frequency":{"ref":"pf"}}},{"id":"pf","type":"PerformNode"},)"
                                      R"({"id":"parts","type":"FullPartials"},{"id":"a","type":"SawSource"})", "z"));
    CHECK(sig.signals.size() == 3 && sig.signals[0].id == "a" && sig.signals[1].id == "pf" && sig.signals[2].id == "z");
    if (sig.signals.size() == 3) CHECK(sig.signals[1].node == sig.nodes.at("pf").get());
    const auto ks = [](const std::string& frequency, const char* field) {
        return built(inst(std::string(R"({"id":"pf","type":"PerformNode","params":{"field":")") + field + R"("}},)"
                          R"({"id":"x2","type":"CombinedSource","params":{"source1":{"ref":"pf"},"source2":2.0,"operation":"Multiply"}},)"
                          R"({"id":"exc","type":"WhiteNoiseSource"},)"
                          R"({"id":"ks","type":"KSString","params":{"source":{"ref":"exc"},"frequency":)" + frequency + "}}", "ks")).warnings;
    };
    const Ids bend{"node 'ks' (KSString): it reads its frequency once at note start, so a bend does not reach it"};
    CHECK(ks(R"({"ref":"pf"})", "frequency") == bend);
    CHECK(ks(R"({"ref":"x2"})", "frequency") == bend);
    CHECK(ks("220", "frequency").empty());
    CHECK(ks(R"({"ref":"pf"})", "velocity").empty());
}

// build_instrument: the block's five fields, the floor, every voice's lists,
// the warnings once; a part as the output and a patch with no block refused.
static void run_build_instrument_tests() {
    using namespace build_tests;
    std::printf("build_instrument: the block, the floor, each voice's lists, the warnings once\n");
    const Patch p = load_text(inst(
        R"({"id":"pf","type":"PerformNode"},{"id":"g","type":"NameGate","params":{"name":"x"}},)"
        R"({"id":"env","type":"Envelope","params":{"trigger":1}},{"id":"env0","type":"Envelope"},)"
        R"({"id":"envW","type":"Envelope","params":{"trigger":{"ref":"g"}}},{"id":"T","type":"SineSource"},)"
        R"({"id":"c","type":"SineSource"},{"id":"ks","type":"KSString","dynamicPins":{"t60":{"ref":"c"}},"params":{"frequency":{"ref":"pf"}}},)"
        R"({"id":"m","type":"CombinedSource","params":{"source1":{"ref":"env"},"source2":{"ref":"env0"},"operation":"Multiply"}},)"
        R"({"id":"m2","type":"CombinedSource","params":{"source1":{"ref":"m"},"source2":{"ref":"envW"},"operation":"Multiply"}},)"
        R"({"id":"m3","type":"CombinedSource","params":{"source1":{"ref":"m2"},"source2":{"tap":"T"},"operation":"Sum"}},)"
        R"({"id":"out","type":"CombinedSource","params":{"source1":{"ref":"m3"},"source2":{"ref":"ks"},"operation":"Sum"}})",
        "out", R"({"polyphony":3,"volume":0.5,"sustaining":true,"onsets":["tongue"],"glideMs":20})"));
    std::vector<std::string> w;
    const auto pi = build_instrument(p, 0, &w);
    CHECK(pi->voicePool.size() == 3 && pi->volume == 0.5f && pi->instrument_class().sustaining && pi->glideSec == 20.0f / 1000.0f);
    CHECK(pi->instrument_class().onsets == std::vector<std::string>({"tongue"}));
    CHECK(w.size() == 2);                        // the gate's name and the bend: once, not once per voice
    CHECK(build_instrument(p, 5)->voicePool.size() == 5 && build_instrument(p, 2)->voicePool.size() == 3);
    for (const auto& vg : pi->voicePool) {
        const auto at = [&](const char* id) { return vg.nodesById.at(id).get(); };
        CHECK(vg.source.get() == at("out"));
        CHECK(vg.allEnvelopes.size() == 3 && vg.allEnvelopes[0] == at("env") && vg.allEnvelopes[1] == at("env0") &&
              vg.allEnvelopes[2] == at("envW"));
        CHECK(vg.triggerBindings.size() == 2 && vg.triggerBindings[0] == at("env") && vg.triggerBindings[1] == at("envW"));
        CHECK(vg.pushBindings.size() == 1 && vg.pushBindings[0].consumer.get() == at("ks") &&
              vg.pushBindings[0].paramName == "t60" && vg.pushBindings[0].chain.get() == at("c"));
        CHECK(vg.advanceList.size() == 1 && vg.advanceList[0].get() == at("T"));
        CHECK(vg.signals.size() == vg.nodesById.size() && std::is_sorted(vg.signals.begin(), vg.signals.end(),
                                                                         [](const auto& x, const auto& y) { return x.id < y.id; }));
        CHECK(at("pf") == vg.freqOut.get() && vg.performSource != nullptr);
    }
    CHECK(pi->voicePool[0].nodesById.at("out") != pi->voicePool[1].nodesById.at("out"));
    Patch partOut = load_text(inst(R"({"id":"o","type":"SineSource"},{"id":"parts","type":"FullPartials"})", "o"));
    partOut.graph.output = "parts";
    std::string msg;
    try { (void)build_instrument(partOut); } catch (const std::exception& e) { msg = e.what(); }
    CHECK(msg == "'parts' is a FullPartials, a part, not a signal");
    msg.clear();
    try { (void)build_instrument(load_text(plain(R"({"id":"o","type":"SineSource"})", "o"))); } catch (const std::exception& e) { msg = e.what(); }
    CHECK(msg == "Patch has no 'instrument' section");
    const Patch slow = load_text(R"({"sampleRate":22050,"instrument":{"polyphony":1},)"
                                 R"("graph":{"output":"o","nodes":[{"id":"o","type":"SineSource"}]}})");
    CHECK(build_instrument(slow)->voicePool[0].ctx.sampleRate == 22050);
}

// build_sound: the base's seven plain-sound hashes; the sharing refusal's
// shapes with their sentences and the shapes that load; the bare output; the
// instrument form and the frame count's three cases; a tail in a plain sound
// and a faulting node, each named by id.
static void run_build_sound_tests() {
    using namespace build_tests;
    std::printf("build_sound: the base's plain sounds, the sharing walk, the three forms, the frames\n");
    const uint64_t kBase[] = {0xbd803f232d11c7d6ull, 0x428f79ca36025021ull, 0x6a4b98ef60321f25ull, 0x6da350faa0c174a3ull,
                              0x8f96948dd16fb3c3ull, 0xbe932bc542335611ull, 0x94a4b14bfce0b145ull};
    const auto all = plain_parts::cases();
    CHECK(all.size() == std::size(kBase));
    for (size_t k = 0; k < all.size() && k < std::size(kBase); ++k) {
        BuiltSound s = build_sound(load_text(plain_parts::sound(all[k].signals, all[k].chans, all[k].mix)));
        const uint64_t h = node_twins::fnv(render_sound(s));
        if (h != kBase[k]) std::printf("  %s: 0x%016llx\n", all[k].name, (unsigned long long)h);
        CHECK(h == kBase[k]);
    }
    const auto refusal = [](const std::string& text) {
        try { (void)build_sound(load_text(text)); } catch (const std::exception& e) { return std::string(e.what()); }
        return std::string();
    };
    for (const auto& c : plain_parts::shared_cases()) {
        const std::string got = refusal(c.text);
        if (got != c.sentence) std::printf("  %s: %s\n", c.what, got.c_str());
        CHECK(got == c.sentence);
    }
    for (const auto& text : plain_parts::sharing_sounds_that_load()) {
        BuiltSound s = build_sound(load_text(text));
        const auto buf = render_sound(s);
        CHECK(std::any_of(buf.begin(), buf.end(), [](float v) { return v != 0.0f; }));
    }
    BuiltSound bare = build_sound(load_text(plain(R"({"id":"osc","type":"SineSource","params":{"frequency":220}})", "osc")));
    CHECK(bare.mixer->channels.size() == 1 && bare.frames == 12000 && bare.mixer->signals.size() == 1 &&
          bare.mixer->signals[0].id == "osc");
    const std::string head = R"({"instrument":{"polyphony":1},)";
    const std::string tail = R"("graph":{"output":"osc","nodes":[{"id":"osc","type":"SineSource","params":{"frequency":220}}]}})";
    const std::string score = R"("score":[{"note":60,"time":0,"duration":0.5}],)";
    const std::string forms[] = {head + tail, head + score + tail, head + score + R"("seconds":0.25,)" + tail,
                                 head + score + R"("seconds":2,)" + tail, head + R"("seconds":0.25,)" + tail};
    const int frames[] = {240000, 24000, 24000, 96000, 12000};
    for (size_t k = 0; k < std::size(forms); ++k) {
        BuiltSound mod = build_sound(load_text(forms[k]));
        if (mod.frames != frames[k]) std::printf("  form %zu: %d frames, want %d\n", k, mod.frames, frames[k]);
        CHECK(mod.sampleRate == 48000 && mod.frames == frames[k]);
    }
    BuiltSound tailed = build_sound(load_text(plain(R"({"id":"T","type":"SawSource"},)"
        R"({"id":"out","type":"CombinedSource","params":{"source1":{"tap":"T"}}})", "out")));
    CHECK(tailed.warnings.size() == 1 && tailed.warnings[0].rfind("node 'T' is read only through a tap", 0) == 0);
    BuiltSound faulty = build_sound(load_text(R"J({"sampleRate":48000,"graph":{"nodes":[{"id":"zero","type":"SawSource","params":{"amplitude":0.3,"frequency":0.0,"phase":0.0}},{"id":"tone","type":"SineSource","params":{"amplitude":0.3,"frequency":220.0,"phase":0.0}},{"id":"chZ","type":"SoundChannel","params":{"volume":0.8,"pan":-0.5,"source":{"ref":"zero"}}},{"id":"chT","type":"SoundChannel","params":{"volume":0.8,"pan":0.5,"source":{"ref":"tone"}}},{"id":"mix","type":"StereoMixer","params":{"gainL":1.0,"gainR":1.0,"channels":[{"ref":"chZ"},{"ref":"chT"}]}}],"output":"mix"}})J"));
    (void)render_sound(faulty, 4800);
    std::vector<FaultReport> faults;
    faulty.mixer->collect_faults(faults);
    CHECK(faults.size() == 1 && faults[0].nodeId == "zero");
    if (!faults.empty()) std::printf("  the faulting node: %s\n", fault_report_text(faults[0]).c_str());
}

// ---------------------------------------------------------------------------
// The generated reader twin: for every registered type, three small patches
// the declarations table writes, read by both readers.
namespace reader_twin {
using J = nlohmann::json;
using patch_door::difference;
using patch_door::live_c4;
using patch_door::read_text;
using patch_door::render_sound;
using patch_door::same_samples;
// The three texts per type: the sparse one; one whose arrays all hold rows
// and whose Params the text does not feed all hold a number off their
// defaults; one whose scalar settings all stand off their defaults.
enum class TwinText { Sparse, Rows, Settings };
// The settings and Params whose usual value makes the quarter-second render
// too slow or too large, and the value each takes instead, off its default:
// a Partials' count of partials the midpoint of its range; an ExpandRule's
// two sizes one each, since a rule makes (1 + 2 * count) to the power
// (1 + recurse) partials of every one its host has.
static const float* chosen_value(const std::string& type, const std::string& setting) {
    static const std::map<std::pair<std::string, std::string>, float> kChosen = {
        {{"FullPartials", "maxPartials"}, 100.0f},
        {{"SequencePartials", "maxPartials"}, 100.0f},
        {{"ExplicitPartials", "maxPartials"}, 100.0f},
        {{"ExpandRule", "count"}, 1.0f},
        {{"ExpandRule", "recurse"}, 1.0f},
    };
    const auto it = kChosen.find({type, setting});
    return it == kChosen.end() ? nullptr : &it->second;
}
// The first registered type whose slot `slot` of an owner takes; empty when none.
static std::string first_accepted(const TypeDeclarations::Input& slot) { return slot.accepts.empty() ? "" : slot.accepts.front(); }
static void add_node(J& nodes, const std::string& id, const std::string& type, bool note, int depth,
                     TwinText text = TwinText::Sparse);
// A node's pins: each signal input fed the saw, each slot but `skip` the first
// type it accepts (made by add_node, to a depth of three), `frequency` the
// note where the type has the pin and the patch has a note.
static J pins(J& nodes, const std::string& id, const TypeDeclarations& d, bool note, int depth, const std::string& skip = "") {
    J params = J::object();
    for (const auto& in : d.inputs) {
        if (in.name == skip) continue;
        if (!in.slot) { params[in.name] = in.list ? J::array({J{{"ref", "drive"}}}) : J{{"ref", "drive"}}; continue; }
        const std::string part = first_accepted(in);
        if (part.empty() || depth >= 3) continue;
        const std::string partId = id + "_" + in.name;
        add_node(nodes, partId, part, note, depth + 1);
        params[in.name] = in.list ? J::array({J{{"ref", partId}}}) : J{{"ref", partId}};
    }
    if (d.param("frequency") && note) params["frequency"] = J{{"ref", "pf"}};
    return params;
}
// Three rows of every array: its row default, or a labelled column's first label.
static void give_rows(J& params, const TypeDeclarations& d) {
    for (const auto& a : d.arrays)
        params[a.name] = a.labels.empty() ? J::array({a.rowDefault, a.rowDefault, a.rowDefault})
                                          : J::array({a.labels.front(), a.labels.front(), a.labels.front()});
}
// Every scalar setting off its default and inside its range: a number its
// maximum (or the value chosen_value gives it), an enum its last label, a
// bool the other value.
static void give_settings(J& params, const TypeDeclarations& d) {
    for (const auto& s : d.settings) {
        if (s.type == SettingType::String) continue;
        if (s.type == SettingType::Bool) params[s.name] = s.defaultValue == 0.0f;
        else if (!s.labels.empty()) params[s.name] = s.labels.back();
        else if (const float* chosen = chosen_value(d.type, s.name)) params[s.name] = *chosen;
        else params[s.name] = s.max;
    }
}
// Every declared Param the text does not feed a number off its default and
// inside its range (or the value chosen_value gives it): a quarter of the way
// to its maximum, or to its minimum where the default is the maximum. A
// Param whose range is one value keeps its default.
static void give_params(J& params, const TypeDeclarations& d) {
    for (const auto& p : d.params) {
        if (params.contains(p.name) || p.min == p.max) continue;
        if (const float* chosen = chosen_value(d.type, p.name)) params[p.name] = *chosen;
        else if (p.defaultValue == p.max) params[p.name] = p.defaultValue - (p.defaultValue - p.min) / 4.0f;
        else params[p.name] = p.defaultValue + (p.max - p.defaultValue) / 4.0f;
    }
}
static void add_node(J& nodes, const std::string& id, const std::string& type, bool note, int depth, TwinText text) {
    const TypeDeclarations* d = type_declarations(type);
    J params = pins(nodes, id, *d, note, depth);
    if (text == TwinText::Rows) {
        give_rows(params, *d);
        give_params(params, *d);
    }
    if (text == TwinText::Settings) give_settings(params, *d);
    nodes.push_back(J{{"id", id}, {"type", type}, {"params", params}});
}
// The first registered type with a slot that takes `part`, and that slot; empty when none.
static std::pair<std::string, std::string> host_of(const std::string& part) {
    for (const auto& t : declared_types())
        for (const auto& in : type_declarations(t)->inputs)
            if (in.slot && std::find(in.accepts.begin(), in.accepts.end(), part) != in.accepts.end()) return {t, in.name};
    return {};
}
}  // namespace reader_twin

namespace reader_twin {
// The text for `type`: an instrument whose output is the node `x` of that
// type, or the signal that hosts it in a slot (a part's host chain followed
// until a signal, each host's other slots filled too); a plain sound when the
// chain ends at the mixer. Empty, with `why` said, when no slot takes the part.
static std::string twin_text(const std::string& type, TwinText text, std::string& why) {
    std::vector<std::pair<std::string, std::string>> hosts;   // each host's type and the slot it holds the one before in
    std::string at = type;
    while (!type_declarations(at)->signal && at != "StereoMixer" && hosts.size() < 3) {
        const auto host = host_of(at);
        if (host.first.empty()) { why = "no registered type's slot takes a " + at; return {}; }
        hosts.push_back(host);
        at = host.first;
    }
    if (!type_declarations(at)->signal && at != "StereoMixer") { why = "no signal hosts it within three slots"; return {}; }
    const bool instrument = at != "StereoMixer";
    J nodes = J::array();
    if (instrument) nodes.push_back(J{{"id", "pf"}, {"type", "PerformNode"}, {"params", {{"field", "frequency"}}}});
    nodes.push_back(J{{"id", "drive"}, {"type", "SawSource"},
                      {"params", {{"amplitude", 0.3}, {"frequency", instrument ? J{{"ref", "pf"}} : J(261.6256)}}}});
    add_node(nodes, "x", type, instrument, 0, text);
    std::string inner = "x";
    for (size_t k = 0; k < hosts.size(); ++k) {
        const std::string id = "h" + std::to_string(k);
        const TypeDeclarations* d = type_declarations(hosts[k].first);
        J params = pins(nodes, id, *d, instrument, 1, hosts[k].second);
        params[hosts[k].second] = d->input(hosts[k].second)->list ? J::array({J{{"ref", inner}}}) : J{{"ref", inner}};
        nodes.push_back(J{{"id", id}, {"type", hosts[k].first}, {"params", params}});
        inner = id;
    }
    J doc{{"sampleRate", 48000}, {"graph", {{"output", inner}, {"nodes", nodes}}}};
    if (instrument) doc["instrument"] = J{{"polyphony", 1}};
    else doc["seconds"] = 0.25;
    return doc.dump();
}
// One render of `text` through the module: a C4 of an instrument, the mix of a sound.
static std::vector<float> twin_render(const std::string& text) {
    const Patch p = read_text(text);
    if (p.instrument) return live_c4(*build_instrument(p));
    BuiltSound s = build_sound(p);
    return render_sound(s);
}
// What one text of one type gave: the module's refusal, and what the
// properties found (empty fields where they hold).
struct TwinResult {
    std::string unbuilt, refusal, sparseVsWritten, identity;
    bool audible{false};
    double seconds{0.0};
};
// The module's render of `text`, or its refusal.
static std::vector<float> render_or_refusal(const std::string& text, std::string& refusal) {
    try { return twin_render(text); } catch (const std::exception& e) { refusal = e.what(); }
    return {};
}
// The types whose text is refused by design: probes that refuse as built,
// and the probe whose refusal is a setting the settings text turns on.
static bool refused_by_design(const std::string& type, TwinText kind) {
    return type == "RefusingProbe" || (type == "CheckProbe" && kind == TwinText::Settings);
}
static TwinResult twin_one(const std::string& type, TwinText kind) {
    TwinResult r;
    const std::string text = twin_text(type, kind, r.unbuilt);
    if (text.empty()) return r;
    const auto start = std::chrono::steady_clock::now();
    const auto mod = render_or_refusal(text, r.refusal);
    r.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    if (!r.refusal.empty()) return r;
    r.audible = std::any_of(mod.begin(), mod.end(), [](float s) { return s != 0.0f; });
    const Patch p = read_text(text);
    J written;
    to_json(written, p);
    Patch back;
    from_json(written, back);
    if (!(back == p)) r.identity = "from_json(to_json(p)) differs from p";
    if (kind == TwinText::Settings) return r;
    const auto again = twin_render(written.dump());
    if (!same_samples(mod, again)) r.sparseVsWritten = difference(mod, again);
    return r;
}
// One text over every registered type: each finding printed and failed, a
// refused type listed with its sentence and failed unless refused by design,
// the counts printed.
static void twin_all(TwinText kind, const char* name) {
    int types = 0, built = 0, audible = 0;
    std::vector<std::string> silent;
    for (const auto& type : declared_types()) {
        ++types;
        const TwinResult r = twin_one(type, kind);
        const char* t = type.c_str();
        if (!r.unbuilt.empty()) std::printf("  %s %s: no text: %s\n", name, t, r.unbuilt.c_str());
        const bool refused = !r.refusal.empty();
        if (refused) std::printf("  %s %s: refused: \"%s\"\n", name, t, r.refusal.c_str());
        if (!r.sparseVsWritten.empty()) std::printf("  %s %s: the text and the written text differ, %s\n", name, t, r.sparseVsWritten.c_str());
        if (!r.identity.empty()) std::printf("  %s %s: %s\n", name, t, r.identity.c_str());
        if (r.seconds > 2.0) std::printf("  %s %s: took %.1f s\n", name, t, r.seconds);
        CHECK(refused == refused_by_design(type, kind) && r.sparseVsWritten.empty() && r.identity.empty());
        if (!r.unbuilt.empty() || refused) continue;
        ++built;
        if (r.audible) ++audible;
        else silent.push_back(type);
    }
    std::printf("  %s: %d types, %d built, %d render something, %zu render silence:", name, types, built, audible, silent.size());
    for (const auto& s : silent) std::printf(" %s", s.c_str());
    std::printf("\n");
    CHECK(types > 0);
}
}  // namespace reader_twin

// For every registered type, three texts the declarations table writes: the
// sparse one; one whose every array holds rows and whose every Param the
// text does not feed holds a number off its default; one whose every scalar
// setting stands off its default. Of each: (i) the module builds it (a probe
// refused by design aside) and renders a C4 of a quarter second, and (ii)
// from_json(to_json(p)) == p; of the first two also (iii) the module renders
// the text and the text of to_json(from_json(text)) the same.
static void run_generated_reader_twin_tests() {
    using namespace reader_twin;
    std::printf("the generated twin: every registered type through the module, three texts\n");
    twin_all(TwinText::Sparse, "sparse");
    twin_all(TwinText::Rows, "rows");
    twin_all(TwinText::Settings, "settings");
}

int main() {
    run_alloc_probe_tests();
    run_allocation_tests_partials();
    run_allocation_tests_tables();
    run_allocation_tests_nodes();
    run_allocation_tests_pins();
    run_structural_setting_tests();
    run_structural_push_refusal_tests();
    run_param_map_refusal_tests();
    run_retired_key_refusal_tests();
    run_dynamic_pin_tests();
    run_wiring_door_tests();
    run_formant_part_tests();
    run_segment_walk_twin_tests();
    run_coherent_walk_twin_tests();
    run_pair_trade_twin_tests();
    run_bandwidth_noise_twin_tests();
    run_onset_gate_twin_tests();
    run_decay_law_twin_tests();
    run_partial_layout_twin_tests();
    run_partial_set_lifecycle_tests();
    run_driven_part_lifecycle_tests();
    run_formant_peaks_twin_tests();
    run_formant_peaks_load_tests();
    run_formant_bands_twin_tests();
    run_formant_sequence_twin_tests();
    run_formant_factor_twin_tests();
    run_formant_lifecycle_tests();
    run_partials_part_tests();
    run_component_root_tests();
    run_target_evolution_registry_tests();
    run_registry_family_tests();   // before the lifecycle test registers its probe
    run_registry_shelf_none_tests();
    run_menu_content_tests();
    run_review_gap_tests();
    run_channel_source_tests();
    run_channel_clock_tests();
    run_instrument_mixer_unity_tests();
    run_output_part_tests();
    run_plain_part_twin_tests();
    run_plain_part_build_tests();
    run_instrument_holding_parts_tests();
    run_plain_part_refusal_tests();
    run_plain_sharing_refusal_tests();
    run_plain_sharing_load_tests();
    run_plain_sharing_multiplex_tests();
    run_retired_spelling_table_tests();
    run_retired_spelling_load_tests();
    run_perform_node_field_tests();
    run_perform_node_type_tests();
    run_preset_curve_label_tests();
    run_review_gap_lifecycle_tests();
    run_enum_column_tests();       // after the registry's tests: it registers its probe
    run_string_setting_tests();
    run_string_setting_loader_tests();   // these four register their probes, as the enum test does
    run_input_list_tests();
    run_input_list_loader_tests();
    run_validate_tests();
    run_pin_tests();
    run_base_tests();
    run_prepare_start_tests();
    run_manual_pin_next_tests();
    run_setting_clamp_tests();
    run_manual_pin_prepare_tests();
    run_processor_manual_input_tests();
    run_wave_source_tests();
    run_range_var_tests();
    run_multi_name_gate_tests();
    run_envelope_pin_tests();
    run_envelope_rebuild_pin_tests();
    run_noise_pin_tests();
    run_noise_white_tests();
    run_noise_default_tests();
    run_noise_red_pin_tests();
    run_noise_manual_amplitude_tests();
    run_noise_layered_tests();
    run_shaped_draw_twin_tests();
    run_wavetable_pin_tests();
    run_wavetable_evolution_tests();
    run_string_pin_refusal_tests();
    run_envelope_factory_tests();
    run_envelope_form_tests();
    run_envelope_form_loader_tests();
    run_envelope_table_tests();
    run_curve_table_tests();
    run_retired_type_message_tests();
    run_hybrid_ks_tests();
    run_evolution_shape_tests();
    run_evolution_setting_tests();
    run_evolution_manual_pin_tests();
    run_evolution_lifecycle_tests();
    run_evolution_live_read_tests();
    run_target_evolution_tests();
    run_target_evolution_morph_tests();
    run_additive_source_pin_tests();
    run_additive_structural_pin_tests();
    run_additive_partials_pin_tests();
    run_additive_smoke_tests();
    run_additive_manual_pin_tests();
    run_additive_formant_pin_tests();
    run_additive_wiring_tests();
    run_additive_setting_hook_tests();
    run_additive_explicit_array_tests();
    run_physical_pin_shape_tests();
    run_physical_auto_pin_tests();
    run_physical_advance_order_tests();
    run_physical_setting_hook_tests();
    run_physical_mesh_hook_tests();
    run_physical_behavior_tests();
    run_filter_tests();
    run_combine_mod_tests();
    run_memo_tests();
    run_previous_tests();
    run_tap_reading_tests();
    run_no_throw_tests();
    run_fault_tests();
    run_wire_rejection_tests();
    run_shared_source_tests();
    run_sweep_follows_wiring_tests();
    run_tap_reads_live_tests();
    run_walk2_tests();
    run_walk3_tests();
    run_walk1_tests();
    run_passage_melody_tests();
    run_passage_parse_tests();
    run_onset_field_tests();
    run_hold_delivery_tests();
    run_note_wire_tests();
    run_multiplex_bend_tests();
    run_note_reseed_tests();
    run_embedded_score_tests();
    run_embedded_score_end_tests();
    run_embedded_score_delivery_tests();
    run_embedded_score_text_tests();
    run_voice_pool_tests();
    run_voice_gain_tests();
    run_envelope_retrigger_tests();
    run_release_relayout_tests();
    run_name_gate_tests();
    run_onset_trigger_tests();
    run_curve_node_tests();
    run_curve_expr_tests();
    run_envelope_range_tests();
    run_stage_nominal_tests();
    run_perform_source_tests();
    run_tap_guard_tests();
    run_tap_cycle_tests();
    run_delay_line_tests();
    run_phase_delay_tests();
    run_loop_compensation_tests();
    run_shaper_tests();
    run_curve_shared_tests();
    run_ramp_hold_tests();
    run_shaper_seg_tests();
    run_shaper_morph_tests();
    run_wormhole_tests();
    run_pierce_passivity_tests();
    run_dsp_scaffold_tests();
    run_dsp_blep_tests();
    run_dsp_dc_blocker_tests();
    run_dsp_tuning_allpass_tests();
    run_dsp_tuning_allpass_boundary_tests();
    run_dsp_one_pole_tests();
    run_dsp_bow_friction_tests();
    run_dsp_biquad_tests();
    run_dsp_biquad_recipe_tests();
    run_dsp_butterworth_tests();
    run_dsp_reset_tests();
    run_dsp_performer_smoother_tests();
    run_bw_section_twin_tests();
    run_hybrid_target_twin_tests();
    run_static_pair_twin_tests();
    run_vibrato_twin_tests();
    run_registry_built_twin_tests();
    run_name_gate_name_tests();
    run_shaper_validate_tests();
    run_declared_default_load_tests();
    run_multi_list_twin_tests();
    run_phased_list_twin_tests();
    run_expand_rule_slot_tests();
    run_list_node_descriptor_tests();
    run_list_node_loader_tests();
    run_expand_rule_loader_tests();
    run_retired_form_table_tests();
    run_retired_form_load_tests();
    run_feed_shape_refusal_tests();
    run_declared_reader_tests();
    run_branch_key_tests();
    run_gain_adj_range_tests();
    run_evolution_unclamped_setting_tests();
    run_trigger_value_tests();
    run_multiplex_builder_tests();
    run_multiplex_template_change_tests();
    run_mixer_signal_fault_tests();
    run_patch_record_tests();
    run_type_declaration_identity_tests();   // the table's first ask: before any other test reads it
    run_type_declaration_field_tests();
    run_type_declaration_accept_tests();
    run_type_declaration_default_tests();
    run_type_declaration_prototype_tests();
    run_patch_check_tests();
    patch_codec::guarded("run_codec_identity_tests", run_codec_identity_tests);
    patch_codec::guarded("run_codec_complete_model_tests", run_codec_complete_model_tests);
    patch_codec::guarded("run_codec_float_tests", run_codec_float_tests);
    patch_codec::guarded("run_codec_kind_tests", run_codec_kind_tests);
    patch_codec::guarded("run_codec_preset_tests", run_codec_preset_tests);
    patch_codec::guarded("run_codec_passenger_tests", run_codec_passenger_tests);
    patch_codec::guarded("run_codec_layout_fragment_tests", run_codec_layout_fragment_tests);
    patch_codec::guarded("run_codec_refusal_tests", run_codec_refusal_tests);
    patch_codec::guarded("run_codec_untouched_tests", run_codec_untouched_tests);
    patch_codec::guarded("run_build_sort_tests", run_build_sort_tests);
    patch_codec::guarded("run_build_fill_tests", run_build_fill_tests);
    patch_codec::guarded("run_build_named_tests", run_build_named_tests);
    patch_codec::guarded("run_build_template_reader_tests", run_build_template_reader_tests);
    patch_codec::guarded("run_build_template_seed_tests", run_build_template_seed_tests);
    patch_codec::guarded("run_build_template_closure_tests", run_build_template_closure_tests);
    patch_codec::guarded("run_build_duplicate_id_tests", run_build_duplicate_id_tests);
    patch_codec::guarded("run_build_list_tests", run_build_list_tests);
    patch_codec::guarded("run_build_instrument_tests", run_build_instrument_tests);
    patch_codec::guarded("run_build_sound_tests", run_build_sound_tests);
    patch_codec::guarded("run_generated_reader_twin_tests", run_generated_reader_twin_tests);
    run_type_declaration_late_tests();       // registers its probe: last
    if (g_fails) { std::printf("%d/%d FAILED\n", g_fails, g_checks); return 1; }
    std::printf("ALL PASS (%d checks)\n", g_checks);
    return 0;
}
