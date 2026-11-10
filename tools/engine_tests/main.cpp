// Assert-style engine unit tests (PerformSource P1, plan_perform_source_p1.md).
// House testing is render-based; this exe covers the value-level semantics
// renders can't isolate (curve interp identity, envelope mapping, adapters).
#include <cstdio>
#include <cmath>
#include <cstdlib>
#include "alloc_probe.h"

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
        if (hp) node = std::make_unique<BWHighpassFilter>(2); else node = std::make_unique<BWLowpassFilter>(2);
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

static void run_curve_node_tests() {
    // Linear (vcurve semantics): clamp + linear interp
    CurveNode lin;
    lin.knots = {{0.0f, 0.3f}, {0.2f, 0.3f}, {0.85f, 1.0f}, {1.0f, 1.6f}};
    CHECK_NEAR(lin.map(-1.0f), 0.3f, 1e-6f);   // clamp low
    CHECK_NEAR(lin.map(0.1f),  0.3f, 1e-6f);   // flat segment
    CHECK_NEAR(lin.map(1.0f),  1.6f, 1e-6f);   // clamp high
    CHECK_NEAR(lin.map(0.925f), 1.3f, 1e-3f);  // midpoint of last segment

    // LogX (default paramMap curve semantics): value linear in log(x)
    CurveNode logx; logx.interp = CurveNode::CurveInterp::LogX;
    logx.knots = {{100.0f, 0.0f}, {10000.0f, 2.0f}};
    CHECK_NEAR(logx.map(1000.0f), 1.0f, 1e-4f);  // geometric midpoint

    // LogLog: 2 knots == exact power law (y = x^2 through (10,100),(100,10000))
    CurveNode ll; ll.interp = CurveNode::CurveInterp::LogLog;
    ll.knots = {{10.0f, 100.0f}, {100.0f, 10000.0f}};
    CHECK_NEAR(ll.map(31.6227766f), 1000.0f, 0.5f);

    // Pull path: source wired, evaluates on change
    auto cs = std::make_shared<ConstantSource>(0.5f);
    CurveNode wired;
    wired.knots = {{0.0f, 0.0f}, {1.0f, 10.0f}};
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
    CurveNode kt; kt.exprMode = true;
    kt.exprKnots = {{440.0f, KF::Linear, 8.0f, 0.0f}};
    CHECK_NEAR(kt.map(100.0f),  800.0f,   1e-3f);
    CHECK_NEAR(kt.map(440.0f),  3520.0f,  1e-2f);
    CHECK_NEAR(kt.map(5000.0f), 40000.0f, 1e-1f);

    // Matt's 3-point example: flat 0.1 low, 0.1 - x/50000 high, smooth
    // morph between; equal formulas on the top knots pin the formula exactly
    // over [3200,16000] and keep extrapolating above.
    CurveNode m; m.exprMode = true;
    m.exprKnots = {{20.0f,    KF::Linear, 0.0f,      0.1f},
                   {3200.0f,  KF::Linear, -2e-5f,    0.1f},
                   {16000.0f, KF::Linear, -2e-5f,    0.1f}};
    CHECK_NEAR(m.map(10.0f),     0.1f,    1e-6f);   // edge: constant holds
    CHECK_NEAR(m.map(8000.0f),  -0.06f,   1e-5f);   // formula exact mid-span
    CHECK_NEAR(m.map(20000.0f), -0.3f,    1e-5f);   // extrapolates past last
    // Blend region: t = (1610-20)/3180 = 0.5, v0 = 0.1, v1 = 0.0678
    CHECK_NEAR(m.map(1610.0f),   0.0839f, 1e-4f);

    // Power form: y = 2 * x^0.5
    CurveNode pw; pw.exprMode = true;
    pw.exprKnots = {{100.0f, KF::Power, 2.0f, 0.5f}};
    CHECK_NEAR(pw.map(100.0f),  20.0f, 1e-4f);
    CHECK_NEAR(pw.map(2500.0f), 100.0f, 1e-3f);

    // exprMode off leaves points behavior untouched even with exprKnots set
    CurveNode off;
    off.exprKnots = {{440.0f, KF::Linear, 8.0f, 0.0f}};
    off.knots = {{0.0f, 0.0f}, {1.0f, 10.0f}};
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
    CurveNode cn; cn.interp = CurveNode::CurveInterp::LogX;
    cn.knots = {{100.0f, 0.0f}, {10000.0f, 2.0f}};
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
    lin.knots = {{0.0f, 0.3f}, {0.2f, 0.3f}, {0.85f, 1.0f}, {1.0f, 1.6f}};
    for (float x : {-1.0f, 0.0f, 0.1f, 0.2f, 0.5f, 0.925f, 1.0f, 2.0f})
        CHECK_NEAR(Curve::eval(lin.knots, {}, Curve::Domain::Linear, 0.5f, x),
                   lin.map(x), 1e-7f);

    // Parity vs CurveNode::map — LogX and LogLog domains
    CurveNode logx; logx.interp = CurveNode::CurveInterp::LogX;
    logx.knots = {{100.0f, 0.0f}, {10000.0f, 2.0f}};
    CHECK_NEAR(Curve::eval(logx.knots, {}, Curve::Domain::LogX, 0.5f, 1000.0f),
               logx.map(1000.0f), 1e-6f);
    CurveNode ll; ll.interp = CurveNode::CurveInterp::LogLog;
    ll.knots = {{10.0f, 100.0f}, {100.0f, 10000.0f}};
    CHECK_NEAR(Curve::eval(ll.knots, {}, Curve::Domain::LogLog, 0.5f,
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

#include "mforce/render/patch_loader.h"
#include "mforce/render/instrument.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <sstream>
#include <cstring>

// Load a baseline patch with its score block stripped (the tests schedule
// their own notes). Run engine_tests from the repo root.
static InstrumentPatch load_scoreless(const char* path) {
    std::ifstream f(path);
    std::stringstream ss; ss << f.rdbuf();
    std::string txt = ss.str();
    auto j = nlohmann::json::parse(txt);
    j.erase("score");
    return load_instrument_patch_json(j.dump());
}

// A minimal sustaining patch: percent-mode adsr (attack 5%, decay 10%,
// expand, release 25%) into a sine whose frequency is WIRED to the Note
// node. The wire matters: a legacy paramMap delivers frequency by a PUSH
// evaluated once at Setup, which freezes a glide at its first value —
// only pulled frequency chains (what every taught wind patch uses) see
// the ramp. %s = the instrument block's glideMs.
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
            "params": { "stages": [
              { "startVal": 0.0, "endVal": 1.0, "type": "Linear", "percent": 0.05 },
              { "startVal": 1.0, "endVal": 0.7, "type": "Linear", "percent": 0.10 },
              { "startVal": 0.7, "endVal": 0.7, "type": "Linear", "percent": 0.0 },
              { "startVal": 0.7, "endVal": 0.0, "type": "Linear", "percent": 0.25 }
            ] } },
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
    auto render_all = [](InstrumentPatch& ip, int n) {
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
        pa->play_note({60.0f, 0.8f, 1.0f}, 0.0f);
        auto bufA = render_all(a, N);

        auto b = load_scoreless("patches/baselines/BaselineSIN.json");
        auto* pb = dynamic_cast<PitchedInstrument*>(b.instrument.get());
        pb->play_note({60.0f, 0.8f, 1.0f, 0.0f, false}, 0.0f);
        auto bufB = render_all(b, N);
        CHECK(std::memcmp(bufA.data(), bufB.data(),
                          size_t(N) * sizeof(float)) == 0);
    }

    // (b) Hold mechanics: quarter {hold:true} + quarter {hold:false},
    //     same pitch. The gate stays open across 0.4 s — no release dip —
    //     and the voice releases after 0.8 s.
    {
        const int N = int(2.0f * sr);
        auto ip = load_instrument_patch_json(hold_patch_json(15.0f));
        auto* pi = dynamic_cast<PitchedInstrument*>(ip.instrument.get());
        CHECK(pi != nullptr);
        pi->play_note({60.0f, 0.8f, 0.4f, 0.0f, true},  0.0f);
        pi->play_note({60.0f, 0.8f, 0.4f, 0.0f, false}, 0.4f);
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
        auto ip = load_instrument_patch_json(hold_patch_json(15.0f));
        auto* pi = dynamic_cast<PitchedInstrument*>(ip.instrument.get());
        pi->play_note({60.0f, 0.8f, 0.4f, 0.0f, true},  0.0f);
        pi->play_note({60.0f, 0.8f, 0.8f, 0.0f, false}, 0.4f);
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
        auto glided = load_instrument_patch_json(hold_patch_json(15.0f));
        auto* pg = dynamic_cast<PitchedInstrument*>(glided.instrument.get());
        pg->play_note({60.0f, 0.8f, 0.4f, 0.0f, true},  0.0f);
        pg->play_note({67.0f, 0.8f, 0.4f, 0.0f, false}, 0.4f);
        auto bg = render_all(glided, N);

        auto instant = load_instrument_patch_json(hold_patch_json(0.0f));
        auto* pn = dynamic_cast<PitchedInstrument*>(instant.instrument.get());
        pn->play_note({60.0f, 0.8f, 0.4f, 0.0f, true},  0.0f);
        pn->play_note({67.0f, 0.8f, 0.4f, 0.0f, false}, 0.4f);
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
        auto ip = load_instrument_patch_json(hold_patch_json(15.0f));
        auto* pi = dynamic_cast<PitchedInstrument*>(ip.instrument.get());
        pi->play_note({60.0f, 0.8f, 0.5f, 0.0f, true}, 0.0f);
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
        pc->play_note({60.0f, 0.8f, 0.5f, 0.0f, true},  0.0f);
        pc->play_note({67.0f, 0.8f, 0.5f, 0.0f, false}, 0.5f);
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

// Renders notes 60 and 72 of an instrument patch whose paramMap drives `osc`'s
// frequency: the load must succeed, the output be finite and audible, and the
// octave double the zero crossings, so the note's pitch reached the oscillator.
static void check_param_map_pitch_reaches_osc(const std::string& patch) {
    const int n = 12000;
    auto render = [&](float midi) {
        auto ip = load_instrument_patch_json(patch);
        auto* pi = dynamic_cast<PitchedInstrument*>(ip.instrument.get());
        pi->play_note({midi, 0.8f, 0.4f}, 0.0f);
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

// A paramMap target on a param the patch leaves unwired loads and binds: the
// loader gives the pin a constant to drive, and the note's pitch reaches it.
static void run_param_map_unwired_pin_tests() {
    check_param_map_pitch_reaches_osc(R"({
      "sampleRate": 48000,
      "instrument": { "polyphony": 2, "paramMap": { "frequency": "osc.frequency" } },
      "graph": { "output": "osc",
        "nodes": [ { "id": "osc", "type": "SineSource", "params": { "phase": 0.0 } } ] }
    })");
}

// The same target under a MultiplexSource: the clones are built from the
// template JSON, so each clone's unwired pin gets its constant when the
// per-note push fans out, and the pitch reaches every clone.
static void run_param_map_unwired_multiplex_tests() {
    check_param_map_pitch_reaches_osc(R"({
      "sampleRate": 48000,
      "instrument": { "polyphony": 2, "paramMap": { "frequency": "osc.frequency" } },
      "graph": { "output": "mux", "nodes": [
        { "id": "osc", "type": "SineSource", "params": { "phase": 0.0 } },
        { "id": "mux", "type": "MultiplexSource",
          "params": { "count": 3, "source": { "ref": "osc" } } } ] }
    })");
}

static void run_envelope_retrigger_tests() {
    // Seconds-mode gesture: 10ms 1.0->0.2, 20ms 0.2->1.0, expand@1.0.
    const int sr = 48000;
    Envelope env;
    env.absolute_time = true;
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
                        "stages": [
              { "startVal": 1.0, "endVal": 0.1, "type": "Linear", "percent": 0.010 },
              { "startVal": 0.1, "endVal": 1.0, "type": "Linear", "percent": 0.020 },
              { "startVal": 1.0, "endVal": 1.0, "type": "Linear", "percent": 0.0 }
            ] } },
          { "id": "sine1", "type": "SineSource",
            "params": { "frequency": 220.0, "amplitude": { "ref": "gest1" },
                        "phase": 0.0 } }
        ]
      }
    })";
    auto ip = load_instrument_patch_json(kJson);
    auto* pi = dynamic_cast<PitchedInstrument*>(ip.instrument.get());
    CHECK(pi != nullptr);
    pi->play_note({57.0f, 0.8f, 0.5f, 0.0f, true}, 0.0f);
    pi->play_note({57.0f, 0.8f, 0.5f, pi->onset_id("tongue"), false}, 0.5f);
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
    auto ip2 = load_instrument_patch_json(kJson);
    auto* pi2 = dynamic_cast<PitchedInstrument*>(ip2.instrument.get());
    pi2->play_note({57.0f, 0.8f, 0.5f, 0.0f, true}, 0.0f);
    pi2->play_note({57.0f, 0.8f, 0.5f, 0.0f, false}, 0.5f);
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
            "params": { "stages": [
              { "startVal": 0.0, "endVal": 1.0, "type": "Linear", "percent": 0.10 },
              { "startVal": 1.0, "endVal": 0.7, "type": "Linear", "percent": 0.10 },
              { "startVal": 0.7, "endVal": 0.7, "type": "Linear", "percent": 0.0 },
              { "startVal": 0.7, "endVal": 0.0, "type": "Linear", "percent": 0.15 }
            ] } },
          { "id": "wn1", "type": "WhiteNoiseSource",
            "params": { "amplitude": { "ref": "env1" } } }
        ]
      }
    })";
    auto ip = load_instrument_patch_json(kJson);
    auto* pi = dynamic_cast<PitchedInstrument*>(ip.instrument.get());
    CHECK(pi != nullptr);
    pi->play_note({57.0f, 0.8f, 0.5f, 0.0f, true,  nullptr}, 0.0f);
    pi->play_note({57.0f, 0.8f, 0.5f, 0.0f, false, nullptr}, 0.5f);
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

