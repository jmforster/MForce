#include "mforce/testing/check.h"
#include "test_support.h"
#include "patch/tests/fixtures.h"
#include "mforce/patch/fragment.h"
#include "mforce/patch/graph.h"
#include "mforce/patch/instrument_block.h"
#include "mforce/patch/layout.h"
#include "mforce/patch/node.h"
#include "mforce/patch/patch.h"
#include "mforce/patch/patch_json.h"
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

using namespace mforce;

namespace patch_codec {
// One refusal case: the text read and the whole message it must give.
struct Refused { const char* name; std::string text; std::string want; };
static void run_refusals(const std::vector<Refused>& cases) {
    for (const auto& c : cases) {
        const std::string got = refusal(c.text);
        if (got != c.want) std::printf("  %s:\n    got  %s\n    want %s\n", c.name, got.c_str(), c.want.c_str());
        CHECK(got == c.want);
    }
}
// An instrument whose top level, besides the graph of one sine, holds `extra`.
static std::string top(const std::string& extra) {
    return "{" + extra + R"J(,"instrument":{},"graph":{"output":"o","nodes":[{"id":"o","type":"SineSource"}]}})J";
}
// An instrument of one sine whose block is `block`.
static std::string block(const std::string& b) {
    return R"J({"instrument":)J" + b + R"J(,"graph":{"output":"o","nodes":[{"id":"o","type":"SineSource"}]}})J";
}
// An instrument of one sine whose layout is `layout`.
static std::string layout(const std::string& l) {
    return R"J({"instrument":{},"graph":{"output":"o","nodes":[{"id":"o","type":"SineSource"}]},"layout":)J" + l + "}";
}
static const std::string kFeedTakes = R"J({"ref": id} or {"tap": id})J";
// The document's levels: the top, the block, the graph and the layout.
static std::vector<Refused> document_refusals() {
    return {
        {"a top-level frames", top(R"J("frames":96000)J"), "frames: unknown key 'frames'"},
        {"a stray key at the top", top(R"J("author":"M")J"), "author: unknown key 'author'"},
        {"sampleRate with a fraction", top(R"J("sampleRate":44100.5)J"), "sampleRate: 'sampleRate' takes a whole number, not 44100.5"},
        {"seconds as a string", top(R"J("seconds":"2")J"), "seconds: 'seconds' takes a number, not \"2\""},
        {"a comment that is not a string", top(R"J("comment":5)J"), "comment: 'comment' takes a string, not 5"},
        {"a comment as a list", top(R"J("comment":["a"])J"), "comment: 'comment' takes a string, not [\"a\"]"},
        {"a graph that is not an object", R"J({"graph":[]})J", "graph: 'graph' takes an object, not []"},
        {"nodes that are not a list", R"J({"graph":{"nodes":{}}})J", "graph.nodes: 'nodes' takes a list, not {}"},
        {"an output that is not a string", R"J({"graph":{"output":3}})J", "graph.output: 'output' takes a string, not 3"},
        {"a stray key in the graph", R"J({"graph":{"extras":1}})J", "graph.extras: unknown key 'extras'"},
        {"a node that is not an object", with_node("5"), "graph.nodes[1]: 'nodes[1]' takes an object, not 5"},
        {"an instrument that is not an object", R"J({"instrument":true})J", "instrument: 'instrument' takes an object, not true"},
        {"instrument.release", block(R"J({"release":0.3})J"), "instrument.release: unknown key 'release'"},
        {"instrument.paramMap", block(R"J({"paramMap":{}})J"),
         "instrument.paramMap: instrument.paramMap is no longer read: python tools/convert_patch.py <patch> rewrites it as wiring"},
        {"polyphony as a string", block(R"J({"polyphony":"4"})J"), "instrument.polyphony: 'polyphony' takes a whole number, not \"4\""},
        {"volume as a bool", block(R"J({"volume":true})J"), "instrument.volume: 'volume' takes a number, not true"},
        {"glideMs as null", block(R"J({"glideMs":null})J"), "instrument.glideMs: 'glideMs' takes a number, not null"},
        {"glideMs as a bool", block(R"J({"glideMs":true})J"), "instrument.glideMs: 'glideMs' takes a number, not true"},
        {"polyphony with a fraction", block(R"J({"polyphony":1.5})J"),
         "instrument.polyphony: 'polyphony' takes a whole number, not 1.5"},
        {"sustaining as 1", block(R"J({"sustaining":1})J"), "instrument.sustaining: 'sustaining' takes true or false, not 1"},
        {"onsets holding a number", block(R"J({"onsets":[1]})J"), "instrument.onsets: 'onsets' takes a list of strings, not [1]"},
        {"a layout that is not an object", R"J({"layout":[]})J", "layout: 'layout' takes an object, not []"},
        {"a stray key in the layout", layout(R"J({"zoom":2})J"), "layout.zoom: unknown key 'zoom'"},
        {"a position of one number", layout(R"J({"positions":{"o":[1]}})J"), "layout.positions.o: 'o' takes [x, y], not [1]"},
        {"panning as a string", layout(R"J({"panning":"x"})J"), "layout.panning: 'panning' takes [x, y], not \"x\""},
        {"groups that are not a list", layout(R"J({"groups":{}})J"), "layout.groups: 'groups' takes a list, not {}"},
        {"a group's stray key", layout(R"J({"groups":[{"name":"G","color":1}]})J"), "layout.groups[0].color: unknown key 'color'"},
        {"a group's pos of three", layout(R"J({"groups":[{"name":"G","pos":[1,2,3]}]})J"),
         "layout.groups[0].pos: 'pos' takes [x, y], not [1,2,3]"},
        {"a group's member that is a number", layout(R"J({"groups":[{"name":"G","members":["o",1]}]})J"),
         "layout.groups[0].members: 'members' takes a list of strings, not [\"o\",1]"},
        {"a face's stray key", layout(R"J({"faces":[{"label":"F","colour":1}]})J"), "layout.faces[0].colour: unknown key 'colour'"},
        {"a face's field that is a number", layout(R"J({"faces":[{"label":"F","fields":{"frequency":3}}]})J"),
         "layout.faces[0].fields.frequency: 'frequency' takes a string, not 3"},
    };
}
// The nineteen rows of retired_key_refusal's table, each at its own place.
static std::vector<Refused> retired_refusals() {
    const std::string tail = " is no longer read: python tools/convert_patch.py <patch> rewrites it as ";
    return {
        {"Envelope.stages", with_params("e", "Envelope", R"J({"stages":[]})J"),
         "graph.nodes[1].params.stages: node 'e' (Envelope): 'stages'" + tail + "the eight stage columns"},
        {"CurveNode.knots", with_params("c", "CurveNode", R"J({"knots":[]})J"),
         "graph.nodes[1].params.knots: node 'c' (CurveNode): 'knots'" + tail + "the columns x and y"},
        {"CurveNode.exprKnots", with_params("c", "CurveNode", R"J({"exprKnots":[]})J"),
         "graph.nodes[1].params.exprKnots: node 'c' (CurveNode): 'exprKnots'" + tail + "the columns exprX, exprForm, exprA and exprB"},
        {"PhasedValueSource.stages", with_params("v", "PhasedValueSource", R"J({"stages":[]})J"),
         "graph.nodes[1].params.stages: node 'v' (PhasedValueSource): 'stages'" + tail + "sources and the columns percent, minSec, maxSec and gain"},
        {"BWLowpassFilter.cutoff", with_params("f", "BWLowpassFilter", R"J({"cutoff":800})J"),
         "graph.nodes[1].params.cutoff: node 'f' (BWLowpassFilter): 'cutoff'" + tail + "cutoffFreq"},
        {"BWHighpassFilter.cutoff", with_params("f", "BWHighpassFilter", R"J({"cutoff":800})J"),
         "graph.nodes[1].params.cutoff: node 'f' (BWHighpassFilter): 'cutoff'" + tail + "cutoffFreq"},
        {"SVFSource.cutoff", with_params("f", "SVFSource", R"J({"cutoff":800})J"),
         "graph.nodes[1].params.cutoff: node 'f' (SVFSource): 'cutoff'" + tail + "cutoffFreq"},
        {"RepeatingSource.gap", with_params("r", "RepeatingSource", R"J({"gap":0.2})J"),
         "graph.nodes[1].params.gap: node 'r' (RepeatingSource): 'gap'" + tail + "gapDuration"},
        {"MultiSource feed weight", with_params("m", "MultiSource", R"J({"source":[{"ref":"o","weight":0.5}]})J"),
         "graph.nodes[1].params.source[0]: node 'm' (MultiSource): 'source': 'weight' on a feed" + tail + "the array weights"},
        {"MultiSource feed delay", with_params("m", "MultiSource", R"J({"source":[{"ref":"o","delay":0.5}]})J"),
         "graph.nodes[1].params.source[0]: node 'm' (MultiSource): 'source': 'delay' on a feed" + tail + "the array delays"},
        {"FullPartials inline expandRule", with_params("p", "FullPartials", R"J({"expandRule":{"recurse":1}})J"),
         "graph.nodes[1].params.expandRule: node 'p' (FullPartials): 'expandRule' as an inline object" + tail + "an ExpandRule node wired by ref"},
        {"SequencePartials inline expandRule", with_params("p", "SequencePartials", R"J({"expandRule":{"recurse":1}})J"),
         "graph.nodes[1].params.expandRule: node 'p' (SequencePartials): 'expandRule' as an inline object" + tail + "an ExpandRule node wired by ref"},
        {"ExplicitPartials inline expandRule", with_params("p", "ExplicitPartials", R"J({"expandRule":{"recurse":1}})J"),
         "graph.nodes[1].params.expandRule: node 'p' (ExplicitPartials): 'expandRule' as an inline object" + tail + "an ExpandRule node wired by ref"},
        {"SoundChannel inputs", with_node(R"J({"id":"c","type":"SoundChannel","inputs":{}})J"),
         "graph.nodes[1].inputs: node 'c' (SoundChannel): 'inputs' beside params" + tail + "the feed 'source' in params"},
        {"StereoMixer inputs", with_node(R"J({"id":"x","type":"StereoMixer","inputs":{}})J"),
         "graph.nodes[1].inputs: node 'x' (StereoMixer): 'inputs' beside params" + tail + "the feeds 'channels' in params"},
        {"the top's ui", top(R"J("ui":{})J"), "ui: 'ui' at the top of the file" + tail + "layout"},
        {"the top's groups", top(R"J("groups":[])J"), "groups: 'groups' at the top of the file" + tail + "layout.groups"},
        {"the top's _comment", top(R"J("_comment":"c")J"), "_comment: '_comment' at the top of the file" + tail + "comment"},
        {"the top's _provenance", top(R"J("_provenance":{})J"), "_provenance: '_provenance' at the top of the file" + tail + "comment"},
    };
}
// A node's own keys, its type, its dynamic pins and its seed.
static std::vector<Refused> node_refusals() {
    const std::string seed = "graph.nodes[1].params.seed: node 'x' (SineSource): 'seed' takes a whole number from 0 to 4294967295, not ";
    const std::string pin = "graph.nodes[1].dynamicPins.timeScale: node 'e' (Envelope): 'timeScale' in dynamicPins takes {\"ref\": id}, not ";
    return {
        {"a stray key on a node", with_node(R"J({"id":"x","type":"SineSource","label":"L"})J"),
         "graph.nodes[1].label: node 'x' (SineSource): unknown key 'label'"},
        {"an id that is not a string", with_node(R"J({"id":5,"type":"SineSource"})J"), "graph.nodes[1].id: 'id' takes a string, not 5"},
        {"a type that is not a string", with_node(R"J({"id":"x","type":[]})J"), "graph.nodes[1].type: 'type' takes a string, not []"},
        {"a node comment that is not a string", with_node(R"J({"id":"x","type":"SineSource","comment":5})J"),
         "graph.nodes[1].comment: node 'x' (SineSource): 'comment' takes a string, not 5"},
        {"a node comment as a list", with_node(R"J({"id":"x","type":"SineSource","comment":["a","b"]})J"),
         "graph.nodes[1].comment: node 'x' (SineSource): 'comment' takes a string, not [\"a\",\"b\"]"},
        {"params that are not an object", with_node(R"J({"id":"x","type":"SineSource","params":[]})J"),
         "graph.nodes[1].params: node 'x' (SineSource): 'params' takes an object, not []"},
        {"an unknown type", with_node(R"J({"id":"x","type":"NoSuchSource"})J"),
         "graph.nodes[1].type: node 'x' (NoSuchSource): Unknown node type: NoSuchSource"},
        {"a retired type", with_node(R"J({"id":"x","type":"AREnvelope"})J"),
         "graph.nodes[1].type: node 'x' (AREnvelope): Unknown node type: AREnvelope; python tools/convert_patch.py <patch> "
         "rewrites a type this engine no longer has"},
        {"frequency on a WhiteNoiseSource", with_params("n", "WhiteNoiseSource", R"J({"frequency":440})J"),
         "graph.nodes[1].params.frequency: node 'n' (WhiteNoiseSource): unknown key 'frequency'"},
        {"a null dynamic pin", with_node(R"J({"id":"e","type":"Envelope","dynamicPins":{"timeScale":null}})J"), pin + "null"},
        {"a tap dynamic pin", with_node(R"J({"id":"e","type":"Envelope","dynamicPins":{"timeScale":{"tap":"o"}}})J"),
         pin + "{\"tap\":\"o\"}"},
        {"a dynamic pin with an extra key", with_node(R"J({"id":"e","type":"Envelope","dynamicPins":{"timeScale":{"ref":"o","x":1}}})J"),
         pin + "{\"ref\":\"o\",\"x\":1}"},
        {"dynamicPins that are not an object", with_node(R"J({"id":"e","type":"Envelope","dynamicPins":3})J"),
         "graph.nodes[1].dynamicPins: node 'e' (Envelope): 'dynamicPins' takes an object, not 3"},
        {"dynamicPins as null", with_node(R"J({"id":"e","type":"Envelope","dynamicPins":null})J"),
         "graph.nodes[1].dynamicPins: node 'e' (Envelope): 'dynamicPins' takes an object, not null"},
        {"a negative seed", with_params("x", "SineSource", R"J({"seed":-5})J"), seed + "-5"},
        {"a fractional seed", with_params("x", "SineSource", R"J({"seed":7.5})J"), seed + "7.5"},
        {"a seed past 32 bits", with_params("x", "SineSource", R"J({"seed":4294967296})J"), seed + "4294967296"},
        {"a seed as a string", with_params("x", "SineSource", R"J({"seed":"7"})J"), seed + "\"7\""},
        {"a seed as a bool", with_params("x", "SineSource", R"J({"seed":true})J"), seed + "true"},
    };
}
// A pin, a list pin and a slot given what they do not take.
static std::vector<Refused> pin_refusals() {
    const std::string sine = "graph.nodes[1].params.frequency: node 'x' (SineSource): 'frequency'";
    const std::string multi = "graph.nodes[1].params.source";
    const std::string wave = "graph.nodes[1].params.evolution: node 'w' (WavetableSource): 'evolution'";
    return {
        {"a string on a pin", with_params("x", "SineSource", R"J({"frequency":"440"})J"),
         sine + " is the string \"440\"; a pin takes a number, " + kFeedTakes},
        {"a list on a pin that is not one", with_params("x", "SineSource", R"J({"frequency":[{"ref":"o"}]})J"),
         sine + " takes one feed, not a list"},
        {"a feed with both keys", with_params("x", "SineSource", R"J({"frequency":{"ref":"o","tap":"o"}})J"),
         sine + ": a feed holds 'ref' or 'tap', not both"},
        {"a key beside a feed's", with_params("x", "SineSource", R"J({"frequency":{"ref":"o","gain":2}})J"),
         sine + ": a feed holds 'ref' or 'tap' alone, not 'gain'"},
        {"a bool on a pin", with_params("x", "SineSource", R"J({"frequency":true})J"), sine + " takes a number, " + kFeedTakes + ", not true"},
        {"null on a pin", with_params("x", "SineSource", R"J({"frequency":null})J"), sine + " takes a number, " + kFeedTakes + ", not null"},
        {"an object that is no feed on a pin", with_params("x", "SineSource", R"J({"frequency":{"value":3}})J"),
         sine + " takes a number, " + kFeedTakes + ", not {\"value\":3}"},
        {"a ref that is not a string", with_params("x", "SineSource", R"J({"frequency":{"ref":3}})J"),
         sine + " takes a number, " + kFeedTakes + ", not {\"ref\":3}"},
        {"a number in a list of feeds", with_params("m", "MultiSource", R"J({"source":[{"ref":"o"},0.25]})J"),
         multi + "[1]: node 'm' (MultiSource): 'source' holds 0.25, where a list holds " + kFeedTakes},
        {"a string in a list of feeds", with_params("m", "MultiSource", R"J({"source":["o"]})J"),
         multi + "[0]: node 'm' (MultiSource): 'source' holds \"o\", where a list holds " + kFeedTakes},
        {"a string on a list pin", with_params("m", "MultiSource", R"J({"source":"o"})J"),
         multi + ": node 'm' (MultiSource): 'source' is the string \"o\"; a pin takes a number, " + kFeedTakes},
        {"a tap on a slot", with_params("w", "WavetableSource", R"J({"evolution":{"tap":"o"}})J"),
         wave + " is a slot: it takes {\"ref\": id}, not {\"tap\":\"o\"}"},
        {"a number on a slot", with_params("w", "WavetableSource", R"J({"evolution":3})J"), wave + " is a slot: it takes {\"ref\": id}, not 3"},
        {"an object that is no feed on a slot", with_params("w", "WavetableSource", R"J({"evolution":{"type":"PluckEvolution"}})J"),
         wave + " holds an object where a slot takes {\"ref\": id}"},
        {"a string on a slot", with_params("w", "WavetableSource", R"J({"evolution":"pluck"})J"),
         wave + " is the string \"pluck\"; a slot takes {\"ref\": id}; the string-typed evolution is gone, python "
                "tools/convert_patch.py <patch> rewrites it as an evolution node"},
        {"a list on a single slot", with_params("w", "WavetableSource", R"J({"evolution":[{"ref":"o"}]})J"), wave + " takes one feed, not a list"},
        {"a tap in a multi slot's list", with_params("mx", "StereoMixer", R"J({"channels":[{"tap":"o"}]})J"),
         "graph.nodes[1].params.channels[0]: node 'mx' (StereoMixer): 'channels' is a slot: it takes {\"ref\": id}, not {\"tap\":\"o\"}"},
    };
}
// Settings and arrays given what their kind does not take; and the
// refusals check() makes once the read is whole.
static std::vector<Refused> value_refusals() {
    const std::string svf = "graph.nodes[1].params.mode: node 's' (SVFSource): 'mode' holds ";
    const std::string labels = ", which is not one of its labels (Lowpass, Highpass, Bandpass, Lowpass1P, Highpass1P) or an index into them";
    const std::string column = "graph.nodes[1].params.type: node 'e' (Envelope): 'type' holds ";
    const std::string stageLabels = ", which is not one of its labels (Linear, Expo, InverseExpo, Sine, Hold) or an index into them";
    return {
        {"a string on a float setting", with_params("b", "Biquad", R"J({"b0":"1.5"})J"),
         "graph.nodes[1].params.b0: node 'b' (Biquad): 'b0' takes a number, not \"1.5\""},
        {"a bool on a float setting", with_params("b", "Biquad", R"J({"b0":true})J"),
         "graph.nodes[1].params.b0: node 'b' (Biquad): 'b0' takes a number, not true"},
        {"null on a float setting", with_params("b", "Biquad", R"J({"b0":null})J"),
         "graph.nodes[1].params.b0: node 'b' (Biquad): 'b0' takes a number, not null"},
        {"a list on a float setting", with_params("b", "Biquad", R"J({"b0":[1.5]})J"),
         "graph.nodes[1].params.b0: node 'b' (Biquad): 'b0' takes a number, not [1.5]"},
        {"a feed on a float setting", with_params("b", "Biquad", R"J({"b1":{"ref":"o"}})J"),
         "graph.nodes[1].params.b1: node 'b' (Biquad): 'b1' takes a number, not {\"ref\":\"o\"}"},
        {"an int given a fraction", with_params("f", "BWLowpassFilter", R"J({"sections":2.5})J"),
         "graph.nodes[1].params.sections: node 'f' (BWLowpassFilter): 'sections' takes a whole number, not 2.5"},
        {"a bool given 2", with_params("s", "SVFSource", R"J({"normalize":2})J"),
         "graph.nodes[1].params.normalize: node 's' (SVFSource): 'normalize' takes true or false, not 2"},
        {"a bool given a string", with_params("s", "SVFSource", R"J({"normalize":"true"})J"),
         "graph.nodes[1].params.normalize: node 's' (SVFSource): 'normalize' takes true or false, not \"true\""},
        {"an enum word that is no label", with_params("s", "SVFSource", R"J({"mode":"Bandstop"})J"), svf + "\"Bandstop\"" + labels},
        {"an enum label in another case", with_params("s", "SVFSource", R"J({"mode":"highpass"})J"), svf + "\"highpass\"" + labels},
        {"an enum index outside its labels", with_params("s", "SVFSource", R"J({"mode":5})J"), svf + "5" + labels},
        {"an enum given a fraction", with_params("s", "SVFSource", R"J({"mode":1.5})J"), svf + "1.5" + labels},
        {"an enum given a bool", with_params("s", "SVFSource", R"J({"mode":true})J"), svf + "true" + labels},
        {"a number on a string setting", with_params("g", "NameGate", R"J({"name":3})J"),
         "graph.nodes[1].params.name: node 'g' (NameGate): 'name' takes a string, not 3"},
        {"a number on an array", with_params("sg", "SegmentSource", R"J({"values":0.5})J"),
         "graph.nodes[1].params.values: node 'sg' (SegmentSource): 'values' takes a list of numbers, not 0.5"},
        {"a string in an array", with_params("sg", "SegmentSource", R"J({"values":[1,"a"]})J"),
         "graph.nodes[1].params.values: node 'sg' (SegmentSource): 'values' takes a list of numbers, not [1,\"a\"]"},
        {"a bool in an array", with_params("sg", "SegmentSource", R"J({"values":[1,true]})J"),
         "graph.nodes[1].params.values: node 'sg' (SegmentSource): 'values' takes a list of numbers, not [1,true]"},
        {"a labelled column holding no label", with_params("e", "Envelope", "{" + stage_columns(R"J(["Linear","Bogus"])J") + "}"),
         column + "\"Bogus\"" + stageLabels},
        {"a labelled column's label in another case", with_params("e", "Envelope", "{" + stage_columns(R"J(["linear",0])J") + "}"),
         column + "\"linear\"" + stageLabels},
        {"a float setting outside its range", with_params("b", "Biquad", R"J({"b0":12})J"),
         "graph.nodes[1].params.b0: node 'b' (Biquad): 'b0' is 12, outside its range -10 to 10"},
        {"an int setting outside its range", with_params("c", "CellularAutomatonEvolution", R"J({"rule":300})J"),
         "graph.nodes[1].params.rule: node 'c' (CellularAutomatonEvolution): 'rule' is 300, outside its range 0 to 255"},
        {"two violations, one per line", with_params("b", "Biquad", R"J({"b0":12,"a2":-3})J"),
         "graph.nodes[1].params.a2: node 'b' (Biquad): 'a2' is -3, outside its range -1 to 1\n"
         "graph.nodes[1].params.b0: node 'b' (Biquad): 'b0' is 12, outside its range -10 to 10"},
        {"a dynamic pin in a plain sound",
         R"J({"seconds":1,"graph":{"output":"mx","nodes":[{"id":"o","type":"SineSource"},)J"
         R"J({"id":"b","type":"Biquad","params":{"source":{"ref":"o"}},"dynamicPins":{"b0":{"ref":"o"}}},)J"
         R"J({"id":"ch","type":"SoundChannel","params":{"source":{"ref":"b"}}},)J"
         R"J({"id":"mx","type":"StereoMixer","params":{"channels":[{"ref":"ch"}]}}]}})J",
         "graph.nodes[1].dynamicPins.b0: node 'b' (Biquad): a plain sound has no note, so nothing drives a dynamic pin"},
        {"a document that is not an object", "5", "a patch takes an object, not 5"},
    };
}
}  // namespace patch_codec

