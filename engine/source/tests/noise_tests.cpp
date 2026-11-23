#include "mforce/testing/check.h"
#include "test_support.h"
#include "mforce/core/dsp_value_source.h"
#include "mforce/core/pins.h"
#include "mforce/core/randomizer.h"
#include "mforce/source/noise/blue_noise_source.h"
#include "mforce/source/noise/crackle_noise_source.h"
#include "mforce/source/noise/layered_red_noise_source.h"
#include "mforce/source/noise/murmuration_noise_source.h"
#include "mforce/source/noise/perlin_noise_source.h"
#include "mforce/source/noise/pink_noise_source.h"
#include "mforce/source/noise/red_noise_source.h"
#include "mforce/source/noise/shaped_draw.h"
#include "mforce/source/noise/velvet_noise_source.h"
#include "mforce/source/noise/violet_noise_source.h"
#include "mforce/source/noise/wander_noise2_source.h"
#include "mforce/source/noise/wander_noise3_source.h"
#include "mforce/source/noise/wander_noise_source.h"
#include "mforce/source/noise/white_noise_source.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

using namespace mforce;


// Every type prepares and renders finite values, and says which pins it
// declares, in the order they are advanced.
void run_noise_pin_tests() {
    RenderContext ctx{48000};

    auto smoke = [&](ValueSource& s) {
        s.prepare(ctx); s.start(4800);
        for (int i = 0; i < 10; ++i) CHECK(std::isfinite(s.next()));
    };
    WhiteNoiseSource white;           smoke(white);
    PinkNoiseSource pink;             smoke(pink);
    BlueNoiseSource blue;             smoke(blue);
    VioletNoiseSource violet;         smoke(violet);
    VelvetNoiseSource velvet;         smoke(velvet);
    PerlinNoiseSource perlin;         smoke(perlin);
    CrackleNoiseSource crackle;       smoke(crackle);
    MurmurationNoiseSource murmur;    smoke(murmur);
    WanderNoiseSource wander;     smoke(wander);
    WanderNoise2Source wander2;   smoke(wander2);
    WanderNoise3Source wander3;   smoke(wander3);
    RedNoiseSource red;           smoke(red);
    LayeredRedNoiseSource layered; smoke(layered);

    CHECK(param_names(white) == "amplitude,density,boost,continuity,zeroCrossTendency,");
    CHECK(param_names(pink) == "amplitude,");
    CHECK(param_names(blue) == "amplitude,");
    CHECK(param_names(violet) == "amplitude,");
    CHECK(param_names(velvet) == "density,amplitude,");
    CHECK(param_names(perlin) == "speed,octaves,persistence,lacunarity,");
    CHECK(param_names(crackle) == "chaos,");
    CHECK(param_names(murmur) == "count,cohesion,alignment,separation,chaos,speed,");
    CHECK(param_names(wander) == "amplitude,speed,deltaSpeed,slopeLimit,");
    CHECK(param_names(wander2) == "amplitude,minSpeed,maxSpeed,reverseProb,retraceProb,retracePct,");
    CHECK(param_names(wander3) == "amplitude,speed,deltaSpeed,slopeLimit,");
    CHECK(param_names(red) ==
          "amplitude,frequency,phase,density,smoothness,rampVariation,boost,continuity,zeroCrossTendency,");
    CHECK(param_names(layered).empty());
    CHECK(layered.input_descriptors().empty());

    CHECK(white.range() == Generator::Range::Bipolar);
    CHECK(red.range() == Generator::Range::Bipolar);
    CHECK(blue.range() == Generator::Range::None);
    CHECK(murmur.range() == Generator::Range::None);
    CHECK(layered.range() == Generator::Range::None);
}

// WhiteNoiseSource: at the inert defaults one draw per sample, exactly
// the historical sequence; a wired pin is prepared and advanced once
// per sample.
void run_noise_white_tests() {
    RenderContext ctx{48000};

    WhiteNoiseSource w(777u);
    Randomizer ref(777u);
    w.prepare(ctx); w.start(16);
    for (int i = 0; i < 5; ++i) CHECK(w.next() == ref.valuePN());
    auto dens = std::make_shared<PinCounter>();
    dens->level = 1.0f;
    w.set_param("density", dens);
    w.prepare(ctx); w.start(16);
    for (int i = 0; i < 5; ++i) w.next();
    CHECK(dens->starts == 1 && dens->nexts == 5);
    CHECK(w.get_param("density") == dens);
    CHECK(w.get_param("boost") == nullptr);
}

