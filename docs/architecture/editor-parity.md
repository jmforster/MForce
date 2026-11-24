# The editor: what "done" is measured against

Matt, 2026-11-23: "I want the refactor to be entirely done before we resume
adding features", and done means "when all the capability that existed
previously is restored". This is the list. Part B is everything a person
could do with the old editor, taken from its last version
(`9f084c9^:tools/mforce_ui/main.cpp`, 13,506 lines, read whole by one agent
and cross-checked with searches), one line each with the old file's line
range and where it stands now. Part A is what I made of it: the order of
the slices that remain, what is not coming back and why, and what the
earlier slice table had missed. **Strike or add a line and the definition
of done moves with it.**

The numbers: 229 things a user could do; 14 headless modes; 17 dead or
never-working things. Of the 229, slice 1 covers 65, the five slices to
come 153, and 11 are not coming back (A2). The line counts in A1 are the
old file's lines behind each slice's capabilities, as a measure of size.

## A1. The slices from here

The order differs from the design's table in one place: Generate comes
before See. The old editor's waveform strips and spectrum were drawn from
the last render, never from live play, so there is nothing to see until
Generate exists.

| Slice | Restores | Capabilities, and the old code behind them | Engine work it needs |
|---|---|---|---|
| 1. Open, play, tweak, hear, save | DONE 2026-11-23 | 65 by the first count; checked since (A5): 48 done whole, 8 in part, 2 no longer apply, 7 were not done | `patch/edit.h`, `LivePlayer::set_value` |
| 2. Build | a new patch; the create menu (families, the Experimental shelf, Note, Output); delete; wire, unwire, taps, the wiring refusals; the note face as one node; dynamic pins (promote, demote, feed, unfeed); rename; the instrument's polyphony, sustaining, onsets and glide; canvas fit and pan keys; the status bar's counts | 41, and from slice 1's area the 7 not done and the open parts of the 8 (A5); about 1,450 lines | the rest of the edits in `patch/edit.h` |
| 3. Generate | the transport's four tabs (Note, Passage, Chords, Drums); Generate, Play, Stop; Save WAV; passage and chord files; the Live checkbox and holding several keys; a plain sound's sound; continuous play | 26; about 1,240 lines | the live player holds more than one line; a plain sound played live |
| 4. See | the waveform strips (output and every node) with zoom, scroll, columns and pop-outs; the spectrum; the Listen tap; the audio device's watchdog and restart | 15; about 790 lines | a node's signal read out of a render (exists) and out of a live voice (Listen) |
| 5. Arrange | groups (make, open, add to, remove from, rename, ungroup, the breadcrumb); wormholes; copy, cut, paste, duplicate, replace-with; multiple selection; MIDI in; the build-stamp banner | 31; about 1,720 lines | `Fragment` edits in `patch/edit.h` |
| 6. Shape | tables and arrays in the panel; the Envelope, Curve, Segment and Shaper editors; the shape editor window; the partials and formant strips with the scrubber; the mapping table; audition; the crash log | 40; about 2,810 lines | none known |

Slice 1 took one session from signature to desk. My estimate for the rest,
from those sizes: a session each for 2 to 5, two for 6. Each gets a
page of interfaces for your signature first.

## A2. What is not coming back, and why

**1. What you deleted on 2026-10-05** (the target architecture; the design's
section 10):
- The node-graph mode as a mode: File > New > Node Graph, the two Convert
  commands, making a Channel or a Mixer from the menu. A plain sound (a
  file with no instrument block) still opens, edits and saves, and gets
  its sound back in slice 3.
- The old editor's own streaming graph. The mechanism goes; what it let
  you do does not: holding a note on any patch (the Live checkbox) and a
  plain sound playing until stopped are both in slice 3.
- The second clipboard. One is kept: the one that goes through the
  system's clipboard, because it carries between two editor windows.
- The 13 headless modes (`--roundtrip`, `--gencheck` and the rest). The
  test programs do their work now.

**2. Faults, which a rebuild mends and does not restore:**
- One Ctrl+V pasting twice when the canvas was both focused and under the
  mouse (the two clipboards answering one key).
- S being a note key and Stream at once; the arrows changing the octave,
  panning the canvas and scrolling the strips at once; `=` and `0` being
  note keys and zoom keys at once. Each key gets one job by where the
  keyboard is; the choices are recorded in the slice that makes them.
- File > New and File > Open throwing away unsaved work without asking.
- Chords refusing to render a patch that was never saved, though it
  rendered the one in memory.
- Duplicate leaving out a node's arrays, dynamic pins and seed.
- Controls that did nothing: the Chords tab's Inversion and Spread, the
  Parameter node.

**3. Changed by the engine's reader, yours to overrule:** the old editor
opened a file that named a node type it did not know and drew that node
inert. The patch module refuses such a file, naming the type and, for a
retired one, the tool that rewrites it. The file does not open.

## A3. What is new, not a restoration

- **Undo and redo** (slice 2). The old editor had none. It is in the
  design you signed this morning (ruling 9) and costs little, since the
  patch is a plain value; it is the one new thing left in the plan.
  Strike it here if "no features" covers it.
- **Asking before New or Open loses unsaved work.** A mended fault.

Taken out of the plan because they were features the design's slice table
had slipped in: waveform strips that follow live play (the old ones came
from Generate, and so will these), rendering the patch's embedded score
from the window, and a left and right output display (the old Output
strip was mono).

## A4. What the design's slice table had missed

The inventory found these, each a working capability that no slice named:
wormholes (a wire that teleports, with jump and ghosting); the Listen tap
(hear any one node while the patch runs); editing dynamic pins; renaming a
node or a note face; editing the instrument block; the Live checkbox;
passage and chord files; Save WAV; pop-out strips; the audio watchdog and
restart.

One is a gap in the engine and not only in the editor. With Live checked
the old editor held as many keys as you pressed on any patch whose
envelopes could be gated. The live player built in Part 5 holds one line
(a mono legato line, right for a patch marked sustaining) and plays
everything else as notes of fixed length. Holding several keys on a patch
not marked sustaining is engine work, in slice 3.

## A5. Lines to check

