# No-throw and wire measurements (for checkpoint 2.5)

Taken 2026-10-20 at engine commit `2954f7d` over the 1615 patches of the set files library.txt, baselines.txt, pending.txt, audition.txt, sweep.txt, old.txt, taps.txt, retired.txt (`cli_25_t0.exe`, `MFORCE_TRACE_WRAP=1`, `python tools/measure_25.py`).
An *unknown key* is a param the loader warns about and ignores today; after the cut such a patch fails to load. A *non-loader* is a patch the CLI could not load or render, with its first ERROR line. A *wrap* is a DelayLine read that landed on the slot past the ring; after the wrap fix the read is slot 0 and the render moves.

## 1 patches the loader warns about (they stop loading after Task 3)

### library (0 of 80)

- none

### baselines (0 of 52)

- none

### pending (0 of 43)

- none

### audition (0 of 708)

- none

### sweep (0 of 457)

- none

### old (1 of 275)

- `patches/old/baselines_culled_2026-09-14/ks_morph_saw_test.json`: `WavetableSource.decayFactor`; `WavetableSource.holdCycles`; `WavetableSource.morphRate`; `WavetableSource.targetLength`

### taps (0 of 47)

- none

### retired (0 of 2)

- none

## 157 patches that do not load or render, by error

### `Unknown node type:` (155)

