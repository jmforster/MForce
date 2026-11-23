#include "mforce/testing/check.h"
#include "test_support.h"
#include "mforce/core/descriptors.h"
#include "mforce/core/dsp_value_source.h"
#include "mforce/core/pins.h"
#include "mforce/source/envelope/vibrato.h"
#include "mforce/source/filter/biquad_source.h"
#include "mforce/source/filter/bw_bandpass_filter.h"
#include "mforce/source/filter/bw_highpass_filter.h"
#include "mforce/source/filter/bw_lowpass_filter.h"
#include "mforce/source/filter/delay_filter.h"
#include "mforce/source/filter/hammer_bank.h"
#include "mforce/source/filter/limiter.h"
#include "mforce/source/filter/reverb.h"
#include "mforce/source/filter/svf_source.h"
#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

using namespace mforce;


static_assert(std::is_base_of_v<Processor, BiquadSource> &&
              std::is_base_of_v<Processor, BWLowpassFilter> &&
              std::is_base_of_v<Processor, BWHighpassFilter> &&
              std::is_base_of_v<Processor, BWBandpassFilter> &&
              std::is_base_of_v<Processor, DelayFilter> &&
              std::is_base_of_v<Processor, HammerBank> &&
              std::is_base_of_v<Processor, Limiter> &&
              std::is_base_of_v<Processor, Reverb> &&
              std::is_base_of_v<Processor, SVFSource>);
static_assert(std::is_base_of_v<Generator, Vibrato> && !std::is_base_of_v<Processor, Vibrato>);

// Every type prepares and renders finite values, and declares its pins in
// the order they are advanced.
static void run_filter_pin_shape_tests() {
    RenderContext ctx{48000};
    auto smoke = [&](ValueSource& s) {
        s.prepare(ctx); s.start(4800);
        for (int i = 0; i < 10; ++i) CHECK(std::isfinite(s.next()));
    };
    BiquadSource bq;         smoke(bq);
    BWLowpassFilter lp;      smoke(lp);
    BWHighpassFilter hp;     smoke(hp);
    BWBandpassFilter bp;     smoke(bp);
    DelayFilter dly;         smoke(dly);
    HammerBank hammer;       smoke(hammer);
    Limiter lim;             smoke(lim);
    Reverb rev;              smoke(rev);
    SVFSource svf;           smoke(svf);
    Vibrato vib;             smoke(vib);

    CHECK(pin_order(bq) == "source,frequency,radius,");
    CHECK(pin_order(lp) == "source,cutoffFreq,");
    CHECK(pin_order(hp) == "source,cutoffFreq,");
    CHECK(pin_order(bp) == "source,lowCutoff,highCutoff,");
    CHECK(pin_order(dly) == "source,delayTime,delayLevel,feedback,");
    CHECK(pin_order(hammer) == "source,frequency,");
    CHECK(pin_order(lim) == "source,threshold,release,");
    CHECK(pin_order(rev) == "source,roomSize,damping,wet,dry,");
    CHECK(pin_order(svf) == "source,cutoffFreq,resonance,");
    CHECK(pin_order(vib) == "frequency,");

    CHECK(input_names(bq) == "source," && input_names(rev) == "source,");
    CHECK(input_names(vib).empty());
    CHECK(param_names(bp) == "lowCutoff,highCutoff,");
    CHECK(param_names(vib) == "frequency,");
    CHECK(setting_names(bq) == "mode,b0,b1,b2,a1,a2,");
    CHECK(setting_names(svf) == "mode,normalize,");
    CHECK(setting_names(hammer) ==
          "numBands,harm1,harm2,harm3,harm4,resStart,resEnd,resDecay,bandTilt,direct,gain,");
    CHECK(setting_names(vib) ==
          "speed,depth,attack,threshold,speedVar,depthVar,zeroCrossTendency,");
    CHECK(setting_names(lp) == "sections," && setting_names(bp) == "sections," && setting_names(dly).empty());
    CHECK(setting_names(lim).empty() && setting_names(rev).empty());

    CHECK(vib.range() == Generator::Range::None);
}