// Three declared defaults: an unwired node reads the descriptor's value, so
// the CLI and the UI agree.
void run_noise_default_tests() {
    RenderContext ctx{48000};

    RedNoiseSource red;
    CHECK_NEAR(red.density.current(), 0.5f, 1e-6f);
    CHECK_NEAR(red.smoothness.current(), 0.5f, 1e-6f);
    CHECK_NEAR(red.rampVariation.current(), 0.0f, 1e-6f);
    CHECK_NEAR(param_default(red, "density"), 0.5f, 1e-6f);
    CHECK_NEAR(param_default(red, "rampVariation"), 0.0f, 1e-6f);

    WanderNoise2Source w2;
    CHECK_NEAR(w2.minSpeed.current(), 0.01f, 1e-6f);
    CHECK_NEAR(w2.maxSpeed.current(), 0.1f, 1e-6f);
    w2.prepare(ctx); w2.start(4800);
    float last = w2.next();
    bool steady = true;
    for (int i = 0; i < 2000; ++i) {              // a walk, not rail to rail
        const float v = w2.next();
        steady = steady && std::fabs(v - last) <= 0.1f + 1e-6f;
        last = v;
    }
    CHECK(steady);
}

// RedNoiseSource keeps WaveSource's three pins and adds its own after
// them; its pins are reached by name, base and own alike.
void run_noise_red_pin_tests() {
    RenderContext ctx{48000};

    RedNoiseSource red(99u);
    auto lfoDepth = std::make_shared<PinCounter>();
    auto zct = std::make_shared<PinCounter>();
    red.set_param("amplitude", lfoDepth);
    red.set_param("zeroCrossTendency", zct);
    CHECK(red.amplitude.source == lfoDepth);
    CHECK(red.zeroCrossTendency.source == zct);
    red.set_param("frequency", std::make_shared<ConstantSource>(100.0f));
    red.prepare(ctx); red.start(4800);
    for (int i = 0; i < 7; ++i) red.next();
    CHECK(lfoDepth->starts == 1 && lfoDepth->nexts == 7);
    CHECK(zct->starts == 1 && zct->nexts == 7);
    red.clear_param("zeroCrossTendency");
    CHECK(red.get_param("zeroCrossTendency") == nullptr);
}

// The walks that never apply `amplitude` leave it unpulled (Manual): it
// is prepared once and never advanced. WanderNoise3 does apply it.
void run_noise_manual_amplitude_tests() {
    RenderContext ctx{48000};

    auto amp1 = std::make_shared<PinCounter>();
    auto amp2 = std::make_shared<PinCounter>();
    auto amp3 = std::make_shared<PinCounter>();
    auto spd = std::make_shared<PinCounter>();
    WanderNoiseSource w1;
    WanderNoise2Source w2;
    WanderNoise3Source w3;
    w1.set_param("amplitude", amp1);
    w1.set_param("speed", spd);
    w2.set_param("amplitude", amp2);
    w3.set_param("amplitude", amp3);
    w1.prepare(ctx); w1.start(4800);
    w2.prepare(ctx); w2.start(4800);
    w3.prepare(ctx); w3.start(4800);
    for (int i = 0; i < 5; ++i) { w1.next(); w2.next(); w3.next(); }
    CHECK(amp1->starts == 1 && amp1->nexts == 0);
    CHECK(amp2->starts == 1 && amp2->nexts == 0);
    CHECK(amp3->starts == 1 && amp3->nexts == 5);
    CHECK(spd->starts == 1 && spd->nexts == 5);
    // The Manual pins were prepared, not only started: they know the rate.
    CHECK(amp1->rate == 48000 && amp2->rate == 48000);
    CHECK(amp3->rate == 48000 && spd->rate == 48000);
}

