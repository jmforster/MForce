# Refactor lane — REVIEW (Matt's interface)

Format, same as dsp/ and comp/: what to review → what a verdict decides.
Each refactor checkpoint lands here with its architecture report. Decisions
on the shape of the code happen in docs/architecture/ (the target
architecture and the per-area parts); this file is where finished
checkpoints wait for Matt's read.

**Standing rule (Matt, 2026-11-02):** for the remainder of the refactor,
easily-tweaked items (colours, menu dividers, labels, strip colours, any
reversible presentation or grouping call) do not come here. If the cause is
understood and the implication is not far-reaching, the session decides,
records the decision in the report's section 6 and adds the item to
`CLEANUP.md` (the post-refactor suggested cleanup master list). An item
lands here only when it is far-reaching: an interface Matt should own, a
render he should hear, a deletion. The oboe1 A/B at 0 moved is accepted
standing. R6, R8 and R9 below were resolved under this rule the day it was
given.

## Awaiting

**R16 — checkpoint 4.1: the seam's render half; `hiBoost`** (2026-11-09,
commits `e5518ef`..`7b32205`, fix waves `e0cb2e2` and `3c012ea`). One pair for your ears, by your 2026-10-05
decision, and one question the close raised that is yours and does not block
4.2.
- Read: `docs/autonomy/refactor/reports/2026-11-09-checkpoint-4.1.md`
  (fifteen minutes; section 1 — what moved and what did not, and why the pair is a
  second take rather than a re-balance; section 2 — `BuiltSound`, `Mixer` and
  `play_embedded_score` as the first half of Part 4's seam; section 6 — the
  session's decisions, 17 to 22 from the review and the close; section 8 —
  the review; section 10 — the C++ lessons).
- Listen: `renders/scratch/ab41/review/old/template_ode_to_joy_1.wav` (the
  base build, `hiBoost` 0.3) against
  `renders/scratch/ab41/review/new/template_ode_to_joy_1.wav` (the close
  build, the field gone). What changed by the rule: every note above 100 Hz
  enters the mix lower, by 1.0 dB at middle C up to 3.4 dB at the top of the
  keyboard. What you will actually be comparing: the first two notes are the
  old ones scaled by exactly that rule, and from the third note on every note
  is a different realisation (the same pitch, length and velocity, a
  different waveform), because `piano_default` plays the whole piece on one
  voice whose random streams run on from note to note and whose notes now end
  at different samples. The other twenty templates' pairs are in
  `renders/scratch/walk3_null_gate/pre41/` and `post41/`. A verdict decides
  only whether anything in the new take is wrong; the deletion itself is
  decided.
- Decide, when you like (non-blocking; BACKLOG row 11): **should an ordinary
  note re-anchor its random draws when it starts**, as a held line's
  continuations have since 2026-09-20? Today a pre-rendered note's waveform
  depends on how many samples every earlier note on its voice rendered, so
  one early note's level can change every later note's realisation; on a
  one-voice patch that is the whole piece. Re-anchoring would make a note's
  realisation a function of the note alone and would move every multi-note
  render in the library, so it is a Part 5 or post-freeze decision, not a
  refactor step. Report section 6, decision 22.
- For your information, nothing to decide: Part 4 section 5.8's sentence on
  `frames` was wrong for a file with no `seconds` and is corrected in place
  (the review caught it; 4.6 would have been written from it); an
  instrument-form file with a `StereoMixer` node named `mix` and gains would
  now have them ignored (no file has that shape); two channels naming one
  source is refused by name; `scores/baselines/test_k467_walker.json` is
  prose, not JSON (CLEANUP 49, your file).
- Try: `python tools/null_gate_perform_source.py --jobs 3` (78/78);
  `python tools/comp_template_null_gate.py compare pre41 post41` (21 DIFF,
  one MISSING: the walker file); `mforce_cli --test-ornaments` (the usage
  line).

**R15 — checkpoint 3.9: the formant family** (2026-11-07, commits
`6e43ca8`..`f94695b`). One render for your ears, by your own decision; nothing
else to decide under the 11-02 rule.
- Read: `docs/autonomy/refactor/reports/2026-11-07-checkpoint-3.9.md`
  (fifteen minutes; section 1 — the one render that moved and the capability
  that left the library with it, the editor-visible changes; section 2 — the
  design: `DrivenPart`, `Formant` with one verb, the three leaves, the fold;
  section 6 — the session's decisions, the plan's four holes among them;
  section 8 — the review; section 10 — the C++ lessons).
