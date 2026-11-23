#pragma once
// Helpers the render tests share, and the suites one file runs from another.
#include "mforce/testing/check.h"
#include "patch/tests/fixtures.h"
#include "render/tests/fixtures.h"
#include "mforce/contract/performance.h"
#include "mforce/core/component.h"
#include "mforce/core/dsp_value_source.h"
#include "mforce/core/pins.h"
#include "mforce/patch/envelope_preset.h"
#include "mforce/patch/patch.h"
#include "mforce/patch/table_rule.h"
#include "mforce/patch/type_declarations.h"
#include "mforce/render/build_instrument.h"
#include "mforce/render/instrument.h"
#include "mforce/render/pitched_instrument.h"
#include "mforce/render/voice.h"
#include "mforce/source/additive/formant_peaks.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <bit>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <fstream>
#include <functional>
#include <initializer_list>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

using namespace mforce;


inline bool same_bits(float a, float b) {
    return std::bit_cast<uint32_t>(a) == std::bit_cast<uint32_t>(b);
}

using CurveKnots = std::vector<std::pair<float, float>>;
namespace patch_door {
inline bool same_samples(const std::vector<float>& a, const std::vector<float>& b) {
    return a.size() == b.size() && (a.empty() || std::memcmp(a.data(), b.data(), a.size() * sizeof(float)) == 0);
}
// The first index where two renders differ; their common length when one is a prefix of the other.
inline size_t first_difference(const std::vector<float>& a, const std::vector<float>& b) {
    size_t i = 0;
    while (i < a.size() && i < b.size() && std::memcmp(&a[i], &b[i], sizeof(float)) == 0) ++i;
    return i;
}
inline std::string difference(const std::vector<float>& a, const std::vector<float>& b) {
    const size_t i = first_difference(a, b);
    char text[160];
    std::snprintf(text, sizeof text, "first at sample %zu of %zu and %zu: %.9g %.9g", i, a.size(), b.size(),
                  i < a.size() ? double(a[i]) : 0.0, i < b.size() ? double(b[i]) : 0.0);
    return text;
}
}  // namespace patch_door

// An instrument as the tests hold it: the build, the patch's rate and what
// the build had to say.
struct LoadedInstrument {
    std::unique_ptr<PitchedInstrument> instrument;
    int sampleRate{48000};
    std::vector<std::string> warnings;
};

// The instrument `json` describes, read through the codec and built with at
// least `minPolyphony` voices.
inline LoadedInstrument load_inst(const std::string& json, int minPolyphony = 0) {
    const mforce::Patch patch = patch_door::read_text(json);
    LoadedInstrument loaded;
    loaded.instrument = build_instrument(patch, minPolyphony, &loaded.warnings);
    loaded.sampleRate = patch.sampleRate;
    return loaded;
}
// Whether a refusal says `sentence`: the module's refusal begins with the
// path to what it refuses, then the sentence. An empty sentence wants none.
inline bool refusal_says(const std::string& refusal, const std::string& sentence) {
    return sentence.empty() ? refusal.empty() : refusal.ends_with(sentence);
}

// Load a baseline patch with its score block stripped (the tests schedule
// their own notes). Run engine_tests from the repo root.
inline LoadedInstrument load_scoreless(const char* path) {
    std::ifstream f(path);
    std::stringstream ss; ss << f.rdbuf();
    std::string txt = ss.str();
    auto j = nlohmann::json::parse(txt);
    j.erase("score");
    return load_inst(j.dump());
}

// The voice's output node as a signal, found by the build's description.
inline std::shared_ptr<ValueSource> output_of(const Voice& v) {
    return std::dynamic_pointer_cast<ValueSource>(v.nodes().at(v.description().output));
}

// Plays `notes` on `inst` as one part: a held line among them ends when the
// list does.
inline void play_notes(Instrument& inst, std::vector<PerformedNote> notes) {
    inst.play(Performance{std::move(notes), {}});
}

