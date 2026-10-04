# MForce architecture and code review — 2026-10-02

Scope: every tracked C++ file under `engine/` and `tools/` plus the CMake build (about 58k lines). Excluded by Matt's instruction: the comp strategy layer (every `*strateg*` header in `music/` and the walk3 helpers they own: phrase_critic, anchor_selector, note_map, melody_profile, passage_melody). Also excluded: `engine/third_party`, the Python research scripts, and the stale git worktrees under `.claude/worktrees/`.

Guidelines applied (Matt, verbatim intent): overall soundness; adherence to current C++ (C++20) practice; elegance; efficiency; performance; and absence of smells, god classes, code copies, workarounds and hacks.

Method: three passes. (1) Find: 24 unit reviewers, each reading every line of one code unit (the 14.6k-line UI file was outlined first and then reviewed in seven ranges), plus 8 cross-cutting architecture reviewers (graph model, render pipeline, music model, tools, duplication, modern C++, hacks census, build/headers). 522 raw findings, 474 after merging duplicates. (2) Verify: findings were handed in batches to independent skeptics instructed to refute them from the code (two skeptics for batches holding a critical or high finding); this pass was cut short by the account's spend limit and covered about a quarter of the findings, spread across all files. (3) Synthesis: this document. I also read the core contracts myself (ValueSource, RenderContext, SourceRegistry, Instrument, PerformSource, the audio callback, Conductor, Chord voicing, the patch loader's type chain, the clipboard bindings, the mixer pan law, the build flags) and ran the CLI to reproduce the three comp-lane bugs; items I confirmed first-hand are marked "confirmed by me" in the text and "confirmed (synthesis read)" in the tables.

Data files for the raw material: `docs/audits/2026-10-02-raw/` (per-unit reports, merged findings with verifier notes, the UI outline).

**Verification coverage.** The skeptic pass was stopped after 123 of 474 merged findings because it was consuming the account's spend limit faster than it could be reset (four interruptions). Of the 123 verified: 107 confirmed, 3 disputed, 13 refuted. A further 17 were confirmed directly during synthesis (code read or run). The remaining 334 carry the reviewer's own evidence and severity and are marked **unverified** in the tables: treat each as a claim with a cited line to check before acting. On the verified sample, roughly one finding in ten was refuted and about one in six was downgraded a severity level, so expect the same of the unverified set. Findings carried into this report (everything not refuted or out of scope): 460 — critical 9, high 76, medium 211, low 164.

---

## 1. Verdict

The engine's foundations are the right shape and are mostly built with care. The ValueSource graph, the explicit registry with self-describing nodes, the PerformSource clock, the tap-guarded feedback edges, the seeding discipline and the per-node real-time hygiene are all sound, and several units (filters, additive, physical models, the music value types) would pass a demanding review on their own. Two of the unit reviewers independently called pieces of core "model engineering".

The project is not releasable as it stands, for reasons that are concrete rather than aesthetic:

1. **The real-time rule is broken in the engine and the UI.** The engine's own non-negotiable (no heap, locks or unbounded work on per-sample paths) is violated by the base oscillator class throwing from `next()`, by several source types that allocate on the first sample of a note or every table cycle (one confirmed by me, the rest reviewer-reported), and by the UI's audio callback blocking on a mutex the UI thread holds while it allocates and walks whole voice graphs. Any of these produces a dropout or a crash on a key press, and the throw terminates the process from the audio thread.
2. **The composition pipeline has functional bugs at its spine.** Conductor replays every part's full event list once per section, so any multi-section piece is a canon of itself; tree-built pieces (the DUN path) render silence because the only realize step is a private Composer method; `compose()` mutates its `const` template through five const_casts; chord voicing loops forever on the power chord and every dictionary voicing. These are not "classical bias" issues, they are the engine producing the wrong notes or none.
3. **One file is the product.** `tools/mforce_ui/main.cpp` is 14,658 lines, 349 file-scope mutable statics, a 1,711-line `main()`, two clipboards bound to the same keys, four hand-maintained copies of the patch JSON codec, and its own second DSP renderer whose pan law has already drifted from the engine's. No second contributor could work in it.
4. **Duplication is the dominant smell everywhere else.** The loader has twin entry points and a 17-branch type chain that re-implements what the descriptors already provide; the ring-out policy exists three times; DSP primitives (BLEP, Thiran allpass, DC blocker, Friedlander friction) are re-typed per node; the four DURN parsers share nothing; the CLI render epilogue is hand-rolled seven times. Several copies have already diverged into audible or behavioural differences.
5. **Nothing enforces the rules.** No CTest registration, no warnings-as-errors, warning levels and language standards vary per target, third_party is an untracked local directory a fresh clone cannot configure without, only a small fraction of node types have any test (the test reviewer counted about 14 of 85), and no test asserts the no-allocation rule the project is built around.

Every claim in points 1 to 5 is either confirmed by the skeptic pass or checked by me directly, except where the text below says "unverified"; the per-item status is in the tables of section 4.

Modern C++ practice is the one guideline the code mostly meets: `std::span`/`std::string_view` descriptor tables, `enum class`, `final` on leaves, `std::variant` for the music model, RAII scopes, atomics where threads meet, no raw `new`/`delete` in the engine. The debt is architectural and in volume, not in idiom.

### Plain-language version

The sound engine's design is good and much of its code is good. What is wrong is (a) a handful of specific places where the audio thread can be blocked, made to allocate, or made to throw, which is the one thing an audio engine must never do; (b) a few outright bugs in the part that turns a composed piece into notes, so multi-section pieces and hand-written scores come out wrong or silent; (c) the editor being a single enormous file that has grown by accretion; and (d) a lot of copy-pasted code that has started to disagree with itself. None of this needs a rewrite. All of it needs a sustained cleanup campaign before anyone else sees the code.

---

## 2. Health by unit

Health is the unit reviewer's 1 (needs rewrite) to 5 (exemplary) call. The architecture rows judge a concern, not a directory.

