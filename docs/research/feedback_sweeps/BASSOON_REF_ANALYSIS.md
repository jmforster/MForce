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

## Rounds 2–4 + the clip hunt (same day, after Matt's "raucous" verdict)

Matt heard every round-1 cell as raucous ("sounds like clipping") on held
notes, with all his fixes trading raucousness for darkness. Rounds 2–3
falsified the in-loop and dry-leg theories (softening the reed fold or
lowpassing legs changed nothing above 2 kHz — the excess was invariant
to every filter). The octave-band map then showed the ref is ~a 500–1k
bandpass both ways; a bandpass body (Formant1 mode 2 @ 550, dry leg
muted) matched sub-2k within ±2 dB (r7).

The invariant top-octave excess was then run to ground: **it WAS
clipping.** The raw mix peaks ~5.7x full scale; at instrument volume 0.5
soft_clip saturates on every note. The tell: every render peaked at
exactly 0.700 = 0.999 clip ceiling x 0.7071 equal-power center pan.
Volume 0.15 (r10): sustained top octaves drop ~30 dB, sub-2k unchanged.
Lessons recorded:
- **A WAV peaking at ~0.700 from the CLI means the mix is PINNED at the
  clipper** (0.999 x pan 0.7071), not "comfortably at 70%".
- Loop patches with hot junction clamps (this one reaches |y|=1.44) need
  instrument.volume set for clipper headroom; every tone knob otherwise
  doubles as a distortion knob and tuning becomes impossible.
- Candidate housekeeping/engine item: a loud stderr warning from the
  render path when soft_clip engages for more than a few samples.

## Plain-language lessons (Matt-requested, 2026-09-07 session end)

1. **If a render's loudest moment reads exactly 0.70, the sound is being
   squashed by the safety limiter.** (0.70 = the limiter's ceiling times
   the stereo-center scaling.) Fix: turn instrument volume down until the
   peak reads well under 0.7. A squashed patch adds a crackly/raspy edge
   that NO tone knob can remove — every knob just trades loudness for
   grit, which is why tuning feels impossible.
2. **A slow attack on LOW notes can be a damping problem, not an
   envelope problem.** The damp filter's keytrack decides how much energy
   the loop keeps per cycle; at the bottom of the range the loop can be
   so starved it takes half a second to speak. Raising the low-register
   damp cutoff fixed a 344 ms attack down to ~50 ms with the envelopes
   untouched. There is a knee: below a certain cutoff nothing changes,
   above it the attack snaps fast.
3. **The big fundamental is doing a job: it MASKS the loop's rough
   edges.** Filtering the output down to just the "honk" band (like the
   real bassoon's spectrum suggests) exposed crackle that was always in
   the circulating signal but inaudible under the fundamental. Lesson:
   add the honk as a BOOST on the full sound; don't strip the sound down
   to the honk.
4. **The measurements find addresses, ears pick furniture.** The
   pipeline correctly located the honk (~500-540 Hz), the attack knee,
   and the clipping — but every "optimized" patch lost to Matt's
   hand-tuned one. Use the numbers to find WHICH knob and WHERE; use
   ears to set it.
5. **Short notes get squeezed articulation** (fraction-mode envelopes
   scale with note length; the loop's bloom time doesn't) — the same
   lesson as the clarinet session, and the reason duration-aware
   articulation (backlog 64) keeps coming up.
6. Tooling: junction curve `values` are (x,y) point PAIRS; the analysis
   scripts must not run their sweeps on import; single-note metrics are
   noisy — average across the phrase before believing a 3 dB claim.

## Open

- Render ~10 dB cleaner than the ref on the matched note (breath axis).
- Vibrato retune (5.6 Hz, half depth) measured best honk but inherited
  b3's soft band — fold into b2 by hand.
- Ref pitch-tracker artifacts at ~497 Hz (formant-hump lock) — ignore
  those segments in future runs.
