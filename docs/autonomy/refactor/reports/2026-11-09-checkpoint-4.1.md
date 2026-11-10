# Checkpoint 4.1 — the seam's render half; `hiBoost` (report R16)

**Landed 2026-11-09 on main, `e5518ef`..`7b32205` with the review's fix
waves `e0cb2e2` and `3c012ea`** (base before 4.1 `e83e520`, the commit that carries the Part 4 document and this checkpoint's
plan; plan `docs/superpowers/plans/2026-11-09-part4-checkpoint-4.1.md`, its
Ledger the record of every ruling and every task). Part 4 section 8.1's first
row, as decided with you on 2026-11-09: the struct the loader hands back is
`BuiltSound` and render's live mixer is `Mixer`, which frees `Patch` and
`StereoMixer` for the data model; the instrument voice loop, written twice in
the loader, is written once; the embedded score is performed by
`play_embedded_score` in a file of its own; the `mix` lookup and the second
whole-graph build behind it are gone; one mono adapter remains; `hiBoost` is
deleted, and with it the dead `--test-ornaments` mode. No patch file was
touched. No patch render moved. Every composition render that set the field
moved, which is your 2026-10-05 decision and the one thing in this checkpoint
for your ears.

Reading guide: fifteen minutes. Section 1 is what you can hear and what you
cannot. Section 2 is what the three new names are the first half of. Section 6
is what the session decided under the 2026-11-02 rule, including two places
where the plan's own proof was weaker than it said. Section 8 is the
independent review. Section 10 is the C++ lessons.

## 1. What changed for you

- **Every composition render that set the field moved, by one rule.**
  `hiBoost` was a field on `PitchedInstrument` that the command-line tool's
  composition modes and `test_figures` set to 0.3 by hand: `--melody`,
  `--compose`, `--play` and `--dun` on their instrument, `--josie` on its
  guitar part only (its bass, kick and snare never had it), `--chords` only
  when its `--hiboost` flag was given. It multiplied each note's voice gain
  by `1 + 0.3 × (log10(max(f, 100)) − 2)`: nothing at or below 100 Hz,
  +1.0 dB at middle C, +1.5 dB at A 440, +2.3 dB at C6, +3.4 dB at the top of
  the keyboard. A patch never saw it and no patch file could set it. With the
  field gone each composed note's voice is mixed at its velocity alone, so a
  note above 100 Hz enters the mix lower by the amount in that list and the
  balance between registers is the patch's own. The composition is
  unchanged: all 21 piece JSONs of the comp template gate are byte-identical
  before and after.
