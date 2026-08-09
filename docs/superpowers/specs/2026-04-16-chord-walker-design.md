# ChordWalker: Constrained Chord-Progression Engine

## Problem

Chord progressions are currently either hand-authored in JSON or selected by name from `ChordProgressionBuilder`'s hard-coded lambdas. There's no way to *generate* a progression that respects genre conventions, satisfies cadence constraints, and fills an arbitrary time span. This blocks composition quality for any passage longer than a known cliché.

## Core Concept

A **constrained walk** over a genre-specific **chord transition graph**. The graph encodes which chords can follow which, with what weight, conditioned on recent history (variable-order Markov with back-off). The walk is pinned by structural constraints — start chord, end chord (cadence target), beat budget — provided by the calling strategy or Composer.

The engine is universal: every passage that needs harmony uses ChordWalker. What varies is the **constraint set** (tight for period forms, loose for free passages) and the **style table** (Mozart vs. Beethoven vs. rock).

## Design Decisions

### 1. Harmony Ownership is Per-Passage

A Section's harmony is not a monolithic `ChordProgression`. It's assembled from **passage-level segments**, each owned by whichever strategy produced it. When a passage is regenerated, only its segment changes; other Parts' corresponding passages become stale (UI prompts the user, no automatic cascade for now).

### 2. Strategy Declares Scope

Each `PassageStrategy` declares whether it produces melody only or melody + harmony:

```cpp
enum class StrategyScope { Melody, MelodyAndHarmony };

class PassageStrategy {
public:
  virtual StrategyScope scope() const { return StrategyScope::Melody; }
  // ...
};
```

- `Melody` (default): Composer ensures harmony exists for this passage's time span before calling the strategy. Harmony is generated via ChordWalker or pre-authored.
- `MelodyAndHarmony`: Composer skips harmony pre-population for this time span. The strategy itself calls ChordWalker (with its own constraints) and writes the resulting segment.

Scope is a property of the **strategy class**, not the template instance. Switching a passage's strategy triggers re-evaluation: if old was `MelodyAndHarmony` and new is `Melody`, Composer clears the old harmony segment and runs ChordWalker to fill the gap.

### 3. PPS Constrains ChordWalker, Not Bypasses It

`PeriodPassageStrategy` (scope `MelodyAndHarmony`) doesn't hard-code chord sequences. It provides **structural pins** to ChordWalker:

- Antecedent: "start on I, arrive at V by beat N" (half cadence)
- Consequent: "start on I (or V), arrive at I by beat M" (authentic cadence)

ChordWalker fills the path between pins using the style table. For simple Mozart (I–V...V7–I), the walk is trivially short. For Beethoven or Schoenberg, the same engine traverses a richer graph with more intermediate movement.

The cadence targets that PPS already selects (HC, PAC, IAC) translate directly into ChordWalker end-chord constraints.

### 4. Style Tables in JSON

A style table is a JSON file loaded by name (e.g., `"classical_mozart"`, `"rock_basic"`). It encodes the transition graph:

```json
{
  "name": "classical_mozart",
  "description": "Common-practice diatonic transitions, Mozart-era",
  "defaultOrder": 1,
  "transitions": {
    "I":    [["IV", 3], ["V", 4], ["vi", 2], ["ii", 2], ["iii", 1]],
    "ii":   [["V", 5], ["viio", 2], ["IV", 1]],
    "iii":  [["vi", 3], ["IV", 2]],
    "IV":   [["V", 5], ["I", 2], ["ii", 1]],
    "V":    [["I", 5], ["vi", 3], ["IV", 1]],
    "V7":   [["I", 6], ["vi", 2]],
    "vi":   [["ii", 3], ["IV", 3], ["V", 2]],
    "viio": [["I", 4], ["iii", 2]]
  },
  "overrides": {
    "I,vi":  [["ii", 5], ["IV", 2]],
    "V,vi":  [["IV", 5], ["ii", 2]],
    "IV,V":  [["I", 6], ["vi", 2]]
  }
}
```

- `transitions`: first-order (bigram) — `from: [(to, weight), ...]`
- `overrides`: higher-order — `"prev,curr": [(to, weight), ...]`. Longest match wins; back-off to first-order if no override exists.
- Weights are relative (not normalized to 1) — engine normalizes at sampling time.
- Chord labels are scale-relative shorthand (`I`, `ii`, `V7`, `bVII`, etc.), resolved against the section's key at walk time. A label parser maps these to `ScaleChord` (degree + alteration + quality).
- Override keys read left-to-right as temporal order: `"I,vi"` means "previous chord was I, current chord is vi."

### 5. ChordWalker API

