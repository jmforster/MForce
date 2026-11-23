#include "mforce/testing/check.h"
#include "test_support.h"
#include "patch/tests/fixtures.h"
#include "mforce/core/descriptors.h"
#include "mforce/core/pins.h"
#include "mforce/patch/envelope_preset.h"
#include "mforce/patch/fragment.h"
#include "mforce/patch/instrument_block.h"
#include "mforce/patch/layout.h"
#include "mforce/patch/node.h"
#include "mforce/patch/patch.h"
#include "mforce/patch/patch_json.h"
#include "mforce/patch/type_declarations.h"
#include "mforce/source/envelope/envelope.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <optional>
#include <random>
#include <sstream>
#include <string>
#include <vector>

using namespace mforce;

namespace patch_codec {
static std::string text_of(const std::filesystem::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}
}  // namespace patch_codec

// Every patch of patches/library and patches/baselines, walked as it stands:
// load_patch reads it, a write and a read give the same record, the saved
// text ends in one newline, and the text of a save read and saved again is
// byte-equal.
void run_codec_identity_tests() {
    using namespace patch_codec;
    std::printf("the codec: every patch of library and baselines reads, writes and reads back the same\n");
    const std::string scratch = (std::filesystem::temp_directory_path() / "mforce_codec_identity.json").string();
    int files = 0, refused = 0, unequal = 0, resaved = 0;
    for (const char* dir : {"patches/library", "patches/baselines"}) {
        std::vector<std::filesystem::path> paths;
        for (const auto& e : std::filesystem::recursive_directory_iterator(dir))
            if (e.is_regular_file() && e.path().extension() == ".json") paths.push_back(e.path());
        std::sort(paths.begin(), paths.end());
        for (const auto& path : paths) {
            ++files;
            try {
                const Patch p = load_patch(path.string());
                Patch back;
                from_json(J(p), back);
                const bool same = back == p;
                if (!same) { ++unequal; std::printf("  a write and a read differ: %s\n", path.string().c_str()); }
                CHECK(same);
                save_patch(scratch, p);
                const std::string first = text_of(scratch);
                const bool oneNewline = first.size() >= 2 && first.back() == '\n' && first[first.size() - 2] != '\n';
                if (!oneNewline) std::printf("  the saved text does not end in one newline: %s\n", path.string().c_str());
                CHECK(oneNewline);
                const Patch again = load_patch(scratch);
                save_patch(scratch, again);
                const bool equal = text_of(scratch) == first && again == p;
                if (!equal) { ++resaved; std::printf("  a second save differs: %s\n", path.string().c_str()); }
                CHECK(equal);
            } catch (const std::exception& e) {
                ++refused;
                std::printf("  refused: %s\n", e.what());
                CHECK(false);
            }
        }
    }
    std::filesystem::remove(scratch);
    std::printf("  %d files: %d refused, %d unequal after a write and a read, %d whose second save differs\n",
                files, refused, unequal, resaved);
    CHECK(files > 0);
}
namespace patch_codec {
// A node of `type` read from `params` holds each declared Param in values at
// `param(p)`, each scalar setting at `setting(s)`, each string setting at
// `text(s)`, and nothing else.
template <class P, class S, class T>
static bool holds(const Node& n, const TypeDeclarations& d, P param, S setting, T text) {
    if (!n.wires.empty() || !n.arrays.empty() || n.seed || !n.dynamicPins.empty()) return false;
    size_t scalars = 0, strings = 0;
    if (n.values.size() != d.params.size()) return false;
    for (const auto& p : d.params) {
        auto it = n.values.find(p.name);
        if (it == n.values.end() || std::bit_cast<uint32_t>(it->second) != std::bit_cast<uint32_t>(param(p))) return false;
    }
    for (const auto& s : d.settings) {
        if (s.type == SettingType::String) {
            ++strings;
            auto it = n.strings.find(s.name);
            if (it == n.strings.end() || it->second != text(s)) return false;
        } else {
            ++scalars;
            auto it = n.settings.find(s.name);
            if (it == n.settings.end() || std::bit_cast<uint32_t>(it->second) != std::bit_cast<uint32_t>(setting(s))) return false;
        }
    }
    return n.settings.size() == scalars && n.strings.size() == strings;
}
// A bag giving every declared Param and setting of `d` a value off its default.
static J everything(const TypeDeclarations& d) {
    J params = J::object();
    for (const auto& p : d.params) params[p.name] = p.max;
    for (const auto& s : d.settings) {
        if (s.type == SettingType::String) params[s.name] = "given";
        else if (s.type == SettingType::Bool) params[s.name] = s.defaultValue == 0.0f;
        else if (!s.labels.empty()) params[s.name] = s.labels.back();
        else params[s.name] = s.max;
    }
    return params;
}
static float given_setting(const TypeDeclarations::Setting& s) {
    if (s.type == SettingType::Bool) return s.defaultValue == 0.0f ? 1.0f : 0.0f;
    if (!s.labels.empty()) return float(s.labels.size() - 1);
    return s.max;
}
}  // namespace patch_codec

