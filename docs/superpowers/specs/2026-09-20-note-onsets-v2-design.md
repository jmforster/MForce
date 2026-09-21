# Note Onsets v2 — per-note phrasing, hold, and the retirement of the phrase object

2026-09-20. Brainstormed with Matt (interactive, two sessions; he drove the
second). SUPERSEDES `2026-09-19-note-transitions-design.md` — v1's delivery
and duration semantics are replaced; its notation, name-matching and
trigger machinery survive. v1's divergence from its own spec is recorded in
§9 as history, deliberately.

**The model, in one paragraph (Matt's oboeist):** a player never looks
ahead. The SCORE looked ahead when it drew the slur; the player just reads
each note's markings: this note starts a certain way, and the arc over it
says keep blowing past its end (or doesn't). So every note is atomic and
carries two facts — **`onset`** (how it begins: `breath` | `tongue` |
`slur`) and **`hold`** (does the breath continue past its end: bool). The
phrase exists only in notation; no phrase object ever reaches Render-land.

---

## 1. The two per-note facts (Perform tier)

- **`onset`** — the way IN. A name from the instrument's declared
  vocabulary. Renamed from v1's "transition" (there is no *between*
  anymore; it is a property of this note's beginning; "attack" was
  rejected for colliding with envelope Attack stages, "onset" is the
  standard audio term and was unused).
- **`hold`** — the way OUT. `true` = the breath/bow/excitation continues
  past this note's end; `false` (default) = it stops there.

Tier language discipline: Perform-land says *hold*. Only Render-land
internals speak of gates.

**The carrier: `PerformedNote` (Matt's name), the Performance→Realization
boundary object.** The performer's per-note decisions already crossed the
boundary as loose `play_note` arguments with the PitchCurve dangling at
the end; they consolidate:

```cpp
struct PerformedNote {
  float noteNumber;
  float velocity;
  float duration;          // seconds
  float onsetId{0};              // interned; 0 = none
  bool  hold{false};
  const PitchCurve* curve{nullptr};
};
// Instrument::play_note(const PerformedNote&, float startTime)
```

One struct, one home for every future per-note performance product
(per-note glideMs when onset grows its object form, ornament curves,
performer-preference resolutions). Call sites are few (NotePerformer,
score loader, UI generate/live, chords/melody harnesses): clean sweep,
no compat overload. Tier discipline made explicit: `music::Note`
(Compose) gains NOTHING — onset/hold are Interpretation products, same
category as PitchCurve, and live only on PerformedNote. When real
phrase notation arrives on the Compose side, the mark belongs on the
existing `music::Phrase` structure, and the Performer derives per-note
onset/hold from it. `NoteState` remains the render-side mirror the
graph reads.

**Onset's future object form (so v2's scalar is not a dead end):** v2
ships `onset` as a scalar name; glide time is the instrument-block
`glideMs`. When per-note expressiveness is wanted (portamento time,
tongue weight), the JSON grows the standard shorthand/object dual:
`"onset": "slur"` ≡ `"onset": {"name": "slur"}`, expressive form
`{"name": "slur", "glideMs": 80}` — backward compatible, no migration.
Gesture DEPTH may never need it: it is already wireable patch-side
(velocity pin → gesture envelope maxValue), so the object form is
reserved for genuinely per-note performance choices.

**Continuation is NOT a fact.** Whether a note continues a living voice is
derived from engine state: if the line's voice has an open gate, the note
continues it; otherwise it gets a fresh voice. This is the same test the
live keyboard's key-overlap makes, against the same state — offline and
live are one mechanism, and zero lookahead exists anywhere (spec §4 of v1
finally implemented honestly).

## 2. The `sustaining` declaration

Instrument block: `"sustaining": true`. Default **false**.

Phrasing (hold, onset gestures, glide) applies only to sustaining
instruments — a fact about the instrument only its author knows. It is
DECLARED, never inferred (not from envelope shapes — piano's long decay
could carry an expand stage and fool any shape test — and not from the
UI's derived `gateable`, which merely means "the envelope walk succeeded"
and is true of piano too).

**The piano invariant, by construction:** the Performer consults
`sustaining` when stamping. A non-sustaining instrument never receives
`hold: true` or continuation onsets, so a phrase-marked passage on
piano_default emits ordinary independent notes — byte-identical to the
unmarked passage, provable, with no delivery special case. UX duty: when
a passage carries `|` marks and the loaded patch is not declared
sustaining, the transport status line says so; silence there would be a
fresh mystery.

`sustaining` is a natural InstrumentClass property later (backlog 78);
the per-patch key is its seed. Adjacent, NOT this round: the live path's
derived `gateable` check could defer to the declaration too (backlog 53's
round).

## 3. Emission rules (the Performer seam, v2)

Within a `|`-marked phrase on a sustaining instrument:

- every note except the last: `hold: true`
- first note: `onset: "breath"`
- later notes: `onset: "tongue"` on repeated pitch, `onset: "slur"` on
  pitch change (a repeated pitch physically cannot be slurred — the
  reference player articulates every repeat; measured 2026-09-20:
  A|A −22.6 dB/50 ms, A|A −19.5/40, F#|F# −10.8/40, D|D −6.8 — depth
  tracks musical weight and becomes an expressive input later)
- explicit per-note keys override the defaults, as always
- a rest ends the phrase (parser rule, unchanged); the note before a rest
  is `hold: false`

Non-sustaining instrument: no `hold`, every onset `breath` (= plain
notes). Unmarked passages: unchanged everywhere.

Score-block JSON carries the facts bluntly per note: `"onset": "tongue"`,
`"hold": true`. The score loader — which plays Performer for these
smoke-test scores — reads them (applying §3's defaults where absent)
and builds PerformedNotes. v1's `"phrase": "cont"` key is REMOVED (no
migration debt: it shipped yesterday; the two audition patches and gen
scripts are regenerated).

## 4. Delivery (Render tier): per-note against a possibly-living voice

Per note, in order:

1. **Continuation test:** does the line's voice exist with an open gate?
   - NO → fresh voice: `set_note` (freq, vel, this note's duration,
     onset id), graph `prepare(this note's duration)` — percent stages
     resolve against THIS note, never a phrase total (`prepare(total)`
     is deleted) — fire onset triggers. **Envelope gating is decided by
     THIS note's `hold`:** `hold: false` on a fresh voice = envelopes
     ungated, release laid out inside the note — byte-for-byte today's
     play_note path, which is what makes every unphrased render
     identical. `hold: true` = envelopes flip to gated at prepare, so
     they hold at sustain when the note's end arrives.
   - YES → continue: `set_note` on the living voice (new freq/vel/
     duration/onset id), NO prepare; pull-chains see the new values;
     non-setting push bindings re-push (isSetting bindings still hold —
     v1 decision, unchanged); onset triggers fire (the tongue/slur
     gesture restarts from current value). **Pitch changes glide**: the
     frequency target ramps over ~15 ms (instrument-block override
     `glideMs`, optional) via the existing bend machinery — an
     instantaneous delay retarget puts a kink in the stored wave (the
     measured retune tick); the glide is the waveguide-native fix and
     later doubles as portamento.
2. **At the note's end:**
   - `hold: true` → the gate stays open; envelopes sit at their sustain
     stage (the live held-key machinery, verbatim). The voice stays warm.
   - `hold: false` → `gate_release()` fires — WITH the release re-layout
     of §5 — then the voice rings out through the normal tail and
     releases its slot.
3. **End of score safety:** any voice still holding when the score ends
   is released (a `hold: true` final note cannot leak an immortal voice).

**Monophonic lines**, as v1: one held voice per instrument line;
polyphonic phrasing is out of scope.

## 5. Envelope semantics: the note, always

- An envelope's timebase is the NOTE. Percent stages resolve against the
  note that prepared the voice. There is no phrase timebase and no scope
  knob — decided against explicitly (§9): patches own note-scale
  character only; phrase-scale shape (a line's crescendo) is the
  PERFORMER varying breath, delivered someday through the perform inputs
  that already exist for exactly this (wheel/pressure; 74a8's contour
  design). "No instrument's breath inherently swells past its first
  note's attack" (Matt); counters were tried and failed (breath
  exhaustion = the player's lungs, not the oboe; evolving sound-design
  textures = held-note/stream citizens, not phrased melodies).
- **Release re-layout (its own decision, not a footnote):** the release
  stage's sample counts were computed at voice birth against note 1's
  duration. At `gate_release`, re-resolve the release stage's length
  against the RELEASING note's duration (same percent/minSec/maxSec
  math, new reference), then jump in with the existing click-free
  anchor. A phrase opening on a quarter and ending on a half gets the
  half note's release, not the quarter's. The live path has the
  identical wrinkle (release runs at prepare-time nominal's scale
  regardless of actual hold length); the mechanism serves both.