Checked in slice 2's first step (2026-11-23), each against the code: the
23 lines the inventory could not vouch for, and what else the check turned
up. 7 are done as the list said (holding a note only on a patch marked
sustaining, as A4 says). 2 no longer apply: a failed load cannot leave an
empty editor, because a load is all or nothing, and the instrument block
has no keys the editor does not carry. 10 were not done, or done in part,
and are small: they are mended inside slice 2 (the window's place on the
screen; one click to type a number; the wheel panning the canvas; a bad
position placed afresh; a pin's hint; the PC Keyboard button; the piano's
width, labels and note names; velocity from where a key is clicked; the
voice count and colours of the level; a save entering the recent files).
2 are Stop of live notes, which goes to slice 3 with the transport, as do
two parts that need the live player to report more than it does (the
pre-clip level; which notes still sound after their keys are up). 2 were
slice 5's already (the clipboard keys; selecting several nodes). The check
also found six lines the list called done that were not, all of them
looks: the node's title coloured by family, the panel header's colour, the
wire's width, grey input-only pins, and the recent files' count, names and
tooltip. Slice 2 as well. Every changed line says "checked".

## B. The list

Line ranges are the old file's. [U] a user used it; [H] a headless or test
mode; [D] dead, unreachable or never working. After the dash: where it
stands. "slice N" is the table of A1.

### 1. Startup, command line, main window

- [U] Launch with a patch path as the only argument and have it loaded, or a "Patch not found" / "Failed to load" status (12374-12403) — done in slice 1
- [U] Launch with no argument and get a fresh Patch graph (Sine, Envelope, Output) (12378, 1200-1222) — slice 2 (start a new patch)
- [U] See the window title "MForce - <file> - Patch Graph|Node Graph" or "(unsaved)", plus a build-stamp suffix (12468-12484) — done in slice 1 (the file and the star); the build stamp: slice 5; the mode label went with the mode flag (A2.1)
- [U] See a fixed docked layout on every launch: Transport top, Node Editor centre, Properties right 22%, Waveforms/Spectrum/Keyboard tabs bottom 28% (12633-12669); rebuilt each launch, so docking is not persisted (12406, 12638-12641) — done in slice 1 (layout; section 10 "decided in the slice that needs them")
- [U] Collapse the bottom pane to a sliver and restore its height with a chevron at its top-right corner (11739-11773) — slice 4
- [U] See a red banner when the exe is older than an engine source, with Dismiss (11775-11798, 11803) — slice 5
- [U] Window placed on the primary monitor's work area, 1400x900 (12297-12307) — the size: done in slice 1; placed on the work area: slice 2 (checked)
- [U] Single-click a drag field to type a number (ConfigDragClickToInputText) (12327-12329) — slice 2 (checked: one setting, not made in slice 1)
- [H] Run with a console attached when the first argument starts with "-" (12805-12820) — deleted by section 10 ("the headless comparison modes")

### 2. Menus

File menu (12499-12577):
- [U] File > New > Patch Graph (12500-12504) — slice 2
- [U] File > New > Node Graph (12505-12508) — deleted by section 10 ("the node-graph mode flag"); a plain sound opens in slice 1, sounds in slice 3
- [U] File > Open (Ctrl+O), Win32 open dialog, JSON filter, remembered folder (12511-12513, 2222-2233, 1269-1283) — done in slice 1
- [U] File > Open Recent: up to 12, filename shown, full path in tooltip, missing file removed with "File not found" (12514-12559, 2165-2220) — the list: done in slice 1; twelve of them, the file's name with its path in a tooltip, a missing file leaving the list: slice 2 (checked)
- [U] File > Open Recent > Clear (12531-12534) — slice 2
- [U] File > Save (Ctrl+S), Save As when unnamed (12560-12562, 3172-3177) — done in slice 1
- [U] File > Save As..., overwrite prompt, remembered folder (12563-12565, 3166-3170, 1252-1267) — done in slice 1
- [U] File > Audition... opens the Audition window (12567-12570) — slice 6
- [U] File > Quit through the unsaved-changes check (12572-12575) — done in slice 1 (closing asks)

Edit menu (12584-12610):
- [U] Edit > Cut / Copy / Paste on the in-process clipboard, enabled by selection / content (12586-12591) — slice 5 (this is one of the two clipboards; see section 10 "the second clipboard")
- [U] Edit > Parameter mapping... (patch graph only) (12593-12595) — slice 6
- [U] Edit > Convert to Patch graph / Convert to Node graph (12597-12601, 9608-9786) — deleted by section 10 ("the node-graph mode flag")
- [U] Edit > Restart audio (Ctrl+Shift+A) (12603-12608) — slice 4

Menu-bar play items (12612-12628):
- [U] Play (Space) / Stop (Space) in the menu bar (12618-12627) — slice 3, Play and Stop of a render and Stop of live notes (checked: nothing stops a note today)
- [U] Stream (S): play the graph continuously (12622-12623) — deleted by section 10 ("its own streaming DSP graph")

Canvas right-click create menu (9873-9976):
- [U] Paste (Ctrl+V) at the top of the create menu, OS-clipboard fragment (9877-9879) — slice 5
- [U] Note (patch graph only; greyed "Note (patch graph only)" otherwise) (9883-9897) — slice 2
- [U] Curve, Envelope, Var, Range quick items (9898-9901) — slice 2
- [U] Output when none exists, else greyed "Output (exists)" (9903-9914) — not needed: the Output node is always on the canvas (slice 1)
- [D] Parameter (node graph only) creates a "frequency" Parameter node (9916-9921): its stated purpose is keyboard play, but the keyboard is disabled in node-graph mode (7037-7039, 7983-7995) and the patch saver skips Parameter nodes (2628, 2692); nothing reads it — deleted by section 10 ("the node-graph mode flag")
- [U] Family submenus generated from the registry's Main shelf, one separator before the shaping families (9835-9859, 9925) — slice 2
- [U] Output submenu: Output (patch), Channel and one Mixer (node graph), the others greyed (9929-9969) — the Output node is always on the canvas; Channel and Mixer went with the mode flag (A2.1)
- [U] Experimental submenu: the Experimental shelf by family (9861-9868, 9973) — slice 2

Node right-click menu (10409-10633): see sections 5, 7, 8, 9 below.

### 3. Keyboard shortcuts (all of them)

