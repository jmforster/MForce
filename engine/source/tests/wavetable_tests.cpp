#include "mforce/testing/check.h"
#include "test_support.h"
#include "mforce/core/descriptors.h"
#include "mforce/core/dsp_value_source.h"
#include "mforce/core/pins.h"
#include "mforce/source/wavetable/hybrid_ks_source.h"
#include "mforce/source/wavetable/pluck_evolution.h"
#include "mforce/source/wavetable/reed_evolution.h"
#include "mforce/source/wavetable/wavetable_source.h"
#include <algorithm>
#include <cmath>
#include <memory>

using namespace mforce;


// WavetableSource: WaveSource's three pins, then speedFactor, inputSource and
// evolution, which the node advances itself (Manual): inputSource while a
// note's table is filled, speedFactor only in the interpolating read.
void run_wavetable_pin_tests() {
    RenderContext ctx{48000};

    WavetableSource w;
    CHECK(param_names(w) == "amplitude,frequency,phase,speedFactor,");
    CHECK(input_names(w) == "inputSource,evolution,");
    CHECK(setting_names(w) == "interpolate,");
    CHECK(w.setting_descriptors()[0].type == SettingType::Bool);
    CHECK(w.range() == Generator::Range::Bipolar);
    CHECK(w.get_param("inputSource") != nullptr);          // the seed noise
    CHECK(w.get_param("evolution") == nullptr);
    CHECK(w.get_param("speedFactor") == nullptr);
    CHECK_NEAR(w.speedFactor.current(), 1.0f, 1e-6f);
    w.prepare(ctx); w.start(4800);
    for (int i = 0; i < 10; ++i) CHECK(std::isfinite(w.next()));

    WavetableSource f;  // 480 Hz: a table of 100
    auto seed = std::make_shared<PinCounter>();
    f.set_param("inputSource", seed);
    f.set_param("frequency", std::make_shared<ConstantSource>(480.0f));
    f.prepare(ctx); f.start(4800);
    for (int i = 0; i < 250; ++i) f.next();
    CHECK(seed->starts == 1 && seed->nexts == 100);
    CHECK(seed->rate == 48000);                      // prepared, not only started

    for (bool interp : {false, true}) {
        WavetableSource s;
        auto sf = std::make_shared<PinCounter>();
        s.set_param("speedFactor", sf);
        s.set_setting("interpolate", interp ? 1.0f : 0.0f);
        CHECK(s.get_setting("interpolate") == (interp ? 1.0f : 0.0f));
        s.prepare(ctx); s.start(4800);
        for (int i = 0; i < 20; ++i) CHECK(std::isfinite(s.next()));
        CHECK(sf->starts == 1);
        CHECK(sf->rate == 48000);
        CHECK(sf->nexts == (interp ? 20 : 0));
    }
}

// The evolution slot holds the evolution; the node prepares and starts it
// through the family's base and evolves once per sample.
void run_wavetable_evolution_tests() {
    RenderContext ctx{48000};

    WavetableSource w;
    auto reed = std::make_shared<ReedEvolution>();
    auto breath = std::make_shared<PinCounter>();
    reed->set_param("breath", breath);
    w.set_param("evolution", reed);
    CHECK(w.get_part("evolution") == reed);
    CHECK(w.evolution.get() == reed.get());
    w.prepare(ctx); w.start(4800);
    CHECK(breath->starts == 1);                          // wavetable -> evolution -> pin
    for (int i = 0; i < 10; ++i) CHECK(std::isfinite(w.next()));
    CHECK(breath->nexts == 10);
    w.clear_param("evolution");                            // no evolution, no more pulls
    CHECK(w.get_part("evolution") == nullptr);
    for (int i = 0; i < 10; ++i) w.next();
    CHECK(breath->nexts == 10);

    WavetableSource n;  // a signal is not an evolution: the slot refuses it, the node renders with no evolution
    CHECK(!n.set_param("evolution", std::make_shared<ConstantSource>(0.5f)));
    CHECK(n.get_part("evolution") == nullptr);
    n.prepare(ctx); n.start(4800);
    for (int i = 0; i < 10; ++i) CHECK(std::isfinite(n.next()));

    WavetableSource o;  // a pluck in the slot renders a non-zero table
    o.set_param("evolution", std::make_shared<PluckEvolution>(1u));
    o.prepare(ctx); o.start(4800);
    float peak = 0.0f;
    for (int i = 0; i < 2000; ++i) peak = std::max(peak, std::fabs(o.next()));
    CHECK(peak > 0.0f);
}

// HybridKSSource: WaveSource's three pins, the seed waveform (Manual: pulled
// while a note's table is filled) and three settings.
void run_hybrid_ks_tests() {
    RenderContext ctx{48000};

    HybridKSSource h;
    CHECK(param_names(h) == "amplitude,frequency,phase,");
    CHECK(input_names(h) == "inputSource,");
    CHECK(setting_names(h) == "holdCycles,morphDuration,numPartials,");
    CHECK_NEAR(h.get_setting("holdCycles"), 5.0f, 1e-6f);
    CHECK_NEAR(h.get_setting("morphDuration"), 0.5f, 1e-6f);
    CHECK_NEAR(h.get_setting("numPartials"), 30.0f, 1e-6f);
    CHECK(h.range() == Generator::Range::Bipolar);
    CHECK(h.get_param("inputSource") != nullptr);
    h.prepare(ctx); h.start(4800);
    for (int i = 0; i < 10; ++i) CHECK(std::isfinite(h.next()));

    HybridKSSource k;  // 480 Hz: a table of 100
    auto seed = std::make_shared<PinCounter>();
    k.set_param("inputSource", seed);
    k.set_param("frequency", std::make_shared<ConstantSource>(480.0f));
    k.set_setting("holdCycles", 2.0f);
    k.set_setting("numPartials", 4.0f);
    k.set_setting("morphDuration", 0.01f);
    CHECK_NEAR(k.get_setting("holdCycles"), 2.0f, 1e-6f);
    k.prepare(ctx); k.start(4800);
    bool finite = true;
    for (int i = 0; i < 1000; ++i) finite = finite && std::isfinite(k.next());   // through the switch
    CHECK(finite);
    CHECK(seed->starts == 1 && seed->nexts == 100);
}
