# Checkpoint 3.8 — `Partials` split by responsibility (report R14)

**Landed 2026-11-06 on main, `2dfabe2`..`e27f7ad` with the two fix waves `b254b5c` and `268fb0a`** (base before 3.8 `4d32899`;
plan `docs/superpowers/plans/2026-11-06-part3-checkpoint-3.8.md`, its Ledger
the record of every ruling and every task). The eighth of Part 3's eight:
Part 3 is complete. `partials.h` is one type per file; `Partials` keeps the
render body, the array protocol and its surface, and composes six value-type
layers and one layout; the pure `IPartials` is the abstract base `PartialSet`;
`BasicAdditiveSource` and `AdditiveSource2` are gone and `FullAdditiveSource`
is `AdditiveSource`. No render in scope moved — the null gate is 78/78 after
every task and the close A/B is 131 of 131 identical, 0 DIFFER — except the one `AdditiveSource2`
baseline, which retired by deletion and was re-authored on the surviving
types; its new render is the one thing in this checkpoint for your ears.

Reading guide: fifteen minutes. Section 1 is the one audible item and the two
editor-visible ones. Section 2 is the design. Section 6 is what the session
decided under the 2026-11-02 rule. Section 8 is the independent review.
Section 10 is five C++ lessons.

## 1. What changed for you

- **One render is new, by deletion.** `patches/baselines/as2_bright_attack_test.json`
  was the only patch in scope on `AdditiveSource2`, the half-finished port the
  target architecture lists for deletion. Its node's form — per-partial-group
  envelopes and an amplitude law of `(1/m^e) * e^2` in the envelope value `e` —
  exists on no surviving type, so there was no rewrite that keeps the render.
  The file is retired to `patches/old/additive_source2/` (nothing in `old/` is
  deleted) and `patches/baselines/additive_bright_attack_test.json` takes its
  place: the same three envelopes and mixer, an `AdditiveSource` over a
  `CompositePartials` of two `ExplicitPartials`, `lo` (partials 1–5 on `loEnv`)
  and `hi` (6–60 on `hiEnv`), each partial's amplitude 0 → 1/m under its group's
  envelope, the old node's amplitude wander as shimmer (0.03 at 0.2 Hz,
  coherence 0). The old and new renders sit side by side at
  `renders/scratch/ab38/review/as2_bright_attack_test.wav` and
  `additive_bright_attack_test.wav` (peak 0.285 → 0.258, rms 0.0473 → 0.0476).
  REVIEW R14 item 1: listen, and say whether the replacement stays as a
  baseline.
- **In the editor**, the Additive family's create menu lost "Basic" and
  "Alternate" (the two deleted types) and "Full" reads "Additive" (the derived
  label, now that it has no siblings to be distinguished from). Nothing else is
  visible: every surviving type keeps its name, pins and settings; the partials
  strip draws as before.
- **Everything else is byte-identical**: 131 of 131 library and baseline
  renders, and thirteen synthetic patches that turn on every `Partials` layer
  no in-scope patch exercises.

## 2. The shape

Part 3 section 7 as written, designed once the files were apart.

**`PartialSet`** (`additive/partial_set.h`) is the abstract `Component` base
of a set of partials, the shape 3.4 gave `WaveEvolution`: non-virtual
`prepare` and `start` that forward to every pin, a non-virtual `next` that
advances every `Param` pin, each in declaration order, then the hooks
`on_prepare`/`on_start`/`on_next`; and the three work verbs `partial_count`,
`get_partial_value`, `sum_partials` (the batched sum's default body as the
interface had it). `Partials` derives from it and keeps its work in the hooks;
the twenty-four hand-written pin forwards are gone. `CompositePartials` derives
from it and forwards each verb to its sets in slot order. `AdditiveSource` and
`CompositePartials` hold a `Slot<PartialSet>`. The pure `IPartials` is gone.

**Six layers, value types with no pins**, each owned by value in `Partials`
and sized at `prepare(n)`, started per note from its own rng streams, advanced
once per sample, read per partial:

