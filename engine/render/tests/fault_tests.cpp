#include "mforce/testing/check.h"
#include "test_support.h"
#include "core/tests/fixtures.h"
#include "patch/tests/fixtures.h"
#include "render/tests/fixtures.h"
#include "source/tests/fixtures.h"
#include "mforce/core/component.h"
#include "mforce/core/dsp_value_source.h"
#include "mforce/core/pins.h"
#include "mforce/patch/envelope_preset.h"
#include "mforce/render/fault_report.h"
#include "mforce/render/pitched_instrument.h"
#include "mforce/source/additive/additive_source.h"
#include "mforce/source/additive/full_partials.h"
#include "mforce/source/oscillator/sine_source.h"
#include <nlohmann/json.hpp>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using namespace mforce;


// The fault bits reach the host. A cycle with no tap is a
// bit, not a print, through next() and through the host calls; a clockless
// pull reports; the instrument collects by node id and clears as it reads.
void run_fault_tests() {
    TestHost h;
    auto loop = tapless_cycle(h);
    h.clock.tick(); CHECK_NEAR(loop->next(), 1.0f, 1e-6f);
    CHECK((loop->faults() & fault_bit(Fault::CycleWithoutTap)) != 0);
    loop->clear_faults();
    loop->prepare(h.ctx);                              // re-entered through the cycle: returns, no overflow
    CHECK((loop->faults() & fault_bit(Fault::CycleWithoutTap)) != 0);
    loop->clear_faults();
    loop->start(16);
    CHECK((loop->faults() & fault_bit(Fault::CycleWithoutTap)) != 0);
    loop->clear_param("source2");
    // A clockless pull on a prepared node reports; the host is the only reader.
    auto bare = std::make_shared<Counter>();
    bare->prepare(RenderContext{48000}); bare->start(16);
    bare->next();
    CHECK((bare->faults() & fault_bit(Fault::Clockless)) != 0);
    CHECK(bare->take_faults() == fault_bit(Fault::Clockless));   // read and cleared in one step
    CHECK(bare->faults() == 0);
    // A throw out of on_prepare (prepare may throw) leaves the node usable:
    // the next prepare runs on_prepare, and next() reports no cycle.
    struct ThrowingPrepare final : Generator {
        bool armed{true};
        int prepares{0};
        void on_prepare(const RenderContext&) override {
            if (armed) { armed = false; throw std::runtime_error("on_prepare failed"); }
            ++prepares;
        }
        float compute() override { return 1.0f; }
        const char* type_name() const override { return "ThrowingPrepare"; }
    };
    auto tp = std::make_shared<ThrowingPrepare>();
    bool threw = false;
    try { tp->prepare(h.ctx); } catch (const std::runtime_error&) { threw = true; }
    CHECK(threw);
    tp->prepare(h.ctx);
    CHECK(tp->prepares == 1);
    tp->start(16); h.clock.tick();
    CHECK_NEAR(tp->next(), 1.0f, 1e-6f);
    CHECK((tp->faults() & fault_bit(Fault::CycleWithoutTap)) == 0);
    // The host collects by node id, once per id across the pool, in id order,
    // and clears as it reads; a prepare after the render clears nothing.
    const char* json = R"({"sampleRate":48000,"instrument":{"polyphony":2},
      "graph":{"output":"sum","nodes":[
        {"id":"b","type":"SineSource","params":{"frequency":0.0,"amplitude":1.0}},
        {"id":"a","type":"SineSource","params":{"frequency":0.0,"amplitude":1.0}},
        {"id":"sum","type":"CombinedSource","params":{"source1":{"ref":"b"},"source2":{"ref":"a"},"operation":3}}]}})";
    auto ip = load_inst(json);
    auto* pi = dynamic_cast<PitchedInstrument*>(ip.instrument.get());
    CHECK(pi != nullptr);
    play_notes(*pi, {{.start = 0.0f, .duration = 0.1f, .noteNumber = 60.0f, .velocity = 0.8f, .onset = 0, .hold = false}});
    std::vector<float> out(4800);
    ip.instrument->render(out.data(), int(out.size()));
    ip.instrument->prepare(RenderContext{48000});      // a scored patch is pre-rendered at load, then prepared again
    std::vector<FaultReport> faults;
    ip.instrument->collect_faults(faults);
    CHECK(faults.size() == 2);
    CHECK(faults.size() == 2 && faults[0].nodeId == "a" && faults[1].nodeId == "b");
    CHECK(faults.size() == 2 && (faults[0].faults & fault_bit(Fault::FrequencyClamped)) != 0
                             && (faults[1].faults & fault_bit(Fault::FrequencyClamped)) != 0);
    faults.clear();
    ip.instrument->collect_faults(faults);
    CHECK(faults.empty());                             // cleared as read
}

