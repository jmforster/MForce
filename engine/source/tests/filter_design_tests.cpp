#include "mforce/testing/check.h"
#include "test_support.h"
#include "core/tests/fixtures.h"
#include "mforce/core/dsp/biquad.h"
#include "mforce/core/dsp/butterworth_section.h"
#include "mforce/core/dsp_value_source.h"
#include "mforce/core/pins.h"
#include "mforce/source/filter/biquad_source.h"
#include "mforce/source/filter/bw_highpass_filter.h"
#include "mforce/source/filter/bw_lowpass_filter.h"
#include "mforce/source/noise/white_noise_source.h"
#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

using namespace mforce;


void run_dsp_biquad_recipe_tests() {
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
}  // namespace

void run_dsp_butterworth_tests() {
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
