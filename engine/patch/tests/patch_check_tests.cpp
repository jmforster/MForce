#include "mforce/testing/check.h"
#include "mforce/patch/instrument_block.h"
#include "mforce/patch/layout.h"
#include "mforce/patch/node.h"
#include "mforce/patch/patch.h"
#include <cstdio>
#include <string>
#include <vector>

using namespace mforce;

namespace patch_check {
static Node node(const char* id, const char* type) {
    Node n;
    n.id = id;
    n.type = type;
    return n;
}
static Node& at(Patch& p, const char* id) {
    for (auto& n : p.graph.nodes)
        if (n.id == id) return n;
    std::printf("  no node '%s' in the case's patch\n", id);
    std::abort();
}
// A three-node instrument: a note's frequency and an Envelope into a sine,
// the Envelope's timeScale pushed from the frequency per note, with a
// position per node, a group and a note face.
static Patch instrument() {
    Patch p;
    Node freq = node("freq", "PerformNode");
    freq.settings["field"] = 0.0f;
    Node env = node("env", "Envelope");
    env.values["maxValue"] = 0.8f;
    env.settings["timeMode"] = 1.0f;
    env.dynamicPins["timeScale"] = "freq";
    Node osc = node("osc", "SineSource");
    osc.wires["frequency"] = {Wire{"freq"}};
    osc.wires["amplitude"] = {Wire{"env"}};
    p.graph.nodes = {freq, env, osc};
    p.graph.output = "osc";
    p.instrument = InstrumentBlock{};
    p.layout.positions = {{"freq", Vec2{0, 0}}, {"env", Vec2{0, 100}}, {"osc", Vec2{200, 50}}};
    p.layout.groups = {Group{"Voice", {"osc", "env", "Note"}, Vec2{100, 50}}};
    p.layout.faces = {NoteFace{"Note", {{"frequency", "freq"}}, Vec2{-100, 0}}};
    return p;
}
// A two-channel plain sound: a sine and a saw, each on a channel of the mixer.
static Patch plain_sound() {
    Patch p;
    Node ch1 = node("ch1", "SoundChannel");
    ch1.wires["source"] = {Wire{"a"}};
    Node ch2 = node("ch2", "SoundChannel");
    ch2.wires["source"] = {Wire{"b"}};
    ch2.values["pan"] = -0.5f;
    Node mix = node("mix", "StereoMixer");
    mix.wires["channels"] = {Wire{"ch1"}, Wire{"ch2"}};
    p.graph.nodes = {node("a", "SineSource"), node("b", "SawSource"), ch1, ch2, mix};
    p.graph.output = "mix";
    p.seconds = 1.0;
    return p;
}
// One break: the patch it starts from, the change, and the one line check() gives.
struct Break { const char* name; bool plain; void (*change)(Patch&); const char* line; };
static void run_breaks(const std::vector<Break>& breaks) {
    for (const auto& b : breaks) {
        Patch p = b.plain ? plain_sound() : instrument();
        b.change(p);
        const auto got = p.check();
        const std::vector<std::string> want = *b.line ? std::vector<std::string>{b.line} : std::vector<std::string>{};
        if (got != want) {
            std::printf("  %s: check() gave %zu lines\n", b.name, got.size());
            for (const auto& l : got) std::printf("    %s\n", l.c_str());
        }
        CHECK(got == want);
    }
}
constexpr const char* kShared = "; ids, group names and face labels share one namespace";
static std::string shared(const char* head) { return std::string(head) + kShared; }
// Every stage column of `env` at two rows, the second stage of type `type`.
static void stages(Node& env, float type) {
    for (const char* column : {"startVal", "endVal", "power", "percent", "minSec", "maxSec", "nominal"})
        env.arrays[column] = {0.0f, 1.0f};
    env.arrays["type"] = {0.0f, type};
}
// The groups: a name in one group at most and once in it, no group inside
// itself; an empty group, as a file may bring one, holds.
static std::vector<Break> group_breaks() {
    return {
        {"an empty group", false, [](Patch& p) { p.layout.groups.push_back(Group{"Empty"}); }, ""},
        {"a name in two groups", false, [](Patch& p) { p.layout.groups.push_back(Group{"Other", {"osc"}}); },
         "layout.groups[1].members[0]: 'osc' is a member of 'Voice' and of 'Other': a name is in one group at most"},
        {"a name twice in one group", false, [](Patch& p) { p.layout.groups[0].members.push_back("osc"); },
         "layout.groups[0].members[3]: 'osc' is listed twice in group 'Voice'"},
        {"a group listing itself", false, [](Patch& p) { p.layout.groups[0].members.push_back("Voice"); },
         "layout.groups[0].members[3]: 'Voice' holds itself"},
    };
}
// Two groups that hold each other: each is refused, through the other.
static void check_mutual_nesting() {
    Patch p = instrument();
    p.layout.groups.push_back(Group{"Outer", {"Voice"}});
    p.layout.groups[0].members.push_back("Outer");
    const std::vector<std::string> want = {
        "layout.groups[0].members[3]: 'Voice' holds itself, through 'Outer'",
        "layout.groups[1].members[0]: 'Outer' holds itself, through 'Voice'"};
    const auto got = p.check();
    for (const auto& l : got)
        if (got != want) std::printf("  two groups holding each other gave: %s\n", l.c_str());
    CHECK(got == want);
    Patch deep = instrument();
    deep.layout.groups.push_back(Group{"A", {"Voice"}});
    deep.layout.groups.push_back(Group{"B", {"A"}});
    deep.layout.groups[0].members.push_back("B");
    CHECK(deep.check().front() == "layout.groups[0].members[3]: 'Voice' holds itself, through 'B'");
}
// Patches that hold, and the ids, the namespace and the output.
static std::vector<Break> name_breaks() {
    static const std::string twoNodes = shared("graph.nodes[3].id: 'osc' names two things: a node, a node");
    static const std::string groupNode = shared("layout.groups[1].name: 'env' names two things: a node, a group");
    static const std::string faceGroup = shared("layout.faces[1].label: 'Voice' names two things: a group, a face");
    static const std::string threeOnce = shared("layout.groups[1].name: 'osc' names two things: a node, a group");
    return {
        {"the instrument holds", false, [](Patch&) {}, ""},
        {"the plain sound holds", true, [](Patch&) {}, ""},
        {"a tap closes a loop", false, [](Patch& p) { at(p, "env").wires["minValue"] = {Wire{"osc", true}}; }, ""},
        {"an empty output", false, [](Patch& p) { p.graph.output.clear(); }, ""},
        {"a member naming a group", false, [](Patch& p) { p.layout.groups.push_back(Group{"Outer", {"Voice"}}); }, ""},
        {"a full stage table", false, [](Patch& p) { stages(at(p, "env"), 4.0f); }, ""},
        {"an empty id", false, [](Patch& p) { p.graph.nodes.push_back(node("", "SineSource")); },
         "graph.nodes[3].id: an id is empty"},
        {"two nodes under one id", false, [](Patch& p) { p.graph.nodes.push_back(node("osc", "SawSource")); },
         twoNodes.c_str()},
        {"a group named as a node", false, [](Patch& p) { p.layout.groups.push_back(Group{"env"}); },
         groupNode.c_str()},
        {"a face labelled as a group", false, [](Patch& p) { p.layout.faces.push_back(NoteFace{"Voice"}); },
         faceGroup.c_str()},
        {"three things under one name, once", false,
         [](Patch& p) { p.layout.groups.push_back(Group{"osc"}); p.layout.faces.push_back(NoteFace{"osc"}); },
         threeOnce.c_str()},
        {"the output naming no node", false, [](Patch& p) { p.graph.output = "nope"; },
         "graph.output: 'nope' names no node"},
        {"the output a part", false,
         [](Patch& p) {
             Node ch = node("ch", "SoundChannel");
             ch.wires["source"] = {Wire{"osc"}};
             p.graph.nodes.push_back(ch);
             p.graph.output = "ch";
         },
         "graph.output: 'ch' is a SoundChannel, a part, not a signal"},
    };
}
// A node's type, the names of its maps, and the shape of its pins.
static std::vector<Break> declaration_breaks() {
    return {
        {"an unknown type", false, [](Patch& p) { p.graph.nodes.push_back(node("x", "NoSuchSource")); },
         "graph.nodes[3].type: node 'x' (NoSuchSource): Unknown node type: NoSuchSource"},
        {"a retired type", false, [](Patch& p) { p.graph.nodes.push_back(node("x", "AREnvelope")); },
         "graph.nodes[3].type: node 'x' (AREnvelope): Unknown node type: AREnvelope; "
         "python tools/convert_patch.py <patch> rewrites a type this engine no longer has"},
        {"an unknown type checked no further", false,
         [](Patch& p) {
             Node x = node("x", "NoSuchSource");
             x.values["frequncy"] = 1.0f;
             x.wires["in"] = {Wire{"ghost"}};
             p.graph.nodes.push_back(x);
         },
         "graph.nodes[3].type: node 'x' (NoSuchSource): Unknown node type: NoSuchSource"},
        {"a number under an undeclared name", false, [](Patch& p) { at(p, "osc").values["frequncy"] = 1.0f; },
         "graph.nodes[2].params.frequncy: node 'osc' (SineSource): 'frequncy' is not a pin of SineSource"},
        {"a feed under an undeclared name", false, [](Patch& p) { at(p, "osc").wires["source"] = {Wire{"env"}}; },
         "graph.nodes[2].params.source: node 'osc' (SineSource): 'source' is not a pin of SineSource"},
        {"a setting under a pin's name", false, [](Patch& p) { at(p, "env").settings["maxValue"] = 1.0f; },
         "graph.nodes[1].params.maxValue: node 'env' (Envelope): 'maxValue' is not a setting of Envelope"},
        {"a number under a string setting's name", false,
         [](Patch& p) { Node g = node("g", "NameGate"); g.settings["name"] = 1.0f; p.graph.nodes.push_back(g); },
         "graph.nodes[3].params.name: node 'g' (NameGate): 'name' is not a setting of NameGate"},
        {"a string under a float setting's name", false, [](Patch& p) { at(p, "env").strings["timeScale"] = "fast"; },
         "graph.nodes[1].params.timeScale: node 'env' (Envelope): 'timeScale' is not a string setting of Envelope"},
        {"an array under an undeclared name", false, [](Patch& p) { at(p, "osc").arrays["startVal"] = {0.0f}; },
         "graph.nodes[2].params.startVal: node 'osc' (SineSource): 'startVal' is not an array of SineSource"},
        {"a number and a feed on one pin", false, [](Patch& p) { at(p, "osc").values["frequency"] = 440.0f; },
         "graph.nodes[2].params.frequency: node 'osc' (SineSource): 'frequency' holds a number and a feed: a pin takes one"},
        {"an empty feed list", false, [](Patch& p) { at(p, "env").wires["minValue"] = {}; },
         "graph.nodes[1].params.minValue: node 'env' (Envelope): 'minValue' holds an empty feed list: "
         "a pin with no feed has no entry"},
        {"two feeds on a single pin", false, [](Patch& p) { at(p, "osc").wires["amplitude"].push_back(Wire{"freq"}); },
         "graph.nodes[2].params.amplitude: node 'osc' (SineSource): 'amplitude' takes one feed, and holds 2"},
        {"a number on a slot", false,
         [](Patch& p) { Node w = node("wt", "WavetableSource"); w.values["evolution"] = 1.0f; p.graph.nodes.push_back(w); },
         "graph.nodes[3].params.evolution: node 'wt' (WavetableSource): 'evolution' is a slot: it takes a part, not a number"},
        {"a tap on a slot", true, [](Patch& p) { at(p, "mix").wires["channels"][1].tap = true; },
         "graph.nodes[4].params.channels: node 'mix' (StereoMixer): 'channels' is a slot: it takes a part by ref, "
         "not through a tap"},
    };
}
// Feeds, settings and columns against the declarations.
static std::vector<Break> value_breaks() {
    return {
        {"a feed naming no node", false, [](Patch& p) { at(p, "osc").wires["phase"] = {Wire{"ghost"}}; },
         "graph.nodes[2].params.phase: node 'osc' (SineSource): 'phase' reads 'ghost', which names no node"},
        {"a tap naming no node", false, [](Patch& p) { at(p, "env").wires["minValue"] = {Wire{"ghost", true}}; },
         "graph.nodes[1].params.minValue: node 'env' (Envelope): 'minValue' reads 'ghost', which names no node"},
        {"a signal on a slot", true, [](Patch& p) { at(p, "mix").wires["channels"].push_back(Wire{"a"}); },
         "graph.nodes[4].params.channels: node 'mix' (StereoMixer): 'channels' does not accept a SineSource; "
         "'channels' takes a SoundChannel"},
        {"a wrong part on a slot", true,
         [](Patch& p) { p.graph.nodes.push_back(node("fp", "FullPartials")); at(p, "mix").wires["channels"].push_back(Wire{"fp"}); },
         "graph.nodes[4].params.channels: node 'mix' (StereoMixer): 'channels' does not accept a FullPartials, "
         "a part, not a signal; 'channels' takes a SoundChannel"},
        {"a part on a Param", false,
         [](Patch& p) { p.graph.nodes.push_back(node("fp", "FullPartials")); at(p, "osc").wires["phase"] = {Wire{"fp"}}; },
         "graph.nodes[2].params.phase: node 'osc' (SineSource): 'phase' does not accept a FullPartials, a part, not a signal"},
        {"a part on an Input", true, [](Patch& p) { at(p, "ch1").wires["source"] = {Wire{"ch2"}}; },
         "graph.nodes[2].params.source: node 'ch1' (SoundChannel): 'source' does not accept a SoundChannel, "
         "a part, not a signal"},
        {"a float setting above its range", false, [](Patch& p) { at(p, "env").settings["timeScale"] = 20.0f; },
         "graph.nodes[1].params.timeScale: node 'env' (Envelope): 'timeScale' is 20, outside its range 0.01 to 10"},
        {"a float setting below its range", false, [](Patch& p) { at(p, "env").settings["stage_accuracy"] = -0.5f; },
         "graph.nodes[1].params.stage_accuracy: node 'env' (Envelope): 'stage_accuracy' is -0.5, outside its range 0 to 1"},
        {"an int setting above its range", false,
         [](Patch& p) { Node e = node("ep", "ExplicitPartials"); e.settings["maxPartials"] = 201.0f; p.graph.nodes.push_back(e); },
         "graph.nodes[3].params.maxPartials: node 'ep' (ExplicitPartials): 'maxPartials' is 201, outside its range 1 to 200"},
        {"an int setting holding a fraction", false,
         [](Patch& p) { Node e = node("ep", "ExplicitPartials"); e.settings["maxPartials"] = 2.5f; p.graph.nodes.push_back(e); },
         "graph.nodes[3].params.maxPartials: node 'ep' (ExplicitPartials): 'maxPartials' is 2.5: an int setting holds "
         "a whole number"},
        {"a bool setting holding 2", false,
         [](Patch& p) { Node e = node("ep", "ExplicitPartials"); e.settings["evolve"] = 2.0f; p.graph.nodes.push_back(e); },
         "graph.nodes[3].params.evolve: node 'ep' (ExplicitPartials): 'evolve' is 2: a bool setting holds 0 or 1"},
        {"an enum outside its labels", false, [](Patch& p) { at(p, "env").settings["timeMode"] = 2.0f; },
         "graph.nodes[1].params.timeMode: node 'env' (Envelope): 'timeMode' is 2, which is not an index into its "
         "labels (fraction, seconds)"},
        {"a labelled column outside its labels", false, [](Patch& p) { stages(at(p, "env"), 7.0f); },
         "graph.nodes[1].params.type: node 'env' (Envelope): 'type' row 1 is 7, which is not an index into its "
         "labels (Linear, Expo, InverseExpo, Sine, Hold)"},
        {"a ragged table", false, [](Patch& p) { at(p, "env").arrays["startVal"] = {0.0f, 1.0f}; },
         "graph.nodes[1].params: node 'env' (Envelope): table 'stages' takes its columns at one length, all present "
         "or all absent (got startVal 2, endVal absent, power absent, type absent, percent absent, minSec absent, "
         "maxSec absent, nominal absent)"},
    };
}
// Dynamic pins, cycles, the plain sound and the layout.
static std::vector<Break> graph_breaks() {
    return {
        {"a dynamic pin on a pin", false, [](Patch& p) { at(p, "env").dynamicPins["trigger"] = "freq"; },
         "graph.nodes[1].dynamicPins.trigger: node 'env' (Envelope): 'trigger' is not a setting on Envelope "
         "(fixed pins belong in \"params\")"},
        {"a dynamic pin on a structural setting", false, [](Patch& p) { at(p, "env").dynamicPins["timeMode"] = "freq"; },
         "graph.nodes[1].dynamicPins.timeMode: node 'env' (Envelope): 'timeMode' is a structural setting on Envelope "
         "(it sizes or reshapes the node at prepare), so it takes no pin"},
        {"a dynamic pin naming no node", false, [](Patch& p) { at(p, "env").dynamicPins["timeScale"] = "ghost"; },
         "graph.nodes[1].dynamicPins.timeScale: node 'env' (Envelope): 'timeScale' reads 'ghost', which names no node"},
        {"a dynamic pin read from a part", false,
         [](Patch& p) { p.graph.nodes.push_back(node("fp", "FullPartials")); at(p, "env").dynamicPins["timeScale"] = "fp"; },
         "graph.nodes[1].dynamicPins.timeScale: node 'env' (Envelope): 'timeScale' is a FullPartials, a part, not a signal"},
        {"a cycle of two", false, [](Patch& p) { at(p, "env").wires["minValue"] = {Wire{"osc"}}; },
         "graph: a cycle with no tap in it: env -> osc -> env"},
        {"a node that reads itself", false, [](Patch& p) { at(p, "osc").wires["phase"] = {Wire{"osc"}}; },
         "graph: a cycle with no tap in it: osc -> osc"},
        {"a cycle through a dynamic pin", false, [](Patch& p) { at(p, "env").dynamicPins["timeScale"] = "osc"; },
         "graph: a cycle with no tap in it: env -> osc -> env"},
        {"a PerformNode in a plain sound", true, [](Patch& p) { p.graph.nodes.push_back(node("pf", "PerformNode")); },
         "graph.nodes[5].type: node 'pf' (PerformNode): a plain sound has no note to read"},
        {"a dynamic pin in a plain sound", true,
         [](Patch& p) { Node e = node("env", "Envelope"); e.dynamicPins["timeScale"] = "a"; p.graph.nodes.push_back(e); },
         "graph.nodes[5].dynamicPins.timeScale: node 'env' (Envelope): a plain sound has no note, so nothing drives "
         "a dynamic pin"},
        {"a position naming no node", false, [](Patch& p) { p.layout.positions["ghost"] = Vec2{}; },
         "layout.positions.ghost: 'ghost' names no node"},
        {"a member naming nothing", false, [](Patch& p) { p.layout.groups[0].members.push_back("ghost"); },
         "layout.groups[0].members[3]: 'ghost' names no node, group or face"},
        {"a face field naming no node", false, [](Patch& p) { p.layout.faces[0].fields["velocity"] = "ghost"; },
         "layout.faces[0].fields.velocity: 'ghost' names no node"},
    };
}
}  // namespace patch_check