// The voice pool's size: the file's "polyphony" (4 when the block has none),
// floored by the caller's minimum; the standalone loader's floor is 0.
static void run_voice_pool_tests() {
    std::printf("the voice pool: the file's polyphony, the default, the floor\n");
    const char* two = R"({"sampleRate":48000,"instrument":{"polyphony":2},"graph":{"output":"osc","nodes":[
      {"id":"osc","type":"SineSource","params":{"frequency":220.0}}]}})";
    const char* unstated = R"({"sampleRate":48000,"instrument":{},"graph":{"output":"osc","nodes":[
      {"id":"osc","type":"SineSource","params":{"frequency":220.0}}]}})";
    CHECK(load_instrument_patch_json(two, 5).instrument->voicePool.size() == 5);
    CHECK(load_instrument_patch_json(two, 0).instrument->voicePool.size() == 2);
    CHECK(load_instrument_patch_json(unstated, 0).instrument->voicePool.size() == 4);
    // The standalone loader reads files only: the patch is written to the
    // scratch folder and removed.
    const char* path = "renders/scratch/_voice_pool_two.json";
    std::filesystem::create_directories("renders/scratch");
    { std::ofstream f(path); f << two; }
    size_t voices = 0;
    try {
        auto built = load_patch_file(path);
        if (built.mixer && built.mixer->channels.size() == 1)
            if (auto* pi = dynamic_cast<PitchedInstrument*>(built.mixer->channels[0].source.get()))
                voices = pi->voicePool.size();
    }
    catch (const std::exception& e) { std::printf("  load failed: %s\n", e.what()); }
    std::filesystem::remove(path);
    CHECK(voices == 2);
}

