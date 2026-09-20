# Note transitions v1 — phrase delivery, NameGate, trigger envelopes

2026-09-19. Spec `docs/superpowers/specs/2026-09-19-note-transitions-design.md`
(brainstormed with Matt same day, spec approved before build), plan
`docs/superpowers/plans/2026-09-19-note-transitions.md`. Built inline
(Fable), tasks 1–10 in plan order, TDD throughout. Engine code: yes —
the first engine round of the articulation thread, all of it dormant
until asked for.

## What shipped, by layer

- **Parser** (`parse_util.h`): `|` phrase-boundary token. Grouping
  activates only when a bar is present; a bar-free passage is
  every-note-its-own-phrase = bit-compat. Rests end phrases.
- **Perform state** (`perform_source.h`): `NoteState.transitionId` (0 =
  none; 1.. = instrument vocabulary in declaration order),
  `PerformOut::Field::Transition`, `set_note(..., transitionId)`.
- **Delivery** (`instrument.h`): `PhraseNote` + `play_phrase` — the
  phrase acquires the voice, prepares the graph at PHRASE length (the
  one-breath model: envelopes span the phrase), and in-phrase notes
  re-drive the living voice: `set_note` + non-setting push bindings +
  trigger firing, NO prepare. `play_note` is now a one-line delegation
  to a one-note phrase — one render loop in the codebase.
  Setting-type (isSetting) bindings are SKIPPED mid-phrase by decision
  (set_setting rebuilds state at prepare; spec verify-flag (b)) — they
  hold phrase-start values. Phrase gain is note 1's velocity (per-voice
  mix gain can't move mid-buffer without a zipper); mid-phrase velocity
  still reaches the graph through the velocity pin.
- **Envelope** (`envelope.h`): `trigger` param (Setup-sampled, never
  pulled in next()) + `retrigger()` — re-enters stage 0 anchored to the
  current output via the existing gate-anchor machinery, click-free by
  test.
- **NameGate** (`name_gate.h`, registered): stateless match node; the
  authored name string resolves against `instrument.transitions[]` at
  load; unresolved names warn once and never fire. Untaught instruments
  intern all names to 0 silently (degradation contract).
- **Loader**: `transitions` vocabulary; NameGate branch; per-voice
  trigger-binding collection; score-block `"phrase":"cont"` grouping
  with the Performer emission rule (first breath, rest tongue, explicit
  `"transition"` overrides) at the spec-§3 seam; articulation/ornament
  on phrase notes throws loudly.
- **UI**: `transition` output pin on the Note node; `trigger` pin on
  Envelope (free via descriptors); NameGate name editing/display/save/
  load/clipboard riding `paramName` (Parameter-node precedent);
  transport passage grouping + emission; `generate_unified` phrases;
  `--gencheck` mirrors the loader's score reading.
- **Taught patches + queue** (`gen_articulation1.py`): trombone_attempt4
  and oboe1 copies taught `tongue` (4 nodes each, originals untouched);
  OTJ phrased/flat pairs + repeated-note hold cells →
  `renders/dsp/audition/articulation1/` = REVIEW 73.

## Gates, all green, with numbers

1. **Dormancy null**: null gate 79/79 byte-identical after EVERY task,
   including the play_note→play_phrase delegation refactor.
2. **Gate-extension correctness**: two-note same-pitch phrase ==
   one long note, byte-identical — engine_tests (in-memory) AND at CLI
   level via `patches/baselines/phrase_smoke_{flat,phrased}.json`
   (sha-identical WAVs). These two baselines are NEW and not yet in the
   frozen manifest — add at the next deliberate refreeze.
3. **Mid-voice re-drive**: C4→G4 phrase, second half zero-crossing
   ratio 1.498 (392/261.6 = 1.498); gencheck vs CLI on the phrased
   baseline and on a taught+phrased score both at scale 1.000000, worst
   residual 1 LSB of 16-bit.
4. **65/71 gremlin watch**: the end-to-end trigger test measures the
   gesture at its authored rate (dip reaches min inside its 10 ms), and
   null gate covers the advance order at rest. No 2x symptom.
5. **Retrigger continuity**: no step at restart (|Δ| < 0.02 at the
   sample), dip to 0.2 ± 0.02 on time, expand holds 1.0 ± 1e-3 to the
   prepared end (the re-entered expand extends past the layout —
   masked by the amp envelope's own silence, same as today).
6. **Roundtrip**: taught patch survives UI --roundtrip with NameGate
   name, trigger wire, vocabulary and rewiring intact. Corpus run over
   baselines+library: 7 render diffs + 1 id change, ALL reproduced
   byte-for-byte identically by a pre-change (46f4564) CLI on identical
   roundtripped JSON — pre-existing lossiness on loop/bug-repro
   baselines (double_advance pair, loop_tap pair, loop_root pair,
   wiring_setting; wiring_smoke id loss already on record), first
   surfaced because this corpus had never been run through the harness.
   Filed as backlog 79.
7. **Ears**: REVIEW 73. Machine checks inside the generator: oboe
   inter-note troughs 8 (flat) → 4 (phrased); trombone tongue dent
   measurable at every in-phrase boundary (mean ratio 0.87). Trombone
   trough counting is blind (ring-out bridges flat gaps too) — the
   generator's gate is instrument-aware and says so.

## Decisions made in-flight (all inside spec latitude)

- `|` grouping activates per-string (any bar present), so bar-free
  strings are untouched — the spec's compat sentence made concrete.
- The tongue consonant closes the breath fully for 22 ms: a partial
  pinch (dip to 0.15 for 12 ms) moved the trombone's output by only
  ~10% — the bore's stored energy rides through. Recorded in the
  generator; the shape is Matt's knob now.
- engine_tests grew from 474 to 494 checks (parser, transition field,
  phrase identity/retune, retrigger, NameGate, end-to-end trigger).

## Deferred, on the record

- Live keyboard legato (overlap = phrase) — designed in the spec §4,
  needs backlog 53's pool/steal work; the delivery machinery is ready.
- `shape` slot (honk/squeak) — mechanism ready, notation/emission is
  its own brainstorm.
- isSetting push bindings mid-phrase — v1 exclusion, revisit when a
  patch actually needs per-note setting changes inside a phrase.
- Multiplex subgraph rebuilds don't carry the vocabulary — a NameGate
  inside a Multiplex warns loudly instead of resolving. Acceptable
  until a multiplexed patch wants transitions.
