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

static void run_onset_field_tests() {
    auto ps = std::make_shared<PerformSource>();
    PerformOut out(ps, PerformOut::Field::Onset);
    CHECK(out.current() == 0.0f);                          // no note yet
    ps->set_note(440.0f, 0.8f, 48000, 1.0f, nullptr, 2.0f);
    CHECK(out.current() == 2.0f);
    CHECK(out.next() == 2.0f);
    ps->set_note(440.0f, 0.8f, 48000, 1.0f, nullptr);      // default arg = none
    CHECK(out.current() == 0.0f);
}

#include "mforce/render/patch_loader.h"
#include "mforce/render/instrument.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <sstream>
#include <cstring>

// Load a baseline patch with its score block stripped (the tests schedule
// their own notes). Run engine_tests from the repo root.
static InstrumentPatch load_scoreless(const char* path) {
    std::ifstream f(path);
    std::stringstream ss; ss << f.rdbuf();
    std::string txt = ss.str();
    auto j = nlohmann::json::parse(txt);
    j.erase("score");
    return load_instrument_patch_json(j.dump());
}

// A minimal sustaining patch: percent-mode adsr (attack 5%, decay 10%,
// expand, release 25%) into a sine whose frequency is WIRED to the Note
// node. The wire matters: a legacy paramMap delivers frequency by a PUSH
// evaluated once at Setup, which freezes a glide at its first value —
// only pulled frequency chains (what every taught wind patch uses) see
// the ramp. %s = the instrument block's glideMs.
static std::string hold_patch_json(float glideMs) {
    char buf[1400];
    std::snprintf(buf, sizeof(buf), R"({
      "sampleRate": 48000,
      "instrument": { "polyphony": 2, "glideMs": %.1f },
      "graph": {
        "output": "sine1",
        "nodes": [
          { "id": "pf", "type": "PerformNode",
            "params": { "field": "frequency" } },
          { "id": "env1", "type": "Envelope",
            "params": { "stages": [
              { "startVal": 0.0, "endVal": 1.0, "type": "Linear", "percent": 0.05 },
              { "startVal": 1.0, "endVal": 0.7, "type": "Linear", "percent": 0.10 },
              { "startVal": 0.7, "endVal": 0.7, "type": "Linear", "percent": 0.0 },
              { "startVal": 0.7, "endVal": 0.0, "type": "Linear", "percent": 0.25 }
            ] } },
          { "id": "sine1", "type": "SineSource",
            "params": { "frequency": { "ref": "pf" },
                        "amplitude": { "ref": "env1" }, "phase": 0.0 } }
        ]
      }
    })", glideMs);
    return std::string(buf);
}

// Per-note delivery with hold (spec 2026-09-20-note-onsets-v2 §4/§5/§7).
static void run_hold_delivery_tests() {
    const int sr = 48000;
    auto rms_of = [](const std::vector<float>& b, float fromSec, float toSec) {
        int a = int(fromSec * 48000), z = int(toSec * 48000);
        a = std::max(0, a); z = std::min(z, int(b.size()));
        double s = 0.0;
        for (int i = a; i < z; ++i) s += double(b[size_t(i)]) * b[size_t(i)];
        return float(std::sqrt(s / double(std::max(1, z - a))));
    };
    auto render_all = [](InstrumentPatch& ip, int n) {
        std::vector<float> b(size_t(n), 0.0f);
        RenderContext ctx{ip.sampleRate};
        ip.instrument->render(ctx, b.data(), n);
        return b;
    };

    // (a) The classic path is deterministic and unchanged in shape: the
    //     same hold:false PerformedNote renders the same bytes twice.
    //     (The null gate over 79 patches is the real referee for "equals
    //     yesterday".)
    {
        const int N = int(1.6f * sr);
        auto a = load_scoreless("patches/baselines/BaselineSIN.json");
        auto* pa = dynamic_cast<PitchedInstrument*>(a.instrument.get());
        CHECK(pa != nullptr);
        pa->play_note({60.0f, 0.8f, 1.0f}, 0.0f);
        auto bufA = render_all(a, N);

        auto b = load_scoreless("patches/baselines/BaselineSIN.json");
        auto* pb = dynamic_cast<PitchedInstrument*>(b.instrument.get());
        pb->play_note({60.0f, 0.8f, 1.0f, 0.0f, false}, 0.0f);
        auto bufB = render_all(b, N);
        CHECK(std::memcmp(bufA.data(), bufB.data(),
                          size_t(N) * sizeof(float)) == 0);
    }

    // (b) Hold mechanics: quarter {hold:true} + quarter {hold:false},
    //     same pitch. The gate stays open across 0.4 s — no release dip —
    //     and the voice releases after 0.8 s.
    {
        const int N = int(2.0f * sr);
        auto ip = load_instrument_patch_json(hold_patch_json(15.0f));
        auto* pi = dynamic_cast<PitchedInstrument*>(ip.instrument.get());
        CHECK(pi != nullptr);
        pi->play_note({60.0f, 0.8f, 0.4f, 0.0f, true},  0.0f);
        pi->play_note({60.0f, 0.8f, 0.4f, 0.0f, false}, 0.4f);
        auto b = render_all(ip, N);
        float sustain  = rms_of(b, 0.30f, 0.34f);
        float boundary = rms_of(b, 0.38f, 0.42f);
        CHECK(sustain > 0.1f);
        CHECK(std::fabs(boundary - sustain) < 0.10f * sustain);
        CHECK(rms_of(b, 0.70f, 0.78f) > 0.1f);        // still sounding
        CHECK(rms_of(b, 1.10f, 1.40f) < 0.01f * sustain);  // and released
    }

    // (c) Release re-layout end to end (§5): quarter {hold:true} + half
    //     {hold:false}. The release is 25% of the HALF note (0.2 s), not
    //     of the quarter that prepared the voice (which would be 0.1 s).
    {
        const int N = int(2.4f * sr);
        auto ip = load_instrument_patch_json(hold_patch_json(15.0f));
        auto* pi = dynamic_cast<PitchedInstrument*>(ip.instrument.get());
        pi->play_note({60.0f, 0.8f, 0.4f, 0.0f, true},  0.0f);
        pi->play_note({60.0f, 0.8f, 0.8f, 0.0f, false}, 0.4f);
        auto b = render_all(ip, N);
        float peak = 0.0f;
        for (float v : b) peak = std::max(peak, std::fabs(v));
        const float thresh = peak * 0.01f;            // -40 dB below peak
        int last = 0;
        for (int i = 0; i < N; ++i)
            if (std::fabs(b[size_t(i)]) > thresh) last = i;
        float lastSec = float(last) / float(sr);
        CHECK(lastSec > 1.34f && lastSec < 1.46f);    // 1.2 s + ~0.2 s
    }

    // (d) Glide (§4.1): C4 {hold:true} then G4 {hold:false} with
    //     glideMs 15. The pitch ARRIVES at G4 within the glide, and the
    //     boundary's per-sample step is smaller than an instant retarget's.
    {
        const int N = int(1.6f * sr);
        auto glided = load_instrument_patch_json(hold_patch_json(15.0f));
        auto* pg = dynamic_cast<PitchedInstrument*>(glided.instrument.get());
        pg->play_note({60.0f, 0.8f, 0.4f, 0.0f, true},  0.0f);
        pg->play_note({67.0f, 0.8f, 0.4f, 0.0f, false}, 0.4f);
        auto bg = render_all(glided, N);

        auto instant = load_instrument_patch_json(hold_patch_json(0.0f));
        auto* pn = dynamic_cast<PitchedInstrument*>(instant.instrument.get());
        pn->play_note({60.0f, 0.8f, 0.4f, 0.0f, true},  0.0f);
        pn->play_note({67.0f, 0.8f, 0.4f, 0.0f, false}, 0.4f);
        auto bn = render_all(instant, N);

        // f0 from INTERPOLATED rising zero crossings (a raw crossing count
        // over 30 ms quantizes to ~70 cents — far too coarse for a
        // 30-cent gate).
        auto f0 = [&](const std::vector<float>& b, float fromSec, float toSec) {
            int a = int(fromSec * sr), z = int(toSec * sr);
            double first = -1.0, last = -1.0;
            int cycles = -1;
            for (int i = a + 1; i < z; ++i) {
                float p = b[size_t(i-1)], c = b[size_t(i)];
                if (p < 0.0f && c >= 0.0f) {
                    double t = double(i - 1) + double(-p) / double(c - p);
                    if (first < 0.0) { first = t; cycles = 0; }
                    else { last = t; ++cycles; }
                }
            }
            if (cycles <= 0) return 0.0f;
            return float(double(cycles) * double(sr) / (last - first));
        };
        const float g4 = 391.995f;
        float cents = 1200.0f * std::log2(f0(bg, 0.415f, 0.445f) / g4);
        CHECK(std::fabs(cents) < 30.0f);
        // Boundary smoothness over the first 5 ms of the glide.
        auto maxStep = [&](const std::vector<float>& b) {
            float m = 0.0f;
            for (int i = int(0.400f * sr) + 1; i < int(0.405f * sr); ++i)
                m = std::max(m, std::fabs(b[size_t(i)] - b[size_t(i-1)]));
            return m;
        };
        CHECK(maxStep(bg) < maxStep(bn));
    }

    // (e) finish_open_lines (§4.3): a line left open at score end is
    //     released, not leaked — the output decays away.
    {
        const int N = int(2.0f * sr);
        auto ip = load_instrument_patch_json(hold_patch_json(15.0f));
        auto* pi = dynamic_cast<PitchedInstrument*>(ip.instrument.get());
        pi->play_note({60.0f, 0.8f, 0.5f, 0.0f, true}, 0.0f);
        pi->finish_open_lines();
        auto b = render_all(ip, N);
        float peak = 0.0f;
        for (float v : b) peak = std::max(peak, std::fabs(v));
        CHECK(peak > 0.1f);
        CHECK(rms_of(b, 1.20f, 1.60f) < 0.001f * peak);   // < -60 dBFS
    }

    // (f) Mid-line retune still re-drives the living voice: the second
    //     half of a C4 -> G4 line oscillates ~1.5x faster (the v1
    //     assertion, re-expressed per note).
    {
        const int N = int(1.6f * sr);
        auto c = load_scoreless("patches/baselines/BaselineSIN.json");
        auto* pc = dynamic_cast<PitchedInstrument*>(c.instrument.get());
        pc->play_note({60.0f, 0.8f, 0.5f, 0.0f, true},  0.0f);
        pc->play_note({67.0f, 0.8f, 0.5f, 0.0f, false}, 0.5f);
        auto bufC = render_all(c, N);
        auto zc = [&](int from, int to) {
            int n = 0;
            for (int i = from + 1; i < to; ++i)
                if ((bufC[size_t(i)] >= 0) != (bufC[size_t(i-1)] >= 0)) ++n;
            return n;
        };
        int half = 24000;
        float ratio = float(zc(half, 2 * half)) / float(std::max(1, zc(0, half)));
        CHECK(ratio > 1.35f && ratio < 1.65f);   // 392/261.6 = 1.498
    }
}

