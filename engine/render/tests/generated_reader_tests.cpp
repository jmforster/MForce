#include "mforce/testing/check.h"
#include "test_support.h"
#include "patch/tests/fixtures.h"
#include "mforce/core/descriptors.h"
#include "mforce/core/dsp_value_source.h"
#include "mforce/core/pins.h"
#include "mforce/patch/envelope_preset.h"
#include "mforce/patch/patch.h"
#include "mforce/patch/patch_json.h"
#include "mforce/patch/type_declarations.h"
#include "mforce/render/build_sound.h"
#include "mforce/render/built_sound.h"
#include "mforce/render/live_player.h"
#include "mforce/render/ring_out.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <exception>
#include <map>
#include <string>
#include <utility>
#include <vector>

using namespace mforce;

namespace patch_door {
// A C4 of `seconds` on the live path: one note through a LivePlayer, filled
// in 256-frame buffers for the note and the fixed 0.4 s tail.
static std::vector<float> live_c4(const mforce::Patch& patch, float seconds = 0.25f) {
    LivePlayer player(patch, 1);
    (void)player.note_on({.start = 0.0f, .duration = seconds, .noteNumber = 60.0f, .velocity = 0.8f});
    std::vector<float> out, buf(256);
    const int buffers = int((seconds + RingOut::kTailSeconds) * float(player.sample_rate())) / 256 + 1;
    for (int k = 0; k < buffers; ++k) {
        player.fill(buf.data(), 256);
        out.insert(out.end(), buf.begin(), buf.end());
    }
    return out;
}
// A sound's first `frames` (all of it for -1) through its mixer, stereo interleaved.
static std::vector<float> render_sound(BuiltSound& s, int frames = -1) {
    const int n = frames < 0 ? s.frames : std::min(frames, s.frames);
    std::vector<float> buf(2 * size_t(n), 0.0f);
    s.mixer->render(buf.data(), n);
    return buf;
}
}  // namespace patch_door
namespace reader_twin {
using J = nlohmann::json;
using patch_door::difference;
using patch_door::live_c4;
using patch_door::read_text;
using patch_door::render_sound;
using patch_door::same_samples;
// The three texts per type: the sparse one; one whose arrays all hold rows
// and whose Params the text does not feed all hold a number off their
// defaults; one whose scalar settings all stand off their defaults.
enum class TwinText { Sparse, Rows, Settings };
// The settings and Params whose usual value makes the quarter-second render
// too slow or too large, and the value each takes instead, off its default:
// a Partials' count of partials the midpoint of its range; an ExpandRule's
// two sizes one each, since a rule makes (1 + 2 * count) to the power
// (1 + recurse) partials of every one its host has.
static const float* chosen_value(const std::string& type, const std::string& setting) {
    static const std::map<std::pair<std::string, std::string>, float> kChosen = {
        {{"FullPartials", "maxPartials"}, 100.0f},
        {{"SequencePartials", "maxPartials"}, 100.0f},
        {{"ExplicitPartials", "maxPartials"}, 100.0f},
        {{"ExpandRule", "count"}, 1.0f},
        {{"ExpandRule", "recurse"}, 1.0f},
    };
    const auto it = kChosen.find({type, setting});
    return it == kChosen.end() ? nullptr : &it->second;
}
// The first registered type whose slot `slot` of an owner takes; empty when none.
static std::string first_accepted(const TypeDeclarations::Input& slot) { return slot.accepts.empty() ? "" : slot.accepts.front(); }
static void add_node(J& nodes, const std::string& id, const std::string& type, bool note, int depth,
                     TwinText text = TwinText::Sparse);
// A node's pins: each signal input fed the saw, each slot but `skip` the first
// type it accepts (made by add_node, to a depth of three), `frequency` the
// note where the type has the pin and the patch has a note.
static J pins(J& nodes, const std::string& id, const TypeDeclarations& d, bool note, int depth, const std::string& skip = "") {
    J params = J::object();
    for (const auto& in : d.inputs) {
        if (in.name == skip) continue;
        if (!in.slot) { params[in.name] = in.list ? J::array({J{{"ref", "drive"}}}) : J{{"ref", "drive"}}; continue; }
        const std::string part = first_accepted(in);
        if (part.empty() || depth >= 3) continue;
        const std::string partId = id + "_" + in.name;
        add_node(nodes, partId, part, note, depth + 1);
        params[in.name] = in.list ? J::array({J{{"ref", partId}}}) : J{{"ref", partId}};
    }
    if (d.param("frequency") && note) params["frequency"] = J{{"ref", "pf"}};
    return params;
}
// Three rows of every array: its row default, or a labelled column's first label.
static void give_rows(J& params, const TypeDeclarations& d) {
    for (const auto& a : d.arrays)
        params[a.name] = a.labels.empty() ? J::array({a.rowDefault, a.rowDefault, a.rowDefault})
                                          : J::array({a.labels.front(), a.labels.front(), a.labels.front()});
}
// Every scalar setting off its default and inside its range: a number its
// maximum (or the value chosen_value gives it), an enum its last label, a
// bool the other value.
static void give_settings(J& params, const TypeDeclarations& d) {
    for (const auto& s : d.settings) {
        if (s.type == SettingType::String) continue;
        if (s.type == SettingType::Bool) params[s.name] = s.defaultValue == 0.0f;
        else if (!s.labels.empty()) params[s.name] = s.labels.back();
        else if (const float* chosen = chosen_value(d.type, s.name)) params[s.name] = *chosen;
        else params[s.name] = s.max;
    }
}
// Every declared Param the text does not feed a number off its default and
// inside its range (or the value chosen_value gives it): a quarter of the way
// to its maximum, or to its minimum where the default is the maximum. A
// Param whose range is one value keeps its default.
static void give_params(J& params, const TypeDeclarations& d) {
    for (const auto& p : d.params) {
        if (params.contains(p.name) || p.min == p.max) continue;
        if (const float* chosen = chosen_value(d.type, p.name)) params[p.name] = *chosen;
        else if (p.defaultValue == p.max) params[p.name] = p.defaultValue - (p.defaultValue - p.min) / 4.0f;
        else params[p.name] = p.defaultValue + (p.max - p.defaultValue) / 4.0f;
    }
}
static void add_node(J& nodes, const std::string& id, const std::string& type, bool note, int depth, TwinText text) {
    const TypeDeclarations* d = type_declarations(type);
    J params = pins(nodes, id, *d, note, depth);
    if (text == TwinText::Rows) {
        give_rows(params, *d);
        give_params(params, *d);
    }
    if (text == TwinText::Settings) give_settings(params, *d);
    nodes.push_back(J{{"id", id}, {"type", type}, {"params", params}});
}
// The first registered type with a slot that takes `part`, and that slot; empty when none.
static std::pair<std::string, std::string> host_of(const std::string& part) {
    for (const auto& t : declared_types())
        for (const auto& in : type_declarations(t)->inputs)
            if (in.slot && std::find(in.accepts.begin(), in.accepts.end(), part) != in.accepts.end()) return {t, in.name};
    return {};
}
// The text for `type`: an instrument whose output is the node `x` of that
// type, or the signal that hosts it in a slot (a part's host chain followed
// until a signal, each host's other slots filled too); a plain sound when the
// chain ends at the mixer. Empty, with `why` said, when no slot takes the part.
static std::string twin_text(const std::string& type, TwinText text, std::string& why) {
    std::vector<std::pair<std::string, std::string>> hosts;   // each host's type and the slot it holds the one before in
    std::string at = type;
    while (!type_declarations(at)->signal && at != "StereoMixer" && hosts.size() < 3) {
        const auto host = host_of(at);
        if (host.first.empty()) { why = "no registered type's slot takes a " + at; return {}; }
        hosts.push_back(host);
        at = host.first;
    }
    if (!type_declarations(at)->signal && at != "StereoMixer") { why = "no signal hosts it within three slots"; return {}; }
    const bool instrument = at != "StereoMixer";
    J nodes = J::array();
    if (instrument) nodes.push_back(J{{"id", "pf"}, {"type", "PerformNode"}, {"params", {{"field", "frequency"}}}});
    nodes.push_back(J{{"id", "drive"}, {"type", "SawSource"},
                      {"params", {{"amplitude", 0.3}, {"frequency", instrument ? J{{"ref", "pf"}} : J(261.6256)}}}});
    add_node(nodes, "x", type, instrument, 0, text);
    std::string inner = "x";
    for (size_t k = 0; k < hosts.size(); ++k) {
        const std::string id = "h" + std::to_string(k);
        const TypeDeclarations* d = type_declarations(hosts[k].first);
        J params = pins(nodes, id, *d, instrument, 1, hosts[k].second);
        params[hosts[k].second] = d->input(hosts[k].second)->list ? J::array({J{{"ref", inner}}}) : J{{"ref", inner}};
        nodes.push_back(J{{"id", id}, {"type", hosts[k].first}, {"params", params}});
        inner = id;
    }
    J doc{{"sampleRate", 48000}, {"graph", {{"output", inner}, {"nodes", nodes}}}};
    if (instrument) doc["instrument"] = J{{"polyphony", 1}};
    else doc["seconds"] = 0.25;
    return doc.dump();
}
// One render of `text` through the module: a C4 of an instrument, the mix of a sound.
static std::vector<float> twin_render(const std::string& text) {
    const Patch p = read_text(text);
    if (p.instrument) return live_c4(p);
    BuiltSound s = build_sound(p);
    return render_sound(s);
}
// What one text of one type gave: the module's refusal, and what the
// properties found (empty fields where they hold).
struct TwinResult {
    std::string unbuilt, refusal, sparseVsWritten, identity;
    bool audible{false};
    double seconds{0.0};
};
// The module's render of `text`, or its refusal.
static std::vector<float> render_or_refusal(const std::string& text, std::string& refusal) {
    try { return twin_render(text); } catch (const std::exception& e) { refusal = e.what(); }
    return {};
}
// The types whose text is refused by design: probes that refuse as built,
// and the probe whose refusal is a setting the settings text turns on.
static bool refused_by_design(const std::string& type, TwinText kind) {
    return type == "RefusingProbe" || (type == "CheckProbe" && kind == TwinText::Settings);
}
static TwinResult twin_one(const std::string& type, TwinText kind) {
    TwinResult r;
    const std::string text = twin_text(type, kind, r.unbuilt);
    if (text.empty()) return r;
    const auto start = std::chrono::steady_clock::now();
    const auto mod = render_or_refusal(text, r.refusal);
    r.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    if (!r.refusal.empty()) return r;
    r.audible = std::any_of(mod.begin(), mod.end(), [](float s) { return s != 0.0f; });
    const Patch p = read_text(text);
    J written;
    to_json(written, p);
    Patch back;
    from_json(written, back);
    if (!(back == p)) r.identity = "from_json(to_json(p)) differs from p";
    if (kind == TwinText::Settings) return r;
    const auto again = twin_render(written.dump());
    if (!same_samples(mod, again)) r.sparseVsWritten = difference(mod, again);
    return r;
}
// One text over every registered type: each finding printed and failed, a
// refused type listed with its sentence and failed unless refused by design,
// the counts printed.
static void twin_all(TwinText kind, const char* name) {
    int types = 0, built = 0, audible = 0;
    std::vector<std::string> silent;
    for (const auto& type : declared_types()) {
        ++types;
        const TwinResult r = twin_one(type, kind);
        const char* t = type.c_str();
        if (!r.unbuilt.empty()) std::printf("  %s %s: no text: %s\n", name, t, r.unbuilt.c_str());
        const bool refused = !r.refusal.empty();
        if (refused) std::printf("  %s %s: refused: \"%s\"\n", name, t, r.refusal.c_str());
        if (!r.sparseVsWritten.empty()) std::printf("  %s %s: the text and the written text differ, %s\n", name, t, r.sparseVsWritten.c_str());
        if (!r.identity.empty()) std::printf("  %s %s: %s\n", name, t, r.identity.c_str());
        if (r.seconds > 2.0) std::printf("  %s %s: took %.1f s\n", name, t, r.seconds);
        CHECK(refused == refused_by_design(type, kind) && r.sparseVsWritten.empty() && r.identity.empty());
        if (!r.unbuilt.empty() || refused) continue;
        ++built;
        if (r.audible) ++audible;
        else silent.push_back(type);
    }
    std::printf("  %s: %d types, %d built, %d render something, %zu render silence:", name, types, built, audible, silent.size());
    for (const auto& s : silent) std::printf(" %s", s.c_str());
    std::printf("\n");
    CHECK(types > 0);
}
}  // namespace reader_twin

// For every registered type, three texts the declarations table writes: the
// sparse one; one whose every array holds rows and whose every Param the
// text does not feed holds a number off its default; one whose every scalar
// setting stands off its default. Of each: (i) the module builds it (a probe
// refused by design aside) and renders a C4 of a quarter second, and (ii)
// from_json(to_json(p)) == p; of the first two also (iii) the module renders
// the text and the text of to_json(from_json(text)) the same.
void run_generated_reader_twin_tests() {
    using namespace reader_twin;
    std::printf("the generated twin: every registered type through the module, three texts\n");
    twin_all(TwinText::Sparse, "sparse");
    twin_all(TwinText::Rows, "rows");
    twin_all(TwinText::Settings, "settings");
}
