#include "mforce/testing/check.h"
#include "test_support.h"
#include "core/tests/fixtures.h"
#include "mforce/core/component.h"
#include "mforce/core/dsp/constants.h"
#include "mforce/core/dsp_value_source.h"
#include "mforce/core/pins.h"
#include "mforce/core/source_registry.h"
#include "mforce/source/register_all_sources.h"
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
#include "mforce/source/wavetable/wave_evolution.h"
#include "mforce/source/wavetable/wavetable_source.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string_view>
#include <vector>

using namespace mforce;


// All twelve evolutions: the settings (and pins) each declares, in order, and
// the declared default is what an untouched evolution reports.
void run_evolution_shape_tests() {
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

// An evolution's settings read back as set, and an evolution reads its own
// live value.
void run_evolution_setting_tests() {
    ReactionDiffusionEvolution rd;
    rd.set_setting("subSteps", 3.0f);
    CHECK_NEAR(rd.get_setting("subSteps"), 3.0f, 1e-6f);

    SortErosionEvolution sort;
    sort.set_setting("descending", 1.0f);
    CHECK_NEAR(sort.get_setting("descending"), 1.0f, 1e-6f);

    CellularAutomatonEvolution ca;
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
void run_evolution_manual_pin_tests() {
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
void run_evolution_lifecycle_tests() {
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
void run_evolution_live_read_tests() {
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
void run_target_evolution_tests() {
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
void run_target_evolution_morph_tests() {
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

// TargetEvolution is registered as a part: created by name, an evolution and
// not a signal, on the Experimental shelf of the Wavetable family.
void run_target_evolution_registry_tests() {
    register_all_sources();
    auto& reg = SourceRegistry::instance();
    CHECK(std::dynamic_pointer_cast<ValueSource>(reg.create("TargetEvolution")) == nullptr);
    CHECK(std::dynamic_pointer_cast<WaveEvolution>(reg.create("TargetEvolution")) != nullptr);
    CHECK(reg.family("TargetEvolution") == Family::Wavetable && reg.shelf("TargetEvolution") == Shelf::Experimental);
    CHECK(reg.label("TargetEvolution") == "Target Evolution");
}

// The four evolutions take an int setting outside its declared range as
// given: it reads back so, and the evolution, hosted in a wavetable, renders
// a thousand finite samples on it with no fault.
void run_evolution_unclamped_setting_tests() {
    std::printf("the four evolutions: an int setting outside its range is used as given\n");
    struct Case { const char* type; const char* setting; float value; };
    const Case cases[] = {
        {"CellularAutomatonEvolution", "rule", 300.0f},
        {"CellularAutomatonEvolution", "rule", -1.0f},
        {"CellularAutomatonEvolution", "stepEvery", 0.0f},
        {"ReactionDiffusionEvolution", "subSteps", 0.0f},
        {"HistogramEqualizeEvolution", "bins", 3.0f},
        {"HistogramEqualizeEvolution", "bins", 300.0f},
        {"SortErosionEvolution", "swapsPerSample", 0.0f},
    };
    auto make = [](std::string_view type) -> std::shared_ptr<Component> {
        if (type == "CellularAutomatonEvolution") return std::make_shared<CellularAutomatonEvolution>();
        if (type == "ReactionDiffusionEvolution") return std::make_shared<ReactionDiffusionEvolution>();
        if (type == "HistogramEqualizeEvolution") return std::make_shared<HistogramEqualizeEvolution>();
        return std::make_shared<SortErosionEvolution>();
    };
    for (const auto& c : cases) {
        auto evo = make(c.type);
        evo->set_setting(c.setting, c.value);
        const bool readBack = evo->get_setting(c.setting) == c.value;
        TestHost h;
        WavetableSource w;
        w.set_param("evolution", evo);
        w.prepare(h.ctx); w.start(4800);
        bool finite = true;
        for (int i = 0; i < 1000; ++i) { h.clock.tick(); finite = finite && std::isfinite(w.next()); }
        const unsigned char faults = w.take_faults();
        if (!readBack || !finite || faults != 0)
            std::printf("  %s.%s = %g: read back %g, finite %d, faults %d\n", c.type, c.setting, c.value,
                        evo->get_setting(c.setting), int(finite), int(faults));
        CHECK(readBack);
        CHECK(finite);
        CHECK(faults == 0);
    }
}
