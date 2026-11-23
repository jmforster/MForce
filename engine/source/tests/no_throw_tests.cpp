#include "mforce/testing/check.h"
#include "test_support.h"
#include "core/tests/fixtures.h"
#include "mforce/core/dsp_value_source.h"
#include "mforce/core/pins.h"
#include "mforce/source/oscillator/sine_source.h"
#include "mforce/source/physical/delay_line_source.h"
#include "mforce/source/wavetable/wavetable_source.h"
#include <cmath>
#include <memory>
#include <stdexcept>
#include <string>

using namespace mforce;


// start() and next() never throw. An invalid runtime value
// is clamped to the narrowest valid value and the node reports a fault
// bit; the host reads the bits after a
// render. prepare() may throw, and does on a host error.
void run_no_throw_tests() {
    TestHost h;
    // A frequency driven to zero mid-note renders with the pin's minimum and
    // reports, instead of terminating the process from the audio thread.
    auto f = std::make_shared<ConstantSource>(440.0f);
    auto osc = std::make_shared<SineSource>();
    osc->set_param("frequency", f);
    osc->prepare(h.ctx); osc->start(48000);
    h.clock.tick(); osc->next();
    CHECK(osc->faults() == 0);
    f->set(0.0f);
    h.clock.tick();
    float v = 0.0f;
    bool threw = false;
    try { v = osc->next(); } catch (...) { threw = true; }
    CHECK(!threw);
    CHECK(std::isfinite(v));
    CHECK((osc->faults() & fault_bit(Fault::FrequencyClamped)) != 0);
    f->set(440.0f);
    h.clock.tick(); osc->next();                      // computes normally again
    osc->clear_faults();
    CHECK(osc->faults() == 0);
    // A table fill at a frequency above the rate fills one sample and reports.
    auto wt = std::make_shared<WavetableSource>();
    wt->set_param("frequency", std::make_shared<ConstantSource>(96000.0f));
    wt->set_param("inputSource", std::make_shared<ConstantSource>(0.5f));
    wt->prepare(h.ctx); wt->start(4800);
    h.clock.tick();
    threw = false;
    try { wt->next(); } catch (...) { threw = true; }
    CHECK(!threw);
    CHECK((wt->faults() & fault_bit(Fault::TableClamped)) != 0);
#ifdef NDEBUG
    // A node no host prepared: start() reports and skips on_start (nothing
    // divides by zero there); a bare node's next() computes as it always has
    // and reports the same fault. (In a Debug build the assert on start()
    // is the contract and fires first.)
    auto dl = std::make_shared<DelayLineSource>();
    dl->set_param("source", std::make_shared<ConstantSource>(1.0f));
    dl->start(4800);
    CHECK((dl->faults() & fault_bit(Fault::NeverPrepared)) != 0);
#else
    auto dl = std::make_shared<DelayLineSource>();
#endif
    auto bareC = std::make_shared<Counter>();
    CHECK_NEAR(bareC->next(), 1.0f, 1e-6f);            // computed, as a bare node always has
    CHECK((bareC->faults() & fault_bit(Fault::NeverPrepared)) != 0);
    // prepare() with no rate is a host error and throws where throwing is allowed.
    threw = false;
    try { dl->prepare(RenderContext{0}); } catch (const std::invalid_argument&) { threw = true; }
    CHECK(threw);
    CHECK(std::string(fault_name(Fault::FrequencyClamped)) == "frequency clamped");
}