- `patches/old/fhn/N16_e0.02_D0.0_I0.4_r80000_dt0.0.json`: Unknown node type: FitzhughNagumoSource
- `patches/old/fhn/N16_e0.02_D0.0_I0.6_r300000_dt0.15.json`: Unknown node type: FitzhughNagumoSource
- `patches/old/fhn/N16_e0.02_D0.0_I1.0_r300000_dt0.0.json`: Unknown node type: FitzhughNagumoSource
- `patches/old/fhn/N16_e0.02_D0.0_I1.0_r80000_dt0.15.json`: Unknown node type: FitzhughNagumoSource
- `patches/old/fhn/N16_e0.02_D0.3_I0.4_r80000_dt0.0.json`: Unknown node type: FitzhughNagumoSource
- `patches/old/fhn/N16_e0.02_D0.3_I0.6_r300000_dt0.15.json`: Unknown node type: FitzhughNagumoSource
- `patches/old/fhn/N16_e0.02_D0.3_I1.0_r300000_dt0.0.json`: Unknown node type: FitzhughNagumoSource
- `patches/old/fhn/N16_e0.02_D0.3_I1.0_r80000_dt0.15.json`: Unknown node type: FitzhughNagumoSource
- `patches/old/fhn/N16_e0.02_D1.5_I0.4_r80000_dt0.0.json`: Unknown node type: FitzhughNagumoSource
- `patches/old/fhn/N16_e0.02_D1.5_I0.6_r300000_dt0.15.json`: Unknown node type: FitzhughNagumoSource
- `patches/old/fhn/N16_e0.02_D1.5_I1.0_r300000_dt0.0.json`: Unknown node type: FitzhughNagumoSource
- `patches/old/fhn/N16_e0.02_D1.5_I1.0_r80000_dt0.15.json`: Unknown node type: FitzhughNagumoSource
- `patches/old/fhn/N16_e0.08_D0.0_I0.4_r80000_dt0.0.json`: Unknown node type: FitzhughNagumoSource
- `patches/old/fhn/N16_e0.08_D0.0_I0.6_r300000_dt0.15.json`: Unknown node type: FitzhughNagumoSource
- `patches/old/fhn/N16_e0.08_D0.0_I1.0_r300000_dt0.0.json`: Unknown node type: FitzhughNagumoSource
- `patches/old/fhn/N16_e0.08_D0.0_I1.0_r80000_dt0.15.json`: Unknown node type: FitzhughNagumoSource
- `patches/old/fhn/N16_e0.08_D0.3_I0.4_r80000_dt0.0.json`: Unknown node type: FitzhughNagumoSource
- `patches/old/fhn/N16_e0.08_D0.3_I0.6_r300000_dt0.15.json`: Unknown node type: FitzhughNagumoSource
- `patches/old/fhn/N16_e0.08_D0.3_I1.0_r300000_dt0.0.json`: Unknown node type: FitzhughNagumoSource
- `patches/old/fhn/N16_e0.08_D0.3_I1.0_r80000_dt0.15.json`: Unknown node type: FitzhughNagumoSource
- `patches/old/fhn/N16_e0.08_D1.5_I0.4_r80000_dt0.0.json`: Unknown node type: FitzhughNagumoSource
- `patches/old/fhn/N16_e0.08_D1.5_I0.6_r300000_dt0.15.json`: Unknown node type: FitzhughNagumoSource
- `patches/old/fhn/N16_e0.08_D1.5_I1.0_r300000_dt0.0.json`: Unknown node type: FitzhughNagumoSource
- `patches/old/fhn/N16_e0.08_D1.5_I1.0_r80000_dt0.15.json`: Unknown node type: FitzhughNagumoSource
- `patches/old/fhn/N16_e0.2_D0.0_I0.4_r80000_dt0.0.json`: Unknown node type: FitzhughNagumoSource
- `patches/old/fhn/N16_e0.2_D0.0_I0.6_r300000_dt0.15.json`: Unknown node type: FitzhughNagumoSource
- `patches/old/fhn/N16_e0.2_D0.0_I1.0_r300000_dt0.0.json`: Unknown node type: FitzhughNagumoSource
- `patches/old/fhn/N16_e0.2_D0.0_I1.0_r80000_dt0.15.json`: Unknown node type: FitzhughNagumoSource
- `patches/old/fhn/N16_e0.2_D0.3_I0.4_r80000_dt0.0.json`: Unknown node type: FitzhughNagumoSource
- `patches/old/fhn/N16_e0.2_D0.3_I0.6_r300000_dt0.15.json`: Unknown node type: FitzhughNagumoSource
- `patches/old/fhn/N16_e0.2_D0.3_I1.0_r300000_dt0.0.json`: Unknown node type: FitzhughNagumoSource
- `patches/old/fhn/N16_e0.2_D0.3_I1.0_r80000_dt0.15.json`: Unknown node type: FitzhughNagumoSource
- `patches/old/fhn/N16_e0.2_D1.5_I0.4_r80000_dt0.0.json`: Unknown node type: FitzhughNagumoSource
- `patches/old/fhn/N16_e0.2_D1.5_I0.6_r300000_dt0.15.json`: Unknown node type: FitzhughNagumoSource
- `patches/old/fhn/N16_e0.2_D1.5_I1.0_r300000_dt0.0.json`: Unknown node type: FitzhughNagumoSource
- `patches/old/fhn/N16_e0.2_D1.5_I1.0_r80000_dt0.15.json`: Unknown node type: FitzhughNagumoSource
- `patches/old/fhn/N1_e0.02_D0.0_I0.6_r300000_dt0.0.json`: Unknown node type: FitzhughNagumoSource
- `patches/old/fhn/N1_e0.02_D0.0_I1.0_r80000_dt0.0.json`: Unknown node type: FitzhughNagumoSource
- `patches/old/fhn/N1_e0.08_D0.0_I0.6_r300000_dt0.0.json`: Unknown node type: FitzhughNagumoSource
- `patches/old/fhn/N1_e0.08_D0.0_I1.0_r80000_dt0.0.json`: Unknown node type: FitzhughNagumoSource
- `patches/old/fhn/N1_e0.2_D0.0_I0.6_r300000_dt0.0.json`: Unknown node type: FitzhughNagumoSource
- `patches/old/fhn/N1_e0.2_D0.0_I1.0_r80000_dt0.0.json`: Unknown node type: FitzhughNagumoSource
- `patches/old/fhn/N4_e0.02_D0.0_I0.4_r80000_dt0.0.json`: Unknown node type: FitzhughNagumoSource
- `patches/old/fhn/N4_e0.02_D0.0_I0.6_r300000_dt0.15.json`: Unknown node type: FitzhughNagumoSource
- `patches/old/fhn/N4_e0.02_D0.0_I1.0_r300000_dt0.0.json`: Unknown node type: FitzhughNagumoSource
- `patches/old/fhn/N4_e0.02_D0.0_I1.0_r80000_dt0.15.json`: Unknown node type: FitzhughNagumoSource
- `patches/old/fhn/N4_e0.02_D0.3_I0.4_r80000_dt0.0.json`: Unknown node type: FitzhughNagumoSource
- `patches/old/fhn/N4_e0.02_D0.3_I0.6_r300000_dt0.15.json`: Unknown node type: FitzhughNagumoSource
- `patches/old/fhn/N4_e0.02_D0.3_I1.0_r300000_dt0.0.json`: Unknown node type: FitzhughNagumoSource
- `patches/old/fhn/N4_e0.02_D0.3_I1.0_r80000_dt0.15.json`: Unknown node type: FitzhughNagumoSource
- `patches/old/fhn/N4_e0.02_D1.5_I0.4_r80000_dt0.0.json`: Unknown node type: FitzhughNagumoSource
- `patches/old/fhn/N4_e0.02_D1.5_I0.6_r300000_dt0.15.json`: Unknown node type: FitzhughNagumoSource
- `patches/old/fhn/N4_e0.02_D1.5_I1.0_r300000_dt0.0.json`: Unknown node type: FitzhughNagumoSource
- `patches/old/fhn/N4_e0.02_D1.5_I1.0_r80000_dt0.15.json`: Unknown node type: FitzhughNagumoSource
- `patches/old/fhn/N4_e0.08_D0.0_I0.4_r80000_dt0.0.json`: Unknown node type: FitzhughNagumoSource
- `patches/old/fhn/N4_e0.08_D0.0_I0.6_r300000_dt0.15.json`: Unknown node type: FitzhughNagumoSource
- `patches/old/fhn/N4_e0.08_D0.0_I1.0_r300000_dt0.0.json`: Unknown node type: FitzhughNagumoSource
- `patches/old/fhn/N4_e0.08_D0.0_I1.0_r80000_dt0.15.json`: Unknown node type: FitzhughNagumoSource
- `patches/old/fhn/N4_e0.08_D0.3_I0.4_r80000_dt0.0.json`: Unknown node type: FitzhughNagumoSource
- `patches/old/fhn/N4_e0.08_D0.3_I0.6_r300000_dt0.15.json`: Unknown node type: FitzhughNagumoSource
- `patches/old/fhn/N4_e0.08_D0.3_I1.0_r300000_dt0.0.json`: Unknown node type: FitzhughNagumoSource
- `patches/old/fhn/N4_e0.08_D0.3_I1.0_r80000_dt0.15.json`: Unknown node type: FitzhughNagumoSource
- `patches/old/fhn/N4_e0.08_D1.5_I0.4_r80000_dt0.0.json`: Unknown node type: FitzhughNagumoSource
- `patches/old/fhn/N4_e0.08_D1.5_I0.6_r300000_dt0.15.json`: Unknown node type: FitzhughNagumoSource
- `patches/old/fhn/N4_e0.08_D1.5_I1.0_r300000_dt0.0.json`: Unknown node type: FitzhughNagumoSource
- `patches/old/fhn/N4_e0.08_D1.5_I1.0_r80000_dt0.15.json`: Unknown node type: FitzhughNagumoSource
- `patches/old/fhn/N4_e0.2_D0.0_I0.4_r80000_dt0.0.json`: Unknown node type: FitzhughNagumoSource
- `patches/old/fhn/N4_e0.2_D0.0_I0.6_r300000_dt0.15.json`: Unknown node type: FitzhughNagumoSource
- `patches/old/fhn/N4_e0.2_D0.0_I1.0_r300000_dt0.0.json`: Unknown node type: FitzhughNagumoSource
- `patches/old/fhn/N4_e0.2_D0.0_I1.0_r80000_dt0.15.json`: Unknown node type: FitzhughNagumoSource
- `patches/old/fhn/N4_e0.2_D0.3_I0.4_r80000_dt0.0.json`: Unknown node type: FitzhughNagumoSource
- `patches/old/fhn/N4_e0.2_D0.3_I0.6_r300000_dt0.15.json`: Unknown node type: FitzhughNagumoSource
- `patches/old/fhn/N4_e0.2_D0.3_I1.0_r300000_dt0.0.json`: Unknown node type: FitzhughNagumoSource
- `patches/old/fhn/N4_e0.2_D0.3_I1.0_r80000_dt0.15.json`: Unknown node type: FitzhughNagumoSource
- `patches/old/fhn/N4_e0.2_D1.5_I0.4_r80000_dt0.0.json`: Unknown node type: FitzhughNagumoSource
- `patches/old/fhn/N4_e0.2_D1.5_I0.6_r300000_dt0.15.json`: Unknown node type: FitzhughNagumoSource
- `patches/old/fhn/N4_e0.2_D1.5_I1.0_r300000_dt0.0.json`: Unknown node type: FitzhughNagumoSource
- `patches/old/fhn/N4_e0.2_D1.5_I1.0_r80000_dt0.15.json`: Unknown node type: FitzhughNagumoSource
- `patches/old/fhn/N64_e0.02_D0.0_I0.4_r80000_dt0.0.json`: Unknown node type: FitzhughNagumoSource
- `patches/old/fhn/N64_e0.02_D0.0_I0.6_r300000_dt0.15.json`: Unknown node type: FitzhughNagumoSource
- `patches/old/fhn/N64_e0.02_D0.0_I1.0_r300000_dt0.0.json`: Unknown node type: FitzhughNagumoSource
- `patches/old/fhn/N64_e0.02_D0.0_I1.0_r80000_dt0.15.json`: Unknown node type: FitzhughNagumoSource
- `patches/old/fhn/N64_e0.02_D0.3_I0.4_r80000_dt0.0.json`: Unknown node type: FitzhughNagumoSource
- `patches/old/fhn/N64_e0.02_D0.3_I0.6_r300000_dt0.15.json`: Unknown node type: FitzhughNagumoSource
- `patches/old/fhn/N64_e0.02_D0.3_I1.0_r300000_dt0.0.json`: Unknown node type: FitzhughNagumoSource
- `patches/old/fhn/N64_e0.02_D0.3_I1.0_r80000_dt0.15.json`: Unknown node type: FitzhughNagumoSource
- `patches/old/fhn/N64_e0.02_D1.5_I0.4_r80000_dt0.0.json`: Unknown node type: FitzhughNagumoSource
- `patches/old/fhn/N64_e0.02_D1.5_I0.6_r300000_dt0.15.json`: Unknown node type: FitzhughNagumoSource
- `patches/old/fhn/N64_e0.02_D1.5_I1.0_r300000_dt0.0.json`: Unknown node type: FitzhughNagumoSource
- `patches/old/fhn/N64_e0.02_D1.5_I1.0_r80000_dt0.15.json`: Unknown node type: FitzhughNagumoSource
- `patches/old/fhn/N64_e0.08_D0.0_I0.4_r80000_dt0.0.json`: Unknown node type: FitzhughNagumoSource
- `patches/old/fhn/N64_e0.08_D0.0_I0.6_r300000_dt0.15.json`: Unknown node type: FitzhughNagumoSource
- `patches/old/fhn/N64_e0.08_D0.0_I1.0_r300000_dt0.0.json`: Unknown node type: FitzhughNagumoSource
- `patches/old/fhn/N64_e0.08_D0.0_I1.0_r80000_dt0.15.json`: Unknown node type: FitzhughNagumoSource
- `patches/old/fhn/N64_e0.08_D0.3_I0.4_r80000_dt0.0.json`: Unknown node type: FitzhughNagumoSource
- `patches/old/fhn/N64_e0.08_D0.3_I0.6_r300000_dt0.15.json`: Unknown node type: FitzhughNagumoSource
- `patches/old/fhn/N64_e0.08_D0.3_I1.0_r300000_dt0.0.json`: Unknown node type: FitzhughNagumoSource
- `patches/old/fhn/N64_e0.08_D0.3_I1.0_r80000_dt0.15.json`: Unknown node type: FitzhughNagumoSource
- `patches/old/fhn/N64_e0.08_D1.5_I0.4_r80000_dt0.0.json`: Unknown node type: FitzhughNagumoSource
- `patches/old/fhn/N64_e0.08_D1.5_I0.6_r300000_dt0.15.json`: Unknown node type: FitzhughNagumoSource
- `patches/old/fhn/N64_e0.08_D1.5_I1.0_r300000_dt0.0.json`: Unknown node type: FitzhughNagumoSource
- `patches/old/fhn/N64_e0.08_D1.5_I1.0_r80000_dt0.15.json`: Unknown node type: FitzhughNagumoSource
- `patches/old/fhn/N64_e0.2_D0.0_I0.4_r80000_dt0.0.json`: Unknown node type: FitzhughNagumoSource
- `patches/old/fhn/N64_e0.2_D0.0_I0.6_r300000_dt0.15.json`: Unknown node type: FitzhughNagumoSource
- `patches/old/fhn/N64_e0.2_D0.0_I1.0_r300000_dt0.0.json`: Unknown node type: FitzhughNagumoSource
- `patches/old/fhn/N64_e0.2_D0.0_I1.0_r80000_dt0.15.json`: Unknown node type: FitzhughNagumoSource
- `patches/old/fhn/N64_e0.2_D0.3_I0.4_r80000_dt0.0.json`: Unknown node type: FitzhughNagumoSource
- `patches/old/fhn/N64_e0.2_D0.3_I0.6_r300000_dt0.15.json`: Unknown node type: FitzhughNagumoSource
- `patches/old/fhn/N64_e0.2_D0.3_I1.0_r300000_dt0.0.json`: Unknown node type: FitzhughNagumoSource
- `patches/old/fhn/N64_e0.2_D0.3_I1.0_r80000_dt0.15.json`: Unknown node type: FitzhughNagumoSource
- `patches/old/fhn/N64_e0.2_D1.5_I0.4_r80000_dt0.0.json`: Unknown node type: FitzhughNagumoSource
- `patches/old/fhn/N64_e0.2_D1.5_I0.6_r300000_dt0.15.json`: Unknown node type: FitzhughNagumoSource
- `patches/old/fhn/N64_e0.2_D1.5_I1.0_r300000_dt0.0.json`: Unknown node type: FitzhughNagumoSource
- `patches/old/fhn/N64_e0.2_D1.5_I1.0_r80000_dt0.15.json`: Unknown node type: FitzhughNagumoSource
- `patches/old/fhn_sub_bass.json`: Unknown node type: FitzhughNagumoSource
- `patches/old/sort_bubble_cursor.json`: Unknown node type: SortOscillator
- `patches/pending/gs_chaotic.json`: Unknown node type: GrayScottSource
- `patches/pending/gs_spots.json`: Unknown node type: GrayScottSource
- `patches/pending/gs_waves.json`: Unknown node type: GrayScottSource
- `patches/sweep/F0.010_k0.030_r100000_N128.json`: Unknown node type: GrayScottSource
- `patches/sweep/F0.010_k0.030_r300000_N32.json`: Unknown node type: GrayScottSource
- `patches/sweep/F0.010_k0.045_r100000_N128.json`: Unknown node type: GrayScottSource
- `patches/sweep/F0.010_k0.045_r300000_N32.json`: Unknown node type: GrayScottSource
- `patches/sweep/F0.010_k0.055_r100000_N128.json`: Unknown node type: GrayScottSource
- `patches/sweep/F0.010_k0.055_r300000_N32.json`: Unknown node type: GrayScottSource
- `patches/sweep/F0.010_k0.065_r100000_N128.json`: Unknown node type: GrayScottSource
- `patches/sweep/F0.010_k0.065_r300000_N32.json`: Unknown node type: GrayScottSource
- `patches/sweep/F0.022_k0.030_r100000_N128.json`: Unknown node type: GrayScottSource
- `patches/sweep/F0.022_k0.030_r300000_N32.json`: Unknown node type: GrayScottSource
- `patches/sweep/F0.022_k0.045_r100000_N128.json`: Unknown node type: GrayScottSource
- `patches/sweep/F0.022_k0.045_r300000_N32.json`: Unknown node type: GrayScottSource
- `patches/sweep/F0.022_k0.055_r100000_N128.json`: Unknown node type: GrayScottSource
- `patches/sweep/F0.022_k0.055_r300000_N32.json`: Unknown node type: GrayScottSource
- `patches/sweep/F0.022_k0.065_r100000_N128.json`: Unknown node type: GrayScottSource
- `patches/sweep/F0.022_k0.065_r300000_N32.json`: Unknown node type: GrayScottSource
- `patches/sweep/F0.040_k0.030_r100000_N128.json`: Unknown node type: GrayScottSource
- `patches/sweep/F0.040_k0.030_r300000_N32.json`: Unknown node type: GrayScottSource
- `patches/sweep/F0.040_k0.045_r100000_N128.json`: Unknown node type: GrayScottSource
- `patches/sweep/F0.040_k0.045_r300000_N32.json`: Unknown node type: GrayScottSource
- `patches/sweep/F0.040_k0.055_r100000_N128.json`: Unknown node type: GrayScottSource
- `patches/sweep/F0.040_k0.055_r300000_N32.json`: Unknown node type: GrayScottSource
- `patches/sweep/F0.040_k0.065_r100000_N128.json`: Unknown node type: GrayScottSource
- `patches/sweep/F0.040_k0.065_r300000_N32.json`: Unknown node type: GrayScottSource
- `patches/sweep/F0.060_k0.030_r100000_N128.json`: Unknown node type: GrayScottSource
- `patches/sweep/F0.060_k0.030_r300000_N32.json`: Unknown node type: GrayScottSource
- `patches/sweep/F0.060_k0.045_r100000_N128.json`: Unknown node type: GrayScottSource
- `patches/sweep/F0.060_k0.045_r300000_N32.json`: Unknown node type: GrayScottSource
- `patches/sweep/F0.060_k0.055_r100000_N128.json`: Unknown node type: GrayScottSource
- `patches/sweep/F0.060_k0.055_r300000_N32.json`: Unknown node type: GrayScottSource
- `patches/sweep/F0.060_k0.065_r100000_N128.json`: Unknown node type: GrayScottSource
- `patches/sweep/F0.060_k0.065_r300000_N32.json`: Unknown node type: GrayScottSource
- `patches/sweep/sweep_F0.010_k0.030_r200000_N256.json`: Unknown node type: GrayScottSource
- `patches/sweep/sweep_F0.010_k0.030_r50000_N64.json`: Unknown node type: GrayScottSource
- `patches/sweep/sweep_F0.010_k0.050_r200000_N256.json`: Unknown node type: GrayScottSource
- `patches/sweep/sweep_F0.010_k0.050_r50000_N64.json`: Unknown node type: GrayScottSource

