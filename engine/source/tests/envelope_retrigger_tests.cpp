#include "mforce/testing/check.h"
#include "mforce/core/pins.h"
#include "mforce/core/ramp.h"
#include "mforce/source/envelope/envelope.h"
#include <algorithm>
#include <cmath>

using namespace mforce;


void run_envelope_retrigger_tests() {
    // Seconds-mode gesture: 10ms 1.0->0.2, 20ms 0.2->1.0, expand@1.0.
    const int sr = 48000;
    Envelope env;
    env.set_setting("timeMode", 1.0f);
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

// Release re-layout: the release stage's sample count is computed at
// voice birth against note 1's
// duration; at gate_release the caller may re-reference it to the
// RELEASING note's duration.
void run_release_relayout_tests() {
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
    // Default arg: fresh envelope, no ref. The
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
