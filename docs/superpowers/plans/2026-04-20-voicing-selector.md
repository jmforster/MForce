# Voicing Selector Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use `superpowers:subagent-driven-development` (recommended) or `superpowers:executing-plans` to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Ship a per-Section-configurable `VoicingSelector` that resolves `ScaleChord` → concrete `Chord` pitches with voice-leading awareness. Rename `ChordArticulation` → `ChordRealization` along the way to remove a name collision. Two concrete selectors (`SmoothVoicingSelector`, `JazzPianoVoicingSelector`) prove the pattern.

**Architecture:** `VoicingSelectorRegistry` singleton (parallel to `StrategyRegistry`). `PassageTemplate` gains a `voicingSelector` name field. `realize_chord_parts_` calls the resolved selector instead of hardcoded `sc->resolve(...)`. `ChordDictionary::canonic()` accessor for the distinguished reference dictionary. All existing renders remain bit-identical until a patch explicitly opts in.

**Spec:** `docs/superpowers/specs/2026-04-20-voicing-selector-design.md` — read before starting.

**Worktree:** implement in `C:/@dev/repos/mforce/.claude/worktrees/composer-model-period-forms/` (4 commits ahead of main on `chord-walker` branch).

---

## Pre-flight

- [ ] **Step 0.1:** Read the spec end-to-end.
- [ ] **Step 0.2:** Confirm the worktree builds clean before starting.
  ```bash
  "C:/Program Files/Microsoft Visual Studio/2022/Community/MSBuild/Current/Bin/MSBuild.exe" "C:/@dev/repos/mforce/.claude/worktrees/composer-model-period-forms/build/tools/mforce_cli/mforce_cli.vcxproj" -p:Configuration=Release -nologo -v:minimal
  ```
- [ ] **Step 0.3:** Render `test_k467_pass3` and record peak/rms as baseline golden.
  ```bash
  "C:/@dev/repos/mforce/.claude/worktrees/composer-model-period-forms/build/tools/mforce_cli/Release/mforce_cli.exe" --compose "C:/@dev/repos/mforce/.claude/worktrees/composer-model-period-forms/patches/PluckU.json" "C:/@dev/repos/mforce/.claude/worktrees/composer-model-period-forms/renders/k467_pass3_baseline" 1 --template "C:/@dev/repos/mforce/.claude/worktrees/composer-model-period-forms/patches/test_k467_pass3.json"
  ```
  Record peak + rms + a hash of the output JSON. This is Stage 0 of the spec's migration plan.

---

## Stage 1 — Rename `ChordArticulation` → `ChordRealization`

**Files:**
- Modify: `engine/include/mforce/music/figures.h`
- Modify: `engine/include/mforce/music/conductor.h`
- Modify: any other file referencing `ChordArticulation` (grep first)

- [ ] **Step 1.1:** Grep for all occurrences.
  ```
  (use Grep tool for "ChordArticulation" across engine/include/mforce/**)
  ```
  Make a list; expect hits in `figures.h` (struct def), `conductor.h` (heavy usage), possibly tools.

- [ ] **Step 1.2:** Rename the struct at `figures.h:627` from `ChordArticulation` to `ChordRealization`. Update the type name, doc comment, and the `DIR_ASCENDING`/`DIR_DESCENDING`/`DIR_RANDOM` constants stay where they are (now qualified as `ChordRealization::DIR_ASCENDING`).