| Layer | File | Holds | Reads from the body |
|---|---|---|---|
| `dsp::SegmentWalk` | `core/dsp/segment_walk.h` | one smoothed random walk with meta-randomised segments (`seg_len`, `init`, `advance`) | — (the primitive under the next two) |
| `CoherentWalk` | `additive/coherent_walk.h` | one shared walk and one per partial, coherence-mixed; held twice as `motion_` and `shimmer_` | `value(index)` |
| `PairTrade` | `additive/pair_trade.h` | one walk per adjacent pair on one stream | `value(index >> 1)` |
| `BandwidthNoise` | `additive/bandwidth_noise.h` | per-partial smoothed noise: current, target, position, stream | `factor(index, dry, wet)` — one step and the mix |
| `OnsetGate` | `additive/onset_gate.h` | per-partial delays, the fade length, the note's sample count | `apply(index, pampl)` — the assignment for `t <= 0` |
| `DecayLaw` | `additive/decay_law.h` | the per-partial factor (double-precision law) and running gain | `gain(index)` |

**`PartialLayout`** (`additive/partial_layout.h`) is the six arrays (`mult1`,
`mult2`, `ampl1`, `ampl2`, `po1`, `po2`, public: a set's `init_arrays` writes
them, the body reads them), the originals the last rebuild kept (private),
`snapshot`/`restore`, `reserve_expanded` (the capacity for a rule's full
expansion, returning the count) and `expand` (`apply_expand_rule` verbatim).
`Partials` restores and expands on it per note.

**What `Partials` keeps** (`partials.h`, 1,148 → 561 lines): the eight
envelope pins and twenty-seven settings, the stream map (`PartialsRngLayer`),
the layout, the expand rule and its stream, the per-note detune, the five
layer flags, the six layers, the per-partial phase state, the two caches, the
render body `partial_value_impl` with its order of operations unchanged, and
the array protocol (`init_arrays` / `update_arrays` / `arrayUpdateReq_`).

**One type per file**: `expand_rule.h` (`ExpandRule`), `expand_rule_node.h`,
`partials.h`, `full_partials.h`, `sequence_partials.h`, `explicit_partials.h`,
`composite_partials.h`. `full_additive_source.{h,cpp}` are `additive_source.{h,cpp}`
(`git mv`, the class renamed to its type name); the alias header that spelled
the old name is gone. `formant.h` and the four spectra are untouched (section 7).

## 3. The seven tasks, commit by commit

| Task | Commit | Who | What |
|---|---|---|---|
| 0 | — | Fable; Opus 5.5 for the set | `cli_pre38.exe` (the unchanged 11-05 binary), the ab36 arm reused as the old arm, the 131-patch list; thirteen synthetics and thirteen feature-off controls, every synthetic differing from its control; the replacement baseline authored and rendered |
| 1 | `2dfabe2` | Opus 5.5; Fable's patch | the two types deleted with registrations, loader branch, fourteen branch keys, four accessors, editor strings, tests, three tool scripts' type lists; `retired_type_hint`; the tool's `Refuse` rule (two tests); the baseline retired, the replacement in the manifest |
| 2 | `e900c73` | Fable | `PartialSet`; `Partials` and `CompositePartials` rebased; `Slot<PartialSet>`; the lifecycle test |
| 3 | `745eaaf` | Opus 5.5 | one type per file, eight spans verbatim; the rename; the label |
| 4 | `5f25207` | Fable | `dsp::SegmentWalk`, `CoherentWalk`, `PairTrade`; three twins |
| 5 | `d4218e0` | Fable | `BandwidthNoise`, `OnsetGate`, `DecayLaw`; three twins |
| 6 | `e27f7ad` | Fable | `PartialLayout`; the expansion twin; the array members renamed |
| 7 | the docs commit | Opus 5.5 for the runs; Opus 5.5 reviewer; Fable | the close A/B, the meter, the timing; the review; this report |

Every engine task was verified by the coordinator before its commit: the
null gate rerun, the synthetic A/B, and — for every twin — one mutation of
the layer watched to fail at the twin's `CHECK(bad == 0)` and restored.

## 4. The proofs

- **Null gate** (`null_gate_perform_source.py --jobs 3`, 78 manifest
  patches): 78/78 identical after each of Tasks 1–6 (Task 1 swapped the
  retired entry for the replacement's hash, rendered by the pre-3.8 CLI and
  matched by every later build).
- **Close A/B** (`ab_render_set.py`, the 131 library + baseline patches,
  against `cli_pre38.exe`): 131 of 131 identical, 0 DIFFER. The replacement baseline is NEW (no
  old render) and its close render equals its manifest hash.
