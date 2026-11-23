#include "mforce/testing/check.h"
#include "mforce/core/component.h"
#include "mforce/core/source_registry.h"
#include "mforce/source/envelope/envelope.h"
#include "mforce/source/register_all_sources.h"
#include <cstdio>
#include <exception>
#include <set>
#include <string>
#include <vector>

using namespace mforce;


// The one classification, declared where a type is registered: every type's
// family, label and shelf (the families table in docs/architecture/part3-nodes.md;
// the shelf is today's menu — everything not in a main family menu is Experimental,
// and a type the host makes by an item of its own is on no shelf), the label
// derived from the type name where today's menu had none, the loading alias
// still building its type, and labels unique within a family and shelf.
struct FamilyRow { const char* name; Family family; const char* label; Shelf shelf; };
static const FamilyRow kFamilyTable[] = {
    {"SineSource", Family::Oscillator, "Sine", Shelf::Main},
    {"SawSource", Family::Oscillator, "Saw", Shelf::Main},
    {"PulseSource", Family::Oscillator, "Pulse", Shelf::Main},
    {"TriangleSource", Family::Oscillator, "Triangle", Shelf::Main},
    {"FMSource", Family::Oscillator, "FM", Shelf::Main},
    {"RedNoiseSource", Family::Noise, "Red", Shelf::Main},
    {"PinkNoiseSource", Family::Noise, "Pink", Shelf::Main},
    {"WhiteNoiseSource", Family::Noise, "White", Shelf::Main},
    {"BlueNoiseSource", Family::Noise, "Blue", Shelf::Main},
    {"VioletNoiseSource", Family::Noise, "Violet", Shelf::Main},
    {"VelvetNoiseSource", Family::Noise, "Velvet", Shelf::Main},
    {"PerlinNoiseSource", Family::Noise, "Perlin", Shelf::Main},
    {"CrackleNoiseSource", Family::Noise, "Crackle", Shelf::Main},
    {"WavetableSource", Family::Wavetable, "Wavetable", Shelf::Main},
    {"HybridKSSource", Family::Wavetable, "Hybrid KS", Shelf::Main},
    {"EKSEvolution", Family::Wavetable, "EKS Evolution", Shelf::Main},
    {"PluckEvolution", Family::Wavetable, "Pluck Evolution", Shelf::Main},
    {"AveragingEvolution", Family::Wavetable, "Averaging Evolution", Shelf::Main},
    {"AdditiveSource", Family::Additive, "Additive", Shelf::Main},
    {"FullPartials", Family::Additive, "Full Partials", Shelf::Main},
    {"SequencePartials", Family::Additive, "Sequence Partials", Shelf::Main},
    {"ExplicitPartials", Family::Additive, "Explicit Partials", Shelf::Main},
    {"CompositePartials", Family::Additive, "Composite Partials", Shelf::Main},
    {"ExpandRule", Family::Additive, "Partials Expand Rule", Shelf::Main},
    {"FormantPeaks", Family::Additive, "Formant Peaks", Shelf::Main},
    {"FormantBands", Family::Additive, "Formant Bands", Shelf::Main},
    {"FormantSequence", Family::Additive, "Formant Sequence", Shelf::Main},
    {"KSString", Family::Physical, "KS String", Shelf::Main},
    {"AllpassResonator", Family::Physical, "Allpass Resonator", Shelf::Main},
    {"DelayLine", Family::Physical, "Delay Line", Shelf::Main},
    {"DelayFilter", Family::Filter, "Delay", Shelf::Main},
    {"Reverb", Family::Filter, "Reverb", Shelf::Main},
    {"Limiter", Family::Filter, "Limiter", Shelf::Main},
    {"HammerBank", Family::Filter, "Hammer Bank", Shelf::Main},
    {"DistortedSource", Family::Filter, "Distortion", Shelf::Main},
    {"BWBandpassFilter", Family::Filter, "BW Bandpass", Shelf::Main},
    {"BWLowpassFilter", Family::Filter, "BW Lowpass", Shelf::Main},
    {"BWHighpassFilter", Family::Filter, "BW Highpass", Shelf::Main},
    {"SVFSource", Family::Filter, "SVF (State Variable)", Shelf::Main},
    {"Shaper", Family::Filter, "Shaper", Shelf::Main},
    {"Envelope", Family::Envelope, "Envelope", Shelf::Main},
    {"Vibrato", Family::Envelope, "Vibrato", Shelf::Main},
    {"PhasedValueSource", Family::Envelope, "Phased", Shelf::Main},
    {"SegmentSource", Family::Envelope, "Segment", Shelf::Main},
    {"RepeatingSource", Family::Modulator, "Repeating", Shelf::Main},
    {"VarSource", Family::Modulator, "Var", Shelf::Main},
    {"RangeSource", Family::Modulator, "Range", Shelf::Main},
    {"CurveNode", Family::Modulator, "Curve", Shelf::Main},
    {"CombinedSource", Family::Combiner, "Combined", Shelf::Main},
    {"CrossfadeSource", Family::Combiner, "Crossfade", Shelf::Main},
    {"MultiSource", Family::Combiner, "Multi", Shelf::Main},
    {"MultiplexSource", Family::Combiner, "Multiplex", Shelf::Main},
    {"Wormhole", Family::Utility, "Wormhole", Shelf::Main},
    {"LayeredRedNoiseSource", Family::Noise, "Layered Red", Shelf::Experimental},
    {"MurmurationNoiseSource", Family::Noise, "Murmuration", Shelf::Experimental},
    {"WanderNoiseSource", Family::Noise, "Wander 1", Shelf::Experimental},
    {"WanderNoise2Source", Family::Noise, "Wander 2", Shelf::Experimental},
    {"WanderNoise3Source", Family::Noise, "Wander 3", Shelf::Experimental},
    {"BowedStringEvolution", Family::Wavetable, "Bowed String Evolution", Shelf::Experimental},
    {"ReedEvolution", Family::Wavetable, "Reed Evolution", Shelf::Experimental},
    {"BrassEvolution", Family::Wavetable, "Brass Evolution", Shelf::Experimental},
    {"ReactionDiffusionEvolution", Family::Wavetable, "Reaction-Diffusion (Gray-Scott)", Shelf::Experimental},
    {"SortErosionEvolution", Family::Wavetable, "Sort Erosion (->saw)", Shelf::Experimental},
    {"CellularAutomatonEvolution", Family::Wavetable, "Cellular Automaton (Wolfram)", Shelf::Experimental},
    {"HistogramEqualizeEvolution", Family::Wavetable, "Histogram Equalize", Shelf::Experimental},
    {"BezierPullEvolution", Family::Wavetable, "Bezier Pull (->curve)", Shelf::Experimental},
    {"BitRotateEvolution", Family::Wavetable, "Bit Rotate (glitch)", Shelf::Experimental},
    {"TargetEvolution", Family::Wavetable, "Target Evolution", Shelf::Experimental},
    {"Mesh2D", Family::Physical, "Mesh 2D", Shelf::Experimental},
    {"PierceFilter", Family::Physical, "Pierce Filter", Shelf::Experimental},
    {"BowTable", Family::Physical, "Bow Table", Shelf::Experimental},
    {"Biquad", Family::Filter, "Biquad", Shelf::Experimental},
    {"NameGate", Family::Modulator, "Name Gate", Shelf::Experimental},
    {"StaticVarSource", Family::Utility, "Static Var", Shelf::Experimental},
    {"StaticRangeSource", Family::Utility, "Static Range", Shelf::Experimental},
    {"PerformNode", Family::Modulator, "Note Field", Shelf::None},
    {"SoundChannel", Family::Output, "Channel", Shelf::None},
    {"StereoMixer", Family::Output, "Mixer", Shelf::None},
};