static void run_envelope_retrigger_tests() {
    // Seconds-mode gesture: 10ms 1.0->0.2, 20ms 0.2->1.0, expand@1.0.
    const int sr = 48000;
    Envelope env(sr);
    env.absolute_time = true;
    env.add_stage({{1.0f, 0.2f, RampType::Linear, 0.0f}, 0.010f, 0.0f, 0.0f});
    env.add_stage({{0.2f, 1.0f, RampType::Linear, 0.0f}, 0.020f, 0.0f, 0.0f});
    env.add_stage({{1.0f, 1.0f, RampType::Linear, 0.0f}, 0.0f,   0.0f, 0.0f});
    env.prepare(RenderContext{sr}, sr);   // 1.0 s

    float last = 0.0f;
    for (int i = 0; i < sr / 2; ++i) last = env.next();
    CHECK_NEAR(last, 1.0f, 1e-4f);        // settled on expand

    env.retrigger();
    float first = env.next();
    CHECK(std::fabs(first - last) < 0.02f);   // no step at the restart
    float minSeen = first;
    for (int i = 1; i < int(0.010f * sr) + 4; ++i)
        minSeen = std::min(minSeen, env.next());
    CHECK_NEAR(minSeen, 0.2f, 0.02f);     // the dip fired
    float v = 0.0f;
    for (int i = 0; i < int(0.030f * sr); ++i) v = env.next();
    CHECK_NEAR(v, 1.0f, 1e-3f);           // recovered to neutral
    // Holds neutral through what remains of the prepared second.
    for (int i = 0; i < sr / 4; ++i) v = env.next();
    CHECK_NEAR(v, 1.0f, 1e-3f);
}

// Release re-layout (spec 2026-09-20-note-onsets-v2 §5): the release
// stage's sample count was computed at voice birth against note 1's
// duration; at gate_release the caller may re-reference it to the
// RELEASING note's duration.
static void run_release_relayout_tests() {
    const int sr = 48000;
    // adsr-ish: attack 10%, decay 10%, expand, release 25% (percent mode)
    Envelope env(sr);
    env.add_stage({{0.0f, 1.0f, RampType::Linear, 0.0f}, 0.10f, 0.0f, 0.0f});
    env.add_stage({{1.0f, 0.7f, RampType::Linear, 0.0f}, 0.10f, 0.0f, 0.0f});
    env.add_stage({{0.7f, 0.7f, RampType::Linear, 0.0f}, 0.0f,  0.0f, 0.0f});
    env.add_stage({{0.7f, 0.0f, RampType::Linear, 0.0f}, 0.25f, 0.0f, 0.0f});
    env.set_gated(true);
    env.prepare(RenderContext{sr}, int(0.4f * sr));      // quarter note
    for (int i = 0; i < int(0.35f * sr); ++i) env.next(); // into the hold
    // Release re-referenced to a HALF note: 25% of 0.8 s = 0.2 s.
    int rem = env.gate_release(int(0.8f * sr));
    CHECK(std::abs(rem - int(0.25f * 0.8f * sr)) <= 2);
    // Default arg keeps old behavior: fresh envelope, no ref. The
    // prepare-time layout resolves percent against the LAYOUT window,
    // i.e. the note minus the engine-wide reflection allowance — that
    // 10 ms is why the expectation is not a flat 25% of 0.4 s.
    Envelope e2(sr);
    e2.add_stage({{0.0f, 1.0f, RampType::Linear, 0.0f}, 0.10f, 0.0f, 0.0f});
    e2.add_stage({{1.0f, 1.0f, RampType::Linear, 0.0f}, 0.0f,  0.0f, 0.0f});
    e2.add_stage({{1.0f, 0.0f, RampType::Linear, 0.0f}, 0.25f, 0.0f, 0.0f});
    e2.set_gated(true);
    e2.prepare(RenderContext{sr}, int(0.4f * sr));
    for (int i = 0; i < int(0.2f * sr); ++i) e2.next();
    int rem2 = e2.gate_release();
    CHECK(std::abs(rem2 - int(0.25f * (0.4f - Envelope::kReflectionAllowanceSec)
                              * sr)) <= 2);
}