| unit | health (1-5) | raw findings | dominant problem (reviewer's words, trimmed) |
|---|---|---|---|
| core | 3 | 18 | The dominant structural problem is Envelope, a ~560-line accretion point holding three timing semantics, gate/retrigger, two RNG decorrelators, range mapping, shape-sniffing sustain rewrite and the preset factories, with a hand-ro |
| sources-noise | 3 | 17 | The dominant problem is structural: every class hand-maintains five parallel pin lists (descriptor table, set_param chain, get_param chain, prepare list, next list, plus dead typed set_X/get_X accessors), and in this unit alone th |
| sources-physical | 3 | 16 | The dominant problems are (1) HybridKSSource, which resizes vectors and runs an O(partials x period) DFT inside the render path, (2) one real ordering bug in KSString::init_note (the dispersion-shedding guard reads lpDelay_ a few  |
| sources-evolution | 2 | 18 | The dominant problems are real-time-safety violations on the audio thread (a per-table-cycle heap allocation in HistogramEqualizeEvolution, a throw in fill_table reachable from an uncaught audio callback, and every evolution alloc |
| sources-combinators | 3 | 12 | The dominant problem is the C# ports in combined_source. |
| additive | 3 | 15 | The dominant problems are (1) the two legacy sources are both redundant with AdditiveSource+FullPartials and both carry correctness bugs — AdditiveSource2 bakes partial frequencies from a stale currFreq_ at prepare (wrong pitch, m |
| filters | 3 | 13 | The dominant problems are (1) one memory-unsafe path — DelayFilter never clamps delayTime, so a modulated or over-range delay reads buffer_[negative]; (2) the mixer: master gainL/gainR are advanced once per channel per frame, and  |
| render-instrument | 3 | 15 | The dominant problem is PitchedInstrument itself: a 620-line all-public struct that has accreted a dozen policies (pool rotation, live slot ledger, capture, streaming handoff, push bindings, onset vocabulary, held-line, ring-out/c |
| render-loader | 3 | 15 | The dominant problem is that the generic path has not been allowed to win: build_graph carries a 17-branch special-case chain of which at least six branches re-implement wiring that the types' own ParamDescriptors/SettingDescripto |
| music-model | 3 | 15 | The dominant problems are correctness holes in rarely exercised but reachable paths: Chord::init_pitches loops forever for the power chord and every guitar/piano dictionary voicing (reachable from mforce_cli --josie), and the brac |
| music-composer | 2 | 16 | Dominant problem: realization and performance semantics drifted during the Stage-8 tree-walk retirement and nothing in the unit's tests covers multi-section pieces or tree-built pieces. |
| music-json | 3 | 15 | The dominant problem is hand-rolled per-field serialization that has already drifted: a 42-line copy of from_json(PhraseTemplate) inside from_json(PeriodSpec) silently drops strategy configs, parse_chord_progression re-implements  |
| music-builders-voicing | 3 | 18 | The dominant problem is correctness, not shape: the two core generators do not honor their stated contracts. |
| tools-cli | 3 | 10 | The dominant problem is copy-paste: the beats→seconds→frames→render→mono→stereo→peak/rms pipeline is hand-rolled six to seven times in main. |
| tools-ui-chunk1-L1-2488 | 2 | 14 | The dominant problems are structural: GraphNode is a tagged-union-by-convention god struct dispatched by string if-chains, load_graph_from_path is a nine-phase god function with dead state and an embedded layout algorithm, and the |
| tools-ui-chunk2-L2489-5005 | 3 | 15 | The dominant problem is that the callback takes a blocking std::mutex that the UI thread holds across whole-graph prepare, envelope walks, make_shared glide envelopes and shared_ptr destruction, so the 10. |
| tools-ui-chunk3-L5006-7525 | 3 | 11 | the dominant problem is draw_shape_editor: ~930 lines with the client type-switch repeated more than a dozen times, i. |
| tools-ui-chunk4-L7526-9195 | 3 | 18 | No real-time-safety violations in anything the audio callback touches from this range; offline renders are UI-thread and the buffer-detach invariant is honored except in render_drums_waveforms (currently masked by callers). |
| tools-ui-chunk5-L9196-11693 | 2 | 14 | First change: delete one clipboard (keep the fragment format, drop the file round-trip by reusing serialize_patch_graph and a direct node-instantiation path), then make the context menu return after any mutating item, then route e |
| tools-ui-chunk6-L11694-13843 | 2 | 15 | The dominant structural problem is main(): the CLI modes repeat the same context/registry/try-catch scaffold ten times and two of them re-implement the RtAudio callback's mixing arithmetic by hand while claiming parity with it. |
| tools-ui-chunk7-L13844-14658 | 2 | 13 | The dominant problem is that the window body is a bag of per-feature patches accreted in place rather than a module, which is exactly how the duplicate bindings and the unrewired delete path slipped in. |
| tools-tests | 3 | 14 | The dominant problems are structural: neither binary is registered with CTest and both silently require CWD = repo root; a failed CHECK keeps running and then dereferences the null it just reported (crash, not a report), and main( |
| tools-durn | 2 | 15 | The dominant problem is the absence of a shared intermediate representation: the per-parser to_figure_units layers are dead (MIDI, MusicXML), discarded (kern, which is parsed twice per file) or lossy (ABC, whose FigureUnit drops a |
| tools-misc-build | 3 | 18 | The dominant problem is that build policy is copy-pasted per target instead of expressed once, so nothing keeps the targets consistent or the warnings at zero. |
| arch-valuesource-graph | 3 | 15 | The dominant architectural problem is the implicit 'first-wired consumer advances, everyone else reads current()' sharing model: it makes a shared node's value depend on JSON node order (a one-sample lag flips with wiring order),  |
| arch-render-pipeline | 3 | 15 | The dominant problem is the UI<->audio handoff itself: the audio callback takes a blocking std::mutex for the whole buffer, and the UI thread holds that same mutex across note-on work that allocates and walks the entire voice grap |
| arch-music-model | 2 | 18 | The dominant problem is that two refactors (Composer-owns-events, strategy framework) were each left half-landed, so the boundaries the docs describe are not the boundaries the code enforces. |
| arch-ui-tools | 2 | 12 | The dominant problem is the absence of a document model between UI and engine: JSON text is the only bridge, so every concern re-implements the format and every parameter edit round-trips through serialize->parse->N-voice rebuild. |
| arch-duplication | 3 | 29 | the dominant problem is copy-on-extend: DSP primitives (BLEP, Thiran tuning allpass, DC blocker, Friedlander friction, KS averaging, RBJ/resonance biquads) are re-typed per node; the patch loader has twin entry points and special- |
| arch-modern-cpp | 3 | 32 | The dominant problem is that the project's own real-time rule (no heap, locks or unbounded work in per-sample paths) is broken in several engine render paths independent of the UI: WaveSource::next() throws with no exception bound |
| arch-hacks-census | 3 | 17 | The dominant problem is that several documented debts have sat live for months and now have multiple spellings: the legacy paramMap exists as three separate implementations (engine build_bindings, UI JSON converter, UI stash edito |
| arch-build-headers | 3 | 24 | The dominant problem is compile-time and structural leakage from the header-only policy: nlohmann/json (25,526 lines) is included by core data-model headers (music/templates. |

---

## 3. Architecture assessment

### 3.1 DSP core: the ValueSource graph

The model: every node is a `ValueSource` with `prepare(ctx, frames)`, `next()`, `current()`, and a cold self-description surface (param/input/setting/array descriptors, string-keyed `set_param`/`set_setting`). Parameters are `shared_ptr<ValueSource>`, so any pin can be a constant or a sub-graph. One generic loader and one generic UI drive about 70 node types through that surface. This is the right design for the project's goals and it is what makes the node editor possible without reflection.

Three architectural weaknesses, in order of consequence:

- **Shared-node semantics depend on wiring order.** The "first-wired consumer advances the source, every later consumer reads `current()` through a RefSource" rule means a shared LFO's value at a given consumer depends on which consumer the JSON lists first; a consumer evaluated before the advancer sees the previous sample. The engine has grown about 250 lines of compensating machinery (usage counters, starved-ref promotion, advance lists, a UI mirror of the auto-wrap) to live with this. A per-tick memo in the base class (each node computes once per sample index and serves the cached value to every consumer) removes the whole class of problem and most of that machinery. This is the single highest-leverage engine change in the review.
- **The registry is only half the dispatch.** `patch_loader.cpp` special-cases 17 types before it consults the registry ([patch_loader.cpp:656](engine/src/patch_loader.cpp:656) through [1050](engine/src/patch_loader.cpp:1050), confirmed by me). At least six of those branches re-implement wiring the types' own descriptors already declare, and the registry configurators for the Butterworth filters are unreachable. Defaults and seeds therefore live in three places and have already drifted (three noise classes sound different from the CLI than from the UI because constructor defaults disagree with descriptor defaults).
- **`prepare()` conflates "reset for a new note" with "here is the note length".** Envelopes lay stages out over `frames`, crossfaders divide by it, MultiSource rewrites it. A host that calls `render()` per block would restart every envelope and zero every delay line each block. This is the concrete blocker for a JUCE or any block-streaming host, and it is cheaper to fix now than after more nodes depend on it.

A fourth point is a performance ceiling rather than a flaw: the graph is pulled one sample at a time through virtual calls and pointer chases. For the current patch sizes it is fine. A block-processing API (`next(float* out, int n)` with a default per-sample fallback) is the path to SIMD and to a host, and the additive source already shows how to collapse the per-partial virtual calls.

### 3.2 Render and live audio

Offline: `PitchedInstrument` pre-renders each note into a buffer and `Instrument::render` sums them. Live: the UI's RtAudio callback pulls up to 16 pool voices per sample. The ownership story is careful (voices hold the patch, destruction is deferred to a UI-thread GC, wheel/pressure cross via atomics, FTZ/DAZ is set on every rendering thread, a watchdog restarts dead streams).

The problems are at the seam:

- **The audio callback takes `g_audioMutex` for the whole buffer** ([main.cpp:3864](tools/mforce_ui/main.cpp:3864), confirmed by me), and the UI thread holds the same mutex across `prepare_voice_at`, `continue_voice_live`, envelope walks with `dynamic_cast`, `make_shared` glide envelopes, and whole-graph rewiring. On Windows a plain `std::mutex` gives the audio thread no priority inheritance. The period budget is 10.7 ms; the instrument cache comment itself cites 0.6 to 1.3 s rebuilds on heavy patches. Every key press on a heavy patch is a potential dropout. The fix is the standard one: the UI thread builds the new voice/graph off-thread and publishes it through a lock-free swap (atomic pointer exchange with deferred reclamation on the GC the UI already has); the callback never blocks.
- **The engine's live-path API forces that mutex.** `prepare_voice_at`, `continue_voice_live` and `acquire_voice` allocate and mutate the live graph in place and say in comments that the caller must serialize them against the audio thread. So the UI's lock is the engine's design, not a UI mistake. The engine should offer a prepared-off-thread handoff.
- **Exceptions on the render path.** `WaveSource::next()` throws on a non-positive frequency ([dsp_wave_source.h:38](engine/include/mforce/core/dsp_wave_source.h:38), confirmed by me) and `WavetableSource::fill_table` throws too; there is no handler between them and RtAudio's thread, so a modulator or bend that touches zero terminates the process. Four reviewers found this independently. Clamp and warn once; never throw from `next()`.
- **Allocation on the first sample.** WavetableSource, HybridKSSource, BasicAdditiveSource and four WaveEvolution subclasses allocate scratch in `next()`/`evolve()` instead of `prepare()` (reviewer claims with cited lines, unverified except the next one); HistogramEqualize allocates a fresh `std::vector<int>` every table cycle ([wave_evolution.h:1328](engine/include/mforce/source/wave_evolution.h:1328), confirmed by me). `RepeatingSource::next()` calls `prepare_repetition()` from inside the sample loop at every repetition boundary ([repeating_source.h:95](engine/include/mforce/source/repeating_source.h:95), confirmed by me that the call is there; the reviewer's claim that it re-prepares the whole subgraph is unverified).
- **The UI mutates live DSP objects without the lock** in at least four places (envelope stage vectors in the Properties panel, shape/knot arrays from the shape editor, formant lists, the audition buffer that the callback indexes by raw pointer). The audition case ([main.cpp:5401](tools/mforce_ui/main.cpp:5401), confirmed by me) resizes the vector the audio thread is reading before taking the lock: a use-after-free on the audio thread when the next WAV is longer.
- `PitchedInstrument` is a 620-line all-public struct carrying ten policies, with the ring-out/cap-fade/containment block copy-pasted between `play_note` and `finish_line` ([instrument.h:620](engine/include/mforce/render/instrument.h:620) and [671](engine/include/mforce/render/instrument.h:671), confirmed by me) and mirrored a third time in the UI callback with a hard-coded sample rate.

### 3.3 Composition lane

The value types (pitch, scale, chord, figures, the Piece/Section/Part tree, the closed `Element` variant, pure figure transforms, the harmony timeline) are a reasonable model and were praised by every reviewer that touched them. The problems are in the two execution classes and in how strategy configuration leaked into the data model:

- **Conductor replays the score once per section** ([conductor.h:545](engine/include/mforce/music/conductor.h:545), confirmed by me in code and empirically: `perform` loops sections, and for each section calls `perform_events` on every part's entire `elementSequence` with the cumulative offset, while Composer already stores absolute beats. I composed the two-section binary baseline, kept a single note at beat 0, played it, and found onsets at 0.03 s and 8.75 s). Every multi-section render Matt has listened to since the tree-walk retirement has carried this.
- **Tree-built pieces are inaudible.** The only passage-to-events realization is a private Composer method; `--dun scores/baselines/k467_bars_1_to_12.dun` renders peak 0 (confirmed by me). A tracked baseline score has rendered silence since the commit that deleted the tree-walk.
- **`compose()` lies about const.** Five const_casts ([composer.h:405](engine/include/mforce/music/composer.h:405), 820, 859, 879, 881, confirmed by me) write realized motifs, planned passages and walker harmony back into the input template. Callers happen to pass non-const locals, so it works by accident.
- **Chord voicing spins to integer overflow.** `Chord::init_pitches` ([chord.cpp:204](engine/src/chord.cpp:204)) walks positions looking for an unvoiced pitch class; a chord definition with fewer distinct pitch classes than tones (every guitar and piano dictionary voicing lists octave duplicates) can never satisfy it, so the loop runs until `int` overflows, which is undefined behaviour, and then emits garbage. Measured by me: `--chords --dict Guitar-Bar-6 C:M` spins for 50 s and fails with "Unknown PitchDef offset: -8"; a plain major chord takes 0.5 s. The reviewers' prediction that the plain power chord also spins did not reproduce (it rendered in 0.5 s); I did not determine why.
- **Harmony context is stored twice per Section** (`chordProgression` and `harmonyTimeline`) and read by different consumers with different scale resolution, so melody chord-figures and accompaniment can disagree on the sounding chord in the same bar. This is the structural root of several of the comp lane's "sounds wrong" reports.
- **composer.h is a 2,081-line god-header**, 57% of which is the out-of-line bodies of eight strategies declared in six other headers, parked there on a dependency justification that is no longer true. Every TU that touches Composer recompiles every strategy, and the strategy headers are not usable standalone.
- **Strategy configuration is hard-wired into the template structs**: `PassageTemplate` is a union of every strategy's fields and `PhraseTemplate` carries four mutually exclusive optional configs. The registry is a lookup table in front of a hard-coded schema, not an extension point, and that is what makes the exempt strategy files awkward.
- **Serialization is hand-rolled per field and has drifted**: a 42-line copy of `from_json(PhraseTemplate)` inside `from_json(PeriodSpec)` silently drops strategy configs; six enum loaders map unknown strings to a default while four throw, so a typo in a template changes the composition instead of failing the load.
- Generator contracts are not honoured: `RandomFigureBuilder::build()` never verifies count or length and three shape helpers emit the wrong unit count; the style table's override rows can never fire because the walk pushes the current label before the lookup; `ChordLabel` is keyed on quality names that do not exist in the chord table, so V7 is relabelled V and viio collapses to I.

### 3.4 Tools and the UI

`mforce_ui/main.cpp` holds the editor model, four JSON codecs, the audio bridge, every panel, twelve headless CLI modes and `main()`. The reviewers' structural reading is consistent: it works, it is unusually well annotated with provenance, and it has accreted rather than consolidated. The dominant architectural gap is the absence of a document model between the UI and the engine: JSON text is the only bridge, so every concern re-implements the format (engine loader, UI loader, two UI savers, a clipboard schema) and every parameter edit round-trips through serialize, parse and an N-voice rebuild. The UI also builds and streams its own DSP graph for Stream/Listen while Play/Generate use the engine-built instrument, which is the root of the recurring "UI sounds different from the CLI" class the repo has tooling to chase, and its pan law has already drifted (the UI lacks the engine's unity-at-centre normalisation: [mixer.cpp:40](engine/src/mixer.cpp:40) vs [main.cpp:3965](tools/mforce_ui/main.cpp:3965), confirmed by me, a 3 dB mismatch at centre).

User-visible defects found in the UI chunks (only the first was checked by me; the rest are reviewer claims with cited lines, unverified): Ctrl+C/Ctrl+V bound to two separate clipboard subsystems so one paste lands twice ([main.cpp:14255](tools/mforce_ui/main.cpp:14255) and [14463](tools/mforce_ui/main.cpp:14463), confirmed by me); File > Open on any non-patch JSON exits the process; Delete removes nodes from the model but not from the DSP graph, so a deleted node keeps sounding in continuous play; Fit asserts (Debug) or reads out of bounds (Release) with any collapsed group; a QWERTY key released while Ctrl is held strands the voice; the node context menu reads a `GraphNode*` after the vector it points into has reallocated; Duplicate of a partials/formant/envelope/curve node silently drops its arrays.

Oversized functions: `main()` 1,711 lines (with the whole Node Editor window inline), `draw_properties_panel` 1,035, `draw_shape_editor` about 930, `load_graph_from_path` about 800, `serialize_patch_graph` 460.

`mforce_cli` is a clean switchboard spoiled by copy-paste: the beats-to-frames-to-render-to-stereo-to-stats epilogue appears six or seven times and hard-codes one bpm although Conductor honours per-section tempo. `explore.cpp` is the only tool code with a real module boundary, and it has one real bug: its instrument-mode loop ignores the streaming contract (no `tick()`, no advance list), so sweeps of any tap-closed feedback-loop patch, which is the project's best-sounding family, are silent or wrong.

`durn_converter` has four self-contained parsers with no shared intermediate representation; the MusicXML path is dead (nothing populates the units it checks), ABC drops accidentals silently, and the emitter quantises durations to ten tokens though DURN supports triplets and ties.

### 3.5 Build, tests, hygiene

- The engine is a proper CMake library with PUBLIC include dirs and the include graph is acyclic with every header-defined function `inline`. Good.
- No CTest registration anywhere; both test binaries depend on the working directory being the repo root; a failed CHECK keeps running and then dereferences the null it reported.
- No warnings-as-errors; the UI builds at /W3 while the engine is at /W4; two tools are pinned to C++17 and stk_ref to C++11 beside C++20 headers.
- `engine/third_party` is gitignored with no configure-time check, so a fresh clone fails at configure with a generic CMake error. The directory was wiped once already by a worktree removal.
- Header weight is real but the usual suspects are not the cause. A verifier measured compiling `mforce_cli/main.cpp` alone at about 95 s with the project's Release flags, of which about 36 s is pure header parsing repeated in every consumer TU; nlohmann/json (reachable from 38 of 128 engine headers) accounts for only 5 to 13 s of that, the rest is the engine's own inline code. A PCH of json plus the standard library helps a little; moving code out of headers is what helps a lot. The `/bigobj` flag on the CLI turned out not to be needed today. No PCH or unity build is configured.
- Test coverage (the test reviewer's count, unverified): about 14 of roughly 85 node and instrument types are exercised; the additive and physical-model cores have no tests and no golden checksums; nothing asserts the no-allocation rule (I grepped for allocation hooks and found none).
- Comment hygiene: the code carries its changelog inline (dates, backlog ids, REVIEW numbers; 126 such references in the UI file, 20 in instrument.h). The reviewers valued the provenance while auditing. For a public release it reads as a diary and should move to the docs that already exist.
- The UI exe relink-lock is handled by a stamp guard, a dedicated test tool, a `--stamp` mode and a session-start sweep instruction in CLAUDE.md instead of a build rule that never links over the running binary.

---

## 4. Findings

Severity: critical = correctness, real-time-safety or data-loss flaw that would embarrass a public release or corrupt audio; high = significant debt, performance loss or structural smell with a concrete cost; medium = worth fixing; low = nit. Status: confirmed = every skeptic upheld it (severity is the verifiers' median); confirmed (synthesis read) = I checked it directly; disputed = skeptics disagreed, read the notes before acting; unverified = the reviewer's claim and severity, not yet checked by anyone else. Refuted and out-of-scope items are in Appendix A. "(xN)" marks a finding N reviewers reported independently. Full text (evidence, consequence, recommendation, verifier notes) for every id is in `docs/audits/2026-10-02-raw/findings_verified.md`.

### Critical (9)

| id | sev | location | finding | status |
|---|---|---|---|---|
| F014 | critical | `core/dsp_wave_source.h:38` | WaveSource::next() throws from the per-sample path; the live audio callback has no handler, so a non-positive frequency terminates the process from the audio thread (x4) | confirmed |
| F076 | critical | `music/conductor.h:545` | Conductor::perform(Piece) performs every part's entire elementSequence once per section, each pass shifted by the cumulative section length (x2) | confirmed |
| F215 | critical | `source/phased_value_source.h:135` | Stage crossfade is run twice with a restart at every boundary, producing a discontinuity | confirmed (synthesis read) |
| F246 | critical | `engine/src/additive_source2.cpp:100` | AdditiveSource2 bakes partial frequencies from a stale currFreq_ at prepare, so notes play at the previous note's pitch and frequency modulation is ignored | confirmed (synthesis read) |
| F250 | critical | `engine/src/chord.cpp:204` | Chord::init_pitches loops forever whenever the chord has fewer distinct pitch classes than tones (power chord and every guitar/piano dictionary voicing) | confirmed (synthesis read) |
| F325 | critical | `mforce_cli/explore.cpp:361` | Explore instrument-mode pull loop violates the StreamingVoice contract: no performSource->tick(), no advanceList advance | confirmed (synthesis read) |
| F375 | critical | `mforce_ui/main.cpp:3864` | Audio callback blocks on g_audioMutex while the UI thread holds it across allocating, O(graph) note-on and rewiring work (x5) | confirmed (synthesis read) |
| F388 | critical | `mforce_ui/main.cpp:5401` | audition_load_at resizes/refills the buffer g_bufferPlayback points into before taking g_audioMutex (audio-thread use-after-free) (x2) | confirmed (synthesis read) |
| F428 | critical | `mforce_ui/main.cpp:11258` | Two live clipboard implementations are both bound to Ctrl+C/Ctrl+V, so one Ctrl+V pastes twice | confirmed (synthesis read) |

### High (76)

| id | sev | location | finding | status |
|---|---|---|---|---|
| F003 | high | `CMakeLists.txt:24` | Third-party dependencies are unversioned local checkouts; a fresh clone cannot configure and nothing says why | confirmed |
| F027 | high | `core/ramp.h:55` | Sine ramp with nonzero power (pseudo-sine) is mathematically broken: pow of a negative base for t>0.5 (NaN) and a stray absolute +0.5 offset | confirmed |
| F041 | high | `filter/filters.h:427` | DelayFilter never clamps delayTime; a delay longer than the 1 s buffer indexes buffer_[negative] | confirmed |
| F062 | high | `music/composer.h:404` | compose(Piece&, const PieceTemplate&) mutates the template through const_cast at three sites, plus a const_cast<Section*> on an object reachable non-const (x4) | confirmed (synthesis read) |
| F065 | high | `music/composer.h:524` | Two sources of truth for section harmony; the melody chord-tone path hand-scans Section::chordProgression and resolves at the static section scale, while the chord-part path uses HarmonyTimeline::chord_at, key contexts and passage-local progressions | unverified |
| F077 | high | `music/conductor.h:560` | Tree-built Pieces are silently inaudible: the only passage->event realization is a private Composer method, and Conductor drops parts with an empty elementSequence without a diagnostic | confirmed |
| F083 | high | `music/dun_parser.h:439` | Hand-built Pieces (DUN mode) have no realize step; `--dun` renders silence since the tree-walk was deleted | confirmed |
| F084 | high | `music/figure_transforms.h:236` | split/retrograde_steps/add_neighbor/add_turn brace-init FigureUnit{dur, step} and silently drop rest, accidental, articulation and ornament | confirmed |
| F109 | high | `music/random_figure_builder.h:62` | build() never verifies count or length; zigzag/neighbor/leap_fill emit wrong unit counts and wander_ pads/truncates pulses so total length != c.length | confirmed |
| F121 | high | `music/structure.h:215` | Harmony context is stored twice per Section and the two copies are read by different consumers with different scale resolution | unverified |
| F142 | high | `render/instrument.h:99` | PitchedInstrument is a 620-line all-public header struct carrying ~10 unrelated responsibilities, with trailing-underscore 'private' state that callers reach into (x2) | unverified |
| F145 | high | `render/instrument.h:193` | Live-path API (prepare_voice_at / continue_voice_live / acquire_voice) allocates and mutates the live graph in place, so the only valid caller discipline is a mutex the audio callback also takes | unverified |
| F160 | high | `source/additive/basic_additive_source.h:16` | Three additive sources coexist; BasicAdditiveSource is fully subsumed by AdditiveSource+FullPartials and AdditiveSource2 is a half-integrated, buggy port that one baseline patch uses | unverified |
| F161 | high | `source/additive/basic_additive_source.h:118` | BasicAdditiveSource resizes five std::vectors inside compute_wave_value (per-sample render path) | unverified |
| F164 | high | `source/additive/full_additive_source.h:49` | Structural pins (formant/partials/spectra) silently no-op when the wired source has the wrong interface — Parked.txt item 5 still live and undocumented in code | unverified |
| F169 | high | `source/additive/partials.h:266` | Partials is an 800-line class with ~85 data members and seven distinct responsibilities | unverified |
| F172 | high | `source/additive/partials.h:442` | Expand rule mutates the source arrays in place and restores from an 'orig' snapshot, so config changes after the first prepare are silently discarded; ExplicitPartials adds a third copy (Stat_) to work around it | unverified |
| F185 | high | `source/combined_source.h:198` | CrossfadeSource and PhasedValueSource crash/UB on an incompletely wired graph; siblings guard | unverified |
| F211 | high | `source/noise_sources.h:249` | PerlinNoiseSource narrows an unbounded double position to float before extracting the fraction, so the noise degrades over minutes | unverified |
| F218 | high | `source/repeating_source.h:81` | RepeatingSource never re-triggers when the gap rounds to zero samples — permanent silence after the first repetition | unverified |
| F219 | high | `source/repeating_source.h:95` | RepeatingSource::next() re-runs prepare() on its subgraph, pulling unbounded setup work (JSON graph rebuilds, node allocation, RTTI walks) into the sample path | unverified |
| F228 | high | `source/triangle_source.h:99` | TriangleSource emits NaN when bias is 0, a value its own descriptor permits | unverified |
| F229 | high | `source/wander_noise_source.h:66` | WanderNoiseSource and WanderNoise2Source expose an `amplitude` pin that is prepared but never advanced or applied | unverified |
| F233 | high | `source/wave_evolution.h:284` | Every evolution (and WavetableSource itself) allocates its scratch on the first next() of a note instead of in prepare(); EKS allocates and frees a full table every note | unverified |
| F235 | high | `source/wave_evolution.h:603` | Thirteen *EvolutionSource holder classes are ~580 lines of copy-pasted boilerplate; wave_evolution.h is a 1552-line god-file that should be split | unverified |
| F238 | high | `source/wave_evolution.h:1326` | HistogramEqualizeEvolution allocates a fresh std::vector<int> once per table cycle on the audio thread (x2) | confirmed (synthesis read) |
| F239 | high | `source/wave_evolution.h:1328` | WaveEvolution::evolve() implementations allocate per table cycle (HistogramEqualize, TargetEvolution, BezierPull, CellularAutomaton) | unverified |
| F248 | high | `engine/src/additive_source2.cpp:187` | AdditiveSource2 phase accumulator is never wrapped, so float precision degrades through every sustained note | unverified |
| F252 | high | `engine/src/hybrid_ks_source.cpp:35` | HybridKSSource allocates and runs an unbounded DFT inside the render path | unverified |
| F254 | high | `engine/src/mixer.cpp:18` | Mixer::render allocates a scratch vector on every block (x2) | unverified |
| F255 | high | `engine/src/mixer.cpp:28` | StereoMixer advances master gainL/gainR once per channel per frame, so with 2+ channels a modulated master gain runs N× too fast and channels see different gain samples | unverified |
| F256 | high | `engine/src/mixer.cpp:36` | StereoMixer::render is hand-duplicated per-sample in mforce_ui and the two copies have already diverged (engine unity-at-center pan vs UI's old -3 dB-center law) | confirmed (synthesis read) |
| F260 | high | `engine/src/patch_loader.cpp:70` | MonoSource/StereoMixer re-prepare the entire graph on every render() call; no block-streaming contract exists below the voice, which blocks a JUCE/host integration for mixer-mode patches | unverified |
| F263 | high | `engine/src/patch_loader.cpp:126` | Tap edges create shared_ptr ownership cycles; every voice graph containing a feedback loop leaks on rebuild | unverified |
| F269 | high | `engine/src/patch_loader.cpp:605` | build_graph is a 460-line if/else type switch that pre-empts the registry; 15 registered types never reach their registry factory or configurator | confirmed (synthesis read) |
| F272 | high | `engine/src/patch_loader.cpp:656` | build_graph's special-case chain re-implements wiring that the types' own descriptors already provide, bypasses reg.create so default seeds and constructor defaults are triplicated, and leaves the registry's BW-filter configurators dead (x2) | confirmed (synthesis read) |
| F276 | high | `engine/src/patch_loader.cpp:1194` | The legacy paramMap exists as three parallel implementations: engine build_bindings, UI JSON converter, and UI stash editor/dialog | unverified |
| F277 | high | `engine/src/patch_loader.cpp:1514` | Instrument assembly is copy-pasted between load_patch_file and load_instrument_patch_json and has already drifted: nodesById is only set on the JSON path, so per-note reseed and capture are silently inert for CLI patch-file renders | unverified |
| F278 | high | `engine/src/patch_loader.cpp:1514` | Patch loader has twin entry points and three copies of the JSON ref scanner | unverified |
| F284 | high | `engine/src/red_noise_source.cpp:10` | Constructor/fallback defaults disagree with descriptor defaults in three classes, so the same node sounds different from CLI JSON vs the UI | unverified |
| F290 | high | `engine/src/wavetable_source.cpp:21` | fill_table() throws std::runtime_error from inside next(); the live audio callback has no exception handler | unverified |
| F291 | high | `engine/src/wavetable_source.cpp:27` | Table allocation deferred to the first next(): WavetableSource, HybridKSSource and BasicAdditiveSource resize vectors inside the sample path | unverified |
| F298 | high | `durn_converter/main.cpp:59` | No common intermediate representation: four parsers each own a FigureUnit, a ScaleMap, a tie-merger and a letter table; their conversion layers are dead, discarded or lossy, and main.cpp re-derives everything in four bespoke adapters | unverified |
| F300 | high | `durn_converter/main.cpp:211` | ABC chromatic notes are silently re-pitched to the nearest diatonic degree with no accidental emitted | unverified |
| F303 | high | `durn_converter/main.cpp:449` | MusicXML conversion is dead: figureUnits is never populated, so every .xml file throws 'No notes found' | unverified |
| F312 | high | `engine_tests/CMakeLists.txt:1` | Neither test executable is registered with CTest; both silently depend on CWD = repo root; engine_tests has no warning flags (x3) | confirmed (synthesis read) |
| F317 | high | `engine_tests/main.cpp:831` | The non-negotiable 'no heap allocation in hot render loops' rule is asserted by no test anywhere in the repo | unverified |
| F321 | high | `engine_tests/main.cpp:1896` | Coverage: ~14 of ~85 engine source/instrument types are exercised; the synthesis core has no tests and no golden checksums | unverified |
| F328 | high | `mforce_cli/main.cpp:116` | beats→frames→render→stereo→peak/rms pipeline hand-rolled 6-7 times in main.cpp, again in explore.cpp and the UI, while engine signal_stats.h already provides the stats | unverified |
| F339 | high | `mforce_ui/main.cpp:1` | mforce_ui/main.cpp is a 14,658-line single translation unit with 349 file-scope statics; every UI hack in this census lives in it | confirmed (synthesis read) |
| F350 | high | `mforce_ui/main.cpp:1022` | update_all_dsp / update_node_dsp do allocating, graph-size-proportional work while holding the mutex the RtAudio callback blocks on | unverified |
| F354 | high | `mforce_ui/main.cpp:1368` | The patch JSON format has four hand-maintained codecs (engine loader, UI loader, two UI savers) plus a third clipboard schema; jsonExtras carry-through is the symptom | unverified |
| F356 | high | `mforce_ui/main.cpp:1561` | File > Open / Ctrl+O has no exception boundary: a malformed or non-patch JSON exits the process; a structural failure leaves an empty graph aimed at the bad file | unverified |
| F357 | high | `mforce_ui/main.cpp:1561` | load_graph_from_path is a ~800-line, nine-phase god function with dead state and an embedded layout algorithm | unverified |
| F359 | high | `mforce_ui/main.cpp:2499` | topo_sort is a 5-deep nested scan and runs every frame per drawn group via group_output_node | unverified |
| F377 | high | `mforce_ui/main.cpp:3963` | UI re-implements StereoMixer's equal-power pan twice and has already drifted from mixer.cpp (missing unity-center normalisation) (x2) | confirmed (synthesis read) |
| F383 | high | `mforce_ui/main.cpp:4872` | Dual DSP representation: the UI builds and streams its own DSP graph while Play/Generate use the engine-built instrument, so Stream and Play can render the same patch differently | unverified |
| F390 | high | `mforce_ui/main.cpp:6093` | Live shape/knot data replaced on DSP objects without g_audioMutex while the node-graph stream may be pulling them on the audio thread | unverified |
| F391 | high | `mforce_ui/main.cpp:6100` | draw_shape_editor is a ~930-line function with the Curve/Segment/Shaper client switch repeated more than a dozen times | unverified |
| F399 | high | `mforce_ui/main.cpp:7858` | QWERTY key-up handling is inside the key-down gate, so a held live note can be stranded (stuck voice) | unverified |
| F403 | high | `mforce_ui/main.cpp:8098` | Chords and Drums keep a second render pipeline beside generate_unified, duplicating post-processing and skipping the spectrum/evo updates | unverified |
| F417 | high | `mforce_ui/main.cpp:9227` | draw_properties_panel is a 1,035-line function dispatching on typeName strings with three nested lambdas and function-local statics | unverified |
| F421 | high | `mforce_ui/main.cpp:9826` | Array/formant/constant edits mutate DSP objects the audio thread is streaming without the audio lock | unverified |
| F422 | high | `mforce_ui/main.cpp:10029` | Properties panel mutates live DSP objects (Envelope stage vector, arrays, formant list) without g_audioMutex while the continuous streams read them on the audio thread | unverified |
| F431 | high | `mforce_ui/main.cpp:11481` | show_node_context_menu reads a GraphNode* after emplace_back/delete_node invalidated it (use-after-realloc / use-after-erase) | unverified |
| F432 | high | `mforce_ui/main.cpp:11507` | Four divergent implementations of 'copy a node's state' (Duplicate, clip_instantiate, replace_node_with, saver/loader fragment) | unverified |
| F433 | high | `mforce_ui/main.cpp:11739` | Waveform mip cache keyed on buffer pointer + 3-sample fingerprint can serve a stale display after a regenerate | unverified |
| F437 | high | `mforce_ui/main.cpp:12342` | draw_formant_strip advances the live node's formant from the UI thread while the audio thread may be pulling the same object | unverified |
| F443 | high | `mforce_ui/main.cpp:12948` | main() is a 1,711-line function: 12 inline headless CLI modes, GLFW/ImGui/audio/MIDI init, dock layout, menu bar and the whole frame loop in one body inside a zero-indented 1,200-line try (x2) | unverified |
| F446 | high | `mforce_ui/main.cpp:13284` | --dump-playback and --dump-stream hand-copy the audio callback's mixing arithmetic (and use a different voice entry point than the live path) while documenting themselves as exact replicas | unverified |
| F448 | high | `mforce_ui/main.cpp:13848` | The entire Node Editor window (~675 lines) is inline in main()'s frame loop while every sibling panel is a draw_* function | unverified |
| F453 | high | `mforce_ui/main.cpp:14253` | Ctrl+C / Ctrl+V are bound twice to two separate clipboard subsystems; a single Ctrl+V pastes the selection twice (x2) | confirmed (synthesis read) |
| F454 | high | `mforce_ui/main.cpp:14253` | Two independent node clipboards, and both key handlers fire on the same Ctrl+C / Ctrl+V | confirmed (synthesis read) |
| F457 | high | `mforce_ui/main.cpp:14427` | Fit (F key / status-bar Fit) queries imnodes positions for nodes hidden in collapsed groups, hitting imnodes' assert / Pool[-1] read | unverified |
| F458 | high | `mforce_ui/main.cpp:14498` | Delete-key node deletion never rewires DSP: consumers keep the deleted node's ValueSource and continuous play still hears it | unverified |
| F467 | high | `stamp_test/CMakeLists.txt:1` | The UI relink-lock is handled by a runtime stamp guard, a dedicated test tool and manual exe sweeps instead of fixing the build so the running binary is never the link output | unverified |

### Medium (211)

| id | sev | location | finding | status |
|---|---|---|---|---|
| F004 | medium | `engine/CMakeLists.txt:17` | No precompiled headers or unity build despite a header-heavy engine; each tool TU re-parses the whole engine (15-16 MB objects, /bigobj already required) | confirmed |
| F008 | medium | `core/curve_node.h:62` | CurveNode::map_expr re-implements the bracket search and Linear/LogX/LogLog interpolation that Curve::eval_core (curve.h:45-67) was introduced to share | confirmed |
| F011 | medium | `core/dsp_value_source.h:103` | Silent-no-op class: void set_param/add_param defaults mean the loader cannot detect a rejected wire; three further loader paths swallow bad input silently (Parked.txt item 5) | confirmed |
| F012 | medium | `core/dsp_value_source.h:103` | set_param/set_setting/add_param are string-keyed, return void and silently ignore unknown names and wrong-typed sources; structural pins have no type tag | confirmed |
| F013 | medium | `core/dsp_value_source.h:146` | Shared-node sharing model is wiring-order dependent: a RefSource consumer evaluated before the advancer reads the previous sample | confirmed |
| F015 | medium | `core/dsp_wave_source.h:59` | Per-sample std::fmod in every oscillator's phase wrap — a CRT call in the hottest loop the project has already profiled against | confirmed |
| F017 | medium | `core/envelope.h:16` | Envelope is a ~560-line accretion of five orthogonal concerns with ~30 state fields and three timing semantics interleaved in prepare() | confirmed |
| F021 | medium | `core/envelope.h:190` | replace_stages() move-assigns a polymorphic object onto itself (`*this = std::move(fresh)`), relying on implicit copy/move of a virtual class and discarding prepared runtime state | confirmed |
| F023 | medium | `core/envelope_presets.h:29` | Five preset envelope classes hand-mirror the same descriptor/set_setting/get_setting boilerplate and each re-stores the sample rate the base already holds | confirmed |
| F024 | medium | `core/envelope_presets.h:29` | Six envelope preset classes repeat ~70 lines of per-stage Curve/Power settings dispatch; seven identical factory lambdas | confirmed |
| F025 | medium | `core/envelope_presets.h:29` | Six copy-pasted envelope preset classes (backlog 36b) plus three envelope authoring dialects (36a) still present | confirmed |
| F028 | medium | `core/randomizer.h:73` | int_range can return max+1 (and select_int then reads past the vector) when uniform_real_distribution<float> yields exactly 1.0; select_int on an empty vector is UB | confirmed |
| F029 | medium | `core/range_source.h:19` | RangeSource has three defaults for `normalized` and they disagree: ctor default true, member initializer false, descriptor false | confirmed |
| F031 | medium | `core/smoothness_interpolator.h:21` | interpolate() evaluates the cosine branch unconditionally, wasting a std::cos per sample for smoothness 0, 0.5 and <0.5 | confirmed |
| F033 | medium | `core/var_source.h:26` | Every pin costs two virtual calls per sample (next() then current()) although next() already returns the value; constants pay the same price | confirmed |
| F035 | medium | `filter/filters.h:13` | Two biquad kernels in one unit with opposite a/b naming; the BW one is a generic shift-register FIR+IIR with a redundant, bounds-unsafe `n` parameter | unverified |
| F036 | medium | `filter/filters.h:54` | Butterworth LP and HP sections and filters are copy-pasted pairs differing only in section type (x2) | unverified |
| F038 | medium | `filter/filters.h:116` | BWLowpassFilter/BWHighpassFilter and BWLPSection/BWHPSection are copy-pasted pairs; FIR/IIR use heap vectors with shift loops for fixed 2nd-order sections | unverified |
| F040 | medium | `filter/filters.h:194` | BWLowpassFilter, BWHighpassFilter (and BWBandpassFilter) are copy-pasted; BWLPSection/BWHPSection likewise differ only in update() | unverified |
| F046 | medium | `filter/svf_source.h:32` | Sample rate is baked into nodes at construction while RenderContext also carries it; a host-driven sample-rate change cannot propagate and nodes disagree on which to trust | confirmed |
| F047 | medium | `filter/svf_source.h:119` | Per-sample transcendental recompute of slowly-varying coefficients in three nodes, contradicting the unit's own tan()-guard convention | confirmed |
| F048 | medium | `filter/vibrato.h:97` | Vibrato::prepare() rebuilds its entire LFO sub-graph (~10 make_shared) on every note-on, although the graph depends only on edit-time settings | confirmed |
| F049 | medium | `music/basics.h:116` | Uninitialized aggregate members: Pitch{} is a null-pointer sentinel that note_number() dereferences; Chord::def, Section::beats and PoolAtom fields are likewise uninitialized | confirmed |
| F054 | medium | `music/classical_composer.h:30` | The IComposer/Genre facade is dead and its narrower overloads cannot work on the event path | confirmed |
| F056 | medium | `music/composer.h:164` | Composer's constructor re-registers ~20 strategies/factories into four process-global singletons on every instance construction (x3) | confirmed |
| F057 | medium | `music/composer.h:241` | Dead or unreachable members: Composer::compose_figure/compose_phrase/registry_get_for_phase2, SectionStrategy, duration-keyed ChordPerformer lookup, unused locals/params | confirmed |
| F061 | medium | `music/composer.h:313` | Piece/section/passage key levels are derived by string concatenation and splitting at three sites, with two concrete leaks (exotic piece scale dropped; passage override uses piece tonic) | unverified |
| F063 | medium | `music/composer.h:420` | Beat bookkeeping disagrees across the pipeline: section_start_beat_ ignores truncateTailBeats while the realize loop, Conductor and the CLI all use effective (truncated) beats; totalBars truncates partial bars | unverified |
| F064 | medium | `music/composer.h:478` | realize_phrase_to_events_ takes 11 parameters including two out-params and a defaulted trailing bool | unverified |
| F066 | medium | `music/composer.h:587` | RealizationStrategyRegistry is dead weight: `realizationStrategy` is parsed but never read, 'block' is hard-coded, and the rhythm-pattern strategy's logic is re-implemented inline | unverified |
| F067 | medium | `music/composer.h:736` | RhythmPatternRealizationStrategy is registered but never resolved; its per-bar pattern walk is reimplemented inline in realize_chord_parts_ | confirmed |
| F068 | medium | `music/composer.h:820` | No-template fallback composes with a Locus pinned to (section 0, part 0) regardless of the actual part/section (x2) | confirmed |
| F069 | medium | `music/composer.h:902` | composer.h hosts ~1180 lines (57% of the file) of eight concrete strategies' out-of-line bodies under a dependency rationale that is no longer true (x2) | confirmed |
| F070 | medium | `music/composer.h:902` | composer.h is the hidden definition site for member functions of classes declared in six other headers, papering over a Composer<->strategy include cycle | disputed |
| F072 | medium | `music/composer.h:1215` | Literal-figure realization and the contour-motif transform chain are duplicated inside composer.h (x2) | confirmed |
| F074 | medium | `music/composer.h:1343` | Engine library code reads environment variables and writes diagnostics straight to std::cerr/stderr; <iostream> is a transitive dependency of 12 headers | confirmed |
| F078 | medium | `music/drift_voicing_profile_selector.h:40` | inversionProfiles/spreadProfiles config parsing is copy-pasted between Drift and Random selectors; VoicingProfile JSON parsing is duplicated between Scripted and templates_json.h | unverified |
| F080 | medium | `music/drift_voicing_profile_selector.h:88` | Non-static profile selectors drop the passage's authored repeatPenalty and cadential; composer injects the baseline only via dynamic_cast to StaticVoicingProfileSelector | unverified |
| F082 | medium | `music/dun_parser.h:147` | DURN parse_token never verifies the token was fully consumed; trailing characters, misplaced dots and glued `/` separators are silently accepted | confirmed |
| F088 | medium | `music/figures.h:110` | StepGenerator::random_sequence ignores its second parameter, so the preferSkips template knob is silently dead on the procedural path | confirmed |
| F090 | medium | `music/figures.h:399` | Binary duration alphabet, inverse-distance weighting and roulette selection are implemented three times (plus a fourth roulette in elaborate) (x2) | confirmed |
| F091 | medium | `music/figures.h:533` | Figure uses a virtual hierarchy, clone() and dynamic_cast to carry what is a one-bit tag, forcing a hand-written deep copy on Phrase and losing the type in JSON | confirmed |
| F093 | medium | `music/locus.h:3` | locus.h, piece_utils.h and rhythm_util.h pull templates.h (and with it nlohmann/json, figure_transforms, realization_strategy, voicing_profile) into the model layer | unverified |
| F095 | medium | `music/music_json.h:19` | Eleven hand-written two-direction enum<->string tables | confirmed |
| F098 | medium | `music/music_json.h:504` | Round-trip asymmetries: Phrase loses connectors and figure type; Section loses harmony and truncation; PieceTemplate::defaultPulse and SectionTemplate::styleName are one-directional (x2) | confirmed |
| F100 | medium | `music/parse_util.h:174` | Note-name parsing exists in two forms inside parse_util.h and in four more places across dun_parser and durn_converter | confirmed |
| F101 | medium | `music/parse_util.h:183` | Note-letter parsing duplicated between parse_note_input and parse_passage; DunToken->FigureUnit copy repeated three times in dun_to_piece; two identical drum structs | confirmed |
| F104 | medium | `music/piece_utils.h:58` | Pitch has no valid/invalid state; piece_utils uses Pitch{} (null pitchDef) as a not-found sentinel and feeds it to PitchReader::set_pitch | unverified |
| F105 | medium | `music/piece_utils.h:67` | Cursor walk (leadStep then units) and template/seed lookup are copied four and two times across piece_utils | unverified |
| F107 | medium | `music/pitch_walker.h:59` | Two scale walkers (PitchReader::step and step_note) with divergent descending semantics and a copy-pasted degree search | confirmed |
| F108 | medium | `music/random_figure_builder.h:52` | Function-local static Randomizer for figure count breaks seed reproducibility (x2) | confirmed |
| F111 | medium | `music/random_figure_builder.h:148` | try_arc_ throws inside the retry loop for /net/ > count-1 while feasible_ ignores net, so the same constraints succeed or throw depending on the seed | confirmed |
| F112 | medium | `music/random_figure_builder.h:199` | "Random" builder is deterministic given (count, pulse, net) for 70% of its shape mass; callers compensate by re-rolling inputs | confirmed |
| F113 | medium | `music/realization_strategy.h:79` | Composer<->Conductor chord boundary is still split across both tiers, contrary to the recorded decision | unverified |
| F117 | medium | `music/smooth_voicing_selector.h:32` | No-scale "fallback" null-dereferences: Scale{} has uninitialized raw pointers and resolve() calls scale.length() immediately | confirmed |
| F118 | medium | `music/smooth_voicing_selector.h:41` | Dictionary lookup uses catch(...) as control flow and silently falls back to canonic voicings on a typo | confirmed |
| F119 | medium | `music/smooth_voicing_selector.h:53` | First chord of every Part ignores allowedInversions/allowedSpreads/cadential (returns inv=0, spread=0 whenever previous is null) | confirmed |
| F123 | medium | `music/style_table.h:89` | ChordLabel round-trip is keyed on ChordDef::name strings that do not exist: V7 relabels as "V", viio as "VIIo" (unreachable row → silent I), parse("M7"/"m7") throws | confirmed |
| F124 | medium | `music/style_table.h:130` | StyleTable overrides can never fire: walk() pushes the current label before lookup, so the back-off key is always "X,X" | confirmed |
| F133 | medium | `music/templates_json.h:111` | Six enum loaders silently map unknown strings to a default while four throw — a typo in an authored template changes the composition instead of failing the load | confirmed |
| F135 | medium | `music/templates_json.h:799` | from_json(PeriodSpec) carries a 42-line copy of from_json(PhraseTemplate) that has already drifted and silently drops strategy configs (x4) | confirmed |
| F137 | medium | `music/templates_json.h:1035` | const json::operator[] used on keys that are not checked — JSON_ASSERT in debug, end-iterator dereference (UB) in release on malformed input | unverified |
| F138 | medium | `music/templates_json.h:1179` | to_json(SectionTemplate) omits styleName, which from_json reads and Composer consumes — engine round trip drops the style table | unverified |
| F139 | medium | `music/templates_json.h:1236` | No schema version on PieceTemplate or Piece JSON; back-compat shims accumulate and unknown keys are silently ignored | unverified |
| F143 | medium | `render/instrument.h:99` | PitchedInstrument is a ~620-line all-public struct mixing voice-pool accounting, offline rendering, held-line state machine, capture, live-continuation and diagnostics | unverified |
| F144 | medium | `render/instrument.h:181` | hiBoost hidden loudness law (backlog 19) still live; formula duplicated in two methods and the magic 0.3 hardcoded at six call sites | unverified |
| F146 | medium | `render/instrument.h:246` | StreamingVoice's per-sample protocol (tick → next → advance) is enforced by comment only and is already violated by mforce_cli --explore, which renders bend/loop patches wrong | confirmed |
| F148 | medium | `render/instrument.h:306` | Library code writes diagnostics straight to stderr/cerr (58 sites in engine/) | confirmed |
| F152 | medium | `render/instrument.h:609` | fire_triggers after prepare on the fresh offline path re-anchors stage 0 at the slot's stale cur_, and the live path never fires triggers at all — fresh notes diverge offline vs live | confirmed |
| F153 | medium | `render/instrument.h:620` | Ring-out / cap-fade / containment block is copy-pasted between play_note (classic) and finish_line, and mirrored a third time in the UI audio callback (x3) | confirmed |
| F155 | medium | `render/instrument.h:629` | Ring-out + cap-fade + containment loop is triplicated with diverging constants (play_note, finish_line, UI audio callback) | confirmed |
| F159 | medium | `source/additive/additive_source2.h:116` | AdditiveSource2's 'partials' pin snapshots the raw Partials arrays, bypassing the lazy arrayUpdateReq_ rebuild, so it receives constructor defaults instead of the patch's settings | unverified |
| F165 | medium | `source/additive/partials.h:62` | ExpandRuleNode exposes ten ValueSource params that are read exactly once at patch load; wiring an envelope to them in the UI silently does nothing | unverified |
| F166 | medium | `source/additive/partials.h:175` | Descriptor spans re-declared in subclasses (macro splice in partials.h; frequency/amplitude/phase in 8 wave sources) (x2) | unverified |
| F167 | medium | `source/additive/partials.h:175` | Macro splices shared descriptor rows into four descriptor tables; descriptor names are otherwise hand-triplicated across descriptors/set_param/get_param with a documented drift bug | unverified |
| F170 | medium | `source/additive/partials.h:319` | Every node hand-writes the same pin plumbing five times per pin (descriptor, set_param, get_param, prepare forward, next forward, current read) — 44 set_param overrides of pure boilerplate | unverified |
| F174 | medium | `source/additive/partials.h:499` | Motion and shimmer layers are copy-pasted: identical prepare blocks, identical advance loops, parallel member triplets | unverified |
| F175 | medium | `source/additive/partials.h:818` | Per-partial cache rebuilds every sample with std::pow per partial whenever multEnv or roEnv actually moves — the documented purpose of those params | unverified |
| F176 | medium | `source/additive/partials.h:859` | const member mutates through const_cast instead of a mutable cache (x2) | unverified |
| F177 | medium | `source/additive/partials.h:1087` | Base Partials setting descriptors are hand-copied into all three subclass lists, with only the newer entries spliced by macro | unverified |
| F178 | medium | `source/allpass_resonator.h:131` | AllpassResonator captures frequency once per note but does not override tracks_frequency_live() | unverified |
| F179 | medium | `source/allpass_resonator.h:219` | AllpassResonator lacks KSString's top-of-keyboard and Thiran-eta guards | unverified |
| F184 | medium | `source/combined_source.h:101` | Three hand-rolled linear crossfades; CrossfadeSource is the two-stage case of PhasedValueSource | unverified |
| F186 | medium | `source/combined_source.h:331` | StaticVar/StaticRange/PhasedValueSource bypass the self-describing contract via registry configurators poking public fields | unverified |
| F187 | medium | `source/delay_line_source.h:25` | Buffer is sized for ratio 1 at 20 Hz while the ratio pin advertises up to 20x, so long delays silently clamp | unverified |
| F189 | medium | `source/fm_source.h:74` | Oversampled FM rebuilds its decimation filter bank with heap allocations on every prepare (per note) | unverified |
| F191 | medium | `source/ks_string.h:56` | KSString bundles four mechanisms, 21 settings and five pins in one 636-line class | unverified |
| F193 | medium | `source/ks_string.h:156` | Stability-critical settings are unclamped in set_setting, and the loader does not clamp against descriptors | unverified |
| F195 | medium | `source/ks_string.h:253` | 48 kHz-baked per-sample constants (backlog 69) and the WaveEvolution sample-rate audit (Parked.txt item 3) are still open in code | unverified |
| F197 | medium | `source/ks_string.h:386` | Buffer sizes and time constants are baked for 48 kHz while sibling nodes derive from sr | unverified |
| F198 | medium | `source/ks_string.h:426` | Waveguide primitives (tuning allpass, DC blocker, phase-delay solvers, KS averaging, fill_table) re-typed per node | unverified |
| F199 | medium | `source/ks_string.h:438` | Four DSP primitives are copy-pasted across KSString, AllpassResonator, BowTable and wave_evolution | unverified |
| F201 | medium | `source/ks_string.h:483` | init_note reads lpDelay_ in the dispersion-shedding guard before computing it | unverified |
| F202 | medium | `source/layered_red_noise_source.h:60` | LayeredRedNoiseSource::set_array leaves count_ inconsistent with the arrays and sync_arrays_ has a dead branch | unverified |
| F205 | medium | `source/multiplex_source.h:26` | reseed() per-note determinism contract not honored by MultiplexSource clones or DistortedSource | unverified |
| F207 | medium | `source/multiplex_source.h:97` | set_clone_param allocates strings and a map node on every note-on, under the audio mutex | unverified |
| F208 | medium | `source/multiplex_source.h:105` | MultiplexSource::prepare() can run a full JSON parse + build_graph for N clones; in the UI this happens under the audio mutex at note-on | unverified |
| F209 | medium | `source/noise_sources.h:16` | BlueNoiseSource has no reseed() override, so its embedded PinkNoiseSource is never re-anchored at a continuation note | unverified |
| F212 | medium | `source/noise_sources.h:444` | MurmurationNoiseSource runs an O(n²) flock update plus a pow and a sin per bird at audio rate | unverified |
| F213 | medium | `source/noise_sources.h:456` | Murmuration separation force sums every running-minimum neighbour, not the nearest one, so the result depends on bird index order | unverified |
| F220 | medium | `source/repeating_source.h:112` | RepeatingSource re-runs the child graph's prepare() from inside next() on every repetition | unverified |
| F221 | medium | `source/saw_source.h:18` | PolyBLEP residual is duplicated between SawSource and PulseSource | unverified |
| F222 | medium | `source/saw_source.h:18` | Oscillator / filter math duplicated across nodes: BLEP, Friedlander friction, resonance and RBJ biquad coefficients, random-walk variation | unverified |
| F223 | medium | `source/segment_source.h:22` | SegmentSource and RepeatingSource carry a constructor-time public `sampleRate` and ignore RenderContext — two sources of truth after the render-context refactor | unverified |
| F231 | medium | `source/wave_evolution.h:45` | Parked.txt item 3 (48k rate-bake) is CONFIRMED by the code and is broader than the note states; the note is also stale (names a deleted class) | unverified |
| F236 | medium | `source/wave_evolution.h:603` | Twelve WaveEvolution holder wrappers repeat the same ValueSource shell | unverified |
| F237 | medium | `source/wave_evolution.h:620` | Live setting edits on Pluck/Averaging holders replace the evolution object and lose adjust() state until the next note — the sounding note does not reflect the slider | unverified |
| F240 | medium | `source/wavetable_source.h:15` | Nothing in this unit implements reseed(); WavetableSource's internal noise and every evolution Randomizer free-run through an in-line note | unverified |
| F241 | medium | `source/wavetable_source.h:21` | Two ownership/wiring paths for the evolution (legacy string-typed unique_ptr vs holder-node shared_ptr) with divergent defaults; TargetEvolution is reachable only via the legacy path and divides by zero on an empty target | unverified |
| F242 | medium | `source/white_noise_source.h:40` | Every class hand-maintains five parallel pin lists plus dead typed accessors; the drift bugs above are the measured cost | unverified |
| F243 | medium | `source/white_noise_source.h:98` | Sign/boost/continuity draw block is copied verbatim between WhiteNoiseSource and RedNoiseSource | unverified |
| F244 | medium | `source/white_noise_source.h:98` | Noise family copies: white/red shaping block, Wander1/Wander3 pin plumbing | unverified |
| F247 | medium | `engine/src/additive_source2.cpp:182` | Legacy sources pay CRT transcendental calls per partial per sample (pow, fmod, sin, floor) — the same costs partials.h measured and eliminated | unverified |
| F257 | medium | `engine/src/music.cpp:35` | Pitch::from_note_number throws for negative input and truncates fractional input; this is the documented crash path for two strategies | unverified |
| F262 | medium | `engine/src/patch_loader.cpp:98` | The shared-source auto-RefSource rule is implemented twice (engine loader and UI) and the JSON ref-edge scanner four times inside the loader | unverified |
| F264 | medium | `engine/src/patch_loader.cpp:190` | Four hand-rolled recursive JSON edge scanners with diverging semantics (ref / inputs / tap coverage differs per copy) | unverified |
| F265 | medium | `engine/src/patch_loader.cpp:190` | Recursive JSON ref/tap walker copied three times and the voice-pool build copied twice ('see render-path twin above') | unverified |
| F266 | medium | `engine/src/patch_loader.cpp:398` | Transitive-include reliance and unused includes (x2) | unverified |
| F267 | medium | `engine/src/patch_loader.cpp:432` | wire_params_generic contains type-specific blocks dispatched by dynamic_cast (Shaper invariants, Partials expandRule) although the registry provides a per-type configurator hook for exactly this | unverified |
| F268 | medium | `engine/src/patch_loader.cpp:509` | Unknown-key warning lives inside wire_params_generic, so the special-case branches that never call it (VarSource, RangeSource, FormantSpectrum, FixedSpectrum, PerformNode) silently ignore typos; allowlist is hand-grown | unverified |
| F270 | medium | `engine/src/patch_loader.cpp:617` | Registry population is ad hoc: a check-then-set static bool in build_graph plus 14 explicit register_all_sources() calls in the tools; register_type silently overwrites duplicates | unverified |
| F273 | medium | `engine/src/patch_loader.cpp:680` | Envelope stage-form parser and the RefSource first-consumer-advances rule are each duplicated in mforce_ui/main.cpp; the loader's own comment says only the preset form was shared | unverified |
| F274 | medium | `engine/src/patch_loader.cpp:838` | Loader special-case branches shadow registry configurators and re-apply what wire_params_generic already did | unverified |
| F275 | medium | `engine/src/patch_loader.cpp:1041` | Multiplex instances are built lazily on first prepare(): JSON parse plus count× build_graph runs inside the audio-mutex critical section on each voice's first note in the live UI | unverified |
| F279 | medium | `engine/src/patch_loader.cpp:1534` | JSON-only analyses are recomputed once per voice (and the advance list twice per voice) although they depend only on the patch JSON | unverified |
| F280 | medium | `engine/src/patch_loader.cpp:1534` | Voice-pool construction and instrument-block parsing are duplicated between load_patch_file and load_instrument_patch_json, and have already diverged (nodesById only populated in one) (x2) | unverified |
| F282 | medium | `engine/src/patch_loader.cpp:1687` | Whole node graph is built a second time, with a throwaway PerformContext, only to resolve the mixer's gainL/gainR (x2) | unverified |
| F283 | medium | `engine/src/patch_loader.cpp:1783` | Mixer/channel params resolved after build_graph bypass the usage counter, so a source shared between a graph pin and a channel volume/pan or mixer gain is advanced twice per sample | unverified |
| F285 | medium | `engine/src/red_noise_source.cpp:20` | prepare() leaves ramp/walk state from the previous note in RedNoise, WhiteNoise, WanderNoise and WanderNoise2, unlike their siblings | unverified |
| F287 | medium | `engine/src/wav_reader.cpp:74` | Chunk walk mis-handles data-before-fmt, odd-length chunk padding, and the 0xFFFFFFFF data size used by streaming-written WAVs (bad_alloc out of a bool API) | unverified |
| F288 | medium | `engine/src/wav_reader.cpp:96` | Reader and writer both do per-sample 2-byte stream I/O instead of one bulk transfer | unverified |
| F289 | medium | `engine/src/wav_writer.cpp:56` | write_wav_16le_stereo returns true without ever checking the stream after writing | unverified |
| F292 | medium | `engine/src/wavetable_source.cpp:44` | First-sample init and the tuning allpass are duplicated across compute_raw/compute_interpolated (and the allpass a third time in EKS); the interpolated copy is incomplete | unverified |
| F293 | medium | `engine/src/wavetable_source.cpp:120` | Evolutions assume evolve() sweeps indices sequentially once per sample; the interpolated readout path with speedFactor != 1 skips or repeats indices and misses the index==0 cycle triggers | unverified |
| F296 | medium | `durn_converter/CMakeLists.txt:4` | Two tools pin C++17 while pointing at the C++20 engine include tree they do not actually use | unverified |
| F297 | medium | `durn_converter/main.cpp:25` | emit_durn silently quantizes durations to ten tokens and re-synthesizes bar/phrase structure from accumulated beats, although DURN supports triplets and ties and the sources carry real barlines | unverified |
| F299 | medium | `durn_converter/main.cpp:176` | ABC pickup detection re-scans the raw text behind the parser's back and miscounts chord symbols and grace notes | unverified |
| F301 | medium | `durn_converter/main.cpp:224` | kern path parses and tie-merges every file twice; the first conversion's output is thrown away | unverified |
| F302 | medium | `durn_converter/main.cpp:391` | midi_to_durn copy-pastes a 14-line accidental block into both branches and re-implements ScaleMap's nearest-degree search | unverified |
| F306 | medium | `durn_converter/parsers/kern_parser.h:40` | durn_converter's four format parsers each carry a private ScaleMap/FigureUnit/trim implementation | unverified |
| F307 | medium | `durn_converter/parsers/kern_parser.h:452` | Spine manipulators (*^ *v *- *x) are not handled, so chosen_spine indexes the wrong column after any spine split | unverified |
| F309 | medium | `durn_converter/parsers/musicxml_parser.h:26` | Hand-rolled XML by substring search, with a dead get_attribute whose logic is re-implemented inline | unverified |
| F310 | medium | `durn_converter/parsers/musicxml_parser.h:527` | Four different error conventions across the four parsers; one (MusicXML) uses a sentinel title string main.cpp never inspects | unverified |
| F311 | medium | `durn_converter/parsers/musicxml_parser.h:578` | MusicXML durations are divided by the last <divisions> value seen, not the one in force when each note was parsed | unverified |
| F313 | medium | `engine_tests/main.cpp:9` | Two divergent hand-rolled test harnesses with different failure semantics and output | unverified |
| F314 | medium | `engine_tests/main.cpp:144` | engine_tests/main.cpp is a 1930-line append-only single TU: 20 mid-file #includes, one header included twice, headers used before they are included, arbitrary run order | unverified |
| F318 | medium | `engine_tests/main.cpp:847` | Harness failure modes are crashes, not reports: CHECK continues after a failed null-check and the pointer is dereferenced; main() catches nothing; load_scoreless parses a possibly-unopened file | unverified |
| F323 | medium | `mforce_cli/explore.cpp:313` | run_explore's 110-line while(true) with three goto next_variant and a double parse of the base patch | unverified |
| F327 | medium | `mforce_cli/main.cpp:103` | CLI render epilogue (mono->stereo, peak/rms, 'Wrote:' report) is copy-pasted seven times; UI chord render duplicates CLI run_chords (x3) | unverified |
| F331 | medium | `mforce_cli/main.cpp:709` | Output buffer length uses a single section's bpm while Conductor::perform honours per-section tempo and truncation | unverified |
| F332 | medium | `mforce_cli/main.cpp:968` | --test-ornaments, --josie and --melody are unrunnable or orphaned dev harnesses carrying hard-coded paths and song data | unverified |
| F335 | medium | `mforce_ui/CMakeLists.txt:4` | mforce_ui is a single 14,658-line translation unit; every UI edit recompiles the whole UI plus all engine headers | unverified |
| F337 | medium | `mforce_ui/CMakeLists.txt:30` | Windows-only surface is concentrated in mforce_ui but is unguarded at both the CMake and source level | unverified |
| F338 | medium | `mforce_ui/CMakeLists.txt:40` | Warning policy is copy-pasted per target, inconsistent, and never enforced (no /WX anywhere) | unverified |
| F340 | medium | `mforce_ui/main.cpp:132` | No single source of truth for UI-special node types: five hand-maintained lists that have already drifted once | unverified |
| F342 | medium | `mforce_ui/main.cpp:245` | GraphNode is a tagged-union-by-convention god struct: every node carries every node type's fields, dispatch is typeName string if-chains, and the constructor reads global s_nodes during its own emplace_back | unverified |
| F345 | medium | `mforce_ui/main.cpp:649` | std::vector<GraphNode> with a non-noexcept move: every reallocation deep-copies all nodes, and GraphNode* handles are invalidated by any emplace_back | unverified |
| F346 | medium | `mforce_ui/main.cpp:722` | s_loadedParamMap is documented as dead residue but is still a live, edited binding model; two contradictory comments in this range | unverified |
| F347 | medium | `mforce_ui/main.cpp:806` | Linear-scan pin/node lookups nested inside per-frame and per-edit loops; vector<GraphNode> hands out raw pointers that emplace_back invalidates | unverified |
| F349 | medium | `mforce_ui/main.cpp:938` | Editor wires every unconnected input-descriptor pin to a 0.0 stand-in constant, clobbering engine defaults (backlog 35c), and reconstructs wires by exact pin name so loader aliases drop silently (35a) | unverified |
| F351 | medium | `mforce_ui/main.cpp:1081` | update_all_dsp swallows every exception, silently accepting a half-wired graph | unverified |
| F352 | medium | `mforce_ui/main.cpp:1114` | delete_node removes links and the node but never re-wires DSP, so consumers keep pulling the deleted node's dspSource; the continuous stream then plays a graph the canvas no longer shows | unverified |
| F353 | medium | `mforce_ui/main.cpp:1258` | save_file_dialog / open_file_dialog are verbatim specializations of text_save_dialog / text_open_dialog — four copies of the OPENFILENAMEA setup | unverified |
| F355 | medium | `mforce_ui/main.cpp:1368` | Envelope stage-list parsing exists in the UI and the engine loader but not in envelope_json.h; the UI serializer has no engine twin at all | unverified |
| F363 | medium | `mforce_ui/main.cpp:2806` | serialize_patch_graph is 460 lines with eight phases and a side effect on the model (x2) | unverified |
| F364 | medium | `mforce_ui/main.cpp:2806` | 14,658-line single translation unit with 349 file-scope statics and three parallel node serializers plus two clipboard systems | unverified |
| F365 | medium | `mforce_ui/main.cpp:3024` | ~130 lines of per-node emission copy-pasted between serialize_patch_graph and save_node_graph | unverified |
| F367 | medium | `mforce_ui/main.cpp:3268` | Save ignores write failure and then clears the dirty flag | unverified |
| F369 | medium | `mforce_ui/main.cpp:3502` | ~150 lines of retired render-path code still compiled: get_playback_patch_path, voice_schedule, apply_perform_nodes, apply_param_map, eval_map_curve/vcurve | unverified |
| F373 | medium | `mforce_ui/main.cpp:3827` | Voice stealing is a hard cut with no declick, and a 'self-heal' masks an acknowledged slot-accounting bug | unverified |
| F374 | medium | `mforce_ui/main.cpp:3842` | UI thread reads audio-thread-written state (voice.active, midiNote, g_streamSource, g_bufferPlayback) without the mutex or atomics (x2) | unverified |
| F376 | medium | `mforce_ui/main.cpp:3880` | Pure per-sample granularity end to end: no block path in ValueSource, 16-slot active scan per sample in the callback, render_chunk called with n=1 per sample offline | unverified |
| F378 | medium | `mforce_ui/main.cpp:4103` | audio_watchdog never retries once init_audio has failed, although its status messages promise it will | unverified |
| F379 | medium | `mforce_ui/main.cpp:4208` | UI fakes infinite streaming by mutating engine Envelope objects (absolute_time flip, temporary hold stage, 2-hour prepare) — a documented UI-side workaround for a one-flag engine feature (x2) | unverified |
| F380 | medium | `mforce_ui/main.cpp:4484` | Three UI mirrors of CurveNode::map, two of which already disagree | unverified |
| F381 | medium | `mforce_ui/main.cpp:4629` | Instrument cache key is 'any edit', so every Properties value tweak forces a full serialize->parse->polyphony×build on the next key press | unverified |
| F382 | medium | `mforce_ui/main.cpp:4716` | UI collect_envelopes re-walks the voice graph with dynamic_cast on every note-on (under the audio lock) although the loader already collects allEnvelopes per voice | unverified |
| F384 | medium | `mforce_ui/main.cpp:5080` | MIDI input is polled once per UI frame at frame end, adding a frame of latency and freezing during synchronous Generate | unverified |
| F393 | medium | `mforce_ui/main.cpp:7054` | curve_eval re-implements CurveNode::map (LogX) in the UI while the sibling editor already evaluates through the engine | unverified |
| F394 | medium | `mforce_ui/main.cpp:7142` | Three copies of the breakpoint-table + log-spaced PlotLines editor idiom | unverified |
| F395 | medium | `mforce_ui/main.cpp:7228` | curve_node_destination is O(nodes x links x nodes x pins) and runs every frame to build a header string | unverified |
| F396 | medium | `mforce_ui/main.cpp:7552` | Mappings table rebuilds a perform-rooted ancestry walk per pin per frame using linear scans (O(nodes x pins x links x depth) with std::function recursion) | unverified |
| F398 | medium | `mforce_ui/main.cpp:7796` | Mappings dialog carries two binding models at once: wires are declared canonical, but Add still creates legacy paramMap stash entries | unverified |
| F401 | medium | `mforce_ui/main.cpp:7955` | On-screen keyboard still assumes the single-zone QWERTY map: lower-zone keys (offsets -12..-1) play below the drawn range and get no label or highlight | unverified |
| F402 | medium | `mforce_ui/main.cpp:8037` | Key rectangle geometry is computed twice: once in the draw loops and again, verbatim, in the hit-test loops | unverified |
| F404 | medium | `mforce_ui/main.cpp:8275` | render_drums_waveforms resizes g_outputWaveform without buffer_playback_detach(), violating the documented invariant | unverified |
| F405 | medium | `mforce_ui/main.cpp:8275` | g_bufferPlayback raw-pointer-into-vector invariant is enforced by convention at four sites and the drums path omits the required detach | unverified |
| F406 | medium | `mforce_ui/main.cpp:8420` | transport_generate overwrites the renderers' error status with 'Generated N ...', and transport_play plays after a failed regenerate | unverified |
| F411 | medium | `mforce_ui/main.cpp:8625` | Octave has two sources of truth (noteStr vs g_transport.octave) that only sync in one direction | unverified |
| F412 | medium | `mforce_ui/main.cpp:8712` | Inversion and Spread spinners on the Chords tab write fields nothing reads | unverified |
| F413 | medium | `mforce_ui/main.cpp:8920` | No undo/redo and no edit abstraction: dirty flag and instrument-cache invalidation are maintained by convention at ~65 call sites, with misses | unverified |
| F419 | medium | `mforce_ui/main.cpp:9337` | Repeated blocks inside draw_properties_panel: pin-edit apply, setting apply, centered table header, preview plotting | unverified |
| F426 | medium | `mforce_ui/main.cpp:10787` | source_family_menus/experimental_family_menu are a hand-maintained second registry of ~75 (label, type) pairs | unverified |
| F427 | medium | `mforce_ui/main.cpp:10974` | show_create_menu creates Output in two places with different side effects; special-node items skip the group/dirty handling the generic path does | unverified |
| F429 | medium | `mforce_ui/main.cpp:11269` | OS-clipboard copy/paste round-trips the whole graph through temp files and a full reload | unverified |
| F430 | medium | `mforce_ui/main.cpp:11442` | Temp files on disk are the graph-merge mechanism for paste/copy, and a dead temp-file playback path plus stale comments survive the 2026-09-13 unification | unverified |
| F435 | medium | `mforce_ui/main.cpp:11922` | Log-frequency axis, freq<->x mapping, hover readout box, pop-out button and early-return blocks are duplicated across spectrum, partials and formant drawers | unverified |
| F439 | medium | `mforce_ui/main.cpp:12472` | Zoom/Fit/scrollbar/wheel/ctrl-wheel handling is copied between draw_waveform_window and draw_wave_popouts | unverified |
| F440 | medium | `mforce_ui/main.cpp:12503` | Scroll range and Fit zoom ignore the strip margin and column width, so the tail of every buffer is unreachable (and Fit cuts it off) | unverified |
| F444 | medium | `mforce_ui/main.cpp:12978` | Ten headless modes repeat the identical context/registry prologue and try/catch-print-return epilogue; mono-to-stereo widening copied twice | unverified |
| F445 | medium | `mforce_ui/main.cpp:13054` | --gencheck re-implements the engine's v2 score parsing instead of calling it | unverified |
| F447 | medium | `mforce_ui/main.cpp:13571` | Audio device is torn down and reopened on every window refocus after >2 s away, plus a manual 'Restart audio' menu item, as substitutes for device-change notification | unverified |
| F450 | medium | `mforce_ui/main.cpp:13997` | 'Visible representative at this drill level' ancestor walk is written four times (one copy dead) and the wormhole ghost overlay is written twice | unverified |
| F451 | medium | `mforce_ui/main.cpp:14005` | Per-frame link/visibility pass is O(links x nodes x pins) plus string-keyed group lookups, and redoes lookups it already has | unverified |
| F452 | medium | `mforce_ui/main.cpp:14145` | Wormhole double-click 'jump to twin' centering is overwritten by the drill camera on the next frame whenever the twin is at a different drill level | unverified |
| F460 | medium | `mforce_ui/main.cpp:14630` | Generate is a blocking render on the UI thread sequenced by a magic-int state machine so one 'Generating...' frame gets drawn first | unverified |
| F461 | medium | `mforce_ui/main.cpp:14649` | Exception exit path skips shutdown_audio/shutdown_midi, so static teardown destroys g_voices while the RtAudio callback thread may still run | unverified |
| F462 | medium | `ppl_to_json/CMakeLists.txt:4` | Tools hardcode engine include paths (one of them unused) instead of consuming targets; nlohmann/json has no target of its own | unverified |
| F468 | medium | `stamp_test/main.cpp:42` | stamp_test --deps re-implements build_stamp.h's newest_from_tlogs dependency walk line for line; header comment is stale (claims no CMake target, C++17) | unverified |
| F470 | medium | `stk_ref/bowed_ref.cpp:31` | write_wav16_mono is copied verbatim into all eight stk_ref programs (x2) | unverified |
| F472 | medium | `test_figures/main.cpp:73` | PieceTemplate scaffold copied six times, three identical Result early-return ladders, eight identical integ_* wrappers, and a local total_duration() duplicating MelodicFigure::total_duration() | unverified |

### Low (164)

| id | sev | location | finding | status |
|---|---|---|---|---|
| F001 | low | `CMakeLists.txt:4` | ISA baseline and floating-point model are implicit; hot-loop code is hand-written around an unstated build constraint and correctness depends on the unstated /fp default | confirmed |
| F002 | low | `CMakeLists.txt:19` | GLFW, RtAudio and RtMidi are built unconditionally even for CLI/render-only configurations; no CMAKE_BUILD_TYPE default for single-config generators | confirmed |
| F005 | low | `engine/CMakeLists.txt:17` | mforce_engine is not consumable as a library: C++20 is not a target property, third_party is exported wholesale, and there is no export/install/alias | confirmed |
| F006 | low | `engine/CMakeLists.txt:22` | Warning configuration is inconsistent per target, has no warnings-as-errors, and is MSVC-only | disputed |
| F009 | low | `core/denormals.h:8` | FTZ/DAZ helper gates on _MSC_VER, which also matches MSVC ARM64 where <immintrin.h>/_MM_SET_* do not exist | confirmed |
| F019 | low | `core/envelope.h:73` | A physical-model policy (reflection allowance) is baked into every Envelope as an engine-wide constant | confirmed |
| F020 | low | `core/envelope.h:113` | gate_release re-layout diverges from prepare()'s stage math (no stage_accuracy jitter, no nominal override) and leaves a running stage's end stale | confirmed |
| F022 | low | `core/envelope_presets.h:23` | Preset envelopes cannot select RampType::Hold: label table has four entries and setters clamp to [0,3] | confirmed |
| F026 | low | `core/multi_source.h:46` | MultiSource `delaySamples` mutes the source's first N samples rather than delaying it, prepare over-provisions frames that are never pulled, and index/null accesses are unchecked | unverified |
| F030 | low | `core/sequence.h:48` | sequence.h is dead (included nowhere in engine/ or tools/) and does not include the headers it uses | unverified |
| F032 | low | `core/var_source.h:19` | VarSource relative mode carries lastVal_ across prepare(), so a re-prepared voice continues from the previous note's last value | confirmed |
| F034 | low | `filter/biquad_source.h:104` | Resonance mode overwrites the a1/a2 *settings*, so get_setting reports derived coefficients and a mode flip back to Raw inherits them | confirmed |
| F037 | low | `filter/filters.h:63` | 39 hand-typed PI literals instead of std::numbers | unverified |
| F039 | low | `filter/filters.h:117` | Dead parallel accessor API (set_source/get_source/set_cutoffFreq/...) duplicated on every node beside set_param/get_param, with zero callers | unverified |
| F042 | low | `filter/hammer_bank.h:128` | Hand-written pi literals of differing precision instead of C++20 std::numbers::pi_v<float> | confirmed |
| F044 | low | `filter/reverb.h:118` | Ring-buffer wrap via integer modulo per sample (12 idivs/sample in Reverb, 3 in DelayFilter, 1 in Limiter) | unverified |
| F045 | low | `filter/svf_source.h:30` | Unscoped enums and non-explicit single-argument constructors | confirmed |
| F050 | low | `music/basics.h:280` | Dead legacy API left in the core headers: SimpleNote/Tone/DrumHit/Beat, RhythmicFigure, semitones_between, passing-tone helpers, reader_before, generate_musical_rhythm, and test-only transforms | confirmed |
| F051 | low | `music/basics.h:333` | Chord mixes identity, duration, a performer hint and resolved pitches, and the whole struct rides inside every Element variant | confirmed |
| F052 | low | `music/chord_progression_builder.h:52` | Self-declared "throwaway-grade" std::function map of five fixed lambdas is the production path for progressionName | unverified |
| F053 | low | `music/chord_walker.h:229` | is_chord_tone ignores alteration, assumes a 7-degree scale, and infers a 7th from intervals.size() > 3 | confirmed |
| F055 | low | `music/composer.h:38` | Library header pulls <iostream> and emits unroutable std::cerr diagnostics from the compose path | confirmed |
| F058 | low | `music/composer.h:241` | Composer's public dispatch surface is dead or duplicated, and two-thirds of the plan/compose interface is never dispatched | confirmed |
| F059 | low | `music/composer.h:299` | Part/Section lookup-by-name loops are hand-written six times in Composer | confirmed |
| F060 | low | `music/composer.h:313` | Piece-level scaleName other than Major/Minor is silently collapsed to Major and inherited by every section without an override | confirmed |
| F071 | low | `music/composer.h:1012` | Self-declared 'for now' / throwaway code and design-note comments left in production headers | confirmed |
| F073 | low | `music/composer.h:1318` | Oversized functions and classes in headers: a 320-line compose_passage with nested lambdas and local structs, a 185-line realize_chord_parts_, a 620-line PitchedInstrument, an 800-line Partials | confirmed |
| F075 | low | `music/conductor.h:426` | ChordPerformer::perform_with_figure loops on elapsed < chordDurSeconds with no progress guard and indexes fig.elements without a count check | confirmed |
| F079 | low | `music/drift_voicing_profile_selector.h:72` | Hand-rolled Box-Muller with a literal pi instead of a Randomizer gaussian helper / std::numbers | unverified |
| F086 | low | `music/figure_transforms.h:560` | Dead expressions, silent no-op op, stale notes and mojibake comments scattered through the unit | confirmed |
| F087 | low | `music/figures.h:14` | figures.h has encoding mojibake (em-dashes replaced by '???') and the repo has no .editorconfig/.gitattributes/.clang-format to prevent it | confirmed |
| F089 | low | `music/figures.h:337` | Dead code and encoding damage in figures.h | confirmed |
| F092 | low | `music/figures.h:574` | Figure/MelodicFigure/ChordFigure polymorphism exists only to be recovered by dynamic_cast; it costs a unique_ptr per figure, virtual clone and a hand-written deep copy in Phrase | confirmed |
| F096 | low | `music/music_json.h:368` | from_json loaders are split between 'clear then fill' and 'append to whatever is there' with no rule | confirmed |
| F099 | low | `music/parse_util.h:115` | Dead branch with wrong error message in parse_chord_string; parse_duration treats any non-dot second char as x1.25 | confirmed |
| F102 | low | `music/parse_util.h:306` | C-style casts in music, tools and test harnesses | confirmed |
| F106 | low | `music/pitch_reader.h:61` | Dead comp-lane API families: chromatic passing tones, piece history queries, HarmonyTimeline::slice, Pitch::relative, Genre, RhythmicFigure, SimpleNote/DrumHit, drum perform, core/sequence.h | confirmed |
| F110 | low | `music/random_figure_builder.h:94` | No-op placeholder functions kept as "seams": post_clamp_ and ChordWalker::melody_aware_start | confirmed |
| F115 | low | `music/rng.h:11` | Ambient thread_local RNG dereferenced without a null check; public strategy dispatchers can run without a Scope installed | confirmed |
| F116 | low | `music/rng.h:11` | rng::next() is a thread_local service locator that dereferences a possibly-null pointer with no check | confirmed |
| F120 | low | `music/smooth_voicing_selector.h:208` | melody_penalty is unreachable (melodyPitch never set) and its magnitudes are not normalized against the [0,1] composite | confirmed |
| F122 | low | `music/style_table.h:15` | `using json = nlohmann::json;` at namespace-mforce scope in a header | disputed |
| F125 | low | `music/style_table.h:160` | parse_json repeats the transition-row parsing loop verbatim for transitions and overrides | confirmed |
| F127 | low | `music/templates.h:110` | FigureTemplate is a union-of-modes struct; PhraseTemplate/PassageTemplate pair a strategy string with 4 and 5 'only one populated' optional configs — hand-rolled tagged unions | confirmed |
| F128 | low | `music/templates.h:305` | Cadential 'held' arrival — a self-described 2026-04 workaround — is still the default PhraseTemplate behaviour | confirmed |
| F129 | low | `music/templates.h:470` | Strategy configuration is hard-wired into the core template structs: PassageTemplate is a union of every strategy's fields and PhraseTemplate carries four mutually exclusive optional configs | confirmed |
| F130 | low | `music/templates.h:732` | add_derived_motif re-implements find_motif inline and carries stale plan-step comments that contradict the code | confirmed |
| F131 | low | `music/templates_json.h:33` | parse_chord_progression re-implements from_json(ScaleChord) including the VoicingPin block, with a different spelling of the quality default | confirmed |
| F132 | low | `music/templates_json.h:95` | Ten enums hand-serialized as paired switch + if/else chains (~250 lines) when the vendored nlohmann 3.12 generates both directions from one table | confirmed |
| F136 | low | `music/templates_json.h:934` | Passage config structs are field-listed inline in both directions of PassageTemplate (~110 lines) instead of having their own serializers | confirmed |
| F140 | low | `render/instrument.h:62` | Instrument::render ignores ctx, re-tests bounds per sample, and implements a whole-timeline render behind a block-shaped MonoSource interface | unverified |
| F141 | low | `render/instrument.h:87` | Offline note buffers are copied into renderedNotes instead of moved, and every note is retained until render() — memory grows with total note-seconds | unverified |
| F147 | low | `render/instrument.h:268` | prepare_voice has no empty-pool guard while play_note does; a patch with "polyphony": 0 is a modulo-by-zero on the render-path loader | confirmed |
| F150 | low | `render/instrument.h:475` | Continuation reseed and offline capture both depend on nodesById, which the render-path loader never fills — determinism and capture silently no-op for mixer-path instruments | confirmed |
| F151 | low | `render/instrument.h:527` | Continuation branch silently ignores the note's startTime and its PitchCurve | confirmed |
| F154 | low | `render/instrument.h:624` | Per-note 8-second transient reserve plus a second full copy in add_rendered, and a per-sample render_chunk call that resizes on every sample | confirmed |
| F156 | low | `render/instrument.h:651` | Engine render path writes diagnostics straight to stderr, which the WIN32 UI build cannot display | confirmed |
| F157 | low | `render/limiter.h:23` | soft_clip's HARD clamp is unreachable, the mixer comment states the wrong ceiling, and the file shares its name with an unrelated filter/limiter.h | unverified |
| F158 | low | `render/perform_source.h:84` | PerformSource::frequency() recomputes exp2 on every read; every PerformOut consumer pays it per sample | unverified |
| F162 | low | `source/additive/formant.h:320` | RTTI cross-casts in the wiring path | unverified |
| F163 | low | `source/additive/full_additive_source.h:18` | Stale tombstone comments describe code that no longer exists (registry alias, configurator, endHold_ member, retired UI windows) | unverified |
| F168 | low | `source/additive/partials.h:198` | IFormant and IPartials duplicate ValueSource's prepare/next under prefixed names, requiring forwarding shims in every implementer | unverified |
| F171 | low | `source/additive/partials.h:410` | Dead DSP setters: Partials::set_ro/set_dt, three Partials::setup overloads, MultiSource::set_weight/get_weight, BW registry configurators | unverified |
| F173 | low | `source/additive/partials.h:459` | Dead state and dead interface surface: partialPO_ is sized but never read, BasicAdditiveSource has seven dead stores, IPartials::get_partial_value has no external caller | unverified |
| F180 | low | `source/bow_table_source.h:81` | Per-sample std::pow for an integer power, and runtime modulo in DelayLine's inner loop | unverified |
| F181 | low | `source/combined_source.h:17` | combined_source.h is a grab-bag of five unrelated nodes | unverified |
| F182 | low | `source/combined_source.h:59` | CombineOp ordinal/label/string mapping maintained in three places that 'must stay in step' | unverified |
| F183 | low | `source/combined_source.h:70` | Unchecked float-to-enum conversion for the operation setting | unverified |
| F188 | low | `source/delay_line_source.h:167` | Loop walk silently drops members past the cap and frees shared_ptrs under the audio lock | unverified |
| F190 | low | `source/fm_source.h:110` | Per-sample double virtual call (next() result discarded, current() re-read) and unused base-class phase bookkeeping | unverified |
| F192 | low | `source/ks_string.h:62` | KSString allocates its maximum configuration (~200 KB) per instance regardless of numCombs | unverified |
| F194 | low | `source/ks_string.h:202` | prepare() zero-fills the maximum-capacity state, not the used portion, inside the audio-critical section | unverified |
| F196 | low | `source/ks_string.h:271` | Inner `float d` shadows the damper value `d` in next() (x2) | unverified |
| F200 | low | `source/ks_string.h:457` | No regression tests for KSString, AllpassResonator, Mesh2D, BowTable or HybridKS | unverified |
| F203 | low | `source/layered_red_noise_source.h:80` | LayeredRedNoise rebuilds its layers lazily in prepare() though everything needed is known at construction/edit time | unverified |
| F204 | low | `source/mesh2d_source.h:292` | tick0 and tick1 are mirror-image copies differing only in which plane set is read vs written | unverified |
| F206 | low | `source/multiplex_source.h:84` | templateJson_/baseSeed_ are write-only state and the header comment describes an access path that does not exist | unverified |
| F210 | low | `source/noise_sources.h:169` | VelvetNoiseSource::recompute_gap ignores the density pin and hardcodes the descriptor default | unverified |
| F214 | low | `source/phased_value_source.h:108` | Unused variable, unused member, and three unused includes in phased_value_source.h; shaper_source.h relies on a transitive <cmath> | unverified |
| F216 | low | `source/pulse_source.h:54` | Dead assignments, unused include, conversational TODO comment and never-null checks left in the unit | unverified |
| F217 | low | `source/repeating_source.h:80` | No engine test exercises any node in this unit | unverified |
| F224 | low | `source/segment_source.h:164` | SegmentSource null-checks only width_ in next() while set_param accepts nullptr for every pin; constructor initialiser order differs from declaration order | unverified |
| F225 | low | `source/sine_source.h:15` | Hand-rolled constants and bit loops where C++20 <numbers>/<bit>/<cmath> provide the idiom | unverified |
| F226 | low | `source/sine_source.h:15` | Pi written out as a literal in ~25 places with differing precision | unverified |
| F227 | low | `source/triangle_source.h:45` | `asymmetric` setting keeps an earlier per-leg warp alive as a toggle | unverified |
| F230 | low | `source/wave_evolution.h:24` | evolve()/shape_excitation() take std::vector<float>& — exposing the container lets an evolution reallocate the ring buffer in the render path; pi is a hand-typed literal | unverified |
| F232 | low | `source/wave_evolution.h:56` | adjust() returns a frequency correction nobody consumes; dead members in Target and EKS | unverified |
| F234 | low | `source/wave_evolution.h:466` | Per-sample std::pow in the bow friction table and up to ~570 modulo divisions per sample in PluckEvolution | unverified |
| F245 | low | `util/fft.h:11` | util nits: C-cast and hand-typed pi in fft.h; dead counter and always-true branch in signal_stats.h; raw pointer+length instead of std::span | unverified |
| F249 | low | `engine/src/chord.cpp:24` | ChordDef table defines the same quality twice (dim7 / -7, m7b5 / h7) and the by-name lookup is ambiguous | unverified |
| F251 | low | `engine/src/full_additive_source.cpp:10` | formantWeight_ is left null in the constructor while formantFloor_ is defaulted, forcing null checks and a nullptr get_param result | unverified |
| F253 | low | `engine/src/hybrid_ks_source.cpp:107` | HybridKSSource carries dead debug code, an unused RNG, duplicate setters, and a loader special case | unverified |
| F258 | low | `engine/src/music.cpp:160` | Out-of-line static constants and manual init flags where constexpr inline variables and function-local statics would remove SIOF risk and the non-thread-safe lazy init | unverified |
| F259 | low | `engine/src/patch_loader.cpp:58` | WaveSourceMono and ValueSourceMono are identical; add_mono's dynamic_pointer_cast chooses between two copies of the same class | unverified |
| F261 | low | `engine/src/patch_loader.cpp:89` | Tap binding is threaded through a thread_local global with an RAII scope rather than a build context; several process-global mutable statics in the loader | unverified |
| F271 | low | `engine/src/patch_loader.cpp:617` | Hand-rolled `static bool` lazy-init guards instead of C++11 magic statics / std::call_once (four sites) | unverified |
| F281 | low | `engine/src/patch_loader.cpp:1585` | load_patch_file (330 lines) embeds score scheduling and then a second pass that re-implements the same 'time defaults to prevEnd' rule to compute duration | unverified |
| F286 | low | `engine/src/source_registrations.cpp:64` | ~75 factory lambdas are one of four shapes and could be a table; source_registry.h pulls the full nlohmann/json.hpp into a core header | unverified |
| F294 | low | `research/additive_perf/simd_proto.cpp:85` | Prototype hygiene: unused 'base' parameter with a stale comment, a scalar tail that never runs and is not the faithful control it claims, and AVX2/FMA intrinsics with no CPU check | unverified |
| F295 | low | `durn_converter/CMakeLists.txt:4` | Target pins C++17 under a C++20 project and adds an engine include directory nothing uses | unverified |
| F304 | low | `durn_converter/main.cpp:569` | Grouped nits: unsafe ::tolower, redundant whole-file read, unchecked ofstream, exit code ignores failures, raw FILE*, mixed include guards, uninitialized struct members | unverified |
| F305 | low | `durn_converter/parsers/abc_parser.h:302` | ABC inline fields and chord brackets are not parsed: `[K:G]` emits a spurious G note, `[CEG]` emits three sequential notes | unverified |
| F308 | low | `durn_converter/parsers/midi_parser.h:505` | Dead code and stale comments across the parsers | unverified |
| F315 | low | `engine_tests/main.cpp:796` | hold_patch_json builds JSON with snprintf into a fixed char[1400]; truncation would be silent and surface as a parse exception | unverified |
| F316 | low | `engine_tests/main.cpp:824` | Signal-measurement helpers (RMS, peak, zero-crossing f0) are re-written inline per test with inconsistent conventions | unverified |
| F319 | low | `engine_tests/main.cpp:1268` | Test writes a scratch file into renders/scratch/, a directory CLAUDE.md reserves for manual material, and creates it in whatever CWD it runs from | unverified |
| F320 | low | `engine_tests/main.cpp:1551` | Statistical assertions are pinned to the exact Randomizer draw order; any refactor that changes draw sequence flips them without a behavioral regression | unverified |
| F322 | low | `mforce_cli/explore.cpp:158` | Relies on transitive includes for <cctype> and <stdexcept> | unverified |
| F324 | low | `mforce_cli/explore.cpp:324` | goto-based loop control in explore and unchecked atof/duplicated WAV writer across the stk_ref harnesses | unverified |
| F326 | low | `mforce_cli/explore.cpp:507` | --explore-filter parses the next argv as float before validating the flag, so unknown flags die with 'ERROR: stof' | unverified |
| F329 | low | `mforce_cli/main.cpp:141` | build_descending_phrase: unused stepDown parameter, misleading name, redundant first-iteration unroll | unverified |
| F330 | low | `mforce_cli/main.cpp:592` | run_compose mixes all instruments at the alphabetically-first patch's sampleRate with no equality check | unverified |
| F333 | low | `mforce_cli/main.cpp:968` | Stale/dead items: --test-ornaments loads a patch that does not exist; write-only nodeMap; local with static-prefix name; per-sample cos/sin for constant pan | unverified |
| F334 | low | `mforce_cli/main.cpp:1217` | Eleven copy-pasted dispatch ifs; unknown --flags fall through to run_patch and report 'Patch file not found: --flag' | unverified |
| F336 | low | `mforce_ui/CMakeLists.txt:9` | imgui_demo.cpp is compiled into mforce_ui but ShowDemoWindow is never called | unverified |
| F341 | low | `mforce_ui/main.cpp:152` | UI classifies node types by substring match on the type name instead of the registry's SourceCategory | unverified |
| F343 | low | `mforce_ui/main.cpp:611` | Instantiate-to-introspect: every node creation constructs its DSP object twice, and the paramMap converter constructs one per entry, because descriptors are only reachable through an instance | unverified |
| F344 | low | `mforce_ui/main.cpp:636` | Link direction is an enforced invariant (start = output, end = input) that is undocumented and re-checked in both orientations at three sites | unverified |
| F348 | low | `mforce_ui/main.cpp:910` | Dead code in this range: dsp_rewire_link, GraphNode::performField, load's nodeMap, and the waveOut-era <mmsystem.h> include | unverified |
| F358 | low | `mforce_ui/main.cpp:2366` | Recents/settings are persisted CWD-relative although documented as 'next to the exe', and recent-path dedupe is case-sensitive on a case-insensitive filesystem | unverified |
| F360 | low | `mforce_ui/main.cpp:2547` | Six copies of the same input-pin -> source-output link walk | unverified |
| F361 | low | `mforce_ui/main.cpp:2547` | UI graph-walk helpers triplicated; group-ancestor walk and wormhole ghost drawing copied inline | unverified |
| F362 | low | `mforce_ui/main.cpp:2759` | rename_node and rename_group duplicate the identifier validation rules | unverified |
| F366 | low | `mforce_ui/main.cpp:3125` | Sample-rate and pi literals duplicated where named constants exist; two names for one sample rate | unverified |
| F368 | low | `mforce_ui/main.cpp:3477` | Two inconsistent lists of which types are Partials/Formant implementors | unverified |
| F370 | low | `mforce_ui/main.cpp:3541` | Stale banner describes the retired waveOut design; forward declarations scattered; dead default drum-map path | unverified |
| F371 | low | `mforce_ui/main.cpp:3541` | Stale section header still documents the removed waveOut poll design ('no callbacks, no threading issues') | unverified |
| F372 | low | `mforce_ui/main.cpp:3755` | voice_schedule_unlocked takes 10 positional parameters (6 defaulted) for data the engine already packages as StreamingVoice | unverified |
| F385 | low | `mforce_ui/main.cpp:5095` | Mod-wheel / channel-pressure MIDI messages can trigger a full instrument rebuild on the UI thread | unverified |
| F386 | low | `mforce_ui/main.cpp:5139` | apply_score_defaults has duplicated assignments and a leftover (void)0; from an incomplete edit | unverified |
| F387 | low | `mforce_ui/main.cpp:5333` | pick_folder_dialog uses raw COM pointers and CP_ACP conversions; non-ANSI folder names are mangled and the dialog has no owner window | unverified |
| F389 | low | `mforce_ui/main.cpp:5941` | Cluster of small duplicated helpers and one dead wrapper in the shape/audition/curve sections | unverified |
| F392 | low | `mforce_ui/main.cpp:6543` | Shaper overlay instantiates a ShaperSource per frame and calls next() purely for its side effect of loading smoothness | unverified |
| F397 | low | `mforce_ui/main.cpp:7556` | root_perform's viaCurve flag is never cleared on a dead-end branch, so a direct perform link can be reported as 'curve' | unverified |
| F400 | low | `mforce_ui/main.cpp:7941` | Keyboard highlight reads Voice.active / midiNote from the UI thread without the audio mutex (formal data race) | unverified |
| F407 | low | `mforce_ui/main.cpp:8500` | save_wav_dialog is a copy of text_save_dialog | unverified |
| F408 | low | `mforce_ui/main.cpp:8550` | Dead code: transport_label, transport_label_inline, and the unused figurePrefix parameter | unverified |
| F409 | low | `mforce_ui/main.cpp:8550` | Dead UI helpers and leftover statements | unverified |
| F410 | low | `mforce_ui/main.cpp:8623` | Duplicated literal tables inside draw_transport_panel: note names (x2) and the two-line text-box height (x2) | unverified |
| F414 | low | `mforce_ui/main.cpp:8947` | Per-pin per-frame std::string allocation for a prefix test | unverified |
| F415 | low | `mforce_ui/main.cpp:9008` | Node face width 150.0f and the right-align formula are hardcoded in three places | unverified |
| F416 | low | `mforce_ui/main.cpp:9055` | draw_group_node computes the group boundary twice per group per frame (group_output_node repeats group_boundary and may topo_sort) | unverified |
| F418 | low | `mforce_ui/main.cpp:9316` | substr-based prefix/suffix tests allocate per pin per frame where C++20 starts_with/ends_with are free | unverified |
| F420 | low | `mforce_ui/main.cpp:9699` | Per-frame heap work in previews and mapping_badge while a node is selected | unverified |
| F423 | low | `mforce_ui/main.cpp:10279` | find_node_by_id exists but the range re-rolls the id loop seven times and queries the imnodes selection three times in one menu | unverified |
| F424 | low | `mforce_ui/main.cpp:10688` | Dead leftovers in convert_node_to_patch_graph and clipboard_copy | unverified |
| F425 | low | `mforce_ui/main.cpp:10742` | s_menuSourceAction is a global std::function side-channel set by whichever popup draws; menu_source throws if a caller forgets | unverified |
| F434 | low | `mforce_ui/main.cpp:11874` | Spectrum sample rate hard-coded to 48000 instead of AUDIO_SAMPLE_RATE | unverified |
| F436 | low | `mforce_ui/main.cpp:12168` | Partials scrubber undoes rolloff with pow() to recover weights it discarded ten lines earlier | unverified |
| F438 | low | `mforce_ui/main.cpp:12469` | Assorted nits in the range: unused local, misnamed local, comment drift, triplicated basename extraction, no-op dynamic_cast, shared_ptr copied per call, triple map lookup | unverified |
| F441 | low | `mforce_ui/main.cpp:12649` | Waveform-window keyboard shortcuts fire while a text input in the same window is active | unverified |
| F442 | low | `mforce_ui/main.cpp:12801` | Crash logger duplicates DbgHelp initialisation and the log header between crash_log_stack and seh_crash_filter | unverified |
| F449 | low | `mforce_ui/main.cpp:13939` | Drill camera panStack goes stale after a non-prefix path change (wormhole jump), and the deeper branch fills then overwrites the same slot | unverified |
| F455 | low | `mforce_ui/main.cpp:14365` | Gold-wire 'unwire dynamic pin' block duplicated between IsLinkDestroyed and the Delete-key handler | unverified |
| F456 | low | `mforce_ui/main.cpp:14400` | Right-click handler has an empty if-branch with collapsed indentation and a loop whose body does not depend on the loop variable | unverified |
| F459 | low | `mforce_ui/main.cpp:14580` | Three different hand-rolled basename extractions of s_currentFilePath in the same function | unverified |
| F463 | low | `ppl_to_json/CMakeLists.txt:13` | Two tools override the project's C++20 standard down to C++17 for no stated reason | unverified |
| F464 | low | `ppl_to_json/main.cpp:74` | Twelve copies of the ostringstream-then-throw error pattern; a [[noreturn]] helper collapses ~45 lines | unverified |
| F465 | low | `ppl_to_json/main.cpp:270` | Input validation gaps: stoi accepts trailing garbage, filesystem exceptions in main/convert_dir are uncaught, default mode is cwd-relative | unverified |
| F466 | low | `ppl_to_json/main.cpp:288` | Standard functions used without their headers (compiles only via MSVC transitive includes) | unverified |
| F469 | low | `stk_ref/CMakeLists.txt:57` | foreach applying /O2 and _CRT_SECURE_NO_WARNINGS runs before four of the eight targets exist; STK sources are recompiled once per executable | unverified |
| F471 | low | `stk_ref/mesh2d_ref.cpp:171` | MeshBig is ~300 KB of inline arrays constructed on the stack; fine under MSVC's 1 MB default but one NMAX bump from overflow | unverified |
| F473 | low | `test_figures/main.cpp:773` | ~300 lines pin the behavior of strategies Matt flags as probably-obsolete, including one assertion already weakened to pass | unverified |
| F474 | low | `wav_check.cpp:13` | wav_check.cpp is an orphan (not built by any CMakeLists, untouched since the 2026-03-29 port) and its header handling is wrong for anything but a canonical 44-byte 16-bit file | unverified |

---

## 5. Duplication map

Each line names one copy-pair or family; both locations and the recommended single home are in the raw record. 98 confirmed or disputed duplication findings.

- **F065** (high) `music/composer.h:524` — Two sources of truth for section harmony; the melody chord-tone path hand-scans Section::chordProgression and resolves at the static section scale, while the chord-part p [unverified]
- **F121** (high) `music/structure.h:215` — Harmony context is stored twice per Section and the two copies are read by different consumers with different scale resolution [unverified]
- **F160** (high) `source/additive/basic_additive_source.h:16` — Three additive sources coexist; BasicAdditiveSource is fully subsumed by AdditiveSource+FullPartials and AdditiveSource2 is a half-integrated, buggy port that one baselin [unverified]
- **F235** (high) `source/wave_evolution.h:603` — Thirteen *EvolutionSource holder classes are ~580 lines of copy-pasted boilerplate; wave_evolution.h is a 1552-line god-file that should be split [unverified]
- **F256** (high) `engine/src/mixer.cpp:36` — StereoMixer::render is hand-duplicated per-sample in mforce_ui and the two copies have already diverged (engine unity-at-center pan vs UI's old -3 dB-center law)
- **F276** (high) `engine/src/patch_loader.cpp:1194` — The legacy paramMap exists as three parallel implementations: engine build_bindings, UI JSON converter, and UI stash editor/dialog [unverified]
- **F277** (high) `engine/src/patch_loader.cpp:1514` — Instrument assembly is copy-pasted between load_patch_file and load_instrument_patch_json and has already drifted: nodesById is only set on the JSON path, so per-note res [unverified]
- **F278** (high) `engine/src/patch_loader.cpp:1514` — Patch loader has twin entry points and three copies of the JSON ref scanner [unverified]
- **F298** (high) `durn_converter/main.cpp:59` — No common intermediate representation: four parsers each own a FigureUnit, a ScaleMap, a tie-merger and a letter table; their conversion layers are dead, discarded or los [unverified]
- **F328** (high) `mforce_cli/main.cpp:116` — beats→frames→render→stereo→peak/rms pipeline hand-rolled 6-7 times in main.cpp, again in explore.cpp and the UI, while engine signal_stats.h already provides the stats [unverified]
- **F354** (high) `mforce_ui/main.cpp:1368` — The patch JSON format has four hand-maintained codecs (engine loader, UI loader, two UI savers) plus a third clipboard schema; jsonExtras carry-through is the symptom [unverified]
- **F377** (high) `mforce_ui/main.cpp:3963` — UI re-implements StereoMixer's equal-power pan twice and has already drifted from mixer.cpp (missing unity-center normalisation)
- **F403** (high) `mforce_ui/main.cpp:8098` — Chords and Drums keep a second render pipeline beside generate_unified, duplicating post-processing and skipping the spectrum/evo updates [unverified]
- **F432** (high) `mforce_ui/main.cpp:11507` — Four divergent implementations of 'copy a node's state' (Duplicate, clip_instantiate, replace_node_with, saver/loader fragment) [unverified]
- **F446** (high) `mforce_ui/main.cpp:13284` — --dump-playback and --dump-stream hand-copy the audio callback's mixing arithmetic (and use a different voice entry point than the live path) while documenting themselves [unverified]
- **F453** (high) `mforce_ui/main.cpp:14253` — Ctrl+C / Ctrl+V are bound twice to two separate clipboard subsystems; a single Ctrl+V pastes the selection twice
- **F454** (high) `mforce_ui/main.cpp:14253` — Two independent node clipboards, and both key handlers fire on the same Ctrl+C / Ctrl+V
- **F008** (medium) `core/curve_node.h:62` — CurveNode::map_expr re-implements the bracket search and Linear/LogX/LogLog interpolation that Curve::eval_core (curve.h:45-67) was introduced to share
- **F023** (medium) `core/envelope_presets.h:29` — Five preset envelope classes hand-mirror the same descriptor/set_setting/get_setting boilerplate and each re-stores the sample rate the base already holds
- **F024** (medium) `core/envelope_presets.h:29` — Six envelope preset classes repeat ~70 lines of per-stage Curve/Power settings dispatch; seven identical factory lambdas
- **F025** (medium) `core/envelope_presets.h:29` — Six copy-pasted envelope preset classes (backlog 36b) plus three envelope authoring dialects (36a) still present
- **F036** (medium) `filter/filters.h:54` — Butterworth LP and HP sections and filters are copy-pasted pairs differing only in section type [unverified]
- **F038** (medium) `filter/filters.h:116` — BWLowpassFilter/BWHighpassFilter and BWLPSection/BWHPSection are copy-pasted pairs; FIR/IIR use heap vectors with shift loops for fixed 2nd-order sections [unverified]
- **F040** (medium) `filter/filters.h:194` — BWLowpassFilter, BWHighpassFilter (and BWBandpassFilter) are copy-pasted; BWLPSection/BWHPSection likewise differ only in update() [unverified]
- **F067** (medium) `music/composer.h:736` — RhythmPatternRealizationStrategy is registered but never resolved; its per-bar pattern walk is reimplemented inline in realize_chord_parts_
- **F072** (medium) `music/composer.h:1215` — Literal-figure realization and the contour-motif transform chain are duplicated inside composer.h
- **F078** (medium) `music/drift_voicing_profile_selector.h:40` — inversionProfiles/spreadProfiles config parsing is copy-pasted between Drift and Random selectors; VoicingProfile JSON parsing is duplicated between Scripted and template [unverified]
- **F090** (medium) `music/figures.h:399` — Binary duration alphabet, inverse-distance weighting and roulette selection are implemented three times (plus a fourth roulette in elaborate)
- **F095** (medium) `music/music_json.h:19` — Eleven hand-written two-direction enum<->string tables
- **F100** (medium) `music/parse_util.h:174` — Note-name parsing exists in two forms inside parse_util.h and in four more places across dun_parser and durn_converter
- **F101** (medium) `music/parse_util.h:183` — Note-letter parsing duplicated between parse_note_input and parse_passage; DunToken->FigureUnit copy repeated three times in dun_to_piece; two identical drum structs
- **F105** (medium) `music/piece_utils.h:67` — Cursor walk (leadStep then units) and template/seed lookup are copied four and two times across piece_utils [unverified]
- **F107** (medium) `music/pitch_walker.h:59` — Two scale walkers (PitchReader::step and step_note) with divergent descending semantics and a copy-pasted degree search
- **F135** (medium) `music/templates_json.h:799` — from_json(PeriodSpec) carries a 42-line copy of from_json(PhraseTemplate) that has already drifted and silently drops strategy configs
- **F153** (medium) `render/instrument.h:620` — Ring-out / cap-fade / containment block is copy-pasted between play_note (classic) and finish_line, and mirrored a third time in the UI audio callback
- **F155** (medium) `render/instrument.h:629` — Ring-out + cap-fade + containment loop is triplicated with diverging constants (play_note, finish_line, UI audio callback)
- **F166** (medium) `source/additive/partials.h:175` — Descriptor spans re-declared in subclasses (macro splice in partials.h; frequency/amplitude/phase in 8 wave sources) [unverified]
- **F174** (medium) `source/additive/partials.h:499` — Motion and shimmer layers are copy-pasted: identical prepare blocks, identical advance loops, parallel member triplets [unverified]
- **F177** (medium) `source/additive/partials.h:1087` — Base Partials setting descriptors are hand-copied into all three subclass lists, with only the newer entries spliced by macro [unverified]
- **F184** (medium) `source/combined_source.h:101` — Three hand-rolled linear crossfades; CrossfadeSource is the two-stage case of PhasedValueSource [unverified]
- **F198** (medium) `source/ks_string.h:426` — Waveguide primitives (tuning allpass, DC blocker, phase-delay solvers, KS averaging, fill_table) re-typed per node [unverified]
- **F199** (medium) `source/ks_string.h:438` — Four DSP primitives are copy-pasted across KSString, AllpassResonator, BowTable and wave_evolution [unverified]
- **F221** (medium) `source/saw_source.h:18` — PolyBLEP residual is duplicated between SawSource and PulseSource [unverified]
- **F222** (medium) `source/saw_source.h:18` — Oscillator / filter math duplicated across nodes: BLEP, Friedlander friction, resonance and RBJ biquad coefficients, random-walk variation [unverified]
- **F236** (medium) `source/wave_evolution.h:603` — Twelve WaveEvolution holder wrappers repeat the same ValueSource shell [unverified]
- **F242** (medium) `source/white_noise_source.h:40` — Every class hand-maintains five parallel pin lists plus dead typed accessors; the drift bugs above are the measured cost [unverified]
- **F243** (medium) `source/white_noise_source.h:98` — Sign/boost/continuity draw block is copied verbatim between WhiteNoiseSource and RedNoiseSource [unverified]
- **F244** (medium) `source/white_noise_source.h:98` — Noise family copies: white/red shaping block, Wander1/Wander3 pin plumbing [unverified]
- **F262** (medium) `engine/src/patch_loader.cpp:98` — The shared-source auto-RefSource rule is implemented twice (engine loader and UI) and the JSON ref-edge scanner four times inside the loader [unverified]
- **F264** (medium) `engine/src/patch_loader.cpp:190` — Four hand-rolled recursive JSON edge scanners with diverging semantics (ref / inputs / tap coverage differs per copy) [unverified]
- **F265** (medium) `engine/src/patch_loader.cpp:190` — Recursive JSON ref/tap walker copied three times and the voice-pool build copied twice ('see render-path twin above') [unverified]
- **F273** (medium) `engine/src/patch_loader.cpp:680` — Envelope stage-form parser and the RefSource first-consumer-advances rule are each duplicated in mforce_ui/main.cpp; the loader's own comment says only the preset form wa [unverified]
- **F274** (medium) `engine/src/patch_loader.cpp:838` — Loader special-case branches shadow registry configurators and re-apply what wire_params_generic already did [unverified]
- **F280** (medium) `engine/src/patch_loader.cpp:1534` — Voice-pool construction and instrument-block parsing are duplicated between load_patch_file and load_instrument_patch_json, and have already diverged (nodesById only popu [unverified]
- **F292** (medium) `engine/src/wavetable_source.cpp:44` — First-sample init and the tuning allpass are duplicated across compute_raw/compute_interpolated (and the allpass a third time in EKS); the interpolated copy is incomplete [unverified]
- **F302** (medium) `durn_converter/main.cpp:391` — midi_to_durn copy-pastes a 14-line accidental block into both branches and re-implements ScaleMap's nearest-degree search [unverified]
- **F306** (medium) `durn_converter/parsers/kern_parser.h:40` — durn_converter's four format parsers each carry a private ScaleMap/FigureUnit/trim implementation [unverified]
- **F313** (medium) `engine_tests/main.cpp:9` — Two divergent hand-rolled test harnesses with different failure semantics and output [unverified]
- **F327** (medium) `mforce_cli/main.cpp:103` — CLI render epilogue (mono->stereo, peak/rms, 'Wrote:' report) is copy-pasted seven times; UI chord render duplicates CLI run_chords [unverified]
- **F353** (medium) `mforce_ui/main.cpp:1258` — save_file_dialog / open_file_dialog are verbatim specializations of text_save_dialog / text_open_dialog — four copies of the OPENFILENAMEA setup [unverified]
- **F355** (medium) `mforce_ui/main.cpp:1368` — Envelope stage-list parsing exists in the UI and the engine loader but not in envelope_json.h; the UI serializer has no engine twin at all [unverified]
- **F365** (medium) `mforce_ui/main.cpp:3024` — ~130 lines of per-node emission copy-pasted between serialize_patch_graph and save_node_graph [unverified]
- **F380** (medium) `mforce_ui/main.cpp:4484` — Three UI mirrors of CurveNode::map, two of which already disagree [unverified]
- **F382** (medium) `mforce_ui/main.cpp:4716` — UI collect_envelopes re-walks the voice graph with dynamic_cast on every note-on (under the audio lock) although the loader already collects allEnvelopes per voice [unverified]
- **F393** (medium) `mforce_ui/main.cpp:7054` — curve_eval re-implements CurveNode::map (LogX) in the UI while the sibling editor already evaluates through the engine [unverified]
- **F394** (medium) `mforce_ui/main.cpp:7142` — Three copies of the breakpoint-table + log-spaced PlotLines editor idiom [unverified]
- **F402** (medium) `mforce_ui/main.cpp:8037` — Key rectangle geometry is computed twice: once in the draw loops and again, verbatim, in the hit-test loops [unverified]
- **F419** (medium) `mforce_ui/main.cpp:9337` — Repeated blocks inside draw_properties_panel: pin-edit apply, setting apply, centered table header, preview plotting [unverified]
- **F426** (medium) `mforce_ui/main.cpp:10787` — source_family_menus/experimental_family_menu are a hand-maintained second registry of ~75 (label, type) pairs [unverified]
- **F427** (medium) `mforce_ui/main.cpp:10974` — show_create_menu creates Output in two places with different side effects; special-node items skip the group/dirty handling the generic path does [unverified]
- **F435** (medium) `mforce_ui/main.cpp:11922` — Log-frequency axis, freq<->x mapping, hover readout box, pop-out button and early-return blocks are duplicated across spectrum, partials and formant drawers [unverified]
- **F439** (medium) `mforce_ui/main.cpp:12472` — Zoom/Fit/scrollbar/wheel/ctrl-wheel handling is copied between draw_waveform_window and draw_wave_popouts [unverified]
- **F444** (medium) `mforce_ui/main.cpp:12978` — Ten headless modes repeat the identical context/registry prologue and try/catch-print-return epilogue; mono-to-stereo widening copied twice [unverified]
- **F445** (medium) `mforce_ui/main.cpp:13054` — --gencheck re-implements the engine's v2 score parsing instead of calling it [unverified]
- **F450** (medium) `mforce_ui/main.cpp:13997` — 'Visible representative at this drill level' ancestor walk is written four times (one copy dead) and the wormhole ghost overlay is written twice [unverified]
- **F462** (medium) `ppl_to_json/CMakeLists.txt:4` — Tools hardcode engine include paths (one of them unused) instead of consuming targets; nlohmann/json has no target of its own [unverified]
- **F468** (medium) `stamp_test/main.cpp:42` — stamp_test --deps re-implements build_stamp.h's newest_from_tlogs dependency walk line for line; header comment is stale (claims no CMake target, C++17) [unverified]
- **F470** (medium) `stk_ref/bowed_ref.cpp:31` — write_wav16_mono is copied verbatim into all eight stk_ref programs [unverified]
- **F472** (medium) `test_figures/main.cpp:73` — PieceTemplate scaffold copied six times, three identical Result early-return ladders, eight identical integ_* wrappers, and a local total_duration() duplicating MelodicFi [unverified]
- **F059** (low) `music/composer.h:299` — Part/Section lookup-by-name loops are hand-written six times in Composer
- **F125** (low) `music/style_table.h:160` — parse_json repeats the transition-row parsing loop verbatim for transitions and overrides
- **F131** (low) `music/templates_json.h:33` — parse_chord_progression re-implements from_json(ScaleChord) including the VoicingPin block, with a different spelling of the quality default
- **F132** (low) `music/templates_json.h:95` — Ten enums hand-serialized as paired switch + if/else chains (~250 lines) when the vendored nlohmann 3.12 generates both directions from one table
- **F136** (low) `music/templates_json.h:934` — Passage config structs are field-listed inline in both directions of PassageTemplate (~110 lines) instead of having their own serializers
- **F182** (low) `source/combined_source.h:59` — CombineOp ordinal/label/string mapping maintained in three places that 'must stay in step' [unverified]
- **F204** (low) `source/mesh2d_source.h:292` — tick0 and tick1 are mirror-image copies differing only in which plane set is read vs written [unverified]
- **F249** (low) `engine/src/chord.cpp:24` — ChordDef table defines the same quality twice (dim7 / -7, m7b5 / h7) and the by-name lookup is ambiguous [unverified]
- **F259** (low) `engine/src/patch_loader.cpp:58` — WaveSourceMono and ValueSourceMono are identical; add_mono's dynamic_pointer_cast chooses between two copies of the same class [unverified]
- **F360** (low) `mforce_ui/main.cpp:2547` — Six copies of the same input-pin -> source-output link walk [unverified]
- **F361** (low) `mforce_ui/main.cpp:2547` — UI graph-walk helpers triplicated; group-ancestor walk and wormhole ghost drawing copied inline [unverified]
- **F362** (low) `mforce_ui/main.cpp:2759` — rename_node and rename_group duplicate the identifier validation rules [unverified]
- **F368** (low) `mforce_ui/main.cpp:3477` — Two inconsistent lists of which types are Partials/Formant implementors [unverified]
- **F389** (low) `mforce_ui/main.cpp:5941` — Cluster of small duplicated helpers and one dead wrapper in the shape/audition/curve sections [unverified]
- **F407** (low) `mforce_ui/main.cpp:8500` — save_wav_dialog is a copy of text_save_dialog [unverified]
- **F410** (low) `mforce_ui/main.cpp:8623` — Duplicated literal tables inside draw_transport_panel: note names (x2) and the two-line text-box height (x2) [unverified]
- **F442** (low) `mforce_ui/main.cpp:12801` — Crash logger duplicates DbgHelp initialisation and the log header between crash_log_stack and seh_crash_filter [unverified]
- **F455** (low) `mforce_ui/main.cpp:14365` — Gold-wire 'unwire dynamic pin' block duplicated between IsLinkDestroyed and the Delete-key handler [unverified]
- **F459** (low) `mforce_ui/main.cpp:14580` — Three different hand-rolled basename extractions of s_currentFilePath in the same function [unverified]

---

## 6. God classes and oversized functions

19 findings. Line counts are the reviewers' measurements.

- **F142** (high) `render/instrument.h:99` — PitchedInstrument is a 620-line all-public header struct carrying ~10 unrelated responsibilities, with trailing-underscore 'private' state that callers reach into [unverified]
- **F169** (high) `source/additive/partials.h:266` — Partials is an 800-line class with ~85 data members and seven distinct responsibilities [unverified]
- **F269** (high) `engine/src/patch_loader.cpp:605` — build_graph is a 460-line if/else type switch that pre-empts the registry; 15 registered types never reach their registry factory or configurator
- **F339** (high) `mforce_ui/main.cpp:1` — mforce_ui/main.cpp is a 14,658-line single translation unit with 349 file-scope statics; every UI hack in this census lives in it
- **F357** (high) `mforce_ui/main.cpp:1561` — load_graph_from_path is a ~800-line, nine-phase god function with dead state and an embedded layout algorithm [unverified]
- **F391** (high) `mforce_ui/main.cpp:6100` — draw_shape_editor is a ~930-line function with the Curve/Segment/Shaper client switch repeated more than a dozen times [unverified]
- **F417** (high) `mforce_ui/main.cpp:9227` — draw_properties_panel is a 1,035-line function dispatching on typeName strings with three nested lambdas and function-local statics [unverified]
- **F443** (high) `mforce_ui/main.cpp:12948` — main() is a 1,711-line function: 12 inline headless CLI modes, GLFW/ImGui/audio/MIDI init, dock layout, menu bar and the whole frame loop in one body inside a zero-indent [unverified]
- **F448** (high) `mforce_ui/main.cpp:13848` — The entire Node Editor window (~675 lines) is inline in main()'s frame loop while every sibling panel is a draw_* function [unverified]
- **F017** (medium) `core/envelope.h:16` — Envelope is a ~560-line accretion of five orthogonal concerns with ~30 state fields and three timing semantics interleaved in prepare()
- **F069** (medium) `music/composer.h:902` — composer.h hosts ~1180 lines (57% of the file) of eight concrete strategies' out-of-line bodies under a dependency rationale that is no longer true
- **F143** (medium) `render/instrument.h:99` — PitchedInstrument is a ~620-line all-public struct mixing voice-pool accounting, offline rendering, held-line state machine, capture, live-continuation and diagnostics [unverified]
- **F191** (medium) `source/ks_string.h:56` — KSString bundles four mechanisms, 21 settings and five pins in one 636-line class [unverified]
- **F314** (medium) `engine_tests/main.cpp:144` — engine_tests/main.cpp is a 1930-line append-only single TU: 20 mid-file #includes, one header included twice, headers used before they are included, arbitrary run order [unverified]
- **F342** (medium) `mforce_ui/main.cpp:245` — GraphNode is a tagged-union-by-convention god struct: every node carries every node type's fields, dispatch is typeName string if-chains, and the constructor reads global [unverified]
- **F363** (medium) `mforce_ui/main.cpp:2806` — serialize_patch_graph is 460 lines with eight phases and a side effect on the model [unverified]
- **F364** (medium) `mforce_ui/main.cpp:2806` — 14,658-line single translation unit with 349 file-scope statics and three parallel node serializers plus two clipboard systems [unverified]
- **F073** (low) `music/composer.h:1318` — Oversized functions and classes in headers: a 320-line compose_passage with nested lambdas and local structs, a 185-line realize_chord_parts_, a 620-line PitchedInstrumen
- **F127** (low) `music/templates.h:110` — FigureTemplate is a union-of-modes struct; PhraseTemplate/PassageTemplate pair a strategy string with 4 and 5 'only one populated' optional configs — hand-rolled tagged u

---

## 7. Workarounds and hacks still live

26 findings; the hacks-census reviewer also cross-checked docs/Parked.txt and the backlogs.

- **F062** (high) `music/composer.h:404` — compose(Piece&, const PieceTemplate&) mutates the template through const_cast at three sites, plus a const_cast<Section*> on an object reachable non-const
- **F172** (high) `source/additive/partials.h:442` — Expand rule mutates the source arrays in place and restores from an 'orig' snapshot, so config changes after the first prepare are silently discarded; ExplicitPartials ad [unverified]
- **F272** (high) `engine/src/patch_loader.cpp:656` — build_graph's special-case chain re-implements wiring that the types' own descriptors already provide, bypasses reg.create so default seeds and constructor defaults are t
- **F467** (high) `stamp_test/CMakeLists.txt:1` — The UI relink-lock is handled by a runtime stamp guard, a dedicated test tool and manual exe sweeps instead of fixing the build so the running binary is never the link ou [unverified]
- **F066** (medium) `music/composer.h:587` — RealizationStrategyRegistry is dead weight: `realizationStrategy` is parsed but never read, 'block' is hard-coded, and the rhythm-pattern strategy's logic is re-implement [unverified]
- **F070** (medium) `music/composer.h:902` — composer.h is the hidden definition site for member functions of classes declared in six other headers, papering over a Composer<->strategy include cycle [disputed]
- **F144** (medium) `render/instrument.h:181` — hiBoost hidden loudness law (backlog 19) still live; formula duplicated in two methods and the magic 0.3 hardcoded at six call sites [unverified]
- **F241** (medium) `source/wavetable_source.h:21` — Two ownership/wiring paths for the evolution (legacy string-typed unique_ptr vs holder-node shared_ptr) with divergent defaults; TargetEvolution is reachable only via the [unverified]
- **F282** (medium) `engine/src/patch_loader.cpp:1687` — Whole node graph is built a second time, with a throwaway PerformContext, only to resolve the mixer's gainL/gainR [unverified]
- **F299** (medium) `durn_converter/main.cpp:176` — ABC pickup detection re-scans the raw text behind the parser's back and miscounts chord symbols and grace notes [unverified]
- **F309** (medium) `durn_converter/parsers/musicxml_parser.h:26` — Hand-rolled XML by substring search, with a dead get_attribute whose logic is re-implemented inline [unverified]
- **F346** (medium) `mforce_ui/main.cpp:722` — s_loadedParamMap is documented as dead residue but is still a live, edited binding model; two contradictory comments in this range [unverified]
- **F351** (medium) `mforce_ui/main.cpp:1081` — update_all_dsp swallows every exception, silently accepting a half-wired graph [unverified]
- **F379** (medium) `mforce_ui/main.cpp:4208` — UI fakes infinite streaming by mutating engine Envelope objects (absolute_time flip, temporary hold stage, 2-hour prepare) — a documented UI-side workaround for a one-fla [unverified]
- **F398** (medium) `mforce_ui/main.cpp:7796` — Mappings dialog carries two binding models at once: wires are declared canonical, but Add still creates legacy paramMap stash entries [unverified]
- **F429** (medium) `mforce_ui/main.cpp:11269` — OS-clipboard copy/paste round-trips the whole graph through temp files and a full reload [unverified]
- **F430** (medium) `mforce_ui/main.cpp:11442` — Temp files on disk are the graph-merge mechanism for paste/copy, and a dead temp-file playback path plus stale comments survive the 2026-09-13 unification [unverified]
- **F447** (medium) `mforce_ui/main.cpp:13571` — Audio device is torn down and reopened on every window refocus after >2 s away, plus a manual 'Restart audio' menu item, as substitutes for device-change notification [unverified]
- **F460** (medium) `mforce_ui/main.cpp:14630` — Generate is a blocking render on the UI thread sequenced by a magic-int state machine so one 'Generating...' frame gets drawn first [unverified]
- **F019** (low) `core/envelope.h:73` — A physical-model policy (reflection allowance) is baked into every Envelope as an engine-wide constant
- **F086** (low) `music/figure_transforms.h:560` — Dead expressions, silent no-op op, stale notes and mojibake comments scattered through the unit
- **F128** (low) `music/templates.h:305` — Cadential 'held' arrival — a self-described 2026-04 workaround — is still the default PhraseTemplate behaviour
- **F227** (low) `source/triangle_source.h:45` — `asymmetric` setting keeps an earlier per-leg warp alive as a toggle [unverified]
- **F261** (low) `engine/src/patch_loader.cpp:89` — Tap binding is threaded through a thread_local global with an RAII scope rather than a build context; several process-global mutable statics in the loader [unverified]
- **F392** (low) `mforce_ui/main.cpp:6543` — Shaper overlay instantiates a ShaperSource per frame and calls next() purely for its side effect of loading smoothness [unverified]
- **F436** (low) `mforce_ui/main.cpp:12168` — Partials scrubber undoes rolloff with pow() to recover weights it discarded ten lines earlier [unverified]

---

## 8. Recommended order of work

Ranked by payoff per unit of risk. Each campaign is independent of the others except where noted.

1. **Make the render path unconditionally real-time safe (engine).** Remove both throws from `next()` paths (clamp, warn once). Move every first-sample allocation into `prepare()`. Fix HistogramEqualize's per-cycle vector and RepeatingSource's re-prepare. Add an allocation-counting test over every baseline patch so the rule is enforced from now on. Small diffs, high confidence, no behaviour change for valid patches.
2. **Fix the comp-lane spine bugs.** Conductor per-section replay (one loop level), expose the realize step for tree-built pieces, make `compose()` take `PieceTemplate&` and drop the const_casts, fix `Chord::init_pitches`, honour `RandomFigureBuilder` contracts, fix the style-table lookup order and the ChordLabel names. Each is a contained change with an obvious test.
3. **Replace the UI's audio mutex with a publish/swap handoff.** Build voices and rewired graphs on the UI thread, publish by atomic exchange, reclaim on the existing GC. Lock every remaining unlocked DSP mutation behind the same mechanism. Fix the audition buffer resize. This is the one campaign that needs a design note first; the engine's live-path API changes with it (a prepared-voice handoff instead of in-place mutation).
4. **Let the registry win in the loader.** Collapse the 17-branch chain to the generic path plus configurators; merge the twin entry points; single source of truth for defaults and seeds. Then the three noise classes stop sounding different from CLI and UI. Medium effort, mechanical, well tested by the existing round-trip tools.
5. **Per-tick memo in ValueSource.** Replace the first-wired-advancer model with "compute once per sample index, serve cached"; delete the compensating machinery. Needs a null gate over library patches because it changes the one-sample-lag cases that currently depend on wiring order. Do it after 4.
6. **Split `mforce_ui/main.cpp`.** First extractions by payoff: (a) the patch document model with ONE codec shared with the engine (kills four of five JSON copies and the clipboard duplication), (b) the audio bridge (callback, voice pool, streams, GC, watchdog), (c) the Node Editor window body out of `main()`, then the panels one per file. Delete one clipboard on day one.
7. **Build and test hygiene.** CTest registration with a working-directory fix, `/WX` on the engine first, uniform warning level and standard across targets, a configure-time check or FetchContent for third_party, PCH for the tool TUs, move nlohmann/json behind the serializer headers. One afternoon, permanent payoff.
8. **De-duplicate the rest.** Ring-out policy to one function; DSP primitives (BLEP, Thiran, DC blocker, friction) to `core/`; CLI render epilogue to one helper on `signal_stats.h`; a common unit for the DURN parsers; the thirteen WaveEvolution holders to one template; the Butterworth trio to one template; composer.h strategy bodies back to their headers.
9. **Comment hygiene before release.** Move inline changelog comments to `docs/`, keep the one-line "why" at the site.

Not recommended now: a block-processing API or SIMD. The per-sample pull is a ceiling, not a bug, and campaigns 1 to 5 change the shapes it would be built on.

---

## 9. What is solid and should be preserved

- *core*: ValueSource interface is minimal and explicit (next/current/prepare + descriptor-based self-description); registry is a plain map of factories, no reflection, per project rule.
- *core*: fast_math.h and pierce_allpass.h are model engineering: measured motivation, stated accuracy bounds, exact-energy stiffness switch with provenance; no CRT calls in the hot path.
- *sources-noise*: No heap allocation, locks, exceptions or unbounded loops in any next()/compute_wave_value(); PinkNoise rows and LayeredRedNoise layers are allocated at construction/first prepare only.
- *sources-noise*: Seeds are explicit per instance and deterministic (LayeredRedNoise derives per-layer seeds via golden-ratio XOR); reseed()/reanchor hooks exist on nearly every RNG-bearing node and LayeredRedNoise forwards them.
- *sources-physical*: Allocation discipline is explicit and mostly honored: KSString/AllpassResonator assign buffers in the ctor and prepare() only zero-fills; Mesh2D uses fixed 64-cap arrays so set_setting/prepare never allocate; DelayLine's loop walk uses fixed visited/member arr
- *sources-physical*: Denormals are handled at the right layer: core/denormals.h enable_flush_denormals() is called in main() and re-asserted at the top of every RtAudio callback (mforce_ui/main.cpp:3862), so high-Q ring-downs in these feedback nodes cannot stall the audio thread.
- *sources-evolution*: WaveEvolution is a genuinely small strategy interface; the physical models (EKS/Jaffe-Smith, Friedlander bow table, RBJ biquad) are short, cite their references, and are readable top to bottom.
- *sources-evolution*: BowedStringEvolution and BrassEvolution correctly derive delay-line length and biquad coefficients from ctx.sampleRate via on_prepare (wave_evolution.h:423-431, 522-543).
- *sources-combinators*: Hot loops are heap-free: ShaperSource::map's lambdas feed a templated Curve::eval_core (curve.h:45-47), FMSource builds its decimation sections in prepare() (fm_source.h:70-82) and MultiplexSource only rebuilds when dirty (multiplex_source.h:106).
- *sources-combinators*: FMSource documents every legacy-parity decision (float accumulator precision wall, unbounded_pos single-step wrap, phase as PM offset not integrated) and keeps M==1 byte-identical (fm_source.h:119-124, 133-136).
- *additive*: Render path honours the no-allocation rule: all per-note vectors are sized in partials_prepare (partials.h:457-547), the per-sample body only indexes them, and the live audio_callback never calls prepare.
- *additive*: Hot-loop engineering is measured, not guessed: sum_partials collapses N virtual calls to one (partials.h:224-235, 618-631), per-sample scalars and per-partial caches hoist invariants (792-847), int-cast truncation and fast_sin_turns replace CRT calls (688-702,
- *filters*: No heap allocation or unbounded work in any next(): HammerBank, Reverb, Limiter, SVF, Biquad all keep state in fixed arrays or buffers sized at construction, and HammerBank documents the guarantee explicitly.
- *filters*: Coefficient-change guards (BW cutoff, SVF fc/res, Biquad resonance f/r) correctly implement the 2026-08-12 CPU-audit convention where they exist.
- *render-instrument*: PerformSource/PerformOut: explicit per-voice sample clock advanced once by the render driver, with idempotent stateless adapters (perform_source.h 47-139) — a clean solution to the per-consumer double-advance bug it replaced.
- *render-instrument*: InstrumentState uses std::atomic<float> with relaxed loads for the UI/MIDI→audio handoff (perform_source.h 14-17, 73-76); no locks in tick().
- *render-loader*: wire_params_generic (patch_loader.cpp:349-425) is a genuine generic path: input/param/setting/array descriptors drive wiring with zero per-type code for most of the ~75 registered types.
- *render-loader*: SourceRegistry (source_registry.h/.cpp) is minimal, explicit (no reflection), and the JsonConfigurator hook is exactly the right extension point for per-type JSON quirks.
- *music-model*: Articulation/Ornament/Element are closed std::variants with exhaustive handling; the model is value-typed and cheap to reason about
- *music-model*: figure_transforms are pure (const in, new figure out), randomized ones take Randomizer& explicitly, and apply() takes an explicit seed, matching the seeds-in-JSON rule
- *music-composer*: The four strategy base classes use a clean two-phase plan_*/compose_* contract with defaulted plan_* and a single pure-virtual compose_*; registries are explicit, name-keyed and trivially readable (project rule honored).
- *music-composer*: Composer::compose_passage centralizes the plan-persist-compose-grid_complete sequence so every passage strategy gets ending quantization for free (composer.h:263-278).
- *music-json*: ADL to_json/from_json organization in namespace mforce; nlohmann get<T>()/value() used idiomatically for optional fields with defaults, producing terse output that omits defaults
- *music-json*: Round-trip failures discovered in the past are fixed and annotated with the reason (chordConfig at templates_json.h:871-878, canonical-vs-flat chord progression at 15-26, endGrid always-written at 865-867), so the serializer explains its own quirks
- *music-builders-voicing*: Voicing framework has clean, minimal abstract interfaces (VoicingSelector::select, VoicingProfileSelector::profile_for_chord) with explicit factory/instance registries; the min-max normalized VL/common-tone composite in SmoothVoicingSelector is a sound, well-e
- *music-builders-voicing*: shape_figures.h is pure and stateless, every shape honors the units[0].step==0 convention, and it has no RNG or walker assumptions.
- *tools-cli*: explore.cpp spec parser throws on every missing/typo'd key (parse_axis, set_patch_param) instead of silently defaulting; manifest records params + SignalStats so --explore-filter can rank variants without re-rendering
- *tools-cli*: --lint-template's four-way DROPPED/DEFAULTED/FORM/CHANGED classification with the reasoning documented inline is a well-designed, low-noise round-trip linter
- *tools-tests*: engine_tests pins real DSP invariants sample-exactly rather than vibes: 100-sample impulse through DelayLineSource (226-262), self-referencing counter proving the tap z^-1 (203-222), loop period on-pitch to 0.5 samples with/without compensation (322-409), clos
- *tools-tests*: Determinism is disciplined: every stochastic test seeds explicitly (Randomizer(s) loops, std::mt19937 rng(7)), the best-of-N test asserts melody(100)==melody(100) (1887-1888), and the reseed test asserts exact sample replay (1167-1171).
- *tools-durn*: Parsers are genuinely self-contained (std-only, no engine dependency), so the tool builds in isolation and the headers are individually testable.
- *tools-durn*: midi_parser.h's detail::Reader is consistently bounds-checked (has()/skip()/sub()), handles running status, SysEx skipping, note-on-with-velocity-0 as note-off, SMPTE rejection and per-(channel,pitch) pending-note stacks, so malformed MIDI cannot read out of b
- *tools-misc-build*: mforce_engine is a proper add_library with PUBLIC include dirs; mforce_cli, mforce_ui, test_figures and engine_tests consume it via target_link_libraries, so include paths propagate correctly (engine/CMakeLists.txt:17-20).
- *tools-misc-build*: Release PDB generation is done right and documented: /Zi plus /DEBUG with /OPT:REF /OPT:ICF restored so the SEH crash filter can symbolize without losing Release optimizations (CMakeLists.txt:7-17).
- *arch-valuesource-graph*: Single, small hot interface (prepare/next/current) plus a separate cold self-description surface lets one generic loader (wire_params_generic) and one generic UI serve every node; adding a plain node is factory + descriptors only.
- *arch-valuesource-graph*: PerformSource as a non-ValueSource backing store with idempotent PerformOut leaf adapters (perform_source.h:102-139) is the right shape: one write per note, pull everywhere else, no per-consumer state.
- *arch-render-pipeline*: Off-thread destruction discipline: the callback only flips flags (voice_deactivate_unlocked, main.cpp:3745-3750, 3919-3924) and voice_gc (4151-4171) moves shared_ptrs out under the lock and destroys after releasing it.
- *arch-render-pipeline*: InstrumentState wheel/pressure are std::atomic<float> stored by the UI/MIDI thread and smoothed per voice in PerformSource::tick (perform_source.h:14-17, 70-78); PerformOut is a stateless idempotent view so RefSource-wrapped fan-out cannot double-advance.
- *arch-music-model*: Element is a closed std::variant<Note, Chord, Hit, Rest> and ElementSequence is the single authoritative score; Conductor::perform_events reads only it (conductor.h:588-610) - the intended boundary is clearly stated and partially realized.
- *arch-music-model*: Articulation and Ornament are std::variant value types dispatched with std::visit / if constexpr (basics.h:51-96, conductor.h:31-46, music_json.h:19-44) - modern, exhaustive, no enum-plus-payload smell.
- *arch-ui-tools*: generate_unified (main.cpp 4347-4470) serializes the live graph and loads it through load_instrument_patch_json so Generate IS the engine render; per-node strips are captured from the clones that actually play rather than from a second UI-side render.
- *arch-ui-tools*: Audio-thread hygiene where it exists is careful: no shared_ptr destruction on the callback (3919-3924), voice_gc releases on the UI thread (4151-4171), denormal flush per callback (3862), heartbeat watchdog + focus-regain restart for WASAPI zombie streams (408
- *arch-duplication*: envelope_json.h: single preset-form dispatch shared by patch_loader.cpp and mforce_ui (the comment at main.cpp:1857-1864 records the drift bug it fixed)
- *arch-duplication*: curve.h Curve::eval_seg is the one segment evaluator for CurveNode, ShaperSource and the UI shape editor overlays
- *arch-modern-cpp*: Descriptor API uses std::span + std::string_view (ParamDescriptor/InputDescriptor/SettingDescriptor/ArrayDescriptor) and static constexpr tables; enum class + final on leaf ValueSource types
- *arch-modern-cpp*: No raw new/delete or malloc in engine/ (only four `new` sites in composer.h, all immediately wrapped); std::variant for articulations/ornaments; std::optional used for template fields
- *arch-hacks-census*: Debt is tracked with stable ids and most in-code hacks cite them ('backlog 26a', '63b', 'spec §6'), so the census could be cross-checked rather than guessed
- *arch-hacks-census*: Legacy formats (paramMap, string evolution, numeric CombineOp ordinals, KSPianoString name) are converted or aliased at LOAD; no runtime compat toggles exist inside render paths, consistent with the no-back-compat policy
- *arch-build-headers*: Header include graph has no cycles (tsort over 330 header->header edges reports none) and every header uses #pragma once.
- *arch-build-headers*: No ODR hazards: all namespace-scope function definitions in headers are `inline`; class statics (e.g. Meter::M_4_4) are defined once in engine/src/music.cpp:160-162; registry singletons use function-local statics inside inline functions.

---

## Appendix A. Refuted and out-of-scope findings

Refuted (13): the skeptics could not confirm the evidence or the consequence. The original text is kept in the raw folder.

- F007 `core/curve.h:59` Log-domain curve evaluation (curve.h) and expression-mode (curve_node.h) have no guard against duplicate or no — Evidence is at the cited lines (curve.h:59-60, curve_node.h:74/76) but the headline consequence does not follow. In both eval_core and map_expr the segment search is: guard `x <= px(0)` returns py(0),
- F010 `core/dsp_value_source.h:1` Zero [[nodiscard]] and zero noexcept across engine and tools; move operations not noexcept so vector reallocat — Evidence partly checks out: grep over engine/ + tools/ (excluding third_party) gives 0 `[[nodiscard]]` and 0 `noexcept`, and the three s_nodes reallocation comments exist at tools/mforce_ui/main.cpp:1
- F016 `core/envelope.h:6` Envelope embeds two std::mt19937 engines and pulls <random> into 72 of 128 headers, while the rest of the engi — Evidence partly checks out: envelope.h:6 includes <random>; :554-555 hold two std::mt19937; :338 uses uniform_real_distribution; partials.h:986-987, wave_evolution.h:90, ks_string.h:401 use Randomizer
- F043 `filter/limiter.h:90` Per-sample transcendental recomputation and non-const/double-evaluated helpers in sample paths — Evidence partially confirmed, stated consequences refuted. CONFIRMED: limiter.h:90 has `std::exp(-1.0f / std::max(1.0f, relSec * float(sampleRate_)))` inside next() (per-sample). shaper_source.h:206 `
- F081 `music/dun_parser.h:11` Missing direct includes and an unused <iostream> in headers that fan out through templates.h to 19 TUs — Evidence at dun_parser.h:11 is real (`#include <iostream>`; zero std::cout/cerr/cin/clog in the file). The stated consequence does not follow: dun_parser.h is included by exactly ONE TU (tools/mforce_
- F085 `music/figure_transforms.h:372` Exceptions used as control flow inside complexify; smooth_voicing_selector swallows everything with catch(...) — Evidence exists: try at 372, catch(const std::invalid_argument&) at 376, and smooth_voicing_selector.h:46 catch(...). The stated consequence does not follow. (1) complexify() is not on any production 
- F094 `music/music_json.h:13` Namespace-scope `using json = nlohmann::json;` in public headers — Evidence confirmed: `using json = nlohmann::json;` at music_json.h:13, templates_json.h:9, style_table.h:15, all inside namespace mforce. Consequence does not follow: there is no other json type anywh
- F097 `music/music_json.h:368` Several from_json overloads append into the target without clearing it, unlike their siblings — Same evidence as F096 (append-without-clear confirmed at 368-374, 515-522, 530-535) but the stated consequence is unsupported: the 'UI edit/reload loop' does not exist -- tools/mforce_ui/main.cpp neve
- F103 `music/passage_melody.h:113` Exact float equality on an accumulated beat cursor decides barline figure boundaries — Evidence is accurate: passage_melody.h:110 `std::fmod(passageBeat, beatsPerBar)`, :113 `beatInBar == 0.0f`, :128 `passageBeat += n.durationSeconds`. The consequence does not follow for this code path.
- F114 `music/rhythm_util.h:54` Weighted roulette selection hand-rolled in eight places; rhythm generator duplicated against PulseGenerator — Evidence check: every cited location exists and is materially as described — roulette loops at rhythm_util.h:54-60, chord_walker.h:159-169 and 310-315, core/randomizer.h:91-99 (select_int weighted), p
- F126 `music/templates.h:9` nlohmann/json is a transitive dependency of the core data model: 38 of 128 engine headers and every tool TU co — Evidence lines verified (templates.h:9 and :545; source_registry.h:3,14-15; music_json.h:13 and templates_json.h:9 'using json' — style_table.h:15 also has it, so three headers not two). Re-measured t
- F134 `music/templates_json.h:692` JSON sub-object parsers duplicated instead of from_json overloads: PhraseTemplate vs PeriodSpec phrase, Voicin — Two of the three headline claims do not survive inspection. (a) PhraseTemplate vs PeriodSpec loadPhrase duplication, verbatim connectors blocks 741-758 vs 821-838, and the stale 'enforces startingPitc
- F149 `render/instrument.h:442` render_chunk resizes the output vector per call and is called with n=1 per sample; its 'caller must reserve' c — Evidence lines are accurate: 442 resize per call, 631/686 n=1 per sample, 436-437 'caller must reserve', 590-591 reserve = first note durSamples + kMaxRingSec(8 s, line 118)*sr. A continuation line wh

Out of scope per the severity judge (1): style preference without a named cost, contradicts a project rule, or targets an exempt file.

- F018 `core/envelope.h:44` Public members with private-style trailing-underscore names, and a mis-encoded comment — The rename is a style preference with no concrete cost (25 of the 28 uses are inside envelope.h; 3 external in patch_loader/instrument — churn in the core envel

## Appendix B. Method notes

- Find pass: 33 agents (24 unit, 1 UI outline, 8 architecture), each instructed to read every line of its files, cite file and line, quote evidence, and report duplication only with both locations. Agents were told which project rules are not smells (explicit registries, seeds in JSON, header-heavy layout without a named cost, and that no heap/locks/unbounded work on per-sample paths is a hard rule).
- Dedup: findings on the same file within six lines with overlapping titles, or with strongly overlapping titles within 400 lines, were merged; the highest-severity reporter's text was kept and the others recorded as alternates.
- Verify pass: batches of up to six findings per file; one skeptic per batch, two for batches containing a critical or high finding, plus a severity judge for those. Skeptics were told to default to "refuted" when they could not confirm the evidence or the consequence from the code. Final severity is the median of the verifier calls.
- Items marked "confirmed by me" were read directly in the course of synthesis, independent of the agents.
- Not covered: the Python research scripts (about 51k lines), the exempt strategy files, runtime profiling (no measurements were taken; every performance statement here is structural), and dynamic analysis (no sanitizer or allocation-hook runs; the allocation findings are from reading).
