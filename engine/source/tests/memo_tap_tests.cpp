#include "mforce/testing/check.h"
#include "test_support.h"
#include "core/tests/fixtures.h"
#include "source/tests/fixtures.h"
#include "mforce/core/dsp_value_source.h"
#include "mforce/core/pins.h"
#include "mforce/source/combiner/combined_source.h"
#include "mforce/source/envelope/envelope.h"
#include "mforce/source/oscillator/sine_source.h"
#include <memory>
#include <string>
#include <utility>

using namespace mforce;


// WaveSource under Generator with its three pins declared once, in the
// order the base advances them (amplitude, frequency, phase); an unwired
// pin reads its constant and has no source.
void run_wave_source_tests() {
    SineSource s;
    auto pd = s.param_descriptors();
    CHECK(pd.size() == 3);
    CHECK(std::string(pd[0].name) == "amplitude");
    CHECK(std::string(pd[1].name) == "frequency");
    CHECK(std::string(pd[2].name) == "phase");
    CHECK(s.get_param("phase") == nullptr);
    CHECK(s.range() == mforce::Generator::Range::Bipolar);
    s.set_param("frequency", std::make_shared<ConstantSource>(12000.0f));
    s.prepare(RenderContext{48000}); s.start(8);
    CHECK_NEAR(s.next(), 0.0f, 1e-6f);           // pos 0 -> sin(0)
    CHECK_NEAR(s.next(), 1.0f, 1e-6f);           // a quarter cycle later
    CHECK(s.get_param("frequency") != nullptr);
}

void run_memo_tests() {
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
    auto loop = tapless_cycle(h);             // illegal, must be finite
    h.clock.tick();
    CHECK_NEAR(loop->next(), 1.0f, 1e-6f);    // 1 + the previous value (0)
    h.clock.tick();
    CHECK_NEAR(loop->next(), 2.0f, 1e-6f);    // 1 + 1
    loop->clear_param("source2");             // break the ownership cycle
}

void run_previous_tests() {
    TestHost h;
    auto c = std::make_shared<Counter>();
    c->prepare(h.ctx); c->start(16);
    CHECK_NEAR(c->previous(), 0.0f, 1e-6f);   // nothing computed yet: cur_
    h.clock.tick();
    CHECK_NEAR(c->previous(), 0.0f, 1e-6f);   // this tick not yet claimed: cur_, still last tick's
    CHECK_NEAR(c->next(), 1.0f, 1e-6f);
    CHECK_NEAR(c->previous(), 0.0f, 1e-6f);   // claimed and computed: what cur_ replaced
    CHECK_NEAR(c->current(), 1.0f, 1e-6f);
    h.clock.tick();
    CHECK_NEAR(c->previous(), 1.0f, 1e-6f);   // a new tick, not yet claimed: cur_
    CHECK_NEAR(c->next(), 2.0f, 1e-6f);
    CHECK_NEAR(c->previous(), 1.0f, 1e-6f);
    h.clock.tick();                           // step() moves the value the same way
    CHECK_NEAR(c->step(), 3.0f, 1e-6f);
    CHECK_NEAR(c->previous(), 2.0f, 1e-6f);
    CHECK_NEAR(c->step(), 4.0f, 1e-6f);
    CHECK_NEAR(c->previous(), 3.0f, 1e-6f);   // before the latest step
    h.clock.tick(); h.clock.tick();           // not pulled for two ticks
    CHECK_NEAR(c->previous(), 4.0f, 1e-6f);   // its last value
    auto mid = std::make_shared<CombinedSource>(std::make_shared<ConstantSource>(1.0f),
                                                std::make_shared<ConstantSource>(0.0f),
                                                CombineOp::Sum, 0.0f);
    struct Probe final : Generator {            // reads its target's previous() from inside the target's compute
        ValueSource* target{nullptr};
        float seen{-1.0f};
        float compute() override { seen = target->previous(); return 0.0f; }
        const char* type_name() const override { return "Probe"; }
    };
    auto probe = std::make_shared<Probe>(); probe->target = mid.get();
    mid->prepare(h.ctx); mid->start(16); probe->prepare(h.ctx); probe->start(16);
    mid->set_param("source2", probe);         // mid's sweep pulls the probe: a read mid-compute
    h.clock.tick();
    CHECK_NEAR(mid->next(), 1.0f, 1e-6f);
    CHECK_NEAR(probe->seen, 0.0f, 1e-6f);     // mid-compute: last tick's value (0), not two ticks old
    h.clock.tick();
    CHECK_NEAR(mid->next(), 1.0f, 1e-6f);
    CHECK_NEAR(probe->seen, 1.0f, 1e-6f);
    auto bare = std::make_shared<Counter>();  // no clock: every pull computes, previous() is the present value
    bare->prepare(RenderContext{48000}); bare->start(16);
    bare->next(); bare->next();
    CHECK_NEAR(bare->previous(), 2.0f, 1e-6f);
}

