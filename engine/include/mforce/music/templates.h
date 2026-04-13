#pragma once
#include "mforce/music/basics.h"
#include "mforce/music/figures.h"
#include "mforce/music/structure.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <optional>
#include <variant>
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

enum class TransformOp {
    None,
    Invert,           // flip step directions
    Reverse,          // retrograde
    Stretch,          // multiply durations by factor (default 2x)
    Compress,         // divide durations by factor (default 2x)
    VaryRhythm,       // split/dot some pulses randomly
    VarySteps,        // randomly perturb some step values
    NewSteps,         // keep rhythm, generate new steps
    NewRhythm,        // keep steps, generate new rhythm
    Replicate,        // repeat N times with step offset between
    TransformGeneral, // do *something* — composer decides the operation
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

struct Motif {
    std::string name;              // "main_theme", "hook", "bridge_motif"

    using Content = std::variant<MelodicFigure, PulseSequence, StepSequence>;
    Content content;               // the actual musical content (figure, rhythm, or contour)

    bool userProvided{false};      // true = user entered, don't regenerate
    uint32_t generationSeed{0};    // for reproducibility if generated
    std::optional<FigureTemplate> constraints;  // generation constraints when !userProvided

    bool is_figure()  const { return std::holds_alternative<MelodicFigure>(content); }
    bool is_rhythm()  const { return std::holds_alternative<PulseSequence>(content); }
    bool is_contour() const { return std::holds_alternative<StepSequence>(content); }

    const MelodicFigure& figure()  const { return std::get<MelodicFigure>(content); }
    const PulseSequence& rhythm()  const { return std::get<PulseSequence>(content); }
    const StepSequence&  contour() const { return std::get<StepSequence>(content); }
};

// ===========================================================================
// PeriodPhraseConfig — classical period (antecedent + consequent)
// ===========================================================================

struct PeriodPhraseConfig {
  MelodicFigure basicIdea;           // opens both sub-phrases (parallel)
  MelodicFigure antecedentTail;      // closes antecedent on halfCadenceTarget
  MelodicFigure consequentTail;      // closes consequent on authentic cadence
  int halfCadenceTarget{4};          // 0-indexed scale degree for half cadence
};

// ===========================================================================
// SentencePhraseConfig — classical sentence (basic idea + repeat + continuation)
// ===========================================================================

struct SentencePhraseConfig {
  MelodicFigure basicIdea;
  int variationTransposition{0};     // scale-degree offset for the repetition
  MelodicFigure continuation;
};

// ===========================================================================
// PhraseTemplate — sequence of FigureTemplates
// ===========================================================================

struct PhraseTemplate {
    std::string name;                          // "antecedent", "bridge", etc.
    std::optional<Pitch> startingPitch;        // where to begin (may come from context)
    std::vector<FigureTemplate> figures;

    // Phrase-level constraints
    float totalBeats{0.0f};                    // 0 = sum of figures
    int cadenceType{0};                        // 0=none, 1=half, 2=full
    int cadenceTarget{-1};                     // target scale degree (-1 = composer decides)
    MelodicFunction function{MelodicFunction::Free}; // drives shape selection for Free figures

    // State
    uint32_t seed{0};
    bool locked{false};

    // Strategy selection (Phase 3). Empty = default_phrase.
    std::string strategy;

    // Typed configs for specific phrase strategies. Only one is populated
    // at a time (matching the selected strategy).
    std::optional<PeriodPhraseConfig> periodConfig;
    std::optional<SentencePhraseConfig> sentenceConfig;
};

// ===========================================================================
// PassageTemplate — what a Part plays during a Section
// ===========================================================================

struct PassageTemplate {
    std::string name;
    std::optional<Pitch> startingPitch;           // REQUIRED at JSON-load time;
                                                  // optional storage because Pitch
                                                  // has no default constructor.
    std::vector<PhraseTemplate> phrases;

    // Passage-level directives
    std::string character;           // freeform: "energetic", "lyrical", "transitional"
    std::string fromKey;             // for modulatory passages
    std::string toKey;               // ending key (empty = same)

    // Strategy selection. Empty = default_passage.
    std::string strategy;

    // State
    uint32_t seed{0};
    bool locked{false};
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

    // Motif material
    std::vector<Motif> motifs;

    // Section definitions (in order)
    struct SectionDef {
        std::string name;            // "verse", "chorus", "A", "B"
        float beats{32.0f};
        std::string scaleOverride;   // empty = use piece key

        // Harmony
        std::string progressionName;  // name for HarmonyComposer (empty = no progression)
        std::optional<ChordProgression> chordProgression;  // inline progression (overrides progressionName)
        std::vector<KeyContext> keyContexts;
    };
    std::vector<SectionDef> sections;

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

    // Helpers
    const Motif* find_motif(const std::string& name) const {
        for (auto& m : motifs) if (m.name == name) return &m;
        return nullptr;
    }
};

} // namespace mforce