// The registry refuses a name it does not know, naming it; label() and shelf()
// share family()'s lookup, so the one path stands for the three.
static bool throws_with(const SourceRegistry& reg, const char* name) {
    try { (void)reg.family(name); } catch (const std::exception& e) {
        return std::string(e.what()).find(name) != std::string::npos;
    }
    return false;
}

void run_registry_family_tests() {
    std::printf("the registry: families, labels, shelves\n");
    register_all_sources();
    auto& reg = SourceRegistry::instance();
    int main = 0, experimental = 0, none = 0;
    for (const auto& row : kFamilyTable) {
        const TypeTag* tag = reg.find(row.name);
        if (!tag) { std::printf("  in kFamilyTable but not registered: %s\n", row.name); CHECK(tag != nullptr); continue; }
        const std::string label = reg.label(row.name);
        if (tag->family != row.family || label != row.label || tag->shelf != row.shelf)
            std::printf("  %s: %s / %s / %d\n", row.name, family_name(tag->family), label.c_str(), int(tag->shelf));
        CHECK(tag->family == row.family);
        CHECK(label == row.label);
        CHECK(tag->shelf == row.shelf);
        (row.shelf == Shelf::Main ? main : row.shelf == Shelf::Experimental ? experimental : none)++;
    }
    CHECK(main == 53 && experimental == 22 && none == 3);
    for (const auto& e : reg.registered_types()) {                             // every registered type is in the table
        bool inTable = false;
        for (const auto& row : kFamilyTable) inTable = inTable || e.name == row.name;
        if (!inTable) std::printf("  registered but not in kFamilyTable: %s\n", e.name.c_str());
        CHECK(inTable);
    }
    // I11-style guard: a label is given only where it differs from the derivation
    for (const auto& e : reg.registered_types()) {
        if (e.tag.label && std::string(e.tag.label) == derived_label(e.name))
            std::printf("  redundant label on %s: %s\n", e.name.c_str(), e.tag.label);
        CHECK(!e.tag.label || std::string(e.tag.label) != derived_label(e.name));
    }
    // the derivation: the suffix off, a space before each word, acronyms kept
    CHECK(derived_label("HybridKSSource") == "Hybrid KS" && derived_label("BWLowpassFilter") == "BW Lowpass");
    CHECK(derived_label("Mesh2D") == "Mesh 2D" && derived_label("PierceFilterSource") == "Pierce Filter");
    CHECK(derived_label("Envelope") == "Envelope" && derived_label("CurveNode") == "Curve");
    CHECK(derived_label("WanderNoise2Source") == "Wander Noise 2" && derived_label("StaticVarSource") == "Static Var");
    // the alias still builds its type, on the Experimental shelf of its family
    // labels are non-empty and unique within a family and shelf; types_in walks every type once
    int walked = 0;
    for (Family f : kFamilyOrder)
        for (Shelf s : {Shelf::Main, Shelf::Experimental, Shelf::None}) {
            std::set<std::string> labels;
            for (const auto& e : reg.types_in(f, s)) {
                CHECK(e.tag.family == f);
                CHECK(!reg.label(e.name).empty());
                CHECK(labels.insert(reg.label(e.name)).second);
                ++walked;
            }
        }
    CHECK(walked == 78);
    CHECK(std::string(family_name(Family::Oscillator)) == "Oscillators" && std::string(family_name(Family::Noise)) == "Noise"
          && std::string(family_name(Family::Filter)) == "Filters" && std::string(family_name(Family::Utility)) == "Utility");
    CHECK(reg.find("NoSuchType") == nullptr && reg.find("SineSource") != nullptr);
    CHECK(reg.find("SineSource")->family == Family::Oscillator);
    CHECK(throws_with(reg, "NoSuchType"));                                      // unknown: no invented answer
    // the Envelope family is what the editor's envelope views read: both shelves, exactly these
    std::set<std::string> envelopes;
    for (Shelf s : {Shelf::Main, Shelf::Experimental})
        for (const auto& e : reg.types_in(Family::Envelope, s)) envelopes.insert(e.name);
    CHECK((envelopes == std::set<std::string>{"Envelope", "PhasedValueSource", "SegmentSource", "Vibrato"}));
}

