#include "mforce/render/patch_loader.h"
#include "mforce/core/source_registry.h"
#include "mforce/core/dsp_value_source.h"
#include "mforce/core/dsp_wave_source.h"
#include "mforce/core/range_source.h"
#include "mforce/core/var_source.h"
#include "mforce/core/curve_node.h"
#include "mforce/core/envelope.h"
#include "mforce/core/envelope_json.h"
#include "mforce/render/instrument.h"
#include "mforce/music/basics.h"
#include "mforce/music/conductor.h"
#include "mforce/music/music_json.h"
#include "mforce/music/pitch_bend.h"
#include "mforce/music/pitch_curve.h"
// Types still needed for special-case construction
#include "mforce/source/additive/full_additive_source.h"
#include "mforce/source/additive/partials.h"
#include "mforce/source/additive/additive_source2.h"
#include "mforce/source/additive/formant.h"
#include "mforce/source/wavetable_source.h"
#include "mforce/source/hybrid_ks_source.h"
#include "mforce/source/segment_source.h"
#include "mforce/source/phased_value_source.h"
#include "mforce/source/wave_evolution.h"
#include "mforce/source/multiplex_source.h"
#include "mforce/filter/filters.h"
#include "mforce/filter/vibrato.h"
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

// Adapters: WaveSource/ValueSource → MonoSource for channel rendering
struct WaveSourceMono final : MonoSource {
    std::shared_ptr<WaveSource> src;
    explicit WaveSourceMono(std::shared_ptr<WaveSource> s) : src(std::move(s)) {}
    void render(const RenderContext& ctx, float* out, int frames) override {
        src->prepare(ctx, frames);
        for (int i = 0; i < frames; ++i) out[i] = src->next();
    }
};

struct ValueSourceMono final : MonoSource {
    std::shared_ptr<ValueSource> src;
    explicit ValueSourceMono(std::shared_ptr<ValueSource> s) : src(std::move(s)) {}
    void render(const RenderContext& ctx, float* out, int frames) override {
        src->prepare(ctx, frames);
        for (int i = 0; i < frames; ++i) out[i] = src->next();
    }
};

// ---------------------------------------------------------------------------
// Param resolution: number -> ConstantSource, {"ref":"id"} -> lookup
// ---------------------------------------------------------------------------

// Resolve a JSON ref/number into a ValueSource. When `usage` is non-null,
// each resolved ref increments usage[refId]; on the SECOND+ resolve of the
// same source, the returned shared_ptr wraps the source in a RefSource so
// only the first consumer's next() advances the underlying state. Without
// this, a single envelope wired to N consumer pins would be advanced N times
// per audio sample, exhausting its timeline at 1/N of the note duration.
// Mirrors the auto-wrap logic in mforce_ui/main.cpp::update_all_dsp().
static std::shared_ptr<ValueSource> resolve_param(
    const json& val,
    const std::unordered_map<std::string, std::shared_ptr<ValueSource>>& valueNodes,
    std::unordered_map<std::string, int>* usage = nullptr)
{
    if (val.is_number())
        return std::make_shared<ConstantSource>(val.get<float>());

    if (val.is_object() && val.contains("ref")) {
        std::string refId = val.at("ref").get<std::string>();
        auto it = valueNodes.find(refId);
        if (it == valueNodes.end())
            throw std::runtime_error("Unresolved node ref: " + refId);
        if (usage) {
            int& count = (*usage)[refId];
            count++;
            if (count > 1) return std::make_shared<RefSource>(it->second);
        }
        return it->second;
    }

    throw std::runtime_error("Param must be a number or {\"ref\":\"...\"}");
}

static std::shared_ptr<ValueSource> resolve_param_or(
    const json& params, const char* key, float defaultVal,
    const std::unordered_map<std::string, std::shared_ptr<ValueSource>>& valueNodes,
    std::unordered_map<std::string, int>* usage = nullptr)
{
    if (!params.contains(key))
        return std::make_shared<ConstantSource>(defaultVal);
    return resolve_param(params.at(key), valueNodes, usage);
}

// ---------------------------------------------------------------------------
// Graph building: creates all ValueSource/MonoSource nodes from JSON.
// Extracted so it can be called once (normal mode) or N times (instrument voices).
// ---------------------------------------------------------------------------

struct GraphResult {
    std::unordered_map<std::string, std::shared_ptr<ValueSource>> valueNodes;
    std::unordered_map<std::string, std::shared_ptr<IFormant>>    formantNodes;
    std::unordered_map<std::string, std::unique_ptr<MonoSource>>  monoNodes;
};