// The score a patch file carries under "score": performed at load and its end
// returned in seconds; the loader turns the end into the frame count.
static void run_embedded_score_tests() {
    std::printf("the embedded score: its end, hold, the phrased refusal, the frames\n");
    const char* plain = R"({"sampleRate":48000,"instrument":{"polyphony":1},"graph":{"output":"osc","nodes":[
      {"id":"osc","type":"SineSource","params":{"frequency":220.0}}]}})";
    const char* sustaining = R"({"sampleRate":48000,"instrument":{"polyphony":1,"sustaining":true},"graph":{"output":"osc","nodes":[
      {"id":"osc","type":"SineSource","params":{"frequency":220.0}}]}})";
    // A fresh instrument per score.
    auto end_of = [](const char* patch, const char* score) {
        auto ip = load_instrument_patch_json(patch);
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
    // The loader's frame arithmetic: the render covers at least the score, a
    // larger "seconds" extends it. Each patch is written to the scratch folder
    // and removed.
    auto frames_of = [](const char* path, const char* text) {
        std::filesystem::create_directories("renders/scratch");
        { std::ofstream f(path); f << text; }
        int frames = -1;
        try { frames = load_patch_file(path).frames; }
        catch (const std::exception& e) { std::printf("  load failed: %s\n", e.what()); }
        std::filesystem::remove(path);
        return frames;
    };
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

// The loader hands the score over as text. Every score block in
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
    CHECK(checked == 96);
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
    const json stages = json::array({
        json{{"startVal", 0.0}, {"endVal", 1.0}, {"type", "Linear"}, {"percent", 0.05}},
        json{{"startVal", 1.0}, {"endVal", 0.7}, {"type", "Linear"}, {"percent", 0.10}},
        json{{"startVal", 0.7}, {"endVal", 0.7}, {"type", "Linear"}, {"percent", 0.0}},
        json{{"startVal", 0.7}, {"endVal", 0.0}, {"type", "Linear"}, {"percent", 0.25}}});
    auto env = [&](const char* id) {
        return json{{"id", id}, {"type", "Envelope"}, {"params", {{"stages", stages}}}};
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
    auto render_all = [](InstrumentPatch& p, int n) {
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
    auto ip = load_instrument_patch_json(shared_env_patch_json(false));
    auto* pi = dynamic_cast<PitchedInstrument*>(ip.instrument.get());
    CHECK(pi != nullptr && !pi->voicePool.empty());
    auto& vg = pi->voicePool[0];
    auto env = vg.nodesById.at("env");
    CHECK(vg.nodesById.at("sine2")->get_param("amplitude").get() == env.get());
    CHECK(vg.nodesById.at("sine1")->get_param("amplitude").get() == env.get());   // the second consumer holds the node itself
    const int N = 24000;
    pi->play_note({60.0f, 0.8f, 0.25f, 0.0f, false}, 0.0f);
    auto shared = render_all(ip, N);
    auto dupIp = load_instrument_patch_json(shared_env_patch_json(true));
    auto* dp = dynamic_cast<PitchedInstrument*>(dupIp.instrument.get());
    dp->play_note({60.0f, 0.8f, 0.25f, 0.0f, false}, 0.0f);
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

    // RangeSource: the declared default of `normalized` is false, whichever
    // constructor builds it; the 4-argument form sets it explicitly.
    {
        auto zero = std::make_shared<ConstantSource>(0.0f);
        RangeSource a(std::make_shared<ConstantSource>(10.0f),
                      std::make_shared<ConstantSource>(20.0f), zero);
        CHECK(a.normalized.value == false);
        CHECK(!a.setting_descriptors().empty());
        CHECK_NEAR(a.setting_descriptors()[0].default_value, 0.0f, 1e-6f);
        a.prepare(ctx); a.start(16);
        CHECK_NEAR(a.next(), 15.0f, 1e-5f);        // var 0 remapped (0+1)/2
        RangeSource b(std::make_shared<ConstantSource>(10.0f),
                      std::make_shared<ConstantSource>(20.0f), zero, true);
        b.prepare(ctx); b.start(16);
        CHECK_NEAR(b.next(), 10.0f, 1e-5f);        // already normalized
        CHECK(a.param_descriptors().size() == 3);
        CHECK(std::string(a.param_descriptors()[2].name) == "var");
        CHECK(a.get_param("max") != nullptr);
        for (int i = 0; i < 10; ++i) CHECK(std::isfinite(a.next()));
    }

    // VarSource: absolute and relative modes; wired through the 3-argument
    // constructor, default absolute.
    {
        VarSource v(std::make_shared<ConstantSource>(2.0f),
                    std::make_shared<ConstantSource>(0.5f),
                    std::make_shared<ConstantSource>(0.5f));
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

    // MultiSource: `source` is a list served by name; weights and delays
    // apply; clear_param empties it.
    {
        MultiSource m;
        m.set_param("source", std::make_shared<ConstantSource>(1.0f));
        m.add_param("source", std::make_shared<ConstantSource>(2.0f));
        CHECK(m.entries.size() == 2);
        CHECK(m.input_descriptors().size() == 1);
        CHECK(m.input_descriptors()[0].multi);
        CHECK(m.pins().empty());                   // the list is not a registry pin
        CHECK(m.get_param("source") == nullptr);
        m.entries[1].weight = 0.5f;
        m.entries[1].delaySamples = 1;
        m.prepare(ctx); m.start(16);
        CHECK_NEAR(m.next(), 1.0f, 1e-6f);         // the delayed entry is not yet in
        CHECK_NEAR(m.next(), 2.0f, 1e-6f);         // 1 + 2 * 0.5
        m.clear_param("source");
        CHECK(m.entries.empty());
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

// The settings the evolutions clamp when they are set keep clamping, and an
// evolution reads its own live value.
static void run_evolution_setting_tests() {
    ReactionDiffusionEvolution rd;
    rd.set_setting("subSteps", 0.0f);
    CHECK_NEAR(rd.get_setting("subSteps"), 1.0f, 1e-6f);
    rd.set_setting("subSteps", 3.0f);
    CHECK_NEAR(rd.get_setting("subSteps"), 3.0f, 1e-6f);

    SortErosionEvolution sort;
    sort.set_setting("swapsPerSample", -4.0f);
    CHECK_NEAR(sort.get_setting("swapsPerSample"), 1.0f, 1e-6f);
    sort.set_setting("descending", 1.0f);
    CHECK_NEAR(sort.get_setting("descending"), 1.0f, 1e-6f);

    HistogramEqualizeEvolution he;
    he.set_setting("bins", 1000.0f);
    CHECK_NEAR(he.get_setting("bins"), 256.0f, 1e-6f);
    he.set_setting("bins", 2.0f);
    CHECK_NEAR(he.get_setting("bins"), 4.0f, 1e-6f);

    CellularAutomatonEvolution ca;
    ca.set_setting("rule", 999.0f);
    CHECK_NEAR(ca.get_setting("rule"), 255.0f, 1e-6f);
    ca.set_setting("rule", -5.0f);
    CHECK_NEAR(ca.get_setting("rule"), 0.0f, 1e-6f);
    ca.set_setting("stepEvery", 0.0f);
    CHECK_NEAR(ca.get_setting("stepEvery"), 1.0f, 1e-6f);

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
    CHECK(input_names(full).empty());
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
    BWLowpassFilter lp(2);   smoke(lp);
    BWHighpassFilter hp(2);  smoke(hp);
    BWBandpassFilter bp(2);  smoke(bp);
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
    CHECK(setting_names(lp).empty() && setting_names(bp).empty() && setting_names(dly).empty());
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
    BWBandpassFilter bp(2);
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
    BWLowpassFilter lp(2);   count(lp, {"source", "cutoffFreq"});
    BWHighpassFilter hp(2);  count(hp, {"source", "cutoffFreq"});
    BWBandpassFilter bp(2);  count(bp, {"source", "lowCutoff", "highCutoff"});
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
    BWLowpassFilter lp(2);   order(lp, {"source", "cutoffFreq"});
    BWBandpassFilter bp(2);  order(bp, {"source", "lowCutoff", "highCutoff"});
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
    BWLowpassFilter lp(2);
    BWHighpassFilter hp(2);
    BWBandpassFilter bp(2);
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
    BWLowpassFilter low(2);
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
    StaticVarSource sv(1.0f, 0.1f);                          smoke(sv);
    StaticRangeSource sr(0.0f, 1.0f);                        smoke(sr);
    MultiplexSource mux;                                     smoke(mux);
    PhasedValueSource phased(0.05f);
    phased.add_stage({konst(1.0f), 0.5f, 0.0f, 0.0f, 1.0f});
    phased.add_stage({konst(2.0f), 0.0f, 0.0f, 0.0f, 1.0f});     smoke(phased);
    RepeatingSource rep;
    rep.set_param("source", konst(1.0f));                        smoke(rep);
    SegmentSource seg(std::vector<float>{2.0f, 1.0f, 2.0f, -1.0f});  smoke(seg);
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
    CHECK(input_names(phased).empty() && input_names(seg).empty());
    CHECK(param_names(comb).empty() && param_names(xfade) == "amplitude,");
    CHECK(param_names(dist) == "density,gain,shift,amplitude,");
    CHECK(param_names(shaper) == "drive,smoothness,morph,breakaway,capture,");
    CHECK(param_names(seg) == "amplitude,smoothness,width,gap,gapVarPct,widthVarPct,valVarPct,");
    CHECK(setting_names(comb) == "operation,gainAdj," && setting_names(xfade) == "ratio,overlap,gainAdj,");
    CHECK(setting_names(mux) == "count," && setting_names(shaper) == "hysteresis,");
    CHECK(setting_names(rep) == "duration,durVarPct,gapDuration,gapVarPct,");
    CHECK(setting_names(seg) == "oneShot,timeMode,");
    CHECK(setting_names(dist).empty() && setting_names(wh).empty() && setting_names(phased).empty());
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
    SegmentSource seg(std::vector<float>{});
    CHECK_NEAR(param_default(seg, "smoothness"), 0.5f, 1e-6f);
    CHECK_NEAR(param_default(seg, "width"), 1.0f, 1e-6f);
    CHECK_NEAR(param_default(seg, "gap"), 0.0f, 1e-6f);
    PhasedValueSource phased(0.05f);
    CHECK_NEAR(param_default(phased, "amplitude"), 1.0f, 1e-6f);
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
    SegmentSource once(std::vector<float>{4.0f, 1.0f}, true);
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
    PhasedValueSource phased(0.05f);
    phased.add_stage({konst(1.0f), 0.5f, 0.0f, 0.0f, 1.0f});
    phased.add_stage({konst(2.0f), 0.0f, 0.0f, 0.0f, 1.0f});
    count(phased, {"amplitude"});
    SegmentSource seg(std::vector<float>{2.0f, 1.0f});
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
    cloned.set_template("{}", 0, [](int) {
        return std::make_pair(std::shared_ptr<ValueSource>(konst(2.0f)), MultiplexSource::NodeMap{});
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
    SegmentSource seg(std::vector<float>{2.0f, 1.0f});
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
    mux.set_template("{}", 0, [&builds](int) {
        ++builds;
        return std::make_pair(std::shared_ptr<ValueSource>(konst(1.0f)), MultiplexSource::NodeMap{});
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
    SegmentSource seg(std::vector<float>{4.0f, 1.0f}, true);
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
    StaticVarSource sv(2.0f, 0.0f);
    sv.prepare(ctx); sv.start(10);
    CHECK_NEAR(sv.current(), 2.0f, 1e-6f);
    CHECK_NEAR(sv.next(), 2.0f, 1e-6f);
    StaticRangeSource sr(0.2f, 0.4f, 0.3f);
    sr.prepare(ctx); sr.start(10);
    const float held = sr.current();
    CHECK(held >= 0.2f && held <= 0.4f && sr.next() == held && sr.next() == held);

    // a one-shot segment ramps over its width, scaled by amplitude, then stops
    SegmentSource once(std::vector<float>{4.0f, 1.0f}, true);
    once.set_param("amplitude", konst(2.0f));
    once.prepare(ctx); once.start(100);
    CHECK_NEAR(once.next(), 0.5f, 1e-5f);
    CHECK_NEAR(once.next(), 1.0f, 1e-5f);
    CHECK_NEAR(once.next(), 1.5f, 1e-5f);
    CHECK_NEAR(once.next(), 2.0f, 1e-5f);
    CHECK(once.next() == 0.0f);

    // the phased source plays its first stage at its gain and amplitude
    PhasedValueSource phased(0.05f);
    phased.add_stage({konst(1.0f), 0.5f, 0.0f, 0.0f, 1.0f});
    phased.add_stage({konst(2.0f), 0.0f, 0.0f, 0.0f, 1.0f});
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

// The configurators set the migrated settings from the patch JSON, in the
// forms the loader accepts.
static void run_combine_mod_configurator_tests() {
    register_all_sources();
    auto& reg = SourceRegistry::instance();
    const ResolveParamFn none = [](const nlohmann::json&) { return std::shared_ptr<ValueSource>(); };
    auto configure = [&](const char* type, const char* json) {
        auto src = reg.create(type);
        (*reg.get_configurator(type))(*src, nlohmann::json::parse(json), none);
        return src;
    };
    auto comb = configure("CombinedSource", R"({"operation":"multiply","gainAdj":0.5})");
    CHECK_NEAR(comb->get_setting("operation"), 1.0f, 1e-6f);
    CHECK_NEAR(comb->get_setting("gainAdj"), 0.5f, 1e-6f);
    comb = configure("CombinedSource", R"({"operation":3})");
    CHECK_NEAR(comb->get_setting("operation"), 3.0f, 1e-6f);
    CHECK_NEAR(comb->get_setting("gainAdj"), 0.0f, 1e-6f);
    comb = configure("CombinedSource", R"({"operation":"mix"})");
    CHECK_NEAR(comb->get_setting("operation"), 0.0f, 1e-6f);
    bool threw = false;
    try { configure("CombinedSource", R"({"operation":9})"); }
    catch (const std::runtime_error&) { threw = true; }
    CHECK(threw);

    auto xf = configure("CrossfadeSource", R"({"ratio":0.7,"overlap":0.2,"gainAdj":0.1})");
    CHECK_NEAR(xf->get_setting("ratio"), 0.7f, 1e-6f);
    CHECK_NEAR(xf->get_setting("overlap"), 0.2f, 1e-6f);
    CHECK_NEAR(xf->get_setting("gainAdj"), 0.1f, 1e-6f);

    auto rep = configure("RepeatingSource", R"({"duration":0.25,"durVarPct":0.1,"gap":0.4,"gapVarPct":0.3})");
    CHECK_NEAR(rep->get_setting("duration"), 0.25f, 1e-6f);
    CHECK_NEAR(rep->get_setting("durVarPct"), 0.1f, 1e-6f);
    CHECK_NEAR(rep->get_setting("gapDuration"), 0.4f, 1e-6f);
    CHECK_NEAR(rep->get_setting("gapVarPct"), 0.3f, 1e-6f);
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
    run_combine_mod_configurator_tests();
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
};

static void run_split_equivalence_tests() {
    register_all_sources();
    auto& reg = SourceRegistry::instance();
    // Types that cannot render at their registry defaults at all (they
    // index an empty part). Their wired cases live in their family's tests.
    static const std::set<std::string> kNotRenderableUnwired = {
        "PhasedValueSource",   // stage-less: compute() indexes an empty boundaries_
    };
    std::set<std::string> parts;   // registered types that are not signals: the split has nothing to compare
    for (const auto& entry : reg.registered_types()) {
        const std::string n = entry.name;
        if (kNotRenderableUnwired.count(n)) continue;
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
        m->add(std::make_shared<SineSource>(), 1.0f, 0);
        m->add(Envelope::make_ar(0.3f), 0.5f, 100);
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
    // The loader's shape: seed noise in inputSource, an evolution in the
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
            h->set_target_partials(std::vector<float>(20, 0.05f));
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
    if (name == "BWLowpass")       n = std::make_shared<BWLowpassFilter>(2);
    else if (name == "BWHighpass") n = std::make_shared<BWHighpassFilter>(2);
    else if (name == "BWBandpass") n = std::make_shared<BWBandpassFilter>(2);
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
    auto ip = load_instrument_patch_json(R"({
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

    PhasedValueSource ph(0.05f);
    auto st1 = std::make_shared<PinCounter>(), st2 = std::make_shared<PinCounter>();
    ph.add_stage({st1, 0.5f, 0.0f, 0.0f, 1.0f});
    ph.add_stage({st2, 0.0f, 0.0f, 0.0f, 1.0f});
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
    mux.set_template("{}", 0, [&clones](int) {
        clones.push_back(std::make_shared<PinCounter>());
        return std::make_pair(std::shared_ptr<ValueSource>(clones.back()), MultiplexSource::NodeMap{});
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
    auto p = std::make_shared<PhasedValueSource>(0.01f);
    p->add_stage({sine_at(220.0f), 0.5f, 0.0f, 0.0f, 1.0f});
    p->add_stage({sine_at(330.0f), 0.0f, 0.0f, 0.0f, 1.0f});
    return p;
}

static std::shared_ptr<SegmentSource> make_segment(bool seconds, float varPct = 0.2f) {
    auto g = std::make_shared<SegmentSource>(
        seconds ? std::vector<float>{0.0004f, 1.0f, 0.0006f, -1.0f}
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
    CHECK(split_equivalent([] { return std::make_shared<StaticVarSource>(2.0f, 0.5f, 0.1f); }));
    CHECK(split_equivalent([] { return std::make_shared<StaticRangeSource>(0.2f, 0.4f, 0.3f); }));

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

// The hosts: a loaded instrument is prepared by the
// loader and only started per note; render takes no context.
static void run_host_split_tests() {
    const int n = 48000;
    auto render_twice = [&](const char* path) {
        std::vector<float> a(size_t(n), 0.0f), b(size_t(n), 0.0f);
        for (auto* out : {&a, &b}) {
            auto ip = load_scoreless(path);
            auto* inst = dynamic_cast<PitchedInstrument*>(ip.instrument.get());
            inst->play_note({60.0f, 0.8f, 0.4f, 0.0f, false, nullptr}, 0.0f);
            inst->play_note({64.0f, 0.8f, 0.4f, 0.0f, false, nullptr}, 0.5f);
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
    auto ip = load_instrument_patch_json(json);
    auto* pi = dynamic_cast<PitchedInstrument*>(ip.instrument.get());
    CHECK(pi != nullptr);
    pi->play_note({60.0f, 0.8f, 0.1f, 0.0f, false}, 0.0f);
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
// the node does not have or a part of the wrong kind; the loader turns that,
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
    try { load_instrument_patch_json(bad_key); } catch (const std::runtime_error& e) { msg = e.what(); }
    CHECK(msg.find("node 'osc' (SineSource): unknown key 'frequncy'") != std::string::npos);
    const char* bad_kind = R"({"sampleRate":48000,"instrument":{"polyphony":1},"graph":{"output":"add","nodes":[
        {"id":"s","type":"SineSource","params":{"frequency":440.0}},
        {"id":"add","type":"AdditiveSource","params":{"frequency":220.0,"partials":{"ref":"s"}}}]}})";
    msg.clear();
    try { load_instrument_patch_json(bad_kind); } catch (const std::runtime_error& e) { msg = e.what(); }
    CHECK(msg.find("node 'add' (AdditiveSource): 'partials' does not accept a SineSource") != std::string::npos);
    // A key a hand-built node's branch never read (the VarSource case the lint
    // found in the bwg patches) fails the load too: the check runs per node.
    const char* hand_built = R"({"sampleRate":48000,"instrument":{"polyphony":1},"graph":{"output":"v","nodes":[
        {"id":"v","type":"VarSource","params":{"value":0.0}}]}})";
    msg.clear();
    try { load_instrument_patch_json(hand_built); } catch (const std::runtime_error& e) { msg = e.what(); }
    CHECK(msg.find("node 'v' (VarSource): unknown key 'value'") != std::string::npos);
}

// A string where a pin takes a value fails the load naming the node, the pin
// and the string; the wavetable's old string-typed evolution also names the
// tool that rewrites it as a node, and the node form of the same patch loads.
static void run_string_pin_refusal_tests() {
    const char* evo = R"({"sampleRate":48000,"instrument":{"polyphony":1},"graph":{"output":"wt","nodes":[
        {"id":"wt","type":"WavetableSource","params":{"frequency":330.0,"evolution":"pluck","muting":0.0,"evolutionSeed":42}}]}})";
    std::string msg;
    try { load_instrument_patch_json(evo); } catch (const std::runtime_error& e) { msg = e.what(); }
    CHECK(msg.find("node 'wt' (WavetableSource): 'evolution' is the string \"pluck\"") != std::string::npos);
    CHECK(msg.find("tools/string_evolution_to_node.py") != std::string::npos);
    CHECK(msg.find("a slot takes {\"ref\": id}") != std::string::npos);       // a slot, so no number is offered
    const char* freq = R"({"sampleRate":48000,"instrument":{"polyphony":1},"graph":{"output":"osc","nodes":[
        {"id":"osc","type":"SineSource","params":{"frequency":"440"}}]}})";
    msg.clear();
    try { load_instrument_patch_json(freq); } catch (const std::runtime_error& e) { msg = e.what(); }
    CHECK(msg.find("node 'osc' (SineSource): 'frequency' is the string \"440\"") != std::string::npos);
    CHECK(msg.find("string_evolution_to_node") == std::string::npos);
    CHECK(msg.find("a pin takes a number") != std::string::npos);
    const char* node = R"({"sampleRate":48000,"instrument":{"polyphony":1},"graph":{"output":"wt","nodes":[
        {"id":"wt_evolution","type":"PluckEvolution","params":{"muting":0.0,"seed":42}},
        {"id":"wt","type":"WavetableSource","params":{"frequency":330.0,"evolution":{"ref":"wt_evolution"}}}]}})";
    auto ip = load_instrument_patch_json(node);
    CHECK(ip.instrument != nullptr);
}

