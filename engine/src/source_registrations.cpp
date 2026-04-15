#include "mforce/core/source_registry.h"

// All source types
#include "mforce/source/sine_source.h"
#include "mforce/source/saw_source.h"
#include "mforce/source/triangle_source.h"
#include "mforce/source/pulse_source.h"
#include "mforce/source/fm_source.h"
#include "mforce/source/red_noise_source.h"
#include "mforce/source/pink_noise_source.h"
#include "mforce/source/white_noise_source.h"
#include "mforce/source/noise_sources.h"
#include "mforce/source/wander_noise_source.h"
#include "mforce/source/sort_oscillator.h"
#include "mforce/source/gray_scott_source.h"
#include "mforce/source/fitzhugh_nagumo_source.h"
#include "mforce/source/markov_ode_source.h"
#include "mforce/source/mass_spring_source.h"
#include "mforce/source/self_rewriting_ast_source.h"
#include "mforce/source/sat_dpll_source.h"
#include "mforce/source/micro_nn_source.h"
#include "mforce/source/self_avoiding_walk_source.h"
#include "mforce/source/homotopy_source.h"
#include "mforce/source/ldpc_source.h"
#include "mforce/source/wavetable_source.h"
#include "mforce/source/hybrid_ks_source.h"
#include "mforce/source/combined_source.h"
#include "mforce/source/segment_source.h"
#include "mforce/source/repeating_source.h"
#include "mforce/source/phased_value_source.h"
#include "mforce/source/additive/basic_additive_source.h"
#include "mforce/source/additive/additive_source2.h"
#include "mforce/source/additive/full_additive_source.h"
#include "mforce/source/additive/formant.h"
#include "mforce/source/additive/partials.h"
#include "mforce/source/wave_evolution.h"
#include "mforce/core/var_source.h"
#include "mforce/core/range_source.h"
#include "mforce/core/envelope.h"
#include "mforce/core/envelope_presets.h"
#include "mforce/core/multi_source.h"
#include "mforce/filter/filters.h"
#include "mforce/filter/vibrato.h"

#include <cmath>

