# MForce project instructions

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
- Windows-only is fine for now, but plan for future cross-platform

## Non-negotiables
- No heap allocation in hot render loops
- Avoid reflection-like designs; prefer explicit registries
- Seeds: random at creation time, stored in JSON for reproducibility. Not user-facing.

## Architecture direction
- ValueSource graph model is fundamental for DSP
- Keep the code real-time safe where practical
- Maintain compatibility with future use of JUCE or other frameworks

## Build and run
- Build from repo root
- Main executable: mforce_cli
- Write renders into renders/

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
- renders/ keeps the lane level — {dsp, comp}/{sweep, pending}/<effort-family>/ — because
  renders come from both lanes; per-lane pending/ = Matt's two listening queues.
  renders/library/ is laneless (audio archive of locked patches); renders/scratch/ for
  manual material. All of renders/ stays gitignored.
- old/ — deletion grace window (patches ONLY): rejected/failed patches are MOVED here,
  never deleted directly; the nightly scheduled runs purge files >30 days old.

Rules: no loose files at any level above a family folder; new patches/scores/renders are
NEVER written to a tree root. patches/audition/ holds only what awaits Matt's ears; each
audition verdict promotes to library/ or moves to old/ the same day. Never write into
patches/pending/ — that is Matt's sandbox. (scores/ and renders/ keep their pending/ naming.) Failed renders are deleted
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
