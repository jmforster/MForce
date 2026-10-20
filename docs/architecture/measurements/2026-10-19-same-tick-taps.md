# Same-tick tap measurements (for the tap rule, checkpoint 2.4)

Taken 2026-10-19 at engine commit `fc7ad11` over the 1615 patches of the set files library.txt, baselines.txt, audition.txt, pending.txt, sweep.txt, old.txt, taps.txt, retired.txt (`cli_t0.exe`, `MFORCE_TRACE_TAPS=1`, `python tools/measure_taps.py`).
A *same-tick tap read* is a tap reading a source that had already claimed and finished this tick's compute: reading A hands it this tick's value, reading B the value held at the end of the previous tick. The count is an upper bound on the renders that change: a read of a value that did not move between the two ticks changes nothing.

## 45 patches with same-tick tap reads (expected to change under reading B)

### library (0 of 80)

- none

### baselines (0 of 52)

- none

### audition (10 of 708)

- `patches/audition/pierce1d_1/pierce_ctl_s00.json`: 996083 same-tick tap reads
- `patches/audition/pierce1d_1/pierce_s12k.json`: 1091818 same-tick tap reads
- `patches/audition/pierce1d_1/pierce_s1k.json`: 1019933 same-tick tap reads
- `patches/audition/pierce1d_1/pierce_s3k.json`: 1050423 same-tick tap reads
- `patches/audition/pierce1d_1/pierce_s5k.json`: 1069308 same-tick tap reads
- `patches/audition/pierce1d_1/pierce_s8k.json`: 1085143 same-tick tap reads
- `patches/audition/trombone1/trombone_attempt1.json`: 648000 same-tick tap reads
- `patches/audition/trombone1/trombone_attempt2.json`: 653270 same-tick tap reads
- `patches/audition/trombone1/trombone_attempt3.json`: 653489 same-tick tap reads
- `patches/audition/trombone1/trombone_attempt4.json`: 653488 same-tick tap reads

### pending (1 of 43)

- `patches/pending/brass/Trombone_att1.json`: 725622 same-tick tap reads

### sweep (28 of 457)

- `patches/sweep/nlbore_probe1/nlbore_flute_d030.json`: 316800 same-tick tap reads
- `patches/sweep/nlbore_probe1/nlbore_oboe_ctl.json`: 576753 same-tick tap reads
- `patches/sweep/nlbore_probe1/nlbore_oboe_soft_d030.json`: 576765 same-tick tap reads
- `patches/sweep/nlbore_probe1/nlbore_sine_d005.json`: 504000 same-tick tap reads
- `patches/sweep/nlbore_probe1/nlbore_sine_d040.json`: 504000 same-tick tap reads
- `patches/sweep/nlbore_probe1/nlbore_sine_f0466_d040.json`: 504000 same-tick tap reads
- `patches/sweep/nlbore_probe1/nlbore_sine_neg040.json`: 504000 same-tick tap reads
- `patches/sweep/onemass_lip1/flat_r080_loud_st1.json`: 345600 same-tick tap reads
- `patches/sweep/onemass_lip1/lip_r080_loud_st0.json`: 345600 same-tick tap reads
- `patches/sweep/onemass_lip1/lip_r080_soft_st1.json`: 345600 same-tick tap reads
- `patches/sweep/onemass_lip1/lip_r100_soft_st0.json`: 345600 same-tick tap reads
- `patches/sweep/onemass_lip1/lip_r120_loud_st1.json`: 345600 same-tick tap reads
- `patches/sweep/onemass_lip1/reed_r080_loud_st0.json`: 345600 same-tick tap reads
- `patches/sweep/onemass_lip1/reed_r080_soft_st1.json`: 345600 same-tick tap reads
- `patches/sweep/stk_blowhole_port/stk_blowhole_default.json`: 264600 same-tick tap reads
- `patches/sweep/stk_blowhole_port/stk_blowhole_reed_hard.json`: 264600 same-tick tap reads
- `patches/sweep/stk_blowhole_port/stk_blowhole_th_half.json`: 264600 same-tick tap reads
- `patches/sweep/trombone1/line_bell_off.json`: 654385 same-tick tap reads
- `patches/sweep/trombone1/loud.json`: 648000 same-tick tap reads
- `patches/sweep/trombone2/held_6s.json`: 307200 same-tick tap reads
- `patches/sweep/trombone2/loud.json`: 654567 same-tick tap reads
- `patches/sweep/trombone2/parity_low.json`: 1308614 same-tick tap reads
- `patches/sweep/trombone3/ab_attempt2_ff.json`: 650209 same-tick tap reads
- `patches/sweep/trombone3/ladder_low.json`: 261789 same-tick tap reads
- `patches/sweep/trombone3/line_soft.json`: 1334143 same-tick tap reads
- `patches/sweep/trombone3/vc_on.json`: 648000 same-tick tap reads
- `patches/sweep/trombone4/hold_hard.json`: 212780 same-tick tap reads
- `patches/sweep/trombone4/ladder_mid.json`: 248425 same-tick tap reads