#include "mforce/core/name_gate.h"

static void run_name_gate_tests() {
    auto ps = std::make_shared<PerformSource>();
    auto in = std::make_shared<PerformOut>(ps, PerformOut::Field::Onset);
    auto ng = std::make_shared<NameGate>(48000);
    ng->set_param("in", in);
    ng->targetId = 2.0f;
    ps->set_note(440.0f, 0.8f, 100, 0.0f, nullptr, 2.0f);
    CHECK(ng->current() == 1.0f);
    CHECK(ng->next() == 1.0f);
    ps->set_note(440.0f, 0.8f, 100, 0.0f, nullptr, 1.0f);
    CHECK(ng->current() == 0.0f);
    ng->targetId = -1.0f;                    // unresolved never matches
    ps->set_note(440.0f, 0.8f, 100, 0.0f, nullptr, 0.0f);
    CHECK(ng->current() == 0.0f);
}

static void run_onset_trigger_tests() {
    // Minimal taught patch: sine * gesture envelope; gesture's trigger
    // wired Note.onset -> NameGate("tongue") -> Envelope.trigger.
    // Spec §6 end to end: the dip fires on the tongued note only.
    const char* kJson = R"({
      "sampleRate": 48000,
      "instrument": { "polyphony": 1, "onsets": ["tongue"] },
      "graph": {
        "output": "sine1",
        "nodes": [
          { "id": "perf1", "type": "PerformNode",
            "params": { "field": "onset" } },
          { "id": "ng1", "type": "NameGate",
            "params": { "name": "tongue", "in": { "ref": "perf1" } } },
          { "id": "gest1", "type": "Envelope",
            "params": { "timeMode": "seconds",
                        "trigger": { "ref": "ng1" },
                        "stages": [
              { "startVal": 1.0, "endVal": 0.1, "type": "Linear", "percent": 0.010 },
              { "startVal": 0.1, "endVal": 1.0, "type": "Linear", "percent": 0.020 },
              { "startVal": 1.0, "endVal": 1.0, "type": "Linear", "percent": 0.0 }
            ] } },
          { "id": "sine1", "type": "SineSource",
            "params": { "frequency": 220.0, "amplitude": { "ref": "gest1" },
                        "phase": 0.0 } }
        ]
      }
    })";
    auto ip = load_instrument_patch_json(kJson);
    auto* pi = dynamic_cast<PitchedInstrument*>(ip.instrument.get());
    CHECK(pi != nullptr);
    pi->play_note({57.0f, 0.8f, 0.5f, 0.0f, true}, 0.0f);
    pi->play_note({57.0f, 0.8f, 0.5f, pi->onset_id("tongue"), false}, 0.5f);
    const int N = int(1.2f * 48000);
    std::vector<float> buf(size_t(N), 0.0f);
    RenderContext ctx{48000};
    ip.instrument->render(ctx, buf.data(), N);

    auto rms = [&](float fromSec, float toSec) {
        int a = int(fromSec * 48000), b = int(toSec * 48000);
        double s = 0.0;
        for (int i = a; i < b; ++i) s += double(buf[size_t(i)]) * buf[size_t(i)];
        return float(std::sqrt(s / double(b - a)));
    };
    float before = rms(0.45f, 0.49f);          // settled, pre-boundary
    float dip    = rms(0.507f, 0.513f);        // around the dip minimum
                                               // (10ms fall + start of rise)
    float after  = rms(0.60f, 0.90f);          // recovered sustain
    CHECK(dip < 0.5f * before);                // the consonant fired
    CHECK(after > 0.9f * before);              // and got out of the way
    // Control: same line, no onset name — no dip.
    auto ip2 = load_instrument_patch_json(kJson);
    auto* pi2 = dynamic_cast<PitchedInstrument*>(ip2.instrument.get());
    pi2->play_note({57.0f, 0.8f, 0.5f, 0.0f, true}, 0.0f);
    pi2->play_note({57.0f, 0.8f, 0.5f, 0.0f, false}, 0.5f);
    std::vector<float> buf2(size_t(N), 0.0f);
    RenderContext ctx2{48000};
    ip2.instrument->render(ctx2, buf2.data(), N);
    double s = 0.0;
    int a = int(0.500f * 48000), b = int(0.508f * 48000);
    for (int i = a; i < b; ++i) s += double(buf2[size_t(i)]) * buf2[size_t(i)];
    float dip2 = float(std::sqrt(s / double(b - a)));
    CHECK(dip2 > 0.9f * before);
}

static void run_note_reseed_tests() {
    // Per-note determinism (onsets-v2 addendum): an in-line note's Setup
    // re-anchors stochastic draws, so a held-line note's noise realization
    // equals a fresh note's. Patch: white noise * gated sustain envelope.
    // Property: with reseed, note 2's samples in the flat-sustain window
    // REPLAY note 1's (same draw index from anchor, same sustain level);
    // free-running draws would diverge at every sample.
    const char* kJson = R"({
      "sampleRate": 48000,
      "instrument": { "polyphony": 1, "sustaining": true },
      "graph": {
        "output": "wn1",
        "nodes": [
          { "id": "env1", "type": "Envelope",
            "params": { "stages": [
              { "startVal": 0.0, "endVal": 1.0, "type": "Linear", "percent": 0.10 },
              { "startVal": 1.0, "endVal": 0.7, "type": "Linear", "percent": 0.10 },
              { "startVal": 0.7, "endVal": 0.7, "type": "Linear", "percent": 0.0 },
              { "startVal": 0.7, "endVal": 0.0, "type": "Linear", "percent": 0.15 }
            ] } },
          { "id": "wn1", "type": "WhiteNoiseSource",
            "params": { "amplitude": { "ref": "env1" } } }
        ]
      }
    })";
    auto ip = load_instrument_patch_json(kJson);
    auto* pi = dynamic_cast<PitchedInstrument*>(ip.instrument.get());
    CHECK(pi != nullptr);
    pi->play_note({57.0f, 0.8f, 0.5f, 0.0f, true,  nullptr}, 0.0f);
    pi->play_note({57.0f, 0.8f, 0.5f, 0.0f, false, nullptr}, 0.5f);
    const int N = int(0.5f * 48000);
    std::vector<float> buf(size_t(int(1.6f * 48000)), 0.0f);
    RenderContext ctx{48000};
    ip.instrument->render(ctx, buf.data(), int(buf.size()));
    // Flat-sustain overlap: note 1 is at sustain from 25% on; note 2 is
    // HELD at sustain throughout (no re-attack). Skip the last 10% for
    // note 2's release re-layout territory.
    int from = int(0.25f * N), to = int(0.85f * N);
    int same = 0, total = 0;
    for (int k = from; k < to; ++k) {
        ++total;
        if (buf[size_t(k)] == buf[size_t(N + k)]) ++same;
    }
    CHECK(same == total);   // exact replay — draws re-anchored
}

