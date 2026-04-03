# MForce UI Plan

## Overview

The UI replaces Unity's multi-pane editor with a single standalone ImGui application using the docking branch for flexible window management. Every capability from legacy is covered, reorganized around what the user is actually doing rather than mimicking Unity's generic structure.

## Windows

### 1. Node Editor (center, primary workspace)
**What it is:** The imnodes graph editor we already have.

**What's new:**
- Nothing structurally new — it's working. Becomes a dockable window instead of a fixed child region, which fixes the current scrolling/sizing issues.

**Class:** No new class — remains inline in main.cpp render loop. The `GraphNode`, `Pin`, `Link` structs stay as-is.

### 2. Waveform Display (default: docked bottom)
**What it is:** The per-node waveform viewer we just built.

**What's new:**
- Becomes its own dockable window (`WaveformWindow`)
- Filter checkboxes at top: show/hide by category (Oscillators, Envelopes, Modulators, etc.) — replaces legacy's Mixer/Channels/Ramps/Simple toggles
- Column count selector (1, 2, 3 columns for tiled layout vs. stacked) — from legacy VMeterDisplay.Columns
- Click on a waveform strip to select that node in the node editor

**Rendering:** Same `draw_waveform()` function, just called from its own window context.

### 3. Transport / Playback Panel (default: docked top, toolbar-style)
**What it is:** Replaces legacy's button panel + SoundController inspector fields.

**Contains:**
- **Play mode selector** (radio buttons or dropdown):
  - `Note` — single note (note number + duration fields)
  - `Passage` — text string of notes (passage string + octave + BPM fields)
  - `Chords` — chord sequence (chord string + default chord group + figure + inversion + spread + delay fields)
  - `Drums` — drum pattern (pattern name + repeats + BPM)
  - Fields for the selected mode appear/disappear dynamically
- **Generate** button — offline render, populates waveforms, does NOT play audio
- **Play** / **Stop** toggle — streams the generated audio
- **Note Mode** toggle — enables QWERTY keyboard for live note triggering
- **Save WAV** button + filename field
- **BPM** field (shared across modes that need it)

**Why combined:** In legacy these were split across SoundController inspector fields (data) and the button panel (actions). Combining them puts everything the user needs for "render and hear something" in one place.

**Class:** `TransportPanel` — struct with all the fields + render method.

### 4. Properties Panel (default: docked right)
**What it is:** Shows detailed properties of the selected node. Replaces clicking through nodes in the graph.

