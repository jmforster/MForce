# Render-Capture Unification Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** One Generate pass: the engine render that fills the play buffer also
fills every node's waveform strip, by capturing the playing clones — the
UI-graph offline renderer and both `*_authoritative` functions are deleted.

**Architecture:** The UI serializes the editor graph to in-memory JSON and
loads a `PitchedInstrument` from it directly (no temp file). A new offline
capture facility on `PitchedInstrument` appends each registered clone's
`current()` per sample inside `play_note`'s existing loop, summing at
timeline offsets exactly the way `add_rendered` sums audio (that is the
polyphony semantics: a strip = the node's total contribution). Evo
snapshots (Partials/Formant scrubber) become a post-render downsample of
the captured strips.

**Tech Stack:** C++17, nlohmann::json (engine third_party), ImGui/imnodes UI,
MSVC via CMake (`cmake --build build --config Release`).

**Spec:** docs/superpowers/specs/2026-09-13-render-capture-unification.md

## Global Constraints

- No heap allocation added inside per-sample loops: capture buffers are
  pre-allocated in `capture_begin`; the per-sample hook only indexes.
- The live/streaming path (`prepare_voice_at`, voice mixer, keyboard) is
  NOT touched by any task.
- Multiplex-internal clones are NOT captured (Matt 2026-09-13: strip may
  be blank or first-voice-only for Multiplex contents — don't care for now).
- Regression scope = `patches/library/` only; run the null corpus gate
  once, pre-commit, over the whole batch (not per task).
- Build from repo root. If `mforce_ui.exe` is link-locked (UI running):
  rename the locked exe (`mv ... mforce_ui_running_old.exe`) and relink.
- Two Claudes share this working copy: `git -C C:/@dev/repos/mforce branch
  --show-current` immediately before any commit; stage with `git add -u`
  plus explicit new-file paths, never a bare directory.
- Commit messages end with:
  `Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>`

---

### Task 1: Engine — JSON-text loader entry + per-voice id map

**Files:**
- Modify: `engine/include/mforce/render/patch_loader.h` (after line 28)
- Modify: `engine/src/patch_loader.cpp:1655-1713` (`load_instrument_patch`)
- Modify: `engine/include/mforce/render/instrument.h:97-119` (`VoiceGraph`)

**Interfaces:**
- Produces: `InstrumentPatch load_instrument_patch_json(const std::string&
  jsonText, int minPolyphony = 0);` (namespace `mforce`) and
  `PitchedInstrument::VoiceGraph::nodesById`
  (`std::unordered_map<std::string, std::shared_ptr<ValueSource>>`,
  JSON node id → this voice's clone instance).
- Consumes: nothing new.

Note: the overload takes serialized TEXT, not `nlohmann::json`, because
`engine/third_party/nlohmann/` ships only `json.hpp` (no `json_fwd.hpp`)
and `patch_loader.h` must not drag the full header into every consumer.
A patch JSON is <100 KB; dump+parse is negligible next to graph build.

- [ ] **Step 1: Declare the new entry in patch_loader.h**

```cpp
// Same as load_instrument_patch, from already-serialized JSON text —
// no file. The UI's Generate path serializes its live editor graph and
// loads it directly (render-capture unification spec 2026-09-13).
InstrumentPatch load_instrument_patch_json(const std::string& jsonText,
                                           int minPolyphony = 0);
```

- [ ] **Step 2: Split the implementation in patch_loader.cpp**

Rename the body of `load_instrument_patch` (line 1655) to
`load_instrument_patch_json`, replace its first line
`json root = json::parse(slurp(path));` with
`json root = json::parse(jsonText);`, replace the two `+ path` error-message
uses with `+ std::string(" (in-memory patch)")` — no, keep messages useful:
change the "no 'instrument' section" throw to
`throw std::runtime_error("Patch has no 'instrument' section");` (drop the
path suffix; the caller's context names the patch). Then re-add:

```cpp
InstrumentPatch load_instrument_patch(const std::string& path,
                                      int minPolyphony)
{
    return load_instrument_patch_json(slurp(path), minPolyphony);
}
```

- [ ] **Step 3: Retain the per-voice id map**

In `instrument.h` `VoiceGraph` (after `advanceList`, line 118):

```cpp
    // JSON node id -> this voice's clone (retained from build_graph so
    // offline capture can resolve display ids; ~node-count shared_ptrs,
    // the graph outlives them anyway). Empty for mixer-path instruments.
    std::unordered_map<std::string, std::shared_ptr<ValueSource>> nodesById;
```

In `load_instrument_patch_json`, after the `collect_advance_ids` loop
(the last use of `g.valueNodes`, currently line 1706-1707):

```cpp
        vg.nodesById = std::move(g.valueNodes);
```

- [ ] **Step 4: Build engine + both exes**

Run: `cmake --build C:/@dev/repos/mforce/build --config Release --target mforce_cli --target mforce_ui`
Expected: clean build (rename-then-link if the UI exe is locked).

- [ ] **Step 5: Regression check — loader unchanged**

```bash
C:/@dev/repos/mforce/build/tools/mforce_cli/Release/mforce_cli.exe patches/library/keys/acoustic_piano/piano_default.json "C:/Users/MATTFO~1/AppData/Local/Temp/claude/C---dev-repos-mforce/c3725ba1-1bfb-4ed7-bf2f-9a4a7bdf4ddd/scratchpad/t1_piano.wav"
```

and

```bash
C:/@dev/repos/mforce/build/tools/mforce_ui/Release/mforce_ui.exe --gatecheck patches/audition/string_harness2/str2_g32_b36_h05.json
```

Expected: render stats printed (peak/rms nonzero), and
`gateable=1 envelopes=2` exactly as before this task.

- [ ] **Step 6: Commit**

`git -C C:/@dev/repos/mforce branch --show-current` (must be main), then
commit patch_loader.h/.cpp + instrument.h:
`engine: load_instrument_patch_json entry + VoiceGraph.nodesById retention`

---

### Task 2: Engine — offline capture facility on PitchedInstrument

**Files:**
- Modify: `engine/include/mforce/render/instrument.h` (PitchedInstrument,
  members near line 127, `play_note` at lines 256-296)

**Interfaces:**
- Consumes: `VoiceGraph::nodesById` (Task 1).
- Produces:
  `void PitchedInstrument::capture_begin(const std::vector<std::string>& ids, int timelineFrames)`
  — allocates `captureBuffers` (one zeroed `std::vector<float>` of
  timelineFrames per id, same order) and resolves ids per voice;
  `std::vector<std::vector<float>> PitchedInstrument::captureBuffers`
  — public, caller moves buffers out after rendering;
  `void PitchedInstrument::capture_end()` — clears the per-voice lists so
  later `play_note` calls pay nothing.

- [ ] **Step 1: Add members + API**

Inside `PitchedInstrument`, after `slotInUse` / `acquire_voice` block:

```cpp
  // --- Offline per-node capture (render-capture unification spec
  // 2026-09-13). Registered by the UI's Generate; play_note SUMS each
  // captured clone's current() into a timeline-length buffer at the
  // note's start offset — strips accumulate across voices exactly like
  // add_rendered accumulates audio. OFFLINE path only: the streaming/
  // live path never touches this, and with no registration the render
  // loop pays one branch. Buffers are pre-allocated here — nothing
  // allocates inside the per-sample loop.
  struct CaptureEntry { ValueSource* node; int bufIdx; };
  std::vector<std::vector<CaptureEntry>> capturePerVoice; // parallel to voicePool
  std::vector<std::vector<float>> captureBuffers;         // per id, timeline frames

  void capture_begin(const std::vector<std::string>& ids, int timelineFrames) {
    captureBuffers.assign(ids.size(), {});
    for (auto& b : captureBuffers) b.assign(size_t(timelineFrames), 0.0f);
    capturePerVoice.assign(voicePool.size(), {});
    for (size_t v = 0; v < voicePool.size(); ++v)
      for (size_t k = 0; k < ids.size(); ++k) {
        auto it = voicePool[v].nodesById.find(ids[k]);
        if (it != voicePool[v].nodesById.end())
          capturePerVoice[v].push_back({it->second.get(), int(k)});
      }
  }
  void capture_end() { capturePerVoice.clear(); }
```

Unknown ids resolve to nothing per voice — silently skipped (UI-only
nodes, Multiplex internals). That is the spec's §3d behavior.

- [ ] **Step 2: Hook play_note**

Change the top of `play_note` to keep the voice index:

```cpp
  void play_note(float noteNumber, float velocity, float duration, float startTime,
                 const PitchCurve* curve = nullptr) {
    int vIdx = int(nextVoice % int(voicePool.size()));
    auto& vg = voicePool[vIdx];
    nextVoice++;
```

and extend the per-sample loop (currently lines 277-281):

```cpp
    int startFrame = int(startTime * float(sampleRate));
    const bool capturing = !capturePerVoice.empty();
    std::vector<float> buf(durSamples);
    for (int i = 0; i < durSamples; ++i) {
      if (vg.performSource) vg.performSource->tick();   // P3 sample clock
      buf[i] = vg.source->next() * gain;
      for (auto& a : vg.advanceList) a->next();         // tap-only loop tails
      if (capturing) {
        int f = startFrame + i;
        for (auto& ce : capturePerVoice[vIdx]) {
          auto& dst = captureBuffers[size_t(ce.bufIdx)];
          if (f >= 0 && f < int(dst.size()))
            dst[size_t(f)] += ce.node->current();
        }
      }
    }
```

(Strips record raw `current()` — no velocity/volume gain — matching what
the old UI pass displayed per node.)

- [ ] **Step 3: Build + null check**

Run: `cmake --build C:/@dev/repos/mforce/build --config Release --target mforce_cli`
Then re-render `t1_piano.wav` as in Task 1 Step 5 and byte-compare:

```bash
C:/@dev/repos/mforce/build/tools/mforce_cli/Release/mforce_cli.exe patches/library/keys/acoustic_piano/piano_default.json "C:/Users/MATTFO~1/AppData/Local/Temp/claude/C---dev-repos-mforce/c3725ba1-1bfb-4ed7-bf2f-9a4a7bdf4ddd/scratchpad/t2_piano.wav" && cmp "C:/Users/MATTFO~1/AppData/Local/Temp/claude/C---dev-repos-mforce/c3725ba1-1bfb-4ed7-bf2f-9a4a7bdf4ddd/scratchpad/t1_piano.wav" "C:/Users/MATTFO~1/AppData/Local/Temp/claude/C---dev-repos-mforce/c3725ba1-1bfb-4ed7-bf2f-9a4a7bdf4ddd/scratchpad/t2_piano.wav"
```

Expected: identical (capture unregistered = inert).

- [ ] **Step 4: Commit**

`engine: offline per-node capture facility on PitchedInstrument`

---

### Task 3: UI — split serialize_patch_graph out of save_patch_graph

**Files:**
- Modify: `tools/mforce_ui/main.cpp:2742-3174` (`save_patch_graph`)

**Interfaces:**
- Produces: `static nlohmann::json serialize_patch_graph(bool tapOverride,
  std::unordered_map<int, std::string>* outIds)` — returns the patch JSON
  built from the live editor state; when `outIds` is non-null it receives
  the GraphNode.id (int) → serialized string id map (`nodeIds` in the
  existing body), so capture registration can never drift from what was
  serialized.
- Consumes: nothing new.

- [ ] **Step 1: Baseline for byte-identity**

```bash
C:/@dev/repos/mforce/build/tools/mforce_ui/Release/mforce_ui.exe --roundtrip patches/library/winds/oboe1.json "C:/Users/MATTFO~1/AppData/Local/Temp/claude/C---dev-repos-mforce/c3725ba1-1bfb-4ed7-bf2f-9a4a7bdf4ddd/scratchpad/rt_oboe_before.json"
```

- [ ] **Step 2: Refactor**

Change the signature at 2742 to
`static nlohmann::json serialize_patch_graph(bool tapOverride, std::unordered_map<int, std::string>* outIds)`
(drop the `path` parameter), and replace the tail (lines 3171-3174):

```cpp
    if (outIds) *outIds = nodeIds;
    return root;
}

static void save_patch_graph(const std::string& path, bool tapOverride = false) {
    nlohmann::json root = serialize_patch_graph(tapOverride, nullptr);
    std::ofstream f(path);
    f << root.dump(2);
}
```

Verify with grep that nothing else in the old body referenced `path`.

- [ ] **Step 3: Build + byte-identity**

Rebuild mforce_ui, then:

```bash
C:/@dev/repos/mforce/build/tools/mforce_ui/Release/mforce_ui.exe --roundtrip patches/library/winds/oboe1.json "C:/Users/MATTFO~1/AppData/Local/Temp/claude/C---dev-repos-mforce/c3725ba1-1bfb-4ed7-bf2f-9a4a7bdf4ddd/scratchpad/rt_oboe_after.json" && cmp "C:/Users/MATTFO~1/AppData/Local/Temp/claude/C---dev-repos-mforce/c3725ba1-1bfb-4ed7-bf2f-9a4a7bdf4ddd/scratchpad/rt_oboe_before.json" "C:/Users/MATTFO~1/AppData/Local/Temp/claude/C---dev-repos-mforce/c3725ba1-1bfb-4ed7-bf2f-9a4a7bdf4ddd/scratchpad/rt_oboe_after.json"
```

Expected: identical files.

- [ ] **Step 4: Commit**

`ui: serialize_patch_graph split from save (returns json + id map)`

---

### Task 4: UI — unified Generate + --gencheck harness; wire Note/Passage

**Files:**
- Modify: `tools/mforce_ui/main.cpp` — new `generate_unified` near
  `render_passage_output_authoritative` (line ~4139); rewire
  `transport_generate` Note case (8125-8149) and Passage case (8150-8169);
  new `--gencheck` headless mode replacing the Task-0 (2026-09-13
  hotfix session) `--gatecheck` block's neighborhood (insert after it,
  before `--roundtrip`).

**Interfaces:**
- Consumes: `serialize_patch_graph` (Task 3), `load_instrument_patch_json`
  (Task 1), `capture_begin/captureBuffers/capture_end` (Task 2),
  `write_wav_16le_stereo` (mforce/render/wav_writer.h, already included).
- Produces:
  `struct SchedNote { float noteNumber; float durationSeconds; float startSeconds; float velocity; };`
  `static bool generate_unified(const std::vector<SchedNote>& notes);`
  — later tasks (evo, chords) call exactly this.

- [ ] **Step 1: Write the harness FIRST (the failing test)**

New headless mode, inserted after the `--gatecheck` block:

```cpp
    // Headless unified-Generate check: render the patch's EMBEDDED score
    // through generate_unified, write the output buffer as WAV, print
    // per-node strip RMS. Acceptance for the render-capture unification
    // spec §6: output proportional to mforce_cli on the same patch, and
    // in-loop strips (NutDelay class) non-flat.
    if (argc >= 4 && std::string(argv[1]) == "--gencheck") {
        s_headless = true;
        ImGui::CreateContext();
        ImNodes::CreateContext();
        register_all_sources();
        try {
            load_graph_from_path(argv[2]);
            std::vector<SchedNote> notes;
            for (const auto& ev : s_loadedScore)
                notes.push_back({ev.value("note", 60.0f),
                                 ev.value("duration", 1.0f),
                                 ev.value("time", 0.0f),
                                 ev.value("velocity", 0.8f)});
            if (notes.empty()) { fprintf(stderr, "gencheck: no score\n"); return 1; }
            if (!generate_unified(notes)) {
                fprintf(stderr, "gencheck: generate_unified failed: %s\n",
                        g_transport.statusMsg);
                return 1;
            }
            std::vector<float> stereo(size_t(g_waveformSamples) * 2);
            for (int i = 0; i < g_waveformSamples; ++i)
                stereo[size_t(i)*2] = stereo[size_t(i)*2+1] = g_outputWaveform[size_t(i)];
            if (!write_wav_16le_stereo(argv[3], AUDIO_SAMPLE_RATE, stereo))
                { fprintf(stderr, "gencheck: wav write failed\n"); return 1; }
            for (auto& n : s_nodes) {
                if (n.waveformData.empty()) continue;
                double acc = 0.0;
                for (float s : n.waveformData) acc += double(s) * double(s);
                printf("strip %-24s rms %.6f\n", n.label.c_str(),
                       std::sqrt(acc / double(n.waveformData.size())));
            }
            printf("gencheck ok: %d frames\n", g_waveformSamples);
            return 0;
        } catch (const std::exception& e) {
            fprintf(stderr, "gencheck failed: %s\n", e.what());
            return 1;
        }
    }
```

(`s_loadedScore` may be a non-array when absent — guard with
`s_loadedScore.is_array()` before iterating if the compiler path allows
null; mirror how `apply_score_defaults` reads it.)

- [ ] **Step 2: Confirm it fails to compile** (SchedNote/generate_unified
undefined) — that is this task's "run test, expect fail".

- [ ] **Step 3: Implement generate_unified**

Above `render_passage_output_authoritative` (~line 4136):

```cpp
// ONE Generate path for patch mode (render-capture unification spec
// 2026-09-13): serialize the live editor graph, load a PitchedInstrument
// from it directly — no temp file — and render through the engine with
// per-node capture. Generate IS the CLI render path by construction;
// strips observe the clones that actually play, summed across voices at
// timeline offsets like the audio itself.
struct SchedNote {
    float noteNumber;
    float durationSeconds;
    float startSeconds;
    float velocity;
};

static bool generate_unified(const std::vector<SchedNote>& notes) {
    if (notes.empty()) return false;
    try {
        std::unordered_map<int, std::string> idMap;
        nlohmann::json root = serialize_patch_graph(false, &idMap);
        auto ip = load_instrument_patch_json(root.dump());
        auto* pitched = dynamic_cast<PitchedInstrument*>(ip.instrument.get());
        if (!pitched) {
            transport_set_status("Generate: patch did not load as a "
                                 "PitchedInstrument", true);
            return false;
        }

        // Capture set = every displayable node, by its serialized id.
        std::vector<GraphNode*> capNodes;
        std::vector<std::string> capIds;
        for (auto& n : s_nodes) {
            auto it = idMap.find(n.id);
            if (n.dspSource && !is_special_ui_type(n.typeName)
                && it != idMap.end()) {
                capNodes.push_back(&n);
                capIds.push_back(it->second);
            } else {
                n.waveformData.clear();
            }
        }

        float end = 0.0f;
        for (const auto& sn : notes)
            end = std::max(end, sn.startSeconds + sn.durationSeconds);
        int frames = int(end * float(ip.sampleRate));

        pitched->capture_begin(capIds, frames);
        for (const auto& sn : notes)
            pitched->play_note(sn.noteNumber, sn.velocity,
                               sn.durationSeconds, sn.startSeconds);

        buffer_playback_detach();   // it points into g_outputWaveform
        g_outputWaveform.assign(size_t(frames), 0.0f);
        g_waveformSamples = frames;
        RenderContext ctx{ip.sampleRate};
        ip.instrument->render(ctx, g_outputWaveform.data(), frames);

        for (size_t k = 0; k < capNodes.size(); ++k)
            capNodes[k]->waveformData = std::move(pitched->captureBuffers[k]);
        pitched->capture_end();

        wave_view_after_render(frames);
        compute_output_spectrum();
        return true;
    } catch (const std::exception& e) {
        char buf[256];
        snprintf(buf, sizeof(buf), "Generate failed: %s", e.what());
        transport_set_status(buf, true);
        return false;
    }
}
```

- [ ] **Step 4: Rewire transport_generate**

Note case (8125-8149) — replace the two render calls with:

```cpp
        case PlayMode::Note: {
            ValueSource* uiSrc = find_output_source();
            if (!transport_can_generate(uiSrc)) break;
            float noteNum = parse_note_input(g_transport.noteStr);
            std::vector<SchedNote> notes{
                {noteNum, g_transport.duration, 0.0f, g_transport.velocity}};
            if (generate_unified(notes)) {
                transport_set_status("Generated note", false);
                note_played(noteNum, g_transport.velocity);
                std::snprintf(g_noteGenSnap.noteStr, sizeof(g_noteGenSnap.noteStr),
                              "%s", g_transport.noteStr);
                g_noteGenSnap.velocity = g_transport.velocity;
                g_noteGenSnap.duration = g_transport.duration;
                g_noteGenSnap.valid = true;
            }
            break;
        }
```

Passage case (8150-8169) — replace the two render calls with a sequential
SchedNote conversion:

```cpp
                    std::vector<SchedNote> sched;
                    float cursor = 0.0f;
                    for (const auto& pn : notes) {
                        sched.push_back({pn.noteNumber, pn.durationSeconds,
                                         cursor, g_transport.velocity});
                        cursor += pn.durationSeconds;
                    }
                    if (generate_unified(sched)) {
                        char buf[128];
                        snprintf(buf, sizeof(buf), "Generated %d notes",
                                 (int)notes.size());
                        transport_set_status(buf, false);
                    }
```

Do NOT delete `render_waveforms` / `render_passage_waveforms` /
`render_*_authoritative` yet — Task 6 removes them once evo snapshots
(Task 5) no longer need the old pass.

- [ ] **Step 5: Build, run the harness, verify strip truth**

```bash
cmake --build C:/@dev/repos/mforce/build --config Release --target mforce_ui
```

```bash
C:/@dev/repos/mforce/build/tools/mforce_ui/Release/mforce_ui.exe --gencheck patches/audition/string_harness2/str2_g32_b36_h05.json "C:/Users/MATTFO~1/AppData/Local/Temp/claude/C---dev-repos-mforce/c3725ba1-1bfb-4ed7-bf2f-9a4a7bdf4ddd/scratchpad/gen_str2.wav"
```

Expected: `gencheck ok`, and the `NutDelay` strip rms is **non-zero**
(the exact strip that is silently flat today — spec §6.3).

- [ ] **Step 6: CLI proportionality (spec §6.2)**

```bash
C:/@dev/repos/mforce/build/tools/mforce_cli/Release/mforce_cli.exe patches/audition/string_harness2/str2_g32_b36_h05.json "C:/Users/MATTFO~1/AppData/Local/Temp/claude/C---dev-repos-mforce/c3725ba1-1bfb-4ed7-bf2f-9a4a7bdf4ddd/scratchpad/cli_str2.wav"
```

Then compare with a scratchpad script (write it as
`<scratchpad>/proportional.py`, run with `python`):

```python
import struct, sys, wave
def read(p):
    w = wave.open(p); n, ch = w.getnframes(), w.getnchannels()
    d = struct.unpack("<%dh" % (n*ch), w.readframes(n)); w.close()
    return [d[i*ch] for i in range(n)]              # left channel
a, b = read(sys.argv[1]), read(sys.argv[2])
n = min(len(a), len(b))
pairs = [(x, y) for x, y in zip(a[:n], b[:n]) if abs(x) > 300]
k = sum(y/x for x, y in pairs) / len(pairs)
worst = max(abs(y - k*x) for x, y in pairs)
print(f"gain ratio {k:.4f}, worst abs dev {worst:.1f} LSB, n {len(pairs)}")
```

Expected: a stable ratio with worst deviation of a few LSB (16-bit
quantization + mixer gain staging). A structural mismatch (deviation in
the thousands) fails the task.

- [ ] **Step 7: Commit**

`ui: unified Generate — engine render with per-node capture (+ --gencheck)`

---

### Task 5: UI — evo snapshots derived from captured strips

**Files:**
- Modify: `tools/mforce_ui/main.cpp` — `generate_unified` (Task 4);
  helper near `find_source_node` (line 2499).

**Interfaces:**
- Consumes: `generate_unified` internals; `g_evoSnapshots`, `EVO_SNAP_COUNT`,
  `is_partials_type`, `is_formant_type`, `find_source_node` — all existing.
- Produces: `static std::pair<GraphNode*, std::string> find_source_pin(int
  inputPinId)` — source node AND the name of its output pin feeding this
  input (needed to resolve which perform FIELD feeds a pin).

**Why:** today `render_waveforms`/`render_passage_waveforms` fill
`g_evoSnapshots` (Partials/Formant input-pin scrubber data) during their
loop (main.cpp:4373-4409). Once those die, snapshots must come from the
capture pass: stride-sample the SOURCE node's captured strip. A pin fed by
a Perform face needs the face's per-field file node captured too — those
ids live in `GraphNode::perfFieldIds`.

- [ ] **Step 1: Add find_source_pin**

Next to `find_source_node` (2499), same s_links scan but returning the
output pin's name:

```cpp
static std::pair<GraphNode*, std::string> find_source_pin(int inputPinId) {
    for (auto& link : s_links) {
        int other = link.endPinId == inputPinId ? link.startPinId
                  : link.startPinId == inputPinId ? link.endPinId : -1;
        if (other < 0) continue;
        for (auto& node : s_nodes)
            for (auto& pin : node.outputs)
                if (pin.id == other) return {&node, pin.name};
    }
    return {nullptr, {}};
}
```

- [ ] **Step 2: Capture perform field nodes too**

In `generate_unified`, when building the capture set, append after the
displayable-node loop:

```cpp
        // Perform FIELD nodes: captured so evo snapshots can show
        // keytracked pins (frequency into Partials). Buffers land in a
        // side map, not in a GraphNode strip.
        std::vector<std::string> perfIds;
        for (auto& n : s_nodes)
            if (n.typeName == NT_PERFORM)
                for (auto& [field, fid] : n.perfFieldIds)
                    perfIds.push_back(fid);
        size_t nStrips = capIds.size();
        capIds.insert(capIds.end(), perfIds.begin(), perfIds.end());
```

and after moving the strip buffers out, build the side map before
`capture_end()`:

```cpp
        std::unordered_map<std::string, std::vector<float>> perfStrips;
        for (size_t k = 0; k < perfIds.size(); ++k)
            perfStrips[perfIds[k]] =
                std::move(pitched->captureBuffers[nStrips + k]);
```

- [ ] **Step 3: Derive the snapshots**

Still inside `generate_unified`, after strips are in place (before
`wave_view_after_render`):

```cpp
        g_evoSnapshots.clear();
        int snapStride = std::max(1, frames / EVO_SNAP_COUNT);
        for (auto& n : s_nodes) {
            if (!is_partials_type(n.typeName) && !is_formant_type(n.typeName))
                continue;
            auto& byPin = g_evoSnapshots[n.id];
            for (auto& pin : n.inputs) {
                auto [sn, srcPinName] = find_source_pin(pin.id);
                if (!sn) continue;
                const std::vector<float>* buf = nullptr;
                if (sn->typeName == NT_PERFORM) {
                    auto fit = sn->perfFieldIds.find(srcPinName);
                    if (fit != sn->perfFieldIds.end()) {
                        auto pit = perfStrips.find(fit->second);
                        if (pit != perfStrips.end()) buf = &pit->second;
                    }
                } else if (!sn->waveformData.empty()) {
                    buf = &sn->waveformData;
                }
                if (!buf) continue;
                auto& vec = byPin[pin.name];
                vec.assign(EVO_SNAP_COUNT, 0.0f);
                for (int s = 0; s < EVO_SNAP_COUNT; ++s) {
                    int f = s * snapStride;
                    if (f < int(buf->size())) vec[size_t(s)] = (*buf)[size_t(f)];
                }
            }
        }
```

- [ ] **Step 4: Build + verify on a Partials patch**

Rebuild mforce_ui; run `--gencheck` on an additive/Partials library patch
(e.g. `patches/library/strings/viola_default.json`) — expected:
`gencheck ok` and no crash; snapshot correctness is a hands check in
Task 7 (scrubber shows evolving values on a formant patch).

- [ ] **Step 5: Commit**

`ui: evo snapshots derived from capture strips (+ find_source_pin)`

---

### Task 6: UI — delete the old passes; convert Chords; drop temp-file loads

**Files:**
- Modify: `tools/mforce_ui/main.cpp` — delete `render_waveforms`
  (4339-4416), `render_output_authoritative` (4192-...),
  `render_passage_output_authoritative` (4139-4184),
  `render_passage_waveforms` (7840-7889 DSP pass); convert
  `render_chords_waveforams`'s loader call; convert the instrument cache
  build (`get_cached_instrument`, ~4457-4461) to
  `load_instrument_patch_json`.

**Interfaces:**
- Consumes: `generate_unified` (Task 4), `serialize_patch_graph` (Task 3),
  `load_instrument_patch_json` (Task 1).
- Produces: nothing new — deletions and rewires.

- [ ] **Step 1: Delete the four dead renderers**

Remove the four functions and grep for remaining references:
`render_waveforms(`, `render_passage_waveforms(`,
`render_output_authoritative(`, `render_passage_output_authoritative(`.
Every caller must already be on `generate_unified` (Tasks 4-5) — any
straggler (e.g. a Listen-tap or chords path calling render_waveforms) is
rewired in this step; if a straggler needs single-note strips, call
`generate_unified({{note, dur, 0.0f, vel}})`.

- [ ] **Step 2: Chords through the same loader (captures come free)**

In `render_chords_waveforms` (line ~7893): replace
`auto ip = load_instrument_patch(get_playback_patch_path());` with

```cpp
        nlohmann::json root = serialize_patch_graph(false, nullptr);
        auto ip = load_instrument_patch_json(root.dump());
```

(Chords render via Conductor → `play_note`, so per-node strips would
capture if registered; wiring chord strips is NOT in scope — the change
here is only killing the temp-file hop. Strip capture for chords rides
the same call whenever wanted.)

- [ ] **Step 3: Instrument cache without the temp file**

In `get_cached_instrument`'s rebuild (the
`load_instrument_patch(path, LIVE_MIN_POLYPHONY)` call at ~4461): when the
rebuild source is the editor's serialized state (the current temp-file
mechanism — follow `get_playback_patch_path()` to where it writes the temp
file), serialize in memory instead:

```cpp
        nlohmann::json root = serialize_patch_graph(false, nullptr);
        cached = std::make_shared<CachedInstrument>(
            load_instrument_patch_json(root.dump(), LIVE_MIN_POLYPHONY));
```

Keep the disk path for the not-dirty case if the cache currently loads
the saved file directly. If `get_playback_patch_path`'s temp file has NO
remaining consumers after this, delete the temp-file writer too;
otherwise leave it (Listen tap is explicitly out of scope, spec §4c).

- [ ] **Step 4: Build + full headless suite**

Rebuild; run `--gatecheck` (str2 → `envelopes=2`), `--roundtrip`
byte-identity (oboe1), `--gencheck` (str2 → NutDelay non-flat, and
piano_default → `gencheck ok`).

- [ ] **Step 5: Commit**

`ui: one Generate path — old UI-pass/authoritative renderers deleted`

---

### Task 7: Validation batch — null gate + hands checklist

**Files:** none (validation only; REVIEW/STATUS notes as usual).

- [ ] **Step 1: Null corpus gate** (once, over the whole batch, per the
standing rule): render every patch in `patches/library/` with the current
`mforce_cli` and compare peak/rms against the pre-batch values (the Task 1
/ Task 2 scratchpad WAVs cover piano; for the rest, render at the batch
start if not already done — engine changes in Tasks 1-2 are the only
engine-touching commits and both are designed inert; any sample delta
fails the batch).

- [ ] **Step 2: --gencheck proportionality on three families**:
piano_default (baseline), str2_g32_b36_h05 (tap-only tail),
oboe1 (Wormhole family) — the Task 4 Step 6 script, all three expected
proportional.

- [ ] **Step 3: Hands checklist for Matt** (report, don't wait):
Generate/Play A/B in Note + Passage modes on the three patches above;
node strips now live for string-family patches (NutDelay etc.); evo
scrubber on a formant patch; Play latency gone; live keyboard unchanged.

---

## Self-Review

- Spec §3a-c → Tasks 1-2; §3d honored in capture_begin skip semantics
  (+ Matt's 2026-09-13 relaxation recorded in Global Constraints).
- Spec §4a → Task 3; §4b → Tasks 4-5; §4c → Task 6; §4d untouched
  (grep-verified: no task edits play_note_held/play_continuous/voice mixer).
- Spec §6.1 → Tasks 1/6; §6.2 → Task 4 Step 6 + Task 7; §6.3 → Task 4
  Step 5; §6.4 → Task 7 Step 3.
- Type check: `SchedNote{noteNumber, durationSeconds, startSeconds,
  velocity}` used identically in Tasks 4 and 6; `capture_begin(ids,
  timelineFrames)` / public `captureBuffers` / `capture_end()` names match
  across Tasks 2, 4, 5; `serialize_patch_graph(bool, map*)` matches across
  Tasks 3, 4, 6; `nodesById` matches Tasks 1-2.
- Known open risk (accepted): score events with explicit `time` gaps render
  correctly via SchedNote.startSeconds; overlapping score events sum — same
  as CLI. `--gencheck` on patches whose embedded score overlaps notes is
  still proportional because both paths use play_note accumulation.