#include "mforce/music/passage_melody.h"
#include "mforce/music/templates_json.h"
#include <filesystem>

static void run_passage_melody_tests() {
    using namespace mforce;
    Scale c = Scale::get("C", "Major");
    // House convention: E5 = 64, D5 = 62, C5 = 60, G5 = 67.
    CHECK(scale_steps_between(64.0f, 62.0f, c) == -1);   // E -> D
    CHECK(scale_steps_between(62.0f, 60.0f, c) == -1);   // D -> C
    CHECK(scale_steps_between(64.0f, 67.0f, c) ==  2);   // E -> G (E F G)
    CHECK(scale_steps_between(64.0f, 64.0f, c) ==  0);
    CHECK(scale_steps_between(60.0f, 72.0f, c) ==  7);   // C5 -> C6, full octave
    CHECK(scale_steps_between(72.0f, 59.0f, c) == -8);   // C6 -> B4, crosses octave
    // Non-scale tone throws (crawl material must be diatonic).
    bool threw = false;
    try { scale_steps_between(60.0f, 61.0f, c); } catch (const std::exception&) { threw = true; }
    CHECK(threw);
    // Non-C tonic: G Major, F#5 = 66 is a scale tone, F5 = 65 is not.
    Scale g = Scale::get("G", "Major");
    CHECK(scale_steps_between(67.0f, 66.0f, g) == -1);   // G -> F#
    threw = false;
    try { scale_grid_index(65.0f, g); } catch (const std::exception&) { threw = true; }
    CHECK(threw);

    const std::string mary =
        "Eq Dq Cq Dq Eq Eq Eh Dq Dq Dh Eq Gq Gh | "
        "Eq Dq Cq Dq Eq Eq Eq Eq Dq Dq Eq Dq Cw";
    auto phrases = phrases_from_passage(mary, 5, c, 4.0f);
    CHECK(phrases.size() == 2);

    // Phrase 1: 4 figures, DENSE connectors (one per figure, [0] a dummy —
    // composer.h:1471 reads connectors[i] as the lead INTO figure i),
    // starts E5.
    const auto& p1 = phrases[0];
    CHECK(p1.figures.size() == 4);
    CHECK(p1.connectors.size() == 4);
    CHECK(p1.startingPitch && int(p1.startingPitch->note_number()) == 64);
    CHECK(p1.cadenceType == 0);
    // Every figure Locked with a lockedFigure present.
    for (const auto& ft : p1.figures) {
        CHECK(ft.source == FigureSource::Locked);
        CHECK(ft.lockedFigure.has_value());
    }
    // fig1 = E D C D: durations 1,1,1,1; steps 0,-1,-1,+1.
    {
        const auto& u = p1.figures[0].lockedFigure->units;
        CHECK(u.size() == 4);
        CHECK(u[0].duration == 1.0f && u[0].step == 0);
        CHECK(u[1].step == -1 && u[2].step == -1 && u[3].step == 1);
    }
    // fig4 = E G Gh: durations 1,1,2; steps 0,+2,0.
    {
        const auto& u = p1.figures[3].lockedFigure->units;
        CHECK(u.size() == 3);
        CHECK(u[2].duration == 2.0f);
        CHECK(u[0].step == 0 && u[1].step == 2 && u[2].step == 0);
    }
    // Connectors: [0] dummy 0, then +1 (D->E), -1 (E->D), +1 (D->E).
    CHECK(p1.connectors[0] && p1.connectors[0]->leadStep == 0);
    CHECK(p1.connectors[1] && p1.connectors[1]->leadStep == 1);
    CHECK(p1.connectors[2] && p1.connectors[2]->leadStep == -1);
    CHECK(p1.connectors[3] && p1.connectors[3]->leadStep == 1);

    // Phrase 2: last figure is the whole-note C alone in its bar.
    const auto& p2 = phrases[1];
    CHECK(p2.figures.size() == 4);
    CHECK(p2.connectors.size() == 4);
    CHECK(p2.startingPitch && int(p2.startingPitch->note_number()) == 64);
    {
        const auto& u = p2.figures[3].lockedFigure->units;
        CHECK(u.size() == 1 && u[0].duration == 4.0f && u[0].step == 0);
    }
    // fig3 = D D E D: steps 0,0,+1,-1; its lead connector is -1 (E->D).
    CHECK(p2.figures[2].lockedFigure->units[2].step == 1);
    CHECK(p2.connectors[2] && p2.connectors[2]->leadStep == -1);
    // fig4's lead is -1 (D->C).
    CHECK(p2.connectors[3] && p2.connectors[3]->leadStep == -1);

    // A '|'-free string is one phrase (parse marks every note phraseStart
    // only when grouping is active — verify we don't fragment).
    auto one = phrases_from_passage("Cq Dq Eq Fq", 5, c, 4.0f);
    CHECK(one.size() == 1 && one[0].figures.size() == 1);

    // Rests refuse loudly (parse's rest-ends-phrase rule would corrupt
    // structural grouping; revisit when a crawl tune needs rests).
    threw = false;
    try { phrases_from_passage("Cq Rq Eq", 5, c, 4.0f); }
    catch (const std::exception&) { threw = true; }
    CHECK(threw);

    // Schema + apply: template with a melodyPassageFile gets its phrases
    // derived; the two new fields round-trip; a missing file throws.
    {
        // Write a scratch .psg next to the test's CWD-independent temp dir.
        const std::string psgPath = "renders/scratch/_pm_test.psg";
        std::filesystem::create_directories("renders/scratch");
        { std::ofstream f(psgPath); f << "Eq Dq Cq Dq | Cq Cq Ch"; }

        json tj = json::parse(R"({
          "keyName": "C", "scaleName": "Major", "bpm": 80,
          "sections": [{"name": "Main", "beats": 8}],
          "parts": [{
            "name": "melody", "role": "melody",
            "passages": { "Main": {
              "melodyPassageFile": "renders/scratch/_pm_test.psg",
              "melodyOctave": 5, "phrases": [] } }
          }]
        })");
        PieceTemplate tmpl;
        from_json(tj, tmpl);
        CHECK(tmpl.parts[0].passages.at("Main").melodyPassageFile
              == "renders/scratch/_pm_test.psg");
        CHECK(tmpl.parts[0].passages.at("Main").melodyOctave == 5);

        apply_passage_melodies(tmpl);
        const auto& pass = tmpl.parts[0].passages.at("Main");
        CHECK(pass.phrases.size() == 2);
        CHECK(pass.phrases[0].figures.size() == 1);   // one bar
        CHECK(pass.phrases[1].figures.size() == 1);

        // Round-trip keeps the fields.
        json out; to_json(out, tmpl.parts[0].passages.at("Main"));
        CHECK(out.value("melodyPassageFile", std::string())
              == "renders/scratch/_pm_test.psg");
        CHECK(out.value("melodyOctave", 0) == 5);

        // Missing file names the path.
        tmpl.parts[0].passages.at("Main").melodyPassageFile = "no/such.psg";
        threw = false;
        try { apply_passage_melodies(tmpl); }
        catch (const std::exception& e) {
            threw = std::string(e.what()).find("no/such.psg") != std::string::npos;
        }
        CHECK(threw);
        std::filesystem::remove(psgPath);
    }
}