void run_tap_reading_tests() {
    TestHost h;
    // What a consumer gets. A node reads its pins after its sweep through
    // current(), so the tap's current() is the delivery: sum = c + tap(c),
    // with c computed first in the tick, reads c's previous value through the
    // tap — 1 + 0, 2 + 1, 3 + 2 (reading A gave 2, 4, 6).
    auto c = std::make_shared<Counter>();
    auto tap = std::make_shared<TapSource>(c);
    auto sum = std::make_shared<CombinedSource>(c, tap, CombineOp::Sum, 0.0f);
    sum->prepare(h.ctx); sum->start(16);
    h.clock.tick(); CHECK_NEAR(sum->next(), 1.0f, 1e-6f);
    h.clock.tick(); CHECK_NEAR(sum->next(), 3.0f, 1e-6f);
    h.clock.tick(); CHECK_NEAR(sum->next(), 5.0f, 1e-6f);
    // The tap alone, read position by read position.
    c = std::make_shared<Counter>();
    tap = std::make_shared<TapSource>(c);
    c->prepare(h.ctx); c->start(16); tap->prepare(h.ctx); tap->start(16);
    h.clock.tick();
    CHECK_NEAR(c->next(), 1.0f, 1e-6f);       // the source computes first this tick
    CHECK_NEAR(tap->next(), 0.0f, 1e-6f);     // the tap is one sample late anyway (reading A gave 1)
    h.clock.tick();
    CHECK_NEAR(tap->next(), 1.0f, 1e-6f);     // the tap first: the source has not claimed this tick
    CHECK_NEAR(c->next(), 2.0f, 1e-6f);
    CHECK_NEAR(tap->next(), 1.0f, 1e-6f);     // the tap's own memo: pulled again this tick, the same value
    CHECK_NEAR(tap->current(), 1.0f, 1e-6f);  // pulled this tick: current() is the memo, what a consumer reads after its sweep
    h.clock.tick();
    CHECK_NEAR(c->next(), 3.0f, 1e-6f);
    CHECK_NEAR(tap->current(), 3.0f, 1e-6f);  // not pulled this tick: live — between ticks, the value the next tick's tap delivers
    CHECK_NEAR(tap->next(), 2.0f, 1e-6f);
    CHECK_NEAR(tap->current(), 2.0f, 1e-6f);  // and the memo again once pulled
    h.clock.tick();                           // inside a fill: one step late
    CHECK_NEAR(c->step(), 4.0f, 1e-6f);
    CHECK_NEAR(c->step(), 5.0f, 1e-6f);
    CHECK_NEAR(tap->step(), 4.0f, 1e-6f);
    h.clock.tick(); h.clock.tick();           // a source not pulled this tick: its last value
    CHECK_NEAR(tap->next(), 5.0f, 1e-6f);
    // Between ticks (a Setup-time read) the clock holds at the last rendered
    // tick: a tap pulled in that tick still answers with its memo — one sample
    // older than what the coming tick delivers — and a Setup-time step()
    // through it reads the same memo. A stated limit, not a goal.
    h.clock.tick();
    CHECK_NEAR(c->next(), 6.0f, 1e-6f);
    CHECK_NEAR(tap->next(), 5.0f, 1e-6f);     // pulled this tick
    CHECK_NEAR(tap->current(), 5.0f, 1e-6f);  // "between ticks" now: the memo, though the coming tick delivers 6
    CHECK_NEAR(tap->step(), 5.0f, 1e-6f);     // a Setup-time step() through the pulled tap: the same memo
    // Inside a fill, a tap stepped before its source reads two steps late: the
    // source re-claims the same tick, so previous() cannot tell a fresh step
    // from an earlier one. A stated limit of step().
    h.clock.tick();
    CHECK_NEAR(tap->step(), 6.0f, 1e-6f);     // draw 1: the source has not claimed this tick — its last value
    CHECK_NEAR(c->step(), 7.0f, 1e-6f);
    CHECK_NEAR(tap->step(), 6.0f, 1e-6f);     // draw 2: the value before the source's latest step, not the latest
    CHECK_NEAR(c->step(), 8.0f, 1e-6f);
    CHECK_NEAR(tap->step(), 7.0f, 1e-6f);     // draw 3: 7, not 8
    // The cycle skeleton under a clock: out = 1 + tap(out) counts 1, 2, 3 — the
    // tap reads its source mid-compute, and readings A and B agree there.
    auto counter = std::make_shared<CombinedSource>(std::make_shared<ConstantSource>(1.0f),
                                                    std::make_shared<ConstantSource>(0.0f),
                                                    CombineOp::Sum, 0.0f);
    auto self = std::make_shared<TapSource>(counter);
    counter->prepare(h.ctx); counter->start(16);
    counter->set_param("source2", self);      // after the host calls, which walk the pins
    self->prepare(h.ctx); self->start(16);
    h.clock.tick(); CHECK_NEAR(counter->next(), 1.0f, 1e-6f);
    h.clock.tick(); CHECK_NEAR(counter->next(), 2.0f, 1e-6f);
    h.clock.tick(); CHECK_NEAR(counter->next(), 3.0f, 1e-6f);
    counter->clear_param("source2");          // break the ownership cycle
    auto bare = std::make_shared<Counter>();  // no clock: positional, as every pull computes
    auto bareTap = std::make_shared<TapSource>(bare);
    bare->prepare(RenderContext{48000}); bare->start(16);
    bareTap->prepare(RenderContext{48000}); bareTap->start(16);
    bare->next();
    CHECK_NEAR(bareTap->next(), 1.0f, 1e-6f);
}

// The sweep follows every door that changes a pin's source: a source wired
// after prepare and start is pulled on the next tick, and one cleared is
// not.
void run_sweep_follows_wiring_tests() {
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

    // A node assigning itself (an envelope taking a new layout through
    // replace_stages) keeps its wires in the sweep: the range source is still
    // pulled afterwards.
    auto r = std::make_shared<Counter>();
    auto env = Envelope::make_ar(0.2f);
    env->set_param("maxValue", r);
    env->prepare(h.ctx); env->start(4800);
    for (int i = 0; i < 3; ++i) { h.clock.tick(); env->next(); }
    CHECK(r->computes == 3);
    env->replace_stages(std::move(*Envelope::make_ar(0.3f)));   // the stages are replaced, the wire must stay swept
    for (int i = 0; i < 3; ++i) { h.clock.tick(); env->next(); }
    CHECK(r->computes == 6);
    CHECK(env->get_param("maxValue").get() == r.get());
}