- **The synthetic set** (`renders/scratch/ab38/synth/`, thirteen instrument
  patches, two notes each — motion, shimmer, trade, onset, decay,
  inharmonicity, bandwidth with a wired `bwEnv`, an expand rule, a
  `CompositePartials`, a `SequencePartials`, an evolving `ExplicitPartials`
  with five wired pins, everything at once, a formant spectrum): 13/13
  identical to the pre-3.8 arm after every task. Thirteen feature-off
  controls proved each feature reaches the render (every synthetic's hash
  differs from its control's).
- **Seven twin tests** (3.3's method: the deleted block retyped as it stood,
  both sides on identically seeded `Randomizer`s, `same_bits` per output):
  the segment walk (48,000 rows), the coherent walk (252,000), the pair trade
  (10,800), the bandwidth noise (110,400), the onset gate (28,800, both signs
  of the input and the `t <= 0` rows counted), the decay law (128,000) and
  the expansion (54 rule × size × seed cases, every array). Each was mutated
  once by the coordinator and failed at its own `CHECK`: a reordered draw, a
  reassociated mix, a swapped pair index, a reassociated smoothstep, a
  multiply for the onset's assignment (the sign of the zero), a float decay
  law, a wrong spacing index and a dropped centre copy.
- **Allocation gate** (`alloc_gate.py --jobs 3`): 132 patches, 0 allocate,
  after every task (every layer sizes itself at `prepare(n)` and fills
  within that at `start`); the layout's `reserve_expanded` + two expansions
  under the probe count nothing (in the twin).
- **`engine_tests`**: 7462 → 7375 (the deleted types' tests) → 7405 → 7411 →
  7418 → 7908 (the twins; the layout twin checks `restore` per row), ALL PASS.
- **`gates --fast`** after every task: ctest 5/5, the stamp current, the
  allocation gate PASS, and the one standing failure (the roundtrip smoke's
  `wiring` diff, 5/6 shapes, since 2.4).
- **Timing** (`ab_render_time.py`, three rounds, `cli_pre38.exe` vs the
  close build):

  Wall time (the real measure for instrument patches, which pre-render at
  load) and the `render=` line, minimum over the rounds, new over old:

  | Patch | Wall | Render | Spread old / new |
  |---|---|---|---|
  | `strings/viola_default` | 0.98 | 1.10 | 1.0 / 2.5 % |
  | `strings/cello_full_range` | 1.08 | 0.94 | 1.3 / 3.7 % |
  | `winds/clarinet1` | 1.00 | 0.90 | 10.7 / 8.8 % |
  | `baselines/fadd_explicit_test` | 1.02 | 1.04 | 4.6 / 4.8 % |
  | `baselines/fadd_sequence_test` | 1.03 | 1.02 | 25 / 28 % |
  | `voice/sing_bass_A` | 1.00 | 0.90 | 1.6 / 1.5 % |
  | `voice/speech_m_AH` | 1.01 | 0.97 | 12 / 5 % |
  | `voice/words/liar1` | 1.01 | 1.00 | 2.2 / 3.2 % |
  | the tool's fixed twelve (`ab_render_time.py`, one additive) | 0.95–1.20 | — | — |

  One finding, found and fixed before the seal: `fadd_explicit_test` (seven
  partials, two wired pins) was slower in every paired round — render 1.04,
  1.12 and 1.14 over 3-, 5- and 10-round runs, the new arm above the old in
  nine of ten pairs, medians 89.8 → 100.6 ms. The cause was the reviewer's
  flag: `PartialSet::next` walked every pin of the set with a kind branch per
  sample (eight Params and some thirty Settings) where `Partials::next` made
  eight calls, and a seven-partial patch has little per-partial work to hide
  that behind. The second fix wave (`268fb0a`) lists the Param pins at
  `prepare` into a fixed array inside the node — `Component`'s own sweep
  idiom — and `next` walks the list. Ten paired rounds after it, on a noisy
  machine: `fadd_explicit_test` render 0.99 (at or below the old arm in nine
  of ten pairs, the tenth an outlier on both arms), `cello_full_range` 0.96,
  `viola_default` 1.02. `cello_full_range`'s 1.08 wall on the first run (tight
  spreads) did not recur on three later runs.

## 5. The meter (`--delta` from the base `4d32899`)

| Measure | Before (`4d32899`) | After (`e27f7ad`) | Change |
|---|---|---|---|
| files | 211 | 221 | +10 (fifteen added, five removed) |
| lines | 61,833 | 61,911 | +78 |
| files_over_limit | 19 | 18 | −1 (`partials.h` 1,148 → 561) |
| functions_over_limit | 98 | 98 | +0 |
| dynamic_cast | 63 | 63 | +0 |
| const_cast | 10 | 10 | +0 |
| include_violations | 14 | 14 | +0 |
| unmapped_files | 0 | 0 | +0 |
| duplicate_blocks | 163 | 161 | −2 |

The committed snapshot at `4d32899` was 3.6's (only docs changed between),
so `--delta` measures the whole checkpoint. After the two fix waves only
`lines` differs (61,929). `tools/structure/metrics.json` is regenerated at
the docs commit.

`partials.h`'s longest functions: `partial_value_impl` 149 → 134 lines (the
bandwidth and onset blocks left; the anti-result comments stayed with the
code they describe; still over 80), `on_start` 131 → 72 (it was `start`),
`ensure_partial_cache` 30 → 22; `apply_expand_rule` (56) and `next` (42) are
gone from the file. Among the fifteen new types the meter lists a test-local
`struct W`, the twins' retyped walk.

