#include "mforce/render/patch_loader.h"
#include "mforce/core/source_registry.h"
#include "mforce/core/dsp_value_source.h"
#include "mforce/source/modulator/name_gate.h"
#include "mforce/source/modulator/range_source.h"
#include "mforce/source/modulator/var_source.h"
#include "mforce/source/modulator/curve_node.h"
#include "mforce/render/perform_source.h"
#include "mforce/source/envelope/envelope.h"
#include "mforce/source/envelope/envelope_json.h"
#include "mforce/render/instrument.h"
#include "mforce/render/embedded_score.h"
// Types still needed for special-case construction
#include "mforce/source/additive/partials.h"
#include "mforce/source/additive/expand_rule_node.h"
#include "mforce/source/additive/expand_rule.h"
#include "mforce/source/wavetable/wavetable_source.h"
#include "mforce/source/wavetable/hybrid_ks_source.h"
#include "mforce/source/envelope/segment_source.h"
#include "mforce/source/filter/shaper_source.h"
#include "mforce/source/envelope/phased_value_source.h"
#include "mforce/source/combiner/multiplex_source.h"
#include "mforce/source/filter/bw_lowpass_filter.h"
#include "mforce/source/filter/bw_highpass_filter.h"
#include "mforce/source/filter/bw_bandpass_filter.h"
#include "mforce/source/envelope/vibrato.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <nlohmann/json.hpp>
#include <fstream>
#include <functional>
#include <cstdio>
#include <sstream>
#include <stdexcept>
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

static std::shared_ptr<ValueSource> resolve_param_or(
    const json& params, const char* key, float defaultVal,
    const std::unordered_map<std::string, std::shared_ptr<Component>>& graphNodes)
{
    if (!params.contains(key))
        return std::make_shared<ConstantSource>(defaultVal);
    return resolve_signal(params.at(key), graphNodes);
}

