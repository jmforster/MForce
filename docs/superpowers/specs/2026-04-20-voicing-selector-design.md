# Voicing Selector — Design

## Context

The MForce composer currently resolves an abstract `ScaleChord` (scale-degree + alteration + quality) into a concrete `Chord` (pitch set + inversion + spread) via a single call in `realize_chord_parts_` (`composer.h:266-313`) using hardcoded per-part `cfg.octave`, `cfg.inversion`, `cfg.spread` from `ChordAccompanimentConfig` (`templates.h:316-335`). Every chord event on a Part in a Section gets the same inversion and spread. That produces mechanically voiced, voice-leading-blind chord accompaniments.

The framework already has the primitives to do better:

- `ChordDef` (`basics.h:320`) defines a chord quality as an ordered interval list.
- `ChordDictionary` (`basics.h:360`) is a named namespace of `ChordDef`s; multiple dictionaries can coexist.
- Legacy C# `ChordDictionary.cs` populates a `Default` dictionary (all canonical qualities including jazz: `7b9`, `7#9`, `7b5b9`, `7b13`, `m7b9`, `M13`, `69`) and a `Piano` dictionary (same quality names, different voicings using wider intervals like `[5, 8, M10]`).
- `Chord` supports `inversion` and `spread` as further per-instance adjustments (`basics.h:336-337`).

What's missing is the **selection mechanism**: given a `ScaleChord`, the Part's context (instrument, genre, previous chord, optional melody pitch), and a set of candidate dictionaries, which named voicing (dictionary, chord-def, inversion) do we pick, and how do we score candidates?

## Problem

1. **No smooth voice leading.** Each chord is voiced independently with the same inversion/spread, producing large unnecessary jumps between adjacent chords.
2. **No per-instrument voicing idioms.** The Legacy `Piano` dictionary exists as a model but the C++ side has nothing equivalent wired in; guitar-idiomatic voicings (barre, open-string) have no home.
3. **No genre sensitivity.** Jazz wants rootless/shell voicings; classical wants close-with-common-tone-preservation; rock wants power chords; modal wants quartal. All expressible *as dictionaries*, none selectable from templates.
4. **No canonic reference.** Voice-leading distance calculations need a consistent comparison form; "closest-interval" canonic voicings are the natural baseline but don't exist as a distinguished dictionary.
5. **Naming collision.** `ChordArticulation` (`figures.h:627`) is the C++ port of legacy `ChordFigure.cs` and serves the "Realization" role (how to unfold a chord in time: basso continuo, Alberti, jazz comp, strum). Meanwhile C++ introduced a *new* `ChordFigure` (`figures.h:568`) for chord-tone melodic movement — a `Figure` subclass. These are different concepts, but the name `ChordFigure` meaning shifted during port. Renaming `ChordArticulation` → `ChordRealization` removes the ambiguity.

## Goals

