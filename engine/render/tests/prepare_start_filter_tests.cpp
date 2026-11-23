#include "mforce/testing/check.h"
#include "test_support.h"
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
#include "mforce/source/oscillator/sine_source.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace mforce;


// ---------------------------------------------------------------------------
// Filters and Vibrato: the rate arrives through prepare(ctx), the note through
// start(frames). Every filter is built here by name with `in` on its input.
// A rate-dependence check feeds DC, because a Sine would differ between two
// rates by itself and hide a coefficient that ignored the rate.
// ---------------------------------------------------------------------------
static const char* const kFilterNames[] = {
    "BWLowpass", "BWHighpass", "BWBandpass", "SVF", "SVF1P", "Delay", "Limiter",
    "Reverb", "Hammer", "BiquadRes", "BiquadRaw"};

static std::shared_ptr<ValueSource> make_filter(const std::string& name,
                                                std::shared_ptr<ValueSource> in) {
    std::shared_ptr<ValueSource> n;
    if (name == "BWLowpass")       n = std::make_shared<BWLowpassFilter>();
    else if (name == "BWHighpass") n = std::make_shared<BWHighpassFilter>();
    else if (name == "BWBandpass") n = std::make_shared<BWBandpassFilter>();
    else if (name == "Delay")      n = std::make_shared<DelayFilter>();
    else if (name == "Limiter")    n = std::make_shared<Limiter>();
    else if (name == "Reverb")     n = std::make_shared<Reverb>();
    else if (name == "Hammer")     n = std::make_shared<HammerBank>();
    else if (name == "SVF" || name == "SVF1P") {
        n = std::make_shared<SVFSource>();
        if (name == "SVF1P") n->set_setting("mode", float(SVFSource::kLowpass1P));
    } else {
        n = std::make_shared<BiquadSource>();
        if (name == "BiquadRes") {
            n->set_setting("mode", float(BiquadSource::kResonance));
            n->set_param("frequency", std::make_shared<ConstantSource>(1000.0f));
            n->set_param("radius", std::make_shared<ConstantSource>(0.9f));
        } else {
            n->set_setting("b0", 0.5f);
            n->set_setting("b1", 0.3f);
            n->set_setting("a1", -0.2f);
        }
    }
    n->set_param("source", std::move(in));
    return n;
}

// Rendered at `rate` with DC in: whatever differs between two rates came from
// the node's own use of the rate.
static std::vector<float> filter_at(const std::string& name, int rate) {
    auto f = make_filter(name, std::make_shared<ConstantSource>(1.0f));
    f->prepare(RenderContext{rate});
    f->start(4096);
    return render_samples(*f, 4096);
}

static void run_filter_rate_tests() {
    for (const std::string name : kFilterNames) {
        if (name != "BiquadRaw") {   // a raw biquad's coefficients are settings, not the rate
            const auto lo = filter_at(name, 24000), hi = filter_at(name, 48000);
            if (same_bits(lo, hi)) std::printf("  rate ignored: %s\n", name.c_str());
            CHECK(!same_bits(lo, hi));
            CHECK(std::all_of(hi.begin(), hi.end(), [](float v) { return std::isfinite(v); }));
            CHECK(std::any_of(hi.begin(), hi.end(), [](float v) { return v != 0.0f; }));
        }
        const auto make = [&] { return make_filter(name, std::make_shared<ConstantSource>(1.0f)); };
        const bool down = follows_rate(make, 48000, 22050), up = follows_rate(make, 22050, 48000);
        if (!down || !up) std::printf("  rate not followed: %s\n", name.c_str());
        CHECK(down);
        CHECK(up);
    }
}

// The pins the node prepares and starts itself, the buffers the rate sizes,
// and the filters whose history is not a per-note state.
static void run_filter_pin_split_tests() {
    BiquadSource bq;
    bq.set_setting("mode", float(BiquadSource::kResonance));
    auto freq = std::make_shared<PinCounter>(), rad = std::make_shared<PinCounter>();
    bq.set_param("frequency", freq);
    bq.set_param("radius", rad);
    bq.prepare(RenderContext{48000});
    CHECK(freq->rate == 48000 && rad->rate == 48000);
    CHECK(freq->starts == 0 && rad->starts == 0);        // prepared, not started
    bq.start(64);
    CHECK(freq->starts == 1 && rad->starts == 1);        // started once
    bq.prepare(RenderContext{48000});
    bq.start(64);
    CHECK(freq->starts == 2 && rad->starts == 2);

    DelayFilter dly;
    dly.prepare(RenderContext{22050});
    CHECK(dly.capacity() == 22050);
    dly.prepare(RenderContext{22050});
    CHECK(dly.capacity() == 22050);
    dly.prepare(RenderContext{48000});
    CHECK(dly.capacity() == 48000);
    Limiter lim;
    lim.prepare(RenderContext{22050});
    CHECK(lim.capacity() == 110);
    lim.prepare(RenderContext{22050});
    CHECK(lim.capacity() == 110);
    lim.prepare(RenderContext{48000});
    CHECK(lim.capacity() == 240);

    // The Butterworth histories carry across notes (no per-note reset): after
    // a settled DC note, the next note's first sample is still on the plateau.
    for (const char* name : {"BWLowpass", "BWHighpass"}) {
        auto f = make_filter(name, std::make_shared<ConstantSource>(1.0f));
        f->prepare(RenderContext{48000});
        f->start(4800);
        const float settled = render_samples(*f, 4800).back();
        f->start(4800);
        const float firstOfNext = f->next();
        CHECK_NEAR(firstOfNext, settled, 1e-3f);
    }
}

