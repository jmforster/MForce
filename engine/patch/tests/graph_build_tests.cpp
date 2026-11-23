#include "mforce/testing/check.h"
#include "test_support.h"
#include "patch/tests/fixtures.h"
#include "mforce/core/dsp_value_source.h"
#include "mforce/core/pins.h"
#include "mforce/core/render_context.h"
#include "mforce/patch/build_graph.h"
#include "mforce/patch/built_graph.h"
#include "mforce/patch/graph.h"
#include "mforce/patch/instrument_block.h"
#include "mforce/patch/node.h"
#include "mforce/patch/note_signals.h"
#include "mforce/patch/patch.h"
#include "mforce/patch/type_declarations.h"
#include "mforce/source/combiner/multi_source.h"
#include "mforce/source/modulator/name_gate.h"
#include "mforce/source/modulator/perform_node.h"
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace mforce;

namespace patch_door {
static bool same_samples(const std::vector<float>& a, const std::vector<float>& b) {
    return a.size() == b.size() && (a.empty() || std::memcmp(a.data(), b.data(), a.size() * sizeof(float)) == 0);
}
}  // namespace patch_door
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
// `text` read through the codec, its graph built with the constant note and the patch's block.
static BuiltGraph built(const std::string& text) {
    const Patch p = load_text(text);
    const NoteSignals note = constant_note();
    return build_graph(p.graph, &note, p.instrument ? &*p.instrument : nullptr);
}
// The refusal a graph's build gives, whole; empty when it builds.
static std::string build_refusal(const Graph& g, bool withNote = true, const InstrumentBlock* block = nullptr) {
    const NoteSignals note = constant_note();
    try { (void)build_graph(g, withNote ? &note : nullptr, block); } catch (const std::exception& e) { return e.what(); }
    return {};
}
static std::string text_refusal(const std::string& text) { return build_refusal(load_text(text).graph); }
// One feed by ref, one by tap.
static std::vector<Wire> ref(const char* id) { return {Wire{id, false}}; }
static std::vector<Wire> tap(const char* id) { return {Wire{id, true}}; }
// A node of the record made by hand: one pin's feeds, or none.
static Node node(const char* id, const char* type, const char* pin = nullptr, std::vector<Wire> feeds = {}) {
    Node n;
    n.id = id;
    n.type = type;
    if (pin) n.wires[pin] = std::move(feeds);
    return n;
}
static Graph graph_of(std::vector<Node> nodes, const char* output) {
    Graph g;
    g.nodes = std::move(nodes);
    g.output = output;
    return g;
}
// A Shaper whose values2 is not as long as its values, which validate()
// refuses; `source` names the node it reads, or none.
static std::string bad_shaper(const char* id, const char* source = nullptr) {
    return std::string(R"({"id":")") + id + R"(","type":"Shaper","params":{"values2":[-1,-1,0,0,1,1])" +
           (source ? std::string(R"(,"source":{"ref":")") + source + R"("})" : std::string()) + "}}";
}
const std::string kShaperRefusal = "values2 length must match values";
// `src` rendered for `n` samples on a clock of its own at 48 kHz.
static std::vector<float> render_signal(ValueSource& src, int n) {
    SampleClock clock;
    src.prepare(RenderContext{48000}.with_clock(&clock));
    src.start(n);
    std::vector<float> out(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) { clock.tick(); out[size_t(i)] = src.next(); }
    return out;
}
}  // namespace build_tests

// The order of construction: the file's, where every feed comes first; a
// node a feed names later is built before its reader, the rest in the file's
// order; a dynamic pin's later source builds; a cycle with no tap, and a node
// reading itself, are refused naming their nodes; a tap closes a loop.
void run_build_sort_tests() {
    using namespace build_tests;
    std::printf("the builder's order: the file's where feeds come first; cycles refused by name\n");
    CHECK(text_refusal(inst(bad_shaper("A") + "," + bad_shaper("B"), "A")) == "node 'A' (Shaper): " + kShaperRefusal);
    CHECK(text_refusal(inst(bad_shaper("B") + "," + bad_shaper("A"), "A")) == "node 'B' (Shaper): " + kShaperRefusal);
    CHECK(text_refusal(inst(bad_shaper("A", "C") + "," + bad_shaper("C"), "A")) == "node 'C' (Shaper): " + kShaperRefusal);
    CHECK(text_refusal(inst(bad_shaper("A", "C") + "," + bad_shaper("B") + R"(,{"id":"C","type":"SineSource"})", "A")) ==
          "node 'B' (Shaper): " + kShaperRefusal);
    const BuiltGraph pinned = built(inst(
        R"({"id":"env","type":"Envelope","dynamicPins":{"timeScale":{"ref":"late"}}},)"
        R"({"id":"out","type":"SawSource","params":{"amplitude":{"ref":"env"}}},{"id":"late","type":"SineSource"})", "out"));
    CHECK(pinned.dynamicPins.size() == 1 && pinned.dynamicPins[0].source.get() == pinned.nodes.at("late").get());
    const Graph cycle = graph_of({node("A", "SineSource", "amplitude", ref("B")), node("B", "SineSource", "amplitude", ref("A"))}, "A");
    CHECK(build_refusal(cycle) == "a cycle with no tap in it: A -> B -> A");
    CHECK(build_refusal(graph_of({node("A", "SineSource", "amplitude", ref("A"))}, "A")) == "a cycle with no tap in it: A -> A");
    Graph loop = cycle;
    loop.nodes[1].wires["amplitude"] = tap("A");
    CHECK(build_refusal(loop).empty());
    CHECK(build_refusal(graph_of({node("A", "SineSource", "amplitude", ref("ghost"))}, "A")) == "Unresolved node ref: ghost");
    CHECK(build_refusal(graph_of({node("A", "SineSource", "amplitude", tap("ghost"))}, "A")) == "Unresolved tap target: ghost");
    CHECK(build_refusal(graph_of({node("A", "SineSource")}, "")) == "the patch has no output");
    CHECK(build_refusal(graph_of({node("A", "NoSuchSource")}, "A")) == "Unknown node type: NoSuchSource");
}

// The fill: a Param's number its own constant with nothing wired, an Input's
// a wired constant; a list's feeds in order; a slot's part; a setting, a
// string, an array, a labelled column by index; settings before arrays.
void run_build_fill_tests() {
    using namespace build_tests;
    std::printf("the builder's fill: a Param's number, an Input's, feeds in order, settings before arrays\n");
    const BuiltGraph g = built(inst(
        R"({"id":"a","type":"SineSource","params":{"amplitude":0.3,"frequency":220}},{"id":"b","type":"SawSource"},)"
        R"({"id":"sum","type":"CombinedSource","params":{"source1":{"ref":"a"},"source2":0.5}},)"
        R"({"id":"list","type":"MultiSource","params":{"source":[{"ref":"b"},{"ref":"a"}]}},)"
        R"({"id":"parts","type":"FullPartials","params":{"maxPartials":4}},)"
        R"({"id":"add","type":"AdditiveSource","params":{"partials":{"ref":"parts"}}},)"
        R"({"id":"svf","type":"SVFSource","params":{"source":{"ref":"b"},"mode":"Highpass"}},)"
        R"({"id":"gate","type":"NameGate","params":{"name":"tongue"}},{"id":"seg","type":"SegmentSource","params":{"values":[0,1,0.5]}},)"
        R"({"id":"env","type":"Envelope","params":{)" + patch_codec::stage_columns("[0,3]") + "}}", "sum"));
    Param* amp = g.nodes.at("a")->param("amplitude");
    CHECK(amp && amp->source == nullptr && amp->constant == 0.3f);
    const auto s2 = g.nodes.at("sum")->get_param("source2");
    const auto* k = dynamic_cast<const ConstantSource*>(s2.get());
    CHECK(k && k->current() == 0.5f);
    CHECK(g.nodes.at("sum")->get_param("source1").get() == g.nodes.at("a").get());
    const auto* list = dynamic_cast<const MultiSource*>(g.nodes.at("list").get());
    CHECK(list && list->source.sources.size() == 2 && list->source.sources[0].get() == g.nodes.at("b").get() &&
          list->source.sources[1].get() == g.nodes.at("a").get());
    CHECK(g.nodes.at("add")->get_part("partials") == g.nodes.at("parts"));
    const auto& modes = type_declarations("SVFSource")->setting("mode")->labels;
    CHECK(g.nodes.at("svf")->get_setting("mode") == float(std::find(modes.begin(), modes.end(), "Highpass") - modes.begin()));
    CHECK(g.nodes.at("gate")->get_string("name") == "tongue");
    CHECK(g.nodes.at("seg")->get_array("values") == std::vector<float>({0.0f, 1.0f, 0.5f}));
    CHECK(g.nodes.at("env")->get_array("type") == std::vector<float>({0.0f, 3.0f}));
    // Settings before arrays: a sustainLevel beside four-stage columns leaves the file's cells.
    const BuiltGraph four = built(inst(R"({"id":"env","type":"Envelope","params":{"sustainLevel":0.9,)"
        R"("startVal":[0,1,0.5,0.5],"endVal":[1,0.5,0.5,0],"power":[0,0,0,0],"type":[0,0,0,0],)"
        R"("percent":[0.05,0.1,0,0.25],"minSec":[0,0,0,0],"maxSec":[0,0,0,0],"nominal":[0,0,0,0]}})", "env"));
    CHECK(four.nodes.at("env")->get_array("startVal") == std::vector<float>({0.0f, 1.0f, 0.5f, 0.5f}));
    CHECK(four.nodes.at("env")->get_setting("sustainLevel") == 0.5f);
    // The door's refusals, on records the codec would not have read.
    Patch p = load_text(inst(R"({"id":"a","type":"SineSource"},{"id":"parts","type":"FullPartials"},)"
                             R"({"id":"add","type":"AdditiveSource"})", "a"));
    Graph partOnSignal = p.graph;
    partOnSignal.nodes[0].wires["amplitude"] = ref("parts");
    CHECK(build_refusal(partOnSignal) == "node 'a' (SineSource): 'amplitude' does not accept a FullPartials, a part, not a signal");
    Graph signalOnSlot = p.graph;
    signalOnSlot.nodes[2].wires["partials"] = ref("a");
    CHECK(build_refusal(signalOnSlot) == "node 'add' (AdditiveSource): 'partials' does not accept a SineSource; 'partials' takes " +
                                             type_declarations("AdditiveSource")->input("partials")->hint);
    CHECK(text_refusal(inst(bad_shaper("sh"), "sh")) == "node 'sh' (Shaper): " + kShaperRefusal);
    // The table rule on a record made in memory and never checked.
    Graph ragged = graph_of({node("env", "Envelope")}, "env");
    ragged.nodes[0].arrays["startVal"] = {0.0f, 1.0f};
    CHECK(build_refusal(ragged) == "node 'env' (Envelope): table 'stages' takes its columns at one length, all present or "
                                   "all absent (got startVal 2, endVal absent, power absent, type absent, percent absent, "
                                   "minSec absent, maxSec absent, nominal absent)");
}

// The three names: a PerformNode is the note's signal its field names, and
// refused with no note; a NameGate's name finds its 1-based place among the
// onsets, and one outside them, or any name with no block, is one warning.
void run_build_named_tests() {
    using namespace build_tests;
    std::printf("the builder's three names: the note's fields, the onsets, the template\n");
    std::string fields;
    for (int k = 0; k < 6; ++k)
        fields += std::string(R"({"id":"f)") + char('0' + k) + R"(","type":"PerformNode","params":{"field":")" +
                  PerformNode::kFieldLabels[k] + R"("}},)";
    const Patch p = load_text(inst(fields + R"({"id":"out","type":"SineSource","params":{"frequency":{"ref":"f0"}}})", "out"));
    const NoteSignals note = constant_note();
    const BuiltGraph g = build_graph(p.graph, &note, nullptr);
    for (int k = 0; k < 6; ++k) CHECK(g.nodes.at(std::string("f") + char('0' + k)).get() == note.field(k).get());
    CHECK(note.field(0) == note.frequency && note.field(1) == note.velocity && note.field(2) == note.wheel &&
          note.field(3) == note.pressure && note.field(4) == note.duration && note.field(5) == note.onset);
    CHECK(build_refusal(p.graph, false) == "node 'f0' (PerformNode): a plain sound has no note to read");
    const Patch gp = load_text(inst(R"({"id":"g1","type":"NameGate","params":{"name":"bow"}},)"
                                    R"({"id":"g2","type":"NameGate","params":{"name":"tongue"}},)"
                                    R"({"id":"g3","type":"NameGate","params":{"name":"slap"}},)"
                                    R"({"id":"g4","type":"NameGate"},{"id":"out","type":"SineSource"})",
                                    "out", R"({"polyphony":1,"onsets":["tongue","bow"]})"));
    const BuiltGraph gg = build_graph(gp.graph, &note, &*gp.instrument);
    const auto target = [&](const BuiltGraph& b, const char* id) { return static_cast<NameGate&>(*b.nodes.at(id)).targetId; };
    CHECK(target(gg, "g1") == 2.0f && target(gg, "g2") == 1.0f && target(gg, "g3") < 0.0f && target(gg, "g4") < 0.0f);
    CHECK(gg.warnings == std::vector<std::string>(
        {"node 'g3' (NameGate): the name 'slap' is not one of the instrument's onsets, so the gate never fires"}));
    const BuiltGraph none = build_graph(gp.graph, &note, nullptr);
    CHECK(none.warnings.size() == 3 && target(none, "g2") < 0.0f);
    if (!none.warnings.empty())
        CHECK(none.warnings[0] == "node 'g1' (NameGate): the name 'bow' finds no onsets: the graph is built without an "
                                  "instrument block, so the gate never fires");
}
namespace build_tests {
// One node of `type` holding `params`, as text.
static std::string node_text(const char* id, const char* type, const std::string& params) {
    return std::string(R"({"id":")") + id + R"(","type":")" + type + R"(","params":)" + params + "}";
}
// A white noise of amplitude 0.3 and `seed`, rendered alone for `n` samples.
static std::vector<float> noise_alone(uint32_t seed, int n) {
    Patch p = load_text(plain(node_text("N", "WhiteNoiseSource", R"({"amplitude":0.3})"), "N"));
    p.graph.nodes[0].seed = seed;
    BuiltGraph g = build_graph(p.graph, nullptr, nullptr);
    return render_signal(*std::dynamic_pointer_cast<ValueSource>(g.output), n);
}
// The mean of renders, summed in order and divided as a MultiplexSource divides.
static std::vector<float> mean_of(const std::vector<std::vector<float>>& renders) {
    std::vector<float> out(renders.front().size());
    for (size_t s = 0; s < out.size(); ++s) {
        float sum = 0.0f;
        for (const auto& r : renders) sum += r[s];
        out[s] = sum / float(renders.size());
    }
    return out;
}
}  // namespace build_tests

// The seed rule, read back through each clone's noise: clone i's every node's
// seed is its own XOR-ed with base ^ (i * 0x9E3779B9), base the root's seed,
// and that value where a node has none; a template inside a template applies
// the rule again over the seeds its clone was given.
void run_build_template_seed_tests() {
    using namespace build_tests;
    std::printf("the template's seed rule: each clone's seeds, nested included\n");
    const int n = 4800;
    const std::string three = node_text("N", "WhiteNoiseSource", R"({"amplitude":0.3,"seed":1101})") + "," +
                              node_text("M", "WhiteNoiseSource", R"({"amplitude":0.3})") + "," +
                              node_text("R", "CombinedSource", R"({"source1":{"ref":"N"},"source2":{"ref":"M"},"operation":"Sum","seed":77})");
    BuiltGraph flat = built(plain(three + "," + node_text("mux", "MultiplexSource", R"({"count":3,"source":{"ref":"R"}})"), "mux"));
    std::vector<std::vector<float>> clones;
    for (uint32_t i = 0; i < 3; ++i) {
        const uint32_t pert = 77u ^ (i * 0x9E3779B9u);
        Patch p = load_text(plain(three, "R"));
        p.graph.nodes[0].seed = 1101u ^ pert;
        p.graph.nodes[1].seed = pert;
        p.graph.nodes[2].seed = 77u ^ pert;
        BuiltGraph g = build_graph(p.graph, nullptr, nullptr);
        clones.push_back(render_signal(*std::dynamic_pointer_cast<ValueSource>(g.output), n));
    }
    CHECK(!same_samples(clones[0], clones[1]));
    CHECK(same_samples(render_signal(*std::dynamic_pointer_cast<ValueSource>(flat.output), n), mean_of(clones)));
    BuiltGraph nested = built(plain(node_text("N", "WhiteNoiseSource", R"({"amplitude":0.3,"seed":1101})") + "," +
                                    node_text("inner", "MultiplexSource", R"({"count":2,"source":{"ref":"N"},"seed":55})") + "," +
                                    node_text("outer", "MultiplexSource", R"({"count":2,"source":{"ref":"inner"}})"), "outer"));
    std::vector<std::vector<float>> outerClones;
    for (uint32_t i = 0; i < 2; ++i) {
        const uint32_t p = 55u ^ (i * 0x9E3779B9u);           // the outer root, `inner`, holds 55
        const uint32_t noiseSeed = 1101u ^ p;                 // N in outer clone i, the inner template's root
        std::vector<std::vector<float>> innerClones;
        for (uint32_t j = 0; j < 2; ++j) innerClones.push_back(noise_alone(noiseSeed ^ (noiseSeed ^ (j * 0x9E3779B9u)), n));
        outerClones.push_back(mean_of(innerClones));
    }
    CHECK(same_samples(render_signal(*std::dynamic_pointer_cast<ValueSource>(nested.output), n), mean_of(outerClones)));
}

// The lists: the tails in the graph's order, a dynamic pin's read not a read;
// the dynamic pins in the graph's order, a node's by setting; the signals in
// id order, a PerformNode's id among them; the bend's warning.
void run_build_list_tests() {
    using namespace build_tests;
    std::printf("the builder's lists: tails, dynamic pins, signals, the bend's warning\n");
    const auto tails = [](const std::string& nodes, const char* output) { return advance_ids(load_text(inst(nodes, output)).graph); };
    using Ids = std::vector<std::string>;
    const std::string four = R"({"id":"D0","type":"SawSource"},{"id":"D1","type":"SawSource"},{"id":"D2","type":"SawSource"},)"
        R"({"id":"D3","type":"SawSource"},{"id":"out","type":"MultiSource","params":{"source":[{"tap":"D3"},{"tap":"D2"},{"tap":"D1"},{"tap":"D0"}]}})";
    CHECK(tails(four, "out") == Ids({"D0", "D1", "D2", "D3"}));
    const BuiltGraph g4 = built(inst(four, "out"));
    CHECK(g4.advance.size() == 4);
    for (size_t k = 0; k < g4.advance.size() && k < 4; ++k) CHECK(g4.advance[k].get() == g4.nodes.at("D" + std::to_string(k)).get());
    const std::string one = R"({"id":"T","type":"SawSource"},{"id":"out","type":"CombinedSource","params":{"source1":{"tap":"T"}}})";
    CHECK(tails(one, "out") == Ids({"T"}));
    // T1, first in the file, reads L, listed after T2: T2 is built before T1,
    // and advance still holds T1 then T2.
    const BuiltGraph late = built(inst(R"({"id":"T1","type":"SawSource","params":{"amplitude":{"ref":"L"}}},)"
        R"({"id":"T2","type":"SawSource"},{"id":"L","type":"SineSource"},)"
        R"({"id":"out","type":"MultiSource","params":{"source":[{"tap":"T2"},{"tap":"T1"}]}})", "out"));
    CHECK(late.advance.size() == 2 && late.advance[0].get() == late.nodes.at("T1").get() &&
          late.advance[1].get() == late.nodes.at("T2").get());
    CHECK(tails(R"({"id":"out","type":"SawSource"})", "out").empty());
    CHECK(tails(R"({"id":"T","type":"SawSource","params":{"amplitude":{"tap":"T"}}})", "T").empty());
    CHECK(tails(R"({"id":"T","type":"SawSource"},{"id":"env","type":"Envelope","dynamicPins":{"timeScale":{"ref":"T"}}},)"
                R"({"id":"out","type":"CombinedSource","params":{"source1":{"tap":"T"},"source2":{"ref":"env"}}})", "out") == Ids({"T"}));
    const BuiltGraph pins = built(inst(
        R"({"id":"eB","type":"Envelope","dynamicPins":{"timeScale":{"ref":"late"}}},{"id":"s","type":"SineSource"},)"
        R"({"id":"eA","type":"Envelope","dynamicPins":{"timeScale":{"ref":"s"},"sustainLevel":{"ref":"s"}}},)"
        R"({"id":"late","type":"SineSource"},{"id":"out","type":"CombinedSource","params":{"source1":{"ref":"eA"},"source2":{"ref":"eB"}}})", "out"));
    const auto pin_is = [&](size_t k, const char* id, const char* setting, const char* source) {
        return k < pins.dynamicPins.size() && pins.dynamicPins[k].node == pins.nodes.at(id) && pins.dynamicPins[k].setting == setting &&
               pins.dynamicPins[k].source.get() == pins.nodes.at(source).get();
    };
    CHECK(pins.dynamicPins.size() == 3 && pin_is(0, "eB", "timeScale", "late") && pin_is(1, "eA", "sustainLevel", "s") &&
          pin_is(2, "eA", "timeScale", "s"));
    const BuiltGraph sig = built(inst(R"({"id":"z","type":"SineSource","params":{"frequency":{"ref":"pf"}}},{"id":"pf","type":"PerformNode"},)"
                                      R"({"id":"parts","type":"FullPartials"},{"id":"a","type":"SawSource"})", "z"));
    CHECK(sig.signals.size() == 3 && sig.signals[0].id == "a" && sig.signals[1].id == "pf" && sig.signals[2].id == "z");
    if (sig.signals.size() == 3) CHECK(sig.signals[1].node == sig.nodes.at("pf").get());
    const auto ks = [](const std::string& frequency, const char* field) {
        return built(inst(std::string(R"({"id":"pf","type":"PerformNode","params":{"field":")") + field + R"("}},)"
                          R"({"id":"x2","type":"CombinedSource","params":{"source1":{"ref":"pf"},"source2":2.0,"operation":"Multiply"}},)"
                          R"({"id":"exc","type":"WhiteNoiseSource"},)"
                          R"({"id":"ks","type":"KSString","params":{"source":{"ref":"exc"},"frequency":)" + frequency + "}}", "ks")).warnings;
    };
    const Ids bend{"node 'ks' (KSString): it reads its frequency once at note start, so a bend does not reach it"};
    CHECK(ks(R"({"ref":"pf"})", "frequency") == bend);
    CHECK(ks(R"({"ref":"x2"})", "frequency") == bend);
    CHECK(ks("220", "frequency").empty());
    CHECK(ks(R"({"ref":"pf"})", "velocity").empty());
}
