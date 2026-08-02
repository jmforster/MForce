# Oversampled FM render path — design (item 7, G3)

Dipsy, 2026-07-30. Autonomous dsp run.

## Problem
FM/PM synthesis generates carrier sidebands at `f_c ± k·f_m` for high modulation
index `k`. At high carrier frequency / high index, sidebands exceed Nyquist and
fold back as inharmonic aliasing — the classic FM "dirtiness" on bright, high
patches. Goal (G3): conquer aliasing via oversampling; measure the suppression.

## Approach
Oversample the nonlinearity, not the control rate. The aliasing originates in the
`sin()` carrier nonlinearity, so running carrier+modulator phase accumulation at
`M×SR` pushes Nyquist to `M·SR/2` (sidebands fold-free), then a decimation
lowpass removes everything above the base Nyquist before downsampling by M.

Implemented entirely inside `FMSource::compute_wave_value` — FMSource keeps its
own `carrierPhase_`/`modPhase_` independent of the base-class `currPos_`
bookkeeping, so oversampling is local and does not perturb the graph.

### Config
- `oversample` (Int, 1..16, default **1**). Default 1 ⇒ existing patches
  (incl. the fm_spacy_n177 SOLVED family) render byte-for-byte identical: the
  M==1 branch is the original single-step code with no filter.

### Decimation filter
Reuse `BWLPSection` (filters.h) — 4 cascaded 2nd-order Butterworth LP sections
(8th-order, ~48 dB/oct) at rate `M·SR`, fixed cutoff `0.45·SR`. Built in
`prepare()` (per-note, not hot loop; no per-sample heap alloc). Every sub-sample
is filtered; the last of each group of M is the output (decimate-by-M).

### Control params
`carrierRatio`/`modRatio`/`depth` are sampled once per output sample and held
across the M sub-steps. Correct for control-rate modulation; audio-rate param
modulation would want its own oversampling (future, if a sweep motivates it).

### Legacy-precision note
The spacy character depends on float-mantissa exhaustion of `modPhase_` at ~8 s.
Oversampling shrinks the per-step increment, shifting that timing — but only for
patches that opt into oversample>1. The spacy patches stay at M=1. Documented,
not a regression.

## Verification (metric — reference-render method)
Render one heavily-aliasing FM test patch at M = 1,2,4,8 and M=16 (clean
ground-truth reference). Alias residual(M) = RMS of the in-band (0–18 kHz)
magnitude-spectrum difference vs the M=16 reference. Suppression(M) in dB =
20·log10(residual_1 / residual_M). No sideband bookkeeping; magnitude-only so
filter group-delay is common-mode. Band capped at 18 kHz to stay below the
decimation filter's transition so the comparison measures folded aliases, not
the (common-mode) rolloff.

Tooling: tools/gen_fm_oversample_test.py (patches), research/fm_alias/measure.py
(metric, numpy/scipy).

## Convention (Matt-approved 2026-08-01)

Engine default stays oversample=1 forever (byte-stability). Authoring
rule keys on CARRIER FREQUENCY and DEVIATION (index x mod freq), not
index: high-carrier patches use os4-8 (converges, cleans); low-carrier /
huge-deviation patches stay os1 — there oversampling changes character
rather than removing aliasing (measured run 12), so >1 is a flavor
choice, not a fix.