- **Shapeless envelopes** (no sustain/expand stage) inside a sustaining
  patch: run once from voice birth and hold their final value at the
  boundary — the boundary restarts nothing except via triggers. Lint
  (backlog-31 family): a sustaining patch whose output-path envelope has
  no sustain stage → warn. This population should be near-empty; a
  decay-to-zero patch can't sustain a long note today either.
- min/maxSec remain the authoring tool for duration-independent stage
  times (their job; not a phrase-safety kludge — under v2 phrase-safety
  needs no authoring at all). The v1 maxSec clamps in the taught oboe
  are REVERTED.

## 6. Onset gestures and their placement

Machinery unchanged from v1 (it was right): patch declares its vocabulary
— now `"onsets": [...]` — NameGate matches a name, the gesture envelope's
`trigger` restarts it from its current value. No envelope swapping
anywhere: the main envelope holds (never re-attacks mid-line); gestures
shape the continuing air. Literal per-onset envelope VARIANTS (a honk
with a structurally different attack curve) remain backlog-68 territory
and layer on later.

Placement, from the 2026-09-20 measurements: the oboe's tongue moves
**post-loop** (an output-side dent) — the in-loop drive dip is dropout
roulette against the loop's ignition margin (9/28 boundaries died,
one re-ignited an octave up; backlog 72's marginality). Depth calibrated
to the reference player: ~10–20 dB over 40–60 ms, default mid-range;
depth as an expressive input (velocity/beat) is future. The trombone
keeps its breath-side tongue (its lip mechanism measured no dropouts).
`slur` needs no gesture at all — the glide is the slur.

