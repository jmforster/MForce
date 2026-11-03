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

## Byte-identity preservations to retire after Part 8

Guards that exist only so a render stays byte-identical during the refactor
(`BACKLOG.md` row 8: "re-ask whether we are growing as much compensating
code as we lose"). Each report's section 7 lists its own; the ones named by a
verdict or a 3.3+ ruling are collected here as they arise.

| # | Preservation | Where | Source |
|---|---|---|---|
| P1 | `BiquadSource` in resonance mode writes the derived `a1`/`a2` into its own settings (editor-visible, saved with the patch); kept so the editor shows what it showed. | `source/filter/biquad_source.h` | 3.3 ruling |
| P2 | `ButterworthSection::process` keeps the deleted FIR/IIR loops' exact operation order, including the `0.0f +` start of the accumulator, for the sign of a zero product. | `core/dsp/butterworth_section.h` | 3.3 ruling |
