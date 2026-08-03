# Iowa MIS piano mf — groundwork analysis (pre-CMA-ES)

Samples: `research/inst_samples/piano_mf` (86 keys B0-C8, stereo 44.1k, mono-summed for analysis).
Script: `research/ml_ears/piano_analysis.py`; raw numbers in `out/piano_analysis_data.json`.
13 notes analysed, weighted low/mid: B0 C1 F1 C2 G2 C3 G3 C4 G4 C5 C6 C7 C8.

Method notes: partial frequencies from a 4x-zero-padded Hann FFT of the early decay
(2.0 s window in the bass, 0.4 s mid, 0.25 s top), peaks tracked sequentially with a running
refit of (f_n/n)^2 = f0^2(1 + B n^2) so the search window follows the stretch. Per-partial
decay from heterodyne envelopes (fit starts 100 ms post-onset to skip the hammer thump,
requires 20 dB headroom over the noise floor). Double decay from a grid-searched two-segment
piecewise-linear fit of the dB envelope.

## 1. Inharmonicity B(f0)

f_n = n·f0·sqrt(1 + B·n²), fitted per note. `pair(f1,f2)` = top-octave fallback
(only 2-4 real partials exist there; the full-track fit picks up soundboard junk).

| note | f0 fit (Hz) | vs ET (cents) | B | partials fitted | method |
|---|---|---|---|---|---|
| B0 | 30.76 | -5.8 | 2.41e-4 | 68 | track |
| C1 | 32.48 | -11.8 | 2.37e-4 | 75 | track |
| F1 | 43.52 | -5.3 | 1.11e-4 | 72 | track |
| C2 | 65.42 | +0.4 | 1.23e-4 | 66 | track |
| G2 | 97.86 | -2.5 | 1.29e-4 | 59 | track |
| C3 | 130.98 | +2.3 | 1.14e-4 | 51 | track |
| G3 | 196.75 | +6.6 | 1.89e-4 | 36 | track |
| C4 | 262.57 | +6.2 | 2.89e-4 | 33 | track |
| G4 | 393.81 | +8.0 | 6.10e-4 | 27 | track |
| C5 | 523.38 | +0.4 | 9.59e-4 | 17 | track |
| C6 | 1040.96 | -9.2 | 4.37e-3 | 12 | track |
| C7 | 2100.79 | +6.4 | 7.33e-3 | 2 | pair(f1,f2) |
| C8 | 3988.69 | -83.6 | 2.48e-2 | 2 | pair(f1,f2) |

The expected V: minimum ~1.1e-4 in the F1-C3 wound-string region, rising both ways.

- **Bass side** (f0 ≤ 70 Hz): B = 6.56e-3 · f0^-0.99 (R² = 0.68, 4 notes).
- **Treble side** (f0 ≥ 120 Hz): **B = 4.32e-8 · f0^1.60 (R² = 0.99)** — very clean.
- Concrete stretch: C4's 12th partial sits +35 cents sharp of harmonic and its 33rd
  (highest measured) +237 cents; C6's 12th is +422 cents (mult 15.32 vs 12); C8's octave
  partial is +61 cents wide (8363 Hz vs 2·4038).
- Caveats: C8's f0 measures 84 cents flat of ET (consistent across windows — either the
  Iowa top string is flat or partial 1 is contaminated; flagged, not resolved). C7/C8 B
  comes from two partials only. Tuning offsets elsewhere follow a plausible mild Railsback.

![piano_B_vs_f0.png](piano_B_vs_f0.png)

### Engine expressibility (stretched mult tables)

Per-note stretched table m_n = n·sqrt(1+B n²), partials to 18 kHz, vs the
ExplicitPartials descriptor caps (mult ≤ 200, maxPartials ≤ 200):

| note | partials to 18 kHz | max mult | mult cap binds? |
|---|---|---|---|
| B0 | 188 | 580 | YES — cap reached at partial 104 (≈6.2 kHz) |
| C1 | 184 | 552 | YES — at partial 105 (≈6.5 kHz) |
| F1 | 187 | 413 | YES — at partial 122 (≈8.7 kHz) |
| C2 | 145 | 274 | YES — at partial 120 (≈13.1 kHz) |
| G2 | 113 | 184 | no |
| C3+ | ≤ 96 | ≤ 137 | no |

maxPartials = 200 never binds (max needed: 188 at B0). The mult ≤ 200 cap is
**descriptor-level only**: `patch_loader.cpp:163` passes arrays to `set_array` unclamped, so
values over 200 would load and render — but they are out of the descriptor contract (UI
table clamps to 200). Practically: mid/treble piano is fully expressible today; sub-G2
keys lose content above ~6-13 kHz at the contract boundary, which the hammer-tilted bass
spectrum barely reaches anyway (see attack tilt below) — acceptable for v1.

