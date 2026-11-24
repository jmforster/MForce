# MForce project instructions

## Session style (hard rule — Matt has had to ask repeatedly)
- NEVER wrap up. No day summaries ("where the day nets out/ended up"), no
  closure framing ("X awaits whenever you're done"), no offers to stop or
  defer to "next session". Matt works on MForce alongside other things and
  decides himself when a day ends — sometimes that's noon, sometimes not.
- Reports state results and the next live threads, then stop. Summarize
  STATE, never elapsed time or the session as an arc.
- Close-out rule (Matt 2026-10-05): when Matt closes out a session
  ("anything to save?", "starting a new session"), check `git status -sb`
  live and, if main is ahead of origin, ask in one line whether to push the
  commits since the last push. Push only on his yes. (main sat 658 commits
  unpushed from 07-03 to 10-04 because nothing ever said to.)

## Commands
- Use literal paths, not shell variables ($CMAKE, $p, etc.)
- Avoid for/while loops in bash commands, chain independent commands with && instead
- Never combine cd with git — use git -C path instead
- Never combine cd with write operations (mv, rm, sed -i) — use absolute paths instead
- For commands that need to run from the repo root, rely on the working directory already being correct rather than
  prefixing with cd

## Repositories
- Current C++ implementation: this repo
- Legacy C# reference implementations live as sibling repos, consult only if needed:
  ../mforce-legacy (core) and ../mforce-unity (Unity controller/UI, gen-2 *Node classes were the port source)

## Mission
Create a platform for making music and sound effects, with 3 pillars
1. Sound
- First class node-based UI for building DSP graph
- DSP graph is graph of generators, filters, modulators, envelopes, formants etc.
- Most of these nodes share a common ValueSource interface
- Every parameter of each ValueSource can itself be a ValueSource
- Realtime rendering, ie, tweak parameter, hear the results and see updated waveform display
- Agent-assisted "search" for new sounds by batch rendering and analysis
2. Music
- Object/data model representing every aspect of music: rhythm, pitch, melody, harmony, ornamentation, etc.
3. Composition
- Algorithmic composition using the Music building blocks
  + RhythmicSequences represent duration of notes/events
  + StepSequences represent movement within a musical Scale
  + Figures combine RhythmicSequence + StepSequences into a playable melody
  + Support for DrumSequences and ChordSequences 
- Two "flavors" of algorithmic composition
  1. Procedural: random construction of Figures, intelligent combination into Parts/Pieces
  2. AI: model-based prediction of next note analogous to an LLM for text
- Either "flavor" can be fully automatic or user-driver
- User-driven mode allows step by step generation, review, acceptance or rejection, to build a Piece

