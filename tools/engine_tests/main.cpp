// Assert-style engine unit tests (PerformSource P1, plan_perform_source_p1.md).
// House testing is render-based; this exe covers the value-level semantics
// renders can't isolate (curve interp identity, envelope mapping, adapters).
#include <cstdio>
#include <cmath>
#include <cstdlib>

static int g_checks = 0, g_fails = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_fails; \
  std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_NEAR(a, b, eps) do { ++g_checks; float _a=(a), _b=(b); \
  if (std::fabs(_a-_b) > (eps)) { ++g_fails; \
  std::printf("FAIL %s:%d  %s=%g vs %s=%g\n", __FILE__, __LINE__, #a, _a, #b, _b); } } while (0)

#include "mforce/core/curve_node.h"
#include <memory>
using namespace mforce;

static void run_curve_node_tests() {
    // Linear (vcurve semantics): clamp + linear interp
    CurveNode lin;
    lin.knots = {{0.0f, 0.3f}, {0.2f, 0.3f}, {0.85f, 1.0f}, {1.0f, 1.6f}};
    CHECK_NEAR(lin.map(-1.0f), 0.3f, 1e-6f);   // clamp low
    CHECK_NEAR(lin.map(0.1f),  0.3f, 1e-6f);   // flat segment
    CHECK_NEAR(lin.map(1.0f),  1.6f, 1e-6f);   // clamp high
    CHECK_NEAR(lin.map(0.925f), 1.3f, 1e-3f);  // midpoint of last segment

    // LogX (default paramMap curve semantics): value linear in log(x)
    CurveNode logx; logx.interp = CurveNode::CurveInterp::LogX;
    logx.knots = {{100.0f, 0.0f}, {10000.0f, 2.0f}};
    CHECK_NEAR(logx.map(1000.0f), 1.0f, 1e-4f);  // geometric midpoint

    // LogLog: 2 knots == exact power law (y = x^2 through (10,100),(100,10000))
    CurveNode ll; ll.interp = CurveNode::CurveInterp::LogLog;
    ll.knots = {{10.0f, 100.0f}, {100.0f, 10000.0f}};
    CHECK_NEAR(ll.map(31.6227766f), 1000.0f, 0.5f);

    // Pull path: source wired, evaluates on change
    auto cs = std::make_shared<ConstantSource>(0.5f);
    CurveNode wired;
    wired.knots = {{0.0f, 0.0f}, {1.0f, 10.0f}};
    wired.set_param("source", cs);
    wired.next();
    CHECK_NEAR(wired.current(), 5.0f, 1e-6f);

    // Empty knots = identity
    CurveNode ident;
    CHECK_NEAR(ident.map(123.0f), 123.0f, 1e-6f);
}

static void run_curve_expr_tests() {
    using EK = CurveNode::ExprKnot;
    using KF = CurveNode::KnotForm;
    // One Linear knot = global formula with live extrapolation on both sides
    // (the keytrack case: cutoff = 8*f0 everywhere, no 16000-knot needed).
    CurveNode kt; kt.exprMode = true;
    kt.exprKnots = {{440.0f, KF::Linear, 8.0f, 0.0f}};
    CHECK_NEAR(kt.map(100.0f),  800.0f,   1e-3f);
    CHECK_NEAR(kt.map(440.0f),  3520.0f,  1e-2f);
    CHECK_NEAR(kt.map(5000.0f), 40000.0f, 1e-1f);

    // Matt's 3-point example: flat 0.1 low, 0.1 - x/50000 high, smooth
    // morph between; equal formulas on the top knots pin the formula exactly
    // over [3200,16000] and keep extrapolating above.
    CurveNode m; m.exprMode = true;
    m.exprKnots = {{20.0f,    KF::Linear, 0.0f,      0.1f},
                   {3200.0f,  KF::Linear, -2e-5f,    0.1f},
                   {16000.0f, KF::Linear, -2e-5f,    0.1f}};
    CHECK_NEAR(m.map(10.0f),     0.1f,    1e-6f);   // edge: constant holds
    CHECK_NEAR(m.map(8000.0f),  -0.06f,   1e-5f);   // formula exact mid-span
    CHECK_NEAR(m.map(20000.0f), -0.3f,    1e-5f);   // extrapolates past last
    // Blend region: t = (1610-20)/3180 = 0.5, v0 = 0.1, v1 = 0.0678
    CHECK_NEAR(m.map(1610.0f),   0.0839f, 1e-4f);

    // Power form: y = 2 * x^0.5
    CurveNode pw; pw.exprMode = true;
    pw.exprKnots = {{100.0f, KF::Power, 2.0f, 0.5f}};
    CHECK_NEAR(pw.map(100.0f),  20.0f, 1e-4f);
    CHECK_NEAR(pw.map(2500.0f), 100.0f, 1e-3f);

    // exprMode off leaves points behavior untouched even with exprKnots set
    CurveNode off;
    off.exprKnots = {{440.0f, KF::Linear, 8.0f, 0.0f}};
    off.knots = {{0.0f, 0.0f}, {1.0f, 10.0f}};
    CHECK_NEAR(off.map(0.5f), 5.0f, 1e-6f);
}

#include "mforce/core/envelope.h"

static void run_envelope_range_tests() {
    RenderContext ctx{48000};
    auto mk = []() {
        auto env = std::make_shared<Envelope>(48000);
        env->add_stage({{0.0f, 1.0f, RampType::Linear, 0.0f}, 0.5f, 0.0f, 0.0f});
        env->add_stage({{1.0f, 1.0f, RampType::Linear, 0.0f}, 0.5f, 0.0f, 0.0f});
        return env;
    };
    // Default (no min/max): final value 1.0 — unchanged behavior
    auto e1 = mk(); e1->prepare(ctx, 4800);
    float last = 0; for (int i = 0; i < 4300; ++i) last = e1->next();
    CHECK_NEAR(last, 1.0f, 1e-3f);

    // minValue 80, maxValue 500 (constants): early sample near 80, late 500
    auto e2 = mk();
    e2->set_param("minValue", std::make_shared<ConstantSource>(80.0f));
    e2->set_param("maxValue", std::make_shared<ConstantSource>(500.0f));
    e2->prepare(ctx, 4800);
    float first = e2->next();
    CHECK(first < 120.0f && first >= 80.0f);
    for (int i = 1; i < 4300; ++i) last = e2->next();
    CHECK_NEAR(last, 500.0f, 1.0f);

    // get_param roundtrip
    CHECK(e2->get_param("maxValue") != nullptr);
    CHECK(e1->get_param("maxValue") == nullptr);
}

static void run_stage_nominal_tests() {
    RenderContext ctx{48000};
    auto env = std::make_shared<Envelope>(48000);
    // 0->1 attack with nominal 0.25 s (percent tiny), then gated expand hold.
    Envelope::Stage a{{0.0f, 1.0f, RampType::Linear, 0.0f}, 0.001f, 0.0f, 0.0f};
    a.nominal = 0.25f;
    env->add_stage(a);
    env->add_stage({{1.0f, 1.0f, RampType::Linear, 0.0f}, 0.0f, 0.0f, 0.0f});
    env->set_gated(true);
    env->prepare(ctx, 48000);  // nominal note duration 1 s
    // Nominal honored: attack = 12000 samples; value at sample 6000 ~ 0.5.
    // Without nominal the 0.001-pct attack finishes in ~48 samples.
    float v = 0; for (int i = 0; i < 6000; ++i) v = env->next();
    CHECK(v > 0.3f && v < 0.7f);

    // NOT gated: nominal ignored, tiny attack — value ~1.0 by sample 6000.
    auto env2 = std::make_shared<Envelope>(48000);
    Envelope::Stage a2{{0.0f, 1.0f, RampType::Linear, 0.0f}, 0.001f, 0.0f, 0.0f};
    a2.nominal = 0.25f;
    env2->add_stage(a2);
    env2->add_stage({{1.0f, 1.0f, RampType::Linear, 0.0f}, 0.0f, 0.0f, 0.0f});
    env2->prepare(ctx, 48000);
    float v2 = 0; for (int i = 0; i < 6000; ++i) v2 = env2->next();
    CHECK_NEAR(v2, 1.0f, 1e-3f);
}

#include "mforce/render/perform_source.h"

static void run_perform_source_tests() {
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
    CHECK_NEAR(d.current(), 0.5f, 1e-6f);   // backlog 64 Tier 1
    CHECK(ps->note().durSamples == 24000);

    // Chain: PerformOut freq -> LogX CurveNode, re-evaluates on re-strike
    auto fo = std::make_shared<PerformOut>(ps, PerformOut::Field::Frequency);
    CurveNode cn; cn.interp = CurveNode::CurveInterp::LogX;
    cn.knots = {{100.0f, 0.0f}, {10000.0f, 2.0f}};
    cn.set_param("source", fo);
    cn.next();
    CHECK_NEAR(cn.current(), cn.map(440.0f), 1e-6f);
    ps->set_note(1000.0f, 0.5f, 24000);
    cn.next();
    CHECK_NEAR(cn.current(), 1.0f, 1e-4f);
}

static void run_tap_guard_tests() {
    // Unguarded RefSource: passes values through untouched (existing behavior)
    auto c = std::make_shared<ConstantSource>(42.0f);
    c->next();
    RefSource plain(c);
    CHECK_NEAR(plain.next(), 42.0f, 1e-6f);

    // Guarded: clamp to +-8
    RefSource g1(c, true);
    CHECK_NEAR(g1.next(), 8.0f, 1e-6f);
    c->set(-100.0f); c->next();
    CHECK_NEAR(g1.next(), -8.0f, 1e-6f);
    c->set(3.5f); c->next();
    CHECK_NEAR(g1.next(), 3.5f, 1e-6f);

    // Guarded: NaN/inf scrub to 0
    c->set(std::nanf("")); c->next();
    CHECK_NEAR(g1.next(), 0.0f, 1e-6f);
    c->set(INFINITY); c->next();
    CHECK_NEAR(g1.next(), 0.0f, 1e-6f);

    // Null source still reads 0 (pass-2 placeholder state)
    RefSource empty(nullptr, true);
    CHECK_NEAR(empty.next(), 0.0f, 1e-6f);
}

#include "mforce/source/combined_source.h"

static void run_tap_cycle_tests() {
    // The positional z-1 the whole feedback subsystem rests on: a tap
    // consumer evaluates before its source each tick, so reading current()
    // yields last tick's value. Self-referencing counter: out = 1 + tap(out)
    // ramps 1, 2, 3... and the guard ceiling (not inf) catches the runaway.
    auto counter = std::make_shared<CombinedSource>(
        std::make_shared<ConstantSource>(1.0f),
        std::make_shared<ConstantSource>(0.0f),   // replaced by the tap below
        CombineOp::Sum, 0.0f);                    // Sum: true addition (Add averages)
    auto tap = std::make_shared<RefSource>(counter, true);
    counter->set_param("source2", tap);

    CHECK_NEAR(counter->next(), 1.0f, 1e-6f);
    CHECK_NEAR(counter->next(), 2.0f, 1e-6f);
    CHECK_NEAR(counter->next(), 3.0f, 1e-6f);
    for (int i = 0; i < 20; ++i) counter->next();
    // Equilibrium, not inf: the guard caps the FEEDBACK at 8, so the
    // counter settles at 1 + 8 = 9.
    CHECK_NEAR(counter->current(), 9.0f, 1e-6f);
}

#include "mforce/source/delay_line_source.h"

static void run_delay_line_tests() {
    RenderContext ctx{48000};
    // sr 48000, freq 480 -> exactly 100 samples of delay
    auto dl = std::make_shared<DelayLineSource>(48000);
    dl->set_param("frequency", std::make_shared<ConstantSource>(480.0f));
    auto in = std::make_shared<ConstantSource>(0.0f);
    dl->set_param("source", in);
    dl->prepare(ctx, 48000);

    in->set(1.0f);
    float first = dl->next();          // impulse enters at tick 1
    in->set(0.0f);
    CHECK_NEAR(first, 0.0f, 1e-6f);    // empty line
    float out = 0.0f;
    for (int i = 0; i < 99; ++i) out = dl->next();
    CHECK_NEAR(out, 0.0f, 1e-6f);      // still inside the line (tick 100)
    out = dl->next();                  // tick 101: impulse emerges
    CHECK_NEAR(out, 1.0f, 1e-3f);
    out = dl->next();
    CHECK_NEAR(out, 0.0f, 1e-3f);      // and passes

    // ratio doubles the period: 200 samples
    auto dl2 = std::make_shared<DelayLineSource>(48000);
    dl2->set_param("frequency", std::make_shared<ConstantSource>(480.0f));
    dl2->set_param("ratio", std::make_shared<ConstantSource>(2.0f));
    auto in2 = std::make_shared<ConstantSource>(0.0f);
    dl2->set_param("source", in2);
    dl2->prepare(ctx, 48000);
    in2->set(1.0f); dl2->next(); in2->set(0.0f);
    for (int i = 0; i < 199; ++i) out = dl2->next();
    out = dl2->next();
    CHECK_NEAR(out, 1.0f, 1e-3f);

    // prepare() clears the line: no ring-over between notes
    dl2->prepare(ctx, 48000);
    for (int i = 0; i < 300; ++i) CHECK(dl2->next() == 0.0f);
}

#include "mforce/filter/svf_source.h"

// Measured phase delay: drive the filter with a unit sine at hz, project the
// steady-state output onto sin/cos at the same index (I/Q). y = A sin(wn+p)
// gives p = atan2(sum y*cos, sum y*sin); delay in samples = -p/w.
static float measure_phase_delay(SVFSource& f, ConstantSource& in,
                                 float hz, int sr) {
    const double w = 2.0 * 3.141592653589793 * hz / sr;
    const int settle = sr / 2, N = sr;
    double si = 0.0, co = 0.0;
    for (int n = 0; n < settle + N; ++n) {
        in.set(float(std::sin(w * n)));
        const float y = f.next();
        if (n >= settle) {
            si += y * std::sin(w * n);
            co += y * std::cos(w * n);
        }
    }
    return float(-std::atan2(co, si) / w);
}

static void run_phase_delay_tests() {
    RenderContext ctx{48000};
    // Closed-form phase_delay_at must match the filter actually measured,
    // across modes and both sides of cutoff.
    struct Case { int mode; float fc, res, hz; };
    const Case cases[] = {
        {SVFSource::kLowpass,   1000.0f, 1.0f, 200.0f},
        {SVFSource::kLowpass,   1000.0f, 1.0f, 1000.0f},  // at fc: (pi/2)/w
        {SVFSource::kLowpass,   4000.0f, 2.0f, 500.0f},
        {SVFSource::kBandpass,  1000.0f, 4.0f, 300.0f},   // below fc: lead
        {SVFSource::kLowpass1P, 2000.0f, 1.0f, 400.0f},
    };
    for (const auto& c : cases) {
        auto in = std::make_shared<ConstantSource>(0.0f);
        SVFSource f(48000);
        f.set_setting("mode", float(c.mode));
        f.set_param("source", in);
        f.set_param("cutoffFreq", std::make_shared<ConstantSource>(c.fc));
        f.set_param("resonance", std::make_shared<ConstantSource>(c.res));
        f.prepare(ctx, 48000);
        const float meas = measure_phase_delay(f, *in, c.hz, 48000);
        const float pred = f.phase_delay_at(c.hz);
        CHECK_NEAR(pred, meas, 0.15f);
    }
    // At fc the 2-pole LP delay is exactly (pi/2)/w regardless of res.
    {
        auto in = std::make_shared<ConstantSource>(0.0f);
        SVFSource f(48000);
        f.set_param("source", in);
        f.set_param("cutoffFreq", std::make_shared<ConstantSource>(1000.0f));
        f.prepare(ctx, 8);
        f.next();
        const float w = 2.0f * 3.14159265f * 1000.0f / 48000.0f;
        CHECK_NEAR(f.phase_delay_at(1000.0f), (0.5f * 3.14159265f) / w, 1e-2f);
    }
}

static void run_loop_compensation_tests() {
    RenderContext ctx{48000};
    // Bare tap-closed loop: delay <- sum(excite, tap(delay)). The cycle is
    // len + 1 ticks (the tap's positional z^-1); compensate shortens len so
    // the cycle lands exactly on sr/f.
    auto run_bare = [&](bool comp) {
        auto dl = std::make_shared<DelayLineSource>(48000);
        dl->set_param("frequency", std::make_shared<ConstantSource>(480.0f));
        auto ex = std::make_shared<ConstantSource>(0.0f);
        auto sum = std::make_shared<CombinedSource>(
            ex, std::make_shared<ConstantSource>(0.0f), CombineOp::Sum, 0.0f);
        sum->set_param("source2", std::make_shared<RefSource>(dl, true));
        dl->set_param("source", sum);
        if (comp) dl->set_setting("compensate", 1.0f);
        dl->prepare(ctx, 48000);
        ex->set(1.0f); dl->next(); ex->set(0.0f);
        int first = 0, second = 0;
        for (int t = 2; t <= 400 && !second; ++t) {
            if (std::fabs(dl->next()) > 0.5f) {
                if (!first) first = t; else if (t > first + 4) second = t;
            }
        }
        return std::pair<int, int>(first, second - first);
    };
    auto off = run_bare(false);
    CHECK(off.first == 101 && off.second == 101);   // sr/f + 1: rings flat
    auto on = run_bare(true);
    CHECK(on.first == 100 && on.second == 100);     // exactly sr/f

    // Loop with an SVF inside: sum -> svf -> delay. The walk must find the
    // filter and subtract its (current) phase delay; the echo period lands
    // on sr/f instead of sr/f + 1 + ~7.8 samples of filter lag.
    auto run_svf = [&](bool comp) {
        auto dl = std::make_shared<DelayLineSource>(48000);
        dl->set_param("frequency", std::make_shared<ConstantSource>(200.0f));
        auto ex = std::make_shared<ConstantSource>(0.0f);
        auto sum = std::make_shared<CombinedSource>(
            ex, std::make_shared<ConstantSource>(0.0f), CombineOp::Sum, 0.0f);
        sum->set_param("source2", std::make_shared<RefSource>(dl, true));
        // fc between mode 1 (200 Hz) and mode 2 (~400 Hz): only the
        // fundamental sees |H| > 1 and grows, so the settled tail is a
        // near-sine at the loop's mode-1 frequency. (With a higher cutoff
        // several modes grow at once and the STRETCHED upper modes — loop
        // lag varies with frequency, exactly like KS partials — dominate
        // the autocorrelation.) fc 300 also makes the lag big: ~33 samples,
        // 14% of the period, so uncompensated flatness is unmistakable.
        auto svf = std::make_shared<SVFSource>(48000);
        svf->set_param("source", sum);
        svf->set_param("cutoffFreq", std::make_shared<ConstantSource>(300.0f));
        svf->set_param("resonance", std::make_shared<ConstantSource>(1.0f));
        dl->set_param("source", svf);
        if (comp) dl->set_setting("compensate", 1.0f);
        dl->prepare(ctx, 48000);
        ex->set(1.0f); dl->next(); ex->set(0.0f);
        // The recirculating impulse smears into a multi-harmonic tone, so
        // measure the loop period by autocorrelation of the settled tail
        // (lag peak + parabolic refinement), not by threshold crossings.
        for (int t = 0; t < 3000; ++t) dl->next();
        std::vector<float> tail(4800);
        for (auto& v : tail) v = dl->next();
        // Normalized (correlation-coefficient) form: the tail grows a few
        // percent per cycle, and unnormalized autocorrelation tilts its peak
        // under an exponential envelope.
        auto ac = [&](int L) {
            double r = 0.0, e0 = 0.0, eL = 0.0;
            for (size_t i = 0; i + L < tail.size(); ++i) {
                r  += double(tail[i]) * double(tail[i + L]);
                e0 += double(tail[i]) * double(tail[i]);
                eL += double(tail[i + L]) * double(tail[i + L]);
            }
            return (e0 > 0.0 && eL > 0.0) ? r / std::sqrt(e0 * eL) : 0.0;
        };
        int bestL = 200;
        double bestR = ac(200);
        for (int L = 201; L <= 280; ++L) {
            const double r = ac(L);
            if (r > bestR) { bestR = r; bestL = L; }
        }
        const double r0 = ac(bestL - 1), r1 = ac(bestL), r2 = ac(bestL + 1);
        const double den = r0 - 2.0 * r1 + r2;
        const double frac = (den != 0.0) ? 0.5 * (r0 - r2) / den : 0.0;
        return float(bestL + frac);
    };
    const float periodOff = run_svf(false);
    const float periodOn = run_svf(true);
    CHECK(periodOff > 265.0f);                      // ~273: flat by a third of a semitone x4
    CHECK_NEAR(periodOn, 240.0f, 0.5f);             // on pitch
}

#include "mforce/source/shaper_source.h"

static void run_shaper_tests() {
    RenderContext ctx{48000};
    // Default identity curve passes input through
    auto sh = std::make_shared<ShaperSource>();
    auto in = std::make_shared<ConstantSource>(0.5f);
    sh->set_param("source", in);
    sh->prepare(ctx, 100);
    CHECK_NEAR(sh->next(), 0.5f, 1e-3f);

    // Clamp past the drawn ends
    in->set(3.0f); in->next();
    CHECK_NEAR(sh->next(), 1.0f, 1e-3f);
    in->set(-3.0f); in->next();
    CHECK_NEAR(sh->next(), -1.0f, 1e-3f);

    // drive scales input BEFORE the lookup: drive 2 pushes 0.4 to 0.8
    sh->set_param("drive", std::make_shared<ConstantSource>(2.0f));
    in->set(0.4f); in->next();
    CHECK_NEAR(sh->next(), 0.8f, 1e-3f);

    // A drawn dead-zone curve: flat 0 across [-0.5, 0.5], ramps outside.
    // smoothness 0.5 = linear segments (0 = hold-v1 step, 1 = sine).
    auto dz = std::make_shared<ShaperSource>();
    dz->set_array("values", {-1.0f, -1.0f, -0.5f, 0.0f, 0.5f, 0.0f, 1.0f, 1.0f});
    dz->set_param("smoothness", std::make_shared<ConstantSource>(0.5f));
    auto in2 = std::make_shared<ConstantSource>(0.2f);
    dz->set_param("source", in2);
    dz->prepare(ctx, 100);
    float mid = dz->next();
    CHECK(std::fabs(mid) < 0.05f);           // inside the dead zone
    in2->set(0.75f); in2->next();
    float hi = dz->next();
    CHECK_NEAR(hi, 0.5f, 1e-3f);             // linear outer ramp midpoint

    // smoothness 0 steps: same input holds the segment's START value (0)
    dz->set_param("smoothness", std::make_shared<ConstantSource>(0.0f));
    CHECK_NEAR(dz->next(), 0.0f, 1e-6f);

    // values round-trip via get_array (UI re-pull path)
    auto back = dz->get_array("values");
    CHECK(back.size() == 8 && back[2] == -0.5f);
}

#include "mforce/core/curve.h"

// Shaper per-segment overrides (curve-morph plan Task 5). "segs" array:
// flat [typeIdx, power] per segment; typeIdx 0 = default (smoothness),
// else 1 + int(RampType).
static void run_shaper_seg_tests() {
    RenderContext ctx{48000};
    ShaperSource sh;
    sh.set_array("values", {-1.0f,-1.0f, 0.0f,0.0f, 1.0f,1.0f});
    sh.set_param("smoothness", std::make_shared<ConstantSource>(0.5f));
    sh.set_param("drive", std::make_shared<ConstantSource>(1.0f));
    sh.set_param("source", std::make_shared<ConstantSource>(0.0f));
    sh.prepare(ctx, 8);
    sh.next();
    // Segment 1 (0->1) overridden to Hold (typeIdx 5): y stays 0 across it.
    sh.set_array("segs", {0.0f,0.0f, 5.0f,0.0f});
    CHECK_NEAR(sh.map(0.5f), 0.0f, 1e-9f);
    // Segment 0 default: smoothness 0.5 == lerp
    CHECK_NEAR(sh.map(-0.5f), -0.5f, 1e-6f);
    // Expo override with power 2 on segment 1 (typeIdx 2 = 1 + Expo)
    sh.set_array("segs", {0.0f,0.0f, 2.0f,2.0f});
    CHECK_NEAR(sh.map(0.5f), 0.25f, 1e-6f);   // t^2 at t=0.5
    // Round-trip: get_array returns what was set (Expo override live)
    auto back = sh.get_array("segs");
    CHECK(back.size() == 4);
    // A fresh node has no segs and an all-default set encodes to empty
    ShaperSource fresh;
    CHECK(fresh.get_array("segs").empty());
    sh.set_array("segs", {0.0f,0.0f, 0.0f,0.0f});
    CHECK(sh.get_array("segs").empty());
    // Smoothness must NOT bend an overridden Linear segment (typeIdx 1)
    sh.set_array("segs", {0.0f,0.0f, 1.0f,0.0f});
    sh.set_param("smoothness", std::make_shared<ConstantSource>(1.0f));
    sh.next();
    CHECK_NEAR(sh.map(0.5f), 0.5f, 1e-6f);
}

#include "mforce/source/wormhole_source.h"

// Wormhole: pure pass-through, including chained pairs (out-half refs
// in-half — the hidden-wire configuration the UI creates).
static void run_wormhole_tests() {
    RenderContext ctx{48000};
    auto in = std::make_shared<ConstantSource>(0.42f);
    WormholeSource wh;
    wh.set_param("source", in);
    wh.prepare(ctx, 8);
    CHECK_NEAR(wh.next(), 0.42f, 1e-9f);
    auto whIn = std::make_shared<WormholeSource>();
    whIn->set_param("source", in);
    WormholeSource whOut;
    whOut.set_param("source", whIn);
    whOut.prepare(ctx, 8);
    CHECK_NEAR(whOut.next(), 0.42f, 1e-9f);
    // Unwired = silence, not garbage
    WormholeSource bare;
    bare.prepare(ctx, 8);
    CHECK_NEAR(bare.next(), 0.0f, 1e-9f);
}

// Shaper morph pin (curve-morph plan Task 8): point-space A/B blend.
static void run_shaper_morph_tests() {
    RenderContext ctx{48000};
    ShaperSource sh;
    sh.set_array("values",  {-1.0f,-1.0f, 0.4f,0.2f, 1.0f,1.0f});
    sh.set_array("values2", {-1.0f,-1.0f, 0.1f,0.6f, 1.0f,1.0f});
    auto morph = std::make_shared<ConstantSource>(0.0f);
    sh.set_param("morph", morph);
    sh.set_param("source", std::make_shared<ConstantSource>(0.0f));
    sh.set_param("drive", std::make_shared<ConstantSource>(1.0f));
    sh.set_param("smoothness", std::make_shared<ConstantSource>(0.5f));
    sh.prepare(ctx, 8);
    sh.next();
    // m=0: exactly curve A (knee at 0.4)
    CHECK_NEAR(sh.map(0.4f), 0.2f, 1e-7f);
    // m=1: exactly curve B (knee at 0.1)
    morph->set(1.0f); morph->next(); sh.next();
    CHECK_NEAR(sh.map(0.1f), 0.6f, 1e-7f);
    // m=0.5: the knee SLIDES to (0.25, 0.4) — point-space, not output blend
    morph->set(0.5f); morph->next(); sh.next();
    CHECK_NEAR(sh.map(0.25f), 0.4f, 1e-6f);
    // Segment powers lerp when both sides override the same segment
    sh.set_array("segs",  {0.0f,0.0f, 2.0f,1.0f});   // seg1 Expo p1
    sh.set_array("segs2", {0.0f,0.0f, 2.0f,3.0f});   // seg1 Expo p3
    // blended seg1: Expo p2 between blended points (0.25,0.4)->(1,1):
    // at x=0.625 (t=0.5): 0.4 + 0.6*0.25 = 0.55
    CHECK_NEAR(sh.map(0.625f), 0.55f, 1e-5f);
    // Signed-curvature lerp across the Expo/InverseExpo axis: A Expo p2
    // (bulge down) vs B InverseExpo p2 (bulge up) at m=0.5 cancels to
    // linear — segment midpoint lands on the chord midpoint.
    sh.set_array("segs",  {0.0f,0.0f, 2.0f,2.0f});
    sh.set_array("segs2", {0.0f,0.0f, 3.0f,2.0f});
    CHECK_NEAR(sh.map(0.625f), 0.7f, 1e-5f);
    // Mismatched point count: morph ignored (B degenerates to A)
    sh.set_array("segs", {}); sh.set_array("segs2", {});
    sh.set_array("values2", {-1.0f,-1.0f, 1.0f,1.0f});
    CHECK_NEAR(sh.map(0.4f), 0.2f, 1e-6f);
    // No values2 at all: byte-identity path
    ShaperSource plain;
    plain.set_array("values", {-1.0f,-1.0f, 1.0f,1.0f});
    CHECK_NEAR(plain.map(0.3f), 0.3f, 1e-7f);
}

// RampType::Hold + holdPct removal (curve-morph plan Task 4).
static void run_ramp_hold_tests() {
    Ramp h{0.7f, 0.2f, RampType::Hold, 0.0f};
    CHECK_NEAR(h.value(0.0f), 0.7f, 1e-9f);
    CHECK_NEAR(h.value(0.5f), 0.7f, 1e-9f);
    CHECK_NEAR(h.value(1.0f), 0.7f, 1e-9f);
    // Byte-identity guard: every type returns startVal at exactly pos==0
    // (the old holdPct==0 branch did this; Expo with power 0 would
    // otherwise flip to endVal at pos 0).
    Ramp e{0.3f, 0.9f, RampType::Expo, 0.0f};
    CHECK_NEAR(e.value(0.0f), 0.3f, 1e-9f);
    Ramp s{0.3f, 0.9f, RampType::Sine, 0.0f};
    CHECK_NEAR(s.value(0.0f), 0.3f, 1e-6f);
}

// Shared Curve evaluator parity (2026-09-05 curve-morph plan Task 2):
// Curve::eval must reproduce CurveNode::map (all three domains, smoothness
// 0.5 == exact lerp) and ShaperSource::map (SmoothnessInterpolator path)
// before either host delegates to it.
static void run_curve_shared_tests() {
    RenderContext ctx{48000};

    // Parity vs CurveNode::map — Linear domain
    CurveNode lin;
    lin.knots = {{0.0f, 0.3f}, {0.2f, 0.3f}, {0.85f, 1.0f}, {1.0f, 1.6f}};
    for (float x : {-1.0f, 0.0f, 0.1f, 0.2f, 0.5f, 0.925f, 1.0f, 2.0f})
        CHECK_NEAR(Curve::eval(lin.knots, {}, Curve::Domain::Linear, 0.5f, x),
                   lin.map(x), 1e-7f);

    // Parity vs CurveNode::map — LogX and LogLog domains
    CurveNode logx; logx.interp = CurveNode::CurveInterp::LogX;
    logx.knots = {{100.0f, 0.0f}, {10000.0f, 2.0f}};
    CHECK_NEAR(Curve::eval(logx.knots, {}, Curve::Domain::LogX, 0.5f, 1000.0f),
               logx.map(1000.0f), 1e-6f);
    CurveNode ll; ll.interp = CurveNode::CurveInterp::LogLog;
    ll.knots = {{10.0f, 100.0f}, {100.0f, 10000.0f}};
    CHECK_NEAR(Curve::eval(ll.knots, {}, Curve::Domain::LogLog, 0.5f,
                           31.6227766f),
               ll.map(31.6227766f), 0.5f);

    // Parity vs ShaperSource::map — smoothness 0.6, asymmetric curve
    ShaperSource sh;
    sh.set_array("values", {-1.0f,-0.76f, -0.87f,-0.73f, -0.1f,-0.2f,
                             0.0f,0.0f, 0.11f,0.19f, 0.78f,0.57f,
                             0.93f,0.66f});
    sh.set_param("smoothness", std::make_shared<ConstantSource>(0.6f));
    sh.set_param("drive", std::make_shared<ConstantSource>(1.0f));
    sh.set_param("source", std::make_shared<ConstantSource>(0.0f));
    sh.prepare(ctx, 8);
    sh.next();  // latches smoothness into the evaluator
    auto vals = sh.get_array("values");
    for (float x : {-1.5f, -0.9f, -0.3f, 0.0f, 0.05f, 0.5f, 0.9f, 1.5f})
        CHECK_NEAR(Curve::eval_flat(vals, {}, Curve::Domain::Linear, 0.6f, x),
                   sh.map(x), 1e-6f);

    // Identity on empty
    CHECK_NEAR(Curve::eval({}, {}, Curve::Domain::Linear, 0.5f, 123.0f),
               123.0f, 1e-9f);
}

#include "mforce/source/pierce_filter.h"
#include <vector>
#include <random>

// ---------------------------------------------------------------------------
// HARD GATE for the Pierce/Van Duyne passive nonlinear filter (round 2): the
// whole point of this structure is that it cannot create energy, so nothing it
// terminates can run away and it can be driven at full scale.  Drives the
// SHIPPED PierceFilterSource (not a transcription) open loop and requires the
// cumulative output energy never to exceed the cumulative input energy.
//
// Probes: white-noise burst, continuous white noise, a 20 Hz - 20 kHz sweep, a
// full-scale square, an impulse, and DC — across eight coefficient pairs from
// near-linear to the most asymmetric the clamp allows.  The literal recurrence
// from the patent/Faust fails this at 2.43; see
// docs/research/stk_port/PIERCE_PASSIVE_NOTES.md.
// ---------------------------------------------------------------------------
namespace {

struct Playback final : mforce::ValueSource {
    const std::vector<float>* buf{nullptr};
    size_t i{0};
    float v{0.0f};
    const char* type_name() const override { return "Playback"; }
    mforce::SourceCategory category() const override {
        return mforce::SourceCategory::Modulator;
    }
    void prepare(const mforce::RenderContext&, int) override { i = 0; v = 0.0f; }
    float next() override {
        v = (buf && i < buf->size()) ? (*buf)[i++] : 0.0f;
        return v;
    }
    float current() const override { return v; }
};

double worst_energy_ratio(const std::vector<float>& x, float aNeg, float aPos) {
    RenderContext ctx{48000};
    auto src = std::make_shared<Playback>();
    src->buf = &x;
    PierceFilterSource f;
    f.set_param("source", src);
    f.set_param("coefNeg", std::make_shared<ConstantSource>(aNeg));
    f.set_param("coefPos", std::make_shared<ConstantSource>(aPos));
    f.prepare(ctx, int(x.size()));
    double cx = 0.0, cy = 0.0, worst = 0.0;
    for (float s : x) {
        const double y = f.next();
        cx += double(s) * double(s);
        cy += y * y;
        if (cx > 1e-12) worst = std::fmax(worst, cy / cx);
    }
    return worst;
}

} // namespace

static void run_pierce_passivity_tests() {
    const int N = 48000;
    const double sr = 48000.0;
    std::vector<std::pair<const char*, std::vector<float>>> probes;
    std::mt19937 rng(7);
    std::normal_distribution<float> gauss(0.0f, 1.0f);

    std::vector<float> burst(N, 0.0f);
    for (int n = 0; n < N / 5; ++n) burst[n] = gauss(rng);
    probes.emplace_back("noiseburst", burst);

    std::vector<float> noise(N);
    for (int n = 0; n < N; ++n) noise[n] = gauss(rng);
    probes.emplace_back("noise", noise);

    std::vector<float> sweep(N);
    for (int n = 0; n < N; ++n) {
        const double t = n / sr;
        sweep[n] = float(std::sin(2.0 * 3.14159265358979 *
                                  (20.0 * t + (19980.0 / 2.0) * t * t)));
    }
    probes.emplace_back("sweep", sweep);

    std::vector<float> square(N);
    for (int n = 0; n < N; ++n)
        square[n] = std::sin(2.0 * 3.14159265358979 * 220.0 * n / sr) >= 0.0
                        ? 1.0f : -1.0f;
    probes.emplace_back("square_fullscale", square);

    std::vector<float> imp(N, 0.0f);
    imp[0] = 1.0f;
    probes.emplace_back("impulse", imp);
    probes.emplace_back("dc", std::vector<float>(N, 1.0f));

    const float pairs[][2] = {{0.5f, -0.5f},  {0.9f, -0.9f}, {0.99f, -0.99f},
                              {0.9f, 0.1f},   {-0.9f, 0.3f}, {0.0f, 0.8f},
                              {0.75f, -0.25f}, {0.999f, -0.999f}};
    double worst = 0.0;
    const char* worstName = "";
    for (const auto& p : probes)
        for (const auto& c : pairs) {
            const double r = worst_energy_ratio(p.second, c[0], c[1]);
            CHECK(r <= 1.0 + 1e-6);
            if (r > worst) { worst = r; worstName = p.first; }
        }
    std::printf("pierce passivity: worst cumulative out/in energy = %.9f "
                "(%s) over %d probes\n", worst, worstName,
                int(probes.size() * 8));

    // coefNeg == coefPos must be an exactly linear first-order allpass:
    // unity magnitude, so total output energy equals total input energy to
    // within the stored energy left in the state.
    const double lin = worst_energy_ratio(noise, 0.7f, 0.7f);
    CHECK(lin <= 1.0 + 1e-6);
    CHECK(lin > 0.999);
    std::printf("pierce linear check (coefNeg == coefPos == 0.7): "
                "energy ratio %.9f\n", lin);
}

#include "mforce/music/parse_util.h"

static void run_passage_parse_tests() {
    // No bars: every note starts its own phrase (compat: one-note phrases).
    auto plain = parse_passage("Cq Dq Eq", 4, 60.0f);
    CHECK(plain.size() == 3);
    for (auto& n : plain) CHECK(n.phraseStart);

    // Bars group; leading/doubled/trailing bars are no-ops.
    auto p = parse_passage("| Cq Dq | | Eq Fq Gq |", 4, 60.0f);
    CHECK(p.size() == 5);
    CHECK(p[0].phraseStart);  CHECK(!p[1].phraseStart);
    CHECK(p[2].phraseStart);  CHECK(!p[3].phraseStart);  CHECK(!p[4].phraseStart);

    // A rest ends the phrase: the note after it starts fresh (spec §2 v1
    // rule — silence breaks the breath; `|` is the explicit marker).
    // Grouping is active because the string contains a bar.
    auto r = parse_passage("| Cq Dq Rq Eq", 4, 60.0f);
    CHECK(r.size() == 4);
    CHECK(r[0].phraseStart);  CHECK(!r[1].phraseStart);
    CHECK(r[2].noteNumber == kRestNote);
    CHECK(r[3].phraseStart);
}

#include "mforce/render/perform_source.h"

static void run_transition_field_tests() {
    auto ps = std::make_shared<PerformSource>();
    PerformOut out(ps, PerformOut::Field::Transition);
    CHECK(out.current() == 0.0f);                          // no note yet
    ps->set_note(440.0f, 0.8f, 48000, 1.0f, nullptr, 2.0f);
    CHECK(out.current() == 2.0f);
    CHECK(out.next() == 2.0f);
    ps->set_note(440.0f, 0.8f, 48000, 1.0f, nullptr);      // default arg = none
    CHECK(out.current() == 0.0f);
}

int main() {
    run_passage_parse_tests();
    run_transition_field_tests();
    run_curve_node_tests();
    run_curve_expr_tests();
    run_envelope_range_tests();
    run_stage_nominal_tests();
    run_perform_source_tests();
    run_tap_guard_tests();
    run_tap_cycle_tests();
    run_delay_line_tests();
    run_phase_delay_tests();
    run_loop_compensation_tests();
    run_shaper_tests();
    run_curve_shared_tests();
    run_ramp_hold_tests();
    run_shaper_seg_tests();
    run_shaper_morph_tests();
    run_wormhole_tests();
    run_pierce_passivity_tests();
    if (g_fails) { std::printf("%d/%d FAILED\n", g_fails, g_checks); return 1; }
    std::printf("ALL PASS (%d checks)\n", g_checks);
    return 0;
}