// LayeredRedNoiseSource: the layer table is two arrays, the layer count a
// setting that follows them.
void run_noise_layered_tests() {
    RenderContext ctx{48000};

    LayeredRedNoiseSource l;
    CHECK(l.setting_descriptors().size() == 1);
    CHECK_NEAR(l.setting_descriptors()[0].default_value, 3.0f, 1e-6f);
    CHECK(l.array_descriptors().size() == 2);
    CHECK_NEAR(l.array_descriptors()[0].default_value, 7.0f, 1e-6f);
    CHECK_NEAR(l.array_descriptors()[1].default_value, 0.05f, 1e-6f);
    CHECK(l.get_array("frequency") == std::vector<float>({0.5f, 2.0f, 8.0f}));
    CHECK(l.get_array("amplitude") == std::vector<float>({1.0f, 1.0f, 1.0f}));
    CHECK_NEAR(l.get_setting("count"), 3.0f, 1e-6f);

    l.set_setting("count", 5.0f);                   // new rows take the declared defaults
    CHECK_NEAR(l.get_setting("count"), 5.0f, 1e-6f);
    CHECK(l.get_array("frequency") == std::vector<float>({0.5f, 2.0f, 8.0f, 7.0f, 7.0f}));
    CHECK(l.get_array("amplitude") == std::vector<float>({1.0f, 1.0f, 1.0f, 0.05f, 0.05f}));
    l.set_setting("count", 99.0f);
    CHECK_NEAR(l.get_setting("count"), 16.0f, 1e-6f);
    CHECK(l.get_array("frequency").size() == 16);
    l.set_setting("count", 0.0f);
    CHECK_NEAR(l.get_setting("count"), 1.0f, 1e-6f);
    CHECK(l.get_array("amplitude").size() == 1);

    l.set_array("frequency", {1.0f, 2.0f});          // the arrays set the count
    CHECK_NEAR(l.get_setting("count"), 2.0f, 1e-6f);
    l.set_array("amplitude", {0.5f, 0.25f});
    CHECK_NEAR(l.get_setting("count"), 2.0f, 1e-6f);
    CHECK(l.get_array("amplitude") == std::vector<float>({0.5f, 0.25f}));
    l.prepare(ctx); l.start(4800);                           // the dirty layers rebuild here
    float sum = 0.0f;
    for (int i = 0; i < 2000; ++i) sum += std::fabs(l.next());
    CHECK(sum > 0.0f);
}

// The shaped-noise draw white and red share: two reference blocks written
// out here against the helper, bit-for-bit over a grid, on
// three identically seeded Randomizers (same seed, same draws).
void run_shaped_draw_twin_tests() {
    auto white_block = [](Randomizer& rng_, float zct, float bst, float cont,
                          float lastVal_, float& lastSign_) {
        float v;
        if (rng_.decide(zct)) {
            lastSign_ = -lastSign_;
        } else {
            lastSign_ = float(rng_.sign());
            if (lastSign_ == 0.0f) lastSign_ = 1.0f;
        }
        const float b = std::clamp(bst, 0.0f, 1.0f);
        v = rng_.range(b, 1.0f) * lastSign_;
        if (cont != 0.0f) {
            const float influence = std::min(cont, 0.999f);
            v = rng_.range(lastVal_, v, lastVal_, influence);
        }
        return v;
    };
    auto red_block = [](Randomizer& rng_, float zct, float boostIn, float contIn,
                        float lastValue_, float& lastSign_) {
        float nextValue_;
        if (rng_.decide(zct)) {
            lastSign_ = -lastSign_;
        } else {
            lastSign_ = float(rng_.sign());
            if (lastSign_ == 0.0f) lastSign_ = 1.0f;
        }
        float b = boostIn;
        b = std::clamp(b, 0.0f, 1.0f);
        nextValue_ = rng_.range(b, 1.0f) * lastSign_;
        float cont = contIn;
        if (cont != 0.0f) {
            float influence = std::min(cont, 0.999f);
            nextValue_ = rng_.range(lastValue_, nextValue_, lastValue_, influence);
        }
        return nextValue_;
    };

    const float grid[] = {0.0f, 0.2f, 0.5f, 0.999f, 1.0f, -0.3f, 1.7f};
    int bad = 0, rows = 0;
    uint32_t seed = 0x5A9Du;
    for (float zct : grid) for (float bst : grid) for (float cont : grid) for (float last : grid) {
        for (float sign0 : {-1.0f, 1.0f}) {
            ++seed; ++rows;
            Randomizer ra(seed), rb(seed), rc(seed);
            float sa = sign0, sb = sign0, sc = sign0;
            for (int k = 0; k < 4; ++k) {          // chain: a reordered draw desyncs later rows
                float a = white_block(ra, zct, bst, cont, last, sa);
                float b = red_block(rb, zct, bst, cont, last, sb);
                float c = shaped_draw(rc, zct, bst, cont, last, sc);
                if (!same_bits(a, b) || !same_bits(a, c)) ++bad;
                if (!same_bits(sa, sb) || !same_bits(sa, sc)) ++bad;
            }
            if (!same_bits(ra.valuePN(), rc.valuePN())) ++bad;   // the draw count agrees too
        }
    }
    CHECK(rows == 7 * 7 * 7 * 7 * 2);
    CHECK(bad == 0);
}