// The model is complete in pins and settings and not in arrays: for every
// registered type but the Envelope (whose bag with no stage column is the
// preset form), a node given nothing holds every Param and setting at its
// declared default and no array; a node given everything holds what it was
// given, and writes and reads back the same.
void run_codec_complete_model_tests() {
    using namespace patch_codec;
    std::printf("the codec: the model complete in pins and settings, not in arrays\n");
    int types = 0;
    for (const auto& t : declared_types()) {
        const TypeDeclarations* d = type_declarations(t);
        if (!d || t == "Envelope") continue;
        ++types;
        try {
            const Node bare = read_node(J{{"id", "n"}, {"type", t}}.dump());
            const bool defaults = holds(bare, *d, [](const auto& p) { return p.defaultValue; },
                                        [](const auto& s) { return s.defaultValue; },
                                        [](const auto& s) { return s.defaultText; });
            if (!defaults) std::printf("  %s given nothing does not hold its defaults\n", t.c_str());
            CHECK(defaults);
            const Node full = read_node(J{{"id", "n"}, {"type", t}, {"params", everything(*d)}}.dump());
            const bool given = holds(full, *d, [](const auto& p) { return p.max; },
                                     [](const auto& s) { return given_setting(s); },
                                     [](const auto&) { return std::string("given"); });
            if (!given) std::printf("  %s given everything does not hold what it was given\n", t.c_str());
            CHECK(given);
            Node back;
            from_json(J(full), back);
            CHECK(back == full);
        } catch (const std::exception& e) {
            std::printf("  %s: %s\n", t.c_str(), e.what());
            CHECK(false);
        }
    }
    std::printf("  %d types read given nothing and given everything\n", types);
    CHECK(types > 0);
}

// A float's text reads back bit for bit: hand-picked values on a Param and
// on a Float setting, then a hundred thousand random finite bit patterns
// from a fixed seed through an array.
void run_codec_float_tests() {
    using namespace patch_codec;
    std::printf("the codec: a float written as its shortest text reads back bit for bit\n");
    const float picked[] = {0.1f, 0.6f, 1e-7f, 16777216.0f, -0.0f, 0.0f, 1.0f / 3.0f,
                            std::numeric_limits<float>::max(), std::numeric_limits<float>::lowest(),
                            std::numeric_limits<float>::min(), std::numeric_limits<float>::denorm_min(),
                            -std::numeric_limits<float>::denorm_min(),
                            // the two whose shortest text names a double that narrows to the next float
                            std::bit_cast<float>(uint32_t(0x15ae43fdu)), std::bit_cast<float>(uint32_t(0x95ae43fdu))};
    for (float v : picked) {
        Node n{"x", "Biquad"};
        n.values["frequency"] = v;
        n.settings["b0"] = v;
        const Node back = read_node(J(n).dump());
        const bool same = std::bit_cast<uint32_t>(back.values.at("frequency")) == std::bit_cast<uint32_t>(v) &&
                          std::bit_cast<uint32_t>(back.settings.at("b0")) == std::bit_cast<uint32_t>(v);
        if (!same) std::printf("  %.9g does not read back\n", v);
        CHECK(same);
    }
    Node short_text{"x", "SineSource"};
    short_text.values["phase"] = 0.6f;
    short_text.values["amplitude"] = 0.1f;
    const std::string text = J(short_text).dump();
    CHECK(text.find("\"phase\":0.6") != std::string::npos && text.find("\"amplitude\":0.1") != std::string::npos);
    std::mt19937 rng(46);
    int drawn = 0, differing = 0;
    while (drawn < 100000) {
        Node n{"s", "SegmentSource"};
        auto& rows = n.arrays["values"];
        while (rows.size() < 1000) {
            const float v = std::bit_cast<float>(uint32_t(rng()));
            if (std::isfinite(v)) rows.push_back(v);
        }
        drawn += int(rows.size());
        const Node back = read_node(J(n).dump());
        const auto& got = back.arrays.at("values");
        for (size_t i = 0; i < rows.size(); ++i)
            differing += std::bit_cast<uint32_t>(got[i]) != std::bit_cast<uint32_t>(rows[i]);
    }
    std::printf("  %d random finite floats written and read: %d differ\n", drawn, differing);
    CHECK(differing == 0);
}

