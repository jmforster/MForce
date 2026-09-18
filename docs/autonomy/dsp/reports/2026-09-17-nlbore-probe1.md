# dsp run — 2026-09-17 — nonlinear bore probe round 1

Spec: `docs/superpowers/specs/2026-09-17-nonlinear-bore-design.md`.
Literature: `docs/research/nonlinear_bore/MSALLAM_DIGEST.md`.
Generator: `tools/gen_nlbore_probe1.py`. Zero engine code touched.

## What was built

A one-way, out-of-loop wave steepener made only of existing nodes, appended
to the output of an existing patch:

```
e ──► NL_in  (CombinedSource ×gain)
        │ ref                          tap
        ├────────► NL_delay ◄──────── NL_curve (CurveNode, points/linear)
        │          (DelayLine)         ratio = 1 − depth·x
        └────────────────────────────► NL_bell (Biquad, RBJ lowpass, optional)
                                       └──► NL_out (×trim) ──► graph output
```

Three wiring facts that were load-bearing and are worth keeping:

1. **`NL_curve` reads `NL_in` through a `{"tap": ...}` edge, not a second
   `{"ref": ...}`.** The loader's shared-source rule makes the *first node in
   JSON order* that refs a node the advancer; a tap never advances and never
   touches the usage counter. With a plain second ref, whichever of curve or
   delay was declared first would advance `NL_in`, and because
   `DelayLineSource::next()` pulls `source_` before `ratio_`, the modulator
   would have sat one sample ahead of the audio it modulates. With the tap,
   `NL_delay` is the sole advancer and `NL_curve` sees exactly the sample
   being written. (Verified by the null: exactly lag 400, not 401.)
2. **Node order still matters for construction** — `resolve_param` looks the
   ref up in already-built nodes, so the order is `NL_in`, `NL_curve`,
   `NL_delay`, `NL_bell`, `NL_out`.
3. **`frequency` pinned to 120 Hz, `compensate` false.** Length =
   `sampleRate/frequency × ratio`, so D = 400 samples = 8.33 ms ≈ a 2.9 m
   bore at 48 k — inside the 2–3 m cylindrical slide the JASA'96 paper
   measures. compensate is a tuned-loop-member feature; this element sits
   outside every loop.

The ratio law is three CurveNode knots at x = −2, 0, +2 carrying
1+2·depth, 1, 1−2·depth. CurveNode points mode is an exact lerp at
smoothness 0.5 and clamps outside the knot span, so the law is exactly
`1 − depth·x` in range and saturates (rather than running away) outside it.

Bell lowpass = `Biquad` in Raw mode with RBJ lowpass coefficients computed in
Python. `Biquad`'s Resonance mode is a two-pole resonator, not what the spec
wanted; Raw mode takes b0/b1/b2/a1/a2 as settings, all inside the descriptor
ranges at the corners used.

## Gate results

**Depth-0 null (dead control).** `sine_bypass` vs `sine_d000`, best lag
searched over D±8:

    residual −231.0 dB re signal rms, at lag 400 — bit-identical.

Gate was ≤ −80 dB. It is exact because at depth 0 the read offset is an exact
integer (48000/120 = 400) so the fractional interpolator contributes nothing.

**Dose-response monotonicity.** 233.08 Hz sine, amplitude 0.5, 1 s analysis
window. THD = sqrt(Σ harmonics 2..40²)/H1.

| depth | THD | THD dB | spectral centroid Hz | rise/fall slope ratio |
|---|---|---|---|---|
| 0.000 | 0.000019 | −94.3 | 233.1 | 1.000 |
| 0.005 | 0.015257 | −36.3 | 233.1 | 1.060 |
| 0.010 | 0.030540 | −30.3 | 233.3 | 1.121 |
| 0.020 | 0.061275 | −24.3 | 234.0 | 1.251 |
| 0.040 | 0.124114 | −18.1 | 236.6 | 1.541 |
| 0.080 | 0.261463 | −11.7 | 248.2 | 1.892 |

Monotonic in THD **and** in centroid. THD is very nearly exactly linear in
depth (3.05 per unit depth over the first four steps), and the rise/fall
slope ratio climbing 1.00 → 1.89 is the waveform asymmetry itself: this is
steepening, measured in the time domain, not inferred.

