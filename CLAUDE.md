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
- Windows-only is fine for now, but plan for future cross-platform

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
  protocol to save quota.
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
  last_model.txt, written by the SessionStart hook flow). Expected: Fable 5.x, High
  or above. Never Opus 5/5.5; fallback is claude-opus-4-8. Offer xhigh/max before
  design or review work.
- Session start: sweep stale renamed UI exes (build/tools/mforce_ui/Release/
  mforce_ui_*.exe — rename-then-link leftovers; skip any still locked/running)
- Build from repo root
- Main executable: mforce_cli
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

## Near-term priorities
1. Create json formats for the data classes in MForce/Music (eg RhythmicFigure)
2. Determine best approach for UI for creating and modifying patches and hearing immediate results
3. Implement UI for creating and modifying patches and hearing immediate results
4. Determine best approach for UI for producing specified chords, melodies, etc. from user input
5. Implement UI for producing specfied chords, melodies, etc. from user input
6. Move on to address goal of supporting user-guided algorithmic composition
