#include "mforce/testing/check.h"
#include "test_support.h"
#include "patch/tests/fixtures.h"
#include "mforce/core/dsp_value_source.h"
#include "mforce/core/pins.h"
#include "mforce/patch/build_graph.h"
#include "mforce/patch/graph.h"
#include "mforce/patch/instrument_block.h"
#include "mforce/patch/note_signals.h"
#include "mforce/patch/patch.h"
#include "mforce/render/build_instrument.h"
#include "mforce/render/build_sound.h"
#include "mforce/render/perform_source.h"
#include "mforce/render/pitched_instrument.h"
#include "mforce/render/voice.h"
#include "mforce/source/combiner/multiplex_source.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <exception>
#include <memory>
#include <string>
#include <vector>

using namespace mforce;

namespace patch_codec {
// A patch read from JSON text through the codec; throws as from_json does.
static Patch load_text(const std::string& text) { return patch_door::read_text(text); }
}  // namespace patch_codec
namespace build_tests {
using patch_codec::load_text;
using patch_door::same_samples;

// The six note signals as the constants 1 to 6, each its own object.
static NoteSignals constant_note() {
    const auto c = [](float v) { return std::make_shared<ConstantSource>(v); };
    return NoteSignals{c(1.0f), c(2.0f), c(3.0f), c(4.0f), c(5.0f), c(6.0f)};
}
// An instrument around `nodes` (the inside of a JSON array), `output` its output.
static std::string inst(const std::string& nodes, const char* output,
                        const std::string& block = R"({"polyphony":1})") {
    return R"({"instrument":)" + block + R"(,"graph":{"output":")" + output + R"(","nodes":[)" + nodes + "]}}";
}
// A plain sound around `nodes`, `output` its output, a quarter second long.
static std::string plain(const std::string& nodes, const char* output) {
    return std::string(R"({"seconds":0.25,"graph":{"output":")") + output + R"(","nodes":[)" + nodes + "]}}";
}
// The refusal a graph's build gives, whole; empty when it builds.
static std::string build_refusal(const Graph& g, bool withNote = true, const InstrumentBlock* block = nullptr) {
    const NoteSignals note = constant_note();
    try { (void)build_graph(g, withNote ? &note : nullptr, block); } catch (const std::exception& e) { return e.what(); }
    return {};
}
static std::string text_refusal(const std::string& text) { return build_refusal(load_text(text).graph); }
// Three notes on the offline path: a held one, the note that ends its line, and one alone.
static std::vector<float> play_three(PitchedInstrument& pi, int onset = 0) {
    play_notes(pi, {{.start = 0.0f, .duration = 0.4f, .noteNumber = 48.0f, .velocity = 0.9f, .onset = onset, .hold = true},
                    {.start = 0.4f, .duration = 0.4f, .noteNumber = 60.0f, .velocity = 0.4f, .onset = onset, .hold = false},
                    {.start = 1.0f, .duration = 0.4f, .noteNumber = 67.0f, .velocity = 0.9f, .onset = onset, .hold = false}});
    std::vector<float> b(size_t(2 * 48000), 0.0f);
    pi.render(b.data(), int(b.size()));
    return b;
}
// Instruments around a MultiplexSource, a1 to a7 (the synthetic set's files of
// those names, without their layout, score and length), and a NameGate inside
// a Multiplex template.
struct Named { const char* name; const char* text; };
static const Named kMultiplexTexts[] = {
    {"a1", R"J({"graph":{"nodes":[{"id":"pf","type":"PerformNode","params":{"field":"frequency"}},{"id":"amp","type":"Envelope","params":{"preset":"adsr","attack":0.02,"decay":0.1,"release":0.1,"sustainLevel":0.7}},{"id":"N","type":"WhiteNoiseSource","params":{"amplitude":0.3,"seed":1101}},{"id":"S","type":"SawSource","params":{"amplitude":0.3,"frequency":{"ref":"pf"},"phase":0.0}},{"id":"F","type":"SVFSource","params":{"source":{"ref":"S"},"cutoffFreq":1500.0,"resonance":0.3,"mode":"Lowpass"}},{"id":"R","type":"CombinedSource","params":{"source1":{"ref":"N"},"source2":{"ref":"F"},"operation":"Sum","gainAdj":0.0,"seed":1201}},{"id":"mux","type":"MultiplexSource","params":{"count":5,"source":{"ref":"R"}}},{"id":"out","type":"CombinedSource","params":{"source1":{"ref":"mux"},"source2":{"ref":"amp"},"operation":"Multiply","gainAdj":0.0}}],"output":"out"},"instrument":{"polyphony":1,"volume":0.5},"sampleRate":48000})J"},
    {"a2", R"J({"graph":{"nodes":[{"id":"pfF","type":"PerformNode","params":{"field":"frequency"}},{"id":"pfV","type":"PerformNode","params":{"field":"velocity"}},{"id":"amp","type":"Envelope","params":{"preset":"adsr","attack":0.02,"decay":0.1,"release":0.1,"sustainLevel":0.7}},{"id":"S","type":"SawSource","params":{"amplitude":0.4,"frequency":{"ref":"pfF"},"phase":0.0}},{"id":"N","type":"WhiteNoiseSource","params":{"amplitude":0.1,"seed":1301}},{"id":"SN","type":"CombinedSource","params":{"source1":{"ref":"S"},"source2":{"ref":"N"},"operation":"Sum","gainAdj":0.0}},{"id":"R","type":"CombinedSource","params":{"source1":{"ref":"SN"},"source2":{"ref":"pfV"},"operation":"Multiply","gainAdj":0.0}},{"id":"mux","type":"MultiplexSource","params":{"count":3,"source":{"ref":"R"}}},{"id":"out","type":"CombinedSource","params":{"source1":{"ref":"mux"},"source2":{"ref":"amp"},"operation":"Multiply","gainAdj":0.0}}],"output":"out"},"instrument":{"polyphony":1,"volume":0.5},"sampleRate":48000})J"},
    {"a3", R"J({"graph":{"nodes":[{"id":"pf","type":"PerformNode","params":{"field":"frequency"}},{"id":"amp","type":"Envelope","params":{"preset":"adsr","attack":0.02,"decay":0.1,"release":0.1,"sustainLevel":0.7}},{"id":"N","type":"WhiteNoiseSource","params":{"amplitude":0.2,"seed":1401}},{"id":"S","type":"SawSource","params":{"amplitude":0.3,"frequency":{"ref":"pf"},"phase":0.0}},{"id":"R2","type":"CombinedSource","params":{"source1":{"ref":"N"},"source2":{"ref":"S"},"operation":"Sum","gainAdj":0.0}},{"id":"inner","type":"MultiplexSource","params":{"count":2,"source":{"ref":"R2"}}},{"id":"F","type":"SVFSource","params":{"source":{"ref":"inner"},"cutoffFreq":2000.0,"resonance":0.3,"mode":"Lowpass"}},{"id":"outer","type":"MultiplexSource","params":{"count":3,"source":{"ref":"F"}}},{"id":"out","type":"CombinedSource","params":{"source1":{"ref":"outer"},"source2":{"ref":"amp"},"operation":"Multiply","gainAdj":0.0}}],"output":"out"},"instrument":{"polyphony":1,"volume":0.5},"sampleRate":48000})J"},
    {"a4", R"J({"graph":{"nodes":[{"id":"pf","type":"PerformNode","params":{"field":"frequency"}},{"id":"E","type":"Envelope","params":{"preset":"adsr","attack":0.02,"decay":0.1,"release":0.1,"sustainLevel":0.7,"stage_accuracy":0.6}},{"id":"R","type":"SawSource","params":{"amplitude":{"ref":"E"},"frequency":{"ref":"pf"},"phase":0.0}},{"id":"mux","type":"MultiplexSource","params":{"count":4,"source":{"ref":"R"}}}],"output":"mux"},"instrument":{"polyphony":1,"volume":0.5},"sampleRate":48000})J"},
    {"a5", R"J({"graph":{"nodes":[{"id":"pf","type":"PerformNode","params":{"field":"frequency"}},{"id":"amp","type":"Envelope","params":{"preset":"adsr","attack":0.02,"decay":0.1,"release":0.1,"sustainLevel":0.7}},{"id":"N","type":"WhiteNoiseSource","params":{"amplitude":0.3,"seed":1501}},{"id":"F","type":"SVFSource","params":{"source":{"ref":"N"},"cutoffFreq":800.0,"resonance":0.6,"mode":"Lowpass"}},{"id":"mux","type":"MultiplexSource","params":{"count":3,"source":{"ref":"F"}}},{"id":"S","type":"SawSource","params":{"amplitude":0.2,"frequency":{"ref":"pf"},"phase":0.0}},{"id":"sum","type":"CombinedSource","params":{"source1":{"ref":"mux"},"source2":{"ref":"S"},"operation":"Sum","gainAdj":0.0}},{"id":"out","type":"CombinedSource","params":{"source1":{"ref":"sum"},"source2":{"ref":"amp"},"operation":"Multiply","gainAdj":0.0}}],"output":"out"},"instrument":{"polyphony":1,"volume":0.5},"sampleRate":48000})J"},
    {"a6a", R"J({"graph":{"nodes":[{"id":"pf","type":"PerformNode","params":{"field":"frequency"}},{"id":"amp","type":"Envelope","params":{"preset":"adsr","attack":0.02,"decay":0.1,"release":0.1,"sustainLevel":0.7}},{"id":"T","type":"SawSource","params":{"amplitude":0.3,"frequency":{"ref":"pf"},"phase":0.0}},{"id":"mux","type":"MultiplexSource","params":{"count":3,"source":{"tap":"T"}}},{"id":"S0","type":"SineSource","params":{"amplitude":0.3,"frequency":{"ref":"pf"},"phase":0.0}},{"id":"sum","type":"CombinedSource","params":{"source1":{"ref":"S0"},"source2":{"ref":"mux"},"operation":"Sum","gainAdj":0.0}},{"id":"out","type":"CombinedSource","params":{"source1":{"ref":"sum"},"source2":{"ref":"amp"},"operation":"Multiply","gainAdj":0.0}}],"output":"out"},"instrument":{"polyphony":1,"volume":0.5},"sampleRate":48000})J"},
    {"a6b", R"J({"graph":{"nodes":[{"id":"pf","type":"PerformNode","params":{"field":"frequency"}},{"id":"amp","type":"Envelope","params":{"preset":"adsr","attack":0.02,"decay":0.1,"release":0.1,"sustainLevel":0.7}},{"id":"mux","type":"MultiplexSource","params":{"count":3}},{"id":"S0","type":"SineSource","params":{"amplitude":0.3,"frequency":{"ref":"pf"},"phase":0.0}},{"id":"sum","type":"CombinedSource","params":{"source1":{"ref":"S0"},"source2":{"ref":"mux"},"operation":"Sum","gainAdj":0.0}},{"id":"out","type":"CombinedSource","params":{"source1":{"ref":"sum"},"source2":{"ref":"amp"},"operation":"Multiply","gainAdj":0.0}}],"output":"out"},"instrument":{"polyphony":1,"volume":0.5},"sampleRate":48000})J"},
    {"a7", R"J({"graph":{"nodes":[{"id":"pf","type":"PerformNode","params":{"field":"frequency"}},{"id":"amp","type":"Envelope","params":{"preset":"adsr","attack":0.02,"decay":0.1,"release":0.1,"sustainLevel":0.7}},{"id":"X","type":"SawSource","params":{"amplitude":0.3,"frequency":{"ref":"pf"},"phase":0.0}},{"id":"N","type":"WhiteNoiseSource","params":{"amplitude":0.05,"seed":1701}},{"id":"XN","type":"CombinedSource","params":{"source1":{"ref":"X"},"source2":{"ref":"N"},"operation":"Sum","gainAdj":0.0}},{"id":"Y","type":"CombinedSource","params":{"source1":{"tap":"XN"},"source2":0.5,"operation":"Multiply","gainAdj":0.0}},{"id":"R","type":"CombinedSource","params":{"source1":{"ref":"XN"},"source2":{"ref":"Y"},"operation":"Sum","gainAdj":0.0}},{"id":"mux","type":"MultiplexSource","params":{"count":3,"source":{"ref":"R"}}},{"id":"out","type":"CombinedSource","params":{"source1":{"ref":"mux"},"source2":{"ref":"amp"},"operation":"Multiply","gainAdj":0.0}}],"output":"out"},"instrument":{"polyphony":1,"volume":0.5},"sampleRate":48000})J"},
};
static const char* const kGateInTemplate = R"J({"graph":{"nodes":[{"id":"pfF","type":"PerformNode","params":{"field":"frequency"}},{"id":"pfO","type":"PerformNode","params":{"field":"onset"}},{"id":"ng","type":"NameGate","params":{"name":"tongue","in":{"ref":"pfO"}}},{"id":"gest","type":"Envelope","params":{"minValue":0.0,"maxValue":1.0,"ramp_accuracy":1.0,"stage_accuracy":1.0,"timeMode":"seconds","timeScale":1.0,"startVal":[1.0,0.1,1.0],"endVal":[0.1,1.0,1.0],"power":[0.0,0.0,0.0],"type":["Linear","Linear","Linear"],"percent":[0.01,0.02,0.0],"minSec":[0.0,0.0,0.0],"maxSec":[0.0,0.0,0.0],"nominal":[0.0,0.0,0.0],"trigger":{"ref":"ng"}}},{"id":"lift","type":"CombinedSource","params":{"source1":{"ref":"ng"},"source2":1.0,"operation":"Sum","gainAdj":0.0}},{"id":"amp2","type":"CombinedSource","params":{"source1":{"ref":"gest"},"source2":{"ref":"lift"},"operation":"Multiply","gainAdj":0.0}},{"id":"R","type":"SawSource","params":{"amplitude":{"ref":"amp2"},"frequency":{"ref":"pfF"},"phase":0.0}},{"id":"mux","type":"MultiplexSource","params":{"count":2,"source":{"ref":"R"}}}],"output":"mux"},"instrument":{"polyphony":1,"volume":0.5,"onsets":["tongue"],"sustaining":true},"sampleRate":48000})J";
}  // namespace build_tests

// The template on the Multiplex texts: each plays something. A NameGate
// inside a template resolves against the block's onsets: it fires on a name
// the block holds and not on one it lacks.
void run_build_template_reader_tests() {
    using namespace build_tests;
    std::printf("the template: the Multiplex files play; a gate in the template fires on the block's onset\n");
    for (const auto& t : kMultiplexTexts) {
        auto mod = build_instrument(load_text(t.text));
        const auto b = play_three(*mod);
        const bool audible = std::any_of(b.begin(), b.end(), [](float s) { return s != 0.0f; });
        if (!audible) std::printf("  %s: silent\n", t.name);
        CHECK(audible);
    }
    std::string outside = kGateInTemplate;
    outside.replace(outside.find(R"("name":"tongue")"), 15, R"("name":"slap")");
    auto mod = build_instrument(load_text(kGateInTemplate));
    auto modOutside = build_instrument(load_text(outside));
    const auto b = play_three(*mod, 1), c = play_three(*modOutside, 1);
    CHECK(!same_samples(b, c));                  // the clone's gate fires on the block's onset, not on a name it lacks
}

// The closure holds copies: an instrument's Multiplex rebuilt after the
// patch, its graph and the note's holder that made it are gone renders as a
// fresh one. A dynamic pin and a tap-only tail inside a template are refused.
void run_build_template_closure_tests() {
    using namespace build_tests;
    std::printf("the template's closure outlives its patch; the two refusals inside a template\n");
    std::unique_ptr<PitchedInstrument> kept;
    {
        const Patch p = load_text(kMultiplexTexts[0].text);
        kept = build_instrument(p);
    }
    static_cast<MultiplexSource&>(*kept->pool().voice(0).nodes().at("mux")).mark_dirty();
    kept->prepare(RenderContext{48000});
    auto fresh = build_instrument(load_text(kMultiplexTexts[0].text));
    CHECK(same_samples(play_three(*kept), play_three(*fresh)));
    CHECK(text_refusal(inst(R"({"id":"pf","type":"PerformNode"},{"id":"c","type":"SineSource"},)"
                            R"({"id":"env","type":"Envelope","dynamicPins":{"timeScale":{"ref":"c"}}},)"
                            R"({"id":"R","type":"SawSource","params":{"amplitude":{"ref":"env"},"frequency":{"ref":"pf"}}},)"
                            R"({"id":"mux","type":"MultiplexSource","params":{"source":{"ref":"R"}}})", "mux")) ==
          "node 'env' (Envelope): a dynamic pin inside the template of 'mux' is not supported: each clone would need its own");
    CHECK(text_refusal(inst(R"({"id":"T","type":"SawSource"},{"id":"Y","type":"CombinedSource","params":{"source1":{"tap":"T"}}},)"
                            R"({"id":"mux","type":"MultiplexSource","params":{"source":{"ref":"Y"}}})", "mux")) ==
          "node 'T' (SawSource): inside the template of 'mux' only a tap reads it, and nothing advances a clone's tail");
}

// A record with two nodes under one id never passed check(): each builder
// refuses it naming the second node, and none crashes.
void run_build_duplicate_id_tests() {
    using namespace build_tests;
    std::printf("the builder refuses two nodes under one id\n");
    const auto refused = [](const auto& build) {
        try { build(); } catch (const std::exception& e) { return std::string(e.what()); }
        return std::string();
    };
    Patch env = load_text(inst(R"({"id":"x","type":"Envelope"},{"id":"y","type":"SineSource"})", "x"));
    env.graph.nodes[1].id = "x";
    const std::string want = "node 'x' (SineSource): the id 'x' names an earlier node too; an id names one node";
    CHECK(build_refusal(env.graph) == want);
    CHECK(refused([&] { (void)build_instrument(env); }) == want);
    Patch mix = load_text(plain(R"({"id":"o","type":"SineSource"},{"id":"ch","type":"SoundChannel","params":{"source":{"ref":"o"}}},)"
                                R"({"id":"m","type":"StereoMixer","params":{"channels":[{"ref":"ch"}]}},{"id":"s","type":"SawSource"})", "m"));
    mix.graph.nodes[3].id = "m";
    CHECK(refused([&] { (void)build_sound(mix); }) == "node 'm' (SawSource): the id 'm' names an earlier node too; an id names one node");
}

// build_instrument: the block's five fields, the floor, every voice's lists,
// the warnings once; a part as the output and a patch with no block refused.
void run_build_instrument_tests() {
    using namespace build_tests;
    std::printf("build_instrument: the block, the floor, each voice's lists, the warnings once\n");
    const Patch p = load_text(inst(
        R"({"id":"pf","type":"PerformNode"},{"id":"g","type":"NameGate","params":{"name":"x"}},)"
        R"({"id":"env","type":"Envelope","params":{"trigger":1}},{"id":"env0","type":"Envelope"},)"
        R"({"id":"envW","type":"Envelope","params":{"trigger":{"ref":"g"}}},{"id":"T","type":"SineSource"},)"
        R"({"id":"c","type":"SineSource"},{"id":"ks","type":"KSString","dynamicPins":{"t60":{"ref":"c"}},"params":{"frequency":{"ref":"pf"}}},)"
        R"({"id":"m","type":"CombinedSource","params":{"source1":{"ref":"env"},"source2":{"ref":"env0"},"operation":"Multiply"}},)"
        R"({"id":"m2","type":"CombinedSource","params":{"source1":{"ref":"m"},"source2":{"ref":"envW"},"operation":"Multiply"}},)"
        R"({"id":"m3","type":"CombinedSource","params":{"source1":{"ref":"m2"},"source2":{"tap":"T"},"operation":"Sum"}},)"
        R"({"id":"out","type":"CombinedSource","params":{"source1":{"ref":"m3"},"source2":{"ref":"ks"},"operation":"Sum"}})",
        "out", R"({"polyphony":3,"volume":0.5,"sustaining":true,"onsets":["tongue"],"glideMs":20})"));
    std::vector<std::string> w;
    const auto pi = build_instrument(p, 0, &w);
    CHECK(pi->pool().size() == 3 && pi->volume == 0.5f && pi->instrument_class().sustaining && pi->glide_seconds() == 20.0f / 1000.0f);
    CHECK(pi->instrument_class().onsets == std::vector<std::string>({"tongue"}));
    CHECK(w.size() == 2);                        // the gate's name and the bend: once, not once per voice
    CHECK(build_instrument(p, 5)->pool().size() == 5 && build_instrument(p, 2)->pool().size() == 3);
    for (const auto& vg : pi->pool()) {
        const auto at = [&](const char* id) { return vg.nodes().at(id).get(); };
        const Voice::Description& d = vg.description();
        CHECK(d.output == "out");
        CHECK(d.heldEnvelopes == std::vector<std::string>({"env", "env0", "envW"}));
        CHECK(d.triggerEnvelopes == std::vector<std::string>({"env", "envW"}));
        CHECK(d.dynamicPins.size() == 1 && d.dynamicPins[0].node == "ks" &&
              d.dynamicPins[0].setting == "t60" && d.dynamicPins[0].source == "c");
        CHECK(d.tails == std::vector<std::string>({"T"}));
        CHECK(vg.signals().size() == vg.nodes().size() && std::is_sorted(vg.signals().begin(), vg.signals().end(),
                                                                         [](const auto& x, const auto& y) { return x.id < y.id; }));
        // pf is the voice's frequency adapter: at rest it reads the PerformSource's 440 Hz.
        const auto* pf = dynamic_cast<const PerformOut*>(at("pf"));
        CHECK(pf != nullptr && pf->current() == 440.0f);
    }
    CHECK(pi->pool().voice(0).nodes().at("out") != pi->pool().voice(1).nodes().at("out"));
    Patch partOut = load_text(inst(R"({"id":"o","type":"SineSource"},{"id":"parts","type":"FullPartials"})", "o"));
    partOut.graph.output = "parts";
    std::string msg;
    try { (void)build_instrument(partOut); } catch (const std::exception& e) { msg = e.what(); }
    CHECK(msg == "'parts' is a FullPartials, a part, not a signal");
    msg.clear();
    try { (void)build_instrument(load_text(plain(R"({"id":"o","type":"SineSource"})", "o"))); } catch (const std::exception& e) { msg = e.what(); }
    CHECK(msg == "Patch has no 'instrument' section");
    // The voice learns the patch's rate: a 100 Hz sine rises through zero 100
    // times in 22050 samples at 22050.
    const Patch slow = load_text(R"({"sampleRate":22050,"instrument":{"polyphony":1},)"
                                 R"("graph":{"output":"o","nodes":[{"id":"o","type":"SineSource","params":{"frequency":100.0}}]}})");
    auto slowInst = build_instrument(slow);
    play_notes(*slowInst, {note_at(0.0f, 1.5f, 60.0f, 0.8f)});
    const auto& slowNote = slowInst->rendered().at(0).samples;
    int rises = 0;
    for (size_t i = 1; i < 22050 && i < slowNote.size(); ++i) rises += slowNote[i - 1] < 0.0f && slowNote[i] >= 0.0f;
    CHECK(slowInst->render_context().sampleRate == 22050 && std::abs(rises - 100) <= 1);
}