**Level dependence (the brass signature).** Same cell, depth 0.040, two input
gains:

| input amplitude | THD | centroid Hz |
|---|---|---|
| 0.10 (pp) | 0.024426 | 233.2 |
| 0.90 (ff) | 0.232194 | 245.1 |

9.5× the distortion for 9× the level — proportional, as the mechanism
requires. Gate was "ff > 3× pp".

**Sign flip.** depth +0.040 vs −0.040: THD 0.124114 vs 0.124118 (identical),
rise/fall slope ratio 1.541 vs 0.649 = 1/1.541. Expected and correct: for a
steady sine, flipping the sign of the modulation is a half-period time shift,
so the magnitude spectrum cannot change; what changes is *which* edge
steepens. Documented as such rather than reported as a null result.

**Feature audibility (WORKFLOW gate, the REVIEW-64 lesson).** Level-matched
difference against each cell's own dead control, floor −26 dB:

| cell | diff vs control | centroid vs control |
|---|---|---|
| flute_d030 | −4.8 dB | 264 vs 227 Hz |
| flute_d060 | +0.7 dB | 405 vs 227 Hz |
| flute_d030_bell | −4.9 dB | 240 vs 227 Hz |
| oboe_d030_bell | +1.2 dB | 845 vs 1072 Hz |
| oboe_soft_d030 | +2.3 dB | 1121 vs 1072 Hz |
| oboe_loud_d030 | +1.9 dB | 1705 vs 1072 Hz |

All pass with enormous margin. Caveat recorded in the queue README: readings
at or above 0 dB mean the waveform has been re-phased enough that the
difference exceeds either signal, so the number saturates — it says "grossly
different", not "n times better".

**Level ceiling / audibility floor / loudness.** Max 0.5 s-window rms across
the 8 ears cells = 0.367 (ceiling 0.5). Every cell peak-normalised to
−6 dBFS (measured peaks 0.501–0.504), so the soft/loud A/B is loudness-matched
and the difference Matt hears is timbre. No cell near the silence floor.

## The honest finding: this is phase modulation, not shock formation

The spec predicted "a sine steepening into a sawtooth-like wave = textbook
signature". **That is not what happens, and it should not have been
expected.** Measured normalised harmonic amplitudes:

| depth | H1 | H2 | H3 | H4 | H5 |
|---|---|---|---|---|---|
| 0.020 | 1.0000 | 0.0612 | 0.0018 | 0.0000 | 0.0000 |
| 0.040 | 1.0000 | 0.1239 | 0.0075 | 0.0003 | 0.0000 |
| 0.080 | 1.0000 | 0.2595 | 0.0316 | 0.0026 | 0.0002 |

A sawtooth is 1/k (0.500, 0.333, 0.250…). What this is, is a Bessel ladder.
A lumped delay of D seconds modulated by its own input gives, for a sine of
amplitude A at f0, exactly phase modulation with index

    β = 2π·f0·D·depth·A  radians

At depth 0.040, A = 0.5, f0 = 233.08, D = 8.333 ms → β = 0.2441 rad, and
J1(β)/J0(β) = 0.1233 against a measured H2/H1 of 0.1239. The agreement is
to three digits; the element is behaving exactly as the algebra says.

This is **not a defect and not a surprise** — Cooper & Abel say so in the
source (digest §7): with the shock discard omitted, "the effect becomes
equivalent to phase/frequency modulation", and the spec explicitly chose to
omit shock handling. But it does mean the probe cannot be described as
producing shocks, and I am not going to describe it that way.

Confirmed with a third measurement not in the original spec, because it
tests the literature's own severity criterion. JASA'96 (digest §2.6) says
the shock-formation distance depends on the maximum pressure **rise rate**,
not on p/P_at. β ∝ f0 is exactly that statement in this element's algebra.
Same depth, same amplitude, three octaves of source tone:

| source tone | THD | H2/H1 | β predicted (rad) |
|---|---|---|---|
| 233 Hz | 0.12411 | 0.1239 | 0.2441 |
| 466 Hz | 0.25472 | 0.2528 | 0.4882 |
| 932 Hz | 0.48715 | 0.4699 | 0.9763 |