// The descriptors declare each filter's documented defaults, and the enum
// settings keep their labels and their types.
static void run_filter_default_tests() {
    BiquadSource bq;
    CHECK_NEAR(param_default(bq, "frequency"), 220.0f, 1e-6f);
    CHECK_NEAR(param_default(bq, "radius"), 0.997f, 1e-6f);
    CHECK_NEAR(bq.get_setting("b0"), 1.0f, 1e-6f);
    CHECK_NEAR(bq.get_setting("a1"), 0.0f, 1e-6f);
    BWBandpassFilter bp;
    CHECK_NEAR(param_default(bp, "lowCutoff"), 100.0f, 1e-6f);
    CHECK_NEAR(param_default(bp, "highCutoff"), 5000.0f, 1e-6f);
    DelayFilter dly;
    CHECK_NEAR(param_default(dly, "delayTime"), 0.01f, 1e-6f);
    CHECK_NEAR(param_default(dly, "delayLevel"), 0.5f, 1e-6f);
    CHECK_NEAR(param_default(dly, "feedback"), 0.3f, 1e-6f);
    Limiter lim;
    CHECK_NEAR(param_default(lim, "threshold"), 0.95f, 1e-6f);
    CHECK_NEAR(param_default(lim, "release"), 0.05f, 1e-6f);
    Reverb rev;
    CHECK_NEAR(param_default(rev, "wet"), 0.33f, 1e-6f);
    CHECK_NEAR(param_default(rev, "dry"), 0.4f, 1e-6f);
    HammerBank hammer;
    CHECK_NEAR(hammer.get_setting("numBands"), 4.0f, 1e-6f);
    CHECK_NEAR(hammer.get_setting("resDecay"), 0.010f, 1e-6f);
    CHECK_NEAR(hammer.get_setting("bandTilt"), -0.5f, 1e-6f);
    Vibrato vib;
    CHECK_NEAR(param_default(vib, "frequency"), 440.0f, 1e-6f);
    CHECK_NEAR(vib.get_setting("speed"), 5.0f, 1e-6f);
    CHECK_NEAR(vib.get_setting("zeroCrossTendency"), 1.0f, 1e-6f);
    // set_setting moves the value, not the declared default
    Vibrato custom;
    custom.set_setting("speed", 6.5f);
    custom.set_setting("depthVar", 0.4f);
    custom.set_setting("zeroCrossTendency", 0.0f);
    CHECK_NEAR(custom.get_setting("speed"), 6.5f, 1e-6f);
    CHECK_NEAR(custom.get_setting("depthVar"), 0.4f, 1e-6f);
    CHECK_NEAR(custom.get_setting("zeroCrossTendency"), 0.0f, 1e-6f);
    for (const auto& d : custom.setting_descriptors())
        if (std::string(d.name) == "speed") CHECK_NEAR(d.default_value, 5.0f, 1e-6f);

    for (const auto& d : bq.setting_descriptors())
        if (std::string(d.name) == "mode") {
            CHECK(d.type == SettingType::Int && d.enum_labels != nullptr);
            CHECK(std::string(d.enum_labels[0]) == "Raw");
            CHECK(std::string(d.enum_labels[1]) == "Resonance" && d.enum_labels[2] == nullptr);
        }
    SVFSource svf;
    for (const auto& d : svf.setting_descriptors()) {
        if (std::string(d.name) == "mode") {
            CHECK(d.type == SettingType::Int && d.enum_labels != nullptr);
            CHECK(std::string(d.enum_labels[0]) == "Lowpass");
            CHECK(std::string(d.enum_labels[4]) == "Highpass1P" && d.enum_labels[5] == nullptr);
            CHECK_NEAR(d.max_value, 4.0f, 1e-6f);
        }
        if (std::string(d.name) == "normalize") CHECK(d.type == SettingType::Bool);
    }
}

