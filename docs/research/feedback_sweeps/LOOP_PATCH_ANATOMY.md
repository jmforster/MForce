# Loop patch anatomy — parts, mechanisms, and search axes

Proto-documentation for the instrument-search and novelty-search lane MDs.
Reference patch: **patches/library/winds/oboe_default.json** (node names below
are its labels). Sources: Matt's hands-on pass 2026-09-06 (Replace-with
excitation subs + node-by-node audit), the junction mechanism session
(JUNCTION_OPERATING_POINT.md, 2026-09-04), and the r4/r5 sweep record.

## The circuit

```
Breath_noise ──┐
               v
              sum ──> Junction ──> dcblock ──> damp ──> delay ──┐
               ^      (Shaper)     (HP 12Hz)   (LP,     (pitch) │
               │                               keytrk)          │
               └────────────────── tap ─────────────────────────┘
                                                        │
                     output: delay ──> SVF ──> SVF2 ──> Combined ──> Reverb
                                       (dry + 2.04x formant branch)
```

Drive path (control, not in the signal cycle):
`Drive_env → Vibrato → Junction.drive`. The family carries TWO vibratos
(hence the doublev_tweaked lineage): a drive vibrato wobbling the junction
operating point (±depth 0.25 at 6.7 Hz, 0.1 s onset — since drive controls
even/odd balance, this is a timbre/loudness pulsation) and a pitch vibrato
on the delay frequency. The committed oboe_default.json carries the drive
one; Matt's working versions carry both (Drive_vib + Pitch_vibrato).

Timing: the loop runs one round trip per period of the note. Everything
in-loop compounds per trip; everything post-loop is ordinary series
processing. `delay.compensate=true` keeps the note in tune by shortening the
read length by the tap's one-sample lag plus every loop member's
phase_delay_at(f0) — this is why you can put filters in the loop without
detuning it.

Two regimes govern almost every observation below (junction doc):

- **Ignition**: round-trip small-signal gain = drive x junction slope at
  origin x filter losses. Below 1, the loop is a damped resonator and you
  hear excitation only; above 1, oscillation grows exponentially, one trip
  per period, so bloom time goes with log(gain). The breathy attack IS the
  sub/near-threshold window.
- **Equilibrium**: amplitude settles where average round-trip gain = 1 —
  excursions ride up the curve until the flattening knee eats the excess.
  Level regulator and tone generator are the same object, and a settled
  loop actively squelches small perturbations (knee compression), which is
  why excitation changes are subtler than expected once the note is ringing.

## Part by part

### Excitation — Breath_noise (+ Breath_curve)

Seeds the loop before ignition and perturbs it forever after. Amplitude is
keytracked by Breath_curve: power form, b = -1.0 at x0 = 440 (amp ~ 440/f)
— the r5a finding (keeper exponents -0.80..-0.95) that flattens the 56x
C3→C7 hiss ramp.

Why substitutions are audible but subtle (Matt 09-06, via Replace-with):
the settled loop suppresses small inputs, so excitation character shows
mainly (a) in the pre-ignition window and (b) as whatever survives the
squelch during sustain.

- **Pink/red for white softens the tone**: the loop is fed less HF energy;
  upper modes get a weaker seed and weaker ongoing perturbation.
- **Chaotic red (smoothness 0, rampVariation 0.5) leaves a persistent
  noise bed**: larger, burstier excursions punch through perturbation
  suppression, and the bed you hear is that noise circulating through the
  loop's comb/damp response, not the raw generator.
- **Tonal excitation (sine/saw/etc.) drives the loop fine, but you hear
  the generator's pitch briefly before ignition** ("reaches critical" =
  round-trip gain crossing 1; the junction doc calls it ignition). The
  generator reaches the output on the first pass; the loop's own tone
  needs tens-to-hundreds of trips to dominate. Off-pitch components are
  comb-notched by the loop, which is why wiring the generator's frequency
  to the note collapses back to "original patch minus breath."