#include "mforce/source/envelope/envelope_json.h"

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
    return a.absolute_time == b.absolute_time && a.has_adsr_shape_() == b.has_adsr_shape_();
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
        CHECK(!e->has_adsr_shape_() && !e->absolute_time);      // none is the four-slot shape, none in seconds
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
        CHECK(!e->has_adsr_shape_() && !e->absolute_time);
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

// Through the loader: a form node and the stages node it lays out as render
// the same bytes; a seed and the accuracy knobs reach the form node as they
// reach the stages node.
static std::vector<float> render_loaded_note(const char* json, int n) {
    auto ip = load_instrument_patch_json(json);
    auto* pi = dynamic_cast<PitchedInstrument*>(ip.instrument.get());
    pi->play_note({60.0f, 0.8f, 0.2f, 0.0f, false}, 0.0f);
    std::vector<float> out(static_cast<size_t>(n));
    ip.instrument->render(out.data(), n);
    return out;
}

static void run_envelope_form_loader_tests() {
    const char* form = R"({"sampleRate":48000,"instrument":{"polyphony":1},"graph":{"output":"osc","nodes":[
        {"id":"Env","type":"Envelope","params":{"preset":"as","attack":0.3,"attackCurve":1,"attackPower":2.0,"reverse":true,"sustainLevel":0.6,"stage_accuracy":0.9,"ramp_accuracy":0.95,"seed":77,"minValue":0.0,"maxValue":0.8}},
        {"id":"osc","type":"SineSource","params":{"frequency":220.0,"amplitude":{"ref":"Env"}}}]}})";
    const char* stages = R"({"sampleRate":48000,"instrument":{"polyphony":1},"graph":{"output":"osc","nodes":[
        {"id":"Env","type":"Envelope","params":{"stages":[{"startVal":0.6,"endVal":0.0,"type":"Expo","power":2.0,"percent":0.3,"maxSec":1.0},{"startVal":0.0,"endVal":0.0}],"stage_accuracy":0.9,"ramp_accuracy":0.95,"seed":77,"minValue":0.0,"maxValue":0.8}},
        {"id":"osc","type":"SineSource","params":{"frequency":220.0,"amplitude":{"ref":"Env"}}}]}})";
    const auto a = render_loaded_note(form, 9600), b = render_loaded_note(stages, 9600);
    CHECK(same_bits(a, b));
    CHECK(a[1000] != 0.0f);                       // inside the attack: reversed, the shape falls to 0 and holds there
    auto env_of = [](const char* json) {
        auto ip = load_instrument_patch_json(json);
        auto* pi = dynamic_cast<PitchedInstrument*>(ip.instrument.get());
        return std::dynamic_pointer_cast<Envelope>(pi->voicePool.at(0).nodesById.at("Env"));
    };
    auto ef = env_of(form), es = env_of(stages);
    CHECK(ef && es && same_layout(*ef, *es));
    CHECK(ef->get_seed() == es->get_seed() && ef->get_seed() != 0u);
    CHECK_NEAR(ef->get_setting("stage_accuracy"), 0.9f, 1e-6f);
    CHECK_NEAR(ef->get_setting("ramp_accuracy"), 0.95f, 1e-6f);
}

