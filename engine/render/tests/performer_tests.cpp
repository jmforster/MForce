#include "mforce/testing/check.h"
#include "test_support.h"
#include "core/tests/fixtures.h"
#include "mforce/core/pins.h"
#include "mforce/render/perform_source.h"
#include "mforce/source/modulator/curve_node.h"
#include <cmath>
#include <memory>

using namespace mforce;


// The performer's wheel and pressure smoothers, against the reference formula.
void run_dsp_performer_smoother_tests() {
    RenderContext ctx{48000};
    PerformSource ps;
    auto is = std::make_shared<InstrumentState>();
    ps.set_instrument_state(is);
    ps.prepare(ctx);
    const float a = 1.0f - std::exp(-1.0f / (0.010f * 48000.0f));   // the reference coefficient
    float wheel = 0.0f, press = 0.0f;
    auto sig = twin_signal(0x5E00u, 300);
    int bad = 0;
    for (float sv : sig) {
        const float w = 0.5f * (sv + 1.0f), pr = 0.25f * (sv + 1.0f);
        is->wheel.store(w);
        is->pressure.store(pr);
        ps.tick();
        wheel += a * (w - wheel);          // the reference smoothers
        press += a * (pr - press);
        if (!same_bits(ps.wheel(), wheel) || !same_bits(ps.pressure(), press)) ++bad;
    }
    CHECK(bad == 0);
}

void run_perform_source_tests() {
    auto ps = std::make_shared<PerformSource>();
    ps->set_note(220.0f, 0.9f, 48000, 1.0f);
    PerformOut f(ps, PerformOut::Field::Frequency);
    PerformOut v(ps, PerformOut::Field::Velocity);
    PerformOut d(ps, PerformOut::Field::Duration);
    f.next(); v.next(); d.next();
    CHECK_NEAR(f.current(), 220.0f, 1e-6f);
    CHECK_NEAR(v.current(), 0.9f, 1e-6f);
    CHECK_NEAR(d.current(), 1.0f, 1e-6f);
    ps->set_note(440.0f, 0.5f, 24000, 0.5f); // re-strike: adapters follow
    f.next(); v.next(); d.next();
    CHECK_NEAR(f.current(), 440.0f, 1e-6f);
    CHECK_NEAR(v.current(), 0.5f, 1e-6f);
    CHECK_NEAR(d.current(), 0.5f, 1e-6f);   // the duration in seconds
    CHECK(ps->note().durSamples == 24000);

    // Chain: PerformOut freq -> LogX CurveNode, re-evaluates on re-strike
    auto fo = std::make_shared<PerformOut>(ps, PerformOut::Field::Frequency);
    CurveNode cn; cn.interp.value = int(CurveNode::CurveInterp::LogX);
    cn.set_knots(CurveKnots{{100.0f, 0.0f}, {10000.0f, 2.0f}});
    cn.set_param("source", fo);
    cn.next();
    CHECK_NEAR(cn.current(), cn.map(440.0f), 1e-6f);
    ps->set_note(1000.0f, 0.5f, 24000);
    cn.next();
    CHECK_NEAR(cn.current(), 1.0f, 1e-4f);
}