// Each kind is written in its one form and read in its forms: an int as an
// integer, a bool as true or false (read also as 0 or 1), an enum and a
// labelled column as labels (read also as indices, never as a label in
// another case), a single pin's feed as one object and a list pin's as a
// list even when it holds one.
void run_codec_kind_tests() {
    using namespace patch_codec;
    std::printf("the codec: ints, bools, enums, labelled columns and feeds, by kind\n");
    const Node f = read_node(R"J({"id":"f","type":"BWLowpassFilter","params":{"sections":3}})J");
    CHECK(f.settings.at("sections") == 3.0f);
    CHECK(J(f)["params"]["sections"].is_number_integer() && J(f)["params"]["sections"] == 3);
    const Node s = read_node(R"J({"id":"s","type":"SVFSource","params":{"mode":"Highpass","normalize":true}})J");
    CHECK(s.settings.at("mode") == 1.0f && s.settings.at("normalize") == 1.0f);
    CHECK(J(s)["params"]["mode"] == "Highpass" && J(s)["params"]["normalize"] == true);
    CHECK(read_node(R"J({"id":"s","type":"SVFSource","params":{"mode":1}})J").settings.at("mode") == 1.0f);
    CHECK(read_node(R"J({"id":"s","type":"SVFSource","params":{"mode":4.0}})J").settings.at("mode") == 4.0f);
    for (const char* b : {"0", "1", "false", "true"}) {
        const Node n = read_node(std::string(R"J({"id":"s","type":"SVFSource","params":{"normalize":)J") + b + "}}");
        const float want = (std::string(b) == "1" || std::string(b) == "true") ? 1.0f : 0.0f;
        CHECK(n.settings.at("normalize") == want);
        CHECK(J(n)["params"]["normalize"] == (want == 1.0f));
    }
    CHECK(refusal(with_params("s", "SVFSource", R"J({"mode":"highpass"})J")) ==
          "graph.nodes[1].params.mode: node 's' (SVFSource): 'mode' holds \"highpass\", which is not one of its "
          "labels (Lowpass, Highpass, Bandpass, Lowpass1P, Highpass1P) or an index into them");
    const Node e = read_node("{\"id\":\"e\",\"type\":\"Envelope\",\"params\":{" + stage_columns(R"J(["Linear",3]})J") + "}");
    CHECK(e.arrays.at("type") == std::vector<float>({0.0f, 3.0f}));
    CHECK(J(e)["params"]["type"] == J::parse(R"J(["Linear","Sine"])J"));
    const Node m = read_node(R"J({"id":"m","type":"MultiSource","params":{"source":{"ref":"o"}}})J");
    CHECK(m.wires.at("source") == std::vector<Wire>({Wire{"o", false}}));
    CHECK(J(m)["params"]["source"] == J::parse(R"J([{"ref":"o"}])J"));
    const Node two = read_node(R"J({"id":"m","type":"MultiSource","params":{"source":[{"ref":"a"},{"tap":"b"}]}})J");
    CHECK(two.wires.at("source") == std::vector<Wire>({Wire{"a", false}, Wire{"b", true}}));
    CHECK(read_node(R"J({"id":"m","type":"MultiSource","params":{"source":[]}})J").wires.empty());
    CHECK(read_node(R"J({"id":"m","type":"MultiSource","params":{"source":0.5}})J").values.at("source") == 0.5f);
    const Node one = read_node(R"J({"id":"x","type":"SineSource","params":{"amplitude":{"tap":"o"}}})J");
    CHECK(one.wires.at("amplitude") == std::vector<Wire>({Wire{"o", true}}));
    CHECK(J(one)["params"]["amplitude"] == J::parse(R"J({"tap":"o"})J"));
    CHECK(one.values.count("amplitude") == 0 && one.values.count("frequency") == 1);
    const Node mix = read_node(R"J({"id":"mx","type":"StereoMixer","params":{"channels":{"ref":"c"}}})J");
    CHECK(J(mix)["params"]["channels"] == J::parse(R"J([{"ref":"c"}])J"));
    const Node w = read_node(R"J({"id":"w","type":"WavetableSource","params":{"evolution":{"ref":"ev"}}})J");
    CHECK(J(w)["params"]["evolution"] == J::parse(R"J({"ref":"ev"})J"));
    // A name the type does not declare is written by the map it sits in.
    Node odd{"x", "SineSource"};
    odd.settings["level"] = 2.0f;
    odd.wires["source"] = {Wire{"o"}};
    odd.strings["name"] = "n";
    const J oddJson = J(odd);
    CHECK(oddJson["params"]["level"] == 2.0 && oddJson["params"]["source"] == J::parse(R"J([{"ref":"o"}])J") &&
          oddJson["params"]["name"] == "n");
    Node lost{"x", "NoSuchSource"};
    lost.values["a"] = 1.0f;
    lost.arrays["b"] = {1.0f};
    CHECK(J(lost)["params"] == J::parse(R"J({"a":1.0,"b":[1.0]})J"));
}
namespace patch_codec {
// The preset cases: each of the six bare and with every knob it reads,
// the adsr in seconds, an as that says seconds, and the load test's node.
static const char* const kPresetCases[] = {
    R"J({"preset":"ar"})J",
    R"J({"preset":"ar","attack":0.4,"attackCurve":2,"attackPower":1.2,"releaseCurve":0,"releasePower":3.0,"attackMin":0.1,"attackMax":0.9})J",
    R"J({"preset":"as"})J",
    R"J({"preset":"as","attack":0.35,"attackCurve":1,"attackPower":2.5,"reverse":true,"sustainLevel":0.6})J",
    R"J({"preset":"asr"})J",
    R"J({"preset":"asr","attack":0.15,"attackCurve":2,"attackPower":1.5,"release":0.3,"releaseCurve":2,"releasePower":0.5,"sustainLevel":0.4})J",
    R"J({"preset":"ads"})J",
    R"J({"preset":"ads","attack":0.1,"attackCurve":3,"decay":0.25,"decayCurve":1,"decayPower":2.0,"sustainLevel":0.3})J",
    R"J({"preset":"adr"})J",
    R"J({"preset":"adr","attack":0.1,"attackPower":0.7,"decay":0.2,"decayCurve":3,"decayLevel":0.55,"releaseCurve":1,"releasePower":1.5})J",
    R"J({"preset":"adsr"})J",
    R"J({"preset":"adsr","attack":0.12,"attackCurve":1,"attackPower":0.8,"decay":0.2,"decayCurve":2,"decayPower":1.1,"sustainLevel":0.5,"release":0.25,"releaseCurve":0,"releasePower":2.0,"attackMin":0.05,"attackMax":0.9,"decayMin":0.01,"decayMax":0.4,"releaseMin":0.0,"releaseMax":0.3})J",
    R"J({"preset":"adsr","timeMode":"seconds","attack":0.05,"decay":0.1,"sustainLevel":0.6,"release":0.2,"attackCurve":"Expo"})J",
    R"J({"preset":"as","timeMode":"seconds","attack":0.3})J",
    R"J({"preset":"as","attack":0.3,"attackCurve":1,"attackPower":2.0,"reverse":true,"sustainLevel":0.6,"stage_accuracy":0.9,"ramp_accuracy":0.95,"seed":77,"minValue":0.0,"maxValue":0.8})J",
};
static const char* const kStageColumnNames[] = {"startVal", "endVal", "power", "type", "percent", "minSec", "maxSec", "nominal"};
// The node read from `params` holds the eight columns envelope_from_preset_json
// lays out from the same params, and nothing more in arrays.
static bool preset_columns(const Node& n, const J& params) {
    const auto env = envelope_from_preset_json(params);
    if (n.arrays.size() != std::size(kStageColumnNames)) return false;
    for (const char* c : kStageColumnNames)
        if (!n.arrays.count(c) || n.arrays.at(c) != env->get_array(c)) return false;
    return true;
}
}  // namespace patch_codec