// The menu's content is the registry's: for each family and shelf, the types
// in the order the menu lists them — today's menu order, which registration
// order now carries. This is what both the create menu and the Replace-with
// menu draw.
struct MenuRow { Family family; Shelf shelf; std::vector<const char*> names; };
static const MenuRow kMenuTable[] = {
    {Family::Oscillator, Shelf::Main, {"SineSource", "SawSource", "PulseSource", "TriangleSource", "FMSource"}},
    {Family::Noise, Shelf::Main, {"RedNoiseSource", "PinkNoiseSource", "WhiteNoiseSource", "BlueNoiseSource", "VioletNoiseSource", "VelvetNoiseSource", "PerlinNoiseSource", "CrackleNoiseSource"}},
    {Family::Noise, Shelf::Experimental, {"LayeredRedNoiseSource", "MurmurationNoiseSource", "WanderNoiseSource", "WanderNoise2Source", "WanderNoise3Source"}},
    {Family::Wavetable, Shelf::Main, {"WavetableSource", "HybridKSSource", "EKSEvolution", "PluckEvolution", "AveragingEvolution"}},
    {Family::Wavetable, Shelf::Experimental, {"BowedStringEvolution", "ReedEvolution", "BrassEvolution", "ReactionDiffusionEvolution", "SortErosionEvolution", "CellularAutomatonEvolution", "HistogramEqualizeEvolution", "BezierPullEvolution", "BitRotateEvolution", "TargetEvolution"}},
    {Family::Additive, Shelf::Main, {"AdditiveSource", "FullPartials", "SequencePartials", "ExplicitPartials", "CompositePartials", "ExpandRule", "FormantPeaks", "FormantBands", "FormantSequence"}},
    {Family::Physical, Shelf::Main, {"KSString", "AllpassResonator", "DelayLine"}},
    {Family::Physical, Shelf::Experimental, {"Mesh2D", "PierceFilter", "BowTable"}},
    {Family::Filter, Shelf::Main, {"DelayFilter", "Reverb", "Limiter", "HammerBank", "DistortedSource", "BWBandpassFilter", "BWLowpassFilter", "BWHighpassFilter", "SVFSource", "Shaper"}},
    {Family::Filter, Shelf::Experimental, {"Biquad"}},
    {Family::Envelope, Shelf::Main, {"Envelope", "Vibrato", "PhasedValueSource", "SegmentSource"}},
    {Family::Modulator, Shelf::Main, {"RepeatingSource", "VarSource", "RangeSource", "CurveNode"}},
    {Family::Modulator, Shelf::Experimental, {"NameGate"}},
    {Family::Combiner, Shelf::Main, {"CombinedSource", "CrossfadeSource", "MultiSource", "MultiplexSource"}},
    {Family::Utility, Shelf::Main, {"Wormhole"}},
    {Family::Utility, Shelf::Experimental, {"StaticVarSource", "StaticRangeSource"}},
};