### `[json.exception.out_of_range.403] key 'graph'` (2)

- `patches/sweep/feedback_curves1/manifest.json`: [json.exception.out_of_range.403] key 'graph' not found
- `patches/sweep/feedback_curves4_criticals.json`: [json.exception.out_of_range.403] key 'graph' not found

## 28 patches whose DelayLine read hits the slot past the ring (expected to change under the wrap fix)

### library (0 of 80)

- none

### baselines (2 of 52)

- `patches/baselines/double_advance_bug.json`: 1470 reads
- `patches/baselines/double_advance_ok.json`: 1470 reads

### pending (1 of 43)

- `patches/pending/brass/Trombone_att1.json`: 1 reads

### audition (14 of 708)

- `patches/audition/bwg_perc1/bwg_chime.json`: 3864 reads
- `patches/audition/bwg_perc1/bwg_glass_long.json`: 4462 reads
- `patches/audition/bwg_perc1/bwg_marimba.json`: 972 reads
- `patches/audition/bwg_perc1/bwg_tomdrum.json`: 1147 reads
- `patches/audition/bwg_perc1/bwg_vibraphone.json`: 1910 reads
- `patches/audition/bwg_perc1/bwg_woodblock.json`: 588 reads
- `patches/audition/bwg_perc1/bwg_xylophone.json`: 718 reads
- `patches/audition/bwg_perc2/bwg2_chime.json`: 3264 reads
- `patches/audition/bwg_perc2/bwg2_glass_long.json`: 2426 reads
- `patches/audition/bwg_perc2/bwg2_marimba.json`: 978 reads
- `patches/audition/bwg_perc2/bwg2_tomdrum.json`: 1092 reads
- `patches/audition/bwg_perc2/bwg2_vibraphone.json`: 1559 reads
- `patches/audition/bwg_perc2/bwg2_woodblock.json`: 588 reads
- `patches/audition/bwg_perc2/bwg2_xylophone.json`: 746 reads