- **Crackle didn't work** — backlog 58; suspected (unverified): sparse
  impulses under-feed the loop between hits with drive at threshold.

### Drive path — Drive_env → Vibrato → Junction.drive

The most misunderstood part, now explained (junction doc "trap"):

- **Envelope stage values are 0..1 normalized and map through the
  envelope's own minValue/maxValue.** oboe_default's band is
  0.533–0.7297. Threshold for the stock curve is ~= 1/slope0 adjusted for
  damp losses — approximately where minValue sits. So the ENTIRE stage
  range lives at-or-above ignition: the loop lights at t=0 no matter what
  the stages do, and stage edits (attack length, the accidental 0.05
  sustain, the extraneous zero-length stage) move gain logarithmically at
  best. This is why Drive_env "does next to nothing" in the stock patch.
- **minValue is the ignition/articulation knob**: it sets the starting
  distance from threshold. Tune down until the note falters, back up until
  it doesn't — the by-ear version of the sweep harnesses' criticals
  bisection. Drop it clearly below threshold and the attack stage becomes
  audible again as the moment its rise crosses threshold (the
  junc_tweaked recipe: minValue 0.3 under a steepened curve).
- **maxValue / stage shape = timbre trajectory, not amplitude**: drive
  scales the window of the curve the signal sees, which sets brightness
  AND even/odd balance. The r5b overshoot/tongue shapes act here.
- **sum.gainAdj** (0.0 in stock) is an independent balance between
  feedback (source2, the tap) and excitation — a loop-gain trim that
  doesn't touch the breath level.

### Junction — the Shaper

Covered by JUNCTION_OPERATING_POINT.md; summary: y = curve(drive * x),
local slope = gain, equilibrium at average round-trip gain 1, origin
asymmetry = even harmonics at all drive levels, clamp asymmetry = evens
only at high drive. One new finding (Matt 09-06): **too-steep a slope near
origin causes aliasing on high notes** — same mechanism as damp resonance
below: the shaper widens the spectrum, nothing oversamples the loop, and
harmonics pushed past Nyquist fold back inharmonically. High notes suffer
most because folded partials land far from the sparse harmonic grid.

### dcblock — SVF mode Highpass1P, 12 Hz

What the name suggests, and why the loop needs one: an asymmetric curve
rectifies — every pass deposits a small DC component — and nothing else in
the cycle removes DC (the damp lowpass and the delay both pass it freely).
Recirculating DC shifts the signal's operating window off curve-center,
which changes even content and can choke or latch the oscillation. The
12 Hz one-pole drains it at ~zero cost to a 30+ Hz fundamental.