**The real blocker is per-NOTE stretch, not per-partial stretch** — see feature mapping.

## 2. Decay structure

### Double decay (prompt sound → aftersound), broadband envelope

Two-segment fit vs single slope; sse2/sse1 well below 1 = double decay is real on every note.

| note | break (s) | prompt (dB/s) | aftersound (dB/s) | sse2/sse1 |
|---|---|---|---|---|
| B0 | 3.7 | -3.5 | -0.7 | 0.10 |
| C1 | 6.0 | -2.8 | -1.0 | 0.23 |
| F1 | 2.6 | -4.5 | -1.2 | 0.29 |
| C2 | 4.6 | -3.6 | -1.0 | 0.31 |
| G2 | 2.7 | -6.7 | -0.7 | 0.15 |
| C3 | 2.2 | -10.1 | -0.8 | 0.07 |
| G3 | 4.2 | -5.6 | -0.9 | 0.16 |
| C4 | 1.0 | -27.3 | -1.2 | 0.27 |
| G4 | 1.0 | -29.2 | -1.6 | 0.20 |
| C5 | 3.0 | -7.7 | -0.9 | 0.26 |
| C6 | 2.2 | -12.9 | -0.1 | 0.23 |
| C7 | 1.0 | -19.7 | -0.1 | 0.25 |
| C8 | 0.9 | -9.1 | +0.1 | 0.60 |

Pattern: prompt rate grows with register (-3 dB/s at C1 → -20..-29 dB/s at C4-C7);
aftersound is register-flat at ≈ -1 dB/s (the coupled-string tail). Break time drifts
from several seconds in the bass to ≈1 s in the treble. C5's slow prompt (-7.7) against
its neighbours C4/G4 (-27/-29) is the one outlier — possibly a break-region fit ambiguity;
not chased. C8 aftersound +0.1 dB/s = fit hit the noise floor.

### Per-partial decay rates

Single-slope fit over the top 25 dB of each heterodyne envelope, first 16 partials
(full lists in the JSON). Representative rows (partial n: rate dB/s):

- **C1**: n2: 1.4, n4: 4.3, n8: 9.8, n12: 11.4, n16: 1.8 — low partials extremely
  long-lived (T60 of 15-45 s), rate roughly triples by n≈10, then non-monotonic.
- **C3**: n1: 14.2, n4: 13.9, n8: 27.9, n11: 44.0, n15: 51.8 — cleanest register:
  rate rises ≈3x from h1 (131 Hz) to h15 (2 kHz).
- **G4**: n1: 32.1, n2: 51.7, n4: 8.3, n7: 8.4 — NON-monotonic: h1/h2 prompt-decay
  faster than mid partials. Real two-string veering (see below), not measurement noise.
- **C7**: n1: 19.1, n2: 29.8. **C8**: n1: 38.3, n2: 145.8.

Pooled power law across all notes: rate ≈ 0.23 · f^0.59 dB/s (f in Hz) — but R² = 0.28,
and that scatter is structural, not noise: the per-partial envelopes are visibly
non-exponential (unison-string beating and prompt/aftersound veering make many partials
bimodal), so a single "rate vs frequency" law under-describes piano decay. Two robust
facts survive: (a) within a note, high partials decay faster than low ones — typically
3-10x from h1 to the top of the measured range; (b) across notes, everything decays
faster up the keyboard (h1 rate: ~2 dB/s at C2 → ~38 dB/s at C8).

![piano_decay_rates.png](piano_decay_rates.png)

## 3. Attack

| note | 10-90 rise (ms) | knock: line-ratio attack | line-ratio sustain | attack band tilt vs sustain (dB): 30-160 / 160-700 / 700-2500 / 2500-8000 |
|---|---|---|---|---|
| B0 | 7.4 | 2.58 | 0.38 | -11.5 / -26.4 / -18.0 / **+16.2** |
| C1 | 16.4 | 2.33 | 0.37 | -8.5 / -27.4 / -25.2 / **+20.0** |
| F1 | 54.8 | 2.56 | 0.38 | -15.6 / -30.2 / -22.3 / **+16.2** |
| C2 | 36.3 | 0.46 | 0.45 | +5.2 / +0.1 / -1.5 / **+6.8** |
| G2 | 22.9 | 3.48 | 0.11 | -9.2 / -24.3 / -30.4 / **+8.8** |
| C3 | 20.7 | 6.73 | 0.02 | -2.5 / -28.3 / -26.4 / +5.7 |
| G3 | 13.0 | 0.020 | 0.001 | +16.8 / -0.8 / +4.0 / +6.2 |
| C4 | 13.2 | 0.007 | 0.002 | +1.2 / +0.9 / -6.9 / -10.6 |
| G4 | 22.7 | 0.006 | 0.000 | -2.8 / +3.4 / -7.6 / -5.8 |
| C5 | 11.7 | 0.002 | 0.000 | +0.9 / +0.2 / -4.3 / -1.4 |
| C6 | 8.8 | 0.020 | 0.001 | -0.3 / +18.9 / -0.1 / +4.2 |
| C7 | 4.0 | 122 | 0.003 | +9.6 / +2.1 / -30.2 / +5.3 |
| C8 | 11.6 | 259 | 10.4 | +2.0 / +0.2 / -7.0 / -4.6 |