## 6. Decisions the session made (2026-11-02 standing rule)

Each is understood and reversible; none is a REVIEW item.

1. **The executor and reviewer tier is Opus 5.5.** Matt rescinded the "never
   Opus 5/5.5" rule on 2026-11-06 ("just delegate to Opus 5.5 agents") while
   this plan was being discussed; ruling 12 and the Execution paragraph were
   amended before Task 0. The refuter also ran on Opus 5.5, read-only: the
   Fable weekly window stood at 93 percent at the close (ruling 13, added at
   the close).
2. **The label "Full" is gone.** With "Basic" and "Alternate" deleted the
   `AdditiveSource` registration had nothing to be distinguished from; the
   derived label "Additive" stands (ruling 9).
3. **The layers' sizing method is `prepare(n)`, not the table's `reserve(n)`.**
   It assigns, as `prepare_note_arrays` assigned those vectors before; a
   method that assigns should not be called reserve. The contract is the
   same: sized at prepare, filled within capacity at start.
4. **Fourteen branch keys left `kBranchKeys`**, not the eight ruling 10
   guessed. The meter's scrape (`structure_meter_tests`) is the authority:
   `envelope`, `filter`, `from`, `to`, `partials` and `partialCount` were read
   by the deleted branch alone (the ruling thought them shared). None restored.
