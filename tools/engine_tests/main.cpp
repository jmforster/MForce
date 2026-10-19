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

static void run_stage_nominal_tests() {
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
    // Clamp to +-8
    auto c = std::make_shared<ConstantSource>(42.0f);
    c->next();
    TapSource tap(c);
    CHECK_NEAR(tap.next(), 8.0f, 1e-6f);
    c->set(-100.0f); c->next();
    CHECK_NEAR(tap.next(), -8.0f, 1e-6f);
    c->set(3.5f); c->next();
    CHECK_NEAR(tap.next(), 3.5f, 1e-6f);

    // NaN/inf scrub to 0
    c->set(std::nanf("")); c->next();
    CHECK_NEAR(tap.next(), 0.0f, 1e-6f);
    c->set(INFINITY); c->next();
    CHECK_NEAR(tap.next(), 0.0f, 1e-6f);

    // Null source still reads 0 (pass-2 placeholder state)
    TapSource empty;
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
    auto tap = std::make_shared<TapSource>(counter);
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
    auto dl = std::make_shared<DelayLineSource>();
    dl->set_param("frequency", std::make_shared<ConstantSource>(480.0f));
    auto in = std::make_shared<ConstantSource>(0.0f);
    dl->set_param("source", in);
    dl->prepare(ctx); dl->start(48000);

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
    auto dl2 = std::make_shared<DelayLineSource>();
    dl2->set_param("frequency", std::make_shared<ConstantSource>(480.0f));
    dl2->set_param("ratio", std::make_shared<ConstantSource>(2.0f));
    auto in2 = std::make_shared<ConstantSource>(0.0f);
    dl2->set_param("source", in2);
    dl2->prepare(ctx); dl2->start(48000);
    in2->set(1.0f); dl2->next(); in2->set(0.0f);
    for (int i = 0; i < 199; ++i) out = dl2->next();
    out = dl2->next();
    CHECK_NEAR(out, 1.0f, 1e-3f);

    // prepare() clears the line: no ring-over between notes
    dl2->prepare(ctx); dl2->start(48000);
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
        SVFSource f;
        f.set_setting("mode", float(c.mode));
        f.set_param("source", in);
        f.set_param("cutoffFreq", std::make_shared<ConstantSource>(c.fc));
        f.set_param("resonance", std::make_shared<ConstantSource>(c.res));
        f.prepare(ctx); f.start(48000);
        const float meas = measure_phase_delay(f, *in, c.hz, 48000);
        const float pred = f.phase_delay_at(c.hz);
        CHECK_NEAR(pred, meas, 0.15f);
    }
    // At fc the 2-pole LP delay is exactly (pi/2)/w regardless of res.
    {
        auto in = std::make_shared<ConstantSource>(0.0f);
        SVFSource f;
        f.set_param("source", in);
        f.set_param("cutoffFreq", std::make_shared<ConstantSource>(1000.0f));
        f.prepare(ctx); f.start(8);
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
        auto dl = std::make_shared<DelayLineSource>();
        dl->set_param("frequency", std::make_shared<ConstantSource>(480.0f));
        auto ex = std::make_shared<ConstantSource>(0.0f);
        auto sum = std::make_shared<CombinedSource>(
            ex, std::make_shared<ConstantSource>(0.0f), CombineOp::Sum, 0.0f);
        sum->set_param("source2", std::make_shared<TapSource>(dl));
        dl->set_param("source", sum);
        if (comp) dl->set_setting("compensate", 1.0f);
        dl->prepare(ctx); dl->start(48000);
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
        auto dl = std::make_shared<DelayLineSource>();
        dl->set_param("frequency", std::make_shared<ConstantSource>(200.0f));
        auto ex = std::make_shared<ConstantSource>(0.0f);
        auto sum = std::make_shared<CombinedSource>(
            ex, std::make_shared<ConstantSource>(0.0f), CombineOp::Sum, 0.0f);
        sum->set_param("source2", std::make_shared<TapSource>(dl));
        // fc between mode 1 (200 Hz) and mode 2 (~400 Hz): only the
        // fundamental sees |H| > 1 and grows, so the settled tail is a
        // near-sine at the loop's mode-1 frequency. (With a higher cutoff
        // several modes grow at once and the STRETCHED upper modes — loop
        // lag varies with frequency, exactly like KS partials — dominate
        // the autocorrelation.) fc 300 also makes the lag big: ~33 samples,
        // 14% of the period, so uncompensated flatness is unmistakable.
        auto svf = std::make_shared<SVFSource>();
        svf->set_param("source", sum);
        svf->set_param("cutoffFreq", std::make_shared<ConstantSource>(300.0f));
        svf->set_param("resonance", std::make_shared<ConstantSource>(1.0f));
        dl->set_param("source", svf);
        if (comp) dl->set_setting("compensate", 1.0f);
        dl->prepare(ctx); dl->start(48000);
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
    sh->prepare(ctx); sh->start(100);
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
    dz->prepare(ctx); dz->start(100);
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
    sh.prepare(ctx); sh.start(8);
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
    wh.prepare(ctx); wh.start(8);
    CHECK_NEAR(wh.next(), 0.42f, 1e-9f);
    auto whIn = std::make_shared<WormholeSource>();
    whIn->set_param("source", in);
    WormholeSource whOut;
    whOut.set_param("source", whIn);
    whOut.prepare(ctx); whOut.start(8);
    CHECK_NEAR(whOut.next(), 0.42f, 1e-9f);
    // Unwired = silence, not garbage
    WormholeSource bare;
    bare.prepare(ctx); bare.start(8);
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
    sh.prepare(ctx); sh.start(8);
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
    sh.prepare(ctx); sh.start(8);
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

struct Playback final : mforce::Generator {
    const std::vector<float>* buf{nullptr};
    size_t i{0};
    const char* type_name() const override { return "Playback"; }
    mforce::SourceCategory category() const override {
        return mforce::SourceCategory::Modulator;
    }
    void on_start(int) override { i = 0; }
    float compute() override { return (buf && i < buf->size()) ? (*buf)[i++] : 0.0f; }
};

double worst_energy_ratio(const std::vector<float>& x, float aNeg, float aPos) {
    RenderContext ctx{48000};
    auto src = std::make_shared<Playback>();
    src->buf = &x;
    PierceFilterSource f;
    f.set_param("source", src);
    f.set_param("coefNeg", std::make_shared<ConstantSource>(aNeg));
    f.set_param("coefPos", std::make_shared<ConstantSource>(aPos));
    f.prepare(ctx); f.start(int(x.size()));
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
        ip.instrument->render(b.data(), n);
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

// Renders notes 60 and 72 of an instrument patch whose paramMap drives `osc`'s
// frequency: the load must succeed, the output be finite and audible, and the
// octave double the zero crossings, so the note's pitch reached the oscillator.
static void check_param_map_pitch_reaches_osc(const std::string& patch) {
    const int n = 12000;
    auto render = [&](float midi) {
        auto ip = load_instrument_patch_json(patch);
        auto* pi = dynamic_cast<PitchedInstrument*>(ip.instrument.get());
        pi->play_note({midi, 0.8f, 0.4f}, 0.0f);
        std::vector<float> b(size_t(n), 0.0f);
        ip.instrument->render(b.data(), n);
        return b;
    };
    std::vector<float> lo, hi;
    bool loaded = true;
    try { lo = render(60.0f); hi = render(72.0f); }
    catch (const std::exception&) { loaded = false; }
    CHECK(loaded);
    if (!loaded) return;

    bool finite = true;
    float peak = 0.0f;
    for (float v : lo) { finite = finite && std::isfinite(v); peak = std::max(peak, std::fabs(v)); }
    CHECK(finite);
    CHECK(peak > 0.1f);
    auto crossings = [](const std::vector<float>& b) {
        int c = 0;
        for (size_t i = 1; i < b.size(); ++i) c += (b[i] >= 0.0f) != (b[i - 1] >= 0.0f);
        return c;
    };
    const float ratio = float(crossings(hi)) / float(std::max(1, crossings(lo)));
    CHECK(ratio > 1.8f && ratio < 2.2f);       // an octave up doubles the frequency
}

// A paramMap target on a param the patch leaves unwired loads and binds: the
// loader gives the pin a constant to drive, and the note's pitch reaches it.
static void run_param_map_unwired_pin_tests() {
    check_param_map_pitch_reaches_osc(R"({
      "sampleRate": 48000,
      "instrument": { "polyphony": 2, "paramMap": { "frequency": "osc.frequency" } },
      "graph": { "output": "osc",
        "nodes": [ { "id": "osc", "type": "SineSource", "params": { "phase": 0.0 } } ] }
    })");
}

// The same target under a MultiplexSource: the clones are built from the
// template JSON, so each clone's unwired pin gets its constant when the
// per-note push fans out, and the pitch reaches every clone.
static void run_param_map_unwired_multiplex_tests() {
    check_param_map_pitch_reaches_osc(R"({
      "sampleRate": 48000,
      "instrument": { "polyphony": 2, "paramMap": { "frequency": "osc.frequency" } },
      "graph": { "output": "mux", "nodes": [
        { "id": "osc", "type": "SineSource", "params": { "phase": 0.0 } },
        { "id": "mux", "type": "MultiplexSource",
          "params": { "count": 3, "source": { "ref": "osc" } } } ] }
    })");
}

static void run_envelope_retrigger_tests() {
    // Seconds-mode gesture: 10ms 1.0->0.2, 20ms 0.2->1.0, expand@1.0.
    const int sr = 48000;
    Envelope env;
    env.absolute_time = true;
    env.add_stage({{1.0f, 0.2f, RampType::Linear, 0.0f}, 0.010f, 0.0f, 0.0f});
    env.add_stage({{0.2f, 1.0f, RampType::Linear, 0.0f}, 0.020f, 0.0f, 0.0f});
    env.add_stage({{1.0f, 1.0f, RampType::Linear, 0.0f}, 0.0f,   0.0f, 0.0f});
    env.prepare(RenderContext{sr}); env.start(sr);   // 1.0 s

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
    Envelope env;
    env.add_stage({{0.0f, 1.0f, RampType::Linear, 0.0f}, 0.10f, 0.0f, 0.0f});
    env.add_stage({{1.0f, 0.7f, RampType::Linear, 0.0f}, 0.10f, 0.0f, 0.0f});
    env.add_stage({{0.7f, 0.7f, RampType::Linear, 0.0f}, 0.0f,  0.0f, 0.0f});
    env.add_stage({{0.7f, 0.0f, RampType::Linear, 0.0f}, 0.25f, 0.0f, 0.0f});
    env.set_gated(true);
    env.prepare(RenderContext{sr}); env.start(int(0.4f * sr));      // quarter note
    for (int i = 0; i < int(0.35f * sr); ++i) env.next(); // into the hold
    // Release re-referenced to a HALF note: 25% of 0.8 s = 0.2 s.
    int rem = env.gate_release(int(0.8f * sr));
    CHECK(std::abs(rem - int(0.25f * 0.8f * sr)) <= 2);
    // Default arg keeps old behavior: fresh envelope, no ref. The
    // prepare-time layout resolves percent against the LAYOUT window,
    // i.e. the note minus the engine-wide reflection allowance — that
    // 10 ms is why the expectation is not a flat 25% of 0.4 s.
    Envelope e2;
    e2.add_stage({{0.0f, 1.0f, RampType::Linear, 0.0f}, 0.10f, 0.0f, 0.0f});
    e2.add_stage({{1.0f, 1.0f, RampType::Linear, 0.0f}, 0.0f,  0.0f, 0.0f});
    e2.add_stage({{1.0f, 0.0f, RampType::Linear, 0.0f}, 0.25f, 0.0f, 0.0f});
    e2.set_gated(true);
    e2.prepare(RenderContext{sr}); e2.start(int(0.4f * sr));
    for (int i = 0; i < int(0.2f * sr); ++i) e2.next();
    int rem2 = e2.gate_release();
    CHECK(std::abs(rem2 - int(0.25f * (0.4f - Envelope::kReflectionAllowanceSec)
                              * sr)) <= 2);
}

#include "mforce/core/name_gate.h"

