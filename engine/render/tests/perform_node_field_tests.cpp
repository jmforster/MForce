#include "mforce/testing/check.h"
#include "test_support.h"
#include "core/tests/fixtures.h"
#include "patch/tests/fixtures.h"
#include "render/tests/fixtures.h"
#include "mforce/core/dsp_value_source.h"
#include "mforce/core/pins.h"
#include "mforce/patch/envelope_preset.h"
#include "mforce/patch/patch.h"
#include "mforce/render/channel.h"
#include "mforce/render/fault_report.h"
#include "mforce/render/mixer.h"
#include "mforce/render/mono_source.h"
#include "mforce/render/perform_source.h"
#include "mforce/render/pitched_instrument.h"
#include "mforce/render/voice.h"
#include "mforce/source/envelope/envelope.h"
#include "mforce/source/modulator/perform_node.h"
#include "mforce/source/oscillator/sine_source.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <exception>
#include <memory>
#include <string>
#include <vector>

using namespace mforce;

namespace {
std::shared_ptr<ConstantSource> konst(float v) { return std::make_shared<ConstantSource>(v); }
}  // namespace

// Which of the voice's six adapters a PerformNode `f` loads as: its index in
// the labels' order, -1 for none, -2 with the refusal when the load throws.
static int perform_field_of(const nlohmann::json& perfNode, std::string& refusal) {
    using J = nlohmann::json;
    const J osc = {{"id", "osc"}, {"type", "SineSource"}, {"params", J{{"frequency", J{{"ref", "f"}}}}}};
    const J doc = {{"sampleRate", 48000}, {"instrument", J{{"polyphony", 1}}},
                   {"graph", J{{"output", "osc"}, {"nodes", J::array({perfNode, osc})}}}};
    try {
        // A note whose six fields all differ: the node's value says which it reads.
        const Patch patch = patch_door::read_text(doc.dump());
        auto state = std::make_shared<InstrumentState>();
        state->wheel.store(0.5f);
        Voice v(patch.graph, *patch.instrument, state, nullptr);
        v.prepare(RenderContext{48000});
        v.start({440.25f, 0.375f, 4800, 0.6875f, 3, nullptr}, false);
        (void)v.next();                                          // the wheel smoother moves off 0
        const auto* f = dynamic_cast<const PerformOut*>(v.nodes().at("f").get());
        if (!f) return -1;
        const float x = f->current();
        if (x == 440.25f) return 0;
        if (x == 0.375f) return 1;
        if (x == 0.6875f) return 4;
        if (x == 3.0f) return 5;
        if (x > 0.0f && x < 0.5f) return 2;
        if (x == 0.0f) return 3;
        return -1;
    }
    catch (const std::exception& e) { refusal = e.what(); }
    return -2;
}

// A PerformNode's field is read against the type's declared labels: a label
// exactly (the label in upper case is refused), an index, absent as the
// first; the adapter is the voice's own.
void run_perform_node_field_tests() {
    using J = nlohmann::json;
    std::printf("PerformNode: the field against its declared labels\n");
    auto field_node = [](const J& v) { return J{{"id", "f"}, {"type", "PerformNode"}, {"params", J{{"field", v}}}}; };
    std::string refusal;
    for (int k = 0; k < 6; ++k) {
        const std::string label = PerformNode::kFieldLabels[k];
        std::string upper = label;
        for (auto& c : upper) c = char(std::toupper(static_cast<unsigned char>(c)));
        CHECK(perform_field_of(field_node(label), refusal) == k);
        refusal.clear();
        CHECK(perform_field_of(field_node(upper), refusal) == -2);
        CHECK(refusal.find("'field' holds \"" + upper + "\", which is not one of its labels") != std::string::npos);
        CHECK(perform_field_of(field_node(k), refusal) == k);
    }
    CHECK(perform_field_of(J{{"id", "f"}, {"type", "PerformNode"}}, refusal) == 0);
    CHECK(perform_field_of(J{{"id", "f"}, {"type", "PerformNode"}, {"params", J::object()}}, refusal) == 0);
    for (const J& bad : {J("bogus"), J(6), J(2.5), J(true)}) {
        refusal.clear();
        CHECK(perform_field_of(field_node(bad), refusal) == -2);
        const std::string want = "node 'f' (PerformNode): 'field' holds " + bad.dump()
                               + ", which is not one of its labels (frequency, velocity, wheel, pressure, duration, onset) or an index into them";
        if (!refusal_says(refusal, want)) std::printf("  %s: %s\n", bad.dump().c_str(), refusal.c_str());
        CHECK(refusal_says(refusal, want));
    }
    // the bend check reads the field the same way: an index loads beside a node that reads frequency once
    bool loaded = true;
    try {
        (void)load_inst(R"({"sampleRate":48000,"instrument":{"polyphony":1},"graph":{"output":"ks","nodes":[
          {"id":"f","type":"PerformNode","params":{"field":0}},
          {"id":"ks","type":"KSString","params":{"frequency":{"ref":"f"}}}]}})");
    } catch (const std::exception& e) { std::printf("  load failed: %s\n", e.what()); loaded = false; }
    CHECK(loaded);
    CHECK(file_fails_naming("renders/scratch/_plain_perform.json",
        R"({"sampleRate":48000,"seconds":0.1,"graph":{"output":"osc","nodes":[
          {"id":"f","type":"PerformNode","params":{"field":"velocity"}},
          {"id":"osc","type":"SineSource","params":{"frequency":220.0,"amplitude":{"ref":"f"}}}]}})", "node 'f' (PerformNode): a plain sound has no note to read"));
}

