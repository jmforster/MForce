#include "mforce/render/patch_loader.h"
#include "mforce/core/source_registry.h"
#include "mforce/core/dsp_value_source.h"
#include "mforce/source/modulator/name_gate.h"
#include "mforce/source/modulator/perform_node.h"
#include "mforce/source/output/sound_channel.h"
#include "mforce/source/output/stereo_mixer.h"
#include "mforce/render/perform_source.h"
#include "mforce/source/envelope/envelope.h"
#include "mforce/source/envelope/envelope_json.h"
#include "mforce/render/instrument.h"
#include "mforce/render/embedded_score.h"
// Types still needed for special-case construction
#include "mforce/source/combiner/multiplex_source.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <nlohmann/json.hpp>
#include <fstream>
#include <functional>
#include <cstdio>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

using json = nlohmann::json;

namespace mforce {

static std::string slurp(const std::string& path)
{
    std::ifstream f(path, std::ios::binary);
    if (!f)
        throw std::runtime_error("Cannot open patch file: " + path);

    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// The adapter: a signal as a mono producer for a channel. It is its own pull
// loop: it owns the clock its graph computes on and completes the host's
// facts with it.
struct ValueSourceMono final : MonoSource {
    std::shared_ptr<ValueSource> src;
    SampleClock clock;
    RenderContext ctx;
    explicit ValueSourceMono(std::shared_ptr<ValueSource> s) : src(std::move(s)) {}
    void prepare(const RenderContext& c) override {
        ctx = c.with_clock(&clock);
        src->prepare(ctx);
    }
    void render(float* out, int frames) override {
        src->start(frames);
        for (int i = 0; i < frames; ++i) { clock.tick(); out[i] = src->next(); }
    }
};

// ---------------------------------------------------------------------------
// Param resolution: number -> ConstantSource, {"ref":"id"} -> lookup
// ---------------------------------------------------------------------------

// Tap edges (feedback_loop_design.md §3.2): {"tap": "<id>"} resolves to a
// TapSource and may point FORWARD in nodeOrder — that is its purpose
// (closing a feedback cycle) — so targets bind in a second pass at the end
// of build_graph, once every node exists. The collector is build-scoped via
// RAII rather than threaded through the ~40 resolve_param call sites; nested
// builds (Multiplex subgraphs) save/restore so each binds within its own
// graph. A tap never advances its source: it reads the source's memo.
struct TapBind { std::shared_ptr<TapSource> tap; std::string targetId; };
static thread_local std::vector<TapBind>* t_tapBinds = nullptr;
struct TapBindScope {
    std::vector<TapBind>* prev;
    explicit TapBindScope(std::vector<TapBind>* cur) : prev(t_tapBinds) {
        t_tapBinds = cur;
    }
    ~TapBindScope() { t_tapBinds = prev; }
};

// Resolve a JSON ref/number into a ValueSource. A source referenced by
// several pins is the same object on each: every consumer pulls it, and it
// computes once per tick (ValueSource::next()), so a shared envelope advances
// once per sample however many pins read it.
// A ref names any node, signal or part; the wiring door decides whether the
// pin takes it. A number is a ConstantSource, a tap a TapSource.
static std::shared_ptr<Component> resolve_param(
    const json& val,
    const std::unordered_map<std::string, std::shared_ptr<Component>>& graphNodes)
{
    if (val.is_number())
        return std::make_shared<ConstantSource>(val.get<float>());

    if (val.is_object() && val.contains("ref")) {
        std::string refId = val.at("ref").get<std::string>();
        auto it = graphNodes.find(refId);
        if (it == graphNodes.end())
            throw std::runtime_error("Unresolved node ref: " + refId);
        return it->second;
    }

    if (val.is_object() && val.contains("tap")) {
        if (!t_tapBinds)
            throw std::runtime_error(
                "{\"tap\":...} not supported in this position");
        auto tap = std::make_shared<TapSource>();
        t_tapBinds->push_back({tap, val.at("tap").get<std::string>()});
        return tap;
    }

    throw std::runtime_error(
        "Param must be a number, {\"ref\":\"...\"} or {\"tap\":\"...\"}");
}

// A signal where one is required — a constructor argument, a tap's target, a
// chain's root, a voice's output, a mixer gain. A part there is a rejected
// wire that says so.
static std::shared_ptr<ValueSource> as_signal(const std::shared_ptr<Component>& node,
                                              const std::string& where)
{
    auto signal = std::dynamic_pointer_cast<ValueSource>(node);
    if (!signal && node)
        throw std::runtime_error("'" + where + "' is a " + std::string(node->type_name())
                                 + ", a part, not a signal");
    return signal;
}

static std::shared_ptr<ValueSource> resolve_signal(
    const json& val,
    const std::unordered_map<std::string, std::shared_ptr<Component>>& graphNodes)
{
    const std::string where = (val.is_object() && val.contains("ref")) ? val.at("ref").get<std::string>()
                                                                        : std::string("param");
    return as_signal(resolve_param(val, graphNodes), where);
}

// ---------------------------------------------------------------------------
// Advance list (feedback_loop_design.md §3.3): a node consumed ONLY by tap
// edges is never reached by the root pull — the loop tail would be silently
// dead — so the voice pulls it once per sample after the root. Membership is
// decided from the file, where every edge is enumerable (a built graph hides
// multi-input entries and setter-held envelopes): every {"ref":id} and
// {"tap":id} across all nodes; result = ids with
// a tap consumer, no normal consumer, and != outputId, in nodeOrder. A tail's
// own forward inputs, if the root pull computed them this tick, are served
// from the memo, so the extra pull advances nothing twice.
// ---------------------------------------------------------------------------
static std::vector<std::string> collect_advance_ids(
    const std::unordered_map<std::string, json>& nodeMap,
    const std::vector<std::string>& nodeOrder,
    const std::string& outputId)
{
    std::unordered_set<std::string> normal, tapped;
    std::function<void(const json&)> scan = [&](const json& v) {
        if (v.is_object()) {
            if (v.size() == 1 && v.contains("ref") && v["ref"].is_string())
                normal.insert(v["ref"].get<std::string>());
            else if (v.size() == 1 && v.contains("tap") && v["tap"].is_string())
                tapped.insert(v["tap"].get<std::string>());
            else
                for (auto it = v.begin(); it != v.end(); ++it) scan(it.value());
        } else if (v.is_array()) {
            for (const auto& item : v) scan(item);
        }
    };
    for (const auto& id : nodeOrder) {
        const auto& node = nodeMap.at(id);
        if (node.contains("params")) scan(node["params"]);
    }
    std::vector<std::string> out;
    for (const auto& id : nodeOrder)
        if (tapped.count(id) && !normal.count(id) && id != outputId)
            out.push_back(id);
    return out;
}

// ---------------------------------------------------------------------------
// Graph building: creates every node from JSON — signals and parts.
// Extracted so it can be called once (normal mode) or N times (instrument voices).
// ---------------------------------------------------------------------------

struct GraphResult {
    std::unordered_map<std::string, std::shared_ptr<Component>> nodes;        // every node by id, signal or part
};

// Wire all connectable params generically via param_descriptors + input_descriptors + set_param.
// A wire the node will not take: a pin it does not have, or a part of the
// wrong kind for a slot. The builder knows the id; the node knows the pin.
[[noreturn]] static void reject_wire(const Component& node, const std::string& id,
                                     std::string_view pin, const Component& part)
{
    const bool isSignal = dynamic_cast<const ValueSource*>(&part) != nullptr;
    std::string takes;
    for (const auto& d : node.input_descriptors())
        if (d.slot && d.hint && pin == d.name) takes = "; '" + std::string(pin) + "' takes " + d.hint;
    throw std::runtime_error("node '" + id + "' (" + node.type_name() + "): '"
                             + std::string(pin) + "' does not accept a " + part.type_name()
                             + (isSignal ? "" : ", a part, not a signal") + takes);
}

std::string retired_key_refusal(std::string_view type, std::string_view key, RetiredAt at)
{
    // Each spelling the engine does not read: the node type (empty: any),
    // the key, where it sat, and what the converter rewrites it as.
    struct Retired { std::string_view type, key; RetiredAt at; std::string_view becomes; };
    static constexpr Retired kRetired[] = {
        {"Envelope",          "stages",     RetiredAt::Key,        "the eight stage columns"},
        {"CurveNode",         "knots",      RetiredAt::Key,        "the columns x and y"},
        {"CurveNode",         "exprKnots",  RetiredAt::Key,        "the columns exprX, exprForm, exprA and exprB"},
        {"PhasedValueSource", "stages",     RetiredAt::Key,        "sources and the columns percent, minSec, maxSec and gain"},
        {"BWLowpassFilter",   "cutoff",     RetiredAt::Key,        "cutoffFreq"},
        {"BWHighpassFilter",  "cutoff",     RetiredAt::Key,        "cutoffFreq"},
        {"SVFSource",         "cutoff",     RetiredAt::Key,        "cutoffFreq"},
        {"RepeatingSource",   "gap",        RetiredAt::Key,        "gapDuration"},
        {"MultiSource",       "weight",     RetiredAt::FeedKey,    "the array weights"},
        {"MultiSource",       "delay",      RetiredAt::FeedKey,    "the array delays"},
        {"FullPartials",      "expandRule", RetiredAt::InlineSlot, "an ExpandRule node wired by ref"},
        {"SequencePartials",  "expandRule", RetiredAt::InlineSlot, "an ExpandRule node wired by ref"},
        {"ExplicitPartials",  "expandRule", RetiredAt::InlineSlot, "an ExpandRule node wired by ref"},
        {"SoundChannel",      "inputs",     RetiredAt::NodeKey,    "the feed 'source' in params"},
        {"StereoMixer",       "inputs",     RetiredAt::NodeKey,    "the feeds 'channels' in params"},
        {"",                  "ui",         RetiredAt::TopKey,     "layout"},
        {"",                  "groups",     RetiredAt::TopKey,     "layout.groups"},
        {"",                  "_comment",   RetiredAt::TopKey,     "comment"},
        {"",                  "_provenance", RetiredAt::TopKey,    "comment"},
    };
    for (const auto& r : kRetired) {
        if ((!r.type.empty() && r.type != type) || r.key != key || r.at != at) continue;
        const char* where = at == RetiredAt::FeedKey    ? " on a feed"
                          : at == RetiredAt::InlineSlot ? " as an inline object"
                          : at == RetiredAt::NodeKey    ? " beside params"
                          : at == RetiredAt::TopKey     ? " at the top of the file"
                                                        : "";
        return "'" + std::string(key) + "'" + where
             + " is no longer read: python tools/convert_patch.py <patch> rewrites it as " + std::string(r.becomes);
    }
    return {};
}

// Every key of a node's params is one the engine reads: a descriptor of
// the node, or one of the keys the hand-written branches and configurators
// consume (the allowlist below, grown until the whole gate corpus loads
// clean). Any other key fails the load: a typo, or a patch newer than the
// engine, must not render silently downgraded. Runs once per node after
// whichever path built it.
static void reject_unknown_keys(const Component& src, const std::string& id,
                                const json& params)
{
    // Every key a hand-written branch or a configurator reads, anywhere in
    // the loader sources, plus the node/JSON structure keys: the union of
    // the list the unknown-key warning grew against the gate corpus and the
    // set tools/lint_patches.py scrapes from the sources. Global, not per
    // type, like the lint's: a spurious entry can only hide a rejection,
    // never invent one. The structure meter's tests fail when the scrape
    // gains a key this list lacks.
    static const std::unordered_set<std::string> kBranchKeys = {
        "attack", "attackCurve", "attackMax",
        "attackMin", "attackPower",
        "curve", "decay", "decayCurve", "decayLevel",
        "decayMax", "decayMin", "decayPower",
        "dynamicPins",
        "field", "formants", "frames", "frequency",
        "gains", "glideMs", "graph",
        "id", "instrument", "interp", "interpolate",
        "mode", "morph",
        "nodes",
        "onsets", "output", "paramMap",
        "polyphony",
        "powers", "powers2", "preset",
        "ref", "release", "releaseCurve", "releaseMax", "releaseMin",
        "releasePower", "reverse", "sampleRate", "score", "seconds",
        "seed", "segs", "segs2", "source",
        "sustain", "sustainLevel",
        "sustaining", "tap", "target", "timeMode",
        "type", "values", "values2", "vcurve", "volume",
    };
    auto known = [&](const std::string& k) {
        if (kBranchKeys.count(k)) return true;
        for (const auto& d : src.input_descriptors())
            if (k == d.name) return true;
        for (const auto& d : src.param_descriptors())
            if (k == d.name) return true;
        for (const auto& d : src.setting_descriptors())
            if (k == d.name) return true;
        for (const auto& d : src.array_descriptors())
            if (k == d.name) return true;
        return false;
    };
    // A retired key is refused by its own sentence first, naming the
    // converter; the unknown-key test alone would let one through that the
    // descriptors or kBranchKeys hold for another type.
    for (auto it = params.begin(); it != params.end(); ++it) {
        const std::string retired = retired_key_refusal(src.type_name(), it.key());
        if (!retired.empty())
            throw std::runtime_error("node '" + id + "' (" + src.type_name() + "): " + retired);
        if (!known(it.key()))
            throw std::runtime_error("node '" + id + "' (" + src.type_name()
                                     + "): unknown key '" + it.key() + "'");
    }
}

std::string ragged_table_refusal(const Component& node,
                                 const std::function<int(const char* column)>& length)
{
    const auto arrays = node.array_descriptors();
    for (size_t g = 0; g < arrays.size(); ++g) {
        if (!arrays[g].groupName) continue;
        const std::string_view group = arrays[g].groupName;
        bool first = true;                      // the group is checked at its first column
        for (size_t k = 0; k < g && first; ++k)
            first = !(arrays[k].groupName && group == arrays[k].groupName);
        if (!first) continue;
        std::string got;
        size_t columns = 0, present = 0;
        int firstLength = 0;
        bool oneLength = true;
        for (const auto& d : arrays) {
            if (!d.groupName || group != d.groupName) continue;
            ++columns;
            const int n = length(d.name);
            if (n >= 0) {
                if (present++ == 0) firstLength = n;
                else oneLength = oneLength && n == firstLength;
            }
            got += std::string(got.empty() ? "" : ", ") + d.name + " "
                 + (n >= 0 ? std::to_string(n) : std::string("absent"));
        }
        if (present != 0 && (present != columns || !oneLength))
            return "table '" + std::string(group) + "' takes its columns at one length, "
                   "all present or all absent (got " + got + ")";
    }
    return {};
}

// The table rule over a node's params: a key present but not an array counts
// as absent, as the array pass skips it. Runs for every node the generic
// wiring runs for.
static void reject_ragged_tables(const Component& src, const std::string& id,
                                 const json& params)
{
    const std::string refusal = ragged_table_refusal(src, [&](const char* column) {
        return params.contains(column) && params.at(column).is_array() ? int(params.at(column).size()) : -1;
    });
    if (!refusal.empty())
        throw std::runtime_error("node '" + id + "' (" + src.type_name() + "): " + refusal);
}

// An element of a column with labels: one of the labels, or a whole-number
// index into them. Anything else fails the load naming the node, the column
// and the value.
static float enum_column_element(const Component& src, const std::string& id,
                                 const ArrayDescriptor& desc, const json& item)
{
    int count = 0;
    while (desc.enum_labels[count]) ++count;
    int index = -1;
    if (item.is_string())
        index = enum_index(desc.enum_labels, item.get<std::string>());
    else if (item.is_number()) {
        const double v = item.get<double>();
        if (v >= 0.0 && v < double(count) && v == std::floor(v)) index = int(v);
    }
    if (index >= 0) return float(index);
    std::string labels;
    for (int k = 0; k < count; ++k) labels += std::string(k ? ", " : "") + desc.enum_labels[k];
    throw std::runtime_error("node '" + id + "' (" + src.type_name() + "): '" + desc.name
                             + "' holds " + item.dump() + ", which is not one of its labels ("
                             + labels + ") or an index into them");
}

// A string where a pin takes a value. The one legacy string form, the
// wavetable's evolution by name, is gone, and a string anywhere else was never
// read: the load fails naming the node, the pin and the string, and for
// `evolution` the tool that rewrites the old form as an evolution node.
static void refuse_string_pin(const Component& src, const std::string& id,
                              const char* pin, const std::string& value, bool slot)
{
    std::string msg = "node '" + id + "' (" + src.type_name() + "): '" + pin
                    + "' is the string \"" + value + "\"; "
                    + (slot ? "a slot takes {\"ref\": id}" : "a pin takes a number, {\"ref\": id} or {\"tap\": id}");
    if (std::strcmp(pin, "evolution") == 0)
        msg += "; the string-typed evolution is gone, python tools/convert_patch.py <patch> rewrites it as an evolution node";
    throw std::runtime_error(msg);
}

// A value a pin is about to resolve, checked for a feed's shape: an object
// holding 'ref' or 'tap' holds that one key and nothing else, and an object
// on a slot is a feed. A key beside a feed's own gets the table's sentence
// for the node's type where it has one, and an object on a slot the table's
// sentence for the pin; otherwise the general sentence. Any other value, an
// object on a signal pin included, goes on to resolve_param and its own
// sentence.
static void refuse_feed_shape(const Component& src, const std::string& id,
                              const char* pin, const json& v, bool slot)
{
    if (!v.is_object()) return;
    const std::string node = "node '" + id + "' (" + src.type_name() + "): ";
    const bool ref = v.contains("ref"), tap = v.contains("tap");
    if (ref && tap)
        throw std::runtime_error(node + "'" + pin + "': a feed holds 'ref' or 'tap', not both");
    if (ref || tap) {
        for (auto it = v.begin(); it != v.end(); ++it) {
            if (it.key() == "ref" || it.key() == "tap") continue;
            const std::string retired = retired_key_refusal(src.type_name(), it.key(), RetiredAt::FeedKey);
            throw std::runtime_error(node + "'" + pin + "': "
                                     + (retired.empty() ? "a feed holds 'ref' or 'tap' alone, not '" + it.key() + "'"
                                                        : retired));
        }
        return;
    }
    if (slot) {
        const std::string retired = retired_key_refusal(src.type_name(), pin, RetiredAt::InlineSlot);
        throw std::runtime_error(node + (retired.empty()
                                         ? "'" + std::string(pin) + "' holds an object where a slot takes {\"ref\": id}"
                                         : retired));
    }
}

static void wire_params_generic(
    Component& src,
    const std::string& id,
    const json& params,
    const std::unordered_map<std::string, std::shared_ptr<Component>>& graphNodes)
{
    for (const auto& desc : src.input_descriptors()) {
        if (!params.contains(desc.name)) continue;
        const auto& v = params.at(desc.name);
        if (v.is_string()) refuse_string_pin(src, id, desc.name, v.get<std::string>(), desc.slot);
        if (desc.multi && v.is_array()) {
            // Multi-input pin with array-of-refs JSON. Iterate and add each.
            src.clear_param(desc.name);
            for (const auto& item : v) {
                refuse_feed_shape(src, id, desc.name, item, desc.slot);
                auto part = resolve_param(item, graphNodes);
                if (!src.add_param(desc.name, part)) reject_wire(src, id, desc.name, *part);
            }
        } else {
            refuse_feed_shape(src, id, desc.name, v, desc.slot);
            auto part = resolve_param(v, graphNodes);
            if (!src.set_param(desc.name, part)) reject_wire(src, id, desc.name, *part);
        }
    }
    for (const auto& desc : src.param_descriptors()) {
        if (!params.contains(desc.name)) continue;
        if (params.at(desc.name).is_string()) refuse_string_pin(src, id, desc.name, params.at(desc.name).get<std::string>(), false);
        refuse_feed_shape(src, id, desc.name, params.at(desc.name), false);
        auto part = resolve_param(params.at(desc.name), graphNodes);
        if (!src.set_param(desc.name, part)) reject_wire(src, id, desc.name, *part);
    }
    // Settings, applied if present in JSON: a string setting takes a string
    // and nothing else; an int, float or bool takes a number, a bool, or an
    // enum label.
    for (const auto& desc : src.setting_descriptors()) {
        if (!params.contains(desc.name)) continue;
        const auto& v = params.at(desc.name);
        if (desc.type == SettingType::String) {
            if (!v.is_string())
                throw std::runtime_error("node '" + id + "' (" + src.type_name() + "): '" + desc.name
                                         + "' takes a string, not " + v.dump());
            src.set_string(desc.name, v.get<std::string>());
            continue;
        }
        float fv = 0.0f;
        if      (v.is_boolean()) fv = v.get<bool>() ? 1.0f : 0.0f;
        else if (v.is_number())  fv = v.get<float>();
        else if (v.is_string() && desc.enum_labels) {
            // An enum setting may be given as its label ("mode": "Highpass"),
            // matched ignoring case; a label that matches none keeps the
            // default, with a line on stderr.
            const std::string s = v.get<std::string>();
            const int li = enum_index(desc.enum_labels, s);
            if (li >= 0) fv = float(li);
            else {
                std::fprintf(stderr,
                    "[load] %s.%s: enum string '%s' matches no label; "
                    "keeping default\n", src.type_name(), desc.name, s.c_str());
                continue;
            }
        }
        else continue;
        src.set_setting(desc.name, fv);
    }
    // User-editable float arrays — ExplicitPartials multipliers, spectrum gains, etc.
    // A column with labels holds indices, given in the file as labels or as
    // indices.
    for (const auto& desc : src.array_descriptors()) {
        if (!params.contains(desc.name)) continue;
        const auto& v = params.at(desc.name);
        if (!v.is_array()) continue;
        if (!desc.enum_labels) {
            src.set_array(desc.name, v.get<std::vector<float>>());
            continue;
        }
        std::vector<float> indices;
        indices.reserve(v.size());
        for (const auto& item : v) indices.push_back(enum_column_element(src, id, desc, item));
        src.set_array(desc.name, std::move(indices));
    }
    reject_ragged_tables(src, id, params);
}

// The note field a PerformNode's `field` names, as an index into
// PerformNode::kFieldLabels: a label matched ignoring ASCII case, or a whole
// number inside the labels; absent, the first. Anything else fails the load,
// listing the labels.
static int perform_field(const std::string& id, const json* params)
{
    if (!params || !params->contains("field")) return 0;
    const char* const* labels = PerformNode::kFieldLabels;
    const json& v = params->at("field");
    int count = 0;
    while (labels[count]) ++count;
    int index = -1;
    if (v.is_string())
        index = enum_index(labels, v.get<std::string>());
    else if (v.is_number()) {
        const double d = v.get<double>();
        if (d >= 0.0 && d < double(count) && d == std::floor(d)) index = int(d);
    }
    if (index >= 0) return index;
    std::string expected;
    for (int k = 0; k < count; ++k) expected += std::string(k ? "|" : "") + labels[k];
    throw std::runtime_error("PerformNode '" + id + "': unknown field " + v.dump()
                             + " (expected " + expected + ")");
}

// The labels and the adapter's fields name the six in one order: the index
// perform_field reads is the PerformOut::Field the voice made the adapter for.
static_assert(std::string_view(PerformNode::kFieldLabels[int(PerformOut::Field::Frequency)]) == "frequency");
static_assert(std::string_view(PerformNode::kFieldLabels[int(PerformOut::Field::Velocity)]) == "velocity");
static_assert(std::string_view(PerformNode::kFieldLabels[int(PerformOut::Field::Wheel)]) == "wheel");
static_assert(std::string_view(PerformNode::kFieldLabels[int(PerformOut::Field::Pressure)]) == "pressure");
static_assert(std::string_view(PerformNode::kFieldLabels[int(PerformOut::Field::Duration)]) == "duration");
static_assert(std::string_view(PerformNode::kFieldLabels[int(PerformOut::Field::Onset)]) == "onset");

// Per-voice performance adapters, handed down so PerformNode instances all
// resolve to the SAME objects (pin_model_design.md; perform_source_design.md
// §2.3: "All instances render the same outputs"). Null in NodeGraph/standalone
// contexts, where a PerformNode is an authoring error rather than a silent
// constant.
//
// Holds shared_ptrs, NOT raw references to the voice's members, because the
// Multiplex instance-builder closure captures this and outlives the loader:
// MultiplexSource::rebuild_() runs from prepare(), and mark_dirty() can
// retrigger it after a UI edit. A pointer into the voice-loop stack frame
// would dangle.
struct PerformContext {
    std::shared_ptr<ValueSource> freqOut, velOut, wheelOut, pressOut, durOut,
                                 onsetOut;
};

// A NameGate's `name` against the instrument's onsets[] vocabulary (spec
// 2026-09-20-note-onsets-v2 §6): ids are 1-based in declaration order. A
// name outside it, or any name where there is no vocabulary, never fires,
// and the load says so once per name.
static void resolve_name_gate(NameGate& gate, const std::string& id,
                              const std::vector<std::string>* onsets)
{
    const std::string name = gate.get_string("name");
    if (onsets)
        for (size_t k = 0; k < onsets->size(); ++k)
            if ((*onsets)[k] == name) {
                gate.targetId = float(k + 1);
                break;
            }
    if (!name.empty() && gate.targetId < 0.0f) {
        static std::unordered_set<std::string> warnedGates;
        if (warnedGates.insert(name).second)
            std::fprintf(stderr, "[load] NameGate '%s': name "
                "'%s' is not in the instrument's onsets[] "
                "vocabulary — it will never fire\n",
                id.c_str(), name.c_str());
    }
}

// Forward declarations for subgraph extraction / rebuild helpers (defined
// after build_graph since build_subgraph_with_seed_perturbation calls it).
static json extract_subgraph_json(
    const std::unordered_map<std::string, json>& nodeMap,
    const std::string& rootId);
// Returns {root source, graphNodes by id}: the map holds the built clone's
// nodes by id, and MultiplexSource keeps it per instance.
static std::pair<std::shared_ptr<ValueSource>,
                 std::unordered_map<std::string, std::shared_ptr<Component>>>
build_subgraph_with_seed_perturbation(
    const std::string& subtreeJsonStr,
    uint32_t seedPerturbation,
    const PerformContext* perf);

static GraphResult build_graph(
    const std::unordered_map<std::string, json>& nodeMap,
    const std::vector<std::string>& nodeOrder,
    const PerformContext* perf = nullptr,
    // Instrument onset vocabulary (spec 2026-09-20-note-onsets-v2 §6/§8)
    // for NameGate name→id resolution. Null on non-instrument paths
    // (and inside Multiplex subgraph rebuilds): an authored NameGate then
    // warns loudly instead of matching.
    const std::vector<std::string>* onsets = nullptr)
{
    // Lazy init registry
    static bool registered = false;
    if (!registered) { register_all_sources(); registered = true; }

    GraphResult g;
    auto& graphNodes   = g.nodes;

    // Tap edges resolve to placeholders during the node loop; bound below
    // once every node exists (they may point forward in nodeOrder).
    std::vector<TapBind> tapBinds;
    TapBindScope tapScope(&tapBinds);

    auto& reg = SourceRegistry::instance();

    // Resolve function for configurators
    ResolveParamFn resolveFn = [&](const json& val) {
        return resolve_signal(val, graphNodes);
    };

    for (const auto& id : nodeOrder) {
        const auto& node = nodeMap.at(id);
        std::string type = node.at("type").get<std::string>();
        const json* pp = node.contains("params") ? &node["params"] : nullptr;

        // A key of the node object the table retires fails the load before
        // the node is built.
        for (auto it = node.begin(); it != node.end(); ++it) {
            const std::string retired = retired_key_refusal(type, it.key(), RetiredAt::NodeKey);
            if (!retired.empty())
                throw std::runtime_error("node '" + id + "' (" + type + "): " + retired);
        }

        // ---- Extract seed if present ----
        std::optional<uint32_t> seed;
        if (pp && pp->contains("seed"))
            seed = static_cast<uint32_t>((*pp)["seed"].get<int>());

        // =================================================================
        // Types the loader builds or fills itself
        // =================================================================

        if (type == "Envelope") {
            if (!pp) throw std::runtime_error("Envelope requires params");
            const auto& p = *pp;
            std::shared_ptr<Envelope> env;

            // Two forms, at most one per node: the stage columns (any array
            // of a default Envelope's group "stages"), or a preset (also what
            // a node with neither gets).
            const char* column = nullptr;
            {
                const Envelope probe;
                for (const auto& d : probe.array_descriptors())
                    if (d.groupName && std::string_view(d.groupName) == "stages" && p.contains(d.name)) {
                        column = d.name;
                        break;
                    }
            }
            if (column && p.contains("preset"))
                throw std::runtime_error("node '" + id + "' (Envelope): holds both 'preset' and '" + column
                                         + "'; an Envelope takes one form: the stage columns or a preset");

            if (column) {
                // The stage columns: the generic array pass below fills them,
                // and its table rule refuses a node that lacks one.
                env = std::make_shared<Envelope>();
            } else {
                // Shared preset dispatch (envelope_json.h) — the UI graph
                // loader calls the same function, so the two cannot drift.
                env = envelope_from_preset_json(p);
            }
            // An authored or multiplex-injected seed, in either form.
            if (p.contains("seed") && p["seed"].is_number())
                env->set_seed(uint32_t(p["seed"].get<int64_t>()));
            graphNodes[id] = env;
            // The pins (minValue, maxValue, trigger), the settings (timeMode
            // read as its label) and the stage columns, as for any node.
            wire_params_generic(*env, id, p, graphNodes);
        }
        else if (type == "PerformNode") {
            // Leaf: resolves to the voice's shared PerformOut adapter. Several
            // PerformNode nodes naming the same field are the SAME object by
            // design — the pin model's "all instances render the same outputs".
            if (!perf)
                throw std::runtime_error("PerformNode '" + id + "': no voice "
                    "context (PerformNode is instrument/patch mode only)");
            // In PerformOut::Field's order, which the static_asserts above
            // tie to the labels.
            const std::shared_ptr<ValueSource> fields[] = {perf->freqOut, perf->velOut, perf->wheelOut,
                                                           perf->pressOut, perf->durOut, perf->onsetOut};
            graphNodes[id] = fields[perform_field(id, pp)];
        }
        else if (type == "NameGate") {
            // Onset-name match: the generic pass fills `name`, then the
            // name resolves against the instrument's vocabulary.
            auto ng = std::make_shared<NameGate>();
            if (pp) wire_params_generic(*ng, id, *pp, graphNodes);
            resolve_name_gate(*ng, id, onsets);
            graphNodes[id] = ng;
        }
        // ---- MultiplexSource: fan template subgraph into N instances ----
        else if (type == "MultiplexSource") {
            auto mux = std::make_shared<MultiplexSource>();
            if (pp) {
                // Apply "count" setting via generic path.
                wire_params_generic(*mux, id, *pp, graphNodes);

                // Resolve the "source" input to its template root node id.
                if (pp->contains("source") && (*pp)["source"].is_object()
                    && (*pp)["source"].contains("ref")) {
                    std::string tmplRootId = (*pp)["source"]["ref"].get<std::string>();

                    // Extract + serialize the template subtree.
                    json subtree = extract_subgraph_json(nodeMap, tmplRootId);
                    std::string subtreeStr = subtree.dump();

                    // Capture base seed from the root node's params (0 if none).
                    uint32_t baseSeed = 0;
                    if (nodeMap.count(tmplRootId)) {
                        const auto& rootNode = nodeMap.at(tmplRootId);
                        if (rootNode.contains("params")
                            && rootNode["params"].contains("seed")
                            && rootNode["params"]["seed"].is_number()) {
                            baseSeed = uint32_t(rootNode["params"]["seed"].get<int64_t>());
                        }
                    }

                    // Closure captures subtree + seed by value, outlives loader stack.
                    // The PerformContext is copied in BY VALUE for the same
                    // reason: rebuild_() runs from prepare(), and mark_dirty()
                    // can retrigger it after a UI edit, so a pointer to the
                    // caller's context would dangle. The copy holds shared_ptrs
                    // to the very same adapters, so clones still share the
                    // voice's one PerformSource.
                    const bool hasPerf = (perf != nullptr);
                    PerformContext perfCopy = hasPerf ? *perf : PerformContext{};
                    MultiplexSource::InstanceBuilder builder =
                        [subtreeStr, baseSeed, hasPerf, perfCopy](int instanceIdx) {
                            uint32_t perturbation =
                                baseSeed ^ (uint32_t(instanceIdx) * 0x9E3779B9u);
                            return build_subgraph_with_seed_perturbation(
                                subtreeStr, perturbation,
                                hasPerf ? &perfCopy : nullptr);
                        };

                    mux->set_template(subtreeStr, baseSeed, std::move(builder));
                }
            }
            graphNodes[id] = mux;
        }
        // =================================================================
        // Generic path: registry create + set_param + optional configurator
        // =================================================================
        else if (reg.has(type)) {
            auto src = reg.create(type, seed);
            if (pp) {
                wire_params_generic(*src, id, *pp, graphNodes);
                auto* configurator = reg.get_configurator(type);
                if (configurator) {
                    try { (*configurator)(*src, *pp, resolveFn); }
                    catch (const std::exception& e) {   // a configurator knows its keys, not its node; json's own errors included
                        throw std::runtime_error("node '" + id + "' (" + type + "): " + e.what());
                    }
                }
            }
            graphNodes[id] = src;
        }
        else {
            std::string msg = "Unknown node type: " + type;
            if (const char* hint = retired_type_hint(type)) msg += std::string("; ") + hint;
            throw std::runtime_error(msg);
        }

        // Whichever path built the node, every key it was given is one the
        // engine read; an unknown key fails the load.
        if (pp && graphNodes.count(id)) reject_unknown_keys(*graphNodes[id], id, *pp);
        // Then the node checks itself, filled: a refusal fails the build
        // naming the node, and each warning is printed.
        if (auto it = graphNodes.find(id); it != graphNodes.end() && it->second) {
            const Component& built = *it->second;
            const std::string problem = built.validate();
            if (!problem.empty())
                throw std::runtime_error("node '" + id + "' (" + built.type_name() + "): " + problem);
            for (const auto& text : built.warnings())
                std::fprintf(stderr, "[load] %s (%s): %s\n", id.c_str(), built.type_name(), text.c_str());
        }
    } // end for each node

    // Pass 2: bind tap targets (feedback_loop_design.md §3.2).
    for (auto& tb : tapBinds) {
        auto it = graphNodes.find(tb.targetId);
        if (it == graphNodes.end())
            throw std::runtime_error("Unresolved tap target: " + tb.targetId);
        tb.tap->source = as_signal(it->second, tb.targetId);
    }

    return g;
}


// ---------------------------------------------------------------------------
// Subgraph extraction + rebuild with seed perturbation.
// Used by MultiplexSource to fan out one template into N independent
// instances without a clone() interface on every node.
// ---------------------------------------------------------------------------

// Walk the node map starting from rootId; collect all transitively-
// referenced node ids in dependency order. Returns a self-contained subtree:
//   { "nodes": [...], "output": "<rootId>" }
// where "nodes" is ordered so every ref appears before its referent
// (matches build_graph's expectation).
static json extract_subgraph_json(
    const std::unordered_map<std::string, json>& nodeMap,
    const std::string& rootId)
{
    std::unordered_set<std::string> visited;
    std::vector<std::string> order;

    std::function<void(const std::string&)> walk = [&](const std::string& id) {
        if (visited.count(id)) return;
        if (!nodeMap.count(id)) return;
        visited.insert(id);
        const auto& node = nodeMap.at(id);
        if (node.contains("params")) {
            std::function<void(const json&)> scanRefs = [&](const json& v) {
                if (v.is_object()) {
                    if (v.size() == 1 && v.contains("ref") && v["ref"].is_string())
                        walk(v["ref"].get<std::string>());
                    else
                        for (auto it = v.begin(); it != v.end(); ++it) scanRefs(it.value());
                } else if (v.is_array()) {
                    for (const auto& item : v) scanRefs(item);
                }
            };
            scanRefs(node["params"]);
        }
        order.push_back(id);
    };

    walk(rootId);

    json nodes = json::array();
    for (const auto& id : order) nodes.push_back(nodeMap.at(id));

    json subtree;
    subtree["nodes"] = nodes;
    subtree["output"] = rootId;
    return subtree;
}

// Build a fresh root source from a subtree JSON string, XOR'ing every
// node's seed param by seedPerturbation so N instances diverge
// deterministically from a shared base seed. Returns {root, graphNodes}:
// the map holds the clone's nodes by id, and MultiplexSource keeps it per
// instance.
static std::pair<std::shared_ptr<ValueSource>,
                 std::unordered_map<std::string, std::shared_ptr<Component>>>
build_subgraph_with_seed_perturbation(
    const std::string& subtreeJsonStr,
    uint32_t seedPerturbation,
    const PerformContext* perf)
{
    json subtree = json::parse(subtreeJsonStr);

    std::unordered_map<std::string, json> nodeMap;
    std::vector<std::string> nodeOrder;
    for (auto& jnode : subtree["nodes"]) {
        json perturbed = jnode;
        if (!perturbed.contains("params")) perturbed["params"] = json::object();
        auto& params = perturbed["params"];
        if (params.contains("seed") && params["seed"].is_number()) {
            uint32_t s = uint32_t(params["seed"].get<int64_t>());
            params["seed"] = int64_t(s ^ seedPerturbation);
        } else {
            // Inject a perturbed seed so factory default is overridden.
            params["seed"] = int64_t(seedPerturbation);
        }
        std::string id = perturbed["id"].get<std::string>();
        nodeMap[id] = perturbed;
        nodeOrder.push_back(id);
    }

    auto g = build_graph(nodeMap, nodeOrder, perf);

    std::string outputId = subtree["output"].get<std::string>();
    auto it = g.nodes.find(outputId);
    if (it == g.nodes.end())
        throw std::runtime_error("build_subgraph: output '" + outputId + "' not found");
    return {as_signal(it->second, outputId), std::move(g.nodes)};
}

// ---------------------------------------------------------------------------
// Whether a node follows a moving frequency: a signal answers for itself; a
// part has no frequency pin that a bend could leave inert.
static bool tracks_frequency_live(const Component& node)
{
    auto* signal = dynamic_cast<const ValueSource*>(&node);
    return !signal || signal->tracks_frequency_live();
}

// ---------------------------------------------------------------------------
// Wiring-format bindings (pin_model_design.md §3, §11). The generic param pass
// already wires refs on ordinary (FIXED) pins; this pass handles DYNAMIC pins:
// a node's "dynamicPins" object, naming the settings this patch drives per
// note.
//
// Settings rebuild internal state and have no pointer to pull, so the chain is
// evaluated ONCE per note and pushed via set_setting. They live outside
// "params" because node CONSTRUCTION reads params before any descriptor is
// known — envelope_from_preset_json does p.value("sustainLevel", 0.7f), and
// nlohmann's value() throws on an object where it wants a number.
// ---------------------------------------------------------------------------
static void bind_wiring(const std::unordered_map<std::string, json>& nodeMap,
                        const std::vector<std::string>& nodeOrder,
                        const GraphResult& g,
                        PitchedInstrument::VoiceGraph& vg)
{
    // P3 (T2, plan_perform_source_p3.md): the bend-graft membership walk that
    // lived here is retired — freqOut itself articulates the bend now, so no
    // swap targets exist. What survives is its reachability question, relaxed
    // (curves no longer disqualify — bend flows through them as of P3), in
    // service of making the 26a hole LOUD: a node that reads its frequency
    // pin ONCE at note start (KSPianoString, init_note) is silently inert
    // under bend articulation. Warn at load, by name, so a bend curve on a
    // piano patch stops being a number nobody reads without anyone knowing.
    std::unordered_map<std::string, int> memo;   // 1 = reaches, 0 = no
    std::function<bool(const std::string&)> reaches_freq =
        [&](const std::string& nid) -> bool {
            auto m = memo.find(nid);
            if (m != memo.end()) return m->second == 1;
            memo[nid] = 0;                        // cycle guard: assume no
            auto it = nodeMap.find(nid);
            if (it == nodeMap.end()) return false;
            const auto& n = it->second;
            const std::string t = n.at("type").get<std::string>();
            if (t == "PerformNode") {
                const bool isFreq =
                    perform_field(nid, n.contains("params") ? &n["params"] : nullptr)
                        == int(PerformOut::Field::Frequency);
                memo[nid] = isFreq ? 1 : 0;
                return isFreq;
            }
            if (!n.contains("params")) return false;
            for (const auto& [k, v] : n["params"].items()) {
                if (v.is_object() && v.contains("ref") &&
                    reaches_freq(v.at("ref").get<std::string>())) {
                    memo[nid] = 1;
                    return true;
                }
                if (v.is_array())
                    for (const auto& e : v)
                        if (e.is_object() && e.contains("ref") &&
                            reaches_freq(e.at("ref").get<std::string>())) {
                            memo[nid] = 1;
                            return true;
                        }
            }
            return false;
        };

    for (const auto& id : nodeOrder) {
        const auto& node = nodeMap.at(id);
        auto nIt = g.nodes.find(id);
        if (nIt != g.nodes.end() && node.contains("params") &&
            !tracks_frequency_live(*nIt->second) &&
            node["params"].contains("frequency")) {
            const auto& v = node["params"].at("frequency");
            if (v.is_object() && v.contains("ref") &&
                reaches_freq(v.at("ref").get<std::string>()))
                std::fprintf(stderr,
                    "[loader] bend inert on '%s' (%s reads frequency once at "
                    "note start; backlog 26a)\n",
                    id.c_str(), nIt->second->type_name());
        }
        if (!node.contains("dynamicPins")) continue;
        auto nodeIt = g.nodes.find(id);
        if (nodeIt == g.nodes.end()) continue;
        const auto& consumer = nodeIt->second;

        for (const auto& [key, v] : node["dynamicPins"].items()) {
            // null = promoted in the editor but not yet wired — the pin
            // exists as editor state only; the stowed scalar governs.
            if (v.is_null()) continue;
            if (!v.is_object() || !v.contains("ref"))
                throw std::runtime_error("wiring: '" + id + ".dynamicPins." + key +
                                         "' must be a {\"ref\": ...} object");
            const SettingDescriptor* setting = nullptr;
            for (const auto& desc : consumer->setting_descriptors())
                if (key == desc.name) { setting = &desc; break; }
            if (!setting)
                throw std::runtime_error("wiring: '" + id + ".dynamicPins." + key +
                    "' is not a setting on " + consumer->type_name() +
                    " (fixed pins belong in \"params\")");
            if (setting->structural)
                throw std::runtime_error("wiring: '" + id + ".dynamicPins." + key +
                    "' is a structural setting on " + consumer->type_name() +
                    " (it sizes or reshapes the node at prepare), so it takes no pin");

            const std::string srcId = v.at("ref").get<std::string>();
            auto srcIt = g.nodes.find(srcId);
            if (srcIt == g.nodes.end())
                throw std::runtime_error("wiring: '" + id + "." + key +
                                         "' refs unknown node '" + srcId + "'");

            PitchedInstrument::PushBinding b;
            b.consumer     = consumer;
            b.paramName    = key;
            b.chain        = as_signal(srcIt->second, id + ".dynamicPins." + key);
            vg.pushBindings.push_back(std::move(b));
        }
    }
}

// The voice takes the graph's nodes: every node by id, and the signals among
// them in id order for the walks that need signals (VoiceGraph::signals).
static void adopt_nodes(PitchedInstrument::VoiceGraph& vg, GraphResult& g)
{
    vg.nodesById = std::move(g.nodes);
    vg.signals.clear();
    for (auto& [id, node] : vg.nodesById)
        if (auto* s = dynamic_cast<ValueSource*>(node.get()))
            vg.signals.push_back({id, s});
    std::sort(vg.signals.begin(), vg.signals.end(),
              [](const auto& a, const auto& b) { return a.id < b.id; });
}

// Create the voice's performance objects. Must run BEFORE build_graph: a
// PerformNode in the JSON resolves to these adapters during construction,
// so they cannot be created afterwards.
static PerformContext make_perform_context(PitchedInstrument::VoiceGraph& vg,
                                           std::shared_ptr<InstrumentState> is)
{
    vg.performSource = std::make_shared<PerformSource>();
    vg.performSource->set_instrument_state(std::move(is));
    vg.freqOut  = std::make_shared<PerformOut>(vg.performSource,
                                               PerformOut::Field::Frequency);
    vg.velOut   = std::make_shared<PerformOut>(vg.performSource,
                                               PerformOut::Field::Velocity);
    vg.wheelOut = std::make_shared<PerformOut>(vg.performSource,
                                               PerformOut::Field::Wheel);
    vg.pressOut = std::make_shared<PerformOut>(vg.performSource,
                                               PerformOut::Field::Pressure);
    vg.durOut   = std::make_shared<PerformOut>(vg.performSource,
                                               PerformOut::Field::Duration);
    vg.onsetOut = std::make_shared<PerformOut>(vg.performSource,
                                               PerformOut::Field::Onset);
    return PerformContext{vg.freqOut, vg.velOut, vg.wheelOut, vg.pressOut,
                          vg.durOut, vg.onsetOut};
}

// ---------------------------------------------------------------------------
// A plain sound's pull loops: one per listed channel, rooted at the node its
// source holds, and the mixer's own, rooted at the nodes wired into its gains
// and every listed channel's volume and pan. A loop holds every node its
// roots reach through {"ref": id} values anywhere in a node's params, single
// or in a list; a {"tap": id} reads its target's last value and pulls
// nothing, so the walk does not follow it, and dynamicPins are not walked.
// Each loop pulls its nodes on its own clock, so a node two loops reach fails
// the load: the first such node in the file's order, and the first two loops
// that reach it in the order given (the mixer's channel order, its own loop
// last).
// ---------------------------------------------------------------------------
struct PullLoop {
    std::string name;                 // "channel '<id>'", or the mixer's own
    std::vector<std::string> roots;   // ids of the nodes the loop pulls first
};

static void refuse_shared_nodes(const std::unordered_map<std::string, json>& nodeMap,
                                const std::vector<std::string>& nodeOrder,
                                const std::vector<PullLoop>& loops)
{
    std::vector<std::unordered_set<std::string>> reached(loops.size());
    for (size_t k = 0; k < loops.size(); ++k) {
        std::vector<std::string> pending = loops[k].roots;
        std::function<void(const json&)> refs = [&](const json& v) {
            if (v.is_object() && v.size() == 1 && v.contains("ref") && v["ref"].is_string())
                pending.push_back(v["ref"].get<std::string>());
            else if (v.is_object())
                for (auto it = v.begin(); it != v.end(); ++it) refs(it.value());
            else if (v.is_array())
                for (const auto& item : v) refs(item);
        };
        while (!pending.empty()) {
            const std::string id = pending.back();
            pending.pop_back();
            if (!reached[k].insert(id).second) continue;
            auto it = nodeMap.find(id);
            if (it != nodeMap.end() && it->second.contains("params")) refs(it->second["params"]);
        }
    }
    for (const auto& id : nodeOrder) {
        std::vector<size_t> in;
        for (size_t k = 0; k < loops.size(); ++k)
            if (reached[k].count(id)) in.push_back(k);
        if (in.size() > 1)
            throw std::runtime_error("'" + id + "' is reached from " + loops[in[0]].name + " and from "
                                     + loops[in[1]].name + ": each pulls its nodes on its own clock, "
                                     "so a node belongs to one");
    }
}

// A mixer pin's signal: the source wired into it, else a constant of the
// pin's declared value.
static std::shared_ptr<ValueSource> pin_signal(const Param& pin)
{
    return pin.source ? pin.source : std::make_shared<ConstantSource>(pin.constant);
}

// ---------------------------------------------------------------------------
// Patch loading
// ---------------------------------------------------------------------------

// The file's nodes by id, their order in the file, and the id the graph
// names as its output.
struct IndexedGraph {
    std::unordered_map<std::string, json> nodeMap;
    std::vector<std::string> nodeOrder;
    std::string outputId;
};

static IndexedGraph index_graph(const json& root)
{
    IndexedGraph ig;
    const json& graph = root.at("graph");
    const json& nodes = graph.at("nodes");
    ig.outputId = graph.at("output").get<std::string>();

    // Index nodes by id
    for (const auto& n : nodes) {
        std::string id = n.at("id").get<std::string>();
        ig.nodeMap.emplace(id, n);
        ig.nodeOrder.push_back(id);
    }
    return ig;
}

// The instrument a patch file's "instrument" block describes: the block read,
// then the voice pool, each voice its own instance of the graph. The caller
// has checked that the block is there. minPolyphony floors the pool's size;
// 0 keeps the file's own "polyphony".
static InstrumentPatch build_instrument_json(const json& root, int minPolyphony)
{
    int sampleRate = root.value("sampleRate", 48000);

    const IndexedGraph ig = index_graph(root);
    const auto& nodeMap   = ig.nodeMap;
    const auto& nodeOrder = ig.nodeOrder;
    const auto& outputId  = ig.outputId;

    const auto& instJson = root.at("instrument");
    if (instJson.contains("paramMap"))
        throw std::runtime_error(kParamMapRefusal);
    int polyphony = std::max(instJson.value("polyphony", 4), minPolyphony);

    auto inst = std::make_unique<PitchedInstrument>();
    // Pre-clip master gain (applied before the soft_clip peak guard, so it
    // is the right knob for keeping hot chains out of the clipper).
    inst->volume = instJson.value("volume", 1.0f);
    if (instJson.contains("onsets"))
        inst->onsetNames =
            instJson["onsets"].get<std::vector<std::string>>();
    // Legato retune time (spec 2026-09-20-note-onsets-v2 §4.1).
    inst->glideSec = instJson.value("glideMs", 15.0f) / 1000.0f;
    inst->sustaining = instJson.value("sustaining", false);   // §2
    if (instJson.value("release", 0.0f) != 0.0f)
        std::fprintf(stderr, "[loader] instrument.release retired "
                     "(note-contained sound 2026-08-13); ignored\n");

    // Build voice pool: N independent graph instances
    for (int v = 0; v < polyphony; ++v) {
        PitchedInstrument::VoiceGraph vg;
        PerformContext perf = make_perform_context(
            vg, inst->instrumentState);
        auto g = build_graph(nodeMap, nodeOrder, &perf,
                             &inst->onsetNames);

        // Find the top-level source for this voice
        auto srcIt = g.nodes.find(outputId);
        if (srcIt == g.nodes.end())
            throw std::runtime_error("instrument: output node '" + outputId + "' not found");
        vg.source = as_signal(srcIt->second, outputId);

        // Trigger bindings (spec 2026-09-20-note-onsets-v2 §6):
        // envelopes with a wired trigger restart at matching Setups.
        // allEnvelopes (§4/§5): the whole population, for hold-gating
        // and release re-layout.
        for (auto& [nid, src] : g.nodes)
            if (auto* env = dynamic_cast<Envelope*>(src.get())) {
                vg.allEnvelopes.push_back(env);
                if (env->trigger.source) vg.triggerBindings.push_back(env);
            }

        bind_wiring(nodeMap, nodeOrder, g, vg);
        // Loop tails consumed only by tap edges: the voice ticks these
        // after the root pull (feedback_loop_design.md §3.3).
        for (const auto& aid : collect_advance_ids(nodeMap, nodeOrder, outputId))
            vg.advanceList.push_back(as_signal(g.nodes.at(aid), aid));

        adopt_nodes(vg, g);

        inst->voicePool.push_back(std::move(vg));
    }
    // The one place this patch's render context is made: the voices learn
    // the rate here, before any note is played. A host's mixer may prepare
    // the instrument again with the same rate, which changes nothing.
    inst->prepare(RenderContext{sampleRate});

    return {std::move(inst), sampleRate};
}

BuiltSound load_patch_file(const std::string& path)
{
    json root = json::parse(slurp(path));

    BuiltSound built;

    int sampleRate = root.value("sampleRate", 48000);
    built.sampleRate = sampleRate;

    if (root.contains("seconds")) {
        double seconds = root["seconds"].get<double>();
        built.frames = static_cast<int>(std::lround(seconds * sampleRate));
    } else {
        built.frames = root.value("frames", sampleRate * 5);
    }

    // -------------------------------------------------------------------
    // PitchedInstrument mode: build voices, schedule notes
    // -------------------------------------------------------------------
    if (root.contains("instrument")) {
        auto inst = build_instrument_json(root, 0).instrument;

        // The embedded score is performed and its end in seconds returned;
        // it is handed over as the JSON text of the "score" array.
        if (root.contains("score")) {
            double end = play_embedded_score(*inst, root.at("score").dump());
            // The render covers at least the score; an explicit larger
            // `seconds` extends it: ring headroom, so struck bars and
            // reverbs finish past the last note instead of clipping at the
            // buffer edge.
            int scoreFrames = int(std::lround(end * sampleRate));
            built.frames = root.contains("seconds")
                ? std::max(built.frames, scoreFrames)
                : scoreFrames;
        }

        // Wire instrument into mixer
        auto mixer = std::make_unique<Mixer>();

        Channel ch;
        ch.source = std::move(inst);
        ch.volume = std::make_shared<ConstantSource>(1.0f);
        ch.pan    = std::make_shared<ConstantSource>(0.0f);

        mixer->channels.push_back(std::move(ch));
        mixer->prepare(RenderContext{sampleRate});
        built.mixer = std::move(mixer);
        return built;
    }

    // -------------------------------------------------------------------
    // Standard mode (no instrument): single graph instance
    // -------------------------------------------------------------------
    const IndexedGraph ig = index_graph(root);
    const auto& nodeMap   = ig.nodeMap;
    const auto& nodeOrder = ig.nodeOrder;
    const auto& outputId  = ig.outputId;

    auto g = build_graph(nodeMap, nodeOrder);
    auto& graphNodes = g.nodes;

    // Mixer-mode patches have no per-sample driver to tick tap-only loop
    // tails (the advance list is a voice concept — feedback_loop_design.md
    // §3.3), so such a tail here would render as a silently dead loop. Say
    // so loudly instead.
    for (const auto& aid : collect_advance_ids(nodeMap, nodeOrder, outputId))
        std::fprintf(stderr,
            "[loader] WARNING: node '%s' is consumed only by tap edges and "
            "will NOT advance in a mixer-mode patch — give the patch an "
            "instrument block, or take audio output from the loop tail\n",
            aid.c_str());

    auto outIt = graphNodes.find(outputId);
    if (outIt == graphNodes.end())
        throw std::runtime_error(
            "graph.output must be a StereoMixer or a mono-renderable source: " + outputId);

    auto mixer = std::make_unique<Mixer>();

    // The output as built: a StereoMixer is the mixer form; anything else is
    // the bare form, a signal in one unity channel at centre pan, as the
    // instrument path plays its voice. A part other than the mixer is refused
    // as one.
    const auto mixPart = std::dynamic_pointer_cast<StereoMixer>(outIt->second);
    if (!mixPart) {
        Channel ch;
        ch.volume = std::make_shared<ConstantSource>(1.0f);
        ch.pan    = std::make_shared<ConstantSource>(0.0f);
        ch.source = std::make_unique<ValueSourceMono>(as_signal(outIt->second, outputId));
        mixer->channels.push_back(std::move(ch));
        mixer->prepare(RenderContext{sampleRate});
        built.mixer = std::move(mixer);
        return built;
    }

    // A built part's id, for a message: the build's map, by pointer.
    auto id_of = [&](const Component* node) {
        for (const auto& [nid, n] : graphNodes)
            if (n.get() == node) return nid;
        return std::string();
    };

    // The mixer form, from the parts' pins. Each listed channel needs its
    // source; then no node may be reached from two pull loops.
    PullLoop mixerLoop{"the mixer's gains, volumes and pans", {}};
    auto root_of = [&](const Param& pin) {
        const std::string id = pin.source ? id_of(pin.source.get()) : std::string();
        if (!id.empty()) mixerLoop.roots.push_back(id);
    };
    root_of(mixPart->gainL);
    root_of(mixPart->gainR);
    std::vector<PullLoop> loops;
    for (const SoundChannel* view : mixPart->channels.views) {
        const std::string chId = id_of(view);
        if (!view->source.source)
            throw std::runtime_error("node '" + chId + "' (SoundChannel): 'source' is not wired; "
                                     "a channel the mixer lists needs a source");
        PullLoop loop{"channel '" + chId + "'", {}};
        if (std::string id = id_of(view->source.source.get()); !id.empty()) loop.roots.push_back(id);
        loops.push_back(std::move(loop));
        root_of(view->volume);
        root_of(view->pan);
    }
    loops.push_back(std::move(mixerLoop));
    refuse_shared_nodes(nodeMap, nodeOrder, loops);

    mixer->gainL = pin_signal(mixPart->gainL);
    mixer->gainR = pin_signal(mixPart->gainR);
    for (const SoundChannel* view : mixPart->channels.views) {
        Channel ch;
        ch.volume = pin_signal(view->volume);
        ch.pan    = pin_signal(view->pan);
        ch.source = std::make_unique<ValueSourceMono>(view->source.get());
        mixer->channels.push_back(std::move(ch));
    }

    mixer->prepare(RenderContext{sampleRate});
    built.mixer = std::move(mixer);
    return built;
}

// ---------------------------------------------------------------------------
// Load just the Instrument (no score, no mixer) for external use
// ---------------------------------------------------------------------------

InstrumentPatch load_instrument_patch_json(const std::string& jsonText,
                                           int minPolyphony)
{
    json root = json::parse(jsonText);

    if (!root.contains("instrument"))
        throw std::runtime_error("Patch has no 'instrument' section");

    return build_instrument_json(root, minPolyphony);
}

InstrumentPatch load_instrument_patch(const std::string& path,
                                      int minPolyphony)
{
    return load_instrument_patch_json(slurp(path), minPolyphony);
}

} // namespace mforce