**Contains:**
- Node type and label (editable)
- All param pins with current values (mirrors what's on the node, but with more room for labels and wider sliders)
- All config values (same as on-node rendering but with more space)
- Inline tables (FormantSpectrum rows, etc.) with more editing room
- For Instrument patches: polyphony, paramMap summary

**Why:** The on-node UI is cramped. Having a properties panel lets the user see and edit everything about a node without squinting at tiny sliders in the graph. The on-node sliders still work for quick tweaks; the panel is for detailed editing.

**Class:** `PropertiesPanel` — renders based on selected node.

### 5. Keyboard (default: docked bottom or floating)
**What it is:** Visual piano keyboard for triggering notes. Replaces legacy LBKeyboard + QWERTY mapping.

**Contains:**
- 2-3 octave piano keyboard rendered with ImGui
- Click to play notes
- QWERTY key overlay (when Note Mode is active, keyboard keys map to piano keys)
- Velocity from vertical click position on each key (top = soft, bottom = loud)
- Current instrument/channel selector (for mixer setups)
- Sustain toggle

**Class:** `KeyboardPanel` — renders piano keys, handles input, calls `play_note()`.

## Window Layout

Default arrangement (user can redock/float anything):

```
+-----------------------------------------------+
|  Transport Panel (toolbar)                     |
+-------------------+---------------------------+
|                   |                           |
|   Node Editor     |   Properties Panel        |
|   (center)        |   (right sidebar)         |
|                   |                           |
+-------------------+---------------------------+
|  Waveform Display  |  Keyboard                |
|  (bottom left)     |  (bottom right)          |
+---------+---------+---------------------------+
```

## Interaction Flow

### Typical workflow:
1. User builds DSP graph in Node Editor
2. Sets note/passage/chord in Transport Panel
3. Clicks **Generate** — offline render fills waveform display
4. Clicks **Play** — hears the result
5. Tweaks parameters in Node Editor or Properties Panel
6. Clicks **Generate** again to see updated waveforms
7. Enables **Note Mode** — plays notes via Keyboard or QWERTY keys, hears results in real-time

### Data flow:
- Transport Panel → calls `render_waveforms()` (Generate) and audio streaming (Play)
- Node Editor → edits graph, triggers `update_all_dsp()`
- Properties Panel → reads/writes selected node's params/configs
- Waveform Display → reads `GraphNode::waveformData` (populated by Generate)
- Keyboard → calls `play_note()` with note number from key position

## Implementation Order

### Phase 1: ImGui Docking
- Switch to ImGui docking branch
- Convert existing monolithic window into dockable windows: Node Editor, Waveform Display
- Remove the BeginChild splitting hack
- Verify scrolling/sizing works correctly

### Phase 2: Transport Panel
- `TransportPanel` struct with fields for each play mode
- Render as toolbar window
- Generate button triggers `render_waveforms()` with the appropriate parameters
- Play/Stop streams audio
- Note entry: note number or note name (C4, D#3, etc.) + duration
- Passage, Chord, Drum string parsing — port from SoundController.cs

### Phase 3: Properties Panel
- `PropertiesPanel` renders based on `selected_node` (set by clicking in node editor)
- Shows all param descriptors, input descriptors, config descriptors with full labels
- For FormantSpectrum: shows the inline table with wider columns
- For Parameter nodes: editable name
- For PatchOutput: polyphony

### Phase 4: Keyboard
- `KeyboardPanel` renders piano keys via ImDrawList
- Maps QWERTY keys to notes (Z=C3, S=C#3, X=D3, ... Q=C5 row)
- Velocity from click Y position
- Note Mode toggle in Transport Panel enables/disables keyboard input

### Phase 5: Waveform Display Improvements
- Filter toggles by SourceCategory
- Column layout selector (tiled vs stacked)
- Click-to-select node
- Zoom to fit button

## Classes Summary

| Class | Location | Purpose |
|-------|----------|---------|
| `TransportPanel` | main.cpp (or transport_panel.h) | Play mode selection, generate, play, save |
| `PropertiesPanel` | main.cpp (or properties_panel.h) | Selected node detail editing |
| `KeyboardPanel` | main.cpp (or keyboard_panel.h) | Piano keyboard, QWERTY mapping, note triggering |
| `WaveformWindow` | main.cpp | Waveform display with filters and layout options |

These are lightweight structs with a `render()` method, not full widget classes. They read/write the existing global graph state (`s_nodes`, `s_links`, etc.).

## What's NOT a separate window

- **File operations** (New, Open, Save) — stay in the menu bar
- **Node creation** — stays as right-click context menu in Node Editor
- **Link management** — stays as drag-and-drop in Node Editor

## Additional capabilities not in legacy

- **Undo/Redo** — not yet, but the architecture supports it (command pattern on graph edits). Flagged for future.
- **Preset browser** — browse/load/save patch files from a panel. Future.
- **Spectrum analyzer** — FFT of the output displayed alongside waveforms. Legacy had CalcFFT commented out. Future.

## Unnecessary legacy items

- **Roll button** — was empty/unused in legacy (`VMeter.Roll()` had no implementation). Skip.
- **Record toggle** — was tied to Unity's audio system. Our save-to-WAV covers this. Skip.
- **NextInstrument / SelectInstrument** — for cycling channels in a mixer. In our UI, you just click the channel node. Skip the buttons, the graph IS the selector.
- **TimeSyncWrapper** — Unity-specific audio timing. Our poll-driven audio doesn't need it. Skip until we move to callback-driven audio.
