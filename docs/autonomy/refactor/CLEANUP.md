# Refactor lane — post-refactor suggested cleanup (master list)

Matt, 2026-11-02: easily-tweaked items (colours, menu dividers, labels,
strips, any reversible presentation or grouping call) do not go through
REVIEW for the rest of the refactor. Where the cause is understood and the
implication is not far-reaching, the session decides, the report's section 6
records the decision, and the item lands here as a *suggestion* for after
Part 8. Nothing here is scheduled; `BACKLOG.md` row 8 (workaround cleanup
after Part 8) is where this list is read. The reports' section 7s remain the
full record of debt; this file is the short list of what is worth a look.

| # | Suggestion | Source | Decided |
|---|---|---|---|
| 1 | Node colours: seven types changed colour when colour began to follow the family (HybridKS teal→tan, PierceFilter lavender→teal, Distortion and Shaper teal→lavender, Vibrato teal→pale gold, Segment reddish-orange→pale gold, Wormhole teal→grey). If any reads wrong in use, one `Theme` entry (the family's colour) or one family word on the registration moves it. | R9 item 1 | stands, 2026-11-02 |
| 2 | The ten dividers the hand-written menus drew inside a family are gone; each family is one flat list. If grouping inside a family is wanted, it is a fourth field on `TypeTag` (a group word) that the generated menu draws as a separator, not a hand list. | R9 item 2 | stands, 2026-11-02 |
| 3 | The Vibrato and Segment waveform strips draw in the envelope trace's colour and hide with the Envelopes toggle, being the Envelope family's. If Segment should read as audio, that is a family exception the registry would carry (a strip-kind field), or Segment moves family. | R9 item 5 | stands, 2026-11-02 |
| 4 | The editor's part strips (Partials holders, formants, evolutions) capture nothing where they drew a flat zero line; the spectra strips are unaffected. Part 7's node model decides whether a part has a strip at all. | R8 item 3 | stands, 2026-11-02 |
| 5 | A fault count on the UI's stamp line beside the transport status, so a render with faults is visible without opening the status. | R6 item 2 | shape stands, 2026-11-02 |
| 6 | `kFamilyOrder` restates the enum's declaration order; the enum's own order could serve. | 3.2 §7 | — |
| 7 | `--dump-descriptors` emits the family's display name ("Oscillators") under `family`; a stable identifier would age better. No consumer today. | 3.2 §7 | — |
| 8 | `Shelf` is a presentation fact in the engine's registry; revisit when the shelf has a second reader or Part 7 gives the editor a menu model. | 3.2 §7 | — |
| 9 | `types_in` returns a fresh vector per call and the menus call it per family per frame while a menu is open; a cached menu model is Part 7's. | 3.2 §7 | — |
| 10 | `theme.h` pulls the whole registry header (and `json.hpp`) for `Family` and `kFamilyCount`; a small `core/family.h` would let the theme depend on the classification alone. | 3.2 §7 | — |
| 11 | Part numbers in comments from the 2.x checkpoints (`dsp_value_source.h` twice, `mixer.h`, `hybrid_ks_source.cpp`, `wavetable_source.cpp`); the meter's changelog pattern could learn "Part" followed by a digit. | 3.2 §7 | — |
| 12 | `engine/include/mforce/core/denormals.h` is Windows-1252 encoded (an em dash at byte 200); every other header is UTF-8. A one-byte fix. | 3.2 §7 | — |
| 13 | The registrations file's banners name a family beside tags that already name it. Cheap, and a reader wants them; drop only if they drift. | 3.2 §7 | — |
| 14 | The quick block (Curve, Envelope, Var, Range) is hand-written twice in the editor's menus. | 3.2 ruling 16 | — |
| 15 | `dsp::TuningAllpass` is a first-order allpass that one of its four owners (EKS) uses as a dispersion filter; `FirstOrderAllpass` would name the thing rather than its first use. Part 3 §4's table chose the name; one rename. | 3.3 review F4 | stands, 2026-11-02 |
| 16 | Twenty-three π literals remain outside `core/dsp` after `kPi`; each is a one-line change once checked to be the same float. | 3.3 §7 | — |
| 17 | The three part interfaces carry two conventions: `IPartials` and `IFormant` are pure and I-prefixed, `WaveEvolution` is an abstract `Component` base that owns its members' pin lifecycle. Whether the two take the same base, and what the three are called, when 3.8 opens `Partials`. | 3.4 rulings 1, 15 | stands, 2026-11-03 |
| 18 | `TargetEvolution`'s declared ranges (`targetLength` 16..8192, `morphRate` 0.0001..0.1, `holdCycles` 0..100, `decayFactor` 0.9..1.0) and its derived label "Target Evolution" are a session call; the string form had no ranges. | 3.4 ruling 7 | stands, 2026-11-03 |
| 19 | Four evolutions' `on_setting_changed` hooks restate bounds their declarations carry (lower-only in three); if Part 4's loader clamps every setting to its declared range, the hooks go. | 3.4 ruling 4 | — |
| 20 | The wavetable's two first-sample blocks (`compute_raw`, `compute_interpolated`) each read the slot and call `adjust`/`shape_excitation`; one block (3.3 ruling 17; 3.6). | 3.4 §7 | — |
| 21 | A Pluck or Averaging setting edited mid-stream in the editor no longer restarts the evolution's draw stream (the wrapper's rebuild did). One `rng_.reanchor()` in `on_setting_changed` restores the restart if it is ever wanted. | 3.4 ruling 3 | stands, 2026-11-03 |
| 22 | `sustainLevel` reads the four-slot view (0) on a two- or three-stage Envelope (`as`, `asr`, `ads`) where the deleted classes reported the level they stored; the knob was already inert on such a shape. A per-shape sustain slot (the expand stage's value) would read truthfully. | 3.5 ruling 1 | stands, 2026-11-04 |
| 23 | The `adsr` form's `release` default is 0.0 where the deleted `ADSREnvelope`'s was 0.1; every `adsr` node in the active folders carries the key (the migration wrote 0.1 where a class node lacked it), so the default could move to 0.1 after Part 8 once `old/` and `sweep/` are surveyed for a bare case. | 3.5 ruling 3 | — |
| 24 | Six `algev_test_*` baselines carry `release` and `releaseMax` on an `ar`-form Envelope that no form ever read (the AR release is the expand); the global allowlist admits them. A per-form key check belongs to the lint. | 3.5 ruling 13 | — |
| 25 | The preset form's curves are ints 0..3 (what the class settings were, what 113 nodes carry); the stages form's are strings. One dialect, with backlog 36. | 3.5 ruling 4 | — |
| 26 | The editor's new-Envelope default (`make_adsr(0.05, 0.1, 0.7, 0.2)`) differs from the registry's `Envelope` factory (`make_ar(0.2)`); one of the two. | 3.5 ruling 9 | — |
| 27 | `retired_type_hint`'s seven names and `tools/migrate_deleted_types.py`'s `RULES` are one list in two languages; a lint selftest could compare them, or the tool could ask `--dump-descriptors` which names are gone. | 3.5 ruling 8 | — |
| 28 | `tools/engine_tests/main.cpp` casts `dynamic_cast<PitchedInstrument*>` nineteen times (two new); one helper. | 3.5 §7 | — |
| 29 | The loader's Envelope stages branch reads `stage_accuracy` and `ramp_accuracy` itself (with a clamp) before `wire_params_generic`'s settings pass reads them again (with the node's own clamp); one read. | 3.5 §7 | — |
| 30 | `Morph_env` (an `as` Envelope) in `patches/library/winds/bassoon1.json`, `oboe2.json` and `patches/baselines/feedback/loop_hysteresis_bow.json` is dead: nothing reads it as a signal, and the render is the same with it changed or gone (the 3.5 re-review's N1). Delete the node, or wire it to what it was meant to drive. | 3.5 re-review N1 | — |
| 31 | `duplicate_blocks` rose 146→163 (+17) at 3.6, against the plan's "must not rise". The rise is the one-type-per-file preamble now replicated per split file (`#pragma once` / shared include set / `namespace` / banner / `struct X final : Base {` + first pins) and sibling-struct similarity already present inside the umbrella headers, now visible cross-file (notably `bw_lowpass`↔`bw_highpass`); no new authored logic (reviewer-confirmed, every flagged body byte-identical to the pre-refactor text). It is the arithmetic cost of the "one type per file" rule; the meter's duplicate_blocks is informational for the node folders from here on, not a ratchet. | 3.6 ruling (section 6) | accepted, 2026-11-05 |
| 32 | `bw_lowpass_filter`, `bw_highpass_filter` and `bw_bandpass_filter` are thin wrappers over the shared `dsp::ButterworthSection` differing only by `Kind::` and `type_name`; a shared Butterworth base (or a single parameterised filter) would remove the sibling duplication CLEANUP 31 names. New design, not a move, so out of a move-only checkpoint; weigh with Part 8's "are we growing as much compensating code as we lose" pass. | 3.6 section 7 | — |
| 33 | The live-edit class: a structural setting changed in the editor after `prepare` and read live on the audio path, where the buffer it sizes was set at `prepare` — `HistogramEqualizeEvolution` sizes `cdf_`/`counts_` in `on_prepare` and reads `bins.value` per sample, so a grow mid-cycle allocates (or reads past `cdf_` for up to one cycle); `HybridKSSource` resizes its partial arrays in `on_setting_changed` when prepared (control side, under the editor's lock). One rule closes it: the prepared size is the audio path's truth (a node keeps its prepared copy of a structural setting), or the editor re-prepares the node after a structural edit — Part 7's call, where 2.6 §7 put "the UI's live-edit case". Whether the editor re-prepares today is unverified. | 3.7 survey 2026-11-06; 2.6 §7 | booked to Part 7 |
| 34 | The allocation gate's blind spots: notes play one after another (no mid-note setting edit, no overlapping voices of one patch), the held note's `gate_release` path has one `engine_tests` case and no per-patch gate case, and `StereoMixer::render`'s own scratch is the offline host's and unarmed. Each is a gate case to add when its path is a checkpoint's subject. | 3.7 survey 2026-11-06 | — |
| 35 | `WavetableSource::compute_raw` and `compute_interpolated` each carry the same first-sample block (the fill, `adjust`, `shape_excitation`, the tuning allpass); folding them is a structure change inside the node that 3.3 ruling 17 left for 3.6/3.7, neither of which touched it. | 3.3 ruling 17 | — |

## Byte-identity preservations to retire after Part 8

Guards that exist only so a render stays byte-identical during the refactor
(`BACKLOG.md` row 8: "re-ask whether we are growing as much compensating
code as we lose"). Each report's section 7 lists its own; the ones named by a
verdict or a 3.3+ ruling are collected here as they arise.

| # | Preservation | Where | Source |
|---|---|---|---|
| P1 | `BiquadSource` in resonance mode writes the derived `a1`/`a2` into its own settings (editor-visible, saved with the patch); kept so the editor shows what it showed. | `source/filter/biquad_source.h` | 3.3 ruling |
| P2 | `ButterworthSection::process` keeps the deleted FIR/IIR loops' exact operation order, including the `0.0f +` start of the accumulator, for the sign of a zero product. | `core/dsp/butterworth_section.h` | 3.3 ruling |
