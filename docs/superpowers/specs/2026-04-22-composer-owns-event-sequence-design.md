# Composer Owns the ElementSequence — Design

## Context

The MForce data model already carries a dual structure on `Part`:

```cpp
struct Part {
    std::unordered_map<std::string, Passage> passages;  // compositional tree
    std::vector<Element> events;                         // realized event list
    // comment: "Realized events (built directly for simple cases)"
};
```

The intended design (per legacy and per the user's mental model): **Composer fully populates the event list. Conductor consumes the event list and performs it.** The score-as-data is the contract between the two tiers.

The current implementation has drifted. Direct-event-building patches do work the intended way (`add_chord` / `add_note` / `add_hit` / `add_rest` populate `events`; Conductor reads via `perform_events`). But the algorithmic / Composer-generated path **never populates `Part.events`**. Instead Conductor walks `passages → phrases → figures` at perform time (`perform_passage` → `perform_phrase`), computes pitches inline (`step_note` for melody, `step_chord_tone` for chord-tone melody, `dynamic_cast<ChordFigure>` to discriminate), and `ChordPerformer` expands `Chord` events to per-tone notes — all at performance time, not at compose time.

The user's framing: "the Conductor was jotting it down while leading the orchestra." A direct consequence is that the system **cannot print a score** — the score doesn't exist as data anywhere; it is invented during performance.

## Problem

Pitch resolution and chord realization happen in the wrong tier. Specifically:

1. **Melody pitch resolution** lives in `Conductor::perform_phrase` (`step_note(currentNN, u.step, scale)` at conductor.h:831). `MelodicFigure` carries abstract scale-degree step indices that aren't resolved to pitches until perform time.

2. **Chord-tone melody pitch resolution** lives in `Conductor::perform_phrase` (`step_chord_tone` at conductor.h:829), discriminated by `dynamic_cast<const ChordFigure*>`.

3. **Chord realization** lives in `ChordPerformer::perform_chord` / `perform_with_figure` (conductor.h:447, 548). `Chord` events are stored on `Part.events` but their per-tone expansion (which pitches sound, with what rhythm) happens at perform time using `ChordArticulation` patterns (the Josie library at conductor.h:462).

4. **Voicing selection** runs at compose time (`composer.h:266-313` calls `sc->resolve(scale, octave, dur, inv, spread)`), but produces a `Chord` event that defers the actual per-tone realization to Conductor. Half-resolved.

5. **Composer's output is incomplete.** `Part.elementSequence` (renamed from `events`) stays empty for any Part fed by the algorithmic pipeline. Conductor relies on the Passage tree, which is supposed to be Composer-internal.

Concrete consequences:

- No score export. The score doesn't exist as data outside of performance.
- Pitch-resolution logic is split between Composer (start-of-figure pitch reader at composer.h:1115-1124) and Conductor (per-step in perform_phrase). Drift risk.
- The `ChordFigure` vs `MelodicFigure` discriminator survives only because Conductor needs to know which stepping rule to apply. Resolved at compose time, the discriminator goes away.
- Voicing-selector work on the `chord-walker` branch (4 selectors, `init_pitches` rule-native, `VoicingProfile`) sits unmerged because its natural home is Composer-side and Composer isn't resolving voicings to per-tone notes.
- `ChordRealization` (rename of `ChordArticulation` per voicing-selector spec) has no symmetric Compose-tier home; it currently lives in `ChordPerformer`.

## Goals

- Composer fully populates `Part.elementSequence` for **all content** (algorithmic and direct-built).
- Conductor reads only `Part.elementSequence` (via `perform_events` and the rest of the perform-tier work). The Passage / Phrase / Figure tree becomes Composer-internal scratch space.
- Pitch resolution (melody and chord-tone) moves to Composer.
- Chord events are expanded to per-tone Note events at compose time via `VoicingSelector` (pitch selection) and `RealizationStrategy` (rhythmic-textural unfolding).
- The `chord-walker` branch's voicing work merges into main as part of this refactor.
- `ChordPerformer`, `Conductor::perform_passage`, `Conductor::perform_phrase`, the `step_note` / `step_chord_tone` helpers, and the `dynamic_cast<ChordFigure>` discriminator are all retired.
- `ChordAccompanimentConfig` dissolves: rhythm pattern absorbs into a `RealizationStrategy` config, voicing hints absorb into `VoicingProfile`.
- K467 patches render bit-identically through every stage (with one possible exception at chordConfig migration where a new golden may be pinned if the migration is non-faithful).

## Non-goals

- **DrumPerformer migration.** Likely follows ChordPerformer out (drums could be expanded to per-`Hit` events at compose time and DrumPerformer becomes a route-to-instrument shim) but the call is explicitly deferred. Decide after the chord-side refactor lands.
- **Conductor → Performer rename.** Conductor stays named Conductor. The narrowed scope is real but a rename adds churn for no functional gain.
- **Score export.** Now possible because the score exists as data. Future feature, not part of this refactor.
- **`StepMode::ChordTone` decision** (orphaned field). Decide at plan time when the realize step is being written; the field may finally get a consumer or get deleted. Does not block the refactor.
- **Phrase tree elimination / direct-emit refactor.** Strategies continue to build the tree as scratch; realization walks it. A future cleanup pass can dismantle the tree-emitting strategies in favor of direct-emit. Not in this refactor.
- **Josie de-hacking** (the hardcoded `register_josie_figures` library). Likely deleted in stage 8 (no current patches consume them per the voicing-selector spec). If any do, port as RealizationStrategy implementations.
- **`Conductor::perform(Piece&)` signature change.** Stays as-is; Conductor has access to passages but doesn't consult them. Tightening to `perform(const std::vector<Part>&, ...)` is a low-priority follow-up.

## Design

### 1. Data model changes (`structure.h`)

**Introduce `ElementSequence`:**

```cpp
struct ElementSequence {
    std::vector<Element> elements;
    float totalBeats{0.0f};

    void add(const Element& e);                  // appends, updates totalBeats
    void sort_by_beat();                         // stable sort on Element::startBeats
    int size() const { return int(elements.size()); }
    auto begin() { return elements.begin(); }
    auto end()   { return elements.end(); }
    // Helpers (at_beat, range_beats, etc.) added when Composer/Conductor need them.
    // Don't pre-design.
};
```

**`Part` changes:**

```cpp
struct Part {
    std::string name;
    std::string instrumentType;
    std::unordered_map<std::string, Passage> passages;  // Composer-internal scratch
    ElementSequence elementSequence;                     // renamed from events; now authoritative

    // Direct-build helpers (existing, just route through elementSequence)
    void add_note(...);
    void add_hit(...);
    void add_rest(...);
    // add_chord(...) is removed when stage 9 lands; until then it's a thin wrapper
    // that constructs and adds a Chord-typed Element.
};
```

**`Element` variant change** (stage 9):

```cpp
// Before:
struct Element {
    float startBeats{0.0f};
    std::variant<Note, Chord, Hit, Rest> content;
};

// After (stage 9):
struct Element {
    float startBeats{0.0f};
    std::variant<Note, Hit, Rest> content;
};
```

`Chord` (the basics.h type) remains as a Composer-internal data type (consumed by VoicingSelector / RealizationStrategy) but is no longer a serializable score event.

### 2. Composer pipeline (`composer.h`)

`plan()` is unchanged. `compose()` becomes internally two sub-steps:

1. **Build tree** — current logic. Strategies populate `Part.passages` with `Passage → Phrase → Figure` content.
2. **Realize** — new sub-step. Walks each `Part.passages`, applies VoicingSelector + RealizationStrategy to chord parts, applies pitch resolution to melody parts, emits `Note` / `Hit` / `Rest` Elements into `Part.elementSequence`.

The realize sub-step contains the work currently in `Conductor::perform_phrase`:

- For each Phrase in each Passage:
  - For each Figure (in figure order):
    - For each FigureUnit:
      - Compute pitch via `step_note(currentNN, u.step, scale)` for `MelodicFigure`, or `step_chord_tone(currentNN, u.step, resolved_chord)` for `ChordFigure`.
      - Apply transient accidental (`u.accidental`).
      - Resolve dynamic markings → velocity at this beat (DynamicState lookup is moved or duplicated; see below).
      - Construct a `Note` and append to `Part.elementSequence`.

**Dynamics handling during realize:** `DynamicMarking`s on the Passage drive velocity. Composer needs DynamicState's velocity-at-beat logic during realize (so each Note carries its resolved `velocity`). Two options at plan time: (a) move DynamicState to `mforce/music` and use it from both Composer and Conductor, (b) duplicate the small velocity-curve helper. Lean: (a). DynamicState is already a clean small piece.

**Articulation/Ornament markings on Notes:** carried through as fields on `Note` (already exist). Conductor expands them at perform time. No semantic change.

### 3. chord-walker merge (stage 2)

Brings the following from `chord-walker` branch onto main, unchanged:

- `voicing_profile.h` — `VoicingProfile { allowedInversions, allowedSpreads, priority }`
- `voicing_profile_selector.h` — base interface + factory registry
- `static_voicing_profile_selector.h`, `random_voicing_profile_selector.h`, `drift_voicing_profile_selector.h`, `scripted_voicing_profile_selector.h`
- `voicing_selector.h` — base interface
- `smooth_voicing_selector.h` — concrete selector with VL + chord-tone scoring
- `chord.cpp` changes: rule-native `init_pitches` (inversion = list-rotation; spread = walk-with-wraparound)
- 7 demo patches: `patches/test_jazz_turnaround_*.json`

These selectors are not yet wired into the Composer realize path (that lands at stage 6). At stage 2 they exist alongside the legacy `sc->resolve()` path.

Voicing-selector open items (from `project_voicing_open_items.md`) ride along as flagged backlog, not blockers: upward drift, missing cadential chord role, boring-repeat penalty.

### 4. RealizationStrategy registry (stage 3)

Parallel to VoicingSelector. Compose-tier. Named, registry-backed, configurable per Passage.

```cpp
// engine/include/mforce/music/realization_strategy.h

struct RealizationRequest {
    Chord chord;                  // already-voiced (output of VoicingSelector)
    float startBeat;
    float durationBeats;
    int barIndex;                 // for per-bar overrides (e.g., RhythmPattern overrides)
    // (further fields as needed: meter, dynamics-at-beat, etc.)
};

struct RealizationStrategy {
    virtual ~RealizationStrategy() = default;
    virtual std::string name() const = 0;
    // Emits zero or more Elements (typically Notes) starting at req.startBeat.
    virtual void realize(const RealizationRequest& req,
                         ElementSequence& out) = 0;
};

class RealizationStrategyRegistry {
public:
    static RealizationStrategyRegistry& instance();
    void register_strategy(std::unique_ptr<RealizationStrategy> s);
    RealizationStrategy* resolve(const std::string& name) const;
};
```

**Initial implementations:**

- `BlockRealizationStrategy` (`"block"`, default) — emits all chord pitches simultaneously at `startBeat` for `durationBeats`. Mirrors the simplest current behavior.
- `RhythmPatternRealizationStrategy` (`"rhythm_pattern"`) — consumes a `RhythmPattern { defaultPattern, overrides }` config (lifted directly from the dissolving `ChordAccompanimentConfig`); for each entry in the pattern, emits all chord pitches at the corresponding beat with the entry's duration (negative entries become Rests). This is the migration target for K467's `chordConfig`.

**Future implementations** (not in this spec): `"alberti"`, `"strum"`, `"jazz_comp"`, etc.

**Template integration:**

```cpp
struct PassageTemplate {
    // ... existing fields ...
    std::string voicingSelector;       // already on chord-walker — added at stage 2
    std::string realizationStrategy;   // new — added at stage 3
    // RhythmPattern config moves to a dedicated optional field consumed by
    // RhythmPatternRealizationStrategy when that strategy is selected.
};
```

If `realizationStrategy` is empty: default to `"block"` (which collapses to current behavior for the simplest case).

### 5. Composer realize-to-events sub-step (stages 4–6)

Implemented per element type, one stage each:

- **Stage 4 — MelodicFigure realization.** For each Part with melodic content, walk passages → phrases → figures, resolve via `step_note`, emit Notes into `Part.elementSequence`. Conductor still authoritative (still walks the tree); the new emit runs in parallel.
- **Stage 5 — ChordFigure realization.** Same pattern, using `step_chord_tone` against the active chord from `chordProgression`.
- **Stage 6 — Chord-event realization.** For each Part with chord events, look up the Part's `voicingSelector` (default Smooth) and `realizationStrategy` (default Block), build a `VoicingRequest` (with `req.previous` = prior chord on this Part), call selector to get a voiced `Chord`, then call strategy's `realize()` to emit per-tone Notes/Rests into `elementSequence`. Chord events also still go through Conductor's `ChordPerformer` for now (parallel path).

After stage 6, `Part.elementSequence` is fully populated for all content. Conductor is still authoritative because it still walks the tree at perform time.

### 6. Conductor switch-and-narrow (stages 7–8)

**Stage 7 — Switch.** `Conductor::perform_part` (or its caller) routes Composer-generated Parts through `perform_events(part.elementSequence)` instead of `perform_passage(...)`. The K467 golden set must produce identical audio. This is the moment of truth — if the realize step in stages 4–6 was faithful to what Conductor does, the audio matches. If not, investigate (likely a velocity-curve detail or a ChordPerformer-only behavior).

**Stage 8 — Delete.** With elementSequence-driven perform proven, delete the now-unused code:
- `Conductor::perform_passage`, `Conductor::perform_phrase` (conductor.h:751, 785)
- `Conductor::step_note`, `Conductor::step_chord_tone` helpers
- `Conductor::dynamic_cast<ChordFigure>` discriminator (becomes dead with perform_phrase)
- `ChordPerformer` struct entirely (conductor.h:436-end-of-struct)
- `Conductor::register_josie_figures` (the hardcoded Josie ChordArticulation library)
- Any other code paths that exclusively served the tree-walk perform mode

**Possible absorbed cleanup**: rename `ChordArticulation` → `ChordRealization` (was on the voicing-selector spec). If the type still has any consumers post-stage-8, do the rename here. If it's completely unused, delete instead.

### 7. Drop `Chord` from Element variant (stage 9)

After stage 8, the only path that ever produced `Chord`-typed Elements (Composer's pre-realize compose path) emits expanded Notes via VoicingSelector + RealizationStrategy instead. Direct-build callers using `Part::add_chord(...)` are migrated to invoke a Composer helper that runs the same VoicingSelector + RealizationStrategy expansion synchronously, populating Notes.

`std::variant<Note, Chord, Hit, Rest>` becomes `std::variant<Note, Hit, Rest>`. `Element::is_chord` / `Element::chord()` accessors deleted. `Part::add_chord(...)` either deleted or refactored to the synchronous-expand helper described above (decide at plan time based on call-site count).

### 8. Patch migration (stage 10)

Audit shows only one patch on main currently uses `chordConfig`: `patches/test_k467_walker.json`. Hand-migrate; no script needed.

The migration replaces the `chordConfig` block on the relevant `PassageTemplate` with explicit `voicingSelector` + `voicingProfile` + `realizationStrategy` + `rhythmPattern` fields:

```jsonc
// Before (PassageTemplate fragment):
{
  "chordConfig": {
    "defaultPattern": [1.0, 1.0, 1.0, 1.0],
    "octave": 3,
    "inversion": 0,
    "spread": 0
  }
}

// After (PassageTemplate fragment):
{
  "voicingSelector": "smooth",
  "voicingProfile": { "allowedInversions": [0], "allowedSpreads": [0], "priority": 0.0 },
  "realizationStrategy": "rhythm_pattern",
  "rhythmPattern": {
    "defaultPattern": [1.0, 1.0, 1.0, 1.0]
  },
  "rootOctave": 3
}
```

If migration is faithful (same pitches, same rhythms emitted), bit-identical. If a default differs by a hair (e.g., voicing-selector picks a different inversion than the hardcoded `0`), pin a new golden after listening.

### 9. Final cleanup (stages 11–12)

**Stage 11 — Delete `ChordAccompanimentConfig` struct.** No consumers after stage 10. Its `BarOverride` nested type also goes (subsumed by `RhythmPattern`'s overrides).

**Stage 12 — Audit residual Phrase / Passage tree exposure.** If Composer is the only consumer post-refactor, mark as Composer-internal (move from `structure.h` to `composer.h` if practical, or document and keep in place). Goal: future maintainer doesn't see the tree types and reach for them.

## Migration plan

The 12-stage chain runs autonomously chain-on-green:

| # | Commit | Bit-identical? |
|---|---|---|
| 0 | Pin baseline goldens (`test_k467_walker` as primary — uses chordConfig — plus `test_k467_harmony`, `test_k467_period`, `test_k467_structural`, and jazz turnaround set after stage 2). | Baseline. |
| 1 | Introduce `ElementSequence` type; rename `Part.events` → `Part.elementSequence`; add helpers. | ✓ |
| 2 | Merge chord-walker into main: VoicingSelector + VoicingProfile + 4 profile selectors + init_pitches rule-native. Selectors only consumed by their own demo patches. | ✓ K467 untouched; chord-walker demos untouched. |
| 3 | RealizationStrategy interface + registry + `"block"` and `"rhythm_pattern"` defaults. Registered but not yet wired to compose path. | ✓ |
| 4 | Composer realize-to-events sub-step: implement for `MelodicFigure` → Note Events. Result populates `elementSequence` in parallel with Conductor still walking the tree. | ✓ |
| 5 | Same for `ChordFigure` → Note Events. | ✓ |
| 6 | Same for Chord events → expand via VoicingSelector + RealizationStrategy → per-tone Notes. Chord events still emit to Conductor's `ChordPerformer` too. | ✓ |
| 7 | Switch Conductor: `perform()` reads `elementSequence` instead of walking passages. K467 golden set must produce identical audio. | ✓ (the moment of truth) |
| 8 | Delete Conductor's `perform_passage`, `perform_phrase`, `step_note`, `step_chord_tone`, `ChordPerformer`, `register_josie_figures`. Possibly absorb `ChordArticulation` → `ChordRealization` rename. | ✓ (already dead) |
| 9 | Drop `Chord` from `Element` variant. Migrate `Part::add_chord(...)` callers. | ✓ |
| 10 | Migrate `patches/test_k467_walker.json`: `chordConfig` → `realizationStrategy: "rhythm_pattern" + voicingSelector: "smooth"` with rhythm pattern + voicing profile. | ✓ if migration faithful; new golden if not. |
| 11 | Delete `ChordAccompanimentConfig` struct. | ✓ (no consumers) |
| 12 | Audit residual Phrase/Passage tree exposure; mark as Composer-internal. | ✓ |

**Each stage** ends with: build clean, render K467 pass3, hash-check vs the prior golden. If the stage is flagged "✓" and the hash matches, autonomously proceed to the next stage. Stop conditions:

- Any golden hash mismatch on a stage flagged "✓" (regression — surface to user with diff details).
- Compile failure that isn't an obvious one-line typo.
- Any stage where the commit-as-described turns out to need a non-trivial design call not anticipated by this spec (surface to user before improvising).

Goldens to enforce:
- `test_k467_walker` (primary — exercises chordConfig path), `test_k467_harmony`, `test_k467_period`, `test_k467_structural` — present from stage 0 onward.
- Jazz turnaround demos (7 patches: `test_jazz_turnaround_*`) — baseline pinned at stage 2 (when chord-walker lands), bit-identical from stage 3 onward.
- Other existing renders (fhn, sweep, additive, etc.) — Sound-tier, presumably untouched. Spot-check one per family at stage 7.

## Resolved during design discussion

- **Whole bullet, not chord-only.** The tier slush applies equally to melody-side pitch resolution (`step_note` in Conductor) and chord-side handling (`step_chord_tone`, `ChordPerformer`). Fixing only the chord flavor would leave the same architectural smell on the melody side.
- **`Part.elementSequence` is authoritative.** Tree becomes Composer-internal scratch.
- **A1 (Pass-then-realize), not A2 (Direct-emit).** Strategies continue to build the tree; a new realize sub-step walks the tree to populate elementSequence. A2 is the eventual destination via a future cleanup pass.
- **Chord-walker merges in this refactor** (option A, not B/C from the brainstorm).
- **RealizationStrategy is a registry-backed Compose-tier interface** (option X, not Y), parallel to VoicingSelector. The two are co-equal.
- **`ChordAccompanimentConfig` dissolves.** Not "deprecated and kept around"; deleted in stage 11. Rhythm pattern absorbs into `RhythmPattern` config of `RhythmPatternRealizationStrategy`; voicing hints absorb into `VoicingProfile`.
- **Conductor signature stays `perform(Piece&)` for now.** Tighten later if we find zero passage reads.
- **Compose-tier output = all musical Events.** Performer expands Articulation/Ornament/Tempo/Dynamic markings (which DO appear in elementSequence, on Notes or as standalone events) and applies attack-time deviations (swing, humanization). Markings as data → score is printable.
- **`ChordRealization` rename absorbed into stage 8** (not given its own stage).
- **DrumPerformer fate deferred.** Likely follows ChordPerformer out post-refactor. Decide after stage 8.
- **Conductor name stays.** Narrowed scope is real but a rename adds churn for no functional gain.

## Open questions to settle at planning time

- **DynamicState location.** Move to `mforce/music` (shared between Composer-realize and Conductor-perform) or duplicate the velocity-curve helper. Lean: move.
- **`Part::add_chord(...)` post-stage-9.** Delete (callers migrate to a Composer-helper that does VoicingSelector + RealizationStrategy expansion synchronously) or keep as a thin wrapper around that helper. Decide based on call-site count.
- **Stage 6 chord-event source.** Today `Chord` events on a Part come from either direct-build (`add_chord`) or HarmonyComposer / AFS auto-generation. Confirm at plan time that both sources route through the same realize path; no second copy of the expansion logic.
- **`StepMode::ChordTone` field.** Was orphaned because AFS bypassed it. Under the refactor, where chord-tone vs scale-degree resolution lives in Composer's realize step, the field could finally have a consumer (or stay orphaned and get deleted). Decide at plan time when stage 4–5 is being written.
- **`RealizationRequest` shape.** The fields listed are a starting point. May need to add `Scale`, melody-pitch hint, `DynamicState` pointer, etc. as the strategies are written. Don't over-spec now.
- **Voicing-selector wiring at stage 6.** Default is `"smooth"` if Part has any `voicingSelector` config OR `voicingProfile` OR `realizationStrategy` set. Otherwise fallback to legacy `sc->resolve(...)` for one stage so K467 (which has none of those at stage 6) still works. The fallback path goes away at stage 10 when K467 migrates.
- **Voicing-selector wiring fallback removal at stage 10.** Once `test_k467_walker.json` migrates, the legacy `sc->resolve(...)` fallback path introduced at stage 6 is dead. Confirm via grep and delete in stage 10's commit.
- **`ChordArticulation` rename vs delete in stage 8.** Decided based on whether any post-stage-8 consumers exist (likely none; the Josie library was its main consumer and that gets deleted).

## What this enables downstream

- **Score export.** ElementSequence is now a complete authoritative score; printable lead-sheet / piano-roll views become a straightforward read.
- **Genre-specific RealizationStrategies.** `"alberti"`, `"strum_down"`, `"jazz_comp"`, `"basso_continuo"` plug in via the registry; PassageTemplate selects per Section.
- **Cross-Part voicing coordination.** `VoicingRequest.melodyPitch` (currently stubbed) gets a real consumer once melody Part is fully realized at compose time before chord Parts run their realize.
- **Direct-emit refactor (A2).** With realize-to-events working, the strategies can later be refactored to emit directly into ElementSequence without an intermediate tree. Future cleanup, not blocking.
- **Conductor signature tightening.** Post-refactor: drop `Piece&` for a narrower input if no tree reads survive.
- **DrumPerformer migration.** Same pattern as ChordPerformer, applied post-refactor.
- **Voicing-selector open items** (upward drift, cadential role, boring-repeat) become tractable as in-Composer fixes rather than cross-tier negotiations.
- **AI / agentic composition.** Models that emit ElementSequences directly bypass the tree entirely; the contract is well-defined.
