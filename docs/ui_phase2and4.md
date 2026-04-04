# UI Phase 2 & 4: Transport Panel + Keyboard

Phases 2 (Transport) and 4 (Keyboard) from ui_plan.md. Independent of each other — can be implemented in parallel.

---

## Phase 2: Transport Panel

### What exists today
- Menu bar has Play/Stop items calling `play_note(60.0f, 0.8f, 2.0f)` and `play_continuous(0.5f)` with hardcoded values
- `render_waveforms(noteNum, velocity, duration)` renders the graph and populates waveform display
- `play_note()` calls `render_waveforms()` then streams
- `play_continuous()` streams indefinitely
- `stop_playback()` nulls `g_streamSource`

### Goal
A dockable toolbar window with mode-specific fields and action buttons, replacing the hardcoded menu bar items.

### Transport state (new globals in main.cpp)

```cpp
enum class PlayMode { Note, Passage, Chords, Drums };

struct TransportState {
    PlayMode mode = PlayMode::Note;

    // Note mode
    char noteStr[16] = "C4";       // note name or MIDI number
    float velocity = 0.8f;
    float duration = 2.0f;

    // Passage mode
    char passageStr[256] = "";
    int octave = 4;
    float bpm = 120.0f;

    // Chords mode
    char chordsStr[256] = "";
    char defChordGrp[64] = "";
    char figure[64] = "";
    int inversion = 0;
    int spread = 0;
    float chordDelay = 0.0f;

    // Drums mode
    char pattern[64] = "";
    int repeats = 2;

    // Shared
    char filename[256] = "output.wav";
    bool noteMode = false;
};
static TransportState g_transport;
```

### UI layout

```
+------------------------------------------------------------------+
| Mode: [Note|Passage|Chords|Drums]   [Generate] [>Play] [NoteMode]|
| (mode-specific fields below)        [Save WAV] [filename.wav    ]|
+------------------------------------------------------------------+
```

- **Mode selector**: radio buttons or tab bar (ImGui::TabBar is compact)
- **Mode-specific fields**: appear/disappear based on selection
  - **Note**: note name/number input + velocity slider + duration input
  - **Passage**: passage string (multiline?), octave spinner, BPM
  - **Chords**: chords string, default chord group, figure, inversion, spread, delay
  - **Drums**: pattern name, repeats, BPM (shared)
- **Action buttons** (always visible):
  - **Generate** — offline render, populates waveforms, no audio
  - **Play / Stop** toggle — streams the last generated audio (or generates first if needed)
  - **Note Mode** toggle — highlighted when active, enables keyboard input
  - **Save WAV** — saves last render + filename field

### Parsing utilities (new header: `engine/include/mforce/music/parse_util.h`)

Keep string-parsing logic out of core classes. **Most of this code already exists** in `tools/mforce_cli/main.cpp` — the task is to extract it into a shared header, not rewrite it.

#### Already written (extract from `mforce_cli/main.cpp`):
- `parse_duration(durStr)` — duration char (`t/s/e/q/h/w/d/f`) → beats float (line 317)
- `map_chord_group(grp)` — shorthand expansion `g4`→`Guitar-Bar-4` etc. (line 334)
- `ParsedChord` struct (line 341)
- `parse_chord_token(token, octave, dictName, durationBeats)` — `"C:M"` → `Chord` (line 23)
- `parse_chord_string(input, startOctave, defaultDict, figurePrefix)` — full chord string → `vector<ParsedChord>` (line 346)

After extraction, update `mforce_cli/main.cpp` to `#include` the shared header and remove the local copies.

#### New code needed:

**`parse_note_input(const char* str) -> float`**
- Accepts MIDI number ("60") or note name ("C4", "D#3", "Eb5")
- Returns MIDI note number as float
- Used by Note mode and Keyboard panel

**`parse_passage(str, octave, bpm) -> vector<ParsedNote>`**

Ports `SoundController.GeneratePassage()` parsing logic. The passage string is a sequence of 3-character commands:
- `"C "` through `"B "` — note letter (using NoteString `"CdDeEFgGaAbB"` for chromatic indexing)
- Followed by duration char (reuses `parse_duration()`)
- `"O+ "` / `"O- "` — octave up/down

Returns a `vector<ParsedNote>` where each has noteNumber and duration in seconds (`beats * 60 / bpm`).

### Generate logic (mirrors legacy SoundController.Generate)

```
switch (g_transport.mode) {
    case Note:    generate_single_note();  break;   // existing render_waveforms()
    case Passage: generate_passage();      break;   // PassageUtil::parse → render sequence
    case Chords:  generate_chords();       break;   // ChordUtil::parse → render via ChordPerformer
    case Drums:   generate_drums();        break;   // stub — deferred
}
```

**Note** — works now via `render_waveforms()`.

**Passage** — `PassageUtil::parse()` produces `vector<MNote>`. Each note rendered sequentially through the DSP graph (same as legacy `GenerateNotes`): for each note, set frequency on the instrument, render `duration * sampleRate` samples, concatenate.

**Chords** — `parse_chord_string()` (already written, just extracted) produces `vector<ParsedChord>`. Rendering via `Conductor::perform()` + `PitchedInstrument` — same flow as `run_josie()` / `run_chords()` in CLI, just wired to the UI graph instead of a CLI-loaded patch.

**Drums** — stub, logs "not yet implemented".

### Dock layout change

Add Transport to the dock builder:
- Split a thin strip off the top of dockCenter for "Transport"
- Or: use `ImGuiWindowFlags_NoTitleBar` + fixed height for a toolbar feel

### Implementation steps

