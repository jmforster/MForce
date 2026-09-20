# Note Transitions Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Phrase-aware note delivery — `|` phrase markers in passage strings render as one continuous voice per phrase with per-note transition gestures (breath/tongue), on trombone and oboe.

**Architecture:** A phrase becomes the unit that acquires a voice and prepares the graph (today's "note"); in-phrase notes re-drive the living voice's PerformSource and push bindings without re-preparing. Transition names arrive as an interned id on a new `Note.transition` output pin; a new stateless NameGate node matches one name; Envelope gains a `trigger` input pin sampled at note Setup that restarts the envelope from its current value.

**Tech Stack:** C++17 engine (header-heavy, `engine/include/mforce/`), nlohmann::json patch loader, Dear ImGui/imnodes UI (`tools/mforce_ui/main.cpp`), CMake build from repo root, engine_tests harness (`tools/engine_tests/main.cpp`, hand-rolled asserts), Python null-gate/render tooling.

**Spec:** `docs/superpowers/specs/2026-09-19-note-transitions-design.md`

## Global Constraints

- No heap allocation in per-sample render loops (allocations at load or note Setup only).
- Explicit registries, no reflection (`SourceRegistry::instance().register_type`).
- Null gate must pass byte-identical over the frozen manifest before every commit that touches engine code (run per WORKFLOW.md; the manifest freeze is only re-done for DELIBERATE diffs, and this plan expects none).
- Feature at rest = byte-identical: a `|`-free passage and every existing patch must render bit-identically.
- Velocity/duration semantics untouched; `apply_articulation` untouched.
- Never stage or commit `docs/autonomy/GOALS.md` (Matt's uncommitted edit). Stage files by name; never `git add -A`.
- Build from repo root. UI exe may be locked by a running instance: rename-then-link per CLAUDE.md.
- Commits end with: `Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>`

---

### Task 1: `|` token in parse_passage

**Files:**
- Modify: `engine/include/mforce/music/parse_util.h:226-315`
- Test: `tools/engine_tests/main.cpp` (add `test_parse_passage_phrase_marks()`, call it from `main()` at line ~734)

**Interfaces:**
- Produces: `ParsedNote { float noteNumber; float durationSeconds; bool phraseStart; }` — `phraseStart` true on the first note of each phrase. Rests keep `kRestNote`. A rest also ends the current phrase (v1 rule: silence breaks the breath; `|` is the explicit marker). Existing callers that ignore `phraseStart` behave exactly as today.

- [ ] **Step 1: Write the failing test**

In `tools/engine_tests/main.cpp`, following the file's existing `void test_*()` + assert style:

```cpp
static void test_parse_passage_phrase_marks() {
    using namespace mforce;
    // No bars: every note starts its own phrase (compat: one-note phrases).
    auto plain = parse_passage("Cq Dq Eq", 4, 60.0f);
    assert(plain.size() == 3);
    for (auto& n : plain) assert(n.phraseStart);

    // Bars group; leading/trailing/doubled bars are no-ops.
    auto p = parse_passage("| Cq Dq | | Eq Fq Gq |", 4, 60.0f);
    assert(p.size() == 5);
    assert(p[0].phraseStart && !p[1].phraseStart);
    assert(p[2].phraseStart && !p[3].phraseStart && !p[4].phraseStart);

    // A rest ends the phrase: note after rest is a fresh phrase start.
    auto r = parse_passage("Cq Dq Rq Eq", 4, 60.0f);
    assert(r.size() == 4);
    assert(r[0].phraseStart && !r[1].phraseStart);
    assert(r[2].noteNumber == kRestNote);
    assert(r[3].phraseStart);
    std::printf("test_parse_passage_phrase_marks OK\n");
}
```

- [ ] **Step 2: Build engine_tests and verify it fails to compile** (`phraseStart` doesn't exist). Build from repo root with the project's usual cmake --build invocation for the engine_tests target.

- [ ] **Step 3: Implement**

In `parse_util.h`: add `bool phraseStart{true};` to `ParsedNote` (default true keeps aggregate-init callers safe — `{kRestNote, secs}` still compiles and rests never read it). In `parse_passage`, track `bool nextIsPhraseStart = true;`. In the token loop, before the size check:

```cpp
        if (token == "|") { nextIsPhraseStart = true; continue; }
```

In the rest branch, after pushing the rest: `nextIsPhraseStart = true;`. In the note push: `result.push_back({noteNumber, durationSeconds, nextIsPhraseStart}); nextIsPhraseStart = false;`. NOTE: `|` is size 1 — the `token.size() < 2` throw must come AFTER the `|` check.

- [ ] **Step 4: Build + run engine_tests, verify PASS.**
- [ ] **Step 5: Commit** `feat(parser): | phrase-boundary token in passage strings (spec §2)`

---

### Task 2: transition id through PerformSource + the Note node's transition pin

**Files:**
- Modify: `engine/include/mforce/render/perform_source.h` (NoteState, set_note, PerformOut)
- Modify: `engine/src/patch_loader.cpp:760-777` (PerformNode field branch) and `make_perform_context` (~line 1412)
- Modify: `engine/include/mforce/render/instrument.h` VoiceGraph (~line 129) — add `transOut`
- Modify: `tools/mforce_ui/main.cpp:568-578` (PerformNode output pins)
- Test: `tools/engine_tests/main.cpp` (`test_transition_field()`)

**Interfaces:**
- Consumes: nothing new.
- Produces: `NoteState.transitionId` (float, 0.0 = none); `PerformSource::set_note(float freqHz, float velocity, int durSamples, float durSeconds = 0.0f, std::shared_ptr<Envelope> bend = nullptr, float transitionId = 0.0f)`; `PerformOut::Field::Transition`; loader accepts PerformNode `field == "transition"` → shared `vg.transOut`; UI Note node shows a `transition` output pin (preview 0.0f).

- [ ] **Step 1: Failing test**

```cpp
static void test_transition_field() {
    using namespace mforce;
    auto ps = std::make_shared<PerformSource>();
    PerformOut out(ps, PerformOut::Field::Transition);
    assert(out.current() == 0.0f);                       // no note yet
    ps->set_note(440.0f, 0.8f, 48000, 1.0f, nullptr, 2.0f);
    assert(out.current() == 2.0f);
    ps->set_note(440.0f, 0.8f, 48000, 1.0f, nullptr);    // default arg
    assert(out.current() == 0.0f);
    std::printf("test_transition_field OK\n");
}
```

- [ ] **Step 2: Build, verify compile failure** (no `Field::Transition`).
- [ ] **Step 3: Implement**

`perform_source.h`: `NoteState` gains `float transitionId{0.0f};` with comment `// interned transition name (spec 2026-09-19-note-transitions §5); 0 = none/unknown, ids 1.. index the instrument's transitions[] vocabulary`. `set_note` gains the trailing default parameter and writes it. `PerformOut::Field` gains `Transition`; `read_()` gains `case Field::Transition: return ps_->note().transitionId;`.

`instrument.h` VoiceGraph: add `transOut` to the adapter member list (line ~129: `freqOut, velOut, wheelOut, pressOut, durOut, transOut;`).

`patch_loader.cpp` `make_perform_context`: create `vg.transOut = std::make_shared<PerformOut>(vg.performSource, PerformOut::Field::Transition);`. Check `PerformContext`'s definition (same file / header it lives in) — extend it with the new adapter the same way the other five travel. PerformNode branch: `else if (field == "transition") valueNodes[id] = perf->transOut;` and extend the unknown-field error text.

`mforce_ui/main.cpp:576`: after the duration pin, `outputs.emplace_back("transition", PinKind::Output, 0.0f);` with a one-line comment citing the spec. The per-field file node (`__perf_transition`) falls out of the generic `"__perf_" + p.name` machinery at line ~2792 — nothing to add, but VERIFY in Task 8's roundtrip.

- [ ] **Step 4: Build engine_tests + run: PASS. Build mforce_cli + mforce_ui: clean.**
- [ ] **Step 5: Run the null gate** (feature at rest — nothing reads the new field). Expected: all entries identical.
- [ ] **Step 6: Commit** `feat(engine): transition id on NoteState + Note-node transition pin (spec §5)`

---

### Task 3: play_phrase — the living voice

**Files:**
- Modify: `engine/include/mforce/render/instrument.h` (PitchedInstrument)
- Test: `tools/engine_tests/main.cpp` (`test_phrase_equals_long_note()`, `test_phrase_retunes()`)

**Interfaces:**
- Consumes: `set_note(..., transitionId)` from Task 2.
- Produces:
```cpp
struct PhraseNote {
  float noteNumber;
  float velocity;
  float durationSeconds;
  std::string transition;   // name; empty = none (id 0)
};
void play_phrase(const std::vector<PhraseNote>& notes, float startTime);
std::vector<std::string> transitionNames;   // instrument vocabulary, PitchedInstrument member
float transition_id(const std::string& name) const;  // 1-based index; 0 + one-shot stderr warn when unknown
```
`play_note` DELEGATES to `play_phrase` with a single PhraseNote (empty transition) — one render loop in the codebase; the PitchCurve* parameter stays on play_note only (phrases carry no bends in v1; assert/document).

- [ ] **Step 1: Failing tests**

Build a minimal in-memory patch the way existing engine_tests build instruments (follow the file's existing instrument-construction test if present; otherwise load `patches/baselines/` — pick one gate-stable baseline WITHOUT a duration/velocity-map wire, e.g. a plain sine+envelope baseline; check `patches/baselines/` and choose in implementation, record choice in the test comment):

```cpp
static void test_phrase_equals_long_note() {
    // Same patch loaded twice. A: play_note(60, .8, 1.0). B: play_phrase
    // {60,.8,0.5,""},{60,.8,0.5,"x"} — same pitch, same velocity.
    // With nothing wired to transition/trigger, buffers must be
    // byte-identical: the phrase IS one long note (spec gate 2).
    // Render both via Instrument::render into float buffers; memcmp.
}
static void test_phrase_retunes() {
    // play_phrase {60,.8,0.5,""},{67,.8,0.5,""} — the second half's
    // dominant frequency must track 67, proving mid-voice set_note
    // re-drives the pull chains. Zero-crossing count on each half.
}
```
Write them fully (real code, real asserts) against whichever construction idiom the file already uses.

- [ ] **Step 2: Build → fails (no play_phrase).**
- [ ] **Step 3: Implement in instrument.h**

Refactor `play_note`'s body into `play_phrase`:

```cpp
void play_phrase(const std::vector<PhraseNote>& pns, float startTime,
                 const PitchCurve* curve = nullptr) {
  if (pns.empty()) return;
  int vIdx = int(nextVoice % int(voicePool.size()));
  auto& vg = voicePool[size_t(vIdx)];
  nextVoice++;

  float totalSec = 0.0f;
  for (auto& p : pns) totalSec += p.durationSeconds;
  const int totalSamples = int(totalSec * float(sampleRate));

  // Phrase start = today's note start: bindings then prepare. The
  // envelope stage layout sees the PHRASE length — a phrase is one
  // long note to every duration-mode envelope (spec §4).
  float freq0 = note_to_freq(pns[0].noteNumber);
  int dur0 = int(pns[0].durationSeconds * float(sampleRate));
  apply_note_bindings(vg, freq0, pns[0].velocity, dur0,
                      pns[0].durationSeconds, curve,
                      transition_id(pns[0].transition));
  // gain (hiBoost formula) from note 1 — verbatim the play_note code.
  ...
  RenderContext ctx{ sampleRate };
  vg.source->prepare(ctx, totalSamples);
  for (auto& a : vg.advanceList) a->prepare(ctx, totalSamples);
  fire_triggers(vg);   // Task 6 wires this in; stub no-op until then

  // Boundary table in samples.
  ...
  // Render loop: verbatim play_note's loop over maxSamples derived from
  // totalSamples, PLUS: when i crosses boundary[k], call
  // advance_phrase_note(vg, pns[k]) BEFORE that sample's tick/next.
}

// Mid-phrase Setup: set_note + push bindings, NO prepare. Settings
// (isSetting) bindings are SKIPPED mid-phrase — set_setting rebuilds
// state at prepare and must not run mid-render (v1 decision, spec
// verify-flag (b)); non-setting deliveries are pointer swaps and safe.
void advance_phrase_note(VoiceGraph& vg, const PhraseNote& p) {
  float freq = note_to_freq(p.noteNumber);
  int durS = int(p.durationSeconds * float(sampleRate));
  if (vg.performSource)
    vg.performSource->set_note(freq, p.velocity, durS, p.durationSeconds,
                               nullptr, transition_id(p.transition));
  for (auto& b : vg.pushBindings) {
    if (b.isSetting) continue;
    b.chain->next();
    float v = b.chain->current();
    b.cs->set(v);
    b.consumer->set_param(b.paramName, b.cs);
    if (vg.topMultiplex && !b.targetNodeId.empty())
      vg.topMultiplex->set_clone_param(b.targetNodeId, b.paramName, v);
  }
  fire_triggers(vg);   // Task 6
}
```

Details the implementer must honor: (a) `apply_note_bindings` gains the trailing `float transitionId = 0.0f` parameter passed into set_note — its other two call sites (`prepare_voice_at`, and play_note if any remnant) pass nothing; (b) the render/ring/capture/containment code is MOVED, not duplicated — `play_note` becomes a 3-line delegation `play_phrase({{noteNumber, velocity, duration, {}}}, startTime, curve);` (`PhraseNote.transition` empty → id 0); (c) per-note gain: v1 keeps the PHRASE gain from note 1's velocity (velocity mid-phrase still reaches the graph via velOut for patches that wire it — the trombone does; the voice-mix gain is per-voice and cannot change mid-buffer without a zipper — document inline); (d) boundary crossing happens before `performSource->tick()` for that sample so the new NoteState is visible to the whole sample; (e) `transition_id` warns once per unknown name per instrument (a `std::set<std::string>` member — load/note-rate only, never in the sample loop).

- [ ] **Step 4: Build + run engine_tests: both new tests PASS.**
- [ ] **Step 5: Run the null gate.** play_note now routes through play_phrase — this is the step that proves the refactor is byte-clean. Expected: all identical. Any DIFF = bug in the refactor; fix before proceeding.
- [ ] **Step 6: Commit** `feat(engine): play_phrase — phrase-as-gate-span delivery, play_note delegates (spec §4)`

---

### Task 4: Envelope trigger input + retrigger()

**Files:**
- Modify: `engine/include/mforce/core/envelope.h`
- Test: `tools/engine_tests/main.cpp` (`test_envelope_retrigger()`)

**Interfaces:**
- Consumes: nothing new (self-contained).
- Produces: `Envelope::trigger_` (`std::shared_ptr<ValueSource>`, read at Setup only — NEVER pulled in next()); `"trigger"` in `param_descriptors()`/`set_param`/`get_param`; preserved across `replace_stages`; `void Envelope::retrigger()` — re-enter stage 0 anchored at the current output value.

- [ ] **Step 1: Failing test**

```cpp
static void test_envelope_retrigger() {
    using namespace mforce;
    // Seconds-mode 3-stage gesture: 0.01s to 0.2, 0.02s to 1.0, expand@1.0.
    // (Build via the same make_adsr_abs / stage-construction idiom the
    // file or envelope_presets.h already uses — copy an existing test's
    // construction.) prepare(48000 frames = 1.0s).
    // Run 24000 samples: output has settled at 1.0 (expand).
    // retrigger(): next sample continues FROM ~1.0 (no step > 0.02),
    // then dips to ~0.2 within 0.01s, recovers to 1.0, and HOLDS 1.0
    // through the last frame (expand must survive re-entry — the
    // "0 past the last stage" rule must not fire while frames remain).
}
```
Assert three things numerically: no discontinuity at the retrigger sample; the dip reaches min within its authored time; the final 1000 samples are all ≈1.0.

- [ ] **Step 2: Build → fails (no retrigger).**
- [ ] **Step 3: Implement**

Members: `std::shared_ptr<ValueSource> trigger_;` next to minValue_/maxValue_ with the comment `// Setup-sampled restart input (spec 2026-09-19-note-transitions §5). Read via current() at note Setup by the instrument's trigger bindings; NEVER pulled in next() — keep trigger chains stateless (NameGate/PerformOut).` Extend `param_descriptors()` with `{"trigger", 0.0f, 0.0f, 1.0f}`, `set_param`/`get_param` with the name, and `replace_stages` with `fresh.trigger_ = std::move(trigger_);`.

`retrigger()` — model on `gate_release()`'s anchoring (envelope.h:85-115), jumping to stage 0 instead of the release stage:

```cpp
  // Re-enter stage 0 NOW, anchored to the current output (click-free) —
  // the transition-gesture restart (spec §5). Uses the gate-anchor
  // machinery: the jumped-to stage interpolates from gateFrom_.
  void retrigger() {
    if (stages_.empty() || stageCounts_.empty()) return;
    gateFrom_   = cur_;
    gateActive_ = true;
    gateStage_  = 0;
    stageStart_ = ptr_ + 1;
    currStage_  = 0;
    stageEnd_   = stageStart_ + stageCounts_[0];
  }
```

THEN read the raw stage-evaluation code (envelope.h ~lines 327-380) end to end and verify against it: (a) that stage progression after re-entry walks 0→1→…→expand using `stageCounts_` correctly from the shifted `stageStart_`; (b) what happens after the final stage completes early relative to the prepared frame count — if output drops to 0 past the last stage while frames remain, extend the expand handling so a re-entered expand stage holds to the buffer end (the test's third assert catches this); (c) that `gateActive_` reuse doesn't collide with a later real `gate_release()` (a gesture envelope has expand-last so gate_release returns 0 — verify and note inline). Adjust the implementation to what the code actually does; the test is the contract.

- [ ] **Step 4: Build + run engine_tests: PASS.**
- [ ] **Step 5: Null gate** (trigger_ unset everywhere = dormant). Expected identical.
- [ ] **Step 6: Commit** `feat(engine): Envelope.trigger param + retrigger-from-current (spec §5)`

---

### Task 5: NameGate node + vocabulary interning

**Files:**
- Create: `engine/include/mforce/core/name_gate.h`
- Modify: `engine/src/source_registrations.cpp` (register the type)
- Modify: `engine/src/patch_loader.cpp` (construction branch; instrument `transitions` array; allowlist at line ~511 if the unknown-key warn flags `name`)
- Test: `tools/engine_tests/main.cpp` (`test_name_gate()`)

**Interfaces:**
- Consumes: `PerformOut::Field::Transition` values (Task 2).
- Produces:
```cpp
struct NameGate final : ValueSource {
  std::shared_ptr<ValueSource> in_;
  float targetId{-1.0f};        // resolved at load; -1 never matches
  std::string name;             // authored string, kept for save/lint
  // param_descriptors: {"in"}; setting_descriptors: none (name is a
  // loader-special string, settings are Float/Int/Bool only).
};
```
Loader: instrument JSON `"transitions": ["tongue", ...]` → `PitchedInstrument::transitionNames` (Task 3's member); NameGate params `{"name": "tongue", "in": {...}}` → resolve `targetId = 1 + index`, or warn once + `-1` when the name is not in the vocabulary (reuse the backlog-67 once-per-(type,key) warn idiom).

- [ ] **Step 1: Failing test**

```cpp
static void test_name_gate() {
    using namespace mforce;
    auto ps = std::make_shared<PerformSource>();
    auto in = std::make_shared<PerformOut>(ps, PerformOut::Field::Transition);
    auto ng = std::make_shared<NameGate>(48000);
    ng->set_param("in", in);
    ng->targetId = 2.0f;
    ps->set_note(440, .8f, 100, 0, nullptr, 2.0f);
    assert(ng->current() == 1.0f && ng->next() == 1.0f);
    ps->set_note(440, .8f, 100, 0, nullptr, 1.0f);
    assert(ng->current() == 0.0f);
    ng->targetId = -1.0f;                    // unresolved never matches
    ps->set_note(440, .8f, 100, 0, nullptr, 0.0f);
    assert(ng->current() == 0.0f);
    std::printf("test_name_gate OK\n");
}
```

- [ ] **Step 2: Build → fails.**
- [ ] **Step 3: Implement**

`name_gate.h`: stateless; `current()` computes live from `in_->current()` (the PerformOut idiom — perform_source.h:98-130 documents why: RefSource copies and Setup-time reads must see the live value); `next()` pulls `in_` then returns the same computation (safe: PerformOut is idempotent; and if a patch wires NameGate into an audible path it advances normally). Match by `std::lround` on both sides; `targetId < 0` → 0. `type_name() = "NameGate"`, category Modulator, one ParamDescriptor `{"in", 0.0f, 0.0f, 0.0f}`, set_param/get_param for "in". Constructor takes sampleRate like every registered type.

`source_registrations.cpp`: register alongside the other Modulators, same `register_type` shape as the file's entries.

`patch_loader.cpp`: (a) in the instrument block parse (where `gateable`/`volume`/`polyphony` are read — find the block, it sets `inst->` members), read `"transitions"` into `inst->transitionNames`; (b) a construction branch BEFORE the generic path:

```cpp
        else if (type == "NameGate") {
            auto ng = std::make_shared<NameGate>(sampleRate);
            if (pp && pp->contains("name")) {
                ng->name = (*pp)["name"].get<std::string>();
                // vocabulary lookup — transitionNames captured by the
                // per-voice build lambda/context the same way `perf` is
                for (size_t k = 0; k < transitionNames.size(); ++k)
                    if (transitionNames[k] == ng->name)
                        { ng->targetId = float(k + 1); break; }
                if (ng->targetId < 0.0f) warn_once("NameGate", ng->name,
                    "names a transition the instrument's transitions[] "
                    "does not declare — it will never fire");
            }
            if (pp) wire_params_generic(*ng, *pp, valueNodes, &usage);
            valueNodes[id] = ng;
        }
```
Adapt `warn_once` to the actual backlog-67 warn helper's name/signature (grep `warn` in the file); adapt vocabulary access to how the graph-build function receives instrument context (the `perf` context pointer is the model). If the unknown-key warn machinery flags `"name"` as an unknown param key for NameGate, add it to the allowlist the way line ~511's list handles `type/id/seed/dynamicPins`.

- [ ] **Step 4: Build + run engine_tests: PASS. Null gate: identical (no patch has a NameGate).**
- [ ] **Step 5: Commit** `feat(engine): NameGate node + instrument transitions vocabulary (spec §5)`

---

### Task 6: trigger bindings — Setup fires the gestures

**Files:**
- Modify: `engine/src/patch_loader.cpp` (collect per-voice trigger bindings after graph build, near the advanceList collection at ~line 1520)
- Modify: `engine/include/mforce/render/instrument.h` (VoiceGraph member + `fire_triggers`)
- Test: `tools/engine_tests/main.cpp` (`test_phrase_fires_trigger()`)

**Interfaces:**
- Consumes: Tasks 2-5 (transition pin, play_phrase, retrigger, NameGate).
- Produces: `VoiceGraph::triggerBindings` — `std::vector<Envelope*>` of envelopes in this voice whose `trigger_` is wired (raw pointers; the graph owns lifetime, same rationale as CaptureEntry); `PitchedInstrument::fire_triggers(VoiceGraph&)` — for each, `if (env->trigger_->current() != 0.0f) env->retrigger();` — called at phrase start (after prepare) and at every in-phrase boundary (Task 3's stubs go live).

- [ ] **Step 1: Failing test**

Build (in-memory JSON string, loaded via `load_instrument_patch_json`) a minimal instrument patch: sine → multiply by gesture-envelope → output; gesture envelope seconds-mode dip (1.0 → 0.2 → 1.0, expand-last), its `trigger` param wired from a NameGate("tongue") reading a PerformNode field="transition"; instrument block `"transitions": ["tongue"]`. Then:

```cpp
    // play_phrase: {A4, .8, 0.5, ""}, {A4, .8, 0.5, "tongue"}.
    // First half: no dip (gesture idles at 1.0 — trigger id 0 at start).
    // Second half: RMS in the 30ms after the boundary is measurably
    // below the 30ms before it (the dip fired), and the final 100ms
    // is back at full level.
```
Write the JSON inline in the test (the file already builds patches from strings or files — follow its idiom).

- [ ] **Step 2: Build → fails (no triggerBindings/fire_triggers).**
- [ ] **Step 3: Implement.** Loader: after the voice's graph is built, iterate `g.valueNodes`, `dynamic_cast<Envelope*>`, collect those with `trigger_` set into `vg.triggerBindings`. instrument.h: the member + `fire_triggers` + replace Task 3's stubs. Setup ordering at a boundary is fixed and load-bearing: `set_note` FIRST (new transitionId visible), push bindings second, `fire_triggers` LAST.
- [ ] **Step 4: Build + run engine_tests: PASS (including Tasks 1-5 tests — no regressions). Null gate: identical.**
- [ ] **Step 5: Commit** `feat(engine): Setup-sampled trigger bindings — transitions fire gestures (spec §5)`

---

### Task 7: score-block phrases (CLI parity for gen scripts and gates)

**Files:**
- Modify: `engine/src/patch_loader.cpp:1528-1552` (score scheduling)
- Test: engine-render level — a baseline patch pair under `patches/baselines/` (new files `phrase_smoke_{flat,phrased}.json`) + a python or CLI byte-compare, following how existing repro pairs in `patches/baselines/` are exercised

**Interfaces:**
- Consumes: play_phrase (Task 3).
- Produces: score note JSON gains optional `"phrase": "cont"` — a note marked `cont` joins the previous note's phrase (its `time` is ignored; it starts where the previous note ends). Transition names: optional `"transition": "tongue"`; DEFAULT emission when absent = the Performer rule: first-of-phrase `"breath"`, continuation `"tongue"` (one function, commented as the spec §3 seam). Notes without `"phrase"` are one-note phrases routed through the existing NotePerformer path UNCHANGED (byte-compat); a `cont` group bypasses NotePerformer (articulation/ornament unsupported inside phrases — document inline, throw if present on a `cont` note so the failure is loud, not silent).

- [ ] **Step 1: Write the failing check.** Create `patches/baselines/phrase_smoke_flat.json` (any simple tracked baseline graph + score: two notes back-to-back, same pitch) and `phrase_smoke_phrased.json` (identical, second note `"phrase":"cont"`, both notes `"transition"` ABSENT, patch has no NameGates). Render both via mforce_cli; the phrased one must be byte-identical to a third render of one note with the summed duration (spec gate 2 at CLI level). Script the compare the way the null gate compares (hash), as a small addition to engine_tests or a python one-liner in the task — implementer's choice, but it must run in one command.
- [ ] **Step 2: Run → phrased file fails to load (unknown key or ignored) or renders re-ignited → check fails.**
- [ ] **Step 3: Implement** the grouping in the score loop: accumulate `Note`+start into a pending vector; flush to `inst->play_phrase` when the next note is not `cont` (or at loop end); non-phrase notes keep the NotePerformer path verbatim. Total-duration computation (line 1554-1571) already sums per-note times/durations — verify `cont` notes' effective start times land in `maxEnd` correctly (they start at prev end; compute and use that).
- [ ] **Step 4: Run the check: byte-identical. Null gate: identical (no tracked patch uses "phrase").** The two new baselines join the tracked baselines; note them for the NEXT deliberate manifest refreeze but do NOT refreeze in this plan (they're new files, not diffs — confirm the gate tooling's behavior with new untracked-in-manifest patches and follow it).
- [ ] **Step 5: Commit** `feat(loader): score-block phrase grouping ("phrase":"cont") via play_phrase (spec §3)`

---

### Task 8: UI — transport phrases, NameGate editing, roundtrip

**Files:**
- Modify: `tools/mforce_ui/main.cpp`: SchedNote struct (grep its definition), transport passage case (:8136-8167), `generate_unified` (:4219), NameGate settings panel (model: NT_PARAMETER name editing at :9951-9961), node save/load/clipboard for NameGate's `name` param (grep `paramName` serialization for the Parameter node and mirror every site), Envelope trigger pin (verify the generic descriptor path picks up `"trigger"` — envelope pins may be special-cased; if so add the input pin where its pins build)

**Interfaces:**
- Consumes: everything engine-side.
- Produces: `SchedNote` gains `bool phraseStart{true}; std::string transition;`; the passage case groups on `phraseStart` (rests break phrases — parser already enforces) and applies the emission rule (first `"breath"`, rest `"tongue"`) — same seam comment; `generate_unified` batches consecutive `!phraseStart` notes into one `play_phrase` call (grouped notes are back-to-back by construction; keep `startSeconds` of the group head). NameGate node: created from the registry list like any node, plus a `name` text field in its settings panel and full save/load/copy-paste of that string.

- [ ] **Step 1: Manual-failing baseline.** Build UI (rename-then-link if locked). Load a wind library patch, enter `| Eq Eq | Fq Gq` — today it renders 4 independent notes. Record the generated WAV hash (the UI writes the buffer via existing save/gencheck tooling) for comparison.
- [ ] **Step 2: Implement** the four sites. Grouping code in generate_unified:

```cpp
        // Group SchedNotes into phrases (spec §4): a run of !phraseStart
        // notes extends the phrase started by the last phraseStart note.
        std::vector<std::vector<PhraseNote>> phrases;
        std::vector<float> phraseStarts;
        for (const auto& sn : notes) {
            if (sn.phraseStart || phrases.empty()) {
                phrases.push_back({});
                phraseStarts.push_back(sn.startSeconds);
            }
            phrases.back().push_back({sn.noteNumber, sn.velocity,
                                      sn.durationSeconds, sn.transition});
        }
        for (size_t k = 0; k < phrases.size(); ++k)
            pitched->play_phrase(phrases[k], phraseStarts[k]);
```
Note-mode and chords/drums paths pass single-note scheds with `phraseStart=true` — untouched behavior by construction.
- [ ] **Step 3: Verify in the running UI:** the `|` passage now sounds phrased (2 attacks, not 4) on a patch with no NameGates (plain retunes); a `|`-free passage renders identical to Step 1's hash. Drop a NameGate node: it appears (registry-generic), takes a name string in settings, wires Note.transition → NameGate.in → Envelope.trigger.
- [ ] **Step 4: Roundtrip:** save a patch carrying NameGate + trigger wire + a `transition` Note pin wire; reload; the graph, the name string, and all three wires survive; the standing roundtrip harness (grep `roundtrip` in tools/) passes 100% on its corpus.
- [ ] **Step 5: `--gencheck` parity** on the Task 7 phrased baseline: UI buffer vs CLI render agree to the established 1e-6.
- [ ] **Step 6: Commit** `feat(ui): | phrasing in transport, NameGate editing, transition pin roundtrip (spec §2,§5)`

---

### Task 9: teach the instruments, render the ears queue

**Files:**
- Create: `patches/audition/articulation1/trombone_tongue.json` (copy of `patches/audition/trombone1/trombone_attempt4.json` + vocabulary/NameGate/gesture wiring — attempt 4 is the current candidate, commit bc24463)
- Create: `patches/audition/articulation1/oboe_tongue.json` (copy of `patches/library/winds/oboe1.json` + same treatment; the library original is NOT touched)
- Create: `tools/gen_articulation1.py` (renders the queue; owns `renders/dsp/audition/articulation1/` per WORKFLOW gen-scripts-own-dirs)
- Create: `renders/dsp/audition/articulation1/README.md` (generated by the script, plain language)

**Interfaces:**
- Consumes: the whole feature.
- Produces: the REVIEW-able queue. OTJ passage string (theme in C, house octave 4, quarter=0.4s at bpm 150 — the classic phrasing, 4 phrases):
```
| Eq Eq Fq Gq Gq Fq Eq Dq | Cq Cq Dq Eq E. Dq Dh |
| Eq Eq Fq Gq Gq Fq Eq Dq | Cq Cq Dq Eq D. Cq Ch |
```
(Adjust duration-token spellings to what `parse_duration` actually accepts — read it — and keep the dotted values right.)

- [ ] **Step 1: Teach the trombone copy.** Instrument block gains `"transitions": ["tongue"]`. Add: NameGate("tongue") with `in` ← a PerformNode field="transition"; a seconds-mode gesture Envelope (stages 1.0 → 0.2 over 15 ms → back to 1.0 over 25 ms, expand-last at 1.0; `trigger` ← NameGate) multiplied into the breath/pressure path — find the exact node the velocity→pressure map drives in trombone_attempt4.json (gen_trombone3/4.py documents the graph; the breath input is the pressure-map CurveNode's output path) and insert the multiply immediately after it. Same pattern for the oboe copy on ITS breath input (oboe1's loop drive — LOOP_PATCH_ANATOMY.md names it).
- [ ] **Step 2: gen_articulation1.py renders 6 cells:** `otj_trombone_flat.wav` (same string, `|` stripped), `otj_trombone_phrased.wav`, `otj_oboe_flat.wav`, `otj_oboe_phrased.wav`, `phrase_hold_trombone.wav` (one long phrase: 8 repeated A#3 quarters tongued — the isolated consonant test), `phrase_hold_oboe.wav`. Render via mforce_cli with score-block phrases (Task 7) — the script converts the passage string to score JSON with `"phrase"` marks (parse in python; keep the emission rule identical: first breath, rest tongue). Queue loudness calibrated to oboe1's 0.1761 sounding rms per the standing rule; level-ceiling check per WORKFLOW.
- [ ] **Step 3: Listen-check gates (machine):** phrased vs flat must differ audibly (feature-audibility gate — README prints the per-cell RMS/centroid deltas); the phrased trombone must show exactly 2 ignition transients per phrase line where flat shows 8 (onset count via the existing lock/onset metrics in gen_trombone tooling — import, don't rewrite).
- [ ] **Step 4: Commit** patches + tool `feat(dsp): articulation1 — trombone+oboe taught tongue, OTJ phrased queue` (renders stay untracked/gitignored as always).

---

### Task 10: bookkeeping + report

**Files:**
- Modify: `docs/autonomy/dsp/REVIEW.md` (new Awaiting entry 73, plain language, THE QUESTION = "does the phrased OTJ sound like one breath per line?", the one cell = `otj_trombone_phrased.wav` A/B'd against `otj_trombone_flat.wav`)
- Modify: `docs/autonomy/STATUS.md` (top update: what shipped, gates, the two v1 exclusions Matt should know — live legato pends 53, settings-type dynamicPins hold phrase-start values mid-phrase)
- Modify: `docs/autonomy/dsp/BACKLOG.md` (GOALS Articulations item decomposition note; 53 gains "phrase delivery landed — live legato = wire overlap runs into play_phrase semantics")
- Create: `docs/autonomy/dsp/reports/2026-09-19-note-transitions.md` (the run report: every gate with numbers, every decision with spec cites)

- [ ] **Step 1: Write all four documents.** REVIEW entry follows the plain-language rule (no "centroid" without translation).
- [ ] **Step 2: Final full-gate pass, in order:** engine_tests all green; null gate identical; roundtrip 100%; gencheck parity 1e-6 on the phrased baseline.
- [ ] **Step 3: Commit** `notes transitions v1: phrased OTJ on trombone+oboe (REVIEW 73)` — docs + any stragglers, GOALS.md excluded as always.

## Self-review record

- Spec coverage: §2→Task 1; §5 transition slot/pin→Task 2; §4 delivery→Task 3 (+7 CLI, +8 UI); §5 trigger/retrigger→Task 4; §5 NameGate/vocabulary/lint-warn→Task 5; §5 Setup firing→Task 6; §3 emission seam→Tasks 7+8 (one rule, two callers, both commented as the seam); §6 teaching→Task 9; §7 gates→distributed per task + Task 10 final pass; §7 verify-flags: (a) advance-order → Task 3 boundary-before-tick decision + null gate, (b) prepare-path reuse → Task 3 explicitly shares apply_note_bindings minus prepare with the isSetting exception recorded, (c) trigger read timing → Task 6 fixed ordering. §4 live rule: deferred by spec cut line — no task, by design.
- Known deviation from spec, recorded: spec §5 said per-note maps "re-evaluate" mid-phrase; settings-type (isSetting) bindings are excluded in v1 because set_setting rebuilds state at prepare (instrument.h:101-104's contract). Non-setting bindings and all pull-chains re-evaluate. STATUS/report must state it (Task 10); trombone's maps are pull-chains + gesture path, so the v1 target is unaffected — Task 9's Step 3 metrics would expose it otherwise.
- Type consistency: PhraseNote/transition_id/fire_triggers/triggerBindings/transOut names consistent across Tasks 2-8.