Doubling the source frequency doubles the distortion. **Consequence for the
campaign: this element rewards bright, fast-moving source material and does
very little to a smooth, dark one.** That is visible straight through to the
musical cells — the oboe (centroid 1072 Hz dry) moves its centroid by
+633 Hz, the flute (227 Hz dry) by +37 Hz at the same depth. It also means a
real brass build needs the source nonlinearity first (the lip closing hard,
digest §1.1) before the propagation nonlinearity has much to bite on. That is
the physical reason the sources insist the lip valve matters.

## Musical cells (the ears queue)

`depth` was calibrated against the literature: each patch's dry output is
normalised to peak 0.8 before the steepener, so the delay swing is
depth×0.8×D samples. depth 0.030 → 2.4% of D = 9.6 samples, inside the 1–3%
band the sources give for a trombone slide at ff. depth 0.060 is deliberately
~2× past physical.

| cell | patch | depth | drive | bell Hz | >1 kHz | >2 kHz | centroid Hz |
|---|---|---|---|---|---|---|---|
| flute_ctl | flute1 | 0.000 | loud | — | 0.0112 | 0.0001 | 227 |
| flute_d030 | flute1 | 0.030 | loud | — | 0.0235 | 0.0037 | 264 |
| flute_d060 | flute1 | 0.060 | loud | — | 0.0696 | 0.0115 | 405 |
| flute_d030_bell | flute1 | 0.030 | loud | 1200 | 0.0100 | 0.0002 | 240 |
| oboe_ctl | oboe1 | 0.000 | loud | — | 0.6200 | 0.0369 | 1072 |
| oboe_d030_bell | oboe1 | 0.030 | loud | 2000 | 0.2896 | 0.1089 | 845 |
| oboe_soft_d030 | oboe1 | 0.030 | soft | — | 0.5991 | 0.0940 | 1121 |
| oboe_loud_d030 | oboe1 | 0.030 | loud | — | 0.5068 | 0.3622 | 1705 |

The A/B that answers "does forte get brassy?" is `oboe_soft_d030` (drive
×0.3) vs `oboe_loud_d030` (drive ×1.0): identical patch, identical effect,
identical playback loudness, centroid 1121 vs 1705 Hz and >2 kHz energy
0.094 vs 0.362.

`flute_d030_bell` is worth flagging as a near-miss: a 1.2 kHz lowpass on a
patch whose dry centroid is 227 Hz removes essentially all of what the
steepener added (>1 kHz 0.0100 vs the control's 0.0112). The bell axis only
has room to say anything on the oboe. Noted in the queue README.

`flute1` ships a single 3 s note; the ears cells give it three (MIDI 41/53/65,
verified at 86.9 / 173.8 / 348.5 Hz before use) so the audition is not one
pitch. `oboe1`'s own 5-note C3–C7 score is unchanged.

## Nothing failed, nothing was substituted

Both spec'd mechanisms worked on the first attempt: the ratio pin accepts a
CurveNode-driven modulation, and the tap keeps modulator and audio in phase.
No cell was culled by a gate. No engine file was opened for editing.

Things I would have had to report as failures and did not have to: the
`ratio` descriptor range (0.05–20) is metadata only — `DelayLineSource`
applies no runtime clamp to `ratio_->current()`, and the loader applies no
clamp to params or settings, so the ratio law is delivered as written.

## Dirs (the gen-script-owns-dir rule)

* `patches/sweep/nlbore_probe1/` — 20 patches (8 ears + 12 measurement)
* `renders/dsp/sweep/nlbore_probe1/` — 12 measurement WAVs (not for ears)
* `renders/dsp/audition/nlbore_probe1/` — 8 ears WAVs + README.md

All three are purged to the current cell set on every run. Calibration
renders and their patches are deleted after use. Nothing was written to
`patches/pending/` or `renders/*/pending/`.

## Next, if Matt likes it (all from the spec's Round 2 list)

1. The faithful Tassart element as a node: input-indexed delay via the
   d⁺→d⁻ implicit conversion (digest §2.3–2.4). The lumped audio-delay form
   measured above is the approximation; the IRCAM structure filters the
   *delay* signal and inverts g at the output, which is a different algorithm
   and is where shock handling can be spliced in.
2. Oversampling, so the added top octave is physics rather than linear-interp
   artifact.
3. In-loop placement (Acta Acustica variant 2).
4. Given the rise-rate finding above, a lip round probably outranks both —
   the steepener has little to work with until the exciter produces a hard
   edge.