namespace mforce {

void register_all_sources() {
    auto& reg = SourceRegistry::instance();

    // -----------------------------------------------------------------------
    // Simple oscillators — factory only, generic set_param handles all wiring
    // -----------------------------------------------------------------------

    reg.register_type("SineSource", SourceCategory::Oscillator,
        [](int sr, auto) { return std::make_shared<SineSource>(sr); });

    reg.register_type("SawSource", SourceCategory::Oscillator,
        [](int sr, auto) { return std::make_shared<SawSource>(sr); });

    reg.register_type("TriangleSource", SourceCategory::Oscillator,
        [](int sr, auto) { return std::make_shared<TriangleSource>(sr); });

    reg.register_type("PulseSource", SourceCategory::Oscillator,
        [](int sr, auto) { return std::make_shared<PulseSource>(sr); });

    reg.register_type("FMSource", SourceCategory::Oscillator,
        [](int sr, auto) { return std::make_shared<FMSource>(sr); });

    reg.register_type("SortOscillator", SourceCategory::Oscillator,
        [](int sr, auto seed) {
            return std::make_shared<SortOscillator>(sr, seed.value_or(0x5041'7201u));
        });

    reg.register_type("GrayScottSource", SourceCategory::Oscillator,
        [](int sr, auto seed) {
            return std::make_shared<GrayScottSource>(sr, seed.value_or(0x6783'0001u));
        });

    reg.register_type("FitzhughNagumoSource", SourceCategory::Oscillator,
        [](int sr, auto seed) {
            return std::make_shared<FitzhughNagumoSource>(sr, seed.value_or(0xF417'0001u));
        });

    reg.register_type("MarkovOdeSource", SourceCategory::Oscillator,
        [](int sr, auto seed) {
            return std::make_shared<MarkovOdeSource>(sr, seed.value_or(0x0DE1'2001u));
        });

    reg.register_type("MassSpringSource", SourceCategory::Oscillator,
        [](int sr, auto seed) {
            return std::make_shared<MassSpringSource>(sr, seed.value_or(0x5291'0001u));
        });

    reg.register_type("SelfRewritingASTSource", SourceCategory::Oscillator,
        [](int sr, auto seed) {
            return std::make_shared<SelfRewritingASTSource>(sr, seed.value_or(0xA571'0001u));
        });

    reg.register_type("SatDpllSource", SourceCategory::Oscillator,
        [](int sr, auto seed) {
            return std::make_shared<SatDpllSource>(sr, seed.value_or(0x5A7D'0001u));
        });

    reg.register_type("MicroNNSource", SourceCategory::Oscillator,
        [](int sr, auto seed) {
            return std::make_shared<MicroNNSource>(sr, seed.value_or(0x11E0'0001u));
        });

    reg.register_type("SelfAvoidingWalkSource", SourceCategory::Oscillator,
        [](int sr, auto seed) {
            return std::make_shared<SelfAvoidingWalkSource>(sr, seed.value_or(0x5A01'0001u));
        });

    reg.register_type("HomotopySource", SourceCategory::Oscillator,
        [](int sr, auto seed) {
            return std::make_shared<HomotopySource>(sr, seed.value_or(0xB007'0001u));
        });

    reg.register_type("LDPCSource", SourceCategory::Oscillator,
        [](int sr, auto seed) {
            return std::make_shared<LDPCSource>(sr, seed.value_or(0x1DBC'0001u));
        });

    // -----------------------------------------------------------------------
    // Additive — BasicAdditiveSource (simple), AdditiveSource (full via Partials)
    // -----------------------------------------------------------------------

    // BasicAdditiveSource (was "AdditiveSource" — alias kept for backward compat)
    reg.register_type("BasicAdditiveSource", SourceCategory::Additive,
        [](int sr, auto seed) {
            return std::make_shared<BasicAdditiveSource>(sr, seed.value_or(0xADD1'0000u));
        });

    // The real AdditiveSource (thin loop over Partials, was FullAdditiveSource)
    reg.register_type("AdditiveSource", SourceCategory::Additive,
        [](int sr, auto seed) {
            return std::make_shared<FullAdditiveSource>(sr, seed.value_or(0xADD2'0000u));
        });

    reg.register_type("Formant", SourceCategory::Additive,
        [](int, auto) { return std::make_shared<Formant>(); });

    reg.register_type("BandSpectrum", SourceCategory::Additive,
        [](int, auto) { return std::make_shared<BandSpectrum>(); },
        // Configurator: gains array
        [](ValueSource& src, const nlohmann::json& p, const ResolveParamFn&) {
            auto& bs = static_cast<BandSpectrum&>(src);
            if (p.contains("gains")) bs.gainValues = p["gains"].get<std::vector<float>>();
        });

    // -----------------------------------------------------------------------
    // Noise generators
    // -----------------------------------------------------------------------

    reg.register_type("WhiteNoiseSource", SourceCategory::Generator,
        [](int, auto seed) {
            return std::make_shared<WhiteNoiseSource>(seed.value_or(0x12345678u));
        });

    reg.register_type("PinkNoiseSource", SourceCategory::Generator,
        [](int, auto seed) {
            return std::make_shared<PinkNoiseSource>(PinkNoiseSource::DEFAULT_ROWS,
                seed.value_or(0xF10C'0001u));
        });

    reg.register_type("RedNoiseSource", SourceCategory::Generator,
        [](int sr, auto seed) {
            return std::make_shared<RedNoiseSource>(sr, seed.value_or(0xBADC0DEu));
        });

    reg.register_type("BlueNoiseSource", SourceCategory::Generator,
        [](int, auto seed) {
            return std::make_shared<BlueNoiseSource>(seed.value_or(0xB100'0001u));
        });

    reg.register_type("VioletNoiseSource", SourceCategory::Generator,
        [](int, auto seed) {
            return std::make_shared<VioletNoiseSource>(seed.value_or(0xF100'0001u));
        });

    reg.register_type("VelvetNoiseSource", SourceCategory::Generator,
        [](int sr, auto seed) {
            return std::make_shared<VelvetNoiseSource>(sr, seed.value_or(0xFE17'0001u));
        });

    reg.register_type("PerlinNoiseSource", SourceCategory::Generator,
        [](int sr, auto seed) {
            return std::make_shared<PerlinNoiseSource>(sr, seed.value_or(0xAE21'0001u));
        });

    reg.register_type("CrackleNoiseSource", SourceCategory::Generator,
        [](int, auto seed) {
            return std::make_shared<CrackleNoiseSource>(seed.value_or(0xC8AC'0001u));
        });

    reg.register_type("MurmurationNoiseSource", SourceCategory::Generator,
        [](int sr, auto seed) {
            return std::make_shared<MurmurationNoiseSource>(sr, seed.value_or(0xB18D'0001u));
        });

    reg.register_type("WanderNoiseSource", SourceCategory::Generator,
        [](int sr, auto seed) {
            return std::make_shared<WanderNoiseSource>(sr, seed.value_or(0xFA0D'0001u));
        });

    reg.register_type("WanderNoise2Source", SourceCategory::Generator,
        [](int sr, auto seed) {
            return std::make_shared<WanderNoise2Source>(sr, seed.value_or(0xFA0D'0002u));
        });

    reg.register_type("WanderNoise3Source", SourceCategory::Generator,
        [](int sr, auto) { return std::make_shared<WanderNoise3Source>(sr); });

    // -----------------------------------------------------------------------
    // Modulators / Utilities
    // -----------------------------------------------------------------------

    reg.register_type("VarSource", SourceCategory::Modulator,
        [](int, auto) {
            return std::make_shared<VarSource>(
                std::make_shared<ConstantSource>(0.0f),
                std::make_shared<ConstantSource>(0.0f),
                std::make_shared<ConstantSource>(0.0f), true);
        });

    reg.register_type("RangeSource", SourceCategory::Modulator,
        [](int, auto) {
            return std::make_shared<RangeSource>(
                std::make_shared<ConstantSource>(0.0f),
                std::make_shared<ConstantSource>(1.0f),
                std::make_shared<ConstantSource>(0.0f), true);
        });

    // -----------------------------------------------------------------------
    // Envelope
    // -----------------------------------------------------------------------

    reg.register_type("Envelope", SourceCategory::Envelope,
        [](int sr, auto) {
            return std::make_shared<Envelope>(Envelope::make_ar(sr, 0.2f));
        });

    reg.register_type("AREnvelope", SourceCategory::Envelope,
        [](int sr, auto) { return std::make_shared<AREnvelope>(sr); });

    reg.register_type("ASEnvelope", SourceCategory::Envelope,
        [](int sr, auto) { return std::make_shared<ASEnvelope>(sr); });

    reg.register_type("ASREnvelope", SourceCategory::Envelope,
        [](int sr, auto) { return std::make_shared<ASREnvelope>(sr); });

    reg.register_type("ADSEnvelope", SourceCategory::Envelope,
        [](int sr, auto) { return std::make_shared<ADSEnvelope>(sr); });

    reg.register_type("ADREnvelope", SourceCategory::Envelope,
        [](int sr, auto) { return std::make_shared<ADREnvelope>(sr); });

    reg.register_type("ADSREnvelope", SourceCategory::Envelope,
        [](int sr, auto) { return std::make_shared<ADSREnvelope>(sr); });

    // -----------------------------------------------------------------------
    // Combiners
    // -----------------------------------------------------------------------

    reg.register_type("CombinedSource", SourceCategory::Combiner,
        [](int, auto) {
            return std::make_shared<CombinedSource>(
                std::make_shared<ConstantSource>(0.0f),
                std::make_shared<ConstantSource>(0.0f),
                CombineOp::Add, 0.0f);
        },
        // Configurator: operation enum, gainAdj
        [](ValueSource& src, const nlohmann::json& p, const ResolveParamFn&) {
            auto& cs = static_cast<CombinedSource&>(src);
            std::string opStr = p.value("operation", std::string("add"));
            if (opStr == "multiply") cs.op = CombineOp::Multiply;
            else if (opStr == "fade") cs.op = CombineOp::Fade;
            else cs.op = CombineOp::Add;
            cs.gainAdj = p.value("gainAdj", 0.0f);
        });

    reg.register_type("CrossfadeSource", SourceCategory::Combiner,
        [](int, auto) { return std::make_shared<CrossfadeSource>(); },
        // Configurator: ratio, overlap, gainAdj
        [](ValueSource& src, const nlohmann::json& p, const ResolveParamFn&) {
            auto& cs = static_cast<CrossfadeSource&>(src);
            cs.ratio   = p.value("ratio", 0.5f);
            cs.overlap = p.value("overlap", 0.1f);
            cs.gainAdj = p.value("gainAdj", 0.0f);
        });

    reg.register_type("DistortedSource", SourceCategory::Modulator,
        [](int, auto seed) {
            return std::make_shared<DistortedSource>(seed.value_or(0xD157'0000u));
        });

    reg.register_type("StaticVarSource", SourceCategory::Utility,
        [](int, auto seed) {
            // Needs all config from JSON — use default, configurator sets it
            return std::make_shared<StaticVarSource>(1.0f, 0.0f, 0.0f,
                seed.value_or(0x57A7'0001u));
        },
        [](ValueSource& src, const nlohmann::json& p, const ResolveParamFn&) {
            auto& sv = static_cast<StaticVarSource&>(src);
            sv.baseValue = p.value("baseValue", 1.0f);
            sv.varPct    = p.value("varPct", 0.0f);
            sv.bias      = p.value("bias", 0.0f);
        });

    reg.register_type("StaticRangeSource", SourceCategory::Utility,
        [](int, auto seed) {
            return std::make_shared<StaticRangeSource>(0.0f, 1.0f, 0.0f,
                seed.value_or(0x57A7'0002u));
        },
        [](ValueSource& src, const nlohmann::json& p, const ResolveParamFn&) {
            auto& sr = static_cast<StaticRangeSource&>(src);
            sr.min  = p.value("min", 0.0f);
            sr.max  = p.value("max", 1.0f);
            sr.bias = p.value("bias", 0.0f);
        });

    // -----------------------------------------------------------------------
    // Generators with structural config
    // -----------------------------------------------------------------------

    reg.register_type("MultiSource", SourceCategory::Combiner,
        [](int, auto) { return std::make_shared<MultiSource>(); });

    reg.register_type("SegmentSource", SourceCategory::Generator,
        [](int sr, auto seed) {
            return std::make_shared<SegmentSource>(
                std::vector<float>{}, sr, false, seed.value_or(0x5E6A'0000u));
        });

    reg.register_type("RepeatingSource", SourceCategory::Modulator,
        [](int sr, auto seed) {
            return std::make_shared<RepeatingSource>(sr, seed.value_or(0xAEAE'0000u));
        },
        // Configurator: duration, gap (plain floats)
        [](ValueSource& src, const nlohmann::json& p, const ResolveParamFn&) {
            auto& rep = static_cast<RepeatingSource&>(src);
            rep.duration    = p.value("duration", 0.5f);
            rep.durVarPct   = p.value("durVarPct", 0.0f);
            rep.gapDuration = p.value("gap", 0.2f);
            rep.gapVarPct   = p.value("gapVarPct", 0.0f);
        });

    reg.register_type("PhasedValueSource", SourceCategory::Envelope,
        [](int sr, auto) {
            return std::make_shared<PhasedValueSource>(sr, 0.05f);
        },
        // Configurator: overlap, stages
        [](ValueSource& src, const nlohmann::json& p, const ResolveParamFn& resolve) {
            auto& pvs = static_cast<PhasedValueSource&>(src);
            pvs.overlapSeconds = p.value("overlap", 0.05f);
            if (p.contains("stages")) {
                for (const auto& sj : p["stages"]) {
                    PhasedValueSource::Stage stg;
                    stg.source  = resolve(sj.at("source"));
                    stg.percent = sj.value("percent", 0.0f);
                    stg.minSec  = sj.value("min", 0.0f);
                    stg.maxSec  = sj.value("max", 0.0f);
                    stg.gainAdj = sj.value("gain", 1.0f);
                    pvs.add_stage(std::move(stg));
                }
            }
        });

    // -----------------------------------------------------------------------
    // Filters
    // -----------------------------------------------------------------------

    reg.register_type("BWLowpassFilter", SourceCategory::Filter,
        [](int sr, auto) { return std::make_shared<BWLowpassFilter>(sr, 2); },
        // Configurator: section count, "cutoff" alias
        [](ValueSource& src, const nlohmann::json& p, const ResolveParamFn& resolve) {
            // JSON uses "cutoff" but param descriptor uses "cutoffFreq"
            if (p.contains("cutoff"))
                src.set_param("cutoffFreq", resolve(p.at("cutoff")));
        });

    reg.register_type("BWHighpassFilter", SourceCategory::Filter,
        [](int sr, auto) { return std::make_shared<BWHighpassFilter>(sr, 2); },
        [](ValueSource& src, const nlohmann::json& p, const ResolveParamFn& resolve) {
            if (p.contains("cutoff"))
                src.set_param("cutoffFreq", resolve(p.at("cutoff")));
        });

    reg.register_type("BWBandpassFilter", SourceCategory::Filter,
        [](int sr, auto) { return std::make_shared<BWBandpassFilter>(sr, 2); });

    reg.register_type("DelayFilter", SourceCategory::Filter,
        [](int sr, auto) { return std::make_shared<DelayFilter>(sr); });

    reg.register_type("Vibrato", SourceCategory::Modulator,
        [](int sr, auto seed) {
            // All params are construction-time; factory uses defaults
            return std::make_shared<Vibrato>(
                sr, 5.0f, 0.02f, 0.3f, 0.0f, 0.0f, 0.0f,
                seed.value_or(0xF1B0'0000u));
        });

    // -----------------------------------------------------------------------
    // Complex additive types — AdditiveSource (thin) + Partials (rendering)
    // -----------------------------------------------------------------------

    // "FullAdditiveSource" — backward compat alias, handled in patch_loader
    // (special-case block creates AdditiveSource + Partials)

    reg.register_type("AdditiveSource2", SourceCategory::Additive,
        [](int sr, auto seed) {
            auto as2 = std::make_shared<AdditiveSource2>(sr, seed.value_or(0xADD3'0000u));
            as2->set_default_partials(500);
            return as2;
        });

    // -----------------------------------------------------------------------
    // Formant collection types
    // -----------------------------------------------------------------------

    reg.register_type("FormantSpectrum", SourceCategory::Additive,
        [](int, auto) { return std::make_shared<FormantSpectrum>(); });

    reg.register_type("FixedSpectrum", SourceCategory::Additive,
        [](int, auto) {
            // Needs gains from JSON — create empty, configurator fills
            return std::make_shared<FixedSpectrum>(std::vector<float>{});
        });

    reg.register_type("FormantSequence", SourceCategory::Additive,
        [](int, auto) { return std::make_shared<FormantSequence>(); });

    reg.register_type("CompositePartials", SourceCategory::Additive,
        [](int, auto) { return std::make_shared<CompositePartials>(); });

    // -----------------------------------------------------------------------
    // Partial specification types (new architecture — these are the renderers)
    // -----------------------------------------------------------------------

    reg.register_type("FullPartials", SourceCategory::Additive,
        [](int, auto seed) {
            return std::make_shared<FullPartials>(seed.value_or(0xADD2'0000u));
        });

    reg.register_type("SequencePartials", SourceCategory::Additive,
        [](int, auto seed) {
            return std::make_shared<SequencePartials>(seed.value_or(0xADD2'0000u));
        });

    reg.register_type("ExplicitPartials", SourceCategory::Additive,
        [](int, auto seed) {
            return std::make_shared<ExplicitPartials>(seed.value_or(0xADD2'0000u));
        });

    // -----------------------------------------------------------------------
    // Wavetable and Hybrid KS
    // -----------------------------------------------------------------------

    reg.register_type("WavetableSource", SourceCategory::Oscillator,
        [](int sr, auto seed) {
            return std::make_shared<WavetableSource>(sr, seed.value_or(0xC0FFEEu));
        });

    reg.register_type("PluckEvolution", SourceCategory::Generator,
        [](int, auto seed) {
            return std::make_shared<PluckEvolutionSource>(seed.value_or(0xDEAD'BEEFu));
        });

    reg.register_type("AveragingEvolution", SourceCategory::Generator,
        [](int, auto seed) {
            return std::make_shared<AveragingEvolutionSource>(seed.value_or(0xCAFE'BABEu));
        });

    reg.register_type("EKSEvolution", SourceCategory::Generator,
        [](int, auto seed) {
            return std::make_shared<EKSEvolutionSource>(seed.value_or(0xEE45'0000u));
        });

    reg.register_type("HybridKSSource", SourceCategory::Oscillator,
        [](int sr, auto seed) {
            return std::make_shared<HybridKSSource>(sr, seed.value_or(0xBEEF'C0DEu));
        });
}

} // namespace mforce
