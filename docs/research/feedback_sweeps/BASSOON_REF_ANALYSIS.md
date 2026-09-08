# bassoon_attempt vs real bassoon (YouTube ref) — 2026-09-07

Reference: YouTube _t2q0lsUl4k (Ode to Joy in D, octave below the
clarinet session — D3–A3). Method per OBOE/CLARINET_REF_ANALYSIS
(research/ml_ears pipeline, pitch floor lowered to 70 Hz). Matched note:
F#3 (185.9 Hz, 1.06 s). Cells and table: the bassoon_ref1 audition
README. Base: Matt's bassoon_attempt (oboe_double_reed lineage with a
one-sided reed-closure junction — positive side returns to zero by
x≈0.56, the first genuinely reed-shaped table in the family).

## What "honk" is, measured

The ref's spectrum PEAKS at H3: H2 +12, H3 +19, H4 +7 dB rel H1 — a
~500–540 Hz formant hump that dominates regardless of fundamental (LTAS
peak 539 Hz, secondary 990). The bassoon fundamental is WEAK; the honk
is that hump. Matt's looked-up 480 Hz was right (though the edit never
reached the saved file). Ref attack: 31 ms — the bassoon speaks fast.

## The finding: one damp knot fixed tone AND attack

The damp keytrack (a=4 @ 440, linear) extrapolates to ~4.7x f0 at D3 —
cutoff ~600–690 Hz, gutting H5+ that the ref holds at −1..−15 dB. Adding
one knot (a=7 @ 150 Hz) plus the formant moves (500 res 10 / 990 res 8):
honk error 23→7.9 dB, H5–H8 error →7.1, and **attack 344→51 ms with no
envelope changes** — the choked loop had been carrying so little
round-trip gain at low register that bloom crawled. Register-dependent
damp chokes masquerade as articulation problems; check the keytrack
extrapolation at the bottom of an instrument's range before touching
envelopes. (The clarinet session's seconds-mode articulation cells were
NOT needed here — b3's re-anchored band came out softer than the
original and regressed.)

## Open

- Render ~10 dB cleaner than the ref on the matched note (breath axis).
- Vibrato retune (5.6 Hz, half depth) measured best honk but inherited
  b3's soft band — fold into b2 by hand.
- Ref pitch-tracker artifacts at ~497 Hz (formant-hump lock) — ignore
  those segments in future runs.