### old (6 of 275)

- `patches/old/nlbore_brass1/nlb_lip_lad000.json`: 1411200 same-tick tap reads
- `patches/old/nlbore_brass1/nlb_lip_lad030.json`: 1411200 same-tick tap reads
- `patches/old/nlbore_brass1/nlb_lip_loud_d030.json`: 1411200 same-tick tap reads
- `patches/old/nlbore_brass1/nlb_stk_d030_bell.json`: 211200 same-tick tap reads
- `patches/old/nlbore_brass1/nlb_stk_lad007.json`: 211200 same-tick tap reads
- `patches/old/nlbore_brass1/nlb_stk_lad060.json`: 211200 same-tick tap reads

### taps (45 of 47)

- `patches/audition/pierce1d_1/pierce_ctl_s00.json`: 996083 same-tick tap reads
- `patches/audition/pierce1d_1/pierce_s12k.json`: 1091818 same-tick tap reads
- `patches/audition/pierce1d_1/pierce_s1k.json`: 1019933 same-tick tap reads
- `patches/audition/pierce1d_1/pierce_s3k.json`: 1050423 same-tick tap reads
- `patches/audition/pierce1d_1/pierce_s5k.json`: 1069308 same-tick tap reads
- `patches/audition/pierce1d_1/pierce_s8k.json`: 1085143 same-tick tap reads
- `patches/audition/trombone1/trombone_attempt1.json`: 648000 same-tick tap reads
- `patches/audition/trombone1/trombone_attempt2.json`: 653270 same-tick tap reads
- `patches/audition/trombone1/trombone_attempt3.json`: 653489 same-tick tap reads
- `patches/audition/trombone1/trombone_attempt4.json`: 653488 same-tick tap reads
- `patches/old/nlbore_brass1/nlb_lip_lad000.json`: 1411200 same-tick tap reads
- `patches/old/nlbore_brass1/nlb_lip_lad030.json`: 1411200 same-tick tap reads
- `patches/old/nlbore_brass1/nlb_lip_loud_d030.json`: 1411200 same-tick tap reads
- `patches/old/nlbore_brass1/nlb_stk_d030_bell.json`: 211200 same-tick tap reads
- `patches/old/nlbore_brass1/nlb_stk_lad007.json`: 211200 same-tick tap reads
- `patches/old/nlbore_brass1/nlb_stk_lad060.json`: 211200 same-tick tap reads
- `patches/pending/brass/Trombone_att1.json`: 725622 same-tick tap reads
- `patches/sweep/nlbore_probe1/nlbore_flute_d030.json`: 316800 same-tick tap reads
- `patches/sweep/nlbore_probe1/nlbore_oboe_ctl.json`: 576753 same-tick tap reads
- `patches/sweep/nlbore_probe1/nlbore_oboe_soft_d030.json`: 576765 same-tick tap reads
- `patches/sweep/nlbore_probe1/nlbore_sine_d005.json`: 504000 same-tick tap reads
- `patches/sweep/nlbore_probe1/nlbore_sine_d040.json`: 504000 same-tick tap reads
- `patches/sweep/nlbore_probe1/nlbore_sine_f0466_d040.json`: 504000 same-tick tap reads
- `patches/sweep/nlbore_probe1/nlbore_sine_neg040.json`: 504000 same-tick tap reads
- `patches/sweep/onemass_lip1/flat_r080_loud_st1.json`: 345600 same-tick tap reads
- `patches/sweep/onemass_lip1/lip_r080_loud_st0.json`: 345600 same-tick tap reads
- `patches/sweep/onemass_lip1/lip_r080_soft_st1.json`: 345600 same-tick tap reads
- `patches/sweep/onemass_lip1/lip_r100_soft_st0.json`: 345600 same-tick tap reads
- `patches/sweep/onemass_lip1/lip_r120_loud_st1.json`: 345600 same-tick tap reads
- `patches/sweep/onemass_lip1/reed_r080_loud_st0.json`: 345600 same-tick tap reads
- `patches/sweep/onemass_lip1/reed_r080_soft_st1.json`: 345600 same-tick tap reads
- `patches/sweep/stk_blowhole_port/stk_blowhole_default.json`: 264600 same-tick tap reads
- `patches/sweep/stk_blowhole_port/stk_blowhole_reed_hard.json`: 264600 same-tick tap reads
- `patches/sweep/stk_blowhole_port/stk_blowhole_th_half.json`: 264600 same-tick tap reads
- `patches/sweep/trombone1/line_bell_off.json`: 654385 same-tick tap reads
- `patches/sweep/trombone1/loud.json`: 648000 same-tick tap reads
- `patches/sweep/trombone2/held_6s.json`: 307200 same-tick tap reads
- `patches/sweep/trombone2/loud.json`: 654567 same-tick tap reads
- `patches/sweep/trombone2/parity_low.json`: 1308614 same-tick tap reads
- `patches/sweep/trombone3/ab_attempt2_ff.json`: 650209 same-tick tap reads
- `patches/sweep/trombone3/ladder_low.json`: 261789 same-tick tap reads
- `patches/sweep/trombone3/line_soft.json`: 1334143 same-tick tap reads
- `patches/sweep/trombone3/vc_on.json`: 648000 same-tick tap reads
- `patches/sweep/trombone4/hold_hard.json`: 212780 same-tick tap reads
- `patches/sweep/trombone4/ladder_mid.json`: 248425 same-tick tap reads