- **But the renders are not the old ones with the upper notes turned down:
  from the second or third note on, every note is a different realisation.**
  This is the finding of the close, and it is about the engine, not about
  `hiBoost`. Fitting the new Ode to Joy onto the old, window by window: the
  first note is the old one times 0.9258 with nothing left over, and the
  second the old one times 0.8946 — exactly the gain law at F♯3 (185.0 Hz)
  and B3 (247.0 Hz). From 0.8 seconds on the two renders are uncorrelated
  (90 to 100 percent of each window is left over after the best fit).
  `template_mary` shows the same: its first note times 0.8655 (the law at E4),
  then no fit. The cause, read from the code: `piano_default` has
  `"polyphony": 1`, so one voice plays every note of the piece, pre-rendered
  one after another; the ordinary note path restarts the voice's graph for
  each note (`start`) but does not re-anchor its random draws (`reseed` is
  called only for a held line's continuations), so at least the noise
  streams run on from wherever the last note left them; and each note is
  rendered until its *gained* level falls under the ring-out floor, so its
  length in samples depends on the gain. Change the gain and a note ends at
  a different sample, the next note finds the voice's generators at a
  different point in their sequences, and it comes out as a different
  waveform: the same pitch, duration and velocity, a different draw. (The
  filters', the string's and the reverb's state carry the same way; how
  much of the difference is the noise and how much that residue was not
  separated here.) In 250 ms
  windows the level of new against old runs from 0.34× to 2.7× on Ode to Joy.
  So the before/after pair cannot be heard as "the same performance,
  re-balanced": it is a second take. Over the 21 templates the whole-render
  RMS moved between −1.32 dB and +1.08 dB (median −0.47 dB): 14 fell and 7
  *rose*, which the gain law alone could never do. (The three templates that
  add an oboe melody also pass through `--compose`'s multi-instrument
  normaliser, which scales a mix whose peak exceeds 0.98; their peaks sat at
  the ceiling before and at or just under it after.) Section 6 decision 22 has the
  question this raises; it is yours, and it is not this checkpoint's to
  answer.
  **The pair for your ears**:
  `renders/scratch/ab41/review/old/template_ode_to_joy_1.wav` (the base build)
  against `renders/scratch/ab41/review/new/template_ode_to_joy_1.wav` (the
  close build). REVIEW R16 item 1. Every other template's pair is on disk in
  `renders/scratch/walk3_null_gate/pre41/` and `post41/`.
- **Nothing else is audible.** The 132 library and baseline patches render
  byte-identically through `mforce_cli <patch> <wav>` (132 of 132, 0 differ).
  The editor never set the field, so nothing it plays moved.
- **Nothing is visible in the editor.** It names neither renamed type; its
  two comment lines about the engine's mixer follow the rename.
- **The command-line tool lost two flags.** `--hiboost` (on `--chords` only)
  and `--test-ornaments`, which loaded `patches/PluckU.json`, a file removed
  on 2026-08-10, and has failed since. `mforce_cli --test-ornaments` now
  prints the usage line. A stale `--chords … --hiboost 0.3` is read as two
  more chord tokens and fails in chord parsing, not as an unknown flag.
- **A plain-sound patch can take its sound from any signal.** A
  `SoundChannel` whose source was an `Envelope`, a `CurveNode`, a `VarSource`,
  a `RangeSource`, a `Vibrato` or a `NameGate` failed with "No mono source
  for", and the same node as a bare `graph.output` failed with "must be a
  StereoMixer or a mono-renderable source"; both now load and render. No file
  in scope used one (every in-scope file loaded before), so this widens what
  loads and moves nothing.
- **One file shape renders differently, and no file has it.** An
  instrument-form patch that also carried a `StereoMixer` or `SoundChannel`
  node named `mix` with `gainL` or `gainR` had those gains applied to the
  instrument's mixer (and a malformed gain there failed the load); now the
  node is ignored like any other node nothing consumes. The census over all
  3,146 patch files finds seven instrument-form files with a node named
  `mix`, none of that shape (section 6, decision 4).
- **Two channels naming one source is now refused by name.** It crashed at
  load before this checkpoint (section 6, decision 17).
- **Found in passing, for your information.** Nine in-scope instrument
  patches carry no score, so `mforce_cli` renders five seconds of silence for
  each (`bell_fast_decay`, `bell_sweep`, `inst_spacy`, `ep_belly`,
  `ep_bright`, `ep_dark`, `ep_std`, `hi_hat1`, `kick_studio_dry`): BACKLOG
  row 10 already counts them; what is new is that every gate so far proved
  only that they load (section 4 says what this close did about it). And
  `scores/baselines/test_k467_walker.json` is prose notes, not JSON, so the
  comp template gate has 21 templates, not the 22 the documents say.

## 2. The shape

Part 4 separates a patch as *data* from the thing built from it. The data
side — `Patch`, `Graph`, `Node` — does not exist yet; 4.6 writes it. What 4.1
lands is the other side of the seam, the types a build hands to a host, under
the names section 2.2 gave them, so that when the data model arrives its names
are free and the hosts already hold the result types.

| Name | What it is | Where | What it will be |
|---|---|---|---|
| `BuiltSound` | what the standalone render builds: `sampleRate`, `frames`, a `unique_ptr<Mixer>` | `render/patch_loader.h`, renamed in place | the return of `build_sound(const Patch&)` in `render/build_sound.h` (5.8); the header it sits in today is deleted at 4.7 |
| `Mixer` | render's live mixer: channels, two gains, `prepare`, `render`, `collect_faults` | `render/mixer.h`, `mixer.cpp` | unchanged; the word `StereoMixer` now means only the node type in a file, which 4.5 registers as a part |
| `play_embedded_score(inst, scoreText) → double` | performs the short score a patch file may carry and returns its end in seconds | `render/embedded_score.h`, `engine/src/embedded_score.cpp` | called by `build_sound` (4.6) until Part 5's play function replaces it; BACKLOG row 10 asks where a patch's test notes live |

**The loader after 4.1** (`engine/src/patch_loader.cpp`, 1,702 → 1,537
lines) reads as four functions where it read as two long ones:

- `index_graph(root)` — the file's nodes by id, their order, and the output's
  id, in an `IndexedGraph`. It was written twice.
- `build_instrument_json(root, minPolyphony)` — the instrument block read and
  the voice pool built, each voice its own instance of the graph, then one
  `prepare`. It was written twice, 62 lines each, the meter's largest
  duplicate; the two differed in one expression, the polyphony floor, which
  is 0 on the render path.
- `load_patch_file(path)` — the three forms: an instrument (the call above
  with floor 0, the embedded score, one unity channel), a bare signal as the
  output (one unity channel around it), a `StereoMixer` node over
  `SoundChannel`s.
- `load_instrument_patch_json(text, minPolyphony)` — parse, the "no
  instrument section" refusal, the call.

**The embedded score** (`embedded_score.cpp`, 113 lines) is the loader's
`perform_score_json` moved whole, plus the loader's second walk of the same
score that computed where it ends. Two file-local functions sit behind the
public one: `perform_notes`, the delivery loop, verbatim but for the one line
that named the JSON root; and `score_end`, the walk in double. The public
function parses the text, performs, and returns the end, so the loader does no
arithmetic over the score any more; it keeps only the frame rule (the render
covers at least the score; a larger `seconds` extends it). The function takes
*text* because the module that will hold `Patch::score` never parses it (Part
4 section 3.5), and landing the final signature now spares a second change at
4.6. Section 10 says why the header carries no JSON type.

**One mono adapter.** The live mixer pulls `MonoSource`s; a graph signal is a
`ValueSource`. The loader had two adapters between them, identical but for the
pointer type each held, and made one for *every* signal of *every* build — the
voice builds included, where nothing ever used them — through `add_mono`,
which seven of the eleven hand-written branches forgot to call. Now there is
one adapter (`ValueSourceMono`), made in exactly the two places a plain sound
consumes a signal: the bare output and each channel's source. A part in either
place is refused by the wording every other signal position already uses
("'x' is a FullPartials, a part, not a signal").

**The `mix` lookup is gone.** An instrument-form load looked for a node named
`mix`; if it had `params`, it built the *whole graph a second time* — every
node, a throwaway voice context — only to read `gainL` and `gainR` off that
node. In all seven files that have such a node it is the output, a
`MultiSource` or a `CombinedSource`, with no gains, so the lookup returned the
constant 1.0 the mixer's constructor already installs. Part 4 section 5.8
decided the instrument form is one unity channel.

**Deleted**: `WaveSourceMono`, `add_mono`, `GraphResult::monoNodes`, the `mix`
lookup, `perform_score_json` and the second score loop from the loader, the
loader's five `music` includes (three moved to the score file, two were dead),
seven keys of the loader's allowlist, `PitchedInstrument::hiBoost` with its
two uses, `--hiboost`, six host assignments, `test_ornaments` (114 lines, 118
with its banner) and its dispatch.

## 3. The tasks, commit by commit

| Task | Commit | Who | What |
|---|---|---|---|
| 0 | `e83e520` | Opus 5.5 | the plan committed with session XII's documents (the base); the base build and `cli_pre41.exe`; ab39's arm reused as the old arm under a five-file spot check (5 of 5 matched); the comp stage `pre41`; the census |
| 1 | `e5518ef` | Opus 5.5; Fable's split and test edits | `play_embedded_score` in its own file; the loader's `music` includes gone; `kBranchKeys` pruned by the measured scrape |
| 2 | `34092d8` | Opus 5.5 | `index_graph`, `build_instrument_json`; both entry points call it |
| 3 | `22d756d` | Opus 5.5 | one adapter at the two consumption sites; the `mix` lookup deleted |
| 4a | `f480370` | Opus 5.5 | `BuiltSound`, `Mixer` |
| 4b | `7b32205` | Opus 5.5 | `hiBoost` and `--test-ornaments` deleted |
| 5 | the docs commit | Opus 5.5 for the runs and the review; Fable | the mutation run, the close A/B, the comp gate, the meter, the timing; the review; this report |
| fix wave | `e0cb2e2` | Opus 5.5 | the review's six Minors and the uncaught mutation: the two-channel refusal, the delivery and other tests, `ScratchPatch`, three comments; fourteen mutations; the close arms again |
| second wave | `3c012ea` | Fable | the re-review's one Minor: `ScratchPatch`'s destructor must not throw |

The split was the plan's as you amended it ("Yes, amend the plan"): Opus 5.5
executed every task and Fable coordinated. No executor committed. Each left
the tree ready and reported; the coordinator then read the diff against the
previous commit, body-diffed each moved block against the base, reran the null
gate itself, made whatever edits its reading called for (section 6), and
committed. `git status` before every commit showed only the task's files
staged; `GOALS.md`, your uncommitted edit, stayed unstaged throughout.

## 4. The proofs

- **Null gate** (`null_gate_perform_source.py --jobs 3`, 78 manifest
  patches): 78/78 after each of Tasks 1, 2, 3, 4a and 4b, run by the executor
  and rerun by the coordinator before each commit. After 4b it is the
  rendered form of the arithmetic claim for the path it renders: the
  standalone render never set `hiBoost`, so the pre-render path's
  `pn.velocity * (1.0f + 0.0f)` was already `pn.velocity` to the bit. The
  streaming path's line, `velocity * (1.0f + 0.0f) * volume`, is equal to
  `velocity * volume` by the same arithmetic (section 10), but no gate
  renders through it; the fix wave's test pins it.
- **Close A/B** (`ab_render_set.py`, the 132 library + baseline patches, the
  base's renders against the close build's): 132 of 132 rendered, 132
  identical, 0 DIFFER at `7b32205`, and the same again at the fix wave's
  commit `e0cb2e2`.
- **The score-less nine.** Nine of the 132 are instrument patches with no
  score; their renders are silence under any build, so the A/B and the null
  gate prove only that they load. The plan leaned on three of them — the
  electric pianos, "render to their null-gate hashes" — as the rendered proof
  that the `mix` lookup was inert; it is no such proof (all three share one
  hash, that of five silent seconds). This close renders the nine with a
  three-note score appended (C3, C4, C5 at three velocities), in scratch
  copies under `renders/scratch/ab41/scoreless/`, under the base CLI and the
  close CLI: 9 of 9 identical, and none silent (RMS 0.039 to 0.187, peaks
  0.59 to 0.98). That is the rendered proof for the electric pianos and for
  the standalone gain line on nine files no gate had ever heard. Identical
  again at `e0cb2e2`.
- **The comp template gate** (`comp_template_null_gate.py`, the templates of
  `scores/baselines/` through `--compose` on `piano_default`): `post41`
  rendered 21 templates (`test_k467_walker` fails to parse, as always);
  against `pre41`, 21 of 21 piece JSONs identical and 21 of 21 WAVs
  different, every duration unchanged. Per template the RMS and peak of both
  WAVs and the ratio are in `renders/scratch/ab41/comp/rms.txt`: −1.32 dB
  (`template_mary`) to +1.08 dB (`template_mary_crawl`,
  `template_mary_structural`), median −0.47 dB; section 1 says why seven
  rose. `post41` is the stage the next comparison starts from; the fix wave's build
  rendered the 21 again (`final41`) and every WAV and JSON equals `post41`'s.
- **`engine_tests`**: 7,639 on the base → 7,746 (Task 1: the score's end, the
  piano invariant as a pair, the phrased refusal, the frame rule, the text
  identity over the 96 in-scope scores) → 7,750 (Task 2: the voice pool's
  size from the file, the default and the floor, through both entry points) →
  7,769 (Task 3: six channel sources, the unity mixer, the two refusals that
  kept their wording), unchanged by 4a and 4b → 7,792 (the fix wave: the
  delivery, the end and its rounding, the pool's floor both ways, the
  adapter's clock, the unity constants, the live gain, the two-channel
  refusal). ALL PASS at every commit.
- **Two tests watched failing first.** The six channel-source cases failed on
  the Task 2 build with "No mono source for:"; the unity-mixer case failed
  there because the lookup applied the quarter gain. Task 1's header test
  failed the build with C1083 until the file existed.
- **Mutations, first run** (each applied to `7b32205`, the detector run, the
  code restored; an Opus 5.5 executor): seven of eight caught.

  | Mutation | Caught by |
  |---|---|
  | the no-`time` rule made 0 in `score_end` | four checks: the 1.75 end, the 2.0 end, two frame counts |
  | the end's `max` dropped | the 3.0 end |
  | `hold = false` deleted from the piano invariant | the held-then-articulated pair |
  | the frame rule's `max` dropped | the `seconds: 5` frame count |
  | the polyphony floor dropped | the floor-5 pool size |
  | the held and ordinary gain made `1.0f` (`play_note`) | the null gate: 42 of 78 differ |
  | `clock.tick()` deleted from the adapter | the null gate: 27 of 78 differ |
  | `* volume` dropped from `prepare_voice_at`'s gain | **nothing** — 78/78 and ALL PASS |

  The one that survived is the streaming path's gain, which only the
  editor's live keyboard and the allocation gate reach; no test pinned
  `volume` there. The fix wave pins it (`prepare_voice_at(...).gain` is
  `velocity × volume`).
- **Mutations, second run** (against the fix wave's tests, the same
  executor method): fourteen, each caught by a named check — the unphrased
  note's `prevEnd` deleted; the velocity default made 0.5; `finish_open_lines`
  deleted; the onset term removed from the phrased condition; `score_end`
  accumulated in `float`; `lround` made a truncation; the floor replacing the
  file's polyphony; the render path's floor made 1; the adapter given no
  clock; the adapter's `start(frames)` deleted; the instrument form's channel
  volume made 0.5; the channel wrap site naming the output's id; `* volume`
  dropped from the streaming gain; the new two-channel refusal deleted. The
  raw results are in `renders/scratch/ab41/fix/mutations.txt`.
- **The text identity.** The loader now hands the score to
  `play_embedded_score` as `root.at("score").dump()`. For each of the 96
  in-scope files with a score the test re-parses the dumped text and checks
  the JSON equal, the text equal, and — for every note's `note`, `velocity`,
  `duration` and `time` — the `float` and the `double` read bit-identical and
  the number's JSON type unchanged. nlohmann writes a double with the
  shortest digits that read back to the same double and an integer as an
  integer, so neither kind of number can change in value under the round
  trip; the test is that sentence run over the scope. (The reviewer found
  the one change of *type* the library makes: a literal `-0` is a signed
  integer on the first read and an unsigned zero on the second, the same
  value to every read the delivery makes. And the test calls nlohmann, not
  the loader: that the loader passes the right subtree is the frame tests'
  and the null gate's to see.)
- **Allocation gate** (`alloc_gate.py --jobs 3`): 132 patches, 0 allocate,
  after every task.
- **`gates --fast`** after every task: ctest 5/5, the stamp current, the
  allocation gate PASS, and the one standing failure (the roundtrip smoke's
  `wiring` diff, 5/6 shapes, since 2.4). **The full gate set at the close**
  (`python tools/gates.py` at `7b32205`): ctest 5/5; the stamp current; the
  allocation gate PASS; the null gate 78/78; the whole-corpus roundtrip 132
  patches, 0 id changes, 0 new render diffs, the 6 known diffs; the wiring
  null gate 78/78 identical after conversion; and the one standing failure,
  the smoke's `wiring` diff.
- **The patch lint** (`lint_patches.py`): PASS over library and baselines
  after the allowlist prune; the structure meter's known-answer test
  (allowlist ⊇ scrape) green.
- **Timing** (`cli_pre41.exe` against the close build, three rounds, the
  minimum of each): flat, as expected for a checkpoint that changed no
  render path — the tool's twelve rows run from 0.92× to 1.08× with no
  direction (`oboe1` 0.99×, `piano_default` 0.98×, `viola_default` 1.00×).
  The three electric pianos' `load=` line, where the second whole-graph
  build went: 1.84, 1.88 and 1.91 ms before, 1.72, 1.75 and 1.69 ms after.
  They are small patches, so the saving is a tenth to a fifth of a
  millisecond; reported, not gated.

## 5. The meter (`--delta` from the base `e83e520`)

| Measure | Before (`e83e520`) | After (`3c012ea`) | Change |
|---|---|---|---|
| files | 221 | 223 | +2 (`embedded_score.h`, `embedded_score.cpp`) |
| lines | 61,855 | 62,100 | +245 (the loader, the instrument and the tools shrank by 303, the score's two files add 129, the tests grew by 419) |
| files_over_limit | 18 | 18 | +0 |
| functions_over_limit | 97 | 96 | −1 (`test_ornaments`) |
| file_level_mutable_vars | 120 | 120 | +0 |
| const_cast | 10 | 10 | +0 |
| dynamic_cast | 64 | 64 | +0 (engine +0) |
| changelog_comments | 241 | 238 | −3 |
| duplicate_blocks | 158 | 156 | −2 (see below) |
| include_violations | 14 | 12 | −2 |
| unmapped_files | 0 | 0 | +0 |

`engine/src/patch_loader.cpp` 1,702 → 1,537 (−165); `instrument.h` 840 → 832;
`tools/mforce_cli/main.cpp` 1,268 → 1,139; `tools/test_figures/main.cpp`
1,211 → 1,210; `tools/engine_tests/main.cpp` 8,808 → 9,227. New types:
`BuiltSound`, `Mixer`, `IndexedGraph`, and the tests' `ScratchPatch`;
removed: `Patch` (the built struct), `StereoMixer` (the live struct),
`WaveSourceMono`.

The include edges outside the map are now twelve: six `music` headers that
include nlohmann, `music/pitch_bend.h` → the envelope, `render/instrument.h`
→ `music/pitch_bend.h` and `music/pitch_curve.h`, and
`engine/src/embedded_score.cpp` → `music/basics.h`, `music/conductor.h`,
`music/music_json.h`. The loader has none.

Duplicate blocks: the 62-line voice-loop pair, the 12-line indexing pair and
the adapter pair are gone from the list; one block naming the loader remains
(18 lines it shares with `tools/mforce_ui/main.cpp`); none names
`embedded_score.cpp`. The count's path was 158 → 159 → 160 → 157 → 157 → 156,
rising twice while duplication fell (section 7).

## 6. Decisions the session made (2026-11-02 standing rule)

Each is understood and reversible; none needed you. Rulings 13 to 18 are in
the plan's Ledger under the same numbers.

1. **The new header's comment carries no Part or BACKLOG number** (ruling
   13). The plan's table described the comment as naming "Part 5's play
   function; BACKLOG row 10"; the plan's own Global Constraints forbid those
   tokens in code. The comment says the function lives there "until the play
   function that performs any score replaces it".
2. **The score file is two file-local functions behind the public one**
   (ruling 14). The executor's first version was one function of 89 lines with
   the delivery loop inside a nested block — one more function over the
   meter's 80-line limit, and a body indented away from its original. Split
   into `perform_notes` and `score_end`, the delivery loop sits at the base's
   indentation and its body-diff against the base is one line.
3. **`kBranchKeys` lost seven keys, not six.** The plan expected
   `articulation`, `hold`, `note`, `onset`, `ornament`, `time`; the measured
   scrape also lost `velocity`, which the loader read only in the score loop.
   The rule was "the measured set", so seven went. No node `params` object in
   the tree (36,169 of them, all 3,146 files) carries any of the seven as a
   key, so nothing that loaded stops loading.
4. **The instrument form's unity mixer is pinned by a test, and the file
   shape it changes is named** (ruling 17). The plan's Ledger 8 said no file
   wanted a gain there; true of the tree. But the lookup was not dead code:
   an instrument-form file with a `StereoMixer` node named `mix` and a gain
   would have had it applied. The test renders that file and the same file
   without the node and requires the same bytes; on the Task 2 build it
   failed at a quarter of the level. Section 1 lists the shape. Three
   refusals went with the lookup (the review's Minor 4): a gain on that node
   that named a missing node, was a string, or named a part used to fail the
   load; such a file now loads with the node ignored.
5. **The electric pianos prove nothing by rendering** — the correction in
   section 4. The census and decision 4's test are the proof that the lookup
   read defaults; the score-less nine with a score appended are the rendered
   proof this close added.
6. **The two refusals for an id that names no built node keep their wording**
   (ruling 16). `build_graph` does not build `SoundChannel` or `StereoMixer`
   nodes, so an output naming a channel, or a channel's source naming nothing,
   reaches the wrap sites as "not found" rather than as a part. Those two keep
   "graph.output must be a StereoMixer or a mono-renderable source" and "No
   mono source for", each pinned by a new check; a part is refused by
   `as_signal`'s wording, as planned. All of it dies with the loader at 4.7.
7. **`root.at("instrument")`, not `root["instrument"]`** (ruling 15), in the
   one function that takes the JSON root by const reference (section 10).
8. **`load_patch_file`'s local is `built`** (ruling 18): a `BuiltSound` named
   `patch` would contradict the name the rename frees.
9. **Two of the plan's comment edits were not the live struct and stay**: the
   editor's "stream the StereoMixer live" sits over code that searches for
   the `NT_STEREO_MIXER` node, and `explore.cpp`'s "Patch mutation by
   dot-path" is about the JSON. The editor took two comment edits, not three.
10. **The frame rule's comment lost its changelog sentence and its backlog
    id** ("Before 2026-09-16 the score end silently overrode `seconds`";
    "backlog 63b"): git holds the history, and the sentence was being re-homed
    beside new code. The sentence that says what the rule is stayed.
11. **One order of refusals moved.** A text with no instrument block is now
    refused as such before its `sampleRate` is read; a non-numeric
    `sampleRate` used to fail first, on its type. Both are refusals of a
    malformed file; no test noticed.
12. **`mforce_cli --test-ornaments` prints the usage line**, not "Patch file
    not found" as the plan's Review Focus 6 expected: the default mode checks
    its argument count before its file. The flag falls to the default mode
    either way.
13. **The comp gate is 21 templates.** `test_k467_walker.json` is prose; the
    driver has printed `rc=1` for it since `eb964a1` and no earlier stage
    holds an output for it. Every "22" in the plan and in Part 4 reads 21.
    Nothing under `scores/` was touched; the file is CLEANUP 49.
14. **The per-task gate criterion is the base's result, not "all green".**
    `gates --fast` prints FAILED on the base because of the smoke's standing
    `wiring` diff; each task was held to: ctest, the stamp and the allocation
    gate PASS, the smoke at 5/6 with that one diff.
15. **The dsp lane's BACKLOG row 19 is annotated closed** ("hiBoost →
    explicit curve"): the field is deleted, not converted, by the 2026-10-05
    decision; a keytracked gain is a patch's own wiring if one wants it.
16. **A mutation run joined the close** (not in the plan's Task 5): eight
    mutations of the moved expressions and the two gain lines, each watched
    caught by a test or the null gate (section 4). The reviewer is read-only,
    so the mutation record is the executor's.
17. **Two channels naming one source are refused by name** (the review's
    Minor 1, in the fix wave). Before 4.1 the second channel received a
    moved-from null adapter and the load crashed in `Mixer::prepare`. After
    Task 3 each channel received its own adapter over the one node — and the
    node, prepared twice, kept only the *second* adapter's clock, so while the
    first channel rendered, its own clock ticked and the node never saw a new
    tick: the first channel rendered its first sample held for the whole
    buffer, silently. A crash had become wrong audio. The loader now refuses
    the file ("channels 'a' and 'b' name one source 'x'"), which is the
    simplest case of the cross-loop sharing Part 4 section 5.8 refuses at
    4.5; a node shared deeper (reachable from two channels' sources) is as it
    was before 4.1, unsupported and unrefused until then. No in-scope file
    has two channels on one source.
18. **Part 4 section 5.8's sentence on `frames` is corrected in place** (the
    review's flag). It said an instrument's frame count is "the larger of
    [`seconds`, or five seconds when absent] and the score's end". The code,
    today and before, takes the score's end alone when the file has no
    `seconds` — 69 of the 96 in-scope files with a score have none — and
    Task 1's frame test pins that (84,000 frames for a 1.75-second score, not
    240,000). 5.8 describes the form "as today", so this is a correction of
    fact, annotated where it stands; a `build_sound` written at 4.6 from the
    uncorrected sentence would have moved most score renders, and the twin
    gate would have caught it there.
19. **The build's floating-point mode is a compiler default.** The plan and
    this lane's documents say `/fp:precise`; no CMake file sets an `/fp`
    flag, so it is MSVC's default that holds it. The byte-identity instrument
    should not rest on a default: CLEANUP 52 suggests stating it.
20. **Commit `7b32205`'s message is loose about which gain line is which.**
    It says the gain is "velocity * volume for a fresh voice and the line's
    first velocity for a held one". `velocity * volume` is
    `prepare_voice_at`'s, the streaming path the editor's keyboard uses; the
    pre-render path that scores and compositions take mixes every voice,
    held or not, at the note's velocity and applies `volume` to the summed
    buffer afterwards. Both lines lost the same factor. The code and this
    report are right; the message stays as written (no history rewrite).
21. **The loader's two stale comments beside edited code are corrected** (the
    review's Minor 3 and a flag): "the voices learn the rate here, once" lost
    its "once" (the standalone render's mixer prepares the instrument again,
    to the same rate, and prepare is idempotent), and the bare-output
    comment lost its claim that centre pan "inherits the -3dB CLI-vs-UI
    level difference": the mixer has normalised centre to unity since
    2026-09-14. The gain comment in `instrument.h` now says it covers both
    the held line and the ordinary note.
22. **A question the close raised, recorded and not answered: should an
    ordinary note re-anchor its random draws when it starts?** Section 1's
    second bullet is the evidence. On the pre-render path, a note's waveform
    depends on how many samples every earlier note on its voice rendered,
    and that count depends on those notes' levels. So today, changing one
    early note's velocity in a piece can change the realisation of every
    later note on that voice; and on `piano_default`, with one voice, "that
    voice" is the whole piece. Held lines have the opposite rule: the code
    comment dates it to you, 2026-09-20 ("a different oboeist per phrase"),
    every continuation re-anchors its draws at Setup, and the comment says
    this pins an in-line note "exactly like a fresh note's always was" —
    which is true at most of the first note a voice plays, and then only for
    nodes that draw nothing at prepare (the re-anchor returns to the
    construction seed, ahead of any layout draws), and of no later note. Re-anchoring
    ordinary notes too would make each note's realisation a function of the
    note alone; it would also move every multi-note render in the library,
    so it is not a refactor step. It is BACKLOG row 11, for the decision on
    Part 5's play function or after the freeze, and REVIEW R16 item 2.
    Two consequences hold either way: the comp template gate re-rolls on any
    change to any note's rendered length, so a Part 5 that must keep
    composition renders byte-identical must keep ring-out lengths exactly;
    and an A/B by ear across such a change compares two takes.

## 7. Debt and future cleanup

- **`mixer.h` holds three types and a function** — `FaultReport` with
  `fault_report_text`, `MonoSource`, `Channel` — beside `Mixer`. Part 3's
  one-type-per-file rule was the nodes'; the split (`Channel` and
  `MonoSource` to their own files, the fault record to its own) is CLEANUP
  46, for 4.6 when `build_sound.cpp` is written or for Part 8.
- **The adapter's name.** `ValueSourceMono` says "a ValueSource as a mono
  producer", which is what it is, in the loader's vocabulary. When it moves
  into `build_sound.cpp` at 4.6 the name is reconsidered (`SignalChannel`?
  `MonoAdapter`?); CLEANUP 47.
- **The loader's file-local types have external linkage.** `IndexedGraph`
  joins `GraphResult`, `PerformContext`, `TapBind`, `TapBindScope` and
  `ValueSourceMono` as plain structs in `namespace mforce` inside a `.cpp`;
  `static` makes the *functions* file-local, and nothing does that for a type
  short of an anonymous namespace (section 10). No other file defines any of
  the six names, and the file is deleted at 4.7; the convention for the new
  module's `.cpp` files is the anonymous namespace. CLEANUP 48.
- **`scores/baselines/test_k467_walker.json` is not JSON** (prose notes about
  deterministic transforms, since `eb964a1`). It sits in the comp gate's glob
  and fails there every run. Yours to rename or move; CLEANUP 49.
- **The meter's duplicate count is not additive.** It is lizard's token
  detector, and its list depends on the whole tree: when Task 2 removed the
  62-line pair and the 12-line pair, three blocks in `test_figures/main.cpp`,
  a file no task touched, entered the list, and the total rose by one. This
  report cites the list's entries as evidence. CLEANUP 50 suggests the meter
  report the entries added and removed, not only the count.
- **A node shared between two channels below their sources** (reachable
  from both, named by neither) is still unsupported and unrefused, as before
  4.1: it is prepared by both adapters and follows only the second one's
  clock. Section 6 decision 17 refuses the direct case; 4.5 refuses the rest
  by name (Part 4 section 5.8, whose own count is 0 of the 27 plain-sound
  files in scope sharing a node). `Mixer::prepare` and `render` still
  dereference a channel's source unchecked.
- **`NotePerformer::humanize` has no caller that sets it** now that
  `test_ornaments` is gone (the one remaining `humanize =` in the tool is the
  chord performer's); its `jitter()` still draws from the generator on every
  call. Part 5's, with the performer; CLEANUP 51.
- **`/fp:precise` is not written anywhere in the build** (section 6,
  decision 19); CLEANUP 52.
- **The delivery loop is thinly exercised by the scope.** Of the 96 in-scope
  scores none omits `velocity`, none has an `onset`, none ends with a line
  still open, and only one has a note without a `time` (after a held note).
  Those lines of `perform_notes` moved verbatim and were guarded only by the
  body-diff until the fix wave's delivery tests (section 8). Whoever answers
  BACKLOG row 10 (where a patch's test notes live) inherits the observation:
  the scores in the tree test very little of the score reader.
- **An unconsumed `StereoMixer` or `SoundChannel` node in an instrument-form
  file is ignored silently**, as it was for every id but `mix`. 4.5 registers
  both as parts; whether an unconsumed part deserves a warning is the codec's
  question (4.6).
- **`load_patch_file` still reads a top-level `frames` key** that no file
  carries (4.7 deletes the reading), and `kBranchKeys`, now seven keys
  shorter with ragged lines, is deleted whole at 4.7.
- **`mixer.h` names "Part 4" in a comment** ("reports nothing until Part 4
  gives it ids") — pre-existing; 4.6 makes the sentence false and removes it.
- **The comp template gate is still outside `gates.py`** (its driver needs a
  pair of stages); its base is now `post41`.
- **No new byte-identity preservation.** Nothing joins the "retire after Part
  8" list.

## 8. The review (R16)

An Opus 5.5 refuter with a fresh context, read-only: it built nothing and
ran no binary, and it read the code only at the commits, because the
mutation run was editing three source files in the working tree while it
worked. Its oracle was the plan's Review Focus as the execution corrected
it, the ten-item brief and the Ledger. It body-diffed the delivery loop, the
end loop, the voice loop and the adapter against the base; reproduced the
`mix` census and the 36,169-object key census with scripts of its own;
checked every declaration of `prepare`, `start` and `next` in the engine for
a hiding one; and read this report's draft against the code.

**Verdict: none of the three claims broken — 0 Critical, 0 Major, 6 Minor,
7 Flag.** No in-scope render moves; every moved expression keeps its
operations and order (the only differences are the three on record: `at()`
for `[]`, the `max` with a floor of 0, the one refusal that moved); the
composition path changes by the two gain lines and nothing else. On the text
hand-over it went through every way `dump()` then `parse` could change a
score — a float's text, an overflow, a NaN, an integer's type, invalid
UTF-8, non-ASCII, key order, a duplicate key — and found one change of type
(`-0`, signed to unsigned) that no read can see. On `hiBoost` it added what
the arithmetic argument had left out: the program runs with denormals
flushed, and the two gain lines still agree there.

**The Minors, all taken in the fix wave** (`e0cb2e2`):

1. *Two channels naming one source rendered wrong audio silently* — the
   finding of the review. The draft of this report said "two clocks tick the
   node"; the reviewer showed that one clock drives it and the first channel
   holds its first sample. Refused by name now (section 6, decision 17).
2. *Lines of the moved delivery loop that no test and no in-scope score
   reaches*: the velocity default, the unphrased note's `prevEnd`, the
   onset term, `finish_open_lines`. The wave adds tests that observe what
   was delivered (start samples, the rendered buffer, the finished line),
   where the first tests checked only the returned end.
3. *"The voices learn the rate here, once"* is false for the standalone
   render (the mixer prepares the instrument again). Reworded.
4. *Three refusals went with the `mix` lookup.* Recorded (section 6,
   decision 4).
5. *The scratch-file idiom was written four times in the new tests.* One
   RAII type, `ScratchPatch`, serves all four (section 10).
6. *Sentences of this report*: "every composition render moved" (not
   `--chords` without its flag, not three of `--josie`'s four parts, not a
   note at or below 100 Hz); "level-changed" (the gain feeds the ring-out
   and the peak guard — which is the thread that led to section 1's
   finding); the `mix` shape (a `SoundChannel` too); the channel widening
   (the bare output too); `test_ornaments`' length; "integer as an integer"
   (in value); the loader's line counts; the denormal caveat; `/fp:precise`
   as a default. All corrected above.

**The Flags.** Taken: Part 4 section 5.8's sentence on `frames` contradicts
the code and the new test pins the code — corrected in the document
(section 6, decision 18), and the most valuable thing the review found for
4.6; the stale centre-pan comment beside an edited site; the gain comment
that described only the held line. Recorded: `NotePerformer::humanize` has
no setter left (CLEANUP 51); a stale `--hiboost` is read as chord tokens
(section 1); the plan overstated which stderr warnings the throwaway build
suppressed (only the `NameGate` one was deduplicated; nothing a render sees);
the 7b32205 commit message's "fresh voice" (section 6, decision 20).

**The ten items.** 1 separation PASS (`embedded_score.cpp` does one job). 2
single responsibility PASS. 3 dependency direction **FAIL, accepted and
reduced**: `embedded_score.cpp` includes three `music` headers from
`render`; the edges fall 14 → 12 and the render↔music cycle stays through
this file and `instrument.h` until Part 5, as Part 4 section 5.9 decided. 4
duplication PASS for the engine, Minor 5 for the tests (fixed). 5
encapsulation PASS (one public field fewer). 6 ownership PASS. 7 abstraction
fit PASS for 4.1, with the 5.8 flag. 8 real-time PASS (the adapter's loop
unchanged; `log10` left note-on). 9 names and comments PASS with Minor 3. 10
tests PASS with the gaps of Minor 2.

**The tests' blind spots it named**, each a mutation the first tests would
have missed, and what the wave did about it: the unphrased `prevEnd` and the
velocity default (delivery tests); `score_end` in float (an end that is not
a dyadic number); `lround` made a truncation (a frame count that rounds
up); the floor replacing the file's polyphony when non-zero (a floor below
the file's); the render path's floor made 1 (a file with no voices); the
adapter given no clock (a shared envelope through a channel); the unity
test passing with every gain halved (the constants read directly); the
refusal needles sharing a suffix (the id in the needle). Each was then applied
as a mutation against the new tests, with five more, and watched caught
(section 4, the second run).

**What the review did not find, and the close did.** The reviewer confirmed
that the composition path changes only at the gain lines, and noted the
ring-out depends on the gain; it did not predict that the consequence is a
re-roll of every later note. That came from the comp gate's RMS table (seven
templates louder with a boost removed) and a window-by-window fit of new
against old. And the one uncaught mutation, the streaming path's `volume`,
came from the executor's run, not from reading. Both are arguments for the
close's measurements being more than a formality after a clean review.

**Re-review (the same refuter): closeable.** Every taken item is in the
diff as described and the wave changes nothing a file in scope can see. It
checked the new refusal for a file it could wrongly refuse: across every
plain-sound mixer file in the whole tree, not only the 27 in scope, none has
two channels on one source id; the same channel listed twice is refused too,
correctly (it shared a source). It could not refute the explanation of the
re-roll and found no cause left unseparated: the classic path calls no
`reseed`; `render_chunk` stores the gained sample and the ring-out measures
it; with one voice the draw streams and the filter, delay and LFO state all
carry; and the other suspects — the peak guard (per sample, memoryless),
the voice counter and start times (independent of gain), the normaliser
(one scale for the whole buffer) — cannot decorrelate two renders. One new
Minor, taken in a second small wave (`3c012ea`): `ScratchPatch`'s destructor
called the throwing `std::filesystem::remove`, and a destructor that throws
terminates the test run; it uses the `error_code` overload now (section
10). And one wording correction to decision 22, taken: a continuation's
re-anchor matches a fresh voice's first note only for nodes that draw
nothing at prepare.

## 9. Reading guide

1. `engine/include/mforce/render/embedded_score.h` (16 lines) and
   `engine/src/embedded_score.cpp` — the public function is six lines; the
   delivery loop above it is the code that was in the loader.
2. `engine/src/patch_loader.cpp`, from the "Patch loading" banner to the end:
   `index_graph`, `build_instrument_json`, `load_patch_file`'s three forms,
   `load_instrument_patch_json`. About 275 lines that were about 410 with
   the score function.
3. `engine/include/mforce/render/patch_loader.h` and `mixer.h` — the two
   names.
4. `engine/include/mforce/render/instrument.h`, `prepare_voice_at`'s gain
   line and the held-line path's: `git show 7b32205 -- engine/include`.
5. `tools/engine_tests/main.cpp`: `run_embedded_score_delivery_tests` (what
   a score delivers), `run_voice_pool_tests`, `run_channel_clock_tests`,
   `run_instrument_mixer_unity_tests`, and `ScratchPatch` above them.

## 10. C++ lessons

- **Floating-point identities you may rely on, and the one you may not.**
  The claim that deleting `hiBoost` moves no patch render rests on three
  facts of IEEE arithmetic, all exact: `1.0f + 0.0f` is `1.0f`; `x * 1.0f` is
  `x` for every normal `x`, every infinity and a zero of either sign (the
  reviewer's caveat: this program runs with denormals flushed, so a
  *subnormal* `x` times one is zero — and is zero in the new line too, where
  it is an operand of the same multiply, so the two lines still agree); and
  C++ multiplication associates left to right, so
  `velocity * (1.0f + boost) * volume` is `(velocity * (1.0f + boost)) *
  volume`, which with `boost` at zero is `(velocity * 1.0f) * volume`, which
  is `velocity * volume` — the same two operands, the same one rounding.
  (That is the streaming path's line. The pre-render path's was the simpler
  `pn.velocity * (1.0f + boost)`, which needs only the first two facts.) What
  you may *not* rely on is reassociation: `(a * b) * c` and `a * (b * c)`
  round differently in general, so had the old line been
  `velocity * ((1.0f + boost) * volume)` the argument would still hold (the
  inner product is exactly `volume`), but a line like
  `(velocity * k) * volume` rewritten as `velocity * (k * volume)` would move
  renders for any `k` other than 1. MSVC's `/fp:precise` forbids the compiler
  from making that rewrite on its own; `/fp:fast` permits it, which is why
  the mode is part of the byte-identity instrument. This build has
  `/fp:precise` because it is the compiler's default — no CMake file says so
  (section 6, decision 19). Two neighbours of these identities are *not* identities and
  have bitten this refactor before: `x + 0.0f` is not `x` when `x` is `-0.0f`
  (the sum is `+0.0f`), and `x * 0.0f` is not `0.0f` when `x` is negative,
  infinite or a NaN. C# has the same arithmetic for `float` (`x * 1f == x`
  holds there too); its specification additionally lets an implementation
  evaluate at higher precision than the type, which is harmless for these
  three identities and one more reason not to lean on any other.
- **A function hands its facts back; the caller does not recompute them.**
  The loader performed the score and then walked the same JSON a second time
  to learn where it ended. The function that performs the score already knows.
  `play_embedded_score` returns the end, and the loader's knowledge of the
  score's format shrinks to nothing: it holds a number of seconds. The two
  accumulations stayed two (the delivery in `float`, as the notes are
  delivered; the end in `double`, as the frame count is computed) because
  merging them would change which rounding the frame count sees; they now
  live in one file, side by side, where the next reader can see both. In C#
  you would write `double PlayEmbeddedScore(...)` for the same reason and
  never think of it as a lesson; it is one here because the old shape, a
  `void` function and a second loop at the call site, is what code grows into
  when the second need arrives later than the first.
- **A text argument at a seam, and why a forward declaration would not have
  been honest.** `embedded_score.h` declares
  `double play_embedded_score(PitchedInstrument&, const std::string&)` and
  includes `<string>` and nothing of nlohmann's. The alternative was
  `const nlohmann::json&`. C++ would allow that with only a *forward
  declaration* of the type — nlohmann ships `<nlohmann/json_fwd.hpp>` for
  exactly this — so the header would stay cheap to compile. But the
  dependency would still be in the interface: every caller would have to hold
  a `json` to call it, the structure meter would count the include as a JSON
  include in `render` (and it would be one), and at 4.6 the caller holds the
  score as the text the codec kept, because the patch module never parses it.
  A forward declaration hides the *cost* of a dependency, not the dependency.
  The C# analogue is an interface whose method takes `JsonElement`: adding
  `using System.Text.Json;` costs nothing to compile and still ties every
  implementer and caller to that library; a `string` parameter does not. The
  price of text is one `dump()` per load and a re-parse, proved harmless by
  the identity test over the 96 scores.
- **`static` functions, the anonymous namespace, and the type that neither
  covers.** A function defined in a `.cpp` is visible to the whole program
  unless it is marked `static` (or sits in an anonymous namespace): "internal
  linkage", the nearest thing C++ has to C#'s `private` at file level, or to
  C# 11's `file` modifier. The loader's helpers are all `static`, and the new
  ones (`index_graph`, `build_instrument_json`, `perform_notes`, `score_end`)
  follow. But `static` cannot be applied to a *type*. `IndexedGraph`, a struct
  defined in the `.cpp`, has external linkage like the loader's five other
  local structs: if another `.cpp` ever defined a different
  `mforce::IndexedGraph`, the program would violate the one-definition rule,
  and the standard requires no diagnostic — the linker may silently pick one
  layout for both. An anonymous namespace (`namespace { struct IndexedGraph
  {...}; }`) is the tool that makes a type file-local, and it covers
  functions too, which is why modern guidance prefers it over `static`. The
  loader keeps its existing convention for its last three checkpoints; the
  module written at 4.6 starts with the anonymous namespace (section 7).
- **Why two adapters were one: the non-virtual interface.** `ValueSource`'s
  `prepare`, `start` and `next` are *non-virtual* public functions that call
  virtual hooks (`on_start`, `compute`). `WaveSource` overrides the hooks and
  declares none of the three. So `src->next()` runs the same function whether
  `src` is a `shared_ptr<ValueSource>` or a `shared_ptr<WaveSource>` — the
  pointer's static type cannot matter, because there is only one `next` to
  call — and an adapter that differed only in the pointer type it held was the
  same adapter twice. The check that made this safe was not "do they look the
  same" but "does `WaveSource` or `Generator` declare its own `next`": a
  derived class that declares a non-virtual function with the base's name
  *hides* the base's, and then the pointer type decides which runs. C# makes
  you say so (`new` on the member, with a warning if you forget); C++ lets it
  happen silently. The pattern itself — public non-virtual, protected virtual
  — is the template method, the same in C# with `protected abstract`.
- **`operator[]` on a const JSON object is a precondition, not a lookup.**
  nlohmann's non-const `j["key"]` inserts a null when the key is absent, like
  `std::map`. The const overload cannot insert, so an absent key is undefined
  behaviour (an assertion in a debug build). `build_instrument_json` takes
  its root by const reference, and the moved line `root["instrument"]`
  quietly changed overloads. Both callers check the key first, so nothing was
  wrong; `root.at("instrument")` says the same thing with a defined failure,
  an exception naming the key. C#'s indexer on a `Dictionary` throws
  `KeyNotFoundException` either way; the C++ container gives you three
  behaviours (`[]` inserting, const `[]` assuming, `at()` throwing) and the
  one you get depends on a `const` two lines away.
- **RAII for a temporary file: the destructor is the `finally`.** The new
  tests wrote a patch to the scratch folder, loaded it, and removed it, four
  times over, each with its own care about removing the file when the load
  throws. `ScratchPatch` is a struct whose constructor writes the file and
  whose destructor removes it; a test declares one and the file's lifetime
  is the variable's scope, on every path out of it, an exception included.
  This is C#'s `using (var f = new TempFile(...)) { ... }` with
  `IDisposable`, except that C++ needs no `using` statement: every local
  object is disposed at the closing brace, deterministically, which is why
  the idiom is everywhere in the language (locks, file handles, the
  loader's own `TapBindScope`). The struct deletes its copy constructor and
  copy assignment: a copy would be a second owner whose destructor removes
  the same file again. That is the "rule of three" in its modern form — if
  you write a destructor, say what copying means, and `= delete` is a
  complete answer. One rule the first version broke and the re-review
  caught: a destructor must not throw. It is `noexcept` by default, so an
  exception leaving it calls `std::terminate` and ends the whole test run
  rather than failing one check; `std::filesystem::remove(path)` throws on
  a locked file, and the overload that takes a `std::error_code` reports
  instead. C#'s `Dispose` has the same convention for the same reason, but
  only as a guideline; in C++ the language enforces the consequence.
