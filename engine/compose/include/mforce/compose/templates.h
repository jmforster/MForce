#pragma once
#include "mforce/music/basics.h"
#include "mforce/music/figures.h"
#include "mforce/compose/figure_transforms.h"
#include "mforce/compose/figure_constraints.h"
#include "mforce/music/structure.h"
#include "mforce/compose/strategy_choice.h"
#include "mforce/voicing/voicing_profile.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <optional>
#include <variant>
#include <set>
#include <algorithm>
#include <stdexcept>
#include <cstdint>

namespace mforce {

// ===========================================================================
// FigureTemplate — the atomic unit of the composition recipe
// ===========================================================================

// ===========================================================================
// MelodicFunction — what role a phrase plays in the musical structure
// ===========================================================================

enum class MelodicFunction {
    Free,         // no constraint (current behavior)
    Statement,    // establishing a motif: clear contour, moderate range
    Development,  // extending/varying: sequences, wider range, fragmentation
    Transition,   // moving between areas: less committed contour
    Cadential,    // resolving: stepwise to target, longer notes, narrowing range
};

// ===========================================================================
// FigureShape — named melodic contour types
// ===========================================================================

enum class FigureShape {
    Free,              // composer uses random_sequence (legacy)
    ScalarRun,         // consecutive steps in one direction
    RepeatedNote,      // same pitch repeated N times
    HeldNote,          // single long note
    CadentialApproach, // stepwise approach to target + held arrival
    TriadicOutline,    // chord-tone outline (root-3rd-5th)
    NeighborTone,      // note, step away, return
    LeapAndFill,       // large leap + stepwise fill back
    ScalarReturn,      // out stepwise, return (arch)
    Anacrusis,         // pickup notes to downbeat
    Zigzag,            // step-up, skip-down alternation
    Fanfare,           // leaps outlining 4th/5th/octave
    Sigh,              // descending step pair
    Suspension,        // held note resolving down
    Cambiata,          // step, skip opposite, step
    Skipping,          // direction-contoured motion by thirds/fourths, musical rhythm
    Stepping,          // direction-contoured stepwise motion, musical rhythm
};

// ===========================================================================
// FigureDirection — contour shape used by Skipping/Stepping strategies
// ===========================================================================

enum class FigureDirection {
    Ascending,              // overall upward motion
    Descending,             // overall downward motion
    TurnaroundAscending,    // start with 1 step down then go up
    TurnaroundDescending,   // start with 1 step up then go down
    AscendingArc,           // up then down (arch)
    DescendingArc,          // down then up (inverse arch)
    SineAscending,          // up, down past start, back up
    SineDescending,         // down, up past start, back down
    Random,                 // each step direction chosen randomly
};

enum class StepMode { Scale, ChordTone };

enum class FigureSource {
    Generate,    // composer creates from constraints
    Reference,   // use a seed figure directly
    Transform,   // derive from a seed or previous figure
    Locked,      // fixed content, don't touch
    Literal,     // user-authored note list (pitch + duration per note)
};

// TransformOp lives in mforce/music/figure_transforms.h (moved there with
// figure_transforms::apply). Included transitively via figure_transforms.h.

// Declared here (above FigureTemplate) because FigureTemplate::role
// is std::optional<MotifRole>. Motif (further down) also uses these.
enum class MotifRole {
    Thematic,      // main memorable melodic idea
    Cadential,     // approach to a cadence
    PostCadential, // post-cadence tag / codetta-like extension
    Discursive,    // continuation / development material
    Climactic,     // arrival / high-point material
    Connective,    // bridge / pickup / link between larger units
    Ornamental,    // decorative filigree (spelled-out trill, turn-figure)
};

enum class MotifOrigin {
    User,        // authored by hand in UI / JSON
    Generated,   // produced by a procedural generator
    Derived,     // created via transform of another motif
    Extracted,   // pulled from a model / corpus (future-proofing)
};

struct FigureTemplate {
    FigureSource source{FigureSource::Generate};