### sweep (11 of 457)

- `patches/sweep/feedback_valve1/fbv_r090_q5.json`: 1 reads
- `patches/sweep/nlbore_probe1/nlbore_sine_d005.json`: 2 reads
- `patches/sweep/nlbore_probe1/nlbore_sine_neg040.json`: 1 reads
- `patches/sweep/stk_bandedwg_port/stk_bandedwg_bowl_struck.json`: 5852 reads
- `patches/sweep/stk_bandedwg_port/stk_bandedwg_tbar_bowed.json`: 1470 reads
- `patches/sweep/stk_blowhole_port/stk_blowhole_default.json`: 240 reads
- `patches/sweep/stk_blowhole_port/stk_blowhole_reed_hard.json`: 240 reads
- `patches/sweep/stk_blowhole_port/stk_blowhole_th_half.json`: 240 reads
- `patches/sweep/trombone1/loud.json`: 1 reads
- `patches/sweep/trombone3/ab_attempt2_ff.json`: 2 reads
- `patches/sweep/trombone3/line_soft.json`: 6 reads

### old (0 of 275)

- none

### taps (9 of 47)

- `patches/pending/brass/Trombone_att1.json`: 1 reads
- `patches/sweep/nlbore_probe1/nlbore_sine_d005.json`: 2 reads
- `patches/sweep/nlbore_probe1/nlbore_sine_neg040.json`: 1 reads
- `patches/sweep/stk_blowhole_port/stk_blowhole_default.json`: 240 reads
- `patches/sweep/stk_blowhole_port/stk_blowhole_reed_hard.json`: 240 reads
- `patches/sweep/stk_blowhole_port/stk_blowhole_th_half.json`: 240 reads
- `patches/sweep/trombone1/loud.json`: 1 reads
- `patches/sweep/trombone3/ab_attempt2_ff.json`: 2 reads
- `patches/sweep/trombone3/line_soft.json`: 6 reads