- A `VoicingSelector` interface that resolves `ScaleChord` → concrete `Chord` with pitches, aware of previous chord for voice leading.
- Section-level per-Part configuration via `PassageTemplate` (where the Part's per-section behavior already lives).
- Multiple dictionaries organized as: **Canonic** (single distinguished), **per-instrument** (Piano, Guitar, etc., each with multiple voicings per quality), **per-genre subsets** (initial cut: distinct dictionaries per Instrument×Genre).
- Renaming `ChordArticulation` → `ChordRealization` (terminology cleanup, resolves the `ChordFigure` collision).
- Ship two concrete selectors to validate the pattern:
  1. **`SmoothVoicingSelector`** — uses Canonic dictionary, picks inversion minimizing voice-leading distance from previous chord. Default.
  2. **`JazzPianoVoicingSelector`** — uses a jazz-piano dictionary, rootless/shell bias, same voice-leading backend.
- Wire into `realize_chord_parts_` replacing the hardcoded `inversion`/`spread` path.
- Existing renders whose templates don't opt into a selector must continue to produce bit-identical output (fallback preserves current behavior).

## Non-goals

- **Josie de-hacking.** The hardcoded `josie8`/`josie3`/`josie25` ChordArticulation objects in `conductor.h:466-490` stay put; extracting them to a data-driven `RealizationLibrary` is a separate future effort.
- **`RealizationSelector`.** The parallel mechanism for picking Realization patterns (currently picked via `figureName` hint by the ChordPerformer) is explicitly out of scope. This spec only covers pitch selection; rhythmic/textural realization stays as-is.
- **Full jazz-piano dictionary.** We ship enough entries for one passable selector demonstration (Cmaj7, Dm7, G7, plus a few alterations). A comprehensive library is future work.
- **Guitar voicings.** Mentioned as a future use case; not built in this cut.
- **Contrapuntal / Fux-style composition.** Where individual voice lines are primary and harmony emerges — completely out of scope. This spec is about *chord-progression voice-leading*, not counterpoint.
- **Cross-Part awareness** (e.g., bass-Part's root influencing comp-Part's voicing, or melody-Part's top note steering chord-Part's top voice). Documented as a followup; initial selectors consider only the Part's own previous chord.
- **JSON field rename** on `Chord::figureName`. Keep the field name; only rename the C++ type `ChordArticulation` → `ChordRealization`. Field rename is a breaking change parked for a later schema version bump.

## Design

### 1. Rename `ChordArticulation` → `ChordRealization`

Mechanical rename of the C++ type only:

- `figures.h:627` struct definition
- All usages in `conductor.h` (the `namedFigures`, `figures`, `defaultFigure` members; all Josie-* local variables; `select_figure` return type; `perform_with_figure` signature; the `ChordArticulation::DIR_ASCENDING` constants)
- Any comments referring to "ChordArticulation"

**Not renamed:**
- `Chord::figureName` field stays (JSON back-compat; deferred to future schema version)
- Comments that legitimately describe *articulation* (the note-level concept — staccato, legato) must not be swept up in the rename

### 2. Canonic dictionary as a distinguished primitive

**Rationale:** Voice-leading distance scoring needs a consistent reference form of any chord. The "closest-interval" voicing (root + nearest third + nearest fifth + nearest seventh, etc., all within an octave) is the natural canonical form. Today the legacy `Default` dictionary serves this role implicitly; we formalize it.

**Proposal:**
- Introduce `const ChordDictionary& ChordDictionary::canonic()` as a named accessor.
- The canonic dictionary contains every chord quality we care about at its smallest-interval voicing: `M` = `[M3, P5]`, `M7` = `[M3, P5, M7]`, `7b9` = `[M3, P5, m7, m9]`, etc.
- Canonic is the **only** dictionary guaranteed to contain every quality we produce from `ScaleChord`. Other dictionaries are optional extensions.
- All voice-leading distance calculations resolve each chord to its canonic voicing first, then score against candidate voicings.

### 3. Per-instrument dictionaries

**Shape:**
- Multiple named `ChordDictionary`s in the global registry: `"canonic"` (the one above), `"piano_comp"`, `"guitar_barre"`, `"guitar_open"`, etc.
- A dictionary may contain multiple named voicings per chord quality — legacy calls this "multiple voicings per chord". For this spec we keep **one voicing per `ChordDef.shortName`**, with distinct shortNames for distinct voicings (e.g., `"Mg_open"`, `"Mg_bar"` both represent a major chord but with different intervals and name disambiguation). Multi-voicing-per-shortname is a future extension if needed.

**Genre handling (initial cut):**
- Separate dictionaries per (Instrument, Genre): `"guitar_barre_jazz"`, `"guitar_open_folk"`. Simpler than a filter abstraction; easy to add dictionaries.
- A future `ChordFilter` (takes Genre, returns subset) may replace or supplement this if the dictionary population gets unwieldy. Parked.

### 4. `VoicingSelector` interface

```cpp
// engine/include/mforce/music/voicing_selector.h

namespace mforce {

struct VoicingRequest {
    ScaleChord scaleChord;              // what to voice
    const Scale* scale;                 // interpretive context
    int rootOctave;                     // where to place the root
    float durationBeats;                // passes through to Chord.dur
    const Chord* previous{nullptr};     // prior chord on this Part (for VL)
    std::optional<Pitch> melodyPitch;   // optional top-voice hint
};

struct VoicingSelector {
    virtual ~VoicingSelector() = default;
    virtual std::string name() const = 0;
    virtual Chord select(const VoicingRequest& req) = 0;
};

// Selector registry: by name, like StrategyRegistry.
class VoicingSelectorRegistry {
public:
    static VoicingSelectorRegistry& instance();
    void register_selector(std::unique_ptr<VoicingSelector> s);
    VoicingSelector* resolve(const std::string& name) const;
};

} // namespace mforce
```

The registry is populated at Composer construction time (parallel to strategy registration).

### 5. `SmoothVoicingSelector` — the default

**Algorithm** (genre-neutral baseline):

1. Resolve `scaleChord` to its canonic `ChordDef` via `ChordDictionary::canonic().get_chord_def(scaleChord.quality->name)`.
2. Build candidate voicings: for the canonic def, enumerate inversions `0..N-1` and each spread `{0, 1}` (spread=1 may displace top voices up an octave; fine-grained spread semantics deferred to implementation).
3. If `req.previous` is null, return the inversion-0/spread-0 candidate (preserves current default behavior → existing renders unaffected).
4. Else, for each candidate, compute voice-leading distance to `req.previous` using the Hungarian (optimal-assignment) pairing, sum of absolute semitone distances across voice-to-voice assignment, or a simpler greedy nearest-tone if optimal is overkill (implementation detail; start with greedy).
5. If `req.melodyPitch` is set, add a soft penalty when the top voice clashes (within half-step, non-chord-tone) or doesn't contain the melody pitch.
6. Return the winning candidate as `Chord` with pitches populated.

### 6. `JazzPianoVoicingSelector` — second concrete selector

- Uses `"piano_jazz"` dictionary (populated in this spec with a minimum set: `M7`, `m7`, `7`, `7b9`, `m7b5`, some voicings for each that reflect jazz piano — e.g., rootless left-hand voicings where `7` is voiced as `[M3, m7, M9, M13]` without the root).
- Same voice-leading scoring backend as Smooth.
- Demonstrates the pattern for a genre-specific selector: different dictionary, same voice-leading logic.

### 7. Template integration

Add to `PassageTemplate` (`templates.h:341-365`):

```cpp
struct PassageTemplate {
    // ... existing fields ...
    std::string voicingSelector;   // name, empty = default behavior
};
```

If empty and no `chordConfig` present: existing behavior (fallback to hardcoded `sc.resolve(scale, cfg.octave, dur, cfg.inversion, cfg.spread)`). Back-compat preserved.

If empty and `chordConfig` present: use `SmoothVoicingSelector` by default (the migration story: opting into chord accompaniment opts into smooth voicing).

If non-empty: look up the named selector from `VoicingSelectorRegistry`, fall back to `SmoothVoicingSelector` with a warning if unknown.

### 8. Wiring into `realize_chord_parts_`

Current (`composer.h:266-313`):

```cpp
Chord chord = sc->resolve(sec.scale, cfg.octave, dur,
                          cfg.inversion, cfg.spread);
part->add_chord(pos, chord);
```

New:

```cpp
const Chord* prev = part->last_chord_event_before(pos);
VoicingRequest req{*sc, &sec.scale, cfg.octave, dur, prev, /*melodyPitch=*/std::nullopt};

VoicingSelector* selector = resolve_selector(passTmpl.voicingSelector, cfg);
Chord chord = selector ? selector->select(req)
                       : sc->resolve(sec.scale, cfg.octave, dur,
                                     cfg.inversion, cfg.spread);
part->add_chord(pos, chord);
```

`part->last_chord_event_before(pos)` is a new helper on `Part` (or a `piece_utils` free function) that scans events reverse-chronologically for the most recent chord event on this Part.

Melody pitch coordination (wiring the melody Part's pitch-at-this-beat into the VoicingRequest for chord Parts) is a phase-two refinement — stub left as `std::nullopt` for now.

## Migration plan

### Stage 0 — Baseline goldens
Re-verify pass3 (`.claude/worktrees/composer-model-period-forms/patches/test_k467_pass3.json`) renders bit-identically to the current main. This becomes the regression baseline. Commit hash of this render saved.

### Stage 1 — `ChordArticulation` → `ChordRealization` rename
Mechanical rename. Build + re-render pass3 → bit-identical. Commit.

### Stage 2 — Canonic dictionary
Introduce `ChordDictionary::canonic()` as a first-class accessor. Populate with all qualities currently in the legacy `Default` dictionary. Build. No behavioral change — the canonic dictionary exists but nothing consumes it yet. Re-render pass3 → bit-identical. Commit.

### Stage 3 — `VoicingSelector` interface + registry + default-behavior shim
Add the files. Register no selectors initially; `realize_chord_parts_` unchanged by default. Build. No behavioral change. Commit.

### Stage 4 — `SmoothVoicingSelector` + wire fallback
Implement `SmoothVoicingSelector`. Wire `realize_chord_parts_` to the new path, but preserve back-compat: if `voicingSelector` empty and `chordConfig` present, skip the selector and use the legacy `sc->resolve(...)` path. Build + re-render pass3 → bit-identical (no opt-in yet). Commit.

### Stage 5 — Opt pass3 into `SmoothVoicingSelector`
Modify pass3's chord Part to set `"voicingSelector": "smooth"` (or implicitly when `chordConfig` present). Render → a NEW golden. Verify by ear that the chord-Part motion is perceptibly smoother than before. Commit a new golden.

### Stage 6 — `piano_jazz` dictionary + `JazzPianoVoicingSelector`
Populate `piano_jazz` dictionary with a minimum useful set. Implement the selector. Register. Build. No render change (pass3 uses smooth). Commit.

### Stage 7 — Demo template for jazz selector
Create a small chord progression patch (e.g., `patches/test_jazz_ii_V_I.json`) with chord Part using `"voicingSelector": "piano_jazz"`. Render, pin new golden, listen to verify rootless voicings land correctly.

### Stage 8 — Copy renders to main + final build
Copy renders to `renders/` (per feedback memory). Re-run full regression across all existing goldens.

Each stage revert-able independently. Each ends with a build + render + hash check.

## Resolved during design discussion

- **Voicing decisions are baked in Compose tier.** Hybrid mode: `Chord::pitches` is resolved by `VoicingSelector` at Compose time; `Chord::figureName` still steers Realization (`ChordRealization`) at Performance time. One-copy Selector, Compose-tier-only for this phase.
- **Section-level selector config.** Set on `PassageTemplate` (which is the "what a Part plays during a Section" container). Per-Part, per-Section.
- **"Realization"** is the shared discussion term for rhythmic-textural chord patterns (basso continuo / Alberti / comp / strum / arpeggio). In code, implemented as `ChordArticulation` → rename → `ChordRealization`.
- **Canonic dictionary as a distinguished primitive**, not just "Default" — its own thing, accessed via a dedicated static.
- **Multiple dictionaries per instrument/genre**, starting with per-(Instrument, Genre) dictionaries. `ChordFilter` abstraction deferred.
- **`ChordFigure` (the C++ subclass) is a different concept** from `ChordRealization`. ChordFigure = chord-tone melodic stepping (melody lane). ChordRealization = rhythmic-textural chord pattern (harmony lane). Both stay.

## Open questions to settle at planning time

- **Voice-leading algorithm.** Greedy nearest-tone vs. optimal-assignment (Hungarian). Greedy is easier and probably indistinguishable for 3-4-voice chords. Plan should pick one.
- **Spread semantics.** Currently `Chord::spread` is poorly documented. Plan clarifies whether `SmoothVoicingSelector` enumerates spread as a candidate dimension or freezes it to 0 in this phase.
- **How does `last_chord_event_before(pos)` handle rests in the chord Part?** Return the most recent chord, or null if there's been a rest boundary? Lean: return most recent chord regardless of rests — voice leading across rests is still musical.
- **Rootless voicing semantics.** `ChordDef::omitRoot` exists on the struct. Does `SmoothVoicingSelector` ignore this flag, or honor it? Does `JazzPianoVoicingSelector` add root separately if a bass Part is present? Needs decision; initial plan: honor the flag verbatim; bass coordination is the parked cross-Part work.
- **Dictionary population authoring.** Legacy populated via static C++. Should the new dictionaries (e.g., `piano_jazz`) be hardcoded C++ (like legacy) or JSON-loaded? Hardcoded is simpler for V1; JSON is more composer-friendly long term. Lean: hardcoded in this phase, JSON-loadable in a later phase. Plan picks.
- **Octave placement of candidates.** `VoicingRequest.rootOctave` tells us where to place the root. When previous chord is in a different octave, does the selector transpose candidates to match, or search within a ±1 octave band? Plan decides.

## What this enables downstream

- **Instrument-specific voicing libraries** (e.g., all the `_g*` guitar voicings from legacy Josie-era work) can be populated as dictionaries and opted into per Part.
- **Genre-tagged selectors** stack trivially on the same backend: `JazzPianoVoicingSelector`, `ClassicalSATBVoicingSelector`, `RockPowerChordVoicingSelector`, etc. Each is a small class + a dictionary reference + scoring tweaks.
- **Cross-Part coordination** (melody-pitch-aware top-voice, bass-aware root omission) plugs in via additional `VoicingRequest` fields; same interface survives.
- **`RealizationSelector`** later — the parallel mechanism for rhythmic-textural patterns — follows the same pattern (registry + named selector config per Part/Section), re-using the architectural shape proven here.
- **Fux / counterpoint** — still parked, but a per-Part voicing path for chord Parts doesn't preclude a later compose-as-voice-lines path for melody Parts. Orthogonal workstreams.