// A minimal sustaining patch: percent-mode adsr (attack 5%, decay 10%,
// expand, release 25%) into a sine whose frequency is WIRED to the Note
// node. The wire matters: a value pushed once at Setup (a dynamic pin)
// would freeze a glide at its first value — only pulled frequency chains
// (what every taught wind patch uses) see the ramp. %s = the instrument
// block's glideMs.
inline std::string hold_patch_json(float glideMs) {
    char buf[1400];
    std::snprintf(buf, sizeof(buf), R"({
      "sampleRate": 48000,
      "instrument": { "polyphony": 2, "glideMs": %.1f },
      "graph": {
        "output": "sine1",
        "nodes": [
          { "id": "pf", "type": "PerformNode",
            "params": { "field": "frequency" } },
          { "id": "env1", "type": "Envelope",
            "params": {
              "startVal": [0.0, 1.0, 0.7, 0.7], "endVal": [1.0, 0.7, 0.7, 0.0],
              "power": [0.0, 0.0, 0.0, 0.0], "type": ["Linear", "Linear", "Linear", "Linear"],
              "percent": [0.05, 0.1, 0.0, 0.25], "minSec": [0.0, 0.0, 0.0, 0.0],
              "maxSec": [0.0, 0.0, 0.0, 0.0], "nominal": [0.0, 0.0, 0.0, 0.0] } },
          { "id": "sine1", "type": "SineSource",
            "params": { "frequency": { "ref": "pf" },
                        "amplitude": { "ref": "env1" }, "phase": 0.0 } }
        ]
      }
    })", glideMs);
    return std::string(buf);
}

// A note of the common shape: start and duration in seconds, pitch, velocity, hold.
inline PerformedNote note_at(float start, float duration, float noteNumber, float velocity = 0.8f,
                             bool hold = false) {
    return {.start = start, .duration = duration, .noteNumber = noteNumber, .velocity = velocity, .hold = hold};
}
namespace {
struct Counter final : Generator {
    int computes{0};
    float compute() override { return float(++computes); }
    const char* type_name() const override { return "Counter"; }
};
// Counts how often a pin's owner prepared and advanced it.
struct PinCounter final : mforce::Generator {
  int starts{0}, nexts{0}, rate{0}, ctxPrepares{0};
  float level{1.0f};
  void on_prepare(const mforce::RenderContext& c) override { rate = c.sampleRate; ++ctxPrepares; }
  void on_start(int) override { ++starts; }
  float compute() override { ++nexts; return level; }
  const char* type_name() const override { return "PinCounter"; }
};
}  // namespace

// A one-row FormantPeaks: the peak the split tests shape.
inline std::shared_ptr<FormantPeaks> make_one_peak(float hz, float width = 500.0f, float gain = 1.0f, float power = 2.0f) {
    auto pk = std::make_shared<FormantPeaks>();
    pk->set_array("frequency", {hz});
    pk->set_array("gain", {gain});
    pk->set_array("width", {width});
    pk->set_array("power", {power});
    return pk;
}

// The split is exact when a node that is prepared once and started per note
// renders the same bytes as one that is prepared and started before every
// note. Both arms throw or neither;
// a comparison of silence proves nothing, so the result says whether the
// render had any signal in it.
struct SplitResult { bool equal; bool threw; bool nonzero; };

inline SplitResult split_compare(const std::function<std::shared_ptr<ValueSource>()>& make,
                                 int frames = 2048, int notes = 3, int sampleRate = 48000) {
    RenderContext ctx{sampleRate};
    auto a = make(), b = make();
    std::vector<float> ra, rb;
    auto render = [&](ValueSource& s, std::vector<float>& out, bool prepareEachNote) {
        if (!prepareEachNote) s.prepare(ctx);
        for (int n = 0; n < notes; ++n) {
            if (prepareEachNote) s.prepare(ctx);
            s.start(frames);
            for (int i = 0; i < frames; ++i) out.push_back(s.next());
        }
    };
    bool threwA = false, threwB = false;
    try { render(*a, ra, true);  } catch (...) { threwA = true; }
    try { render(*b, rb, false); } catch (...) { threwB = true; }
    if (threwA || threwB) return {threwA == threwB, true, false};
    const bool equal = ra.size() == rb.size() &&
                       std::memcmp(ra.data(), rb.data(), ra.size() * sizeof(float)) == 0;
    const bool nonzero = std::any_of(rb.begin(), rb.end(), [](float v) { return v != 0.0f; });
    return {equal, false, nonzero};
}

inline bool split_equivalent(const std::function<std::shared_ptr<ValueSource>()>& make,
                             int frames = 2048, int notes = 3, int sampleRate = 48000) {
    return split_compare(make, frames, notes, sampleRate).equal;
}

// ---------------------------------------------------------------------------
// Core family: the rate arrives through prepare(ctx), the note through
// start(frames) (WaveSource, Envelope and presets, FM, Multi).
// ---------------------------------------------------------------------------
inline std::vector<float> render_samples(ValueSource& s, int n) {
    std::vector<float> out;
    out.reserve(size_t(n));
    for (int i = 0; i < n; ++i) out.push_back(s.next());
    return out;
}

inline bool same_bits(const std::vector<float>& a, const std::vector<float>& b) {
    return a.size() == b.size() &&
           std::memcmp(a.data(), b.data(), a.size() * sizeof(float)) == 0;
}

