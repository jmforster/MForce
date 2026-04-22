# Composer Owns the ElementSequence Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use `superpowers:subagent-driven-development` (recommended) or `superpowers:executing-plans` to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Move pitch resolution and chord realization out of `Conductor` into `Composer`. Composer fully populates `Part.elementSequence`; Conductor reads only that. Absorb the chord-walker voicing work and introduce `RealizationStrategy` as a Compose-tier registry parallel to `VoicingSelector`. Dissolve `ChordAccompanimentConfig`.

**Spec:** `docs/superpowers/specs/2026-04-22-composer-owns-event-sequence-design.md` — read before starting.

**Architecture:** 12 sequential stages, each one commit (or small commit cluster), each ending with a build + render + golden hash check. Stages chain autonomously on green; stop on red.

**Key staging principle:** The Conductor dispatch becomes elementSequence-driven at **Stage 2** (early), with the legacy tree-walk path retained ONLY as a fallback for Parts whose elementSequence is still empty during the migration. Subsequent stages (5, 6, 7) populate elementSequence for one Part type at a time; as each type lands, dispatch automatically routes it through `perform_events`, skipping the fallback. Stage 8 deletes the now-unreachable fallback. The "Conductor reads only elementSequence" principle is true in dispatch logic from Stage 2 onward; the tree-walk fallback is migration scaffolding, not part of the architecture.

**Tech Stack:** C++17, CMake, Visual Studio (Windows). Bash shell. nlohmann/json. Existing `mforce_cli --compose` mode for renders.

---

## Conventions (read first)

**Build command** (run from repo root):

```bash
cmake --build build --config Release --target mforce_cli
```

If the build directory doesn't exist or CMake config is stale, regenerate:

```bash
cmake -B build -S . -G "Visual Studio 17 2022" -A x64
```

**Render command** (one golden):

```bash
build/tools/mforce_cli/Release/mforce_cli.exe --compose patches/<patch>.json renders/<prefix> 1
```

For chord-progression-style patches (e.g. jazz turnarounds) that use `--chords` mode instead, use the existing invocation in their respective demo scripts.

**Golden hash check** (bash, on Windows):

```bash
sha256sum renders/<prefix>_1.wav
```

Compare to baseline-recorded hash. Mismatch on a "✓ bit-identical" stage = STOP and surface to user.

**Goldens to enforce** (set at Stage 0):
- `patches/test_k467_walker.json` — primary; only patch using `chordConfig`. Touches chord-walker AND realize paths.
- `patches/test_k467_harmony.json` — exercises HarmonyComposer + AFS + MelodicFigure + ChordFigure paths.
- `patches/test_k467_period.json` — period-form passage strategy.
- `patches/test_k467_structural.json` — broader structural coverage.
- `patches/test_jazz_turnaround_*.json` (7 patches) — pinned at Stage 3 when chord-walker lands.
- Sound-tier spot-check (one per family) at Stage 8: `patches/fhn_pure_tone.json`, `patches/Additive1.json`, `patches/sweep/<one>.json`.

**Commit message style:** `<verb>(<scope>): <subject>` matching existing repo convention (e.g. `refactor(structure): introduce ElementSequence type`). Each commit ends with the standard Co-Authored-By footer.

**Stop conditions for autonomous execution:**
1. Any golden WAV hash mismatch on a step flagged "✓ bit-identical" → surface diff details (which patch, hash before/after, possibly first sample diverging) to user.
2. Compile failure that isn't an obvious one-line typo → surface to user.
3. Any task where the plan as written turns out to need a non-trivial design call not anticipated → surface to user before improvising.

**Worktree:** This is a substantial refactor; create a worktree before Stage 1 (the brainstorming skill normally does this; verify before proceeding).

---

## File Structure

**New files (created during plan):**
- `engine/include/mforce/music/realization_strategy.h` — interface, registry, RealizationRequest, BlockRealizationStrategy, RhythmPatternRealizationStrategy (Stage 4)
- `engine/include/mforce/music/dynamic_state.h` — DynamicState moved from conductor.h so Composer-realize can also use it (Stage 5)
- `engine/include/mforce/music/pitch_walker.h` — `step_note` / `step_chord_tone` moved from conductor.h to be reused in Composer-realize (Stage 5)
- (No new test files — the project relies on golden renders rather than unit tests; existing test patches are the fixtures.)

**Files modified:**
- `engine/include/mforce/music/structure.h` — Element, Part, introduce ElementSequence (Stages 1, 9)
- `engine/include/mforce/music/composer.h` — `realize_chord_parts_`, new realize sub-step (Stages 5–7, 9)
- `engine/include/mforce/music/conductor.h` — exclusive dispatch (Stage 2), narrow scope (Stage 8)
- `engine/include/mforce/music/templates.h` — PassageTemplate fields, dissolve ChordAccompanimentConfig (Stages 4, 11)
- `engine/include/mforce/music/templates_json.h` — JSON read/write for new fields, remove old (Stages 4, 11)
- `tools/mforce_cli/main.cpp` — possibly minor signature touch-ups if Conductor signature changes (low likelihood; Stage 8)
- `patches/test_k467_walker.json` — chordConfig migration (Stage 10)
- chord-walker headers — land via merge (Stage 3): `voicing_profile.h`, `voicing_profile_selector.h`, `voicing_selector.h`, `smooth_voicing_selector.h`, `static_voicing_profile_selector.h`, `random_voicing_profile_selector.h`, `drift_voicing_profile_selector.h`, `scripted_voicing_profile_selector.h`, `engine/src/chord.cpp`, plus 7 `patches/test_jazz_turnaround_*.json`

---

## Stage 0 — Pin baseline goldens

**Files:** none modified. Create `docs/superpowers/baselines/2026-04-22-composer-owns-events-baselines.txt`.

- [ ] **0.1** Build clean from current `main`:

```bash
cmake --build build --config Release --target mforce_cli
```

Expected: clean build, no warnings introduced.

- [ ] **0.2** Render each primary K467 golden and record SHA-256:

```bash
build/tools/mforce_cli/Release/mforce_cli.exe --compose patches/test_k467_walker.json renders/baseline_k467_walker 1
build/tools/mforce_cli/Release/mforce_cli.exe --compose patches/test_k467_harmony.json renders/baseline_k467_harmony 1
build/tools/mforce_cli/Release/mforce_cli.exe --compose patches/test_k467_period.json renders/baseline_k467_period 1
build/tools/mforce_cli/Release/mforce_cli.exe --compose patches/test_k467_structural.json renders/baseline_k467_structural 1
sha256sum renders/baseline_k467_*.wav > docs/superpowers/baselines/2026-04-22-composer-owns-events-baselines.txt
```

Expected: 4 hashes recorded.

- [ ] **0.3** Spot-check sound-tier patches (one per family). Render each and record hash to the same baseline file:

```bash
build/tools/mforce_cli/Release/mforce_cli.exe --patch patches/fhn_pure_tone.json renders/baseline_sound_fhn 1
build/tools/mforce_cli/Release/mforce_cli.exe --patch patches/Additive1.json renders/baseline_sound_additive 1
sha256sum renders/baseline_sound_*.wav >> docs/superpowers/baselines/2026-04-22-composer-owns-events-baselines.txt
```

(Verify that `--patch` is the correct subcommand for sound-only patches; `tools/mforce_cli/main.cpp:702` defines `run_patch`. Adjust invocation if needed.)

- [ ] **0.4** Commit baselines:

```bash
git add docs/superpowers/baselines/2026-04-22-composer-owns-events-baselines.txt
git commit -m "chore(baselines): pin goldens before Composer-owns-events refactor"
```

---

## Stage 1 — Introduce `ElementSequence`

**Files:**
- Modify: `engine/include/mforce/music/structure.h:171-213` (Part struct) and surrounding sections

**Bit-identical:** ✓

### 1a — Add ElementSequence type

- [ ] **1.1** In `structure.h`, add `ElementSequence` immediately after the `Element` definition (around line 75, before `Phrase`):

