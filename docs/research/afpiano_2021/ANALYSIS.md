# AF piano — 2021 working-session video analysis

Source: "Synthesisizing an acoustic piano sound - progress", @9b0 (Balázs),
Apr 24 2021, 10:44. https://www.youtube.com/watch?v=uuebeNV-DS8
Method: yt-dlp download (1080p) + auto-caption transcript + ffmpeg frame
extraction. Frames + transcript preserved in this folder. Analyzed 2026-08-10.

**This is the PREDECESSOR of the patch behind the AFNoding-031 description that
our v5 implements.** Several mechanisms here contradict or extend that later
brief description — most importantly the excitation.

## Architecture (as narrated + read from frames)

### 1. Hammer/excitation = enveloped WHITE NOISE through a filter bank (f_0010, f_0014)

Not a bare decaying envelope. The chain:

- Attack env node: Time **4.7 ms** (+Mod). Two "Hit length" envelope nodes:
  one **0.0/0.0 ms**, one **20.2 ms attack / 462.2 ms release** (+Amod/Rmod
  showing decaying-ramp shapes). Gain Lin Vol 0.25.
- The envelope CONTROLS a **White Noise** source ("this envelope controls just
  the white noise", 2:03) — i.e. a shaped noise burst, not a click.
- The burst runs through parallel/serial filters ("shape it with several
  filters... then another filter"): **Filter 1P Modulated**, THREE **Filter SVF
  Modulated** (each with Cutoff, Q, KBD keyboard tracking, Veloc/Cmod/Rmod
  inputs, and a 12-24 dB/oct steepness switch + "Steepness mod"), and a
  **Filter 4P Modulated with Emphasis** fed by a "Brightness" macro knob.
- Four paths mix in **Mixer 4 Lin: Vol 1.00 / 0.76 / 0.33 / 0.47** (f_0014,
  f_0056) before entering the strings.
- Multiple **Custom Function** velocity/pitch maps (Ymax 100, 150, 148.5, 250)
  drive cutoffs/lengths — velocity → brightness is baked in everywhere.
- Narration: "these noises are so identical" (3:18) — likely a frozen/seeded
  noise so hits are repeatable.

### 2. String = KS delay loop with a 4-element filter chain (f_0031, f_0033)

Loop order (String designer, f_0033): input Add → **Delay** → **Dmp** (damping
lowpass; loses HF each pass, "waves slowing down") → **Filter 1P** → 
**2nd_Allpass** (2nd-order allpass built from an SVF: AP = 2·(LP+HP) − in,
narrated 6:06-6:28; internals f_0038 show Filter 2P + the Pnc pitch input and
cutoff math — it is pitch-tracked) → **Designer** (1st-order ZDF allpass =
LP + (−HP) of a one-pole, f_0040; explicitly pitch-responsive) → **Mul**
(feedback) → back to input.

- **Feedback = the damper**: gate held → "almost one"; gate released →
  **0.82** (knob visible, f_0031). Note-off is a feedback drop INSIDE the
  loop, so the release decay keeps passing through the damping filter —
  progressive HF loss during the damp, not a volume fade.
- **Delay-time calc**: pitch → ms via Custom Function (X 0-150 → Y 47.4-150
  window visible) + a **Sample Rate node + Sub** path (f_0033) — the loop
  subtracts the filters' delay contribution from the delay-line length
  (phase-delay compensation; matches the comb tuning we already do).
- Pnc node (pitch conversion, Left/Cent/Anlg/Step modes) feeds the allpass
  cutoff math.

### 3. Three strings per note, always (f_0014, f_0056)

- Three String node instances (String 1 / String 2 / String 4) fed the same
  excitation, summed in Add 6 → post **Filter 1P Modulated** → Gain → out.
- **Detune is pitch-dependent**: Custom Function 5 maps pitch 36→100 to
  0→**0.05** (semitones), scaled by a global Detune macro knob (shown ±2 oct
  range in the demo). Low strings keep 3 unisons at equal pitch when detune
  is 0 ("I'm not removing the low strings, I just make them the same pitch",
  9:22).

### 4. Reverb is structural (8:00-8:36)

"You never hear a piano without a room; even the piano has a body which
creates reflections... without it, it gets somehow weak."

### 5. Sustain patch existed, removed (10:19)

Sustain/pedal layer removed due to polyphony blowups — parked by him too.

## Mapping to our implementation (patches ks_piano_v5 / engine primitives)

| His mechanism | Ours (v5) | Verdict |
|---|---|---|
| Enveloped white-noise burst → filter bank | Noiseless decaying env → 4 resonant SVF bandpasses (HammerBank) | **GAP — biggest one.** The later description's "no noise" postdates this; the 2021 patch Matt liked has noise. Explains our weak treble partials (known gap). |
| Velocity→cutoff/length custom curves | velocity scales gain only | **GAP** — expressive brightness-vs-velocity missing. |
| Damper = feedback drop to 0.82 in-loop | instrument-level exponential fade (release 0.35s) | **Partial.** Ours fades the mixed output; his keeps damping filter active during release (darker, more physical decay). |
| Loop: Delay→damp LP→1P→2nd AP→1st AP (both pitch-tracked) | Delay→damp→2 biquad APs (dispersion)+nested-allpass inharmonic loop | **Close.** Structure equivalent; his APs are explicitly pitch-tracked (our dispersion curve does this via paramMap). |
| Phase-delay-compensated tuning | landed (run 24: phase not group delay) | **Match.** |
| 3 unison strings, pitch-dependent detune curve | numCombs 3, fixed detune 1.8c | **Refinement**: detune should be a pitch curve (≈0 at bass → max at treble), matching real piano unison spread. |
| Room/body reverb | Reverb roomSize 0.18 wet 0.18 | **Match** in spirit. |
| Post-string 1P tone filter + gain | none | Minor gap. |

## Excitation targets measured from his demo audio (added 2026-08-10 pm)

Spectra averaged over detected hit onsets (120 ms windows) in the video audio.
Rough (YouTube compression, room), but the relative band structure is the
design target. Matt flagged this stage as key: "every tweak makes a huge
difference."

| Stage | Hits | Centroid | 0-200 | 200-600 | 600-1.5k | 1.5-4k | 4-10k | 10k+ |
|---|---|---|---|---|---|---|---|---|
| envelope only (1:43) | 2 | 416 Hz | .32 | .57 | .09 | .02 | .00 | .00 |
| after 3 SVFs (2:48) | 31 | 1666 Hz | .56 | .16 | .04 | .09 | .11 | .04 |
| + another filter (3:05) | 25 | 241 Hz | .47 | .52 | .01 | .01 | .00 | .00 |
| final string input (3:40) | 10 | 590 Hz | .63 | .29 | .01 | .00 | .06 | .00 |

Reading: the excitation is BIMODAL — dominant low thump (<600 Hz) + a small,
separated HF click (4-10 kHz), with a deep MID-SCOOP (600-1500 Hz nearly
empty at every stage). The "another filter" step's audible job is killing HF
and concentrating 200-600 (the clunk); the final input reintroduces a
controlled ~6% HF click over the clunk.

Filter-stack values read from frames (ff_015 / svf_stack_zoom): SVF cutoffs
72.22 and 64.13 in KBD-tracked MIDI-note units (~C5 / ~E4), Q 0, steepness
sliders ~40% of 12-24 dB/oct; third SVF's cutoff never fully visible in this
section. Mixer blend 1.00 / 0.76 / 0.33 / 0.47. Exact knobs matter less than
the measured band targets above — iterate our shaping against those.

## Recommended next moves (in impact order; revised after audio measurement)

1. **Noise excitation for HammerBank** — v6 rung 1, SHIPPED 2026-08-10
   (patches/pending/ks_piano_v6, awaiting Matt).
2. **Excitation filter shaping to the measured band targets** (rung 2, Matt:
   "key rung"): shape the burst+bank output toward the final-string-input
   profile — dominant <600 Hz, 600-1500 scooped, ~5% HF click at 4-10 kHz.
   Iterate against the measured table objectively (render, FFT, compare)
   BEFORE audition; no ear-guessing.
3. **Velocity→brightness maps**: paramMap already supports curves keyed on
   frequency; needs velocity-keyed curves (new small engine feature) or
   per-velocity-layer patch variants for audition first.
4. **In-loop damper**: KSPianoString param `releaseFb` (e.g. 0.82) applied
   after the scored duration — needs note-off awareness inside the source or
   a prepare-time schedule (durSamples known at prepare).
5. **Pitch-dependent detune curve** via paramMap (trivial — curve slot exists).
6. Post-string tone filter (1P) — one node in the patch graph.

Open question for the KVR/video trail: whether the "no noise" statement in the
later description means he REPLACED the noise burst (worth re-reading AFNoding
031's narration once more against this), or was describing an ideal the
shipped preset doesn't actually follow.


## Stage-WAV differential analysis (2026-08-15, Matt's clean downloads)

Four isolated stage clips (AFP_excite_step_0..3.wav) beat the 08-10
in-video hit windows: consecutive-stage spectral ratios ARE the inserted
filters' transfer functions. Corrected transcript cross-checked.

**Stage identities (measured, note step_0/1 order vs file names):**
- step_0 = the HAMMER THUMP (envelope-as-signal, 1:43 "this hit"): 96% of
  energy below 200 Hz, decay to 10% in ~40 ms. This is the knock.
- step_1 = enveloped WHITE NOISE (2:05): near-white with a slight upward
  tilt; burst decays to 50% in ~25-33 ms, 10% at ~120 ms, **1% at
  ~280-350 ms** — consistent with the frames' 20.2 ms attack / 462 ms
  release envelope.
- step_2 = after the THREE SVFs ("several filters"): net transfer is
  MILD — +1..4 dB through 200-400 Hz, gentle taper reaching only
  -5..-14 dB above 6 kHz, no resonant peaks (Q≈0 confirmed). The trio
  sculpts; it does not carve.
- step_3 = after the "one more filter": a STEEP LOWPASS — measured
  -18..-20 dB/oct through the knee (≈1.5-2 kHz at the demo keys),
  everything above 4 kHz gone (-28 dB and falling to -70), broad low-side
  lift. That is the frames' **Filter 4P Modulated with Emphasis**
  (Brightness macro): a 4-pole resonant lowpass doing the heavy shaping.

**Anti-result — "these noises are so identical" (3:18) is NOT frozen
noise:** pairwise correlation of step_2 hit waveforms is |r| ≈ 0.09
(median, 50 ms windows, lag-searched). Fresh noise every hit; he meant
perceptual consistency. Do not implement seeded-per-note noise for
fidelity's sake.

**Gaps vs Piano_bright, in measured-impact order:**
1. **Burst length**: his noise excitation rings to ~350 ms (1%); ours
   decays in 20-40 ms. The strings get fed ~10x longer — a plausible
   mechanism for our "harpsichordy attack" family of verdicts.
2. **Final-filter tuning**: our exc_lp is already the right species
   (2-section Butterworth = 4-pole) but knees at 4700 Hz from C6 up vs
   his ≈1.5-2 kHz, and has NO emphasis/resonance. The parallel-mix
   topology (steep-LP body + separate HF click path re-adding controlled
   ~6% HF) matches ours structurally — the numbers, not the architecture,
   are wrong.
3. **The SVF sculpt stage** (3 keyboard-tracked, Q≈0, 12-24 dB switch):
   mild net effect; lowest priority of the excitation moves.
4. **Post-string chain we lack** (transcript 5:37-7:10 + frames): in-loop
   after Delay→damping-1P we have dispersion APs, but not his separate
   pitch-tracked 1st-order ZDF ALLPASS ("Designer" node); post-sum there
   is a Filter 1P Modulated tone stage before gain/reverb. Both cheap.

Proposed next moves (analysis-first contract — for Matt's read, nothing
built): (A) lengthen burst release toward ~450 ms, A/B ladder;
(B) retune exc_lp knee down with keyboard tracking + add an emphasis
bump near the knee; (C) post-string 1P + in-loop 1P allpass as one
small patch rung; (D) skip frozen noise (anti-result above).