// Patch::check() against the eight invariants a patch keeps, each case one
// minimal break of a valid patch giving exactly its one line, path and all:
//  ids non-empty and unique, one namespace with group names and face labels:
//    "an empty id", "two nodes under one id", "a group named as a node",
//    "a face labelled as a group", "three things under one name, once";
//  the output empty or a node that is a signal or the mixer part:
//    "an empty output", "the output naming no node", "the output a part",
//    "the plain sound holds";
//  every feed, tap, dynamic pin's source, member, face field and position
//    naming a node (a member also a group or a face): "a feed naming no
//    node", "a tap naming no node", "a dynamic pin naming no node", "a
//    position naming no node", "a member naming nothing", "a face field
//    naming no node", "a member naming a group";
//  a pin a number or feeds, one feed unless a list, no tap on a slot: "a
//    number and a feed on one pin", "an empty feed list", "two feeds on a
//    single pin", "a number on a slot", "a tap on a slot";
//  every name declared for its kind, the type registered, a dynamic pin on a
//    setting that is not structural, a slot's part of an accepted type (and a
//    signal pin's feed a signal): "an unknown type", "a retired type", "an
//    unknown type checked no further", the five "... under ... name" cases,
//    "a dynamic pin on a pin", "a dynamic pin on a structural setting", "a
//    dynamic pin read from a part", "a signal on a slot", "a wrong part on a
//    slot", "a part on a Param", "a part on an Input";
//  a table's columns at one length, a setting inside its declaration: "a
//    ragged table", "a full stage table", the four range, fraction and bool
//    cases, "an enum outside its labels", "a labelled column outside its
//    labels";
//  no cycle without a tap: "a cycle of two", "a node that reads itself", "a
//    cycle through a dynamic pin", "a tap closes a loop";
//  a plain sound with no PerformNode and no dynamic pin: "a PerformNode in a
//    plain sound", "a dynamic pin in a plain sound".
void run_patch_check_tests() {
    using namespace patch_check;
    std::printf("Patch::check(): each invariant broken once, each line with its path\n");
    run_breaks(name_breaks());
    run_breaks(declaration_breaks());
    run_breaks(value_breaks());
    run_breaks(graph_breaks());
    run_breaks(group_breaks());
    check_mutual_nesting();
    // Three unrelated breaks, made in the reverse of check()'s order, come
    // back in check()'s order: the output, the node, the layout.
    Patch p = instrument();
    p.layout.positions["ghost"] = Vec2{};
    at(p, "osc").values["frequncy"] = 1.0f;
    p.graph.output = "nope";
    const std::vector<std::string> want = {
        "graph.output: 'nope' names no node",
        "graph.nodes[2].params.frequncy: node 'osc' (SineSource): 'frequncy' is not a pin of SineSource",
        "layout.positions.ghost: 'ghost' names no node"};
    const auto got = p.check();
    if (got != want)
        for (const auto& l : got) std::printf("  three breaks gave: %s\n", l.c_str());
    CHECK(got == want);
    // check() reads the record and builds nothing: the patch is as it was.
    const Patch before = instrument();
    Patch after = before;
    (void)after.check();
    CHECK(after == before);
}