// A type this engine no longer has fails the load naming the tool that
// rewrites it; a name it never had does not.
static void run_retired_type_message_tests() {
    auto message_for = [](const char* type) {
        const std::string json = std::string(R"({"sampleRate":48000,"instrument":{"polyphony":1},"graph":{"output":"n","nodes":[
            {"id":"n","type":")") + type + R"(","params":{}}]}})";
        try { load_instrument_patch_json(json); } catch (const std::runtime_error& e) { return std::string(e.what()); }
        return std::string();
    };
    for (const char* t : {"ASEnvelope", "ADSREnvelope", "KSPianoString"}) {
        const std::string msg = message_for(t);
        CHECK(msg.find(std::string("Unknown node type: ") + t) != std::string::npos);
        CHECK(msg.find("tools/migrate_deleted_types.py") != std::string::npos);
    }
    const std::string none = message_for("NoSuchType");
    CHECK(none.find("Unknown node type: NoSuchType") != std::string::npos);
    CHECK(none.find("migrate_deleted_types") == std::string::npos);
    CHECK(retired_type_hint("Envelope") == nullptr && retired_type_hint("ASREnvelope") != nullptr);
    CHECK(retired_type_hint("AdditiveSource2") != nullptr && retired_type_hint("BasicAdditiveSource") != nullptr);
    CHECK(std::string(retired_type_hint("AdditiveSource2")).find("author an AdditiveSource") != std::string::npos);
    for (const char* t : {"Formant", "FormantSpectrum"})
        CHECK(std::string(retired_type_hint(t)).find("rows of a FormantPeaks") != std::string::npos);
    CHECK(std::string(retired_type_hint("FixedSpectrum")).find("FormantBands or FormantPeaks") != std::string::npos);
    CHECK(message_for("FormantSpectrum").find("migrate_deleted_types.py") != std::string::npos);
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
    auto ip = load_instrument_patch_json(patchJson);
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
      {"id":"parts","type":"FullPartials","params":{"maxPartials":8,"decayRate":1.0,"bandwidthHz":30.0,
         "motionDepth1":10.0,"shimmerDepth1":0.2,"tradeDepth":0.2,"onsetSpread":0.1,
         "expandRule":{"count":1,"recurse":1,"spacing1":0.3,"spacing2":0.3}}},
      {"id":"add","type":"AdditiveSource","params":{"frequency":220.0,"amplitude":0.5,"partials":{"ref":"parts"},"formantWeight":0.0}}]}})";
    CHECK(allocations_in_one_note(everything) == 0);
    CHECK(allocations_in_one_note(everything, 48.0f) == 0);
    // A second prepare after a note — the UI prepares every node again at
    // every Play — changes nothing a render can see: the expand rule starts
    // each note from the same originals (one voice, so the second note lands
    // on the voice the first note expanded).
    {
        const char* expanded = R"({"sampleRate":48000,"instrument":{"polyphony":1},"graph":{"output":"add","nodes":[
          {"id":"parts","type":"FullPartials","params":{"maxPartials":4,"expandRule":{"count":1,"recurse":0,"spacing1":0.3,"spacing2":0.3}}},
          {"id":"add","type":"AdditiveSource","params":{"frequency":220.0,"amplitude":0.5,"partials":{"ref":"parts"},"formantWeight":0.0}}]}})";
        auto once = load_instrument_patch_json(expanded);
        (void)render_one_live_note(*once.instrument, 60.0f);
        const auto second = render_one_live_note(*once.instrument, 60.0f);
        auto again = load_instrument_patch_json(expanded);
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
          {"id":"parts","type":"ExplicitPartials","params":{"mult1":[1.0,2.0,3.0,4.0],"ampl1":[1.0,0.5,0.25,0.125],"evolve":false,
             "expandRule":{"count":1,"recurse":0,"spacing1":0.3,"spacing2":0.3}}},
          {"id":"add","type":"AdditiveSource","params":{"frequency":220.0,"amplitude":0.5,"partials":{"ref":"parts"},"formantWeight":0.0}}]}})";
        const char* editedRows = R"({"sampleRate":48000,"instrument":{"polyphony":1},"graph":{"output":"add","nodes":[
          {"id":"parts","type":"ExplicitPartials","params":{"mult1":[1.0,2.0,3.0,4.0],"ampl1":[1.0,0.1,0.1,0.1],"evolve":false,
             "expandRule":{"count":1,"recurse":0,"spacing1":0.3,"spacing2":0.3}}},
          {"id":"add","type":"AdditiveSource","params":{"frequency":220.0,"amplitude":0.5,"partials":{"ref":"parts"},"formantWeight":0.0}}]}})";
        auto edited = load_instrument_patch_json(rows);
        (void)render_one_live_note(*edited.instrument, 60.0f);
        edited.instrument->voicePool[0].nodesById.at("parts")->set_array("ampl1", {1.0f, 0.1f, 0.1f, 0.1f});
        edited.instrument->prepare(RenderContext{48000});
        const auto afterEdit = render_one_live_note(*edited.instrument, 60.0f);
        auto loaded = load_instrument_patch_json(editedRows);
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
// additive nodes, the Multiplex push delivery.
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
      {"id":"phased","type":"PhasedValueSource","params":{"overlap":0.15,"amplitude":0.8,"stages":[
        {"source":{"ref":"saw"},"percent":0.33,"min":0.5,"max":3.0,"gain":1.0},
        {"source":{"ref":"pulse"},"percent":0.33,"min":0.5,"max":3.0,"gain":1.0},
        {"source":{"ref":"sin"},"percent":0,"min":0.0,"max":0.0,"gain":1.0}]}}]}})";
    CHECK(allocations_in_one_note(phased) == 0);
    CHECK(allocations_in_one_note(phased, 48.0f) == 0);
    // SegmentSource: the segment list is reserved at prepare for the shape's size.
    const char* segment = R"({"sampleRate":48000,"instrument":{"polyphony":2},"graph":{"output":"seg","nodes":[
      {"id":"seg","type":"SegmentSource","params":{"values":[8.0,0.65,8.0,-0.54,8.0,0.74,9.0,0.2],"oneShot":false,"seed":42,"smoothness":0.5,"gap":0.0}}]}})";
    CHECK(allocations_in_one_note(segment) == 0);
    CHECK(allocations_in_one_note(segment, 48.0f) == 0);

    // A Multiplex voice's push delivery: the paramMap fan-out into the clones
    // at note-on writes a slot registered at prepare, allocating nothing.
    const char* multiplex = R"({"sampleRate":48000,"instrument":{"paramMap":{"frequency":"Var1.val"},"polyphony":2},"graph":{"output":"Multiplex1","nodes":[
      {"id":"Var1","type":"VarSource","params":{"absolute":true,"val":1.0,"varPct":0.0}},
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
        auto ip = load_instrument_patch_json(held);
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
        auto ip = load_instrument_patch_json(drum);
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
        auto edited = load_instrument_patch_json(even);
        const auto before = render_one_live_note(*edited.instrument, 60.0f);
        edited.instrument->voicePool[0].nodesById.at("parts")->set_setting("evenWeight1", 0.0f);
        edited.instrument->prepare(RenderContext{48000});
        const auto after = render_one_live_note(*edited.instrument, 60.0f);
        auto loaded = load_instrument_patch_json(odd);
        (void)render_one_live_note(*loaded.instrument, 60.0f);
        const auto asLoaded = render_one_live_note(*loaded.instrument, 60.0f);
        CHECK(!same_bits(before, after));
        CHECK(same_bits(after, asLoaded));
    }
}

