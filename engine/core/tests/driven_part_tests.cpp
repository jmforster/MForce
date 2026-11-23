#include "mforce/testing/check.h"
#include "mforce/core/driven_part.h"
#include "mforce/core/dsp_value_source.h"
#include "mforce/core/pins.h"
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace mforce;

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
}  // namespace

// The base drives a set's pins: each prepared once, started once and
// advanced once per next(), in declaration order; a composite forwards each
// verb to its sets and advances no pin of its own.
struct OrderProbe final : Generator {
    std::vector<std::string>* log{nullptr};
    std::string tag;
    float compute() override { if (log) log->push_back(tag); return 0.0f; }
    const char* type_name() const override { return "OrderProbe"; }
};
namespace {
struct TwoPinPart final : DrivenPart {
    Param a{this, "a", 1.0f, 0.0f, 10.0f, nullptr, Pull::Manual};
    Setting<int> n{this, "n", 3, 1, 8};
    Param b{this, "b", 2.0f, 0.0f, 10.0f, nullptr, Pull::Manual};
    int prepares{0}, starts{0}, nexts{0}, rate{0};
    const char* type_name() const override { return "TwoPinPart"; }
protected:
    void on_prepare(const RenderContext& ctx) override { rate = ctx.sampleRate; ++prepares; }
    void on_start(int) override { ++starts; }
    void on_next() override { ++nexts; }
};
}  // namespace
void run_driven_part_lifecycle_tests() {
    std::printf("driven part: the base drives the pins\n");
    RenderContext ctx{48000};
    {
        TwoPinPart part;
        auto pa = std::make_shared<PinCounter>(), pb = std::make_shared<PinCounter>();
        CHECK(part.set_param("a", pa) && part.set_param("b", pb));
        part.prepare(ctx); part.start(4800);
        for (int i = 0; i < 5; ++i) part.next();
        CHECK(pa->ctxPrepares == 1 && pa->starts == 1 && pa->nexts == 5);
        CHECK(pb->ctxPrepares == 1 && pb->starts == 1 && pb->nexts == 5);
        CHECK(pa->rate == 48000 && pb->rate == 48000);
        CHECK(part.prepares == 1 && part.starts == 1 && part.nexts == 5 && part.rate == 48000);
        part.prepare(ctx); part.start(4800); part.next();        // a second prepare relists the pins: nothing doubles
        CHECK(pa->ctxPrepares == 2 && pa->starts == 2 && pa->nexts == 6);
        CHECK(pb->ctxPrepares == 2 && pb->starts == 2 && pb->nexts == 6);
        CHECK(part.prepares == 2 && part.starts == 2 && part.nexts == 6);
    }
    {
        TwoPinPart part;
        std::vector<std::string> log;
        auto oa = std::make_shared<OrderProbe>(); oa->log = &log; oa->tag = "a";
        auto ob = std::make_shared<OrderProbe>(); ob->log = &log; ob->tag = "b";
        CHECK(part.set_param("a", oa) && part.set_param("b", ob));
        part.prepare(ctx); part.start(4800);
        CHECK(log.empty());
        part.next();
        CHECK(log == std::vector<std::string>({"a", "b"}));      // declaration order, the setting skipped
        part.set_setting("n", 5.0f);
        part.next();
        CHECK(log.size() == 4 && part.n.value == 5);
    }
    {
        TwoPinPart part;                                          // unwired: the constants, no source to call
        part.prepare(ctx); part.start(4800);
        for (int i = 0; i < 3; ++i) part.next();
        CHECK(part.nexts == 3);
        CHECK_NEAR(part.a.current(), 1.0f, 1e-6f);
        CHECK_NEAR(part.b.current(), 2.0f, 1e-6f);
    }
}
