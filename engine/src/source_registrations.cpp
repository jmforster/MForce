#include "mforce/core/source_registry.h"
#include "mforce/source/modulator/name_gate.h"
#include <stdexcept>
#include <string>

// All source types
#include "mforce/source/oscillator/sine_source.h"
#include "mforce/source/oscillator/saw_source.h"
#include "mforce/source/oscillator/triangle_source.h"
#include "mforce/source/oscillator/pulse_source.h"
#include "mforce/source/oscillator/fm_source.h"
#include "mforce/source/noise/red_noise_source.h"
#include "mforce/source/noise/layered_red_noise_source.h"
#include "mforce/source/noise/pink_noise_source.h"
#include "mforce/source/noise/white_noise_source.h"
#include "mforce/source/noise/blue_noise_source.h"
#include "mforce/source/noise/crackle_noise_source.h"
#include "mforce/source/noise/murmuration_noise_source.h"
#include "mforce/source/noise/perlin_noise_source.h"
#include "mforce/source/noise/velvet_noise_source.h"
#include "mforce/source/noise/violet_noise_source.h"
#include "mforce/source/noise/wander_noise_source.h"
#include "mforce/source/noise/wander_noise2_source.h"
#include "mforce/source/noise/wander_noise3_source.h"
#include "mforce/source/wavetable/wavetable_source.h"
#include "mforce/source/wavetable/hybrid_ks_source.h"
#include "mforce/source/combiner/combined_source.h"
#include "mforce/source/combiner/multiplex_source.h"
#include "mforce/source/envelope/segment_source.h"
#include "mforce/source/modulator/repeating_source.h"
#include "mforce/source/envelope/phased_value_source.h"
#include "mforce/source/additive/basic_additive_source.h"
#include "mforce/source/additive/additive_source2.h"
#include "mforce/source/additive/full_additive_source.h"
#include "mforce/source/additive/formant.h"
#include "mforce/source/additive/partials.h"
#include "mforce/source/wavetable/wave_evolution.h"
#include "mforce/source/wavetable/pluck_evolution.h"
#include "mforce/source/wavetable/averaging_evolution.h"
#include "mforce/source/wavetable/target_evolution.h"
#include "mforce/source/wavetable/eks_evolution.h"
#include "mforce/source/wavetable/reed_evolution.h"
#include "mforce/source/wavetable/bowed_string_evolution.h"
#include "mforce/source/wavetable/brass_evolution.h"
#include "mforce/source/wavetable/reaction_diffusion_evolution.h"
#include "mforce/source/wavetable/sort_erosion_evolution.h"
#include "mforce/source/wavetable/cellular_automaton_evolution.h"
#include "mforce/source/wavetable/histogram_equalize_evolution.h"
#include "mforce/source/wavetable/bezier_pull_evolution.h"
#include "mforce/source/wavetable/bit_rotate_evolution.h"
#include "mforce/source/modulator/var_source.h"
#include "mforce/source/modulator/range_source.h"
#include "mforce/source/modulator/curve_node.h"
#include "mforce/source/envelope/envelope.h"
#include "mforce/source/combiner/multi_source.h"
#include "mforce/source/filter/filters.h"
#include "mforce/source/filter/hammer_bank.h"
#include "mforce/source/filter/svf_source.h"
#include "mforce/source/filter/limiter.h"
#include "mforce/source/physical/ks_string.h"
#include "mforce/source/physical/allpass_resonator.h"
#include "mforce/source/physical/delay_line_source.h"
#include "mforce/source/filter/shaper_source.h"
#include "mforce/source/utility/wormhole_source.h"
#include "mforce/source/physical/bow_table_source.h"
#include "mforce/source/physical/mesh2d_source.h"
#include "mforce/source/physical/pierce_filter.h"
#include "mforce/source/filter/biquad_source.h"
#include "mforce/source/filter/reverb.h"
#include "mforce/source/envelope/vibrato.h"

#include <cmath>