(line-ratio = between-line / at-line energy 50 Hz-8 kHz, 120 ms window, lines at the
measured stretched positions; band tilt = band energy share of the first 50 ms minus the
same share in the 0.5-1.5 s window, each normalised to its own total.)

Numbers:

- **Attack duration**: 10-90 rise is 4-55 ms, median 16 ms — 10-30x faster than the
  clarinet's 60-480 ms t50, and with NO clear register law (the clarinet's f0^-0.84 rise
  law has no piano analogue; F1's 55 ms is string/soundboard settling, not a slow rise).
  The full spectrum is present immediately: this is an onset-dispersion≈0 instrument.
- **The knock is real and is register-shaped.** In the bass (B0-C3) the first 120 ms
  carries 2.3-6.7x MORE between-line than at-line energy, vs 0.02-0.45 in the settled
  decay — a genuine broadband thump distinct from the partials. Its spectrum: the
  2.5-8 kHz share of the first 50 ms is +6..+20 dB above that band's share in the decay
  (hammer/action click) on every bass note. In the mid keys (G3-C5) at mf the knock is
  nearly absent (ratios 0.002-0.02 — energy stays on the stretched lines throughout).
  At the top (C7/C8) the ratio explodes (122/259) — there the note IS mostly knock:
  2-3 short-lived partials over a broadband strike transient.
- Heterodyne cross-check: the thump leaks into every partial band in the first ~100 ms
  (before the fit-window guard was added it read bass h1 "decay" as 100+ dB/s).

![piano_attack_rise.png](piano_attack_rise.png)

## Feature mapping — engine fit (verified against `engine/include/mforce/source/additive/partials.h` and `engine/src/patch_loader.cpp`)

### What the engine expresses today