- [U] Ctrl+S save, global (12674-12675) — done in slice 1
- [U] Ctrl+O open, global (12676-12677) — done in slice 1
- [U] Space: Play, or Stop when anything plays, unless typing (12678-12683) — slice 3
- [U] S (no Ctrl): Stream, or Stop when playing, unless typing (12684-12689); collides with the QWERTY note key S (4787) when PC Keyboard is on — deleted by section 10 ("its own streaming DSP graph")
- [U] F: centre the canvas on all nodes (13274-13295) — slice 2
- [U] Arrow keys (held): pan the canvas, Shift faster; active whenever no text field has focus (13297-13306) — slice 2, with the collision with the octave and note-length arrows settled (A2.2)
- [U] Ctrl+C / Ctrl+V with the editor HOVERED: OS-clipboard copy/paste (13098-13105) — slice 5
- [U] Ctrl+X / Ctrl+C / Ctrl+V with the editor FOCUSED: in-process clipboard cut/copy/paste (13308-13313); both pairs fire on one press when the editor is focused and hovered (unclear: double paste) — slice 5 / section 10 ("the second clipboard")
- [U] Ctrl+Shift+A: restart the audio stream, anywhere (13315-13320) — slice 4
- [U] Delete or Backspace with the editor focused: delete selected wires, then selected nodes (13322-13353) — slice 2
- [U] Delete in the Audition patch list, then Enter to confirm / Esc to cancel (5265-5270, 5297-5302) — slice 6
- [U] QWERTY note keys, two zones (Z row + home row one octave down; Q row + number row at the octave) when PC Keyboard is on, not with Ctrl, not typing (4775-4840, 7060-7096) — done in slice 1
- [U] Up/Down: octave -1/+1 (0..20); Left/Right: halve/double the note duration (0.05..30 s), while PC Keyboard is on (7097-7106) — done in slice 1
- [U] Waveforms window focused or hovered: Left/Right scroll 10%, Shift = page, Home/End, +/= and keypad + zoom in, - and keypad - zoom out, 0 fit (11494-11527); these overlap the QWERTY keys =, 0 and the arrows — slice 4

### 4. Canvas: display and mouse gestures

- [U] Pan with the mouse wheel (vertical) and Shift+wheel or a horizontal wheel (horizontal) (13012-13025) — slice 2 (checked: nothing reads the wheel today)
- [U] Pan with middle-drag (imnodes native) or Alt+left-drag (12350-12352) — done in slice 1 (checked)
- [U] Pan with the status-bar buttons < > ^ v (100 px) (13362-13368) — slice 2
- [U] Fit button in the status bar (13360) — slice 2
- [U] See the status bar: mode, node count, link count (13355-13358) — slice 2 (the counts; the mode went with the mode flag)
- [U] See the imnodes minimap bottom-right (15%) and drag in it to jump (12913-12915) — done in slice 1 (at 20%; decided)
- [U] See grid lines, rounded nodes, 2.5 px wires (12343-12348) — the grid and the rounding: done in slice 1; the wire's width: slice 2
- [D] Canvas zoom: none exists (imnodes 0.x has no zoom; the wheel pans, 13012-13025)
- [U] Click a node to select it; one selected node drives Properties (13089-13096) — done in slice 1
- [U] Box-select and Ctrl+click multi-select (imnodes built-ins, not coded here; used by Group selection/Copy, 9981-9985, 10204-10208) — slice 5 (checked: imnodes selects several, the canvas keeps one)
- [U] Ctrl+click an already selected node (or one of its pins) to DESELECT it, with "deselected" status (13027-13071) — slice 5 (with multiple selection)
- [U] Drag nodes to move them; positions tracked app-side so group drill-in keeps them (280-287, 12819-12831) — done in slice 1
- [U] Right-click empty canvas: create menu at the click point (13231-13270) — slice 2
- [U] Right-click a node: node menu; right-click a collapsed group: group menu (13250-13260, 10409-10436) — slice 2 (Delete) / slice 5 (the rest)
- [U] Right-click a wire: route it through a new wormhole pair (13234-13248, 8335-8392) — slice 5
- [U] Double-click a collapsed group to drill in (13073-13087) — slice 5
- [U] Double-click a wormhole to jump to its twin (12993-13006) — slice 5
- [U] Auto grid layout (220 px columns, wrap at 900) for New and for files with no positions (12454-12466, 1168-1171) — done in slice 1 (place_unplaced_nodes, 13.1)
- [U] Unplaced nodes of a positioned file laid out as a dependency-column strip below the saved layout (2031-2131) — done in slice 1 (place_unplaced_nodes)
- [U] Corrupt positions (|x| or |y| >= 1e7) ignored with a stderr line (2012-2021) — slice 2 (checked: not done; such a node is placed afresh)
- [U] Saved panning restored on load and written on save (2140-2146, 2934-2937, 3091-3094) — done in slice 1 (13.2)
- [U] Node title colour by registry family, Output/Note/Curve their own (155-169); body a pale tint (171-181); hover lift (8111-8117); selection shows imnodes blue (8111-8113) — slice 2 (checked: not done in slice 1); the theme file: slice 5
- [U] Node body: input pins with name and, unconnected, the constant (%.3f) (8140-8163); input-only pins grey (8151-8153) — the name and the constant: done in slice 1 (as %g; decided); input-only pins grey: slice 2
- [U] Non-empty string settings printed on the node body (8135-8137) — slice 2
- [U] Output pins right-aligned; the tap pin small, teal, quad (8204-8225) — done in slice 1 (tap drawn)
- [U] Tap wires teal (12869-12879) — done in slice 1 (orange)
- [U] Unknown node types drawn inert with one "out" pin, and a status line naming them plus a retired-type hint (510-521, 1582-1605, 2154-2161) — CHANGED (A2.3): the engine's reader refuses the file, naming the type and its hint

### 5. Node operations

