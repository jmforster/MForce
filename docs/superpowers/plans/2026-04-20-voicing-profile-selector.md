# Voicing Profile Selector Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use `superpowers:subagent-driven-development` or `superpowers:executing-plans` to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax.

**Goal:** Refactor `Chord::init_pitches` to rule-native inversion + spread, replace the negative-inversion hack with a `rootOctave` search in the selector, then build a `VoicingProfileSelector` abstraction that varies `(inversion, spread, priority)` per chord under author-configurable policies.

**Spec:** `docs/superpowers/specs/2026-04-20-voicing-profile-selector-design.md` — read before starting.

**Worktree:** `C:/@dev/repos/mforce/.claude/worktrees/composer-model-period-forms/` (chord-walker branch).

---

## Stage 0 — Baseline

- [ ] **0.1** Build clean.
- [ ] **0.2** Render and record (peak, rms, JSON md5) for:
  - `test_jazz_turnaround_flat`
  - `test_jazz_turnaround_p0`
  - `test_jazz_turnaround_p05`
  - `test_jazz_turnaround_p1`
  - `test_k467_pass3`
- [ ] **0.3** Save these as `renders/regress_*` alongside.

---

## Stage 1 — Rule-native `init_pitches` + remove negative inversion

**Files:**
- Modify: `engine/src/chord.cpp` (init_pitches)
- Modify: `engine/include/mforce/music/smooth_voicing_selector.h` (enumerate `inv × spread × octave`; remove negative-inversion loop)

### 1a — init_pitches rewrite

- [ ] **1.1** In `chord.cpp`, rewrite `Chord::init_pitches()` per rule-native semantics:
  - Collect chord-tone semitone offsets: implicit root (0) unless `omitRoot`, then every interval in `def->intervals` except "1" (dedup).
  - Sort offsets ascending. Call this `tones[]`, length `N`.
  - Compute voiced positions by the walk:
    ```
    int bass = clamp(inversion, 0, N-1);     // rotate bass chord-tone
    std::vector<int> pos;
    std::set<int> voicedPCs;
    pos.push_back(bass);
    voicedPCs.insert((int)tones[bass] % 12);
    while ((int)pos.size() < N) {
        int p = pos.back() + (spread + 1);
        while (voicedPCs.count(((int)tones[p % N] + (p / N) * 12) % 12)) ++p;
        pos.push_back(p);
        voicedPCs.insert(((int)tones[p % N]) % 12);
    }
    ```
  - For each position `p`, emit pitch = `root + tones[p % N] + (p / N) * 12`.
  - Remove the old move-lowest-up and negative-inversion branches.
  - Keep the final sort-by-pitch at the end.

- [ ] **1.2** Unit-test in head against the spec's verification table:
  | Chord | inv | spread | Expected |
  |---|---|---|---|
  | Cmaj triad | 0 | 0 | C4 E4 G4 |
  | Cmaj triad | 0 | 1 | C4 G4 E5 |
  | Cmaj triad | 0 | 2 | C4 E5 G6 |
  | Cmaj7 | 0 | 1 | C4 G4 E5 Bb5 |
  | Cmaj7 | 1 | 1 | E4 Bb4 G5 C6 |
  | Cmaj7 | 2 | 2 | G4 E5 Bb5 C6 (verify) |
  | Em7 | 3 | 0 | D5 E5 G5 B5 (at E4 root; to get D4 in bass, caller passes E3 root) |

### 1b — selector candidate enumeration

- [ ] **1.3** In `smooth_voicing_selector.h`, replace the `for (int inv = -(n-1); inv < n; ++inv)` loop with a triple-nested search:
  ```cpp
  const int N = int(base.pitches.size());
  const int octRange = 1;  // ±1 octave search
  for (int inv = 0; inv < N; ++inv) {
      for (int spread = 0; spread < N; ++spread) {
          for (int octOff = -octRange; octOff <= octRange; ++octOff) {
              Chord c = sc.resolve(*req.scale,
                                   req.rootOctave + octOff,
                                   req.durationBeats, inv, spread);
              // score and append to cands as before
          }
      }
  }
  ```
- [ ] **1.4** Pass `spread` through `ScaleChord::resolve`. Verify `resolve`'s signature already supports it (it takes `inversion, spread` per `basics.h:381-382` — yes). If not, extend.

### 1c — verify

