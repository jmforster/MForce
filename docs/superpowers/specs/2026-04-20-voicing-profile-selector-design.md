# Voicing Profile Selector — Design

## Context

With the priority knob (`voicingPriority`), dictionary knob (`voicingDictionary`), and bidirectional inversion search now landed (commit `2879eca` on `chord-walker` branch), the `SmoothVoicingSelector` can produce meaningfully different voicings per chord based on configuration. But that configuration is **static across a Passage**: one priority, one dictionary. Every chord is scored and picked under the same rules.

This spec also folds in a **core cleanup of `Chord::init_pitches`** that emerged during brainstorm: moving from the current (move-lowest-up + unused spread) model to rule-native (inversion = list rotation / bass chord tone, spread = voicing-gap walk with wrap-around). The current negative-inversion hack (added in commit `2879eca` to reach classical 3rd-inversion-with-7-in-bass voicings) gets replaced by the cleaner parameterization: the selector searches across `rootOctave` variations, not negative inversions.

Matt's brainstorm raised two adjacent needs:

1. **Inversion allowlist** — some inversions don't belong in some genres. Rock rarely uses 3rd inversion; classical chorale allows all; jazz might skip certain shapes. A hard filter on which inversions the selector considers.
2. **Per-chord jitter** — even within a single Passage, voicing-selection rules can vary to add variety without losing coherence. E.g., priority drifts across a section; a subset of allowed inversions rotates; beat-1 chords get tight voicings, beat-3 chords go more open.

Both can live as independent `PassageTemplate` fields, but (1) naturally wants to participate in (2)'s variation. So the design question is: **where does the per-chord state live, and what generates it?**

## Problem

Current selector is stateless per Passage. `VoicingRequest` gets the same `priority` for every chord. To introduce per-chord variation we need either:

- A function that the selector calls per chord to get fresh config, or
- An upstream stage that pre-computes per-chord configs before the selector runs.

Second question: how is the variation *authored*? Most users won't want to enumerate priority per chord by hand. They want to say things like "bluesy comping" or "swing from tight to open over 4 bars" as a single choice.

## Goals

- `allowedInversions` as a filter on candidates, respected by `SmoothVoicingSelector`.
- Per-chord variation of (at minimum) `voicingPriority` and `allowedInversions` via a configurable "voicing profile profile selector."
- Authoring-ergonomic surface: common-case configs compose from a few fields; bespoke configs still possible.
- Consistent architectural shape with `ChordProfile Selector` / `StyleTable` so the codebase doesn't grow orthogonal patterns for parallel problems.
- Back-compat: patches that don't set a profile selector keep the current static-config behavior.

## Non-goals

- Variation of `voicingDictionary` per chord. In practice one instrument = one dictionary; dictionary changes mid-Part would imply instrument switching, which is beyond this scope.
- Authoring a library of pre-defined genre profile selectors ("jazz-comp", "rock", "chorale"). This spec only defines the mechanism and ships one or two demonstration profile selectors; curated genre libraries come later.
- Cross-Part voicing coordination (e.g., piano and guitar agreeing on density). Out of scope; parked until the cross-Part awareness work is done.
- Learning profile selector parameters from a corpus. Profile Selectors are authored, not trained.

## Design

### Data model

**`VoicingProfile`** — the configuration bundle the selector consults per chord:

```cpp
struct VoicingProfile {
    std::vector<int> allowedInversions;   // empty = any; else filter
    std::vector<int> allowedSpreads;      // empty = any; else filter
    float priority{0.0f};                 // [0,1] — CT vs VL weighting
};
```

Replaces the scalar `voicingPriority` on `PassageTemplate`. A `VoicingProfile` is the smallest unit of voicing profile.

**Note on octave search**: the selector also enumerates across `rootOctave` variations (e.g., prev ± 1), but this is a selector-internal search dimension, not a profile parameter. The profile selector controls *which inversions and spreads are in play*; the selector figures out the best octave placement given those constraints.