```cpp
// ===========================================================================
// ElementSequence — the realized events for a Part. Composer's authoritative
// output for that Part; Conductor consumes from here.
// ===========================================================================
struct ElementSequence {
    std::vector<Element> elements;
    float totalBeats{0.0f};

    void add(const Element& e) {
        elements.push_back(e);
        float endBeat = e.startBeats;
        if (e.is_note())  endBeat += e.note().durationBeats;
        else if (e.is_hit())   endBeat += e.hit().durationBeats;
        else if (e.is_rest())  endBeat += e.rest().durationBeats;
        else if (e.is_chord()) endBeat += e.chord().dur;  // remove with stage 9
        if (endBeat > totalBeats) totalBeats = endBeat;
    }

    void sort_by_beat() {
        std::stable_sort(elements.begin(), elements.end(),
            [](const Element& a, const Element& b) {
                return a.startBeats < b.startBeats;
            });
    }

    int size() const { return int(elements.size()); }
    bool empty() const { return elements.empty(); }

    auto begin() { return elements.begin(); }
    auto end()   { return elements.end(); }
    auto begin() const { return elements.begin(); }
    auto end()   const { return elements.end(); }
};
```

Add `#include <algorithm>` near the top if not already present.

### 1b — Rename `Part::events` → `Part::elementSequence`

- [ ] **1.2** In `structure.h`, change Part's `events` field to use `ElementSequence`:

Before (current `Part`):

```cpp
struct Part {
    std::string name;
    std::string instrumentType;
    std::unordered_map<std::string, Passage> passages;
    std::vector<Element> events;
    float totalBeats{0.0f};

    void add_chord(float startBeat, const Chord& chord) {
        events.push_back({startBeat, chord});
        totalBeats = std::max(totalBeats, startBeat + chord.dur);
    }
    // ... add_chord(no-arg), add_note (2 overloads), add_hit, add_rest
};
```

After:

```cpp
struct Part {
    std::string name;
    std::string instrumentType;
    std::unordered_map<std::string, Passage> passages;
    ElementSequence elementSequence;

    // Backwards-compat property: code that reads totalBeats keeps working.
    float totalBeats() const { return elementSequence.totalBeats; }

    void add_chord(float startBeat, const Chord& chord) {
        elementSequence.add({startBeat, chord});
    }

    void add_chord(const Chord& chord) {
        add_chord(elementSequence.totalBeats, chord);
    }

    void add_note(float startBeat, float noteNumber, float velocity, float durationBeats,
                  Articulation art = articulations::Default{}, Ornament orn = Ornament{}) {
        elementSequence.add({startBeat, Note{noteNumber, velocity, durationBeats, art, orn}});
    }

    void add_note(float noteNumber, float velocity, float durationBeats,
                  Articulation art = articulations::Default{}) {
        add_note(elementSequence.totalBeats, noteNumber, velocity, durationBeats, art);
    }

    void add_hit(float startBeat, int drumNumber, float velocity, float durationBeats = 0.1f) {
        elementSequence.add({startBeat, Hit{drumNumber, velocity, durationBeats}});
    }

    void add_rest(float durationBeats) {
        elementSequence.add({elementSequence.totalBeats, Rest{durationBeats}});
    }
};
```

Note: `totalBeats` becomes a method (was a field). Callers that wrote `part.totalBeats = X;` must change. Audit and fix.

### 1c — Fix all callers of `Part::events` and `Part::totalBeats`

- [ ] **1.3** Grep for all `Part`-field references that need updating:

```bash
grep -nrE "\.events\b" engine/include engine/src tools | grep -v "third_party"
grep -nrE "\.totalBeats\b" engine/include engine/src tools | grep -v "third_party"
```

For each hit:
- `part.events` (read or iterate) → `part.elementSequence` (the type now exposes begin/end and `elements`)
- `part.events.push_back(X)` → `part.elementSequence.add(X)` (the helper updates totalBeats)
- `part.totalBeats` (read) → `part.totalBeats()`
- `part.totalBeats = X` (write) → `part.elementSequence.totalBeats = X`

Most likely call sites: `conductor.h` (perform_events iterates events), `composer.h` (add_chord during chord-realize), `tools/mforce_cli/main.cpp` (direct-build patches).

- [ ] **1.4** Build:

```bash
cmake --build build --config Release --target mforce_cli
```

Expected: clean build. Likely 1-3 compile errors first round (caller signatures); fix and re-build until clean.

### 1d — Verify bit-identical

- [ ] **1.5** Render all 4 K467 goldens and compare hashes to baseline:

```bash
build/tools/mforce_cli/Release/mforce_cli.exe --compose patches/test_k467_walker.json renders/stage1_k467_walker 1
build/tools/mforce_cli/Release/mforce_cli.exe --compose patches/test_k467_harmony.json renders/stage1_k467_harmony 1
build/tools/mforce_cli/Release/mforce_cli.exe --compose patches/test_k467_period.json renders/stage1_k467_period 1
build/tools/mforce_cli/Release/mforce_cli.exe --compose patches/test_k467_structural.json renders/stage1_k467_structural 1
sha256sum renders/stage1_k467_*.wav
```

Expected: all 4 hashes match the corresponding `renders/baseline_k467_*` entries in the baselines file.

- [ ] **1.6** STOP if any hash differs. Investigate first divergence sample (binary diff tool: `cmp -l renders/baseline_k467_walker_1.wav renders/stage1_k467_walker_1.wav | head`) and surface to user.

- [ ] **1.7** Commit:

```bash
git add engine/include/mforce/music/structure.h engine/include/mforce/music/conductor.h engine/include/mforce/music/composer.h tools/mforce_cli/main.cpp
git commit -m "refactor(structure): introduce ElementSequence; rename Part.events"
```

(Adjust staged files based on what 1.3 actually touched.)

---

## Stage 2 — Make Conductor dispatch elementSequence-only (with tree-walk fallback)

**Files:**
- Modify: `engine/include/mforce/music/conductor.h:685-701` (the `for (const auto& part : piece.parts)` dispatch in `perform(Piece&)`)

**Bit-identical:** ✓ (no Part currently has both `elementSequence` and `passages` populated; the change is invisible today but enforces the architectural rule going forward)

**Rationale:** Today the dispatch is *additive* — both `perform_events(elementSequence)` AND `perform_passage(tree)` run for the same Part. That's a latent bug that current code happens not to trigger. The refactor's substantive stages (5, 6, 7) populate `elementSequence` for Part types one at a time; we need the dispatch to be *exclusive* (elementSequence wins; passage walk is fallback) so that as soon as a Part type's realize lands, Conductor automatically picks the new path.

### 2a — Refactor dispatch

- [ ] **2.1** In `conductor.h`, locate the `perform(const Piece&)` body (around line 674-705). The current dispatch loop:

```cpp
for (const auto& part : piece.parts) {
    Instrument* inst = lookup_instrument(part.instrumentType);
    if (!inst) continue;

    // Event-list path: if the Part has direct events, play them
    if (!part.elementSequence.empty()) {     // post-Stage-1 name
        perform_events(part, bpm, beatOffset, inst);
    }

    // Compositional path: if the Part has a Passage for this Section
    auto it = part.passages.find(section.name);
    if (it != part.passages.end()) {
        perform_passage(it->second, scale, beatOffset, bpm, inst,
                        section.chordProgression, section.scale,
                        4, effectiveBeats);
    }
}
```

Change to exclusive dispatch:

```cpp
for (const auto& part : piece.parts) {
    Instrument* inst = lookup_instrument(part.instrumentType);
    if (!inst) continue;

    // Authoritative path: Composer-populated ElementSequence.
    if (!part.elementSequence.empty()) {
        perform_events(part, bpm, beatOffset, inst);
        continue;  // ← key change: exclusive
    }

    // Fallback during migration ONLY: walk the passage tree.
    // Removed at Stage 8 once every Part type has its realize step.
    auto it = part.passages.find(section.name);
    if (it != part.passages.end()) {
        perform_passage(it->second, scale, beatOffset, bpm, inst,
                        section.chordProgression, section.scale,
                        4, effectiveBeats);
    }
}
```