#include "mforce/music/anchor_selector.h"
#include "mforce/music/classical_composer.h"

static void run_walk1_tests() {
    using namespace mforce;
    Scale c = Scale::get("C", "Major");
    MelodyProfile prof = MelodyProfile::load_by_name("nursery_v1");

    // --- Complexify: target note count, duration preserved, deterministic,
    //     JSON name round-trips.
    {
        MelodicFigure rep3;
        rep3.units.push_back({1.0f, 0});
        rep3.units.push_back({1.0f, 0});
        rep3.units.push_back({2.0f, 0});
        auto a1 = figure_transforms::apply(rep3, TransformOp::Complexify, 4, 77u);
        CHECK(a1.note_count() == 4);
        CHECK(std::fabs(a1.total_duration() - 4.0f) < 1e-5f);
        auto a2 = figure_transforms::apply(rep3, TransformOp::Complexify, 4, 77u);
        bool same = a1.note_count() == a2.note_count();
        for (int k = 0; same && k < a1.note_count(); ++k)
            same = a1.units[k].duration == a2.units[k].duration
                && a1.units[k].step == a2.units[k].step;
        CHECK(same);
        json jt; to_json(jt, TransformOp::Complexify);
        CHECK(jt.get<std::string>() == std::string("complexify"));
        TransformOp back; from_json(jt, back);
        CHECK(back == TransformOp::Complexify);
    }

    // --- Derived motifs: chains resolve parent-first; missing parent throws
    //     by name; generationSeed makes a random op deterministic.
    {
        Motif a; a.name = "a"; a.userProvided = true;
        {
            MelodicFigure fa;
            fa.units.push_back({1.0f, 0});
            fa.units.push_back({1.0f, 1});
            a.content = fa;
        }
        Motif b; b.name = "b"; b.derivedFrom = "a";
        b.transform = TransformOp::Invert; b.content = MelodicFigure{};
        Motif d; d.name = "d"; d.derivedFrom = "b";
        d.transform = TransformOp::Reverse; d.content = MelodicFigure{};
        PieceTemplate tmpl; tmpl.motifs = {a, b, d};
        Randomizer rng(1u);
        realize_motifs(tmpl, rng);
        CHECK(tmpl.realizedMotifs.count("b") == 1);
        CHECK(tmpl.realizedMotifs.count("d") == 1);
        CHECK(tmpl.realizedMotifs.at("b").units[1].step == -1);

        PieceTemplate bad;
        Motif z; z.name = "z"; z.derivedFrom = "ghost";
        z.content = MelodicFigure{};
        bad.motifs = {z};
        bool threw = false;
        try { Randomizer r2(1u); realize_motifs(bad, r2); }
        catch (const std::exception& e) {
            threw = std::string(e.what()).find("ghost") != std::string::npos;
        }
        CHECK(threw);

        Motif v; v.name = "v"; v.derivedFrom = "a";
        v.transform = TransformOp::VarySteps; v.transformParam = 1;
        v.generationSeed = 42u; v.content = MelodicFigure{};
        PieceTemplate t2; t2.motifs = {a, v};
        PieceTemplate t3; t3.motifs = {a, v};
        Randomizer r3(9u);    realize_motifs(t2, r3);
        Randomizer r4(1234u); realize_motifs(t3, r4);
        const auto& u2 = t2.realizedMotifs.at("v").units;
        const auto& u3 = t3.realizedMotifs.at("v").units;
        bool sameV = u2.size() == u3.size();
        for (size_t k = 0; sameV && k < u2.size(); ++k)
            sameV = u2[k].step == u3[k].step;
        CHECK(sameV);
    }

    // --- Anchor selector: R1/R2/R3 hold across seeds; dense connectors;
    //     the low-scoring start is rarely chosen; parallel pinning wins.
    {
        HarmonyTimeline tl;
        ChordProgression prog;
        ScaleChord c1; c1.degree = 0; c1.quality = &ChordDef::get("Major");
        ScaleChord g7; g7.degree = 4; g7.quality = &ChordDef::get("7");
        prog.add(c1, 4.0f);
        prog.add(g7, 2.0f);
        prog.add(c1, 2.0f);
        tl.set_segment(0.0f, 8.0f, prog, "test");

        MelodicFigure fA;   // 4 beats, net -1
        fA.units.push_back({1.0f, 0});
        fA.units.push_back({1.0f, -1});
        fA.units.push_back({2.0f, 0});
        MelodicFigure fB;   // 4 beats, notes at beats 4,5,6 (G7,G7,C)
        fB.units.push_back({1.0f, 0});
        fB.units.push_back({1.0f, 1});
        fB.units.push_back({2.0f, 0});
        std::vector<const MelodicFigure*> figs = {&fA, &fB};
        const Pitch reg = Pitch::from_note_number(64.0f);   // E5 register

        for (uint32_t s = 1; s <= 30; ++s) {
            Randomizer r(s);
            PhraseTemplate local; local.name = "t";
            select_anchors(local, figs, tl, c, 0.0f, 4.0f, 1.0f,
                           /*isPassageFinal*/ true, std::nullopt, reg, r, prof, {}, 0.0f, false);
            CHECK(local.startingPitch.has_value());
            CHECK(local.connectors.size() == 2);
            CHECK(!local.connectors[0].has_value());
            CHECK(local.connectors[1].has_value());
            const int pc0 =
                ((int(local.startingPitch->note_number()) % 12) + 12) % 12;
            CHECK(pc0 == 0 || pc0 == 4 || pc0 == 7);        // R1
            const int g0 =
                scale_grid_index(local.startingPitch->note_number(), c);
            const int g1 = g0 + (-1) + local.connectors[1]->leadStep;
            const int last = g1 + 1;                        // fB net to last
            CHECK(((last % 7) + 7) % 7 == 0);               // R3 (tonic)
        }

        // Parallel pinning: a pinned start is taken verbatim.
        {
            Randomizer r(5u);
            PhraseTemplate local; local.name = "t2";
            select_anchors(local, figs, tl, c, 0.0f, 4.0f, 1.0f, true,
                           Pitch::from_note_number(67.0f), reg, r, prof, {}, 0.0f, false);
            CHECK(local.startingPitch
                  && int(local.startingPitch->note_number()) == 67);
        }

        // Weighting: single-figure phrase where anchors E/G keep both notes
        // chord tones and anchor C leaves its second (long, figure-final)
        // note off-chord — C should be a rare pick.
        {
            MelodicFigure fW;   // 4 beats: [0, -2], second note long+final
            fW.units.push_back({2.0f, 0});
            fW.units.push_back({2.0f, -2});
            std::vector<const MelodicFigure*> one = {&fW};
            int cPicks = 0;
            for (uint32_t s = 1; s <= 100; ++s) {
                Randomizer r(s);
                PhraseTemplate local; local.name = "w";
                select_anchors(local, one, tl, c, 0.0f, 4.0f, 1.0f,
                               /*isPassageFinal*/ false, std::nullopt, reg, r, prof, {}, 0.0f, false);
                const int pc0 =
                    ((int(local.startingPitch->note_number()) % 12) + 12) % 12;
                if (pc0 == 0) ++cPicks;
            }
            CHECK(cPicks < 15);
        }

        // Empty legal set throws with the phrase name: force R3 onto a
        // timeline whose final chord contains no tonic (B diminished-ish via
        // G7 only) — no chain can end on degree 1 as a chord tone.
        {
            HarmonyTimeline tl2;
            ChordProgression p2;
            p2.add(g7, 8.0f);
            tl2.set_segment(0.0f, 8.0f, p2, "test");
            Randomizer r(3u);
            PhraseTemplate local; local.name = "impossible";
            bool threw = false;
            try {
                select_anchors(local, figs, tl2, c, 0.0f, 4.0f, 1.0f,
                               true, std::nullopt, reg, r, prof, {}, 0.0f, false);
            } catch (const std::exception& e) {
                threw = std::string(e.what()).find("impossible")
                        != std::string::npos;
            }
            CHECK(threw);
        }
    }

    // --- End to end: harmonic anchorMode through ClassicalComposer.
    {
        json tj = json::parse(R"({
          "keyName": "C", "scaleName": "Major", "bpm": 80, "masterSeed": 7,
          "sections": [{"name": "Main", "beats": 8,
            "chordProgression": [
              {"degree": 0, "beats": 4},
              {"degree": 4, "quality": "7", "beats": 2},
              {"degree": 0, "beats": 2}]}],
          "motifs": [
            {"name": "m1", "userProvided": true, "figure": {"units": [
              {"duration": 1.0, "step": 0}, {"duration": 1.0, "step": -1},
              {"duration": 2.0, "step": 0}]}},
            {"name": "m2", "userProvided": true, "figure": {"units": [
              {"duration": 1.0, "step": 0}, {"duration": 1.0, "step": 1},
              {"duration": 2.0, "step": 0}]}}
          ],
          "parts": [{"name": "melody", "role": "melody", "passages": {
            "Main": {"anchorMode": "harmonic", "melodyProfile": "nursery_v1",
                     "startingPitch": {"octave": 5, "pitch": "E"},
                     "phrases": [{
                        "name": "p1",
                        "startingPitch": {"octave": 5, "pitch": "E"},
                        "figures": [
                          {"source": "reference", "motifName": "m1"},
                          {"source": "reference", "motifName": "m2"}]}]}}}]
        })");
        PieceTemplate tmpl; from_json(tj, tmpl);
        Piece piece;
        ClassicalComposer composer(tmpl.masterSeed);
        composer.compose(piece, tmpl);
        CHECK(!piece.parts.empty());
        CHECK(piece.parts[0].elementSequence.size() == 6);
        // First event: R1 pitch class; last event: tonic (R3).
        const auto& els = piece.parts[0].elementSequence.elements;
        const int pcF = ((int(els.front().note().noteNumber) % 12) + 12) % 12;
        const int pcL = ((int(els.back().note().noteNumber) % 12) + 12) % 12;
        CHECK(pcF == 0 || pcF == 4 || pcF == 7);
        CHECK(pcL == 0);
    }
}