- [ ] **Step 1.3:** Replace every other `ChordArticulation` reference in `conductor.h` with `ChordRealization`. Local variables like `josie8`, `josie3`, `josie25` keep their names (they're names for specific realizations, not the type). Their type changes from `ChordArticulation` to `ChordRealization`.

- [ ] **Step 1.4:** Do NOT rename `Chord::figureName` (JSON back-compat). Do NOT touch unrelated word "articulation" that means the note-level staccato/legato concept (it lives on `Note::articulation` — a genuine different concept that keeps its name).

- [ ] **Step 1.5:** Build.
  ```bash
  "C:/Program Files/Microsoft Visual Studio/2022/Community/MSBuild/Current/Bin/MSBuild.exe" "C:/@dev/repos/mforce/.claude/worktrees/composer-model-period-forms/build/tools/mforce_cli/mforce_cli.vcxproj" -p:Configuration=Release -nologo -v:minimal
  ```
  Expected: clean compile (warnings OK if not new).

- [ ] **Step 1.6:** Render pass3; compare peak/rms/hash to Stage 0 baseline. **Must be bit-identical.** If not, the rename accidentally altered behavior — stop and debug.

- [ ] **Step 1.7:** Commit.
  ```
  refactor(music): rename ChordArticulation -> ChordRealization
  ```

---

## Stage 2 — Canonic dictionary accessor

**Files:**
- Modify: `engine/include/mforce/music/basics.h` — add `ChordDictionary::canonic()` declaration
- Modify: `engine/src/chord.cpp` — define `ChordDictionary::canonic()` body

- [ ] **Step 2.1:** In `basics.h:360-369`, add `static const ChordDictionary& canonic();` to `ChordDictionary`.

- [ ] **Step 2.2:** In `engine/src/chord.cpp`, define `ChordDictionary::canonic()` to return a static `ChordDictionary` populated with canonical voicings of every quality name present in the legacy Default dictionary: at minimum `M, m, 7, m7, M7, mM7, 6, m6, -, b5, -7, h7, +, +7, +M7, 9, m9, M9, 69, 13, M13, m13, 67, M67, m67, 7b5, 7b9, 7#9, m7b9, 9b5, 7b5b9, 7b5#9, m7b5b9, 7#5#9, 7b13, 5` (power chord).

  Each `ChordDef` uses the smallest-interval voicing (close, root-position, within an octave where possible). Consult the legacy `ChordDictionary.cs` for the canonical interval lists and copy faithfully. Example:
  ```cpp
  canonic.chords["7b9"] = ChordDef{
      /*shortName=*/"7b9",
      /*displayName=*/"7♭9",
      /*name=*/"Seven Flat Nine",
      /*intervals=*/{"M3", "P5", "m7", "m9"},
      /*omitRoot=*/false
  };
  ```
  (adjust the exact struct init syntax to match `ChordDef`'s actual constructor).

- [ ] **Step 2.3:** Make sure `ChordDef::get()` still finds canonic entries by name — either route the global `get()` to check canonic as fallback, or register canonic into the global name table at init time. Pick whichever matches existing patterns in `chord.cpp`.

- [ ] **Step 2.4:** Build + render pass3; bit-identical to baseline.

- [ ] **Step 2.5:** Commit.
  ```
  feat(harmony): ChordDictionary::canonic() first-class primitive
  ```

---

## Stage 3 — `VoicingSelector` interface + registry

**Files:**
- Create: `engine/include/mforce/music/voicing_selector.h`
- Modify: `engine/include/mforce/music/composer.h` — include + register nothing yet (registry available but unused)

- [ ] **Step 3.1:** Create `voicing_selector.h`:
  ```cpp
  #pragma once
  #include "mforce/music/basics.h"
  #include <memory>
  #include <optional>
  #include <string>
  #include <unordered_map>

  namespace mforce {

  struct VoicingRequest {
      ScaleChord scaleChord;
      const Scale* scale{nullptr};
      int rootOctave{3};
      float durationBeats{1.0f};
      const Chord* previous{nullptr};
      std::optional<Pitch> melodyPitch;
  };

  class VoicingSelector {
  public:
      virtual ~VoicingSelector() = default;
      virtual std::string name() const = 0;
      virtual Chord select(const VoicingRequest& req) = 0;
  };

  class VoicingSelectorRegistry {
  public:
      static VoicingSelectorRegistry& instance();
      void register_selector(std::unique_ptr<VoicingSelector> s);
      VoicingSelector* resolve(const std::string& n) const;

  private:
      std::unordered_map<std::string, std::unique_ptr<VoicingSelector>> selectors_;
  };

  } // namespace mforce
  ```

- [ ] **Step 3.2:** Include `voicing_selector.h` from `composer.h` (does not yet wire or register).

- [ ] **Step 3.3:** Build + render pass3; bit-identical.

- [ ] **Step 3.4:** Commit.
  ```
  feat(voicing): VoicingSelector interface + registry
  ```

---

## Stage 4 — `SmoothVoicingSelector` + back-compat shim

**Files:**
- Create: `engine/include/mforce/music/smooth_voicing_selector.h`
- Modify: `engine/include/mforce/music/composer.h` — register `SmoothVoicingSelector`; modify `realize_chord_parts_` with back-compat shim
- Modify: `engine/include/mforce/music/templates.h` — add `voicingSelector` field to `PassageTemplate`
- Modify: `engine/include/mforce/music/templates_json.h` — add `voicingSelector` serialization

### 4a — selector implementation

- [ ] **Step 4.1:** Create `smooth_voicing_selector.h`. Algorithm:
  - Get canonic `ChordDef` via `ChordDictionary::canonic().get_chord_def(req.scaleChord.quality->name)`.
  - Resolve base chord at inversion=0, spread=0 using existing `ScaleChord::resolve(scale, octave, dur, 0, 0)`.
  - If `req.previous == nullptr`, return the base chord. (Preserves current behavior for first chord.)
  - Else enumerate inversions `0..pitches.size()-1`. For each, produce a candidate Chord via `ScaleChord::resolve(scale, octave, dur, inv, 0)`.
  - Score each candidate: greedy nearest-tone sum of `abs(semitone_distance)` from each pitch in previous to its nearest pitch in candidate, then from each pitch in candidate to its nearest pitch in previous (symmetric). Lower = better.
  - If `req.melodyPitch`, add +4 penalty if the candidate's highest pitch is within a half-step-but-not-equal to the melody pitch (clash). Add -2 bonus if candidate contains the melody pitch.
  - Return the lowest-scored candidate.

### 4b — template field + JSON

- [ ] **Step 4.2:** Add to `PassageTemplate` (`templates.h:341-365`):
  ```cpp
  std::string voicingSelector;  // name of a registered VoicingSelector; empty = back-compat
  ```

- [ ] **Step 4.3:** Add JSON round-trip in `templates_json.h`:
  - `to_json`: emit `"voicingSelector"` only if non-empty.
  - `from_json`: read `voicingSelector` with default empty.

### 4c — registration + wiring

- [ ] **Step 4.4:** In `Composer::Composer()` (after existing strategy registrations), register the smooth selector:
  ```cpp
  VoicingSelectorRegistry::instance()
      .register_selector(std::make_unique<SmoothVoicingSelector>());
  ```
  Under the name `"smooth"`.

- [ ] **Step 4.5:** Modify `realize_chord_parts_` (`composer.h:266-313`). Inside the `for (float dur : pattern) { ... }` loop, replace:
  ```cpp
  Chord chord = sc->resolve(sec.scale, cfg.octave, dur,
                            cfg.inversion, cfg.spread);
  ```
  with:
  ```cpp
  VoicingSelector* selector = nullptr;
  if (!passIt->second.voicingSelector.empty()) {
      selector = VoicingSelectorRegistry::instance()
                     .resolve(passIt->second.voicingSelector);
  }
  Chord chord;
  if (selector) {
      // previous chord on this Part: walk part->events backwards for the
      // most recent chord event before `pos`
      const Chord* prev = nullptr;
      for (auto it = part->events.rbegin(); it != part->events.rend(); ++it) {
          if (it->is_chord() && it->startBeats < pos) {
              prev = &it->chord();
              break;
          }
      }
      VoicingRequest req{*sc, &sec.scale, cfg.octave, dur, prev, std::nullopt};
      chord = selector->select(req);
  } else {
      chord = sc->resolve(sec.scale, cfg.octave, dur,
                          cfg.inversion, cfg.spread);
  }
  part->add_chord(pos, chord);
  ```

### 4d — regression

- [ ] **Step 4.6:** Build + render pass3. **Bit-identical to Stage 0 baseline**, because pass3 doesn't set `voicingSelector` (opt-in). If not bit-identical, investigate.

- [ ] **Step 4.7:** Commit.
  ```
  feat(voicing): SmoothVoicingSelector + template opt-in
  ```

---

## Stage 5 — Opt pass3 into `SmoothVoicingSelector`

**Files:**
- Modify: `patches/test_k467_pass3.json` — add `voicingSelector` to chords Part's Main passage

- [ ] **Step 5.1:** Edit `patches/test_k467_pass3.json`. In the `"chords"` part's `Main` passage, add:
  ```json
  "voicingSelector": "smooth"
  ```
  Alongside the existing `chordConfig`.

- [ ] **Step 5.2:** Render pass3 + copy the WAV to main `renders/`:
  ```bash
  "C:/@dev/repos/mforce/.claude/worktrees/composer-model-period-forms/build/tools/mforce_cli/Release/mforce_cli.exe" --compose "C:/@dev/repos/mforce/.claude/worktrees/composer-model-period-forms/patches/PluckU.json" "C:/@dev/repos/mforce/.claude/worktrees/composer-model-period-forms/renders/k467_pass3" 1 --template "C:/@dev/repos/mforce/.claude/worktrees/composer-model-period-forms/patches/test_k467_pass3.json"
  cp "C:/@dev/repos/mforce/.claude/worktrees/composer-model-period-forms/renders/k467_pass3_1.wav" "C:/@dev/repos/mforce/renders/k467_pass3_smooth.wav"
  ```

- [ ] **Step 5.3:** Inspect the rendered JSON. Walk the chords Part's events; verify that consecutive chord voicings show reduced inversion-jumping (compare to Stage 0 baseline JSON). Document finding.

- [ ] **Step 5.4:** Matt listens to the new `.wav`. Proceed only after Matt confirms the chord motion is perceptibly smoother. If not, debug scoring / greedy algorithm — likely issues: candidate-set too narrow (expand spread), or voice-leading metric double-counts (switch to unilateral nearest-tone).

- [ ] **Step 5.5:** Commit (pass3 change + new golden).
  ```
  feat(voicing): pass3 opts into smooth voice leading + new golden
  ```

---

## Stage 6 — `piano_jazz` dictionary + `JazzPianoVoicingSelector`

**Files:**
- Modify: `engine/src/chord.cpp` — add `piano_jazz` dictionary
- Create: `engine/include/mforce/music/jazz_piano_voicing_selector.h`
- Modify: `engine/include/mforce/music/composer.h` — register selector

### 6a — dictionary

- [ ] **Step 6.1:** In `chord.cpp`, during `ChordDictionary::init_all()`, populate `piano_jazz` with a minimum demonstration set. Each voicing reflects a jazz-piano rootless or shell idiom. For example:
  - `M7`: `[M3, P5, M7, M9]` (close rooted) or `[M3, M7, M9, M13]` (rootless) — choose one; preferred rootless: `[M3, M7, M9, M13]`.
  - `m7`: `[m3, P5, m7, M9]` rooted, or `[m3, m7, M9, P11]` rootless
  - `7`: `[M3, m7, M9, M13]` rootless dom
  - `7b9`: `[M3, m7, m9, M13]` rootless
  - `m7b5`: `[m3, dim5, m7, M9]` rooted
  - Plus `M`, `m`, `M9`, `m9`, `9` to have enough to survive a short progression without falling back to canonic.

  Intent: this is a *minimum* set for demo; exhaustive population is future work.

### 6b — selector

- [ ] **Step 6.2:** Create `jazz_piano_voicing_selector.h`. Same voice-leading scoring backend as `SmoothVoicingSelector`, but:
  - Primary dictionary: `piano_jazz`
  - If quality not in `piano_jazz`, fall back to `canonic`
  - Selector name: `"piano_jazz"`

  The cleanest factoring: extract a shared `score_voicing_leading(prev, candidate, melody)` helper into a small `voicing_scoring.h` so both selectors use it.

### 6c — register + smoke test

- [ ] **Step 6.3:** Register in `Composer::Composer()` alongside smooth.

- [ ] **Step 6.4:** Build + render pass3 → bit-identical to Stage 5 (pass3 uses smooth, not jazz).

- [ ] **Step 6.5:** Commit.
  ```
  feat(voicing): piano_jazz dictionary + JazzPianoVoicingSelector
  ```

---

## Stage 7 — Jazz demo template

**Files:**
- Create: `patches/test_jazz_ii_V_I.json`

- [ ] **Step 7.1:** Author a minimal jazz template: C major key, 8 bars, simple `ii-V-I-I-ii-V-I-I` chord progression (literal), one-bar harmonic rhythm, melody Part doing just `generate` with some function tags, chord Part using `"voicingSelector": "piano_jazz"`. Use an existing instrument patch (e.g. `PluckU.json`).

  Simpler variant if `voicingSelector` + literal ChordProgression path isn't wired yet: author the progression via `ChordProgressionBuilder` if that exists, or hand-author it directly in JSON.

- [ ] **Step 7.2:** Render. Copy to main `renders/`. Pin as new golden.

- [ ] **Step 7.3:** Matt listens; validate that the rootless voicings sound jazz-piano-ish (no low root doubling; guide tones clear).

- [ ] **Step 7.4:** Commit.
  ```
  test(voicing): jazz ii-V-I demo patch
  ```

---

## Stage 8 — Final regression + cleanup

- [ ] **Step 8.1:** Re-render every existing test template that exercises chord parts (grep for `"role": "harmony"` in patches). Confirm: every patch that does *not* set `voicingSelector` produces bit-identical output to pre-Stage-1. Every patch that opts in produces the intended new output.

- [ ] **Step 8.2:** Run any project-wide golden comparison script (check `renders/` for pinned goldens).

- [ ] **Step 8.3:** Clean up: remove any `TODO(voicing)` markers added during development that have been resolved.

- [ ] **Step 8.4:** Copy final renders to main `renders/` directory per the copy-worktree-renders-to-main feedback memory.

- [ ] **Step 8.5:** Update `docs/audits/2026-04-20-composition-framework-checkpoint.md` (or a new addendum) noting that voicing selector is landed — checks off one item from that audit's critical-path list.

- [ ] **Step 8.6:** Commit if anything changed; otherwise note clean.
  ```
  chore(voicing): final regression + docs
  ```

---

## Open-question answers to record during planning

At planning time (before starting Stage 1), confirm:

- **Voice-leading algorithm:** greedy symmetric nearest-tone (starting choice; upgrade to Hungarian if greedy produces visibly-bad pairings in practice)
- **Spread dimension:** frozen at 0 in this phase; expand to `{0, 1}` if single-inversion search produces no-good candidates in Stage 5
- **`last_chord_event_before(pos)`:** returns most recent chord regardless of intervening rests
- **`omitRoot` flag:** honored as-is; rootless voicings in `piano_jazz` set `omitRoot=true` on their `ChordDef`s
- **Dictionary population:** hardcoded C++ in `chord.cpp` (not JSON) for this phase; JSON loading deferred
- **Candidate octave placement:** root placed at `req.rootOctave`; no ±1 octave search in this phase — refine later if needed

## Success criteria

- All existing renders bit-identical through Stage 4.
- Pass3 with `voicingSelector: "smooth"` sounds audibly smoother than before (subjective, confirmed by Matt).
- Jazz demo renders with `voicingSelector: "piano_jazz"` produces rootless voicings audible to a jazz-literate listener.
- No new `const_cast`s, no new magic strings beyond the selector-name field, no new architectural debt introduced.
- Spec's Stage 0–8 completed; each commit revert-able.