## 7. Engine consequence surfaced deliberately: resumable voices

v1 rendered a phrase in ONE synchronous loop — that is where
`prepare(total)` came from, and both die together. v2's per-note delivery
with continuation REQUIRES the offline render loop to become resumable:

- A voice renders exactly its note's samples into the timeline and, on
  `hold: true`, SUSPENDS warm — graph state, envelope positions, capture
  offsets intact — instead of running release+tail.
- The next note on the line resumes the same voice's loop at the
  boundary sample.
- Release+tail+adaptive-ring run only when `hold: false` ends the line.

This is the single biggest engine change in v2 and the spec names it so
it cannot slip through as an implementation detail (yesterday's process
lesson). The live path already works this way (the audio thread pulls
voices incrementally); v2 brings the offline path to the same shape —
another unification, not a new invention. `play_phrase`'s
receive-the-whole-vector API is DELETED; its state-continuity core
(set_note on a living voice, trigger firing, boundary handling) is
retained as the per-note resume path. Verify-flags for the plan: capture
buffers across suspend/resume; the containment check moves to line end;
the 65/71 advance-order cluster gets the same byte-level scrutiny as v1.

## 8. Renames and format changes (complete list)

- `transition` → `onset` everywhere: NoteState.transitionId → onsetId;
  PerformOut::Field::Transition → Onset; the Note node's pin
  `transition` → `onset`; instrument key `transitions` → `onsets`;
  score keys per §3. NameGate keeps its name.