static void run_name_gate_tests() {
    auto ps = std::make_shared<PerformSource>();
    auto in = std::make_shared<PerformOut>(ps, PerformOut::Field::Onset);
    auto ng = std::make_shared<NameGate>();
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
    ip.instrument->render(buf.data(), N);

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
    ip2.instrument->render(buf2.data(), N);
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
    ip.instrument->render(buf.data(), int(buf.size()));
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
        // Decision log names a departure when the chosen chain takes one:
        // the 7th of V7 stepping UP (4/100, below departureBelow 25).
        bool named = false;
        for (uint32_t s = 1; s <= 400 && !named; ++s) {
            Randomizer r(s);
            PhraseTemplate local; local.name = "sev";
            auto res = select_anchors(local, figs, tl, c, 0.0f, 4.0f, 1.0f, false,
                                      Pitch::from_note_number(77.0f), reg, r,
                                      prof, {}, 0.0f, true);
            if (local.connectors[1]->leadStep == 1) {
                CHECK(res.departures == 1);
                named = res.log.find("[departure] phrase 'sev' bar 1 beat 1 nn 77: "
                                     "tendency V7:7 step_up odds 4.0") != std::string::npos;
            }
        }
        CHECK(named);
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

#include "mforce/core/component.h"
#include <string>

// Checkpoint 2.1: a pin is a self-registering member. Declaration order is
// registry order; the base serves descriptors and by-name access from it.
namespace {
struct PinProbe : mforce::Component {
  mforce::Param freq{this, "frequency", 440.0f, 0.01f, 20000.0f, "hz"};
  mforce::Param amp{this, "amplitude", 1.0f, 0.0f, 10.0f};
  mforce::Input in{this, "source", mforce::Input::Optional};
  mforce::Setting<int> mode{this, "mode", 2, 0, 3};
  mforce::Array table{this, "weights", nullptr, 0.5f, 0.0f, 1.0f};
  const char* type_name() const override { return "PinProbe"; }
};
}

static void run_pin_tests() {
    PinProbe p;
    auto pd = p.param_descriptors();
    CHECK(pd.size() == 2);
    CHECK(std::string(pd[0].name) == "frequency");
    CHECK_NEAR(pd[0].default_value, 440.0f, 1e-6f);
    CHECK(std::string(pd[0].hint) == "hz");
    CHECK(p.input_descriptors().size() == 1);
    CHECK(p.setting_descriptors().size() == 1);
    CHECK(p.array_descriptors().size() == 1);
    CHECK(p.pins().size() == 5);
    // an unwired param reads its constant; a wired one reads the source
    CHECK_NEAR(p.freq.current(), 440.0f, 1e-6f);
    CHECK(p.get_param("frequency") == nullptr);
    p.set_param("frequency", std::make_shared<mforce::ConstantSource>(220.0f));
    p.freq.source->next();
    CHECK_NEAR(p.freq.current(), 220.0f, 1e-6f);
    CHECK(p.get_param("frequency") != nullptr);
    CHECK(p.get_param("nosuch") == nullptr);
    p.clear_param("frequency");
    CHECK_NEAR(p.freq.current(), 440.0f, 1e-6f);
    // settings and arrays by name; the descriptor keeps the declared default
    p.set_setting("mode", 3.0f);
    CHECK(p.mode.value == 3);
    CHECK_NEAR(p.get_setting("mode"), 3.0f, 1e-6f);
    CHECK_NEAR(p.setting_descriptors()[0].default_value, 2.0f, 1e-6f);
    CHECK(p.setting_descriptors()[0].type == mforce::SettingType::Int);
    CHECK_NEAR(p.get_setting("nosuch"), 0.0f, 1e-6f);
    p.set_array("weights", {0.1f, 0.2f});
    CHECK(p.get_array("weights").size() == 2);
    CHECK(p.table.values.size() == 2);
    // an optional input unwired reads 0; an input is reachable by name too
    CHECK_NEAR(p.in.current(), 0.0f, 1e-6f);
    p.set_param("source", std::make_shared<mforce::ConstantSource>(3.0f));
    CHECK(p.get_param("source") != nullptr);
    CHECK(p.in.source != nullptr);
}

// Checkpoint 2.1: Generator and Processor under ValueSource. next() advances
// every pin in declaration order, then compute() does the node's work.
namespace {
struct Doubler final : mforce::Processor {
  float compute() override { return 2.0f * input.current(); }
  const char* type_name() const override { return "Doubler"; }
};
struct UnitRamp final : mforce::Generator {
  float v_{0.0f};
  float compute() override { return v_ += 1.0f; }
  Range range() const override { return Range::Unipolar; }
  const char* type_name() const override { return "UnitRamp"; }
};
}

static void run_base_tests() {
    auto ramp = std::make_shared<UnitRamp>();
    Doubler d;
    d.set_param("source", ramp);              // Processor's input is the pin named "source"
    CHECK(d.input_descriptors().size() == 1);
    CHECK(d.category() == mforce::SourceCategory::Filter);
    mforce::RenderContext ctx{48000};
    d.prepare(ctx); d.start(100);
    CHECK_NEAR(d.next(), 2.0f, 1e-6f);        // the input advanced (ramp -> 1), then 2 * 1
    CHECK_NEAR(d.next(), 4.0f, 1e-6f);
    CHECK_NEAR(d.current(), 4.0f, 1e-6f);
    CHECK(ramp->range() == mforce::Generator::Range::Unipolar);
    // a constant is a Generator that computes its value
    mforce::ConstantSource c(0.25f);
    CHECK_NEAR(c.current(), 0.25f, 1e-6f);
    c.set(0.5f);
    CHECK_NEAR(c.next(), 0.5f, 1e-6f);
}

#include "mforce/source/sine_source.h"
#include "mforce/source/fm_source.h"

// A Manual pin is driven by its node: next() pulls the source and returns
// its value, or the pin's fallback (the constant; 0 for an input) unwired.
static void run_manual_pin_next_tests() {
    PinProbe p;
    CHECK_NEAR(p.freq.next(), 440.0f, 1e-6f);        // unwired: the constant
    CHECK_NEAR(p.in.next(), 0.0f, 1e-6f);            // unwired input: 0
    auto ramp = std::make_shared<UnitRamp>();
    p.set_param("frequency", ramp);
    p.set_param("source", ramp);
    CHECK_NEAR(p.freq.next(), 1.0f, 1e-6f);          // pulls the source once
    CHECK_NEAR(p.in.next(), 2.0f, 1e-6f);            // the same source, pulled again
    CHECK_NEAR(p.freq.current(), 2.0f, 1e-6f);          // current() does not pull
}

// The base does not clamp a setting (min/max are advisory); a node that
// enforces its declared range calls clamp() in on_setting_changed.
static void run_setting_clamp_tests() {
    PinProbe p;
    p.set_setting("mode", 7.0f);
    CHECK(p.mode.value == 7);
    p.mode.clamp();
    CHECK(p.mode.value == 3);
    p.set_setting("mode", -5.0f);
    p.mode.clamp();
    CHECK(p.mode.value == 0);
    p.set_setting("mode", 1.0f);
    p.mode.clamp();
    CHECK(p.mode.value == 1);
    p.set_setting("mode", 2.7f);                  // an int setting truncates
    CHECK(p.mode.value == 2);
}

// A Manual pin is prepared by its node, not by the base: the base's
// prepare skips it, and the node calls the pin's own prepare at the point
// it chooses.
namespace {
struct PrepCounter final : mforce::Generator {
  int starts{0};
  void on_start(int) override { ++starts; }
  float compute() override { return 0.0f; }
  const char* type_name() const override { return "PrepCounter"; }
};
struct ManualHolder final : mforce::Generator {
  mforce::Param gate{this, "gate", 0.0f, 0.0f, 1.0f, nullptr, mforce::Pull::Manual};
  mforce::Param gain{this, "gain", 1.0f, 0.0f, 1.0f};
  float compute() override { return gain.current(); }
  const char* type_name() const override { return "ManualHolder"; }
};
}

static void run_manual_pin_prepare_tests() {
    auto a = std::make_shared<PrepCounter>();
    auto b = std::make_shared<PrepCounter>();
    ManualHolder h;
    h.set_param("gate", a);
    h.set_param("gain", b);
    mforce::RenderContext ctx{48000};
    h.prepare(ctx); h.start(8);
    CHECK(a->starts == 0);                      // Manual: the base leaves it alone
    CHECK(b->starts == 1);                      // Auto: the base prepared it
    h.gate.prepare(ctx); h.gate.start(8);                       // the node prepares it where it chooses
    CHECK(a->starts == 1);
}

// A Processor may own its primary input: Processor(Pull::Manual) keeps
// `source` out of the sweep so the node pulls it where the old code did.
namespace {
struct ManualDoubler final : mforce::Processor {
  ManualDoubler() : Processor(mforce::Pull::Manual) {}
  float compute() override { return 2.0f * input.next(); }
  const char* type_name() const override { return "ManualDoubler"; }
};
}

namespace {
struct ManualIdle final : mforce::Processor {  // owns its input and never pulls it
  ManualIdle() : Processor(mforce::Pull::Manual) {}
  float compute() override { return 0.0f; }
  const char* type_name() const override { return "ManualIdle"; }
};
}

static void run_processor_manual_input_tests() {
    auto ramp = std::make_shared<UnitRamp>();
    ManualDoubler d;
    d.set_param("source", ramp);
    CHECK(d.input_descriptors().size() == 1);  // the pin is still declared
    mforce::RenderContext ctx{48000};
    d.prepare(ctx); d.start(100);
    CHECK_NEAR(d.next(), 2.0f, 1e-6f);         // one pull per sample, by the node
    CHECK_NEAR(d.next(), 4.0f, 1e-6f);
    ManualIdle idle;
    idle.set_param("source", ramp);
    idle.next();
    CHECK_NEAR(ramp->current(), 2.0f, 1e-6f);  // the base advanced nothing
    Doubler a;
    a.set_param("source", ramp);
    a.next();
    CHECK_NEAR(ramp->current(), 3.0f, 1e-6f);  // the default stays Auto
}

// Checkpoint 2.1: WaveSource under Generator with its three pins declared
// once, in the order the old next() advanced them (amplitude, frequency,
// phase); an unwired pin reads its constant and has no source.
static void run_wave_source_tests() {
    SineSource s;
    auto pd = s.param_descriptors();
    CHECK(pd.size() == 3);
    CHECK(std::string(pd[0].name) == "amplitude");
    CHECK(std::string(pd[1].name) == "frequency");
    CHECK(std::string(pd[2].name) == "phase");
    CHECK(s.get_param("phase") == nullptr);
    CHECK(s.range() == mforce::Generator::Range::Bipolar);
    CHECK(s.category() == mforce::SourceCategory::Oscillator);
    s.set_param("frequency", std::make_shared<ConstantSource>(12000.0f));
    s.prepare(RenderContext{48000}); s.start(8);
    CHECK_NEAR(s.next(), 0.0f, 1e-6f);           // pos 0 -> sin(0)
    CHECK_NEAR(s.next(), 1.0f, 1e-6f);           // a quarter cycle later
    CHECK(s.get_param("frequency") != nullptr);
}

// The per-tick memo: one compute per tick however many pulls, in whatever
// order; step() advances now; no clock means every pull computes; a cycle
// with no tap is finite.
namespace {
struct TestHost { SampleClock clock; RenderContext ctx{48000, &clock}; };
struct Counter final : Generator {
    int computes{0};
    float compute() override { return float(++computes); }
    const char* type_name() const override { return "Counter"; }
};
}

static void run_memo_tests() {
    TestHost h;
    auto c = std::make_shared<Counter>();
    auto sum = std::make_shared<CombinedSource>(c, c, CombineOp::Sum, 0.0f);   // one node on both pins, no wrapper
    sum->prepare(h.ctx); sum->start(16);
    h.clock.tick();
    CHECK_NEAR(sum->next(), 2.0f, 1e-6f);     // computed once, served once
    CHECK(c->computes == 1);
    CHECK_NEAR(c->next(), 1.0f, 1e-6f);       // a third pull this tick: the memo
    CHECK(c->computes == 1);
    h.clock.tick();
    CHECK_NEAR(sum->next(), 4.0f, 1e-6f);
    CHECK(c->computes == 2);
    h.clock.tick();                           // order independence: the leaf first
    CHECK_NEAR(c->next(), 3.0f, 1e-6f);
    CHECK_NEAR(sum->next(), 6.0f, 1e-6f);
    CHECK(c->computes == 3);
    h.clock.tick();                           // step(): one sample now, whatever the tick
    CHECK_NEAR(c->step(), 4.0f, 1e-6f);
    CHECK_NEAR(c->step(), 5.0f, 1e-6f);
    CHECK_NEAR(c->next(), 5.0f, 1e-6f);       // the memo holds the last step
    CHECK(c->computes == 5);
    h.clock.tick();                           // step() is recursive through Auto pins
    auto chain = std::make_shared<CombinedSource>(c, std::make_shared<ConstantSource>(0.0f),
                                                  CombineOp::Sum, 0.0f);
    chain->prepare(h.ctx); chain->start(16);
    CHECK_NEAR(chain->step(), 6.0f, 1e-6f);
    CHECK_NEAR(chain->step(), 7.0f, 1e-6f);
    CHECK(c->computes == 7);
    CHECK_NEAR(sum->step(), 18.0f, 1e-6f);    // a node on two pins of a stepped parent steps twice, and
                                              // both pins read it after the sweep: 9 + 9
    CHECK(c->computes == 9);
    auto bare = std::make_shared<Counter>();  // no clock: every pull computes
    bare->prepare(RenderContext{48000}); bare->start(16);
    bare->next(); bare->next(); bare->next();
    CHECK(bare->computes == 3);
    auto loop = std::make_shared<CombinedSource>(std::make_shared<ConstantSource>(1.0f),
                                                 std::make_shared<ConstantSource>(0.0f),
                                                 CombineOp::Sum, 0.0f);
    loop->prepare(h.ctx); loop->start(16);
    loop->set_param("source2", loop);         // a cycle with no tap, wired after the host calls
                                              // (which walk the pins): illegal, must be finite
    h.clock.tick();
    CHECK_NEAR(loop->next(), 1.0f, 1e-6f);    // 1 + the previous value (0)
    h.clock.tick();
    CHECK_NEAR(loop->next(), 2.0f, 1e-6f);    // 1 + 1
    loop->clear_param("source2");             // break the ownership cycle
}

#include "mforce/core/envelope_presets.h"

// The sweep follows every door that changes a pin's source: a source wired
// after prepare and start is pulled on the next tick, and one cleared is
// not.
static void run_sweep_follows_wiring_tests() {
    TestHost h;
    auto c = std::make_shared<Counter>();
    auto p = std::make_shared<CombinedSource>(std::make_shared<ConstantSource>(1.0f),
                                              std::make_shared<ConstantSource>(0.0f),
                                              CombineOp::Sum, 0.0f);
    p->prepare(h.ctx); p->start(16);
    p->set_param("source2", c);               // wired after the host calls
    h.clock.tick();
    CHECK_NEAR(p->next(), 2.0f, 1e-6f);       // 1 + the counter's first value
    CHECK(c->computes == 1);
    p->set_param("source2", nullptr);         // cleared: back to the pin's fallback (0)
    h.clock.tick();
    CHECK_NEAR(p->next(), 1.0f, 1e-6f);
    CHECK(c->computes == 1);                  // not pulled any more
    auto d = std::make_shared<Counter>();
    CHECK(p->param("source1") == nullptr);    // no Param of that name: source1 is an Input
    p->set_param("source1", d);
    h.clock.tick();
    CHECK_NEAR(p->next(), 1.0f, 1e-6f);       // d's first value + 0
    CHECK(d->computes == 1);

    // A node assigning itself (a preset envelope rebuilding its stages) keeps
    // its wires in the sweep: the range source is still pulled afterwards.
    auto r = std::make_shared<Counter>();
    AREnvelope env;
    env.set_param("maxValue", r);
    env.prepare(h.ctx); env.start(4800);
    for (int i = 0; i < 3; ++i) { h.clock.tick(); env.next(); }
    CHECK(r->computes == 3);
    env.set_setting("attack", 0.3f);          // rebuild(): the stages are replaced, the wire must stay swept
    for (int i = 0; i < 3; ++i) { h.clock.tick(); env.next(); }
    CHECK(r->computes == 6);
    CHECK(env.get_param("maxValue").get() == r.get());
}

// The tap edge reads its source's memo live: a Setup-time read between
// ticks sees the source's present value without the tap being pulled.
static void run_tap_reads_live_tests() {
    TestHost h;
    auto c = std::make_shared<Counter>();
    auto tap = std::make_shared<TapSource>(c);
    c->prepare(h.ctx); c->start(16);
    tap->prepare(h.ctx); tap->start(16);
    h.clock.tick(); c->next();
    CHECK_NEAR(tap->current(), 1.0f, 1e-6f);  // never pulled, reads live
    h.clock.tick(); c->next();
    CHECK_NEAR(tap->current(), 2.0f, 1e-6f);
    CHECK_NEAR(tap->next(), 2.0f, 1e-6f);     // pulled: this tick's read of the memo
    h.clock.tick();
    CHECK_NEAR(tap->next(), 2.0f, 1e-6f);     // the source not yet computed this tick: last tick's value (reading A)
    c->next();
    CHECK_NEAR(tap->current(), 3.0f, 1e-6f);  // live again
}

// A source wired to two pins is the same object on both pins: no wrapper, no
// advancer, no wiring order. A consumer the file lists before the advancer
// used to read a wrapper a sample late; it reads this sample's value now, so
// the shared patch renders exactly as the same patch with the envelope
// duplicated, however the file orders the consumers.
static std::string shared_env_patch_json(bool duplicated) {
    using nlohmann::json;
    const json stages = json::array({
        json{{"startVal", 0.0}, {"endVal", 1.0}, {"type", "Linear"}, {"percent", 0.05}},
        json{{"startVal", 1.0}, {"endVal", 0.7}, {"type", "Linear"}, {"percent", 0.10}},
        json{{"startVal", 0.7}, {"endVal", 0.7}, {"type", "Linear"}, {"percent", 0.0}},
        json{{"startVal", 0.7}, {"endVal", 0.0}, {"type", "Linear"}, {"percent", 0.25}}});
    auto env = [&](const char* id) {
        return json{{"id", id}, {"type", "Envelope"}, {"params", {{"stages", stages}}}};
    };
    auto sine = [](const char* id, const char* envId) {
        return json{{"id", id}, {"type", "SineSource"},
                    {"params", {{"frequency", 220.0}, {"amplitude", {{"ref", envId}}}, {"phase", 0.0}}}};
    };
    json nodes = json::array();
    nodes.push_back(env("env"));
    if (duplicated) nodes.push_back(env("env2"));
    nodes.push_back(sine("sine2", "env"));                            // listed first: the file's advancer
    nodes.push_back(sine("sine1", duplicated ? "env2" : "env"));      // pulled first by the mix
    nodes.push_back(json{{"id", "mix"}, {"type", "CombinedSource"},
                         {"params", {{"operation", 3}, {"gainAdj", 0.0},
                                     {"source1", {{"ref", "sine1"}}}, {"source2", {{"ref", "sine2"}}}}}});
    const json root = {{"sampleRate", 48000}, {"instrument", {{"polyphony", 1}}},
                       {"graph", {{"output", "mix"}, {"nodes", nodes}}}};
    return root.dump();
}

static void run_shared_source_tests() {
    auto render_all = [](InstrumentPatch& p, int n) {
        std::vector<float> b(size_t(n), 0.0f);
        p.instrument->render(b.data(), n);
        return b;
    };
    auto rms_of = [](const std::vector<float>& b, float fromSec, float toSec) {
        int a = int(fromSec * 48000), z = int(toSec * 48000);
        a = std::max(0, a); z = std::min(z, int(b.size()));
        double s = 0.0;
        for (int i = a; i < z; ++i) s += double(b[size_t(i)]) * b[size_t(i)];
        return float(std::sqrt(s / double(std::max(1, z - a))));
    };
    auto ip = load_instrument_patch_json(shared_env_patch_json(false));
    auto* pi = dynamic_cast<PitchedInstrument*>(ip.instrument.get());
    CHECK(pi != nullptr && !pi->voicePool.empty());
    auto& vg = pi->voicePool[0];
    auto env = vg.nodesById.at("env");
    CHECK(vg.nodesById.at("sine2")->get_param("amplitude").get() == env.get());
    CHECK(vg.nodesById.at("sine1")->get_param("amplitude").get() == env.get());   // the second consumer holds the node itself
    const int N = 24000;
    pi->play_note({60.0f, 0.8f, 0.25f, 0.0f, false}, 0.0f);
    auto shared = render_all(ip, N);
    auto dupIp = load_instrument_patch_json(shared_env_patch_json(true));
    auto* dp = dynamic_cast<PitchedInstrument*>(dupIp.instrument.get());
    dp->play_note({60.0f, 0.8f, 0.25f, 0.0f, false}, 0.0f);
    auto dup = render_all(dupIp, N);
    CHECK(shared.size() == dup.size() &&
          std::memcmp(shared.data(), dup.data(), shared.size() * sizeof(float)) == 0);
    CHECK(rms_of(shared, 0.05f, 0.20f) > 0.01f);                                   // and it sounds
}

// Checkpoint 2.1: the core family (envelopes, range/var, multi, name gate)
// under Generator/Processor with its pins declared once.
#include "mforce/core/envelope_presets.h"
#include "mforce/core/multi_source.h"
#include "mforce/core/range_source.h"
#include "mforce/core/var_source.h"

namespace {
// Counts how often a pin's owner prepared and advanced it.
struct PinCounter final : mforce::Generator {
  int starts{0}, nexts{0}, rate{0}, ctxPrepares{0};
  float level{1.0f};
  void on_prepare(const mforce::RenderContext& c) override { rate = c.sampleRate; ++ctxPrepares; }
  void on_start(int) override { ++starts; }
  float compute() override { ++nexts; return level; }
  const char* type_name() const override { return "PinCounter"; }
};

bool has_name(std::span<const SettingDescriptor> d, const char* n) {
  for (const auto& x : d) if (std::string(x.name) == n) return true;
  return false;
}
}

static void run_range_var_tests() {
    RenderContext ctx{48000};

    // RangeSource: the declared default of `normalized` is false, whichever
    // constructor builds it; the 4-argument form sets it explicitly.
    {
        auto zero = std::make_shared<ConstantSource>(0.0f);
        RangeSource a(std::make_shared<ConstantSource>(10.0f),
                      std::make_shared<ConstantSource>(20.0f), zero);
        CHECK(a.normalized.value == false);
        CHECK(!a.setting_descriptors().empty());
        CHECK_NEAR(a.setting_descriptors()[0].default_value, 0.0f, 1e-6f);
        a.prepare(ctx); a.start(16);
        CHECK_NEAR(a.next(), 15.0f, 1e-5f);        // var 0 remapped (0+1)/2
        RangeSource b(std::make_shared<ConstantSource>(10.0f),
                      std::make_shared<ConstantSource>(20.0f), zero, true);
        b.prepare(ctx); b.start(16);
        CHECK_NEAR(b.next(), 10.0f, 1e-5f);        // already normalized
        CHECK(a.param_descriptors().size() == 3);
        CHECK(std::string(a.param_descriptors()[2].name) == "var");
        CHECK(a.get_param("max") != nullptr);
        for (int i = 0; i < 10; ++i) CHECK(std::isfinite(a.next()));
    }

    // VarSource: absolute and relative modes; wired through the 3-argument
    // constructor, default absolute.
    {
        VarSource v(std::make_shared<ConstantSource>(2.0f),
                    std::make_shared<ConstantSource>(0.5f),
                    std::make_shared<ConstantSource>(0.5f));
        CHECK(v.absolute.value == true);
        v.prepare(ctx); v.start(16);
        CHECK_NEAR(v.next(), 2.5f, 1e-5f);         // 2 * (1 + 0.5 * 0.5)
        v.set_setting("absolute", 0.0f);
        CHECK_NEAR(v.next(), 2.5f + (2.0f - 2.5f) * 1.25f, 1e-5f);
        for (int i = 0; i < 10; ++i) CHECK(std::isfinite(v.next()));
        CHECK(v.param_descriptors().size() == 3);
    }
}

static void run_multi_name_gate_tests() {
    RenderContext ctx{48000};

    // MultiSource: `source` is a list served by name; weights and delays
    // apply; clear_param empties it.
    {
        MultiSource m;
        m.set_param("source", std::make_shared<ConstantSource>(1.0f));
        m.add_param("source", std::make_shared<ConstantSource>(2.0f));
        CHECK(m.entries.size() == 2);
        CHECK(m.input_descriptors().size() == 1);
        CHECK(m.input_descriptors()[0].multi);
        CHECK(m.category() == SourceCategory::Combiner);
        CHECK(m.pins().empty());                   // the list is not a registry pin
        CHECK(m.get_param("source") == nullptr);
        m.entries[1].weight = 0.5f;
        m.entries[1].delaySamples = 1;
        m.prepare(ctx); m.start(16);
        CHECK_NEAR(m.next(), 1.0f, 1e-6f);         // the delayed entry is not yet in
        CHECK_NEAR(m.next(), 2.0f, 1e-6f);         // 1 + 2 * 0.5
        m.clear_param("source");
        CHECK(m.entries.empty());
    }

    // NameGate: `in` is an optional signal input, advanced once per sample.
    {
        NameGate ng;
        CHECK(ng.param_descriptors().empty());
        CHECK(ng.input_descriptors().size() == 1);
        CHECK(std::string(ng.input_descriptors()[0].name) == "in");
        ng.targetId = 0.0f;
        CHECK(ng.next() == 0.0f);                  // unwired never matches, even id 0
        auto c = std::make_shared<PinCounter>();
        c->level = 3.0f;
        ng.set_param("in", c);
        ng.targetId = 3.0f;
        ng.prepare(ctx); ng.start(16);
        CHECK(c->starts == 1);
        CHECK(ng.next() == 1.0f);
        CHECK(ng.next() == 1.0f);
        CHECK(c->nexts == 2);
    }
}

static void run_envelope_pin_tests() {
    RenderContext ctx{48000};

    // Envelope: minValue/maxValue are pulled and prepared by the base;
    // `trigger` is Manual, so it is never prepared or advanced.
    {
        auto env = std::make_shared<Envelope>();
        env->add_stage({{0.0f, 1.0f, RampType::Linear, 0.0f}, 0.5f, 0.0f, 0.0f});
        env->add_stage({{1.0f, 1.0f, RampType::Linear, 0.0f}, 0.5f, 0.0f, 0.0f});
        auto lo = std::make_shared<PinCounter>();
        auto hi = std::make_shared<PinCounter>();
        auto trig = std::make_shared<PinCounter>();
        env->set_param("minValue", lo);
        env->set_param("maxValue", hi);
        env->set_param("trigger", trig);
        env->prepare(ctx); env->start(4800);
        for (int i = 0; i < 5; ++i) env->next();
        CHECK(lo->starts == 1 && hi->starts == 1 && trig->starts == 0);
        CHECK(lo->nexts == 5 && hi->nexts == 5 && trig->nexts == 0);
        CHECK(env->trigger.source == trig);
        CHECK(env->range() == Generator::Range::Unipolar);
        CHECK(env->param_descriptors().size() == 3);
        CHECK(std::string(env->param_descriptors()[2].name) == "trigger");
    }

    // Envelope settings: the knobs clamp; sustainLevel is a live view of the
    // four-slot ADSR shape and reads 0 on any other layout.
    {
        auto adsr = Envelope::make_adsr(0.1f, 0.1f, 0.4f, 0.2f);
        CHECK_NEAR(adsr->get_setting("sustainLevel"), 0.4f, 1e-6f);
        adsr->set_setting("sustainLevel", 0.2f);
        CHECK_NEAR(adsr->get_setting("sustainLevel"), 0.2f, 1e-6f);
        CHECK_NEAR(adsr->stage(3).ramp.startVal, 0.2f, 1e-6f);
        adsr->set_setting("stage_accuracy", 7.0f);
        CHECK_NEAR(adsr->get_setting("stage_accuracy"), 1.0f, 1e-6f);
        adsr->set_setting("timeScale", 0.0f);
        CHECK_NEAR(adsr->get_setting("timeScale"), 0.01f, 1e-6f);
        auto ar = Envelope::make_ar(0.2f);
        ar->set_setting("sustainLevel", 0.9f);
        CHECK_NEAR(ar->get_setting("sustainLevel"), 0.0f, 1e-6f);
        CHECK_NEAR(ar->stage(1).ramp.startVal, 1.0f, 1e-6f);   // not rewritten
    }
}

static void run_envelope_preset_tests() {
    RenderContext ctx{48000};

    // The six presets: each declares its own settings after the base's,
    // renders finite values, and keeps the range params and the stored
    // sustain level across a rebuild.
    {
        auto smoke = [&](Envelope& e) {
            e.prepare(ctx); e.start(4800);
            for (int i = 0; i < 10; ++i) CHECK(std::isfinite(e.next()));
        };
        AREnvelope ar;    smoke(ar);
        ASEnvelope as;    smoke(as);
        ASREnvelope asr;  smoke(asr);
        ADSEnvelope ads;  smoke(ads);
        ADREnvelope adr;  smoke(adr);
        ADSREnvelope adsr; smoke(adsr);
        CHECK(has_name(ar.setting_descriptors(), "releaseCurve"));
        CHECK(has_name(adr.setting_descriptors(), "decayLevel"));
        CHECK(has_name(as.setting_descriptors(), "reverse"));
        CHECK(!has_name(ar.setting_descriptors(), "attackLevel"));

        // the declared default of sustainLevel is each preset's own
        auto sustainDefault = [](Envelope& e) {
            for (const auto& d : e.setting_descriptors())
                if (std::string(d.name) == "sustainLevel") return d.default_value;
            return -1.0f;
        };
        CHECK_NEAR(sustainDefault(as), 1.0f, 1e-6f);
        CHECK_NEAR(sustainDefault(asr), 1.0f, 1e-6f);
        CHECK_NEAR(sustainDefault(ads), 0.0f, 1e-6f);
        CHECK_NEAR(sustainDefault(adsr), 0.7f, 1e-6f);
        CHECK_NEAR(as.get_setting("sustainLevel"), 1.0f, 1e-6f);
        CHECK_NEAR(ads.get_setting("sustainLevel"), 0.0f, 1e-6f);

        // a rebuild keeps wired range params, the accuracy knobs and the level
        adsr.set_param("maxValue", std::make_shared<ConstantSource>(5.0f));
        adsr.set_setting("stage_accuracy", 0.5f);
        adsr.set_setting("sustainLevel", 0.3f);
        adsr.set_setting("release", 0.25f);        // triggers a rebuild
        CHECK(adsr.get_param("maxValue") != nullptr);
        CHECK_NEAR(adsr.get_setting("stage_accuracy"), 0.5f, 1e-6f);
        CHECK_NEAR(adsr.get_setting("sustainLevel"), 0.3f, 1e-6f);
        CHECK_NEAR(adsr.get_setting("release"), 0.25f, 1e-6f);
        CHECK_NEAR(adsr.stage(1).ramp.endVal, 0.3f, 1e-6f);
        CHECK_NEAR(asr.get_setting("sustainLevel"), 1.0f, 1e-6f);
        asr.set_setting("sustainLevel", 0.6f);     // a preset's level rebuilds its layout
        CHECK_NEAR(asr.get_setting("sustainLevel"), 0.6f, 1e-6f);
        CHECK_NEAR(asr.stage(0).ramp.endVal, 0.6f, 1e-6f);
    }
}

// A preset rebuilds by assigning a freshly laid-out envelope over itself
// (replace_stages). The pin registry belongs to the object, not to the
// assignment, so the pins stay the same and a wired one is still prepared
// and advanced after the rebuilds.
static void run_envelope_rebuild_pin_tests() {
    RenderContext ctx{48000};
    ADSREnvelope adsr;
    const std::vector<PinBase*> pinsBefore = adsr.pins();
    auto lo = std::make_shared<PinCounter>();
    auto hi = std::make_shared<PinCounter>();
    adsr.set_param("minValue", lo);
    adsr.set_param("maxValue", hi);
    adsr.set_setting("release", 0.25f);          // each of these rebuilds the layout
    adsr.set_setting("attack", 0.05f);
    adsr.set_setting("decayCurve", 1.0f);
    CHECK(adsr.pins() == pinsBefore);
    CHECK(adsr.get_param("minValue") == lo && adsr.get_param("maxValue") == hi);

    adsr.prepare(ctx); adsr.start(4800);
    const int n = 25;
    for (int i = 0; i < n; ++i) adsr.next();
    CHECK(lo->starts == 1 && hi->starts == 1);
    CHECK(lo->nexts == n && hi->nexts == n);
}

// Checkpoint 2.1: the noise family with its pins declared once.
#include "mforce/source/layered_red_noise_source.h"
#include "mforce/source/noise_sources.h"
#include "mforce/source/pink_noise_source.h"
#include "mforce/source/red_noise_source.h"
#include "mforce/source/wander_noise_source.h"
#include "mforce/source/white_noise_source.h"

namespace {
std::string param_names(const mforce::Component& c) {
    std::string out;
    for (const auto& d : c.param_descriptors()) { out += d.name; out += ','; }
    return out;
}
float param_default(const mforce::Component& c, const char* name) {
    for (const auto& d : c.param_descriptors())
        if (std::string(d.name) == name) return d.default_value;
    return -999.0f;
}
}

// Every type prepares and renders finite values, and says which pins it
// declares, in the order they are advanced.
static void run_noise_pin_tests() {
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
    CHECK(white.category() == SourceCategory::Generator);
    CHECK(red.category() == SourceCategory::Generator);
}

// WhiteNoiseSource: at the inert defaults one draw per sample, exactly
// the historical sequence; a wired pin is prepared and advanced once
// per sample.
static void run_noise_white_tests() {
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
static void run_noise_default_tests() {
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
static void run_noise_red_pin_tests() {
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
static void run_noise_manual_amplitude_tests() {
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
static void run_noise_layered_tests() {
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

// Checkpoint 2.1: the wavetable, the hybrid KS and the evolution holders.
#include "mforce/source/hybrid_ks_source.h"
#include "mforce/source/wavetable_source.h"

namespace {
std::string input_names(const mforce::Component& c) {
    std::string out;
    for (const auto& d : c.input_descriptors()) { out += d.name; out += ','; }
    return out;
}
std::string setting_names(const mforce::Component& c) {
    std::string out;
    for (const auto& d : c.setting_descriptors()) { out += d.name; out += ','; }
    return out;
}
// Logs the order in which its consumers advance it, and counts prepares.
struct OrderProbe final : mforce::Generator {
    OrderProbe(std::vector<int>* l, int i) : log(l), id(i) {}
    std::vector<int>* log;
    int id;
    int starts{0};
    void on_start(int) override { ++starts; }
    float compute() override { log->push_back(id); return 1.0f; }
    const char* type_name() const override { return "OrderProbe"; }
};
}

// WavetableSource: WaveSource's three pins, then speedFactor, inputSource and
// evolution, which the node advances itself (Manual): inputSource while a
// note's table is filled, speedFactor only in the interpolating read.
static void run_wavetable_pin_tests() {
    RenderContext ctx{48000};

    WavetableSource w;
    CHECK(param_names(w) == "amplitude,frequency,phase,speedFactor,");
    CHECK(input_names(w) == "inputSource,evolution,");
    CHECK(setting_names(w) == "interpolate,");
    CHECK(w.setting_descriptors()[0].type == SettingType::Bool);
    CHECK(w.range() == Generator::Range::Bipolar);
    CHECK(w.category() == SourceCategory::Oscillator);
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

// The evolution input holds the node and keeps a typed view of the evolution
// it carries; the node prepares the holder and evolves once per sample.
static void run_wavetable_evolution_tests() {
    RenderContext ctx{48000};

    WavetableSource w;
    auto reed = std::make_shared<ReedEvolutionSource>();
    auto breath = std::make_shared<PinCounter>();
    reed->set_param("breath", breath);
    w.set_param("evolution", reed);
    CHECK(w.get_param("evolution") == reed);
    CHECK(w.evolution.source == reed);
    w.prepare(ctx); w.start(4800);
    CHECK(breath->starts == 1);                          // wavetable -> holder -> pin
    for (int i = 0; i < 10; ++i) CHECK(std::isfinite(w.next()));
    CHECK(breath->nexts == 10);
    w.clear_param("evolution");                            // no evolution, no more pulls
    CHECK(w.get_param("evolution") == nullptr);
    for (int i = 0; i < 10; ++i) w.next();
    CHECK(breath->nexts == 10);

    WavetableSource n;  // wired, but not a holder
    n.set_param("evolution", std::make_shared<ConstantSource>(0.5f));
    CHECK(n.get_param("evolution") != nullptr);
    n.prepare(ctx); n.start(4800);
    for (int i = 0; i < 10; ++i) CHECK(std::isfinite(n.next()));

    WavetableSource o;  // the loader's direct-ownership path
    o.set_evolution(std::make_unique<PluckEvolution>(0.0f, 1u));
    o.prepare(ctx); o.start(4800);
    float peak = 0.0f;
    for (int i = 0; i < 2000; ++i) peak = std::max(peak, std::fabs(o.next()));
    CHECK(peak > 0.0f);
}

// HybridKSSource: WaveSource's three pins, the seed waveform (Manual: pulled
// while a note's table is filled) and three settings.
static void run_hybrid_ks_tests() {
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

// All twelve holders: the settings (and pins) each declares, in order, and
// the declared default is what an untouched holder reports.
static void run_evolution_holder_shape_tests() {
    PluckEvolutionSource pluck;
    AveragingEvolutionSource avg;
    EKSEvolutionSource eks;
    ReedEvolutionSource reed;
    BowedStringEvolutionSource bowed;
    BrassEvolutionSource brass;
    ReactionDiffusionEvolutionSource rd;
    SortErosionEvolutionSource sort;
    CellularAutomatonEvolutionSource ca;
    HistogramEqualizeEvolutionSource he;
    BezierPullEvolutionSource bez;
    BitRotateEvolutionSource rot;

    CHECK(setting_names(pluck) == "muting,");
    CHECK(setting_names(avg) == "sampleCount,speed,decayFactor,leading,autoAdjust,");
    CHECK(setting_names(eks) == "pickPosition,pickDirection,stiffness,decayStretch,drumBlend,");
    CHECK(setting_names(reed) == "tubeLoss,loopFilter,");
    CHECK(setting_names(bowed) == "tubeLoss,bowSpeed,bowPosition,brightness,");
    CHECK(setting_names(brass) == "tubeLoss,lipTension,lipFreqRatio,lipQ,");
    CHECK(setting_names(rd) == "feed,kill,diffU,diffV,dt,subSteps,");
    CHECK(setting_names(sort) == "swapsPerSample,descending,");
    CHECK(setting_names(ca) == "rule,threshold,levelHi,levelLo,stepEvery,");
    CHECK(setting_names(he) == "bins,rate,");
    CHECK(setting_names(bez) == "p0,p1,p2,p3,rate,");
    CHECK(setting_names(rot).empty());

    CHECK(param_names(reed) == "reedStiffness," && input_names(reed) == "breath,");
    CHECK(param_names(bowed) == "frictionGain," && input_names(bowed) == "bow,");
    CHECK(param_names(brass) == "brassiness," && input_names(brass) == "breath,");
    CHECK(param_names(rot) == "shiftBits,stepEvery," && input_names(rot).empty());
    CHECK_NEAR(param_default(reed, "reedStiffness"), 1.5f, 1e-6f);
    CHECK_NEAR(param_default(bowed, "frictionGain"), 4.0f, 1e-6f);
    CHECK_NEAR(param_default(brass, "brassiness"), 1.5f, 1e-6f);
    CHECK_NEAR(param_default(rot, "shiftBits"), 3.0f, 1e-6f);
    CHECK_NEAR(param_default(rot, "stepEvery"), 4.0f, 1e-6f);
    CHECK(param_names(pluck).empty() && input_names(pluck).empty());

    const mforce::Component* all[] = {&pluck, &avg, &eks, &reed, &bowed, &brass, &rd, &sort, &ca, &he, &bez, &rot};
    for (const auto* c : all)
        for (const auto& d : c->setting_descriptors())
            CHECK_NEAR(c->get_setting(d.name), d.default_value, 1e-9f);
    CHECK(reed.category() == SourceCategory::Combiner);
    CHECK(pluck.category() == SourceCategory::Generator);
    CHECK(avg.get_evolution() != nullptr);
}

// The settings the holders clamped when they were set keep clamping, the
// evolution reads the holder's live value, and a rebuilt evolution still
// reports the setting that rebuilt it.
static void run_evolution_holder_setting_tests() {
    ReactionDiffusionEvolutionSource rd;
    rd.set_setting("subSteps", 0.0f);
    CHECK_NEAR(rd.get_setting("subSteps"), 1.0f, 1e-6f);
    rd.set_setting("subSteps", 3.0f);
    CHECK_NEAR(rd.get_setting("subSteps"), 3.0f, 1e-6f);

    SortErosionEvolutionSource sort;
    sort.set_setting("swapsPerSample", -4.0f);
    CHECK_NEAR(sort.get_setting("swapsPerSample"), 1.0f, 1e-6f);
    sort.set_setting("descending", 1.0f);
    CHECK_NEAR(sort.get_setting("descending"), 1.0f, 1e-6f);

    HistogramEqualizeEvolutionSource he;
    he.set_setting("bins", 1000.0f);
    CHECK_NEAR(he.get_setting("bins"), 256.0f, 1e-6f);
    he.set_setting("bins", 2.0f);
    CHECK_NEAR(he.get_setting("bins"), 4.0f, 1e-6f);

    CellularAutomatonEvolutionSource ca;
    ca.set_setting("rule", 999.0f);
    CHECK_NEAR(ca.get_setting("rule"), 255.0f, 1e-6f);
    ca.set_setting("rule", -5.0f);
    CHECK_NEAR(ca.get_setting("rule"), 0.0f, 1e-6f);
    ca.set_setting("stepEvery", 0.0f);
    CHECK_NEAR(ca.get_setting("stepEvery"), 1.0f, 1e-6f);

    ca.set_setting("stepEvery", 8.0f);                     // the evolution reads the live level
    std::vector<float> table(8, 0.5f);
    WaveEvolution* evo = ca.get_evolution();
    evo->shape_excitation(table);
    evo->evolve(table, 0);
    CHECK_NEAR(table[0], 0.8f, 1e-6f);
    ca.set_setting("levelHi", 0.3f);
    evo->evolve(table, 0);
    CHECK_NEAR(table[0], 0.3f, 1e-6f);

    PluckEvolutionSource pluck;
    pluck.set_setting("muting", 0.5f);
    CHECK_NEAR(pluck.get_setting("muting"), 0.5f, 1e-6f);
    AveragingEvolutionSource avg;
    avg.set_setting("leading", 1.0f);
    avg.set_setting("speed", 2.5f);
    CHECK_NEAR(avg.get_setting("leading"), 1.0f, 1e-6f);
    CHECK_NEAR(avg.get_setting("speed"), 2.5f, 1e-6f);
    CHECK_NEAR(avg.get_setting("decayFactor"), 0.999f, 1e-6f);
}

// The four holders with wired pins (reed, bowed, brass, bit rotate): the pins
// are Manual. The wavetable prepares them once, the evolution advances each
// once per sample in the order the old evolve() did, and the holder's own
// next() advances nothing.
static void run_evolution_manual_pin_tests() {
    RenderContext ctx{48000};

    auto run = [&](std::shared_ptr<ValueSource> holder, const char* first, const char* second) {
        std::vector<int> log;
        auto a = std::make_shared<OrderProbe>(&log, 1);
        auto b = std::make_shared<OrderProbe>(&log, 2);
        holder->set_param(first, a);
        holder->set_param(second, b);
        WavetableSource w;
        w.set_param("evolution", holder);
        w.prepare(ctx); w.start(4800);
        for (int i = 0; i < 4; ++i) CHECK(std::isfinite(w.next()));
        CHECK(a->starts == 1 && b->starts == 1);
        CHECK(log == std::vector<int>({1, 2, 1, 2, 1, 2, 1, 2}));
        for (int i = 0; i < 5; ++i) holder->next();
        CHECK(log.size() == 8);
    };
    run(std::make_shared<ReedEvolutionSource>(), "reedStiffness", "breath");
    run(std::make_shared<BowedStringEvolutionSource>(), "bow", "frictionGain");
    run(std::make_shared<BrassEvolutionSource>(), "brassiness", "breath");
    run(std::make_shared<BitRotateEvolutionSource>(), "stepEvery", "shiftBits");
}

// Checkpoint 2.1: the additive family (sources, partials, formants).
#include "mforce/source/additive/additive_source2.h"
#include "mforce/source/additive/basic_additive_source.h"
#include "mforce/source/additive/formant.h"
#include "mforce/source/additive/full_additive_source.h"
#include "mforce/source/additive/partials.h"

namespace {
std::string array_names(const mforce::Component& c) {
    std::string out;
    for (const auto& d : c.array_descriptors()) { out += d.name; out += ','; }
    return out;
}
// The eight envelope pins of every Partials, in the order partials_next() advances them.
const char* const kPartialsEnvNames[8] = {"multEnv", "amplEnv", "poEnv", "roEnv",
                                          "dtEnv", "bwEnv", "motionEnv", "shimmerEnv"};
}

// The three sources declare the WaveSource trio and then their own pins, in
// the order wave() advanced them; the structural inputs are named but never
// pulled.
static void run_additive_source_pin_tests() {
    AdditiveSource2 a2;
    CHECK(param_names(a2) == "amplitude,frequency,phase,phaseOffset,freqVarDepth,freqVarSpeed,"
                             "amplVarDepth,amplVarSpeed,");
    CHECK(input_names(a2) == "partials,");
    CHECK(setting_names(a2) == "partialCount,");
    CHECK(a2.setting_descriptors()[0].type == SettingType::Int);
    CHECK(a2.range() == Generator::Range::Bipolar);
    CHECK(a2.category() == SourceCategory::Additive);

    BasicAdditiveSource basic;
    CHECK(param_names(basic) == "amplitude,frequency,phase,evenWeight,oddWeight,rolloff,"
                                "freqVarPct,freqVarSpeed,amplVarPct,amplVarSpeed,");
    CHECK(input_names(basic).empty());
    CHECK(setting_names(basic).empty());
    CHECK_NEAR(param_default(basic, "rolloff"), 1.0f, 1e-6f);
    CHECK_NEAR(param_default(basic, "evenWeight"), 1.0f, 1e-6f);

    FullAdditiveSource full;
    CHECK(param_names(full) == "amplitude,frequency,phase,formantWeight,formantFloor,");
    CHECK(input_names(full) == "formant,partials,");
    CHECK(setting_names(full) == "noiseBedLevel,noiseBedFreq,noiseBedWidth,noiseBedDelay,"
                                 "noiseBedFadePow,noiseBedFade,");
    CHECK_NEAR(param_default(full, "formantWeight"), 0.0f, 1e-6f);
    CHECK_NEAR(param_default(full, "formantFloor"), 1.0f, 1e-6f);
    CHECK_NEAR(full.get_setting("noiseBedFreq"), 2900.0f, 1e-3f);
    CHECK_NEAR(full.get_setting("noiseBedFade"), 0.05f, 1e-6f);
    CHECK(std::string(full.type_name()) == "AdditiveSource");
    CHECK(full.get_param("formantFloor") == nullptr);
    CHECK(full.get_param("formant") == nullptr);
}

// The structural types: the pins they declare, in descriptor order.
static void run_additive_structural_pin_tests() {
    Formant f;
    CHECK(param_names(f) == "frequency,gain,width,power,");
    CHECK_NEAR(param_default(f, "frequency"), 1000.0f, 1e-3f);
    CHECK_NEAR(param_default(f, "width"), 500.0f, 1e-3f);
    CHECK_NEAR(param_default(f, "power"), 2.0f, 1e-6f);
    CHECK_NEAR(f.width.current(), 500.0f, 1e-3f);

    BandSpectrum band;
    CHECK(param_names(band) == "startFreq,freqIncrement,");
    CHECK(array_names(band) == "gains,");
    FixedSpectrum fixed({0.0f, 1.0f, 0.0f});
    CHECK(param_names(fixed).empty());
    CHECK(array_names(fixed) == "gains,");
    CHECK(fixed.get_array("gains") == std::vector<float>({0.0f, 1.0f, 0.0f}));
    CHECK_NEAR(fixed.get_gain(1.0f), 1.0f, 1e-6f);
    band.set_array("gains", {0.0f, 1.0f, 0.0f});
    CHECK_NEAR(band.get_gain(1.0f), 1.0f, 1e-6f);
    CHECK(band.contains(1.5f) && !band.contains(2.5f));

    FormantSequence seq;
    CHECK(param_names(seq) == "blend,");
    CHECK(input_names(seq) == "spectra,");
    CHECK(seq.input_descriptors()[0].multi);
    FormantSpectrum spec;
    CHECK(param_names(spec).empty() && input_names(spec).empty());

    CompositePartials comp;
    CHECK(param_names(comp).empty());
    CHECK(input_names(comp) == "partials,");
    CHECK(comp.input_descriptors()[0].multi);

    ExpandRuleNode rule;
    CHECK(param_names(rule) == "spacing1,spacing2,dt1,dt2,loPct1,loPct2,power1,power2,po1,po2,");
    CHECK(setting_names(rule) == "count,recurse,");
    CHECK(rule.to_struct().count == 2 && rule.to_struct().recurse == 0);
    CHECK_NEAR(rule.to_struct().spacing1, 0.5f, 1e-6f);
    CHECK_NEAR(rule.to_struct().dt2, 0.01f, 1e-6f);
    rule.set_setting("count", 4.0f);
    rule.set_param("po2", std::make_shared<ConstantSource>(0.25f));
    CHECK(rule.to_struct().count == 4);
    CHECK_NEAR(rule.to_struct().po2, 0.25f, 1e-6f);
}

// Every Partials declares the same eight envelope pins and the shared
// settings; a subclass adds its own settings after them.
static void run_additive_partials_pin_tests() {
    FullPartials full;
    SequencePartials seq;
    ExplicitPartials expl;
    std::string envs = "multEnv,amplEnv,poEnv,roEnv,dtEnv,bwEnv,motionEnv,shimmerEnv,";
    CHECK(param_names(full) == envs);
    CHECK(param_names(seq) == envs);
    CHECK(param_names(expl) == envs);
    CHECK(input_names(full).empty());
    CHECK(array_names(full).empty() && array_names(seq).empty());
    CHECK(array_names(expl) == "mult1,ampl1,mult2,ampl2,");
    for (const auto& d : expl.array_descriptors()) CHECK(std::string(d.groupName) == "partials");

    const std::string shared = "rolloff1,rolloff2,detune1,detune2,bandwidth1,bandwidth2,bandwidthHz,";
    CHECK(setting_names(full).rfind(shared, 0) == 0);
    CHECK(setting_names(full).find("maxPartials,minMult,evenWeight1,evenWeight2,oddWeight1,oddWeight2,"
                                   "unitPO1,unitPO2,") != std::string::npos);
    CHECK(setting_names(seq).find("maxPartials,minMult1,minMult2,incr1,incr2,unitPO1,unitPO2,")
          != std::string::npos);
    CHECK(setting_names(expl).find("maxPartials,evolve,unitPO1,unitPO2,") != std::string::npos);
    CHECK(has_name(full.setting_descriptors(), "inharmonicity"));
    CHECK(has_name(seq.setting_descriptors(), "onsetFade"));
    CHECK_NEAR(full.get_setting("maxPartials"), 30.0f, 1e-6f);
    CHECK_NEAR(seq.get_setting("maxPartials"), 30.0f, 1e-6f);
    CHECK_NEAR(expl.get_setting("maxPartials"), 16.0f, 1e-6f);
    CHECK_NEAR(full.get_setting("motionHz"), 4.0f, 1e-6f);
    CHECK_NEAR(full.get_setting("onsetFade"), 0.03f, 1e-6f);
    CHECK_NEAR(full.get_setting("bandwidthHz"), 30.0f, 1e-6f);
    CHECK(expl.get_setting("evolve") == 1.0f);
    for (const auto& d : expl.setting_descriptors())
        if (std::string(d.name) == "evolve") CHECK(d.type == SettingType::Bool);
    CHECK(full.partial_count() == 30 && seq.partial_count() == 30 && expl.partial_count() == 16);
}

// Every type prepares and renders finite values; a wired source plays.
static void run_additive_smoke_tests() {
    RenderContext ctx{48000};
    auto smoke = [&](ValueSource& s) {
        s.prepare(ctx); s.start(4800);
        for (int i = 0; i < 10; ++i) CHECK(std::isfinite(s.next()));
    };
    AdditiveSource2 a2;          a2.set_default_partials(40); smoke(a2);
    BasicAdditiveSource basic;   smoke(basic);
    FullAdditiveSource bare;     smoke(bare);                 // no partials: silent
    Formant formant;                    smoke(formant);
    FormantSpectrum spectrum;           spectrum.formants.push_back(std::make_shared<Formant>()); smoke(spectrum);
    FixedSpectrum fixed({0.0f, 1.0f});  smoke(fixed);
    BandSpectrum band;                  smoke(band);
    FormantSequence seq;                smoke(seq);
    CompositePartials comp;             smoke(comp);
    ExpandRuleNode rule;                smoke(rule);
    FullPartials full;                  smoke(full);
    SequencePartials sequence;          smoke(sequence);
    ExplicitPartials expl;              smoke(expl);

    FullAdditiveSource f;
    f.set_param("partials", std::make_shared<FullPartials>());
    f.set_param("formant", std::make_shared<Formant>());
    f.set_param("formantWeight", std::make_shared<ConstantSource>(0.5f));
    f.prepare(ctx); f.start(4800);
    float sum = 0.0f;
    for (int i = 0; i < 400; ++i) sum += std::fabs(f.next());
    CHECK(sum > 0.0f);

    basic.set_param("frequency", std::make_shared<ConstantSource>(220.0f));
    basic.prepare(ctx); basic.start(4800);
    sum = 0.0f;
    for (int i = 0; i < 400; ++i) sum += std::fabs(basic.next());
    CHECK(sum > 0.0f);
}

// AdditiveSource2 and BasicAdditiveSource pull their own pins once per
// sample and prepare them once per note.
static void run_additive_auto_pin_tests() {
    RenderContext ctx{48000};
    auto count = [&](ValueSource& s, std::initializer_list<const char*> names) {
        std::vector<std::shared_ptr<PinCounter>> probes;
        for (const char* n : names) {
            probes.push_back(std::make_shared<PinCounter>());
            s.set_param(n, probes.back());
        }
        s.prepare(ctx); s.start(4800);
        for (int i = 0; i < 7; ++i) s.next();
        for (const auto& p : probes) CHECK(p->starts == 1 && p->nexts == 7);
    };
    AdditiveSource2 a2;
    a2.set_default_partials(10);
    count(a2, {"phaseOffset", "freqVarDepth", "freqVarSpeed", "amplVarDepth", "amplVarSpeed"});
    BasicAdditiveSource basic;
    count(basic, {"evenWeight", "oddWeight", "rolloff", "freqVarPct", "freqVarSpeed",
                  "amplVarPct", "amplVarSpeed"});

    // the envelopes assigned to partials are not pins: pulled once per sample
    AdditiveSource2 b;
    b.set_default_partials(10);
    auto env = std::make_shared<PinCounter>();
    b.assign_ampl_envelope(env, AdditiveSource2::PartialFilter::All, 1, 10);
    b.prepare(ctx); b.start(4800);
    for (int i = 0; i < 7; ++i) b.next();
    CHECK(env->starts == 1 && env->nexts == 7);
}

// The pins the additive source drives through the partials and formant
// interfaces are Manual: prepared where the old code prepared them, advanced
// once per call, in declaration order, and not at all by next() on the part.
static void run_additive_manual_pin_tests() {
    RenderContext ctx{48000};

    std::vector<int> log;
    std::vector<std::shared_ptr<OrderProbe>> probes;
    FullPartials fp;
    for (int i = 0; i < 8; ++i) {
        probes.push_back(std::make_shared<OrderProbe>(&log, i + 1));
        fp.set_param(kPartialsEnvNames[i], probes.back());
    }
    fp.partials_prepare(ctx);
    fp.partials_start(4800);
    CHECK(log.empty());
    for (const auto& p : probes) CHECK(p->starts == 1);
    fp.partials_next();
    CHECK(log == std::vector<int>({1, 2, 3, 4, 5, 6, 7, 8}));
    log.clear();
    fp.next();                                             // the fake next() is partials_next()
    CHECK(log.size() == 8);
    fp.prepare(ctx); fp.start(4800);                                 // the base prepare reaches partials_prepare once
    for (const auto& p : probes) CHECK(p->starts == 2);

    Formant fm;
    auto c1 = std::make_shared<PinCounter>(), c2 = std::make_shared<PinCounter>();
    fm.set_param("frequency", c1);
    fm.set_param("power", c2);
    fm.prepare(ctx); fm.start(4800);
    fm.next();
    fm.fmt_next();
    CHECK(c1->starts == 1 && c2->starts == 1);
    CHECK(c1->rate == 48000 && c2->rate == 48000);     // fmt_prepare reached the pins
    CHECK(c1->nexts == 2 && c2->nexts == 2);

    BandSpectrum band;
    FormantSequence seq;
    auto b1 = std::make_shared<PinCounter>(), b2 = std::make_shared<PinCounter>();
    band.set_param("startFreq", b1);
    seq.set_param("blend", b2);
    band.fmt_prepare(ctx); band.fmt_start(4800); band.fmt_next();
    seq.fmt_prepare(ctx);  seq.fmt_start(4800);  seq.fmt_next();
    CHECK(b1->starts == 1 && b1->nexts == 1);
    CHECK(b2->starts == 1 && b2->nexts == 1);

    ExpandRuleNode rule;                                   // nothing pulls its pins
    auto r1 = std::make_shared<PinCounter>();
    rule.set_param("spacing1", r1);
    rule.prepare(ctx); rule.start(4800);
    for (int i = 0; i < 3; ++i) rule.next();
    CHECK(r1->starts == 0 && r1->nexts == 0);
}

// FullAdditiveSource reads the formant weight and floor only while a formant
// is wired and partials are set, and prepares them either way.
static void run_additive_formant_pin_tests() {
    RenderContext ctx{48000};
    FullAdditiveSource f;
    auto w = std::make_shared<PinCounter>(), fl = std::make_shared<PinCounter>();
    f.set_param("formantWeight", w);
    f.set_param("formantFloor", fl);
    f.set_param("partials", std::make_shared<FullPartials>());
    f.prepare(ctx); f.start(4800);
    for (int i = 0; i < 5; ++i) f.next();
    CHECK(w->starts == 1 && fl->starts == 1);
    CHECK(w->nexts == 0 && fl->nexts == 0);          // no formant: never advanced

    auto formant = std::make_shared<Formant>();
    auto freq = std::make_shared<PinCounter>();
    formant->set_param("frequency", freq);
    f.set_param("formant", formant);
    f.prepare(ctx); f.start(4800);
    for (int i = 0; i < 5; ++i) f.next();
    CHECK(w->starts == 2 && fl->starts == 2 && freq->starts == 1);
    CHECK(w->nexts == 5 && fl->nexts == 5 && freq->nexts == 5);

    FullAdditiveSource g;                           // no partials: nothing advances
    auto gw = std::make_shared<PinCounter>();
    g.set_param("formantWeight", gw);
    g.set_param("formant", formant);
    g.prepare(ctx); g.start(4800);
    for (int i = 0; i < 5; ++i) g.next();
    CHECK(gw->starts == 1 && gw->nexts == 0);
}

// The structural inputs are type-checked when wired and keep a typed view.
static void run_additive_wiring_tests() {
    FullAdditiveSource f;
    auto fp = std::make_shared<FullPartials>();
    auto fm = std::make_shared<Formant>();
    auto other = std::make_shared<ConstantSource>(1.0f);
    f.set_param("partials", fp);
    f.set_param("formant", fm);
    CHECK(f.get_param("partials") == fp && f.get_param("formant") == fm);
    f.set_param("partials", other);                        // not an IPartials: ignored
    f.set_param("formant", other);
    f.set_param("formant", nullptr);
    CHECK(f.get_param("partials") == fp && f.get_param("formant") == fm);

    AdditiveSource2 a2;
    auto sp = std::make_shared<SequencePartials>();
    sp->set_setting("maxPartials", 12.0f);
    sp->get_array("mult1");                                // rebuild the arrays before they are copied
    a2.set_param("partials", sp);
    CHECK(a2.get_param("partials") == sp);
    CHECK_NEAR(a2.get_setting("partialCount"), 12.0f, 1e-6f);
    a2.set_param("partials", other);                       // not a Partials: ignored
    CHECK(a2.get_param("partials") == sp);

    FormantSequence seq;
    auto s1 = std::make_shared<FormantSpectrum>(), s2 = std::make_shared<FormantSpectrum>();
    seq.set_param("spectra", s1);
    CHECK(seq.formants.size() == 1);
    seq.add_param("spectra", s2);
    CHECK(seq.formants.size() == 2);
    seq.set_param("spectra", s2);                          // set replaces, add appends
    CHECK(seq.formants.size() == 1);
    seq.add_param("spectra", other);                       // not an IFormant: ignored
    CHECK(seq.formants.size() == 1);
    seq.clear_param("spectra");
    CHECK(seq.formants.empty());
    seq.set_param("blend", other);                         // the pin is served by the base
    CHECK(seq.get_param("blend") == other && seq.get_param("spectra") == nullptr);

    CompositePartials comp;
    auto p2 = std::make_shared<ExplicitPartials>();
    comp.add_param("partials", fp);
    comp.set_param("partials", p2);                        // wiring appends
    comp.set_param("partials", other);                     // not an IPartials: ignored
    CHECK(comp.partial_count() == 30 + 16);
    comp.clear_param("partials");
    CHECK(comp.partial_count() == 0);
}

// The settings that reshape the arrays rebuild them; the clamps hold.
static void run_additive_setting_hook_tests() {
    FullPartials full;
    full.set_setting("maxPartials", 12.0f);
    CHECK(full.get_array("mult1").size() == 12);           // get_array rebuilds a dirty set
    full.set_setting("minMult", 3.0f);
    CHECK_NEAR(full.get_array("mult1")[0], 3.0f, 1e-6f);
    full.set_setting("maxPartials", 0.0f);
    CHECK_NEAR(full.get_setting("maxPartials"), 1.0f, 1e-6f);
    full.set_setting("minMult", -4.0f);
    CHECK_NEAR(full.get_setting("minMult"), 1.0f, 1e-6f);
    full.set_setting("bandwidthHz", 0.2f);
    CHECK_NEAR(full.get_setting("bandwidthHz"), 1.0f, 1e-6f);
    full.set_setting("maxPartials", 4.0f);
    full.set_setting("evenWeight1", 0.25f);
    CHECK_NEAR(full.get_array("ampl1")[1], 0.25f, 1e-6f);  // partial 2 is even

    SequencePartials seq;
    seq.set_setting("maxPartials", 5.0f);
    seq.set_setting("incr1", 2.0f);
    auto m1 = seq.get_array("mult1");
    CHECK(m1.size() == 5);
    CHECK_NEAR(m1[2], 5.0f, 1e-6f);                        // 1 + 2 * 2
    seq.set_setting("maxPartials", -3.0f);
    CHECK_NEAR(seq.get_setting("maxPartials"), 1.0f, 1e-6f);

    ExplicitPartials expl;
    expl.set_setting("maxPartials", 5.0f);
    CHECK(expl.get_array("mult1").size() == 5);            // the working defaults
    CHECK_NEAR(expl.get_array("mult1")[4], 5.0f, 1e-6f);
    expl.set_setting("maxPartials", 0.0f);
    CHECK_NEAR(expl.get_setting("maxPartials"), 1.0f, 1e-6f);

    AdditiveSource2 a2;
    a2.set_setting("partialCount", 50.0f);                 // rebuilds the default partials
    CHECK_NEAR(a2.get_setting("partialCount"), 50.0f, 1e-6f);
    RenderContext ctx{48000};
    a2.prepare(ctx); a2.start(4800);
    for (int i = 0; i < 10; ++i) CHECK(std::isfinite(a2.next()));
    CHECK_NEAR(a2.get_setting("partialCount"), 50.0f, 1e-6f);   // prepare does not rewrite it
}

// ExplicitPartials keeps the four user arrays equal and in step with
// maxPartials, and mirrors _1 into _2 while evolve is off.
static void run_additive_explicit_array_tests() {
    // The user's copies arrive in any order; mult1 last keeps each rebuild in range.
    ExplicitPartials e;
    e.set_array("mult2", {1.0f, 2.1f, 3.2f});
    e.set_array("ampl2", {0.5f, 0.4f, 0.3f});
    e.set_array("ampl1", {1.0f, 0.5f, 0.25f});
    e.set_array("mult1", {1.0f, 2.0f, 3.0f});
    CHECK(e.get_array("mult2") == std::vector<float>({1.0f, 2.1f, 3.2f}));
    CHECK(e.get_array("ampl1") == std::vector<float>({1.0f, 0.5f, 0.25f}));
    CHECK_NEAR(e.get_setting("maxPartials"), 3.0f, 1e-6f);
    CHECK(e.partial_count() == 3);

    e.set_setting("evolve", 0.0f);                         // turning evolve off mirrors _1 into _2
    CHECK(e.get_array("mult2") == e.get_array("mult1"));
    CHECK(e.get_array("ampl2") == e.get_array("ampl1"));
    e.set_array("mult1", {4.0f, 5.0f});
    CHECK(e.get_array("mult2") == std::vector<float>({4.0f, 5.0f}));
    CHECK_NEAR(e.get_setting("maxPartials"), 2.0f, 1e-6f);
    e.set_array("ampl1", {0.3f, 0.2f});
    CHECK(e.get_array("ampl2") == std::vector<float>({0.3f, 0.2f}));
    CHECK(e.get_array("nosuch").empty());

    ExplicitPartials fresh;                                // nothing set: the working defaults
    CHECK(fresh.get_array("mult1").size() == 16);
    CHECK(fresh.get_array("ampl2").size() == 16);
    CHECK(fresh.mult1.values.empty());

    RenderContext ctx{48000};
    auto g = std::make_shared<ExplicitPartials>();
    g->set_array("mult2", {1.0f, 2.0f, 3.1f});
    g->set_array("ampl2", {1.0f, 0.5f, 0.2f});
    g->set_array("ampl1", {1.0f, 0.4f, 0.3f});
    g->set_array("mult1", {1.0f, 2.0f, 3.0f});
    FullAdditiveSource f;
    f.set_param("partials", g);
    f.prepare(ctx); f.start(4800);
    float sum = 0.0f;
    for (int i = 0; i < 400; ++i) { float v = f.next(); CHECK(std::isfinite(v)); sum += std::fabs(v); }
    CHECK(sum > 0.0f);
}

// Checkpoint 2.1: the physical models (delay line, KS string, allpass
// resonator, bow table, 2D mesh, Pierce filter), all Processors: the signal
// input is the inherited `source`, declared first.
#include "mforce/source/allpass_resonator.h"
#include "mforce/source/bow_table_source.h"
#include "mforce/source/ks_string.h"
#include "mforce/source/mesh2d_source.h"

namespace {
// The connectable pins of a node in registration order, which is the order
// the base advances them.
std::string pin_order(const mforce::Component& c) {
    std::string out;
    for (const auto* p : c.pins())
        if (p->kind == mforce::PinKind::Param || p->kind == mforce::PinKind::Input) {
            out += p->name;
            out += ',';
        }
    return out;
}
}

// Every type prepares and renders finite values, declares its pins in the
// order they are advanced, and keeps its category and defaults.
static void run_physical_pin_shape_tests() {
    RenderContext ctx{48000};
    auto smoke = [&](ValueSource& s) {
        s.prepare(ctx); s.start(4800);
        for (int i = 0; i < 10; ++i) CHECK(std::isfinite(s.next()));
    };
    AllpassResonator ar;          smoke(ar);
    BowTableSource bt;            smoke(bt);
    DelayLineSource dl;           smoke(dl);
    KSString ks;                  smoke(ks);
    Mesh2DSource mesh;            smoke(mesh);
    PierceFilterSource pierce;    smoke(pierce);

    CHECK(pin_order(ar) == "source,frequency,amplitude,");
    CHECK(pin_order(bt) == "source,slope,offset,");
    CHECK(pin_order(dl) == "source,frequency,ratio,amplitude,");
    CHECK(pin_order(ks) == "source,frequency,amplitude,damper,bow,");
    CHECK(pin_order(mesh) ==
          "source,inX,inY,outX,outY,decay,edgeFc,edgeR,coefNeg,coefPos,");
    CHECK(pin_order(pierce) == "source,coefNeg,coefPos,");

    CHECK(input_names(ar) == "source," && input_names(bt) == "source,");
    CHECK(input_names(ks) == "source,damper,bow,");
    CHECK(param_names(ks) == "frequency,amplitude,");
    CHECK(param_names(pierce) == "coefNeg,coefPos,");
    CHECK(setting_names(ar) == "feedback,damping,stiffness,tension,exciteGain,direct,");
    CHECK(setting_names(bt) == "minOutput,maxOutput,");
    CHECK(setting_names(dl) == "compensate,");
    CHECK(setting_names(mesh) == "cols,rows,edgeMode,");
    CHECK(setting_names(pierce).empty());
    CHECK(setting_names(ks) ==
          "numCombs,detune,spreadJitter,excStagger,t60,brightness,dispersion,"
          "inharmGain,inharmFb,inharmHp,ap1,ap2,ap3,fbCoeff,releaseFb,damperNoise,"
          "exciteGain,direct,bowSpeed,frictionGain,bowGain,");

    CHECK(bt.category() == SourceCategory::Modulator);
    CHECK(pierce.category() == SourceCategory::Filter);
    CHECK(ar.category() == SourceCategory::Oscillator);
    CHECK(ks.category() == SourceCategory::Oscillator && !ks.tracks_frequency_live());
    CHECK(dl.tracks_frequency_live() && mesh.tracks_frequency_live());

    // a declared default wins over anything the constructor once set
    CHECK_NEAR(ks.get_setting("detune"), 0.75f, 1e-6f);
    CHECK_NEAR(ks.get_setting("numCombs"), 3.0f, 1e-6f);
    CHECK_NEAR(param_default(ks, "frequency"), 220.0f, 1e-6f);
    CHECK_NEAR(param_default(mesh, "edgeFc"), 1575.0f, 1e-6f);
    CHECK_NEAR(param_default(pierce, "coefPos"), -0.5f, 1e-6f);
    CHECK_NEAR(mesh.get_setting("cols"), 12.0f, 1e-6f);
    CHECK_NEAR(dl.get_setting("compensate"), 0.0f, 1e-6f);
    for (const auto& d : mesh.setting_descriptors())
        if (std::string(d.name) == "edgeMode") {
            CHECK(d.enum_labels != nullptr && std::string(d.enum_labels[2]) == "pierce");
            CHECK(d.enum_labels[3] == nullptr);
        }
}

// The base prepares and advances every wired pin once per note and once per
// sample, so a node reads each pin's current value in compute().
static void run_physical_auto_pin_tests() {
    RenderContext ctx{48000};
    auto count = [&](ValueSource& s, std::initializer_list<const char*> names) {
        std::vector<std::shared_ptr<PinCounter>> probes;
        for (const char* n : names) {
            probes.push_back(std::make_shared<PinCounter>());
            s.set_param(n, probes.back());
        }
        s.prepare(ctx); s.start(4800);
        for (int i = 0; i < 7; ++i) CHECK(std::isfinite(s.next()));
        for (const auto& p : probes) CHECK(p->starts == 1 && p->nexts == 7);
    };
    AllpassResonator ar;
    count(ar, {"source", "frequency", "amplitude"});
    BowTableSource bt;
    count(bt, {"source", "slope", "offset"});
    DelayLineSource dl;
    count(dl, {"source", "frequency", "ratio", "amplitude"});
    KSString ks;
    count(ks, {"source", "frequency", "amplitude", "damper", "bow"});
    Mesh2DSource mesh;
    count(mesh, {"source", "inX", "inY", "outX", "outY", "decay", "edgeFc", "edgeR",
                 "coefNeg", "coefPos"});
    PierceFilterSource pierce;
    count(pierce, {"source", "coefNeg", "coefPos"});

    // an unwired pin reads its declared constant and is not a source
    CHECK(ks.get_param("damper") != nullptr && ks.get_param("bow") != nullptr);
    KSString bare;
    CHECK(bare.get_param("source") == nullptr && bare.get_param("frequency") == nullptr);
    bare.clear_param("nosuch");
    CHECK(bare.get_param("nosuch") == nullptr);
}

// The base advances the pins in declaration order, the inherited `source`
// first.
static void run_physical_advance_order_tests() {
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
    AllpassResonator ar;
    order(ar, {"source", "frequency", "amplitude"});
    BowTableSource bt;
    order(bt, {"source", "slope", "offset"});
    DelayLineSource dl;
    order(dl, {"source", "frequency", "ratio", "amplitude"});
    KSString ks;
    order(ks, {"source", "frequency", "amplitude", "damper", "bow"});
    Mesh2DSource mesh;
    order(mesh, {"source", "inX", "inY", "outX", "outY", "decay", "edgeFc", "edgeR",
                 "coefNeg", "coefPos"});
    PierceFilterSource pierce;
    order(pierce, {"source", "coefNeg", "coefPos"});
}

// Settings the old set_setting clamped are still clamped; the others are not.
static void run_physical_setting_hook_tests() {
    KSString ks;
    ks.set_setting("numCombs", 40.0f);   CHECK_NEAR(ks.get_setting("numCombs"), 10.0f, 1e-6f);
    ks.set_setting("numCombs", 0.0f);    CHECK_NEAR(ks.get_setting("numCombs"), 1.0f, 1e-6f);
    ks.set_setting("t60", 0.001f);       CHECK_NEAR(ks.get_setting("t60"), 0.05f, 1e-6f);
    ks.set_setting("t60", 600.0f);       CHECK_NEAR(ks.get_setting("t60"), 600.0f, 1e-6f);
    ks.set_setting("ap1", 5.0f);         CHECK_NEAR(ks.get_setting("ap1"), 0.95f, 1e-6f);
    ks.set_setting("ap3", -5.0f);        CHECK_NEAR(ks.get_setting("ap3"), -0.95f, 1e-6f);
    ks.set_setting("brightness", 9.0f);  CHECK_NEAR(ks.get_setting("brightness"), 1.0f, 1e-6f);
    ks.set_setting("releaseFb", -2.0f);  CHECK_NEAR(ks.get_setting("releaseFb"), 0.0f, 1e-6f);
    ks.set_setting("damperNoise", 9.0f); CHECK_NEAR(ks.get_setting("damperNoise"), 2.0f, 1e-6f);
    ks.set_setting("excStagger", 1.0f);  CHECK_NEAR(ks.get_setting("excStagger"), 0.02f, 1e-6f);
    ks.set_setting("detune", 99999.0f);  CHECK_NEAR(ks.get_setting("detune"), 99999.0f, 1e-3f);
    ks.set_setting("inharmGain", -3.0f); CHECK_NEAR(ks.get_setting("inharmGain"), -3.0f, 1e-6f);
    ks.set_setting("nosuch", 1.0f);      CHECK_NEAR(ks.get_setting("nosuch"), 0.0f, 1e-6f);

    AllpassResonator ar;
    ar.set_setting("feedback", 5.0f);    CHECK_NEAR(ar.get_setting("feedback"), 0.9999f, 1e-6f);
    ar.set_setting("damping", -1.0f);    CHECK_NEAR(ar.get_setting("damping"), 0.05f, 1e-6f);
    ar.set_setting("stiffness", 9.0f);   CHECK_NEAR(ar.get_setting("stiffness"), 0.95f, 1e-6f);
    ar.set_setting("tension", -2.0f);    CHECK_NEAR(ar.get_setting("tension"), 0.0f, 1e-6f);
    ar.set_setting("direct", 7.0f);      CHECK_NEAR(ar.get_setting("direct"), 7.0f, 1e-6f);

    DelayLineSource dl;
    dl.set_setting("compensate", 0.4f);  CHECK_NEAR(dl.get_setting("compensate"), 1.0f, 1e-6f);
    dl.set_setting("compensate", 0.0f);  CHECK_NEAR(dl.get_setting("compensate"), 0.0f, 1e-6f);

    BowTableSource bt;
    bt.set_setting("minOutput", 0.2f);   CHECK_NEAR(bt.get_setting("minOutput"), 0.2f, 1e-6f);
    bt.set_setting("maxOutput", 7.0f);   CHECK_NEAR(bt.get_setting("maxOutput"), 7.0f, 1e-6f);
}

// The mesh size and the edge mode are clamped, and each change clears the
// mesh: a ringing mesh with no excitation falls silent at once.
static void run_physical_mesh_hook_tests() {
    Mesh2DSource mesh;
    mesh.set_setting("cols", 500.0f);    CHECK_NEAR(mesh.get_setting("cols"), 64.0f, 1e-6f);
    mesh.set_setting("rows", 1.0f);      CHECK_NEAR(mesh.get_setting("rows"), 2.0f, 1e-6f);
    mesh.set_setting("edgeMode", 9.0f);  CHECK_NEAR(mesh.get_setting("edgeMode"), 2.0f, 1e-6f);
    mesh.set_setting("edgeMode", -3.0f); CHECK_NEAR(mesh.get_setting("edgeMode"), 0.0f, 1e-6f);

    RenderContext ctx{48000};
    auto in = std::make_shared<ConstantSource>(0.0f);
    Mesh2DSource ring;
    ring.set_param("source", in);
    ring.set_setting("cols", 8.0f);
    ring.set_setting("rows", 6.0f);
    ring.prepare(ctx); ring.start(4800);
    in->set(1.0f); ring.next(); in->set(0.0f);
    float energy = 0.0f;
    for (int i = 0; i < 60; ++i) energy += std::fabs(ring.next());
    CHECK(energy > 0.0f);
    ring.set_setting("edgeMode", 1.0f);
    for (int i = 0; i < 20; ++i) CHECK(ring.next() == 0.0f);
}

// The pins are read in compute(): the bow table's curve, KSString's bow mode.
static void run_physical_behavior_tests() {
    RenderContext ctx{48000};
    BowTableSource bt;
    bt.set_param("source", std::make_shared<ConstantSource>(1.0f));
    bt.prepare(ctx); bt.start(4800);
    CHECK_NEAR(bt.next(), 0.01f, 1e-6f);           // rc clamped up to minOutput
    bt.set_param("slope", std::make_shared<ConstantSource>(0.5f));
    CHECK_NEAR(bt.next(), std::pow(1.25f, -4.0f), 1e-5f);
    bt.set_param("offset", std::make_shared<ConstantSource>(1.0f));
    CHECK_NEAR(bt.next(), std::pow(1.75f, -4.0f), 1e-5f);

    // the bow pin switches the bow junction on only while it is wired
    auto strike = [&](bool bowed) {
        auto in = std::make_shared<ConstantSource>(0.0f);
        KSString ks;
        ks.set_param("source", in);
        if (bowed) ks.set_param("bow", std::make_shared<ConstantSource>(0.8f));
        ks.prepare(ctx); ks.start(4800);
        in->set(1.0f); ks.next(); in->set(0.0f);
        float sum = 0.0f;
        for (int i = 0; i < 2000; ++i) sum += ks.next();
        return sum;
    };
    const float plucked = strike(false), bowed = strike(true);
    CHECK(std::isfinite(plucked) && std::isfinite(bowed));
    CHECK(plucked != bowed);
}

// Checkpoint 2.1: the filters (Butterworth sections, delay, hammer bank,
// limiter, reverb, state-variable filter, biquad) and the vibrato modulator.
// Every filter is a Processor: the signal input is the inherited `source`,
// declared first. The vibrato has no signal input and is a Generator.
#include "mforce/filter/biquad_source.h"
#include "mforce/filter/filters.h"
#include "mforce/filter/hammer_bank.h"
#include "mforce/filter/limiter.h"
#include "mforce/filter/reverb.h"
#include "mforce/filter/vibrato.h"

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

// Every type prepares and renders finite values, declares its pins in the
// order they are advanced, and keeps its category.
static void run_filter_pin_shape_tests() {
    RenderContext ctx{48000};
    auto smoke = [&](ValueSource& s) {
        s.prepare(ctx); s.start(4800);
        for (int i = 0; i < 10; ++i) CHECK(std::isfinite(s.next()));
    };
    BiquadSource bq;         smoke(bq);
    BWLowpassFilter lp(2);   smoke(lp);
    BWHighpassFilter hp(2);  smoke(hp);
    BWBandpassFilter bp(2);  smoke(bp);
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
    CHECK(setting_names(lp).empty() && setting_names(bp).empty() && setting_names(dly).empty());
    CHECK(setting_names(lim).empty() && setting_names(rev).empty());

    CHECK(bq.category() == SourceCategory::Filter && lp.category() == SourceCategory::Filter);
    CHECK(hp.category() == SourceCategory::Filter && bp.category() == SourceCategory::Filter);
    CHECK(dly.category() == SourceCategory::Filter && hammer.category() == SourceCategory::Filter);
    CHECK(lim.category() == SourceCategory::Filter && rev.category() == SourceCategory::Filter);
    CHECK(svf.category() == SourceCategory::Filter);
    CHECK(vib.category() == SourceCategory::Modulator);
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
    BWBandpassFilter bp(2);
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
    BWLowpassFilter lp(2);   count(lp, {"source", "cutoffFreq"});
    BWHighpassFilter hp(2);  count(hp, {"source", "cutoffFreq"});
    BWBandpassFilter bp(2);  count(bp, {"source", "lowCutoff", "highCutoff"});
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
    BWLowpassFilter lp(2);   order(lp, {"source", "cutoffFreq"});
    BWBandpassFilter bp(2);  order(bp, {"source", "lowCutoff", "highCutoff"});
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
    BWLowpassFilter lp(2);
    BWHighpassFilter hp(2);
    BWBandpassFilter bp(2);
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
    BWLowpassFilter low(2);
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
static void run_filter_tests() {
    run_filter_pin_shape_tests();
    run_filter_default_tests();
    run_filter_auto_pin_tests();
    run_filter_advance_order_tests();
    run_filter_setting_hook_tests();
    run_filter_behavior_tests();
    run_filter_butterworth_tests();
    run_vibrato_tests();
}

// Checkpoint 2.1: the combiners, modulators and the other sources in source/.
// The distorted source, the shaper, the wormhole, the multiplex and the
// repeating source have one signal input and are Processors (the shaper, the
// multiplex and the repeating source pull it themselves:
// Processor(Pull::Manual)); the phased, segment and
// static sources have none and are Generators. The combiners have two signal
// inputs and no `source`, and stay ValueSources with their categories.
#include "mforce/core/source_registry.h"
#include "mforce/source/multiplex_source.h"
#include "mforce/source/phased_value_source.h"
#include "mforce/source/repeating_source.h"
#include "mforce/source/segment_source.h"

static_assert(std::is_base_of_v<Processor, DistortedSource> &&
              std::is_base_of_v<Processor, ShaperSource> &&
              std::is_base_of_v<Processor, WormholeSource> &&
              std::is_base_of_v<Processor, MultiplexSource> &&
              std::is_base_of_v<Processor, RepeatingSource>);
static_assert(std::is_base_of_v<Generator, PhasedValueSource> &&
              std::is_base_of_v<Generator, SegmentSource> &&
              std::is_base_of_v<Generator, StaticVarSource> &&
              std::is_base_of_v<Generator, StaticRangeSource>);
static_assert(!std::is_base_of_v<Processor, CombinedSource> &&
              !std::is_base_of_v<Processor, CrossfadeSource> &&
              !std::is_base_of_v<Generator, CombinedSource> &&
              !std::is_base_of_v<Generator, CrossfadeSource>);

namespace {
const SettingDescriptor* find_setting(const Component& c, const char* name) {
    for (const auto& d : c.setting_descriptors())
        if (std::string(d.name) == name) return &d;
    return nullptr;
}
// Counts prepares and advances and remembers the length it was last prepared for.
struct LengthProbe final : Generator {
    int starts{0}, nexts{0}, lastFrames{0};
    void on_start(int frames) override { ++starts; lastFrames = frames; }
    float compute() override { ++nexts; return 1.0f; }
    const char* type_name() const override { return "LengthProbe"; }
};
std::shared_ptr<ConstantSource> konst(float v) { return std::make_shared<ConstantSource>(v); }
}

// Every type prepares and renders finite values, and declares its pins in the
// order they are advanced.
static void run_combine_mod_pin_shape_tests() {
    RenderContext ctx{48000};
    auto smoke = [&](ValueSource& s) {
        s.prepare(ctx); s.start(4800);
        bool finite = true;
        for (int i = 0; i < 10; ++i) finite = finite && std::isfinite(s.next());
        CHECK(finite);
    };
    CombinedSource comb(konst(1.0f), konst(2.0f), CombineOp::Add);   smoke(comb);
    CrossfadeSource xfade;
    xfade.set_param("source1", konst(1.0f));
    xfade.set_param("source2", konst(2.0f));                     smoke(xfade);
    DistortedSource dist;                                    smoke(dist);
    StaticVarSource sv(1.0f, 0.1f);                          smoke(sv);
    StaticRangeSource sr(0.0f, 1.0f);                        smoke(sr);
    MultiplexSource mux;                                     smoke(mux);
    PhasedValueSource phased(0.05f);
    phased.add_stage({konst(1.0f), 0.5f, 0.0f, 0.0f, 1.0f});
    phased.add_stage({konst(2.0f), 0.0f, 0.0f, 0.0f, 1.0f});     smoke(phased);
    RepeatingSource rep;
    rep.set_param("source", konst(1.0f));                        smoke(rep);
    SegmentSource seg(std::vector<float>{2.0f, 1.0f, 2.0f, -1.0f});  smoke(seg);
    ShaperSource shaper;                                     smoke(shaper);
    WormholeSource wh;                                       smoke(wh);

    CHECK(pin_order(comb) == "source1,source2,");
    CHECK(pin_order(xfade) == "amplitude,source1,source2,");
    CHECK(pin_order(dist) == "source,density,gain,shift,amplitude,");
    CHECK(pin_order(sv).empty() && pin_order(sr).empty());
    CHECK(pin_order(mux) == "source," && pin_order(rep) == "source,");
    CHECK(pin_order(phased) == "amplitude,");
    CHECK(pin_order(seg) == "amplitude,smoothness,width,gap,gapVarPct,widthVarPct,valVarPct,");
    CHECK(pin_order(shaper) == "source,drive,smoothness,morph,breakaway,capture,");
    CHECK(pin_order(wh) == "source,");

    CHECK(input_names(comb) == "source1,source2," && input_names(xfade) == "source1,source2,");
    CHECK(input_names(dist) == "source," && input_names(mux) == "source,");
    CHECK(input_names(phased).empty() && input_names(seg).empty());
    CHECK(param_names(comb).empty() && param_names(xfade) == "amplitude,");
    CHECK(param_names(dist) == "density,gain,shift,amplitude,");
    CHECK(param_names(shaper) == "drive,smoothness,morph,breakaway,capture,");
    CHECK(param_names(seg) == "amplitude,smoothness,width,gap,gapVarPct,widthVarPct,valVarPct,");
    CHECK(setting_names(comb) == "operation,gainAdj," && setting_names(xfade) == "ratio,overlap,gainAdj,");
    CHECK(setting_names(mux) == "count," && setting_names(shaper) == "hysteresis,");
    CHECK(setting_names(rep) == "duration,durVarPct,gapDuration,gapVarPct,");
    CHECK(setting_names(seg) == "oneShot,timeMode,");
    CHECK(setting_names(dist).empty() && setting_names(wh).empty() && setting_names(phased).empty());
    CHECK(array_names(seg) == "values," && array_names(shaper) == "values,segs,values2,segs2,");
    CHECK(array_names(comb).empty() && array_names(mux).empty());
}

// Each node keeps its place in the UI menu: the Processors' Filter default is
// overridden where the node was not a filter.
static void run_combine_mod_category_tests() {
    CombinedSource comb(nullptr, nullptr, CombineOp::Add);
    CrossfadeSource xfade;
    MultiplexSource mux;
    CHECK(comb.category() == SourceCategory::Combiner && xfade.category() == SourceCategory::Combiner);
    CHECK(mux.category() == SourceCategory::Combiner);
    DistortedSource dist;
    RepeatingSource rep;
    ShaperSource shaper;
    WormholeSource wh;
    CHECK(dist.category() == SourceCategory::Modulator && rep.category() == SourceCategory::Modulator);
    CHECK(shaper.category() == SourceCategory::Modulator && wh.category() == SourceCategory::Modulator);
    PhasedValueSource phased(0.05f);
    SegmentSource seg(std::vector<float>{});
    StaticVarSource sv(1.0f, 0.0f);
    StaticRangeSource sr(0.0f, 1.0f);
    CHECK(phased.category() == SourceCategory::Envelope && seg.category() == SourceCategory::Generator);
    CHECK(sv.category() == SourceCategory::Utility && sr.category() == SourceCategory::Utility);
    CHECK(phased.range() == Generator::Range::None && seg.range() == Generator::Range::None);
}

// A declared default is what an unwired pin reads; the enum settings keep
// their labels and their types; constructor arguments set values, not the
// declared defaults.
static void run_combine_mod_default_tests() {
    CrossfadeSource xf;
    CHECK_NEAR(param_default(xf, "amplitude"), 1.0f, 1e-6f);
    CHECK_NEAR(xf.get_setting("ratio"), 0.5f, 1e-6f);
    CHECK_NEAR(xf.get_setting("overlap"), 0.1f, 1e-6f);
    CHECK_NEAR(xf.get_setting("gainAdj"), 0.0f, 1e-6f);
    DistortedSource dist;
    CHECK_NEAR(param_default(dist, "amplitude"), 1.0f, 1e-6f);
    CHECK_NEAR(param_default(dist, "density"), 1.0f, 1e-6f);
    CHECK_NEAR(param_default(dist, "gain"), 1.0f, 1e-6f);
    CHECK_NEAR(param_default(dist, "shift"), 0.0f, 1e-6f);
    ShaperSource sh;
    CHECK_NEAR(param_default(sh, "drive"), 1.0f, 1e-6f);
    CHECK_NEAR(param_default(sh, "smoothness"), 0.5f, 1e-6f);
    CHECK_NEAR(param_default(sh, "morph"), 0.0f, 1e-6f);
    CHECK_NEAR(param_default(sh, "breakaway"), 0.6f, 1e-6f);
    CHECK_NEAR(param_default(sh, "capture"), 0.0f, 1e-6f);
    SegmentSource seg(std::vector<float>{});
    CHECK_NEAR(param_default(seg, "smoothness"), 0.5f, 1e-6f);
    CHECK_NEAR(param_default(seg, "width"), 1.0f, 1e-6f);
    CHECK_NEAR(param_default(seg, "gap"), 0.0f, 1e-6f);
    PhasedValueSource phased(0.05f);
    CHECK_NEAR(param_default(phased, "amplitude"), 1.0f, 1e-6f);
    MultiplexSource mux;
    CHECK_NEAR(mux.get_setting("count"), 10.0f, 1e-6f);
    RepeatingSource rep;
    CHECK_NEAR(rep.get_setting("duration"), 0.5f, 1e-6f);
    CHECK_NEAR(rep.get_setting("gapDuration"), 0.2f, 1e-6f);

    CombinedSource comb(nullptr, nullptr, CombineOp::Multiply, 0.5f);
    CHECK_NEAR(comb.get_setting("operation"), 1.0f, 1e-6f);
    CHECK_NEAR(comb.get_setting("gainAdj"), 0.5f, 1e-6f);
    CHECK_NEAR(find_setting(comb, "operation")->default_value, 0.0f, 1e-6f);
    CHECK_NEAR(find_setting(comb, "gainAdj")->default_value, 0.0f, 1e-6f);
    SegmentSource once(std::vector<float>{4.0f, 1.0f}, true);
    CHECK_NEAR(once.get_setting("oneShot"), 1.0f, 1e-6f);
    CHECK_NEAR(find_setting(once, "oneShot")->default_value, 0.0f, 1e-6f);

    const auto* op = find_setting(comb, "operation");
    CHECK(op->type == SettingType::Int && op->enum_labels != nullptr && op->max_value == 3.0f);
    CHECK(std::string(op->enum_labels[0]) == "Mix" && std::string(op->enum_labels[3]) == "Sum");
    CHECK(std::string(op->enum_labels[1]) == "Multiply" && std::string(op->enum_labels[2]) == "Fade");
    CHECK(op->enum_labels[4] == nullptr);
    const auto* tm = find_setting(seg, "timeMode");
    CHECK(tm->type == SettingType::Int && tm->max_value == 2.0f);
    CHECK(std::string(tm->enum_labels[0]) == "auto" && std::string(tm->enum_labels[2]) == "seconds");
    CHECK(tm->enum_labels[3] == nullptr);
    CHECK(find_setting(seg, "oneShot")->type == SettingType::Bool);
    CHECK(find_setting(sh, "hysteresis")->type == SettingType::Bool);
    CHECK(find_setting(mux, "count")->type == SettingType::Int);
    CHECK(find_setting(rep, "duration")->type == SettingType::Float);

    // a fresh shaper is the identity curve with no segment overrides
    CHECK((sh.get_array("values") == std::vector<float>{-1.0f, -1.0f, 1.0f, 1.0f}));
    CHECK(sh.get_array("segs").empty() && sh.get_array("values2").empty());
    CHECK(sh.get_array("segs2").empty() && seg.get_array("values").empty());
}

// The base prepares and advances every wired Auto pin once per note and once
// per sample.
static void run_combine_mod_auto_pin_tests() {
    RenderContext ctx{48000};
    auto count = [&](ValueSource& s, std::initializer_list<const char*> names) {
        std::vector<std::shared_ptr<PinCounter>> probes;
        for (const char* n : names) {
            probes.push_back(std::make_shared<PinCounter>());
            s.set_param(n, probes.back());
        }
        s.prepare(ctx); s.start(4800);
        bool finite = true;
        for (int i = 0; i < 7; ++i) finite = finite && std::isfinite(s.next());
        bool each = true;
        for (const auto& p : probes) each = each && p->starts == 1 && p->nexts == 7;
        CHECK(finite && each);
    };
    CombinedSource comb(nullptr, nullptr, CombineOp::Add);
    count(comb, {"source1", "source2"});
    CrossfadeSource xfade;
    count(xfade, {"amplitude"});
    DistortedSource dist;
    count(dist, {"source", "density", "gain", "shift", "amplitude"});
    PhasedValueSource phased(0.05f);
    phased.add_stage({konst(1.0f), 0.5f, 0.0f, 0.0f, 1.0f});
    phased.add_stage({konst(2.0f), 0.0f, 0.0f, 0.0f, 1.0f});
    count(phased, {"amplitude"});
    SegmentSource seg(std::vector<float>{2.0f, 1.0f});
    count(seg, {"amplitude", "smoothness", "width", "gap", "gapVarPct", "widthVarPct", "valVarPct"});
    WormholeSource wh;
    count(wh, {"source"});
    // breakaway and capture are read only in hysteresis mode, but the base
    // pulls them in both
    const auto shaperPins = {"source", "drive", "smoothness", "morph", "breakaway", "capture"};
    ShaperSource shaper;
    count(shaper, shaperPins);
    ShaperSource hyst;
    hyst.set_setting("hysteresis", 1.0f);
    count(hyst, shaperPins);

    // an unwired pin reads its declared constant and is not a source
    CHECK(dist.get_param("source") != nullptr && dist.get_param("nosuch") == nullptr);
    DistortedSource bare;
    CHECK(bare.get_param("source") == nullptr && bare.get_param("gain") == nullptr);
    bare.clear_param("nosuch");
    CHECK(bare.get_param("nosuch") == nullptr);
}

// The Manual pins are the node's: the crossfade's sources are prepared for
// their own part of the note and pulled only in it; the repeating source (its
// inherited input) only while a repetition plays; the multiplex source
// (likewise) only while there are no clones.
static void run_combine_mod_manual_pin_tests() {
    RenderContext ctx{48000};
    // crossfade: 100 samples at ratio 0.5, overlap 0.1 -> each source is
    // prepared for 55 samples and pulled for 55 (45 alone + the 10 overlap)
    CrossfadeSource xf;
    auto s1 = std::make_shared<LengthProbe>(), s2 = std::make_shared<LengthProbe>();
    auto amp = std::make_shared<PinCounter>();
    xf.set_param("source1", s1);
    xf.set_param("source2", s2);
    xf.set_param("amplitude", amp);
    xf.prepare(ctx); xf.start(100);
    for (int i = 0; i < 100; ++i) xf.next();
    CHECK(amp->starts == 1 && amp->nexts == 100);
    CHECK(s1->starts == 1 && s1->lastFrames == 55 && s1->nexts == 55);
    CHECK(s2->starts == 1 && s2->lastFrames == 55 && s2->nexts == 55);

    // repeating: 10 samples of source, 6 of silence, per cycle (sr 1000);
    // the source is prepared again for each repetition and not pulled in a gap
    RepeatingSource rep;
    auto rsrc = std::make_shared<LengthProbe>();
    rep.set_param("source", rsrc);
    rep.set_setting("duration", 0.01f);
    rep.set_setting("gapDuration", 0.005f);
    rep.prepare(RenderContext{1000}); rep.start(100);
    CHECK(rsrc->starts == 1 && rsrc->lastFrames == 10);
    std::vector<float> out;
    for (int i = 0; i < 32; ++i) out.push_back(rep.next());
    bool pattern = true;
    for (int i = 0; i < 32; ++i) pattern = pattern && (out[i] == ((i % 16) < 10 ? 1.0f : 0.0f));
    CHECK(pattern);
    CHECK(rsrc->nexts == 20 && rsrc->starts == 3 && rsrc->lastFrames == 10);

    // multiplex: the wired source plays solo until the clones exist
    MultiplexSource solo;
    auto wired = std::make_shared<LengthProbe>();
    solo.set_param("source", wired);
    solo.prepare(ctx); solo.start(100);
    for (int i = 0; i < 5; ++i) solo.next();
    CHECK(wired->starts == 1 && wired->nexts == 5);
    CHECK_NEAR(solo.current(), 1.0f, 1e-6f);
    MultiplexSource cloned;
    auto unused = std::make_shared<LengthProbe>();
    cloned.set_param("source", unused);
    cloned.set_setting("count", 3.0f);
    cloned.set_template("{}", 0, [](int) {
        return std::make_pair(std::shared_ptr<ValueSource>(konst(2.0f)), MultiplexSource::ValueNodeMap{});
    });
    cloned.prepare(ctx); cloned.start(100);
    for (int i = 0; i < 5; ++i) cloned.next();
    CHECK(unused->starts == 0 && unused->nexts == 0);
    CHECK_NEAR(cloned.current(), 2.0f, 1e-6f);        // the mean of the three clones
}

// A node shared by the shaper's source pin and its drive chain computes once
// per tick, so the source and the drive chain read the same sample's value of
// it: nothing is a sample late.
static void run_combine_mod_shaper_timing_tests() {
    RenderContext ctx{48000};
    TestHost h;
    auto ramp = std::make_shared<UnitRamp>();
    auto doubled = std::make_shared<Doubler>();
    doubled->set_param("source", ramp);                   // the drive chain reads the ramp too
    ShaperSource sh;
    sh.set_array("values", {-100.0f, -100.0f, 100.0f, 100.0f});   // identity over the test range
    sh.set_param("source", ramp);
    sh.set_param("drive", doubled);
    sh.prepare(h.ctx); sh.start(100);
    h.clock.tick(); CHECK_NEAR(sh.next(), 2.0f, 1e-3f);   // 1 * 2: this sample's ramp on both pins
    h.clock.tick(); CHECK_NEAR(sh.next(), 8.0f, 1e-3f);   // 2 * 4
    h.clock.tick(); CHECK_NEAR(sh.next(), 18.0f, 1e-3f);  // 3 * 6

    // every wired pin is prepared once per note and pulled once per sample
    // (four of the six are wired here; the auto-pin test wires all six)
    ShaperSource full;
    std::vector<std::shared_ptr<PinCounter>> probes;
    for (const char* n : {"source", "drive", "smoothness", "morph"}) {
        probes.push_back(std::make_shared<PinCounter>());
        full.set_param(n, probes.back());
    }
    full.prepare(ctx); full.start(100);
    for (int i = 0; i < 7; ++i) full.next();
    bool each = true;
    for (const auto& p : probes) each = each && p->starts == 1 && p->nexts == 7;
    CHECK(each);
}

// The base advances the pins in declaration order, which is the order the
// descriptors list them in, the shaper's six included; a source wired to both
// inputs of a combiner is advanced once, by the first.
static void run_combine_mod_advance_order_tests() {
    RenderContext ctx{48000};
    auto order = [&](ValueSource& s, std::initializer_list<const char*> names, int skip = 0) {
        std::vector<int> log;
        std::vector<std::shared_ptr<OrderProbe>> probes;
        std::vector<int> expect;
        int id = 0;
        for (const char* n : names) {
            probes.push_back(std::make_shared<OrderProbe>(&log, ++id));
            s.set_param(n, probes.back());
            expect.push_back(id);
        }
        s.prepare(ctx); s.start(100);
        for (int i = 0; i < skip; ++i) s.next();
        log.clear();
        s.next();
        CHECK(log == expect);
    };
    CombinedSource comb(nullptr, nullptr, CombineOp::Add);
    order(comb, {"source1", "source2"});
    DistortedSource dist;
    order(dist, {"source", "density", "gain", "shift", "amplitude"});
    SegmentSource seg(std::vector<float>{2.0f, 1.0f});
    order(seg, {"amplitude", "smoothness", "width", "gap", "gapVarPct", "widthVarPct", "valVarPct"});
    ShaperSource shaper;
    order(shaper, {"source", "drive", "smoothness", "morph", "breakaway", "capture"});
    CrossfadeSource xfade;           // sample 45 of 100 is in the overlap: both sources play
    order(xfade, {"amplitude", "source1", "source2"}, 45);

    auto ramp = std::make_shared<UnitRamp>();
    CombinedSource sq(ramp, ramp, CombineOp::Multiply);   // the square through sharing is the memo at work
    TestHost h;
    sq.prepare(h.ctx); sq.start(100);
    h.clock.tick(); CHECK_NEAR(sq.next(), 1.0f, 1e-6f);
    h.clock.tick(); CHECK_NEAR(sq.next(), 4.0f, 1e-6f);
    h.clock.tick(); CHECK_NEAR(sq.next(), 9.0f, 1e-6f);
}

// The setting and array hooks: the multiplex count is clamped and rebuilds
// the clones only when it changes; the segment source re-derives its units
// from its time mode and its first width; the shaper refuses a malformed
// curve and keeps its segment arrays normalised.
static void run_combine_mod_setting_hook_tests() {
    RenderContext ctx{48000};
    MultiplexSource mux;
    mux.set_setting("count", 99.0f);  CHECK_NEAR(mux.get_setting("count"), 50.0f, 1e-6f);
    mux.set_setting("count", 0.0f);   CHECK_NEAR(mux.get_setting("count"), 1.0f, 1e-6f);
    mux.set_setting("count", 4.0f);   CHECK_NEAR(mux.get_setting("count"), 4.0f, 1e-6f);
    int builds = 0;
    mux.set_template("{}", 0, [&builds](int) {
        ++builds;
        return std::make_pair(std::shared_ptr<ValueSource>(konst(1.0f)), MultiplexSource::ValueNodeMap{});
    });
    mux.prepare(ctx); mux.start(8);                                   CHECK(builds == 4);
    mux.prepare(ctx); mux.start(8);                                   CHECK(builds == 4);
    mux.set_setting("count", 4.0f);  mux.prepare(ctx); mux.start(8);  CHECK(builds == 4);
    mux.set_setting("count", 6.0f);  mux.prepare(ctx); mux.start(8);  CHECK(builds == 10);
    mux.mark_dirty();                mux.prepare(ctx); mux.start(8);  CHECK(builds == 16);

    // a 4-sample ramp: the first width is inferred to be seconds (< 1) at sr 1000
    auto ramp4 = [&](SegmentSource& s) {
        s.prepare(RenderContext{1000}); s.start(100);
        std::vector<float> out;
        for (int i = 0; i < 6; ++i) out.push_back(s.next());
        return out;
    };
    SegmentSource seg(std::vector<float>{4.0f, 1.0f}, true);
    seg.set_array("values", {0.004f, 1.0f});
    auto secs = ramp4(seg);
    CHECK_NEAR(secs[0], 0.25f, 1e-5f);
    CHECK_NEAR(secs[3], 1.0f, 1e-5f);
    CHECK(secs[4] == 0.0f && secs[5] == 0.0f);
    seg.set_setting("timeMode", 1.0f);                     // samples: 0.004 rounds to no width
    auto samples = ramp4(seg);
    CHECK(samples[0] == 0.0f && samples[3] == 0.0f);
    seg.set_setting("timeMode", 2.0f);
    CHECK_NEAR(ramp4(seg)[3], 1.0f, 1e-5f);
    seg.set_setting("timeMode", 9.0f);  CHECK_NEAR(seg.get_setting("timeMode"), 2.0f, 1e-6f);
    seg.set_setting("timeMode", -3.0f); CHECK_NEAR(seg.get_setting("timeMode"), 0.0f, 1e-6f);
    seg.set_setting("oneShot", 0.0f);
    CHECK(seg.get_array("values").size() == 2);            // a setting edit keeps the shape

    ShaperSource sh;
    const std::vector<float> identity{-1.0f, -1.0f, 1.0f, 1.0f};
    sh.set_array("values", {0.0f, 0.0f, 1.0f});            // odd length: refused
    CHECK(sh.get_array("values") == identity);
    sh.set_array("values", {0.0f, 0.0f});                  // under two points: refused
    CHECK(sh.get_array("values") == identity);
    sh.set_array("values", {0.0f, 0.0f, 1.0f, 2.0f});
    CHECK(sh.get_array("values").size() == 4 && sh.get_array("values")[3] == 2.0f);
    sh.set_array("values2", {0.0f, 0.0f, 1.0f, 1.0f});
    sh.set_array("values2", {1.0f, 2.0f, 3.0f});           // refused: curve B stays
    CHECK(sh.get_array("values2").size() == 4);
    sh.set_array("values2", {});                           // empty clears curve B
    CHECK(sh.get_array("values2").empty());
    sh.set_array("segs", {0.0f, 0.0f, 9.0f, 5.0f});        // no valid override: normalises to empty
    CHECK(sh.get_array("segs").empty());
    sh.set_array("segs2", {0.0f, 0.0f, 2.0f, 2.0f});
    CHECK(sh.get_array("segs2").size() == 4);
}

// The pins are read in compute(): the combiner's four operations, the
// crossfade's three regions, the distortion's clip and density, the static
// sources' held value, the one-shot segment, the phased first stage.
static void run_combine_mod_behavior_tests() {
    RenderContext ctx{48000};
    CombinedSource c(konst(0.5f), konst(0.25f), CombineOp::Add, 0.2f);
    c.prepare(ctx); c.start(100);
    CHECK_NEAR(c.next(), (0.5f + 0.25f * 1.2f) * 0.5f, 1e-6f);
    c.set_setting("operation", 1.0f);
    CHECK_NEAR(c.next(), 0.5f * 0.25f * 1.2f, 1e-6f);
    c.set_setting("operation", 3.0f);
    CHECK_NEAR(c.next(), 0.5f + 0.25f * 1.2f, 1e-6f);
    c.set_setting("operation", 2.0f);
    c.prepare(ctx); c.start(100);
    CHECK_NEAR(c.next(), 0.5f, 1e-6f);                                     // fade starts at source1
    CHECK_NEAR(c.next(), 0.5f * 0.99f + 0.25f * 1.2f * 0.01f, 1e-6f);

    // crossfade: source1 alone, an overlap where both play, source2 alone
    CrossfadeSource xf;
    xf.set_param("source1", konst(1.0f));
    xf.set_param("source2", konst(2.0f));
    xf.set_param("amplitude", konst(0.5f));
    xf.prepare(ctx); xf.start(100);
    std::vector<float> out;
    for (int i = 0; i < 100; ++i) out.push_back(xf.next());
    CHECK_NEAR(out[0], 0.5f, 1e-6f);
    CHECK_NEAR(out[50], 0.75f, 1e-6f);
    CHECK_NEAR(out[99], 1.0f, 1e-6f);

    // distortion: gain scales and clips; density 0 is silence
    auto in = konst(0.3f);
    DistortedSource d;
    d.set_param("source", in);
    d.set_param("gain", konst(0.0f));
    CHECK_NEAR(d.next(), 0.3f, 1e-6f);
    d.set_param("gain", konst(1.0f));
    CHECK_NEAR(d.next(), 0.6f, 1e-6f);
    in->set(-0.8f);
    CHECK_NEAR(d.next(), -1.0f, 1e-6f);
    d.set_param("density", konst(0.0f));
    bool silent = true;
    for (int i = 0; i < 20; ++i) silent = silent && d.next() == 0.0f;
    CHECK(silent);

    // the static sources hold one value per note, from prepare on
    StaticVarSource sv(2.0f, 0.0f);
    sv.prepare(ctx); sv.start(10);
    CHECK_NEAR(sv.current(), 2.0f, 1e-6f);
    CHECK_NEAR(sv.next(), 2.0f, 1e-6f);
    StaticRangeSource sr(0.2f, 0.4f, 0.3f);
    sr.prepare(ctx); sr.start(10);
    const float held = sr.current();
    CHECK(held >= 0.2f && held <= 0.4f && sr.next() == held && sr.next() == held);

    // a one-shot segment ramps over its width, scaled by amplitude, then stops
    SegmentSource once(std::vector<float>{4.0f, 1.0f}, true);
    once.set_param("amplitude", konst(2.0f));
    once.prepare(ctx); once.start(100);
    CHECK_NEAR(once.next(), 0.5f, 1e-5f);
    CHECK_NEAR(once.next(), 1.0f, 1e-5f);
    CHECK_NEAR(once.next(), 1.5f, 1e-5f);
    CHECK_NEAR(once.next(), 2.0f, 1e-5f);
    CHECK(once.next() == 0.0f);

    // the phased source plays its first stage at its gain and amplitude
    PhasedValueSource phased(0.05f);
    phased.add_stage({konst(1.0f), 0.5f, 0.0f, 0.0f, 1.0f});
    phased.add_stage({konst(2.0f), 0.0f, 0.0f, 0.0f, 1.0f});
    phased.set_param("amplitude", konst(0.5f));
    phased.prepare(ctx); phased.start(4800);
    CHECK_NEAR(phased.next(), 0.5f, 1e-6f);
}

// The shaper sticks until the input passes the breakaway, slips along the
// second curve, and sticks again on a sign reversal.
static void run_combine_mod_hysteresis_tests() {
    RenderContext ctx{48000};
    ShaperSource sh;
    auto in = konst(0.2f);
    sh.set_param("source", in);
    sh.set_array("values2", {-1.0f, 0.0f, 1.0f, 0.0f});    // the slip curve: flat zero
    sh.set_setting("hysteresis", 1.0f);
    sh.prepare(ctx); sh.start(100);
    CHECK_NEAR(sh.next(), 0.2f, 1e-5f);                    // under the 0.6 breakaway: curve A
    in->set(0.9f);
    CHECK_NEAR(sh.next(), 0.0f, 1e-5f);                    // past it: curve B
    in->set(0.5f);
    CHECK_NEAR(sh.next(), 0.0f, 1e-5f);                    // still slipping
    in->set(-0.5f);
    CHECK_NEAR(sh.next(), -0.5f, 1e-5f);                   // sign reversal: stuck again
}

// The configurators set the migrated settings from the patch JSON, in the
// forms the loader accepts.
static void run_combine_mod_configurator_tests() {
    register_all_sources();
    auto& reg = SourceRegistry::instance();
    const ResolveParamFn none = [](const nlohmann::json&) { return std::shared_ptr<ValueSource>(); };
    auto configure = [&](const char* type, const char* json) {
        auto src = reg.create(type);
        (*reg.get_configurator(type))(*src, nlohmann::json::parse(json), none);
        return src;
    };
    auto comb = configure("CombinedSource", R"({"operation":"multiply","gainAdj":0.5})");
    CHECK_NEAR(comb->get_setting("operation"), 1.0f, 1e-6f);
    CHECK_NEAR(comb->get_setting("gainAdj"), 0.5f, 1e-6f);
    comb = configure("CombinedSource", R"({"operation":3})");
    CHECK_NEAR(comb->get_setting("operation"), 3.0f, 1e-6f);
    CHECK_NEAR(comb->get_setting("gainAdj"), 0.0f, 1e-6f);
    comb = configure("CombinedSource", R"({"operation":"mix"})");
    CHECK_NEAR(comb->get_setting("operation"), 0.0f, 1e-6f);
    bool threw = false;
    try { configure("CombinedSource", R"({"operation":9})"); }
    catch (const std::runtime_error&) { threw = true; }
    CHECK(threw);

    auto xf = configure("CrossfadeSource", R"({"ratio":0.7,"overlap":0.2,"gainAdj":0.1})");
    CHECK_NEAR(xf->get_setting("ratio"), 0.7f, 1e-6f);
    CHECK_NEAR(xf->get_setting("overlap"), 0.2f, 1e-6f);
    CHECK_NEAR(xf->get_setting("gainAdj"), 0.1f, 1e-6f);

    auto rep = configure("RepeatingSource", R"({"duration":0.25,"durVarPct":0.1,"gap":0.4,"gapVarPct":0.3})");
    CHECK_NEAR(rep->get_setting("duration"), 0.25f, 1e-6f);
    CHECK_NEAR(rep->get_setting("durVarPct"), 0.1f, 1e-6f);
    CHECK_NEAR(rep->get_setting("gapDuration"), 0.4f, 1e-6f);
    CHECK_NEAR(rep->get_setting("gapVarPct"), 0.3f, 1e-6f);
}

// The combiner/modulator family, in registration order.
static void run_combine_mod_tests() {
    run_combine_mod_pin_shape_tests();
    run_combine_mod_category_tests();
    run_combine_mod_default_tests();
    run_combine_mod_auto_pin_tests();
    run_combine_mod_manual_pin_tests();
    run_combine_mod_shaper_timing_tests();
    run_combine_mod_advance_order_tests();
    run_combine_mod_setting_hook_tests();
    run_combine_mod_behavior_tests();
    run_combine_mod_hysteresis_tests();
    run_combine_mod_configurator_tests();
}

// ---------------------------------------------------------------------------
// prepare(ctx) / start(frames): the two host calls
// ---------------------------------------------------------------------------
#include <functional>

// The split is exact when a node that is prepared once and started per note
// renders the same bytes as one that is prepared and started before every
// note (what the old prepare(ctx, frames) did). Both arms throw or neither;
// a comparison of silence proves nothing, so the result says whether the
// render had any signal in it.
struct SplitResult { bool equal; bool threw; bool nonzero; };

static SplitResult split_compare(const std::function<std::shared_ptr<ValueSource>()>& make,
                                 int frames = 2048, int notes = 3, int sampleRate = 48000) {
    RenderContext ctx{sampleRate};
    auto a = make(), b = make();
    std::vector<float> ra, rb;
    auto render = [&](ValueSource& s, std::vector<float>& out, bool prepareEachNote) {
        if (!prepareEachNote) s.prepare(ctx);
        for (int n = 0; n < notes; ++n) {
            if (prepareEachNote) s.prepare(ctx);
            s.start(frames);
            for (int i = 0; i < frames; ++i) out.push_back(s.next());
        }
    };
    bool threwA = false, threwB = false;
    try { render(*a, ra, true);  } catch (...) { threwA = true; }
    try { render(*b, rb, false); } catch (...) { threwB = true; }
    if (threwA || threwB) return {threwA == threwB, true, false};
    const bool equal = ra.size() == rb.size() &&
                       std::memcmp(ra.data(), rb.data(), ra.size() * sizeof(float)) == 0;
    const bool nonzero = std::any_of(rb.begin(), rb.end(), [](float v) { return v != 0.0f; });
    return {equal, false, nonzero};
}

static bool split_equivalent(const std::function<std::shared_ptr<ValueSource>()>& make,
                             int frames = 2048, int notes = 3, int sampleRate = 48000) {
    return split_compare(make, frames, notes, sampleRate).equal;
}

static void run_split_equivalence_tests() {
    register_all_sources();
    auto& reg = SourceRegistry::instance();
    // Types that cannot render at their registry defaults at all (they
    // index an empty part). Their wired cases live in their family's tests.
    static const std::set<std::string> kNotRenderableUnwired = {
        "PhasedValueSource",   // stage-less: compute() indexes an empty boundaries_
    };
    for (auto& [name, cat] : reg.registered_types()) {
        const std::string n = name;
        if (kNotRenderableUnwired.count(n)) continue;
        const SplitResult r = split_compare([&] { return reg.create(n); });
        if (!r.equal) std::printf("  split differs: %s\n", n.c_str());
        CHECK(r.equal);
        if (r.threw) std::printf("  (both arms threw at the defaults: %s)\n", n.c_str());
        // A sound source at its defaults makes sound; the comparison above
        // was of a real signal for these. Processors and parts read 0 unwired,
        // and so do the sources that need an excitation, a part or a shape
        // wired in; those have wired cases in their family's tests.
        static const std::set<std::string> kSilentUntilWired = {
            "KSString", "KSPianoString", "AllpassResonator", "DelayLine", "Mesh2D",   // excited by `source`
            "PluckEvolution", "AveragingEvolution",                                   // parts read by WavetableSource
            "SegmentSource",                                                          // an empty shape
        };
        if ((cat == SourceCategory::Oscillator || cat == SourceCategory::Generator)
            && !kSilentUntilWired.count(n)) {
            if (!r.threw && !r.nonzero) std::printf("  silent at its defaults: %s\n", n.c_str());
            CHECK(r.threw || r.nonzero);
        }
    }
    // A node knows no rate until a host prepares it.
    struct Probe final : Generator {
        float compute() override { return 0.0f; }
        bool p() const { return prepared(); }
        int  r() const { return sample_rate(); }
    };
    Probe p;
    CHECK(!p.p());
    CHECK(p.r() == 0);
    p.prepare(RenderContext{22050});
    CHECK(p.p());
    CHECK(p.r() == 22050);
    // The context the host gave an object survives assignment, like the
    // registry: a node assigning itself assigns its own state only.
    Probe q;
    q = p;
    CHECK(q.r() == 0);
    p = q;
    CHECK(p.r() == 22050);
}

// ---------------------------------------------------------------------------
// Core family: the rate arrives through prepare(ctx), the note through
// start(frames) (WaveSource, Envelope and presets, FM, Multi).
// ---------------------------------------------------------------------------
static std::vector<float> render_samples(ValueSource& s, int n) {
    std::vector<float> out;
    out.reserve(size_t(n));
    for (int i = 0; i < n; ++i) out.push_back(s.next());
    return out;
}

static bool same_bits(const std::vector<float>& a, const std::vector<float>& b) {
    return a.size() == b.size() &&
           std::memcmp(a.data(), b.data(), a.size() * sizeof(float)) == 0;
}

static void run_core_split_envelope_tests() {
    // A preset keeps the context it was prepared with through a setting
    // change: the rebuild assigns a fresh, unprepared envelope over it.
    {
        AREnvelope e;
        e.prepare(RenderContext{22050});
        e.set_setting("attack", 0.5f);
        e.start(22050);
        AREnvelope f;
        f.set_setting("attack", 0.5f);
        f.prepare(RenderContext{22050});
        f.start(22050);
        const auto ra = render_samples(e, 22050), rb = render_samples(f, 22050);
        CHECK(same_bits(ra, rb));
        CHECK(ra[5000] > 0.0f);                      // rendered, not the silent unprepared path
    }
    // prepare(ctx) twice with the same context changes nothing a render sees.
    {
        auto a = Envelope::make_adsr(0.2f, 0.1f, 0.7f, 0.0f);
        auto b = Envelope::make_adsr(0.2f, 0.1f, 0.7f, 0.0f);
        a->prepare(RenderContext{48000});
        a->prepare(RenderContext{48000});
        a->start(4800);
        b->prepare(RenderContext{48000});
        b->start(4800);
        CHECK(same_bits(render_samples(*a, 4800), render_samples(*b, 4800)));
    }
    // A stage added after prepare is counted by the next start with no
    // second prepare; a removed one is dropped from the counts.
    {
        auto grown = std::make_shared<Envelope>();
        grown->add_stage({{0.0f, 1.0f, RampType::Linear, 0.0f}, 0.5f, 0.0f, 0.0f});
        grown->prepare(RenderContext{48000});
        grown->add_stage({{1.0f, 0.0f, RampType::Linear, 0.0f}, 0.0f, 0.0f, 0.0f});
        grown->start(2400);
        auto fresh = std::make_shared<Envelope>();
        fresh->add_stage({{0.0f, 1.0f, RampType::Linear, 0.0f}, 0.5f, 0.0f, 0.0f});
        fresh->add_stage({{1.0f, 0.0f, RampType::Linear, 0.0f}, 0.0f, 0.0f, 0.0f});
        fresh->prepare(RenderContext{48000});
        fresh->start(2400);
        CHECK(same_bits(render_samples(*grown, 2400), render_samples(*fresh, 2400)));
        grown->remove_stage(1);
        grown->start(2400);
        auto one = std::make_shared<Envelope>();
        one->add_stage({{0.0f, 1.0f, RampType::Linear, 0.0f}, 0.5f, 0.0f, 0.0f});
        one->prepare(RenderContext{48000});
        one->start(2400);
        CHECK(same_bits(render_samples(*grown, 2400), render_samples(*one, 2400)));
    }
    // prepare(ctx) again on a node that has already played a note leaves its
    // next note as it would have been: nothing per-note lives in on_prepare.
    {
        auto a = Envelope::make_adsr(0.2f, 0.1f, 0.7f, 0.0f);
        auto b = Envelope::make_adsr(0.2f, 0.1f, 0.7f, 0.0f);
        a->prepare(RenderContext{48000});
        b->prepare(RenderContext{48000});
        a->start(4800); render_samples(*a, 1000);
        b->start(4800); render_samples(*b, 1000);
        a->prepare(RenderContext{48000});
        a->start(4800);
        b->start(4800);
        CHECK(same_bits(render_samples(*a, 4800), render_samples(*b, 4800)));
    }
}

static void run_core_split_tests() {
    run_core_split_envelope_tests();
    // FM with oversampling: the decimation sections are built at prepare and
    // reset per note; the sweep covers the default (oversample 1).
    CHECK(split_equivalent([] {
        auto f = std::make_shared<FMSource>();
        f->set_setting("oversample", 2.0f);
        return f;
    }));
    // The sections are fresh at every note, as the per-note rebuild left
    // them: a reused node's second note is a fresh node's first note.
    {
        FMSource reused, fresh;
        reused.set_setting("oversample", 2.0f);
        fresh.set_setting("oversample", 2.0f);
        reused.prepare(RenderContext{48000});
        fresh.prepare(RenderContext{48000});
        reused.start(1024); render_samples(reused, 777);
        reused.start(1024);
        fresh.start(1024);
        CHECK(same_bits(render_samples(reused, 1024), render_samples(fresh, 1024)));
    }
    // A setting changed after prepare rebuilds the sections; the node keeps
    // its context.
    {
        FMSource a;
        a.prepare(RenderContext{48000});
        a.set_setting("oversample", 4.0f);
        a.start(1024);
        FMSource b;
        b.set_setting("oversample", 4.0f);
        b.prepare(RenderContext{48000});
        b.start(1024);
        CHECK(same_bits(render_samples(a, 1024), render_samples(b, 1024)));
    }
    // MultiSource: an oscillator and an envelope, one delayed.
    CHECK(split_equivalent([] {
        auto m = std::make_shared<MultiSource>();
        m->add(std::make_shared<SineSource>(), 1.0f, 0);
        m->add(Envelope::make_ar(0.3f), 0.5f, 100);
        return m;
    }));
    // The oscillators read the rate from the context: the same frequency at
    // half the rate advances the phase twice as far per sample.
    {
        SineSource lo, hi;
        lo.prepare(RenderContext{24000});
        hi.prepare(RenderContext{48000});
        lo.start(8);
        hi.start(8);
        const auto a = render_samples(lo, 8), b = render_samples(hi, 8);
        CHECK(!same_bits(a, b));
        CHECK_NEAR(a[1], std::sin(2.0f * 3.14159265358979f * 440.0f / 24000.0f), 1e-4f);
        CHECK_NEAR(b[1], std::sin(2.0f * 3.14159265358979f * 440.0f / 48000.0f), 1e-4f);
    }
}

// ---------------------------------------------------------------------------
// Noise family: constructors lose the rate; RedNoise::reset() is the
// constructed state (Vibrato's per-note fresh chain).
// ---------------------------------------------------------------------------
static void run_noise_split_tests() {
    // reset() returns a played RedNoise to the state of a new one, draw
    // stream included. Every ramp carries a value and the continuity and
    // sign rules are live, so each field reset() touches reaches the output;
    // two rounds at different play lengths so the state exposed differs.
    {
        auto make_red = [] {
            auto r = std::make_shared<RedNoiseSource>(0x51EEDu);
            r->density.set(std::make_shared<ConstantSource>(1.0f));
            r->zeroCrossTendency.set(std::make_shared<ConstantSource>(0.5f));
            r->continuity.set(std::make_shared<ConstantSource>(0.5f));
            r->prepare(RenderContext{48000});
            return r;
        };
        auto used = make_red();
        for (int played : {50, 173}) {
            used->start(100);
            render_samples(*used, played);
            used->reset();
            used->start(100);
            auto fresh = make_red();
            fresh->start(100);
            const auto a = render_samples(*used, 400), b = render_samples(*fresh, 400);
            CHECK(same_bits(a, b));
            int nonzero = 0;
            for (float v : a) nonzero += (v != 0.0f);
            CHECK(nonzero > 300);                 // the comparison is of a real signal
        }
    }
    // The walks leave `amplitude` Manual: the node prepares and starts it.
    CHECK(split_equivalent([] {
        auto w = std::make_shared<WanderNoiseSource>();
        w->amplitude.set(std::make_shared<ConstantSource>(0.5f));
        return w;
    }));
    CHECK(split_equivalent([] {
        auto w = std::make_shared<WanderNoise2Source>();
        w->amplitude.set(std::make_shared<ConstantSource>(0.5f));
        return w;
    }));
}

// ---------------------------------------------------------------------------
// Wavetable family: the seed noise, the evolution and the speed pin are
// Manual, so the node prepares and starts them; HybridKS sizes its target
// array in prepare and again when numPartials changes afterwards.
// ---------------------------------------------------------------------------
static void run_wavetable_split_tests() {
    // The loader's shape: seed noise in inputSource, an evolution holder in
    // evolution; then the interpolating read; then holders with Manual pins.
    for (bool interp : {false, true}) {
        CHECK(split_equivalent([interp] {
            auto w = std::make_shared<WavetableSource>();
            w->set_param("inputSource", std::make_shared<WhiteNoiseSource>(0x1234u));
            w->set_param("evolution", std::make_shared<PluckEvolutionSource>());
            w->set_setting("interpolate", interp ? 1.0f : 0.0f);
            return w;
        }));
    }
    CHECK(split_equivalent([] {
        auto w = std::make_shared<WavetableSource>();
        auto reed = std::make_shared<ReedEvolutionSource>();
        reed->set_param("breath", std::make_shared<ConstantSource>(0.6f));
        w->set_param("evolution", reed);
        w->set_param("speedFactor", std::make_shared<ConstantSource>(1.5f));
        return w;
    }));
    CHECK(split_equivalent([] {
        auto w = std::make_shared<WavetableSource>();
        auto bit = std::make_shared<BitRotateEvolutionSource>();
        bit->set_param("shiftBits", std::make_shared<ConstantSource>(2.0f));
        w->set_param("evolution", bit);
        return w;
    }));
    // An evolution that sizes its lines from the rate learns it through the
    // node's prepare: the same patch at half the rate is a different render.
    {
        auto bowed_at = [](int rate) {
            WavetableSource w;
            auto bow = std::make_shared<BowedStringEvolutionSource>();
            bow->set_param("bow", std::make_shared<ConstantSource>(0.5f));
            w.set_param("evolution", bow);
            w.set_param("inputSource", std::make_shared<WhiteNoiseSource>(0x77u));
            w.prepare(RenderContext{rate});
            w.start(2048);
            return render_samples(w, 2048);
        };
        const auto lo = bowed_at(24000), hi = bowed_at(48000);
        CHECK(!same_bits(lo, hi));
        CHECK(std::all_of(hi.begin(), hi.end(), [](float v) { return std::isfinite(v); }));
    }
}

static void run_hybrid_ks_split_tests() {
    // HybridKS: numPartials changed after prepare must reach the target array
    // (12 shrinks the loop; 40 outgrows the 20 targets set below).
    for (float partials : {12.0f, 40.0f}) {
        auto make = [] {
            auto h = std::make_shared<HybridKSSource>();
            h->set_setting("holdCycles", 2.0f);
            h->set_setting("morphDuration", 0.02f);
            h->set_target_partials(std::vector<float>(20, 0.05f));
            return h;
        };
        const RenderContext ctx{48000};
        auto late = make();
        late->prepare(ctx);
        late->set_setting("numPartials", partials);
        CHECK(late->target_count() >= int(partials));   // the setting hook grew the array
        auto early = make();
        early->set_setting("numPartials", partials);
        early->prepare(ctx);
        std::vector<float> ra, rb;
        for (int note = 0; note < 2; ++note) {
            late->start(4800);
            early->start(4800);
            const auto a = render_samples(*late, 4800), b = render_samples(*early, 4800);
            ra.insert(ra.end(), a.begin(), a.end());
            rb.insert(rb.end(), b.begin(), b.end());
        }
        CHECK(same_bits(ra, rb));
        CHECK(std::all_of(ra.begin(), ra.end(), [](float v) { return std::isfinite(v); }));
    }
    // And the plain split, with the default numPartials.
    CHECK(split_equivalent([] {
        auto h = std::make_shared<HybridKSSource>();
        h->set_setting("holdCycles", 2.0f);
        return h;
    }));
}

// ---------------------------------------------------------------------------
// Additive family: the rate reaches the parts through partials_prepare and
// fmt_prepare; the note counter, the per-note streams and the envelope
// layouts run in partials_start and fmt_start.
// ---------------------------------------------------------------------------
static std::shared_ptr<ExplicitPartials> make_split_partials() {
    auto p = std::make_shared<ExplicitPartials>();
    p->set_array("mult2", {1.0f, 2.1f, 3.2f});
    p->set_array("ampl2", {0.5f, 0.4f, 0.3f});
    p->set_array("ampl1", {1.0f, 0.5f, 0.25f});
    p->set_array("mult1", {1.0f, 2.0f, 3.0f});
    p->set_setting("bandwidth1", 0.4f);
    p->set_setting("bandwidthHz", 2.0f);        // the per-note noise streams
    p->set_setting("motionDepth1", 6.0f);       // the walks are sized from the rate
    p->set_param("multEnv", Envelope::make_ar(0.3f));
    return p;
}

static std::shared_ptr<FullAdditiveSource> make_split_full(std::shared_ptr<ValueSource> formant) {
    auto f = std::make_shared<FullAdditiveSource>();
    f->set_param("partials", make_split_partials());
    if (formant) {
        f->set_param("formant", std::move(formant));
        f->set_param("formantWeight", std::make_shared<ConstantSource>(0.7f));
    }
    f->set_setting("noiseBedLevel", 0.3f);      // the bed's coefficients come from the rate
    f->set_setting("noiseBedDelay", 0.01f);
    return f;
}

static std::shared_ptr<Formant> make_split_formant(float hz) {
    auto fm = std::make_shared<Formant>();
    fm->set_param("frequency", std::make_shared<ConstantSource>(hz));
    fm->set_param("width", std::make_shared<ConstantSource>(900.0f));
    return fm;
}

static std::shared_ptr<AdditiveSource2> make_split_additive2(bool evolving) {
    auto a = std::make_shared<AdditiveSource2>();
    a->set_param("frequency", std::make_shared<ConstantSource>(220.0f));
    a->set_param("freqVarDepth", std::make_shared<ConstantSource>(0.02f));
    a->set_param("freqVarSpeed", std::make_shared<ConstantSource>(3.0f));
    a->set_param("amplVarDepth", std::make_shared<ConstantSource>(0.1f));
    a->set_param("amplVarSpeed", std::make_shared<ConstantSource>(2.0f));
    if (evolving) {
        a->set_partials({1.0f, 2.0f, 3.0f}, {1.0f, 0.5f, 0.25f}, {1.0f, 2.1f, 3.2f}, {0.5f, 0.4f, 0.3f});
        a->assign_freq_envelope(Envelope::make_ar(0.3f), AdditiveSource2::PartialFilter::All, 1, 3);
        a->assign_ampl_envelope(Envelope::make_ar(0.5f), AdditiveSource2::PartialFilter::Odd, 1, 3);
    } else {
        a->set_default_partials(40);
        a->assign_ampl_envelope(Envelope::make_ar(0.4f), AdditiveSource2::PartialFilter::All, 1, 40);
    }
    return a;
}

static void run_additive_split_tests() {
    // The note counter advances at start, never at prepare: a node prepared
    // before every note renders what one prepared once does, across four notes.
    CHECK(split_equivalent([] { return make_split_full(nullptr); }, 2048, 4));
    CHECK(split_equivalent([] { return make_split_full(make_split_formant(800.0f)); }, 2048, 4));
    CHECK(split_equivalent([] {
        auto spec = std::make_shared<FormantSpectrum>();
        spec->formants.push_back(make_split_formant(500.0f));
        spec->formants.push_back(make_split_formant(1500.0f));
        return make_split_full(spec);
    }, 2048, 4));
    CHECK(split_equivalent([] {
        auto seq = std::make_shared<FormantSequence>();
        seq->set_param("blend", Envelope::make_ar(0.5f));
        for (float hz : {400.0f, 1200.0f}) {
            auto spec = std::make_shared<FormantSpectrum>();
            spec->formants.push_back(make_split_formant(hz));
            seq->add_param("spectra", spec);
        }
        return make_split_full(seq);
    }, 2048, 4));
    CHECK(split_equivalent([] {
        auto comp = std::make_shared<CompositePartials>();
        comp->add_param("partials", make_split_partials());
        comp->add_param("partials", std::make_shared<FullPartials>(0x51u));
        auto f = std::make_shared<FullAdditiveSource>();
        f->set_param("partials", comp);
        return f;
    }, 2048, 4));
    CHECK(split_equivalent([] {
        auto b = std::make_shared<BasicAdditiveSource>();
        b->set_param("frequency", std::make_shared<ConstantSource>(220.0f));
        b->set_param("freqVarPct", std::make_shared<ConstantSource>(0.1f));
        b->set_param("freqVarSpeed", std::make_shared<ConstantSource>(5.0f));
        b->set_param("amplVarPct", std::make_shared<ConstantSource>(0.1f));
        b->set_param("amplVarSpeed", std::make_shared<ConstantSource>(3.0f));
        return b;
    }, 2048, 4));
    for (bool evolving : {false, true})
        CHECK(split_equivalent([evolving] { return make_split_additive2(evolving); }, 2048, 4));

    // The rate reaches the partials and the bed only through prepare(ctx).
    auto at = [](int rate) {
        auto f = make_split_full(make_split_formant(800.0f));
        f->prepare(RenderContext{rate});
        f->start(2048);
        return render_samples(*f, 2048);
    };
    const auto lo = at(24000), hi = at(48000);
    CHECK(!same_bits(lo, hi));
    CHECK(std::all_of(hi.begin(), hi.end(), [](float v) { return std::isfinite(v); }));
    CHECK(std::any_of(hi.begin(), hi.end(), [](float v) { return v != 0.0f; }));
}

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
// in float: the tap lands on the one slot past the ring, which stays zero.
static void run_delay_line_zero_slot_tests() {
    DelayLineSource dl;
    dl.set_param("source", std::make_shared<ConstantSource>(1.0f));
    dl.set_param("frequency", std::make_shared<ConstantSource>(std::nextafter(1470.0f, 0.0f)));
    dl.prepare(RenderContext{22050});
    dl.start(64);
    const auto out = render_samples(dl, 32);
    CHECK(out[15] == 0.0f);              // write index 15, delay 15.000001: the zero slot
    CHECK(out[14] == 0.0f);              // still the empty ring
    CHECK_NEAR(out[16], 1.0f, 1e-4f);    // the ring's own sample again
    CHECK(std::all_of(out.begin(), out.end(), [](float v) { return std::isfinite(v); }));
    // Past one full ring the write index has wrapped: the slot was not
    // written, so the stray read at the same phase is still silence.
    const int ring = dl.capacity();
    const auto more = render_samples(dl, ring);      // samples 32 .. 32 + ring
    CHECK(more[size_t(ring + 15 - 32)] == 0.0f);     // write index 15 of the second lap
    CHECK_NEAR(more[size_t(ring + 16 - 32)], 1.0f, 1e-4f);
}

static void run_physical_split_tests() {
    const auto noise = [] { return std::make_shared<WhiteNoiseSource>(0x5EEDu); };
    CHECK(split_equivalent([&] {
        auto k = std::make_shared<KSString>();
        k->set_param("source", noise());
        return k;
    }));
    {   // the loading alias builds the same type; excited, it sounds
        const SplitResult r = split_compare([&] {
            auto k = SourceRegistry::instance().create("KSPianoString");
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
            auto s = SourceRegistry::instance().create(type);
            s->set_param("source", std::make_shared<SineSource>());
            return s;
        }));
    }
    run_shaper_pin_split_tests();
    run_delay_line_zero_slot_tests();

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
    if (name == "BWLowpass")       n = std::make_shared<BWLowpassFilter>(2);
    else if (name == "BWHighpass") n = std::make_shared<BWHighpassFilter>(2);
    else if (name == "BWBandpass") n = std::make_shared<BWBandpassFilter>(2);
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

// A node used at one rate and then prepared at another renders what a node
// prepared at the second rate renders: nothing sized or derived from the
// first rate survives.
static bool follows_rate(const std::function<std::shared_ptr<ValueSource>()>& make,
                         int first, int second) {
    auto used = make(), fresh = make();
    used->prepare(RenderContext{first});
    used->start(4096);
    render_samples(*used, 500);
    used->prepare(RenderContext{second});
    used->start(4096);
    fresh->prepare(RenderContext{second});
    fresh->start(4096);
    return same_bits(render_samples(*used, 4096), render_samples(*fresh, 4096));
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

static void run_filter_split_tests() {
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
static void run_vibrato_split_tests() {
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

// ---------------------------------------------------------------------------
// Combiners and modulators: the rate arrives through prepare(ctx), the note
// through start(frames). The crossfade, repeating and phased sources own their
// sub-sources and prepare and start each themselves, and the multiplex
// prepares its clones; a constant on a pin or a stage cannot show a missing
// call, so those are PinCounters that record the rate and the calls.
// ---------------------------------------------------------------------------
static std::shared_ptr<SineSource> sine_at(float hz) {
    auto s = std::make_shared<SineSource>();
    s->set_param("frequency", std::make_shared<ConstantSource>(hz));
    return s;
}

// The voice source of a loaded patch whose output is a three-clone multiplex
// over a two-node template (a sine under an AR envelope).
static std::shared_ptr<ValueSource> make_multiplex_voice() {
    auto ip = load_instrument_patch_json(R"({
      "sampleRate": 48000,
      "instrument": { "polyphony": 1 },
      "graph": { "output": "mux", "nodes": [
        { "id": "env", "type": "AREnvelope", "params": { "attack": 0.2 } },
        { "id": "osc", "type": "SineSource",
          "params": { "frequency": 220.0, "amplitude": { "ref": "env" } } },
        { "id": "mux", "type": "MultiplexSource",
          "params": { "count": 3, "source": { "ref": "osc" } } } ] }
    })");
    auto* pi = dynamic_cast<PitchedInstrument*>(ip.instrument.get());
    return pi->voicePool.at(0).source;
}

static void run_combine_mod_pin_split_tests() {
    const RenderContext ctx{48000};

    CrossfadeSource xf;
    auto s1 = std::make_shared<PinCounter>(), s2 = std::make_shared<PinCounter>();
    xf.set_param("source1", s1);
    xf.set_param("source2", s2);
    xf.prepare(ctx);
    CHECK(s1->rate == 48000 && s2->rate == 48000);
    CHECK(s1->starts == 0 && s2->starts == 0);           // prepared, not started
    xf.start(100);
    CHECK(s1->starts == 1 && s2->starts == 1);           // started once
    CHECK(s1->ctxPrepares == 1 && s2->ctxPrepares == 1);

    RepeatingSource rep;
    rep.set_setting("duration", 0.05f);
    rep.set_setting("gapDuration", 0.01f);
    auto kid = std::make_shared<PinCounter>();
    rep.set_param("source", kid);
    rep.prepare(ctx);
    CHECK(kid->rate == 48000 && kid->starts == 0);
    rep.start(9600);
    CHECK(kid->starts == 1);                               // the first repetition
    render_samples(rep, 9600);
    // A repetition boundary starts the child and never prepares it.
    CHECK(kid->ctxPrepares == 1);
    CHECK(kid->starts == 4);
    CHECK(kid->nexts > 3 * 2400 && kid->nexts < 4 * 2400);

    PhasedValueSource ph(0.05f);
    auto st1 = std::make_shared<PinCounter>(), st2 = std::make_shared<PinCounter>();
    ph.add_stage({st1, 0.5f, 0.0f, 0.0f, 1.0f});
    ph.add_stage({st2, 0.0f, 0.0f, 0.0f, 1.0f});
    ph.prepare(ctx);
    CHECK(st1->rate == 48000 && st2->rate == 48000);
    CHECK(st1->starts == 0 && st2->starts == 0);
    ph.start(4800);
    CHECK(st1->starts == 1 && st2->starts == 1);
    CHECK(st1->ctxPrepares == 1 && st2->ctxPrepares == 1);

    // The multiplex prepares the clones its builder returns unprepared, builds
    // them once, and starts them per note; unbuilt, it prepares its input.
    MultiplexSource mux;
    mux.set_setting("count", 3.0f);
    std::vector<std::shared_ptr<PinCounter>> clones;
    mux.set_template("{}", 0, [&clones](int) {
        clones.push_back(std::make_shared<PinCounter>());
        return std::make_pair(std::shared_ptr<ValueSource>(clones.back()), MultiplexSource::ValueNodeMap{});
    });
    mux.prepare(ctx);
    CHECK(clones.size() == 3);
    for (const auto& c : clones) CHECK(c->rate == 48000 && c->starts == 0);
    mux.start(64);
    mux.prepare(ctx);
    mux.start(64);
    CHECK(clones.size() == 3);
    for (const auto& c : clones) CHECK(c->starts == 2);
    MultiplexSource solo;
    auto wired = std::make_shared<PinCounter>();
    solo.set_param("source", wired);
    solo.prepare(ctx);
    CHECK(wired->rate == 48000 && wired->starts == 0);
    solo.start(64);
    CHECK(wired->starts == 1);
}

// Each repetition restarts the child by start(); the repeater's output is the
// child's own note, over and over, with the gap between.
static void run_repeating_restart_tests() {
    const RenderContext ctx{48000};
    const auto make = [] {
        auto r = std::make_shared<RepeatingSource>();
        r->set_setting("duration", 0.05f);
        r->set_setting("gapDuration", 0.01f);
        r->set_param("source", Envelope::make_ar(0.3f));
        return r;
    };
    auto once = make(), shim = make();
    once->prepare(ctx);
    once->start(4800);
    shim->prepare(ctx); shim->start(4800);
    const auto out = render_samples(*once, 9600);
    CHECK(same_bits(out, render_samples(*shim, 9600)));

    auto ref = Envelope::make_ar(0.3f);
    ref->prepare(ctx);
    ref->start(2400);
    const auto note = render_samples(*ref, 2400);
    constexpr int kPeriod = 2400 + 1 + 480;                  // the note, then the gap
    CHECK(std::equal(note.begin(), note.end(), out.begin()));
    CHECK(std::all_of(out.begin() + 2400, out.begin() + kPeriod, [](float v) { return v == 0.0f; }));
    CHECK(std::equal(note.begin(), note.end(), out.begin() + kPeriod));
    CHECK(std::equal(note.begin(), note.end(), out.begin() + 2 * kPeriod));
    CHECK(*std::max_element(note.begin(), note.end()) > 0.5f);   // the envelope is audible
}

static std::shared_ptr<RepeatingSource> make_repeater(float varPct = 0.3f) {
    auto r = std::make_shared<RepeatingSource>();
    r->set_setting("duration", 0.02f);
    r->set_setting("durVarPct", varPct);
    r->set_setting("gapDuration", 0.01f);
    r->set_setting("gapVarPct", varPct);
    r->set_param("source", Envelope::make_ar(0.3f));
    return r;
}

static std::shared_ptr<PhasedValueSource> make_phased() {
    auto p = std::make_shared<PhasedValueSource>(0.01f);
    p->add_stage({sine_at(220.0f), 0.5f, 0.0f, 0.0f, 1.0f});
    p->add_stage({sine_at(330.0f), 0.0f, 0.0f, 0.0f, 1.0f});
    return p;
}

static std::shared_ptr<SegmentSource> make_segment(bool seconds, float varPct = 0.2f) {
    auto g = std::make_shared<SegmentSource>(
        seconds ? std::vector<float>{0.0004f, 1.0f, 0.0006f, -1.0f}
                : std::vector<float>{40.0f, 1.0f, 60.0f, -1.0f});
    g->set_param("widthVarPct", std::make_shared<ConstantSource>(varPct));
    g->set_param("valVarPct", std::make_shared<ConstantSource>(varPct / 2.0f));
    return g;
}

static void run_combine_mod_split_tests() {
    CHECK(split_equivalent([] { return make_repeater(); }));
    CHECK(split_equivalent([] {
        auto x = std::make_shared<CrossfadeSource>();
        x->set_param("source1", sine_at(220.0f));
        x->set_param("source2", sine_at(330.0f));
        return x;
    }));
    CHECK(split_equivalent([] { return make_phased(); }));
    CHECK(split_equivalent([] { return make_segment(false); }));
    CHECK(split_equivalent([] { return make_segment(true); }));
    CHECK(split_equivalent([] { return make_multiplex_voice(); }));
    CHECK(split_equivalent([] {
        return std::make_shared<CombinedSource>(sine_at(220.0f), sine_at(330.0f), CombineOp::Fade);
    }));
    CHECK(split_equivalent([] { return std::make_shared<StaticVarSource>(2.0f, 0.5f, 0.1f); }));
    CHECK(split_equivalent([] { return std::make_shared<StaticRangeSource>(0.2f, 0.4f, 0.3f); }));

    // The rate is the host's: a node prepared at one rate and then another is
    // the node prepared at the second. Both draw from a stream that runs on
    // across notes, so the comparison leaves the variation out.
    CHECK(follows_rate([] { return make_repeater(0.0f); }, 48000, 22050));
    CHECK(follows_rate([] { return make_repeater(0.0f); }, 22050, 48000));
    CHECK(follows_rate([] { return make_segment(true, 0.0f); }, 48000, 22050));
    CHECK(follows_rate([] { return make_segment(true, 0.0f); }, 22050, 48000));
    CHECK(follows_rate([] { return make_phased(); }, 48000, 22050));
    CHECK(follows_rate([] { return make_phased(); }, 22050, 48000));
    run_combine_mod_pin_split_tests();
    run_repeating_restart_tests();
}

// Wired cases for the types whose defaults read 0: a brass evolution under a
// wavetable (its rate capture), a distorter with a signal, a sequence of
// partials in an additive source.
static void run_wired_split_tests() {
    CHECK(split_equivalent([] {
        auto w = std::make_shared<WavetableSource>();
        auto brass = std::make_shared<BrassEvolutionSource>();
        brass->set_param("breath", std::make_shared<ConstantSource>(0.6f));
        w->set_param("evolution", brass);
        w->set_param("inputSource", std::make_shared<WhiteNoiseSource>(0x8Au));
        return w;
    }, 2048, 3));
    {
        auto brass_at = [](int rate) {
            WavetableSource w;
            auto brass = std::make_shared<BrassEvolutionSource>();
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
            w->set_param("evolution", std::make_shared<AveragingEvolutionSource>());
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
            auto f = std::make_shared<FullAdditiveSource>();
            auto seq = std::make_shared<SequencePartials>();
            seq->set_setting("maxPartials", 12.0f);
            seq->set_setting("unitPO1", 0.3f);
            f->set_param("partials", seq);
            return f;
        }, 2048, 4);
        CHECK(r.equal && r.nonzero);
    }
}

// A setting changed on a Vibrato that is playing a note: the note keeps the
// chain it started with, and the next start plays the new one.
static void run_vibrato_late_setting_tests() {
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

// The hosts: a loaded instrument is prepared by the
// loader and only started per note; render takes no context.
static void run_host_split_tests() {
    const int n = 48000;
    auto render_twice = [&](const char* path) {
        std::vector<float> a(size_t(n), 0.0f), b(size_t(n), 0.0f);
        for (auto* out : {&a, &b}) {
            auto ip = load_scoreless(path);
            auto* inst = dynamic_cast<PitchedInstrument*>(ip.instrument.get());
            inst->play_note({60.0f, 0.8f, 0.4f, 0.0f, false, nullptr}, 0.0f);
            inst->play_note({64.0f, 0.8f, 0.4f, 0.0f, false, nullptr}, 0.5f);
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

// The two-call contract: the sweep over every type, then one group per family.
static void run_prepare_start_tests() {
    run_split_equivalence_tests();
    run_core_split_tests();
    run_noise_split_tests();
    run_wavetable_split_tests();
    run_hybrid_ks_split_tests();
    run_additive_split_tests();
    run_physical_split_tests();
    run_filter_split_tests();
    run_vibrato_split_tests();
    run_combine_mod_split_tests();
    run_wired_split_tests();
    run_vibrato_late_setting_tests();
    run_host_split_tests();
}

int main() {
    run_pin_tests();
    run_base_tests();
    run_prepare_start_tests();
    run_manual_pin_next_tests();
    run_setting_clamp_tests();
    run_manual_pin_prepare_tests();
    run_processor_manual_input_tests();
    run_wave_source_tests();
    run_range_var_tests();
    run_multi_name_gate_tests();
    run_envelope_pin_tests();
    run_envelope_preset_tests();
    run_envelope_rebuild_pin_tests();
    run_noise_pin_tests();
    run_noise_white_tests();
    run_noise_default_tests();
    run_noise_red_pin_tests();
    run_noise_manual_amplitude_tests();
    run_noise_layered_tests();
    run_wavetable_pin_tests();
    run_wavetable_evolution_tests();
    run_hybrid_ks_tests();
    run_evolution_holder_shape_tests();
    run_evolution_holder_setting_tests();
    run_evolution_manual_pin_tests();
    run_additive_source_pin_tests();
    run_additive_structural_pin_tests();
    run_additive_partials_pin_tests();
    run_additive_smoke_tests();
    run_additive_auto_pin_tests();
    run_additive_manual_pin_tests();
    run_additive_formant_pin_tests();
    run_additive_wiring_tests();
    run_additive_setting_hook_tests();
    run_additive_explicit_array_tests();
    run_physical_pin_shape_tests();
    run_physical_auto_pin_tests();
    run_physical_advance_order_tests();
    run_physical_setting_hook_tests();
    run_physical_mesh_hook_tests();
    run_physical_behavior_tests();
    run_filter_tests();
    run_combine_mod_tests();
    run_memo_tests();
    run_shared_source_tests();
    run_sweep_follows_wiring_tests();
    run_tap_reads_live_tests();
    run_walk2_tests();
    run_walk3_tests();
    run_walk1_tests();
    run_passage_melody_tests();
    run_passage_parse_tests();
    run_onset_field_tests();
    run_hold_delivery_tests();
    run_param_map_unwired_pin_tests();
    run_param_map_unwired_multiplex_tests();
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
