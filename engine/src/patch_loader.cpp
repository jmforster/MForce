#include "mforce/render/patch_loader.h"
#include "mforce/core/source_registry.h"
#include "mforce/core/dsp_value_source.h"
#include "mforce/core/dsp_wave_source.h"
#include "mforce/core/range_source.h"
#include "mforce/core/var_source.h"
#include "mforce/core/envelope.h"
#include "mforce/render/instrument.h"
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
#include "mforce/filter/filters.h"
#include "mforce/filter/vibrato.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

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
    void render(float* out, int frames) override {
        src->prepare(frames);
        for (int i = 0; i < frames; ++i) out[i] = src->next();
    }
};

struct ValueSourceMono final : MonoSource {
    std::shared_ptr<ValueSource> src;
    explicit ValueSourceMono(std::shared_ptr<ValueSource> s) : src(std::move(s)) {}
    void render(float* out, int frames) override {
        src->prepare(frames);
        for (int i = 0; i < frames; ++i) out[i] = src->next();
    }
};

// ---------------------------------------------------------------------------
// Param resolution: number -> ConstantSource, {"ref":"id"} -> lookup
// ---------------------------------------------------------------------------

static std::shared_ptr<ValueSource> resolve_param(
    const json& val,
    const std::unordered_map<std::string, std::shared_ptr<ValueSource>>& valueNodes)
{
    if (val.is_number())
        return std::make_shared<ConstantSource>(val.get<float>());

    if (val.is_object() && val.contains("ref")) {
        std::string refId = val.at("ref").get<std::string>();
        auto it = valueNodes.find(refId);
        if (it == valueNodes.end())
            throw std::runtime_error("Unresolved node ref: " + refId);
        return it->second;
    }

    throw std::runtime_error("Param must be a number or {\"ref\":\"...\"}");
}