**`VoicingProfileSelector`** — abstract interface that produces a `VoicingProfile` per chord:

```cpp
class VoicingProfileSelector {
public:
    virtual ~VoicingProfileSelector() = default;
    virtual std::string name() const = 0;

    // Called once per Passage, before any chord-selection calls.
    virtual void reset(uint32_t seed) = 0;

    // Called per chord in chord order. `beatInBar` helps profile selectors that
    // care about metric position (beat 1 vs beat 3 voicing).
    virtual VoicingProfile profile_for_chord(
        int chordIdx,
        float beatInBar,
        float beatInPassage) = 0;
};
```

Registered in a `VoicingProfileSelectorRegistry`, parallel to `VoicingSelectorRegistry` and `StrategyRegistry`.

**`PassageTemplate` extensions:**

```cpp
struct PassageTemplate {
    // ... existing fields ...

    // Default profile applied when no profile selector is set, or as the baseline
    // the profile selector modulates.
    VoicingProfile voicingProfile;

    // Optional named profile selector + its config. Empty = static use of
    // voicingProfile for every chord.
    std::string voicingProfileSelector;
    json voicingProfileSelectorConfig;  // profile selector-specific params
};
```

The existing `voicingPriority` field migrates into `voicingProfile.priority`. Back-compat achieved by JSON reader supporting both flat and nested forms during a transition period.

### Profile Selector implementations (this spec ships)

**1. `StaticVoicingProfileSelector`** — degenerate profile selector returning the passage's `voicingProfile` unchanged for every chord. The implicit default when no profile selector is named.

**2. `RandomVoicingProfileSelector`** — per-chord uniform random sample within a range.

```json
{
  "voicingProfileSelector": "uniform_jitter",
  "voicingProfileSelectorConfig": {
    "priorityRange": [0.2, 0.8],
    "inversionProfiles": [
      [-1, 0, 1],
      [-2, -1, 0, 1, 2],
      [0, 1]
    ]
  }
}
```

Each chord: samples priority uniformly from `[0.2, 0.8]`; picks one inversionProfile uniformly. Independent samples — noisy but maximally varied.

**3. `DriftVoicingProfileSelector`** — smooth random walk within bounds, more coherent than independent sampling.

```json
{
  "voicingProfileSelector": "drift",
  "voicingProfileSelectorConfig": {
    "priorityRange": [0.0, 1.0],
    "priorityStepMax": 0.15,
    "inversionProfiles": [[0, 1], [-1, 0, 1, 2]],
    "profileTransitionProb": 0.2
  }
}
```

Each chord: priority += gaussian-step (mean 0, stddev `priorityStepMax`), clamped to range; with `profileTransitionProb` swap to a different profile. Bias toward continuity — a bar of chords trends in one direction before drifting back.

**4. `ScriptedVoicingProfileSelector`** — explicit per-chord profile sequence, cycles. Deterministic, for authored compositions.

```json
{
  "voicingProfileSelector": "scripted",
  "voicingProfileSelectorConfig": {
    "sequence": [
      {"priority": 0.0, "allowedInversions": [0, 1]},
      {"priority": 0.5, "allowedInversions": [-1, 0, 1]},
      {"priority": 0.8, "allowedInversions": [-2, -1, 0, 1, 2]},
      {"priority": 0.0, "allowedInversions": [0]}
    ]
  }
}
```

Profile Selector returns `sequence[chordIdx % sequence.size()]`. Use when you want "bar 1 tight, bar 2 loose, bar 3 very loose, bar 4 back home" as a repeating figure.

### Integration in the selector

`SmoothVoicingSelector::select` consults the profile for its chord:

```cpp
VoicingProfile profile = req.profile;  // from caller, via profile selector

// Filter candidates by allowedInversions
std::vector<Candidate> cands;
for (int inv = -(n-1); inv < n; ++inv) {
    if (!profile.allowedInversions.empty() &&
        std::find(profile.allowedInversions.begin(),
                  profile.allowedInversions.end(), inv)
        == profile.allowedInversions.end()) {
        continue;   // skip disallowed inversions
    }
    // ... score candidate as before, using profile.priority
}
```

If the filter eliminates all candidates (a pathological config), fall back to inv=0. Emit a warning once per Passage.

### Caller wiring

`realize_chord_parts_` instantiates the profile selector once per Passage, resets it, and calls `profile_for_chord` before each selector invocation:

```cpp
// Resolve profile selector (empty name = StaticVoicingProfileSelector wrapping voicingProfile)
VoicingProfileSelector* profile selector = /* lookup or stub */;
profile selector->reset(masterSeed + hash_of("voicing-profile") + passageIndex);

int chordIdx = 0;
for (float dur : pattern) {
    // ... existing dispatch ...
    const ScaleChord* sc = sec.harmonyTimeline.chord_at(pos - beatOffset);
    if (sc) {
        VoicingProfile profile = profile selector->profile_for_chord(
            chordIdx, beat_in_bar(pos), pos - sectionStart);
        VoicingRequest req{*sc, &sec.scale, cfg.octave, dur,
                           prevChord, std::nullopt,
                           profile, // carries priority + allowedInversions
                           passIt->second.voicingDictionary};
        chord = selector->select(req);
        // ...
        ++chordIdx;
    }
}
```

The `VoicingRequest` shape changes: the scalar `priority` becomes the profile bundle. Wiring downstream (SmoothVoicingSelector, any future selectors) reads `req.profile.priority` and `req.profile.allowedInversions`.

### Back-compat