static void run_walk2_tests() {
    using namespace mforce;
    Scale c = Scale::get("C", "Major");

    // --- elaborate(): rung 1 always splits the long note evenly; totals
    //     preserved; nothing below a sixteenth; late-placement bias; fine
    //     splits rare and mostly dotted.
    {
        MelodicFigure rep3;
        rep3.units.push_back({1.0f, 0});
        rep3.units.push_back({1.0f, 0});
        rep3.units.push_back({2.0f, 0});
        for (uint32_t s = 1; s <= 20; ++s) {
            Randomizer r(s);
            auto e4 = figure_transforms::elaborate(rep3, r, 4);
            CHECK(e4.note_count() == 4);
            bool allQ = true;
            for (auto& u : e4.units) if (u.duration != 1.0f) allQ = false;
            CHECK(allQ);                       // h -> q q, Mary's own move
            CHECK(e4.net_step() == 0);
        }
        int fineCount = 0, dottedCount = 0;
        double smallStartSum = 0.0;
        int smallCount = 0;
        for (uint32_t s = 1; s <= 400; ++s) {
            Randomizer r(s);
            auto e5 = figure_transforms::elaborate(rep3, r, 5);
            CHECK(std::fabs(e5.total_duration() - 4.0f) < 1e-4f);
            float minDur = 99.0f, minStart = 0.0f, start = 0.0f;
            bool hasDotted = false, hasFine = false;
            for (auto& u : e5.units) {
                CHECK(u.duration >= 0.25f - 1e-6f);
                if (u.duration < minDur) { minDur = u.duration; minStart = start; }
                if (u.duration == 0.75f) hasDotted = true;
                if (u.duration <= 0.25f + 1e-6f) hasFine = true;
                start += u.duration;
            }
            if (hasFine || hasDotted) { ++fineCount; if (hasDotted) ++dottedCount; }
            if (minDur < 1.0f) { smallStartSum += minStart; ++smallCount; }
        }
        // Depth policy: fine splits rare (~20% of the ONE post-rung split).
        CHECK(fineCount > 20 && fineCount < 160);
        // Dotted dominates fine (~80%).
        CHECK(dottedCount * 2 > fineCount);
        // Late-placement: mean start beat of the smallest unit is past the
        // bar's first half (uniform would be ~1.5 on a 4-beat figure).
        CHECK(smallCount > 0 && smallStartSum / smallCount > 1.6);
    }

    // --- Selector R4 (cadential register memory) + regression plumbing:
    //     final note must be a visited pitch when a visited set exists.
    {
        HarmonyTimeline tl;
        ChordProgression prog;
        ScaleChord c1; c1.degree = 0; c1.quality = &ChordDef::get("Major");
        prog.add(c1, 4.0f);
        tl.set_segment(0.0f, 4.0f, prog, "test");
        MelodicFigure whole;
        whole.units.push_back({4.0f, 0});
        std::vector<const MelodicFigure*> figs = {&whole};
        const Pitch reg = Pitch::from_note_number(64.0f);
        MelodyProfile prof = MelodyProfile::load_by_name("nursery_v1");
        // Prior track: C5 only (a whole note in the bar before).
        const std::vector<TrackNote> prior = {{scale_grid_index(60.0f, c), -4.0f, 4.0f}};
        for (uint32_t s = 1; s <= 20; ++s) {
            Randomizer r(s);
            PhraseTemplate local; local.name = "r4";
            auto res = select_anchors(local, figs, tl, c, 0.0f, 4.0f, 1.0f,
                           true, std::nullopt, reg, r, prof, prior, 4.0f, false);
            CHECK(local.startingPitch
                  && int(local.startingPitch->note_number()) == 60);
            CHECK(res.notes.size() == 1);      // the chain's notes, returned
        }
        // Without any visited set, C5 and C6 both stay legal (guard).
        bool saw60 = false, saw72 = false;
        for (uint32_t s = 1; s <= 60; ++s) {
            Randomizer r(s);
            PhraseTemplate local; local.name = "r4b";
            select_anchors(local, figs, tl, c, 0.0f, 4.0f, 1.0f,
                           true, std::nullopt, reg, r, prof, {}, 0.0f, false);
            const int nn = int(local.startingPitch->note_number());
            CHECK(nn == 60 || nn == 72);
            if (nn == 60) saw60 = true;
            if (nn == 72) saw72 = true;
        }
        CHECK(saw60);
        (void)saw72;   // 72 may be rare; only legality is asserted
    }
}

#include "mforce/music/melody_profile.h"
#include "mforce/music/style_table.h"
#include "mforce/music/note_map.h"
#include "mforce/music/phrase_critic.h"
#include <set>

