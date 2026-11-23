#include "mforce/testing/check.h"
#include "test_support.h"
#include "pin_order_probe.h"
#include "mforce/core/component.h"
#include "mforce/core/dsp_value_source.h"
#include "mforce/core/pins.h"
#include "mforce/core/source_registry.h"
#include "mforce/source/additive/composite_partials.h"
#include "mforce/source/additive/explicit_partials.h"
#include "mforce/source/additive/formant.h"
#include "mforce/source/additive/formant_bands.h"
#include "mforce/source/additive/formant_peaks.h"
#include "mforce/source/additive/formant_sequence.h"
#include "mforce/source/additive/full_partials.h"
#include "mforce/source/oscillator/sine_source.h"
#include "mforce/source/register_all_sources.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace mforce;

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
}  // namespace formant_bands_twin
// A Manual pin's source that steps through a list, one value per next().
struct StepSource final : Generator {
    std::vector<float> values;
    size_t at{0};
    float compute() override { const float v = values[at % values.size()]; ++at; return v; }
    const char* type_name() const override { return "StepSource"; }
};
void run_formant_bands_twin_tests() {
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
void run_formant_sequence_twin_tests() {
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

// The base drives a formant's pins: FormantBands' two prepared and started
// once and advanced once per next() in declaration order; FormantPeaks has
// no Param pin and its hook caches the rows; a FormantSequence's blend and
// its child's pins through the hooks.
void run_formant_lifecycle_tests() {
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
void run_partials_part_tests() {
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