// The preset form: its eight columns are envelope_from_preset_json's, timeMode
// and sustainLevel are settings as the file gives them or at their defaults,
// a node with neither form is an ar, both forms and an unknown preset are
// refused naming the node.
void run_codec_preset_tests() {
    using namespace patch_codec;
    std::printf("the codec: the Envelope's preset form read into its eight columns\n");
    for (const char* c : kPresetCases) {
        const J params = J::parse(c);
        const Node n = read_node(J{{"id", "e"}, {"type", "Envelope"}, {"params", params}}.dump());
        const bool columns = preset_columns(n, params);
        if (!columns) std::printf("  %s: the columns differ from the preset's\n", c);
        CHECK(columns);
        const float timeMode = params.value("timeMode", std::string("fraction")) == "seconds" ? 1.0f : 0.0f;
        CHECK(n.settings.at("timeMode") == timeMode);
        CHECK(n.settings.at("sustainLevel") == params.value("sustainLevel", 0.7f));
        CHECK(n.settings.at("stage_accuracy") == params.value("stage_accuracy", 1.0f));
        CHECK(n.seed == (params.contains("seed") ? std::optional<uint32_t>(77u) : std::optional<uint32_t>()));
        Node back;
        from_json(J(n), back);
        CHECK(back == n);
    }
    const Node bare = read_node(R"J({"id":"e","type":"Envelope"})J");
    const Node empty = read_node(R"J({"id":"e","type":"Envelope","params":{}})J");
    CHECK(preset_columns(bare, J::parse(R"J({"preset":"ar"})J")) && empty == bare);
    const auto ar = Envelope::make_ar(0.2f);
    for (const char* c : kStageColumnNames) CHECK(bare.arrays.at(c) == ar->get_array(c));
    CHECK(refusal(with_params("e", "Envelope", "{\"preset\":\"ar\"," + stage_columns("[0,0]") + "}")) ==
          "graph.nodes[1].params.preset: node 'e' (Envelope): holds both 'preset' and 'startVal'; an Envelope "
          "takes one form: the stage columns or a preset");
    CHECK(refusal(with_params("e", "Envelope", R"J({"preset":"adsx"})J")) ==
          "graph.nodes[1].params: node 'e' (Envelope): Unknown envelope preset: adsx");
    CHECK(refusal(with_params("e", "Envelope", R"J({"preset":"as","attackCurve":7})J")) ==
          "graph.nodes[1].params: node 'e' (Envelope): envelope preset: attackCurve is 7; a curve is 0 (Linear), "
          "1 (Expo), 2 (InverseExpo) or 3 (Sine)");
    CHECK(refusal(with_params("e", "Envelope", R"J({"preset":"ar","bogus":1})J")) ==
          "graph.nodes[1].params.bogus: node 'e' (Envelope): unknown key 'bogus'");
    CHECK(refusal(with_params("e", "Envelope", "{\"attack\":0.1," + stage_columns("[0,0]") + "}")) ==
          "graph.nodes[1].params.attack: node 'e' (Envelope): unknown key 'attack'");
}

