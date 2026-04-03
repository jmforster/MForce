# Plan: Expand UI to support all legacy features

# Background

The legacy MForce implementation relies on Unity, as well as 3rd party extensions:
 - Vectrosity for waveform display
 - XNode for node graph editing support
 - Odin Inspector for node features like tables
 - Some collection of button icons for the toolbar (can't recall name, not important)

Key components of the Unity UI are:
- Project pane, where assets are dropped
- Hierarchy pane, where a tree of GameObjects is built
- Inspector pane, where the properties of each GameObject are shown and can be edited
- Play mode window - not a pane in the design UI, main game window that appears when you click Play

Unity organizes games by Scenes which may be saved and loaded. Each Scene has a Hierarchy of game objects.

Legacy capabilities, and how each is supported:

1. Node editing - done in the Unity Project pane - covered already
2. Connection of dsp sources to Mixer - done in Unity Hierarchy pane
   - user adds nodes to hierarchy eg
      Mixer
        Channel
          SoundSource OR
      Mixer
        Instrument
3. Connection of dsp sources to Mixer node graph to dsp
 - done in Inspector of AudioAdapter, under Controller
 - drag graph from Project pane and drop in Inspector
4. Specification of:
    - Note / duration
    OR 
    - Passage / Octave / Bpm
    OR
    - Chords / Def_Chord_Grp / Figure / Inversion / Spread / Chord_Delay
    OR
    - Drums / Pattern / Repeats
    OR
    - Piece / Key_Name / Meter / Composer (barely implemented)
    All of above done in inspector pane of Controller (SoundController.cs) (and what gets played is determined by which field is populated)
5. Specification of filename for WAV that will be saved after Generate, Save buttons pressed
    - Also done in Controller (SoundController.cs) inspector 
6. Specification of:
    - height, width, and column count for tiled waveform displays
    - whether Mixer, Channels, Ramps, and/or SimpleValueSource should be displayed
    Done in inspector pane of Display (VMeterDisplay.cs) node

All remaining actions done in in Play mode, from the button panel:
 - Generate button - renders samples and fires waveform displays
 - Play button - starts and stops continuous realtime streaming
 - Zoom in / out, Scroll left / right buttons - controls waveform displays
 - Save button - saves WAV file with name from SoundController.Filename
 - NoteMode button - enters/exits "note mode" where the QWERTY keys trigger notes

# Reference

Legacy classes to read and understand before proceeding:

(all in mforce-unity unless otherwise noted)

SoundController (MForce/Unity)
VMeterDisplay (MForce/Unity/UI)
ValueCacheManager (non-Unity class, so in mforce-legacy, MForce/Sound

# Mission

Our goal is to replicate the capabilities of the legacy code in our standalone C++ UI, but definitely not the look and feel.
The new layout should follow UI/UX best practices for a modern cross-platform UI (but limited to Windows initially), and prioritize ease of use followed by visual slickness.

# First step
Propose a plan for a UI that will support all of the above
Think about what the user is trying to accomplish in the most efficient way possible
If there are additional useful actions not covered by the above, propose them
If any actions seem unnecessary or not correctly conceived, advise
Decide 
 - what widgets are most appropriate for the various user inputs, actions, displays
 - what windows/panels they should show up in and how they should be arranged
 - how the various windows/panels will interact, both visually (docking, floating) and re: passing data around

# Out of scope for this round
The composition UI, which I envision as a DAW-like timeline.