// A started note carries nothing of the note before it: the second note of a
// used node renders what a fresh node's first note renders. (The Butterworth
// filters are the exception: their histories run on across notes.)
static bool restarts_clean(const std::string& name) {
    const auto make = [&] { return make_filter(name, std::make_shared<SineSource>()); };
    auto used = make(), fresh = make();
    used->prepare(RenderContext{48000});
    used->start(50000);
    render_samples(*used, 49000);          // past one lap of the longest buffer (the delay's, one second)
    used->start(4096);
    fresh->prepare(RenderContext{48000});
    fresh->start(4096);
    return same_bits(render_samples(*used, 4096), render_samples(*fresh, 4096));
}

void run_filter_split_tests() {
    for (const std::string name : kFilterNames) {
        const bool ok = split_equivalent(
            [&] { return make_filter(name, std::make_shared<SineSource>()); });
        if (!ok) std::printf("  split differs: %s\n", name.c_str());
        CHECK(ok);
        if (name.compare(0, 2, "BW") == 0) continue;
        const bool clean = restarts_clean(name);
        if (!clean) std::printf("  note not clean: %s\n", name.c_str());
        CHECK(clean);
    }
    run_filter_rate_tests();
    run_filter_pin_split_tests();
}

static std::shared_ptr<Vibrato> make_vibrato(float speed) {
    auto v = std::make_shared<Vibrato>();
    v->set_param("frequency", std::make_shared<ConstantSource>(440.0f));
    v->set_setting("depth", 0.1f);
    v->set_setting("speed", speed);
    return v;
}

// A Vibrato builds its LFO once and resets it per note, which has to be the
// chain a note used to get by building it fresh.
void run_vibrato_split_tests() {
    constexpr int kNote = 24000;
    {
        auto reused = make_vibrato(6.0f), fresh = make_vibrato(6.0f);
        reused->prepare(RenderContext{48000});
        reused->start(kNote);
        render_samples(*reused, 1000);        // mid-ramp: the noise has drawn, the envelopes have moved
        reused->start(kNote);
        fresh->prepare(RenderContext{48000});
        fresh->start(kNote);
        const auto a = render_samples(*reused, kNote), b = render_samples(*fresh, kNote);
        CHECK(same_bits(a, b));
        const auto [lo, hi] = std::minmax_element(b.begin(), b.end());
        CHECK(*hi - *lo > 1.0f);              // the LFO is audible, so the comparison means something
    }
    CHECK(split_equivalent([] { return make_vibrato(6.0f); }, kNote, 5));

    // A setting changed after prepare rebuilds the chain under the context
    // the node holds: the same bytes as the setting made before prepare.
    {
        auto late = make_vibrato(6.0f), early = make_vibrato(7.0f);
        late->prepare(RenderContext{48000});
        late->set_setting("speed", 7.0f);
        late->start(kNote);
        early->prepare(RenderContext{48000});
        early->start(kNote);
        CHECK(same_bits(render_samples(*late, kNote), render_samples(*early, kNote)));
    }

    // A note below the threshold passes the frequency through and leaves the
    // chain alone; the next long note is a fresh-chain note.
    {
        auto v = make_vibrato(6.0f), fresh = make_vibrato(6.0f);
        v->set_setting("threshold", 0.2f);
        fresh->set_setting("threshold", 0.2f);
        v->prepare(RenderContext{48000});
        v->start(4800);                       // 0.1 s: off
        for (float y : render_samples(*v, 200)) CHECK(y == 440.0f);
        v->start(kNote);
        fresh->prepare(RenderContext{48000});
        fresh->start(kNote);
        CHECK(same_bits(render_samples(*v, kNote), render_samples(*fresh, kNote)));
    }
    CHECK(follows_rate([] { return make_vibrato(6.0f); }, 48000, 24000));
    CHECK(follows_rate([] { return make_vibrato(6.0f); }, 24000, 48000));
}

// A setting changed on a Vibrato that is playing a note: the note keeps the
// chain it started with, and the next start plays the new one.
void run_vibrato_late_setting_tests() {
    auto make = [] {
        auto v = std::make_shared<Vibrato>();
        v->set_param("frequency", std::make_shared<ConstantSource>(440.0f));
        v->set_setting("depth", 0.1f);
        v->set_setting("speed", 6.0f);
        v->prepare(RenderContext{48000});
        return v;
    };
    auto edited = make(), control = make();
    edited->start(4096); control->start(4096);
    const auto e1 = render_samples(*edited, 1000), c1 = render_samples(*control, 1000);
    edited->set_setting("speed", 7.0f);                 // mid-note
    const auto e2 = render_samples(*edited, 1000), c2 = render_samples(*control, 1000);
    CHECK(same_bits(e1, c1) && same_bits(e2, c2));      // the playing note is untouched
    edited->start(4096);
    auto fresh = std::make_shared<Vibrato>();
    fresh->set_param("frequency", std::make_shared<ConstantSource>(440.0f));
    fresh->set_setting("depth", 0.1f);
    fresh->set_setting("speed", 7.0f);
    fresh->prepare(RenderContext{48000});
    fresh->start(4096);
    CHECK(same_bits(render_samples(*edited, 4096), render_samples(*fresh, 4096)));   // the next note is the new chain
}