    // --- For Generate: constraints ---
    int minNotes{2};
    int maxNotes{8};
    float totalBeats{0.0f};        // 0 = composer decides
    float defaultPulse{0.0f};      // 0 = composer decides
    bool preferStepwise{false};
    bool preferSkips{false};
    int targetNet{0};              // required net pitch movement (0 = free)

    // --- For Reference / Transform ---
    std::string motifName;          // references a Motif by name
    TransformOp transform{TransformOp::None};
    int transformParam{0};         // e.g. step offset for Replicate

    // --- For shape-based generation ---
    FigureShape shape{FigureShape::Free};
    int shapeDirection{1};         // +1 = ascending, -1 = descending
    int shapeParam{0};             // type-specific: leap size, extent, etc.
    int shapeParam2{0};            // second parameter where needed
    FigureDirection direction{FigureDirection::Ascending};  // used by Skipping/Stepping strategies

    // --- Role in this specific use (distinct from Motif.roles "toolbox capabilities").
    // If set, strategies that care about per-figure semantic role (e.g.
    // apply_cadence skipping Connective figures) read this field directly.
    // If unset AND source=Reference AND motifName is set, plan-phase
    // propagation may copy the Motif's role (if unambiguous).
    std::optional<MotifRole> role;

    // --- For Locked ---
    std::optional<MelodicFigure> lockedFigure;

    // --- For Literal ---
    struct LiteralNote {
      std::optional<Pitch> pitch;
      float duration{1.0f};  // in beats
      bool rest{false};      // true = silence; pitch is ignored when true
    };
    std::vector<LiteralNote> literalNotes;

    // --- For motif atom references ---
    std::string rhythmMotifName;            // name of a PulseSequence motif
    std::string contourMotifName;           // name of a StepSequence motif
    std::string rhythmTransform;            // transform to apply: "retrograde", "stretch", "compress"
    float rhythmTransformParam{0};          // factor for stretch/compress
    std::string contourTransform;           // transform to apply: "invert", "retrograde", "expand", "contract"
    float contourTransformParam{0};         // factor for expand/contract

    // --- Step constraints ---
    int maxStep{0};                // max absolute step size per note (0 = unconstrained)

    // --- Step interpretation ---
    StepMode stepMode{StepMode::Scale};

    // --- Cadential targeting (for cadential_approach shape) ---
    // 0=none, 1=half (target V), 2=full (target I)
    int figureCadenceType{0};
    bool perfect{true};            // true=root, false=3rd or 5th of target chord

    // --- State ---
    uint32_t seed{0};              // generation seed for reproducibility
    bool locked{false};            // user has accepted this result
};

// ===========================================================================
// Motifs — raw thematic material, stored at Piece level
// ===========================================================================

// MotifRole / MotifOrigin declarations moved above FigureTemplate so
// FigureTemplate::role (std::optional<MotifRole>) compiles.

struct Motif {
    std::string name;              // "main_theme", "hook", "bridge_motif"

    using Content = std::variant<MelodicFigure, PulseSequence, StepSequence>;
    Content content;               // the actual musical content (figure, rhythm, or contour)

    bool userProvided{false};      // true = user entered, don't regenerate
    uint32_t generationSeed{0};    // for reproducibility if generated
    std::optional<FigureTemplate> constraints;  // generation constraints when !userProvided

    // --- New metadata (2026-04-15 umbrella spec) ---
    std::set<MotifRole> roles;                     // multi-tag; empty default
    MotifOrigin origin{MotifOrigin::User};
    std::optional<std::string> derivedFrom;        // parent motif name (walk for root)
    std::optional<TransformOp> transform;          // how derivation was done
    int transformParam{0};                         // transform-specific

    bool is_figure()  const { return std::holds_alternative<MelodicFigure>(content); }
    bool is_rhythm()  const { return std::holds_alternative<PulseSequence>(content); }
    bool is_contour() const { return std::holds_alternative<StepSequence>(content); }

