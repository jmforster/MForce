# Note Transitions — phrase-aware note delivery and articulation gestures

2026-09-19. Brainstormed with Matt (interactive, full session); every decision
below is his or was approved by him explicitly. Supersedes nothing; feeds
backlog 68 (the multi-graph question stays open behind it) and closes the
design phase of the GOALS "Articulations" item for the v1 slice.

**Problem, in one render:** Ode2Joy on the trombone is 72 notes and 72
breath-attacks, because a note is an atom — every note acquires a fresh
voice, ignites a cold bore, and dies. A phrase played on one breath does not
exist as a concept anywhere in the engine.

**v1 target:** OTJ phrase-marked on trombone and oboe, rendering as
first-note-breath + tongued-rest per phrase, audibly different from today's
every-note-re-ignited version. Strings are the eventual prize; winds are the
testbed we have.

---

## 1. Vocabulary (tier placement settled)

- **Articulation** (Compose tier) — a score symbol: mordent, staccato,
  tenuto, accent, slur/phrase mark. Means something, does nothing. The
  existing composed Articulation component and its `apply_articulation`
  flatten (duration/velocity) are UNTOUCHED by this design.
- **Interpretation** (Performance tier) — what the Performer decides when
  reading symbols. Its products come in four types: (1) more events
  (mordents → extra Notes), (2) modified events (staccato →
  adjustedDuration/Velocity — today's flatten), (3) per-note objects
  (bends → PitchCurve), (4) **per-boundary objects → Transitions. Type 4 is
  the only new thing.**
- **Transition** (Performance-tier artifact) — one per note, the way you get
  INTO this note: `breath`, `tongue`, later `slur`... Deliberately NOT named
  NoteConnector: the Figure/Phrase/PassageConnector family is Compose-tier
  (pitch-bridge math, part of what the music is); this is a performance
  decision. Same dense-parallel shape, different tier.
- **Shape** (Performance-tier artifact, DEFERRED) — the second per-note name
  slot: how this note is played (honk, squeak, breathy). Note-shaping vs
  note-connecting is Matt's taxonomy; both ride the same delivery mechanism,
  and v1 builds the mechanism but delivers only transitions. Shape notation
  + emission is its own brainstorm.
- **Realization** (Render tier) — per perform_source_design.md §1: the whole
  instrument-side tier, Setup (push, at play_note) + Rendering (pull). Not a
  per-thing object. The patch's trigger wiring is where transitions are
  realized.

Per-note delivery is a small set of SLOTS, not a bag of strings:
`transition: <name>` (exactly one per note), later `shape: <name>`
(optional). Names are patch-vocabulary strings; see §5 for interning.

## 2. Compose surface: the `|` token

The passage-string parser gains one token, `|` = phrase boundary. Notes
between boundaries (or string ends) form one phrase. `|` adjacent to rests
is legal; leading/trailing/doubled `|` are no-ops, not errors. No
score-symbol Articulation objects are created or consulted — the v1 pipeline
deliberately bypasses Interpretation product types 1–3.

Compatibility is structural, not promised: a string without `|` parses as
every-note-its-own-phrase, and a one-note phrase IS a note as it works today
(§4), so old passages render byte-identical.

## 3. Performer emission

One function, signature "phrase position + note → transition name." v1 rule,
deliberately dumb: first note of phrase → `breath`; every subsequent note →
`tongue`. Phrase end emits nothing (end-of-phrase is delivery's job). No
configuration surface, no instrument-block entries, no `slur` (a later
vocabulary entry + patch wiring, zero machinery).

This seam is where future refinement lands without structural change:
slur-vs-tongue, double-tonguing above a tempo threshold, "Fred prefers…"
(backlog 68 Tier 2), per-InstrumentClass vocabularies (backlog 78).

## 4. Delivery: phrase = gate span, zero lookahead

**Core identification:** the conditional release IS gate semantics. The unit
that acquires a voice, opens a gate, and rings out through kVoiceTailSec —
everything a note is today — becomes the PHRASE. In-phrase notes re-drive
the living voice.

- **Phrase start** = today's path exactly: acquire voice, write NoteState,
  open gate.
- **In-phrase note** = no acquisition. `set_note` (grown by a transition id
  and an explicit continuation flag — structural, never inferred from the
  name) re-drives the same voice: freq retunes the delays (per-sample
  capable per the STK legato groundwork), velocity updates, and ALL per-note
  maps (noteFaces curves, dynamicPins) re-evaluate for the new note. Gate
  stays open.
- **Conditional release, mechanized:** each note EXTENDS the voice's gate
  deadline to its own end. A deadline that expires unextended — offline
  because the event stream moved on, live because key-up came with no key
  held — closes the gate; release fires; tail rings as today. No component
  ever asks "what's next."

**No lookahead anywhere, by decision (Matt):** the gesture is always the
ARRIVING note's attack, both paths. Offline = live literally, not
"same semantics, different scheduling." Timing nuance is owned by the
gesture's internal shape first (the patch author places the re-speak point
within the gesture Curve/Envelope) and Performer rush/layback second
(already assigned to the Performance tier in perform_source_design). On
record: rush shifts retune + gesture together, whereas a human anticipates
the tongue slightly; if ever audible, fix inside the gesture shape — never
with lookahead.

**Live rule (design decided here; BUILD DEFERRED to the backlog-53
follow-up):** overlap = legato, gap = detached, binary, decided at the
instant of each event. NO tolerance threshold — a gap tolerance forces
delaying every release by the tolerance window (you cannot un-release a
bore), which trades sloppy-legato forgiveness for universal detached-play
mush and re-imports lookahead in miniature. Overlap-to-legato is the
industry convention (mono synths, "fingered legato" libraries). Accidental
overlap in fast detached playing produces exactly backlog 53's requested
behavior (cut previous at next attack). Escape hatch if real playing proves
sloppier: a LATCH (UI toggle / MIDI sustain pedal = "everything is one
phrase while down") — explicit intent, zero latency — not a threshold.

**Scope: monophonic lines.** One phrase-voice speaking per line. A phrase
start while the previous phrase's tail rings is two voices, exactly as
consecutive notes are today — nothing new at the boundary. Piano/chords do
not participate.

## 5. Patch side: visible wires, no subscriptions

House principle enforced (Matt caught the violation): perform_source_design
rejected AFM's swconnect-style invisible coupling; per-note facts enter the
graph as visible, group-local wires. A `trigger: "tongue"` STRING SETTING on
Envelope that silently responds to note delivery is that portal reborn —
REJECTED (recorded in §8). The shipped design:

- **`Note.transition` output pin** (later `shape`), first-class alongside
  velocity/duration (precedent: the duration pin, 09-11). It carries the
  note's transition id as a plain HELD LEVEL for the note — an attribute of
  the Note on a wire, exactly like velocity. Not a pulse, not an event
  signal: the wire protocol stays pure value-flow.
- **NameGate**, one new trivial stateless node: one setting (the
  target name, visible on the node in the graph), output 1 while its input
  matches that name's id, else 0. This is where the string lives — on a node
  you can see.
- **`Envelope.trigger` input pin**, SAMPLED AT SETUP: iff nonzero at the
  moment of a note's Setup, the envelope restarts — FROM ITS CURRENT OUTPUT
  VALUE (the no-click rule). Consecutive same-name notes each restart
  because each has its own Setup; no edge detection exists or is needed.
  Composition is ordinary wiring: tongue-or-slur = two NameGates → Add →
  trigger; one name to two gestures = fan-out.
- **Default behavior unchanged:** an envelope with nothing wired to trigger
  starts at voice start (= phrase start) and releases at gate close, which
  for a one-note phrase is exactly today.

**The gesture pattern, stated once:** the restart is never the sound. A
tongue is an envelope idling at 1.0 that dips (e.g. 1.0 → 0.2 over 15 ms →
1.0 over 25 ms) multiplied into the breath path. A honk (later, via shape)
is a permanently wired, normally SILENT path — formant hump, pressure surge
— whose gain envelope idles at 0 and gets energized by its trigger. When a
shape someday refuses to be a silenced path (structurally different
excitation), THAT is the boundary where backlog 68's multi-graph earns its
complexity. Not before.

**Ids and vocabulary:** the instrument block lists the names this patch uses
— inventory, NOT wiring (the connection is the wire; contrast with the
retired paramMap, which WAS the connection). Ids are interned in declaration
order and stable, so ordinal use in a Curve (Matt: "42 for tongue, 55 for
everything else — a Curve with 2 points") is legitimate and dependable, the
duration-into-Curve idiom. No lint restricting these pins to NameGates
(considered, dropped by Matt). Lint that stays: warn on a NameGate name that
nothing ever emits (backlog-31 family).

**Degradation contract:** an untaught patch (no vocabulary, no NameGates)
phrases — in-phrase notes are plain retunes with whatever attack the graph
gives a moving pitch. Emitted-but-unclaimed names are silently fine.

**Terminology note for the implementer:** pin = wireable connection point;
face = a node's editor instance (noteFaces). This spec adds PINS.

## 6. v1 teaching plan

Trombone declares `tongue` (dip envelope into the breath/pressure path — the
same input the velocity map drives); `breath` likely undeclared (phrase-start
attack is what the patch already does on voice start). Oboe: same two-line
treatment. Gesture shapes tuned by ear, by Matt or by run, as ordinary
Envelope editing. Nothing else in either patch changes.

## 7. Verification plan (gate order)

1. **Dormancy null:** feature at rest is byte-identical — full null-gate
   manifest unchanged, and any `|`-free passage renders bit-identical
   (one-note phrase ≡ today's note, proven not asserted).
2. **Gate-extension correctness:** two-note phrase, same pitch, same
   velocity, nothing wired = byte-identical to one note of summed duration.
   The sharpest single test of the conditional release.
3. **Mid-voice re-drive:** two-note phrase C4→D4, offline vs `--gencheck`
   live path agreeing to the usual 1e-6; per-note maps verifiably
   re-evaluated (trombone lip ratio measurably moves at the boundary).
4. **65/71 gremlin watch:** in-phrase Setup happens near loops and shared
   consumers — verify gesture envelopes advance at exactly 1x on in-phrase
   notes (the 2x symptom is the known signature; repro pairs in baselines).
5. **Retrigger continuity:** restart-from-current-value verified
   numerically, no step beyond threshold.
6. **Roundtrip:** UI save/load of patches carrying NameGate + trigger
   wires, 100%.
7. **Ears:** OTJ on trombone and oboe, phrased vs unphrased A/B — the
   render that started this.

Engine-verify flags (named during design, to be resolved in the plan, not
assumed): (a) where mid-voice `set_note` lands in the advance order — the
65/71 cluster is exactly this neighborhood; (b) per-note map re-evaluation
mid-voice must REUSE the prepare-time code path, not re-implement it;
(c) the trigger pin's Setup-time read must be well-defined against the
advance pass (the tap/ref one-sample lessons apply).

## 8. Rejected alternatives (with reasons, for the next simplifier)

- **Lookahead-prepare** (peek at next note, prepare altered envelope):
  impossible live (the future doesn't exist until the key is pressed) →
  two divergent paths, the disease render-unification just cured. Replaced
  by conditional release + gesture-at-attack.
- **Settings-string subscription** (`Envelope.trigger = "tongue"` in the
  settings pane, Setup restarts matchers): invisible coupling, the AFM
  swconnect lesson; composition cases breed string syntax. Replaced by
  wires + NameGate.
- **Per-name pins growing on the Note node:** unbounded pin surface, one
  engine+editor round per articulation forever.
- **Time-ramp pin + Curve as gesture:** a mini-Envelope re-implemented from
  stateless parts to dodge the retrigger question (Matt caught the smell).
  Envelopes are the time-shape citizens; answer the question instead.
- **Pulse/event signal on the transition pin:** superseded by Setup-sampled
  level semantics (Matt's attribute-of-the-Note framing) — no event idiom
  enters the graph, consecutive same-name notes handled by per-note Setup.
- **External override lists** (articulation = named Node.setting sets
  applied from outside): gesture knowledge outside the graph, breaks on
  patch reorganization, cannot express time-shapes. Inside-the-patch was
  decided first and everything downstream depends on it.
- **Gap-tolerance threshold for live legato:** delays every detached
  release by the tolerance; latency trap (see §4).

## 9. Cut line

**In v1:** `|` token; breath/tongue emission rule; transition slot through
`set_note` + continuation flag + gate-extension release; `Note.transition`
pin (held level); NameGate; `Envelope.trigger` (Setup-sampled,
restart-from-current); trombone + oboe taught; gates §7.

**Out, each deliberate:** `shape` slot (mechanism ready; notation/emission
is its own brainstorm); `slur` (vocabulary entry later); rush/layback
(nothing in OTJ needs it); breath-that-moves-during-a-note (74a8 — adjacent
design conversation, NOT this diff); live keyboard legato (immediate
follow-up = backlog 53: pool floor, last-note priority, click-free cuts —
reuses this delivery untouched); trigger pins on non-Envelope nodes
(noise-burst retrigger is the obvious second customer, later).

**Adjacent, filed:** backlog 78 — InstrumentClass: Parts specify an
instrument class, the Performer must use an Instrument of that class, the
class owns its valid transition/shape vocabularies, and Performer emissions
+ patch NameGates are both checked against it. The principled home for the
name coupling; v1's patch-level list is its seed.