1. Extract `parse_duration`, `map_chord_group`, `ParsedChord`, `parse_chord_token`, `parse_chord_string` from `tools/mforce_cli/main.cpp` into `engine/include/mforce/music/parse_util.h`. Add new `parse_note_input()` and `parse_passage()`. Update CLI to `#include` the shared header.
2. Add `TransportState` struct and `g_transport` global
3. Write `draw_transport_panel()`:
   - TabBar for mode selection
   - Per-mode field rendering
   - Action buttons
4. Refactor `play_note()` / `play_continuous()` to read from `g_transport` instead of hardcoded values
5. Add "Transport" to dock layout (top strip)
6. Remove hardcoded play/stop from menu bar (keep as shortcuts only)
7. Wire Generate:
   - Note → existing `render_waveforms()` with parsed note input
   - Passage → `PassageUtil::parse()` then render note sequence
   - Chords → `ChordUtil::parse()` then render chord sequence (via PitchedInstrument or ChordPerformer)
   - Drums → stub
8. Wire Save → `wav_writer` with `g_transport.filename`

### Files touched
- `engine/include/mforce/music/parse_util.h` — new, passage/chord string parsing
- `tools/mforce_ui/main.cpp` — transport UI and generate wiring

---

## Phase 4: Keyboard Panel

### What exists today
Nothing — no keyboard UI, no QWERTY note triggering.

### Goal
A dockable piano keyboard window. Click or press QWERTY keys to play notes in real time.

### Legacy mapping (from LBKeyboard.cs)

QWERTY keys map to chromatic notes across 2 octaves:
```
q=C  2=C#  w=D  3=D#  e=E  r=F  5=F#  t=G  6=G#  y=A  7=A#  u=B
i=C  9=C#  o=D  0=D#  p=E  [=F  ==F#  ]=G
```

Action keys:
```
g=octave down  h=octave up  v=halve duration  b=double duration
```

### Keyboard state

```cpp
struct KeyboardState {
    int octave = 4;
    float duration = 0.5f;
    float velocity = 0.8f;
    bool sustain = false;
    int activeNote = -1;     // currently sounding note (-1 = none)
};
static KeyboardState g_keyboard;
```

### Visual piano rendering

Using ImDrawList to draw 2-3 octaves of piano keys:
- White keys: rectangles with note labels
- Black keys: narrower, raised rectangles overlapping white keys
- Highlight currently-playing key
- QWERTY overlay text on each key (when Note Mode is active)

Layout: keys sized to fill available width, maintain standard piano proportions (white key ~2.5:1 aspect).

### Click interaction
- Click on a key → `play_note(noteNum, velocity, duration)`
- Velocity from Y position within key: top = soft (0.3), bottom = loud (1.0)
- Mouse-down highlights key, mouse-up releases

### QWERTY interaction (only when Note Mode active from Transport)
- Map ImGuiKey values to note offsets using the legacy mapping table
- On key press: compute `absNote = (octave + 1) * 12 + offset`, call `play_note()`
- On key release: stop note (if we implement note-off, otherwise let duration expire)
- g/h keys adjust octave, v/b adjust duration — show current values in panel header

### Panel header bar

```
Oct: [<] 4 [>]  Dur: [<] 0.5s [>]  Vel: [----*---]  [Sustain]
+---------------------------------------------------------------+
| [Q]  [2]  [W]  [3]  [E]  [R]  [5]  [T]  [6]  [Y]  [7]  [U] |
|  C    C#   D    D#   E    F    F#   G    G#   A    A#   B     |
+---------------------------------------------------------------+
```

### Interaction with Transport
- Keyboard only processes QWERTY input when `g_transport.noteMode == true`
- Visual piano click always works (it's explicit mouse interaction, not ambiguous)
- When Note Mode is toggled ON in Transport, the keyboard panel auto-focuses

### Implementation steps

1. Add `KeyboardState` struct and `g_keyboard` global
2. Build QWERTY-to-note mapping table (static array)
3. Write `draw_keyboard_panel()`:
   - Header: octave/duration/velocity controls
   - Piano keys via ImDrawList
   - Click detection per key (hit-test white keys first, then black keys on top)
4. Wire click → `play_note()` with velocity from Y position
5. Wire QWERTY input (gated by `g_transport.noteMode`)
6. Add "Keyboard" to dock layout (bottom-right, sharing space with Waveforms)
7. Highlight active key during playback

### Files touched
- `tools/mforce_ui/main.cpp` — all changes here for now

---

## Parallelization plan

These two are independent:
- **Transport** touches: menu bar, dock layout (top strip), new draw function, refactored play/generate calls
- **Keyboard** touches: dock layout (bottom-right split), new draw function, new input handling

Shared touchpoint: both call `play_note()`. Transport refactors its signature; Keyboard calls it. Resolve by doing Transport first for the `play_note` refactor, then Keyboard uses the new signature. OR define the new `play_note(float noteNum, float vel, float dur)` signature upfront (it already matches!) and both can proceed.

Since `play_note()` already takes `(float, float, float)` and won't change signature, true parallel implementation is safe.

### Order of integration
1. Implement both in parallel (worktree isolation)
2. Merge Transport first (it modifies menu bar / dock layout)
3. Merge Keyboard second (additive — new window + new input handling)
4. Wire `g_transport.noteMode` to gate keyboard QWERTY input

---

## What's deferred

- **Drums generation** — needs drum pattern definitions and DrumPlayer port
- Articulation cycling — low priority, add to keyboard later
- Multi-instrument selection — not needed until mixer has multiple instruments
- Sustain pedal — cosmetic, add later
