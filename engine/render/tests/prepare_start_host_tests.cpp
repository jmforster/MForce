#include "mforce/testing/check.h"
#include "test_support.h"
#include "patch/tests/fixtures.h"
#include "mforce/core/dsp_value_source.h"
#include "mforce/core/pins.h"
#include "mforce/render/perform_source.h"
#include "mforce/render/pitched_instrument.h"
#include "mforce/source/additive/additive_source.h"
#include "mforce/source/additive/sequence_partials.h"
#include "mforce/source/filter/distorted_source.h"
#include "mforce/source/noise/white_noise_source.h"
#include "mforce/source/oscillator/sine_source.h"
#include "mforce/source/wavetable/averaging_evolution.h"
#include "mforce/source/wavetable/brass_evolution.h"
#include "mforce/source/wavetable/wavetable_source.h"
#include <algorithm>
#include <cmath>
#include <memory>
#include <optional>
#include <vector>

using namespace mforce;


// Wired cases for the types whose defaults read 0: a brass evolution under a
// wavetable (its rate capture), a distorter with a signal, a sequence of
// partials in an additive source.
void run_wired_split_tests() {
    CHECK(split_equivalent([] {
        auto w = std::make_shared<WavetableSource>();
        auto brass = std::make_shared<BrassEvolution>();
        brass->set_param("breath", std::make_shared<ConstantSource>(0.6f));
        w->set_param("evolution", brass);
        w->set_param("inputSource", std::make_shared<WhiteNoiseSource>(0x8Au));
        return w;
    }, 2048, 3));
    {
        auto brass_at = [](int rate) {
            WavetableSource w;
            auto brass = std::make_shared<BrassEvolution>();
            brass->set_param("breath", std::make_shared<ConstantSource>(0.6f));
            w.set_param("evolution", brass);
            w.set_param("inputSource", std::make_shared<WhiteNoiseSource>(0x8Au));
            w.prepare(RenderContext{rate});
            w.start(2048);
            return render_samples(w, 2048);
        };
        CHECK(!same_bits(brass_at(24000), brass_at(48000)));   // the lip filter is laid out from the rate
    }
    {   // an averaging evolution is a part: it sounds under a wavetable
        const SplitResult r = split_compare([] {
            auto w = std::make_shared<WavetableSource>();
            w->set_param("evolution", std::make_shared<AveragingEvolution>());
            w->set_param("inputSource", std::make_shared<WhiteNoiseSource>(0x5A5Au));
            return w;
        });
        CHECK(r.equal && r.nonzero);
    }
    {
        const SplitResult r = split_compare([] {
            auto d = std::make_shared<DistortedSource>(0x51u);
            d->set_param("source", std::make_shared<SineSource>());
            d->set_param("density", std::make_shared<ConstantSource>(0.5f));
            return d;
        });
        CHECK(r.equal && r.nonzero);
    }
    {
        const SplitResult r = split_compare([] {
            auto f = std::make_shared<AdditiveSource>();
            auto seq = std::make_shared<SequencePartials>();
            seq->set_setting("maxPartials", 12.0f);
            seq->set_setting("unitPO1", 0.3f);
            f->set_param("partials", seq);
            return f;
        }, 2048, 4);
        CHECK(r.equal && r.nonzero);
    }
}

// The hosts: a loaded instrument is prepared by its
// build and only started per note; render takes no context.
void run_host_split_tests() {
    const int n = 48000;
    auto render_twice = [&](const char* path) {
        std::vector<float> a(size_t(n), 0.0f), b(size_t(n), 0.0f);
        for (auto* out : {&a, &b}) {
            auto ip = load_scoreless(path);
            auto* inst = dynamic_cast<PitchedInstrument*>(ip.instrument.get());
            play_notes(*inst, {{.start = 0.0f, .duration = 0.4f, .noteNumber = 60.0f, .velocity = 0.8f, .onset = 0, .hold = false, .curve = std::nullopt},
                               {.start = 0.5f, .duration = 0.4f, .noteNumber = 64.0f, .velocity = 0.8f, .onset = 0, .hold = false, .curve = std::nullopt}});
            ip.instrument->render(out->data(), n);
        }
        CHECK(same_bits(a, b));
        float peak = 0.0f;
        for (float v : a) peak = std::max(peak, std::fabs(v));
        CHECK(peak > 0.01f);
    };
    render_twice("patches/baselines/BaselineSIN.json");
    // PerformSource learns the rate through prepare: the wheel smoother's
    // first step is 1 - exp(-1 / (0.010 s * rate)).
    {
        PerformSource ps;
        auto is = std::make_shared<InstrumentState>();
        is->wheel.store(1.0f);
        ps.set_instrument_state(is);
        ps.prepare(RenderContext{48000});
        ps.tick();
        CHECK_NEAR(ps.wheel(), 1.0f - std::exp(-1.0f / (0.010f * 48000.0f)), 1e-6f);
    }
}
