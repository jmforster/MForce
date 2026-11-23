#include "mforce/testing/check.h"
#include "mforce/core/dsp_value_source.h"
#include "mforce/core/pins.h"
#include "mforce/source/combiner/combined_source.h"
#include "mforce/source/filter/svf_source.h"
#include "mforce/source/physical/delay_line_source.h"
#include <cmath>
#include <memory>
#include <utility>
#include <vector>

using namespace mforce;


void run_tap_cycle_tests() {
    // The tap edge's one-sample delay with no host clock: every pull computes
    // and the tap reads its source's present value, which for a tap evaluated
    // before its source each tick is last tick's value (under a clock the
    // delay is the rule: run_tap_reading_tests). Self-referencing counter:
    // out = 1 + tap(out)
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

void run_delay_line_tests() {
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

void run_phase_delay_tests() {
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

void run_loop_compensation_tests() {
    RenderContext ctx{48000};
    // Bare tap-closed loop: delay <- sum(excite, tap(delay)). The cycle is
    // len + 1 ticks (the closing tap's one sample); compensate shortens len
    // so the cycle lands exactly on sr/f.
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