5. **One comment inside the moved `Partials` span was edited** after the
   body-diff ("the additive source used to run" for the deleted class's name);
   the executor rightly left it, since the move must be verbatim, and the
   coordinator changed it in the same commit with the diff recorded.
6. **`<cstdint>` in the three subclasses** (their constructors name
   `uint32_t`) and no `<vector>` in `composite_partials.h` (it names no
   `std::`): the executor's reading of "name what you use", kept.
7. **The retired baseline's folder is `patches/old/additive_source2/`**, not
   the 09-14 cull folder that holds its four siblings: the folder names the
   type, the cull folder names a date.

## 7. Debt and future cleanup

- **The formant half of CLEANUP 17.** The convention is recorded (ruling 2:
  a part interface is an abstract `Component` base named for what the part
  is, no I-prefix, owning the pin lifecycle); `IFormant` and its five
  implementers still hand-forward their pins. The move is mechanical and
  follows `PartialSet`'s shape; the base's name is REVIEW R14 item 2
  (non-blocking).
- **The editor** still casts `dynamic_cast<Partials*>` for the strip and
  keeps three hand lists of the part types, `is_partials_type` lacking
  `CompositePartials` (pre-existing; Part 7).
- **`get_array`'s `const_cast`** (the one in the additive folder) is
  pre-existing and untouched.
- **`partial_value_impl`** keeps its anti-result comments and stays over the
  function limit (section 5); the extraction removed its two largest blocks,
  the comments stayed with the code they describe.
- **`duplicate_blocks`** rose with the split's preamble scaffolding, as at 3.6
  (CLEANUP 31).
- **No new byte-identity preservation.** This checkpoint added no guard that
  exists only to keep a render identical; nothing joins the "retire after
  Part 8" list.
- **Line endings:** the edited files are LF in the working tree where the repo
  normalises to CRLF (git warns at every touch); cosmetic, as at 3.6.

## 8. The review (R14)

An Opus 5.5 reviewer with a fresh context — the first under the 2026-11-06
rule — read-only: it built nothing, ran no binary and performed no mutation
(the coordinator's eight mutations in section 4 are the mutation record).
Ruling 13: the Fable weekly window stood at 93 percent at the close. Its
oracle was this plan's eight Review Focus items, the ten-item brief and the
Ledger; it read every new header, the base `partials.h` and the diff
`4d32899..e27f7ad` whole.

**Verdict: 0 Critical, 0 Major, 10 Minor, 11 Flag.** In its words: no render
can move — every moved block matches the base text — and no change to the
real-time rule was introduced. It compared every twin's retyped block line by
line against the base file; diffed `partial_value_impl` block by block
(motion, cutoff before the bandwidth call, phase, rolloff, formant, fade,
decay, bandwidth, shimmer, trade, onset, the sine); checked every layer's
stream seeding order against the base block; diffed `kBranchKeys` at the base
and the close (fourteen removed, none added, none read by a surviving branch);
confirmed the rename touched every former array member and no `mult1.values`.

The Minors were gaps in the twins' grids and the record, each with the
mutation the twin would have missed: the pair trade's length formula at
integer Hz only (`int(rate) / int(hz)` would pass); the coherence grid on the
three values where `ind - coh * ind` is exact; the layout twin never
exercising `restore` before an expansion and checking one array after it,
with every phase offset non-negative (`x - floor(x)` for `fmod` invisible);
a comment claiming state was compared; a wrong row count in the Ledger; the
two retired hints untested through the loader; four comments in `partials.h`
describing the old members; a checkpoint number in a test namespace; a
comment orphaned by an insertion; the editor's lambda still named for the
deleted interface.

**The fix wave (`b254b5c`)** took every Minor and four Flags: the pair
twin runs 0.37 and 7.3 Hz; the coherent twin runs two (rate, Hz) pairs and
coherence 0.4 and 0.59; the layout twin starts every expansion from
`restore`, uses negative phase offsets and compares all six arrays after the
final restore; the segment twin's evolve grid gains 0.1; the decay twin runs
44.1 kHz as well; the hint is asserted for both names and a loader test
expects "Unknown node type: AdditiveSource2" with the hint; the comments,
the namespace (`partials_twin`), the orphan and the editor's name
(`is_partial_set`) fixed. ALL PASS (7588), null gate 78/78, gates as before.

**Re-review (the same reviewer): closeable.** Nothing Major remains and
nothing Minor in code. Two tokens `Slot<IPartials>` in Part 3 sections 2 and
7 — fixed in the docs commit. One thing the wave introduced, by the
coordinator: the measurement executor had staged the close's meter snapshot
(`tools/structure/metrics.json` taken at `e27f7ad`) and the wave's commit
carried it; the Ledger says so and the snapshot is retaken at the docs commit
so the committed snapshot matches the sealed tree.

**The second wave (`268fb0a`), the same reviewer: closeable.** The pin list
is byte-identical to the base's eight calls (registration order is
declaration order; only `Param` pins are listed; no subclass declares one;
`CompositePartials` lists none and forwards in its hooks); nothing allocates
or locks on `next`, and the idiom mirrors `Component::kMaxAutoSources`, with
the same caveat (past the cap a Release build writes out of bounds; the
count is eight); no caller in the engine, the tests or the editor advances
a set before `prepare`. One latent hazard, record-only: `Component`'s
`operator=` is a no-op, so a `PartialSet`'s defaulted copy-assignment would
copy the pin list from the source object until the next prepare; nothing
assigns a set today (the engine's one `*this =` is `Envelope`'s) — CLEANUP 39.

**Flags left as record**, the reviewer agreeing: bandwidth, onset and pair
trade twins still run at 48 kHz only (a layer hard-coding the rate would pass
them; the coherent and decay twins now catch that class); `PartialSet::next`
walks every pin of the set per sample where `Partials::next` made eight direct
calls — measured, and real: `fadd_explicit_test` about ten percent slower before the second fix wave, flat after it (section 4); the `ExplicitPartials` live-edit growth path
(an array edit after `prepare` with no prepare before the next note) is the
base's, CLEANUP 33's class; `BandwidthNoise::factor` is `SegmentWalk::advance`
with evolve 0 but one fewer draw, so the two cannot be merged byte-identically
(CLEANUP 38), and the segment-length formula is written twice; the plan
table's "two-note embedded score" for the replacement baseline was wrong
(neither file has a score; both render four seconds in mixer mode); the
Ledger's "advancing nothing of its own" for a composite is unobservable (it
has no `Param` pin) — the test proves no double-forward.