Why removing it seemed inaudible (Matt 09-06): the stock curve is nearly
odd-symmetric in the moderate-drive window (measured — evens don't grow),
so there's almost no DC to drain. It becomes load-bearing exactly as
origin asymmetry increases — i.e., on the r5c axis and on
oboe_dv_junc_tweaked, which is where any "is dcblock necessary" test
should run. Note: mode 4 is a 1-pole; **its resonance param (0.699) is
ignored** in 1P modes.

### damp — SVF lowpass, cutoff <- keytrack (6 x f0), resonance 0.825

The brightness organ. In-loop filtering compounds: after N trips a
harmonic is scaled by response^N, so the sustained spectrum converges hard
below cutoff and small cutoff changes swing mellow<->strident (Matt:
"dramatic"). The keytrack (linear, a = 6.0) parks the cutoff near the 6th
harmonic at every pitch — roughly constant harmonic count across the
keyboard; this is the register-consistency knob.

Resonance = per-pass emphasis near cutoff, i.e. a formant the loop
re-applies every period:

- 0.825 (stock): the oboe "honk" near the 6th harmonic.
- 0.5: **the parameter's floor** (SVF res range is 0.5–40). No peak →
  no honk, and lower gain near cutoff → slower buildup = the "tentative"
  attack.
- ~1.0+: peak gain above unity concentrates loop gain at cutoff →
  screech; more HF into the shaper → the same aliasing failure as a
  steep junction, worst on high notes where 6 x f0 approaches Nyquist.

### delay — DelayLine (the resonator)

- Length = sampleRate/frequency x ratio − compensation. The loop's pitch
  is 1/length, so **ratio 0.5 = octave up, 2 = octave down** (ratio scales
  the period, not the pitch).
- **amplitude (<- ampenv) is a read-side LOOP-GAIN control, not a volume
  envelope** — the header calls it the KS loss factor. Everything reading
  the delay (the closing tap AND the output chain) sees it. Consequences:
  - Values below 1 are per-pass losses that compound at the note rate;
    the audible effect of a change depends on how much energy the loop has
    already stored, and on which side of unity the TOTAL round-trip gain
    lands. That threshold-y, history-dependent behavior is why ampenv
    edits feel inconsistent ("possibly drugs" — no: state).
  - The release stage is how notes end: gain drops below unity and the
    loop rings down through the damp filter. Ampenv release = damped
    ringdown time, not a fade.
  - An attack stage from 0 gates ignition (no feedback until it rises)
    AND mutes the direct path — the earlier slow-attack observation is
    plausible but wasn't reproducible; ampenv fraction-mode stage
    semantics deserve one directed test before trusting any of it.
- compensate = true: see circuit note; requires the closing tap to target
  the delay (the loop-skeleton rule).

### Post-loop — SVF (1500 Hz, res 5) -> SVF2 (3000 Hz, res 23) -> Combined -> Reverb

Body coloration, outside the compounding regime: two strongly resonant
lowpasses in series form a fixed formant branch, summed 2.04x against the
dry delay signal, then 20% reverb. Because these are post-loop they color
every note identically and don't affect ignition/stability at all —
which is what makes the feedback_inloop1 experiment (same formants moved
inside the loop) a genuinely different animal.

## Aliasing in the loop — not a losing battle

Every alias complaint (steep junction slope, damp res near 1, high notes)
is one mechanism: the shaper widens the spectrum every pass, nothing
bandlimits it, and folded products land inharmonically — and once folded
into the passband they RECIRCULATE, so the damp filter can't retroactively
remove them. Mitigation ladder, cheapest first (Matt 09-07 question):

1. **Quantify it free**: render the same patch at 192 kHz and resample to
   48 kHz offline; the difference against the stock render is exactly the
   aliasing. Tells us how much of the "screech" is folding vs actual
   loop chaos before we build anything. (Render is 378x realtime — even
   4x rates stay far above realtime.)
2. **Keytrack knots** (no features): cap/bend the damp-cutoff expression
   in the top octaves — Matt's octave-7 finding (multiplier 2 instead of
   6 is fine up there) is this fix, expressible today as a knot on the
   keytrack curve. Reduces generation, not the folded residue.
3. **ADAA on the Shaper** (medium feature, the real candidate): first-order
   antiderivative antialiasing — evaluate the curve's antiderivative
   difference instead of the curve. Standard VA practice for memoryless
   waveshapers, works inside feedback loops, and its ~half-sample lag is
   exactly what the DelayLine compensation walk already knows how to
   absorb (phase_delay_at). Piecewise curve → analytic antiderivative.
4. **Oversampled loop region** (heavyweight): run the loop subgraph at
   2–4x internally, decimate at the boundary. Only if ADAA falls short.

## What this buys the two lanes

