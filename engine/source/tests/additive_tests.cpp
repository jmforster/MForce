#include "mforce/testing/check.h"
#include "test_support.h"
#include "mforce/core/descriptors.h"
#include "mforce/core/dsp_value_source.h"
#include "mforce/core/pins.h"
#include "mforce/source/additive/additive_source.h"
#include "mforce/source/additive/composite_partials.h"
#include "mforce/source/additive/expand_rule_node.h"
#include "mforce/source/additive/explicit_partials.h"
#include "mforce/source/additive/formant.h"
#include "mforce/source/additive/formant_bands.h"
#include "mforce/source/additive/formant_peaks.h"
#include "mforce/source/additive/formant_sequence.h"
#include "mforce/source/additive/full_partials.h"
#include "mforce/source/additive/partial_set.h"
#include "mforce/source/additive/sequence_partials.h"
#include <cmath>
#include <memory>
#include <span>
#include <string>
#include <vector>

using namespace mforce;

namespace {

bool has_name(std::span<const SettingDescriptor> d, const char* n) {
  for (const auto& x : d) if (std::string(x.name) == n) return true;
  return false;
}
// The eight envelope pins of every Partials, in the order next() advances them.
const char* const kPartialsEnvNames[8] = {"multEnv", "amplEnv", "poEnv", "roEnv",
                                          "dtEnv", "bwEnv", "motionEnv", "shimmerEnv"};
}  // namespace

// The source declares the WaveSource trio and then its own pins, in the
// order wave() advances them; the structural inputs are named but never
// pulled.
void run_additive_source_pin_tests() {
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
void run_additive_structural_pin_tests() {
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
void run_additive_partials_pin_tests() {
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
void run_additive_smoke_tests() {
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
void run_additive_manual_pin_tests() {
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
void run_additive_formant_pin_tests() {
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
void run_additive_wiring_tests() {
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
void run_additive_setting_hook_tests() {
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
void run_additive_explicit_array_tests() {
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
