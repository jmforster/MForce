# Iowa Bb clarinet ff — attack analysis

Samples: `research/inst_samples/clarinet_Bb`, window onset..+0.5 s, harmonics 1-12 heterodyned (derive_motion method), harmonic part reconstructed from all harmonics <=16 kHz and subtracted for the noise residual. Residual under-counts noise within the heterodyne low-pass (min(0.45*f0, 80) Hz) of each line; the comb fraction column reports how much residual energy still sits within +-6% of harmonic lines (high comb = line-broadening leakage, low comb = genuinely between-line noise).

## Per-note summary

| note | f0 Hz | t50 ms | t90 ms | breath dom ms | NTR attack dB | early NTR dB | breath peak dB | NTR sustain dB | noise lead ms | comb frac | slope dB/oct | centroid Hz | rise ms/harm | E/O attack dB | E/O sustain dB |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| E3 | 164.0 | 350 | 491 | 314 | -37.1 | -28.1 | -38.5 | -34.3 | 68 | 0.12 | -5.9 | 3883 | +3.3 | -20.7 | -22.9 |
| G3 | 196.0 | 479 | 633 | 461 | -40.2 | -28.6 | -42.9 | -40.0 | 289 | 0.08 | -4.3 | 3261 | +8.1 | -19.5 | -19.8 |
| Bb3 | 232.0 | 344 | 577 | 338 | -33.2 | -23.7 | -34.0 | -36.6 | 184 | 0.08 | -1.9 | 4198 | +6.8 | -16.1 | -20.9 |
| D4 | 292.0 | 152 | 348 | 132 | -39.6 | -29.4 | -37.2 | -35.6 | 78 | 0.03 | -4.4 | 2512 | +11.0 | -19.0 | -16.8 |
| F4 | 349.0 | 213 | 319 | 192 | -26.1 | -12.6 | -23.4 | -34.1 | 120 | 0.00 | -3.8 | 398 | +16.7 | -6.6 | -13.4 |
| A4 | 440.0 | 268 | 299 | 290 | -23.4 | -16.8 | -28.5 | -35.6 | 182 | 0.00 | -5.2 | 2046 | +24.4 | +2.6 | +0.7 |
| D5 | 586.0 | 114 | 172 | 108 | -39.1 | -28.3 | -38.8 | -37.7 | 66 | 0.01 | -4.6 | 1674 | +4.8 | +0.1 | -7.6 |
| G5 | 783.0 | 78 | 128 | 88 | -34.7 | -25.2 | -34.1 | -37.8 | 46 | 0.00 | -4.0 | 1254 | +1.6 | +6.7 | +5.4 |
| C6 | 1048.0 | 61 | 229 | 62 | -40.9 | -28.4 | -38.9 | -38.5 | 32 | 0.24 | -1.9 | 3249 | +8.6 | +3.9 | +5.4 |
| F6 | 1380.0 | 106 | 727 | 114 | -35.0 | -18.0 | -28.1 | -36.2 | 92 | 0.15 | +1.0 | 4600 | +1.3 | +19.8 | +23.6 |

t50/t90: total harmonic envelope rise from acoustic onset. NTR: integrated residual/harmonic energy ratio (attack window = onset..t90; early = onset..t50; sustain = onset+0.7..1.7 s). breath peak = peak attack-noise frame RMS relative to sustain tone RMS (absolute breath loudness vs the note). noise lead: tone onset minus noise onset, positive = breath precedes tone.

## Per-harmonic rise (t90 ms, harmonics 1-12)