// A node used at one rate and then prepared at another renders what a node
// prepared at the second rate renders: nothing sized or derived from the
// first rate survives.
inline bool follows_rate(const std::function<std::shared_ptr<ValueSource>()>& make,
                         int first, int second) {
    auto used = make(), fresh = make();
    used->prepare(RenderContext{first});
    used->start(4096);
    render_samples(*used, 500);
    used->prepare(RenderContext{second});
    used->start(4096);
    fresh->prepare(RenderContext{second});
    fresh->start(4096);
    return same_bits(render_samples(*used, 4096), render_samples(*fresh, 4096));
}

// One note played on the instrument, its samples returned: what a listener
// gets. The instrument keeps its voices' state from note to note.
inline std::vector<float> render_one_live_note(PitchedInstrument& inst, float noteNumber) {
    inst.play(Performance{{{.start = 0.0f, .duration = 0.1f, .noteNumber = noteNumber, .velocity = 0.8f}}, {}});
    return inst.rendered().back().samples;
}
namespace enum_column {
// A one-node graph: `node` of `type` with `params`, beside a signal for the output.
inline std::string one_node(const char* type, const nlohmann::json& params) {
    nlohmann::json n = {{"id", "n"}, {"type", type}};
    if (!params.is_null()) n["params"] = params;
    nlohmann::json doc = {{"sampleRate", 48000}, {"instrument", {{"polyphony", 1}}},
                          {"graph", {{"output", "out"}, {"nodes", nlohmann::json::array({
                              nlohmann::json{{"id", "out"}, {"type", "SineSource"}}, n})}}}};
    return doc.dump();
}
// The node `n` of a loaded one-node graph, or null with the refusal printed.
inline std::shared_ptr<Component> load_node(const char* type, const nlohmann::json& params, LoadedInstrument& ip) {
    try { ip = load_inst(one_node(type, params)); }
    catch (const std::exception& e) { std::printf("  %s %s: %s\n", type, params.dump().c_str(), e.what()); return nullptr; }
    return ip.instrument->pool().voice(0).nodes().at("n");
}
inline float load_setting(const char* type, const char* key, const nlohmann::json& value) {
    LoadedInstrument ip;
    auto n = load_node(type, nlohmann::json{{key, value}}, ip);
    return n ? n->get_setting(key) : -1.0f;
}
}  // namespace enum_column
namespace three_kinds {
// The one-node instrument document as a plain sound: no instrument block.
inline std::string plain(const std::string& instrumentDoc) {
    auto doc = nlohmann::json::parse(instrumentDoc);
    doc.erase("instrument");
    doc["seconds"] = 0.05;
    return doc.dump();
}
// Loads `text` as a patch file built as a sound (file) or as an instrument.
inline void load(const std::string& text, bool file) {
    if (!file) { (void)load_inst(text); return; }
    ScratchPatch patch("renders/scratch/engine_tests_three_kinds.json", text);
    (void)load_sound(patch.path);
}
// Whether the load of `text` is refused with every one of `needles`.
inline bool refused_naming(const std::string& text, bool file, std::initializer_list<const char*> needles) {
    try { load(text, file); }
    catch (const std::exception& e) {
        const std::string msg = e.what();
        bool all = true;
        for (const char* n : needles) all = all && msg.find(n) != std::string::npos;
        if (!all) std::printf("  refused for another reason: %s\n", msg.c_str());
        return all;
    }
    std::printf("  loaded: %s\n", text.c_str());
    return false;
}
inline bool loads(const std::string& text, bool file) {
    try { load(text, file); return true; }
    catch (const std::exception& e) { std::printf("  load failed: %s\n", e.what()); return false; }
}
}  // namespace three_kinds
namespace list_twins {
// A voice graph of two sines, `a` at 220 Hz and `b` at 330, then `nodes`.
inline std::string voice_doc(const nlohmann::json& nodes) {
    nlohmann::json all = nlohmann::json::array({
        nlohmann::json{{"id", "a"}, {"type", "SineSource"}, {"params", {{"frequency", 220.0}}}},
        nlohmann::json{{"id", "b"}, {"type", "SineSource"}, {"params", {{"frequency", 330.0}}}}});
    for (const auto& n : nodes) all.push_back(n);
    return nlohmann::json{{"sampleRate", 48000}, {"instrument", {{"polyphony", 1}}},
                          {"graph", {{"output", "a"}, {"nodes", all}}}}.dump();
}
}  // namespace list_twins

void run_noise_split_tests();
void run_wavetable_split_tests();
void run_hybrid_ks_split_tests();
void run_additive_split_tests();
void run_physical_split_tests();
void run_filter_split_tests();
void run_vibrato_split_tests();
void run_combine_mod_split_tests();
void run_wired_split_tests();
void run_vibrato_late_setting_tests();
void run_host_split_tests();