// The base prepares and advances every wired Auto pin once per note and once
// per sample. The biquad's frequency and radius are Manual: the node prepares
// them every note and pulls them only in resonance mode.
static void run_filter_auto_pin_tests() {
    RenderContext ctx{48000};
    auto wire = [&](ValueSource& s, std::initializer_list<const char*> names) {
        std::vector<std::shared_ptr<PinCounter>> probes;
        for (const char* n : names) {
            probes.push_back(std::make_shared<PinCounter>());
            s.set_param(n, probes.back());
        }
        s.prepare(ctx); s.start(4800);
        for (int i = 0; i < 7; ++i) CHECK(std::isfinite(s.next()));
        return probes;
    };
    auto count = [&](ValueSource& s, std::initializer_list<const char*> names) {
        for (const auto& p : wire(s, names)) CHECK(p->starts == 1 && p->nexts == 7);
    };
    BWLowpassFilter lp;      count(lp, {"source", "cutoffFreq"});
    BWHighpassFilter hp;     count(hp, {"source", "cutoffFreq"});
    BWBandpassFilter bp;     count(bp, {"source", "lowCutoff", "highCutoff"});
    DelayFilter dly;         count(dly, {"source", "delayTime", "delayLevel", "feedback"});
    HammerBank hammer;       count(hammer, {"source", "frequency"});
    Limiter lim;             count(lim, {"source", "threshold", "release"});
    Reverb rev;              count(rev, {"source", "roomSize", "damping", "wet", "dry"});
    SVFSource svf;           count(svf, {"source", "cutoffFreq", "resonance"});
    Vibrato vib;             count(vib, {"frequency"});

    BiquadSource raw;
    auto rp = wire(raw, {"source", "frequency", "radius"});
    CHECK(rp[0]->starts == 1 && rp[0]->nexts == 7);
    CHECK(rp[1]->starts == 1 && rp[1]->nexts == 0);
    CHECK(rp[2]->starts == 1 && rp[2]->nexts == 0);
    BiquadSource res;
    res.set_setting("mode", 1.0f);
    auto sp = wire(res, {"source", "frequency", "radius"});
    for (const auto& p : sp) CHECK(p->starts == 1 && p->nexts == 7);

    // an unwired pin reads its declared constant and is not a source
    CHECK(lp.get_param("source") != nullptr && lp.get_param("nosuch") == nullptr);
    DelayFilter bare;
    CHECK(bare.get_param("source") == nullptr && bare.get_param("delayTime") == nullptr);
    bare.clear_param("nosuch");
    CHECK(bare.get_param("nosuch") == nullptr);
}

// The base advances the pins in declaration order, the inherited `source`
// first; the biquad pulls frequency then radius itself, in resonance mode.
static void run_filter_advance_order_tests() {
    RenderContext ctx{48000};
    auto order = [&](ValueSource& s, std::initializer_list<const char*> names) {
        std::vector<int> log;
        std::vector<std::shared_ptr<OrderProbe>> probes;
        std::vector<int> expect;
        int id = 0;
        for (const char* n : names) {
            probes.push_back(std::make_shared<OrderProbe>(&log, ++id));
            s.set_param(n, probes.back());
            expect.push_back(id);
        }
        s.prepare(ctx); s.start(4800);
        s.next();
        CHECK(log == expect);
    };
    BWLowpassFilter lp;      order(lp, {"source", "cutoffFreq"});
    BWBandpassFilter bp;     order(bp, {"source", "lowCutoff", "highCutoff"});
    DelayFilter dly;         order(dly, {"source", "delayTime", "delayLevel", "feedback"});
    HammerBank hammer;       order(hammer, {"source", "frequency"});
    Limiter lim;             order(lim, {"source", "threshold", "release"});
    Reverb rev;              order(rev, {"source", "roomSize", "damping", "wet", "dry"});
    SVFSource svf;           order(svf, {"source", "cutoffFreq", "resonance"});
    BiquadSource bq;
    bq.set_setting("mode", 1.0f);
    order(bq, {"source", "frequency", "radius"});
}