| note | h1 | h2 | h3 | h4 | h5 | h6 | h7 | h8 | h9 | h10 | h11 | h12 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| E3 | 374 | 337 | 723 | 375 | 492 | 367 | 513 | 657 | 659 | 378 | 747 | 366 |
| G3 | 533 | 493 | 692 | 509 | 782 | 542 | 803 | 585 | 914 | 572 | 936 | 571 |
| Bb3 | 452 | 342 | 597 | 352 | 355 | 361 | 744 | 375 | 661 | 379 | 770 | 349 |
| D4 | 185 | 141 | 393 | 191 | 366 | 536 | 341 | 660 | 327 | 358 | 368 | 898 |
| F4 | 270 | 212 | 386 | 237 | 371 | 242 | 1076 | 252 | 386 | 436 | 418 | 444 |
| A4 | 292 | 287 | 313 | 308 | 631 | 428 | 290 | 705 | 713 | 608 | 651 | 500 |
| D5 | 193 | 117 | 186 | 424 | 204 | 179 | 408 | 1088 | 130 | 127 | 202 | 252 |
| G5 | 137 | 80 | 86 | 85 | 329 | 239 | 229 | 112 | 108 | 89 | 231 | 86 |
| C6 | 222 | 65 | 67 | 379 | 364 | 490 | 381 | 68 | 67 | 571 | 254 | 413 |
| F6 | 592 | 829 | 120 | 307 | 978 | 208 | 126 | 126 | 897 | 157 | 128 | - |

## Residual octave bands (dB rel per-note max)

| note | 250 | 500 | 1000 | 2000 | 4000 | 8000 | floor margin dB |
|---|---|---|---|---|---|---|---|
| E3 | -20.0 | -10.7 | -3.8 | -1.6 | -4.7 | 0.0 | 5.8 |
| G3 | -12.1 | -11.0 | -6.0 | -1.6 | 0.0 | -1.4 | 5.4 |
| Bb3 | -18.8 | -13.7 | -6.3 | -0.6 | -1.3 | 0.0 | 10.2 |
| D4 | -16.5 | -16.1 | -0.6 | 0.0 | -2.2 | -6.3 | 13.5 |
| F4 | 0.0 | -25.0 | -21.3 | -18.9 | -16.2 | -22.8 | 25.8 |
| A4 | -8.3 | -2.7 | -1.3 | -1.5 | 0.0 | -13.2 | 18.4 |
| D5 | -1.8 | -18.1 | 0.0 | -5.7 | -1.9 | -13.1 | 20.1 |
| G5 | -1.8 | -25.3 | 0.0 | -7.2 | -9.1 | -15.8 | 26.0 |
| C6 | -12.8 | -11.6 | -4.3 | 0.0 | -1.0 | -2.8 | 17.4 |
| F6 | -21.7 | -19.1 | -8.8 | -0.4 | -3.3 | 0.0 | 26.4 |

floor margin = attack residual energy over pre-onset recording noise in the same 200-10000 Hz band (validity check).

## Register regressions

- Rise time law (t50): **t50 = 29.322 * f0^-0.84** seconds (R² = 0.77). Slope -1.0 would be exact inverse proportionality.
- t90 law: t90 = 2.147 * f0^-0.30 s (R² = 0.15) — t90 is contaminated by slow level drift on some notes (F6, C6); t50 is the robust attack-speed measure.
- Breath excess duration (NTR within 6 dB of sustain): **tB = 17.682 * f0^-0.76** s (R² = 0.69).
- Attack NTR vs register: -0.17 dB/octave (R² = 0.00).
- Breath peak vs sustain tone: **+1.66 dB/octave** (R² = 0.08).
- Noise-precedes-tone lead: lag ~ f0^-0.52 (R² = 0.31).
- Attack-minus-sustain NTR (breath *excess* during attack): +0.24 dB/octave.

## Findings -> additive features

**Verdict on the register claim.** Attack *speed* is confirmed: t50 scales as f0^-0.84 (close to inverse proportionality). Breath *amount* is not a level effect — peak breath level is register-flat (+1.7 dB/oct, R²=0.08, ≈-36 dB below the sustain tone everywhere) — it is a *duration* effect: the breath-excess window scales as f0^-0.76 and the noise-before-tone lead roughly doubles per two octaves down (median 131 ms below 300 Hz vs 56 ms above 500 Hz). Low notes sound breathier because the breath is exposed 3-5x longer, not because it is louder. Exception: the throat/break notes (sounding F4/A4 = written G4/B4) are genuinely the noisiest in level (-23 / -28 dB vs the ~-38 dB norm) — a fingering-region effect, not a register trend.