namespace mforce {

void register_all_sources() {
    auto& reg = SourceRegistry::instance();

    // -----------------------------------------------------------------------
    // Oscillators
    // -----------------------------------------------------------------------

    reg.register_type("SineSource", {.family = Family::Oscillator},
        [](auto) { return std::make_shared<SineSource>(); });

    reg.register_type("SawSource", {.family = Family::Oscillator},
        [](auto) { return std::make_shared<SawSource>(); });

    reg.register_type("PulseSource", {.family = Family::Oscillator},
        [](auto) { return std::make_shared<PulseSource>(); });

    reg.register_type("TriangleSource", {.family = Family::Oscillator},
        [](auto) { return std::make_shared<TriangleSource>(); });

    reg.register_type("FMSource", {.family = Family::Oscillator},
        [](auto) { return std::make_shared<FMSource>(); });

    // -----------------------------------------------------------------------
    // Noise
    // -----------------------------------------------------------------------

    reg.register_type("RedNoiseSource", {.family = Family::Noise, .label = "Red"},
        [](auto seed) {
            return std::make_shared<RedNoiseSource>(seed.value_or(0xBADC0DEu));
        });

    reg.register_type("PinkNoiseSource", {.family = Family::Noise, .label = "Pink"},
        [](auto seed) {
            return std::make_shared<PinkNoiseSource>(PinkNoiseSource::DEFAULT_ROWS,
                seed.value_or(0xF10C'0001u));
        });

    reg.register_type("WhiteNoiseSource", {.family = Family::Noise, .label = "White"},
        [](auto seed) {
            return std::make_shared<WhiteNoiseSource>(seed.value_or(0x12345678u));
        });

    reg.register_type("BlueNoiseSource", {.family = Family::Noise, .label = "Blue"},
        [](auto seed) {
            return std::make_shared<BlueNoiseSource>(seed.value_or(0xB100'0001u));
        });

    reg.register_type("VioletNoiseSource", {.family = Family::Noise, .label = "Violet"},
        [](auto seed) {
            return std::make_shared<VioletNoiseSource>(seed.value_or(0xF100'0001u));
        });

    reg.register_type("VelvetNoiseSource", {.family = Family::Noise, .label = "Velvet"},
        [](auto seed) {
            return std::make_shared<VelvetNoiseSource>(seed.value_or(0xFE17'0001u));
        });

    reg.register_type("PerlinNoiseSource", {.family = Family::Noise, .label = "Perlin"},
        [](auto seed) {
            return std::make_shared<PerlinNoiseSource>(seed.value_or(0xAE21'0001u));
        });

    reg.register_type("CrackleNoiseSource", {.family = Family::Noise, .label = "Crackle"},
        [](auto seed) {
            return std::make_shared<CrackleNoiseSource>(seed.value_or(0xC8AC'0001u));
        });

    // -----------------------------------------------------------------------
    // Noise, the Experimental shelf
    // -----------------------------------------------------------------------

    reg.register_type("LayeredRedNoiseSource", {.family = Family::Noise, .label = "Layered Red", .shelf = Shelf::Experimental},
        [](auto seed) {
            return std::make_shared<LayeredRedNoiseSource>(seed.value_or(0x1A4E4EDDu));
        });

    reg.register_type("MurmurationNoiseSource", {.family = Family::Noise, .label = "Murmuration", .shelf = Shelf::Experimental},
        [](auto seed) {
            return std::make_shared<MurmurationNoiseSource>(seed.value_or(0xB18D'0001u));
        });

    reg.register_type("WanderNoiseSource", {.family = Family::Noise, .label = "Wander 1", .shelf = Shelf::Experimental},
        [](auto seed) {
            return std::make_shared<WanderNoiseSource>(seed.value_or(0xFA0D'0001u));
        });

    reg.register_type("WanderNoise2Source", {.family = Family::Noise, .label = "Wander 2", .shelf = Shelf::Experimental},
        [](auto seed) {
            return std::make_shared<WanderNoise2Source>(seed.value_or(0xFA0D'0002u));
        });

    reg.register_type("WanderNoise3Source", {.family = Family::Noise, .label = "Wander 3", .shelf = Shelf::Experimental},
        [](auto) { return std::make_shared<WanderNoise3Source>(); });

    // -----------------------------------------------------------------------
    // Wavetable
    // -----------------------------------------------------------------------

    reg.register_type("WavetableSource", {.family = Family::Wavetable},
        [](auto seed) {
            return std::make_shared<WavetableSource>(seed.value_or(0xC0FFEEu));
        });

    reg.register_type("HybridKSSource", {.family = Family::Wavetable},
        [](auto seed) {
            return std::make_shared<HybridKSSource>(seed.value_or(0xBEEF'C0DEu));
        });

    reg.register_type("EKSEvolution", {.family = Family::Wavetable},
        [](auto seed) {
            return std::make_shared<EKSEvolution>(seed.value_or(0xEE45'0000u));
        });

    reg.register_type("PluckEvolution", {.family = Family::Wavetable},
        [](auto seed) {
            return std::make_shared<PluckEvolution>(seed.value_or(0xDEAD'BEEFu));
        });

    reg.register_type("AveragingEvolution", {.family = Family::Wavetable},
        [](auto seed) {
            return std::make_shared<AveragingEvolution>(seed.value_or(0xCAFE'BABEu));
        });

    // -----------------------------------------------------------------------
    // Wavetable, the Experimental shelf
    // -----------------------------------------------------------------------

    reg.register_type("BowedStringEvolution", {.family = Family::Wavetable, .shelf = Shelf::Experimental},
        [](auto) { return std::make_shared<BowedStringEvolution>(); });

    reg.register_type("ReedEvolution", {.family = Family::Wavetable, .shelf = Shelf::Experimental},
        [](auto seed) {
            return std::make_shared<ReedEvolution>(seed.value_or(0xBEED'BEEDu));
        });

    reg.register_type("BrassEvolution", {.family = Family::Wavetable, .shelf = Shelf::Experimental},
        [](auto seed) {
            return std::make_shared<BrassEvolution>(seed.value_or(0xB8A5'5000u));
        });

    // Algorithmic evolutions (abstract transforms, not physical models).
    reg.register_type("ReactionDiffusionEvolution", {.family = Family::Wavetable, .label = "Reaction-Diffusion (Gray-Scott)", .shelf = Shelf::Experimental},
        [](auto) { return std::make_shared<ReactionDiffusionEvolution>(); });

    reg.register_type("SortErosionEvolution", {.family = Family::Wavetable, .label = "Sort Erosion (->saw)", .shelf = Shelf::Experimental},
        [](auto) { return std::make_shared<SortErosionEvolution>(); });

    reg.register_type("CellularAutomatonEvolution", {.family = Family::Wavetable, .label = "Cellular Automaton (Wolfram)", .shelf = Shelf::Experimental},
        [](auto) { return std::make_shared<CellularAutomatonEvolution>(); });

    reg.register_type("HistogramEqualizeEvolution", {.family = Family::Wavetable, .label = "Histogram Equalize", .shelf = Shelf::Experimental},
        [](auto) { return std::make_shared<HistogramEqualizeEvolution>(); });

    reg.register_type("BezierPullEvolution", {.family = Family::Wavetable, .label = "Bezier Pull (->curve)", .shelf = Shelf::Experimental},
        [](auto) { return std::make_shared<BezierPullEvolution>(); });

    reg.register_type("BitRotateEvolution", {.family = Family::Wavetable, .label = "Bit Rotate (glitch)", .shelf = Shelf::Experimental},
        [](auto) { return std::make_shared<BitRotateEvolution>(); });

    reg.register_type("TargetEvolution", {.family = Family::Wavetable, .shelf = Shelf::Experimental},
        [](auto) { return std::make_shared<TargetEvolution>(); });

    // -----------------------------------------------------------------------
    // Additive
    // -----------------------------------------------------------------------

    // The real AdditiveSource (thin loop over Partials, was FullAdditiveSource)
    reg.register_type("AdditiveSource", {.family = Family::Additive, .label = "Full"},
        [](auto seed) {
            return std::make_shared<FullAdditiveSource>(seed.value_or(0xADD2'0000u));
        });

    // BasicAdditiveSource (was "AdditiveSource" — alias kept for backward compat)
    reg.register_type("BasicAdditiveSource", {.family = Family::Additive, .label = "Basic"},
        [](auto seed) {
            return std::make_shared<BasicAdditiveSource>(seed.value_or(0xADD1'0000u));
        });

    reg.register_type("AdditiveSource2", {.family = Family::Additive, .label = "Alternate"},
        [](auto seed) {
            auto as2 = std::make_shared<AdditiveSource2>(seed.value_or(0xADD3'0000u));
            as2->set_default_partials(500);
            return as2;
        });

    reg.register_type("FullPartials", {.family = Family::Additive},
        [](auto seed) {
            return std::make_shared<FullPartials>(seed.value_or(0xADD2'0000u));
        });

    reg.register_type("SequencePartials", {.family = Family::Additive},
        [](auto seed) {
            return std::make_shared<SequencePartials>(seed.value_or(0xADD2'0000u));
        });

    reg.register_type("ExplicitPartials", {.family = Family::Additive},
        [](auto seed) {
            return std::make_shared<ExplicitPartials>(seed.value_or(0xADD2'0000u));
        });

    reg.register_type("CompositePartials", {.family = Family::Additive},
        [](auto) { return std::make_shared<CompositePartials>(); });

    // ExpandRule node — parametric block consumed by Partials hosts via
    // a "expandRule": {"ref": "..."} field. Mirrors the Formant pattern.
    reg.register_type("ExpandRule", {.family = Family::Additive, .label = "Partials Expand Rule"},
        [](auto) { return std::make_shared<ExpandRuleNode>(); });

    reg.register_type("Formant", {.family = Family::Additive},
        [](auto) { return std::make_shared<Formant>(); });

    reg.register_type("FormantSequence", {.family = Family::Additive},
        [](auto) { return std::make_shared<FormantSequence>(); },
        // The spectra arrive through the `spectra` slot; the old loader also
        // read them under "formants", and a patch still saying so must not
        // load without its spectra in silence.
        [](Component&, const nlohmann::json& p, const ResolveParamFn&) {
            if (p.contains("formants"))
                throw std::runtime_error("FormantSequence takes its spectra under 'spectra', not 'formants'");
        });

    reg.register_type("FormantSpectrum", {.family = Family::Additive},
        [](auto) { return std::make_shared<FormantSpectrum>(); });

    reg.register_type("BandSpectrum", {.family = Family::Additive},
        [](auto) { return std::make_shared<BandSpectrum>(); },
        // Configurator: gains array
        [](Component& src, const nlohmann::json& p, const ResolveParamFn&) {
            auto& bs = static_cast<BandSpectrum&>(src);
            if (p.contains("gains")) bs.gains.values = p["gains"].get<std::vector<float>>();
        });

    reg.register_type("FixedSpectrum", {.family = Family::Additive},
        [](auto) { return std::make_shared<FixedSpectrum>(std::vector<float>{}); },
        // The gains arrive through the generic array pass; a spectrum without
        // them is an authoring error, not a silent zero.
        [](Component&, const nlohmann::json& p, const ResolveParamFn&) {
            if (!p.contains("gains")) throw std::runtime_error("FixedSpectrum requires params.gains");
        });

    // -----------------------------------------------------------------------
    // Physical
    // -----------------------------------------------------------------------

    reg.register_type("KSString", {.family = Family::Physical},
        [](auto) { return std::make_shared<KSString>(); });

    reg.register_type("AllpassResonator", {.family = Family::Physical},
        [](auto) { return std::make_shared<AllpassResonator>(); });

    reg.register_type("DelayLine", {.family = Family::Physical},
        [](auto) { return std::make_shared<DelayLineSource>(); });

    // -----------------------------------------------------------------------
    // Physical, the Experimental shelf
    // -----------------------------------------------------------------------

    reg.register_type("Mesh2D", {.family = Family::Physical, .shelf = Shelf::Experimental},
        [](auto) { return std::make_shared<Mesh2DSource>(); });

    // Pierce/Van Duyne passive nonlinear allpass (JASA 101(2), 1997) — the
    // same filter Mesh2D edgeMode 2 puts on its boundary, exposed on its own
    // so it can terminate a 1D loop.  See pierce_filter.h for why it is a
    // node rather than a mode on Shaper or Biquad.
    reg.register_type("PierceFilter", {.family = Family::Physical, .label = "Pierce Filter", .shelf = Shelf::Experimental},
        [](auto) { return std::make_shared<PierceFilterSource>(); });

    reg.register_type("BowTable", {.family = Family::Physical, .shelf = Shelf::Experimental},
        [](auto) { return std::make_shared<BowTableSource>(); });

    // -----------------------------------------------------------------------
    // Filters
    // -----------------------------------------------------------------------

    reg.register_type("DelayFilter", {.family = Family::Filter},
        [](auto) { return std::make_shared<DelayFilter>(); });

    reg.register_type("Reverb", {.family = Family::Filter},
        [](auto) { return std::make_shared<Reverb>(); });

    reg.register_type("Limiter", {.family = Family::Filter},
        [](auto) { return std::make_shared<Limiter>(); });

    reg.register_type("HammerBank", {.family = Family::Filter},
        [](auto) { return std::make_shared<HammerBank>(); });

    reg.register_type("DistortedSource", {.family = Family::Filter, .label = "Distortion"},
        [](auto seed) {
            return std::make_shared<DistortedSource>(seed.value_or(0xD157'0000u));
        });

    reg.register_type("BWBandpassFilter", {.family = Family::Filter},
        [](auto) { return std::make_shared<BWBandpassFilter>(2); });

    reg.register_type("BWLowpassFilter", {.family = Family::Filter},
        [](auto) { return std::make_shared<BWLowpassFilter>(2); },
        // Configurator: section count, "cutoff" alias
        [](Component& src, const nlohmann::json& p, const ResolveParamFn& resolve) {
            // JSON uses "cutoff" but param descriptor uses "cutoffFreq"
            if (p.contains("cutoff") && !src.set_param("cutoffFreq", resolve(p.at("cutoff"))))
                throw std::runtime_error("'cutoff' has no pin 'cutoffFreq' to land on");
        });

    reg.register_type("BWHighpassFilter", {.family = Family::Filter},
        [](auto) { return std::make_shared<BWHighpassFilter>(2); },
        [](Component& src, const nlohmann::json& p, const ResolveParamFn& resolve) {
            if (p.contains("cutoff") && !src.set_param("cutoffFreq", resolve(p.at("cutoff"))))
                throw std::runtime_error("'cutoff' has no pin 'cutoffFreq' to land on");
        });

    // 2-pole state-variable filter (AFNoding-031 rebuild): selectable
    // LP/HP/BP tap, modulatable resonance, optional divide-by-res
    // normalization. "cutoff" JSON alias matches the BW filters.
    reg.register_type("SVFSource", {.family = Family::Filter, .label = "SVF (State Variable)"},
        [](auto) { return std::make_shared<SVFSource>(); },
        [](Component& src, const nlohmann::json& p, const ResolveParamFn& resolve) {
            if (p.contains("cutoff") && !src.set_param("cutoffFreq", resolve(p.at("cutoff"))))
                throw std::runtime_error("'cutoff' has no pin 'cutoffFreq' to land on");
        });

    reg.register_type("Shaper", {.family = Family::Filter},
        [](auto) { return std::make_shared<ShaperSource>(); });

    // -----------------------------------------------------------------------
    // Filters, the Experimental shelf
    // -----------------------------------------------------------------------

    reg.register_type("Biquad", {.family = Family::Filter, .shelf = Shelf::Experimental},
        [](auto) { return std::make_shared<BiquadSource>(); });

    // -----------------------------------------------------------------------
    // Envelopes
    // -----------------------------------------------------------------------

    reg.register_type("Envelope", {.family = Family::Envelope},
        [](auto seed) {
            auto e = Envelope::make_ar(0.2f);
            if (seed) e->set_seed(*seed);
            return e;
        });

    reg.register_type("Vibrato", {.family = Family::Envelope},
        [](auto seed) {
            // Settings (speed/depth/attack/...) keep their declared defaults;
            // they are set after creation via set_setting (UI) or the
            // patch_loader special case (JSON).
            return std::make_shared<Vibrato>(seed.value_or(0xF1B0'0000u));
        });

    reg.register_type("PhasedValueSource", {.family = Family::Envelope, .label = "Phased"},
        [](auto) {
            return std::make_shared<PhasedValueSource>(0.05f);
        },
        // Configurator: overlap, stages
        [](Component& src, const nlohmann::json& p, const ResolveParamFn& resolve) {
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

    reg.register_type("SegmentSource", {.family = Family::Envelope},
        [](auto seed) {
            return std::make_shared<SegmentSource>(
                std::vector<float>{}, false, seed.value_or(0x5E6A'0000u));
        });

    // -----------------------------------------------------------------------
    // Modulators
    // -----------------------------------------------------------------------

    reg.register_type("RepeatingSource", {.family = Family::Modulator},
        [](auto seed) {
            return std::make_shared<RepeatingSource>(seed.value_or(0xAEAE'0000u));
        },
        // Configurator: duration, gap (plain floats)
        [](Component& src, const nlohmann::json& p, const ResolveParamFn&) {
            auto& rep = static_cast<RepeatingSource&>(src);
            rep.duration.value    = p.value("duration", 0.5f);
            rep.durVarPct.value   = p.value("durVarPct", 0.0f);
            rep.gapDuration.value = p.value("gap", 0.2f);
            rep.gapVarPct.value   = p.value("gapVarPct", 0.0f);
        });

    reg.register_type("VarSource", {.family = Family::Modulator},
        [](auto) {
            return std::make_shared<VarSource>(
                std::make_shared<ConstantSource>(0.0f),
                std::make_shared<ConstantSource>(0.0f),
                std::make_shared<ConstantSource>(0.0f), true);
        });

    reg.register_type("RangeSource", {.family = Family::Modulator},
        [](auto) {
            return std::make_shared<RangeSource>(
                std::make_shared<ConstantSource>(0.0f),
                std::make_shared<ConstantSource>(1.0f),
                std::make_shared<ConstantSource>(0.0f), true);
        });

    reg.register_type("CurveNode", {.family = Family::Modulator},
        [](auto) { return std::make_shared<CurveNode>(); });

    // -----------------------------------------------------------------------
    // Modulators, the Experimental shelf
    // -----------------------------------------------------------------------

    // Transition-name match (spec 2026-09-19-note-transitions §5). The
    // `name` string and its vocabulary resolution are a loader special
    // case (SettingType has no string).
    reg.register_type("NameGate", {.family = Family::Modulator, .shelf = Shelf::Experimental},
        [](auto) { return std::make_shared<NameGate>(); });

    // -----------------------------------------------------------------------
    // Combiners
    // -----------------------------------------------------------------------

    reg.register_type("CombinedSource", {.family = Family::Combiner},
        [](auto) {
            return std::make_shared<CombinedSource>(
                std::make_shared<ConstantSource>(0.0f),
                std::make_shared<ConstantSource>(0.0f),
                CombineOp::Add, 0.0f);
        },
        // Configurator: operation enum, gainAdj
        [](Component& src, const nlohmann::json& p, const ResolveParamFn&) {
            auto& cs = static_cast<CombinedSource&>(src);
            // Legacy patches write `operation` as a NUMBER (a C# enum ordinal);
            // current ones write a string. Accept both, but do NOT quietly fall
            // back to Add on anything unrecognised — silently rendering an
            // operation nobody asked for is exactly the failure mode this
            // loader is full of. Say so instead. Ordinals must stay in step
            // with CombineOp and the kOpLabels dropdown in combined_source.h.
            if (p.contains("operation") && p.at("operation").is_number()) {
                int op = p.at("operation").get<int>();
                switch (op) {
                    case 0: cs.operation.value = int(CombineOp::Add);      break;
                    case 1: cs.operation.value = int(CombineOp::Multiply); break;
                    case 2: cs.operation.value = int(CombineOp::Fade);     break;
                    case 3: cs.operation.value = int(CombineOp::Sum);      break;
                    default:
                        throw std::runtime_error(
                            "CombinedSource 'operation': ordinal " + std::to_string(op) +
                            " has no equivalent (0=add, 1=multiply, 2=fade, 3=sum). "
                            "Give it as a string instead.");
                }
            } else {
                std::string opStr = p.value("operation", std::string("add"));
                if      (opStr == "add" || opStr == "mix") cs.operation.value = int(CombineOp::Add);
                else if (opStr == "multiply")              cs.operation.value = int(CombineOp::Multiply);
                else if (opStr == "fade")                  cs.operation.value = int(CombineOp::Fade);
                else if (opStr == "sum")                   cs.operation.value = int(CombineOp::Sum);
                else
                    throw std::runtime_error(
                        "CombinedSource 'operation': unknown value \"" + opStr +
                        "\" (expected add|mix|multiply|fade|sum).");
            }
            cs.gainAdj.value = p.value("gainAdj", 0.0f);
        });

    reg.register_type("CrossfadeSource", {.family = Family::Combiner},
        [](auto) { return std::make_shared<CrossfadeSource>(); },
        // Configurator: ratio, overlap, gainAdj
        [](Component& src, const nlohmann::json& p, const ResolveParamFn&) {
            auto& cs = static_cast<CrossfadeSource&>(src);
            cs.ratio.value   = p.value("ratio", 0.5f);
            cs.overlap.value = p.value("overlap", 0.1f);
            cs.gainAdj.value = p.value("gainAdj", 0.0f);
        });

    reg.register_type("MultiSource", {.family = Family::Combiner},
        [](auto) { return std::make_shared<MultiSource>(); },
        // Configurator: per-source weights. Array items can be either a
        // plain ref ({"ref":"id"}) → weight 1.0, OR an object with both ref
        // and weight ({"ref":"id", "weight": 0.5}). The generic wire_params
        // pass has already added entries via add_param at weight 1.0; this
        // configurator overrides weights in-place where specified.
        [](Component& src, const nlohmann::json& p, const ResolveParamFn&) {
            auto& ms = static_cast<MultiSource&>(src);
            if (!p.contains("source") || !p["source"].is_array()) return;
            const auto& arr = p["source"];
            // Iterate entries in parallel with ms.entries (same order — the
            // wire_params pass added them in JSON order).
            int n = std::min(int(arr.size()), int(ms.entries.size()));
            for (int i = 0; i < n; ++i) {
                const auto& item = arr[i];
                if (item.is_object() && item.contains("weight"))
                    ms.entries[i].weight = item["weight"].get<float>();
                if (item.is_object() && item.contains("delay"))
                    ms.entries[i].delaySamples = item["delay"].get<int>();
            }
        });

    reg.register_type("MultiplexSource", {.family = Family::Combiner},
        [](auto) { return std::make_shared<MultiplexSource>(); });

    // -----------------------------------------------------------------------
    // Utility
    // -----------------------------------------------------------------------

    // Pure pass-through; UI draws wormhole pairs with the connecting wire
    // hidden (wormhole_source.h).
    reg.register_type("Wormhole", {.family = Family::Utility},
        [](auto) { return std::make_shared<WormholeSource>(); });

    // -----------------------------------------------------------------------
    // Utility, the Experimental shelf
    // -----------------------------------------------------------------------

    reg.register_type("StaticVarSource", {.family = Family::Utility, .shelf = Shelf::Experimental},
        [](auto seed) {
            // Needs all config from JSON — use default, configurator sets it
            return std::make_shared<StaticVarSource>(1.0f, 0.0f, 0.0f,
                seed.value_or(0x57A7'0001u));
        },
        [](Component& src, const nlohmann::json& p, const ResolveParamFn&) {
            auto& sv = static_cast<StaticVarSource&>(src);
            sv.baseValue = p.value("baseValue", 1.0f);
            sv.varPct    = p.value("varPct", 0.0f);
            sv.bias      = p.value("bias", 0.0f);
        });

    reg.register_type("StaticRangeSource", {.family = Family::Utility, .shelf = Shelf::Experimental},
        [](auto seed) {
            return std::make_shared<StaticRangeSource>(0.0f, 1.0f, 0.0f,
                seed.value_or(0x57A7'0002u));
        },
        [](Component& src, const nlohmann::json& p, const ResolveParamFn&) {
            auto& sr = static_cast<StaticRangeSource&>(src);
            sr.min  = p.value("min", 0.0f);
            sr.max  = p.value("max", 1.0f);
            sr.bias = p.value("bias", 0.0f);
        });

}

} // namespace mforce