    const MelodicFigure& figure()  const { return std::get<MelodicFigure>(content); }
    const PulseSequence& rhythm()  const { return std::get<PulseSequence>(content); }
    const StepSequence&  contour() const { return std::get<StepSequence>(content); }
};

// ===========================================================================
// PhraseTemplate — sequence of FigureTemplates
// ===========================================================================

struct PhraseTemplate {
    std::string name;                          // "antecedent", "bridge", etc.
    std::optional<Pitch> startingPitch;        // where to begin (may come from context)
    bool parallel{false};                      // restart at the passage's startingPitch
                                               // (parallel period) instead of continuing
                                               // from the running pitch
    std::vector<FigureTemplate> figures;

    // DENSE array, same length as figures: connectors[i] is the bridge
    // INTO figures[i], so element 0 is a dummy (author it null). Empty
    // vector means "no connectors anywhere" (pure append).
    //   JSON:  "figures": [A, B, C],  "connectors": [null, 1, -1]
    // REPEAT-OFFENDER WARNING (bit the 2026-09-21 structural-Mary round):
    // this is NOT an N-1 "between figures" list. Authoring [1, -1] for
    // three figures shifts figure 0 by +1 — realize_events applies
    // connectors[f].leadStep to figure f's FIRST note.
    std::vector<std::optional<FigureConnector>> connectors;

    // Phrase-level constraints
    float totalBeats{0.0f};                    // 0 = sum of figures
    int cadenceType{0};                        // 0=none, 1=half, 2=full
    int cadenceTarget{-1};                     // target scale degree (-1 = composer decides)

    // How a Cadential phrase's ARRIVAL figure is shaped (comp #9).
    // "held"     — the arrival is one long note. apply_cadence then has a
    //              single unit to work with, so build_approach_steps takes
    //              its far branch and LEAPS straight onto the target: the
    //              tonic is sustained but nothing approaches it. This is the
    //              standing workaround from 2026-04, kept as the default
    //              because replacing it is a taste question.
    // "approach" — the arrival is a CadentialApproach figure, so
    //              rebuild_cadential_tail can do what it was written for:
    //              step into the target and settle on a final note of at
    //              least one beat.
    // Empty = "held".
    std::string cadentialArrival;
    MelodicFunction function{MelodicFunction::Free}; // drives shape selection for Free figures

    // State
    uint32_t seed{0};
    bool locked{false};

    // The phrase strategy and its settings. Empty name = default_phrase.
    StrategyChoice strategy;
};

// ===========================================================================
// ChordAccompanimentConfig
// ===========================================================================

// Voicing hints for a harmony part's chords: the octave, and the inversion
// and spread used when the passage names no voicing selector. The rhythm a
// part strikes its chords in is its realization's settings.
struct ChordAccompanimentConfig {
    int octave{3};
    int inversion{0};
    int spread{0};
};

// ===========================================================================
// PassageTemplate — what a Part plays during a Section
// ===========================================================================

struct PassageTemplate {
    std::string name;
    std::optional<Pitch> startingPitch;
    std::vector<PhraseTemplate> phrases;

    // Passage-level directives
    std::string character;
    std::string fromKey;
    std::string toKey;

    // Scale name (e.g. "Major", "Harmonic Minor") pinning THIS passage's
    // scale, resolved against the piece key. Also the opt-out from section
    // key contexts: Passage::scaleOverride already turned key-awareness off
    // at realize time, but nothing could set it from a template — so a pedal
    // part in a modulating section had no way to refuse to modulate, and its
    // held note got snapped into each new scale.
    std::string scaleOverride;

    // The passage strategy and its settings. Empty name = default_passage.
    StrategyChoice strategy;

    // Quantize the passage's total length to this grid, in beats, by adjusting
    // the LAST note (comp #18 — 17 of 23 passage renders ended mid-beat).
    // 1.0 = integer beat (default), 4.0 = a 4/4 barline, 0 = leave it alone.
    // Rounds to the NEAREST multiple, not up: run 17 measured ceiling-to-the-
    // next-barline as the dominant term in the phrase path's ending overshoot.
    // See docs/superpowers/specs/2026-08-06-passage-end-grid-design.md.
    float endGrid{1.0f};