// The setting hooks clamp the hammer bank's band count and resonance floors
// and the SVF's mode range. The others are not clamped.
static void run_filter_setting_hook_tests() {
    HammerBank hb;
    hb.set_setting("numBands", 9.0f);   CHECK_NEAR(hb.get_setting("numBands"), 4.0f, 1e-6f);
    hb.set_setting("numBands", 0.0f);   CHECK_NEAR(hb.get_setting("numBands"), 1.0f, 1e-6f);
    hb.set_setting("numBands", 2.0f);   CHECK_NEAR(hb.get_setting("numBands"), 2.0f, 1e-6f);
    hb.set_setting("resEnd", 0.1f);     CHECK_NEAR(hb.get_setting("resEnd"), 0.5f, 1e-6f);
    hb.set_setting("resEnd", 99.0f);    CHECK_NEAR(hb.get_setting("resEnd"), 99.0f, 1e-6f);
    hb.set_setting("resDecay", 0.0f);   CHECK_NEAR(hb.get_setting("resDecay"), 1e-4f, 1e-9f);
    hb.set_setting("resDecay", 2.0f);   CHECK_NEAR(hb.get_setting("resDecay"), 2.0f, 1e-6f);
    hb.set_setting("harm3", 40.0f);     CHECK_NEAR(hb.get_setting("harm3"), 40.0f, 1e-6f);
    hb.set_setting("gain", 20.0f);      CHECK_NEAR(hb.get_setting("gain"), 20.0f, 1e-6f);
    hb.set_setting("nosuch", 1.0f);     CHECK_NEAR(hb.get_setting("nosuch"), 0.0f, 1e-6f);

    SVFSource svf;
    svf.set_setting("mode", 9.0f);      CHECK_NEAR(svf.get_setting("mode"), 4.0f, 1e-6f);
    svf.set_setting("mode", -3.0f);     CHECK_NEAR(svf.get_setting("mode"), 0.0f, 1e-6f);
    svf.set_setting("mode", 2.0f);      CHECK_NEAR(svf.get_setting("mode"), 2.0f, 1e-6f);
    svf.set_setting("normalize", 0.4f); CHECK_NEAR(svf.get_setting("normalize"), 1.0f, 1e-6f);
    svf.set_setting("normalize", 0.0f); CHECK_NEAR(svf.get_setting("normalize"), 0.0f, 1e-6f);

    BiquadSource bq;
    bq.set_setting("mode", 1.0f);       CHECK_NEAR(bq.get_setting("mode"), 1.0f, 1e-6f);
    bq.set_setting("b0", 99.0f);        CHECK_NEAR(bq.get_setting("b0"), 99.0f, 1e-6f);
    bq.set_setting("a2", -5.0f);        CHECK_NEAR(bq.get_setting("a2"), -5.0f, 1e-6f);

    // the vibrato rebuilds its LFO at prepare from whatever the settings hold
    Vibrato vib;
    vib.set_setting("threshold", 0.5f); CHECK_NEAR(vib.get_setting("threshold"), 0.5f, 1e-6f);
    vib.set_setting("depth", 0.3f);     CHECK_NEAR(vib.get_setting("depth"), 0.3f, 1e-6f);
}

// The pins are read in compute(): the biquad's two modes, the delay's echo,
// the limiter's ceiling, the reverb's dry path, the hammer bank's direct path.
static void run_filter_behavior_tests() {
    RenderContext ctx{48000};
    auto in = std::make_shared<ConstantSource>(1.0f);

    // raw mode: y = b0 x on the first sample (no history)
    BiquadSource raw;
    raw.set_param("source", in);
    raw.set_setting("b0", 0.5f);
    raw.prepare(ctx); raw.start(4800);
    CHECK_NEAR(raw.next(), 0.5f, 1e-6f);

    // resonance mode derives a1/a2 from the frequency and radius pins, and
    // reads their declared constants when nothing is wired
    BiquadSource res;
    res.set_param("source", in);
    res.set_setting("mode", 1.0f);
    res.set_param("frequency", std::make_shared<ConstantSource>(1000.0f));
    res.set_param("radius", std::make_shared<ConstantSource>(0.9f));
    res.prepare(ctx); res.start(4800);
    res.next();
    CHECK_NEAR(res.get_setting("a2"), 0.81f, 1e-6f);
    CHECK_NEAR(res.get_setting("a1"),
               -2.0f * 0.9f * std::cos(2.0f * 3.14159265f * 1000.0f / 48000.0f), 1e-5f);
    BiquadSource dflt;
    dflt.set_param("source", in);
    dflt.set_setting("mode", 1.0f);
    dflt.prepare(ctx); dflt.start(4800);
    dflt.next();
    CHECK_NEAR(dflt.get_setting("a2"), 0.997f * 0.997f, 1e-6f);

    // delay: an impulse comes back 0.01 s later at level/(1+level)
    auto imp = std::make_shared<ConstantSource>(1.0f);
    DelayFilter dly;
    dly.set_param("source", imp);
    dly.prepare(ctx); dly.start(4800);
    CHECK_NEAR(dly.next(), 1.0f / 1.5f, 1e-5f);
    imp->set(0.0f);
    float echo = 0.0f;
    for (int i = 1; i <= 480; ++i) echo = dly.next();
    CHECK_NEAR(echo, (0.5f / 1.15f) / 1.5f, 1e-3f);

    // limiter: a constant 2.0 settles at the threshold once the lookahead fills
    auto loud = std::make_shared<ConstantSource>(2.0f);
    Limiter lim;
    lim.set_param("source", loud);
    lim.set_param("threshold", std::make_shared<ConstantSource>(0.5f));
    lim.prepare(ctx); lim.start(4800);
    float last = 0.0f;
    for (int i = 0; i < 1000; ++i) last = lim.next();
    CHECK_NEAR(last, 0.5f, 1e-3f);

    // reverb: wet 0, dry 1 passes the input through
    Reverb rev;
    rev.set_param("source", in);
    rev.set_param("wet", std::make_shared<ConstantSource>(0.0f));
    rev.set_param("dry", std::make_shared<ConstantSource>(1.0f));
    rev.prepare(ctx); rev.start(4800);
    CHECK_NEAR(rev.next(), 1.0f, 1e-6f);

    // hammer bank: gain 0 and direct 1 leaves the input
    HammerBank hammer;
    hammer.set_param("source", std::make_shared<ConstantSource>(0.7f));
    hammer.set_setting("gain", 0.0f);
    hammer.set_setting("direct", 1.0f);
    hammer.prepare(ctx); hammer.start(4800);
    CHECK_NEAR(hammer.next(), 0.7f, 1e-6f);
}