1. **Stretched partials for a single note**: ExplicitPartials takes arbitrary
   non-integer `mult1/mult2` arrays (`ArrayDescriptor {"mult1","partials",1,0,200}`)
   — the per-note tables above drop straight in. Caps: fine from G2 up; sub-G2 loses
   >6-13 kHz content at the descriptor contract (loader itself doesn't clamp).
2. **Global decay shape**: the instrument amplitude envelope (existing Envelope nodes)
   can carry the two-segment prompt/aftersound law — break ≈ 1-6 s, prompt -3..-29 dB/s,
   aftersound ≈ -1 dB/s, all per-note mappable via the existing paramMap
   frequency→curve mechanism (`patch_loader.cpp` "Function" port: log-Hz breakpoint
   interpolation, delivers to params AND scalar configs at note-on).
3. **Fast attack**: `onsetSpread≈0`, `onsetFade` 4-25 ms — trivially in range.
4. **Spectral tilt evolution (coarse)**: `ampl1→ampl2` crossfaded by `amplEnv` gives a
   per-partial START and END level with one shared time trajectory — can fake "highs die
   first" as a slow global tilt, which is the right first-order percept, but the
   trajectory is linear-in-amplitude and common to all partials.

### What is NOT expressible (precisely)

1. **Per-partial decay rates.** In the CMA-ES additive path (AdditiveSource +
   Partials/ExplicitPartials) every partial's amplitude is
   `(ampl1[i] + (ampl2[i]-ampl1[i])·amplEnv) · rolloff · motion...` — ONE scalar
   envelope shared by all partials. Measured piano needs h1 at 2 dB/s while h12 runs
   11 dB/s (C1) — i.e., per-partial exponential rates, not a shared crossfade. Not
   expressible. (AdditiveSource2 — the legacy gen-2 port, registered at
   `source_registrations.cpp:408` — CAN attach separate envelopes to partial
   ranges/filters via `assign_ampl_envelope`, but it lacks motion/shimmer/bandwidth/
   onset/formant entirely and caps partials at 12 kHz/f0; it is not the vehicle.)
2. **Per-note inharmonicity.** One patch renders all score notes, and the mult table is
   static per patch — but B spans 1.1e-4 → 2.5e-2 across the keyboard (225x). A patch
   voiced for C3's stretch is audibly wrong at C6 (its 12th partial would sit ~4
   semitones flat of the real piano's).
3. **Decaying noise transient (knock).** The clarinet `noiseBed*` params make a
   sustained delayed-fade-IN bed; the knock is the opposite (immediate, decaying,
   30-100 ms). No engine feature missing though — a graph-level mix of a noise source
   (RedNoise/LayeredRedNoise) through a fast AR envelope into the mixer expresses it
   today; it just needs template plumbing, and unlike the clarinet breath (which had to
   couple to the tonal path) the knock genuinely IS a separate percussive event.

### Minimal feature proposals (in priority order)

1. **`inharmonicity` scalar config on Partials** (B): at prepare/note-on, transform the
   working mults `m → m·sqrt(1 + B·m²)`. Prepare-time only — zero hot-loop cost, no
   allocation in render. Because paramMap curves already deliver to scalar configs at
   note-on, per-note B then comes free:
   `{"target": "parts.inharmonicity", "curve": [[33,2.4e-4],[131,1.1e-4],[523,9.6e-4],[2100,7.3e-3],[4186,2.5e-2]]}`.
   This one feature unblocks both gap 2 and (for the searchable dims) removes any need
   to spend CMA-ES evals on stretch — B is directly measurable, lock it like Matt's bed.
2. **Per-partial decay law, 2 scalar configs on Partials**: `decayRate` (dB/s at the
   fundamental) + `decayExp`, giving partial i the rate `decayRate · pmult^decayExp`.
   Implementation: at prepare, fill a cached per-partial per-sample gain factor
   `g[i] = 10^(-rate_i/(20·sr))` (alongside the existing pmultCache_ pattern); in the
   per-partial loop multiply a running per-partial amplitude state by g[i] — one extra
   multiply per partial per sample, arrays sized at prepare, real-time safe. Both
   configs per-note mappable via paramMap curves (measured: decayRate ~2 dB/s @C2 →
   ~38 dB/s @C8; within-note exponent ≈ 0.6, note-to-note scatter large). A per-partial
   decay-rate ARRAY was considered and rejected for v1: it adds 16-40 search dims for
   structure the 2-param law captures to first order, and the measured per-partial
   scatter (string veering) is better faked by the existing motion/shimmer layer.
3. **No new feature for the knock**: template-level noise + AR envelope (see above).
   Optional later: `noiseKnock*` params on AdditiveSource if coupling to note gain
   proves necessary.
4. **Double decay**: no new feature — existing envelope nodes + paramMap curves.

### Encoder dims sketch (piano encoder, once 1+2 exist)

- LOCKED (measured, not searched): B(f0) curve; decayRate/decayExp register curves
  (searchable multiplier ±, see below); stretched-mult expressibility handled by 1.
- Searched (~14-18 dims): 8 harmonic-envelope knots (per clarinet pattern; piano
  spectral envelope is register-dependent so knots ride on per-note reference envelopes);
  decayRate scale + decayExp trim (2); global prompt/aftersound break + slopes trim (2-3,
  around measured values); knock level / decay / tilt (3); shimmer depth/Hz/coherence for
  unison beating (3) — beating rates were NOT measured in this pass (envelope fits
  treat it as scatter); measure before fixing those ranges, or leave them searchable.

## Config

`research/ml_ears/configs/piano_mf.json` — clarinet schema; 9 score notes C1-C6
weighted low/mid, eval midis [36, 60, 72]. Two deviations, documented in its `notes`:
(a) 44.1k samples vs 48k engine — no handling needed, all refmetrics extractors are
Hz/seconds-based with bands ≤8 kHz; (b) `reference_build.onset_relative: true` — sample
lead-in varies 0.04-0.54 s so iowa_reference.py must onset-align before slicing windows
(NOT yet implemented there; required before building the piano reference).
**A0/Bb0**: absent at mf on the Iowa server (404, see MANIFEST.md). Left as a gap —
pitch-shifting B0 down would fabricate data with the wrong B (it rises toward A0) and
wrong hammer spectrum. Lowest score note: C1.

## Open items / limitations

- Unison-string beating rates unmeasured (needed to lock shimmer dims).
- C8 tuning anomaly (-84c) unexplained; C7/C8 B from 2 partials each.
- C5 prompt-decay outlier vs C4/G4 not chased.
- Longitudinal-mode / duplex partials visible as off-model peaks in the treble were
  discarded by the 5% model filter, not characterised.
- pp/ff dynamic layers not downloaded; hammer-hardness spectral scaling out of scope.
