#include "mforce/testing/check.h"
#include "core/tests/fixtures.h"
#include "mforce/core/component.h"
#include "mforce/core/descriptors.h"
#include "mforce/core/dsp_value_source.h"
#include "mforce/core/pins.h"
#include <memory>
#include <string>

using namespace mforce;


void run_tap_guard_tests() {
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
namespace {
struct PinProbe : mforce::Component {
  mforce::Param freq{this, "frequency", 440.0f, 0.01f, 20000.0f, "hz"};
  mforce::Param amp{this, "amplitude", 1.0f, 0.0f, 10.0f};
  mforce::Input in{this, "source", mforce::Input::Optional};
  mforce::Setting<int> mode{this, "mode", 2, 0, 3};
  mforce::Array table{this, "weights", nullptr, 0.5f, 0.0f, 1.0f};
  const char* type_name() const override { return "PinProbe"; }
};
}  // namespace

void run_pin_tests() {
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
}  // namespace

void run_base_tests() {
    auto ramp = std::make_shared<UnitRamp>();
    Doubler d;
    d.set_param("source", ramp);              // Processor's input is the pin named "source"
    CHECK(d.input_descriptors().size() == 1);
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

// A Manual pin is driven by its node: next() pulls the source and returns
// its value, or the pin's fallback (the constant; 0 for an input) unwired.
void run_manual_pin_next_tests() {
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
void run_setting_clamp_tests() {
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
}  // namespace

void run_manual_pin_prepare_tests() {
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
namespace {
struct ManualDoubler final : mforce::Processor {
  ManualDoubler() : Processor(mforce::Pull::Manual) {}
  float compute() override { return 2.0f * input.next(); }
  const char* type_name() const override { return "ManualDoubler"; }
};
struct ManualIdle final : mforce::Processor {  // owns its input and never pulls it
  ManualIdle() : Processor(mforce::Pull::Manual) {}
  float compute() override { return 0.0f; }
  const char* type_name() const override { return "ManualIdle"; }
};
}  // namespace

void run_processor_manual_input_tests() {
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
namespace {
struct Counter final : Generator {
    int computes{0};
    float compute() override { return float(++computes); }
    const char* type_name() const override { return "Counter"; }
};
}  // namespace

// The tap edge reads its source's memo live: a Setup-time read between
// ticks sees the source's present value without the tap being pulled.
void run_tap_reads_live_tests() {
    TestHost h;
    auto c = std::make_shared<Counter>();
    auto tap = std::make_shared<TapSource>(c);
    c->prepare(h.ctx); c->start(16);
    tap->prepare(h.ctx); tap->start(16);
    h.clock.tick(); c->next();
    CHECK_NEAR(tap->current(), 1.0f, 1e-6f);  // never pulled, reads live
    h.clock.tick(); c->next();
    CHECK_NEAR(tap->current(), 2.0f, 1e-6f);
    CHECK_NEAR(tap->next(), 1.0f, 1e-6f);     // pulled: the previous tick's value (the source computed this tick already)
    h.clock.tick();
    CHECK_NEAR(tap->next(), 2.0f, 1e-6f);     // the source not yet computed this tick: its value is still last tick's
    c->next();
    CHECK_NEAR(tap->current(), 2.0f, 1e-6f);  // pulled this tick: the memo, not live — what a consumer reads after its sweep
}
