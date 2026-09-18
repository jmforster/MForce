# dsp run — 2026-09-17 — nonlinear bore × lip exciters (the brass pairing round)

Spec: `docs/superpowers/specs/2026-09-17-nonlinear-bore-design.md` (first step
of its §Round 2 direction). Predecessor: `2026-09-17-nlbore-probe1.md`,
REVIEW 66, commit `1a11064`, Matt's verdict "It definitely brightens in a
brassy way." Generator: `tools/gen_nlbore_brass1.py`. **Zero engine code.**

Carrier notes: `docs/research/feedback_sweeps/BRASS_HARNESS_NOTES.md`,
`docs/research/stk_port/STK_PORT_NOTES.md`.

## What was built

The probe's steepener, wired verbatim (the `{"tap"}` edge on `NL_curve` is
load-bearing — a second `{"ref"}` puts the modulator one sample ahead of the
audio), pinned to the same D = 400 samples at 48 k, hung on the two
lip-driven carriers we have:

| id | patch | anchor node | register rendered |
|---|---|---|---|
| lip | `patches/audition/brass_harness1/br1_t100_q15_m3.json` | `Bore` | C3 C4 C5 |
| stk | `patches/sweep/stk_brass_port/stk_brass_default_canon.json` | `Slide` | C4 C5 |

**The one deliberate departure from the probe: placement.** The probe bolted
the chain on the graph *output*, because flute/oboe have no bore-and-bell
anatomy. Both brass carriers do, and the sources put the steepening inside
the slide, upstream of the bell. So the chain is inserted immediately after
the bore delay and every `{"ref"}` consumer of that delay is repointed at
`NL_out`; the loop's own `{"tap"}` on the delay is left alone, which keeps
this the one-way out-of-loop variant the probe validated.

* lip: `Bore → [chain] → Radiated` (bell highpass) `→ Reverb`
* stk: `Slide → [chain] → output` (the STK port has no bell stage at all —
  which is why this round's single bell-lowpass cell lives on that carrier,
  not on the lips patch that already has one)

## Note range — rendered what each carrier can actually play

The task's default was 5 notes C3–C7. Neither carrier speaks across that, so
neither got it, and the queue README says so. Measured before building
anything (bare renders, sustained window, spectral-peak f0):

| carrier | C3 | C4 | C5 | C6 | C7 |
|---|---|---|---|---|---|
| lip | locked, −34 c | locked, −12 c | locked, +22 c | locks an octave low | locks an octave low |
| stk | silent | locked, +65 c | locked, +15 c | locked, −5 c | silent |

The lip harness's locked span is the `t100` column's C3–C5 exactly as the
harness notes record it. Note the notes say "+22–38 c static sharp"; measured
here the error is not a constant offset — it runs from 34 cents FLAT at C3 to
22 cents SHARP at C5. Stated plainly in the queue README as the carrier's
flaw, not the effect's; not touched this round.

STK canon was rendered C4+C5. C6 does lock (−5 c) but the STK port notes only
validated C4/C5 after the low-shelf DC fix, so I stayed inside what is
validated. It did not fight — it rendered clean on the first attempt, no
retries used.

## Gate results — all pass

**Depth-0 null (dead control).** −218.8 dB (lip) and −212.3 dB (stk) re
signal rms, both at exactly lag 400. Bit-identical, so the steepener at
strength zero is provably inert and the dead controls are the untouched
carrier — including through the downstream bell filter and reverb, which
confirms the insertion is a clean series splice.

Worth recording how this was gotten wrong first: the initial reference render
used unity drive gains while the depth-0 ladder cell used 0.911/1.098, and
the null floored at −69 dB on 16-bit rounding of that gain pair. Re-rendering
the reference with the *same* gains took it to bit-identical. A null gate is
only as good as what it holds constant.

**Dose-response (lip carrier — the gated one).** Drive normalised to peak
0.8 before the element, so `depth` means the same thing on both carriers;
sustained portions of all three notes, concatenated.