- [U] Add a registered node at the right-click point; it joins the open group when drilled in (9803-9821) — slice 2
- [U] A new node's id: a global counter (75-76, 342); its label (= saved id): display name ("Source" stripped; Channel/Mixer/Output/Parameter/Note/Curve) then Name2, Name3... (141-153, 610-621) — slice 2
- [U] A new node's defaults: pin constants from param descriptors, input-only pins at 0, settings at descriptor defaults, arrays from the live object (463-536, 417-438); an Envelope is ADSR 0.05/0.1/0.7/0.2 (383-388); a Curve gets knots (0,0),(1,1) (9806-9813) — slice 2
- [U] Seeds: a new node gets none (jsonSeed -1, 257-263); a loaded "seed" is carried verbatim and re-emitted on save (1645-1648, 2810-2815, 3053-3057) — slice 2 (new) / done in slice 1 (carry)
- [U] Delete a node (context menu, Delete/Backspace): stops streams, removes its wires, drops it from groups, unwires (not demotes) dynamic pins it drove, releases a Listen tap on it, clears selection (1100-1157, 10507-10510, 13346-13350) — slice 2
- [U] Delete a selected wire with Delete/Backspace (13327-13345, 1159-1166) — slice 2
- [U] Wire: drag from output pin to input pin (or back); a single-input pin's old wire is replaced, a multi pin takes more (13107-13206) — slice 2
- [U] Wiring refuses with a status sentence: Formant-only pins, PartialSet-only pins, Mixer.channels needs a Channel (759-797, 13154-13170) — slice 2
- [U] Wiring refuses a normal wire that would close a cycle (self-wire included), "Cycle - close loops through a tap pin"; tap wires may close loops (1026-1056, 13171-13178) — slice 2
- [U] Wire from a node's tap pin: the consumer reads the previous sample (103-106, 984-989); saved as {"tap": id} (2723-2726, 2742-2746) and reloaded onto the tap pin (1897-1917); no tap on a Wormhole (531-533) — slice 2
- [U] Unwire by dragging a wire off its input pin (EnableLinkDetachWithDragClick) (8145, 13208-13229); the pin falls back to its constant (800-814, 954-1002) — slice 2
- [U] Rename a node in Properties (Enter commits): refuses empty, ".", leading "__", a taken node or group name; rewrites refs in extras, dynamic pins, group members (8444-8461, 2505-2565) — slice 2
- [U] Duplicate (node menu): same type, pin constants and settings and strings copied by index, offset +30,+30; arrays, dynamic pins, seed not copied (10438-10473) — slice 5
- [U] Replace with (node menu): any registered type; same-named pin constants, settings and dynamic pins carried; wires remapped by pin name, unmappable ones dropped and named in the status; label refs, Listen tap and selection follow (10079-10187, 10485-10505) — slice 5
- [U] Edit a pin constant in Properties: InputFloat, step 1% of value, x10 fast step; erases a carried extra of that name (8483-8515) — done in slice 1
- [U] Edit a constant on an input-only pin that the file gave one (8517-8549, 96-101) — done in slice 1 (checked)
- [U] Dynamic pins: click the circle beside a non-structural setting to promote it (gold quad pin appears, unwired, hollow) or demote it (pin and wire dropped, stowed scalar restored) (8565-8605) — slice 2
- [U] Wire a Curve output onto a gold pin to drive the setting once per note; any other source refused "Only a Curve may feed a dynamic pin" (13124-13149) — slice 2 (the engine takes any signal there, so the editor will too: wider than before)
- [U] Drag a gold wire off, or select it and Delete: the pin stays promoted and unwired (13212-13219, 13336-13342) — slice 2
- [U] See gold wires and gold pins with tooltips; Properties shows "<- driver" for a driven setting (8165-8202, 12883-12909, 8607-8620) — drawn in slice 1 (green); editing them: slice 2
- [D] performField (299-303): "Legacy", superseded by per-field pins

### 6. Note faces (PerformNode)

- [U] Add a Note node from the create menu, patch graph only (9883-9897) — slice 2
- [U] See a Note as one node, title "Note", its own colour, six output pins frequency, velocity, wheel, pressure, duration, onset, no inputs (492-507, 146, 164, 8107-8232) — slice 2
- [U] Wire any Note field to any input; each pin previews a fixed value on the canvas (440, 0.8, 0, 0, 1, 0) (369-381, 977-983, 4183-4194) — slice 2
- [U] Load: the file's per-field PerformNodes are merged into faces by layout "faces", else greedy-packed in file order, stragglers given their own pin (1564-1579, 1806-1877) — slice 2
- [U] Save: one PerformNode per WIRED face pin, id kept from the file or a fresh __perf_<field>[n], prepended, comments kept, layout "faces" with each face's position (2634-2663, 2841-2847, 2939-2948) — slice 2
- [U] Rename a Note face (its face label) in Properties (8444-8461) — slice 2
- [U] A face's fields never count as a group's signal output (2475-2480) — slice 5

### 7. Groups

- [U] Group selection (node menu, "(n)"): refuses the Output node and selections with more than one outward signal, naming them; name Group/Group2...; placed at the centroid; nested when drilled in (9978-10056, 10524-10538) — slice 5
- [U] See a collapsed group as a purple node: name, "n nodes (double-click)", one input pin per boundary input labelled "member.pin", one "out" pin (8234-8313, 2433-2503) — slice 5
- [U] Wire onto a collapsed group's projected pins (wires the real pin) and see boundary wires drawn to the face (13110-13120, 8315-8333, 12837-12881) — slice 5
- [U] Group menu: Listen here / Stop listening here, Open, Ungroup (10415-10436, 10058-10077) — slice 5 (Listen: see section 8)
- [U] Add to group (n) with the two-output rule on a trial membership; Remove from '<group>' to the parent level (10540-10630) — slice 5
- [U] Rename a group in Properties, same name rules, shared namespace with node ids; see its members list (8403-8434, 2567-2591) — slice 5
- [U] Breadcrumb bar "Patch > Group > ...", click to go up (12704-12719) — slice 5
- [U] Drill-in frames the level's content; drill-out restores the pan you left (12752-12800) — slice 5
- [U] Groups saved in layout "groups" (name, members, pos), dead members dropped with a stderr line at load (2604-2611, 1961-1995) — done in slice 1 (kept in the file) / slice 5 (drawn)

### 8. Listen tap and wormholes

