#include "mforce/testing/check.h"
#include "test_support.h"
#include "source/tests/fixtures.h"
#include "mforce/core/component.h"
#include "mforce/core/dsp_value_source.h"
#include "mforce/core/pins.h"
#include "mforce/core/ramp.h"
#include "mforce/core/source_registry.h"
#include "mforce/source/combiner/multi_source.h"
#include "mforce/source/envelope/envelope.h"
#include "mforce/source/oscillator/fm_source.h"
#include "mforce/source/oscillator/sine_source.h"
#include "mforce/source/register_all_sources.h"
#include <cmath>
#include <cstdio>
#include <memory>
#include <set>
#include <string>
#include <utility>

using namespace mforce;


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

// The two-call contract: the sweep over every type, then one group per family.
void run_prepare_start_tests() {
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
