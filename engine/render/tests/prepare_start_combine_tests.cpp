#include "mforce/testing/check.h"
#include "test_support.h"
#include "patch/tests/fixtures.h"
#include "source/tests/fixtures.h"
#include "mforce/core/dsp_value_source.h"
#include "mforce/core/pins.h"
#include "mforce/render/pitched_instrument.h"
#include "mforce/source/combiner/combined_source.h"
#include "mforce/source/combiner/crossfade_source.h"
#include "mforce/source/combiner/multiplex_source.h"
#include "mforce/source/envelope/envelope.h"
#include "mforce/source/envelope/phased_value_source.h"
#include "mforce/source/envelope/segment_source.h"
#include "mforce/source/modulator/repeating_source.h"
#include "mforce/source/oscillator/sine_source.h"
#include "mforce/source/utility/static_range_source.h"
#include "mforce/source/utility/static_var_source.h"
#include <algorithm>
#include <memory>
#include <vector>

using namespace mforce;


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
    return output_of(pi->pool().voice(0));
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

void run_combine_mod_split_tests() {
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
