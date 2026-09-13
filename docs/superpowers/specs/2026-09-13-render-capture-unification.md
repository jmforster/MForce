# Render-capture unification — one Generate pass, displays observe the render

Status: SPEC, 2026-09-13 (Matt's directive after the tap-loop UI bugs:
the two-renderer Generate is a kludge; bring back the legacy
addValueListener idea — displays subscribe to the graph that actually
plays, one pass fills the play buffer AND every node strip).
Context: the 09-13 fixes (collect_envelopes through RefSource; passage
Play = replay last Generate) stopped the bleeding; this spec removes
the underlying split. Supersedes the "tick tap tails in the UI pass"
option — that pass is deleted instead of repaired.

## 1. Problem

Generate in patch mode runs TWO renderers:

1. The UI-graph pass (`render_waveforms` / `render_passage_waveforms`)
   drives the editor's in-memory template graph to fill per-node
   waveform strips — and is a second, worse implementation of
   playback: no advanceList (tap-only loop tails freeze → flat strips
   on string-family patches), no Multiplex fan-out, no load-time
   constructs (starved-ref promotion, paramMap push bindings).
2. The authoritative pass (`render_*_output_authoritative`) writes the
   current graph to a TEMP FILE, re-loads it via
   `load_instrument_patch`, renders through PitchedInstrument, and
   overwrites `g_outputWaveform` — correct audio, but per-node strips
   keep pass 1's lies, and "authoritative" exists as a word only
   because pass 1 is not.

Legacy precedent: C# ValueSources had addValueListener; waveform
panels subscribed to their node and were populated by the SAME render
that filled the play buffer. That worked because the edited graph WAS
the playing graph. In C++ the editor graph is a template and playback
runs per-voice CLONES built by the loader — so the fix is not "make
the editor graph play," it is "let displays observe the clones."

## 2. Design (one sentence)

Generate = serialize the editor graph to in-memory JSON → load a
PitchedInstrument from it directly (no temp file) → register a capture
per visible node id → play the notes; the engine's own per-sample loop
appends each captured clone's `current()` into timeline-length strip
buffers, summing across voices exactly like the audio does → copy
strips to the UI, done. `render_passage_waveforms`'s DSP pass and both
`*_authoritative` functions collapse into this single path.

## 3. Engine changes (mforce/render)

3a. `load_instrument_patch(const nlohmann::json& root, int
    minPolyphony = 0)` overload; the path version parses and
    delegates. No behavior change for existing callers.

3b. VoiceGraph retains its id → instance map: `build_graph` already
    returns `g.valueNodes`; keep it as `vg.nodesById`
    (shared_ptr map, ~node-count entries per voice — negligible; the
    graph outlives it anyway). Loader-only change, no render cost.

3c. Capture facility on PitchedInstrument (OFFLINE pre-render path
    only — `play_note`; the live/streaming path `prepare_voice_at` is
    untouched, so no RT-safety exposure):

    - `capture_begin(const std::vector<std::string>& ids, int
      timelineFrames)`: allocates one float buffer of timelineFrames
      per id (zeroed), and per voice resolves ids against
      vg.nodesById into a per-voice list of (ValueSource*, bufferIdx).
      Unknown ids are skipped silently (UI-only nodes).
    - In `play_note`'s per-sample loop, after `vg.source->next()` and
      the advanceList ticks: for each capture entry,
      `buf[startFrame + i] += node->current()`. ADDITIVE at the note's
      timeline offset — overlapping notes/voices sum into the strip
      the same way `add_rendered` sums the audio. This is the
      polyphony semantics decision: a strip shows the node's total
      contribution to what you hear.
    - `capture_end()` / accessor hands the buffers back; captures
      cleared so subsequent play_note calls pay nothing.

3d. NOT in scope: capture inside MultiplexSource internal clones. A
    Multiplex node's strip shows its own (summed) output; its
    children show their template silence. Revisit only if a real
    patch needs it.

## 4. UI changes (tools/mforce_ui)

4a. Split `save_patch_graph` into `json serialize_patch_graph(
    std::unordered_map<int,std::string>* outIds)` + a thin file
    writer. The id map (GraphNode.id → serialized string id, label-
    based) is RETURNED by serialization so capture registration can
    never drift from what was serialized.

4b. One Generate implementation for Note and Passage modes (Chords
    already loads an instrument; it gains captures with the same
    call): serialize → load_instrument_patch(json) → capture_begin
    with every displayable node id (dspSource-bearing, non-special) →
    play_note per parsed note → `instrument.render` into
    g_outputWaveform → copy capture buffers into node.waveformData →
    wave_view_after_render + compute_output_spectrum.

4c. Delete: `render_passage_waveforms`'s DSP pass,
    `render_output_authoritative`, `render_passage_output_
    authoritative`, and the Generate-side temp-file hop (the Listen
    tap's temp-file path is separate and OUT OF SCOPE here).

4d. Untouched: live keyboard path, streaming (`play_continuous`),
    node-graph (Mixer) mode, Play-replays-last-Generate behavior.

## 5. Consequences

- "Authoritative" ceases to exist as a concept: Generate IS the CLI
  render path, by construction, including taps, Multiplex, promotion,
  push bindings. UI-vs-CLI divergence (RD audition-path mismatch
  class) closes for Generate.
- Node strips become truthful for feedback-loop patches (today they
  are flat for every tap-only-tail patch and nobody had looked).
- Passage Generate gets faster (one render, no temp file, no double
  pass); memory unchanged (strips were already timeline-length).
- Param tweaks since last save are included — serialization reads the
  live editor state, same as the temp file did.

## 6. Validation

1. `--gatecheck` stays green (loader refactor must not disturb it).
2. New headless mode `--gencheck <patch> <notes>`: runs the unified
   Generate path, writes the output buffer as WAV; byte-compare
   against `mforce_cli` rendering the same patch + score. Identity
   (or documented gain-staging delta) = the "authoritative is a
   given" acceptance test.
3. Strip truth test: for a string-harness patch, assert NutDelay's
   capture buffer is non-flat (the exact strip that is silently flat
   today).
4. Hands: Generate/Play A/B on piano_default (regression),
   str2_g32_b36_h05 (the bug family), oboe1 (Wormhole family).

## 7. Risks / open edges

- Voice reuse in long passages: voicePool cycles via `nextVoice %
  size`; captures are per-voice pointers, so reuse is free (same
  clone, later offset). No issue expected; test 2 covers it.
- Grouped nodes: serialization flattens groups, ids are labels —
  capture works; drilled-in group UI strips follow the same id map.
- Note-mode Generate currently seeds transport from the embedded
  score; unchanged, only the render engine underneath swaps.
- `nlohmann::json` in the loader header: overload takes const ref;
  patch_loader.cpp already includes it. Header gains a forward
  declare only (keep JUCE-future include hygiene).