    // State
    uint32_t seed{0};
    bool locked{false};

    // Chord accompaniment config (optional): the voicing hints of a
    // Harmony-role part (octave/inversion/spread).
    std::optional<ChordAccompanimentConfig> chordConfig;

    // Passage-local harmony (optional). When present, a Harmony-role part
    // realizes THIS progression for this section instead of the section's
    // harmonyTimeline — so two passages inside one section can carry
    // different chords, and a passage can author an altered chord (e.g.
    // bVI7 = a German sixth) without touching section harmony. The section
    // timeline is left alone: melody chord-tone lookups still read it.
    std::optional<ChordProgression> chordProgression;

    // How a Harmony-role part's chords become events: the realization
    // strategy and its settings. Empty name = "block".
    StrategyChoice realization;

    // Name of a registered VoicingSelector. Empty = legacy behavior
    // (use ChordAccompanimentConfig.inversion/spread uniformly per chord).
    // Consumed by Composer::realize_chord_parts_ for Harmony-role parts.
    std::string voicingSelector;

    // Baseline profile (priority + inversion/spread allow-lists) for the
    // VoicingSelector. When no VoicingProfileSelector is configured, this
    // profile is used for every chord. When one is configured, this serves
    // as the baseline the profile selector modulates.
    VoicingProfile voicingProfile;

    // The VoicingProfileSelector that emits a VoicingProfile per chord, and
    // its settings. Empty name = "static" (uses voicingProfile unchanged for
    // every chord).
    StrategyChoice voicingProfileSelector;

    // ChordDictionary name used by the VoicingSelector for candidate voicings.
    // Empty = "Canonic" (smallest-interval close voicings). Names like "Piano",
    // "Guitar-Bar-6", etc. pull wider/idiomatic voicings from the registry.
    std::string voicingDictionary;

    // Comp crawl (spec 2026-09-21): melody authored as a passage string in
    // a .psg file ('|' = structural phrase boundary). When set,
    // apply_passage_melodies() derives phrases from it, replacing phrases[].
    std::string melodyPassageFile;
    int melodyOctave{5};   // house octave anchor for octave-less note names

    // Comp walk (spec 2026-09-21-comp-walk1 §3): ""/"authored" = phrase
    // startingPitch + connectors are template data (default, byte-identical
    // behavior); "harmonic" = anchor_selector.h derives them per phrase
    // from the section chord timeline (rules R1-R3 + weighted chord-tone
    // preference). Under harmonic mode the authored startingPitch anchors
    // only the REGISTER window, not the pitch.
    std::string anchorMode;
    // genre profile name (styles/<name>.json "melody" block); required when anchorMode == "harmonic"
    std::string melodyProfile;
};

// ===========================================================================
// PartTemplate — one voice/instrument across all sections
// ===========================================================================

enum class PartRole {
    Melody,
    Bass,
    Harmony,
    Drums,
    Countermelody,
    Pad,
};

struct PartTemplate {
    std::string name;
    PartRole role{PartRole::Melody};
    std::string instrumentPatch;      // patch file for rendering

    // One PassageTemplate per section (keyed by section name)
    std::unordered_map<std::string, PassageTemplate> passages;
};

// ===========================================================================
// PieceTemplate — top-level composition template
// ===========================================================================

struct PieceTemplate {
    // Musical identity
    std::string keyName{"C"};
    std::string scaleName{"Major"};
    Meter meter{Meter::M_4_4};
    float bpm{120.0f};

    // Motif material — declarations.
    std::vector<Motif> motifs;

    // Motif material — realized content, populated by mforce::realize_motifs
    // during Composer::setup_piece_. Keyed by motif name. Strategies read
    // these during the compose phase; during plan phase (Plan B) strategies
    // may add to them via add_motif.
    std::unordered_map<std::string, MelodicFigure> realizedMotifs;
    std::unordered_map<std::string, PulseSequence> realizedRhythms;
    std::unordered_map<std::string, StepSequence>  realizedContours;