// The passengers: the score's text is its block's dump(), [] kept apart from
// no score; a comment at both levels; a seed; seconds absent and present;
// sampleRate; an empty document an empty Patch.
void run_codec_passenger_tests() {
    using namespace patch_codec;
    std::printf("the codec: the score, the comments, a seed, seconds and the sample rate\n");
    const std::string score = R"J([{"note":60,"velocity":0.8,"duration":0.5,"time":0.0,"articulation":"staccato"}])J";
    const std::string node = R"J({"id":"x","type":"SineSource","params":{"seed":4294967295},"comment":"the tone"})J";
    const std::string text = R"J({"sampleRate":22050,"seconds":2.5,"comment":"a patch","score":)J" + score +
                             R"J(,"instrument":{},"graph":{"output":"x","nodes":[)J" + node + "]}}";
    const Patch p = load_text(text);
    CHECK(p.score == J::parse(score).dump());
    CHECK(p.comment == "a patch" && p.graph.nodes.at(0).comment == "the tone");
    CHECK(p.graph.nodes.at(0).seed == std::optional<uint32_t>(4294967295u));
    CHECK(p.seconds == std::optional<double>(2.5) && p.sampleRate == 22050);
    const J written = J(p);
    CHECK(written["score"] == J::parse(score) && written["comment"] == "a patch" && written["seconds"] == 2.5);
    CHECK(written["graph"]["nodes"][0]["comment"] == "the tone");
    CHECK(written["graph"]["nodes"][0]["params"]["seed"].is_number_unsigned() &&
          written["graph"]["nodes"][0]["params"]["seed"] == 4294967295u);
    const Patch emptyScore = load_text(R"J({"score":[],"instrument":{},"graph":{"output":"x","nodes":[{"id":"x","type":"SineSource"}]}})J");
    const Patch noScore = load_text(R"J({"instrument":{},"graph":{"output":"x","nodes":[{"id":"x","type":"SineSource"}]}})J");
    CHECK(emptyScore.score == "[]" && noScore.score.empty() && !(emptyScore == noScore));
    CHECK(J(emptyScore)["score"] == J::array() && !J(noScore).contains("score"));
    CHECK(!noScore.seconds && noScore.sampleRate == 48000 && noScore.comment.empty());
    CHECK(!J(noScore).contains("seconds") && !J(noScore).contains("comment") && !J(noScore).contains("layout"));
    CHECK(!J(noScore)["graph"]["nodes"][0].contains("comment") && !J(noScore)["graph"]["nodes"][0]["params"].contains("seed"));
    CHECK(noScore.instrument == std::optional<InstrumentBlock>(InstrumentBlock{}));
    CHECK(J(noScore)["instrument"] == J::parse(R"J({"polyphony":4,"volume":1.0,"sustaining":false,"onsets":[],"glideMs":15.0})J"));
    const Patch block = load_text(R"J({"instrument":{"polyphony":2,"volume":0.5,"sustaining":true,"onsets":["tongue"],"glideMs":20},"graph":{"output":"x","nodes":[{"id":"x","type":"SineSource"}]}})J");
    CHECK(block.instrument == std::optional<InstrumentBlock>(InstrumentBlock{2, 0.5f, true, {"tongue"}, 20.0f}));
    CHECK(load_text("{}") == Patch{});
    CHECK(J(Patch{}) == J::parse(R"J({"sampleRate":48000,"graph":{"output":"","nodes":[]}})J"));
}