| Part | Instrument-search axes (params, existing topology) | Novelty directions (features/topology) |
|---|---|---|
| Excitation | noise color, keytrack exponent, chaos settings (smoothness/rampVariation) | non-noise drivers: one-shots (halfhump), pitched/locked seeds, another loop's output; Crackle (backlog 58) |
| Drive | minValue vs measured threshold (articulation), maxValue/stage shape (timbre trajectory), drive-vibrato depth/rate | non-monotonic drive (re-ignition, flutter), near-audio-rate drive modulation (sidebands via the nonlinearity), stochastic drive (wandering even/odd), state-coupled drive (envelope follower = self-regulating breath — topology) |
| Junction | origin-asymmetry ratio, drive floor vs threshold (the r5c directed axes), smoothness, clamp shapes | morph-pin time-varying curves (shipped; Matt's first probes 09-06 hit the steep-slope→aliasing limit); **hysteresis junction (stick/slip state — the bowed-string feature)**; resonant valve (in-loop bandpass near f0 feeding the junction — the brass/lip candidate, possibly existing primitives); 2D junction (drive as second curve axis) |
| Loop core | damp cutoff multiple + res, keytrack form, ratio, ampenv release | what else lives in-loop (inloop1 formants; allpass dispersion; modulated delay = ModDelayLoop); coupled/multiple loops |
| Post | formant placement/res, dry/wet balance | in-loop vs out placement as a lane question, not a knob |

Cross-cutting, from Matt 09-07: **both lanes eventually need both columns.**
Instrument search starts as parameter sweeps on the known skeleton, but the
skeleton is near-exhausted by hand — brass and bowed strings likely need the
feature column (lip = resonant valve, bow = hysteresis), and the loop's
memoryless junction is the mechanism reason every attack so far lands on
the tube-to-reed spectrum regardless of curve shape. Novelty search
likewise sweeps (chaotic-excitation settings, drive stochastics) inside
new topologies once one shows life.

Escape hatch for sustains the loop can't reach (Matt 09-07): graft a
different engine for the sustain stage — CrossfadeSource(loop, Additive)
exists as a primitive. The bar is that the loop must come close enough
that the handoff doesn't read as a crossfade; unexplored.

## Skeleton taxonomy (2026-09-11, from the bow rounds' framing correction)

The "one skeleton, different junction curve" framing was TRUE at the
MSW level (nonlinearity + resonator + feedback) but overcompressed:
instrument identity also lives in the junction's SEAT - where it sits
on the resonator, how energy enters, what the output tap is. Two
chassis, not four:

WIND CHASSIS (proven - flute/oboe/clarinet/bassoon live here): valve
at the END of a bore, breath = pressure source into the medium
(additive noise in-loop is CORRECT here), single delay loop, output
through bell/formants.
- Reed: valve resonance far above playing range -> memoryless junction
  suffices. The bore picks the note.
- Jet/flute: jet junction, same seat.
- BRASS = wind chassis + four deltas: (1) resonant OUTWARD-striking
  valve near the note (2D junction/state roadmap) - the LIP picks
  which bore mode speaks; (2) mode-selection architecture: delay at
  the BORE fundamental (long - a trumpet C5 rides a ~C3 bore), lip
  resonance selects harmonic n; valve1's "mode pulling" was this
  trying to happen; (3) bell = complementary crossover (loop keeps the
  lowpass REFLECTION, output IS the highpass TRANSMISSION - one
  signal, two filters, existing primitives); (4) fixed mouthpiece-cup
  bandpass ~300-1000 Hz, NOT keytracked. Loud brassiness = shock
  steepening, approximable as mild distributed in-loop shaping.

STRING CHASSIS (not yet built): junction at an INTERIOR point - two
delay segments (bridge side p, nut side 1-p; bow position = the split
= the bowing-point comb), silent at rest (NO additive noise in-loop:
all energy enters through friction - bias x grip; bow-hair noise =
modulation of breakaway/bias, not a summed signal), gentle HF-tilted
losses near unity, output = bridge-side force into body resonances.
Key mechanism hope: the returning Helmholtz corner strikes the
junction once per period - the natural one-slip-per-cycle trigger the
single-loop retrofits never had (their stick/slip cycles ran period-2:
the note was a SUBHARMONIC of the loop, hence the two-octave
overblown-wind character of bow rounds 2-4).

Retraction (Matt 2026-09-11): the hysteresis octave-doubling is NOT an
overblow novelty - oboe_default does real overblowing well already.
