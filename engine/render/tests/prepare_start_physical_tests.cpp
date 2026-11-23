#include "mforce/testing/check.h"
#include "test_support.h"
#include "mforce/core/dsp_value_source.h"
#include "mforce/core/pins.h"
#include "mforce/core/source_registry.h"
#include "mforce/source/filter/shaper_source.h"
#include "mforce/source/noise/white_noise_source.h"
#include "mforce/source/oscillator/sine_source.h"
#include "mforce/source/physical/allpass_resonator.h"
#include "mforce/source/physical/delay_line_source.h"
#include "mforce/source/physical/ks_string.h"
#include "mforce/source/physical/mesh2d_source.h"
#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

using namespace mforce;


// ---------------------------------------------------------------------------
// Physical models: the rate arrives through prepare(ctx), the note through
// start(frames). A constant on a pin cannot show a missing call, so the
// shaper's pins here are PinCounters that record the rate and the starts they
// were given.
// ---------------------------------------------------------------------------
static void run_shaper_pin_split_tests() {
    ShaperSource sh;
    const char* names[] = {"source", "drive", "smoothness", "morph", "breakaway", "capture"};
    std::vector<std::shared_ptr<PinCounter>> pins;
    for (const char* n : names) {
        pins.push_back(std::make_shared<PinCounter>());
        sh.set_param(n, pins.back());
    }
    sh.prepare(RenderContext{48000});
    for (const auto& p : pins) CHECK(p->rate == 48000 && p->starts == 0);   // prepared, not started
    sh.start(64);
    for (const auto& p : pins) CHECK(p->rate == 48000 && p->starts == 1);   // started once
    // A second prepare is idempotent and starts nothing; each start reaches each pin.
    sh.prepare(RenderContext{48000});
    sh.start(64);
    for (const auto& p : pins) CHECK(p->rate == 48000 && p->starts == 2);
    // The shaper itself, with the drive and the input wired to real sources.
    CHECK(split_equivalent([] {
        auto s = std::make_shared<ShaperSource>();
        s->set_param("source", std::make_shared<SineSource>());
        s->set_param("drive", std::make_shared<ConstantSource>(2.0f));
        return s;
    }));
}

// A read position a hair under the write index rounds up to the ring length
// in float: the read wraps to slot 0, the ring's first write, on every lap.
static void run_delay_line_wrap_tests() {
    DelayLineSource dl;
    dl.set_param("source", std::make_shared<Counter>());   // 1, 2, 3, ...: consecutive samples differ, so a wrong fraction shows
    dl.set_param("frequency", std::make_shared<ConstantSource>(std::nextafter(1470.0f, 0.0f)));
    dl.prepare(RenderContext{22050});
    dl.start(64);
    const int ring = dl.capacity();
    const auto out = render_samples(dl, 32);
    CHECK_NEAR(out[15], 1.0f, 1e-4f);    // write index 15, delay 15.000001: slot 0, the first write (1)
    CHECK(out[14] == 0.0f);              // still the empty ring
    CHECK_NEAR(out[16], 2.0f, 1e-3f);    // one past the wrap: between slot 0 and slot 1, the second write (2)
    CHECK(std::all_of(out.begin(), out.end(), [](float v) { return std::isfinite(v); }));
    // Past one full ring the write index has wrapped; the read at the same
    // phase wraps to slot 0 again, which now holds the second lap's first
    // write (sample `ring`: the Counter at ring + 1).
    const auto more = render_samples(dl, ring);      // samples 32 .. 32 + ring
    CHECK_NEAR(more[size_t(ring + 15 - 32)], float(ring + 1), 1e-2f);
    CHECK_NEAR(more[size_t(ring + 16 - 32)], float(ring + 2), 1e-2f);
}

void run_physical_split_tests() {
    const auto noise = [] { return std::make_shared<WhiteNoiseSource>(0x5EEDu); };
    CHECK(split_equivalent([&] {
        auto k = std::make_shared<KSString>();
        k->set_param("source", noise());
        return k;
    }));
    {   // the registry's string is the same type; excited, it sounds
        const SplitResult r = split_compare([&] {
            auto k = std::dynamic_pointer_cast<ValueSource>(SourceRegistry::instance().create("KSString"));
            k->set_param("source", noise());
            return k;
        });
        CHECK(r.equal && r.nonzero);
    }
    CHECK(split_equivalent([&] {
        auto a = std::make_shared<AllpassResonator>();
        a->set_param("source", noise());
        return a;
    }));
    CHECK(split_equivalent([] {
        auto d = std::make_shared<DelayLineSource>();
        d->set_param("source", std::make_shared<SineSource>());
        return d;
    }));
    CHECK(split_equivalent([] {
        auto m = std::make_shared<Mesh2DSource>();
        m->set_param("source", std::make_shared<SineSource>());
        m->set_setting("edgeMode", 1.0f);
        return m;
    }));
    for (const char* type : {"PierceFilter", "BowTable", "Wormhole"}) {
        CHECK(split_equivalent([&] {
            auto s = std::dynamic_pointer_cast<ValueSource>(SourceRegistry::instance().create(type));
            s->set_param("source", std::make_shared<SineSource>());
            return s;
        }));
    }
    run_shaper_pin_split_tests();
    run_delay_line_wrap_tests();

    // The delay line follows the rate its host prepared it at: the same
    // rate again changes nothing, a new rate re-sizes it.
    DelayLineSource dl;
    dl.prepare(RenderContext{22050});
    CHECK(dl.capacity() == 22050 / 20 + 4);
    dl.prepare(RenderContext{22050});
    CHECK(dl.capacity() == 22050 / 20 + 4);
    dl.prepare(RenderContext{48000});
    CHECK(dl.capacity() == 48000 / 20 + 4);

    // The strings' comb lengths come from the rate in init_note; Mesh2D's
    // boundary filter from the rate it was prepared with.
    auto at = [&](int rate, auto make) {
        auto s = make();
        s->prepare(RenderContext{rate});
        s->start(2048);
        return render_samples(*s, 2048);
    };
    const auto ks = [&] {
        auto k = std::make_shared<KSString>();
        k->set_param("source", noise());
        return k;
    };
    const auto lo = at(24000, ks), hi = at(48000, ks);
    CHECK(!same_bits(lo, hi));
    CHECK(std::all_of(hi.begin(), hi.end(), [](float v) { return std::isfinite(v); }));
    CHECK(std::any_of(hi.begin(), hi.end(), [](float v) { return v != 0.0f; }));
    const auto mesh = [] {
        auto m = std::make_shared<Mesh2DSource>();
        m->set_param("source", std::make_shared<SineSource>());
        m->set_setting("edgeMode", 1.0f);
        return m;
    };
    CHECK(!same_bits(at(24000, mesh), at(48000, mesh)));
}
