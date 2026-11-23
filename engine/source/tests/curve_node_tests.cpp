#include "mforce/testing/check.h"
#include "test_support.h"
#include "mforce/core/dsp_value_source.h"
#include "mforce/core/pins.h"
#include "mforce/core/ramp.h"
#include "mforce/source/envelope/envelope.h"
#include "mforce/source/modulator/curve_node.h"
#include <memory>
#include <vector>

using namespace mforce;

using CurveRows = std::vector<CurveNode::ExprKnot>;

void run_curve_node_tests() {
    // Linear (vcurve semantics): clamp + linear interp
    CurveNode lin;
    lin.set_knots(CurveKnots{{0.0f, 0.3f}, {0.2f, 0.3f}, {0.85f, 1.0f}, {1.0f, 1.6f}});
    CHECK_NEAR(lin.map(-1.0f), 0.3f, 1e-6f);   // clamp low
    CHECK_NEAR(lin.map(0.1f),  0.3f, 1e-6f);   // flat segment
    CHECK_NEAR(lin.map(1.0f),  1.6f, 1e-6f);   // clamp high
    CHECK_NEAR(lin.map(0.925f), 1.3f, 1e-3f);  // midpoint of last segment

    // LogX (a frequency curve's usual interp): value linear in log(x)
    CurveNode logx; logx.interp.value = int(CurveNode::CurveInterp::LogX);
    logx.set_knots(CurveKnots{{100.0f, 0.0f}, {10000.0f, 2.0f}});
    CHECK_NEAR(logx.map(1000.0f), 1.0f, 1e-4f);  // geometric midpoint

    // LogLog: 2 knots == exact power law (y = x^2 through (10,100),(100,10000))
    CurveNode ll; ll.interp.value = int(CurveNode::CurveInterp::LogLog);
    ll.set_knots(CurveKnots{{10.0f, 100.0f}, {100.0f, 10000.0f}});
    CHECK_NEAR(ll.map(31.6227766f), 1000.0f, 0.5f);

    // Pull path: source wired, evaluates on change
    auto cs = std::make_shared<ConstantSource>(0.5f);
    CurveNode wired;
    wired.set_knots(CurveKnots{{0.0f, 0.0f}, {1.0f, 10.0f}});
    wired.set_param("source", cs);
    wired.next();
    CHECK_NEAR(wired.current(), 5.0f, 1e-6f);

    // Empty knots = identity
    CurveNode ident;
    CHECK_NEAR(ident.map(123.0f), 123.0f, 1e-6f);
}

void run_curve_expr_tests() {
    using EK = CurveNode::ExprKnot;
    using KF = CurveNode::KnotForm;
    // One Linear knot = global formula with live extrapolation on both sides
    // (the keytrack case: cutoff = 8*f0 everywhere, no 16000-knot needed).
    CurveNode kt; kt.mode.value = 1;
    kt.set_expr_knots(CurveRows{{440.0f, KF::Linear, 8.0f, 0.0f}});
    CHECK_NEAR(kt.map(100.0f),  800.0f,   1e-3f);
    CHECK_NEAR(kt.map(440.0f),  3520.0f,  1e-2f);
    CHECK_NEAR(kt.map(5000.0f), 40000.0f, 1e-1f);

    // A 3-point example: flat 0.1 low, 0.1 - x/50000 high, smooth
    // morph between; equal formulas on the top knots pin the formula exactly
    // over [3200,16000] and keep extrapolating above.
    CurveNode m; m.mode.value = 1;
    m.set_expr_knots(CurveRows{{20.0f,    KF::Linear, 0.0f,      0.1f},
                               {3200.0f,  KF::Linear, -2e-5f,    0.1f},
                               {16000.0f, KF::Linear, -2e-5f,    0.1f}});
    CHECK_NEAR(m.map(10.0f),     0.1f,    1e-6f);   // edge: constant holds
    CHECK_NEAR(m.map(8000.0f),  -0.06f,   1e-5f);   // formula exact mid-span
    CHECK_NEAR(m.map(20000.0f), -0.3f,    1e-5f);   // extrapolates past last
    // Blend region: t = (1610-20)/3180 = 0.5, v0 = 0.1, v1 = 0.0678
    CHECK_NEAR(m.map(1610.0f),   0.0839f, 1e-4f);

    // Power form: y = 2 * x^0.5
    CurveNode pw; pw.mode.value = 1;
    pw.set_expr_knots(CurveRows{{100.0f, KF::Power, 2.0f, 0.5f}});
    CHECK_NEAR(pw.map(100.0f),  20.0f, 1e-4f);
    CHECK_NEAR(pw.map(2500.0f), 100.0f, 1e-3f);

    // Points mode leaves points behavior untouched even with expression rows set
    CurveNode off;
    off.set_expr_knots(CurveRows{{440.0f, KF::Linear, 8.0f, 0.0f}});
    off.set_knots(CurveKnots{{0.0f, 0.0f}, {1.0f, 10.0f}});
    CHECK_NEAR(off.map(0.5f), 5.0f, 1e-6f);
}