### retired (0 of 2)

- none

## Against the 2026-10-18 list (47 patches, taken at `587b539` before the memo)

In the 2026-10-18 list, not counted now:

- `patches/sweep/stk_bowed_ext1/ext_tors_c10.json` (was 1059020)
- `patches/sweep/stk_bowed_ext2/ext2_larson_btd.json` (was 1059020)

Both are the same shape and the same cause, read from `ext_tors_c10.json`'s wiring: the
bow chain `NewVel` (BowTable) ← `DeltaVT` ← `DeltaV` ← `Sum1` ← `StringLP` (a Biquad whose
input is a tap on `BridgeDelay`) has three consumers — `TorsFeed`, `BridgeIn`, `NeckIn` — and
in the engine of 2026-10-18 the first of them in wiring order, `TorsFeed`, was its advancer
while the other two read a wrapper. `TorsFeed` sits inside the tap-only tail `TorsDelay`, so
the whole bow chain advanced AFTER the root pull had already advanced `BridgeDelay` (through
`Body0`), and `StringLP`'s tap read `BridgeDelay` after its advance: a same-tick read, twice
per sample (the sweep's advance and the owner's post-sweep `current()`). The root pull's own
visits to the chain, through `BridgeIn`, were the wrapped reads the 10-18 document lists as
that patch's lag reads (1,588,530), and are why both patches changed at checkpoint 2.3. Under
the memo there is no advancer: the root pull computes the chain at its first visit, inside
`BridgeDelay`'s own compute, where the tap reads `BridgeDelay` mid-compute — the position
where readings A and B agree — and the tail's later pull is served from the memo. The old
count (1,059,020 over 480,000 frames) is dominated by that one tap at two reads per sample;
its exact composition is not reconstructed, since the position no longer exists. Confirmed
by re-running the old binary (`cli_pre23.exe`, `MFORCE_TRACE_SHARING=1`): lagReads=1588530
sameTickTaps=1059020, the 10-18 numbers. Not a defect of the counter: reading B cannot move
these two renders, because no tap in them reads a completed same-tick value any more.

Counted now, not in the 2026-10-18 list:

- none

On both lists with a different count:

