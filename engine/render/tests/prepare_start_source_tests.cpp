#include "mforce/testing/check.h"
#include "test_support.h"
#include "mforce/core/component.h"
#include "mforce/core/dsp_value_source.h"
#include "mforce/core/pins.h"
#include "mforce/patch/table_rule.h"
#include "mforce/patch/type_declarations.h"
#include "mforce/source/additive/additive_source.h"
#include "mforce/source/additive/composite_partials.h"
#include "mforce/source/additive/explicit_partials.h"
#include "mforce/source/additive/formant_peaks.h"
#include "mforce/source/additive/formant_sequence.h"
#include "mforce/source/additive/full_partials.h"
#include "mforce/source/envelope/envelope.h"
#include "mforce/source/noise/red_noise_source.h"
#include "mforce/source/noise/wander_noise2_source.h"
#include "mforce/source/noise/wander_noise_source.h"
#include "mforce/source/noise/white_noise_source.h"
#include "mforce/source/wavetable/bit_rotate_evolution.h"
#include "mforce/source/wavetable/bowed_string_evolution.h"
#include "mforce/source/wavetable/hybrid_ks_source.h"
#include "mforce/source/wavetable/pluck_evolution.h"
#include "mforce/source/wavetable/reed_evolution.h"
#include "mforce/source/wavetable/wavetable_source.h"
#include <algorithm>
#include <cmath>
#include <memory>
#include <utility>
#include <vector>

using namespace mforce;


// ---------------------------------------------------------------------------
// Noise family: constructors lose the rate; RedNoise::reset() is the
// constructed state (Vibrato's per-note fresh chain).
// ---------------------------------------------------------------------------
void run_noise_split_tests() {
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
void run_wavetable_split_tests() {
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

void run_hybrid_ks_split_tests() {
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

void run_additive_split_tests() {
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