- Listen: `renders/scratch/ab39/review/old/sing_soprano_E.wav` (the pre-3.9
  CLI over the unrewritten file: F1 following the 440 Hz note above its 350 Hz
  knee) against `renders/scratch/ab39/review/new/sing_soprano_E.wav` (the
  close build over the rewritten file: F1 at 350 Hz). This is the one in-scope
  render 3.9 moved — your "2" on 2026-11-07, rows only. The other seven
  paramMap-driven vowels (`sing_alto_A`, `sing_alto_U`, `sing_bass_U`,
  `sing_soprano_A`, `sing_tenor_O`, `sing_tenor_U`, `speech_w_AW`) and the two
  `sweep/vowel_pitch_control/` files lost the same capability without a render
  moving (they sing below their knees). IDEAS holds the way back (spectrum-wide
  offset/scale pins on `FormantPeaks`). A verdict decides: does the soprano E
  stay in the library as it now renders, or is pitch-following wanted back
  before Part 4 (then IDEAS' entry becomes a backlog row)?
- Try: `python tools/null_gate_perform_source.py --jobs 3` (78/78); open
  `patches/library/voice/sing_alto_A.json` in the editor: one `FormantPeaks`
  node, its five rows in the grouped table; drag a row's frequency and watch
  the strip; the Additive menu reads Formant Peaks / Formant Bands / Formant
  Sequence.

**R14 — checkpoint 3.8: `Partials` split by responsibility** (2026-11-06,
commits `2dfabe2`..`e27f7ad`) — Part 3 complete. One render for your ears
and one non-blocking interface question.
- Read: `docs/autonomy/refactor/reports/2026-11-06-checkpoint-3.8.md`
  (fifteen minutes; section 1 — the one audible change and the two
  editor-visible ones; section 2 — the design: `PartialSet`, six value-type
  layers, `PartialLayout`; section 6 — the session's decisions, the executor
  and reviewer tier on Opus 5.5 among them; section 8 — the review,
  0 Critical, 0 Major, 10 Minor, 11 Flag — the Minors fixed in the wave, re-review closeable; section 10 — five C++ lessons: composition versus
  inheritance for a layer, an abstract class versus an interface, `reserve`
  as an owner–layer contract, signed zero, the most vexing parse).
- Listen: `renders/scratch/ab38/review/as2_bright_attack_test.wav` (the
  retired `AdditiveSource2` baseline, rendered by the pre-3.8 CLI) against
  `renders/scratch/ab38/review/additive_bright_attack_test.wav` (its
  replacement, `patches/baselines/additive_bright_attack_test.json`: two
  `ExplicitPartials` under a `CompositePartials`, per-group envelopes, the
  old amplitude wander as shimmer; ruling 8).
- Try: `python tools/null_gate_perform_source.py --jobs 3` (78/78); open
  `patches/library/strings/viola_default.json` in the editor and play it:
  the Additive family's menu now reads Additive / FullPartials / … with
  no Basic or Alternate.
- Decide: (1) the retired `AdditiveSource2` baseline's replacement — keep
  `additive_bright_attack_test.json` as a baseline as authored, re-author
  it differently, or drop it (the deletion itself is the target's, decided
  2026-10-05; the old file stays in `patches/old/additive_source2/`); (2)
  non-blocking: the name of the formant part base when the formant half of
  CLEANUP 17 moves (`Formant` is taken by the concrete peak; the
  candidates are `FormantShape` or a spectrum-flavoured name) — the
  convention itself is recorded in ruling 2 and needs no verdict.
  RESOLVED 2026-11-07 in session: Part 3 §11 — `Formant` the base,
  `FormantPeaks` rows, `FormantBands`, `FormantSequence`; checkpoint 3.9.

**R13 — checkpoint 3.6: one type per file** (2026-11-05, commits
`ed04dda`..`5c4c5db`) — nothing to decide. A move-only checkpoint: the six
multi-type node headers each became one file per type, `noise_sources.h` and
`filters.h` deleted (no umbrella), the shaped noise draw white and red share
folded into one `inline` helper, the six family `.cpp` moved under
`engine/src/source/<family>/`. No render moved.
- Read: `docs/autonomy/refactor/reports/2026-11-05-checkpoint-3.6.md` (ten
  minutes; section 1 — nothing is audible or editor-visible; section 6 — the
  two session decisions; section 8 — the CLEAN independent review; section 10
  — four C++ lessons: what `#include` really does and why an umbrella is a
  shim, `inline` free functions and the ODR, `git mv` and history across a
  split, a module map as a build-time contract).
- Try: `python tools/null_gate_perform_source.py --jobs 3` (78/78); open the
  editor and right-click the canvas — the Noise, Wavetable, Filter, Combiner
  and Additive menus list exactly what they did at 3.5 (one type per file is
  invisible there). `git show 400a587:engine/include/mforce/source/filter/filters.h`
  against the four new `filter/bw_*`/`delay_filter.h` files — byte-identical
  bodies.
- Decided, not asked (2026-11-02 rule): (1) `duplicate_blocks` rose 146→163
  (+17) against the plan's "must not rise"; the rise is the one-type-per-file
  preamble replicated per file plus the `bw_lowpass`/`bw_highpass` sibling
  similarity that already existed inside the umbrella header — no new authored
  logic (the reviewer confirmed). Accepted; CLEANUP 31. (2) `fm_source.h` and
  `patch_loader.cpp` dropped transitive-only includes; a stale `filters.h`
  comment and `lint_patches.py`'s source path were repaired in place.
- Future cleanup: a shared Butterworth base for the three BW filters would
  remove the sibling duplication (new design, out of a move-only checkpoint;
  CLEANUP 32).
- Review: an independent reviewer in place of the Fable refuter (ruling 8; 3.6
  touches none of the refuter's surfaces). Verdict CLEAN, nothing above Flag —
  it body-diffed all 26 types, failed the build on a removed include, failed
  the twin under two helper mutations, crossed the `.cpp` renames with
  `git log --follow`, and judged the duplicate rise benign. Two Flags
  (record-only): the helper's `lastSign==0` guard is a twin blind spot that
  cannot affect byte-identity; the `lint_patches.py` edit was not in the plan's
  Task 4 bullet (it is the move's minimal consequence). No Major, no re-review.

**R12 — checkpoint 3.5: the envelope presets become one type; the `KSPianoString` alias goes**
(2026-11-04, commits `750f70e`..`a2ff727`) — under the standing rule,
nothing blocks the landing; one interface question is yours (item 1,
answer any time), and the consequences of the two deletions are named.
- Read: `docs/autonomy/refactor/reports/2026-11-04-checkpoint-3.5.md`
  (its reading guide is ten minutes; section 1 names the one correction to
  Part 3 section 6 — the preset was a load-time form, never a setting —
  and the three editor-visible differences; section 6 item 3 is the table
  of the six layouts and the one default that differs; section 8 the
  refutation and the wave; section 10 four C++ lessons: a table of
  function pointers as a dispatch, static factories versus subclasses to
  name a layout, `inline constexpr` in a header, the most vexing parse met
  in a test).
- Try: `python tools/null_gate_perform_source.py --jobs 3` (78/78); open the
  editor and right-click the canvas — Envelopes lists one Envelope; create
  one and look above its stage table: a Preset combo with Apply; pick ASR
  and Apply, the table lays out three stages. Open
  `patches/library/winds/bassoon1.json`: `Morph_env` is an Envelope whose
  stage table holds what its AS knob group held. Load any file from
  `patches/old/` that names a preset class: the status line names
  `tools/migrate_deleted_types.py`.
- Decide, yours, non-blocking: (1) **the editor's face of an Envelope.**
  The six preset types are gone from the menu and with them the preset
  knob groups you laid out on 2026-08-29 (Duration, Shape); every Envelope
  is the stage table plus the Preset combo, and the 174 rewritten nodes
  show the table where they showed knobs. If you want the knobs back, the
  way is `Setting<int> preset` plus the layout knobs as settings on
  `Envelope` (inert on an authored shape) — additive on top of this
  checkpoint; the files on disk already carry exactly the keys that design
  would read, so there is no second rewrite (plan ruling 1). The code as
  landed is the recommendation: the stage list is the one truth, a preset
  is a way of writing one, which is what your own note in `envelope.h`
  says ("presets are UI conveniences").
- Decided, named here and not asked: (2) `patches/old/`'s 59 nodes (54
  preset-class, 5 alias) in 41 files fail to load naming the tool; one
  command rewrites them if ever wanted. (3) `patches/pending/` was
  rewritten under Part 3 section 6's own count (23 files; originals at
  `renders/scratch/ab35/pending_before/`), and `sweep/` and `scratch/`
  too (whitespace reformatted to indent 2 on the first run; the tool keeps
  a file's indent since). (4) `sustainLevel` reads 0 on an `as`/`asr`/`ads`
  shape where the class showed its stored level — the knob was inert
  there (CLEANUP 22). (5) The `adsr` form's `release` default 0.0 against
  the class's 0.1: every node on disk carries the key; the tool wrote 0.1
  where a class node lacked one (CLEANUP 23). (6) Two generator scripts'
  AR ramp drivers now apply the `attackMin`/`attackMax` they always carried
  (dead on the class). (7) The lint's selftest was passing nothing (its
  bad key is in the global allowlist); fixed in Task 1's commit.
- Refuter: byte-identity upheld with the refuter's own A/B (the eleven rewritten scope files and three controls 14/14, eight synthetics of its own 8/8, five archived copies through the tool); no Critical; two Majors, both in the editor's new Preset Apply (the cached `sustainLevel` — 0 on every two- or three-stage node — pushed over a fresh ADSR's four slots, and that push outside the audio lock); three Minors (the tool carried keys a class never read into a form that reads them, seven archived files would have moved; the refusal test pinned one knob of eleven; the whole-corpus roundtrip gate permanently red since 3.2); two Flags. The wave (`b1d0222`) fixed all but the standing Flag — Apply re-pulls under the lock through one function with a headless `--apply-preset` twin that proved it on `bassoon1` and `reed_test`; the tool drops the dead keys by name; the gate's known set is the backlog-79 six and the run is green. Re-review: closeable, nothing introduced, four Flags — two one-line fixes taken (an upper-case preset name; an ASCII note), one coverage correction accepted (the three in-scope `as` nodes are dead in the render, so `as` is render-pinned by the synthetic sets and the tests, not by the scope — CLEANUP 30), one stale scratch label.

## Resolved

**3.7 — the real-time fixes** (2026-11-06) — closed without a checkpoint,
Matt's verdict "skip to 3.8". Part 3 §9's seven items were already landed by
2.6 and 3.4 (the tables and every evolution buffer reserved at `prepare`,
fitted within capacity per note); the allocation gate is 0 of 132 and
`engine_tests` covers each named node. The live-edit class (a structural
setting read live on the audio path after a prepare-time sizing) stays booked
to Part 7 (CLEANUP 33); the gate's blind spots are CLEANUP 34. No report:
nothing was built.

**R11 — checkpoint 3.4: each evolution a `Component`; the holders and the string form gone**
(2026-11-03, commits `efd10c7`..`0d1e826`) — under the standing rule,
nothing to decide; listed so the chain of reports is unbroken, and so two
consequences of a deletion you signed on 2026-10-05 are in front of you.
- Read: `docs/autonomy/refactor/reports/2026-11-03-checkpoint-3.4.md`
  (its reading guide is ten minutes; section 1 names the two live-edit
  differences and the deletion's consequences, section 6 the three
  comparison tables — the string form against the node form, each
  evolution's moved members, `TargetEvolution`'s synthesis — section 8 the
  refutation and the wave, section 10 four C++ lessons: an abstract base
  versus a pure interface, a member that files itself versus a reference to
  another's, a return nobody reads, idempotence as a contract).
- Try: `python tools/null_gate_perform_source.py --jobs 3` (78/78); open the
  editor and right-click the canvas — Experimental > Wavetable now ends with
  Target Evolution: create one, wire it into a Wavetable's `evolution`, give
  it two or three partials, play; point the CLI at
  `patches/old/baselines_culled_2026-09-14/pluck_test.json` — it refuses the
  load naming `tools/string_evolution_to_node.py`; run that tool on a copy
  and the copy loads; open
  `engine/include/mforce/source/wavetable/wave_evolution.h` — one base,
  thirteen evolutions, no wrappers.
- Decided, named here and not asked: (0) the A/B, 0 of 132; (1)
  `WaveEvolution` is an abstract `Component` base rather than a pure
  interface beside it (the pin lifecycle written once; Part 3 §5's words
  kept; CLEANUP row 17 holds the naming question); (2) a Pluck or Averaging
  setting edited mid-stream in the editor no longer restarts its draw stream
  — the old wrapper rebuilt its evolution on every change, Pluck's with a
  constant seed that ignored the registered one; no render in scope; one
  `reanchor()` in a hook brings the restart back if you ever want it
  (CLEANUP row 21); (3) the eight archived string-form patches in
  `patches/old/baselines_culled_2026-09-14/` no longer load — never deleted,
  the converter rewrites one on demand — and `targetWave` has no node form
  (no patch in the tree ever used it); (4) `patches/pending/harsh_pluck.json`,
  your sandbox, was converted in place under the target's 2026-10-05 line
  that names it as one of the two live string forms; the original is at
  `renders/scratch/ab34/harsh_pluck_before.json`; (5) six `algev_test_*`
  baselines lost a dead `evolutionSeed` key (only the deleted branch ever
  read it) rather than the loader tolerating it; (6) `TargetEvolution` sits
  on the Experimental shelf with the derived label and session-chosen ranges
  (CLEANUP row 18). The refuter: no Critical, no Major; six Minors — the converter's averaging defaults, the refusal's wording for a slot, a stale `component.h` comment, the Part 3 section 2 diagram, and two missing test pins (Averaging's `autoAdjust` path, `TargetEvolution`'s morph and resample) — and ten flags; the wave took all six and the cheap flags with tests first, the same refuter re-reviewed it (closeable: every finding closed at the cited lines, nothing introduced — its spot A/B of the twelve in-scope wavetable patches, the synthetic seven, the eight archived forms and the pending patch all identical, the 132 re-compared; its one new flag, an unpinned converter default, closed by a test the same hour), and its own renders agreed with the arms: every in-scope wavetable patch, the synthetic seven, and all eight archived string forms converted by the tool render byte-identical across the checkpoint.

**R10 — checkpoint 3.3: the shared DSP primitives in `core/dsp`**
(2026-11-02, commits `0d500fc`..`b3c6160`) — under the standing rule,
nothing to decide; listed so the chain of reports is unbroken.
- Read: `docs/autonomy/refactor/reports/2026-11-02-checkpoint-3.3.md`
  (its reading guide is ten minutes; section 1 names the one departure by
  construction and the bounded sign-of-zero class, section 6 the seven
  comparison tables — where "exact" needed an argument — section 8 the
  refutation and the wave, section 10 four C++ lessons: value members, free
  functions and `inline`, what float arithmetic promises, two initialisation
  traps).
- Try: `python tools/null_gate_perform_source.py --jobs 3` (78/78); play
  `patches/library/winds/oboe1.json` and any KS piano patch on the keyboard —
  nothing changed (the A/B is 0 moved over 132); open
  `engine/include/mforce/core/dsp/` — nine headers, one primitive each.
- Decided, named here and not asked: (0) the A/B, 0 of 132; (1) EKS's
  stiffness allpass clears both memories at a note's start — a one-LSB
  difference over the rest of every note after the first, only with
  stiffness > 0, and no such patch in library or baselines (the alternative
  was a method on a shared primitive for one caller's bytes); (2)
  `BiquadSource`'s resonance mode runs the general section (the sign of a
  zero only, which 16-bit renders cannot carry); (3) `TuningAllpass` keeps
  the design table's name (CLEANUP row 15).

**R9 — checkpoint 3.2: families at registration; the menu generated; `Theme`; folders by family**
(2026-10-31, commits `67071f8`..`d5ebbf3`) — VERDICT 2026-11-02 (standing rule): (0) accepted, byte-identical; (1) the seven colours stand (CLEANUP: colour follows family, one Theme entry or one family word moves any); (2) the generated menu stands, the dividers are not restored (CLEANUP: a grouping fact on the tag if ever wanted); (3) the reading stands — the `music` → envelope edge is carried visibly until Part 5's split of `pitch_bend.h`, as the target's 3.7 already schedules; (4) the alias stays creatable until 3.5 deletes it; (5) the Vibrato and Segment strips stand as the Envelope family's (CLEANUP); (6) the timing is accepted on the mechanism (no render-path file changed except its include lines; the refuter traced it).
- Read: `docs/autonomy/refactor/reports/2026-10-31-checkpoint-3.2.md`
  (its reading guide is ten minutes; section 1 names the four things the
  editor shows differently — seven colours, eight Experimental newcomers,
  the lost dividers, two strips — and the meter's +1, section 5 the gates,
  the A/B and the timing, section 6 the decisions taken on your behalf —
  rulings 26 and 27 are the ones a verdict below decides — section 7 the
  debt, section 8 the refutation and the wave, section 10 four C++ lessons).
- Try: open the editor and right-click the canvas — the create menu reads
  Curve, Envelope, Var, Range, then Oscillators, Noise, Wavetable, Additive,
  a line, Physical, Filters, Envelopes, Modulators, Combiners, Utility, then
  Experimental with the same families inside; every family is one flat list
  (the dividers that used to sit inside Generators, Additive and the rest
  are gone); create a Shaper (lavender now, was teal) and a Vibrato (pale
  gold, was teal); load a patch with a SegmentSource and untick "Envelopes"
  in the waveform header — its strip hides with the envelopes; play
  `patches/library/winds/oboe1.json` on the keyboard — nothing you can hear
  changed (the A/B is 0 moved over 132); `python tools/structure/check.py`
  lists fourteen include edges outside the map, the new one
  `music/pitch_bend.h` → `source/envelope/envelope.h`.
- A verdict decides: (0) **the A/B**: 0 of 132 moved — accept the
  checkpoint as byte-identical; (1) **the colours**: seven types changed
  colour because colour follows the family now (HybridKS teal→tan,
  PierceFilter lavender→teal, Distortion and Shaper teal→lavender, Vibrato
  teal→pale gold, Segment reddish-orange→pale gold, Wormhole teal→grey) —
  accept, or name one that belongs in another family (one word on its
  registration moves it, colour, menu and folder together); (2) **the
  menu**: generated from the registry — the families read as Part 3
  section 3 says ("Generators" is Oscillators, "Loop" is Physical), each
  family is one list without the ten dividers the hand-written menus drew
  inside families, Experimental mirrors the families and lists eight types
  no menu had, the Curve/Envelope/Var/Range quick block stays — accept, or
  want the dividers back (a grouping fact on the registration, not added
  unasked); (3) **the meter's +1**: `include_violations` 13 → 14 where the
  plan said +0 — the new edge is `music/pitch_bend.h` compiling a PitchCurve
  into an Envelope, which existed before and was hidden by the envelope
  header's old address in `core/`; the map was not widened (the target says
  music depends on core only) and the function was not moved early (the
  target's 3.7 moves it to render when `pitch_bend.h` splits) — accept the
  reading, with the edge carried visibly until Part 5, or want
  `compile_pitch_curve` moved to render now; (4) **the `KSPianoString`
  alias** is creatable from Experimental > Physical until 3.5 deletes it
  (your "throw them all under experimental" swept it in; the refuter notes
  an alias is not a type) — accept, 3.5 migrating any patch saved under it,
  or want it hidden (a third shelf value, "registered, not offered"); (5)
  **the Vibrato and Segment strips** draw in the envelope trace's yellow
  and hide with the "Envelopes" toggle, being the Envelope family's (Segment
  drew green as audio before) — accept, or want Segment drawn as audio,
  which is a family exception the registry would have to carry; (6) **the
  timing** — two runs of the
  render-time A/B, neither on a quiet machine (the base arm's own walls
  ran 9 to 25 percent above 3.1's clean run, higher on the second run
  with nothing of this session's running; rows swing 0.87x to 1.10x); the
  mechanism argument stands (no file on a render path changed except its
  include lines; the refuter traced it) — accept it for a checkpoint that
  touched no render path, or want a rerun on a quiet machine before Part 3
  continues.

**R8 — checkpoint 3.1: `Component` root; the parts leave `ValueSource`**
(2026-10-30, commits `ba097f6`..`f1f8244`) — VERDICT 2026-11-02 (standing rule): (0) the malformed-wire refusal stands as 2.5's "a rejected wire says so" applied to the two kinds (no patch in scope carries one); (1) accepted, byte-identical; (2) the undocumented `"formants"` key stays unread (only `patches/old/` carries it, and the loader has refused it by name since the wave); (3) the part strips' empty capture stands for Part 7 (CLEANUP); (4) `Component::on_slot_changed` stands as the third hook — one owner today; a second owner that keeps a view into its part reuses it, and if the hook then wants a payload that is Part 7's design; (5) the unlink fix stands, pinned by its test; the manual try is not required; (6) the editor's hand lists are Part 7's (3.1 report section 7); (7) the timing is accepted on the mechanism.
- Read: `docs/autonomy/refactor/reports/2026-10-30-checkpoint-3.1.md`
  (its reading guide is ten minutes; section 1 names the one behaviour
  change — a malformed wire now fails the load — and the hook the plan did
  not foresee, section 5 the gates, the A/B and the timing, section 6 the
  decisions taken on your behalf — rulings 27, 25, 19 and 29 departed from
  the plan or corrected it — section 7 the debt, section 8 the refutation,
  section 10 four C++ lessons).
- Try: copy any library additive patch, point its AdditiveSource's
  `frequency` at its FullPartials node (`"frequency": {"ref": "<parts id>"}`)
  and load it — the load fails naming the node, the pin and "a part, not a
  signal", where it used to render a silent zero; wire a constant into a
  WavetableSource's `evolution` in the editor — the slot refuses it; play
  `patches/library/winds/oboe1.json` and any formant patch on the keyboard —
  nothing you can hear changed (the A/B is 0 moved over 132).
- A verdict decides: (0) **the behaviour change on malformed wires** (a
  signal into a slot, a part into a signal pin, a part as output or channel
  source now fail the load with the reason; no patch in library or baselines
  carries one) — accept as 2.5's "a rejected wire says so" applied to the two
  kinds; (1) **the A/B**: 0 of 132 moved — accept the checkpoint as
  byte-identical; (2) **`FormantSequence`'s undocumented `"formants"` key**
  (read beside the descriptor's `"spectra"` by the deleted branch) is no
  longer read: a `patches/old/` sequence carrying it loads without its
  spectra — accept, or have the loader refuse the key by name; (3) **the
  editor's part strips** capture nothing where they drew a flat zero line
  (a part has no signal); the Partials and Formant spectra strips are
  unaffected — accept for Part 7, or want the zero line back now; (4)
  **`Component::on_slot_changed`** as the third hook beside
  `on_setting_changed` and `on_array_changed` (section 6 ruling 27): the
  design, or a different answer to an owner that keeps a view into its part.
Two more from the refutation (section 8): (5) **the editor's unlink** —
  before the wave, unlinking a holder from a WavetableSource (or a formant
  from an AdditiveSource) left the part in place until reload, a regression
  the roundtrip gate cannot see; the rewire pass now clears every pin first.
  Try it: wire a PluckEvolution into a WavetableSource, play, delete the
  link, play — the pluck must be gone; (6) **the editor's part-type lists**
  (`pin_type_compat_error`, the strips' two predicates) are three hand
  lists beside the registry, and a link the engine refuses is drawn and
  inert — left for Part 7 (the editor asking the door), or wanted sooner.
  And one from the close: (7) **the timing** — three runs of the
  render-time A/B, none on a quiet machine (the base arm's own walls
  spread 35 percent between runs of one binary; rows swing 0.83x to
  1.31x); the mechanism argument stands (no per-sample path changed, the
  refuter traced it) — accept it for a checkpoint that touched no render
  path, or want a rerun on a quiet machine before Part 3 continues.

**R6 — checkpoint 2.5: no throw on a render path; fault flags; a rejected
wire says so** (2026-10-26, commits `cb1b4ab`..`1481d15`) — VERDICT 2026-11-02 (standing rule): (0) the five patches that stop loading stay as listed (`sweep/` is regenerable, `old/` is never deleted; both outside the measurement scope); (1) the wrap list's renders are the frozen truth (the two `double_advance` baselines are regression fixtures, refrozen; the rest are queue and sweep patches outside the scope); (2) the fault reports' shape stands; a fault count on the UI stamp is a CLEANUP suggestion; (3) `prepare()` throwing on a zero rate stands as the host contract; (4) the narrower sentence is accepted into Part 2 section 6 (edited 2026-11-02): the clamp applies to a non-positive frequency only.
- Read: `docs/autonomy/refactor/reports/2026-10-20-checkpoint-2.5.md`
  (its reading guide is ten minutes; section 1 names the three latent host
  bugs the fault bits found on the way in, section 5 the patches that stop
  loading and the twenty-five renders the DelayLine wrap moved, section 6
  the decisions taken on your behalf — rulings 17, 20 and 25 are the ones
  that departed from the plan or corrected the ledger — section 7 the debt,
  section 8 the refutation, section 10 three C++ lessons).
- Try: `python tools/null_gate_perform_source.py --jobs 3` (78/78; the two
  `double_advance` baselines were refrozen for the wrap and nothing else
  moved); render `renders/scratch/ab25_tree/zero_freq.json` with the CLI —
  it used to terminate the process from the audio thread, now it renders
  and prints `[faults] osc (SineSource): frequency clamped` after the
  stats; copy any library patch, misspell one key on one node, and load it
  — the load fails naming the node, the pin and the reason instead of
  rendering wrong in silence.
- A verdict decides: (0) **the patches that stop loading** (section 5):
  four `sweep/stk_bandedwg_port/*_struck` and `old/Squeaker.json`, each a
  stale key on a hand-built or legacy node, left as they are and listed
  (`sweep/` is regenerable, `old/` is never deleted) — accepted, or any to
  be repaired by hand; (1) **the wrap list's renders as the frozen truth**
  (`double_advance_bug` and `double_advance_ok` refrozen; `Trombone_att1`,
  the fourteen `bwg_perc1`/`bwg_perc2` queue patches and eight sweeps
  changed — re-listen any before accepting); (2) **the fault reports'
  shape**: the CLI's `[faults]` block after the stats and the UI's
  transport status line — enough, or a fault count on the stamp too; (3)
  **`prepare()` throwing on a zero rate** as the host contract (the host
  that forgets the rate fails loudly at build, never on the audio thread);
  (4) **ruling 2's narrowing of the signed text** (report section 6; the
  refuter's I11): the clamp applies to a non-positive frequency only, a
  positive frequency below the pin's minimum is left as it was, so no
  non-terminating render moved — accept the narrower sentence into Part 2
  §6, or schedule the wider clamp as a behaviour checkpoint with its own
  measured list.

**R7 — checkpoint 2.6: the allocation test over every baseline patch**
(2026-10-30, commits `6a37849`..`d3df3ea`) — VERDICT 2026-10-30: (0) the gate stays in `gates.py`'s slow list; a `--jobs` split across processes is a plan 3.1 tooling task (Part 3 touches every node, so the gate runs per task there); (1) accepted — byte-identical over the sample; (2) the queues were measured once after the fixes (752 patches, 0 allocate, 3 fail to load: the GrayScott three) and are not measured again — library + baselines is the standard for every gate, A/B and measurement from here (Matt: too much time on patches that will likely never be used; a promising old patch that breaks is diagnosed when he loads it); (3) the 20 Hz floor stays; (4) a structural setting takes no per-note push: the mark is on the pin and its descriptor (`SettingRole::Structural`; every int and bool by type, as pin_model_design §6 already said; the Partials shaping floats declared), the loader refuses a paramMap or dynamicPins wire into one naming the node, the setting and the reason, the UI offers no pin for one, and the two Partials handlers read the mark instead of a name list — landed `6007b29`.
- Read: `docs/autonomy/refactor/reports/2026-10-30-checkpoint-2.6.md`
  (its reading guide is ten minutes; section 1 names the one live-path fix
  and the three offline-host allocations left to Part 5, section 5 the gate's
  numbers — 97 allocating patches to 0 — and the A/B, section 6 the decisions
  taken on your behalf — rulings 12, 15 and 18 departed from the plan —
  section 7 the debt, section 8 the refutation, section 10 four C++ lessons).
- Try: `build/tools/alloc_gate/Release/alloc_gate.exe patches/library patches/baselines`
  (132 patches: 0 allocate; about four minutes); point it at any queue patch,
  `alloc_gate.exe --verbose patches/audition/<family>/<patch>.json`, for its
  count and the first allocation's call stack with file and line; play
  `patches/baselines/double_advance_ok.json` and `feedback/loop_tap_starved.json`
  on the keyboard — a live note-on on a loop patch no longer copies the
  voice's tail list (nothing you can hear changed: the A/B is 0 moved).
- A verdict decides: (0) **the gate's place**: `gates.py`'s slow list, where
  its 78 to 230 seconds put it (78 at the seal with nothing to symbolize), or a ctest after a `--jobs` split across processes
  (not done here); (1) **the A/B**: no render moved over the 1,615-patch
  sample — accept the checkpoint as byte-identical; (2) **the queue folders**:
  398 of 749 audition and pending patches allocated on the first arming, in
  the same families, and are not re-measured after the fixes — left as
  information, or a run over them worth doing now; (3) **the floor,
  `WaveSource::kMinTableHz` = 20 Hz**, the length every frequency-dependent
  table (and BasicAdditive's partial count) is reserved for: the survey found
  220 Hz the lowest constant in use and A0 (27.5 Hz) the lowest key, and a
  note below the floor gets the floor's length — keep, lower it, or make it a
  setting; (4) **a per-note push into a Partials shaping setting** (`evenWeight`,
  `minMult`, `incr`, `unitPO` through a paramMap or a dynamic pin): the array
  rebuild moved from `start` to `prepare`, so such a push is now ignored until
  the next prepare, where the old `start` honoured it (within capacity for a
  value-only change). No patch outside `old/` carries one. Honour it at `start`
  within capacity (size for the setting's declared maximum), or have the loader
  refuse a push binding into a shaping setting (2.5's "a rejected wire says so").

**R5 — checkpoint 2.4: taps reading B** (2026-10-19, commits
`a8d5aa9`..`403269f`) — VERDICT 2026-10-26: (0) the forty changed renders
are the frozen truth (Matt: the renders pointed out sound great, the
trombone improved); (1) `previous()` and `claimed_tick()` stay public on
every node until Part 3's pins narrow the door; (2) `feedback_loop_design.md`
section 2's "positional" paragraph stays as the 08-31 design with the
header delta.
- Read: `docs/autonomy/refactor/reports/2026-10-19-checkpoint-2.4.md`
  (its reading guide is ten minutes; section 5 holds the forty renders that
  changed by family and the render-time table, section 6 the decisions taken
  on your behalf — ruling 18 is the one that departed from the plan —
  section 7 the debt, section 8 the refutation, section 10 three C++ lessons).
- Try: `python tools/null_gate_perform_source.py --jobs 3` (78/78 — nothing in
  library or baselines moved); open `patches/audition/trombone1/trombone_attempt4.json`
  and play — the nonlinear bore's ratio modulation now follows the previous
  sample (the loop period is unchanged); open
  `patches/baselines/feedback/loop_hysteresis_bow.json` — unchanged, as every
  loop closed through a tap on its forward path is.
- A verdict decides: (0) **the forty changed renders** (section 5, Appendix
  A) accepted as the frozen truth — they are your listening queues and sweeps
  (`audition/trombone1` ×4, `audition/pierce1d_1` ×6, `pending/brass/Trombone_att1`,
  the `sweep` trombone, onemass, nlbore and stk_blowhole families,
  `old/nlbore_brass1` ×5), nothing in library or baselines, so no manifest was
  refrozen; re-listen any before accepting; (1) **`previous()` and
  `claimed_tick()` on every node** for the tap edge alone (section 7): leave
  them until Part 3's pins narrow the door, or narrow it now; (2)
  **`feedback_loop_design.md` section 2's "positional" paragraph**: left as the
  08-31 design with the header delta (Task 2's choice), or rewritten.

**R4 — checkpoint 2.3: the per-tick memo** (2026-10-18, commits
`22bf3be`..`fdf7536`) — VERDICT 2026-10-19: (0) the render-time cost is
accepted as the price of one `next()` for every node; Part 3's node pass
is not asked to recover it; (1) the changed renders are the frozen
truth; (2) the `loop_root_sensitivity` pair is retired — moved to
`patches/old/baselines_retired_2026-10-19/` (never deleted), its two
manifest entries dropped (the null gate is 78 entries), dsp backlog 65
closed to that file's ledger; (3) `step()` stays for now, with a
post-refactor revisit listed (refactor BACKLOG row 9a); (4) ShaperSource's
Auto pins stand for now, with a post-refactor revisit listed (row 9b).
- Read: `docs/autonomy/refactor/reports/2026-10-18-checkpoint-2.3.md`
  (its reading guide is ten minutes; section 5 holds the renders that
  changed and the render-time tables, section 6 the decisions taken on
  your behalf, section 7 the debt, section 8 the refutation, section 10
  three C++ lessons).
- Try: `python tools/null_gate_perform_source.py --jobs 3`; open the UI,
  wire one envelope to two nodes' pins and play — both read it, in whatever
  order you wired them; play `patches/baselines/feedback/loop_root_sensitivity_fires.json`
  and its `_dead` sibling: they are the same sound now.
- A verdict decides: (0) **render time** (section 5): the checkpoint itself
  is neutral against the tree before it, and the sweep experiment bought
  8 to 16 points where pins are wired, but the 2.1 cost stands — oboe1 at
  0.73x, piano_default at 0.88x of the pre-2.1 binary. The rest is the
  generic per-node `next()` (a call layer, a loop and a store per node
  where the old bodies loaded their pins at fixed offsets). Accept it as
  the price of one `next()` for every node, or let the hot leaf types pull
  their own pins inside `compute()`, decided per family in Part 3's node
  pass; (1) **the renders that changed** (section 5, Appendix A): the two
  `double_advance` baselines, `loop_root_sensitivity_fires`, and 106
  gitignored audition/sweep patches that share a stateful node — accepted
  as the new frozen truth, or any you want re-listened first; (2)
  **`loop_root_sensitivity_fires` / `_dead`**: the pair no longer shows
  what it was written for (the "root sensitivity" was the double advance;
  both are dead now) — keep them as the memo's regression pair, rename,
  or retire; (3) **`step()`** as a permanent verb on `ValueSource` (one
  sample now, whatever the tick; two callers, the table fills), or a
  Part 3 item that gives those two nodes a private excitation source
  instead; (4) **ShaperSource's `breakaway` and `capture`** are pulled
  every sample with hysteresis off (Auto pins), where before they were
  pulled only in hysteresis mode — stands (a threshold modulator switched
  on mid-note has been running since the note began), or Manual again.

**R3 — checkpoint 2.2: prepare(ctx) / start(frames)** (2026-10-18, commits
`7e20e5f`..`5980873`) — VERDICT 2026-10-18: (0) the DelayLine zero slot
stays listed for 2.5; (1) the UI preparing its editor graph at every
play is fine; (2) the report's section-7 list is the work list for
checkpoint 2.6's allocation test — every listed allocation moves into
prepare within 2.6, the test lands with no tolerance list; (3)
`render_context()` and `prepared()` become protected — a host that makes
a node after prepare hands it the context it prepared the owner with
(`ensure_constant`'s three-argument form), the node is not asked. Items
2 and 3 are my recommendations, taken on Matt's "take your
recommendations and proceed"; item 3 landed as a fix on top of 2.2.
- Read: `docs/autonomy/refactor/reports/2026-10-17-checkpoint-2.2.md`
  (its reading guide is ten minutes; section 6 holds the decisions taken
  on your behalf, section 7 the debt, section 8 the refutation and the
  seven per-task reviews, section 10 three C++ lessons).
- Try: `python tools/null_gate_perform_source.py --jobs 3`; open the UI,
  play a key, Generate — every node is now prepared once by the loader
  and only started per note; a patch saved from the UI records the rate
  the audio stream actually opened with.
- A verdict decides: (0) **DelayLineSource's stray read** (section 7,
  first item): a pre-existing one-past-the-end read is kept byte-identical
  through an extra always-zero ring slot; the correct fix (wrap to slot 0)
  changes the bytes of at least one baseline — leave it listed for
  checkpoint 2.5, or take the render change now with that baseline
  refrozen; (1) whether the UI preparing its editor graph again at every
  play (not per note; idempotent) is the behaviour you want, or whether a
  node should be prepared once when it is created; (2) the list of
  `start()` bodies that still allocate (section 7) as the starting list
  for checkpoint 2.6's test; (3) `render_context()` and `prepared()` as
  public read-only getters on every node (section 6, item 13).

**R2 — checkpoint 2.1: pins declared once** (2026-10-17, commits
`1b9b69f`..`5f9aeea` plus the close)
- Read: `docs/autonomy/refactor/reports/2026-10-16-checkpoint-2.1.md`
  (its reading guide is ten minutes; section 6 holds the decisions
  taken on your behalf, section 7 the debt, section 10 the lessons).
- Try: open any patch in the UI — the Properties panel is served from the
  pin registry now; `python tools/null_gate_perform_source.py --jobs 3`.
- A verdict decides: (0) **the render-time cost** — oboe1 renders about
  30-40% slower than before the checkpoint, piano 25-45%, viola 2-9%
  (report section 5: measured, bisected, experimented; a few nanoseconds
  per node per sample that the hand-written next() bodies did not pay).
  Carry it to checkpoint 2.3, where the per-sample path is rewritten
  anyway, or stop and recover it first; (1) whether the two tolerated
  byte-identity classes (section 6, items 6 and 7 — wiring that no stored
  patch has, retired by the memo in 2.3) are acceptable as named, or must
  be closed by Manual pins now; (2) whether the eight stored patches that
  now state the RedNoise defaults they relied on should keep those keys
  (they do today); (3) the descriptor-order changes the UI shows
  (WaveSource's amplitude first; the presets' and Partials subclasses'
  inherited settings first).

**R1 — checkpoint 0: the structure meter** (2026-10-05, commit `637207a`) — VERDICT 2026-10-16: format accepted; R2 (checkpoint 2.1) uses it unchanged.
- Read: `docs/autonomy/refactor/reports/2026-10-05-checkpoint0-meter.md`
  (the first architecture report in the agreed format; ten minutes via
  its reading guide).
- Try: `python tools/structure/check.py` from the repo root, and
  `python tools/gates.py --fast`.
- A verdict decides: whether the report format gives you what you asked
  for (types and files with responsibility, users, dependencies, the
  design choice and its alternative, the name, lines to read; generated
  metrics; decisions; debt; the independent review; a reading guide; a
  C++ lesson), before checkpoint 2.1 produces the next one.