- `patches/audition/pierce1d_1/pierce_ctl_s00.json`: 1992166 → 996083
- `patches/audition/pierce1d_1/pierce_s12k.json`: 2183636 → 1091818
- `patches/audition/pierce1d_1/pierce_s1k.json`: 2039866 → 1019933
- `patches/audition/pierce1d_1/pierce_s3k.json`: 2100846 → 1050423
- `patches/audition/pierce1d_1/pierce_s5k.json`: 2138616 → 1069308
- `patches/audition/pierce1d_1/pierce_s8k.json`: 2170286 → 1085143
- `patches/audition/trombone1/trombone_attempt1.json`: 1296000 → 648000
- `patches/audition/trombone1/trombone_attempt2.json`: 1306540 → 653270
- `patches/audition/trombone1/trombone_attempt3.json`: 1306978 → 653489
- `patches/audition/trombone1/trombone_attempt4.json`: 1306976 → 653488
- `patches/old/nlbore_brass1/nlb_lip_lad000.json`: 2822400 → 1411200
- `patches/old/nlbore_brass1/nlb_lip_lad030.json`: 2822400 → 1411200
- `patches/old/nlbore_brass1/nlb_lip_loud_d030.json`: 2822400 → 1411200
- `patches/old/nlbore_brass1/nlb_stk_d030_bell.json`: 422400 → 211200
- `patches/old/nlbore_brass1/nlb_stk_lad007.json`: 422400 → 211200
- `patches/old/nlbore_brass1/nlb_stk_lad060.json`: 422400 → 211200
- `patches/pending/brass/Trombone_att1.json`: 1451244 → 725622
- `patches/sweep/nlbore_probe1/nlbore_flute_d030.json`: 633600 → 316800
- `patches/sweep/nlbore_probe1/nlbore_oboe_ctl.json`: 1153506 → 576753
- `patches/sweep/nlbore_probe1/nlbore_oboe_soft_d030.json`: 1153530 → 576765
- `patches/sweep/nlbore_probe1/nlbore_sine_d005.json`: 1008000 → 504000
- `patches/sweep/nlbore_probe1/nlbore_sine_d040.json`: 1008000 → 504000
- `patches/sweep/nlbore_probe1/nlbore_sine_f0466_d040.json`: 1008000 → 504000
- `patches/sweep/nlbore_probe1/nlbore_sine_neg040.json`: 1008000 → 504000
- `patches/sweep/onemass_lip1/flat_r080_loud_st1.json`: 691200 → 345600
- `patches/sweep/onemass_lip1/lip_r080_loud_st0.json`: 691200 → 345600
- `patches/sweep/onemass_lip1/lip_r080_soft_st1.json`: 691200 → 345600
- `patches/sweep/onemass_lip1/lip_r100_soft_st0.json`: 691200 → 345600
- `patches/sweep/onemass_lip1/lip_r120_loud_st1.json`: 691200 → 345600
- `patches/sweep/onemass_lip1/reed_r080_loud_st0.json`: 691200 → 345600
- `patches/sweep/onemass_lip1/reed_r080_soft_st1.json`: 691200 → 345600
- `patches/sweep/stk_blowhole_port/stk_blowhole_default.json`: 529200 → 264600
- `patches/sweep/stk_blowhole_port/stk_blowhole_reed_hard.json`: 529200 → 264600
- `patches/sweep/stk_blowhole_port/stk_blowhole_th_half.json`: 529200 → 264600
- `patches/sweep/trombone1/line_bell_off.json`: 1308770 → 654385
- `patches/sweep/trombone1/loud.json`: 1296000 → 648000
- `patches/sweep/trombone2/held_6s.json`: 614400 → 307200
- `patches/sweep/trombone2/loud.json`: 1309134 → 654567
- `patches/sweep/trombone2/parity_low.json`: 2617228 → 1308614
- `patches/sweep/trombone3/ab_attempt2_ff.json`: 1300418 → 650209
- `patches/sweep/trombone3/ladder_low.json`: 523578 → 261789
- `patches/sweep/trombone3/line_soft.json`: 2668286 → 1334143
- `patches/sweep/trombone3/vc_on.json`: 1296000 → 648000
- `patches/sweep/trombone4/hold_hard.json`: 425560 → 212780
- `patches/sweep/trombone4/ladder_mid.json`: 496850 → 248425