// The layout and the fragment: each read, written and read back the same; a
// fragment's feed may name a node it does not hold.
void run_codec_layout_fragment_tests() {
    using namespace patch_codec;
    std::printf("the codec: the layout and the fragment\n");
    const std::string layout = R"J({"positions":{"x":[1.5,-2.0],"y":[0.0,3.25]},"output":[10.0,20.0],"panning":[-5.0,6.0],)J"
                               R"J("groups":[{"name":"Voice","members":["x","Note"],"pos":[7.0,8.0]}],)J"
                               R"J("faces":[{"label":"Note","fields":{"frequency":"x"},"pos":[-1.0,1.0]},{"label":"Bare","fields":{}}]})J";
    const Patch p = load_text(R"J({"instrument":{},"graph":{"output":"x","nodes":[{"id":"x","type":"SineSource"},{"id":"y","type":"SawSource"}]},"layout":)J" +
                              layout + "}");
    CHECK(p.layout.positions.at("x") == (Vec2{1.5f, -2.0f}) && p.layout.outputPos == std::optional<Vec2>(Vec2{10, 20}));
    CHECK(p.layout.panning == std::optional<Vec2>(Vec2{-5, 6}) && p.layout.groups.size() == 1 && p.layout.faces.size() == 2);
    CHECK(p.layout.faces[0].pos == std::optional<Vec2>(Vec2{-1, 1}) && !p.layout.faces[1].pos);
    CHECK(J(p)["layout"] == J::parse(layout));
    Patch back;
    from_json(J(p), back);
    CHECK(back == p);
    Layout l;
    from_json(J::parse(layout), l);
    CHECK(l == p.layout && J(l) == J::parse(layout));
    CHECK(J(Layout{}) == J::object());
    const std::string fragment = R"J({"nodes":[{"id":"f","type":"SVFSource","params":{"source":{"ref":"elsewhere"},"cutoffFreq":{"tap":"g"}}},)J"
                                 R"J({"id":"g","type":"SineSource"}],"positions":{"f":[1.0,2.0]},)J"
                                 R"J("groups":[{"name":"G","members":["f","g"],"pos":[0.0,0.0]}],"faces":[{"label":"N","fields":{"frequency":"g"}}]})J";
    Fragment fr;
    from_json(J::parse(fragment), fr);
    CHECK(fr.nodes.size() == 2 && fr.nodes[0].wires.at("source") == std::vector<Wire>({Wire{"elsewhere", false}}));
    CHECK(fr.positions.at("f") == (Vec2{1, 2}) && fr.groups.size() == 1 && fr.faces.size() == 1);
    Fragment frBack;
    from_json(J(fr), frBack);
    CHECK(frBack == fr);
    CHECK(J(Fragment{}) == J::parse(R"J({"nodes":[]})J"));
}