- `Instrument::play_note` signature → `play_note(const PerformedNote&,
  float startTime)`; the loose (noteNumber, velocity, duration, curve)
  arguments and v1's play_phrase vector API both retire. All call sites
  swept in one pass.
- **UI, Patch Output settings panel:** gains a labeled `sustaining`
  checkbox and a labeled `onsets` vocabulary text field (comma-
  separated) — teaching a patch must not require a text editor. Fix in
  passing: the existing polyphony widget's label is invisible (drawn
  under PushItemWidth(-1), where ImGui suppresses labels — Matt's
  "mystery number"); all three get visible labels. Other instrument
  keys (`volume`, `glideMs`) stay pass-through/hand-edit.
- Score: `"phrase": "cont"` removed; `"onset"`, `"hold"` added.
- Passage `|` notation unchanged; transport emission per §3.
- Taught patches regenerated by gen_articulation1.py with: `onsets` key,
  `sustaining: true`, oboe post-loop gesture, reverted maxSec clamps.
  oboe1/trombone library/audition originals gain nothing (teaching still
  happens on copies); `sustaining: true` will be added to library wind
  patches by Matt or with his sign-off, since library/ is his.

## 9. History: what v1 got wrong, on the record

v1's spec described deadline-extension ("each note EXTENDS the voice's
gate deadline... no component ever asks what's next") but the offline
build received whole phrases and baked the total at `prepare` — the
phrase became one long note to every duration consumer, silently
re-scoping every percent envelope in existing patches (oboe1's unbounded
25% release → a 3 s die-off over a line; the "I hear breath" 1 s attack).
The duration consequence never got its own design gate in the v1
brainstorm. v2's per-note model is what the v1 spec's own words
described. Also rejected on the way here: an envelope `scope: note|phrase`
knob (phrase-scale shape doesn't belong to patches at all — clean
separation, Matt); lookahead-prepare (again); envelope swapping as the
onset mechanism (gated-hold + gestures compose; swapping waits for
backlog 68 variants).

## 10. Verification

1. **Piano invariant:** phrase-marked == unmarked on a non-sustaining
   patch, byte-identical (emission-level, plus a render proof).
2. **Compat:** one-note "phrase" == today's note, byte-identical; a
   `|`-free passage byte-identical on every patch; full null gate
   after every task.
3. **Hold mechanics:** on a sustaining patch, quarter+quarter same-pitch
   with hold ≈ half note (NOT byte-identical by design — percent stages
   resolve per note; assert equality where stages hit min/maxSec or
   seconds mode, and document the delta otherwise).
4. **Release re-layout:** phrase ending on a long note measurably gets
   that note's release length.
5. **Glide:** retune-tick metric (per-sample derivative + HF burst at
   boundaries) drops to the plain-sustain floor on a phrased scale.
6. **No dropouts:** the boundary-trough metric (deep-dropout counter
   from the 2026-09-20 diagnosis) runs over phrased scales on the taught
   oboe: 0 deep dropouts required — the post-loop gesture makes this
   achievable.
7. **Resumable-voice integrity:** captured strips across suspend/resume
   sum to the same timeline as v1's single-loop render for an
   equivalent case; containment check at line end; 65/71-style byte
   scrutiny on the boundary advance order.
8. **Live/offline parity:** gencheck on phrased scores at the usual
   1e-6; the live path itself is still backlog 53.
9. **Ears:** regenerated articulation1 queue — OTJ phrased/flat pairs,
   hold cells, now with slur glide + post-loop tongue.

## 11. Cut line

IN: everything above. OUT (deliberate): live keyboard phrasing (53 —
now pure wiring); Performer breath contour (74a8); onset-keyed envelope
variants (68); depth-as-expression; polyphonic phrasing; per-node
duration scoping beyond envelopes (segment/evolution nodes still see the
prepared length — now always a NOTE length, which shrinks that
inconsistency to nothing in practice).