void run_envelope_range_tests() {
    RenderContext ctx{48000};
    auto mk = []() {
        auto env = std::make_shared<Envelope>();
        env->add_stage({{0.0f, 1.0f, RampType::Linear, 0.0f}, 0.5f, 0.0f, 0.0f});
        env->add_stage({{1.0f, 1.0f, RampType::Linear, 0.0f}, 0.5f, 0.0f, 0.0f});
        return env;
    };
    // Default (no min/max): final value 1.0 — unchanged behavior
    auto e1 = mk(); e1->prepare(ctx); e1->start(4800);
    float last = 0; for (int i = 0; i < 4300; ++i) last = e1->next();
    CHECK_NEAR(last, 1.0f, 1e-3f);

    // minValue 80, maxValue 500 (constants): early sample near 80, late 500
    auto e2 = mk();
    e2->set_param("minValue", std::make_shared<ConstantSource>(80.0f));
    e2->set_param("maxValue", std::make_shared<ConstantSource>(500.0f));
    e2->prepare(ctx); e2->start(4800);
    float first = e2->next();
    CHECK(first < 120.0f && first >= 80.0f);
    for (int i = 1; i < 4300; ++i) last = e2->next();
    CHECK_NEAR(last, 500.0f, 1.0f);

    // get_param roundtrip
    CHECK(e2->get_param("maxValue") != nullptr);
    CHECK(e1->get_param("maxValue") == nullptr);
}

void run_stage_nominal_tests() {
    RenderContext ctx{48000};
    auto env = std::make_shared<Envelope>();
    // 0->1 attack with nominal 0.25 s (percent tiny), then gated expand hold.
    Envelope::Stage a{{0.0f, 1.0f, RampType::Linear, 0.0f}, 0.001f, 0.0f, 0.0f};
    a.nominal = 0.25f;
    env->add_stage(a);
    env->add_stage({{1.0f, 1.0f, RampType::Linear, 0.0f}, 0.0f, 0.0f, 0.0f});
    env->set_gated(true);
    env->prepare(ctx); env->start(48000);  // nominal note duration 1 s
    // Nominal honored: attack = 12000 samples; value at sample 6000 ~ 0.5.
    // Without nominal the 0.001-pct attack finishes in ~48 samples.
    float v = 0; for (int i = 0; i < 6000; ++i) v = env->next();
    CHECK(v > 0.3f && v < 0.7f);

    // NOT gated: nominal ignored, tiny attack — value ~1.0 by sample 6000.
    auto env2 = std::make_shared<Envelope>();
    Envelope::Stage a2{{0.0f, 1.0f, RampType::Linear, 0.0f}, 0.001f, 0.0f, 0.0f};
    a2.nominal = 0.25f;
    env2->add_stage(a2);
    env2->add_stage({{1.0f, 1.0f, RampType::Linear, 0.0f}, 0.0f, 0.0f, 0.0f});
    env2->prepare(ctx); env2->start(48000);
    float v2 = 0; for (int i = 0; i < 6000; ++i) v2 = env2->next();
    CHECK_NEAR(v2, 1.0f, 1e-3f);
}