**What the noise is.** The residual is genuinely between-line broadband (comb fraction ≤ 0.24, mostly ≤ 0.1): it is NOT line-broadening around the harmonics. Spectral shape: broad hiss with centroid ≈ 2.9 kHz (typically 1.5-4 kHz), roughly flat through 1-4 kHz then falling (-4.1 dB/oct median slope 300 Hz-8 kHz) — band-passed reed/mouthpiece turbulence, not pink noise and not harmonic-locked. Critically, its LEVEL is roughly constant through the note: peak attack noise ≈ sustain noise (both ≈-36 dB rel sustain tone). The breath is a steady hiss bed that starts 30-290 ms before the tone; the 'breathy attack' percept is the bed being exposed while the tone is still rising, plus a genuine +8..+12 dB attack excess on the throat/break notes only.

**Rise order.** Spectrum assembles bottom-up: t50 increases with harmonic number on every note (median +7.4 ms/harmonic, up to +17..+24 ms/harmonic on the throat notes). Within that, the STRONG odd harmonics (chalumeau) rise slower than the weak evens — the attack is the fundamental family swelling, with h1 first. Even content is relatively stronger during the attack on several chalumeau notes (Bb3 +4.8 dB, F4 +6.8 dB attack-vs-sustain E/O shift) before settling to odd dominance.

### Feature mapping (concrete numbers)

1. **Amp envelope attack** via paramMap frequency->value breakpoints (t50 law): attack ≈ 0.43 s @150 Hz, 0.19 s @400 Hz, 0.09 s @1000 Hz (t50 = 29.3*f0^-0.84).
2. **Onset dispersion**: onsetTilt = +1.0 (low-first matches the measured bottom-up order; random/-1 do not). onsetSpread by register curve: 0.10 s @150 Hz, 0.20 s @400 Hz (throat region is the most dispersed), 0.06 s @1000 Hz (≈ 12 harmonics x measured ms/harmonic). onsetFade ≈ 0.10 s (per-harmonic 10-90 rise once entered).
3. **Bandwidth (per-partial AM) is NOT the breath**: it is multiplicative with partial amplitude, so it cannot precede the tone and at bandwidthHz≈30 it stays near the lines, while the measured noise is between-line and leads by 30-290 ms. Keep a small floor for sustain line texture only: bandwidth1 ≈ 0.05-0.1, bandwidthHz ≈ 30-60 (sustain NTR ≈ -36 dB). A ramped bwEnv (bandwidth2 ≈ 0.3 during attack) adds the coupled roughness component but not the lead-in breath.
4. **NEW feature needed — breath bed with tone head-start**: a filtered noise component, band-passed ≈ 1.5-4 kHz (centroid 2.9 kHz, skirts -4 dB/oct; shareable with the formant/BandSpectrum path so it tracks the instrument body). Its level is CONSTANT, ≈ -36 dB rel the sustain tone (not a loud decaying burst); it starts at note-on while the tone is delayed by ≈ 0.15 s @150 Hz, 0.10 s @400 Hz, 0.04 s @1000 Hz (noise-lead law f0^-0.5) and then rises on the t50 curve — the breathiness is exposure of the bed during the tB window (0.39 s @150 Hz, 0.18 s @400 Hz, 0.09 s @1000 Hz), not extra noise gain. Optional: +8..+12 dB attack-only noise excess for throat-register patches (sounding ~350-450 Hz). The coupling (shared formant shaping, noise gated by the same note-on, level tied to the tone's sustain level) is what distinguishes this from the previously-rejected independent parallel tone+noise sum.
5. **Odd/even during attack** (optional refinement): give even partials a small transient boost or earlier onset in chalumeau patches (+3..+7 dB attack-only), decaying to the odd-dominant sustain balance.

## Plots

![clarinet_attack_rise.png](clarinet_attack_rise.png)
![clarinet_attack_ntr.png](clarinet_attack_ntr.png)
![clarinet_attack_register.png](clarinet_attack_register.png)
![clarinet_attack_noisepsd.png](clarinet_attack_noisepsd.png)