// ---------------------------------------------------------------------------
// Advance list (feedback_loop_design.md §3.3): a node consumed ONLY by tap
// edges is never reached by the root pull — the loop tail would be silently
// dead — so the voice pulls it once per sample after the root. Membership is
// decided from the file, where every edge is enumerable (a built graph hides
// multi-input entries and setter-held envelopes): every {"ref":id},
// plain-string "inputs" id and {"tap":id} across all nodes; result = ids with
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
        if (node.contains("inputs")) {
            std::function<void(const json&)> scanIds = [&](const json& v) {
                if (v.is_string()) normal.insert(v.get<std::string>());
                else if (v.is_object())
                    for (auto it = v.begin(); it != v.end(); ++it) scanIds(it.value());
                else if (v.is_array())
                    for (const auto& item : v) scanIds(item);
            };
            scanIds(node["inputs"]);
        }
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
        "a", "absolute", "attack", "attackCurve", "attackMax",
        "attackMin", "attackPower", "b", "baseValue", "bias", "channels",
        "count", "curve", "cutoff", "decay", "decayCurve", "decayLevel",
        "decayMax", "decayMin", "decayPower", "delay", "dt1", "dt2",
        "durVarPct", "duration", "dynamicPins", "endVal", "expandRule",
        "exprKnots", "field", "form", "formants", "frames", "frequency", "gain",
        "gainAdj", "gains", "gap", "gapVarPct", "glideMs", "graph",
        "id", "inputs", "instrument", "interp", "interpolate", "knots",
        "loPct1", "loPct2", "max", "maxSec", "min", "minSec", "mode", "morph",
        "name", "nodes", "nominal", "normalized", "oneShot",
        "onsets", "operation", "output", "overlap", "paramMap",
        "percent", "po1", "po2", "polyphony", "power", "power1", "power2",
        "powers", "powers2", "preset", "ramp_accuracy", "ratio", "recurse",
        "ref", "release", "releaseCurve", "releaseMax", "releaseMin",
        "releasePower", "reverse", "sampleRate", "score", "seconds", "sections",
        "seed", "segs", "segs2", "source", "spacing1", "spacing2",
        "stage_accuracy", "stages", "startVal", "sustain", "sustainLevel",
        "sustaining", "tap", "target", "targetPartials", "timeMode",
        "type", "values", "values2", "varPct", "vcurve", "volume",
        "weight", "x",
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
    for (auto it = params.begin(); it != params.end(); ++it)
        if (!known(it.key()))
            throw std::runtime_error("node '" + id + "' (" + src.type_name()
                                     + "): unknown key '" + it.key() + "'");
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
                auto part = resolve_param(item, graphNodes);
                if (!src.add_param(desc.name, part)) reject_wire(src, id, desc.name, *part);
            }
        } else {
            auto part = resolve_param(v, graphNodes);
            if (!src.set_param(desc.name, part)) reject_wire(src, id, desc.name, *part);
        }
    }
    for (const auto& desc : src.param_descriptors()) {
        if (!params.contains(desc.name)) continue;
        if (params.at(desc.name).is_string()) refuse_string_pin(src, id, desc.name, params.at(desc.name).get<std::string>(), false);
        auto part = resolve_param(params.at(desc.name), graphNodes);
        if (!src.set_param(desc.name, part)) reject_wire(src, id, desc.name, *part);
    }
    // Scalar settings (int/float/bool) — apply if present in JSON.
    for (const auto& desc : src.setting_descriptors()) {
        if (!params.contains(desc.name)) continue;
        const auto& v = params.at(desc.name);
        float fv = 0.0f;
        if      (v.is_boolean()) fv = v.get<bool>() ? 1.0f : 0.0f;
        else if (v.is_number())  fv = v.get<float>();
        else if (v.is_string() && desc.enum_labels) {
            // Enum settings arrive as strings in hand-written patches
            // ("mode": "Highpass"). Match display labels case-
            // insensitively; a miss keeps the default LOUDLY — silently
            // skipping strings is how SVFSource's mode no-opped on first
            // use (same drop class as the UI-side 2026-08-13 fix).
            const std::string s = v.get<std::string>();
            bool matched = false;
            for (int li = 0; desc.enum_labels[li]; ++li) {
                if (s.size() == std::strlen(desc.enum_labels[li]) &&
                    std::equal(s.begin(), s.end(), desc.enum_labels[li],
                               [](char a, char b) {
                                   return std::tolower((unsigned char)a) ==
                                          std::tolower((unsigned char)b);
                               })) {
                    fv = float(li);
                    matched = true;
                    break;
                }
            }
            if (!matched) {
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
    for (const auto& desc : src.array_descriptors()) {
        if (!params.contains(desc.name)) continue;
        const auto& v = params.at(desc.name);
        if (!v.is_array()) continue;
        src.set_array(desc.name, v.get<std::vector<float>>());
    }
    // Shaper morph invariants (curve-morph spec §5): curve B must have the
    // same point count as A (point-space morph needs correspondence), and
    // segs2's TYPE column must equal segs' (types are shared structure;
    // only powers differ per curve). Checked here, right after the generic
    // array pass, so a hand-edited patch fails loudly instead of morphing
    // garbage.
    if (dynamic_cast<ShaperSource*>(&src)) {
        const auto arr = [&](const char* n) {
            return params.contains(n) && params.at(n).is_array()
                 ? params.at(n).get<std::vector<float>>()
                 : std::vector<float>{};
        };
        const auto vals = arr("values"), vals2 = arr("values2");
        if (!vals2.empty() && vals2.size() != vals.size())
            throw std::runtime_error(
                "Shaper: values2 length must match values");
        const auto segs = arr("segs"), segs2 = arr("segs2");
        if (!segs2.empty() && !segs.empty()) {
            if (segs2.size() != segs.size())
                throw std::runtime_error(
                    "Shaper: segs2 length must match segs");
            // Types are shared structure, EXCEPT the Expo/InverseExpo pair:
            // those are one signed-curvature axis (bulge direction is
            // geometry) and morph lerps between them.
            auto isPow = [](int t) { return t == 2 || t == 3; };
            for (size_t i = 0; i < segs.size(); i += 2) {
                const int a = int(segs[i]), b = int(segs2[i]);
                if (a != b && !(isPow(a) && isPow(b)))
                    throw std::runtime_error(
                        "Shaper: segs2 segment types must match segs");
            }
        }
        // Hysteresis invariants (hysteresis spec §3). The generic
        // settings pass above has already applied the flag.
        if (src.get_setting("hysteresis") != 0.0f) {
            if (params.contains("morph"))
                std::fprintf(stderr,
                    "[load] Shaper: morph is ignored while hysteresis "
                    "is on\n");
            if (vals2.empty())
                std::fprintf(stderr,
                    "[load] Shaper: hysteresis without values2 is "
                    "inert (no slip curve)\n");
        }
    }
    // ExpandRule (PartialGroups) — a struct, not a pin/setting/array, so it isn't
    // covered by the loops above. Consumed only by Partials hosts. Two JSON forms:
    //   "expandRule": { "count": 4, "spacing1": 0.3, ... }   (inline)
    //   "expandRule": { "ref": "<ExpandRule node id>" }        (shared node)
    if (params.contains("expandRule")) {
        if (auto* host = dynamic_cast<Partials*>(&src)) {
            const auto& er = params.at("expandRule");
            if (er.contains("ref")) {
                auto it = graphNodes.find(er.at("ref").get<std::string>());
                if (it != graphNodes.end())
                    if (auto* node = dynamic_cast<ExpandRuleNode*>(it->second.get()))
                        host->set_expand_rule(node->to_struct());
            } else {
                ExpandRule rule;
                rule.count    = er.value("count", 2);
                rule.recurse  = er.value("recurse", 0);
                rule.spacing1 = er.value("spacing1", 0.5f);
                rule.spacing2 = er.value("spacing2", 0.5f);
                rule.dt1      = er.value("dt1", 0.01f);
                rule.dt2      = er.value("dt2", 0.01f);
                rule.loPct1   = er.value("loPct1", 0.1f);
                rule.loPct2   = er.value("loPct2", 0.1f);
                rule.power1   = er.value("power1", 1.0f);
                rule.power2   = er.value("power2", 1.0f);
                rule.po1      = er.value("po1", 0.0f);
                rule.po2      = er.value("po2", 0.0f);
                host->set_expand_rule(rule);
            }
        }
    }

}

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

        // ---- Extract seed if present ----
        std::optional<uint32_t> seed;
        if (pp && pp->contains("seed"))
            seed = static_cast<uint32_t>((*pp)["seed"].get<int>());

        // =================================================================
        // Types that need special construction (setting in constructor args)
        // =================================================================

        if (type == "VarSource") {
            if (!pp) throw std::runtime_error("VarSource requires params");
            const auto& p = *pp;
            bool absolute = p.value("absolute", true);
            auto src = std::make_shared<VarSource>(
                resolve_param_or(p, "val",    0.0f, graphNodes),
                resolve_param_or(p, "var",    0.0f, graphNodes),
                resolve_param_or(p, "varPct", 0.0f, graphNodes), absolute);
            graphNodes[id] = src;
        }
        else if (type == "RangeSource") {
            if (!pp) throw std::runtime_error("RangeSource requires params");
            const auto& p = *pp;
            bool normalized = p.value("normalized", false);
            auto src = std::make_shared<RangeSource>(
                resolve_param_or(p, "min", 0.0f, graphNodes),
                resolve_param_or(p, "max", 1.0f, graphNodes),
                resolve_param_or(p, "var", 0.0f, graphNodes), normalized);
            graphNodes[id] = src;
        }
        else if (type == "Envelope") {
            if (!pp) throw std::runtime_error("Envelope requires params");
            const auto& p = *pp;
            std::shared_ptr<Envelope> env;

            if (p.contains("stages")) {
                env = std::make_shared<Envelope>();
                for (const auto& sj : p["stages"]) {
                    Envelope::Stage s;
                    s.ramp.startVal = sj.value("startVal", 0.0f);
                    s.ramp.endVal   = sj.value("endVal",   0.0f);
                    s.ramp.power    = sj.value("power",    0.0f);
                    // holdPct removed 2026-09-05 (curve-morph spec §3): a
                    // stale key in old JSON is simply ignored here.
                    std::string t   = sj.value("type", std::string("Linear"));
                    s.ramp.type = (t == "Expo")        ? RampType::Expo
                                : (t == "InverseExpo") ? RampType::InverseExpo
                                : (t == "Sine")        ? RampType::Sine
                                : (t == "Hold")        ? RampType::Hold
                                                       : RampType::Linear;
                    s.percent = sj.value("percent", 0.0f);
                    s.minSec  = sj.value("minSec",  0.0f);
                    s.maxSec  = sj.value("maxSec",  0.0f);
                    s.nominal = sj.value("nominal", 0.0f);
                    env->add_stage(s);
                }
                if (p.contains("stage_accuracy") && p["stage_accuracy"].is_number())
                    env->stage_accuracy.value = std::clamp(p["stage_accuracy"].get<float>(), 0.0f, 1.0f);
                if (p.contains("ramp_accuracy") && p["ramp_accuracy"].is_number())
                    env->ramp_accuracy.value = std::clamp(p["ramp_accuracy"].get<float>(), 0.0f, 1.0f);
                // Literal-seconds stage times (percent = seconds; see envelope.h
                // absolute_time). Same key as the adsr preset form.
                if (p.value("timeMode", std::string("fraction")) == "seconds")
                    env->absolute_time = true;
            } else {
                // Shared preset dispatch (envelope_json.h) — the UI graph
                // loader calls the same function, so the two cannot drift.
                env = envelope_from_preset_json(p);
            }
            // An authored or multiplex-injected seed, in either form.
            if (p.contains("seed") && p["seed"].is_number())
                env->set_seed(uint32_t(p["seed"].get<int64_t>()));
            graphNodes[id] = env;
            // Pullable range params (minValue / maxValue, PerformSource P1,
            // envelope.h param_descriptors). This explicit branch never ran
            // the generic wiring, so from 2026-08-18 to 2026-08-22 the keys
            // were silently IGNORED on every stage-form and preset-form
            // Envelope — proved by A/B, maxValue 0.25 rendered byte-identical
            // to the control. Registry-path envelope types (ADSREnvelope…)
            // were always wired. Null when absent → identity, unchanged.
            wire_params_generic(*env, id, p, graphNodes);
        }
        else if (type == "CurveNode") {
            auto cn = std::make_shared<CurveNode>();
            if (pp) {
                const auto& p = *pp;
                if (p.contains("knots"))
                    for (const auto& k : p["knots"])
                        cn->knots.emplace_back(k.at(0).get<float>(),
                                               k.at(1).get<float>());
                // Expressions mode: knot values are formulas of x, stored as
                // {"x":..,"form":"linear"|"power","a":..,"b":..} rows (b is
                // the exponent p for power). See curve_node.h header comment.
                cn->exprMode = p.value("mode", std::string("points")) == "expressions";
                if (p.contains("exprKnots"))
                    for (const auto& k : p["exprKnots"]) {
                        CurveNode::ExprKnot ek;
                        ek.x = k.at("x").get<float>();
                        ek.form = k.value("form", std::string("linear")) == "power"
                                  ? CurveNode::KnotForm::Power
                                  : CurveNode::KnotForm::Linear;
                        ek.a = k.value("a", 0.0f);
                        ek.b = k.value("b", 0.0f);
                        cn->exprKnots.push_back(ek);
                    }
                const std::string in = p.value("interp", std::string("linear"));
                cn->interp = in == "loglog" ? CurveNode::CurveInterp::LogLog
                           : in == "logx"   ? CurveNode::CurveInterp::LogX
                           :                  CurveNode::CurveInterp::Linear;
            }
            // "source" is a param_descriptor, but wire_params_generic is NOT
            // automatic — every special-cased type calls it explicitly. Without
            // this call a CurveNode loaded from JSON has a null source: next()
            // returns 0, and map(0) clamps to the first knot's value for every
            // note.
            if (pp) wire_params_generic(*cn, id, *pp, graphNodes);
            graphNodes[id] = cn;
        }
        else if (type == "PerformNode") {
            // Leaf: resolves to the voice's shared PerformOut adapter. Several
            // PerformNode nodes naming the same field are the SAME object by
            // design — the pin model's "all instances render the same outputs".
            if (!perf)
                throw std::runtime_error("PerformNode '" + id + "': no voice "
                    "context (PerformNode is instrument/patch mode only)");
            const std::string field = pp ? pp->value("field", std::string("frequency"))
                                         : std::string("frequency");
            if      (field == "frequency") graphNodes[id] = perf->freqOut;
            else if (field == "velocity")  graphNodes[id] = perf->velOut;
            else if (field == "wheel")     graphNodes[id] = perf->wheelOut;
            else if (field == "pressure")  graphNodes[id] = perf->pressOut;
            else if (field == "duration")  graphNodes[id] = perf->durOut;
            else if (field == "onset")     graphNodes[id] = perf->onsetOut;
            else throw std::runtime_error("PerformNode '" + id + "': unknown field '"
                + field
                + "' (expected frequency|velocity|wheel|pressure|duration|onset)");
        }
        else if (type == "NameGate") {
            // Onset-name match (spec 2026-09-20-note-onsets-v2 §6).
            // The `name` string resolves against the instrument's
            // onsets[] vocabulary; ids are 1-based declaration order.
            auto ng = std::make_shared<NameGate>();
            if (pp && pp->contains("name")) {
                ng->name = (*pp)["name"].get<std::string>();
                if (onsets)
                    for (size_t k = 0; k < onsets->size(); ++k)
                        if ((*onsets)[k] == ng->name) {
                            ng->targetId = float(k + 1);
                            break;
                        }
                if (!ng->name.empty() && ng->targetId < 0.0f) {
                    static std::unordered_set<std::string> warnedGates;
                    if (warnedGates.insert(ng->name).second)
                        std::fprintf(stderr, "[load] NameGate '%s': name "
                            "'%s' is not in the instrument's onsets[] "
                            "vocabulary — it will never fire\n",
                            id.c_str(), ng->name.c_str());
                }
            }
            if (pp) wire_params_generic(*ng, id, *pp, graphNodes);
            graphNodes[id] = ng;
        }
        else if (type == "SegmentSource") {
            std::vector<float> values;
            bool oneShot = false;
            if (pp) {
                if (pp->contains("values")) values = (*pp)["values"].get<std::vector<float>>();
                oneShot = pp->value("oneShot", false);
            }
            auto src = std::make_shared<SegmentSource>(
                std::move(values), oneShot, seed.value_or(0x5E6A'0000u));
            if (pp) wire_params_generic(*src, id, *pp, graphNodes);
            graphNodes[id] = src;
        }
        else if (type == "Vibrato") {
            if (!pp) throw std::runtime_error("Vibrato requires params");
            const auto& p = *pp;
            auto vib = std::make_shared<Vibrato>(seed.value_or(0xF1B0'0000u));
            for (const auto& [key, dflt] : std::initializer_list<std::pair<const char*, float>>{
                     {"speed", 5.0f}, {"depth", 0.02f}, {"attack", 0.3f}, {"threshold", 0.0f},
                     {"speedVar", 0.0f}, {"depthVar", 0.0f}, {"zeroCrossTendency", 1.0f}})
                vib->set_setting(key, p.value(key, dflt));
            wire_params_generic(*vib, id, p, graphNodes);
            graphNodes[id] = vib;
        }
        else if (type == "BWLowpassFilter" || type == "BWHighpassFilter" || type == "BWBandpassFilter") {
            int sections = pp ? pp->value("sections", 2) : 2;
            std::shared_ptr<ValueSource> src;
            if (type == "BWLowpassFilter")
                src = std::make_shared<BWLowpassFilter>(sections);
            else if (type == "BWHighpassFilter")
                src = std::make_shared<BWHighpassFilter>(sections);
            else
                src = std::make_shared<BWBandpassFilter>(sections);
            if (pp) {
                wire_params_generic(*src, id, *pp, graphNodes);
                // JSON uses "cutoff" alias for single-band filters
                if (pp->contains("cutoff"))
                {
                    auto part = resolve_param(pp->at("cutoff"), graphNodes);
                    if (!src->set_param("cutoffFreq", part)) reject_wire(*src, id, "cutoffFreq", *part);
                }
            }
            graphNodes[id] = src;
        }
        // ---- HybridKSSource (setting params) ----
        else if (type == "HybridKSSource") {
            auto hks = std::make_shared<HybridKSSource>(seed.value_or(0xBEEF'C0DEu));
            if (pp) {
                const auto& p = *pp;
                wire_params_generic(*hks, id, p, graphNodes);
                if (p.contains("targetPartials"))
                    hks->set_target_partials(p["targetPartials"].get<std::vector<float>>());
            }
            graphNodes[id] = hks;
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
        // SoundChannel and StereoMixer handled by caller
        else if (type != "SoundChannel" && type != "StereoMixer") {
            std::string msg = "Unknown node type: " + type;
            if (const char* hint = retired_type_hint(type)) msg += std::string("; ") + hint;
            throw std::runtime_error(msg);
        }

        // Whichever path built the node, every key it was given is one the
        // engine read; an unknown key fails the load.
        if (pp && graphNodes.count(id)) reject_unknown_keys(*graphNodes[id], id, *pp);
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
                    !n.contains("params") ||
                    n["params"].value("field", std::string("frequency")) == "frequency";
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

    auto outIt = nodeMap.find(outputId);
    if (outIt == nodeMap.end())
        throw std::runtime_error("graph.output not found");

    auto mixer = std::make_unique<Mixer>();

    // A bare mono source as graph.output used to be a hard error, which made
    // seven patches unrenderable from the CLI while working fine in the UI
    // (CombineTest, ks_morph_*, mux_*). The instrument path above already
    // auto-wraps a bare output in a unity channel; this does the same for the
    // standard path so the two agree. Unity volume + centre pan matches the
    // instrument path exactly rather than inventing a second convention.
    if (outIt->second.at("type").get<std::string>() != "StereoMixer") {
        auto it = graphNodes.find(outputId);
        if (it == graphNodes.end())
            throw std::runtime_error(
                "graph.output must be a StereoMixer or a mono-renderable source: " + outputId);

        Channel ch;
        ch.volume = std::make_shared<ConstantSource>(1.0f);
        ch.pan    = std::make_shared<ConstantSource>(0.0f);
        ch.source = std::make_unique<ValueSourceMono>(as_signal(it->second, outputId));
        mixer->channels.push_back(std::move(ch));
        mixer->prepare(RenderContext{sampleRate});
        built.mixer = std::move(mixer);
        return built;
    }

    if (outIt->second.contains("params")) {
        const auto& params = outIt->second["params"];
        mixer->gainL = resolve_param_or(params, "gainL", 1.0f, graphNodes);
        mixer->gainR = resolve_param_or(params, "gainR", 1.0f, graphNodes);
    }

    const auto& chIds = outIt->second.at("inputs").at("channels");

    // Each channel's adapter is its own pull loop with its own clock. A node
    // two adapters prepare follows only the second one's clock, so the first
    // channel would render one held sample: a source feeds one channel.
    std::unordered_map<std::string, std::string> channelOfSource;
    for (const auto& chIdVal : chIds) {
        std::string chId = chIdVal.get<std::string>();
        const auto& chNode = nodeMap.at(chId);

        if (chNode.at("type").get<std::string>() != "SoundChannel")
            throw std::runtime_error("Node is not SoundChannel: " + chId);

        Channel ch;

        if (chNode.contains("params")) {
            const auto& p = chNode["params"];
            ch.volume = resolve_param_or(p, "volume", 1.0f, graphNodes);
            ch.pan    = resolve_param_or(p, "pan",    0.0f, graphNodes);
        } else {
            ch.volume = std::make_shared<ConstantSource>(1.0f);
            ch.pan    = std::make_shared<ConstantSource>(0.0f);
        }

        std::string srcId =
            chNode.at("inputs").at("source").get<std::string>();

        auto it = graphNodes.find(srcId);
        if (it == graphNodes.end())
            throw std::runtime_error("No mono source for: " + srcId);
        const auto [taken, fresh] = channelOfSource.emplace(srcId, chId);
        if (!fresh)
            throw std::runtime_error("channels '" + taken->second + "' and '" + chId + "' name one source '"
                                     + srcId + "': a source feeds one channel");

        ch.source = std::make_unique<ValueSourceMono>(as_signal(it->second, srcId));
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
