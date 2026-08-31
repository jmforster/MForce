# 2026-08-29 — Dipsy interactive: the grail hunt closes; envelope/UI batch lands

Interactive with Matt (08-28 evening + 08-29). Commit ba5200b.

## The noisy-attack grail — five constructions, five fool's gold, CLOSED

Goal restated by Matt: attach a noisy attack to ANY patch, generically.
Every construction was auditioned same-day and rejected as non-integrated
("two layers"):

1. **BitRot mix ramp** — memoryless bit-rotation waveshaper, wet/dry ramp.
   Rejected: with rotation deep enough to make noise, wet is fully
   decorrelated → a crossfade regardless of plumbing.
2. **Scramble (jitter + bitshuffle)** — time-domain read scramble, width
   ramp, all energy from the source's own samples. Rejected: "phony snare
   hit over a building sine" — causal unity is NOT what the ear checks.
3. **Reverse-graft** — degrade one period forward, play pass-reversed into
   a bit-exact phase-aligned graft. Rejected: non-integrated.
4. **Filter-FM (peanut gallery)** — audio-rate noise on a 4-pole BW LP
   cutoff for 20 ms. Built from existing primitives only. Rejected:
   "sounds just like all the others."

**Standing conclusion (2 structural classes failed + Matt's old breath
finding):** integration lives UPSTREAM, in the excitation coupled through
the tone's own resonant structure — KS gets it free; a downstream generic
insert cannot. The thread continues as backlog 34(a): excite4 attack
textures into the KSString source pin. BitRotSource and ScrambleSource
were built, validated, and DELETED same-session on Matt's call (engine
stays clean; the BitRotateEvolution pin promotion survives).

## Found en route (real, kept)

- **Editor↔loader asymmetries → backlog 35**: (a) loader param aliases
  drop wires in the editor; (b) numeric constants on input-descriptor
  pins evaporate on load; (c) update_node_dsp clobbers engine defaults
  with ConstantSource(0) on unconnected input pins — silences
  value-conserving evolutions (Sort/HE/Pluck) in the UI only. Reed/Brass
  "make no sound" root cause = breath pin starvation (they're functional).
- **Convert node→patch synthesizes a legacy paramMap frequency row** that
  collides with a later Note-face wire ("not a ConstantSource" at play).
  Fix direction in backlog 35: synthesize a Note face instead; Parameter-
  mapping dialog to become residue-only.
- **BitRotate float-ratchet**: non-byte-aligned rotations + float32
  re-quantization converge every slot to +1.0 DC (only k=8/16/24 form
  stable cycles); k=16 + odd table length produces an f0/2 subharmonic
  attack (parity keytrack). Documented here for whenever the evolution
  gets used as a texture tool.

## Landed (commit ba5200b, gate 180/180 pre-commit)

Engine: Envelope::replace_stages (Matt's 08-26 min/max-loss fix);
per-stage Curve/Power settings on all six preset envelopes (defaults
byte-identical); ADSR default release 0→0.1 (corpus verified unaffected);
BitRotateEvolution shiftBits/stepEvery → pins.
UI: 08-26 create-menu rearrangement + Experimental submenu; first-render-
only waveform auto-fit; envelope display below settings for all envelope
types; bottom-pane collapse/restore chevron.

## REVIEW 49 verdicted in full (excite4_smooth, Matt 2026-08-29)

- **Bed level is the dominant problem**: buzz bed "way too loud" at every
  level tried, and acceptable-at-low-f / terrible-at-high-f — the bed
  amplitude MUST keytrack down with note frequency (pin model: Note face
  → Curve → bed amplitude; patch-level, no engine work).
- **Seams are real but secondary**: the 4 ms sine-merge cells still carry
  a subtle "zithering"; smoothing helps, doesn't cure. A longer merge is
  a candidate cell for round 2, behind the level fix.
- **wtsaw (Helmholtz bed) = ok; jag40 > jag15, extend the ladder upward**
  (60/80 next round). The pitch-locked drawn-saw bed remains the thesis
  bet for bowing.
- **Hot attack variants: no audible difference** — dropped.
- **jins + jags all sound alike, "more plucky than bowy"** — attack
  duration/texture does NOT move the pluck→bow needle. Design consequence
  for backlog 34a: the KSString source pin buys attack BITE; bow
  character must come from sustained coupling (wtsaw-style bed / the bow
  junction), not from attack shaping.

Round-2 spec when a run picks it up: keytracked bed level, jag 60/80,
wtsaw as the default bed core, no hot cells, optional long-merge seam
cell.

## Process changes (memory-logged)

- Null gate: once per commit (pre-commit over the batch), not per tweak.
- Envelope min/maxValue instead of Range nodes; min>max inversion
  RETRACTED — author falling stages instead.
