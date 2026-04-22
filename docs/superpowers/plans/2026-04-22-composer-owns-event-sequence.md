# Composer Owns the ElementSequence Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use `superpowers:subagent-driven-development` (recommended) or `superpowers:executing-plans` to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Move pitch resolution and chord realization out of `Conductor` into `Composer`. Composer fully populates `Part.elementSequence`; Conductor reads only that. Absorb the chord-walker voicing work and introduce `RealizationStrategy` as a Compose-tier registry parallel to `VoicingSelector`. Dissolve `ChordAccompanimentConfig`.

**Spec:** `docs/superpowers/specs/2026-04-22-composer-owns-event-sequence-design.md` — read before starting.

**Architecture:** 12 sequential stages, each one commit (or small commit cluster), each ending with a build + render + golden hash check. Stages chain autonomously on green; stop on red.

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
- `patches/test_jazz_turnaround_*.json` (7 patches) — pinned at Stage 2 when chord-walker lands.
- Sound-tier spot-check (one per family) at Stage 7: `patches/fhn_pure_tone.json`, `patches/Additive1.json`, `patches/sweep/<one>.json`.

**Commit message style:** `<verb>(<scope>): <subject>` matching existing repo convention (e.g. `refactor(structure): introduce ElementSequence type`). Each commit ends with the standard Co-Authored-By footer.

**Stop conditions for autonomous execution:**
1. Any golden WAV hash mismatch on a step flagged "✓ bit-identical" → surface diff details (which patch, hash before/after, possibly first sample diverging) to user.
2. Compile failure that isn't an obvious one-line typo → surface to user.
3. Any task where the plan as written turns out to need a non-trivial design call not anticipated → surface to user before improvising.

**Worktree:** This is a substantial refactor; create a worktree before Stage 1 (the brainstorming skill normally does this; verify before proceeding).

---

## File Structure

**New files (created during plan):**
- `engine/include/mforce/music/realization_strategy.h` — interface, registry, RealizationRequest, BlockRealizationStrategy, RhythmPatternRealizationStrategy (Stage 3)
- (No new test files — the project relies on golden renders rather than unit tests; existing test patches are the fixtures.)

**Files modified:**
- `engine/include/mforce/music/structure.h` — Element, Part, introduce ElementSequence (Stages 1, 9)
- `engine/include/mforce/music/composer.h` — `realize_chord_parts_`, new realize sub-step (Stages 4–6, 9)
- `engine/include/mforce/music/conductor.h` — narrow scope (Stages 7, 8)
- `engine/include/mforce/music/templates.h` — PassageTemplate fields, dissolve ChordAccompanimentConfig (Stages 3, 11)
- `engine/include/mforce/music/templates_json.h` — JSON read/write for new fields, remove old (Stages 3, 11)
- `engine/include/mforce/music/composer.h` (composer constructor/registration) — register RealizationStrategy implementations (Stage 3)
- `tools/mforce_cli/main.cpp` — possibly minor signature touch-ups if Conductor signature changes (low likelihood; Stages 7, 8)
- `patches/test_k467_walker.json` — chordConfig migration (Stage 10)
- chord-walker headers — land via merge (Stage 2): `voicing_profile.h`, `voicing_profile_selector.h`, `voicing_selector.h`, `smooth_voicing_selector.h`, `static_voicing_profile_selector.h`, `random_voicing_profile_selector.h`, `drift_voicing_profile_selector.h`, `scripted_voicing_profile_selector.h`, `engine/src/chord.cpp`, plus 7 `patches/test_jazz_turnaround_*.json`

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

## Stage 2 — Merge chord-walker into main

**Files:** Many — see chord-walker branch tip. The merge brings:
- New: `engine/include/mforce/music/voicing_profile.h`, `voicing_profile_selector.h`, `voicing_selector.h`, `smooth_voicing_selector.h`, `static_voicing_profile_selector.h`, `random_voicing_profile_selector.h`, `drift_voicing_profile_selector.h`, `scripted_voicing_profile_selector.h`
- Modified: `engine/src/chord.cpp` (rule-native `init_pitches`), `engine/include/mforce/music/templates.h` (PassageTemplate gains `voicingSelector` field), `engine/include/mforce/music/templates_json.h` (read it)
- New patches: `patches/test_jazz_turnaround_flat.json`, `_p0.json`, `_p05.json`, `_p1.json`, `_random.json`, `_drift.json`, `_scripted.json` (and possibly `_smooth.json`, `_rock.json`)