The added `continue` is the entire behavior change. Add a comment block above the loop noting the fallback is migration scaffolding scheduled for deletion at Stage 8.

### 2b — Verify bit-identical

- [ ] **2.2** Build:

```bash
cmake --build build --config Release --target mforce_cli
```

Expected: clean build.

- [ ] **2.3** Render all 4 K467 goldens + sha256sum + compare to baseline. Expected: all bit-identical.

  (Today no Part has BOTH paths populated. Audit assumption: chord parts use `add_chord` → events only, no passage. Algorithmic melody parts use AFS/period strategies → passage only, no events. If any patch breaks, we've found a Part with both populated; surface to user.)

- [ ] **2.4** STOP on any mismatch.

- [ ] **2.5** Commit:

```bash
git add engine/include/mforce/music/conductor.h
git commit -m "refactor(conductor): exclusive dispatch — elementSequence wins, tree-walk is migration fallback"
```

---

## Stage 3 — Merge chord-walker into main

**Files:** Many — see chord-walker branch tip. The merge brings:
- New: `engine/include/mforce/music/voicing_profile.h`, `voicing_profile_selector.h`, `voicing_selector.h`, `smooth_voicing_selector.h`, `static_voicing_profile_selector.h`, `random_voicing_profile_selector.h`, `drift_voicing_profile_selector.h`, `scripted_voicing_profile_selector.h`
- Modified: `engine/src/chord.cpp` (rule-native `init_pitches`), `engine/include/mforce/music/templates.h` (PassageTemplate gains `voicingSelector` field), `engine/include/mforce/music/templates_json.h` (read it)
- New patches: `patches/test_jazz_turnaround_flat.json`, `_p0.json`, `_p05.json`, `_p1.json`, `_random.json`, `_drift.json`, `_scripted.json` (and possibly `_smooth.json`, `_rock.json`)

**Bit-identical:** ✓ for K467 (chord-walker work doesn't touch its path); jazz turnaround demos are NEW goldens pinned this stage.

### 3a — Merge

- [ ] **3.1** Verify chord-walker branch has all stages 0-2 changes from main applied first. If main has diverged from chord-walker since last update:

```bash
git log --oneline main ^chord-walker | head
```

Expected: list of main commits NOT yet on chord-walker. Stages 0-2 will be in this list. Merging will need to incorporate them.

- [ ] **3.2** Create merge branch (preserves main + chord-walker history independently):

```bash
git checkout -b merge-chord-walker
git merge chord-walker
```

Expected outcomes:
- (a) Clean merge → proceed to 3.4.
- (b) Conflicts → likely in `templates.h` (PassageTemplate gained `voicingSelector` on chord-walker; main may have touched the same area) and possibly `structure.h` (Stage 1 renamed `Part.events`). Resolve by hand. Ask user before any non-mechanical resolution decision.

- [ ] **3.3** If conflicts resolved:

```bash
git add <resolved files>
git commit
```

- [ ] **3.4** Build:

```bash
cmake --build build --config Release --target mforce_cli
```

Expected: clean build. Errors are likely compile-time signature mismatches between chord-walker's voicing types and main's post-Stage-1 ElementSequence. Fix or surface to user.

### 3b — Re-verify K467 bit-identical after merge

- [ ] **3.5** Render K467 goldens and confirm hashes still match baseline:

```bash
for p in walker harmony period structural; do
    build/tools/mforce_cli/Release/mforce_cli.exe --compose patches/test_k467_${p}.json renders/stage3_k467_${p} 1
done
sha256sum renders/stage3_k467_*.wav
```

Expected: all match baseline. (chord-walker introduced VoicingSelector but didn't wire it into `realize_chord_parts_`; K467 still goes through `sc->resolve(...)`.)

### 3c — Pin jazz turnaround goldens

- [ ] **3.6** Render each jazz turnaround patch and add hash to baseline file:

```bash
for p in flat p0 p05 p1 random drift scripted; do
    build/tools/mforce_cli/Release/mforce_cli.exe --compose patches/test_jazz_turnaround_${p}.json renders/stage3_jazz_${p} 1
done
sha256sum renders/stage3_jazz_*.wav >> docs/superpowers/baselines/2026-04-22-composer-owns-events-baselines.txt
```

Expected: 7 (or however many demo patches landed) NEW hashes appended to baseline.

- [ ] **3.7** Fast-forward main to merge branch:

```bash
git checkout main
git merge --ff-only merge-chord-walker
```

If not fast-forwardable (some commit landed on main during the merge), surface to user.

- [ ] **3.8** Commit the baseline update:

```bash
git add docs/superpowers/baselines/2026-04-22-composer-owns-events-baselines.txt
git commit -m "chore(baselines): pin jazz turnaround goldens after chord-walker merge"
```

---

## Stage 4 — `RealizationStrategy` interface + registry

**Files:**
- Create: `engine/include/mforce/music/realization_strategy.h`
- Modify: `engine/include/mforce/music/templates.h` (PassageTemplate gains `realizationStrategy` field; introduce `RhythmPattern` struct)
- Modify: `engine/include/mforce/music/templates_json.h` (read/write the new fields)
- Modify: `engine/include/mforce/music/composer.h` (constructor registers default strategies)

**Bit-identical:** ✓ (interface exists but is not yet wired into compose path)

### 4a — Define interface

- [ ] **4.1** Create `engine/include/mforce/music/realization_strategy.h`:

```cpp
#pragma once
#include "mforce/music/basics.h"
#include "mforce/music/structure.h"
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace mforce {

// ---------------------------------------------------------------------------
// RhythmPattern — durations per bar (negative = rest), with per-bar overrides
// (lifted from the dissolving ChordAccompanimentConfig).
// ---------------------------------------------------------------------------
struct RhythmPattern {
    std::vector<float> defaultPattern{2.0f, 2.0f};
    struct BarOverride {
        std::vector<int> bars;
        std::vector<float> pattern;
    };
    std::vector<BarOverride> overrides;

    const std::vector<float>& pattern_for_bar(int bar1) const {
        for (const auto& ov : overrides) {
            for (int b : ov.bars) if (b == bar1) return ov.pattern;
        }
        return defaultPattern;
    }
};

// ---------------------------------------------------------------------------
// RealizationRequest — input to a RealizationStrategy.
// chord is already voiced (output of VoicingSelector or sc->resolve()).
// ---------------------------------------------------------------------------
struct RealizationRequest {
    Chord chord;
    float startBeat;
    float durationBeats;
    int barIndex;                              // 1-based bar within the section
    const RhythmPattern* rhythmPattern;        // optional; nullptr if N/A
    // Future fields (meter, dynamics-at-beat, etc.) added as needed.
};

// ---------------------------------------------------------------------------
// RealizationStrategy — emits zero or more Elements (typically Notes)
// starting at req.startBeat into the supplied output sequence.
// ---------------------------------------------------------------------------
struct RealizationStrategy {
    virtual ~RealizationStrategy() = default;
    virtual std::string name() const = 0;
    virtual void realize(const RealizationRequest& req,
                         ElementSequence& out) = 0;
};

// ---------------------------------------------------------------------------
// Registry — singleton, name-keyed.
// ---------------------------------------------------------------------------
class RealizationStrategyRegistry {
public:
    static RealizationStrategyRegistry& instance() {
        static RealizationStrategyRegistry reg;
        return reg;
    }
    void register_strategy(std::unique_ptr<RealizationStrategy> s) {
        strategies_[s->name()] = std::move(s);
    }
    RealizationStrategy* resolve(const std::string& name) const {
        auto it = strategies_.find(name);
        return it == strategies_.end() ? nullptr : it->second.get();
    }
private:
    std::unordered_map<std::string, std::unique_ptr<RealizationStrategy>> strategies_;
};

// ---------------------------------------------------------------------------
// BlockRealizationStrategy — emits the chord as a single Chord-event at
// startBeat for durationBeats. (Stage 4 starts as Chord-event for back-compat
// with ChordPerformer; Stage 9 will rewrite to emit per-tone Notes.)
// ---------------------------------------------------------------------------
class BlockRealizationStrategy : public RealizationStrategy {
public:
    std::string name() const override { return "block"; }
    void realize(const RealizationRequest& req, ElementSequence& out) override {
        Element e{req.startBeat, req.chord};
        out.add(e);
    }
};

// ---------------------------------------------------------------------------
// RhythmPatternRealizationStrategy — for each entry in the rhythm pattern,
// emits the chord at the corresponding beat with the entry's duration
// (negative entries become Rests).
// ---------------------------------------------------------------------------
class RhythmPatternRealizationStrategy : public RealizationStrategy {
public:
    std::string name() const override { return "rhythm_pattern"; }
    void realize(const RealizationRequest& req, ElementSequence& out) override {
        if (!req.rhythmPattern) {
            out.add({req.startBeat, req.chord});
            return;
        }
        const auto& pattern = req.rhythmPattern->pattern_for_bar(req.barIndex);
        float beat = req.startBeat;
        for (float dur : pattern) {
            if (dur < 0.0f) {
                out.add({beat, Rest{-dur}});
                beat += -dur;
            } else {
                Chord c = req.chord;
                c.dur = dur;
                out.add({beat, c});
                beat += dur;
            }
        }
    }
};

} // namespace mforce
```

### 4b — Register the two defaults at Composer construction

- [ ] **4.2** In `composer.h`, find the Composer constructor (around line 90-130 where strategies are registered). Add an include for `realization_strategy.h` and register:

```cpp
// At top with other includes:
#include "mforce/music/realization_strategy.h"

// In the constructor, after the existing strategy registrations:
auto& realRegistry = RealizationStrategyRegistry::instance();
realRegistry.register_strategy(std::make_unique<BlockRealizationStrategy>());
realRegistry.register_strategy(std::make_unique<RhythmPatternRealizationStrategy>());
```

### 4c — Add `realizationStrategy` field to PassageTemplate

- [ ] **4.3** In `templates.h`, in `PassageTemplate` (around line 336-365):

```cpp
struct PassageTemplate {
    // ... existing fields ...
    std::string voicingSelector;       // already added by chord-walker (Stage 3)
    std::string realizationStrategy;   // NEW — empty defaults to "block"
    std::optional<RhythmPattern> rhythmPattern;  // NEW — config for rhythm_pattern strategy
};
```

Add `#include "mforce/music/realization_strategy.h"` if needed.

### 4d — JSON read/write

- [ ] **4.4** In `templates_json.h`, find the `PassageTemplate` from_json (around the line where `voicingSelector` is read; chord-walker added it at Stage 3). Add:

```cpp
if (j.contains("realizationStrategy"))
    pt.realizationStrategy = j["realizationStrategy"].get<std::string>();
if (j.contains("rhythmPattern")) {
    RhythmPattern rp;
    const auto& jrp = j["rhythmPattern"];
    if (jrp.contains("defaultPattern")) {
        rp.defaultPattern.clear();
        for (auto& v : jrp["defaultPattern"]) rp.defaultPattern.push_back(v.get<float>());
    }
    if (jrp.contains("overrides")) {
        for (auto& ov : jrp["overrides"]) {
            RhythmPattern::BarOverride bo;
            for (auto& b : ov["bars"]) bo.bars.push_back(b.get<int>());
            for (auto& v : ov["pattern"]) bo.pattern.push_back(v.get<float>());
            rp.overrides.push_back(bo);
        }
    }
    pt.rhythmPattern = rp;
}
```

In the corresponding `to_json` for `PassageTemplate`:

```cpp
if (!pt.realizationStrategy.empty())
    j["realizationStrategy"] = pt.realizationStrategy;
if (pt.rhythmPattern) {
    json jrp;
    jrp["defaultPattern"] = pt.rhythmPattern->defaultPattern;
    if (!pt.rhythmPattern->overrides.empty()) {
        json jOvs = json::array();
        for (const auto& ov : pt.rhythmPattern->overrides) {
            json jOv;
            jOv["bars"] = ov.bars;
            jOv["pattern"] = ov.pattern;
            jOvs.push_back(jOv);
        }
        jrp["overrides"] = jOvs;
    }
    j["rhythmPattern"] = jrp;
}
```

- [ ] **4.5** Build:

```bash
cmake --build build --config Release --target mforce_cli
```

Expected: clean build.

### 4e — Verify bit-identical

- [ ] **4.6** Render all 11 goldens (4 K467 + 7 jazz turnaround) and compare hashes:

```bash
for p in walker harmony period structural; do
    build/tools/mforce_cli/Release/mforce_cli.exe --compose patches/test_k467_${p}.json renders/stage4_k467_${p} 1
done
for p in flat p0 p05 p1 random drift scripted; do
    build/tools/mforce_cli/Release/mforce_cli.exe --compose patches/test_jazz_turnaround_${p}.json renders/stage4_jazz_${p} 1
done
sha256sum renders/stage4_k467_*.wav renders/stage4_jazz_*.wav
```

Expected: all 11 match the corresponding entries in `docs/superpowers/baselines/2026-04-22-composer-owns-events-baselines.txt`.

- [ ] **4.7** STOP on any mismatch.

- [ ] **4.8** Commit:

```bash
git add engine/include/mforce/music/realization_strategy.h engine/include/mforce/music/composer.h engine/include/mforce/music/templates.h engine/include/mforce/music/templates_json.h
git commit -m "feat(realization): RealizationStrategy interface + Block/RhythmPattern defaults"
```

---

## Stage 5 — Realize melody Parts (MelodicFigure)

**Files:**
- Create: `engine/include/mforce/music/dynamic_state.h` (extracted from conductor.h)
- Create: `engine/include/mforce/music/pitch_walker.h` (extracted from conductor.h)
- Modify: `engine/include/mforce/music/conductor.h` (replace inline DynamicState + step_note with includes)
- Modify: `engine/include/mforce/music/composer.h` (add realize sub-step at end of compose())

**Bit-identical:** ✓

**What this stage does:** Composer's `compose()` gains a final sub-step that walks each melody Part's Passage tree and emits realized Notes into `Part.elementSequence`. Stage 2's exclusive dispatch then automatically routes those Parts through `perform_events`, skipping the tree-walk fallback. The bit-identity check at the end verifies that the realize step produced the same Notes as the tree-walk would have.

### 5a — Extract DynamicState to its own header

- [ ] **5.1** Read `conductor.h:125-160` (DynamicState struct + `velocity_at`). Create `engine/include/mforce/music/dynamic_state.h`:

```cpp
// engine/include/mforce/music/dynamic_state.h
#pragma once
#include "mforce/music/structure.h"
#include "mforce/music/rng.h"

namespace mforce {

// (Move the full DynamicState struct verbatim from conductor.h here,
//  preserving constructors, members, methods.)

} // namespace mforce
```

In `conductor.h`, replace the inline struct with `#include "mforce/music/dynamic_state.h"`.

### 5b — Extract pitch-walker helpers to their own header

- [ ] **5.2** Locate `step_note` and `step_chord_tone` in `conductor.h` (grep for them). They're currently methods or free functions in conductor's namespace. Move to `engine/include/mforce/music/pitch_walker.h`:

```cpp
// engine/include/mforce/music/pitch_walker.h
#pragma once
#include "mforce/music/basics.h"
#include "mforce/music/structure.h"
#include <vector>

namespace mforce {

// Step `currentNN` by `step` scale degrees in the given Scale.
inline float step_note(float currentNN, int step, const Scale& scale) {
    // (move current implementation verbatim)
}

// Step `currentNN` to nearest chord-tone, then `step` chord-tones within
// the resolved chord's pitch list.
inline float step_chord_tone(float currentNN, int step,
                             const std::vector<Pitch>& chordTones) {
    // (move current implementation verbatim)
}

} // namespace mforce
```

In `conductor.h`, replace the inline definitions with `#include "mforce/music/pitch_walker.h"` and use the free functions.

- [ ] **5.3** Build to verify the moves are clean:

```bash
cmake --build build --config Release --target mforce_cli
```

Expected: clean build, no behavior change.

### 5c — Render once to confirm extractions are still bit-identical

- [ ] **5.4** Render all 11 goldens + sha256sum + compare. Expected: all bit-identical (we just moved code, didn't change it).

  STOP on any mismatch.

### 5d — Add realize sub-step skeleton to Composer

- [ ] **5.5** In `composer.h`, after the existing `compose()` body completes (after `realize_chord_parts_(piece, tmpl);` at line 146), add a new call:

```cpp
realize_event_sequences_(piece, tmpl);
```

- [ ] **5.6** Define `realize_event_sequences_` and helpers as private methods on Composer. **Stage 5 implements the MelodicFigure path only** — Stages 6 and 7 will extend this same method to handle ChordFigure and Chord-events.

```cpp
void realize_event_sequences_(Piece& piece, const PieceTemplate& tmpl) {
    for (auto& part : piece.parts) {
        for (const auto& sec : piece.sections) {
            auto it = part.passages.find(sec.name);
            if (it == part.passages.end()) continue;
            const Passage& passage = it->second;
            float passageStartBeat = section_start_beat_(piece, sec.name);
            realize_passage_to_events_(part, passage, sec, passageStartBeat);
        }
    }
}

float section_start_beat_(const Piece& piece, const std::string& sectionName) const {
    float beat = 0.0f;
    for (const auto& s : piece.sections) {
        if (s.name == sectionName) return beat;
        beat += s.beats;
    }
    return 0.0f;
}

void realize_passage_to_events_(Part& part, const Passage& passage,
                                const Section& section, float passageStartBeat) {
    // Stage 5: MelodicFigure only. Stages 6 and 7 add ChordFigure and chord-events.

    DynamicState dyn(passage.dynamicMarkings);
    float currentNN = -1.0f;
    bool haveStartingPitch = false;
    float currentBeat = passageStartBeat;
    const float effectiveBeats = section.beats - section.truncateTailBeats;
    const float passageEndBeat = passageStartBeat + std::max(0.0f, effectiveBeats);

    for (const auto& phrase : passage.phrases) {
        if (!haveStartingPitch) {
            currentNN = phrase.startingPitch.note_number();
            haveStartingPitch = true;
        }
        for (const auto& figPtr : phrase.figures) {
            auto* mel = dynamic_cast<const MelodicFigure*>(figPtr.get());
            if (!mel) continue;  // chord figure: handled at Stage 6
            for (const auto& unit : mel->units) {
                if (currentBeat >= passageEndBeat) return;  // truncation
                currentNN = step_note(currentNN, unit.step, section.scale);
                float soundNN = currentNN + float(unit.accidental);
                float vel = dyn.velocity_at(currentBeat - passageStartBeat);
                Note n;
                n.noteNumber = soundNN;
                n.velocity = vel;
                n.durationBeats = unit.duration;
                part.elementSequence.add({currentBeat, n});
                currentBeat += unit.duration;
            }
        }
    }
}
```

**Important**: cross-check exact behavior of `Conductor::perform_phrase` (`conductor.h:785-860+`) — articulation/ornament transfer, accidental handling, beat tracking. The realize step must produce the same Notes (same pitch, velocity, duration, articulation) Conductor would have produced, OR bit-identity fails at 5.7.

If Conductor's `perform_phrase` does anything that doesn't fit cleanly in "emit a Note" (e.g. starts a long-running ornament expansion that consumes multiple figure-units), surface to user — that may need to stay in the perform tier and be re-thought.

### 5e — Verify bit-identical

- [ ] **5.7** Build:

```bash
cmake --build build --config Release --target mforce_cli
```

- [ ] **5.8** Render all 11 goldens + sha256sum + compare.

  Expected: all bit-identical. Now that melody Parts have non-empty elementSequence, Stage 2's exclusive dispatch routes them through `perform_events` (NotePerformer reads the realized Notes) instead of `perform_passage` (which would have walked the tree and computed pitches inline).

  Most likely failure modes:
  - Notes don't match what tree-walk would have produced (different pitches due to a step_note difference, different velocities due to DynamicState lookup at different beats, etc.).
  - Articulation/ornament fields on Note aren't being populated by the realize step.
  - The `Section::truncateTailBeats` boundary is hit at a different beat than tree-walk hit it.

  STOP on any mismatch. Diagnose with `cmp -l`, then look for the divergence in Note construction.

- [ ] **5.9** Commit:

```bash
git add engine/include/mforce/music/dynamic_state.h engine/include/mforce/music/pitch_walker.h engine/include/mforce/music/conductor.h engine/include/mforce/music/composer.h
git commit -m "feat(composer): realize MelodicFigure into Part.elementSequence"
```

---

## Stage 6 — Realize chord-figure Parts (ChordFigure)

**Files:**
- Modify: `engine/include/mforce/music/composer.h` (extend `realize_passage_to_events_` to handle ChordFigure)

**Bit-identical:** ✓

**What this stage does:** Extends Stage 5's realize sub-step to also walk ChordFigure units and emit Notes via `step_chord_tone` against the active chord. Same atomic-swap mechanic via Stage 2's exclusive dispatch.

### 6a — Add ChordFigure branch

- [ ] **6.1** In `realize_passage_to_events_`, where Stage 5 added `if (!mel) continue;`, replace with discrimination plus a chord-tone branch:

```cpp
const auto* mel = dynamic_cast<const MelodicFigure*>(figPtr.get());
const auto* chf = dynamic_cast<const ChordFigure*>(figPtr.get());
if (!mel && !chf) continue;

if (mel) {
    // (existing Stage 5 melodic-step body)
    for (const auto& unit : mel->units) {
        // ... step_note path ...
    }
} else {
    // ChordFigure: walk chord tones from active chord at this beat.
    for (const auto& unit : chf->units) {
        if (currentBeat >= passageEndBeat) return;  // truncation
        const float sectionBeat = currentBeat - passageStartBeat;
        Chord active = find_active_chord_(section, sectionBeat);
        currentNN = step_chord_tone(currentNN, unit.step, active.pitches);
        float soundNN = currentNN + float(unit.accidental);
        float vel = dyn.velocity_at(sectionBeat);
        Note n;
        n.noteNumber = soundNN;
        n.velocity = vel;
        n.durationBeats = unit.duration;
        part.elementSequence.add({currentBeat, n});
        currentBeat += unit.duration;
    }
}
```

- [ ] **6.2** Add `find_active_chord_(section, sectionBeat)` helper (mirrors `conductor.h:817-827`):

```cpp
Chord find_active_chord_(const Section& sec, float sectionBeat) const {
    if (!sec.chordProgression) return Chord{};
    const auto& prog = *sec.chordProgression;
    float chordBeat = 0.0f;
    int chordIdx = 0;
    for (int ci = 0; ci < prog.count(); ++ci) {
        if (chordBeat + prog.pulses.get(ci) > sectionBeat) {
            chordIdx = ci;
            break;
        }
        chordBeat += prog.pulses.get(ci);
        chordIdx = ci;
    }
    return prog.chords.get(chordIdx).resolve(sec.scale, /*octave=*/4);
}
```

(Cross-check the exact octave handling in `conductor.h:828`: `chordProg->chords.get(chordIdx).resolve(sectionScale, baseOctave);`. Use the same `baseOctave` value Conductor passes — likely a parameter to `perform_phrase`. Trace from `perform_passage` how it's set; the dispatch in `perform()` passes literal `4`.)

### 6b — Verify bit-identical

- [ ] **6.3** Build + render all 11 goldens + sha256sum + compare. Expected: bit-identical (chord-figure Parts now go through realize → perform_events; melody Parts continue to work via Stage 5 changes).

- [ ] **6.4** STOP on any mismatch.

- [ ] **6.5** Commit:

```bash
git add engine/include/mforce/music/composer.h
git commit -m "feat(composer): realize ChordFigure into Part.elementSequence"
```

---

## Stage 7 — Realize chord-event Parts (replace `add_chord` path)

**Files:**
- Modify: `engine/include/mforce/music/composer.h`:
  - `realize_chord_parts_` (line 266-313) — STOP populating elementSequence with `Chord` events via `add_chord`; instead route through VoicingSelector + RealizationStrategy and emit via the new realize step

**Bit-identical:** ✓

**What this stage does:** Today `realize_chord_parts_` calls `add_chord(pos, chord)` which puts `Chord`-typed Elements into `elementSequence`; Conductor's ChordPerformer then expands them at perform time. After this stage, `realize_chord_parts_` instead calls VoicingSelector to pick a voicing AND RealizationStrategy to emit pre-expanded content (Chord events still, until Stage 9 swaps to per-tone Notes). Conductor's path is unchanged — it still reads `elementSequence` via `perform_events` and uses ChordPerformer to play `Chord` Elements.

This is the trickiest bit-identical check because we're replacing the populator without changing the consumer (Conductor still uses ChordPerformer via perform_events for Chord-typed Elements). Bit-identity holds IF the Realize step + BlockRealizationStrategy emit identically to what `add_chord(pos, chord)` did.

### 7a — Replace `add_chord` calls in `realize_chord_parts_`

- [ ] **7.1** Read `composer.h:266-313` (`realize_chord_parts_`). Locate the existing iteration that calls `sc->resolve(...)` and `part->add_chord(pos, chord)` (around line 303-305).

- [ ] **7.2** Rewrite the per-chord emit. For each `(ScaleChord, beat, duration, cfg)` iteration:

```cpp
// Before (composer.h:303-305 area):
Chord chord = sc->resolve(sec.scale, cfg.octave, dur, cfg.inversion, cfg.spread);
part->add_chord(pos, chord);

// After:
Chord voiced = resolve_voicing_(sc, &sec.scale, cfg, /*previous=*/lastChord, dur, passTmpl);
RealizationRequest realReq{
    /*chord=*/voiced,
    /*startBeat=*/pos,
    /*durationBeats=*/dur,
    /*barIndex=*/compute_bar_index_(pos, sec.meter),
    /*rhythmPattern=*/(passTmpl && passTmpl->rhythmPattern) ? &*passTmpl->rhythmPattern : nullptr
};
RealizationStrategy* strat = pick_realization_strategy_(passTmpl);
strat->realize(realReq, part->elementSequence);
lastChord = voiced;
```

Plus helpers:

```cpp
Chord resolve_voicing_(const ScaleChord* sc, const Scale* scale,
                       const ChordAccompanimentConfig& cfg,
                       const Chord* previous, float dur,
                       const PassageTemplate* passTmpl) {
    if (passTmpl && !passTmpl->voicingSelector.empty()) {
        VoicingSelector* selector = VoicingSelectorRegistry::instance()
            .resolve(passTmpl->voicingSelector);
        if (selector) {
            VoicingRequest req{*sc, scale, cfg.octave, dur, previous, std::nullopt};
            return selector->select(req);
        }
    }
    return sc->resolve(*scale, cfg.octave, dur, cfg.inversion, cfg.spread);
}

RealizationStrategy* pick_realization_strategy_(const PassageTemplate* passTmpl) {
    auto& reg = RealizationStrategyRegistry::instance();
    std::string name = (passTmpl && !passTmpl->realizationStrategy.empty())
        ? passTmpl->realizationStrategy : "block";
    RealizationStrategy* strat = reg.resolve(name);
    return strat ? strat : reg.resolve("block");
}

int compute_bar_index_(float beatInSection, Meter meter) {
    float beatsPerBar = beats_per_bar(meter);
    return int(beatInSection / beatsPerBar) + 1;
}
```

(Verify `beats_per_bar(meter)` exists; otherwise inline the standard bars-per-meter lookup.)

### 7b — Verify bit-identical

- [ ] **7.3** Build:

```bash
cmake --build build --config Release --target mforce_cli
```

- [ ] **7.4** Render all 11 goldens + sha256sum + compare.

  Expected: all bit-identical. Reasoning:
  - K467 patches don't currently set `voicingSelector`, `realizationStrategy`, or `rhythmPattern` → use the `sc->resolve(...)` fallback in `resolve_voicing_` AND default to `BlockRealizationStrategy` → emits a single `Chord` Element with the same data `add_chord` would have. ChordPerformer plays it identically. Bit-identical.
  - **But**: `test_k467_walker.json` DOES use `chordConfig` (has rhythm pattern). Today `realize_chord_parts_` reads `cfg.defaultPattern` from `ChordAccompanimentConfig` and emits one `Chord` event per pattern entry. After this stage, `chordConfig` still exists as a field (we haven't touched it yet), but the new code reads `passTmpl->rhythmPattern` (the new field added at Stage 4), which is empty for unmigrated patches. So `RhythmPatternRealizationStrategy` won't trigger; the `Block` default emits a single Chord per call.
  - That means the `chordConfig`-driven rhythm pattern emit (one Chord per pattern entry) is currently produced via `realize_chord_parts_`'s pattern-loop logic. Read `realize_chord_parts_` carefully to see exactly how it handles `cfg.defaultPattern`. If it emits multiple `add_chord` calls per chord (one per pattern entry), our new code needs to preserve THAT loop and emit one RealizationRequest per pattern entry — OR funnel the rhythm pattern through the strategy via `RealizationRequest.rhythmPattern`.

  **At execution time, before 7.4**: trace `realize_chord_parts_`'s current pattern handling. If it loops and add_chords multiple times per chord, the new code needs the same loop (one RealizationRequest per emit, not one per chord). Adjust.

- [ ] **7.5** STOP on any mismatch. Most likely cause: rhythm pattern loop semantics drift between old and new code. Fix and re-verify.

- [ ] **7.6** Commit:

```bash
git add engine/include/mforce/music/composer.h
git commit -m "feat(composer): replace add_chord with VoicingSelector + RealizationStrategy"
```

---

## Stage 8 — Delete tree-walk path entirely

**Files:**
- Modify: `engine/include/mforce/music/conductor.h`:
  - Delete the tree-walk fallback branch in `perform()` dispatch
  - Delete `Conductor::perform_passage` (line 751-end-of-method)
  - Delete `Conductor::perform_phrase` (line 785-end-of-method)
  - Delete `ChordPerformer` struct entirely (line 436 — end of struct)
  - Delete `Conductor::register_josie_figures` (around line 462)
  - Delete the `dynamic_cast<const ChordFigure*>` discriminator (lives inside `perform_phrase`, dies with it)
- Modify: `engine/include/mforce/music/figures.h` (decide on `ChordArticulation`)

**Bit-identical:** ✓ (the deleted code is unused after Stages 5-7)

### 8a — Confirm dead code

- [ ] **8.1** Confirm no remaining callers of the deletion targets:

```bash
grep -nrE "perform_passage|perform_phrase" engine tools | grep -v "third_party"
grep -nrE "ChordPerformer\b|register_josie_figures" engine tools | grep -v "third_party"
```

Expected: zero hits outside of `conductor.h` itself. If any external caller exists, surface to user before deleting.

### 8b — Delete fallback branch + tree-walk methods + ChordPerformer

- [ ] **8.2** In `conductor.h`'s `perform(const Piece&)` dispatch (the loop modified at Stage 2), delete the tree-walk fallback:

```cpp
// Before (Stage 2 + 5-7 state):
for (const auto& part : piece.parts) {
    Instrument* inst = lookup_instrument(part.instrumentType);
    if (!inst) continue;

    if (!part.elementSequence.empty()) {
        perform_events(part, bpm, beatOffset, inst);
        continue;
    }

    // Fallback during migration ONLY: walk the passage tree.
    auto it = part.passages.find(section.name);
    if (it != part.passages.end()) {
        perform_passage(it->second, scale, beatOffset, bpm, inst, ...);
    }
}

// After Stage 8:
for (const auto& part : piece.parts) {
    Instrument* inst = lookup_instrument(part.instrumentType);
    if (!inst) continue;
    if (!part.elementSequence.empty()) {
        perform_events(part, bpm, beatOffset, inst);
    }
}
```

- [ ] **8.3** Delete the methods/structs/functions listed above:
  - `perform_passage` (line 751-end)
  - `perform_phrase` (line 785-end)
  - `ChordPerformer` struct (line 436-end-of-struct)
  - `register_josie_figures` (line 462)
  - Any `dynamic_cast<const ChordFigure*>` references that die with `perform_phrase`

### 8c — Decide on `ChordArticulation`

- [ ] **8.4** After 8.3's deletions, grep for remaining `ChordArticulation` consumers:

```bash
grep -nrE "ChordArticulation" engine tools | grep -v "third_party"
```

- If zero hits: delete the `ChordArticulation` struct from `figures.h:627`.
- If one or more hits: rename to `ChordRealization` (mechanical: rename the struct, all members, and all usages). Build clean. Commit separately:

```bash
git commit -m "refactor(figures): rename ChordArticulation -> ChordRealization"
```

### 8d — Verify

- [ ] **8.5** Build:

```bash
cmake --build build --config Release --target mforce_cli
```

Expected: clean build. Composer's tree (`Part.passages`) is now never read by Conductor; it's purely Composer-internal scratch.

- [ ] **8.6** Render all 11 goldens + sha256sum + compare. Expected: bit-identical.

- [ ] **8.7** Spot-check one Sound-tier patch (e.g. `fhn_pure_tone`):

```bash
build/tools/mforce_cli/Release/mforce_cli.exe --patch patches/fhn_pure_tone.json renders/stage8_sound_fhn 1
sha256sum renders/stage8_sound_fhn_1.wav
```

Compare to baseline. Expected: bit-identical (Sound tier untouched).

- [ ] **8.8** Commit:

```bash
git add engine/include/mforce/music/conductor.h engine/include/mforce/music/figures.h
git commit -m "refactor(conductor): delete tree-walk path, ChordPerformer, Josie library"
```

---

## Stage 9 — Drop `Chord` from `Element` variant; emit Notes from realize

**Files:**
- Modify: `engine/include/mforce/music/structure.h` (Element variant)
- Modify: `engine/include/mforce/music/realization_strategy.h` (Block + RhythmPattern emit per-tone Notes)
- Modify: `engine/include/mforce/music/composer.h` (any `Part::add_chord` callers post-Stage-7 — should be none)

**Bit-identical:** ✓

**What this stage does:** Today `BlockRealizationStrategy` and `RhythmPatternRealizationStrategy` both emit `Chord`-typed Elements into elementSequence; Conductor's `perform_events` sees these and... wait — ChordPerformer was deleted at Stage 8. So who's playing the Chord Elements after Stage 8?

The answer: by Stage 8, Conductor's `perform_events` must already be expanding Chord Elements to per-tone Notes inline (since ChordPerformer is gone). EITHER:
- (a) `perform_events` was already doing per-tone playback for Chord Elements all along (some inline expansion separate from ChordPerformer). Verify by reading `perform_events` (`conductor.h:725+`).
- (b) Stage 8 must have included a substep where `perform_events` learns to expand Chord Elements to Notes itself OR where the realize strategies are switched to emit Notes directly. The plan as written defers this to Stage 9. Need to fix the plan if (a) is false.

**Pre-execution check at Stage 8**: read `Conductor::perform_events` carefully. If it routes `Chord` Elements to ChordPerformer (which is being deleted at Stage 8), the deletion is incomplete — Stage 8 must ALSO refactor `perform_events` to either drop the Chord-Element handling (if it's about to disappear at Stage 9) OR inline-expand the chord to per-tone notes.

The cleanest reordering: do the Stage 9 work AS PART of Stage 8. Specifically:
- **Stage 8** deletes ChordPerformer
- **Stage 8** changes BlockRealizationStrategy and RhythmPatternRealizationStrategy to emit per-tone Notes (not Chord Elements) — what Stage 9 was doing
- **Stage 8** drops Chord from Element variant — what Stage 9 was doing
- **Stage 9** then becomes empty; can be removed

If you (executor) discover at Stage 8 that ChordPerformer is the sole consumer of Chord Elements in `perform_events`, surface to user with the merge proposal.

### 9a — If Stage 9 still has work after the Stage 8 reordering

- [ ] **9.1** Refactor `BlockRealizationStrategy::realize` to emit per-tone Notes:

```cpp
void realize(const RealizationRequest& req, ElementSequence& out) override {
    for (const auto& pitch : req.chord.pitches) {
        Note n;
        n.noteNumber = pitch.note_number();
        n.velocity = 1.0f;
        n.durationBeats = req.durationBeats;
        out.add({req.startBeat, n});
    }
}
```

Same change for `RhythmPatternRealizationStrategy`: where it emits `out.add({beat, c})` (a Chord), replace with a per-pitch loop.

- [ ] **9.2** Migrate any direct-build `Part::add_chord(...)` callers (grep first):

```bash
grep -nrE "\.add_chord\(" engine tools | grep -v "third_party"
```

For each remaining caller, construct a `BlockRealizationStrategy` ad-hoc (or call a Composer helper `emit_chord_as_notes(part, beat, chord)`) and emit Notes instead.

- [ ] **9.3** Drop `Chord` from `Element` variant in `structure.h`:

```cpp
// Before:
std::variant<Note, Chord, Hit, Rest> content;
// After:
std::variant<Note, Hit, Rest> content;
```

Drop the `is_chord` / `chord()` accessors. Drop or refactor `Part::add_chord(...)`. Drop the `e.is_chord()` branch in `ElementSequence::add`'s totalBeats calculation.

### 9b — Verify

- [ ] **9.4** Build + render all 11 goldens + sha256sum + compare.

  Expected: ALL bit-identical. The audio doesn't care whether the WAV came from "Chord Element expanded to N notes by ChordPerformer at perform time" vs "N Notes emitted at compose time"; same notes at same beats with same velocities → same audio.

  If a hash differs, most likely the velocity defaults differ. Check that `BlockRealizationStrategy` per-tone emit uses the same velocity ChordPerformer was using.

- [ ] **9.5** STOP on any mismatch.

- [ ] **9.6** Commit:

```bash
git add engine/include/mforce/music/structure.h engine/include/mforce/music/realization_strategy.h engine/include/mforce/music/composer.h
git commit -m "refactor(element): drop Chord from variant; realize emits per-tone Notes"
```

---

## Stage 10 — Migrate `test_k467_walker.json`

**Files:**
- Modify: `patches/test_k467_walker.json` (replace `chordConfig` with new fields)

**Bit-identical:** ✓ if migration faithful; new golden if not.

### 10a — Migrate

- [ ] **10.1** Read `patches/test_k467_walker.json`. Find the `chordConfig` block(s) on PassageTemplate(s).

- [ ] **10.2** For each `chordConfig`, replace with explicit fields per spec section 8:

```jsonc
// Before (PassageTemplate fragment):
{
  "chordConfig": {
    "defaultPattern": [<rhythm>],
    "octave": <N>,
    "inversion": 0,
    "spread": 0
  }
}

// After:
{
  "voicingSelector": "smooth",
  "voicingProfile": { "allowedInversions": [0], "allowedSpreads": [0], "priority": 0.0 },
  "realizationStrategy": "rhythm_pattern",
  "rhythmPattern": {
    "defaultPattern": [<rhythm>]
  },
  "rootOctave": <N>
}
```

Translate verbatim — `defaultPattern` lifts as-is; the (probably default-zero) `inversion`/`spread`/`priority` map to single-element allow-lists.

- [ ] **10.3** Render `test_k467_walker` and compare to baseline:

```bash
build/tools/mforce_cli/Release/mforce_cli.exe --compose patches/test_k467_walker.json renders/stage10_k467_walker 1
sha256sum renders/stage10_k467_walker_1.wav
```

Compare to the baseline-pinned hash. Expected: bit-identical. If not, listen to both renders, decide whether the migration was faithful or whether an inversion shifted, etc. If decided to accept new render, update the baseline file and pin a new golden.

- [ ] **10.4** Render all other 10 goldens + sha256sum + compare. Expected: bit-identical to baseline (untouched patches).

- [ ] **10.5** With the migration done, the legacy `sc->resolve(...)` fallback path in `resolve_voicing_` (Stage 7) is now reached only by patches with no voicingSelector/realizationStrategy. K467's other patches (harmony/period/structural) still use it; jazz turnaround patches use VoicingSelector. Leave the fallback in place — it's still serving real patches. (This contradicts the Stage 7 spec note about removing the fallback at Stage 10; **the spec note was wrong** — the fallback stays because not all patches opt in.)

- [ ] **10.6** Commit:

```bash
git add patches/test_k467_walker.json
git commit -m "feat(k467): migrate walker patch from chordConfig to selector+strategy"
```

If golden was re-pinned, add baselines file and amend message.

---

## Stage 11 — Delete `ChordAccompanimentConfig`

**Files:**
- Modify: `engine/include/mforce/music/templates.h` (delete struct)
- Modify: `engine/include/mforce/music/templates_json.h` (delete from_json + any callers)
- Modify: `engine/include/mforce/music/composer.h` (the `realize_chord_parts_` body uses `cfg` from ChordAccompanimentConfig; refactor to read directly from PassageTemplate's `voicingSelector` / `rhythmPattern` fields)

**Bit-identical:** ✓

### 11a — Confirm no consumers

- [ ] **11.1** Grep:

```bash
grep -nrE "ChordAccompanimentConfig\b|chordConfig" engine tools patches | grep -v "third_party" | grep -v "\.json"
```

Expected: hits in `composer.h` (`realize_chord_parts_` reads `passTmpl->chordConfig`) and `templates.h`/`templates_json.h`. No other code paths.

If any patch besides `test_k467_walker.json` (which migrated at Stage 10) still has `chordConfig` blocks, audit and migrate them now.

### 11b — Refactor `realize_chord_parts_` to drop `cfg` dependency

- [ ] **11.2** In `composer.h`, find every `cfg.octave`, `cfg.inversion`, `cfg.spread`, `cfg.defaultPattern`, `cfg.overrides` reference. Replace each:
- `cfg.octave` → read from PassageTemplate's `rootOctave` (add this field if it doesn't exist; per Stage 10 migration JSON it was implied to be on PassageTemplate)
- `cfg.inversion`, `cfg.spread` → already absorbed into VoicingProfile (chord-walker work). The fallback path's `sc->resolve(...)` call must still pass numeric inversion/spread; default to 0/0 if nothing else specified.
- `cfg.defaultPattern`, `cfg.overrides` → already absorbed into `passTmpl->rhythmPattern`. The new realize path consumes via `RealizationRequest`.

If `rootOctave` doesn't exist on PassageTemplate, add it (with JSON read at templates_json.h matching the Stage 10 migration).

### 11c — Delete the struct

- [ ] **11.3** In `templates.h`, delete the `ChordAccompanimentConfig` struct (around line 311-330) and its `BarOverride` nested type.

- [ ] **11.4** In `PassageTemplate`, delete the `std::optional<ChordAccompanimentConfig> chordConfig` field.

- [ ] **11.5** In `templates_json.h`, delete the `from_json(const json&, ChordAccompanimentConfig&)` function and the corresponding `if (j.contains("chordConfig"))` block in PassageTemplate's from_json.

### 11d — Verify

- [ ] **11.6** Build + render all 11 goldens + sha256sum + compare. Expected: bit-identical.

- [ ] **11.7** Commit:

```bash
git add engine/include/mforce/music/templates.h engine/include/mforce/music/templates_json.h engine/include/mforce/music/composer.h
git commit -m "refactor(templates): delete ChordAccompanimentConfig (subsumed by RhythmPattern + VoicingProfile)"
```

---

## Stage 12 — Audit residual Phrase / Passage tree exposure

**Files:**
- Possibly: `engine/include/mforce/music/structure.h` (move tree types to composer-internal location)
- Possibly: `engine/include/mforce/music/composer.h` (consume tree internally only)

**Bit-identical:** ✓

### 12a — Find consumers

- [ ] **12.1** Grep for all consumers of `Phrase`, `Passage`, `MelodicFigure`, `ChordFigure`, `FigureUnit` outside Composer:

```bash
grep -nrE "\b(Phrase|Passage|MelodicFigure|ChordFigure|FigureUnit)\b" engine tools | grep -v "third_party" | grep -v "music/composer\." | grep -v "music/strategy" | grep -v "music/figures\."
```

Expected: only Composer + strategy headers + figures.h itself reference these types. If Conductor still references any (it shouldn't after Stage 8), surface.

### 12b — Mark as Composer-internal

- [ ] **12.2** Two options based on findings:
  - **(a) Light touch**: add a comment block on `Phrase`/`Passage` definitions in structure.h saying "Composer-internal; not consumed by Conductor or instrument-side code."
  - **(b) Move**: physically move the type definitions to `engine/include/mforce/music/composer_internal.h` (new), include from composer.h and strategy headers only. structure.h no longer pulls them in.

  Recommendation: (a) for now. (b) is invasive and the value is mostly cosmetic.

- [ ] **12.3** Commit:

```bash
git add engine/include/mforce/music/structure.h
git commit -m "docs(structure): mark Phrase/Passage/Figure as Composer-internal"
```

---

## Final verification

- [ ] **Final-1** Render every patch in `patches/` (golden + non-golden) and compare to baselines where applicable. Surface anything unexpected.

- [ ] **Final-2** Run the non-K467 sound-tier patches sampled in Stage 0 (`fhn_pure_tone`, `Additive1`, etc.) to confirm Sound tier is untouched.

- [ ] **Final-3** Update memory:
  - Add a new project memory entry: "Composer owns the ElementSequence" — refactor landed; Conductor narrowed; ChordAccompanimentConfig dissolved; voicing-selector merged.
  - Mark the older project memories that referred to ChordAccompanimentConfig, the Chord-as-event variant, and the perform-time pitch resolution as outdated; either delete or update.

- [ ] **Final-4** If a worktree was used, follow the standard finishing-a-development-branch flow (PR or merge to main).

---

## Self-review notes

This plan covers all 12 stages from the spec, with the corrected staging:
- **Stage 2 added** (exclusive dispatch) — necessary because the original additive dispatch made the "in parallel" framing of Stages 4-6 incoherent. Each substantive stage is now an atomic per-Part-type swap from tree-walk-fallback to events-driven.
- **Stage 5 (was 4)**: realize MelodicFigure → atomic swap via dispatch.
- **Stage 6 (was 5)**: realize ChordFigure → atomic swap via dispatch.
- **Stage 7 (was 6)**: realize chord-events by replacing add_chord — now it's a swap, not a parallel population.
- **Stage 8 (was 7+8)**: delete the now-unreachable tree-walk fallback path AND the now-dead Conductor methods (perform_passage, perform_phrase, ChordPerformer).
- **Stage 9** flagged: Stage 8 may need to absorb Stage 9's per-tone-Notes refactor if `perform_events`'s Chord-handling depends on ChordPerformer. Surface to user at execution time.

Spot-checks against the spec:
- Section 1 (Data model changes) → Stages 1, 9.
- Section 2 (Composer pipeline) → Stages 5, 6, 7.
- Section 3 (chord-walker merge) → Stage 3.
- Section 4 (RealizationStrategy registry) → Stage 4.
- Section 5 (realize-to-events) → Stages 5, 6, 7.
- Section 6 (Conductor switch-and-narrow) → Stages 2 (dispatch swap), 8 (deletion).
- Section 7 (Drop Chord from Element variant) → Stage 9 (possibly absorbed into 8).
- Section 8 (Patch migration) → Stage 10.
- Section 9 (Final cleanup) → Stages 11, 12.
- Migration plan table → reproduced in stage flow.

Open at execution time (cannot be answered without touching the code):
- Exact line numbers for every "modify X.h:N-M" reference. The plan uses spec-time line numbers; verify with `grep` at execution time as files evolve through the chain.
- DrumPerformer fate (deferred per spec; revisit after Stage 8).
- Whether `StepMode::ChordTone` field (orphaned) gets a consumer in the realize step at Stages 5-6 or stays orphaned. Deferred decision; if it stays orphaned, delete in Stage 11 cleanup.
- Whether Stage 8 must absorb Stage 9's per-tone-Notes refactor. Determined by reading Conductor::perform_events carefully at Stage 8 execution.