| strength | centroid Hz | energy >1 kHz | energy >2 kHz |
|---|---|---|---|
| 0.0000 | 1114 | 0.4312 | 0.1116 |
| 0.0075 | 1134 | 0.4351 | 0.1157 |
| 0.0150 | 1209 | 0.4969 | 0.1939 |
| 0.0300 | 1396 | 0.6941 | 0.2903 |
| 0.0600 | 1807 | 0.9321 | 0.3113 |
| 0.1200 | 2528 | 0.8698 | 0.5178 |

Monotonic in centroid and in >2 kHz energy, no exceptions.

**Level dependence (the brass signature), lip carrier at strength 0.030.**
Quiet drive (×0.3) → 12.7 % of energy above 2 kHz; loud drive (×1.0) →
29.0 %, a factor of **2.28**, centroid 1146 → 1396 Hz. Both cells
peak-normalised to −6 dBFS, so that is timbre at matched loudness.

**Pitch-lock preservation.** Every cell holds its carrier's tuning to within
a cent at every strength (table in the queue README). The element is outside
the loop and measurably does not tune it.

This gate also had to be rebuilt mid-run and the first version was wrong in
an instructive way. Autocorrelation with integer lag reported spurious
19-cent drifts — one lag bin at C5 *is* 19 cents. Parabolic interpolation on
the autocorrelation peak fixed the resolution but still reported the STK
carrier drifting up to 15 cents sharp as strength rose. A parabolic FFT-peak
estimator says the fundamental does not move at all: +65.0 cents at strength
0.000, 0.030, 0.060 and 0.120, identical to one decimal. Autocorrelation is
pulled around by changing harmonic balance, which is precisely the thing this
element changes, so it is the wrong instrument for this measurement. The
script now uses the spectral peak and the gate is 3 cents.

**Feature audibility** (level-matched difference vs each carrier's own dead
control, floor −26 dB) — all six non-control cells pass with margin:

| cell | diff vs ctl | >2 kHz vs ctl | centroid vs ctl |
|---|---|---|---|
| lip_soft_d030 | −3.8 dB | 0.1271 vs 0.1116 | 1146 vs 1114 Hz |
| lip_loud_d030 | +1.6 dB | 0.2903 vs 0.1116 | 1396 vs 1114 Hz |
| lip_loud_d060 | +2.2 dB | 0.3113 vs 0.1116 | 1807 vs 1114 Hz |
| stk_d030 | −1.8 dB | 0.2413 vs 0.1307 | 1528 vs 1216 Hz |
| stk_d060 | +2.2 dB | 0.4346 vs 0.1307 | 1998 vs 1216 Hz |
| stk_d030_bell | −1.8 dB | 0.0980 vs 0.1307 | 1040 vs 1216 Hz |

**Level ceiling / audibility floor / loudness.** Max 0.5 s-window rms across
the 8 ears cells = 0.169 (ceiling 0.5). All 8 peak-normalised to −6 dBFS,
measured peak 0.501 on every one, so the soft/loud A/B is loudness-matched.
No cell near the silence floor.

A calibration bug was caught and fixed here too: the unity-trim calibration
render clipped int16 at strength 0.060, the clipped peak mis-set the
listening trim, and `lip_loud_d060` shipped at peak 0.559 instead of 0.501.
The calibration pass now renders at 0.35 headroom and scales the reading.
That is the REVIEW-62 loudness rule biting in the other direction — worth
remembering that a peak-normalisation step can itself be the thing that
breaks level matching.

## The one thing I could not explain

On the **STK carrier only**, the two weakest strengths make it DARKER before
brighter. Band shares of the sustained signal:

| strength | <100 Hz | 100 Hz–1 kHz | 1–2 kHz | >2 kHz |
|---|---|---|---|---|
| 0.0000 | 0.0119 | 0.2920 | 0.5667 | 0.1293 |
| 0.0075 | 0.0104 | 0.3040 | 0.6005 | 0.0850 |
| 0.0150 | 0.0092 | 0.3179 | 0.5992 | 0.0737 |
| 0.0300 | 0.0075 | 0.3480 | 0.4048 | 0.2397 |
| 0.0600 | 0.0073 | 0.3276 | 0.2325 | 0.4326 |