// Wire all connectable params generically via param_descriptors + input_descriptors + set_param.
static void wire_params_generic(
    ValueSource& src,
    const json& params,
    const std::unordered_map<std::string, std::shared_ptr<ValueSource>>& valueNodes,
    std::unordered_map<std::string, int>* usage = nullptr)
{
    // A STRING in a pin/param slot is always a legacy enum form consumed by a
    // hand-written branch further down (e.g. WavetableSource's
    // "evolution": "target", which is ALSO an input descriptor for the newer
    // ref-wired form). resolve_param can never handle a string, so throwing
    // here just kills the patch before its own special case runs — that is why
    // the three ks_morph_*_test patches were unrenderable. Skip strings; keep
    // throwing on other junk so genuine mistakes stay loud.
    auto skip_unresolvable = [](const json& v) { return v.is_string(); };

    for (const auto& desc : src.input_descriptors()) {
        if (!params.contains(desc.name)) continue;
        const auto& v = params.at(desc.name);
        if (skip_unresolvable(v)) continue;
        if (desc.multi && v.is_array()) {
            // Multi-input pin with array-of-refs JSON. Iterate and add each.
            src.clear_param(desc.name);
            for (const auto& item : v)
                src.add_param(desc.name, resolve_param(item, valueNodes, usage));
        } else {
            src.set_param(desc.name, resolve_param(v, valueNodes, usage));
        }
    }
    for (const auto& desc : src.param_descriptors()) {
        if (!params.contains(desc.name)) continue;
        if (skip_unresolvable(params.at(desc.name))) continue;
        src.set_param(desc.name, resolve_param(params.at(desc.name), valueNodes, usage));
    }
    // Scalar configs (int/float/bool) — apply if present in JSON.
    for (const auto& desc : src.config_descriptors()) {
        if (!params.contains(desc.name)) continue;
        const auto& v = params.at(desc.name);
        float fv = 0.0f;
        if      (v.is_boolean()) fv = v.get<bool>() ? 1.0f : 0.0f;
        else if (v.is_number())  fv = v.get<float>();
        else if (v.is_string() && desc.enum_labels) {
            // Enum configs arrive as strings in hand-written patches
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
        src.set_config(desc.name, fv);
    }
    // User-editable float arrays — ExplicitPartials multipliers, spectrum gains, etc.
    for (const auto& desc : src.array_descriptors()) {
        if (!params.contains(desc.name)) continue;
        const auto& v = params.at(desc.name);
        if (!v.is_array()) continue;
        src.set_array(desc.name, v.get<std::vector<float>>());
    }
    // ExpandRule (PartialGroups) — a struct, not a pin/config/array, so it isn't
    // covered by the loops above. Consumed only by Partials hosts. Two JSON forms:
    //   "expandRule": { "count": 4, "spacing1": 0.3, ... }   (inline)
    //   "expandRule": { "ref": "<ExpandRule node id>" }        (shared node)
    if (params.contains("expandRule")) {
        if (auto* host = dynamic_cast<Partials*>(&src)) {
            const auto& er = params.at("expandRule");
            if (er.contains("ref")) {
                auto it = valueNodes.find(er.at("ref").get<std::string>());
                if (it != valueNodes.end())
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

// Register MonoSource wrapper (WaveSource → WaveSourceMono, else ValueSourceMono).
static void add_mono(
    GraphResult& g, const std::string& id, std::shared_ptr<ValueSource> src)
{
    auto ws = std::dynamic_pointer_cast<WaveSource>(src);
    if (ws) g.monoNodes[id] = std::make_unique<WaveSourceMono>(ws);
    else    g.monoNodes[id] = std::make_unique<ValueSourceMono>(src);
}

// Register IFormant if the source implements it.
static void add_formant(
    GraphResult& g, const std::string& id, std::shared_ptr<ValueSource> src)
{
    auto fmt = std::dynamic_pointer_cast<IFormant>(src);
    if (fmt) g.formantNodes[id] = fmt;
}

// Forward declarations for subgraph extraction / rebuild helpers (defined
// after build_graph since build_subgraph_with_seed_perturbation calls it).
static json extract_subgraph_json(
    const std::unordered_map<std::string, json>& nodeMap,
    const std::string& rootId);
// Returns {root source, valueNodes by id} so callers (Multiplex's paramMap
// fan-out) can reach into the built clone's internals by node id.
static std::pair<std::shared_ptr<ValueSource>,
                 std::unordered_map<std::string, std::shared_ptr<ValueSource>>>
build_subgraph_with_seed_perturbation(
    const std::string& subtreeJsonStr,
    uint32_t seedPerturbation,
    int sampleRate);

static GraphResult build_graph(
    const std::unordered_map<std::string, json>& nodeMap,
    const std::vector<std::string>& nodeOrder,
    int sampleRate)
{
    // Lazy init registry
    static bool registered = false;
    if (!registered) { register_all_sources(); registered = true; }

    GraphResult g;
    auto& valueNodes   = g.valueNodes;
    auto& formantNodes = g.formantNodes;

    auto& reg = SourceRegistry::instance();

    // Tracks how many times each value source has been wired as a consumer
    // input. The 2nd+ time, resolve_param wraps in RefSource so only one
    // consumer drives next() — fixes the "envelope shared across multiple
    // pins exhausts in 1/N duration" bug.
    std::unordered_map<std::string, int> usage;

    // Resolve function for configurators
    ResolveParamFn resolveFn = [&](const json& val) {
        return resolve_param(val, valueNodes, &usage);
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
        // Types that need special construction (config in constructor args)
        // =================================================================

        if (type == "VarSource") {
            if (!pp) throw std::runtime_error("VarSource requires params");
            const auto& p = *pp;
            bool absolute = p.value("absolute", true);
            auto src = std::make_shared<VarSource>(
                resolve_param_or(p, "val",    0.0f, valueNodes, &usage),
                resolve_param_or(p, "var",    0.0f, valueNodes, &usage),
                resolve_param_or(p, "varPct", 0.0f, valueNodes, &usage), absolute);
            valueNodes[id] = src;
        }
        else if (type == "RangeSource") {
            if (!pp) throw std::runtime_error("RangeSource requires params");
            const auto& p = *pp;
            bool normalized = p.value("normalized", false);
            auto src = std::make_shared<RangeSource>(
                resolve_param_or(p, "min", 0.0f, valueNodes, &usage),
                resolve_param_or(p, "max", 1.0f, valueNodes, &usage),
                resolve_param_or(p, "var", 0.0f, valueNodes, &usage), normalized);
            valueNodes[id] = src;
        }
        else if (type == "Envelope") {
            if (!pp) throw std::runtime_error("Envelope requires params");
            const auto& p = *pp;

            if (p.contains("stages")) {
                auto env = std::make_shared<Envelope>(sampleRate);
                for (const auto& sj : p["stages"]) {
                    Envelope::Stage s;
                    s.ramp.startVal = sj.value("startVal", 0.0f);
                    s.ramp.endVal   = sj.value("endVal",   0.0f);
                    s.ramp.power    = sj.value("power",    0.0f);
                    s.ramp.holdPct  = sj.value("holdPct",  0.0f);
                    std::string t   = sj.value("type", std::string("Linear"));
                    s.ramp.type = (t == "Expo")        ? RampType::Expo
                                : (t == "InverseExpo") ? RampType::InverseExpo
                                : (t == "Sine")        ? RampType::Sine
                                                       : RampType::Linear;
                    s.percent = sj.value("percent", 0.0f);
                    s.minSec  = sj.value("minSec",  0.0f);
                    s.maxSec  = sj.value("maxSec",  0.0f);
                    s.nominal = sj.value("nominal", 0.0f);
                    env->add_stage(s);
                }
                // Pick up multiplex-injected seed and any accuracy configs.
                if (p.contains("seed") && p["seed"].is_number())
                    env->set_seed(uint32_t(p["seed"].get<int64_t>()));
                if (p.contains("stage_accuracy") && p["stage_accuracy"].is_number())
                    env->stage_accuracy = std::clamp(p["stage_accuracy"].get<float>(), 0.0f, 1.0f);
                if (p.contains("ramp_accuracy") && p["ramp_accuracy"].is_number())
                    env->ramp_accuracy = std::clamp(p["ramp_accuracy"].get<float>(), 0.0f, 1.0f);
                // Literal-seconds stage times (percent = seconds; see envelope.h
                // absolute_time). Same key as the adsr preset form.
                if (p.value("timeMode", std::string("fraction")) == "seconds")
                    env->absolute_time = true;
                valueNodes[id] = env;
            } else {
                // Shared preset dispatch (envelope_json.h) — the UI graph
                // loader calls the same function, so the two cannot drift.
                valueNodes[id] = std::make_shared<Envelope>(
                    envelope_from_preset_json(p, sampleRate));
            }
        }
        else if (type == "CurveNode") {
            auto cn = std::make_shared<CurveNode>();
            if (pp) {
                const auto& p = *pp;
                if (p.contains("knots"))
                    for (const auto& k : p["knots"])
                        cn->knots.emplace_back(k.at(0).get<float>(),
                                               k.at(1).get<float>());
                const std::string in = p.value("interp", std::string("linear"));
                cn->interp = in == "loglog" ? CurveNode::CurveInterp::LogLog
                           : in == "logx"   ? CurveNode::CurveInterp::LogX
                           :                  CurveNode::CurveInterp::Linear;
            }
            // "source" wires through the generic param pass like any node.
            valueNodes[id] = cn;
        }
        else if (type == "SegmentSource") {
            std::vector<float> values;
            bool oneShot = false;
            if (pp) {
                if (pp->contains("values")) values = (*pp)["values"].get<std::vector<float>>();
                oneShot = pp->value("oneShot", false);
            }
            auto src = std::make_shared<SegmentSource>(
                std::move(values), sampleRate, oneShot, seed.value_or(0x5E6A'0000u));
            if (pp) wire_params_generic(*src, *pp, valueNodes, &usage);
            valueNodes[id] = src;
            add_mono(g, id, src);
        }
        else if (type == "Vibrato") {
            if (!pp) throw std::runtime_error("Vibrato requires params");
            const auto& p = *pp;
            auto vib = std::make_shared<Vibrato>(sampleRate,
                p.value("speed", 5.0f), p.value("depth", 0.02f),
                p.value("attack", 0.3f), p.value("threshold", 0.0f),
                p.value("speedVar", 0.0f), p.value("depthVar", 0.0f),
                p.value("zeroCrossTendency", 1.0f),
                seed.value_or(0xF1B0'0000u));
            wire_params_generic(*vib, p, valueNodes, &usage);
            valueNodes[id] = vib;
        }
        else if (type == "BWLowpassFilter" || type == "BWHighpassFilter" || type == "BWBandpassFilter") {
            int sections = pp ? pp->value("sections", 2) : 2;
            std::shared_ptr<ValueSource> src;
            if (type == "BWLowpassFilter")
                src = std::make_shared<BWLowpassFilter>(sampleRate, sections);
            else if (type == "BWHighpassFilter")
                src = std::make_shared<BWHighpassFilter>(sampleRate, sections);
            else
                src = std::make_shared<BWBandpassFilter>(sampleRate, sections);
            if (pp) {
                wire_params_generic(*src, *pp, valueNodes, &usage);
                // JSON uses "cutoff" alias for single-band filters
                if (pp->contains("cutoff"))
                    src->set_param("cutoffFreq", resolve_param(pp->at("cutoff"), valueNodes, &usage));
            }
            valueNodes[id] = src;
            add_mono(g, id, src);
        }
        // ---- Formant collection types (need formantNodes map) ----
        else if (type == "FormantSpectrum") {
            auto spec = std::make_shared<FormantSpectrum>();
            if (pp && pp->contains("formants")) {
                for (auto& fref : (*pp)["formants"]) {
                    std::string fid = fref.at("ref").get<std::string>();
                    auto it = formantNodes.find(fid);
                    if (it == formantNodes.end()) throw std::runtime_error("Unresolved formant ref: " + fid);
                    spec->formants.push_back(it->second);
                }
            }
            formantNodes[id] = spec; valueNodes[id] = spec;
        }
        else if (type == "FixedSpectrum") {
            if (!pp || !pp->contains("gains"))
                throw std::runtime_error("FixedSpectrum requires params.gains");
            auto fs = std::make_shared<FixedSpectrum>((*pp)["gains"].get<std::vector<float>>());
            formantNodes[id] = fs; valueNodes[id] = fs;
        }
        else if (type == "FormantSequence") {
            auto fseq = std::make_shared<FormantSequence>();
            if (pp) {
                if (pp->contains("formants")) {
                    for (auto& fref : (*pp)["formants"]) {
                        std::string fid = fref.at("ref").get<std::string>();
                        auto it = formantNodes.find(fid);
                        if (it == formantNodes.end()) throw std::runtime_error("Unresolved formant ref: " + fid);
                        fseq->formants.push_back(it->second);
                    }
                }
                wire_params_generic(*fseq, *pp, valueNodes, &usage);
            }
            formantNodes[id] = fseq; valueNodes[id] = fseq;
        }
        // ---- AdditiveSource2 (partial modes + per-partial envelopes) ----
        else if (type == "AdditiveSource2") {
            auto as2 = std::make_shared<AdditiveSource2>(sampleRate, seed.value_or(0xADD3'0000u));
            if (pp) {
                const auto& p = *pp;
                wire_params_generic(*as2, p, valueNodes, &usage);
                std::string pMode = p.value("partialMode", std::string("default"));
                if (pMode == "default") {
                    as2->set_default_partials(p.value("partialCount", 500));
                } else if (pMode == "explicit") {
                    as2->set_partials(p.at("partials").get<std::vector<float>>(),
                                      p.at("amplitudes").get<std::vector<float>>());
                } else if (pMode == "evolving") {
                    as2->set_partials(
                        p.at("startPartials").get<std::vector<float>>(),
                        p.at("startAmplitudes").get<std::vector<float>>(),
                        p.at("endPartials").get<std::vector<float>>(),
                        p.at("endAmplitudes").get<std::vector<float>>());
                }
                auto parse_filter = [](const std::string& s) -> AdditiveSource2::PartialFilter {
                    if (s == "even")     return AdditiveSource2::PartialFilter::Even;
                    if (s == "odd")      return AdditiveSource2::PartialFilter::Odd;
                    if (s == "mult3")    return AdditiveSource2::PartialFilter::Mult3;
                    if (s == "nonMult3") return AdditiveSource2::PartialFilter::NonMult3;
                    return AdditiveSource2::PartialFilter::All;
                };
                for (const char* key : {"amplEnvelopes", "freqEnvelopes"}) {
                    if (p.contains(key)) {
                        for (const auto& ae : p[key]) {
                            auto env = resolve_param(ae.at("envelope"), valueNodes, &usage);
                            auto filt = parse_filter(ae.value("filter", std::string("all")));
                            int from = ae.value("from", 1), to = ae.value("to", 500);
                            if (std::string(key) == "amplEnvelopes")
                                as2->assign_ampl_envelope(std::move(env), filt, from, to);
                            else
                                as2->assign_freq_envelope(std::move(env), filt, from, to);
                        }
                    }
                }
            }
            valueNodes[id] = as2;
            add_mono(g, id, as2);
        }
        // ---- WavetableSource (evolution types) ----
        else if (type == "WavetableSource") {
            auto wt = std::make_shared<WavetableSource>(sampleRate, seed.value_or(0xC0FFEEu));
            if (pp) {
                const auto& p = *pp;
                wire_params_generic(*wt, p, valueNodes, &usage);
                wt->set_interpolate(p.value("interpolate", false));
                // Legacy patches have evolution as a string ("pluck", "averaging", "target").
                // New UI patches wire evolution as a ref (handled by wire_params_generic above).
                std::string evoType = "none";
                if (p.contains("evolution") && p.at("evolution").is_string())
                    evoType = p.at("evolution").get<std::string>();
                if (evoType == "pluck") {
                    wt->set_evolution(std::make_unique<PluckEvolution>(
                        p.value("muting", 0.0f), uint32_t(p.value("evolutionSeed", 42))));
                } else if (evoType == "averaging") {
                    wt->set_evolution(std::make_unique<AveragingEvolution>(
                        p.value("sampleCount", 2.0f), p.value("speed", 1.0f),
                        p.value("decayFactor", 0.996f), p.value("leading", false),
                        p.value("autoAdjust", false), uint32_t(p.value("evolutionSeed", 42))));
                } else if (evoType == "target") {
                    std::vector<float> target;
                    if (p.contains("targetWave")) {
                        target = p["targetWave"].get<std::vector<float>>();
                    } else if (p.contains("targetPartials")) {
                        auto partials = p["targetPartials"].get<std::vector<float>>();
                        int tLen = p.value("targetLength", 1024);
                        target.resize(tLen, 0.0f);
                        constexpr float TAU = 2.0f * 3.14159265358979323846f;
                        for (int i = 0; i < tLen; ++i) {
                            float pos = float(i) / float(tLen);
                            for (int h = 0; h < int(partials.size()); ++h)
                                target[i] += partials[h] * std::sin(pos * TAU * float(h + 1));
                        }
                        float peak = 0.0f;
                        for (float v : target) peak = std::max(peak, std::fabs(v));
                        if (peak > 0.0f) for (float& v : target) v /= peak;
                    }
                    wt->set_evolution(std::make_unique<TargetEvolution>(
                        p.value("morphRate", 0.001f), p.value("holdCycles", 3),
                        p.value("decayFactor", 0.998f), std::move(target),
                        uint32_t(p.value("evolutionSeed", 42))));
                }
            }
            valueNodes[id] = wt;
            add_mono(g, id, wt);
        }
        // ---- HybridKSSource (config params) ----
        else if (type == "HybridKSSource") {
            auto hks = std::make_shared<HybridKSSource>(sampleRate, seed.value_or(0xBEEF'C0DEu));
            if (pp) {
                const auto& p = *pp;
                wire_params_generic(*hks, p, valueNodes, &usage);
                hks->set_hold_cycles(p.value("holdCycles", 5));
                hks->set_morph_duration(p.value("morphDuration", 0.5f));
                hks->set_num_partials(p.value("numPartials", 30));
                if (p.contains("targetPartials"))
                    hks->set_target_partials(p["targetPartials"].get<std::vector<float>>());
            }
            valueNodes[id] = hks;
            add_mono(g, id, hks);
        }
        // ---- MultiplexSource: fan template subgraph into N instances ----
        else if (type == "MultiplexSource") {
            auto mux = std::make_shared<MultiplexSource>();
            if (pp) {
                // Apply "count" config via generic path.
                wire_params_generic(*mux, *pp, valueNodes, &usage);

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
                    int sr = sampleRate;
                    MultiplexSource::InstanceBuilder builder =
                        [subtreeStr, baseSeed, sr](int instanceIdx) {
                            uint32_t perturbation =
                                baseSeed ^ (uint32_t(instanceIdx) * 0x9E3779B9u);
                            return build_subgraph_with_seed_perturbation(
                                subtreeStr, perturbation, sr);
                        };

                    mux->set_template(subtreeStr, baseSeed, std::move(builder));
                }
            }
            valueNodes[id] = mux;
            add_mono(g, id, mux);
        }
        // =================================================================
        // Generic path: registry create + set_param + optional configurator
        // =================================================================
        else if (reg.has(type)) {
            auto src = reg.create(type, sampleRate, seed);
            if (pp) {
                wire_params_generic(*src, *pp, valueNodes, &usage);
                auto* configurator = reg.get_configurator(type);
                if (configurator) (*configurator)(*src, *pp, resolveFn);
            }
            valueNodes[id] = src;
            add_formant(g, id, src);
            add_mono(g, id, src);
        }
        // SoundChannel and StereoMixer handled by caller
        else if (type != "SoundChannel" && type != "StereoMixer") {
            throw std::runtime_error("Unknown node type: " + type);
        }

    } // end for each node

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
// deterministically from a shared base seed. Returns {root, valueNodes}
// so external paramMap fan-out code can reach into the clone's nodes
// by id.
static std::pair<std::shared_ptr<ValueSource>,
                 std::unordered_map<std::string, std::shared_ptr<ValueSource>>>
build_subgraph_with_seed_perturbation(
    const std::string& subtreeJsonStr,
    uint32_t seedPerturbation,
    int sampleRate)
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

    auto g = build_graph(nodeMap, nodeOrder, sampleRate);

    std::string outputId = subtree["output"].get<std::string>();
    auto it = g.valueNodes.find(outputId);
    if (it == g.valueNodes.end())
        throw std::runtime_error("build_subgraph: output '" + outputId + "' not found");
    return {it->second, std::move(g.valueNodes)};
}


// ---------------------------------------------------------------------------
// Resolve paramMap: "frequency" → target(s).
// Target can be a single string ("fmBody.frequency") or an array of strings
// (["fmBody.frequency", "fmTine.frequency"]) for dual/N-stack instruments
// where one logical name retunes several graph edges. Returns one entry per
// name with a vector of ParamSlots — size 1 in the common case.
// ---------------------------------------------------------------------------

static std::unordered_map<std::string, std::vector<PitchedInstrument::ParamSlot>>
resolve_param_map(
    const json& paramMapJson,
    const GraphResult& g,
    const std::unordered_map<std::string, json>& /*nodeMap*/)
{
    std::unordered_map<std::string, std::vector<PitchedInstrument::ParamSlot>> result;

    auto resolve_one_target = [&](const std::string& name, const std::string& target,
                                  std::vector<std::pair<float, float>> curve = {},
                                  std::vector<std::pair<float, float>> vcurve = {},
                                  bool loglog = false) {
        std::string nodeId, paramName;
        auto dot = target.find('.');
        if (dot != std::string::npos) {
            nodeId = target.substr(0, dot);
            paramName = target.substr(dot + 1);
        } else {
            nodeId = target;
            paramName = "frequency";  // default for backward compat
        }
        auto nodeIt = g.valueNodes.find(nodeId);
        if (nodeIt == g.valueNodes.end())
            throw std::runtime_error("paramMap: unknown node '" + nodeId + "'");
        auto paramSrc = nodeIt->second->get_param(paramName);
        if (paramSrc) {
            auto cs = std::dynamic_pointer_cast<ConstantSource>(paramSrc);
            if (!cs)
                throw std::runtime_error("paramMap: '" + target + "' is not a ConstantSource (it's wired to a ref)");
            PitchedInstrument::ParamSlot slot{nodeIt->second, paramName, cs, nodeId};
            slot.curve = std::move(curve);
            slot.vcurve = std::move(vcurve);
            slot.loglog = loglog;
            result[name].push_back(std::move(slot));
            return;
        }
        // Not a pluggable param — maybe a scalar config (motion-layer knobs,
        // Envelope sustainLevel, ...). Config slots deliver the curve-mapped
        // value via set_config at note-on.
        for (const auto& desc : nodeIt->second->config_descriptors()) {
            if (paramName == desc.name) {
                PitchedInstrument::ParamSlot slot{nodeIt->second, paramName, nullptr, nodeId};
                slot.isConfig = true;
                slot.curve = std::move(curve);
                slot.vcurve = std::move(vcurve);
                slot.loglog = loglog;
                result[name].push_back(std::move(slot));
                return;
            }
        }
        throw std::runtime_error("paramMap: cannot resolve '" + target + "' (neither param nor config)");
    };

    // Object entry: { "target": "node.paramOrConfig", "curve": [[hz, value], ...],
    //                 "vcurve": [[velocity, multiplier], ...] }
    // — the ParameterMapping "Function" port: frequency → curve(frequency)
    // (log-hz linear-value interpolation, clamped at the end breakpoints).
    // vcurve composes multiplicatively: value = curve(freq) * vcurve(velocity)
    // (linear interpolation in velocity; the AF-style "Veloc" mod input).
    auto resolve_entry = [&](const std::string& name, const json& t) {
        if (t.is_string()) { resolve_one_target(name, t.get<std::string>()); return; }
        if (t.is_object() && t.contains("target")) {
            std::vector<std::pair<float, float>> curve;
            if (t.contains("curve")) {
                for (const auto& bp : t.at("curve"))
                    curve.emplace_back(bp.at(0).get<float>(), bp.at(1).get<float>());
                if (curve.size() < 2)
                    throw std::runtime_error("paramMap: curve needs >= 2 breakpoints");
            }
            std::vector<std::pair<float, float>> vcurve;
            if (t.contains("vcurve")) {
                for (const auto& bp : t.at("vcurve"))
                    vcurve.emplace_back(bp.at(0).get<float>(), bp.at(1).get<float>());
                if (vcurve.size() < 2)
                    throw std::runtime_error("paramMap: vcurve needs >= 2 breakpoints");
            }
            // "interp": "loglog" — interpolate log(value) over log(freq):
            // a two-point curve is then EXACTLY y = k*f^n (tracking /
            // reciprocal laws) instead of a dense piecewise approximation.
            const bool loglog = t.value("interp", std::string()) == "loglog";
            resolve_one_target(name, t.at("target").get<std::string>(),
                               std::move(curve), std::move(vcurve), loglog);
            return;
        }
        throw std::runtime_error("paramMap: '" + name + "' entries must be strings or {target, curve} objects");
    };

    for (auto& [name, targetJson] : paramMapJson.items()) {
        if (targetJson.is_array()) {
            for (const auto& t : targetJson) resolve_entry(name, t);
        } else {
            resolve_entry(name, targetJson);
        }
    }

    return result;
}

// ---------------------------------------------------------------------------
// Patch loading
// ---------------------------------------------------------------------------

Patch load_patch_file(const std::string& path)
{
    json root = json::parse(slurp(path));

    Patch patch;

    int sampleRate = root.value("sampleRate", 48000);
    patch.sampleRate = sampleRate;

    if (root.contains("seconds")) {
        double seconds = root["seconds"].get<double>();
        patch.frames = static_cast<int>(std::lround(seconds * sampleRate));
    } else {
        patch.frames = root.value("frames", sampleRate * 5);
    }

    const json& graph = root.at("graph");
    const json& nodes = graph.at("nodes");
    std::string outputId = graph.at("output").get<std::string>();

    // Index nodes by id
    std::unordered_map<std::string, json> nodeMap;
    std::vector<std::string> nodeOrder;
    for (const auto& n : nodes) {
        std::string id = n.at("id").get<std::string>();
        nodeMap.emplace(id, n);
        nodeOrder.push_back(id);
    }

    // -------------------------------------------------------------------
    // PitchedInstrument mode: build voices, schedule notes
    // -------------------------------------------------------------------
    if (root.contains("instrument")) {
        const auto& instJson = root["instrument"];
        int polyphony = instJson.value("polyphony", 4);

        auto inst = std::make_unique<PitchedInstrument>();
        inst->sampleRate = sampleRate;
        // Pre-clip master gain (applied before the soft_clip peak guard, so it
        // is the right knob for keeping hot chains out of the clipper).
        inst->volume = instJson.value("volume", 1.0f);
        if (instJson.value("release", 0.0f) != 0.0f)
            std::fprintf(stderr, "[loader] instrument.release retired "
                         "(note-contained sound 2026-08-13); ignored\n");

        // Build voice pool: N independent graph instances
        for (int v = 0; v < polyphony; ++v) {
            auto g = build_graph(nodeMap, nodeOrder, sampleRate);
            PitchedInstrument::VoiceGraph vg;

            // Find the top-level source for this voice
            auto srcIt = g.valueNodes.find(outputId);
            if (srcIt == g.valueNodes.end())
                throw std::runtime_error("instrument: output node '" + outputId + "' not found");
            vg.source = srcIt->second;

            // If the voice's output is a MultiplexSource, capture it so play_note
            // can fan paramMap changes into each clone.
            vg.topMultiplex = std::dynamic_pointer_cast<MultiplexSource>(vg.source);

            // Resolve param map for this voice
            if (instJson.contains("paramMap"))
                vg.params = resolve_param_map(instJson["paramMap"], g, nodeMap);

            inst->voicePool.push_back(std::move(vg));
        }

        // Schedule notes from score section. Route through NotePerformer so
        // slide-run bundling works. BPM=60 makes beats == seconds, matching
        // the score section's "time"/"duration" fields directly.
        if (root.contains("score")) {
            NotePerformer performer;
            const float bpm = 60.0f;
            for (const auto& noteJson : root["score"]) {
                float note     = noteJson.at("note").get<float>();
                float velocity = noteJson.value("velocity", 0.8f);
                float duration = noteJson.at("duration").get<float>();
                float start    = noteJson.value("time", 0.0f);

                Articulation art = articulations::Default{};
                if (noteJson.contains("articulation"))
                    from_json(noteJson.at("articulation"), art);

                Ornament orn{};
                if (noteJson.contains("ornament"))
                    from_json(noteJson.at("ornament"), orn);

                Note n{note, velocity, duration, art, orn};
                performer.perform_note(n, start, bpm, *inst);
            }
            performer.conclude(bpm, *inst);
        }

        // Compute total duration from score
        if (root.contains("score")) {
            double maxEnd = 0;
            for (const auto& noteJson : root["score"]) {
                double t = noteJson.value("time", 0.0);
                double d = noteJson.at("duration").get<double>();
                maxEnd = std::max(maxEnd, t + d);
            }
            // Note-contained sound (2026-08-13): all sound ends by the last
            // note's duration end, so the render is exactly the score length.
            patch.frames = int(std::lround(maxEnd * sampleRate));
        }

        // Wire instrument into mixer
        auto mixer = std::make_unique<StereoMixer>();

        Channel ch;
        ch.source = std::move(inst);
        ch.volume = std::make_shared<ConstantSource>(1.0f);
        ch.pan    = std::make_shared<ConstantSource>(0.0f);

        // Override from graph's mixer node if present
        auto mixIt = nodeMap.find("mix");
        if (mixIt != nodeMap.end() && mixIt->second.contains("params")) {
            // Build a throwaway graph just to resolve mixer/channel params
            auto gMix = build_graph(nodeMap, nodeOrder, sampleRate);
            const auto& mp = mixIt->second["params"];
            mixer->gainL = resolve_param_or(mp, "gainL", 1.0f, gMix.valueNodes);
            mixer->gainR = resolve_param_or(mp, "gainR", 1.0f, gMix.valueNodes);
        }

        mixer->channels.push_back(std::move(ch));
        patch.mixer = std::move(mixer);
        return patch;
    }

    // -------------------------------------------------------------------
    // Standard mode (no instrument): single graph instance
    // -------------------------------------------------------------------
    auto g = build_graph(nodeMap, nodeOrder, sampleRate);
    auto& valueNodes = g.valueNodes;
    auto& monoNodes  = g.monoNodes;

    auto outIt = nodeMap.find(outputId);
    if (outIt == nodeMap.end())
        throw std::runtime_error("graph.output not found");

    auto mixer = std::make_unique<StereoMixer>();

    // A bare mono source as graph.output used to be a hard error, which made
    // seven patches unrenderable from the CLI while working fine in the UI
    // (CombineTest, ks_morph_*, mux_*). The instrument path above already
    // auto-wraps a bare output in a unity channel; this does the same for the
    // standard path so the two agree. Unity volume + centre pan matches the
    // instrument path exactly rather than inventing a second convention —
    // note that centre pan is equal-power, so this inherits the -3dB
    // CLI-vs-UI level difference that is backlog item 3d.
    if (outIt->second.at("type").get<std::string>() != "StereoMixer") {
        auto monoIt = monoNodes.find(outputId);
        if (monoIt == monoNodes.end())
            throw std::runtime_error(
                "graph.output must be a StereoMixer or a mono-renderable source: " + outputId);

        Channel ch;
        ch.volume = std::make_shared<ConstantSource>(1.0f);
        ch.pan    = std::make_shared<ConstantSource>(0.0f);
        ch.source = std::move(monoIt->second);
        mixer->channels.push_back(std::move(ch));
        patch.mixer = std::move(mixer);
        return patch;
    }

    if (outIt->second.contains("params")) {
        const auto& params = outIt->second["params"];
        mixer->gainL = resolve_param_or(params, "gainL", 1.0f, valueNodes);
        mixer->gainR = resolve_param_or(params, "gainR", 1.0f, valueNodes);
    }

    const auto& chIds = outIt->second.at("inputs").at("channels");

    for (const auto& chIdVal : chIds) {
        std::string chId = chIdVal.get<std::string>();
        const auto& chNode = nodeMap.at(chId);

        if (chNode.at("type").get<std::string>() != "SoundChannel")
            throw std::runtime_error("Node is not SoundChannel: " + chId);

        Channel ch;

        if (chNode.contains("params")) {
            const auto& p = chNode["params"];
            ch.volume = resolve_param_or(p, "volume", 1.0f, valueNodes);
            ch.pan    = resolve_param_or(p, "pan",    0.0f, valueNodes);
        } else {
            ch.volume = std::make_shared<ConstantSource>(1.0f);
            ch.pan    = std::make_shared<ConstantSource>(0.0f);
        }

        std::string srcId =
            chNode.at("inputs").at("source").get<std::string>();

        auto monoIt = monoNodes.find(srcId);
        if (monoIt == monoNodes.end())
            throw std::runtime_error("No mono source for: " + srcId);

        ch.source = std::move(monoIt->second);
        mixer->channels.push_back(std::move(ch));
    }

    patch.mixer = std::move(mixer);
    return patch;
}

// ---------------------------------------------------------------------------
// Load just the Instrument (no score, no mixer) for external use
// ---------------------------------------------------------------------------

InstrumentPatch load_instrument_patch(const std::string& path,
                                      int minPolyphony)
{
    json root = json::parse(slurp(path));

    int sampleRate = root.value("sampleRate", 48000);

    if (!root.contains("instrument"))
        throw std::runtime_error("Patch has no 'instrument' section: " + path);

    const json& graph = root.at("graph");
    const json& nodes = graph.at("nodes");
    std::string outputId = graph.at("output").get<std::string>();

    std::unordered_map<std::string, json> nodeMap;
    std::vector<std::string> nodeOrder;
    for (const auto& n : nodes) {
        std::string id = n.at("id").get<std::string>();
        nodeMap.emplace(id, n);
        nodeOrder.push_back(id);
    }

    const auto& instJson = root["instrument"];
    int polyphony = std::max(instJson.value("polyphony", 4), minPolyphony);

    auto inst = std::make_unique<PitchedInstrument>();
    inst->sampleRate = sampleRate;
    inst->volume = instJson.value("volume", 1.0f);
    if (instJson.value("release", 0.0f) != 0.0f)
        std::fprintf(stderr, "[loader] instrument.release retired "
                     "(note-contained sound 2026-08-13); ignored\n");

    for (int v = 0; v < polyphony; ++v) {
        auto g = build_graph(nodeMap, nodeOrder, sampleRate);
        PitchedInstrument::VoiceGraph vg;

        auto srcIt = g.valueNodes.find(outputId);
        if (srcIt == g.valueNodes.end())
            throw std::runtime_error("instrument: output node '" + outputId + "' not found");
        vg.source = srcIt->second;

        vg.topMultiplex = std::dynamic_pointer_cast<MultiplexSource>(vg.source);

        if (instJson.contains("paramMap"))
            vg.params = resolve_param_map(instJson["paramMap"], g, nodeMap);

        inst->voicePool.push_back(std::move(vg));
    }

    return {std::move(inst), sampleRate};
}

} // namespace mforce