// Every refusal of the reader, each through load_text and compared whole:
// the document's levels, the nineteen rows of retired_key_refusal at their places
// (TopKey among them), a node's keys, its pins, its settings and arrays, and
// what check() refuses once the read is whole.
void run_codec_refusal_tests() {
    using namespace patch_codec;
    std::printf("the codec: every refusal with its path and its sentence\n");
    run_refusals(document_refusals());
    run_refusals(retired_refusals());
    run_refusals(node_refusals());
    run_refusals(pin_refusals());
    run_refusals(value_refusals());
    std::printf("  %zu document, %zu retired, %zu node, %zu pin and %zu value refusals\n", document_refusals().size(),
                retired_refusals().size(), node_refusals().size(), pin_refusals().size(), value_refusals().size());
}

// A read that fails leaves its target as it was: a Patch, a Node, a Graph, a
// block, a layout and a fragment, each given a record first.
void run_codec_untouched_tests() {
    using namespace patch_codec;
    std::printf("the codec: a read that fails leaves its target as it was\n");
    const Patch given = load_text(with_params("x", "SawSource", R"J({"frequency":330})J"));
    for (const std::string& bad : {with_params("x", "SawSource", R"J({"frequency":"330"})J"),
                                   with_params("b", "Biquad", R"J({"b0":12})J"), top(R"J("frames":1)J"),
                                   std::string(R"J({"graph":{"output":"o","nodes":[{"id":"o","type":"SineSource"},5]}})J")}) {
        Patch target = given;
        try { from_json(J::parse(bad), target); CHECK(false); } catch (const std::runtime_error&) {}
        CHECK(target == given);
    }
    const Node node = read_node(R"J({"id":"x","type":"SawSource","params":{"frequency":330}})J");
    Node nodeTarget = node;
    try { from_json(J::parse(R"J({"id":"y","type":"SawSource","params":{"bogus":1}})J"), nodeTarget); CHECK(false); }
    catch (const std::runtime_error&) {}
    CHECK(nodeTarget == node);
    Graph graph = given.graph, graphTarget = given.graph;
    try { from_json(J::parse(R"J({"output":"z","nodes":[{"id":"z","type":"NoSuchSource"}]})J"), graphTarget); CHECK(false); }
    catch (const std::runtime_error&) {}
    CHECK(graphTarget == graph);
    const InstrumentBlock blockGiven{2, 0.5f, true, {"tongue"}, 20.0f};
    InstrumentBlock blockTarget = blockGiven;
    try { from_json(J::parse(R"J({"polyphony":3,"release":1})J"), blockTarget); CHECK(false); } catch (const std::runtime_error&) {}
    CHECK(blockTarget == blockGiven);
    Layout layoutGiven;
    layoutGiven.positions["x"] = Vec2{1, 2};
    Layout layoutTarget = layoutGiven;
    try { from_json(J::parse(R"J({"positions":{"y":[3,4]},"zoom":1})J"), layoutTarget); CHECK(false); } catch (const std::runtime_error&) {}
    CHECK(layoutTarget == layoutGiven);
    const Fragment fragmentGiven{{node}, {}, {}, {}};
    Fragment fragmentTarget = fragmentGiven;
    try { from_json(J::parse(R"J({"nodes":[],"faces":[3]})J"), fragmentTarget); CHECK(false); } catch (const std::runtime_error&) {}
    CHECK(fragmentTarget == fragmentGiven);
}