**Bit-identical:** ✓ for K467 (chord-walker work doesn't touch its path); jazz turnaround demos are NEW goldens pinned this stage.

### 2a — Merge

- [ ] **2.1** Verify chord-walker branch has all stage 0 stage 1 changes from main applied first. If main has diverged from chord-walker since last update:

```bash
git -C . log --oneline main ^chord-walker | head
```

Expected: list of main commits NOT yet on chord-walker. If non-empty, the merge will need conflict resolution.

- [ ] **2.2** Create merge branch (preserves main + chord-walker history independently):

```bash
git checkout -b merge-chord-walker
git merge chord-walker
```

Expected outcomes:
- (a) Clean merge → proceed to 2.4.
- (b) Conflicts → likely in `templates.h` (Stage 1 also touched it indirectly via Part) or in places main has changed since chord-walker branched. Resolve by hand. Ask user before any non-mechanical resolution decision.

- [ ] **2.3** If conflicts resolved:

```bash
git add <resolved files>
git commit
```

- [ ] **2.4** Build:

```bash
cmake --build build --config Release --target mforce_cli
```

Expected: clean build. Errors are likely compile-time signature mismatches between chord-walker's voicing types and main's post-Stage-1 ElementSequence. Fix or surface to user.

### 2b — Re-verify K467 bit-identical after merge

- [ ] **2.5** Render K467 goldens and confirm hashes still match baseline:

```bash
for p in walker harmony period structural; do
    build/tools/mforce_cli/Release/mforce_cli.exe --compose patches/test_k467_${p}.json renders/stage2_k467_${p} 1
done
sha256sum renders/stage2_k467_*.wav
```

Expected: all match baseline. (chord-walker introduced VoicingSelector but didn't wire it into `realize_chord_parts_`; K467 still goes through `sc->resolve(...)`.)

### 2c — Pin jazz turnaround goldens

- [ ] **2.6** Render each jazz turnaround patch and add hash to baseline file:

```bash
for p in flat p0 p05 p1 random drift scripted; do
    build/tools/mforce_cli/Release/mforce_cli.exe --compose patches/test_jazz_turnaround_${p}.json renders/stage2_jazz_${p} 1
done
sha256sum renders/stage2_jazz_*.wav >> docs/superpowers/baselines/2026-04-22-composer-owns-events-baselines.txt
```

Expected: 7 (or however many demo patches landed) NEW hashes appended to baseline.

- [ ] **2.7** Fast-forward main to merge branch:

```bash
git checkout main
git merge --ff-only merge-chord-walker
```

If not fast-forwardable (some commit landed on main during the merge), surface to user.

- [ ] **2.8** Commit the baseline update:

```bash
git add docs/superpowers/baselines/2026-04-22-composer-owns-events-baselines.txt
git commit -m "chore(baselines): pin jazz turnaround goldens after chord-walker merge"
```

---

## Stage 3 — `RealizationStrategy` interface + registry

**Files:**
- Create: `engine/include/mforce/music/realization_strategy.h`
- Modify: `engine/include/mforce/music/templates.h` (PassageTemplate gains `realizationStrategy` field; introduce `RhythmPattern` struct)
- Modify: `engine/include/mforce/music/templates_json.h` (read/write the new fields)
- Modify: `engine/include/mforce/music/composer.h` (constructor registers default strategies)

**Bit-identical:** ✓ (interface exists but is not yet wired into compose path)

### 3a — Define interface

- [ ] **3.1** Create `engine/include/mforce/music/realization_strategy.h`:

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
    // Future fields (meter, dynamics-at-beat, scale, etc.) added as needed.
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
// BlockRealizationStrategy — emits all chord pitches simultaneously at
// startBeat for durationBeats. The "do nothing fancy" default.
// ---------------------------------------------------------------------------
class BlockRealizationStrategy : public RealizationStrategy {
public:
    std::string name() const override { return "block"; }
    void realize(const RealizationRequest& req, ElementSequence& out) override {
        Element e{req.startBeat, req.chord};   // until stage 9, emit Chord-event
        // Note: req.chord.dur should already equal req.durationBeats. Trust it.
        out.add(e);
    }
};

// ---------------------------------------------------------------------------
// RhythmPatternRealizationStrategy — for each entry in the rhythm pattern,
// emits all chord pitches at the corresponding beat with the entry's
// duration (negative entries become Rests).
// ---------------------------------------------------------------------------
class RhythmPatternRealizationStrategy : public RealizationStrategy {
public:
    std::string name() const override { return "rhythm_pattern"; }
    void realize(const RealizationRequest& req, ElementSequence& out) override {
        if (!req.rhythmPattern) {
            // Fallback: behave like Block.
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

### 3b — Register the two defaults at Composer construction

- [ ] **3.2** In `composer.h`, find the Composer constructor (around line 90-130 where strategies are registered). Add an include for `realization_strategy.h` and register:

```cpp
// At top with other includes:
#include "mforce/music/realization_strategy.h"

// In the constructor, after the existing strategy registrations:
auto& realRegistry = RealizationStrategyRegistry::instance();
realRegistry.register_strategy(std::make_unique<BlockRealizationStrategy>());
realRegistry.register_strategy(std::make_unique<RhythmPatternRealizationStrategy>());
```

### 3c — Add `realizationStrategy` field to PassageTemplate

- [ ] **3.3** In `templates.h`, in `PassageTemplate` (around line 336-365):

```cpp
struct PassageTemplate {
    // ... existing fields ...
    std::string voicingSelector;       // already added by chord-walker (Stage 2)
    std::string realizationStrategy;   // NEW — empty defaults to "block"
    std::optional<RhythmPattern> rhythmPattern;  // NEW — config for rhythm_pattern strategy
};
```

Add `#include "mforce/music/realization_strategy.h"` if needed.

### 3d — JSON read/write

- [ ] **3.4** In `templates_json.h`, find the `PassageTemplate` from_json (around the line where `voicingSelector` is read; chord-walker added it at Stage 2). Add:

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

- [ ] **3.5** Build:

```bash
cmake --build build --config Release --target mforce_cli
```

Expected: clean build.

### 3e — Verify bit-identical

- [ ] **3.6** Render all 11 goldens (4 K467 + 7 jazz turnaround) and compare hashes:

```bash
for p in walker harmony period structural; do
    build/tools/mforce_cli/Release/mforce_cli.exe --compose patches/test_k467_${p}.json renders/stage3_k467_${p} 1
done
for p in flat p0 p05 p1 random drift scripted; do
    build/tools/mforce_cli/Release/mforce_cli.exe --compose patches/test_jazz_turnaround_${p}.json renders/stage3_jazz_${p} 1
done
sha256sum renders/stage3_k467_*.wav renders/stage3_jazz_*.wav
```

Expected: all 11 match the corresponding entries in `docs/superpowers/baselines/2026-04-22-composer-owns-events-baselines.txt`.

- [ ] **3.7** STOP on any mismatch.

- [ ] **3.8** Commit:

```bash
git add engine/include/mforce/music/realization_strategy.h engine/include/mforce/music/composer.h engine/include/mforce/music/templates.h engine/include/mforce/music/templates_json.h
git commit -m "feat(realization): RealizationStrategy interface + Block/RhythmPattern defaults"
```

---

## Stage 4 — Composer realize-to-events: `MelodicFigure`

**Files:**
- Modify: `engine/include/mforce/music/composer.h` (add realize sub-step at end of compose())
- Possibly: `engine/include/mforce/music/conductor.h` (move `step_note` helper to a shared header or duplicate)

**Bit-identical:** ✓ (Conductor still authoritative; new emit runs in parallel and is ignored)

### 4a — Decide DynamicState location

- [ ] **4.1** Read `conductor.h:125-160` (DynamicState struct + `velocity_at`). Move to `engine/include/mforce/music/dynamic_state.h` (new file) so both Composer and Conductor can include it. Update Conductor's include accordingly. No behavior change.

```cpp
// engine/include/mforce/music/dynamic_state.h
#pragma once
#include "mforce/music/structure.h"
#include "mforce/music/rng.h"

namespace mforce {
// ... full DynamicState struct moved verbatim from conductor.h ...
}
```

In `conductor.h`, replace the inline struct with `#include "mforce/music/dynamic_state.h"`.

- [ ] **4.2** Build to verify the move is clean:

```bash
cmake --build build --config Release --target mforce_cli
```

### 4b — Add `step_note` to a shared location

- [ ] **4.3** `Conductor::step_note` is currently a method on Conductor (or a free function in `conductor.h`). Identify its definition (grep: `step_note`). Move to `engine/include/mforce/music/pitch_walker.h` (new):

```cpp
// engine/include/mforce/music/pitch_walker.h
#pragma once
#include "mforce/music/basics.h"

namespace mforce {

// Step `currentNN` by `step` scale degrees in the given Scale.
inline float step_note(float currentNN, int step, const Scale& scale) {
    // ... copy current implementation verbatim ...
}

// Step `currentNN` to nearest chord-tone, then `step` chord-tones.
inline float step_chord_tone(float currentNN, int step,
                             const std::vector<Pitch>& chordTones) {
    // ... copy current implementation verbatim ...
}

} // namespace mforce
```

Replace Conductor's local implementations with includes + calls. Build clean. (No behavior change.)

### 4c — Add realize sub-step skeleton

- [ ] **4.4** In `composer.h`, after the existing `compose()` body completes (after `realize_chord_parts_(piece, tmpl);` at line 146), add a new call:

```cpp
realize_event_sequences_(piece, tmpl);
```

- [ ] **4.5** Define `realize_event_sequences_` as a private method on Composer:

```cpp
void realize_event_sequences_(Piece& piece, const PieceTemplate& tmpl) {
    for (auto& part : piece.parts) {
        // For each Section, look up the Part's Passage in this Section
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
    // Stage 4 implements MelodicFigure path only.
    // Stages 5 and 6 add ChordFigure and Chord-event paths.
    // For now: skip ChordFigure-only passages (handled at Stage 5).

    DynamicState dyn(passage.dynamicMarkings);
    float currentNN = -1.0f;
    bool haveStartingPitch = false;

    for (const auto& phrase : passage.phrases) {
        if (!haveStartingPitch) {
            currentNN = phrase.startingPitch.note_number();
            haveStartingPitch = true;
        }
        for (const auto& figPtr : phrase.figures) {
            auto* mel = dynamic_cast<const MelodicFigure*>(figPtr.get());
            if (!mel) continue;  // chord figure → handled at Stage 5
            for (const auto& unit : mel->units) {
                currentNN = step_note(currentNN, unit.step, section.scale);
                float soundNN = currentNN + float(unit.accidental);
                float vel = dyn.velocity_at(/* current passage-relative beat */);
                // TODO at 4.6: track passage-relative beat
                Note n;
                n.noteNumber = soundNN;
                n.velocity = vel;
                n.durationBeats = unit.duration;
                part.elementSequence.add({/*startBeats=*/0.0f, n});
                // TODO at 4.6: actual startBeats
            }
        }
    }
}
```

- [ ] **4.6** Replace the TODO placeholders with proper beat tracking. Track `currentBeat` (initially `passageStartBeat`), advance by `unit.duration` after each Note, pass to `dyn.velocity_at(currentBeat - passageStartBeat)`.

The full body should mirror `Conductor::perform_phrase` (`conductor.h:785-860+`) — same step-walking, same accidental handling, same dynamics lookup, same cross-passage truncation logic (`Section::truncateTailBeats`), but emitting Notes into `part.elementSequence` rather than playing through an instrument.

**Important**: `Conductor::perform_phrase` ALSO walks ChordFigure units (the `dynamic_cast<ChordFigure>` discriminator). For Stage 4, only handle MelodicFigure; Stage 5 adds ChordFigure.

### 4d — Verify bit-identical

- [ ] **4.7** Build:

```bash
cmake --build build --config Release --target mforce_cli
```

Expected: clean build.

- [ ] **4.8** Render all 11 goldens. Compare hashes:

```bash
# (same render commands as 3.6)
sha256sum renders/stage4_k467_*.wav renders/stage4_jazz_*.wav
```

Expected: all match baseline. (Conductor still walks the tree; the new realize step populates `elementSequence` but Conductor doesn't read it yet.)

- [ ] **4.9** STOP on any mismatch.

- [ ] **4.10** Commit:

```bash
git add engine/include/mforce/music/composer.h engine/include/mforce/music/dynamic_state.h engine/include/mforce/music/pitch_walker.h engine/include/mforce/music/conductor.h
git commit -m "feat(composer): realize MelodicFigure into Part.elementSequence"
```

---

## Stage 5 — Composer realize-to-events: `ChordFigure`

**Files:**
- Modify: `engine/include/mforce/music/composer.h` (extend `realize_passage_to_events_`)

**Bit-identical:** ✓

### 5a — Add ChordFigure branch

- [ ] **5.1** In `realize_passage_to_events_`, replace the `if (!mel) continue;` skip with a discrimination:

```cpp
const auto* mel  = dynamic_cast<const MelodicFigure*>(figPtr.get());
const auto* chf  = dynamic_cast<const ChordFigure*>(figPtr.get());
if (!mel && !chf) continue;
```

For the ChordFigure branch, replicate `Conductor::perform_phrase`'s chord-tone path (`conductor.h:814-829`):

```cpp
if (chf) {
    for (const auto& unit : chf->units) {
        // Find active chord at currentBeat (section-relative)
        float sectionBeat = currentBeat - passageStartBeat;
        Chord active = find_active_chord_(section, sectionBeat);
        auto resolvedTones = resolve_chord_tones_(active, section.scale);
        currentNN = step_chord_tone(currentNN, unit.step, resolvedTones);
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

- [ ] **5.2** Add `find_active_chord_(section, sectionBeat)` helper (mirrors `conductor.h:817-827`):

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

std::vector<Pitch> resolve_chord_tones_(const Chord& chord, const Scale& scale) const {
    // Likely already a helper; reuse if so. Otherwise:
    return chord.pitches;  // assuming chord is already resolved
}
```

(Cross-check the exact octave handling in conductor.h:828: `chordProg->chords.get(chordIdx).resolve(sectionScale, baseOctave);`. Use the same `baseOctave` value Conductor passes — likely a parameter to `perform_phrase`. Trace from `perform_passage` how it's set.)

### 5b — Verify bit-identical

- [ ] **5.3** Build + render all 11 goldens + sha256sum + compare.

- [ ] **5.4** STOP on any mismatch.

- [ ] **5.5** Commit:

```bash
git add engine/include/mforce/music/composer.h
git commit -m "feat(composer): realize ChordFigure into Part.elementSequence"
```

---

## Stage 6 — Composer realize-to-events: Chord events (via VoicingSelector + RealizationStrategy)

**Files:**
- Modify: `engine/include/mforce/music/composer.h` (Chord-events realize branch in `realize_passage_to_events_`; possibly refactor `realize_chord_parts_` to call the new path)

**Bit-identical:** ✓ (Chord events still emit to Conductor's ChordPerformer too — duplicate path; switch is at Stage 7)

### 6a — Chord-event realize branch

- [ ] **6.1** Currently `realize_chord_parts_` (composer.h:266-313) emits `Chord` Elements into `part.events` (now `part.elementSequence`) via `add_chord(pos, chord)`. Each Chord Element will get expanded into per-tone Notes by Conductor's ChordPerformer at perform time.

  We need to replace that with: same iteration, but use VoicingSelector to pick a voicing AND RealizationStrategy to emit per-tone Notes (or per-chord Chord events for now — see 6.2).

- [ ] **6.2** Add a method `realize_chord_events_for_part_(part, section, passage)` that runs after the existing `realize_chord_parts_` builds the chord progression. For each chord:

```cpp
// Pseudo:
const PassageTemplate* pt = lookup_passage_template_(part, section);
const std::string selectorName = pt ? pt->voicingSelector : "";
const std::string strategyName = pt ? pt->realizationStrategy : "";

VoicingSelector* selector = VoicingSelectorRegistry::instance().resolve(selectorName);
RealizationStrategy* strategy = RealizationStrategyRegistry::instance().resolve(
    strategyName.empty() ? "block" : strategyName);

const Chord* prev = nullptr;
for (each ChordProgression entry at beat) {
    Chord voiced;
    if (selector) {
        VoicingRequest req{
            /*scaleChord=*/sc, /*scale=*/&section.scale,
            /*rootOctave=*/cfg.octave, /*durationBeats=*/dur,
            /*previous=*/prev, /*melodyPitch=*/std::nullopt
        };
        voiced = selector->select(req);
    } else {
        voiced = sc.resolve(section.scale, cfg.octave, dur, cfg.inversion, cfg.spread);
    }

    RealizationRequest realReq{
        /*chord=*/voiced, /*startBeat=*/pos, /*durationBeats=*/dur,
        /*barIndex=*/computed_bar(pos, section.meter),
        /*rhythmPattern=*/(pt && pt->rhythmPattern) ? &*pt->rhythmPattern : nullptr
    };
    strategy->realize(realReq, part.elementSequence);
    prev = &voiced;  // hold last reference; lifetime managed locally
}
```

Notes:
- For Stage 6, **keep the existing `add_chord(pos, chord)` calls** in `realize_chord_parts_` so Conductor's ChordPerformer can still handle them. The new code adds in parallel, populating elementSequence with per-tone Notes (or with Chord-events depending on strategy implementation).
- Wait — there's a problem: BlockRealizationStrategy emits a `Chord`-typed Element (the simplest case). RhythmPatternRealizationStrategy ALSO emits Chord-typed Elements (one per pattern entry). At Stage 6 these go into `part.elementSequence` ALONGSIDE the existing `add_chord` calls — meaning the chord part will have DUPLICATED chord events. That's fine because Conductor ignores `elementSequence` for tree-walked Parts and only the existing `add_chord` calls reach Conductor's perform path.
- **Confirm at execution time**: trace the flow. If `add_chord` calls populate elementSequence AND the new realize pass also populates it AND Conductor reads from elementSequence ANYWHERE… then we'd double-emit. Verify by reading Conductor's path before stage 7. If risk found, surface to user.

- [ ] **6.3** Wire the chord-walker default fallback per spec section 4 ("Voicing-selector wiring at stage 6 default"): if Part has any of `voicingSelector` / `voicingProfile` / `realizationStrategy` set OR `rhythmPattern`, use the new selector path. Otherwise fall back to legacy `sc.resolve(...)`.

### 6b — Verify bit-identical

- [ ] **6.4** Build + render all 11 goldens + sha256sum + compare.

  Expected: bit-identical. K467 patches don't have `voicingSelector` / `realizationStrategy` set, so they use the legacy path; jazz turnaround patches use VoicingSelector but their PassageTemplate doesn't currently reference RealizationStrategy, so they default to "block" which emits a single Chord-event per call (matching pre-Stage 6 behavior).

  If any hash differs, the most likely cause is that `BlockRealizationStrategy` (which emits a Chord-event into elementSequence) differs from the existing `add_chord` (which appends a Chord-event with the same data). The two should be byte-identical in WAV terms; if not, investigate.

- [ ] **6.5** STOP on any mismatch.

- [ ] **6.6** Commit:

```bash
git add engine/include/mforce/music/composer.h
git commit -m "feat(composer): realize Chord events via VoicingSelector + RealizationStrategy"
```

---

## Stage 7 — Switch Conductor to read `elementSequence`

**Files:**
- Modify: `engine/include/mforce/music/conductor.h` (`perform(Part&)` and `perform(Piece&)` paths)

**Bit-identical:** ✓ (the moment of truth)

### 7a — Reroute the dispatch

- [ ] **7.1** Read `conductor.h:691-697` (the dispatch in `Conductor::perform_part` or its caller). Currently:

```cpp
if (!part.elementSequence.empty()) {
    perform_events(part, bpm, beatOffset, inst);
} else {
    perform_passage(it->second, scale, beatOffset, bpm, inst, ...);
}
```

(The actual current code is `if (!part.events.empty())` → after Stage 1 this should already be `if (!part.elementSequence.empty())`. Verify.)

- [ ] **7.2** Change the dispatch so EVERY Part with non-empty elementSequence routes through `perform_events`:

```cpp
// After Stage 7:
if (!part.elementSequence.empty()) {
    perform_events(part, bpm, beatOffset, inst);
}
// No else branch — if elementSequence is empty, nothing to do.
```

(Composer is now responsible for populating elementSequence for ALL Parts. Direct-build patches already do so. Algorithmic Parts started populating at Stages 4-6.)

### 7b — Verify bit-identical

- [ ] **7.3** Build:

```bash
cmake --build build --config Release --target mforce_cli
```

Expected: clean build (perform_passage is now unreferenced internally — but won't be deleted until Stage 8).

- [ ] **7.4** Render all 11 goldens + sha256sum + compare.

  Expected: ALL bit-identical to baseline. This is the moment of truth. If the realize step in Stages 4-6 was faithful to what Conductor did, the audio matches.

  Most likely failure modes:
  - Velocity curve drift (DynamicState lookup not at exactly the same beat)
  - Cross-passage / cross-section continuity (currentNN reset between passages)
  - Chord-event expansion timing (Chord vs per-tone Notes for the same wall-clock duration)
  - Articulation/ornament markings on Notes not transferred during realize
  - `Section::truncateTailBeats` not honored in realize step

- [ ] **7.5** If a hash mismatches:
  - Render the offending patch with `wav_check` to confirm it's not a cosmic-bit-flip:
    ```bash
    build/tools/wav_check.exe renders/baseline_X_1.wav
    build/tools/wav_check.exe renders/stage7_X_1.wav
    ```
  - Compare first divergent sample: `cmp -l baseline.wav stage7.wav | head -1`
  - Compare the dumped JSON (Composer-stage representation):
    ```bash
    diff renders/baseline_X_1.json renders/stage7_X_1.json
    ```
  - Surface to user with the diff. DO NOT proceed.

- [ ] **7.6** Commit (if green):

```bash
git add engine/include/mforce/music/conductor.h
git commit -m "feat(conductor): switch to ElementSequence-driven perform"
```

---

## Stage 8 — Delete unused Conductor code

**Files:**
- Modify: `engine/include/mforce/music/conductor.h` (delete `perform_passage`, `perform_phrase`, `step_note`/`step_chord_tone` helpers, `ChordPerformer`, `register_josie_figures`)
- Possibly: rename `ChordArticulation` → `ChordRealization` (decide based on consumer count)

**Bit-identical:** ✓ (the deleted code is unused after Stage 7)

### 8a — Identify dead code

- [ ] **8.1** Confirm no external callers of the targets:

```bash
grep -nrE "perform_passage|perform_phrase" engine tools | grep -v "third_party"
grep -nrE "ChordPerformer\b|register_josie_figures" engine tools | grep -v "third_party"
```

Expected: zero hits outside of `conductor.h` itself (and possibly some tests, which would also be deleted/migrated).

If any external caller exists, surface to user before deleting.

### 8b — Delete

- [ ] **8.2** Delete from `conductor.h`:
  - `Conductor::perform_passage` (line 751-end-of-method)
  - `Conductor::perform_phrase` (line 785-end-of-method)
  - The `step_note` / `step_chord_tone` calls inside perform_phrase already moved to `pitch_walker.h` at Stage 4. The originals (if still here) go.
  - `ChordPerformer` struct entirely (line 436 - end of struct, around line 660s)
  - `Conductor::register_josie_figures` (around line 462)
  - The `dynamic_cast<const ChordFigure*>` discriminator (becomes dead with perform_phrase)

### 8c — Decide on ChordArticulation

- [ ] **8.3** After 8.2's deletions, grep for remaining `ChordArticulation` consumers:

```bash
grep -nrE "ChordArticulation" engine tools | grep -v "third_party"
```

- If zero hits: delete the type from `figures.h` (it's at line 627).
- If one or more hits: rename to `ChordRealization` (mechanical: rename the struct, all members, and all usages). Build clean. Commit separately:

```bash
git commit -m "refactor(figures): rename ChordArticulation -> ChordRealization"
```

If deleted, no separate commit; rolls into 8.4.

### 8d — Verify

- [ ] **8.4** Build:

```bash
cmake --build build --config Release --target mforce_cli
```

Expected: clean build (deletions left no callers).

- [ ] **8.5** Render all 11 goldens + sha256sum + compare. Expected: bit-identical.

- [ ] **8.6** Commit:

```bash
git add engine/include/mforce/music/conductor.h engine/include/mforce/music/figures.h
git commit -m "refactor(conductor): delete tree-walk perform path and ChordPerformer"
```

---

## Stage 9 — Drop `Chord` from `Element` variant

**Files:**
- Modify: `engine/include/mforce/music/structure.h` (Element variant)
- Modify: `engine/include/mforce/music/composer.h` (chord-events realize: emit Notes not Chord, via VoicingSelector + RealizationStrategy)
- Modify: `engine/include/mforce/music/realization_strategy.h` (BlockRealizationStrategy emits per-tone Notes)
- Modify: any direct-build callers using `Part::add_chord(...)` — replace with helper

**Bit-identical:** ✓

### 9a — Refactor BlockRealizationStrategy to emit Notes

- [ ] **9.1** In `realization_strategy.h`, rewrite `BlockRealizationStrategy::realize` to expand the chord into per-tone Notes:

```cpp
void realize(const RealizationRequest& req, ElementSequence& out) override {
    for (const auto& pitch : req.chord.pitches) {
        Note n;
        n.noteNumber = pitch.note_number();
        n.velocity = 1.0f;     // velocity will come from a future pass; default for now
        n.durationBeats = req.durationBeats;
        out.add({req.startBeat, n});
    }
}
```

Same change for `RhythmPatternRealizationStrategy`: where it emits `out.add({beat, c})` (a Chord), replace with a loop emitting per-tone Notes for each pattern entry.

### 9b — Migrate direct-build add_chord callers

- [ ] **9.2** Grep for all callers of `Part::add_chord(...)`:

```bash
grep -nrE "\.add_chord\(" engine tools | grep -v "third_party"
```

For each caller:
- Identify the chord (already-resolved) and beat.
- Replace with: construct a temporary `BlockRealizationStrategy`, call `realize` on it with a `RealizationRequest`, emit Notes into `part.elementSequence`.
- Preferably wrap this in a Composer helper:

```cpp
// In Composer (or as a free function in composer.h):
inline void emit_chord_as_notes(Part& part, float startBeat, const Chord& chord) {
    BlockRealizationStrategy strat;
    RealizationRequest req{chord, startBeat, chord.dur, /*barIndex=*/1, /*rhythmPattern=*/nullptr};
    strat.realize(req, part.elementSequence);
}
```

Direct-build callers then call `emit_chord_as_notes(part, beat, chord)` instead of `part.add_chord(beat, chord)`.

### 9c — Drop Chord from variant

- [ ] **9.3** In `structure.h`:

```cpp
// Before:
struct Element {
    float startBeats{0.0f};
    std::variant<Note, Chord, Hit, Rest> content;
    // ... is_chord, chord() ...
};

// After:
struct Element {
    float startBeats{0.0f};
    std::variant<Note, Hit, Rest> content;
    // is_chord and chord() accessors deleted
};
```

Also drop the `is_chord` / `chord()` accessors. Drop `Part::add_chord(...)` (decide: delete entirely or keep as a thin wrapper that calls `emit_chord_as_notes`). Remove `Chord` includes if no other usage.

In `ElementSequence::add`, drop the `e.is_chord()` branch in the totalBeats calculation.

### 9d — Verify

- [ ] **9.4** Build:

```bash
cmake --build build --config Release --target mforce_cli
```

Expected: clean build. Likely 1-3 caller fixes needed if a `add_chord` call was missed.

- [ ] **9.5** Render all 11 goldens + sha256sum + compare.

  Expected: ALL bit-identical. The audio doesn't care whether the WAV came from "chord-event expanded to N notes at perform time" vs "N notes at compose time"; the same notes should sound at the same beats with the same velocities.

  If a hash differs, most likely the velocity defaults differ. Check that `emit_chord_as_notes` and the realize-step Note construction use the same velocity (1.0f or DynamicState lookup).

- [ ] **9.6** STOP on any mismatch.

- [ ] **9.7** Commit:

```bash
git add engine/include/mforce/music/structure.h engine/include/mforce/music/realization_strategy.h engine/include/mforce/music/composer.h
git commit -m "refactor(element): drop Chord from variant; expand to per-tone Notes at compose"
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
diff <(echo "$BASELINE_HASH") <(sha256sum renders/stage10_k467_walker_1.wav | awk '{print $1}')
```

  Expected: bit-identical. If not, listen to both renders, decide whether the migration was faithful or whether an inversion shifted, etc. If decided to accept new render, update the baseline file and pin a new golden:

```bash
sha256sum renders/stage10_k467_walker_1.wav > docs/superpowers/baselines/2026-04-22-composer-owns-events-baselines.txt.tmp
# Manually edit baseline file to update only the walker hash
```

- [ ] **10.4** Render all other 10 goldens + sha256sum + compare. Expected: bit-identical to baseline (untouched patches).

- [ ] **10.5** Identify and remove the legacy `sc->resolve(...)` fallback path introduced at Stage 6: now that `test_k467_walker` migrates, ALL chord parts route through VoicingSelector + RealizationStrategy. Remove the fallback in composer.h. Re-render and re-verify.

- [ ] **10.6** Commit:

```bash
git add patches/test_k467_walker.json engine/include/mforce/music/composer.h
git commit -m "feat(k467): migrate walker patch from chordConfig to selector+strategy"
```

If golden was re-pinned, add baselines file and amend message.

---

## Stage 11 — Delete `ChordAccompanimentConfig`

**Files:**
- Modify: `engine/include/mforce/music/templates.h` (delete struct)
- Modify: `engine/include/mforce/music/templates_json.h` (delete from_json + any callers)
- Modify: any consumer that still references it

**Bit-identical:** ✓

### 11a — Confirm no consumers

- [ ] **11.1** Grep:

```bash
grep -nrE "ChordAccompanimentConfig\b|chordConfig" engine tools patches | grep -v "third_party" | grep -v "\.json"
```

Expected: zero hits in code (only patches, which were migrated at Stage 10). If any remain, surface to user.

(Patches with `chordConfig` other than test_k467_walker would have been audited at Stage 10. If new ones exist, migrate them now.)

### 11b — Delete

- [ ] **11.2** In `templates.h`, delete the `ChordAccompanimentConfig` struct (around line 311-330) and its `BarOverride` nested type.

- [ ] **11.3** In `PassageTemplate`, delete the `std::optional<ChordAccompanimentConfig> chordConfig` field.

- [ ] **11.4** In `templates_json.h`, delete the `from_json(const json&, ChordAccompanimentConfig&)` function and the corresponding `if (j.contains("chordConfig"))` block in PassageTemplate's from_json.

### 11c — Verify

- [ ] **11.5** Build + render all 11 goldens + sha256sum + compare. Expected: bit-identical.

- [ ] **11.6** Commit:

```bash
git add engine/include/mforce/music/templates.h engine/include/mforce/music/templates_json.h
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

- [ ] **Final-2** Run any non-K467 sound-tier patches sampled in Stage 0 (`fhn_pure_tone`, `Additive1`, etc.) to confirm Sound tier is untouched.

- [ ] **Final-3** Update memory:
  - Add a new project memory entry: "Composer owns the ElementSequence" — refactor landed; Conductor narrowed; ChordAccompanimentConfig dissolved; voicing-selector merged.
  - Mark the older project memories that referred to ChordAccompanimentConfig, the Chord-as-event variant, and the perform-time pitch resolution as outdated; either delete or update.

- [ ] **Final-4** If a worktree was used, follow the standard finishing-a-development-branch flow (PR or merge to main).

---

## Self-review notes

This plan covers all 12 stages from the spec. Spot-checks against the spec:

- Section 1 (Data model changes) → Stages 1, 9.
- Section 2 (Composer pipeline) → Stages 4, 5, 6 (the realize sub-steps).
- Section 3 (chord-walker merge) → Stage 2.
- Section 4 (RealizationStrategy registry) → Stage 3.
- Section 5 (realize-to-events) → Stages 4, 5, 6.
- Section 6 (Conductor switch-and-narrow) → Stages 7, 8.
- Section 7 (Drop Chord from Element variant) → Stage 9.
- Section 8 (Patch migration) → Stage 10.
- Section 9 (Final cleanup) → Stages 11, 12.
- Migration plan table → reproduced in Stage 0 conventions.
- Resolved-during-design discussion → reflected as concrete instructions in stages.
- Open questions to settle at planning time → handled inline in respective stages (DynamicState location at 4.1, add_chord post-stage-9 at 9.2, fallback removal at 10.5, ChordArticulation rename-vs-delete at 8.3).

Open at execution time (cannot be answered without touching the code):
- Exact line numbers for every "modify X.h:N-M" reference. The plan uses spec-time line numbers; verify with `grep` at execution time as files evolve through the chain.
- DrumPerformer fate (deferred per spec; revisit after Stage 8).
- Whether `StepMode::ChordTone` field (orphaned) gets a consumer in the realize step at Stages 4-5 or stays orphaned. Deferred decision; if it stays orphaned, delete in Stage 11 cleanup.