void run_menu_content_tests() {
    std::printf("the menu: each family's types in menu order\n");
    register_all_sources();
    auto& reg = SourceRegistry::instance();
    for (const auto& row : kMenuTable) {
        std::vector<std::string> got;
        for (const auto& e : reg.types_in(row.family, row.shelf)) got.push_back(e.name);
        std::vector<std::string> want(row.names.begin(), row.names.end());
        if (got != want) {
            std::printf("  %s/%s: got", family_name(row.family), row.shelf == Shelf::Main ? "main" : "experimental");
            for (const auto& n : got) std::printf(" %s", n.c_str());
            std::printf("\n");
        }
        CHECK(got == want);
    }
    // a family with nothing on a shelf draws nothing: no placeholder
    CHECK(reg.types_in(Family::Oscillator, Shelf::Experimental).empty());
    CHECK(reg.types_in(Family::Additive, Shelf::Experimental).empty());
}

// The third shelf: the three types a host makes by an item of its own sit on
// no generated menu's shelf. Main and Experimental hold exactly the 75 types
// they held; Output's two and the note field are on None alone.
void run_registry_shelf_none_tests() {
    std::printf("the registry: the types no generated menu offers\n");
    register_all_sources();
    auto& reg = SourceRegistry::instance();
    const std::set<std::string> none = {"PerformNode", "SoundChannel", "StereoMixer"};
    int shelved = 0;
    for (Family f : kFamilyOrder)
        for (Shelf s : {Shelf::Main, Shelf::Experimental})
            for (const auto& e : reg.types_in(f, s)) {
                CHECK(none.count(e.name) == 0);
                ++shelved;
            }
    CHECK(shelved == 75);
    auto names = [&](Family f, Shelf s) {
        std::vector<std::string> out;
        for (const auto& e : reg.types_in(f, s)) out.push_back(e.name);
        return out;
    };
    CHECK((names(Family::Output, Shelf::None) == std::vector<std::string>{"SoundChannel", "StereoMixer"}));
    CHECK((names(Family::Modulator, Shelf::None) == std::vector<std::string>{"PerformNode"}));
    CHECK(names(Family::Output, Shelf::Main).empty() && names(Family::Output, Shelf::Experimental).empty());
    int onNone = 0;
    for (Family f : kFamilyOrder) onNone += int(names(f, Shelf::None).size());
    CHECK(onNone == 3);
    CHECK(std::string(family_name(Family::Output)) == "Output");
    CHECK(kFamilyOrder[kFamilyCount - 1] == Family::Output);
}