// Butterworth sections: a constant passes the lowpass, is blocked by the
// highpass and by a bandpass whose low edge is above DC.
static void run_filter_butterworth_tests() {
    RenderContext ctx{48000};
    auto dc = std::make_shared<ConstantSource>(1.0f);
    BWLowpassFilter lp;
    BWHighpassFilter hp;
    BWBandpassFilter bp;
    lp.set_param("source", dc);
    hp.set_param("source", dc);
    bp.set_param("source", dc);
    lp.prepare(ctx); lp.start(48000);
    hp.prepare(ctx); hp.start(48000);
    bp.prepare(ctx); bp.start(48000);
    float l = 0.0f, h = 1.0f, b = 1.0f;
    for (int i = 0; i < 4800; ++i) { l = lp.next(); h = hp.next(); b = bp.next(); }
    CHECK_NEAR(l, 1.0f, 1e-2f);
    CHECK_NEAR(h, 0.0f, 1e-2f);
    CHECK_NEAR(b, 0.0f, 1e-2f);

    // the cutoff pin moves the corner: a 100 Hz lowpass blocks a 5 kHz tone
    auto tone = std::make_shared<ConstantSource>(0.0f);
    BWLowpassFilter low;
    low.set_param("source", tone);
    low.set_param("cutoffFreq", std::make_shared<ConstantSource>(100.0f));
    low.prepare(ctx); low.start(48000);
    float peak = 0.0f;
    for (int n = 0; n < 4800; ++n) {
        tone->set(std::sin(2.0f * 3.14159265f * 5000.0f * float(n) / 48000.0f));
        const float y = low.next();
        if (n > 2400) peak = std::max(peak, std::fabs(y));
    }
    CHECK(peak < 0.01f);
}

// The vibrato passes the frequency through when the note is shorter than its
// threshold, and wobbles it when the note is long enough.
static void run_vibrato_tests() {
    RenderContext ctx{48000};
    Vibrato off;
    off.set_param("frequency", std::make_shared<ConstantSource>(300.0f));
    off.set_setting("threshold", 1.0f);
    off.prepare(ctx); off.start(4800);                          // 0.1 s: below the threshold
    for (int i = 0; i < 200; ++i) CHECK_NEAR(off.next(), 300.0f, 1e-6f);

    Vibrato on;
    on.set_setting("depth", 0.05f);
    on.set_setting("attack", 0.1f);
    on.set_param("frequency", std::make_shared<ConstantSource>(300.0f));
    on.prepare(ctx); on.start(48000);
    float lo = 1e9f, hi = -1e9f, y = 0.0f;
    bool finite = true;
    for (int i = 0; i < 48000; ++i) {
        y = on.next();
        finite = finite && std::isfinite(y);
        lo = std::min(lo, y); hi = std::max(hi, y);
    }
    CHECK(finite);
    CHECK(hi - lo > 1.0f && lo > 300.0f * 0.9f && hi < 300.0f * 1.1f);
    CHECK(on.current() == y);
}

// The filter family, in registration order.
void run_filter_tests() {
    run_filter_pin_shape_tests();
    run_filter_default_tests();
    run_filter_auto_pin_tests();
    run_filter_advance_order_tests();
    run_filter_setting_hook_tests();
    run_filter_behavior_tests();
    run_filter_butterworth_tests();
    run_vibrato_tests();
}