static void run_walk3_tests() {
    using namespace mforce;
    // --- vary_steps reaches Mary's exact head_a from head ([0,-1,-1,+1] ->
    //     [0,0,+1,-1]) once the final step is perturbable (spec §4).
    {
        MelodicFigure head;
        head.units.push_back({1.0f, 0});
        head.units.push_back({1.0f, -1});
        head.units.push_back({1.0f, -1});
        head.units.push_back({1.0f, 1});
        bool reached = false;
        bool step0Touched = false;
        for (uint32_t s = 1; s <= 20000 && !reached; ++s) {
            auto f = figure_transforms::apply(head, TransformOp::VarySteps, 3, s);
            if (f.units[0].step != 0) step0Touched = true;
            if (f.units[1].step == 0 && f.units[2].step == 1
                && f.units[3].step == -1) reached = true;
        }
        CHECK(reached);
        CHECK(!step0Touched);
    }

    // --- MelodyProfile: strict parse of the shipped NRS v1 file.
    {
        MelodyProfile p = MelodyProfile::load_by_name("nursery_v1");
        CHECK(p.name == "nursery_v1");
        CHECK(p.tendencies.count("V7:7") == 1);
        CHECK(std::fabs(p.tendencies.at("V7:7").odds_per_100("step_down") - 96.0) < 1e-9);
        CHECK(std::fabs(p.tendencies.at("V7:7").odds_per_100("leap_up") - 4.0) < 1e-9); // "other"
        CHECK(std::fabs(p.nct.appoggiatura - 3.0) < 1e-9);
        CHECK(p.critic.departureBudget == 1);
        CHECK(p.search.phraseCandidates == 10 && p.search.topK == 3
              && p.search.passageCandidates == 10);
        // Missing key -> throws naming it.
        json j = json::parse(R"({"melody": {"tendencies": {}}})");
        bool threw = false;
        try { MelodyProfile::parse_json(j); }
        catch (const std::exception& e) {
            threw = std::string(e.what()).find("nct") != std::string::npos;
        }
        CHECK(threw);
        // StyleTable still parses a file carrying a "melody" block.
        StyleTable st = StyleTable::load_by_name("nursery_v1");
        CHECK(st.transitions.count("I") == 1);
        // Template round trip keeps melodyProfile.
        PassageTemplate pt; pt.melodyProfile = "nursery_v1";
        json pj; to_json(pj, pt);
        PassageTemplate back; from_json(pj, back);
        CHECK(back.melodyProfile == "nursery_v1");
    }

    // --- note_map: classification cases from Matt's annotations.
    {
        Scale c = Scale::get("C", "Major");
        MelodyProfile prof = MelodyProfile::load_by_name("nursery_v1");
        ScaleChord cI;  cI.degree = 0; cI.quality = &ChordDef::get("Major");
        ScaleChord g7;  g7.degree = 4; g7.quality = &ChordDef::get("7");
        auto timeline = [&](std::vector<std::pair<ScaleChord, float>> segs) {
            HarmonyTimeline tl; ChordProgression pr; float tot = 0;
            for (auto& [sc, b] : segs) { pr.add(sc, b); tot += b; }
            tl.set_segment(0.0f, tot, pr, "t");
            return tl;
        };
        auto G = [&](float nn) { return scale_grid_index(nn, c); };
        // Quarter-note track from note numbers, starting at beat 0.
        auto track = [&](std::vector<float> nns) {
            std::vector<TrackNote> t; float b = 0;
            for (float nn : nns) { t.push_back({G(nn), b, 1.0f}); b += 1.0f; }
            return t;
        };
        auto kinds = [&](const NoteMapResult& r) {
            std::vector<std::string> k;
            for (auto& v : r.verdicts) k.push_back(v.kind);
            return k;
        };
        // Mary bar 7 over G7 then C: D D E D | C -> E is an upper NEIGHBOR (licensed).
        {
            HarmonyTimeline tl = timeline({{g7, 4}, {cI, 4}});
            ChordLookup cl(tl, c);
            auto r = evaluate_note_map(track({74, 74, 76, 74, 72}), 0, cl, c, 4.0f, prof, true);
            auto k = kinds(r);
            CHECK(std::find(k.begin(), k.end(), "neighbor") != k.end());
            CHECK(r.departures == 0);
        }
        // D D D E | C -> E leaves by leap: ESCAPE tone (departure).
        {
            HarmonyTimeline tl = timeline({{g7, 4}, {cI, 4}});
            ChordLookup cl(tl, c);
            auto r = evaluate_note_map(track({74, 74, 74, 76, 72}), 0, cl, c, 4.0f, prof, true);
            auto k = kinds(r);
            CHECK(std::find(k.begin(), k.end(), "escape") != k.end());
            CHECK(r.departures == 1);
        }
        // Passing tone: E D C over C -> D passing, odds 100, contributes 0.
        {
            HarmonyTimeline tl = timeline({{cI, 4}});
            ChordLookup cl(tl, c);
            auto r = evaluate_note_map(track({76, 74, 72, 72}), 0, cl, c, 4.0f, prof, true);
            CHECK(kinds(r).size() >= 1 && kinds(r)[0] == "passing");
            CHECK(std::fabs(r.logScore) < 1e-9);
        }
        // Suspension (walk1 s101): F F F F over G7 | F E over C -> F held into C,
        // resolves down by step = suspension; F G instead = unresolved (departure).
        {
            HarmonyTimeline tl = timeline({{g7, 4}, {cI, 4}});
            ChordLookup cl(tl, c);
            auto good = evaluate_note_map(track({77, 77, 77, 77, 77, 76, 76, 76}), 0, cl, c, 4.0f, prof, true);
            auto kg = kinds(good);
            CHECK(std::find(kg.begin(), kg.end(), "suspension") != kg.end());
            CHECK(good.departures == 0);
            auto bad = evaluate_note_map(track({77, 77, 77, 77, 77, 79, 79, 79}), 0, cl, c, 4.0f, prof, true);
            auto kb = kinds(bad);
            CHECK(std::find(kb.begin(), kb.end(), "unresolvedSuspension") != kb.end());
            CHECK(bad.departures == 1);
        }
        // Tendency: 7th of V7 (F) stepping down vs up -> 96:4 = ln ratio ln(24).
        {
            HarmonyTimeline tl = timeline({{g7, 4}});
            ChordLookup cl(tl, c);
            auto down = evaluate_note_map(track({77, 76}), 0, cl, c, 4.0f, prof, true);
            auto up   = evaluate_note_map(track({77, 79}), 0, cl, c, 4.0f, prof, true);
            // F->E: E is an NCT over G7 left with no next -> unscored; only F's
            // tendency differs between the two tracks.
            CHECK(std::fabs((down.logScore - up.logScore) - std::log(24.0)) < 1e-6);
            CHECK(up.departures == 1 && down.departures == 0);
        }
        // Leading tone B over G7 leaping down to F (walk2 s101): tendency
        // "other" (1/100) -> departure.
        {
            HarmonyTimeline tl = timeline({{g7, 4}, {cI, 4}});
            ChordLookup cl(tl, c);
            auto r = evaluate_note_map(track({71, 71, 71, 71, 77, 79}), 0, cl, c, 4.0f, prof, true);
            CHECK(r.departures >= 2);   // B leap-down + F appoggiatura over C
        }
        // Accented modifier: neighbor on the bar downbeat.
        {
            HarmonyTimeline tl = timeline({{cI, 4}, {cI, 4}});
            ChordLookup cl(tl, c);
            // beats 0..: C E G G | A G ... -> A at beat 4 (downbeat), step in, step out opposite
            auto r = evaluate_note_map(track({72, 76, 79, 79, 81, 79}), 0, cl, c, 4.0f, prof, true);
            bool found = false;
            for (auto& v : r.verdicts)
                if (v.kind == "neighbor" && std::fabs(v.odds - 30.0) < 1e-9) found = true;
            CHECK(found);
        }
        // Long NCT: E held 2 beats over G7 -> odds 1.
        {
            HarmonyTimeline tl = timeline({{g7, 4}, {cI, 4}});
            ChordLookup cl(tl, c);
            std::vector<TrackNote> t = {{G(74), 0, 1}, {G(76), 1, 2}, {G(74), 3, 1}, {G(72), 4, 4}};
            auto r = evaluate_note_map(t, 0, cl, c, 4.0f, prof, true);
            bool found = false;
            for (auto& v : r.verdicts) if (std::fabs(v.odds - 1.0) < 1e-9) found = true;
            CHECK(found);
        }
        // Scope: firstScoredNote excludes earlier fully-determined events.
        {
            HarmonyTimeline tl = timeline({{cI, 8}});
            ChordLookup cl(tl, c);
            auto all  = evaluate_note_map(track({76, 74, 72, 74, 76, 74, 72}), 0, cl, c, 4.0f, prof, true);
            auto tail = evaluate_note_map(track({76, 74, 72, 74, 76, 74, 72}), 4, cl, c, 4.0f, prof, true);
            CHECK(all.verdicts.size() > tail.verdicts.size());
        }
    }

    // --- phrase critic + top-k.
    {
        Scale c = Scale::get("C", "Major");
        MelodyProfile prof = MelodyProfile::load_by_name("nursery_v1");
        ScaleChord cI; cI.degree = 0; cI.quality = &ChordDef::get("Major");
        HarmonyTimeline tl; ChordProgression pr; pr.add(cI, 16.0f);
        tl.set_segment(0.0f, 16.0f, pr, "t");
        ChordLookup cl(tl, c);
        auto ph = [&](std::vector<int> grids) {
            std::vector<PhraseNote> v; float b = 0;
            for (int g : grids) { v.push_back({g, b, 1.0f, 0}); b += 1.0f; }
            return v;
        };
        // Range: span 4 costs nothing; span 10 costs 1.5 * 3.
        auto small = score_phrase_critic(ph({35, 36, 37, 38, 39}), {}, {}, false, 0.0f, cl, prof.critic);
        auto wide  = score_phrase_critic(ph({30, 32, 34, 36, 38, 40}), {}, {}, false, 0.0f, cl, prof.critic);
        CHECK(std::fabs(small.range) < 1e-9);
        CHECK(std::fabs(wide.range - (-4.5)) < 1e-9);
        // Motion: 3 repeats of 4 moves (0.75) -> -4 * 0.25 = -1.
        auto rep = score_phrase_critic(ph({35, 35, 35, 35, 36}), {}, {}, false, 0.0f, cl, prof.critic);
        CHECK(std::fabs(rep.motion - (-1.0)) < 1e-9);
        // Gap-fill: leap up 3 then step down -> +1.5.
        auto gap = score_phrase_critic(ph({35, 38, 37}), {}, {}, false, 0.0f, cl, prof.critic);
        CHECK(std::fabs(gap.gap - 1.5) < 1e-9);
        // pick_top_k: in-budget always outranks over-budget; k bounds the pool.
        std::vector<RankItem> items = {{false, 100.0}, {true, 1.0}, {true, 0.5}, {true, -50.0}};
        for (uint32_t s = 1; s <= 200; ++s) {
            Randomizer r(s);
            size_t p = pick_top_k(items, 2, r);
            CHECK(p == 1 || p == 2);
        }
        std::vector<RankItem> none = {{false, 3.0}, {false, 1.0}};
        Randomizer r1(1u);
        CHECK(pick_top_k(none, 1, r1) == 0);
    }

    // --- select_anchors: the 7th of V7 now resolves down ~24:1 (was ~400:1),
    //     measured end to end on a 2-figure phrase over G7 -> C.
    {
        Scale c = Scale::get("C", "Major");
        MelodyProfile prof = MelodyProfile::load_by_name("nursery_v1");
        ScaleChord cI; cI.degree = 0; cI.quality = &ChordDef::get("Major");
        ScaleChord g7; g7.degree = 4; g7.quality = &ChordDef::get("7");
        HarmonyTimeline tl; ChordProgression pr;
        pr.add(g7, 2.0f); pr.add(cI, 2.0f);
        tl.set_segment(0.0f, 4.0f, pr, "t");
        MelodicFigure a; a.units.push_back({2.0f, 0});          // over G7
        MelodicFigure b; b.units.push_back({2.0f, 0});          // over C
        std::vector<const MelodicFigure*> figs = {&a, &b};
        const Pitch reg = Pitch::from_note_number(77.0f);       // F5
        int down = 0, up = 0;
        for (uint32_t s = 1; s <= 400; ++s) {
            Randomizer r(s);
            PhraseTemplate local; local.name = "sev";
            auto res = select_anchors(local, figs, tl, c, 0.0f, 4.0f, 1.0f, false,
                                      Pitch::from_note_number(77.0f), reg, r,
                                      prof, {}, 0.0f, false);
            (void)res;
            const int lead = local.connectors[1]->leadStep;
            if (lead == -1) ++down;
            if (lead > 0) ++up;
        }
        CHECK(down > up);     // the 96-row dominates; exact ratio is mixed with placement
    }

    // --- best-of-N end to end on the Mary walk template: deterministic per
    //     seed; legality (R3); variety across seeds.
    {
        std::ifstream f("scores/baselines/template_mary_walk.json");
        json base = json::parse(f);
        auto melody = [&](uint32_t seed) {
            json tj = base; tj["masterSeed"] = seed;
            PieceTemplate tmpl; from_json(tj, tmpl);
            Piece piece; ClassicalComposer composer(tmpl.masterSeed);
            composer.compose(piece, tmpl);
            std::vector<int> nns;
            for (auto& part : piece.parts) {
                if (part.name != "melody") continue;
                for (auto& el : part.elementSequence.elements)
                    nns.push_back(int(el.note().noteNumber));
            }
            return nns;
        };
        auto a = melody(100), b = melody(100);
        CHECK(a == b);                          // deterministic
        CHECK(!a.empty() && (a.back() % 12) == 0);   // R3
        std::set<std::vector<int>> distinct;
        for (uint32_t s = 100; s < 110; ++s) distinct.insert(melody(s));
        CHECK(distinct.size() >= 3);            // search didn't collapse to one tune
    }
}

int main() {
    run_walk2_tests();
    run_walk3_tests();
    run_walk1_tests();
    run_passage_melody_tests();
    run_passage_parse_tests();
    run_onset_field_tests();
    run_hold_delivery_tests();
    run_note_reseed_tests();
    run_envelope_retrigger_tests();
    run_release_relayout_tests();
    run_name_gate_tests();
    run_onset_trigger_tests();
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
