# Sharing measurements (for the per-tick memo and the tap rule)

Taken 2026-10-16 at engine commit `009b983` over the 80 null-gate manifest patches, with `MFORCE_TRACE_SHARING=1` (`python tools/measure_sharing.py`).
A *lag read* is a wrapped consumer reading a shared source before its primary consumer advanced it that sample (it reads the previous sample's value). A *same-tick tap* is a guarded tap reading a source that had already advanced that sample.

## 13 patches with wiring-order lag reads (expected to change under the memo)

- `patches/baselines/double_advance_bug.json`: 9183825 lag reads
- `patches/baselines/double_advance_ok.json`: 8643600 lag reads
- `patches/baselines/fadd_explicit_test.json`: 1 lag reads
- `patches/baselines/fadd_sequence_test.json`: 1 lag reads
- `patches/baselines/feedback/loop_hysteresis_bow.json`: 576870 lag reads
- `patches/library/keys/acoustic_piano/piano_bright.json`: 8 lag reads
- `patches/library/keys/acoustic_piano/piano_dark.json`: 8 lag reads
- `patches/library/keys/acoustic_piano/piano_default.json`: 8 lag reads
- `patches/library/keys/acoustic_piano/piano_seg.json`: 6 lag reads
- `patches/library/strings/viola_default.json`: 2 lag reads
- `patches/library/winds/bassoon1.json`: 584047 lag reads
- `patches/library/winds/oboe1.json`: 579198 lag reads
- `patches/library/winds/oboe2.json`: 596060 lag reads

## 0 patches with same-tick tap reads (expected to change under the tap rule)

- none

## 0 patches that produced no trace line

- none