Two facts, both measured. (1) Energy is conserved — the four band shares move
by +0.012, +0.034, −0.044, −0.002 at strength 0.0075, summing to zero. It is
a redistribution downward out of the top octave into 1–2 kHz, not a loss and
not difference tones piling up at the bottom. (2) It is **not** static
fractional-read blur, which was the obvious suspect: a fixed half-sample
delay offset costs 0.1307 → 0.1269 of >2 kHz share (0.4 points), and this
costs 4.4 points. Ruled out.

I do not have the cause. It does not happen on the lip carrier, it is gone by
the real-instrument strength, and no cell in the ears queue sits in the dip,
so I stopped at one elimination rather than burning the round on it. Recorded
here and in the queue README so it is not quietly dropped. The lip carrier's
monotonicity is what the gate is on, and it passes; the STK ladder is
reported, not gated.

## Ears queue

8 cells → `renders/dsp/audition/nlbore_brass1/` + README.md. 4 per carrier:
a dead control, a dose ladder, plus the soft/loud A/B on the lip carrier and
the bell-lowpass cell on the STK carrier.

| cell | carrier | strength | drive | bell |
|---|---|---|---|---|
| `nlb_lip_ctl` | lip | 0.000 | loud | — |
| `nlb_lip_soft_d030` | lip | 0.030 | soft | — |
| `nlb_lip_loud_d030` | lip | 0.030 | loud | — |
| `nlb_lip_loud_d060` | lip | 0.060 | loud | — |
| `nlb_stk_ctl` | stk | 0.000 | loud | — |
| `nlb_stk_d030` | stk | 0.030 | loud | — |
| `nlb_stk_d060` | stk | 0.060 | loud | — |
| `nlb_stk_d030_bell` | stk | 0.030 | loud | 2.5 kHz |

**The A/B pair: `nlb_lip_soft_d030` vs `nlb_lip_loud_d030`.** Same patch,
same strength, same playback loudness; the only difference is drive into the
element. 2.28× the energy above 2 kHz, centroid +250 Hz.

`nlb_stk_d030_bell` answers "does taming the top help" and the measurement
says the 2.5 kHz corner over-tames: >2 kHz share 0.0980 against the dead
control's own 0.1307, i.e. the bell removes more than the steepener added.
Still comfortably audible (−1.8 dB vs control), so it is a fair taste
question, but it is a darkening cell, not a brightening one.

**THE QUESTION for Matt: is this brass yet, or still saxy?**

## Honest position on what this round can and cannot settle

The element works and it works harder on lips than it did on winds: on the
oboe the probe moved the centroid +633 Hz at strength 0.030, and here the lip
carrier moves +282 Hz and the STK carrier +312 Hz — same order, on sources
that are already much brighter to begin with. The probe's rise-rate finding
(effect ∝ source brightness) is consistent with that.

But the carriers are the limit, not the bore. The lip harness covers three
notes with a tuning error that changes sign across them; the STK port covers
two and is quiet. If the verdict is "still saxy", the reading is that the
exciter is the problem — which is exactly where the probe's run report
already pointed ("a real brass build needs the source nonlinearity first").
Nothing this round measured contradicts that, and I am not going to claim the
pairing fixed it.

## Dirs (gen-script-owns-dir rule)

* `patches/sweep/nlbore_brass1/` — 22 patches (8 ears + 14 measurement)
* `renders/dsp/sweep/nlbore_brass1/` — 14 measurement WAVs (not for ears)
* `renders/dsp/audition/nlbore_brass1/` — 8 ears WAVs + README.md

All three purged to the current cell set on every run; calibration renders
and their patches deleted after use. Nothing written to `patches/pending/`,
`renders/*/pending/`, `patches/library/`, or any tree root. Stale renamed UI
exe `mforce_ui_locked_0916d.exe` swept at session start.

## Next

1. Matt's verdict on the headline question gates everything else.
2. If "still saxy": a lip round is the front, not more bore — the digest's
   S5 one-mass parameter set as the starting point (spec §Round 2, item 4).
3. If "getting there": oversampling first, so the added top octave is physics
   rather than linear-interp artifact, then in-loop placement (Acta Acustica
   variant 2).
4. Independent of the verdict: the STK darkening dip above is an open thread
   worth one focused pass if this element is going to be a general-purpose
   "brassifier" on arbitrary patch outputs.