- [U] Listen here / Stop listening here on any node with an output (node menu), "<)))" marker on the node; the patch keeps running and only the monitored signal moves; monitored at volume 1; never saved (575-578, 10514-10522, 8124-8127, 2832-2836, 2857-2863, 3140-3147, 3823-3830) — slice 4
- [U] Breadcrumb "Listen: Patch | Group" toggle inside a group (12720-12748); drill-out returns the ears to the patch (12801-12811) — slice 5 (the toggle); the tap itself: slice 4
- [U] Wormhole node pair: a wire that teleports; mini one-row violet face; the span between two Wormholes is hidden (8068-8105, 12859-12866) — slice 5
- [U] Hover or select a wormhole half to ghost the span to its twin; tooltip names the twin and its group; "unpaired" tooltip (12919-13010) — slice 5
- [U] Wiring two wormholes pairs them, with a status sentence (13191-13201) — slice 5

### 9. Clipboard (two of them), undo

- [U] OS-clipboard copy (node menu "Copy (n)", Ctrl+C over the editor): the selection's saved form as a JSON fragment; Note, Output, Parameter, Mixer, Channel and groups skipped (10189-10254, 10475-10483) — slice 5
- [U] OS-clipboard paste (create menu, Ctrl+V over the editor): refuses a fragment of the other graph mode; fresh labels; internal refs renamed, refs that name a node in the target kept, others dropped and counted; +60,+60; joins the open group; merged through save and reload, restored on failure (10256-10406) — slice 5
- [U] In-process clipboard: Edit menu and Ctrl+X/C/V with the editor focused; copies pin constants, settings, strings, arrays, seed, polyphony, sustaining, onsets and internal wires; pastes +40 per successive paste; skips a second Output, a second Mixer, wrong-mode types (9348-9606) — slice 5 / deleted by section 10 ("the second clipboard"; which of the two is "second" is not said)
- [U] Cut = copy then delete the selection (9596-9606) — slice 5
- [D] Undo/redo: none exists. The shape editor's comment says "the editor has no undo stack" (5400-5401); grep finds no undo, redo or Ctrl+Z. Slice 2's undo is new, not a restoration. — none existed; undo and redo are new in slice 2 (A3)

### 10. Properties panel (generic)

- [U] Empty selection: "Select a node to edit" (8435-8437) — done in slice 1
- [U] Header with the type's display name in its colour (8440-8442) — slice 2 (checked: slice 1 shows the id and the type, uncoloured)
- [U] Pins with their hint "(hz)" etc.; a connected pin shows "<-- source" (8491-8501, 8517-8529) — the source: done in slice 1; the hint: slice 2 (checked)
- [U] Settings: bool checkbox, enum combo from labels, int with clamp, float with clamp to descriptor range; applied under the audio lock, arrays re-pulled (8551-8678) — done in slice 1
- [U] String settings as text fields (8680-8706) — done in slice 1
- [U] Generic arrays: standalone column with per-row x and "+ Add row" (8916-8946) — slice 6 (tables)
- [U] Grouped arrays (a table): columns side by side, one row count, row x, "+ Add row", pushed together (8947-9025) — slice 6
- [U] ExplicitPartials: mult2/ampl2 columns disabled while evolve is off (8839-8851, 8989-8998) — slice 6

### 11. Per-type editors in Properties