Every one is exactly half. The 10-18 trace counted inside `RefSource::read()`, which a
consumer ran twice per sample: once through the sweep (`advance()` → `compute()` → `read()`)
and once through its post-sweep `current()` read (checkpoint 2.1's read-after-sweep rule), and
both saw the same already-advanced source. This counter sits in `TapSource::compute()`,
which the memo runs once per tick; the tap's `current()` is not counted. Same reads, counted
once.

## 157 patches that produced no trace line (they do not load)

- `patches/old/fhn/N16_e0.02_D0.0_I0.4_r80000_dt0.0.json`
- `patches/old/fhn/N16_e0.02_D0.0_I0.6_r300000_dt0.15.json`
- `patches/old/fhn/N16_e0.02_D0.0_I1.0_r300000_dt0.0.json`
- `patches/old/fhn/N16_e0.02_D0.0_I1.0_r80000_dt0.15.json`
- `patches/old/fhn/N16_e0.02_D0.3_I0.4_r80000_dt0.0.json`
- `patches/old/fhn/N16_e0.02_D0.3_I0.6_r300000_dt0.15.json`
- `patches/old/fhn/N16_e0.02_D0.3_I1.0_r300000_dt0.0.json`
- `patches/old/fhn/N16_e0.02_D0.3_I1.0_r80000_dt0.15.json`
- `patches/old/fhn/N16_e0.02_D1.5_I0.4_r80000_dt0.0.json`
- `patches/old/fhn/N16_e0.02_D1.5_I0.6_r300000_dt0.15.json`
- `patches/old/fhn/N16_e0.02_D1.5_I1.0_r300000_dt0.0.json`
- `patches/old/fhn/N16_e0.02_D1.5_I1.0_r80000_dt0.15.json`
- `patches/old/fhn/N16_e0.08_D0.0_I0.4_r80000_dt0.0.json`
- `patches/old/fhn/N16_e0.08_D0.0_I0.6_r300000_dt0.15.json`
- `patches/old/fhn/N16_e0.08_D0.0_I1.0_r300000_dt0.0.json`
- `patches/old/fhn/N16_e0.08_D0.0_I1.0_r80000_dt0.15.json`
- `patches/old/fhn/N16_e0.08_D0.3_I0.4_r80000_dt0.0.json`
- `patches/old/fhn/N16_e0.08_D0.3_I0.6_r300000_dt0.15.json`
- `patches/old/fhn/N16_e0.08_D0.3_I1.0_r300000_dt0.0.json`
- `patches/old/fhn/N16_e0.08_D0.3_I1.0_r80000_dt0.15.json`
- `patches/old/fhn/N16_e0.08_D1.5_I0.4_r80000_dt0.0.json`
- `patches/old/fhn/N16_e0.08_D1.5_I0.6_r300000_dt0.15.json`
- `patches/old/fhn/N16_e0.08_D1.5_I1.0_r300000_dt0.0.json`
- `patches/old/fhn/N16_e0.08_D1.5_I1.0_r80000_dt0.15.json`
- `patches/old/fhn/N16_e0.2_D0.0_I0.4_r80000_dt0.0.json`
- `patches/old/fhn/N16_e0.2_D0.0_I0.6_r300000_dt0.15.json`
- `patches/old/fhn/N16_e0.2_D0.0_I1.0_r300000_dt0.0.json`
- `patches/old/fhn/N16_e0.2_D0.0_I1.0_r80000_dt0.15.json`
- `patches/old/fhn/N16_e0.2_D0.3_I0.4_r80000_dt0.0.json`
- `patches/old/fhn/N16_e0.2_D0.3_I0.6_r300000_dt0.15.json`
- `patches/old/fhn/N16_e0.2_D0.3_I1.0_r300000_dt0.0.json`
- `patches/old/fhn/N16_e0.2_D0.3_I1.0_r80000_dt0.15.json`
- `patches/old/fhn/N16_e0.2_D1.5_I0.4_r80000_dt0.0.json`
- `patches/old/fhn/N16_e0.2_D1.5_I0.6_r300000_dt0.15.json`
- `patches/old/fhn/N16_e0.2_D1.5_I1.0_r300000_dt0.0.json`
- `patches/old/fhn/N16_e0.2_D1.5_I1.0_r80000_dt0.15.json`
- `patches/old/fhn/N1_e0.02_D0.0_I0.6_r300000_dt0.0.json`
- `patches/old/fhn/N1_e0.02_D0.0_I1.0_r80000_dt0.0.json`
- `patches/old/fhn/N1_e0.08_D0.0_I0.6_r300000_dt0.0.json`
- `patches/old/fhn/N1_e0.08_D0.0_I1.0_r80000_dt0.0.json`
- `patches/old/fhn/N1_e0.2_D0.0_I0.6_r300000_dt0.0.json`
- `patches/old/fhn/N1_e0.2_D0.0_I1.0_r80000_dt0.0.json`
- `patches/old/fhn/N4_e0.02_D0.0_I0.4_r80000_dt0.0.json`
- `patches/old/fhn/N4_e0.02_D0.0_I0.6_r300000_dt0.15.json`
- `patches/old/fhn/N4_e0.02_D0.0_I1.0_r300000_dt0.0.json`
- `patches/old/fhn/N4_e0.02_D0.0_I1.0_r80000_dt0.15.json`
- `patches/old/fhn/N4_e0.02_D0.3_I0.4_r80000_dt0.0.json`
- `patches/old/fhn/N4_e0.02_D0.3_I0.6_r300000_dt0.15.json`
- `patches/old/fhn/N4_e0.02_D0.3_I1.0_r300000_dt0.0.json`
- `patches/old/fhn/N4_e0.02_D0.3_I1.0_r80000_dt0.15.json`
- `patches/old/fhn/N4_e0.02_D1.5_I0.4_r80000_dt0.0.json`
- `patches/old/fhn/N4_e0.02_D1.5_I0.6_r300000_dt0.15.json`
- `patches/old/fhn/N4_e0.02_D1.5_I1.0_r300000_dt0.0.json`
- `patches/old/fhn/N4_e0.02_D1.5_I1.0_r80000_dt0.15.json`
- `patches/old/fhn/N4_e0.08_D0.0_I0.4_r80000_dt0.0.json`
- `patches/old/fhn/N4_e0.08_D0.0_I0.6_r300000_dt0.15.json`
- `patches/old/fhn/N4_e0.08_D0.0_I1.0_r300000_dt0.0.json`
- `patches/old/fhn/N4_e0.08_D0.0_I1.0_r80000_dt0.15.json`
- `patches/old/fhn/N4_e0.08_D0.3_I0.4_r80000_dt0.0.json`
- `patches/old/fhn/N4_e0.08_D0.3_I0.6_r300000_dt0.15.json`
- `patches/old/fhn/N4_e0.08_D0.3_I1.0_r300000_dt0.0.json`
- `patches/old/fhn/N4_e0.08_D0.3_I1.0_r80000_dt0.15.json`
- `patches/old/fhn/N4_e0.08_D1.5_I0.4_r80000_dt0.0.json`
- `patches/old/fhn/N4_e0.08_D1.5_I0.6_r300000_dt0.15.json`
- `patches/old/fhn/N4_e0.08_D1.5_I1.0_r300000_dt0.0.json`
- `patches/old/fhn/N4_e0.08_D1.5_I1.0_r80000_dt0.15.json`
- `patches/old/fhn/N4_e0.2_D0.0_I0.4_r80000_dt0.0.json`
- `patches/old/fhn/N4_e0.2_D0.0_I0.6_r300000_dt0.15.json`
- `patches/old/fhn/N4_e0.2_D0.0_I1.0_r300000_dt0.0.json`
- `patches/old/fhn/N4_e0.2_D0.0_I1.0_r80000_dt0.15.json`
- `patches/old/fhn/N4_e0.2_D0.3_I0.4_r80000_dt0.0.json`
- `patches/old/fhn/N4_e0.2_D0.3_I0.6_r300000_dt0.15.json`
- `patches/old/fhn/N4_e0.2_D0.3_I1.0_r300000_dt0.0.json`
- `patches/old/fhn/N4_e0.2_D0.3_I1.0_r80000_dt0.15.json`
- `patches/old/fhn/N4_e0.2_D1.5_I0.4_r80000_dt0.0.json`
- `patches/old/fhn/N4_e0.2_D1.5_I0.6_r300000_dt0.15.json`
- `patches/old/fhn/N4_e0.2_D1.5_I1.0_r300000_dt0.0.json`
- `patches/old/fhn/N4_e0.2_D1.5_I1.0_r80000_dt0.15.json`
- `patches/old/fhn/N64_e0.02_D0.0_I0.4_r80000_dt0.0.json`
- `patches/old/fhn/N64_e0.02_D0.0_I0.6_r300000_dt0.15.json`
- `patches/old/fhn/N64_e0.02_D0.0_I1.0_r300000_dt0.0.json`
- `patches/old/fhn/N64_e0.02_D0.0_I1.0_r80000_dt0.15.json`
- `patches/old/fhn/N64_e0.02_D0.3_I0.4_r80000_dt0.0.json`
- `patches/old/fhn/N64_e0.02_D0.3_I0.6_r300000_dt0.15.json`
- `patches/old/fhn/N64_e0.02_D0.3_I1.0_r300000_dt0.0.json`
- `patches/old/fhn/N64_e0.02_D0.3_I1.0_r80000_dt0.15.json`
- `patches/old/fhn/N64_e0.02_D1.5_I0.4_r80000_dt0.0.json`
- `patches/old/fhn/N64_e0.02_D1.5_I0.6_r300000_dt0.15.json`
- `patches/old/fhn/N64_e0.02_D1.5_I1.0_r300000_dt0.0.json`
- `patches/old/fhn/N64_e0.02_D1.5_I1.0_r80000_dt0.15.json`
- `patches/old/fhn/N64_e0.08_D0.0_I0.4_r80000_dt0.0.json`
- `patches/old/fhn/N64_e0.08_D0.0_I0.6_r300000_dt0.15.json`
- `patches/old/fhn/N64_e0.08_D0.0_I1.0_r300000_dt0.0.json`
- `patches/old/fhn/N64_e0.08_D0.0_I1.0_r80000_dt0.15.json`
- `patches/old/fhn/N64_e0.08_D0.3_I0.4_r80000_dt0.0.json`
- `patches/old/fhn/N64_e0.08_D0.3_I0.6_r300000_dt0.15.json`
- `patches/old/fhn/N64_e0.08_D0.3_I1.0_r300000_dt0.0.json`
- `patches/old/fhn/N64_e0.08_D0.3_I1.0_r80000_dt0.15.json`
- `patches/old/fhn/N64_e0.08_D1.5_I0.4_r80000_dt0.0.json`
- `patches/old/fhn/N64_e0.08_D1.5_I0.6_r300000_dt0.15.json`
- `patches/old/fhn/N64_e0.08_D1.5_I1.0_r300000_dt0.0.json`
- `patches/old/fhn/N64_e0.08_D1.5_I1.0_r80000_dt0.15.json`
- `patches/old/fhn/N64_e0.2_D0.0_I0.4_r80000_dt0.0.json`
- `patches/old/fhn/N64_e0.2_D0.0_I0.6_r300000_dt0.15.json`
- `patches/old/fhn/N64_e0.2_D0.0_I1.0_r300000_dt0.0.json`
- `patches/old/fhn/N64_e0.2_D0.0_I1.0_r80000_dt0.15.json`
- `patches/old/fhn/N64_e0.2_D0.3_I0.4_r80000_dt0.0.json`
- `patches/old/fhn/N64_e0.2_D0.3_I0.6_r300000_dt0.15.json`
- `patches/old/fhn/N64_e0.2_D0.3_I1.0_r300000_dt0.0.json`
- `patches/old/fhn/N64_e0.2_D0.3_I1.0_r80000_dt0.15.json`
- `patches/old/fhn/N64_e0.2_D1.5_I0.4_r80000_dt0.0.json`
- `patches/old/fhn/N64_e0.2_D1.5_I0.6_r300000_dt0.15.json`
- `patches/old/fhn/N64_e0.2_D1.5_I1.0_r300000_dt0.0.json`
- `patches/old/fhn/N64_e0.2_D1.5_I1.0_r80000_dt0.15.json`
- `patches/old/fhn_sub_bass.json`
- `patches/old/sort_bubble_cursor.json`
- `patches/pending/gs_chaotic.json`
- `patches/pending/gs_spots.json`
- `patches/pending/gs_waves.json`
- `patches/sweep/F0.010_k0.030_r100000_N128.json`
- `patches/sweep/F0.010_k0.030_r300000_N32.json`
- `patches/sweep/F0.010_k0.045_r100000_N128.json`
- `patches/sweep/F0.010_k0.045_r300000_N32.json`
- `patches/sweep/F0.010_k0.055_r100000_N128.json`
- `patches/sweep/F0.010_k0.055_r300000_N32.json`
- `patches/sweep/F0.010_k0.065_r100000_N128.json`
- `patches/sweep/F0.010_k0.065_r300000_N32.json`
- `patches/sweep/F0.022_k0.030_r100000_N128.json`
- `patches/sweep/F0.022_k0.030_r300000_N32.json`
- `patches/sweep/F0.022_k0.045_r100000_N128.json`
- `patches/sweep/F0.022_k0.045_r300000_N32.json`
- `patches/sweep/F0.022_k0.055_r100000_N128.json`
- `patches/sweep/F0.022_k0.055_r300000_N32.json`
- `patches/sweep/F0.022_k0.065_r100000_N128.json`
- `patches/sweep/F0.022_k0.065_r300000_N32.json`
- `patches/sweep/F0.040_k0.030_r100000_N128.json`
- `patches/sweep/F0.040_k0.030_r300000_N32.json`
- `patches/sweep/F0.040_k0.045_r100000_N128.json`
- `patches/sweep/F0.040_k0.045_r300000_N32.json`
- `patches/sweep/F0.040_k0.055_r100000_N128.json`
- `patches/sweep/F0.040_k0.055_r300000_N32.json`
- `patches/sweep/F0.040_k0.065_r100000_N128.json`
- `patches/sweep/F0.040_k0.065_r300000_N32.json`
- `patches/sweep/F0.060_k0.030_r100000_N128.json`
- `patches/sweep/F0.060_k0.030_r300000_N32.json`
- `patches/sweep/F0.060_k0.045_r100000_N128.json`
- `patches/sweep/F0.060_k0.045_r300000_N32.json`
- `patches/sweep/F0.060_k0.055_r100000_N128.json`
- `patches/sweep/F0.060_k0.055_r300000_N32.json`
- `patches/sweep/F0.060_k0.065_r100000_N128.json`
- `patches/sweep/F0.060_k0.065_r300000_N32.json`
- `patches/sweep/feedback_curves1/manifest.json`
- `patches/sweep/feedback_curves4_criticals.json`
- `patches/sweep/sweep_F0.010_k0.030_r200000_N256.json`
- `patches/sweep/sweep_F0.010_k0.030_r50000_N64.json`
- `patches/sweep/sweep_F0.010_k0.050_r200000_N256.json`
- `patches/sweep/sweep_F0.010_k0.050_r50000_N64.json`

## Structural scans

Over the 1,615 patches of the eight set files, from the JSON (a scratch script; the rules:
an edge is any `{"ref": id}` or `{"tap": id}` under a node's params, nested graphs
included; a cone follows both kinds).

- **(a) Taps on a stateless view** — target type `NameGate`, or an id beginning `__perf`: **0**.
  A tap on a view would read the view's own memo under reading B; no stored patch does it.
- **(b) A tap inside a table fill's excitation** — `WavetableSource` / `HybridKSSource` with a
  `{"ref"}` `inputSource` whose cone contains a tap edge: **0 of 62** ref-excited fills.
  `step()` moves the previous value the same way `next()` does, but the limit is unexercised.
- **(c) A tap inside a push-binding chain** — a `dynamicPins` `{"ref"}` whose cone contains a
  tap edge: **0 of 134** dynamicPins refs. A Setup-time `step()` of a chain never crosses a tap.
- **(d) The listed families and DelayLine `compensate`** (patches / compensate on / off): every
  listed patch has a DelayLine. `audition/pierce1d_1` 6 / 6 / 0; `audition/trombone1` 4 / 4 / 0;
  `old/nlbore_brass1` 6 / 3 / 3; `pending/brass` 1 / 1 / 0; `sweep/nlbore_probe1` 7 / 3 / 4;
  `sweep/onemass_lip1` 7 / 7 / 0; `sweep/stk_blowhole_port` 3 / 0 / 3; `sweep/trombone1` 2 / 2 / 0;
  `sweep/trombone2` 3 / 3 / 0; `sweep/trombone3` 4 / 4 / 0; `sweep/trombone4` 2 / 2 / 0;
  (`sweep/stk_bowed_ext1` 1 / 0 / 1 and `sweep/stk_bowed_ext2` 1 / 0 / 1 are the two above that
  no longer count). The `compensate` setting is irrelevant to the change — corrected after
  the refutation of report R5 (C1): the same-tick read in these families is
  `NL_curve ← tap(NL_in)` driving `NL_delay.ratio`, a control read (the closing taps on
  `Bore` are mid-compute, where readings A and B agree); in `pierce1d_1` it is
  `SignCurve ← tap(Delay_line)` driving `Damp_lpf.cutoffFreq` (the loop walk finds no
  closing tap through input pins, so `compensate` is inert there); in `stk_blowhole_port`
  it is `D1In ← tap(D2)`, a signal-path tap between two tail delay lines.

The counter sits at the tap's `compute()` position, not the delivery position: a same-tick
delivery created by a later pin of the same consumer's sweep (the source computed after the
tap was pulled but before the post-sweep `current()` read) would count 0. The A/B over the
same sample is the proof that none exists there. The sample is 1,615 patches (`sweep` and
`old` every third); on disk the class is 116 patches (101 with `tap(NL_in)`, 6 `pierce1d_1`,
9 `stk_blowhole_port`; `pierce1d_2`'s four tap `Pierce` mid-compute only and are not in it).