### retired (0 of 2)

- none

## Lint cross-check

`python tools/lint_patches.py` over `patches/library`, `patches/baselines`, `patches/audition`,
`patches/pending` (the tracked folders and Matt's queues), against the loader's own warnings above:

- The lint reports 8 findings in library and 318 in audition, but almost all are its own
  paramMap range notes (`curve endpoints 800..1100 (want 20..16000)`: 8 library, 268 audition)
  and 36 `instrument.release (retired)` notes — not node keys; the loader warns about the
  retired instrument-block key separately and does not reject it (an instrument-block key is
  Part 4's codec, outside this checkpoint).
- The lint found 14 genuine node keys the loader did NOT warn about: `VarSource.value` in the
  seven `audition/bwg_perc1` and seven `audition/bwg_perc2` patches. The loader is blind there:
  `VarSource` is built by a hand-written branch (`val`, `var`, `varPct`) that never reaches the
  unknown-key block inside `wire_params_generic`. So the loader's warning is the authority for
  descriptor-wired nodes only, and the list of patches that stop loading after the cut is the
  UNION of the loader's warnings and the lint's node-key findings; the cut itself runs the
  unknown-key check once per node, whichever path built it.
- Repaired before the cut (the patches are Matt's listening queue, gitignored): the `value`
  key, 0.0 in all fourteen, was removed from the VarSource node `Input` by a byte-surgical
  edit (the rest of each file untouched); each patch re-rendered with `cli_pre25.exe` and
  compared against checkpoint 2.4's arm — 14 of 14 identical, as the branch's `val` default
  is 0.0.
- What stops loading after the cut, then: `old/baselines_culled_2026-09-14/ks_morph_saw_test.json`
  (the legacy `ks_morph` keys on a WavetableSource; the archive, never deleted) — and nothing
  in library, baselines, pending or audition.

## The non-loaders are not render-path throws

Of the 157 patches that do not load, 155 fail on `Unknown node type` (114 of them the
`FitzhughNagumoSource` sweep under `old/fhn`) and 2 are sweep manifests without a `graph` key.
None reaches `WaveSource::compute` or `WavetableSource::fill_table`, so removing the two
render-path throws (Task 1) moves no stored render; the clamp-and-fault path is reachable only
by a patch no file in the tree expresses.

## The wrap list is an upper bound

A read that lands on the slot past the ring returned that slot's zero; after the fix it
returns slot 0. The render moves only where slot 0 held something other than zero at that
moment. The 28 listed patches are the ones where the read happened at all; the A/B at Task 4
says which of them moved. Both `double_advance` baselines are on it (1,470 reads each, the
same count — the `_ok` and `_bug` pair share their loop), so the manifest is refrozen for those
two entries and no library entry.

