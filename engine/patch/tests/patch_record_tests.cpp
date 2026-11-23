#include "mforce/testing/check.h"
#include "mforce/patch/fragment.h"
#include "mforce/patch/graph.h"
#include "mforce/patch/instrument_block.h"
#include "mforce/patch/layout.h"
#include "mforce/patch/node.h"
#include "mforce/patch/patch.h"
#include <cstdio>
#include <limits>
#include <vector>

using namespace mforce;

namespace patch_records {
// A Patch with every member of every record set away from its default.
static Patch full_patch() {
    Node a;
    a.id = "osc"; a.type = "SineSource"; a.seed = 7u;
    a.values["phase"] = 0.25f;
    a.wires["amplitude"] = {Wire{"env", false}, Wire{"lfo", true}};
    a.settings["mode"] = 1.0f;
    a.strings["name"] = "tongue";
    a.arrays["col"] = {0.5f, 1.0f};
    a.dynamicPins["t60"] = "curve";
    a.comment = "the tone";
    Patch p;
    p.graph.nodes = {a, Node{"env", "Envelope"}};
    p.graph.output = "osc";
    p.instrument = InstrumentBlock{2, 0.5f, true, {"tongue", "slur"}, 20.0f};
    p.layout.positions["osc"] = Vec2{1, 2};
    p.layout.outputPos = Vec2{3, 4};
    p.layout.panning = Vec2{5, 6};
    p.layout.groups = {Group{"Voice", {"osc", "env"}, Vec2{7, 8}}};
    p.layout.faces = {NoteFace{"Note", {{"frequency", "freq"}}, Vec2{9, 10}}};
    p.sampleRate = 44100;
    p.seconds = 2.5;
    p.score = "[]";
    p.comment = "a patch";
    return p;
}
static Fragment full_fragment() {
    const Patch p = full_patch();
    return Fragment{p.graph.nodes, p.layout.positions, p.layout.groups, p.layout.faces};
}
struct PatchCase { const char* member; void (*change)(Patch&); };
static std::vector<PatchCase> patch_cases() {
    return {
        {"Wire.source", [](Patch& p) { p.graph.nodes[0].wires["amplitude"][1].source = "lfo2"; }},
        {"Wire.tap", [](Patch& p) { p.graph.nodes[0].wires["amplitude"][1].tap = false; }},
        {"Node.id", [](Patch& p) { p.graph.nodes[0].id = "osc2"; }},
        {"Node.type", [](Patch& p) { p.graph.nodes[0].type = "SawSource"; }},
        {"Node.seed", [](Patch& p) { p.graph.nodes[0].seed = 8u; }},
        {"Node.seed (absent)", [](Patch& p) { p.graph.nodes[0].seed.reset(); }},
        {"Node.values", [](Patch& p) { p.graph.nodes[0].values["phase"] = 0.5f; }},
        {"Node.wires", [](Patch& p) { p.graph.nodes[0].wires["phase"] = {Wire{"env"}}; }},
        {"Node.settings", [](Patch& p) { p.graph.nodes[0].settings["mode"] = 2.0f; }},
        {"Node.strings", [](Patch& p) { p.graph.nodes[0].strings["name"] = "slur"; }},
        {"Node.arrays", [](Patch& p) { p.graph.nodes[0].arrays["col"][1] = 2.0f; }},
        {"Node.dynamicPins", [](Patch& p) { p.graph.nodes[0].dynamicPins["t60"] = "curve2"; }},
        {"Node.comment", [](Patch& p) { p.graph.nodes[0].comment = "another"; }},
        {"Graph.nodes", [](Patch& p) { p.graph.nodes.pop_back(); }},
        {"Graph.output", [](Patch& p) { p.graph.output = "env"; }},
        {"InstrumentBlock.polyphony", [](Patch& p) { p.instrument->polyphony = 3; }},
        {"InstrumentBlock.volume", [](Patch& p) { p.instrument->volume = 0.25f; }},
        {"InstrumentBlock.sustaining", [](Patch& p) { p.instrument->sustaining = false; }},
        {"InstrumentBlock.onsets", [](Patch& p) { p.instrument->onsets.pop_back(); }},
        {"InstrumentBlock.glideMs", [](Patch& p) { p.instrument->glideMs = 15.0f; }},
        {"Vec2.x", [](Patch& p) { p.layout.positions["osc"].x = 1.5f; }},
        {"Vec2.y", [](Patch& p) { p.layout.positions["osc"].y = 2.5f; }},
        {"Group.name", [](Patch& p) { p.layout.groups[0].name = "Body"; }},
        {"Group.members", [](Patch& p) { p.layout.groups[0].members.pop_back(); }},
        {"Group.pos", [](Patch& p) { p.layout.groups[0].pos.y = 0.0f; }},
        {"NoteFace.label", [](Patch& p) { p.layout.faces[0].label = "Note2"; }},
        {"NoteFace.fields", [](Patch& p) { p.layout.faces[0].fields["velocity"] = "vel"; }},
        {"NoteFace.pos", [](Patch& p) { p.layout.faces[0].pos.reset(); }},
        {"Layout.positions", [](Patch& p) { p.layout.positions["env"] = Vec2{}; }},
        {"Layout.outputPos", [](Patch& p) { p.layout.outputPos->x = 0.0f; }},
        {"Layout.panning", [](Patch& p) { p.layout.panning.reset(); }},
        {"Layout.groups", [](Patch& p) { p.layout.groups.push_back(Group{"Body"}); }},
        {"Layout.faces", [](Patch& p) { p.layout.faces.clear(); }},
        {"Patch.graph", [](Patch& p) { p.graph = Graph{}; }},
        {"Patch.instrument", [](Patch& p) { p.instrument.reset(); }},
        {"Patch.layout", [](Patch& p) { p.layout = Layout{}; }},
        {"Patch.sampleRate", [](Patch& p) { p.sampleRate = 48000; }},
        {"Patch.seconds", [](Patch& p) { p.seconds = 3.0; }},
        {"Patch.score", [](Patch& p) { p.score = ""; }},
        {"Patch.comment", [](Patch& p) { p.comment = ""; }},
    };
}
struct FragmentCase { const char* member; void (*change)(Fragment&); };
static std::vector<FragmentCase> fragment_cases() {
    return {
        {"Fragment.nodes", [](Fragment& f) { f.nodes[1].type = "SawSource"; }},
        {"Fragment.positions", [](Fragment& f) { f.positions["env"] = Vec2{}; }},
        {"Fragment.groups", [](Fragment& f) { f.groups[0].members.push_back("lfo"); }},
        {"Fragment.faces", [](Fragment& f) { f.faces[0].fields.clear(); }},
    };
}
}  // namespace patch_records

