#include "mforce/testing/check.h"
#include "test_support.h"
#include "core/tests/fixtures.h"
#include "mforce/core/dsp/biquad.h"
#include "mforce/core/dsp/bow_friction.h"
#include "mforce/core/dsp/butterworth_section.h"
#include "mforce/core/dsp/constants.h"
#include "mforce/core/dsp/dc_blocker.h"
#include "mforce/core/dsp/one_pole.h"
#include "mforce/core/dsp/tuning_allpass.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

using namespace mforce;


void run_dsp_scaffold_tests() {
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

void run_dsp_dc_blocker_tests() {
    for (float sr : {44100.0f, 48000.0f, 96000.0f}) {
        auto sig = twin_signal(0xDC0u + uint32_t(sr), 10000);
        float dcR = 1.0f - 2.0f * 3.14159265f * 20.0f / sr;   // the reference formula, 20 Hz
        float dcX = 0.0f, dcY = 0.0f;
        dsp::DcBlocker dc;
        dc.set(20.0f, sr);
        int bad = 0;
        for (float exc : sig) {
            float x = dcR * dcY + exc - dcX;                   // the reference formula
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

void run_dsp_tuning_allpass_tests() {
    for (float C : {0.2f, 0.5f, 0.9f, 0.0f}) {
        auto sig = twin_signal(0x7A11u + uint32_t(C * 100.0f), 10000);
        float prevIn = 0.0f, state = 0.0f;
        dsp::TuningAllpass ap;
        ap.set_coefficient(C);
        int bad = 0;
        for (float x : sig) {
            float y = C * x + prevIn - C * state;    // the reference formula
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
void run_dsp_tuning_allpass_boundary_tests() {
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
        state = 0.0f;                              // a reset of y1 only
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

void run_dsp_one_pole_tests() {
    for (float c : {0.05f, 0.6f, 1.0f}) {
        auto sig = twin_signal(0x1E01u + uint32_t(c * 100.0f), 10000);
        float lp = 0.0f;
        dsp::OnePole pole;
        int bad = 0;
        for (float v : sig) {
            lp += c * (v - lp);                    // the reference formula
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
}  // namespace

void run_dsp_bow_friction_tests() {
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
}  // namespace

void run_dsp_biquad_tests() {
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
}  // namespace

void run_dsp_reset_tests() {
    auto sig = twin_signal(0x2E5E7u, 205);
    {
        dsp::DcBlocker a, b;
        a.set(60.0f, 44100.0f); b.set(60.0f, 44100.0f);
        CHECK(reset_mismatches(a, b, sig) == 0);
        dsp::DcBlocker c;
        c.set(60.0f, 44100.0f);
        const float r = 1.0f - 2.0f * 3.14159265f * 60.0f / 44100.0f;   // the reference formula at another corner
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
