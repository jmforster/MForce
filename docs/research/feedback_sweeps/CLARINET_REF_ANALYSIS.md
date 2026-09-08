# clarinet_attempt vs real clarinet (YouTube ref) — 2026-09-07

Reference: YouTube kSfEDb1cMAw (Ode to Joy on clarinet, 14.5 s). Measured:
concert **D major**, first note F#4 (Matt's "OTJ in F#/Gb" = the opening
note), player ~10–18 cents flat, eighth ≈ 0.31 s. Method identical to
OBOE_REF_ANALYSIS.md: research/ml_ears/oboe_ref_compare.py against
clarinet_attempt rendering the same phrase at matched tempo/key. Matched
note = the long final D4 (ref 292.7 Hz, 1.67 s).

## Baseline deltas (clarinet_attempt as saved)

- **Even harmonics 10–28 dB too HOT** — the exact inverse of the oboe
  finding. The ref shows the cylindrical-bore signature (H2 −36 dB,
  H3 −15: odd-dominant); the patch carries the oboe lineage's asymmetric
  junction, which manufactures evens the clarinet must not have.
- **Body resonance in the wrong place**: ref LTAS peaks ~1125 Hz (main)
  and ~2590 Hz; render peaks 697/2.1k — and the 2.1 kHz Formant1 ring is
  strong enough that the pitch tracker locks onto it on upper notes.
- Vibrato: ref 6.5 Hz / ~3.5 cents; render 5.2 Hz / ~12.7 cents.
- Attack (matched legato note): ref 90 ms; render 247 ms.
- Noise: ref harmonic-to-interharmonic gap 15.3 dB (breathy recording +
  room); render 47.4 dB.

## Directed cells (patches/sweep/clar_ref1/, renders in audition/clar_ref1/)

Axes: junction odd-symmetrization (mirror the positive-x breakpoints
through the origin — kills evens, keeps the working half's slopes),
formant placement per ref (1125/2590), vibrato, attack, breath. Results
table in the audition README. Lead cell **c3_sym_fmt**: evens 17.9→13.0,
noise gap →13.6 dB (ref 15.3), attack →59 ms (ref 90).

## Mechanism notes (for the lane MDs)

1. **Junction symmetry IS the even/odd axis, live and confirmed in both
   directions**: adding origin asymmetry gave the oboe its evens
   (09-04 session); removing it here suppresses them for the clarinet.
   Same knob, opposite ends — r5c's origin-asymmetry ratio axis now has
   validated endpoints.
2. **Symmetrizing moves the oscillation threshold**: the asymmetric
   down-slope carried loop gain; after mirroring, ignition needed a +6%
   drive rebisection. Any junction-shape sweep must re-find criticals
   per cell (the r5b harnesses already do; hand edits must too — this is
   the JUNCTION_OPERATING_POINT trap again from the other side).
3. **Shaper values are (x,y) PAIRS**, absolute breakpoints (8 points =
   16 floats) — two failed sweep rounds came from treating them as
   16 y-samples. Written down so nobody re-learns it.
4. The ref's 15 dB noise gap partly reflects recording/room, not only
   breath — matching it exactly may overshoot on a dry render.

## Open

- c4/c5 (vib/attack add-ons) measured worse than c3 on the single
  matched note — implausible physically; likely matched-note selection
  noise. Re-measure across all notes before trusting.
- c6's drive probe tripped on the louder breath bed instead of tone
  (probe flaw) — its drive was never rebisected.
- Upper-register behavior unexamined (ref phrase sits D4–A4).