- [ ] **1.5** Build. Fix any compile errors.
- [ ] **1.6** Render all 5 baseline patches. Output **will differ** from Stage 0 because the voicing semantics changed.
- [ ] **1.7** Matt listens to before/after pairs for at least `jazz_p0` and `jazz_p05` (the ones with selector active). Expected: the register behavior shifts, but voicings should still sound musical. If broken, stop and diagnose.
- [ ] **1.8** Copy new renders to main `renders/` per feedback memory.
- [ ] **1.9** Commit:
  ```
  refactor(voicing): rule-native init_pitches + rootOctave search

  Replaces the negative-inversion hack with a clean parameterization:
    inversion = bass chord-tone index (list rotation, 0..N-1)
    spread    = voicing-gap walk rule (advance spread+1 positions;
                skip dupes until new pitch class)
    rootOctave = passed by the selector, searched across ±1 octave
  ```

---

## Stage 2 — `VoicingProfile` struct + field lift

**Files:**
- Create: `engine/include/mforce/music/voicing_profile.h`
- Modify: `voicing_selector.h` (VoicingRequest carries profile)
- Modify: `templates.h` (PassageTemplate.voicingProfile)
- Modify: `templates_json.h` (back-compat reader)
- Modify: `smooth_voicing_selector.h` (reads profile)
- Modify: `composer.h` (threads profile)

- [ ] **2.1** Create `voicing_profile.h`:
  ```cpp
  struct VoicingProfile {
      std::vector<int> allowedInversions;   // empty = any
      std::vector<int> allowedSpreads;      // empty = any
      float priority{0.0f};                 // [0,1]
  };
  ```

- [ ] **2.2** In `VoicingRequest` (voicing_selector.h), replace scalar `priority` + `dictionaryName` with:
  ```cpp
  VoicingProfile profile;
  std::string dictionaryName;
  ```
- [ ] **2.3** In `SmoothVoicingSelector`, read `req.profile.priority` instead of `req.priority`. Allow-list filtering added in Stage 3.

- [ ] **2.4** In `PassageTemplate` (templates.h), replace `voicingPriority` scalar with nested `voicingProfile`:
  ```cpp
  VoicingProfile voicingProfile;
  ```

- [ ] **2.5** Update `templates_json.h` with back-compat:
  - On read: if JSON has `voicingPriority` at top level, lift into `voicingProfile.priority`. Also accept `voicingProfile` object directly.
  - On read: same lift for future `allowedInversions` / `allowedSpreads` if top-level.
  - On write: always emit `voicingProfile` (nested), not flat fields.

- [ ] **2.6** Update `composer.h` `realize_chord_parts_` to build `VoicingRequest` with `req.profile = passIt->second.voicingProfile`.

- [ ] **2.7** Build. Render all 5 test patches. Expect bit-identical to Stage 1 (filter is empty, priority same).

- [ ] **2.8** Commit:
  ```
  feat(voicing): VoicingProfile bundle + JSON back-compat

  Bundles allowedInversions/allowedSpreads/priority into a single
  profile struct on PassageTemplate. Flat-field JSON (voicingPriority)
  still parses; lifted into profile at read time.
  ```

---

## Stage 3 — Filter enforcement

- [ ] **3.1** In `SmoothVoicingSelector::select`, wrap the candidate-enumeration loop body:
  ```cpp
  const auto& pf = req.profile;
  auto invOk = [&](int v){
      return pf.allowedInversions.empty() ||
             std::find(pf.allowedInversions.begin(),
                       pf.allowedInversions.end(), v)
                != pf.allowedInversions.end();
  };
  auto sprOk = [&](int v){
      return pf.allowedSpreads.empty() ||
             std::find(pf.allowedSpreads.begin(),
                       pf.allowedSpreads.end(), v)
                != pf.allowedSpreads.end();
  };
  for (int inv = 0; inv < N; ++inv) {
      if (!invOk(inv)) continue;
      for (int spread = 0; spread < N; ++spread) {
          if (!sprOk(spread)) continue;
          // ...rootOctave loop, score candidate...
      }
  }
  ```

- [ ] **3.2** Handle pathological config (all candidates filtered out): fall back to `(inv=0, spread=0, rootOctave=req.rootOctave)` with a stderr warning "VoicingProfile allow-lists eliminated all candidates; using fallback."

- [ ] **3.3** Build. Render Stage-2 patches → bit-identical (allow-lists still empty in all patches).

- [ ] **3.4** Add a test patch `test_jazz_turnaround_rock.json` with `allowedInversions: [0, 1]` (no 2nd/3rd inv). Render, copy to main, Matt listens to confirm the restriction fires (no 2nd/3rd inversions in output).

- [ ] **3.5** Commit:
  ```
  feat(voicing): allowedInversions + allowedSpreads filtering
  ```

---

## Stage 4 — Profile Selector interface + StaticVoicingProfileSelector

**Files:**
- Create: `engine/include/mforce/music/voicing_style_profile selector.h`
- Create: `engine/include/mforce/music/static_profile selector.h`
- Modify: `templates.h` (add profile selector fields to PassageTemplate)
- Modify: `templates_json.h` (serialize profile selector config)
- Modify: `composer.h` (instantiate profile selector per Passage, call per chord)