Replicate legacy functionality of node-based UI to:
- Build DSP graph
- Display resulting waveforms (from low-level ValueSource output to the mixer's L and R output)
- Allow continuous playing of sounds with parameter changes altering output in real time
- Allow triggering of discrete notes via UI keyboard (replacing QWERTY harness already implemented)

## Scope right now
- REFACTOR ONLY (Matt 2026-10-04): "Absolutely no feature work for awhile", in
  any lane, including autonomous "go" runs, until Matt lifts it. Protocol:
  docs/superpowers/specs/2026-10-04-refactor-gates-design.md
- THE REFACTOR, LIGHTER AND REORDERED (Matt 2026-11-21, approved that day
  with "YEs"; his words on why: the refactor "seems to be collapsing under
  the weight of all your safeguards", "I've done nothing productive (ie,
  making new sounds) since the refactor started", and of the old editor "one
  13k line pile of rubbish, albeit working rubbish", with the hope that the
  result "won't be pile-of-rubbish 2.0"). Where this conflicts with the gates
  protocol or with a line below, this wins:
  + The engine as refactored through Part 4 stays. The old editor
    (tools/mforce_ui) is DELETED, not adapted: Matt has no need for an editor
    until the rest of the refactor is done. The new editor is written fresh on
    the `patch` module, from a short design Matt signs before any code.
  + Order (Matt 2026-11-22: "Editor last". The 11-21 text put the editor
    second; that misread his "no need for the editor until this is done"):
    (1) the engine flip (every host on the `patch` module; the loader and the
    old editor deleted), DONE 11-21; (2) the render, music and build parts,
    in that order (render and music DONE 11-22; build DONE 11-23); (3) the
    new editor's design (`docs/architecture/editor.md`, SIGNED 11-23);
    (4) the editor in slices, the first being open, play, tweak, hear, save
    (slices 1 and 2 BUILT 11-23 and on Matt's desk, REVIEW R28; each later slice
    gets a page of interfaces for his signature before its code).
  + WHEN THE REFACTOR IS DONE (Matt 2026-11-23): "I want the refactor to
    be entirely done before we resume adding features." Done means "when
    all the capability that existed previously is restored": every slice
    of the editor, measured against the list of what the old editor did
    (`docs/architecture/editor-parity.md`). Then "we will take a pause and
    I will try to empirically reassure myself that the code does not suck
    (have reached out to some old C++ colleagues, for eg)". Features
    resume only after that pause, on his word. Do not propose lifting the
    freeze earlier or building a capability between slices. He does not
    want Max effort ("it just turns into Hamlet ... unable to move
    forward (and that's been bad enuf on xhigh)"): decide what is yours to
    decide and keep moving.
  + The 11-21 wording here, "the freeze lifts for sound work", was a
    misreading, corrected by Matt 2026-11-23: "I am not engaged in 'sound
    work' other than to try out new capabilities of MForce - eg, you build
    physical modeling capability, I create an oboe patch to test it out.
    I'm engaged (with you) in building MForce." and "I'm just fervently
    hoping we get back to moving *forward* again". So: the editor is part
    of MForce and the bench a new capability is tried on; "forward" means
    building capability again. Never frame a milestone as when Matt can
    make sounds; report what is left as the distance to building
    capability.
  + Proof from here: the build, engine_tests and the null gate. (Since
    Part 8, 2026-11-23, `engine_tests` is one test program per module and
    the proof is one command, `python tools/gates.py`: see "Build and
    run".) No synthetic
    sets, twin gates, mutation runs, fix waves or per-checkpoint reports. One
    independent review and one short report per part, the C++ lessons kept.
    Interfaces are still designed with Matt before code: design is not what
    was cut.
  + The new editor's structure limits fail the build from its first commit,
    with no exemptions: a file 600 lines, a function 80, no file-level
    mutable variable, the module map. Its logic is operations on plain data
    with tests; the window is a thin layer over them.
- Windows-only is fine for now, but plan for future cross-platform
- Refactor verdicts (Matt 2026-11-02): for the remainder of the refactor do
  NOT put Matt in the loop for easily-tweaked items (colours, menu dividers,
  labels, strip colours, any reversible presentation or grouping call). If
  the cause is understood and the implication is not far-reaching, decide
  it, record the decision in the report's section 6, and add it to
  docs/autonomy/refactor/CLEANUP.md (the post-refactor *suggested* cleanup
  master list). REVIEW items are for far-reaching calls only: an interface
  Matt should own, a render he should hear, a deletion. The oboe1 A/B at
  0 moved is accepted standing ("oboe patch is fine").

## Non-negotiables
- No heap allocation in hot render loops
- Avoid reflection-like designs; prefer explicit registries
- Seeds: random at creation time, stored in JSON for reproducibility. Not user-facing.

## Design principles (Matt 2026-10-04, recorded at his request)
- Matt is NOT a "just land it" owner — the polar opposite. Speed is never a
  reason to skip design.
- Correct abstractions and design patterns are crucial, not just important,
  including inheritance where it is powerful (exhibit A: ValueSource).
  Inheritance vs composition is a design decision, never dictated by the language.
- Goal: go public to attract a community of developers to collaborate (not a
  commercial product). When that day arrives the code must be as close to
  pristine as we can get it.
- Git holds the full history: no dated changelog in code comments.
- If the refactor protocol (gates, independent review, architecture reports)
  exhausts the weekly quota by Wednesday, that is acceptable. Do not thin the
  protocol to save quota. (Amended 2026-11-21, see "Scope right now": the
  per-checkpoint proofs were cut because they cost far more than they caught,
  not to save quota; design and review are not thinned.)
- C++ boot camp is part of the work: primer at docs/matt/cpp_primer_10000ft.md,
  then a lesson or two in every architecture report (the idiom used and its
  alternative, the C#/Java analogue, why the name was chosen).
- Architecture reports err on moderately verbose.

## Architecture direction
- ValueSource graph model is fundamental for DSP
- Keep the code real-time safe where practical
- Maintain compatibility with future use of JUCE or other frameworks

## Build and run
- Session start (hard rule, Matt 2026-10-02): FIRST line of the first reply states
  the model and effort level (verify with get_session self) and flags any change
  from the last session's recorded value (~/.claude/projects/C---dev-repos-mforce/
  last_model.txt, written by the SessionStart hook flow). Expected: Fable 5.2
  (Matt 2026-11-09; a session on Fable 5.1 is a change to flag), High or above.
  Nothing in a settings file picks the app's model: the Code tab's model
  picker remembers its last choice, so a new session comes up on whatever was
  picked last; Matt sets 5.2 in the picker. Opus 5.5 is allowed again (Matt 2026-11-06 rescinded the "never
  Opus 5/5.5" rule: "just delegate to Opus 5.5 agents"); executors and
  reviewers are Opus 5.5 subagents (the Agent tool's model "opus"), and
  claude-opus-4-8 is no longer the required fallback. Do NOT offer or suggest
  a higher effort setting (Matt 2026-11-22, removing the old "offer xhigh/max
  before design or review work" rule: "overkill is currently what's killing
  us"). Matt picks the setting; work at it. The target architecture's
  "maximum effort" marks predate this and no longer apply.
- (No UI exe to sweep at session start: the build moves a running
  `mforce_editor.exe` aside before it links, so a build never fails because
  the editor is open. Matt restarts it when he chooses.)
- Build from repo root: `cmake --build build --config Release --parallel`.
  Every warning is an error. A NEW build folder needs
  `-G "Visual Studio 17 2022"` (the `cmake` on PATH defaults to Ninja and
  MinGW). Matt's `build/` is configured with `-DMFORCE_SHELVED=ON`; the
  default is off (Part 8, 2026-11-23).
- Tests and gates: `python tools/gates.py --fast` (the per-module test
  programs, the structure gate, the tool tests) or `python tools/gates.py`
  (adds the null, allocation and comp gates). Neither builds.
- The engine is one folder per module, `engine/<module>/{include,src,tests}`;
  a module's `CMakeLists.txt` names what it may use and the compiler
  refuses anything else. The structure limits are a gate:
  `python tools/structure/check.py --gate`.
- The editor is one folder per module under `editor/` (`ui_model`,
  `ui_audio`, `ui_control`, `ui_views`, `ui_app`), made the same way, with
  NO baseline entries: a file over 600 lines, a function over 80 or a
  file-level variable there fails the gate. Its libraries are git submodules
  under `third_party/` (`git submodule update --init`); without them the
  engine and every gate still build.
- Main executable: mforce_cli. The editor:
  `build\editor\ui_app\Release\mforce_editor.exe [patch.json]`, run from the
  repo root (`--frames N --shot FILE` draws N frames, saves a picture and
  exits: how a session looks at the window).
- Write renders into renders/
- Comp-lane renders: MELODY on patches/library/winds/oboe1.json,
  ACCOMPANIMENT on patches/library/keys/acoustic_piano/piano_default.json,
  unless otherwise indicated (Matt 2026-09-21; supersedes the 09-05
  all-piano rule). Single-line harnesses (corpus/mtd_seg PATCH constant)
  are melody, so they repoint to oboe1 on next touch (constants still
  say piano_default today).

## Patch/score/render organization
A **patch** is an instrument (DSP graph + instrument block). A **score** is
instrument-independent musical material (score events, templates, passage/piece specs).
The embedded "score" block in patch JSON is a smoke-test convenience only; new comp
artifacts are score files. Full triage record: docs/patch_triage_2026_08_10.md.

- patches/ = dsp lane: {sweep, audition}/<effort-family>/ (gitignored) + library/<instrument-family>/
  (tracked, FINAL patches only — Matt's 2026-08-22 curation) + baselines/ (tracked dev-test/regression
  patches) + pending/ (gitignored — Matt's own play area, NOT a queue) + old/ + scratch/ (gitignored).
  Renamed 2026-08-22: audition/ is the listening queue (was pending/); pending/ is Matt's sandbox.
- scores/ = comp lane: same shape (sweep, pending, baselines, library, scratch)
- renders/ keeps the lane level — {dsp, comp}/{sweep, audition}/<effort-family>/ — because
  renders come from both lanes; per-lane audition/ = Matt's two listening queues
  (renamed from pending/ 2026-09-03 to match patches/; renders/*/pending/ is now
  Matt's hand-work, runs never write there; pre-rename queues left in place).
  renders/library/ is laneless (audio archive of locked patches); renders/scratch/ for
  manual material. All of renders/ stays gitignored.
- old/ — permanent archive (patches ONLY): rejected/failed patches are MOVED here,
  never deleted. NO purge — Matt 2026-09-05: nothing in patches/old/ is ever deleted
  (the former 30-day scheduled purge is rescinded and removed from the run prompts).

Rules: no loose files at any level above a family folder; new patches/scores/renders are
NEVER written to a tree root. patches/audition/ holds only what awaits Matt's ears; each
audition verdict promotes to library/ or moves to old/ the same day. Never write into
patches/pending/ or renders/*/pending/ — those are Matt's sandboxes. (scores/ keeps its
pending/ naming.) Failed renders are deleted
outright (derived data — score + patch + engine commit reproduces them). Never delete/move
anything still cited by an open item in docs/autonomy/*/REVIEW.md. Keeper WAVs in
renders/library/ are the audio archive — don't re-render over them after engine changes
without a reason.

## Validation expectations
After making code changes:
1. Build successfully
2. Run at least one relevant patch / snippet / piece
3. Report exactly what changed
4. If behavior differs, explain whether it is intentional or a bug

## Measurement scope (Matt 2026-10-30)
- patches/library + patches/baselines is the standard for every gate, A/B,
  allocation measurement and regression check. Spend no checkpoint time on
  audition/, pending/, sweep/ or old/ patches: working material that will
  likely never be used. If Matt later loads an old patch with promise and it
  is broken, say why; it gets figured out then.

## Near-term priorities
1. Create json formats for the data classes in MForce/Music (eg RhythmicFigure)
2. Determine best approach for UI for creating and modifying patches and hearing immediate results
3. Implement UI for creating and modifying patches and hearing immediate results
4. Determine best approach for UI for producing specified chords, melodies, etc. from user input
5. Implement UI for producing specfied chords, melodies, etc. from user input
6. Move on to address goal of supporting user-guided algorithmic composition