    // Section definitions (in order)
    struct SectionTemplate {
        std::string name;            // "verse", "chorus", "A", "B"
        float beats{32.0f};
        // Section TONIC. Empty = inherit the piece key. Accepts a bare tonic
        // ("G") or a full key name ("G Minor"); with a bare tonic the type
        // comes from scaleOverride, else from the piece's scaleName.
        // Desugars at compose time into a beat-0 KeyContext — see
        // docs/superpowers/specs/2026-08-06-section-key-design.md.
        std::string keyName;
        std::string scaleOverride;   // scale TYPE; empty = piece scaleName

        // Harmony
        std::string progressionName;  // name for ChordProgressionBuilder (empty = no progression)
        std::optional<ChordProgression> chordProgression;  // inline progression (overrides progressionName)
        std::vector<KeyContext> keyContexts;
        std::string styleName;  // style table name for ChordWalker (empty = use progressionName or inline)
    };
    std::vector<SectionTemplate> sections;

    // Form shorthand (optional): "AABA", "verse chorus verse chorus bridge chorus"
    // Composer can expand this into sections if sections vector is empty
    std::string form;

    // Parts
    std::vector<PartTemplate> parts;

    // Harmonic foundation (optional — can be seed material)
    std::optional<ChordProgression> harmonySeeds;

    // Global
    float defaultPulse{0.0f};        // 0 = composer picks once at realize time
    uint32_t masterSeed{0};

    // Helpers — declaration lookup.
    const Motif* find_motif(const std::string& name) const {
        for (auto& m : motifs) if (m.name == name) return &m;
        return nullptr;
    }

    // Helpers — realized-content lookup. Names match Composer's legacy
    // accessors so migration is a textual substitution.
    const std::unordered_map<std::string, MelodicFigure>& realized_motifs() const {
        return realizedMotifs;
    }
    const PulseSequence* find_rhythm_motif(const std::string& name) const {
        auto it = realizedRhythms.find(name);
        return it == realizedRhythms.end() ? nullptr : &it->second;
    }
    const StepSequence* find_contour_motif(const std::string& name) const {
        auto it = realizedContours.find(name);
        return it == realizedContours.end() ? nullptr : &it->second;
    }

    // --- Write API — called from plan_* phase. Idempotent by name. ---
    //
    // Adds a motif declaration + immediately realizes its content into the
    // appropriate realized map, so the pool stays consistent from the
    // strategy's point of view.
    void add_motif(Motif motif) {
        auto it = std::find_if(motifs.begin(), motifs.end(),
            [&](const Motif& m){ return m.name == motif.name; });
        const std::string& name = motif.name;
        if (std::holds_alternative<MelodicFigure>(motif.content)) {
            realizedMotifs[name] = std::get<MelodicFigure>(motif.content);
        } else if (std::holds_alternative<PulseSequence>(motif.content)) {
            realizedRhythms[name] = std::get<PulseSequence>(motif.content);
        } else if (std::holds_alternative<StepSequence>(motif.content)) {
            realizedContours[name] = std::get<StepSequence>(motif.content);
        }
        if (it != motifs.end()) *it = std::move(motif);
        else motifs.push_back(std::move(motif));
    }