- [U] Envelope: Preset combo (engine's preset list, upper-cased) + Apply, laying out the stages with defaults (9077-9108, 1009-1024) — slice 6
- [U] Envelope: stage table Pct/Start/End/Curve/Power/Min Secs/Max Secs, drag fields, per-row x, "+ Add Stage" continuing the previous end value (9031-9213) — slice 6
- [U] Envelope (all envelope types): a 256-point plot of the stage shape (9215-9278) — slice 6
- [U] Envelope minValue/maxValue as real pins (475-490) — done in slice 1
- [U] CurveNode: Properties pane IS the curve editor: "in: source", header "label -> destination" (dynamic pin "(per note)" or pin) (8464-8475, 6656-6680, 6785-6794) — slice 6
- [U] CurveNode: mode points|expressions, interp linear|logx|loglog, with tooltips (6795-6828) — slice 6
- [U] CurveNode points: x/y table, x re-sorted on commit, x min 0.0001, x per row (needs >2), "+ point" doubling x; log-x plot through the engine; double-click the plot opens the shape editor (6840-6928) — slice 6
- [U] CurveNode expressions: rows x | form (a*x+b, a*x^p) | a | b/p | x, "+ point", extrapolating log-x plot an octave past the ends; switching in seeds one row (6682-6783, 6799-6806) — slice 6
- [U] SegmentSource: shape preview through the engine's interpolator; double-click opens the shape editor (8708-8770) — slice 6
- [U] SegmentSource values edited as point rows # | Width | Value | x, "+ point", odd tail healed (8853-8915) — slice 6
- [U] Shaper: transfer-curve preview (-1.5..1.5) through the engine; double-click opens the shape editor (8772-8806) — slice 6
- [U] Output node: polyphony drag 1..32 (9281-9294) — slice 2
- [U] Output node: sustaining checkbox, onsets text (comma list), glide ms 0..100 written only once set (9296-9332, 241-255, 2864-2885) — slice 2
- [U] Instrument-block keys the panel does not show (release, volume...) carried through save (638-641, 1515-1518, 2851-2856) — not applicable (checked: the block is the five keys the engine reads, all of them saved; any other key is refused at load)
- [D] Parameter node: editable name (9334-9343) — see the Parameter create item; deleted by section 10 ("the node-graph mode flag")

### 12. Shape editor window (5375-6654, about 1,280 lines)

- [U] Open by double-clicking a Curve, Segment or Shaper preview; window "Shape — <label>" (5530-5559, 5722-5788) — slice 6
- [U] Toolbar: log x (not for Shaper), Fit, units readout "x: samples|seconds" for Segment, point count, read-only above 512 points (5790-5810, 5953-5959) — slice 6
- [U] Canvas: grid with labels (decades on log x), engine-evaluated curve overlaid on the grey editing skeleton, points lit on hover, "(x, y)" readout (6056-6112, 6164-6272, 6344-6367) — slice 6
- [U] Left-click empty canvas: insert a point and drag it; left-drag a point; Shift-drag slides the tail (6403-6492, 6577-6609) — slice 6
- [U] Right-click a point: delete it (keeps at least 1 or 2) (6493-6513) — slice 6
- [U] Wheel zooms x around the cursor, Shift+wheel zooms y; middle-drag pans; works read-only (6543-6576) — slice 6
- [U] Shaper: right-click a segment to create a curvature override and drag it vertically; left-drag its orange dot; right-click the dot to reset; green override curves (6314-6342, 6514-6540, 6611-6637) — slice 6
- [U] Shaper: Clear overrides (confirm) (5960-5983) — slice 6
- [U] Shaper: Preset combo (bow, reed1, reed2, lip, jet, hard, sine, saw, triangle) and one-step Revert (5468-5522, 5899-5949) — slice 6
- [U] Shaper: Clear to identity (confirm) (5866-5898) — slice 6
- [U] Shaper morph: Add Morph (B = copy of A), A|B radio (Stick|Slip under hysteresis), Remove Morph keeping the active curve (confirm); B ghosted, live blend drawn from the morph pin; inserts/deletes mirrored on both curves (5811-5865, 6274-6312, 6433-6488, 6498-6510) — slice 6
- [U] Segment: "Generate — Pulse Train" panel: pulses, width, widthRamp, spacing, spacingRamp, peakEnd, varPct, bipolar, seed; Generate (new seed), Reuse seed; forces timeMode samples (5416-5466, 5985-6035) — slice 6
- [U] Segment: Show variance ghosts (three re-randomised traces when widthVarPct/valVarPct > 0) (6017-6021, 6114-6162) — slice 6
- [U] Segment edits pin auto timeMode to explicit before changing values (5668-5705) — slice 6

### 13. Parameter mapping dialog

- [U] Edit > Parameter mapping...: read-only table Target | Kind (dynamic pin / fixed pin) | Driven by | Shape (curve/direct), traced back to a Note node, "Nothing in this patch is driven by the note." (6930-7028) — slice 6 ("the mapping table"; section 10 deletes "the legacy parts of the mapping dialog", which this version no longer has)

### 14. Live play

- [U] PC Keyboard toggle button (green when on, disabled outside a patch graph) gating the QWERTY keys (7979-7999, 7063-7064) — slice 2 (checked: not done; the keys are always on)
- [U] Live checkbox: checked = keys and mouse hold the note until release; unchecked = fixed-Duration notes, key-up ignored (4632-4643, 7859-7864, 7079-7090, 7277-7287) — slice 3 (needs the live player to hold several keys on a patch not marked sustaining: A4)
- [U] On-screen piano: octaves chosen from width (1..10, keys 25-45 px), from the transport octave, ending on a top C; sounding notes highlighted; QWERTY labels on keys when PC Keyboard is on (7109-7224) — from the octave: done in slice 1; the octaves from the width, the top C, the key labels and the note names: slice 2; a note still sounding after its key is up shown as sounding: slice 3 (the live player must report it)
- [U] Click an on-screen key: velocity from where on the key you click (0.125 at top to 1.0 at bottom); held until mouse-up anywhere (7226-7288) — held until mouse-up: done in slice 1; velocity from the height: slice 2 (checked)
- [U] Up to 16 mixed voices; steal same-source, then free, then nearest-done (3338-3453); per-instrument pool slots, held notes stolen last (3465-3483) — done in slice 1 (eight notes)
- [U] Notes ring out adaptively past their length and fade the last chunk instead of clicking (3371-3380, 3543-3569) — done in slice 1 (checked: the engine's ring-out)
- [U] Held notes: envelopes gated on key-down, released on key-up with the longest release plus the reflection allowance; MultiplexSource graphs fall back to scheduled notes (4319-4433, 4435-4471) — on a patch marked sustaining: done in slice 1 (checked); on any patch, with Live: slice 3 (A4)
- [U] Sustaining patches play as a mono legato line: overlap slurs the living voice, a gap cuts the previous release with a 3 ms fade, lifting the sounding key slurs back to a still-held key (4228-4237, 4359-4394, 4437-4462) — the line and the slur: done in slice 1; the 3 ms cut of the release before it and the slur back to a key still held: slice 3
- [U] Live instrument cached and rebuilt only after an edit, "Instrument rebuilt: N voices, M ms" status (4212-4270) — done in slice 1 (rebuild/swap, 4.2)
- [U] MIDI in: first port auto-opened at start; combo "MIDI: name|none" re-enumerating ports, "(none)" closes (4645-4694, 8002-8032) — slice 5
- [U] MIDI note on/off (velocity per note), CC1 mod wheel and channel pressure into the instrument state (4696-4744) — slice 5
- [D] MIDI sustain pedal, pitch bend, poly aftertouch: explicitly deferred, ignored (4739-4742)
- [U] AUDIO line in the Keyboard tab: voices=N, pre-clip and post-clip peaks, green/yellow/red, decaying (3286-3292, 3636-3643, 7041-7058) — the peak, decaying: done in slice 1; voices=N and the three colours: slice 2; the pre-clip peak: slice 3 (the live player must report it)
- [U] Soft clip on the mix; no polyphony scaling (3509-3515, 3624-3627) — done in slice 1 (checked: the clip follows the instrument's peak guard, which is on unless a test turns it off)
- [U] "Last note: 60 C5 261.6Hz vel 0.80" right-aligned in the transport (7489-7497, 8046-8059) — slice 3
- [U] Panic: no separate control; Stop (button, Space, menu) stops every voice and stream (4599-4607) — slice 3, with Stop (checked: not done)
- [D] A volume control: none exists (only per-patch instrument volume, carried, 638-641)

### 15. Transport and generating audio

- [U] Transport tabs Note | Passage | Chords | Drums select the mode (3236-3268, 7820-7827) — slice 3
- [U] Note tab: pitch combo C..B + octave spinner composing "C#4", velocity, duration, Live (7831-7866) — slice 3
- [U] Passage tab: two-line text, octave, BPM 20-300, velocity, Onsets auto|all tongue|all slur|all breath (7868-7888) — slice 3
- [U] Passage phrase marks "|" on a sustaining patch stamp breath/tongue/slur and hold (3991-4035, 7559-7614); non-sustaining patch reports "phrase marks ignored" — slice 3
- [U] Save Passage / Load Passage as .psg/.txt with its own folder memory (7773-7805, 7889-7896, 1285-1321) — slice 3
- [U] Chords tab: text, Group (dictionary), Figure, octave, BPM, Delay (spread ms, default 15) (7900-7933, 7615-7635) — slice 3
- [D] Chords tab Inversion and Spread spinners (7926-7929): transport_generate never reads them (7615-7635)
- [U] Save Chords / Load Chords as .chd/.txt (7913-7921) — slice 3
- [U] Chords render through Conductor/ChordPerformer with Josie figures, peak-normalised to 0.95, no per-node strips; refuses when the patch was never saved ("No patch file loaded") although it renders the in-memory graph (7301-7382) — slice 3
- [U] Drums tab: pattern text, drum map "KK=path;SN=path", BPM; one patch per drum loaded from disk, repeats from the pattern, peak-normalised (7935-7944, 7384-7487, 7636-7652) — slice 3
- [D] TransportState::repeats (3262): no widget, never read (the pattern's own repeats are used, 7451)
- [D] render_chords_waveforms' figurePrefix argument (7303-7304): unused inside the function
- [U] Generate: one engine render of the in-memory patch fills the output buffer and every node's strip, with the voice tail added; "Generating..." button state drawn before the blocking render (3971-4171, 3271-3277, 7949-7964, 12439-12448, 13478-13484) — slice 3
- [U] Generate in Note mode renders one note; Passage schedules notes back to back with rests as gaps (7532-7614) — slice 3
- [U] The patch's score seeds the transport note, velocity, duration and keyboard octave at load (4746-4773, 1507-1522) — slice 3
- [D] Render the patch's embedded score from the window: not possible, no control does it (only --gencheck, 11855-11916); slice 4 names it, so it would be new — was never possible from the window; not added (A3)
- [U] Play: Note mode replays the last render, regenerating when note, velocity or duration changed; Passage replays the last render without regenerating; Chords and Drums regenerate every Play (7656-7704) — slice 3
- [U] Play / Stop button flips with playback state (7967-7976) — slice 3
- [D] Loop playback: none exists
- [U] Save WAV: the last render as 16-bit stereo (mono duplicated), default folder renders/ then remembered, status with frame count (7706-7749, 8035-8037) — slice 3
- [U] Faults the render's nodes reported shown in the status line; the live pool's faults shown once a frame (4037-4046, 4119, 7549, 7810-7816) — slice 3
- [U] Status line in the transport, green or red (7499-7502, 8039-8044) — done in slice 1 (session status)
- [U] "Output node has no source connected" / "No patch loaded" precondition messages (7504-7521) — slice 3
- [U] Node graph Play/Stream: stream the Mixer live in stereo (channel volume, equal-power pan, gainL/R) (4473-4581, 7656-7664) — deleted by section 10 ("its own streaming DSP graph"); plain sounds sound in slice 3
- [U] Patch graph Stream: the Output's source pulled continuously, envelopes held in seconds with a hold stage appended for 2 h (3862-3962, 4479-4498) — deleted by section 10 ("its own streaming DSP graph")

### 16. Displays of sound

- [U] Waveforms tab: an "Output" strip (mono) and one strip per node with a capture, Envelope-family strips in their own colour (11314-11406) — slice 4 (old strips come only from Generate, not live: 4276-4278)
- [U] Each strip peak-normalised with +peak/-peak in the margin, min/max columns from a 64-sample mipmap (10639-10767) — slice 4
- [U] Zoom arrows (1x..4096x), Fit, Columns 1..4, Envelopes on/off, horizontal scroll slider (11325-11366) — slice 4
- [U] Wheel scrolls, Ctrl+wheel zooms, left-drag scrolls (11467-11492) — slice 4
- [U] Keep the zoom and scroll across Generates; first render auto-fits (3201-3210) — slice 4
- [U] Pop out any time-domain strip (arrow button) into its own window with its own zoom, Fit, scroll, wheel/Ctrl+wheel (3213-3222, 11453-11460, 11533-11626) — slice 4
- [U] Spectrum tab: Hann-windowed 8192-point FFT of the middle of the last render, log-frequency 20 Hz..Nyquist, -100..+6 dB grid, hover crosshair "Hz dB" readout, "No render yet" (10769-10964) — slice 4
- [U] Partials strip (Full/Sequence/Explicit): bars per partial with rolloff applied, _1 and faded _2 side by side, x ticks, peak label, hover "p i m1 a1 m2 a2" (10979-11176) — slice 6
- [U] Scrub slider (0..1): amber bars of the evolving partials at that point of the render from 100 envelope snapshots (3123-3134, 4129-4160, 11089-11133, 11351-11354) — slice 6
- [U] Formant strip (Peaks/Bands/Sequence): gain vs log frequency 20 Hz-20 kHz filled, grid, peak label, hover "Hz gain" (11178-11312) — slice 6
- [D] Pop-out for Partials/Formant strips: disabled, "isn't wired" (11441-11451)
- [D] Output L and R displays: none exist (the Output strip is the mono render, 11387-11389)

### 17. Files and persistence

- [U] Read patch JSON: graph.nodes/output, params (numbers, refs, taps, lists), dynamicPins, comments, instrument, score, seconds, layout positions/output/panning/groups/faces (1408-2162) — done in slice 1
- [U] Load refusals with the engine's sentences, leaving the open graph untouched: instrument.paramMap, retired top/node/feed keys, non-string comments, a list pin holding a non-link, a PerformNode's bad field, an Envelope with both preset and stages, ragged tables, a non-string string setting, a bad enum label in a column (1408-1483, 1661-1669, 1712-1721, 1731-1749, 1329-1372) — done in slice 1 (checked)
- [U] A failure after the refusals leaves an empty, unnamed, clean editor (1484-1496) — not applicable (checked: a load is all or nothing, so a failure leaves the patch that was open, open)
- [U] Write patch JSON: nodes topologically sorted (taps not counted), ids = sanitised labels, settings as bool/label/int/float, columns with labels, Envelope stage table even empty, sustainLevel not written, seed, unmodeled params verbatim and winning, comments, sampleRate, the file's score and seconds verbatim, no score added when the file had none, the default note (60, 0.8, 2 s, seconds 3) for a fresh patch, layout (2298-2961) — done in slice 1 (save); the default note for a fresh patch: slice 3, with the transport whose note it is
- [U] Write a plain node graph: output = the Mixer, else the file's output; seconds as loaded or 5 (2963-3102) — deleted by section 10 ("the node-graph mode flag") for the mode; a plain sound saves in slice 1
- [U] The file's top-level comment and node comments carried and written back, shown nowhere (273-275, 643-645) — done in slice 1 (checked; a node's comment is now shown in the panel)
- [U] Save adds the file to Open Recent (3104-3112) — slice 2 (checked: only an open added it)
- [U] Playback reads a temp file %TEMP%/mforce_playback.json when the graph is dirty or a Listen tap is on (3136-3164) — internal; the live paths serialise in memory (4248-4255, 4052-4053)
- [U] Recent files persisted in mforce_recents.json in the working directory, normalised and de-duplicated (2165-2220) — done in slice 1 (%APPDATA%, 13.8)
- [U] Per-feature folder memory (open, save, wav, audition, passage, chords) and the curated folder in mforce_ui_settings.json in the working directory (2235-2296, 1228-1250) — with each feature's slice (open and save: slice 2), kept in %APPDATA%\MForce\
- [U] ImGui's imgui.ini in the working directory (floating window sizes; the dock layout is rebuilt each launch) (12321-12341, 12638) — done in slice 1 (%APPDATA%)
- [U] WAV out (16-bit stereo) (7724-7749) and WAV in for audition (5006-5035) — WAV out: slice 3; WAV in for audition: slice 6
- [U] Text files .psg/.chd/.txt for passages and chords (7773-7805) — slice 3
- [D] Templates, import, export of other formats, autosave: none exist
- [U] Unsaved-changes prompt on window close, taskbar close and File > Quit: Save / Don't Save / Cancel; a cancelled Save dialog keeps the prompt (689-695, 12309-12316, 12411-12437, 13420-13462) — done in slice 1
- [U] File > New and File > Open discard unsaved changes WITHOUT asking (1200-1222, 2222-2233, 1408-1497) — a fault (A2.2): Open asks since slice 1, New from slice 2

### 18. Audio device

- [U] Open the default output device only, stereo float, 48 kHz asked, 512 frames; the engine runs at the rate the device opened (3184-3188, 3660-3716) — done in slice 1
- [D] Choose an audio device: no control exists
- [U] Startup audio failure shown in a Windows message box; the UI runs without sound (3648-3654, 12357-12360) — done in slice 1 another way: the status line says why there is no sound; no box
- [U] Watchdog: no callback for 1 s reopens the stream every 2 s, status "reopened (attempt n) on '<device>' — died: <error>" or "FAILED" (3293-3311, 3728-3793) — slice 4
- [U] Restart the stream when the window regains focus after more than 2 s away (3298-3304, 12416-12427) — slice 4
- [U] Restart audio by hand (Edit menu, Ctrl+Shift+A) (12603-12608, 13315-13320) — slice 4
- [U] Dead voices freed on the UI thread, not the audio thread (3795-3821) — done in slice 1 (keep_up, 13.13)

### 19. Audition window (4880-5373, about 495 lines)

- [U] Source folder Browse (real folder picker, remembered) and its path; "Variant i of n: file" (4953-5004, 5157-5183) — slice 6
- [U] Prev, Play/Stop, Repeat, Next, Auto-advance through the folder's WAVs; click a WAV in the list to play it (5037-5086, 5185-5231) — slice 6
- [U] Closing the window stops its audio; opening re-scans the target list (5137-5146) — slice 6
- [U] Save...: modal, pauses playback, name pre-filled, Overwrite if exists, copies <variant>.patch.json to the target folder, resumes on close (5088-5134, 5202-5218, 5326-5372) — slice 6
- [U] Target folder Browse (persisted) and its patch list; select and Delete then Enter deletes the file from disk (permanent), Esc cancels (5237-5324) — slice 6

### 20. Robustness and diagnostics

- [U] Crash log mforce_ui_crash.log in the working directory: SEH code, address, access-violation kind, faulting symbol, symbolic stack; C++ exceptions escaping main logged too (11632-11737, 13497-13505) — slice 6
- [U] Every playback/structural change serialised against the audio thread (one mutex), streams stopped before a node is destroyed (816-843, 1089-1098, 4583-4626) — done in slice 1 (the new adapter's design)

### 21. Graph modes

- [U] Two graph modes, Patch graph (instrument) and Node graph (Channel -> Mixer, no instrument); the mode follows the file's "instrument" key (79-82, 1504-1505) — deleted by section 10 ("the node-graph mode flag")
- [U] Convert Patch -> Node: Output and Parameter nodes removed, Channel -> Mixer added after the old source, score/seconds/polyphony stashed (9625-9673) — deleted by section 10 ("the node-graph mode flag")
- [U] Convert Node -> Patch: Channel/Mixer removed, Output added; the stash restored, else a Note wired to the first unconnected frequency pin, polyphony 1 (9675-9786) — deleted by section 10 ("the node-graph mode flag")
- [U] Keyboard, PC Keyboard and Note creation disabled in node-graph mode (7037-7039, 7979-7995, 9883-9897) — deleted by section 10

### 22. Headless and test modes (all deleted by section 10, "the headless comparison modes")

- [H] --stamp: print the build-stamp staleness report (11822-11826)
- [H] --gatecheck <patch>: can live gating reach the voice's envelopes; count them (11828-11853)
- [H] --gencheck <patch> <out.wav>: render the embedded score through the window's Generate path, write the WAV, print each strip's RMS and faults (11855-11916)
- [H] --roundtrip <in> <out>: load and re-save through the mode's saver (11918-11939)
- [H] --rename <in> <old> <new> <out> (11941-11964)
- [H] --apply-preset <in> <envelope> <preset> <out> (11966-11989)
- [H] --replace <in> <node> <type> <out> (11991-12012)
- [H] --dump-boundary <patch>: each group's derived interface (12014-12041)
- [H] --paste-frag <in> <fragment> <out>: the OS-clipboard paste path (12043-12065)
- [H] --convert-roundtrip <in> <out> (12067-12091)
- [H] --dump-playback <patch> <out.wav> [--note N] [--vel V] [--dur D] [--keyboard]: the Generate or keyboard path's exact audio, peak and RMS (12093-12170)
- [H] --dump-layout <patch>: every node's resolved position, group, UNPLACED (12172-12203)
- [H] --dump-stream <patch> <out.wav> [--secs N] [--vel V]: the continuous stream offline with per-second RMS and pan (12205-12288)
- [D] Any other first argument starting with "-" falls through to the GUI and is tried as a patch path ("Patch not found") (12379-12385)