// The records are plain values: a copy compares equal, and a change to any
// one member of any record, at its own depth, compares unequal.
void run_patch_record_tests() {
    using namespace patch_records;
    std::printf("the patch records: a copy equal, a change to any member unequal\n");
    const Patch whole = full_patch();
    Patch copy = whole;
    CHECK(copy == whole);
    for (const auto& c : patch_cases()) {
        Patch changed = whole;
        c.change(changed);
        const bool unequal = !(changed == whole);
        if (!unequal) std::printf("  %s: the change compares equal\n", c.member);
        CHECK(unequal);
    }
    const Fragment fragment = full_fragment();
    Fragment fragmentCopy = fragment;
    CHECK(fragmentCopy == fragment);
    for (const auto& c : fragment_cases()) {
        Fragment changed = fragment;
        c.change(changed);
        const bool unequal = !(changed == fragment);
        if (!unequal) std::printf("  %s: the change compares equal\n", c.member);
        CHECK(unequal);
    }
    // What == on a float cannot see: a negative zero compares equal to a
    // zero, so two records that would be written as "-0" and "0" are equal.
    Patch zero = whole, negativeZero = whole;
    zero.graph.nodes[0].values["phase"] = 0.0f;
    negativeZero.graph.nodes[0].values["phase"] = -0.0f;
    CHECK(zero == negativeZero);
    // What == on a float sees oddly: a NaN compares unequal to itself, so a
    // record holding one is unequal to its own copy.
    Patch nan = whole;
    nan.graph.nodes[0].values["phase"] = std::numeric_limits<float>::quiet_NaN();
    const Patch nanCopy = nan;
    CHECK(!(nanCopy == nan));
}