    // --- Derive a motif from an existing parent via TransformOp. ---
    //
    // Idempotent: if a motif with (derivedFrom==parent, transform==t,
    // transformParam==param) already exists, returns its name without
    // creating a duplicate. Otherwise synthesizes content from the parent,
    // names the new motif (auto-generated as parent+"'" or explicitly
    // provided), stores it via add_motif, returns the chosen name.
    //
    // This skeletal version (Plan B Task 7) handles Invert + Reverse; Task 10
    // extends coverage to RhythmTail / VarySteps / VaryRhythm. Other
    // TransformOps throw runtime_error.
    std::string add_derived_motif(
            const std::string& parentName,
            TransformOp transform,
            int transformParam = 0,
            std::optional<std::string> explicitName = std::nullopt);
};

// --- PieceTemplate::add_derived_motif implementation ---
inline std::string PieceTemplate::add_derived_motif(
        const std::string& parentName,
        TransformOp transform,
        int transformParam,
        std::optional<std::string> explicitName) {
    // Idempotency check: existing derived motif with matching (parent, transform, param)?
    for (const auto& m : motifs) {
        if (m.origin == MotifOrigin::Derived
            && m.derivedFrom && *m.derivedFrom == parentName
            && m.transform && *m.transform == transform
            && m.transformParam == transformParam) {
            return m.name;
        }
    }

    // Find parent declaration.
    const Motif* parent = nullptr;
    for (const auto& m : motifs) {
        if (m.name == parentName) { parent = &m; break; }
    }
    if (!parent) {
        throw std::runtime_error("add_derived_motif: parent not in pool: " + parentName);
    }

    // Choose child name.
    std::string chosenName;
    if (explicitName) chosenName = *explicitName;
    else {
        chosenName = parentName + "'";
        while (std::any_of(motifs.begin(), motifs.end(),
                [&](const Motif& m){ return m.name == chosenName; })) {
            chosenName += "'";
        }
    }

    // Synthesize content per transform. Task 7's skeletal coverage: Invert,
    // Reverse only. Other transforms throw; Task 10 extends.
    Motif derived;
    derived.name = chosenName;
    derived.origin = MotifOrigin::Derived;
    derived.derivedFrom = parentName;
    derived.transform = transform;
    derived.transformParam = transformParam;

    switch (transform) {
        case TransformOp::Invert: {
            if (!std::holds_alternative<MelodicFigure>(parent->content))
                throw std::runtime_error("Invert requires MelodicFigure parent");
            derived.content = figure_transforms::invert(std::get<MelodicFigure>(parent->content));
            break;
        }
        case TransformOp::Reverse: {
            if (!std::holds_alternative<MelodicFigure>(parent->content))
                throw std::runtime_error("Reverse requires MelodicFigure parent");
            derived.content = figure_transforms::retrograde_steps(std::get<MelodicFigure>(parent->content));
            break;
        }
        case TransformOp::VarySteps: {
            if (!std::holds_alternative<MelodicFigure>(parent->content))
                throw std::runtime_error("VarySteps requires MelodicFigure parent");
            Randomizer rng(parent->generationSeed + 2);
            MelodicFigure copy = std::get<MelodicFigure>(parent->content);
            int variations = transformParam > 0 ? transformParam : 1;
            derived.content = figure_transforms::vary_steps(copy, rng, variations);
            break;
        }
        case TransformOp::VaryRhythm: {
            if (!std::holds_alternative<MelodicFigure>(parent->content))
                throw std::runtime_error("VaryRhythm requires MelodicFigure parent");
            Randomizer rng(parent->generationSeed + 3);
            derived.content = figure_transforms::vary_rhythm(std::get<MelodicFigure>(parent->content), rng);
            break;
        }
        case TransformOp::RhythmTail: {
            // Derive a PulseSequence from a MelodicFigure's rhythm, skipping
            // the first `transformParam` pulses.
            if (!std::holds_alternative<MelodicFigure>(parent->content))
                throw std::runtime_error("RhythmTail requires MelodicFigure parent");
            const MelodicFigure& mf = std::get<MelodicFigure>(parent->content);
            PulseSequence ps;
            int skip = transformParam > 0 ? transformParam : 0;
            for (int i = skip; i < (int)mf.units.size(); ++i) {
                ps.pulses.push_back(mf.units[i].duration);
            }
            derived.content = std::move(ps);
            break;
        }
        default:
            throw std::runtime_error(
                "add_derived_motif: TransformOp not supported: "
                "add a case in the switch if you need it");
    }

    add_motif(std::move(derived));
    return chosenName;
}

} // namespace mforce
