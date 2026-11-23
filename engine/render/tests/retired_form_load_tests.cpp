#include "mforce/testing/check.h"
#include "test_support.h"
#include "patch/tests/fixtures.h"
#include "render/tests/fixtures.h"
#include "mforce/core/pins.h"
#include "mforce/patch/envelope_preset.h"
#include "mforce/source/modulator/var_source.h"
#include <nlohmann/json.hpp>
#include <cstdio>
#include <exception>
#include <string>
#include <utility>
#include <vector>

using namespace mforce;

namespace retired_forms {
// The message the load of `text` fails with, as an instrument or, as a plain
// sound, a patch file built as a sound; empty when it loads.
static std::string refusal(const std::string& text, bool file) {
    try { three_kinds::load(file ? three_kinds::plain(text) : text, file); }
    catch (const std::exception& e) { return e.what(); }
    return {};
}
// The voice graph of the two sines `a` and `b` and the nodes of `nodes`, a JSON array.
static std::string doc(const std::string& nodes) {
    return list_twins::voice_doc(nlohmann::json::parse(nodes));
}
// One node `n` of `type` holding `params`, as a JSON array of one.
static std::string node(const char* type, const std::string& params) {
    return std::string(R"([{"id":"n","type":")") + type + R"(","params":)" + params + "}]";
}
// Whether the load of `nodes` fails through both entry points with `want` after its path.
static bool refused_with(const std::string& nodes, const std::string& want) {
    bool ok = true;
    for (bool file : {false, true}) {
        const std::string msg = refusal(doc(nodes), file);
        if (!refusal_says(msg, want)) std::printf("  %s: got \"%s\"\n  want \"%s\"\n", file ? "file" : "text", msg.c_str(), want.c_str());
        ok = ok && refusal_says(msg, want);
    }
    return ok;
}
// Whether `nodes` loads through both entry points.
static bool loads_both(const std::string& nodes) {
    bool ok = true;
    for (bool file : {false, true}) {
        const std::string msg = refusal(doc(nodes), file);
        if (!msg.empty()) std::printf("  %s: load failed: %s\n", file ? "file" : "text", msg.c_str());
        ok = ok && msg.empty();
    }
    return ok;
}
const std::string kConv = "is no longer read: python tools/convert_patch.py <patch> rewrites it as ";
}  // namespace retired_forms

void run_retired_form_load_tests() {
    using namespace retired_forms;
    std::printf("retired forms: each fails through both entry points with its sentence; its new form loads\n");
    struct Form { std::string oldNodes, newNodes, want; };
    std::vector<Form> forms = {
        {node("PhasedValueSource", R"({"stages":[{"source":{"ref":"a"},"percent":0.5},{"source":{"ref":"b"}}]})"),
         node("PhasedValueSource", R"({"sources":[{"ref":"a"},{"ref":"b"}],"percent":[0.5,0.0],"minSec":[0.0,0.0],)"
                                   R"("maxSec":[0.0,0.0],"gain":[1.0,1.0]})"),
         "node 'n' (PhasedValueSource): 'stages' " + kConv + "sources and the columns percent, minSec, maxSec and gain"},
        {node("RepeatingSource", R"({"source":{"ref":"a"},"gap":0.1})"),
         node("RepeatingSource", R"({"source":{"ref":"a"},"gapDuration":0.1})"),
         "node 'n' (RepeatingSource): 'gap' " + kConv + "gapDuration"},
        {node("MultiSource", R"({"source":[{"ref":"a","weight":0.5},{"ref":"b"}]})"),
         node("MultiSource", R"({"source":[{"ref":"a"},{"ref":"b"}],"weights":[0.5,1.0]})"),
         "node 'n' (MultiSource): 'source': 'weight' on a feed " + kConv + "the array weights"},
        {node("MultiSource", R"({"source":{"ref":"a","weight":0.5}})"),
         node("MultiSource", R"({"source":{"ref":"a"},"weights":[0.5]})"),
         "node 'n' (MultiSource): 'source': 'weight' on a feed " + kConv + "the array weights"},
        {node("MultiSource", R"({"source":[{"ref":"a"},{"ref":"b","delay":100}]})"),
         node("MultiSource", R"({"source":[{"ref":"a"},{"ref":"b"}],"delays":[0.0,100.0]})"),
         "node 'n' (MultiSource): 'source': 'delay' on a feed " + kConv + "the array delays"},
        {node("MultiSource", R"({"source":{"ref":"a","delay":100}})"),
         node("MultiSource", R"({"source":{"ref":"a"},"delays":[100.0]})"),
         "node 'n' (MultiSource): 'source': 'delay' on a feed " + kConv + "the array delays"},
    };
    for (const char* type : {"BWLowpassFilter", "BWHighpassFilter", "SVFSource"})
        forms.push_back({node(type, R"({"source":{"ref":"a"},"cutoff":500.0})"),
                         node(type, R"({"source":{"ref":"a"},"cutoffFreq":500.0})"),
                         "node 'n' (" + std::string(type) + "): 'cutoff' " + kConv + "cutoffFreq"});
    for (const char* type : {"FullPartials", "SequencePartials", "ExplicitPartials"}) {
        const std::string host = node(type, R"({"expandRule":{"ref":"r"}})");
        forms.push_back({node(type, R"({"expandRule":{"count":3,"spacing1":0.3}})"),
                         R"([{"id":"r","type":"ExpandRule","params":{"count":3,"spacing1":0.3}},)" + host.substr(1),
                         "node 'n' (" + std::string(type) + "): 'expandRule' as an inline object " + kConv
                             + "an ExpandRule node wired by ref"});
    }
    CHECK(forms.size() == 12);
    for (const auto& f : forms) {
        CHECK(refused_with(f.oldNodes, f.want));
        CHECK(loads_both(f.newNodes));
    }
    // A cutoff on the Butterworth bandpass is a key no reader has: unknown.
    CHECK(refused_with(node("BWBandpassFilter", R"({"source":{"ref":"a"},"cutoff":500.0})"),
                       "node 'n' (BWBandpassFilter): unknown key 'cutoff'"));
}

void run_feed_shape_refusal_tests() {
    using namespace retired_forms;
    std::printf("a feed holds 'ref' or 'tap' alone; a slot takes a feed, not an object\n");
    // A key beside a feed's ref or tap that the table has no row for.
    CHECK(refused_with(node("MultiSource", R"({"source":[{"ref":"a"},{"ref":"b","foo":1}]})"),
                       "node 'n' (MultiSource): 'source': a feed holds 'ref' or 'tap' alone, not 'foo'"));
    CHECK(refused_with(node("SineSource", R"({"amplitude":{"ref":"a","foo":1}})"),
                       "node 'n' (SineSource): 'amplitude': a feed holds 'ref' or 'tap' alone, not 'foo'"));
    CHECK(refused_with(node("SineSource", R"({"amplitude":{"tap":"b","foo":1}})"),
                       "node 'n' (SineSource): 'amplitude': a feed holds 'ref' or 'tap' alone, not 'foo'"));
    // MultiSource's rows are its own: a weight on another node's feed is the general case.
    CHECK(refused_with(node("SineSource", R"({"amplitude":{"ref":"a","weight":0.5}})"),
                       "node 'n' (SineSource): 'amplitude': a feed holds 'ref' or 'tap' alone, not 'weight'"));
    CHECK(refused_with(node("CombinedSource", R"({"source1":{"ref":"a","delay":2}})"),
                       "node 'n' (CombinedSource): 'source1': a feed holds 'ref' or 'tap' alone, not 'delay'"));
    // Both ref and tap.
    CHECK(refused_with(node("SineSource", R"({"amplitude":{"ref":"a","tap":"b"}})"),
                       "node 'n' (SineSource): 'amplitude': a feed holds 'ref' or 'tap', not both"));
    CHECK(refused_with(node("MultiSource", R"({"source":[{"ref":"a","tap":"b"}]})"),
                       "node 'n' (MultiSource): 'source': a feed holds 'ref' or 'tap', not both"));
    // An object with neither on a slot the table has no row for.
    CHECK(refused_with(node("AdditiveSource", R"({"formant":{"foo":1}})"),
                       "node 'n' (AdditiveSource): 'formant' holds an object where a slot takes {\"ref\": id}"));
    // On a signal pin, the same object reaches the resolver's own sentence.
    CHECK(refused_with(node("SineSource", R"({"amplitude":{"foo":1}})"),
                       "node 'n' (SineSource): 'amplitude' takes a number, {\"ref\": id} or {\"tap\": id}, not {\"foo\":1}"));
    // A plain feed and a tap load.
    CHECK(loads_both(node("SineSource", R"({"amplitude":{"ref":"a"},"frequency":{"tap":"b"}})")));
}

void run_declared_reader_tests() {
    using enum_column::load_node;
    using enum_column::load_setting;
    std::printf("the deleted readers' keys, read by the declarations alone\n");
    // A VarSource with empty params and with none: nothing wired, val 1.
    for (bool withParams : {true, false}) {
        LoadedInstrument ip;
        auto n = load_node("VarSource", withParams ? nlohmann::json::object() : nlohmann::json(), ip);
        auto* v = dynamic_cast<VarSource*>(n.get());
        CHECK(v && !v->get_param("val") && !v->get_param("var") && !v->get_param("varPct"));
        CHECK(v && v->val.current() == 1.0f && v->get_setting("absolute") == 1.0f);
        if (v) { v->prepare(RenderContext{48000}); v->start(4); CHECK(v->next() == 1.0f); }
    }
    // CombinedSource's operation by its labels and indices; "add", which
    // matches no label, and a label in another case are refused.
    for (const char* word : {"add", "mix", "fade", "sum"}) {
        const std::string msg = load_refused(enum_column::one_node("CombinedSource", {{"operation", word}}));
        const bool named = msg.find(std::string("'operation' holds \"") + word + "\", which is not one of its labels") != std::string::npos;
        if (!named) std::printf("  operation %s: %s\n", word, msg.c_str());
        CHECK(named);
    }
    const std::pair<nlohmann::json, float> ops[] = {
        {"Mix", 0.0f}, {"Fade", 2.0f}, {"Multiply", 1.0f}, {"Sum", 3.0f},
        {0, 0.0f}, {1, 1.0f}, {2, 2.0f}, {3, 3.0f}};
    for (const auto& [op, want] : ops) {
        const float got = load_setting("CombinedSource", "operation", op);
        if (got != want) std::printf("  operation %s: %g\n", op.dump().c_str(), got);
        CHECK(got == want);
    }
    {
        LoadedInstrument ip;
        auto c = load_node("CombinedSource", nlohmann::json{{"operation", "Multiply"}}, ip);
        CHECK(c && c->get_setting("gainAdj") == 0.0f && c->get_setting("operation") == 1.0f);
    }
    CHECK(load_setting("RepeatingSource", "gapDuration", 0.5) == 0.5f);
    // A CrossfadeSource and a RepeatingSource with no settings hold the declared defaults.
    for (const char* type : {"CrossfadeSource", "RepeatingSource"})
        for (bool withParams : {true, false}) {
            LoadedInstrument ip;
            auto n = load_node(type, withParams ? nlohmann::json::object() : nlohmann::json(), ip);
            CHECK(n != nullptr);
            if (!n) continue;
            CHECK(!n->setting_descriptors().empty());
            for (const auto& d : n->setting_descriptors()) CHECK(n->get_setting(d.name) == d.default_value);
        }
}

void run_branch_key_tests() {
    using namespace retired_forms;
    std::printf("the declarations hold no key a deleted reader alone read\n");
    // Each key that left the list, on a type that declares none of them.
    for (const char* key : {"weight", "delay", "sections", "targetPartials", "oneShot", "normalized",
                            "absolute", "baseValue", "bias", "varPct", "name"})
        CHECK(refused_with(node("SineSource", std::string("{\"") + key + "\":1}"),
                           std::string("node 'n' (SineSource): unknown key '") + key + "'"));
    CHECK(refused_with(node("MultiSource", R"({"source":[{"ref":"a"}],"delay":2})"), "node 'n' (MultiSource): unknown key 'delay'"));
    // Where a type declares the key, it loads.
    CHECK(loads_both(node("BWLowpassFilter", R"({"source":{"ref":"a"},"sections":3})")));
    CHECK(loads_both(node("MultiSource", R"({"source":[{"ref":"a"},{"ref":"b"}],"weights":[0.5,0.25]})")));
    CHECK(loads_both(node("NameGate", R"({"name":"tongue"})")));
    CHECK(loads_both(node("SegmentSource", R"({"values":[0.0,1.0],"oneShot":true})")));
}
namespace plain_parts {
struct Case { const char* name; nlohmann::json signals; std::vector<Chan> chans; nlohmann::json mix; };
}  // namespace plain_parts

// A channel's or a mixer's node-level `inputs` fails the load of a plain sound
// and of an instrument, with the table's sentence after the node's id and
// type; the same parts with their feeds in params load.
void run_retired_spelling_load_tests() {
    using plain_parts::file_refusal;
    std::printf("retired spellings: a channel's and a mixer's 'inputs' refused in a file and an instrument\n");
    const std::string conv = "is no longer read: python tools/convert_patch.py <patch> rewrites it as ";
    const std::string osc = R"({"id":"osc","type":"SineSource","params":{"frequency":220.0,"amplitude":0.5}})";
    const std::string chNew = R"({"id":"ch1","type":"SoundChannel","params":{"source":{"ref":"osc"},"volume":0.5}})";
    const std::string chOld = R"({"id":"ch1","type":"SoundChannel","inputs":{"source":"osc"},"params":{"volume":0.5}})";
    const std::string mixNew = R"({"id":"mix","type":"StereoMixer","params":{"channels":[{"ref":"ch1"}],"gainL":0.5}})";
    const std::string mixOld = R"({"id":"mix","type":"StereoMixer","inputs":{"channels":["ch1"]},"params":{"gainL":0.5}})";
    const std::string chWant = "node 'ch1' (SoundChannel): 'inputs' beside params " + conv + "the feed 'source' in params";
    const std::string mixWant = "node 'mix' (StereoMixer): 'inputs' beside params " + conv + "the feeds 'channels' in params";
    const std::string none;
    auto nodes = [&](const std::string& ch, const std::string& mix) { return osc + "," + ch + "," + mix + "]}}"; };
    auto instrument_refusal = [&](const std::string& ch, const std::string& mix) -> std::string {
        return load_refused(R"({"sampleRate":48000,"instrument":{"polyphony":1},"graph":{"output":"osc","nodes":[)" + nodes(ch, mix));
    };
    struct Case { const char* what; const std::string& ch; const std::string& mix; const std::string& want; };
    const Case cases[] = {{"the channel's", chOld, mixNew, chWant}, {"the mixer's", chNew, mixOld, mixWant},
                          {"both, the channel first", chOld, mixOld, chWant}, {"neither", chNew, mixNew, none}};
    for (const auto& c : cases) {
        const std::string inFile = file_refusal("renders/scratch/_retired_inputs.json",
            R"({"sampleRate":48000,"seconds":0.05,"graph":{"output":"mix","nodes":[)" + nodes(c.ch, c.mix));
        const std::string inVoice = instrument_refusal(c.ch, c.mix);
        if (!refusal_says(inFile, c.want) || !refusal_says(inVoice, c.want))
            std::printf("  %s: file \"%s\", instrument \"%s\"\n", c.what, inFile.c_str(), inVoice.c_str());
        CHECK(refusal_says(inFile, c.want));
        CHECK(refusal_says(inVoice, c.want));
    }
}