// A held line of three notes on an instrument whose amplitude Envelope holds
// `trigger` as given, each note shorter than the Envelope's first stage so it
// is mid-flight at every Setup.
static std::string trigger_line_patch(int trigger) {
    return std::string(R"({"sampleRate":48000,"instrument":{"polyphony":1,"sustaining":true},
      "graph":{"output":"sine1","nodes":[
        {"id":"env1","type":"Envelope","params":{"timeMode":"seconds","trigger":)")
         + std::to_string(trigger) + R"(,
          "startVal":[1.0,0.1,1.0],"endVal":[0.1,1.0,1.0],"power":[0.0,0.0,0.0],
          "type":["Linear","Linear","Linear"],"percent":[0.15,0.15,0.0],"minSec":[0.0,0.0,0.0],
          "maxSec":[0.0,0.0,0.0],"nominal":[0.0,0.0,0.0]}},
        {"id":"sine1","type":"SineSource","params":{"frequency":220.0,"amplitude":{"ref":"env1"},"phase":0.0}}]}})";
}

// Loads trigger_line_patch(trigger) and renders the line. A `constant` of 0
// or more first unwires every voice's env1.trigger and sets the pin's own
// constant to it. `listed` says whether env1 is in every voice's trigger
// bindings with no wire on its trigger: a number is the pin's own constant.
static std::vector<float> render_trigger_line(int trigger, int constant, bool& listed) {
    auto ip = load_inst(trigger_line_patch(trigger));
    auto* pi = dynamic_cast<PitchedInstrument*>(ip.instrument.get());
    listed = pi != nullptr && !pi->pool().empty();
    if (!pi) return {};
    for (auto& vg : pi->pool()) {
        auto it = vg.nodes().find("env1");
        auto env = it == vg.nodes().end() ? nullptr : std::dynamic_pointer_cast<Envelope>(it->second);
        if (!env) { listed = false; continue; }
        if (constant >= 0) {
            env->clear_param("trigger");
            env->trigger.constant = float(constant);
        }
        listed = listed && env->trigger.source == nullptr
              && std::find(vg.description().triggerEnvelopes.begin(), vg.description().triggerEnvelopes.end(),
                           "env1") != vg.description().triggerEnvelopes.end();
    }
    play_notes(*pi, {{.start = 0.0f, .duration = 0.1f, .noteNumber = 57.0f, .velocity = 0.8f, .onset = 0, .hold = true},
                     {.start = 0.1f, .duration = 0.1f, .noteNumber = 59.0f, .velocity = 0.8f, .onset = 0, .hold = true},
                     {.start = 0.2f, .duration = 0.1f, .noteNumber = 60.0f, .velocity = 0.8f, .onset = 0, .hold = false}});
    std::vector<float> buf(size_t(0.5f * 48000), 0.0f);
    ip.instrument->render(buf.data(), int(buf.size()));
    return buf;
}

// fire_triggers asks the trigger's value: the pin's own constant 1 restarts
// the Envelope, as loaded or set by hand, and a constant 0 does not.
void run_trigger_value_tests() {
    std::printf("fire_triggers: an Envelope restarts on its trigger's value, wired or not\n");
    bool listedA = false, listedB = false, listedC = false, listedD = false;
    const auto a = render_trigger_line(1, -1, listedA);   // as loaded: the trigger the pin's own constant 1
    const auto b = render_trigger_line(1, 1, listedB);    // unwired, the pin's constant 1
    const auto c = render_trigger_line(1, 0, listedC);    // unwired, the pin's constant 0
    const auto d = render_trigger_line(0, 0, listedD);    // "trigger": 0, the pin's own 0, which binds nothing
    CHECK(listedA && listedB && listedC);
    CHECK(!listedD);
    CHECK(!a.empty() && a == b);
    CHECK(c != a);
    CHECK(c == d);
}

// The mixer's listed signals report under their ids after the channels'
// reports, each once, as their bits are taken; with none listed the mixer
// reports what its channels report.
void run_mixer_signal_fault_tests() {
    std::printf("Mixer: the listed signals report under their ids, after the channels\n");
    struct OnceFaultSource final : MonoSource {
        bool reported{false};
        void prepare(const RenderContext&) override {}
        void render(float* out, int frames) override { std::fill(out, out + frames, 0.0f); }
        void collect_faults(std::vector<FaultReport>& out) override {
            if (!reported) out.push_back({"ch", "OnceFault", fault_bit(Fault::TableClamped)});
            reported = true;
        }
    };
    TestHost h;
    auto a = std::make_shared<SineSource>();
    a->set_param("frequency", konst(440.0f));
    auto b = std::make_shared<SineSource>();
    b->set_param("frequency", konst(0.0f));               // clamped: a fault
    a->prepare(h.ctx); b->prepare(h.ctx);
    a->start(16); b->start(16);
    for (int i = 0; i < 4; ++i) { h.clock.tick(); a->next(); b->next(); }
    Mixer m;
    m.channels.push_back(Channel{std::make_unique<OnceFaultSource>(), nullptr, nullptr});
    m.signals = {{"a", a}, {"b", b}};
    std::vector<FaultReport> got;
    m.collect_faults(got);
    CHECK(got.size() == 2);
    CHECK(got.size() == 2 && got[0].nodeId == "ch" && got[1].nodeId == "b");
    CHECK(got.size() == 2 && std::string(got[1].type) == b->type_name()
          && got[1].faults == fault_bit(Fault::FrequencyClamped));
    got.clear();
    m.collect_faults(got);
    CHECK(got.empty());                                   // the bits were taken
    Mixer plain;
    plain.channels.push_back(Channel{std::make_unique<OnceFaultSource>(), nullptr, nullptr});
    plain.collect_faults(got);
    CHECK(got.size() == 1 && got[0].nodeId == "ch" && got[0].faults == fault_bit(Fault::TableClamped));
}
