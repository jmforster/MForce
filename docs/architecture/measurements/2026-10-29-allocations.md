# Allocations between note-on and the last sample (for checkpoint 2.6)

Measured 2026-10-29/30 at `94e9a90` plus the gate's own commit (the engine at
checkpoint 2.5's seal, `1481d15`, with the loader's score delivery extracted
and the streaming voice's tail list made a view), with
`build/tools/alloc_gate/Release/alloc_gate.exe` — the allocation probe armed
around the live path: for each note of a patch's embedded score (or one C4
when it has none) `acquire_voice`, `prepare_voice_at` (the note's Setup,
then `start` on the voice and its tails), one pull per sample for the note's
life (`tick`, `next`, the tails), `release_voice`; for a mixer-mode graph,
each channel source's `render` (the loader's adapters: `start`, then `next`
per sample). Everything before the arm — load, prepare, the slot table's
first sizing — may allocate by contract.

```
alloc_gate.exe patches/library patches/baselines          > renders/scratch/alloc26_baseline_stacks.txt
alloc_gate.exe --list patches/library patches/baselines   > renders/scratch/alloc26_baseline.txt
alloc_gate.exe --list patches/audition patches/pending    > renders/scratch/alloc26_queues.txt
python tools/survey_table_frequencies.py patches/library patches/baselines patches/audition patches/pending patches/sweep
                                                          > renders/scratch/alloc26_table_frequencies.txt
```

## The gate's set: library and baselines

132 patches, 97 allocate, 0 fail to load. The run with stacks took 230 s on
this machine (191 s on the first arming), well over the 60 seconds the plan
set for a ctest; the gate joins `gates.py`'s slow list (plan ruling 8).

By the first engine frame on the first allocation's stack:

| family (first engine frame) | patches | allocations per patch | where |
|---|---|---|---|
| `FullPartials::init_arrays` | 68 | 17 | the 48 `library/voice` vowels and words, `FormantSequence1`, `fadd_band_test`, `fadd_formant_test`, `prof_additive/add_032p`..`add_200p` — the partial arrays sized at the first `partials_start` |
| `Partials::partials_start` | 7 | 11–18 | `strings/cello_full_range`, `strings/viola_default`, `winds/clarinet1`, `fadd_explicit_test`, `fadd_sequence_test`, `prof_additive/add_008p`, `add_016p` — the per-partial state arrays (`assign` at the first start) |
| `WavetableSource::fill_table` | 11 | 1–5, and 1,025 | the six `algev_test_*`, `bend_mordent_test_pluck`, `bow_evolution_discovery`, `delay_test`, `reed_test`, `percussion/hi_hat1` — the table sized at the first sample; `algev_test_he` allocates on every evolution step (HistogramEqualize) |
| `SegmentSource::update_segments` | 8 | 1–3 | `feedback/loop_amp_release`, `loop_bowed`, `loop_keytrack`, `repeating_segment_test`, `keys/acoustic_piano/piano_seg`, `keys/electric_piano/ep_bright`, `ep_dark`, `ep_std` — the segment list rebuilt in `compute()` |
| `HybridKSSource::compute_ks` | 1 | 4 | `hybrid_breath_horn_test` — the table and the partial arrays at the first sample |
| `PhasedValueSource::on_start` | 1 | 2 | `phased_test` — the stage counters and boundaries at the first start |
| `MultiplexSource::set_clone_param` | 1 | 1 | `MPXTest3` — a push delivery at note-on (`apply_note_bindings` fanning a value into the clones) |

Every patch not in the table allocated nothing (35 of 132), among them
`winds/oboe1`, the whole `keys/acoustic_piano` family but `piano_seg`, the
`brass`, the `feedback` loops that carry no SegmentSource, and every
mixer-mode graph whose channel sources do not carry one of the families
above.

## Against the 2.2 report's work list

Confirmed, in the gate's set: Partials (`partials_start`, `init_arrays`,
`init_detune_values`), WavetableSource (`fill_table`) and its evolutions
(HistogramEqualize certainly — `algev_test_he`'s 1,025; the Target, Bowed,
Brass, ReactionDiffusion, CellularAutomaton and Bezier buffers are the first
stacks of `algev_test_sort/bitrot`, `bow_evolution_discovery`, `reed_test`,
`algev_test_rd`, `algev_test_ca`, `algev_test_bezier` once the table itself
is reserved), HybridKSSource (`fill_table`, `extract_partials`),
PhasedValueSource (`on_start`), SegmentSource (`update_segments`).

On the 2.2 list, not exercised by any patch in the set:
`BasicAdditiveSource::wave()` (no library or baselines patch carries the
node; its fix is pinned by its `engine_tests` case alone) and
`AdditiveSource2`'s note arrays (within the capacity reserved at prepare —
`as2_bright_attack_test` allocates nothing).

New, not on the 2.2 list: `MultiplexSource::set_clone_param` at note-on
(Task 7).

Host allocations the gate found and the arming now excludes or the engine
fixed, none a node's:
- `PitchedInstrument::play_note` allocates the note's pre-render buffer
  (`instrument.h:678`, three allocations per note): the offline host's model
  (the CLI pre-renders every note into a buffer and mixes them), not the
  live path — the gate drives the live path instead; listed for Part 5.
- `StereoMixer::render` allocates a scratch `mono` vector per call: the
  offline mix (the UI streams its own mixer in the audio callback and never
  calls it); the gate arms around the channel sources, not the mix; listed
  for Part 5.
- `PitchedInstrument::prepare_voice_at` copied the voice's tail list into
  the returned `StreamingVoice` — one allocation at every live note-on on a
  patch with tap-only tails (`double_advance_bug`, `double_advance_ok`,
  `feedback/loop_tap_starved`); the list is a `std::span` over the pool
  voice's now, and the three patches are clean.

## The queues, for information

`patches/audition` and `patches/pending`: 752 patches, 749 load, 398
allocate (the three that do not load are pending's known three). Not the
gate's set; the families are the same as above by the first frames sampled,
and a reader with a queue patch in hand runs `alloc_gate.exe <patch>` for its
count and first stack.

## The table floor

`tools/survey_table_frequencies.py`: every `WavetableSource` and
`HybridKSSource` node's `frequency` in every folder.

| folder | table nodes | lowest constant frequency |
|---|---|---|
| library | 1 | 6000 Hz (`percussion/hi_hat1`) |
| baselines | 11 | 220 Hz (the six `algev_test_*`); one wired, `bow_evolution_discovery`'s to a Vibrato around its base |
| audition | 49 | 220 Hz |
| pending | 7 | 220 Hz |
| sweep | 0 | — |

Instrument patches take their frequency from the note; the UI keyboard's
lowest key is A0, 27.5 Hz. A floor of 20 Hz — DelayLine's since checkpoint
2.2 (`sample_rate() / 20 + 4`) — is crossed by nothing in use; REVIEW R7
item (3) decides whether it stands.