static std::shared_ptr<ValueSource> resolve_param_or(
    const json& params, const char* key, float defaultVal,
    const std::unordered_map<std::string, std::shared_ptr<ValueSource>>& valueNodes)
{
    if (!params.contains(key))
        return std::make_shared<ConstantSource>(defaultVal);
    return resolve_param(params.at(key), valueNodes);
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
    const std::unordered_map<std::string, std::shared_ptr<ValueSource>>& valueNodes)
{
    for (const auto& desc : src.input_descriptors()) {
        if (params.contains(desc.name))
            src.set_param(desc.name, resolve_param(params.at(desc.name), valueNodes));
    }
    for (const auto& desc : src.param_descriptors()) {
        if (params.contains(desc.name))
            src.set_param(desc.name, resolve_param(params.at(desc.name), valueNodes));
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

    // Resolve function for configurators
    ResolveParamFn resolveFn = [&](const json& val) {
        return resolve_param(val, valueNodes);
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
                resolve_param_or(p, "val",    0.0f, valueNodes),
                resolve_param_or(p, "var",    0.0f, valueNodes),
                resolve_param_or(p, "varPct", 0.0f, valueNodes), absolute);
            valueNodes[id] = src;
        }
        else if (type == "RangeSource") {
            if (!pp) throw std::runtime_error("RangeSource requires params");
            const auto& p = *pp;
            bool normalized = p.value("normalized", false);
            auto src = std::make_shared<RangeSource>(
                resolve_param_or(p, "min", 0.0f, valueNodes),
                resolve_param_or(p, "max", 1.0f, valueNodes),
                resolve_param_or(p, "var", 0.0f, valueNodes), normalized);
            valueNodes[id] = src;
        }
        else if (type == "Envelope") {
            if (!pp) throw std::runtime_error("Envelope requires params");
            const auto& p = *pp;
            std::string preset = p.value("preset", std::string("ar"));
            std::shared_ptr<Envelope> env;
            if (preset == "ar") {
                env = std::make_shared<Envelope>(Envelope::make_ar(sampleRate,
                    p.value("attack", 0.2f), p.value("attackMin", 0.0f), p.value("attackMax", 1.0f)));
            } else if (preset == "adsr") {
                env = std::make_shared<Envelope>(Envelope::make_adsr(sampleRate,
                    p.value("attack", 0.2f), p.value("decay", 0.1f),
                    p.value("sustainLevel", 0.7f), p.value("release", 0.0f)));
            } else {
                throw std::runtime_error("Unknown envelope preset: " + preset);
            }
            valueNodes[id] = env;
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
            if (pp) wire_params_generic(*src, *pp, valueNodes);
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
                seed.value_or(0xF1B0'0000u));
            wire_params_generic(*vib, p, valueNodes);
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
                wire_params_generic(*src, *pp, valueNodes);
                // JSON uses "cutoff" alias for single-band filters
                if (pp->contains("cutoff"))
                    src->set_param("cutoffFreq", resolve_param(pp->at("cutoff"), valueNodes));
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
                wire_params_generic(*fseq, *pp, valueNodes);
            }
            formantNodes[id] = fseq; valueNodes[id] = fseq;
        }
        // ---- FullAdditiveSource / AdditiveSource (thin source + Partials) ----
        else if (type == "FullAdditiveSource") {
            auto fas = std::make_shared<FullAdditiveSource>(sampleRate, seed.value_or(0xADD2'0000u));
            if (pp) {
                const auto& p = *pp;
                // Wire frequency/amplitude/phase generically
                wire_params_generic(*fas, p, valueNodes);

                // Create the appropriate Partials subclass based on mode
                std::string mode = p.value("mode", std::string("full"));
                std::shared_ptr<Partials> partials;

                if (mode == "full") {
                    auto fp = std::make_shared<FullPartials>(seed.value_or(0xADD2'0000u));
                    fp->setup(p.value("maxPartials", 30), p.value("minMult", 1),
                        p.value("evenWeight1", 1.0f), p.value("evenWeight2", 1.0f),
                        p.value("oddWeight1",  1.0f), p.value("oddWeight2",  1.0f),
                        p.value("unitPO1", 0.0f), p.value("unitPO2", 0.0f));
                    partials = fp;
                } else if (mode == "sequence") {
                    auto sp = std::make_shared<SequencePartials>(seed.value_or(0xADD2'0000u));
                    sp->setup(p.value("maxPartials", 30),
                        p.value("minMult1", 1.0f), p.value("minMult2", 1.0f),
                        p.value("incr1", 1.0f), p.value("incr2", 1.0f),
                        p.value("unitPO1", 0.0f), p.value("unitPO2", 0.0f));
                    partials = sp;
                } else if (mode == "explicit") {
                    auto ep = std::make_shared<ExplicitPartials>(seed.value_or(0xADD2'0000u));
                    ep->setup(
                        p.at("mult1").get<std::vector<float>>(), p.at("mult2").get<std::vector<float>>(),
                        p.at("ampl1").get<std::vector<float>>(), p.at("ampl2").get<std::vector<float>>(),
                        p.value("unitPO1", 0.0f), p.value("unitPO2", 0.0f));
                    partials = ep;
                } else {
                    throw std::runtime_error("Unknown FullAdditiveSource mode: " + mode);
                }

                // Configure partials: rolloff, detune, envelopes
                partials->set_ro(p.value("rolloff1", 1.0f), p.value("rolloff2", 1.0f));
                partials->set_dt(p.value("detune1",  0.0f), p.value("detune2",  0.0f));

                // Wire envelope params to the partials object
                if (p.contains("multEnv")) partials->set_param("multEnv", resolve_param(p.at("multEnv"), valueNodes));
                if (p.contains("amplEnv")) partials->set_param("amplEnv", resolve_param(p.at("amplEnv"), valueNodes));
                if (p.contains("poEnv"))   partials->set_param("poEnv",   resolve_param(p.at("poEnv"),   valueNodes));
                if (p.contains("roEnv"))   partials->set_param("roEnv",   resolve_param(p.at("roEnv"),   valueNodes));
                if (p.contains("dtEnv"))   partials->set_param("dtEnv",   resolve_param(p.at("dtEnv"),   valueNodes));

                // Expand rule
                if (p.contains("expandRule")) {
                    const auto& er = p["expandRule"];
                    ExpandRule rule;
                    rule.count = er.value("count", 2); rule.recurse = er.value("recurse", 0);
                    rule.spacing1 = er.value("spacing1", 0.5f); rule.spacing2 = er.value("spacing2", 0.5f);
                    rule.dt1 = er.value("dt1", 0.01f); rule.dt2 = er.value("dt2", 0.01f);
                    rule.loPct1 = er.value("loPct1", 0.1f); rule.loPct2 = er.value("loPct2", 0.1f);
                    rule.power1 = er.value("power1", 1.0f); rule.power2 = er.value("power2", 1.0f);
                    rule.po1 = er.value("po1", 0.0f); rule.po2 = er.value("po2", 0.0f);
                    partials->set_expand_rule(rule);
                }

                // Wire partials into the thin source
                fas->set_partials(partials);

                // Formant
                if (p.contains("formant")) {
                    std::string fid = p["formant"].at("ref").get<std::string>();
                    auto fit = formantNodes.find(fid);
                    if (fit == formantNodes.end()) throw std::runtime_error("Unresolved formant spectrum ref: " + fid);
                    fas->set_formant(fit->second, resolve_param_or(p, "formantWeight", 0.0f, valueNodes));
                }

                // Store partials in valueNodes so it can be referenced
                valueNodes[id + "_partials"] = partials;
            }
            valueNodes[id] = fas;
            add_mono(g, id, fas);
        }
        // ---- AdditiveSource2 (partial modes + per-partial envelopes) ----
        else if (type == "AdditiveSource2") {
            auto as2 = std::make_shared<AdditiveSource2>(sampleRate, seed.value_or(0xADD3'0000u));
            if (pp) {
                const auto& p = *pp;
                wire_params_generic(*as2, p, valueNodes);
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
                            auto env = resolve_param(ae.at("envelope"), valueNodes);
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
                wire_params_generic(*wt, p, valueNodes);
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
                wire_params_generic(*hks, p, valueNodes);
                hks->set_hold_cycles(p.value("holdCycles", 5));
                hks->set_morph_duration(p.value("morphDuration", 0.5f));
                hks->set_num_partials(p.value("numPartials", 30));
                if (p.contains("targetPartials"))
                    hks->set_target_partials(p["targetPartials"].get<std::vector<float>>());
            }
            valueNodes[id] = hks;
            add_mono(g, id, hks);
        }
        // =================================================================
        // Generic path: registry create + set_param + optional configurator
        // =================================================================
        else if (reg.has(type)) {
            auto src = reg.create(type, sampleRate, seed);
            if (pp) {
                wire_params_generic(*src, *pp, valueNodes);
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
// Resolve paramMap: "frequency" → "rn1.frequency" → shared_ptr<ConstantSource>
// ---------------------------------------------------------------------------

static std::unordered_map<std::string, std::shared_ptr<ConstantSource>>
resolve_param_map(
    const json& paramMapJson,
    const GraphResult& g,
    const std::unordered_map<std::string, json>& /*nodeMap*/)
{
    std::unordered_map<std::string, std::shared_ptr<ConstantSource>> result;

    for (auto& [name, targetJson] : paramMapJson.items()) {
        std::string target = targetJson.get<std::string>();

        // Parse "nodeId.paramName" or just "nodeId" (defaults to param "value")
        std::string nodeId, paramName;
        auto dot = target.find('.');
        if (dot != std::string::npos) {
            nodeId = target.substr(0, dot);
            paramName = target.substr(dot + 1);
        } else {
            nodeId = target;
            paramName = "frequency";  // default for backward compat
        }

        // Find the ValueSource wired as that param on that node.
        auto nodeIt = g.valueNodes.find(nodeId);
        if (nodeIt == g.valueNodes.end())
            throw std::runtime_error("paramMap: unknown node '" + nodeId + "'");

        // Generic: use get_param() — works for any self-describing type
        auto paramSrc = nodeIt->second->get_param(paramName);
        if (!paramSrc)
            throw std::runtime_error("paramMap: cannot resolve '" + target + "'");

        auto cs = std::dynamic_pointer_cast<ConstantSource>(paramSrc);
        if (!cs)
            throw std::runtime_error("paramMap: '" + target + "' is not a ConstantSource (it's wired to a ref)");

        result[name] = cs;
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

        // Build voice pool: N independent graph instances
        for (int v = 0; v < polyphony; ++v) {
            auto g = build_graph(nodeMap, nodeOrder, sampleRate);
            PitchedInstrument::VoiceGraph vg;

            // Find the top-level source for this voice
            auto srcIt = g.valueNodes.find(outputId);
            if (srcIt == g.valueNodes.end())
                throw std::runtime_error("instrument: output node '" + outputId + "' not found");
            vg.source = srcIt->second;

            // Resolve param map for this voice
            if (instJson.contains("paramMap"))
                vg.params = resolve_param_map(instJson["paramMap"], g, nodeMap);

            inst->voicePool.push_back(std::move(vg));
        }

        // Schedule notes from score section
        if (root.contains("score")) {
            for (const auto& noteJson : root["score"]) {
                float note     = noteJson.at("note").get<float>();
                float velocity = noteJson.value("velocity", 0.8f);
                float duration = noteJson.at("duration").get<float>();
                float start    = noteJson.value("time", 0.0f);
                inst->play_note(note, velocity, duration, start);
            }
        }

        // Compute total duration from score
        if (root.contains("score")) {
            double maxEnd = 0;
            for (const auto& noteJson : root["score"]) {
                double t = noteJson.value("time", 0.0);
                double d = noteJson.at("duration").get<double>();
                maxEnd = std::max(maxEnd, t + d);
            }
            patch.frames = int(std::lround((maxEnd + 0.5) * sampleRate)); // +0.5s tail
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

    if (outIt->second.at("type").get<std::string>() != "StereoMixer")
        throw std::runtime_error("Only StereoMixer output supported");

    auto mixer = std::make_unique<StereoMixer>();

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

InstrumentPatch load_instrument_patch(const std::string& path)
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
    int polyphony = instJson.value("polyphony", 4);

    auto inst = std::make_unique<PitchedInstrument>();
    inst->sampleRate = sampleRate;

    for (int v = 0; v < polyphony; ++v) {
        auto g = build_graph(nodeMap, nodeOrder, sampleRate);
        PitchedInstrument::VoiceGraph vg;

        auto srcIt = g.valueNodes.find(outputId);
        if (srcIt == g.valueNodes.end())
            throw std::runtime_error("instrument: output node '" + outputId + "' not found");
        vg.source = srcIt->second;

        if (instJson.contains("paramMap"))
            vg.params = resolve_param_map(instJson["paramMap"], g, nodeMap);

        inst->voicePool.push_back(std::move(vg));
    }

    return {std::move(inst), sampleRate};
}

} // namespace mforce
