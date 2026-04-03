# MForce project instructions

## Commands                                                                                                       - Use literal paths, not shell variables ($CMAKE, $p, etc.)
- Use literal paths, not shell variables ($CMAKE, $p, etc.)
- Avoid for/while loops in bash commands, chain independent commands with && instead
- Never combine cd with git — use git -C path instead
- Never combine cd with write operations (mv, rm, sed -i) — use absolute paths instead
- For commands that need to run from the repo root, rely on the working directory already being correct rather than
  prefixing with cd

## Repositories
- Current C++ implementation: this repo
- Legacy C# core implementation: ./mforce-legacy
- Legacy C#/Unity controller and UI implementation: ./mforce-unity
- The base class for all components is MUnityComponent which extended MonoBehaviour (base for all Unity objects)
- The top-level unity class is SoundController.cs
- There are 2 generations of unity wrapper classes per "core" class:
  1 - *Wrapper are gen 1, scripts user can add added to Unity hierarchy and set params via inspector UI
  2 - *Node are gen 2, UI nodes using 3rd party xNode library which user can add to a visual node graph
- Both generations are functional but source for port should be gen 2 nodes

When porting code:
- Always use best practices for modern C++ architecture
- This will be a large professional project, not a toy, so architect accordingly
- Don't blindly port class for class if you see issues or a better approach
- Come back with a proposal or multiple options vs. proceeding if warranted

The legacy implementation is unfinished, so this is only initially a porting exercise

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
