#include "mforce/testing/check.h"
#include "test_support.h"
#include "core/tests/fixtures.h"
#include "source/tests/fixtures.h"
#include "mforce/core/dsp_value_source.h"
#include "mforce/core/pins.h"
#include "mforce/source/combiner/combined_source.h"
#include "mforce/source/combiner/crossfade_source.h"
#include "mforce/source/combiner/multiplex_source.h"
#include "mforce/source/envelope/phased_value_source.h"
#include "mforce/source/envelope/segment_source.h"
#include "mforce/source/filter/distorted_source.h"
#include "mforce/source/filter/shaper_source.h"
#include "mforce/source/utility/static_range_source.h"
#include "mforce/source/utility/static_var_source.h"
#include <cstdio>
#include <initializer_list>
#include <memory>
#include <string_view>
#include <vector>

using namespace mforce;


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
void run_combine_mod_tests() {
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

// A combiner's gainAdj scales its second operand by 1 + gainAdj, and its
// declared range takes a signed scale.
void run_gain_adj_range_tests() {
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