- [ ] **4.1** Create `voicing_style_profile selector.h`:
  ```cpp
  class VoicingProfileSelector {
   public:
      virtual ~VoicingProfileSelector() = default;
      virtual std::string name() const = 0;
      virtual void reset(uint32_t seed) = 0;
      virtual VoicingProfile profile_for_chord(
          int chordIdx, float beatInBar, float beatInPassage) = 0;
  };

  class VoicingProfileSelectorRegistry {
   public:
      static VoicingProfileSelectorRegistry& instance();
      void register_factory(const std::string& name,
                            std::function<std::unique_ptr<VoicingProfileSelector>()> factory);
      std::unique_ptr<VoicingProfileSelector> create(const std::string& name) const;
  };
  ```
  Factory pattern (not singleton instance) because profile selectors carry per-Passage state that must not leak across Passages.

- [ ] **4.2** Create `static_profile selector.h`:
  ```cpp
  class StaticVoicingProfileSelector : public VoicingProfileSelector {
   public:
      void configure(const VoicingProfile& baseline) { baseline_ = baseline; }
      std::string name() const override { return "static"; }
      void reset(uint32_t) override {}
      VoicingProfile profile_for_chord(int, float, float) override {
          return baseline_;
      }
   private:
      VoicingProfile baseline_;
  };
  ```

- [ ] **4.3** In `PassageTemplate`:
  ```cpp
  std::string voicingProfileSelector;        // empty = StaticVoicingProfileSelector
  nlohmann::json voicingProfileSelectorConfig;  // profile selector-specific params
  ```

- [ ] **4.4** In `templates_json.h`, serialize the two new fields.

- [ ] **4.5** In `Composer`, register the `StaticVoicingProfileSelector` factory on ctor:
  ```cpp
  VoicingProfileSelectorRegistry::instance()
      .register_factory("static", [](){
          return std::make_unique<StaticVoicingProfileSelector>();
      });
  ```

- [ ] **4.6** In `realize_chord_parts_`, instantiate profile selector per Passage:
  ```cpp
  std::string selectorName = passIt->second.voicingProfileSelector.empty()
                           ? "static" : passIt->second.voicingProfileSelector;
  auto profileSelector = VoicingProfileSelectorRegistry::instance().create(selectorName);
  if (auto* sw = dynamic_cast<StaticVoicingProfileSelector*>(profileSelector.get())) {
      sw->configure(passIt->second.voicingProfile);
  }
  // TODO: profileSelector->configure_from_json(passIt->second.voicingProfileSelectorConfig)
  profileSelector->reset(tmpl.masterSeed ^ 0x566F6953u ^ uint32_t(sectionIdx * 100 + partIdx));
  int chordIdx = 0;
  // in the chord loop:
  VoicingProfile profile = profile selector->profile_for_chord(chordIdx, beatInBar, beatInPassage);
  req.profile = profile;
  // ...
  ++chordIdx;
  ```

- [ ] **4.7** Build. Render all → bit-identical (StaticVoicingProfileSelector over default profile).

- [ ] **4.8** Commit:
  ```
  feat(voicing): VoicingProfileSelector interface + StaticVoicingProfileSelector
  ```

---

## Stage 5 — RandomVoicingProfileSelector

**Files:**
- Create: `engine/include/mforce/music/uniform_jitter_profile selector.h`
- Modify: `composer.h` (register factory)

- [ ] **5.1** Create `uniform_jitter_profile selector.h`:
  ```cpp
  class RandomVoicingProfileSelector : public VoicingProfileSelector {
   public:
      void configure_from_json(const nlohmann::json& cfg) {
          priorityMin_ = cfg.value("priorityMin", 0.0f);
          priorityMax_ = cfg.value("priorityMax", 1.0f);
          if (cfg.contains("inversionProfiles")) {
              inversionProfiles_.clear();
              for (auto& p : cfg["inversionProfiles"]) {
                  std::vector<int> prof;
                  for (auto& v : p) prof.push_back(v.get<int>());
                  inversionProfiles_.push_back(std::move(prof));
              }
          }
          if (cfg.contains("spreadProfiles")) {
              // analogous
          }
      }

      std::string name() const override { return "uniform_jitter"; }
      void reset(uint32_t seed) override { rng_ = Randomizer(seed); }
      VoicingProfile profile_for_chord(int, float, float) override {
          VoicingProfile p;
          p.priority = rng_.value() * (priorityMax_ - priorityMin_) + priorityMin_;
          if (!inversionProfiles_.empty()) {
              int idx = rng_.int_range(0, (int)inversionProfiles_.size());
              p.allowedInversions = inversionProfiles_[idx];
          }
          if (!spreadProfiles_.empty()) {
              int idx = rng_.int_range(0, (int)spreadProfiles_.size());
              p.allowedSpreads = spreadProfiles_[idx];
          }
          return p;
      }

   private:
      Randomizer rng_{0};
      float priorityMin_{0.0f};
      float priorityMax_{1.0f};
      std::vector<std::vector<int>> inversionProfiles_;
      std::vector<std::vector<int>> spreadProfiles_;
  };
  ```

