#include "mforce/patch_loader.h"
#include "mforce/red_noise_source.h"
#include "mforce/sine_source.h"
#include "mforce/saw_source.h"
#include "mforce/triangle_source.h"
#include "mforce/pulse_source.h"
#include "mforce/wavetable_source.h"
#include "mforce/white_noise_source.h"
#include "mforce/range_source.h"
#include "mforce/var_source.h"
#include "mforce/wander_noise_source.h"
#include "mforce/fm_source.h"
#include "mforce/additive_source.h"
#include "mforce/full_additive_source.h"
#include "mforce/additive_source2.h"
#include "mforce/hybrid_ks_source.h"
#include "mforce/combined_source.h"
#include "mforce/filters.h"
#include "mforce/vibrato.h"
#include "mforce/formant.h"
#include "mforce/envelope.h"
#include "mforce/instrument.h"
#include "mforce/dsp_value_source.h"
#include "mforce/dsp_wave_source.h"
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

static GraphResult build_graph(
    const std::unordered_map<std::string, json>& nodeMap,
    const std::vector<std::string>& nodeOrder,
    int sampleRate)
{
    GraphResult g;
    auto& valueNodes  = g.valueNodes;
    auto& formantNodes = g.formantNodes;
    auto& monoNodes   = g.monoNodes;

    for (const auto& id : nodeOrder) {
        const auto& node = nodeMap.at(id);
        std::string type = node.at("type").get<std::string>();

        if (type == "SineSource") {
            auto src = std::make_shared<SineSource>(sampleRate);
            if (node.contains("params")) { auto& p = node["params"];
                src->set_frequency(resolve_param_or(p, "frequency", 440.0f, valueNodes));
                src->set_amplitude(resolve_param_or(p, "amplitude", 1.0f,   valueNodes));
                src->set_phase(    resolve_param_or(p, "phase",     0.0f,   valueNodes));
            }
            valueNodes[id] = src; monoNodes[id] = std::make_unique<WaveSourceMono>(src);
        }
        else if (type == "SawSource") {
            auto src = std::make_shared<SawSource>(sampleRate);
            if (node.contains("params")) { auto& p = node["params"];
                src->set_frequency(resolve_param_or(p, "frequency", 440.0f, valueNodes));
                src->set_amplitude(resolve_param_or(p, "amplitude", 1.0f,   valueNodes));
                src->set_phase(    resolve_param_or(p, "phase",     0.0f,   valueNodes));
            }
            valueNodes[id] = src; monoNodes[id] = std::make_unique<WaveSourceMono>(src);
        }
        else if (type == "TriangleSource") {
            auto src = std::make_shared<TriangleSource>(sampleRate);
            if (node.contains("params")) { auto& p = node["params"];
                src->set_frequency(resolve_param_or(p, "frequency", 440.0f, valueNodes));
                src->set_amplitude(resolve_param_or(p, "amplitude", 1.0f,   valueNodes));
                src->set_phase(    resolve_param_or(p, "phase",     0.0f,   valueNodes));
                src->set_bias(     resolve_param_or(p, "bias",      0.5f,   valueNodes));
            }
            valueNodes[id] = src; monoNodes[id] = std::make_unique<WaveSourceMono>(src);
        }
        else if (type == "PulseSource") {
            auto src = std::make_shared<PulseSource>(sampleRate);
            if (node.contains("params")) { auto& p = node["params"];
                src->set_frequency(  resolve_param_or(p, "frequency",  440.0f, valueNodes));
                src->set_amplitude(  resolve_param_or(p, "amplitude",  1.0f,   valueNodes));
                src->set_phase(      resolve_param_or(p, "phase",      0.0f,   valueNodes));
                src->set_duty_cycle( resolve_param_or(p, "dutyCycle",  0.5f,   valueNodes));
                src->set_bend(       resolve_param_or(p, "bend",       0.0f,   valueNodes));
            }
            valueNodes[id] = src; monoNodes[id] = std::make_unique<WaveSourceMono>(src);
        }
        else if (type == "FMSource") {
            auto src = std::make_shared<FMSource>(sampleRate);
            if (node.contains("params")) { auto& p = node["params"];
                src->set_frequency(    resolve_param_or(p, "frequency",     440.0f, valueNodes));
                src->set_amplitude(    resolve_param_or(p, "amplitude",     1.0f,   valueNodes));
                src->set_phase(        resolve_param_or(p, "phase",         0.0f,   valueNodes));
                src->set_carrier_ratio(resolve_param_or(p, "carrierRatio",  1.0f,   valueNodes));
                src->set_mod_ratio(    resolve_param_or(p, "modRatio",      1.0f,   valueNodes));
                src->set_depth(        resolve_param_or(p, "depth",         1.0f,   valueNodes));
            }
            valueNodes[id] = src; monoNodes[id] = std::make_unique<WaveSourceMono>(src);
        }
        else if (type == "AdditiveSource") {
            uint32_t seed = 0xADD1'0000u;
            if (node.contains("params") && node["params"].contains("seed"))
                seed = static_cast<uint32_t>(node["params"]["seed"].get<int>());

            auto add = std::make_shared<AdditiveSource>(sampleRate, seed);

            if (node.contains("params")) {
                const auto& p = node["params"];
                add->set_frequency(    resolve_param_or(p, "frequency",     440.0f, valueNodes));
                add->set_amplitude(    resolve_param_or(p, "amplitude",     1.0f,   valueNodes));
                add->set_phase(        resolve_param_or(p, "phase",         0.0f,   valueNodes));
                add->set_even_weight(  resolve_param_or(p, "evenWeight",    1.0f,   valueNodes));
                add->set_odd_weight(   resolve_param_or(p, "oddWeight",     1.0f,   valueNodes));
                add->set_rolloff(      resolve_param_or(p, "rolloff",       1.0f,   valueNodes));
                add->set_freq_var_pct( resolve_param_or(p, "freqVarPct",    0.0f,   valueNodes));
                add->set_freq_var_speed(resolve_param_or(p, "freqVarSpeed", 0.0f,   valueNodes));
                add->set_ampl_var_pct( resolve_param_or(p, "amplVarPct",    0.0f,   valueNodes));
                add->set_ampl_var_speed(resolve_param_or(p, "amplVarSpeed", 0.0f,   valueNodes));
            }

            valueNodes[id] = add;
            monoNodes[id]  = std::make_unique<WaveSourceMono>(add);
        }
        else if (type == "Formant") {
            auto fmt = std::make_shared<Formant>();
            if (node.contains("params")) { auto& p = node["params"];
                fmt->frequency = resolve_param_or(p, "frequency", 1000.0f, valueNodes);
                fmt->gain      = resolve_param_or(p, "gain",      1.0f,    valueNodes);
                fmt->width     = resolve_param_or(p, "width",     500.0f,  valueNodes);
                fmt->power     = resolve_param_or(p, "power",     2.0f,    valueNodes);
            }
            formantNodes[id] = fmt; valueNodes[id] = fmt;
        }
        else if (type == "FormantSpectrum") {
            auto spec = std::make_shared<FormantSpectrum>();
            if (node.contains("params") && node["params"].contains("formants")) {
                for (auto& fref : node["params"]["formants"]) {
                    std::string fid = fref.at("ref").get<std::string>();
                    auto it = formantNodes.find(fid);
                    if (it == formantNodes.end()) throw std::runtime_error("Unresolved formant ref: " + fid);
                    spec->formants.push_back(it->second);
                }
            }
            formantNodes[id] = spec; valueNodes[id] = spec;
        }
        else if (type == "FixedSpectrum") {
            if (!node.contains("params") || !node["params"].contains("gains"))
                throw std::runtime_error("FixedSpectrum requires params.gains");
            auto fs = std::make_shared<FixedSpectrum>(node["params"]["gains"].get<std::vector<float>>());
            formantNodes[id] = fs; valueNodes[id] = fs;
        }
        else if (type == "BandSpectrum") {
            auto bs = std::make_shared<BandSpectrum>();
            if (node.contains("params")) { auto& p = node["params"];
                bs->startFreq     = resolve_param_or(p, "startFreq",     0.0f, valueNodes);
                bs->freqIncrement = resolve_param_or(p, "freqIncrement", 1.0f, valueNodes);
                if (p.contains("gains")) bs->gainValues = p["gains"].get<std::vector<float>>();
            }
            formantNodes[id] = bs; valueNodes[id] = bs;
        }
        else if (type == "FormantSequence") {
            auto fseq = std::make_shared<FormantSequence>();
            if (node.contains("params")) { auto& p = node["params"];
                if (p.contains("formants")) {
                    for (auto& fref : p["formants"]) {
                        std::string fid = fref.at("ref").get<std::string>();
                        auto it = formantNodes.find(fid);
                        if (it == formantNodes.end()) throw std::runtime_error("Unresolved formant ref: " + fid);
                        fseq->formants.push_back(it->second);
                    }
                }
                fseq->blend = resolve_param_or(p, "blend", 0.0f, valueNodes);
            }
            formantNodes[id] = fseq; valueNodes[id] = fseq;
        }
        else if (type == "FullAdditiveSource") {
            uint32_t seed = 0xADD2'0000u;
            if (node.contains("params") && node["params"].contains("seed"))
                seed = static_cast<uint32_t>(node["params"]["seed"].get<int>());

            auto fas = std::make_shared<FullAdditiveSource>(sampleRate, seed);

            if (node.contains("params")) {
                const auto& p = node["params"];

                // Base WaveSource params
                fas->set_frequency(resolve_param_or(p, "frequency", 440.0f, valueNodes));
                fas->set_amplitude(resolve_param_or(p, "amplitude", 1.0f,   valueNodes));
                fas->set_phase(    resolve_param_or(p, "phase",     0.0f,   valueNodes));

                // 5 evolution envelopes
                fas->set_mult_env(resolve_param_or(p, "multEnv", 0.0f, valueNodes));
                fas->set_ampl_env(resolve_param_or(p, "amplEnv", 0.0f, valueNodes));
                fas->set_po_env(  resolve_param_or(p, "poEnv",   0.0f, valueNodes));
                fas->set_ro_env(  resolve_param_or(p, "roEnv",   0.0f, valueNodes));
                fas->set_dt_env(  resolve_param_or(p, "dtEnv",   0.0f, valueNodes));

                // Global rolloff/detune ranges
                fas->set_ro(p.value("rolloff1", 1.0f), p.value("rolloff2", 1.0f));
                fas->set_dt(p.value("detune1",  0.0f), p.value("detune2",  0.0f));

                // Formant (optional)
                if (p.contains("formant")) {
                    std::string fid = p["formant"].at("ref").get<std::string>();
                    auto fit = formantNodes.find(fid);
                    if (fit == formantNodes.end())
                        throw std::runtime_error("Unresolved formant spectrum ref: " + fid);
                    fas->set_formant(fit->second,
                        resolve_param_or(p, "formantWeight", 0.0f, valueNodes));
                }

                // Partial mode
                std::string mode = p.value("mode", std::string("full"));

                if (mode == "full") {
                    int maxP = p.value("maxPartials", 30);
                    int minM = p.value("minMult", 1);
                    fas->init_full_partials(maxP, minM,
                        p.value("evenWeight1", 1.0f), p.value("evenWeight2", 1.0f),
                        p.value("oddWeight1",  1.0f), p.value("oddWeight2",  1.0f),
                        p.value("unitPO1", 0.0f), p.value("unitPO2", 0.0f));
                }
                else if (mode == "sequence") {
                    int maxP = p.value("maxPartials", 30);
                    fas->init_sequence_partials(maxP,
                        p.value("minMult1", 1.0f), p.value("minMult2", 1.0f),
                        p.value("incr1", 1.0f),    p.value("incr2", 1.0f),
                        p.value("unitPO1", 0.0f),   p.value("unitPO2", 0.0f));
                }
                else if (mode == "explicit") {
                    auto m1 = p.at("mult1").get<std::vector<float>>();
                    auto m2 = p.at("mult2").get<std::vector<float>>();
                    auto a1 = p.at("ampl1").get<std::vector<float>>();
                    auto a2 = p.at("ampl2").get<std::vector<float>>();
                    fas->init_explicit_partials(
                        std::move(m1), std::move(m2),
                        std::move(a1), std::move(a2),
                        p.value("unitPO1", 0.0f), p.value("unitPO2", 0.0f));
                }
                else {
                    throw std::runtime_error("Unknown FullAdditiveSource mode: " + mode);
                }

                // Expand rule (optional)
                if (p.contains("expandRule")) {
                    const auto& er = p["expandRule"];
                    FullAdditiveSource::ExpandRule rule;
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
                    fas->set_expand_rule(rule);
                }
            }

            valueNodes[id] = fas;
            monoNodes[id]  = std::make_unique<WaveSourceMono>(fas);
        }
        else if (type == "AdditiveSource2") {
            uint32_t seed = 0xADD3'0000u;
            if (node.contains("params") && node["params"].contains("seed"))
                seed = static_cast<uint32_t>(node["params"]["seed"].get<int>());

            auto as2 = std::make_shared<AdditiveSource2>(sampleRate, seed);

            if (node.contains("params")) {
                const auto& p = node["params"];

                as2->set_frequency(resolve_param_or(p, "frequency", 440.0f, valueNodes));
                as2->set_amplitude(resolve_param_or(p, "amplitude", 1.0f,   valueNodes));

                // Variation params
                as2->set_phase_offset(  resolve_param_or(p, "phaseOffset",   0.0f, valueNodes));
                as2->set_freq_var_depth(resolve_param_or(p, "freqVarDepth",  0.0f, valueNodes));
                as2->set_freq_var_speed(resolve_param_or(p, "freqVarSpeed",  0.0f, valueNodes));
                as2->set_ampl_var_depth(resolve_param_or(p, "amplVarDepth",  0.0f, valueNodes));
                as2->set_ampl_var_speed(resolve_param_or(p, "amplVarSpeed",  0.0f, valueNodes));

                // Partials
                std::string pMode = p.value("partialMode", std::string("default"));
                if (pMode == "default") {
                    int cnt = p.value("partialCount", 500);
                    as2->set_default_partials(cnt);
                } else if (pMode == "explicit") {
                    auto idx  = p.at("partials").get<std::vector<float>>();
                    auto ampl = p.at("amplitudes").get<std::vector<float>>();
                    as2->set_partials(std::move(idx), std::move(ampl));
                } else if (pMode == "evolving") {
                    auto si = p.at("startPartials").get<std::vector<float>>();
                    auto sa = p.at("startAmplitudes").get<std::vector<float>>();
                    auto ei = p.at("endPartials").get<std::vector<float>>();
                    auto ea = p.at("endAmplitudes").get<std::vector<float>>();
                    as2->set_partials(std::move(si), std::move(sa), std::move(ei), std::move(ea));
                }

                // Per-partial envelope assignments
                auto parse_filter = [](const std::string& s) -> AdditiveSource2::PartialFilter {
                    if (s == "even")     return AdditiveSource2::PartialFilter::Even;
                    if (s == "odd")      return AdditiveSource2::PartialFilter::Odd;
                    if (s == "mult3")    return AdditiveSource2::PartialFilter::Mult3;
                    if (s == "nonMult3") return AdditiveSource2::PartialFilter::NonMult3;
                    return AdditiveSource2::PartialFilter::All;
                };

                if (p.contains("amplEnvelopes")) {
                    for (const auto& ae : p["amplEnvelopes"]) {
                        auto env = resolve_param(ae.at("envelope"), valueNodes);
                        auto filt = parse_filter(ae.value("filter", std::string("all")));
                        int from = ae.value("from", 1);
                        int to   = ae.value("to", 500);
                        as2->assign_ampl_envelope(std::move(env), filt, from, to);
                    }
                }

                if (p.contains("freqEnvelopes")) {
                    for (const auto& fe : p["freqEnvelopes"]) {
                        auto env = resolve_param(fe.at("envelope"), valueNodes);
                        auto filt = parse_filter(fe.value("filter", std::string("all")));
                        int from = fe.value("from", 1);
                        int to   = fe.value("to", 500);
                        as2->assign_freq_envelope(std::move(env), filt, from, to);
                    }
                }
            }

            valueNodes[id] = as2;
            monoNodes[id]  = std::make_unique<WaveSourceMono>(as2);
        }
        else if (type == "WanderNoiseSource") {
            uint32_t seed = 0xFA0D'0001u;
            if (node.contains("params") && node["params"].contains("seed"))
                seed = static_cast<uint32_t>(node["params"]["seed"].get<int>());

            auto wn = std::make_shared<WanderNoiseSource>(sampleRate, seed);

            if (node.contains("params")) {
                const auto& p = node["params"];
                wn->amplitude  = resolve_param_or(p, "amplitude",  1.0f, valueNodes);
                wn->speed      = resolve_param_or(p, "speed",      1.0f, valueNodes);
                wn->deltaSpeed = resolve_param_or(p, "deltaSpeed", 1.0f, valueNodes);
                wn->slopeLimit = resolve_param_or(p, "slopeLimit", 1.0f, valueNodes);
            }

            valueNodes[id] = wn;
            monoNodes[id]  = std::make_unique<ValueSourceMono>(wn);
        }
        else if (type == "WanderNoise2Source") {
            uint32_t seed = 0xFA0D'0002u;
            if (node.contains("params") && node["params"].contains("seed"))
                seed = static_cast<uint32_t>(node["params"]["seed"].get<int>());

            auto wn = std::make_shared<WanderNoise2Source>(sampleRate, seed);

            if (node.contains("params")) {
                const auto& p = node["params"];
                wn->amplitude   = resolve_param_or(p, "amplitude",   1.0f, valueNodes);
                wn->minSpeed    = resolve_param_or(p, "minSpeed",    1.0f, valueNodes);
                wn->maxSpeed    = resolve_param_or(p, "maxSpeed",    1.0f, valueNodes);
                wn->reverseProb = resolve_param_or(p, "reverseProb", 0.0f, valueNodes);
                wn->retraceProb = resolve_param_or(p, "retraceProb", 0.0f, valueNodes);
                wn->retracePct  = resolve_param_or(p, "retracePct",  0.0f, valueNodes);
            }

            valueNodes[id] = wn;
            monoNodes[id]  = std::make_unique<ValueSourceMono>(wn);
        }
        else if (type == "WanderNoise3Source") {
            auto wn = std::make_shared<WanderNoise3Source>(sampleRate);

            if (node.contains("params")) {
                const auto& p = node["params"];
                wn->amplitude  = resolve_param_or(p, "amplitude",  1.0f, valueNodes);
                wn->speed      = resolve_param_or(p, "speed",      1.0f, valueNodes);
                wn->deltaSpeed = resolve_param_or(p, "deltaSpeed", 1.0f, valueNodes);
                wn->slopeLimit = resolve_param_or(p, "slopeLimit", 1.0f, valueNodes);
            }

            valueNodes[id] = wn;
            monoNodes[id]  = std::make_unique<ValueSourceMono>(wn);
        }
        else if (type == "VarSource") {
            if (!node.contains("params"))
                throw std::runtime_error("VarSource requires params");
            const auto& p = node["params"];

            auto val    = resolve_param_or(p, "val",    0.0f, valueNodes);
            auto var    = resolve_param_or(p, "var",    0.0f, valueNodes);
            auto varPct = resolve_param_or(p, "varPct", 0.0f, valueNodes);
            bool absolute = p.value("absolute", true);

            valueNodes[id] = std::make_shared<VarSource>(
                std::move(val), std::move(var), std::move(varPct), absolute);
        }
        else if (type == "RangeSource") {
            if (!node.contains("params"))
                throw std::runtime_error("RangeSource requires params");
            const auto& p = node["params"];

            auto min = resolve_param_or(p, "min", 0.0f, valueNodes);
            auto max = resolve_param_or(p, "max", 1.0f, valueNodes);
            auto var = resolve_param_or(p, "var", 0.0f, valueNodes);
            bool normalized = p.value("normalized", false);

            valueNodes[id] = std::make_shared<RangeSource>(
                std::move(min), std::move(max), std::move(var), normalized);
        }
        else if (type == "Envelope") {
            if (!node.contains("params"))
                throw std::runtime_error("Envelope requires params");
            const auto& p = node["params"];

            std::string preset = p.value("preset", std::string("ar"));

            std::shared_ptr<Envelope> env;

            if (preset == "ar") {
                float attack = p.value("attack", 0.2f);
                float attackMin = p.value("attackMin", 0.0f);
                float attackMax = p.value("attackMax", 1.0f);
                env = std::make_shared<Envelope>(
                    Envelope::make_ar(sampleRate, attack, attackMin, attackMax));
            }
            else if (preset == "adsr") {
                float attack       = p.value("attack", 0.2f);
                float decay        = p.value("decay",  0.1f);
                float sustainLevel = p.value("sustainLevel", 0.7f);
                float release      = p.value("release", 0.0f);
                env = std::make_shared<Envelope>(
                    Envelope::make_adsr(sampleRate,
                        attack, decay, sustainLevel, release));
            }
            else {
                throw std::runtime_error("Unknown envelope preset: " + preset);
            }

            valueNodes[id] = env;
        }
        else if (type == "RedNoiseSource") {
            uint32_t seed = 0xBADC0DEu;
            if (node.contains("params") && node["params"].contains("seed"))
                seed = static_cast<uint32_t>(node["params"]["seed"].get<int>());

            auto rn = std::make_shared<RedNoiseSource>(sampleRate, seed);

            if (node.contains("params")) {
                const auto& p = node["params"];

                // Base WaveSource params
                rn->set_frequency(resolve_param_or(p, "frequency", 440.0f, valueNodes));
                rn->set_amplitude(resolve_param_or(p, "amplitude", 1.0f,   valueNodes));
                rn->set_phase(    resolve_param_or(p, "phase",     0.0f,   valueNodes));

                // RedNoise-specific params
                rn->density           = resolve_param_or(p, "density",           1.0f, valueNodes);
                rn->smoothness        = resolve_param_or(p, "smoothness",        1.0f, valueNodes);
                rn->rampVariation     = resolve_param_or(p, "rampVariation",     1.0f, valueNodes);
                rn->boost             = resolve_param_or(p, "boost",             0.0f, valueNodes);
                rn->continuity        = resolve_param_or(p, "continuity",        0.0f, valueNodes);
                rn->zeroCrossTendency = resolve_param_or(p, "zeroCrossTendency", 0.0f, valueNodes);
            }

            valueNodes[id] = rn;
            monoNodes[id]  = std::make_unique<WaveSourceMono>(rn);
        }
        else if (type == "WavetableSource") {
            uint32_t seed = 0xC0FFEEu;
            if (node.contains("params") && node["params"].contains("seed"))
                seed = static_cast<uint32_t>(node["params"]["seed"].get<int>());

            auto wt = std::make_shared<WavetableSource>(sampleRate, seed);

            if (node.contains("params")) {
                const auto& p = node["params"];

                wt->set_frequency(resolve_param_or(p, "frequency", 440.0f, valueNodes));
                wt->set_amplitude(resolve_param_or(p, "amplitude", 1.0f,   valueNodes));
                wt->set_phase(    resolve_param_or(p, "phase",     0.0f,   valueNodes));

                if (p.contains("speedFactor"))
                    wt->set_speed_factor(resolve_param_or(p, "speedFactor", 1.0f, valueNodes));

                bool interpolate = p.value("interpolate", false);
                wt->set_interpolate(interpolate);

                // Input source override
                if (p.contains("inputSource")) {
                    std::string inputRef = p["inputSource"].at("ref").get<std::string>();
                    auto it = valueNodes.find(inputRef);
                    if (it == valueNodes.end())
                        throw std::runtime_error("Unresolved inputSource ref: " + inputRef);
                    wt->set_input_source(it->second);
                }

                // Evolution
                std::string evoType = p.value("evolution", std::string("none"));

                if (evoType == "pluck") {
                    float muting = p.value("muting", 0.0f);
                    uint32_t evoSeed = p.value("evolutionSeed", 42);
                    wt->set_evolution(
                        std::make_unique<PluckEvolution>(muting, uint32_t(evoSeed)));
                }
                else if (evoType == "averaging") {
                    float sc = p.value("sampleCount", 2.0f);
                    float sp = p.value("speed", 1.0f);
                    float df = p.value("decayFactor", 0.996f);
                    bool leading    = p.value("leading", false);
                    bool autoAdjust = p.value("autoAdjust", false);
                    uint32_t evoSeed = p.value("evolutionSeed", 42);
                    wt->set_evolution(
                        std::make_unique<AveragingEvolution>(
                            sc, sp, df, leading, autoAdjust, uint32_t(evoSeed)));
                }
                else if (evoType == "target") {
                    float mr = p.value("morphRate", 0.001f);
                    int hold = p.value("holdCycles", 3);
                    float df = p.value("decayFactor", 0.998f);

                    // Build target waveform
                    std::vector<float> target;

                    if (p.contains("targetWave")) {
                        // Explicit array
                        target = p["targetWave"].get<std::vector<float>>();
                    }
                    else if (p.contains("targetPartials")) {
                        // Generate from partial amplitudes: sum of sines
                        auto partials = p["targetPartials"].get<std::vector<float>>();
                        int tLen = p.value("targetLength", 1024);
                        target.resize(tLen, 0.0f);
                        constexpr float TAU = 2.0f * 3.14159265358979323846f;
                        for (int i = 0; i < tLen; ++i) {
                            float pos = float(i) / float(tLen);
                            for (int h = 0; h < int(partials.size()); ++h) {
                                target[i] += partials[h] * std::sin(pos * TAU * float(h + 1));
                            }
                        }
                        // Normalize
                        float peak = 0.0f;
                        for (float v : target) peak = std::max(peak, std::fabs(v));
                        if (peak > 0.0f) for (float& v : target) v /= peak;
                    }

                    uint32_t evoSeed2 = p.value("evolutionSeed", 42);
                    wt->set_evolution(
                        std::make_unique<TargetEvolution>(
                            mr, hold, df, std::move(target), uint32_t(evoSeed2)));
                }
            }

            valueNodes[id] = wt;
            monoNodes[id]  = std::make_unique<WaveSourceMono>(wt);
        }
        else if (type == "HybridKSSource") {
            uint32_t seed = 0xBEEF'C0DEu;
            if (node.contains("params") && node["params"].contains("seed"))
                seed = static_cast<uint32_t>(node["params"]["seed"].get<int>());

            auto hks = std::make_shared<HybridKSSource>(sampleRate, seed);

            if (node.contains("params")) {
                const auto& p = node["params"];
                hks->set_frequency(resolve_param_or(p, "frequency", 440.0f, valueNodes));
                hks->set_amplitude(resolve_param_or(p, "amplitude", 1.0f,   valueNodes));

                hks->set_hold_cycles(p.value("holdCycles", 5));
                hks->set_morph_duration(p.value("morphDuration", 0.5f));
                hks->set_num_partials(p.value("numPartials", 30));

                if (p.contains("inputSource")) {
                    std::string ref = p["inputSource"].at("ref").get<std::string>();
                    auto it = valueNodes.find(ref);
                    if (it != valueNodes.end())
                        hks->set_input_source(it->second);
                }

                if (p.contains("targetPartials"))
                    hks->set_target_partials(p["targetPartials"].get<std::vector<float>>());
            }

            valueNodes[id] = hks;
            monoNodes[id]  = std::make_unique<WaveSourceMono>(hks);
        }
        else if (type == "CombinedSource") {
            if (!node.contains("params"))
                throw std::runtime_error("CombinedSource requires params");
            const auto& p = node["params"];

            auto s1 = resolve_param_or(p, "source1", 0.0f, valueNodes);
            auto s2 = resolve_param_or(p, "source2", 0.0f, valueNodes);
            std::string opStr = p.value("operation", std::string("add"));
            float ga = p.value("gainAdj", 0.0f);

            CombineOp op = CombineOp::Add;
            if (opStr == "multiply") op = CombineOp::Multiply;
            else if (opStr == "fade") op = CombineOp::Fade;

            valueNodes[id] = std::make_shared<CombinedSource>(
                std::move(s1), std::move(s2), op, ga);
        }
        else if (type == "CrossfadeSource") {
            auto cs = std::make_shared<CrossfadeSource>();
            if (node.contains("params")) {
                const auto& p = node["params"];
                cs->source1   = resolve_param_or(p, "source1",   0.0f, valueNodes);
                cs->source2   = resolve_param_or(p, "source2",   0.0f, valueNodes);
                cs->amplitude = resolve_param_or(p, "amplitude", 1.0f, valueNodes);
                cs->ratio     = p.value("ratio", 0.5f);
                cs->overlap   = p.value("overlap", 0.1f);
                cs->gainAdj   = p.value("gainAdj", 0.0f);
            }
            valueNodes[id] = cs;
        }
        else if (type == "DistortedSource") {
            uint32_t seed = 0xD157'0000u;
            if (node.contains("params") && node["params"].contains("seed"))
                seed = static_cast<uint32_t>(node["params"]["seed"].get<int>());

            auto ds = std::make_shared<DistortedSource>(seed);
            if (node.contains("params")) {
                const auto& p = node["params"];
                ds->source    = resolve_param_or(p, "source",    0.0f, valueNodes);
                ds->amplitude = resolve_param_or(p, "amplitude", 1.0f, valueNodes);
                ds->density   = resolve_param_or(p, "density",   1.0f, valueNodes);
                ds->gain      = resolve_param_or(p, "gain",      1.0f, valueNodes);
                ds->shift     = resolve_param_or(p, "shift",     0.0f, valueNodes);
            }
            valueNodes[id] = ds;
            monoNodes[id]  = std::make_unique<ValueSourceMono>(ds);
        }
        else if (type == "StaticVarSource") {
            const auto& p = node.at("params");
            uint32_t seed = p.value("seed", 0x57A7'0001);
            valueNodes[id] = std::make_shared<StaticVarSource>(
                p.value("baseValue", 1.0f), p.value("varPct", 0.0f),
                p.value("bias", 0.0f), uint32_t(seed));
        }
        else if (type == "StaticRangeSource") {
            const auto& p = node.at("params");
            uint32_t seed = p.value("seed", 0x57A7'0002);
            valueNodes[id] = std::make_shared<StaticRangeSource>(
                p.value("min", 0.0f), p.value("max", 1.0f),
                p.value("bias", 0.0f), uint32_t(seed));
        }
        else if (type == "BWLowpassFilter") {
            int sections = 2;
            if (node.contains("params"))
                sections = node["params"].value("sections", 2);

            auto f = std::make_shared<BWLowpassFilter>(sampleRate, sections);
            if (node.contains("params")) {
                const auto& p = node["params"];
                f->source     = resolve_param_or(p, "source",     0.0f, valueNodes);
                f->cutoffFreq = resolve_param_or(p, "cutoff",  5000.0f, valueNodes);
            }
            valueNodes[id] = f;
            monoNodes[id]  = std::make_unique<ValueSourceMono>(f);
        }
        else if (type == "BWHighpassFilter") {
            int sections = 2;
            if (node.contains("params"))
                sections = node["params"].value("sections", 2);

            auto f = std::make_shared<BWHighpassFilter>(sampleRate, sections);
            if (node.contains("params")) {
                const auto& p = node["params"];
                f->source     = resolve_param_or(p, "source",    0.0f, valueNodes);
                f->cutoffFreq = resolve_param_or(p, "cutoff",  100.0f, valueNodes);
            }
            valueNodes[id] = f;
            monoNodes[id]  = std::make_unique<ValueSourceMono>(f);
        }
        else if (type == "BWBandpassFilter") {
            int sections = 2;
            if (node.contains("params"))
                sections = node["params"].value("sections", 2);

            auto f = std::make_shared<BWBandpassFilter>(sampleRate, sections);
            if (node.contains("params")) {
                const auto& p = node["params"];
                f->source     = resolve_param_or(p, "source",      0.0f, valueNodes);
                f->lowCutoff  = resolve_param_or(p, "lowCutoff", 100.0f, valueNodes);
                f->highCutoff = resolve_param_or(p, "highCutoff",5000.0f, valueNodes);
            }
            valueNodes[id] = f;
            monoNodes[id]  = std::make_unique<ValueSourceMono>(f);
        }
        else if (type == "DelayFilter") {
            auto d = std::make_shared<DelayFilter>(sampleRate);
            if (node.contains("params")) {
                const auto& p = node["params"];
                d->source     = resolve_param_or(p, "source",     0.0f, valueNodes);
                d->delayTime  = resolve_param_or(p, "delayTime",  0.1f, valueNodes);
                d->delayLevel = resolve_param_or(p, "delayLevel", 0.5f, valueNodes);
                d->feedback   = resolve_param_or(p, "feedback",   0.3f, valueNodes);
            }
            valueNodes[id] = d;
            monoNodes[id]  = std::make_unique<ValueSourceMono>(d);
        }
        else if (type == "Vibrato") {
            if (!node.contains("params"))
                throw std::runtime_error("Vibrato requires params");
            const auto& p = node["params"];

            float speed     = p.value("speed", 5.0f);
            float depth     = p.value("depth", 0.02f);
            float attack    = p.value("attack", 0.3f);
            float threshold = p.value("threshold", 0.0f);
            float speedVar  = p.value("speedVar", 0.0f);
            float depthVar  = p.value("depthVar", 0.0f);
            uint32_t seed   = p.value("seed", 0xF1B0'0000);

            auto vib = std::make_shared<Vibrato>(
                sampleRate, speed, depth, attack, threshold, speedVar, depthVar, uint32_t(seed));
            vib->frequency = resolve_param_or(p, "frequency", 440.0f, valueNodes);

            valueNodes[id] = vib;
        }
        // SoundChannel and StereoMixer handled by caller
    }

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
        // The node must exist in valueNodes, and the param must be a ConstantSource.
        auto nodeIt = g.valueNodes.find(nodeId);
        if (nodeIt == g.valueNodes.end())
            throw std::runtime_error("paramMap: unknown node '" + nodeId + "'");

        // For WaveSource nodes, we can resolve known param names
        auto ws = std::dynamic_pointer_cast<WaveSource>(nodeIt->second);
        std::shared_ptr<ValueSource> paramSrc;

        if (ws) {
            if (paramName == "frequency") paramSrc = ws->get_frequency();
            else if (paramName == "amplitude") paramSrc = ws->get_amplitude();
            else if (paramName == "phase") paramSrc = ws->get_phase();
        }

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
    // Instrument mode: build voices, schedule notes
    // -------------------------------------------------------------------
    if (root.contains("instrument")) {
        const auto& instJson = root["instrument"];
        int polyphony = instJson.value("polyphony", 4);

        auto inst = std::make_unique<Instrument>();
        inst->sampleRate = sampleRate;

        // Build voice pool: N independent graph instances
        for (int v = 0; v < polyphony; ++v) {
            auto g = build_graph(nodeMap, nodeOrder, sampleRate);
            Instrument::VoiceGraph vg;

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

} // namespace mforce