```cpp
struct WalkConstraint {
  ScaleChord startChord;          // first chord of the walk
  std::optional<ScaleChord> endChord;  // cadence target (if any)
  float totalBeats;               // beat budget
  float minChordBeats{2.0f};      // minimum harmonic rhythm
  float maxChordBeats{8.0f};      // maximum harmonic rhythm
};

struct ChordWalker {
  // Load a style table by name from the styles/ directory
  static ChordWalker load(const std::string& styleName);

  // Generate a constrained chord progression
  ChordProgression walk(const WalkConstraint& constraint, uint32_t seed) const;
};
```

The `walk()` method:
1. Starts at `startChord`.
2. At each step, queries the style table for valid next chords (checking overrides first, falling back to first-order).
3. Samples next chord using RNG seeded by `seed`.
4. Assigns a duration within `[minChordBeats, maxChordBeats]` bounds. Duration selection can be uniform-random within bounds, or style tables can specify a preferred harmonic rhythm (e.g., "one chord per bar" = 4 beats in 4/4). Strong-beat preference: chord changes favor landing on beat 1 or 3.
5. If `endChord` is set, steers toward it as remaining beats shrink (approach logic — increases weight of chords that can reach the target in 1–2 steps).
6. Final chord is forced to `endChord` if set.

### 6. Composer Orchestration

The Composer's section-level loop becomes:

```
for each passage in section:
  strategy = resolve_strategy(passage.template)
  if strategy.scope() == MelodyAndHarmony:
    mark passage's time span as "strategy-owned harmony"
  
for each unclaimed time span in section:
  run ChordWalker with section-level constraints
  write resulting ChordProgression segment

for each passage in section (ordered: MelodyAndHarmony first):
  if strategy.scope() == MelodyAndHarmony:
    call strategy (it calls ChordWalker internally with its own constraints)
    write harmony segment to section timeline
  else:
    provide harmony segment via Locus
    call strategy (melody only)
```

`MelodyAndHarmony` passages compose first so their harmony segments are available when `Melody` passages in other Parts need them.

### 7. Section Harmony Timeline

`Section` gains a `HarmonyTimeline` — an ordered list of `HarmonySegment`:

```cpp
struct HarmonySegment {
  float startBeat;
  float endBeat;
  ChordProgression progression;
  std::string ownerStrategy;  // which strategy produced this (for invalidation)
};

struct HarmonyTimeline {
  std::vector<HarmonySegment> segments;  // non-overlapping, ordered by startBeat

  // Query: what chord is active at beat B?
  const ScaleChord* chord_at(float beat) const;
  
  // Query: what's the full progression for [start, end)?
  ChordProgression slice(float startBeat, float endBeat) const;
  
  // Write: replace the segment for [start, end)
  void set_segment(float startBeat, float endBeat, 
                   ChordProgression prog, const std::string& owner);
};
```

`Locus` gains access to the timeline so any strategy can query `chord_at(beat)` during composition.

## Relationship to Existing Code

- **ChordProgressionBuilder**: becomes a convenience wrapper — its named lambdas (`I-V7-V7-I`) turn into pre-authored style-table walks or inline progressions. Kept for backwards compatibility but no longer the primary generation path.
- **ChordProgression**: unchanged — still `PulseSequence + ScaleChordSequence`. ChordWalker produces these.
- **ScaleChord**: unchanged — ChordWalker works in scale-relative space.
- **PeriodPassageStrategy**: gains `scope() = MelodyAndHarmony`, calls ChordWalker with cadence-derived constraints.
- **Locus**: gains `const HarmonyTimeline*` so strategies can query per-beat chord context.
- **PitchReader**: future work — could consult chord context to prefer chord tones during step resolution.

## What This Does NOT Cover

- **Regeneration/invalidation cascade**: when a MelodyAndHarmony passage regenerates, other Parts' passages for that time span become stale. For now, UI prompts; no automatic re-render.
- **Cliché library**: curated named progressions as first-class phrase templates. Complementary to ChordWalker; deferred to a separate design.
- **Modulation planning**: key-center changes within a section. ChordWalker operates within a single key; modulations would be a layer above that constrains which key the walker uses per sub-span.
- **Voice leading / chord voicing**: ChordWalker produces scale-relative chord identities, not voiced pitches. Voicing remains the Conductor/ChordPerformer's job.
- **PitchReader chord-tone awareness**: melody strategies could use chord context to prefer chord tones. Separate enhancement.

## Style Table Seeding

Initial tables to author:

| Style | Description | Key transitions |
|-------|-------------|-----------------|
| `classical_mozart` | Common-practice diatonic, Mozart-era | Strong V→I, ii→V, I→IV/V |
| `classical_beethoven` | Wider palette, more chromaticism | bVI, Aug6, secondary dominants |
| `rock_basic` | I–IV–V–vi with bVII | bVII common, IV↔V free |
| `pop_four_chord` | Ultra-constrained | I–V–vi–IV loop with variants |

Start with `classical_mozart` since K467 is the test case.