- [ ] **5.2** Register factory in Composer ctor.

- [ ] **5.3** Wire `configure_from_json` call in `realize_chord_parts_` (dynamic_cast pattern, same as StaticVoicingProfileSelector).

- [ ] **5.4** Create `test_jazz_turnaround_jitter.json` using this profile selector with priority range 0.2–0.8 and 3 inversion profiles.

- [ ] **5.5** Render, copy to main, Matt listens. Expect hear-able per-chord variation.

- [ ] **5.6** Commit:
  ```
  feat(voicing): RandomVoicingProfileSelector + demo patch
  ```

---

## Stage 6 — DriftVoicingProfileSelector

**Files:**
- Create: `engine/include/mforce/music/drift_profile selector.h`

- [ ] **6.1** Create `drift_profile selector.h`. Per-chord update:
  - `priority += gaussian(mean=0, stddev=priorityStepMax)`, clamp to `[priorityMin, priorityMax]`.
  - With probability `profileTransitionProb`, swap to next `inversionProfiles` entry.
  - Spread profiles same structure.

- [ ] **6.2** Gaussian: use `Randomizer::gaussian()` if present; else Box-Muller:
  ```cpp
  float u1 = rng_.value(); if (u1 < 1e-6f) u1 = 1e-6f;
  float u2 = rng_.value();
  return std::sqrt(-2.0f * std::log(u1)) * std::cos(2.0f * M_PI * u2);
  ```

- [ ] **6.3** Register, wire, make demo patch `test_jazz_turnaround_drift.json`.

- [ ] **6.4** Render, copy, listen.

- [ ] **6.5** Commit.

---

## Stage 7 — ScriptedVoicingProfileSelector

**Files:**
- Create: `engine/include/mforce/music/scripted_profile selector.h`

- [ ] **7.1** Create scripted profile selector. `profile_for_chord(idx)` returns `sequence_[idx % sequence_.size()]`.

- [ ] **7.2** Config JSON:
  ```json
  {
      "sequence": [
          {"priority": 0.0, "allowedInversions": [0, 1], "allowedSpreads": [0]},
          {"priority": 0.5, "allowedInversions": [0, 1, 2], "allowedSpreads": [0, 1]}
      ]
  }
  ```

- [ ] **7.3** Register, wire, demo patch.

- [ ] **7.4** Render, copy, listen.

- [ ] **7.5** Commit.

---

## Stage 8 — Listening pass + cleanup

- [ ] **8.1** Matt A/B compares `jazz_static` / `jazz_jitter` / `jazz_drift` / `jazz_scripted`. Notes what works and what's noise.

- [ ] **8.2** Tune profile selector defaults based on feedback.

- [ ] **8.3** Regression: re-render every pinned golden; confirm no regressions in patches that don't use profile selectors.

- [ ] **8.4** Copy final renders to main `renders/`.

- [ ] **8.5** Final commit if any changes. Otherwise note clean.

---

## Open-question answers to record at plan-start

- **Seed hashing**: `masterSeed ^ 0x566F6953u ^ (sectionIdx * 100 + partIdx)` — profile selector-specific salt `0x566F6953u` = ASCII "VoiS".
- **beat_in_bar / beat_in_passage**: derived from `pos` + `sec.meter.beats_per_bar()` + section start beat. Computed in `realize_chord_parts_`, passed to profile selector.
- **Empty-filter fallback**: return `{inv=0, spread=0, rootOctave=req.rootOctave}` candidate. Emit one stderr warning per Passage.
- **Profile Selector state across Passages**: profile selector instance is per-Passage (created fresh via factory). State doesn't persist.
- **Gaussian in DriftVoicingProfileSelector**: Box-Muller inline (no `Randomizer::gaussian()` in codebase).

## Success criteria

- Stage 1 renders are still musical (Matt listens, confirms).
- Stage 2–4 renders bit-identical to Stage 1 baselines (wherever profile selector is absent or Static).
- Stages 5–7 each produce audibly distinct variation on the jazz turnaround.
- No new `const_cast`s, no new magic strings beyond profile selector-name field.
- Backward-compat: test patches without profile selector config render identical to pre-profile selector behavior.