// A rejected wire says so. set_param answers false for a pin
// the node does not have or a part of the wrong kind; the build turns that,
// and an unknown key, into a load failure naming node, pin and reason.
void run_wire_rejection_tests() {
    auto osc = std::make_shared<SineSource>();
    CHECK(osc->set_param("frequency", std::make_shared<ConstantSource>(1.0f)));
    CHECK(!osc->set_param("freq", std::make_shared<ConstantSource>(1.0f)));         // no such pin
    auto add = std::make_shared<AdditiveSource>();                              // registry name "AdditiveSource"
    CHECK(!add->set_param("partials", std::make_shared<SineSource>()));              // wrong kind for the slot
    CHECK(add->set_param("partials", std::make_shared<FullPartials>()));
    const char* bad_key = R"({"sampleRate":48000,"instrument":{"polyphony":1},"graph":{"output":"osc","nodes":[
        {"id":"osc","type":"SineSource","params":{"frequncy":440.0}}]}})";
    std::string msg;
    msg = load_refused(bad_key);
    CHECK(msg.find("node 'osc' (SineSource): unknown key 'frequncy'") != std::string::npos);
    const char* bad_kind = R"({"sampleRate":48000,"instrument":{"polyphony":1},"graph":{"output":"add","nodes":[
        {"id":"s","type":"SineSource","params":{"frequency":440.0}},
        {"id":"add","type":"AdditiveSource","params":{"frequency":220.0,"partials":{"ref":"s"}}}]}})";
    msg.clear();
    msg = load_refused(bad_kind);
    CHECK(msg.find("node 'add' (AdditiveSource): 'partials' does not accept a SineSource") != std::string::npos);
    // A key a hand-built node's branch never read (the VarSource case the lint
    // found in the bwg patches) fails the load too: the check runs per node.
    const char* hand_built = R"({"sampleRate":48000,"instrument":{"polyphony":1},"graph":{"output":"v","nodes":[
        {"id":"v","type":"VarSource","params":{"value":0.0}}]}})";
    msg.clear();
    msg = load_refused(hand_built);
    CHECK(msg.find("node 'v' (VarSource): unknown key 'value'") != std::string::npos);
}

// A string where a pin takes a value fails the load naming the node, the pin
// and the string; the wavetable's old string-typed evolution also names the
// tool that rewrites it as a node, and the node form of the same patch loads.
void run_string_pin_refusal_tests() {
    const char* evo = R"({"sampleRate":48000,"instrument":{"polyphony":1},"graph":{"output":"wt","nodes":[
        {"id":"wt","type":"WavetableSource","params":{"frequency":330.0,"evolution":"pluck","muting":0.0,"evolutionSeed":42}}]}})";
    std::string msg;
    msg = load_refused(evo);
    CHECK(msg.find("node 'wt' (WavetableSource): 'evolution' is the string \"pluck\"") != std::string::npos);
    CHECK(msg.find("tools/convert_patch.py") != std::string::npos);
    CHECK(msg.find("a slot takes {\"ref\": id}") != std::string::npos);       // a slot, so no number is offered
    const char* freq = R"({"sampleRate":48000,"instrument":{"polyphony":1},"graph":{"output":"osc","nodes":[
        {"id":"osc","type":"SineSource","params":{"frequency":"440"}}]}})";
    msg.clear();
    msg = load_refused(freq);
    CHECK(msg.find("node 'osc' (SineSource): 'frequency' is the string \"440\"") != std::string::npos);
    CHECK(msg.find("convert_patch") == std::string::npos);
    CHECK(msg.find("a pin takes a number") != std::string::npos);
    const char* node = R"({"sampleRate":48000,"instrument":{"polyphony":1},"graph":{"output":"wt","nodes":[
        {"id":"wt_evolution","type":"PluckEvolution","params":{"muting":0.0,"seed":42}},
        {"id":"wt","type":"WavetableSource","params":{"frequency":330.0,"evolution":{"ref":"wt_evolution"}}}]}})";
    auto ip = load_inst(node);
    CHECK(ip.instrument != nullptr);
}