- `voicingPriority` at PassageTemplate top level continues to parse; loader lifts it into `voicingProfile.priority`.
- `allowedInversions` (new, proposed in brainstorm for #1) lives only in `voicingProfile`, accepted at the top level too for author convenience, lifted the same way.
- Patches with no profile selector field use `StaticVoicingProfileSelector` over the `voicingProfile` — exact equivalent of pre-profile selector behavior.
- Existing test patches (`test_jazz_turnaround_p*.json`) render bit-identical.

### Seeding and reproducibility

The profile selector's reset seed derives from the PieceTemplate masterSeed + a selector-specific salt + passage position. This keeps profile-selector randomness deterministic under a fixed seed (same rendering story as rest of composer RNG) and isolated from other random streams (melody generation, ChordWalker) so tweaking one doesn't destabilize the others.

## Migration plan

**Stage 0 — Baseline.** Re-render `test_jazz_turnaround_{flat,p0,p05,p1}` and `test_k467_pass3`; record peak/rms and JSON md5 for each as regression baselines.

**Stage 1 — Rule-native `init_pitches` + remove negative inversion.** Rewrite `Chord::init_pitches` to use rule-native semantics: inversion = bass chord-tone index (0..N-1); spread applied per the verified walk-with-wraparound rule. Remove the negative-inversion branch added in `2879eca`. Update `SmoothVoicingSelector` to enumerate `(inversion × spread × rootOctave)` triples instead of `[-(N-1)..+(N-1)]` signed inversions. Goldens will change (new voicings appear; register choices differ) — Matt listens and confirms before committing.

**Stage 2 — `VoicingProfile` + field lift.** Introduce `VoicingProfile { allowedInversions, allowedSpreads, priority }`. Move `voicingPriority` into it; keep flat-field JSON compatible via reader lifting. No behavioral change (empty allow-lists = all). Bit-identical vs Stage 1 regression.

**Stage 3 — `allowedInversions` / `allowedSpreads` in selector.** Selector filters candidates by the profile's allow-lists. Empty = current (all allowed). Bit-identical for patches that don't set them.

**Stage 4 — Profile Selector interface + registry + `StaticVoicingProfileSelector`.** Abstract interface, `StaticVoicingProfileSelector` registered as default. `realize_chord_parts_` routes through profile selector. Bit-identical.

**Stage 5 — `RandomVoicingProfileSelector`.** First stochastic profile selector. Demo patch over jazz turnaround.

**Stage 6 — `DriftVoicingProfileSelector`.** Coherent random walk. Demo patch.

**Stage 7 — `ScriptedVoicingProfileSelector`.** Deterministic per-chord sequencing. Demo patch.

**Stage 8 — A/B listening + tuning.** Matt compares the three profile selectors; tune defaults and document per-profile selector recommendations.

Each stage commits independently. Stage 1 is the load-bearing refactor — the others are incremental additions.

## Resolved during design discussion

- **Inversion allowlist (#1) lives inside `VoicingProfile`**, not as a standalone PassageTemplate field — because profile selectors need to vary it per chord.
- **Per-chord jitter (#2) lives in a Profile Selector abstraction**, parallel to `ChordProfile Selector` in spirit. Multiple profile selector implementations ship; patches pick by name.
- **`voicingDictionary` does not vary per chord.** It's a Part/instrument property, not a stylistic one.
- **Back-compat via JSON reader lifting**: flat top-level `voicingPriority` / `allowedInversions` get pulled into `voicingProfile`. No schema version bump needed.
- **Rule-native `init_pitches` semantics** (verified against legacy comments and Matt's mental model):
  - `inversion` = chord-tone index of bass (0..N-1). A list rotation. Does *not* imply octave-shifting.
  - `spread` = voicing-gap walk rule: "from bass, advance (spread+1) chord-tone positions to next voice; if landing pitch-class is already voiced, advance one more until a new pitch-class appears." Invariant: each chord tone voiced exactly once; at least `spread` unvoiced positions between each voiced pair.
- **Negative inversion removed.** The hack that gave classical-3rd-inversion-with-bass-in-low-register voicings is replaced by the selector enumerating across `rootOctave` values. Cleaner and orthogonal to inversion semantics.
- **Selector search space**: `inversion × spread × rootOctave`. For a 4-voice chord with 4 inversions, 3 spread values (0..2), and 3 octave choices (prev ± 1): 36 candidates per chord.

## Open questions for plan time

- **Seed hashing** for profile selector reset — what salt scheme keeps profile selector randomness isolated from other composer RNG? Candidate: `masterSeed ^ 0x56_6F_69_53u ^ passageIndex` or similar. Plan picks.
- **`beat_in_bar` / `beat_in_passage` semantics** — float beats from the current bar's start, and from the passage's start. Both derived from the current `pos`. Confirm implementation reads these from `sec.meter` correctly.
- **What happens when `allowedInversions` is empty after filtering** (e.g., profile selector emits `[3]` but chord only has 3 voices so inv=3 is invalid) — fall back to canonic inv=0 with a stderr warning? Return the unmodified base chord? Plan picks.
- **Should profile selector state persist across Passages in the same Section?** Argument for: a section-long drift is more musical than reset-per-passage. Argument against: coupling. Plan decides; initial lean is reset-per-passage with an opt-in flag to continue.
- **Gaussian step in DriftVoicingProfileSelector** — use `Randomizer::gaussian()` if it exists, or roll our own Box-Muller? Plan picks.

## What this enables downstream

- **Genre profile selectors** — `JazzCompProfile Selector`, `RockRhythmProfile Selector`, `ChoraleProfile Selector`, each with tuned defaults for priority range and inversion profiles. Future work, but the extension point is here.
- **Section-level style arcs** — drift profile selectors whose center-point evolves across a Section can produce intro-verse-chorus arcs where voicings progressively open up.
- **Cross-Part style coordination** — later, a single `VoicingProfileSelector` instance shared across Piano and Guitar Parts of the same Section would let them move in sync.
- **Educational exploration** — musicians can audition different profile selectors on the same progression to hear how voice-leading rules reshape a piece.