// The loader refuses a per-note push into a structural setting — the legacy
// paramMap's and the wiring format's dynamic pin — naming the node, the
// setting and the reason; a value setting still takes one.
// A load that fails for the reason the needle names; another reason is
// printed, so a test that expected one refusal and got another says so.
static bool load_fails_naming(const char* patch, const char* needle) {
    try { (void)load_instrument_patch_json(patch); }
    catch (const std::exception& e) {
        const std::string msg = e.what();
        if (msg.find(needle) == std::string::npos)
            std::printf("  load failed for another reason: %s\n", msg.c_str());
        return msg.find(needle) != std::string::npos;
    }
    return false;
}

static void run_structural_push_refusal_tests() {
    std::printf("structural settings: the loader refuses the wire\n");
    auto loads = [](const char* patch) {
        try { (void)load_instrument_patch_json(patch); return true; }
        catch (const std::exception& e) { std::printf("  load failed: %s\n", e.what()); return false; }
    };
    // The legacy paramMap into a shaping float (refused) and a value float (taken).
    const char* pushShaping = R"({"sampleRate":48000,"instrument":{"polyphony":1,"paramMap":{"frequency":["add.frequency","parts.evenWeight1"]}},"graph":{"output":"add","nodes":[
      {"id":"parts","type":"FullPartials","params":{"maxPartials":4}},
      {"id":"add","type":"AdditiveSource","params":{"frequency":220.0,"amplitude":0.5,"partials":{"ref":"parts"},"formantWeight":0.0}}]}})";
    CHECK(load_fails_naming(pushShaping, "parts.evenWeight1"));
    CHECK(load_fails_naming(pushShaping, "structural"));
    const char* pushValue = R"({"sampleRate":48000,"instrument":{"polyphony":1,"paramMap":{"frequency":["add.frequency","parts.rolloff1"]}},"graph":{"output":"add","nodes":[
      {"id":"parts","type":"FullPartials","params":{"maxPartials":4}},
      {"id":"add","type":"AdditiveSource","params":{"frequency":220.0,"amplitude":0.5,"partials":{"ref":"parts"},"formantWeight":0.0}}]}})";
    CHECK(loads(pushValue));
    // The wiring format's dynamic pin into a shaping float (refused), an int
    // (refused: structural by type) and a value float (taken).
    const char* pinShaping = R"({"sampleRate":48000,"instrument":{"polyphony":1},"graph":{"output":"add","nodes":[
      {"id":"pf","type":"PerformNode","params":{"field":"frequency"}},
      {"id":"parts","type":"SequencePartials","params":{"maxPartials":4},"dynamicPins":{"incr1":{"ref":"pf"}}},
      {"id":"add","type":"AdditiveSource","params":{"frequency":{"ref":"pf"},"amplitude":0.5,"partials":{"ref":"parts"},"formantWeight":0.0}}]}})";
    CHECK(load_fails_naming(pinShaping, "parts.dynamicPins.incr1"));
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
    auto ip = load_instrument_patch_json(additive);
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
        auto ip = load_instrument_patch_json(shaped);
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
    CHECK(load_fails_naming(ragged, "one length"));
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
    auto ip = load_instrument_patch_json(rows);
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
    auto empty = load_instrument_patch_json(none);                            // no rows: a formant that gains nothing
    auto* pk0 = dynamic_cast<FormantPeaks*>(empty.instrument->voicePool[0].nodesById.at("pk").get());
    CHECK(pk0 != nullptr && pk0->peak_count() == 0 && same_bits(pk0->gain(1000.0f), 0.0f));
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
// are not signals, every other type is. The twenty-one parts are the four
// Partials types and the expand rule, the three formant types and the
// thirteen evolutions.
static void run_component_root_tests() {
    std::printf("the root: parts and signals\n");
    register_all_sources();
    auto& reg = SourceRegistry::instance();
    CHECK(kPartTypes.size() == 21);
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
    CHECK(parts == 21);
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

// The review's gaps, the loader's: a refused slot wire says what the slot
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
    CHECK(load_fails_naming(seqAlias, "not 'formants'"));
    // a part as the instrument's output, as a mixer-mode output, as a channel's source
    const char* partAsVoiceOutput = R"({"sampleRate":48000,"instrument":{"polyphony":1},"graph":{"output":"parts","nodes":[
      {"id":"parts","type":"FullPartials","params":{"maxPartials":4}}]}})";
    CHECK(load_fails_naming(partAsVoiceOutput, "not a signal"));
    // The standalone loader reads files only: each patch is written to the
    // scratch folder (made here, since a fresh clone has none) and removed.
    auto file_fails_naming = [](const char* path, const char* text, const char* needle) {
        std::filesystem::create_directories("renders/scratch");
        { std::ofstream f(path); f << text; }
        bool named = false;
        try { (void)load_patch_file(path); }
        catch (const std::exception& e) {
            const std::string msg = e.what();
            if (msg.find(needle) == std::string::npos) std::printf("  load failed for another reason: %s\n", msg.c_str());
            named = msg.find(needle) != std::string::npos;
        }
        std::filesystem::remove(path);
        return named;
    };
    CHECK(file_fails_naming("renders/scratch/_part_as_output.json",
        R"({"sampleRate":48000,"seconds":0.1,"graph":{"output":"parts","nodes":[{"id":"parts","type":"FullPartials","params":{"maxPartials":4}}]}})",
        "mono-renderable"));
    CHECK(file_fails_naming("renders/scratch/_part_as_channel.json",
        R"({"sampleRate":48000,"seconds":0.1,"graph":{"output":"mix","nodes":[
          {"id":"parts","type":"FullPartials","params":{"maxPartials":4}},
          {"id":"ch1","type":"SoundChannel","inputs":{"source":"parts"},"params":{"volume":1.0,"pan":0.0}},
          {"id":"mix","type":"StereoMixer","inputs":{"channels":["ch1"]},"params":{"gainL":1.0,"gainR":1.0}}]}})",
        "No mono source"));
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
        auto ip = load_instrument_patch_json(probed);
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
// the shelf is today's menu — everything not in a main family menu is Experimental), the label
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
    int main = 0, experimental = 0;
    for (const auto& row : kFamilyTable) {
        const TypeTag* tag = reg.find(row.name);
        if (!tag) { std::printf("  in kFamilyTable but not registered: %s\n", row.name); CHECK(tag != nullptr); continue; }
        const std::string label = reg.label(row.name);
        if (tag->family != row.family || label != row.label || tag->shelf != row.shelf)
            std::printf("  %s: %s / %s / %d\n", row.name, family_name(tag->family), label.c_str(), int(tag->shelf));
        CHECK(tag->family == row.family);
        CHECK(label == row.label);
        CHECK(tag->shelf == row.shelf);
        (row.shelf == Shelf::Main ? main : experimental)++;
    }
    CHECK(main == 53 && experimental == 22);
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
        for (Shelf s : {Shelf::Main, Shelf::Experimental}) {
            std::set<std::string> labels;
            for (const auto& e : reg.types_in(f, s)) {
                CHECK(e.tag.family == f);
                CHECK(!reg.label(e.name).empty());
                CHECK(labels.insert(reg.label(e.name)).second);
                ++walked;
            }
        }
    CHECK(walked == 75);
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

int main() {
    run_alloc_probe_tests();
    run_allocation_tests_partials();
    run_allocation_tests_tables();
    run_allocation_tests_nodes();
    run_allocation_tests_pins();
    run_structural_setting_tests();
    run_structural_push_refusal_tests();
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
    run_menu_content_tests();
    run_review_gap_tests();
    run_review_gap_lifecycle_tests();
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
    run_param_map_unwired_pin_tests();
    run_param_map_unwired_multiplex_tests();
    run_note_reseed_tests();
    run_embedded_score_tests();
    run_embedded_score_text_tests();
    run_voice_pool_tests();
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
    if (g_fails) { std::printf("%d/%d FAILED\n", g_fails, g_checks); return 1; }
    std::printf("ALL PASS (%d checks)\n", g_checks);
    return 0;
}
