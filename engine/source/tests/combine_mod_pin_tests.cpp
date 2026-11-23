#include "mforce/testing/check.h"
#include "test_support.h"
#include "core/tests/fixtures.h"
#include "source/tests/fixtures.h"
#include "mforce/core/descriptors.h"
#include "mforce/core/dsp_value_source.h"
#include "mforce/core/pins.h"
#include "mforce/source/combiner/combined_source.h"
#include "mforce/source/combiner/crossfade_source.h"
#include "mforce/source/combiner/multiplex_source.h"
#include "mforce/source/envelope/phased_value_source.h"
#include "mforce/source/envelope/segment_source.h"
#include "mforce/source/filter/distorted_source.h"
#include "mforce/source/filter/shaper_source.h"
#include "mforce/source/modulator/repeating_source.h"
#include "mforce/source/utility/static_range_source.h"
#include "mforce/source/utility/static_var_source.h"
#include "mforce/source/utility/wormhole_source.h"
#include <cmath>
#include <initializer_list>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

using namespace mforce;

namespace {
struct Doubler final : mforce::Processor {
  float compute() override { return 2.0f * input.current(); }
  const char* type_name() const override { return "Doubler"; }
};
}  // namespace

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
// Counts prepares and advances and remembers the length it was last prepared for.
struct LengthProbe final : Generator {
    int starts{0}, nexts{0}, lastFrames{0};
    void on_start(int frames) override { ++starts; lastFrames = frames; }
    float compute() override { ++nexts; return 1.0f; }
    const char* type_name() const override { return "LengthProbe"; }
};
}  // namespace

// Every type prepares and renders finite values, and declares its pins in the
// order they are advanced.
void run_combine_mod_pin_shape_tests() {
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
void run_combine_mod_default_tests() {
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
void run_combine_mod_auto_pin_tests() {
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
void run_combine_mod_manual_pin_tests() {
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
void run_combine_mod_shaper_timing_tests() {
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
