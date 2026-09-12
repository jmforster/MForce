# String harness — component notes, reed diffs, hand-tweak guide

2026-09-12, rounds 1/1b. Spec: 2026-09-11-string-chassis-design.md.
Tool: tools/gen_string_harness1.py. Queues: string_harness1 family.
NO new engine components were required: the hysteresis Shaper (09-08)
+ existing primitives express the published waveguide model in full.

## What it is

The digital-waveguide bowed string (Smith/CCRMA; MSW friction), native:
two delay segments around the bow point closed through the stick/slip
junction, loss lumped at the bridge, output = bridge force. First build
EVER to hold pitch across C2..C6 (+/-22c, all bow positions) with a
bowed-mechanism junction — the retrofit rounds' period-2 subharmonic
disease is impossible here by construction.

## The three build lessons (each measured, each now load-bearing)

1. **Two-delay tuning is exact with compensate on the NUT delay only.**
   Its walk compensates the tap + all filter phases; the bridge delay's
   bulk is exactly its own ratio share. No generator arithmetic.
2. **The cycle needs a DC block.** The bridge lowpass passes DC; the
   biased junction emits it; without the 12 Hz Highpass1P the "note"
   is a ~45 Hz DC charge/relax cycle that ignores the delays entirely.
   (Physical reading: string terminations cannot sustain DC.)
3. **Output taps POST-DCBlock.** Tapping the bridge delay upstream put
   the bias's standing DC on the output and every threshold probe
   false-positived at the bisection floor.

## Differences to the reed/flute harness

| | Reed/flute (wind) | String harness |
|---|---|---|
| Energy entry | breath noise summed in-loop | NONE - bias transient + hair jitter only; silent at rest |
| Junction | single memoryless curve | hysteresis stick/slip pair + bias (bow velocity) |
| Loop | one delay, ratio ~1 | TWO delays, ratios P and 1-P (bow position) |
| Loss | damp keytrack ~3-6x f0, res = honk | bridge LP 12x f0, res 0.5 floor (gentle) |
| DC | dcblock after junction | dcblock after bridge LP (same node type) |
| Output | loop + formants + reverb | bridge force (post-DC), body deferred |
| Articulation | Drive_env (breath) | Bow_bias env (stroke) + breakaway (pressure) |

## Measured state (round 1/1b)

- capture 0.25 = the period-1 rule (round 5) transfers: all cells in
  tune; capture 0.12 collapses to the 0.5x grid. THE gate setting.
- Bridge loss 12 x f0 beats 8 x f0 on ML ears across the board (8 lets
  the pitch wander: motion resid 48-60c rms vs 17-21c).
- ML ears vs Iowa viola (lower = better; the tuned-additive lineage
  STARTED its optimization at ~1.28): round-1 cells 1.11-1.19.
  Term profile: harm 0.88 good, motion ~1.0 good (the mechanism's own
  jitter reads as bowing), attack 0.44-0.76, broadband 2.2-2.6 = THE
  gap -> round 1b added bow-hair noise (jitter on the breakaway pin);
  scores in the 1b manifest/session record.

## Hand-tweak guide (mechanism per knob; ears decide)

- **Bow position P (BridgeDelay ratio; keep NutDelay = 1-P):** the
  bowing-point comb. Smaller P = closer to bridge = sul ponticello
  brightness family. It is a live pin - no player can move it mid-note.
- **capture (Junction):** the regime knob. Below ~0.2 of a 0.6
  breakaway you get sub-octave period-2; 0.25 locks the note. Between
  the two = the scratchy boundary, on purpose if wanted.
- **breakaway level (Breakaway_mod source2, 0.6):** bow pressure.
  With Bow_bias 0.36 fixed, raising it toward 0.5+ under-drives
  (surface sound); lowering toward 0.4 slips easier (looser tone).
- **Hair_noise amplitude (0-0.12):** friction texture; drives the
  broadband term. THE first knob for "sterile" complaints.
- **Bow_bias envelope:** the stroke. Attack percent = bow landing;
  its release = lift. Duration field (backlog 64) can pace it.
- **BridgeLP mult (Bridge_curve a=12):** string darkness; below ~8
  the pitch stability suffers (measured), above ~16 approaches
  lossless zing.
- **Ampl_env maxValue (bisected ~0.63 crit; cells at 1.2x):** the
  Helmholtz vigor. More = harder saw; the release stage is the
  ring-down.
- **Body:** deliberately absent. Add 2-3 fixed SVF resonances on the
  output when the raw mechanism satisfies - one variable at a time.