## 9. Reading guide

1. `engine/include/mforce/source/additive/partial_set.h` (71 lines) — the
   base: three non-virtual verbs over `pins()`, three work verbs, three hooks.
2. `engine/include/mforce/core/dsp/segment_walk.h` (47) — the primitive;
   then `coherent_walk.h` (60): a layer's whole shape in one page.
3. `engine/include/mforce/source/additive/partials.h` — `on_start` (the
   layers started inside their flags), `on_next` (the advances), and
   `partial_value_impl` (the body reading them); then `partial_layout.h`
   (113) for the expansion.
4. `tools/engine_tests/main.cpp`, `run_onset_gate_twin_tests` — a twin in
   full, and why its rows with `t <= 0` matter.
5. `patches/baselines/additive_bright_attack_test.json` beside
   `patches/old/additive_source2/as2_bright_attack_test.json`.

## 10. C++ lessons

- **A layer is a value member, not a node: composition where inheritance
  would be wrong.** `Partials` *has* a `CoherentWalk`; it is not *a kind of*
  anything the walk is. The layer has no pins, no descriptor, no registry
  entry; it is owned by value (`CoherentWalk motion_;`), lives inside the
  node's own memory, and costs the per-partial loop one array index, exactly
  what the inline block cost. The C#/Java analogue is a strategy or helper
  object held as a field — never a `MonoBehaviour`, never something the
  editor could wire. The anti-pattern the design rejected (F169's "each
  layer's own descriptor list") would have made every layer a graph citizen
  for a gain of nothing the editor can use. Inheritance was right for
  `PartialSet` (every set *is a* set of partials with one lifecycle) and
  wrong for the layers (a set is not a walk).
- **An abstract base with a template method versus a pure interface.**
  `IPartials` was a Java/C# interface: pure virtuals, no state, no
  behaviour. `PartialSet` is an abstract class: it *does* something
  (`prepare` walks the pins, then calls the hook) and leaves the rest to the
  derived type. Java has both keywords; C++ has only `virtual` and `= 0`, so
  the distinction is a design choice written in the body, not the
  declaration. The reason to choose the abstract class here is the
  forgotten-override class 3.4 named: with the interface, every implementer
  rewrote the eight-pin forward three times and a ninth pin had to be added
  to all three or it silently never prepared; with the base, a pin that
  registers itself is prepared by the base's loop, no override needed.
- **`reserve` versus `assign` as a contract between an owner and a layer it
  holds by value.** The real-time rule says `start` may not allocate. A layer
  the owner holds by value cannot know the partial count until the owner
  tells it, so the owner calls `layer.prepare(n)` once (which assigns to
  size n — allocation allowed here) and `layer.start(n, …)` per note (which
  assigns within that capacity — no allocation). The contract is invisible
  in the code — `assign` looks the same in both — so it is stated in the
  comment and proved by the allocation gate and the probe in the layout's
  twin. In C# or Java the GC hides this entirely; a `List<float>` resized in
  a hot loop is a performance bug there, not a correctness rule.
- **Signed zero: why `x = 0.0f` and `x *= 0.0f` differ bit for bit.** IEEE
  float has +0 and −0. An assignment writes +0; a multiply of a negative
  value by zero gives −0. Both compare equal, both print as 0, and a 16-bit
  WAV hash cannot tell them apart — but `fast_sin_turns(pos) * pampl` with
  `pampl` = −0 is −0, and `same_bits` sees it. That is why `OnsetGate::apply`
  keeps the assignment the original had, why the onset twin feeds both signs
  of the input, and why the coordinator's mutation of that one line failed
  the twin. The rule for a byte-identity refactor: every moved expression
  keeps its operations, and "equivalent" arithmetic is not identical
  arithmetic.
- **The most vexing parse** (a harness lesson from Task 4).
  `std::vector<W> walks(size_t(n));` does not construct a vector of n
  elements; C++ reads `size_t(n)` as a parameter declaration and the whole
  line as a function declaration, and the first use of `walks[i]` fails with
  "subscript requires array or pointer type". `static_cast<size_t>(n)` or
  braces `walks(size_t{n})` disambiguate. Java and C# have no such ambiguity
  because a declaration and an expression cannot look alike there.
