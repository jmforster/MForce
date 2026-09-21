# Note Onsets v2 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace v1's phrase-vector delivery with per-note `onset`/`hold` semantics: atomic notes, resumable voices, note-timebased envelopes, glided slurs, and the `sustaining` declaration.

**Architecture:** `PerformedNote` becomes the Performance→Realization boundary object. Delivery is per-note against a possibly-open line voice: `hold: true` leaves the gate open and suspends the voice warm; the next note resumes it (glide on pitch change, onset gesture fires); `hold: false` releases with the releasing note's own release scale. `play_phrase` and `prepare(total)` are deleted. Non-sustaining instruments never receive hold, so unphrased and piano behavior is byte-identical by construction.

**Tech Stack:** C++17 engine headers (`engine/include/mforce/`), nlohmann::json loader, ImGui UI (`tools/mforce_ui/main.cpp`), engine_tests (CHECK macros), python null gate.

**Spec:** `docs/superpowers/specs/2026-09-20-note-onsets-v2-design.md` (supersedes the 2026-09-19 v1 spec; §9 records v1's divergence).

## Global Constraints

- Null gate (`python tools/null_gate_perform_source.py --jobs 8`) byte-identical 79/79 after EVERY engine-touching task.
- Feature at rest byte-identical: unphrased renders, `|`-free passages, and any non-sustaining patch must not change by one bit.
- No heap allocation in per-sample loops (note-rate allocation is fine).
- Perform-tier language says `hold`; only Render-land internals say gate.
- `music::Note` gains nothing; onset/hold live only on `PerformedNote`.
- Never stage/commit `docs/autonomy/GOALS.md`; stage files by name.
- UI exe may be locked: rename-then-link per CLAUDE.md.
- Commits end with: `Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>`
- v1 back-compat is explicitly NOT owed: the `"phrase":"cont"` score key and `transition*` names die; only gitignored audition patches carry them and Task 8 regenerates those.

---

### Task 1: the rename sweep — transition → onset

**Files:**
- Modify: `engine/include/mforce/render/perform_source.h` (NoteState.transitionId → onsetId; `PerformOut::Field::Transition` → `Onset`; set_note param name)
- Modify: `engine/include/mforce/render/instrument.h` (transitionNames → onsetNames; transition_id() → onset_id(); PhraseNote.transition → .onset — struct dies in Task 4, rename keeps intermediate states coherent; warn text)
- Modify: `engine/src/patch_loader.cpp` (PerformNode field `"transition"` → `"onset"` + error text; instrument key `"transitions"` → `"onsets"`; NameGate vocabulary lookup + warn text)
- Modify: `tools/mforce_ui/main.cpp` (Note node output pin `"transition"` → `"onset"`; gencheck/emission strings — they change again in Task 6, keep compiling now)
- Test: `tools/engine_tests/main.cpp` (mechanical string/name updates in run_transition_field_tests → run_onset_field_tests, run_name_gate_tests, run_phrase_trigger_tests)

**Interfaces:**
- Consumes: v1 as committed (ecba96e lineage).
- Produces: `NoteState.onsetId`; `PerformOut::Field::Onset`; loader accepts PerformNode `field:"onset"` and instrument `"onsets": [...]`; UI pin `onset`. NO alias for the old names — old strings throw/warn loudly.

- [ ] **Step 1:** Apply the renames mechanically (grep `transition` case-insensitively across the five files; every hit is either renamed or is prose/comment updated). Update the three engine_tests functions' JSON strings (`"field": "onset"`, `"onsets": ["tongue"]`) and identifiers.
- [ ] **Step 2:** Build engine_tests + mforce_cli + mforce_ui: zero errors. Run engine_tests: ALL PASS (494 checks).
- [ ] **Step 3:** Null gate: 79/79 identical.
- [ ] **Step 4:** Commit `refactor(engine,ui): transition -> onset everywhere (spec 2026-09-20 §8)`.

### Task 2: PerformedNote + the play_note signature sweep

**Files:**
- Modify: `engine/include/mforce/render/instrument.h` (struct + signature; play_note body still delegates to the v1 single-note phrase path for now — byte-neutral refactor)
- Modify: `engine/src/patch_loader.cpp` (score loader call site; NotePerformer's Note delivery — find `perform_note` in `engine/include/mforce/music/` and update where it calls play_note)
- Modify: `tools/mforce_ui/main.cpp` (generate_unified, gencheck, any transport play path calling play_note)
- Modify: `tools/mforce_cli/main.cpp` (melody/chords/compose paths that call play_note — grep `play_note(`)
- Test: `tools/engine_tests/main.cpp` (run_phrase_tests call sites)

**Interfaces:**
- Produces (verbatim from spec §1):
```cpp
struct PerformedNote {
  float noteNumber;
  float velocity;
  float duration;          // seconds
  float onsetId{0.0f};     // interned; 0 = none
  bool  hold{false};
  const PitchCurve* curve{nullptr};
};
void play_note(const PerformedNote& pn, float startTime);
```
The loose-argument overload is DELETED, not deprecated. `onsetId` is the interned float; callers that have a NAME call `onset_id(name)` themselves (keeps the struct dumb). `hold` is carried but ignored until Task 4.

- [ ] **Step 1:** Add the struct + new signature; body = verbatim the old body reading fields from `pn`. Delete the old overload. Sweep every call site the compiler finds (build until clean; the compiler is the checklist).
- [ ] **Step 2:** engine_tests: update run_phrase_tests to the struct where it calls play_note; ALL PASS.
- [ ] **Step 3:** Null gate 79/79. Gencheck parity spot-check on `patches/baselines/phrase_smoke_phrased.json` (still v1 semantics this task — unchanged output).
- [ ] **Step 4:** Commit `refactor(engine): PerformedNote boundary object; play_note(PerformedNote&, start) (spec §1)`.

### Task 3: Envelope — release re-layout + engine-side envelope collection

**Files:**
- Modify: `engine/include/mforce/core/envelope.h`
- Modify: `engine/src/patch_loader.cpp` (collect ALL envelopes per voice)
- Modify: `engine/include/mforce/render/instrument.h` (VoiceGraph member)
- Test: `tools/engine_tests/main.cpp` (`run_release_relayout_tests`)

**Interfaces:**
- Produces: `int Envelope::gate_release(int releaseRefFrames = -1)` — when `releaseRefFrames >= 0` and `!absolute_time`, the release stage's count is recomputed as `percent * (releaseRefFrames/sr) * timeScale`, clamped by the stage's minSec/maxSec, BEFORE the jump (seconds-mode stages keep their literal length; the anchor behavior is untouched). Existing callers (live path) pass nothing = today's behavior bit-for-bit.
- Produces: `VoiceGraph::allEnvelopes` (`std::vector<Envelope*>`) — every envelope reachable in the voice graph, collected at load by an engine-side walk (RefSource-following, input+param descriptors, cycle-guarded — mirror the UI's `collect_envelopes` at tools/mforce_ui/main.cpp:4604, but engine-side in the loader where `g.valueNodes` already enumerates every node: `dynamic_cast<Envelope*>` over `g.valueNodes` values, exactly like the Task-6-v1 triggerBindings collection two lines above it). A voice whose output is a MultiplexSource keeps `allEnvelopes` from valueNodes as-is; hold-gating on Multiplex patches is untested territory — Task 4 warns once and degrades hold to false for a voice with empty `allEnvelopes`.

- [ ] **Step 1: Failing test**
```cpp
static void run_release_relayout_tests() {
    const int sr = 48000;
    // adsr-ish: attack 10%, decay 10%, expand, release 25% (percent mode)
    Envelope env(sr);
    env.add_stage({{0.0f, 1.0f, RampType::Linear, 0.0f}, 0.10f, 0.0f, 0.0f});
    env.add_stage({{1.0f, 0.7f, RampType::Linear, 0.0f}, 0.10f, 0.0f, 0.0f});
    env.add_stage({{0.7f, 0.7f, RampType::Linear, 0.0f}, 0.0f,  0.0f, 0.0f});
    env.add_stage({{0.7f, 0.0f, RampType::Linear, 0.0f}, 0.25f, 0.0f, 0.0f});
    env.set_gated(true);
    env.prepare(RenderContext{sr}, int(0.4f * sr));      // quarter note
    for (int i = 0; i < int(0.35f * sr); ++i) env.next(); // into the hold
    // Release re-referenced to a HALF note: 25% of 0.8 s = 0.2 s.
    int rem = env.gate_release(int(0.8f * sr));
    CHECK(std::abs(rem - int(0.25f * 0.8f * sr)) <= 2);
    // Default arg keeps old behavior: fresh envelope, no ref.
    Envelope e2(sr);
    e2.add_stage({{0.0f, 1.0f, RampType::Linear, 0.0f}, 0.10f, 0.0f, 0.0f});
    e2.add_stage({{1.0f, 1.0f, RampType::Linear, 0.0f}, 0.0f,  0.0f, 0.0f});
    e2.add_stage({{1.0f, 0.0f, RampType::Linear, 0.0f}, 0.25f, 0.0f, 0.0f});
    e2.set_gated(true);
    e2.prepare(RenderContext{sr}, int(0.4f * sr));
    for (int i = 0; i < int(0.2f * sr); ++i) e2.next();
    int rem2 = e2.gate_release();
    CHECK(std::abs(rem2 - int(0.25f * 0.4f * sr)) <= 2);
}
```
- [ ] **Step 2:** Build → fails (no parameter). 
- [ ] **Step 3:** Implement: in `gate_release(int releaseRefFrames = -1)`, before computing `relStage` counts, if `releaseRefFrames >= 0 && !absolute_time` recompute `stageCounts_[relStage]` from `stages_[relStage].percent * (float(releaseRefFrames)/sampleRate_) * timeScale_`, clamped `[minSec, maxSec>0?maxSec:∞]`, `lround(* sampleRate_)`. (Also recompute any later stages the same way — the damper-choke class after the release.) Loader: populate `vg.allEnvelopes` beside the triggerBindings loop, both instrument sites.
- [ ] **Step 4:** engine_tests ALL PASS; null gate 79/79 (parameter unused by any caller yet).
- [ ] **Step 5:** Commit `feat(engine): gate_release(releaseRefFrames) re-layout + per-voice envelope collection (spec §5)`.

### Task 4: resumable voices — per-note delivery with hold

The core. `play_phrase` and `prepare(total)` die here.

**Files:**
- Modify: `engine/include/mforce/render/instrument.h`
- Test: `tools/engine_tests/main.cpp` (`run_hold_delivery_tests`; retire `run_phrase_tests`'s vector API usage, keep its assertions re-expressed per-note)

**Interfaces:**
- Consumes: PerformedNote (T2), gate_release ref + allEnvelopes (T3), onset machinery (T1/v1).
- Produces, inside PitchedInstrument:
```cpp
struct HeldLine {                  // one mono line (v2 scope)
  bool  open{false};
  int   vIdx{-1};
  float startTime{0.0f};           // timeline seconds of line start
  std::vector<float> buf;          // samples rendered since line start
  float gain{1.0f};
  float lastFreq{0.0f};
};
HeldLine line_;
float glideSec{0.015f};            // loader sets from instrument "glideMs"
void  finish_line();               // release + tail + add_rendered + close
void  finish_open_lines() { if (line_.open) finish_line(); }
```
`play_note(pn, start)` behavior (spec §4, exactly):
1. `line_.open`? **continue**: if `freq != line_.lastFreq`, build a glide bend — a seconds-mode Envelope, stages `{semis→0 over glideSec}` + expand-at-0, where `semis = 12*log2(lastFreq/newFreq)`, passed to `set_note` as the bend with base = NEW freq (uses the existing `bend_` articulation: `frequency() = base * 2^(bend/12)` starts exactly at lastFreq and lands on newFreq — no PitchCurve compile needed); then set_note(with onsetId), non-setting push bindings, fire_triggers; append exactly `durSamples` via the shared chunk renderer; if `!pn.hold` → finish_line().
2. not open, `pn.hold == true`? **fresh held**: apply_note_bindings; `for (auto* e : vg.allEnvelopes) e->set_gated(true);` (empty allEnvelopes → warn once, treat as hold=false); prepare(durSamples); fire_triggers; append durSamples; mark line open (record vIdx/startTime/gain/lastFreq).
3. not open, `hold == false`? **classic**: byte-for-byte the v1 single-note path (ungated prepare, one loop with tail + adaptive ring + cap fade + containment + add_rendered). Implementation requirement: this path and finish_line() share ONE chunk renderer so the loop exists once:
```cpp
// Renders n samples of vg into line-style buf at absolute timeline
// frame (startFrame + buf.size()), with capture and performSource
// tick — the v1 loop body, extracted verbatim. Ring/tail/cap-fade
// live in the CALLERS (finish paths), not here.
void render_chunk(VoiceGraph& vg, std::vector<float>& buf,
                  int startFrame, int n, float gain);
```
`finish_line()`: `int rem = 0; for (auto* e : vg.allEnvelopes) rem = std::max(rem, e->gate_release(lastDurSamples));` then render `rem` + kVoiceTailSec with the adaptive-ring/cap-fade/containment logic from v1's loop tail, `add_rendered(line_.startTime, buf...)`, close. (Track `lastDurSamples` in HeldLine when each note is delivered.)
- Callers of `finish_open_lines()`: the score loader after its note loop (Task 5) and generate_unified after its (Task 6). Safety per spec §4.3.

- [ ] **Step 1: Failing tests** (in-memory instrument JSON as in v1's run_phrase_trigger_tests; BaselineSIN-derived for byte checks):
```cpp
static void run_hold_delivery_tests() {
    // (a) classic path byte-identity: hold:false PerformedNote ==
    //     yesterday's play_note output on BaselineSIN (load_scoreless,
    //     render, memcmp against a render made through the same path —
    //     the null gate is the real referee; this asserts determinism).
    // (b) hold mechanics: sine+gated-adsr patch; two notes same pitch,
    //     {hold:true} then {hold:false}; output sustains through the
    //     boundary (no release dip at 0.4 s: rms in [0.38,0.42] within
    //     10% of rms in [0.30,0.34]) and releases after 0.8 s.
    // (c) release re-layout end to end: quarter {hold:true} + half
    //     {hold:false}; envelope tail length ≈ 25% of the HALF note
    //     (measure last crossing under -40 dBFS relative to 0.8 s).
    // (d) glide: C4 {hold:true} then G4 {hold:false} on BaselineSIN
    //     with "glideMs": 15; f0 15 ms after the boundary is within
    //     30 cents of G4 (zero-crossing over [0.415, 0.445]); max
    //     per-sample derivative at the boundary < the no-glide case
    //     (render both, compare).
    // (e) finish_open_lines: single {hold:true} note then
    //     finish_open_lines(); output decays below -60 dBFS.
}
```
Write all five concretely against the harness idioms already in the file.
- [ ] **Step 2:** Build → fails. 
- [ ] **Step 3:** Implement per the interface block: extract `render_chunk` from the v1 loop FIRST as a pure refactor (build + null gate 79/79 before continuing — this is the step that proves the extraction is byte-clean), then add HeldLine + the three-way play_note + finish_line + glide-envelope builder + loader `glideMs` read (`inst->glideSec = instJson.value("glideMs", 15.0f) / 1000.0f`). Delete `play_phrase`, `PhraseNote`, `advance_phrase_note`, the boundary vector.
- [ ] **Step 4:** engine_tests ALL PASS; null gate 79/79; commit `feat(engine): resumable line voices — per-note hold/onset delivery, play_phrase retired (spec §4,§7)`.

### Task 5: score loader — onset/hold keys, sustaining, finish

**Files:**
- Modify: `engine/src/patch_loader.cpp` (score block: read `"onset"` (name → `onset_id`) and `"hold"`; DELETE the `"phrase":"cont"` grouping; consult `instrument.sustaining` — read into `inst->sustaining` bool member next to onsetNames; a `hold:true` on a non-sustaining instrument is forced false with a one-line stderr note; call `inst->finish_open_lines()` after the loop; duration computation: notes are sequential when they follow a held note — `"time"` absent ⇒ starts at previous end, same rule as v1 cont)
- Modify: `patches/baselines/phrase_smoke_phrased.json` (rewrite to v2 keys: note 1 `"hold": true`, note 2 plain)
- Test: CLI byte checks (same smoke trio as v1 Task 7)

**Interfaces:**
- Produces: score JSON `{"note":..,"duration":..,"onset":"tongue","hold":true}`; `PitchedInstrument::sustaining` (bool, default false).

- [ ] **Step 1:** Rewrite phrase_smoke_phrased.json to v2 keys + `"sustaining": true` in its instrument block. Render check: **is it still byte-identical to phrase_smoke_flat?** NO — and that is CORRECT under v2 (per-note envelope layout differs from one long note; spec §10.3). Replace the old identity check with the v2 assertions: same-pitch hold pair sustains through the boundary (no trough deeper than −3 dB at 0.5 s) and total sounding length ≈ flat's (within the release delta). Record the change of meaning in the file's score comment field if present, else in the commit message.
- [ ] **Step 2:** Implement loader changes. 
- [ ] **Step 3:** Gates: null gate 79/79 (the smoke pair is NOT in the manifest); piano invariant at CLI level — take `piano_default.json`, add a two-note score with `"hold": true` on note 1, render vs the same score without hold keys: byte-identical (sustaining absent = forced false). engine_tests ALL PASS.
- [ ] **Step 4:** Commit `feat(loader): v2 score keys onset/hold + sustaining consult; phrase:cont removed (spec §2,§3)`.

### Task 6: transport emission v2 + gencheck + status line

**Files:**
- Modify: `tools/mforce_ui/main.cpp`: SchedNote fields → `std::string onset; bool hold{false};` (phraseStart/transition die); the passage case computes v2 stamps; generate_unified drops grouping (per-note `play_note(PerformedNote{...})`, name→id via `pitched->onset_id(sn.onset)`, then `finish_open_lines()`); gencheck mirrors the Task-5 score reading; status line.

**Interfaces:**
- Consumes: parse_passage phraseStart flags (unchanged from v1 Task 1), `sustaining` via the serialized instrument block (`root["instrument"].value("sustaining", false)` at generate time).
- Produces: the emission function, THE seam, one place:
```cpp
// Performer emission (spec §3): phrase marks + sustaining -> per-note
// onset/hold. Notes vector comes from parse_passage.
static void stamp_passage(std::vector<SchedNote>& sched, bool sustaining);
// first of phrase: onset "breath"; later: prev pitch == this ? "tongue"
//                                                            : "slur";
// hold = true unless last of phrase. !sustaining -> all defaults + if
// the string contained '|', transport_set_status("patch is not marked
// sustaining — phrase marks ignored", false).
```

- [ ] **Step 1:** Implement; keep the `|`-free path emitting `{onset:"", hold:false}` = classic notes.
- [ ] **Step 2:** Verify in the running UI on the taught oboe (after Task 8 regenerates it — for now on a hand-edited copy with `"sustaining": true`): `|` passage phrases; `|`-free identical to before (hash the buffer via save); non-sustaining patch + `|` shows the status note and plays plain.
- [ ] **Step 3:** gencheck parity on the v2 smoke patch: scale 1.0, ≤1 LSB. Roundtrip harness over baselines+library: same 7 known pre-existing diffs (backlog 79), nothing new.
- [ ] **Step 4:** Commit `feat(ui): v2 emission (onset/hold), per-note generate, sustaining status note (spec §3,§4)`.

### Task 7: Output-panel declarations + labels

**Files:**
- Modify: `tools/mforce_ui/main.cpp` (NT_PATCH_OUTPUT settings block at ~line 9941; GraphNode members `bool sustaining{false}; char onsetsBuf[128]{};`; serialize into `root["instrument"]["sustaining"]` / `["onsets"]` (overriding extras, like polyphony); load populates from the instrument block)

- [ ] **Step 1:** Implement. The polyphony DragInt, the new Checkbox("sustaining"), and InputText("onsets") are drawn WITHOUT full-width PushItemWidth swallowing their labels: use explicit visible label text (`ImGui::Checkbox("sustaining##ps%d", ...)`, and for the width-constrained items draw `ImGui::Text("polyphony"); ImGui::SameLine();` before the widget) — the mystery-number fix. `onsetsBuf` parses comma-separated names on serialize (trim spaces, drop empties).
- [ ] **Step 2:** Roundtrip: set sustaining + onsets in the panel, save, reload — both survive; a patch with hand-written keys shows them in the panel.
- [ ] **Step 3:** Commit `feat(ui): sustaining + onsets on the Output settings panel, labels fixed (spec §8)`.

### Task 8: re-teach the instruments — v2 queue

**Files:**
- Modify: `tools/gen_articulation1.py`
- Regenerates: `patches/audition/articulation1/{trombone,oboe}_tongue.json`, `renders/dsp/audition/articulation1/` (gitignored)

- [ ] **Step 1:** Update the generator: (a) instrument keys `"onsets": ["tongue","slur"]`, `"sustaining": true`; (b) score events carry `"onset"`/`"hold"` (emission mirrored in python: hold except last, breath/tongue/slur per §3); (c) REVERT the v1 maxSec clamps (delete ENV_CLAMPS application — per-note semantics makes them moot); (d) **oboe gesture moves post-loop**: `BreathT` multiply relocates to the patch's output chain — read oboe1's graph, find the node feeding the output (`graph.output` id) and interpose `OutT = CombinedSource(multiply, thatNode, TDip)`, repoint `graph.output` to `OutT`; dip depth per the reference calibration: stages 1.0 → 0.25 (≈ −12 dB) over 8 ms, 30 ms there, 20 ms recovery (≈ 45–60 ms total); (e) trombone keeps breath-side placement and the same calibrated shape; (f) `slur` declared but unwired in both (the glide IS the slur).
- [ ] **Step 2:** Regenerate. Machine gates in the script (update): oboe phrased OTJ **0 deep dropouts** (the boundary-trough counter — post-loop placement must achieve this); tongue dent measurable at repeated-pitch boundaries only (slurred boundaries show glide, no dent); flat renders of both instruments byte-identical to a no-gesture control (gesture dormant without hold/onset).
- [ ] **Step 3:** UI roundtrip of both taught patches (NameGate/trigger/vocab/sustaining survive). Commit `feat(dsp): articulation1 v2 — post-loop oboe tongue, slur glide, sustaining declarations`.

### Task 9: bookkeeping + full-gate pass

**Files:**
- Modify: `docs/autonomy/dsp/REVIEW.md` (fold 73 to Resolved — superseded by the v2 round with Matt driving the redesign; new Awaiting 74: the v2 OTJ queue, plain language, THE QUESTION: "one breath per line now — with tongues only on repeated notes and clean slurs between pitches?")
- Modify: `docs/autonomy/STATUS.md` (top block), `docs/autonomy/dsp/BACKLOG.md` (53 note stands; 76 gets the perf-fix close-out note; 74a8 pointer to spec §5's performer-contour hook)
- Create: `docs/autonomy/dsp/reports/2026-09-20-note-onsets-v2.md` (all gates with numbers; the day's diagnosis chain — identical-WAV fingerprint, dropout roulette, reference calibration, perf root cause — cited)

- [ ] **Step 1:** Final gates in order: engine_tests ALL PASS; null gate 79/79; piano invariant (CLI + UI); v2 smoke assertions; gencheck parity; roundtrip corpus (7 known, 0 new); the articulation1 v2 machine gates.
- [ ] **Step 2:** Write the four documents; commit `note onsets v2: per-note phrasing shipped (REVIEW 74)`.

## Self-review record

- Spec coverage: §1 → T1/T2; §2 → T5 (loader) + T6 (status) + T7 (panel); §3 → T5/T6/T8; §4 → T4 (delivery, glide, finish safety) + T5/T6 (finish callers); §5 → T3 (re-layout) + T4 (per-note layout, gating-by-hold) + shapeless-envelope rule (T4's fresh-held path leaves ungated-shape envelopes running-once — no code needed, rule documented in report; lint deferred with a backlog note in T9); §6 → T8; §7 → T4; §8 → T1/T2/T7/T8; §10 gates → distributed + T9. §10.8 live parity: gencheck only (live path itself = backlog 53, per cut line).
- Placeholder scan: clean (Task 4 Step 1's prose test descriptions are specifications with concrete numbers, to be written as code against existing harness idioms — the idioms are in the file from v1).
- Type consistency: PerformedNote fields, onset_id, allEnvelopes, HeldLine, render_chunk, finish_open_lines, stamp_passage consistent across tasks.
- Known measured risk: Task 4's render_chunk extraction gates on an intermediate null-gate run BEFORE the semantics change lands — the same discipline that caught nothing-broken in v1's play_note delegation.
