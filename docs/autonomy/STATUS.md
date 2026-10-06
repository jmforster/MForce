# Status — session handoff (Claude reads this first; Matt's queue = */REVIEW.md)

Updated: 2026-10-04 later (refactor session I with Matt; no code changed).
DECIDED: no restart. Engine is refactored under its existing gates; the UI
is rebuilt module by module out of main.cpp. Goal = go public to attract
developer collaborators, code near pristine (CLAUDE.md "Design principles",
recorded at Matt's request). AGREED, NOT BUILT: enforcement goes in BEFORE
campaign 1 — ratcheted structure-budget script (CTest + pre-commit + Claude
Code edit hook), a design section in every spec for Matt's sign-off, a
module map, an independent refuter with a principles checklist (separation
of concerns etc.), and an "architecture delta" report in REVIEW per
campaign (moderately verbose, script-generated metrics, 1-2 C++ lessons).
C++ primer for Matt: docs/matt/cpp_primer_10000ft.md. Measured from git:
mforce_ui/main.cpp = 196 commits, only 3 of them >=500 lines, never a
second source file; Fable 5 53% of added lines, Opus 4.6/4.7 37% — not a
model problem. NEXT: campaign-by-campaign discussion with Matt (my read:
gates first, then campaign 1 = engine RT safety with the allocation test
first and CTest registration pulled forward); spec at xhigh.
SAME DAY, LATER — PLAN CHANGED TO TWO PHASES (Matt: major surgery first,
"extra latitude to slash n burn"; gates only after). Spec:
docs/superpowers/specs/2026-10-04-refactor-gates-design.md, section 0.
**FEATURE FREEZE: no feature work in any lane, no "go" runs, until Matt
lifts it** (CLAUDE.md "Scope right now"). Phase 1 = surgery: latitude on
the route (delete, rewrite, break internal interfaces, big commits, no
hooks/ratchet, areas a goal not a rule); fixed = ONE signed
target-architecture doc for the whole codebase incl. deletion list,
behaviour gates before/after every cut, checkpoints ending in commit +
independent review + architecture report. Checker is built now only as a
meter. Phase 2 (ratchet, hooks, per-change sign-off) switches on when
surgery ends; its five open decisions (spec section 5: limits 600/80,
`lizard` NOT installed, source/filter peers, default-proceed, third REVIEW
file) still need Matt.
PRE-SURGERY STATE OF GIT (10-04): main had no uncommitted code. Docs
checkpoint 93a8633 PUSHED (Matt approved; origin had been 658 commits
behind since 07-03; the repo is public and he is fine with that; push
only when he asks). The two September commits that never reached main
are now on it: 70ae4c9 (UI fix, duplicate same-field PerformNode ids
survive roundtrip) and f684b47 (FormantSequence1 repaired, NATest1
retired). Validation after a full rebuild: engine_tests ALL PASS (3581
checks), test_figures 43/43, UI stamp clean, null gate 77/79 identical
with exactly the two expected differences, roundtrip 134 patches 0 id
changes + 8 render diffs (backlog 79 updated). Manifest refrozen for
those entries plus the two phrase_smoke baselines: 80 entries. April
branches/worktrees (chord-walker, harmony-first, agent-*) are old
experiments, left alone; agent-a57ee260 holds a 27-line uncommitted edit
to music/structure.h from 04-12. GOALS.md stays Matt's uncommitted edit.
TARGET ARCHITECTURE PART 1 DRAFTED: docs/architecture/target-architecture.md
(layers, UI modules vs his Unity project, deletion list, bug list, which
parts get max effort). Five "(ask)" items in its section 5 need Matt.
10-05: MATT ANNOTATED PART 1 (17 `MATT:` notes in the doc); we debate and
decide ONE TOPIC AT A TIME AT MAX EFFORT, and each decision is written
into the doc in place of his note (remaining `MATT:` lines = still open).
DECIDED so far: (1) tiers are joined by DATA, not calls — Performance
returns a list of performed notes, knows an instrument only by its
InstrumentClass; render knows nothing about music; new modules contract /
voicing / play (doc sections 3, 3.7, 3.9-3.11); (2) InstrumentClass is a
real type now in seed form (sustaining + onset vocabulary), named classes
stay backlog 78; (3) converters stay; (4) RenderContext guide written
(doc 3.1) — its fate is a Part 2 decision. Found while reading: performer
logic lives in FOUR places (conductor.h, patch_loader embedded-score
path, UI stamp_passage, UI live keys) and pitch_bend.h is a misnamed
two-tier file, not a type.
ALL 17 NOTES DECIDED 10-05 (every decision is in the doc, dated): Source
vs Filter = one layer, one classification, family list in Part 3; names =
Patch/Graph/Node (description) vs ValueSource/voice/Instrument (live),
patch_json functions not a codec class, build_instrument a function,
OpenPatch in the UI (no "*Document"), Transport not Generator; Theme from
JSON, constants per module, one home for the default sample rate; capture
= per-node strips (not live play); live hand-off = ownership passed by
two queues, agreed in principle, details Part 5; templates + strategy
settings move to compose (form in Part 6; strategies are an open set so
each owns its settings); Composer's writes-to-template = request+record,
Part 6 decides in/out vs separate record; paramMap retired entirely (list
docs/matt/parammap_patches.md; convert library+baselines+audition+the 9
PENDING ones — Matt's one-time exception; wiring gate 80/80 identical);
Mappings dialog reduced to its read-only view; TargetEvolution kept as a
node, string evolution form deleted; IComposer/Genre/ClassicalComposer,
SectionStrategy, 3 Composer members, one mono wrapper deleted;
realization strategies are half-finished NOT dead — finish in Part 6;
node-graph FORM stays (1,048 files), MODE goes, convert = two Patch
operations; pattern library moves to an optional shelved target
(mechanism fits all shelved generators); hiBoost DELETED outright (comp
renders change, listed as expected diffs; curves added by ear later).
ALSO 10-05: standing CLOSE-OUT RULE (CLAUDE.md): when Matt closes a
session, ask whether to push the unpushed commits. Matt's universal-
parameter idea (min/max/density on the base) QUEUED in IDEAS.md §dsp as
a feature for later (freeze); its structure lands in Part 2: a
Generator / Processor level under ValueSource (Matt's shape; Envelope
joins Generator; three pin kinds param / signal input / structural
input; pins declared once).
PART 2 IN PROGRESS = the ValueSource contract at MAX (per-tick memo,
Generator/Processor bases, pin kinds, prepare split, sample-rate truth/
RenderContext fate, no-throw, rejected wires). Reading list: envelope.h,
patch_loader's RefSource auto-wrap + collect_advance_ids + starved-ref
promotion, feedback_loop_design.md §3.3 (tap z^-1), engine_tests tap/loop
tests, perform_source.h, repeating/multiplex/vibrato prepare paths. The
spec goes in docs/architecture/ as its own part file. Then Parts 3-8 per
doc section 7; then the meter, cutting.

Updated: 2026-10-04 (full architecture/code review DELIVERED 10-03;
no code changed). Report: docs/audits/2026-10-02-architecture-code-review.md
(+ 2026-10-02-raw/). 474 findings, 123 skeptic-verified, 17 confirmed by
hand, rest marked unverified. Headline bugs REPRODUCED: Conductor replays
every part once per section; --dun renders silence; dictionary chord
voicings spin to int overflow. Campaign order in report §8 (RT safety ->
comp spine bugs -> lock-free voice publish -> registry-first loader ->
per-tick memo -> UI split -> build/test hygiene). Effort scheme: xhigh for
specs/reviews, high for executing plans, max for single hard turns,
ultracode keyword per fan-out turn only (5-hour window ~60 agents).
NEXT: priorities discussion with Matt (he is re-reading the report), then
a short xhigh spec for campaign 1. Nothing committed; audits/ untracked.

Updated: 2026-09-22 (comp walk3 BUILT via Opus dispatch; plan
docs/superpowers/plans/2026-09-22-comp-walk3.md T1-T8 done.)
State: NRS v1 profile = styles/nursery_v1.json (tendency rows, NCT
license odds, placement, critic, search sizes); note_map.h +
phrase_critic.h + melody_profile.h; select_anchors scores from the
profile and returns AnchorResult; DefaultPassageStrategy harmonic mode
runs nested best-of-N (10 phrase candidates rerolling first-referenced
derived motifs, 10 passage attempts, top-3 dice). Harmonic mode requires
passage melodyProfile. Batch: renders/comp/audition/walk3/, 10/10
validator, 0 over-budget phrases; strings in
docs/matt/Comp_walk3_for_annotation.txt. Null gate: only
template_mary_walk differs (test_k467_walker fails at HEAD, pre-existing).
Known: ChordLabel::to_string labels G7 "V" (compares ChordDef name, not
shortName); note_map uses its own label function; chord_walker untouched.
comp REVIEW Awaiting: **23** (walk3 listen/annotate); 22 folded into 23.
NEXT: Matt's walk3 annotations → next profile edit or rule round.

Updated: 2026-09-22 (comp walk2 annotated; session ended on QUOTA: 75% of
Matt's weekly Fable limit burned by Tuesday — NEXT SESSION RUNS THE
QUOTA PATTERN: Fable coordinates thin, every heavy build/spec-execution
DISPATCHES TO OPUS. Walk1 was built inline on Fable only because Opus
529'd twice that night; that exception is over.)
**WALK2 ANNOTATED (docs/matt/comp_walk2_annotated.txt, committed) —
DISCUSSION OPEN, NOTHING BUILT (Matt: "don't go off half cocked").**
Verdict: much improved, bar-6 nonsense gone; s108's antecedent came out
as LITERAL Mary; s100 a clean Pass. The distilled finding: walk2's
complaints are ONE missing grammar — NCT approach/departure rules with
leaps as the violation vector: (a) leap-TO-an-NCT (appoggiatura) out of
genre; (b) leap-FROM-a-passing-tone breaks its license; (c) chord
EXTENSIONS are NCT-class (Mary's own E-over-G7 13th is fine BECAUSE it
steps down — the rule was never "no 13ths"); (d) leading tone B wants C,
may detour B-D-C, never leaps down (first scale-degree tendency); (e)
departures COMPOUND ("taken together = computer"). Answered Matt's dice
question: F-G over C was NOT dice — suspension term only fires across
chord changes, 96:4 only over V; fresh-struck NCT left upward inside a
bar was scored zero. AWAITING MATT, the two design forks before any
build: (1) compounding = hard per-phrase departure BUDGET (my lean —
matches his judging) vs super-additive penalties; (2) genre profile
becomes DATA now ("NRS v1" — his "rules are genre-dependent" point;
compound-melody exception = a future profile level) vs constants until
the ruleset stabilizes (my lean: data now). Context-priming (s103 "less
jarring after repeated Gs") deliberately deferred.
Standing order (in batch script): every batch emits passage strings to
docs/matt/Comp_<batch>_for_annotation.txt. False alarm resolved: walk1/
walk2 same-named WAVs — Matt played walk1 s105 by accident; chain
verified byte-exact end-to-end (template==JSON==WAV==strings), spectrum
confirmed. Matt declined filename stamping.
comp REVIEW Awaiting: **22** (walk2 — annotations IN, forks OPEN),
**20** (crawl verdict, formally), 19, 18, 17, 16, 14, 13, 12, 11, 10, 9.
NEXT SESSION: read comp_walk2_annotated.txt + REVIEW 22 fold, get
Matt's fork answers, THEN spec walk3 (NCT grammar + leading-tone
tendency + compounding + possibly profile-as-data) and DISPATCH THE
BUILD TO OPUS. head_a permutation fix still queued behind it.

Updated: 2026-09-21 (comp lane revived — brainstorm → spec → plan with
Matt, then the build; Opus dispatch died twice on 529s, finished inline
on Fable).
**COMP CRAWL SHIPPED — REVIEW 20 (Mary, harmony-first, zero generation).**
Matt's five ground rules are standing law (memory + spec §1): crawl
before walk; harmony fully specified FIRST in the template; generated
figures get FITTED to the underlying chord; model stack extends, never
rebuilds; every generator except Markov Figure is shelved
(comp/GENERATORS.md is the catalog). NEW INSTRUMENT RULE: melody oboe1,
accompaniment piano_default (CLAUDE.md updated, supersedes 09-05
all-piano). NEW CONVENTION: comp-purpose .psg saves use `|` as
STRUCTURAL phrase boundary only.
Spec docs/superpowers/specs/2026-09-21-comp-crawl-mary-design.md; plan
docs/superpowers/plans/2026-09-21-comp-crawl-mary.md.
WHAT LANDED: passage_melody.h (scale-grid math + passage-string →
Locked-figure PhraseTemplates, engine_tests cover the full Mary oracle);
PassageTemplate.melodyPassageFile/melodyOctave (round-trips);
apply_passage_melodies at CLI template load; compose honors per-part
instrumentPatch with one instance per unique path (template null gate
27/27 byte-identical) + multi-instrument-only mix normalization (the
first scoped-to-everything version moved 2 chord baselines — caught by
the gate, rescoped); scores/baselines/template_mary_crawl.json + tracked
Comp_Mary.psg (gitignore anchored: /passages/ not passages/); render
verified event-exact (26 melody events == Matt's transcription, C-E-G /
G-B-D-F root position at the right bars, peak 0.98) →
renders/comp/audition/crawl1/, WAV sent to Matt. Backlog 21 done
(zero-event = failure, rc-0 silent class proven live by positive
control). Baseline template cull 27→19 by feature cover (Matt's
directive; 9 subsets → scores/old/baselines_culled_2026-09-21/); the
k467 family kept SIX not one — period/harmony/motifs/parallel/opening
each carry unique loader features. TRAP for template authors:
ScaleChord.degree is 0-BASED (tonic 0, dominant 4) — 1-based roman
numerals render supertonic chords; first Mary render did exactly that.
Old template_mary.json connectors ({"type":"Step","step":N}) parse as
leadStep 0 — pre-existing, left as-is, loader-era artifact.
SAME DAY, LATER: **STRUCTURAL TEMPLATE + WALK ROUND 1 SHIPPED** (Matt
drove the brainstorms, then "spec/plan/build without me").
(1) template_mary_structural.json — named motifs (family scheme
head/rep3/rep3_a/rep3_b/head_a/close), references, parallel period —
renders BYTE-IDENTICAL to the crawl; first-ever proof of the
reference+connector path. TRAP PINNED at every authoring surface:
template connectors are DENSE per figure ([i] = bridge INTO figure i,
[0] null dummy), NOT an N-1 between-figures list — my plan had it wrong,
the Opus agent silently built the derivation right, and the misauthored
JSON shifted every figure by its connector.
(2) Walk round 1 (spec/plan 2026-09-21-comp-walk1*): Complexify
TransformOp (elaboration reined by target note count); derived-motif
synthesis in realize_motifs (content-less derivedFrom declarations
resolve through figure_transforms::apply, chains, throws by name);
harmonic anchor selector (anchor_selector.h, opt-in
PassageTemplate.anchorMode="harmonic") — Matt's 3 edge rules hard
(phrase-opening downbeat + phrase-final = chord tone; passage-final =
degree 1), chord-tone weights with stacked bar-final/figure-final/long
boosts, cursor proximity, seeded roulette over the ENUMERATED legal
chains, parallel intent pins the consequent opening. Gates: engine_tests
557→754; template null gate 19/19 byte-identical (opt-in); batch
validator 10/10. **REVIEW 21 = ten generated siblings**
(renders/comp/audition/walk1/), first generated output of the reset —
sibling melodies are already tune-shaped (s101 re-anchors head on the
5th, uses F-over-G7 as the seventh, cadences to C).
Known/accepted: exact Mary out of walk support (vary_steps is
interior-only — final-step/cadential freedom is a named future thread);
interior NCT hole open by choice (minor-Mary passes rules).
OPEN ON MATT: **REVIEW 20** (crawl verdict — covers structural template
too, byte-identical) + **REVIEW 21** (walk siblings: which please,
which fall, how) + the standing 18/17/16/13/14/9/10/11/12/19.
OPEN ON DEV: on walk verdicts — weight calibration or next rule
(interior NCTs / re-shape-to-fit); then generated figure slots (Markov
proposes, fit disposes). Voicing-tier question (score vs lead-sheet)
parked in IDEAS.md §comp.

Updated: 2026-09-21 (autonomous dsp run, Fable inline; Matt brainstormed
comp in a parallel session — comp GOALS deliberately untouched here).
WORKING-TREE NOTE: docs/autonomy/GOALS.md is Matt's own uncommitted
refresh — never stage/commit/revert (decomposed into backlog 81–89 this
run, but the file itself stays his).
**STACK RUN — full report reports/2026-09-21-dsp-run.md.** Shipped:
(1) **Oboe trading licks SOLVED as two patches → REVIEW 76**
(oboe_nasal1 queue): two ignition states (fundamental-carried = Matt's
"nasal oboe"; H2-carried, +8 dB = his "flute/clarinet"), line inherits
its breath note's ignition, D6 = the 3/8 coin-flip pitch; junction
middle-slope ×0.85 makes the oboe state unanimous (tone preserved,
−1.6 dB; 1/8 residual flip on 2 s held E6/F#6); −46 dB H2 bias into the
loop makes the flute state unanimous. Tooling tools/oboe_licks.py.
MID-RUN CORRECTION on record: Matt's oboe_compare.wav is the FULL psg
at 120 bpm — first per-note grid of HIS file was mislabeled (2× tempo);
corrected same day via his side-chat note; offline findings were
self-consistent throughout. (2) **Live legato SHIPPED (backlog 53 v1) →
REVIEW 75**: engine deliver_continuation()/continue_voice_live()
extraction (null gate 79/79 byte-identical, engine_tests 508), UI
overlap=slur / release-back / gap=detached / ring-cut on sustaining
patches; tongue needs a latch (wire-ready, Matt's word); FLAG: patches
declare polyphony 1 as offline semantics (even piano_default), so mono
rides `sustaining`, not polyphony. (3) **"All breath" Onsets option**
(mode 3 = pre-articulation) — relaunch picks up both UI items.
(4) **Trombone transitions DIAGNOSED, folded into 74**: glide
no-man's-land collapse → re-ignition overshoot (+2% = the squeak) over
old-partial residue (= the blend); tongue can't fix (pitch still
glides); candidates recorded not built (patch "unfinished" per Matt).
(5) **62 UNBLOCKED**: bwg_perc2 recalibrated into the patches (0–3.3 dB
from library reference, was −20); tomdrum ~47 dB structurally quiet
since round 1, flagged. GREMLIN on record: ONE unreproducible loud
render of identical tomdrum bytes (35 dB), evidence overwritten by my
own stress test — offline-determinism watch, backlog 59 family.
(6) **65 pluck flag ANSWERED**: plucks track; a 27 s ring masks it +
38 c flat; scale demos staged; follow-ups backlog 89. (7) GOALS
decomposed → backlog 81–89; junction periphery measured (−20 dB rel at
±10%, second-order, not zero). dsp REVIEW Awaiting: **76**, **75**,
**74**, **72**, 62 (re-listen), 65 (scales), 38, 39.

Previous update: 2026-09-20 late (interactive with Matt, after the v2 Opus build
below; session continuity save 09-21 morning).
WORKING-TREE NOTE unchanged: docs/autonomy/GOALS.md is Matt's own
uncommitted edit — never stage/commit/revert.
**POST-V2 EVENING ROUND — Matt's first-pass 74 verdict ("goals 100%
achieved. No clicks, clean and smooth articulations") + two unforeseen
effects, both shipped same evening; articulation1 queue REGENERATED
after, so his next listen is one build newer than his first.**
(1) **Per-note draw re-anchoring** (his option (a)): ValueSource::reseed()
no-op virtual; Randomizer stores seed + reanchor(); overrides across the
noise family + Envelope draw rngs + Vibrato/LayeredRed forwards; the
CONTINUATION Setup re-anchors every node (never fresh paths — prepare
consumes layout draws, post-prepare reseed ≠ byte-neutral). Fixes the
phrased-oboe "different oboeist per phrase" (backlog 72 unmasked by
phrasing); pins notes 5/7's overblow back — r5c/72 remains the real
cure. Null gate 79/79; engine_tests 508 incl. an exact-replay test.
(2) **Trombone slur struggles = partial-crossing (diagnosis, Matt
concurs by ear)**; his two experiment knobs shipped: transport "Onsets"
combo (auto/all tongue/all slur — stamp_passage takes the mode; phrase
starts stay breath) and Output-panel "glide ms" drag (write-only-when-
touched; taught-patch + corpus roundtrip verified, 8 known findings, 0
new). If his ears confirm, the likely fix = one emission rule (tongue
across partial boundaries). (3) Onset vocabulary corrected to the
approved [breath|tongue|slur] — breath was undeclared, interning to 0
and false-positiving the vocab warn; now declared-but-unwired (the
fresh-voice attack IS the breath; the glide IS the slur).
OPEN ON MATT: 74 re-listen + his two trombone experiments; 72 formal
verdict (informal: "velocity-linked brightness is there"); 62/65/38/39.
OPEN ON DEV: partial-crossing emission rule (gated on Matt's
experiments); backlog 75 passage-cutoff diagnosis (Matt: minor;
UI-buffer half already fixed 09-20); backlog 79 roundtrip lossiness
(with 65/71); 74a8 breath contour design (its Performer-side home =
wheel/pressure inputs, per v2 spec §5); live legato = backlog 53 (pure
wiring now). Specs: 2026-09-20-note-onsets-v2-design.md (+ plan) is
CANON; the 09-19 v1 spec is history (§9 records the divergence).


Updated: 2026-09-20 (onsets v2: Matt drove the redesign after auditioning
73; brainstorm → spec → plan → Opus dispatch, coordinator re-verified).
WORKING-TREE NOTE unchanged: docs/autonomy/GOALS.md carries Matt's own
uncommitted refresh — never stage, commit or revert it.
**NOTE ONSETS v2 SHIPPED — REVIEW 74; v1 (REVIEW 73) folded to Resolved
the day after it shipped.**
Spec docs/superpowers/specs/2026-09-20-note-onsets-v2-design.md; plan
docs/superpowers/plans/2026-09-20-note-onsets-v2.md; report
reports/2026-09-20-note-onsets-v2.md.
WHY v2 EXISTS: v1's spec described deadline-extension, but the build baked
a phrase's TOTAL at prepare — so a phrase was one long note to every
duration consumer and silently re-scoped every percent envelope (oboe1's
25% release → a 3 s die-off over a line). Matt's audition named the
symptoms: retune ticks, no repeated-note articulation, envelope stretch.
THE SHAPE: every note is atomic and carries two facts — `onset`
(breath|tongue|slur) and `hold` (does the excitation continue past its
end). No phrase object reaches Render-land; continuation is DERIVED from
engine state (is the line's voice open?), the same test live key-overlap
makes. PerformedNote is the Performance→Realization boundary object and
play_note's only argument; play_phrase / PhraseNote / prepare(total) are
deleted. The offline render loop is now RESUMABLE: render_chunk is the
one sample loop, a hold:true note suspends the voice warm, the next note
resumes it at the boundary sample, release+tail run only at line end.
Envelope timebase is the NOTE again, always; its companion is release
re-layout — gate_release(releaseRefFrames) re-resolves the release stage
against the RELEASING note. Pitch changes inside a line GLIDE over
instrument-block glideMs (default 15 ms) via the existing bend machinery
(note: a legacy paramMap patch pushes frequency once at Setup, so only
pulled frequency chains glide — every taught wind patch does).
`sustaining` is DECLARED in the instrument block, never inferred: a
non-sustaining instrument never receives hold, so the piano invariant
holds by construction. UI: Patch Output pane gained sustaining + onsets
widgets (and visible labels — the polyphony spinner's was swallowed by
PushItemWidth(-1) since it was added).
GATES: null gate 79/79 after every engine-touching task, including the
intermediate checkpoint that proved the render_chunk extraction byte-clean
before any semantics changed; engine_tests 494→506; piano invariant
md5-identical at CLI and through gencheck; gencheck parity scale
1.000000 / 1 LSB; roundtrip corpus = the same 8 known findings (backlog
79), 0 new; articulation1 v2 — oboe phrased 0 deep dropouts, dent
−10.9 dB repeated / 0.0 dB slurred, trombone 0 dropouts, −1.4 / 0.0 dB;
both instruments' flat renders byte-identical to a trigger-unwired
control (dormancy).
DEFERRED, on record: live keyboard phrasing is now pure wiring (backlog
53); phrase-scale shape belongs to the Performer, pointer added to
74a8; onset-keyed envelope variants (68); depth-as-expression;
polyphonic phrasing; new backlog 80 = the sustaining/shapeless-envelope
lint the spec defers. Backlog 76 CLOSED (strip drawing was O(all samples
× strips) per frame, commit 030a4d5).
NOT VERIFIED BY THIS RUN: the `|`-passage path in the running UI — there
is no headless entry point for the transport's passage string, so the
emission is covered by construction and by the score-side renders. Matt's
UI pass confirms it.
NOTED, not a regression: a phrased oboe1 line shows gencheck-vs-CLI scale
1.002499 / 313 LSB because a held line renders ~7 dB hotter than four
separate attacks (peak 0.98 vs 0.73) and enters the soft-clip knee, where
the UI's direct-instrument render and the CLI's mixer path have always
differed. Same patch at half gain: 1.000000 / 1 LSB.
dsp REVIEW Awaiting: **74**, **72**, 62, 65, 38, 39.

Previous update: 2026-09-19 evening (articulation day: the note-transitions
build, inline on Fable, same day as the brainstorm).
WORKING-TREE NOTE unchanged: docs/autonomy/GOALS.md carries Matt's own
uncommitted refresh — never stage, commit or revert it.
**NOTE TRANSITIONS v1 SHIPPED — brainstorm (Matt + Fable, this morning) →
spec → plan → build in one day. REVIEW 73.**
Spec docs/superpowers/specs/2026-09-19-note-transitions-design.md; plan
docs/superpowers/plans/2026-09-19-note-transitions.md; report
reports/2026-09-19-note-transitions.md. The shape: `|` phrase marks in
passage strings; a phrase = the unit that acquires a voice and opens the
gate (play_phrase; play_note is now a one-note phrase, byte-identically);
in-phrase notes re-drive the LIVING voice (set_note + non-setting push
bindings, no prepare); transitions arrive as a per-note interned name on
a new Note.transition pin; a new NameGate node (name string visible on
its face) feeds the new Envelope.trigger pin (Setup-sampled,
restart-from-current, click-free by numeric test). Emission rule v1:
first-of-phrase "breath", rest "tongue" — one seam, two callers (score
loader "phrase":"cont" + UI transport). Taught patches:
patches/audition/articulation1/{trombone,oboe}_tongue.json (originals
untouched; the consonant = one TDip envelope, Matt's knob). Ears queue
renders/dsp/audition/articulation1/ — OTJ one-breath-per-line against
today's every-note-a-breath.
GATES: null gate 79/79 after every task incl. the play_note delegation
refactor; two-note phrase == one long note byte-identical (engine test +
CLI smoke pair patches/baselines/phrase_smoke_*.json — NEW baselines,
add to manifest at next deliberate refreeze); mid-voice retune
zero-crossing ratio 1.498; gencheck parity scale 1.000000 / 1 LSB incl.
a taught+phrased score; taught-patch roundtrip intact; engine_tests
474→494.
DEFERRED, on record: live keyboard legato (overlap = phrase; needs
backlog 53's pool work — delivery machinery ready); shape slot
(honk/squeak — own brainstorm); isSetting push bindings hold their
phrase-start value mid-phrase (v1 decision); backlog 75 passage-cutoff
diagnosis queued (Matt: "extremely minor", deferred).
NEW FIND, backlog 79: 7 pre-existing roundtrip render diffs on
loop/bug-repro baselines (+ the known wiring_smoke id loss) — proven
pre-existing against a 46f4564-built CLI on identical roundtripped
JSON; first surfaced because that corpus had never met the harness.
Matt's informal note on 72 (formal verdict pending): "the
velocity-linked brightness is there."
dsp REVIEW Awaiting: **73**, **72**, 62, 65, 38, 39.

Previous update: 2026-09-19 (dsp, trombone attempt 4 — the held-note round;
Opus 5 dispatch).
WORKING-TREE NOTE for the next session: docs/autonomy/GOALS.md carries
Matt's own uncommitted refresh (his steering action item) — never revert
or fold it into a run commit; he commits it himself.
**TROMBONE ATTEMPT 4 SHIPPED — REVIEW 72, backlog 74(a4)(a5)(a7) RESOLVED,
(a6) measurement attempt 1 of 2 spent with no mechanism standing, 71
superseded. Zero engine code.** Artifacts: tools/gen_trombone4.py,
candidate patches/audition/trombone1/trombone_attempt4.json (56 nodes,
gateable=1), 8 ears cells in renders/dsp/audition/trombone4/, report
reports/2026-09-19-trombone4.md.

**74(a5) — THE SUSTAIN WAS NEVER FLAT; A FIXED OUTPUT FILTER WAS EATING
IT.** Matt heard "sustain phases sound identical, just louder". Measured
BEFORE the voicing lowpass (probe Trans4), attempt 3's own settled-window
pp→ff centroid is F2 **+24.9%**, A#3 **+80.8%**, C5 **+53.0%**, F5
**+163.0%**; measured AFTER it, +0.8 / +18.5 / +33.8 / +27.0%. The fixed
1100 Hz corner Matt picked at attempt 2 was picked while the instrument was
stuck at forte (attempt 3's two gain stages), and a FIXED corner sitting
where the energy moves removes most of the movement. FIX: one CurveNode
(Vk) on __perf_v driving both SVF cutoff pins — 550 / **1100** / 1600 Hz at
pp / 0.8 / ff. Both ends SOLVED, not picked: the criterion is that the
instrument's own pre-filter pp→ff ratio must survive to the output, 0.8
pinned to Matt's number. RESULT settled pp→ff: F2 **+23.7%**, A#3
**+99.2%**, C5 **+89.9%**, F5 **+66.9%**; mf→ff (the half he pushes into,
and where attempt 3 was nearly dead) A#3 +0.5%→**+19.8%**, F5
+10.6%→+31.2%. >1 kHz share pp→ff ×3.45→**×102** (A#3), ×8.42→**×233**
(F5). Velocity 0.8 matches attempt 3 to **one 16-bit step** (knot-value
rounding from the steepener domain ±4→±8 at identical slope).

**ELIMINATED FIRST, BY MEASUREMENT.** The loop does NOT self-limit: drive
at the steepener input is linear in breath over 10.8–27.2 dB (A#3 13.9×
drive for 13.8× pressure). The steepener's transfer DOES scale with
amplitude (Δcentroid pp→ff +5→+56 Hz on A#3, +7→+125 on C5). So attempt
3's deferred work order 5 (steepener depth riding the breath) was never
triggered.

**74(a4) CLOSED — IT WAS MY OWN PROBE'S CLAMP.** A probe reading through a
{"tap"} is a GUARDED RefSource clamping every read to ±8
(dsp_value_source.h:164; only {"tap"} sets guard=true,
patch_loader.cpp:105-122). Attempt 3's "the lip rails 53–69% of every
cycle" could not have returned anything else. Raw-{"ref"} probe on the same
render: lip peak **70.0** where the tap said 8.000. In this graph the
engine clamp never touches the lip (Qn reads it by plain {"ref"}) and rails
**0.0%** on all three tap reads in sustain. What does hold is OUR Qn end
knot (37–69% of the positive half-cycle) — and widening it 8→256 at
identical slope moves the settled centroid **1.2%** and breaks F2's tuning.
Not load-bearing. **NO ENGINE ROUND NEEDED.**

**74(a6), MEASUREMENT ATTEMPT 1 OF 2 — THE OSCILLATION IS THE NOTE.**
Envelope modulation rate, two independent detrending methods (polynomial on
the log envelope; moving average), F2 through C5: measured/f0 =
**0.99–1.01** on every note. Bore round trip 0.50–4.01 (matches only where
p=1), lip resonance 1.23–1.41, envelope constants note-independent by
construction. No sub-f0 modulation exists. There is no rate to retune —
changing it means changing the pitch. BELOW the solved range, measured:
C1/F1 produce NO oscillation (rms 0.0000, lock 0.01–0.03), so all that is
audible is the attack thump and the bore ring-down = his "dullish impact
put thru a spring reverb"; the first note that holds is **D2** (lock 0.72),
the note he named by ear; D2/E2 run +27/+12 cents because every map holds
its F2 knot below midi 29.

**74(a7) FIXED WHERE IT WAS REAL.** Slices off the written partial 10 / 32
/ 37 at velocity 0.8 / 0.9 / 1.0 over 14 notes. Not the clamp (0% rails),
not a clean overblow (off-slices ×1.07–1.25 = an attack that overshoots and
settles); C#6 at 1.0 genuinely drops an octave. Fix: per-note ff ceiling
walked down until the ATTACK is stable, not just the settled pitch — **6 of
51 notes moved**, 45 unchanged, 0.8 untouched (his liked squeak intact).
TWO METRIC BUGS OF MINE found on the way: a fixed 60 ms slice is 2.6
periods at F2 and its autocorrelation lag search overruns the slice; and
counting DOWNWARD excursions catches subharmonic readings and pulled 25
notes' ff in on an artifact.

**NEW OPEN, 74(a8): NOTHING CHANGES DURING A HELD NOTE** — the honest
remaining half of Matt's verdict ("missing any swell or brightening *after*
the attack"). Measured at ff, the centroid reaches its value by ~200 ms and
is then identical to three digits from 0.2 s to 2.2 s (A#3 505/505/505/505).
Velocity is fixed for a note's life, so the breath is, so the timbre is.
Needs breath that MOVES while a note sounds — controller (blocked on
hardware, REVIEW 39), per-note breath contour, or PerformSource curve.
Design question, brainstorm before any build. Also new: 74(a9), pp is 4.5 dB
quieter (one constant if Matt says too much); 74(a10), above mf some notes
have almost no breath left (A#3's ceiling is 1.135× its mf).

Gates: 51/51 speak at 0.8 worst **1.9 c** (= attempt 3), no tuning or
speak-time regression, 51/51 ignite from silence at pp AND mf AND ff,
steepener drive inside its domain on every note at ff over the whole note
(worst 6.16 of ±8 — attempt 3 checked 4 notes over the settled window only
and saw 3.19), no clip (worst peak 0.900), level ceiling worst 0.331,
held-pair level match −0.00 dB, queue −0.1 to −7.4 dB vs oboe1 (0.1761).
GATE NOTE: the ignition gate is pitch-and-periodicity, never loudness —
the darker pp corner drops pp 4.5 dB, so attempt 3's 0.0015 rms floor would
have failed 22 notes that lock to the same cent and the same 1.00
periodicity. Floor is now the repo true-silence floor; pp level reported.
dsp REVIEW Awaiting: **72**, 62, 65, 38, 39 (71, 70, 69, 68, 67 resolved).

Previous update: 2026-09-18 (dsp, trombone attempt 3 — the blowing-harder
round; Opus 5 dispatch).
**TROMBONE ATTEMPT 3 SHIPPED — REVIEW 71, backlog 74(a) RESOLVED, 70 folded
to a Resolved stub with Matt's verdict verbatim. Zero engine code.**
Artifacts: tools/gen_trombone3.py, candidate
patches/audition/trombone1/trombone_attempt3.json (55 nodes, gateable=1),
8 ears cells in renders/dsp/audition/trombone3/, report
reports/2026-09-18-trombone3.md.

**74(a) WAS NEVER THE PHYSICS — IT WAS TWO GAIN STAGES OF MINE.** (1)
gen_trombone2.calibrate() peak-normalised INTO the steepener per cell
(in_gain = DRIVE_PEAK/peak) and un-normalised after it, so soft and loud
drove the nonlinearity to the same 0.800 peak BY CONSTRUCTION. Ablated, the
steepener contributes +6 Hz of centroid at S=0.2 and +65 Hz at S=3.0 — it is
a dynamics component the moment it sees dynamics. (2) the instrument was
permanently at forte: one global pressure had to keep G6 (72 kPa) alive
while A#3 plays in tune over 2.7–53 kPa, so there was no softer to go to.
FIXED: DRIVE_GAIN is a constant 1.0 (steepener sees the raw travelling
wave; verified the output trim does not move NL_in by 1e-6), and blowing
pressure is now PER NOTE and VELOCITY-DRIVEN, solved from each note's own
measured floor and ceiling (51 notes × 19 pressures, single note per render
from silence). Velocity 0.8 = attempt 2's exact working point, so the gates
hold by construction.

**THE METRIC, centroid pp→ff:** F2 +0.6%→+1.3%, A#3 −0.3%→**+18.5%**, F5
−0.4%→**+27.0%**. >1 kHz share ×0.98→×3.44 (A#3), ×1.02→×8.42 (F5). Level
pp→ff +4 dB→+23 dB. Attribution measured separately: the per-note span alone
buys +5.3%, removing the normalisation triples it. Both fixes were needed.

**LEVELLING DE-KLUDGED per Matt's directive** ("we want the sound to be
right, can always get bigger speakers"). Attempt 2's trim decomposes into a
15.1 dB smooth register ramp and ±4.9 dB of note-to-note scatter with 5.0 dB
jumps between ADJACENT semitones. Scatter = unambiguous artifact, kept in
full; 65% of the ramp applied. Register spread 0.0 dB → **5.3 dB**, top
louder. Queue WAVs still presentation-calibrated to oboe1 (0.1761).

**THE LIP WAS NOT THE LIMIT** (work order 4, one measurement, negative):
shut ~28% of every cycle even at pp, pressure rise rate up 28× (A#3) and 53×
(F5) pp→ff. No lip parameter touched. Work order 5 (steepener depth riding
the breath) NOT built — it was conditioned on 1–4 leaving brightness flat.

**NEW 74(a4), ENGINE TRAP LOGGED NOT FIXED:** the engine's global ±8 value
clamp (core/dsp_value_source.h:144) is inside the lip's operating range at
ff — displacement rails 53–69% of each cycle, and the POSITIVE rail matters
because flow is proportional to the opening. The top of the dynamic range is
squared off by a safety clamp, not by physics.
**MEASUREMENT NOTE, reusable:** a probe rendering an internal graph node is
multiplied by the voice mix gain (velocity × volume) like the real output —
this round's first pass read a soft note's lip as never closing because of
it. gen_trombone3.probe_signal() divides both out.
**STILL FLAT:** F2, +1.3%. Its breath span is 3.8:1 against A#3's 13.7:1 —
below ~26 kPa it goes sharp and then falls into a pressure hole. 74(a3).
Gates: 51/51 speak, worst 1.9 c, no tuning/speak regression vs attempt 2,
51/51 ignite from silence at pp AND mf AND ff, steepener drive inside its
domain (knots widened ±2→±4 at the identical slope, untested territory
attempt 2's normalisation had hidden), no clip, level ceiling ok.

Previous update: 2026-09-18 (dsp, trombone attempt 2; Opus 5 dispatch).
**TROMBONE ATTEMPT 2 SHIPPED — REVIEW 70, backlog 74 updated. Zero engine
code.** Matt verdicted 69 ("no, not quite yet. But it is way, way better")
with five items; this round answers all five. Artifacts:
tools/gen_trombone2.py (imports gen_trombone1's chassis — verified
BIT-IDENTICAL at matched config, max sample diff 0.0), candidate
patches/audition/trombone1/trombone_attempt2.json (43 nodes, instrument-
style, noteFaces, gateable=1), 8 ears cells in
renders/dsp/audition/trombone2/, report reports/2026-09-18-trombone2.md.

**THE HEADLINE IS A NOTE-NAMING BUG AND IT WAS MINE.** Matt: "low C does
not sound; lowest note is G2 (49Hz)." There is NO live/offline divergence —
mforce_cli and mforce_ui --gencheck agree to **1e-6** on the same patch at
every note midi 24–39, gatecheck gateable=1. mforce_ui names notes by the
HOUSE convention (octave = midi/12, main.cpp `baseNote = octave*12`, cites
comp REVIEW 19); gen_trombone1.py used scientific (midi/12−1). The two ends
of the conversation were an octave apart all round. His "C2" = midi 24 =
32.70 Hz, an octave below anything attempt 1 tuned; his "G2" = midi 31 =
49.00 Hz, which IS the lowest locking note in the render; his G6 = midi 79
= 784 Hz and the "tink" above it = midi 81+, attack transient only. Every
range claim he made reproduces to the Hz. gen_trombone2.py uses HOUSE names
throughout. **There is no shared note-naming helper in the repo** — backlog
74(g), do it on the next tool touch.

**RESULTS.** Solved range went 37 notes (midi 36–72) → **51 notes (midi
29–79, house F2–G6), all speaking, worst tuning 1.9 cents** (was 4.2 over
37). Regression-gated against attempt 1 across the shared range: zero
tuning regressions, zero speak-time regressions. Register level spread
**14.3 dB → 0.0 dB** via a measured per-note output trim in the patch (so
it applies live too). Queue loudness now matched to a measured library
reference — patches/library/winds/oboe1.json, sounding rms 0.1761, the
median of that family — worst cell **−4.8 dB (was −18.9)**, `soft` **+12.1
dB** on last round. Voicing lowpass INSIDE the patch, two corners staged
for his ears (1100 Hz candidate / 2200 Hz alternative, median centroid
737 → 498 / 611 Hz).

**SECOND REAL FINDING: ignition threshold climbs steeply with register.**
Measured as SINGLE NOTES FROM SILENCE (a note rendered after another
inherits the bore's energy and starts on it — which is why attempt 1's top
notes looked fine in a line and would not start alone): everything to B5
ignites at 8 kPa, C6 12, F6 36, F#6 52, G6 72. PRESS_MAP now carries
per-note breath support at 1.6× each note's own threshold, FLOORED AT 1.0
so nothing that already worked moves. This is also the mechanism behind
Matt's "high C in soft struggles to lock in" — attempt 1's flat soft
pressure sat below the top notes' threshold. Cost, stated: the soft end is
limited by the top note, so soft/loud are 31/50 kPa, narrower than attempt
1's claimed 4× (that window had been measured on a line).

**TWO HYPOTHESES KILLED, both my own metric bugs — the corrected metrics
are reusable and live in gen_trombone2.residual / lock_time.** (1) "sustain
is non-harmonic at the bottom": artifact of a FIXED 16-harmonic comb, which
at 49 Hz only covers to 784 Hz. Whole-band comb → residual 0.02–0.05 at
every note. (2) "low notes take longer to become periodic than to become
loud": artifact of a comb tolerance narrower than the analysis window's own
Hann mainlobe (2 bins). Fixed, with a period-scaled window: the bottom
locks in 10–50 ms and the TOP is the slow one (C6 90 ms). Four onset levers
ablated (breath grain, lip kick, steepener, overshoot amount/duration) —
none moves anything; making the onset SLOWER kills the bottom three notes
outright, so "scale the onset in periods" is measurably the wrong
direction. 2-attempt rule invoked on "separate low-register onset defect".
ALSO BUILT, MEASURED, REJECTED: a keytracked voicing corner. It does
flatten brightness-relative-to-pitch (13.4×/1.9× → 4.1×/1.8×) and it takes
the register spread from 13.1 dB to **36.5 dB** by stripping the low notes
of the only band they have energy in. Fixed corner ships.

**KNOWN WEAK SPOT, unchanged and re-measured at real level:** blowing
harder barely changes timbre — centroid 473 → 477 Hz, energy >1 kHz
0.0194 → 0.0210, from 31 to 50 kPa. Backlog 74(a), still the next
measurement, suspects unchanged. New 74(b): G6 needing 72 kPa where
everything below B5 needs 8 is not what a real instrument does.
For the record against the word "bright": 95% of attempt 1's energy is
below 1571 Hz and 22% above 1 kHz, against 67% above 1 kHz for the library
oboe. Whatever Matt hears as brightness, it is not HF content by that
measure.
dsp REVIEW Awaiting: **70**, 62, 65, 38, 39 (69, 68, 67 resolved).

Previous update: 2026-09-17 (interactive, Fable 5 with Matt; ~8% Fable quota — both
heavy dispatches ran on Opus subagents per the quota pattern).
**NONLINEAR BORE FRONT OPENED (Matt: skip survey, go Msallam classic).**
(1) Literature recovered (Opus, fetch-then-convert):
docs/research/nonlinear_bore/MSALLAM_DIGEST.md — ICMC'97 algorithm paper FULL
(all equations; the faithful IRCAM element filters the DELAY signal with
self-referential feedback, integer p>1, Lagrange ≤2) + JASA'96 measurements
FULL; Acta Acustica 2000 unobtainable (metadata-only HAL deposit) so lip
params / bell coefficients / rates are tier-C gaps. Load-bearing physics:
steepening acts on the RADIATED sound, not the lip loop → out-of-loop
placement is sanctioned. (2) Spec (Fable):
docs/superpowers/specs/2026-09-17-nonlinear-bore-design.md — zero-code
probe, DelayLine ratio ← Curve(tap of own input), d = D·(1−depth·x), no
shock handling (C&A: omitting it sounds better). (3) Probe BUILT (Opus,
1a11064, REVIEW 66): mechanism works first try; wiring fact — modulator
must read via TAP not ref (ref = one sample early; null proves lag exactly
400); gates all green (depth-0 bit-identical −231 dB; THD monotonic 6-step
ladder; 9× input → 9.5× distortion = effect appears with level). SPEC
CORRECTION: sine → Bessel ladder (PM, β match to 3 digits), not sawtooth —
expected for the shock-omitted variant, probe is a PM-style brightener.
KEY FINDING: distortion ∝ source frequency (doubles/octave) → bites bright
material only (oboe centroid +633 Hz vs flute +37 Hz at same depth) →
**lip exciter outranks more bore work for round 2**. Ears queue:
renders/dsp/audition/nlbore_probe1/ (8 cells; money A/B =
oboe_soft_d030 vs oboe_loud_d030, centroid 1121 vs 1705 Hz). Round-2
options in spec §Round 2: faithful delay-signal node, in-loop variant,
Smyth&Scott measured bell filters, lip round on the loop family.
**Same day, rounds 2+3 (Opus dispatches 3+4):** Matt verdicted 66 within
the hour ("definitely brightens in a brassy way" — element KEPT, folded to
Resolved). Pairing round (13bb1b5/b0accd0, REVIEW 67): steepener onto the
memoryless-lip carriers, gates green (2.3× >2 kHz loud-vs-soft, pitch
unmoved ±1c, steepener now INSIDE the tube pre-bell), BUT built on the
known-inferior lip → Matt issued the **BEST-POSSIBLE-IN-ROUND-ONE rule**
(verbatim in WORKFLOW.md §Steal-first, bd2666f + ca96c24; also in Claude
memory): known targets get the full state-of-the-art assembly round one,
stealing included; iteration is for true experiments only; probes = prep.
Agent self-relabeled 67 as optional prep. Harness-notes correction: the
09-12 lip is NOT uniformly sharp — 34c flat C3 → 22c sharp C5. Open
oddity: STK carrier goes darker at weakest steepener settings (one cause
eliminated, real one unknown; no queued cell in the dip).
**ONE-MASS LIP ROUND (the real brass attempt; spec
2026-09-17-onemass-lip-design.md, commit 08f00d7, REVIEW 68): the
published dynamic lip IGNITED AND LOCKED, zero engine code.** Berjamin
arXiv:1511.04247 §3.1 Eqs. 43–49 + Table 2 fetched and transcribed
(digest's uncertain y-row CONFIRMED); r=√(mk)/4 ⇒ Q=4 exactly ⇒ constant-Q
keytracking licensed. Bernoulli √ ships real (CurveNode power knot a=1
p=0.5; only approx = one-sample tap lag for the fixed-point solve; flow
subgraph validated ≤0.07% vs Python closed form). Ignition/lock C3–C5
+18…+33c over a 16–46 kPa pressure window; brightness 2.1× >1 kHz with
pressure, steepener adds 2.6× on top independently. DESIGN FIND (bore not
lip): one-period loop ignites but plays wildly sharp — lip reflection
measured 0.38, loop lacks authority; THREE-period bore fixes it and the
lip ratio then SELECTS THE PARTIAL (3.03/3.28/4.13 at ratio 0.8/1.0/1.2)
= the register mechanism in existing nodes. Controls are decisive:
mass-deleted graph is SILENT at every pressure; memoryless reed on the
same bore sounds only the bore fundamental, ignition window 1 scan step
vs the lip's 16. Known + deferred: lip cells consistently sharp, ratio
~0.77 would center (timbre first per spec). Ears queue:
renders/dsp/audition/onemass_lip1/, 7 cells; money A/B =
lip_r080_loud_st1 vs reed_r080_loud_st1 (only difference: the valve has
mass). Opus-meter total for the day ~945k across 4 dispatches; Fable
coordination stayed light. dsp REVIEW Awaiting: **68**, 67 (optional
prep), 62, 65, 38, 39. Plucks/plates paused til next week (Matt); Matt on
catch-up/cleanup + GOALS refresh.

Previous update: 2026-09-16 (interactive morning + "go dsp" run, Fable 5 with Matt).
**Morning (interactive):** clarinet_att2 passage flutter root-caused —
NOT an engine bug: per-note reset proven thorough; ignition at short
prepared durations is marginal and noise-realization-sensitive, masked
everywhere by the fixed virgin RNG seed (backlog 72; gates/sweeps rate
ignition on one lucky seed — needs a notes-2+/varied-seed robustness
probe + an audibility floor). Matt's minSec-floor fix (both attacks
0.1) validated ~9-10/12 across random states; his UI zero-flutter =
Generate determinism (fresh instrument per gen = same seed). Matt's
envelope pass committed (72be940): trailing hold-at-zero stages
REMOVED from winds+viola + releases to 0.1 — they were stealing
duration (percent stages), the staccato culprit; manifest refrozen.
**"go dsp" run (verdicts 58/59 folded, 2768f90):** (1) BandedWG cutoff
click = the fixed 0.4 s tail cutting ringing bars → **adaptive
ring-out shipped (63b, 5dd07d7)**: voices ring to −60 dB or +8 s cap,
offline + live; loader now honors seconds > score end (was silently
overridden — ring headroom was inexpressible); two deliberate
refreezes, quiet-enders byte-identical both times; 4 verdicted
bandedwg pairs re-rendered click-free. (2) **Percussion round 1 →
REVIEW 60**: gen_bwg_perc1.py, F&R mode sets at 48k, 7/8 cells pass
(marimba/xylo/vibes/chime/tomdrum/woodblock/glass; tbar_bright
machine-culled). (3) **Donor papers → REVIEW 61** (backlog 73 done):
DAFx'04 brass NO-GO (linear bore; mouthpiece two-pole steal noted);
Chafe 2D-mesh GO with a zero-code 1D Pierce probe proposed first.
INCIDENT (repaired): worktree removal followed third_party junctions
and wiped the vendored libs; restored (rtmidi re-vendored 6.0.0),
remaining worktree junctions de-linked, builds green. Details:
dsp/reports/2026-09-16-dsp-run.md.
**Same-day round 2 (Matt verdicted 60/61 + BandedWG re-audition within
hours):** ring-out KEPT per his conditional (helps musical notes,
trivial cost); residual hard cutoff was the 8 s cap on near-lossless
resonators → cap now FADES 80 ms (offline+live; piano_seg the one
deliberate DIFF, refrozen). Perc ROUND 2 shipped (gen_bwg_perc2.py →
REVIEW 62): per-mode gains = constant-T60 frequency curves (round 1's
fixed gains made T60 ∝ period = gong-everything), measured calibration
/4, 7/7 pass, woodblock 0.13 s → chime 6.4 s. Bowed "cut before bloom"
explained: gen scripts bake bow-stop at 4.5 s (seconds-mode AdsrEnv),
not a physics threshold — next bowed round uses gated/hold envelopes.
NONLINEAR BORE GREENLIT ("I'm counting on you.. let's do it!"); 1D
Pierce probe approved. Passage strings gained REST tokens (R+duration,
dc4fce3, implemented by an Opus subagent — quota conservation pattern,
works). Session-start rule added to CLAUDE.md: sweep stale renamed UI
exes. dsp REVIEW Awaiting: 62, 38, 39.
NOTE: Matt's running UI predates today's builds — relaunch picks up
ring-out + cap fade + seconds-honor + rests live.
**Evening: MESH2D CAMPAIGN, both stages, via Opus subagents (quota
pattern, 2 dispatches, ~376k on Opus's meter, both verified by
coordinator).** Stage A (74c81c3): Mesh2DSource ported from STK —
positions/decay as pins, 64x64 cap (STK caps 12x12; extended ref class
proven sample-exact vs stock first), validated to exactly one 16-bit
LSB across 6 cases at 48k; null gate 79/79. Stage B (9ecdd82):
edgeMode setting (0 = STK verbatim default, byte-identical proven;
1 = Chafe allpass with edgeFc/edgeR PINS) → pie-pan/Pierce-gong as
pure patch wiring; 8-cell queue = REVIEW 64 (mode count 2-4 → 11-84,
mode-set downshift = Chafe's published detuning; bar_piepan 16.9x
upwelling; drive = safety knob, naive drive runs away — time-varying
allpass non-passive, global edgeR pumps coherently). Speaker incident
earlier folded: stale v1 pierce WAVs in queue → gen-scripts-own-dirs +
level-ceiling rules in WORKFLOW.md (59062f7). Pierce 1D probe =
REVIEW 63 (upwelling 2.35 at s8k). dsp REVIEW Awaiting: 62, 63, 64, 65,
38, 39. Matt verdicted 64 same evening (underwhelming near-identical
chimes; metrics measured -60 dB trivia — feature-audibility gate born
from it) → ROUND 2 same night (faf2cff, REVIEW 65): REAL Pierce/Van
Duyne passive filter recovered from patent 5,703,313 + Faust fi.apnl;
FINDING: the literal published recurrence is NOT passive in discrete
time (2.43x energy creation) — shipped form adds an energy-preserving
state rescale, measured 1.000000000, hard engine_tests gate; new
PierceFilter node + Mesh2D edgeMode 2, full drive, twin-controlled
cells with real audible spread (ΔhB to +95.7 pp), pierce1d_2 pluck
thread included. Homogeneity fact recorded (velocity can't change
timbre through a passive piecewise-linear spring). Next on verdicts: real instrument attempt on the winning
geometry/edge behavior; passive Pierce filter structure (the
principled fix for the pump); perc axis round; nonlinear bore front.

Previous update: 2026-09-14 (fall cleaning, Fable 5 with Matt). **Steering-meeting
execution DONE except GOALS refresh (Matt's) and the audition-dashboard
brainstorm (own session).** What landed (commits aa50c49, dda11f3):
- **Dirty-patch verdict: KEEP** — piano_default (curve renames + added
  Reverb) and viola_default (PerformNode re-save) are Matt's, committed;
  cello/oboe1 render-neutral churn committed with them. chords/ +
  passages/ (UI transport save areas) gitignored; stray hysteresis plan
  doc tracked.
- **BACKLOG nuke** — dsp 864→330 lines: 6 items to ledger, 13
  maybe-somedays to IDEAS.md (ledger keeps ids resolvable), survivors
  compressed to live threads; comp untouched (already tight) but gained
  item 23 = the steering comp-restart campaign. Open Matt questions 3d
  (pan law) and 22 (Vibrato envelopes) still parked — he skipped them.
- **WORKFLOW.md rewritten** for the single-dev run contract: personas
  retired, campaigns carry stop conditions, steal-first model + rate
  discipline, machine self-verdict REJECTS, ears budget ≤ 20/run,
  dashboard Keep→pending/<campaign>/ semantics, IDEAS housekeeping rule.
- **Baselines set-cover cull 116 → 42** (+9 prof_additive ladder): 65
  redundant patches → patches/old/baselines_culled_2026-09-14/; keepers =
  one per node type/loader feature + harness citations + repro patches.
  Additive1 culled (comp uses piano_default). FIND: Biquad/BowTable have
  ZERO tracked gate coverage (only gitignored sweep cells use them) —
  Matt: STK cells go in once they do something good.
- **Null gate refit**: skips library/voice vowels except sing_alto_A +
  speech_c_AE (identical mechanism, formant freqs only — Matt); manifest
  refrozen 196 → 77 entries, verified 77/77 (2 RENDER_FAILs = deliberate
  keepers FormantSequence1/NATest1). Gate wall-clock drops ~60%.
Ears queues UNCHANGED and standing: stk_port1, stk_bowed_ext1/2,
hysteresis_bow*, valve2, string_harness1/2, brass_harness1, r5b axis
tour, feedback_inloop1, + the whole comp REVIEW queue.
**Same-day addendum — Matt verdicted the whole dsp REVIEW queue (1 hr),
verdicts folded + executed:** inloop1 REJECTED (patches→old, renders
deleted; formants stay outside the loop); ext1 hysteresis cells rejected
harsh (→old; next hysteresis round needs ext2's leaky-stick fix);
ext_perf runaway vibrato root-caused (±0.27 summed into neck ratio) and
fixed canonically (±19c C4 measured), "most stringy" keeper; ext1
saxophony + all ext2 + fb5b cells KEPT in place for Matt's hand-play
(his call: heap, no copying to pending/). r5b thread closed, no neighbor
round. Decisions executed: **pan law → unity center** (mixer.cpp,
verified exactly sqrt2 x old, WAV==UI loudness; manifest refrozen 77/77,
UI relinked via rename — Matt was live in it); **Vibrato per-note pins
already work** via dynamicPins, render-proven, backlog 22 closed, no
code. IR pick DROPPED (rotten links) — STK round 3 proceeds from
Larson's measured LTAS curve instead (biquad-stack body fit, A/B vs
bachd.wav) = campaign item 70. **Round 3 DONE same evening:**
fit_larson_body.py (4-pair fine LTAS, 14-biquad fit, err 0.68 dB median
at the data's 2.8 dB spread) + gen_stk_bowed_ext3.py → 4/4 cells to
renders/dsp/audition/stk_bowed_ext3/ (nobody / Maestre / Larson body /
btd+Larson full recipe), all +0c locks, in-graph LTAS validated
(+0.83 dB median vs target) = **REVIEW 58**. AF sax still blocked on
Discord annoyance; offered alternative: green-light building from the
video table.
**09-15: STK round 4 — remaining families ported (Matt: "move on to
remaining families").** Saxofony/BlowHole/BandedWG reference drivers
built (tools/stk_ref/), ground truth at native 22050, ports validated
per rate discipline: saxofony ±1c all locks, noise-free 0.04 dB C4;
blowhole C3-C5 locks (per-note comp anchors; model is register-BISTABLE
at 22050 and the ref flips against itself run-to-run — parity); bandedwg
struck bars/bowls with EXACTLY matching decay rates + bowed bars
blooming ~4 s like the model (bar_bowed not ported — ref silent).
Ears queue = **REVIEW 59** (stk_port2, 7 A/B pairs). En-route engine
finds: **backlog 71** — a shared stateless node in loop context
double-advances the stateful chain behind it (env at exactly 2x; repro
pair patches/baselines/double_advance_{ok,bug}.json; minimal case IS
guarded — needs the advance-list context; 65/66 cluster); 1-sample
seconds-mode envelope stages never fire (2+ do); engine BowTable folds
dv* into its output. Manifest refrozen 79 entries with the repro pair.
dsp REVIEW Awaiting: 59 + 58 + 38 + 39. Campaign next on verdicts:
extension rounds for surviving families, leaky-stick hysteresis round,
48k canonicalization, or the tone-shaping filter round.
**09-15 fronts 2+3 (same run): backlog 67 + 63 SHIPPED.** 67: loader
warns once per (type,key) on unknown param keys — allowlist
corpus-verified (133 patches warning-free; all 16 flagged classes
proved branch-consumed, none dead; positive control fires; gate 79/79
byte-identical). 63: voice tail allowance kVoiceTailSec=0.4 s —
voices render/live past duration so in-voice reverb/filters ring out
(piano_default tail now ends at -66 dB, zero step; containment check
moved to tail end; live path inherits via StreamingVoice.durSamples;
15/79 gate patches ring past duration, manifest refrozen). The
trailing hold-at-zero-stage workaround is obsolete. Run stops here
per contract (3 fronts): everything else dsp is verdict-gated.

Previous update: 2026-09-13 evening (steering meeting + STK port day, Fable 5).
**STEERING MEETING HELD — process decisions, execution deferred by Matt
except the STK port directive.** Decisions (capture only, action items
NOT started): (1) Dipsy/Wolfie personas retired; ONE dev runs both
lanes; lanes survive as backlog namespaces, campaigns = big backlog
items. (2) Run contract: machine gates may self-verdict REJECTS
(logged, patches to old/, renders deleted) + near-duplicate culling by
perceptual distance; ears budget <= 20/run honest count; Keep on the
future dashboard writes to pending/<campaign>/ (Matt's live-play bench;
runs still never write pending/ — the dashboard writes as Matt's
click); library promote from bench stays Matt-only; stop conditions
live in each campaign item. (3) "Audition dashboard" = own brainstorm
session later (queue aware, play/A-B/verdict buttons writing structured
verdict files → future Matt-likelihood ranker). (4) Comp restart: back
to Mary-Had-a-Little-Lamb scale WITH harmony from bar one,
piano_default as the comp instrument (vanilla, non-fatiguing). (5)
DSP model = STEAL-FIRST: vendor proven code, render its WAVs as ground
truth, port into the graph, extend from the proven point; ML-ears and
comp-scorer get cheap discrimination backtests before trust. (6)
Cleanup to come, collaborative and brutal: BACKLOG nuke = dead/done/
moot items die, "maybe someday" moves to IDEAS (NOT keep-only-remembered);
baselines cull 188 -> ~40 by node/feature coverage (set-cover proposal
from me, axe swung by Matt). (7) Null gate measured today: 290 s at
--jobs (parallelized 09-05); batch-mode mforce_cli remains the lever
(95% spawn/load). Gate found the 09-08 mystery edits CHANGE AUDIO:
piano_default + viola_default DIFF vs manifest (cello edit is
render-neutral); manifest also stale (8 NEW). Deferred with the rest.
**STK PORT CAMPAIGN — 4 families ported and validated in one run (Matt's
directive before leaving).** STK checkout at ../stk; minimal reference
drivers tools/stk_ref/ (bowed/clarinet/flute/brass, 48 kHz, no RtAudio);
ground truth in renders/scratch/stk_ref/ (+ JOS Mohonk05 14-WAV bowed
set archived there). Engine: NEW nodes **Biquad** (raw coefficients +
resonance mode with frequency/radius pins) and **BowTable** (Smith
friction curve, slope/offset pins) — registry-only, null gate 186/188
(same 2 pre-existing dirty-file DIFFs as the morning run, engine
byte-neutral). Ports: patches/sweep/stk_{bowed,clarinet,flute,brass}_port/
via tools/gen_stk_*.py, each auto-compared to reference (f0 cents,
envelope corr, gain fit, harmonic profile). RESULTS: **bowed +0.0c on
43/43 locked slots incl. reproducing STK's own C6 multiphonic and
wolves; clarinet +0.0c all 40, noise-free variant 0.09 dB exact; flute
noise-free exact <= 0.11 dB; brass reproduces the reference INCLUDING
its silences (STK brass barely speaks at 48 kHz — corroborates the
native harness ignition-window finding).** Noisy-variant deviations =
noise realization (STK seeds from clock), documented. Roundtrip 33/33
clean after adding noteFaces (UI save consolidates faceless
PerformNodes — same known class as the wiring_smoke find). Full record:
docs/research/stk_port/STK_PORT_NOTES.md; spec
docs/superpowers/specs/2026-09-13-stk-bowed-port-design.md. EARS QUEUE:
renders/dsp/audition/stk_port1/ = 4 port-vs-REF A/B pairs + README
(question: does the port sound like the reference?). NOT COMMITTED —
awaiting Matt (working tree also still carries the deferred dirty-file
question). Next when Matt returns: his ears on stk_port1, then the
extension round (compensate ON, hysteresis-junction swap-in, drive
exploration past STK's ranges) and/or more families (Saxofony,
BlowHole, BandedWG, Mohonk torsion/dispersion features).

**EVENING ADDENDUM — Matt's ear caught a bogus baseline; fixed.** The
Maestre body coefficients (and other fixed numbers in STK) are designed
at STK's NATIVE 22050 Hz; verbatim at 48k the body resonances shift
2.18x up (261/525/1072/1287/1839 -> 569..4004 Hz). Fixes: drivers take
a rate arg (22050 canonical refs rendered); gen_stk_bowed.py default =
**body48** re-realized sections (z -> z^(22050/48000); matches native
body to 0.46 dB max 100-4000 Hz; --verbatim keeps the STK-null form);
audition pair restaged = body48 port vs STK@22050. Measured: STK brass
at 22050 SPEAKS at C4/C5 (48k silence was partly rate artifact). Queued:
brass lip-radius + clarinet OneZero rate corrections. Port lesson:
every fixed constant must be sorted physical-Hz vs normalized-frequency.
**Round 2 of Matt's ears → CANONICAL bowed mode** (gen_stk_bowed default;
--verbatim = old form): his two complaints measured+fixed — (a) "high
harmonic drowns fundamental" = STK@48k itself double-slips (H2 +31..46dB
vs 22050's Helmholtz +18.6; per-sample ADSR rates halve gestures at 48k
+ string filter under-darkens) → canon re-realizes the 22050 model at
48k (pole 0.7600, 22050-second gestures, f-calibrated comp table, 2
iterations); (b) clicks = literal-seconds envelope truncating short
Passage notes + voice cut mid-ring at note end → duration-adaptive
pinned stages + LoopGate 90ms loop-closure on NeckDelay read-gain
(MForce-native; STK truncates too). Canon vs 22050 ref: C3-C5 ±5c,
envCorr ≥.98, C3/C4 harmonics match ~1dB/harmonic, click gone (0.018
residual = attack transient). C6/C7 fragile in ref too. Roundtrip 24/24.
bowed_port.wav in audition = default_canon.
**Round 4 (Matt away): CANONICAL PASS ALL FAMILIES + BOWED EXTENSIONS.**
Bowed verdicted by Matt ("port matches REF"). Clarinet canon VALIDATED
(±2c C3-C6 all variants; 48k biquad refit of the OneZero, zero pinned
11025; parabolic-autocorr f0 added after catching integer-lag
calibration chasing). Flute canon VALIDATED zero-calibration (±1c
C3-C6). Brass canon PARTIAL, root cause PROVEN by experiment: lip DC
gain at 48k = 2.13x native (rate ratio) even peak-preserved; unblocked
mouth DC times the ignition window (C4 speaks with DC-preserving b0);
clean fix = keytracked DC-zero in Biquad resonance mode — parked, brass
superseded by junction roadmap. EXTENSIONS ROUND 1: 13 zero-code cells
on the canonical base retuned to EQUAL TEMPERAMENT (port inherits STK's
own +7/+25/+49c sharpness; extensions don't — control = +0c). 12/13
pass gates+dedup → renders/dsp/audition/stk_bowed_ext1/ (README inside).
HEADLINE: **hysteresis stick/slip junction inside the proven chassis
locks ±7c across C3-C5** — the in-tune multi-octave lock the native
harness never achieved; the chassis was the missing half. Also: torsion
coupling (passes, +5-16c pull), dispersion allpass (re-comped, clean),
bow-bite pressure gesture (live pin, STK can't), body A/B/ablation.
Roundtrip 59/59. EARS QUEUES: stk_port1/ (4 families, canon pairs) +
stk_bowed_ext1/ (12 cells). Nothing committed all day — Matt hasn't
asked; tree carries engine nodes + tools + patches + docs.
**Round 5 (Matt verdicted round 4, approved round-2 plan): PROVENANCE +
BRASS FIX + EXT2.** Matt's verdicts: families match refs; STK skeletons
underwhelm everywhere (flute rubbish both sides, clarinet loses to oboe
harness, gold strike elusive); brass UN-PARKED ("we have no brass at
all"). Mohonk WAVs identified = Peder Larson Music 421 project 2003
(writeup archived in docs/research/stk_port/ + bach/staccato WAVs);
code unpublished → exact WAV reproduction impossible (his cello-body IR
+ bridge coeffs + gestures are lost), mechanism-exact fully possible
(all constants documented). New rate discipline: validate ports AT
native rate; canonicalize separately. Brass canon COMPLETED patch-level:
fixed 12.5 Hz low-shelf (DC 0.461) before the lip — DC excess is
constant 2.17x across notes — ignition map now matches ref (C4/C5
speak); +29c C4 lock offset left for trim. EXT2 (cello register, from
Larson's numbers): 5/7 staged → renders/dsp/audition/stk_bowed_ext2/ —
control2 +0c, width_h2 (his #1 feature), tors_phys (Z/Zt=0.306), disp16
(D-string range by design), larson_btd (b+t+d, +25c retunable). Killed:
2 hyperbolic cells, diagnosed (stick must leak ~2%, plateau must be
continuous) → own round. Body deferred: Matt ear-picks a public cello
IR (links in ext2 README), then biquad-stack fit. Still nothing
committed.
**Late 09-13 addendum:** QWERTY lower zone fixed to ONE octave down
(was two; tools/mforce_ui/main.cpp s_qwertyMap; zones now overlap a
fifth, shared keys show both hints "Q ,"); UI rebuilt, uncommitted with
the rest. Also standing offer accepted in principle: late-night ideas
get dictated and filed to IDEAS.md dated (runs read, never act).
**NEXT SESSION = steering-meeting EXECUTION (Matt, end of 09-13):**
workflow retooling, housekeeping, cleanup — the deferred action items
from the morning meeting (WORKFLOW.md rewrite around the single-dev run
contract, queue bankruptcy w/ BACKLOG→IDEAS rule, baselines coverage
cull to ~40, null-gate refreeze after the piano/viola verdict, GOALS
refresh is Matt's). STK campaign TABLED mid-thread — round-3 entry
point when resumed: fit Larson's body filter (measured curve in
STK_PORT_NOTES; 3-pair LTAS ratio: +20 dB @150 Hz block, −10 @500,
+15/+11 @800-1200, −22..−30 above 2k) as a biquad stack on the chassis,
A/B vs bachd.wav.
**Evening verdicts (Matt), session ends here:** brass canon = "roughly
the same, only C4 actually producing a note" — STK-brass chapter closed
as parity-with-a-poor-reference. Key insight recorded: "brassy" needs
NONLINEAR bore propagation (wave steepening at forte); linear bore +
valve = "saxy not brassy" — which retro-explains the native brass
harness verdict exactly. Lightweight surrogate = amplitude-dependent
waveshaping along the bore (Shaper + drive ← level follower is the
native idiom) — required ingredient for EITHER brass path (donor
survey: OpenWind/MoReeSC/NESS lineage; or native lip on the loop
family). Larger context Matt named: the well-done physical models
(VL1, SWAM, Arturia) are closed; the open ones are skeletons — the
campaign's real yield is the validated chassis + the documented list of
missing ingredients (nonlinear bore, real body IR, finite bow width),
not finished instruments. Ears queues stand: stk_port1, stk_bowed_ext1,
stk_bowed_ext2 (+ hysteresis_bow/valve2/string_harness backlogs).
Nothing committed 09-13; meeting action items still deferred.
**Round 3: legato mechanism verified** — STK slurs = setFrequency on the
sounding string (bow held, no reclear): rise times 0.36/0.03/0/0/0 s vs
detached 0.36/0.14/0.04/0.04/0. Ours re-ignites every note (fresh voice,
state cleared) → staccato 8ths. Graph is already legato-capable (per-
sample frequency pin retunes delays); the gap is note delivery (voice
reuse without reset) = Slide/HammerOn/PullOff articulation family →
backlog 68 brainstorm input, backlog 53 mono-mode twin. NOT built.
bowed_REF_legato.wav staged. Full analysis in STK_PORT_NOTES §LEGATO.

Previous update: 2026-09-13 (render-unification day, Fable 5 with Matt; session
restarted clean after this update — pick up from here; next sesh =
steering committee meeting).
**09-13 digest — tap-loop UI bugs root-caused, then the two-renderer
Generate deleted at Matt's direction.** Morning: Matt's harness live-play
anomalies traced (systematic-debugging): (1) collect_envelopes couldn't
see through RefSource (taps + secondary-consumer wraps expose no
descriptors) → string loop-gain env (Ampl_env on tap-only NutDelay)
stayed ungated → key-up ignored, notes = spinner length (brass gated
fine — Mouth is walkable; Matt confirmed the split). (2) Passage Play
re-rendered via the UI-graph pass (no advanceList → tap tails frozen →
flat waveform + silence + re-render delay). Fixes: walk follows
RefSource::source; Play = dumb replay of last Generate (no staleness —
generate/tweak/Play is Matt's A/B workflow). Then the big one
(spec+plan 2026-09-13-render-capture-unification): generate_unified =
serialize editor graph → load_instrument_patch_json (no temp file) →
play_note with per-node CAPTURE (engine sums each clone's current()
at timeline offsets, legacy addValueListener reborn) → strips + play
buffer from ONE engine render. render_waveforms/passage/both
*_authoritative DELETED (−211 lines); chords/cache/--dump-playback on
the in-memory loader. Proof: UI output ∝ CLI at exactly 1/√2 pan law
(worst 2.5 LSB, 3 families); NutDelay strip live (was silently flat);
null gate 80/80 hash-identical; --gatecheck + --gencheck are the
standing harnesses. Matt's morning verdict: "works great." He deleted
oboe1's orphan Morph_env (found by the truthful strips). STRING VERDICT
(post-fix, 09-13, full log in STRING_HARNESS_NOTES.md §Round 2 ears):
multi-attack GONE except C5/C6 on some cells (possibly BEATING not
retrigger); attack+sustain harsh/saturated "distorted guitar played by
a bow"; ALL cells sound TWO OCTAVES per note (near-equal weight at
4.0x gain); 32/36/05 = reference cell (stability winner + only clean
C6); b48 column confirmed dead (thump-then-sine / breath). Measure
next: f0-vs-2f0 spectrum across gain band, C6 beat-rate vs detune;
drive-onset decoupled from loop gain stays the saturation lever. BRASS
prelim (pre-fix): "very saxy not brassy," squeaky/out-of-tune high,
buzzy vowel low — maps to documented +22-38c sharp, C6+ non-lock,
spectral-balance gap; per-note pressure-window map (probe was C4-only)
is the offered zero-code next step. No promotions; queues in place
pending the steering meeting. Backlogs 65/67/68 unchanged.

Previous update: 2026-09-12 (harness-campaign day, Fable 5 with Matt; session
restarted clean after this update — pick up from here).
**09-12 digest — string + brass harnesses, specced/built/measured in one
day (Matt's directive: state-of-the-art first, sweep, ML ears).** Both
built with ZERO new engine code. STRING (waveguide two-segment, native;
specs 09-11 + addendum; tools/gen_string_harness1.py): IN TUNE ±22c
across C2..C6 all bow positions with capture 0.25; build lessons —
compensate-on-NUT = exact two-delay tuning, cycle NEEDS dcblock (bias
DC → 45 Hz relax "note"), output post-DC. Round 2 drive sweep
(string_harness2): Matt's flutter diagnosis confirmed numerically
(1.2x crit = 98% envelope dips); GAIN is the flutter knob, sweet spot
3.2x crit → flutter 9%, pitch +5.7c iqr 0, ML ears 1.075 = campaign
best (additive-viola lineage started at 1.28). Start cell
str2_g32_b36_h05. BRASS (outward-striking lip = morph-pin N=2 slice;
spec 09-12; tools/gen_brass_harness1.py): pressure must enter POST-cup;
ignition is a WINDOW (linear scan, not bisection); lock probe
pitch-gated; locked register C3-C5 (t100 column), ML 1.65-1.9, flutter
<2% but +22-38c static sharp = intonation trim + spectral-balance
knobs open (blend test moved total but worsened harm — stopped per
no-blind-tweaks). Docs: {STRING,BRASS}_HARNESS_NOTES.md (component
notes, reed-diff tables, hand-tweak guides);
tools/measure_stability.py = new flutter/pitch metrics in the
pipeline; trumpet ML config+reference built (ml_ears/configs/
trumpet_bb.json). Matt's round-1 ears: "far from good but extremely
promising - moments of stringiness, one amazing moment of saxiness";
he's playing string round-1 winner + brass q15_m3 by hand. Queues
AWAITING EARS: string_harness1/2, brass_harness1 (+ hysteresis_bow*
and valve2 verdicts still open). Backlogs: 65 (root sensitivity, +
1-sample ordering residual) OPEN; 67 (loader unknown-key warning)
OPEN; 68 brainstorm pending (weekend meeting postponed). Prior 09-11:
duration perform field + Live checkbox landed (backlog 64 Tier 1);
transport tab rework + passage/chords save-load; bow rounds 1-5
(period-1 capture rule) — see 09-11 commits and hysteresis_bow*
READMEs.

Previous update: 2026-09-08 (marathon interactive session, Fable 5 with Matt; he
may be away ~4 days — Fable usage limit — check powered-by at session
start per the model-switch protocol).
**09-08 session digest — the junction-state day.** Library went NUMBERED
(oboe1/2, flute1, bassoon1 replace _defaults; f330497). Brass probes:
series valve (round 1) and drive-coupled valve (rounds 2-4, valve2) both
FAILED to leave the reed basin — ceiling named: memoryless single-input
junction = reed family, Matt accepted ("we can make any reed you can
imagine"); verdict log docs/research/feedback_sweeps/VALVE_VERDICTS.md.
Two engine bugs found with repro pairs and FIXED: backlog 66 compensation
walk (param-pin descent + member-cap eviction; walk now inputs-only, cap
16; 4c34acf) — 65 (root-sensitivity, ±1-sample ordering ambiguity)
remains OPEN with baselines. Backlog 67 NEW: loader should warn on
unknown keys (would make old-binary/new-patch mismatches self-announcing; NOTE: the 09-08 confusion it was filed under turned out NOT to be a stale binary - Matt was on a fresh launch and the feature is just invisible by design).
**HYSTERESIS JUNCTION SHIPPED same day as spec** (specs 2026-09-08-
hysteresis-junction + junction-2d; plan + tasks f556358/3586751/dc8dd21/
48c70fa): Shaper stick/slip mode, breakaway/capture pins, Stick/Slip
editor labels, null gate 195 clean, open-loop state-machine test. Bow
round 1 = double-slip buzz (no bow); round 2 added BOW-VELOCITY BIAS
(Bow_bias env + Bow_sum; drive PINNED 1.0 — upstream bias gets
drive-scaled into clamps; loop gain via Ampl_env maxValue) → measured
mode-locking chain (sub-octave period-2 cycle → damp x3 → sawtooth
corners → **delay ratio 0.5 lands it ON PITCH: peak/f0 0.994 C3..C7,
1.97 corners/period**, first bowed-mechanism oscillation at pitch;
a3b4ef7). Matt's verdict on those cells: "syn-drum attached to an oboe"
— mechanism right, character wrong; suspects logged in
audition/hysteresis_bow2/README (bias-step percussion, slow bloom,
wind front end). NEXT: bow round 3 from those suspects; 2D junction
gate = zero-code morph-pin probe (spec §2), Matt-gated. Awaiting Matt:
both bow queues + valve2 queue verdicts. Loose end: piano_default /
cello_full_range / viola_default modified in worktree, Matt says not
his (recent) doing — uncommitted, uninvestigated. CLAUDE.md gained the
no-wrap-ups hard rule (ab21d1d).

Previous update: 2026-09-07 (marathon interactive session, Fable 5 with Matt).
**09-07 session digest** (details in the docs cited): UI shipped
copy/paste (OS clipboard, cross-instance; commit 7a1b6ac), plus fixes:
group out-pin killed by Note-face refs, GUI crash from corrupt __output
positions written by save-while-drilled (both 9f15efb), auto-listen on
drill-in REMOVED (cf04155 — the ears now stay on the patch; the tap
surprise cost a day of confusion). oboe_grouped promoted to
library/winds/oboe_default (9d04b64, wormhole install completed).
Reference-match sessions: CLARINET (kSfEDb1cMAw, D maj) and BASSOON
(_t2q0lsUl4k, D maj 8vb) — full records + PLAIN-LANGUAGE LESSONS in
docs/research/feedback_sweeps/{CLARINET,BASSOON}_REF_ANALYSIS.md and
LOOP_PATCH_ANATOMY.md (the proto-doc for Matt's lane MDs). Headline
lessons: WAV peak exactly 0.70 = pinned at the soft-clip (0.999 x pan
0.7071) — volume is the un-distort knob; low-register damp choke
masquerades as slow attack (knee near a=6-7); the fundamental MASKS loop
hash — boost the honk, never strip to it; metrics find addresses, ears
pick furniture (Matt's hand patches beat every optimized cell). Matt's
verdicted keepers: clarinet c8_fast_breath_tweaked +
c8_h130_v25_tweaked, damp res ~0.66-0.67 = HIS overblow fix
(pending/clarinets/); bassoon_attempt still champion, he'll "honk it up"
by hand (honk address: ~500-540 Hz formant hump, ref spectrum peaks at
H3). Backlog 57-64 added (toggle A/B node, Crackle revisit, live-click
incident, save backups, settings audit + housekeeping sub-lane, noiseBed
scoping, voice tail allowance, duration-aware articulation two-tier).
Unreproduced gremlin watch: two knot-save losses + density=2000
reverts — capture drill in backlog 59.
STATUS and both BACKLOGs compacted: done items are now one-line ledgers in
the lane backlogs; run-by-run history lives in the lane `reports/` and in
git history of these files. Stale gates fixed in the same pass (dsp 15's
dead CombineTest ask, 12's folded verdict, 16→3r supersession, 27+3q merge).
**GOALS.md awaits Matt's refresh** — the July goals are largely satisfied,
and the current center of gravity (PerformSource/pin model, AF archaeology,
KS piano family, live MIDI) grew out of interactive sessions, not GOALS.

| Lane | Dev | State | Next up | Awaiting Matt |
|---|---|---|---|---|
| dsp | Dipsy | **09-02: r4 VERDICTED — "nailed it, concept proven."** First feedback-loop patch in library: **flute002 → library/winds/flute_default.json** ("killer, makes Clarinet1 sound fake"-class integrated attacks); reed001a library-grade, awaiting a name (oboe?). Winds > brass > strings (no bow attack yet). Full record: docs/research/feedback_sweeps/R4_VERDICTS.md — incl. code-verified physics answers (voice state IS cleared per note; noise RNG free-runs → real per-hit non-determinism). Cross-cutting gap: noise amp + drive ramp not keytracked (cutoff is) → patches only characteristic in home register **r5a RUN same day**: keytrack round — 6 keepers × 3×3 (noise-amp exp × ramp-time exp), 54/54 rendered to renders/dsp/pending/feedback_curves5a/ (README inside; n00_r00 = control). New render format per Matt: 5 notes C3..C7, 2 s each. Axes verified: pre-bloom hiss 56x C3→C7 flattened by n80; attack 0.38s→0.04s across board at r70. Mechanisms: hiss.amplitude ← power-form CurveNode; driveenv timeScale ← dynamicPins per-note setting **5a verdicted 09-03** (noise kt keeper at -0.80..-0.95, ramp curve dropped) → **r5b RUN 09-03**: reed001a deep-dive 6×4×6 (cutoff mult × drive shape incl. overshoot/tongue × junction jitter ±15%), criticals re-measured per cell, 144/144 rendered **09-05: CURVE-MORPH SHIPPED same day as spec** (docs/superpowers/specs/2026-09-05-curve-morph-design.md + plan): shared Curve evaluator (CurveNode+Shaper delegate, byte-identical), per-segment interp overrides (Envelope vocab + Hold; Stage holdPct REMOVED — scanned unused), Shaper editor gestures (right-drag segment = Expo+power, power dots, presets bow/reed1/reed2/lip/jet/hard/sine/saw/triangle), point-space morph pin + A/B editor workflow. Demo: renders/dsp/audition/curve_morph/oboe_morph_demo.wav (origin-steepened curve B morphed in over each note). Gates parallelized (null gate + roundtrip, --jobs; ~3x) — next gate lever: mforce_cli batch mode (render is 378x realtime; 95% of gate cost is process spawn+load). Pre-existing find: UI roundtrip drops wiring_smoke's __perf_freq (Matt spun off fix task). **09-06: WORMHOLE node pairs replace the net-label tag experiment** (tags fully removed; commits e3780bb..4f4bd82): pure-glass pass-through type, pairing BY WIRING (hidden span; hover/select ghosts it, double-click jumps), right-click a wire = route through a pair, spans exempt from group interface, rename fixes for tap refs. Byte-identical proof: oboe tap through a pair == stock render. OPEN THREADS for next session: (1) face-to-face fallback wire for boundary-invisible edges (proposed, NO go yet — oboe_grouped's tap edge is invisible at every drill level until wormholed); (2) DONE 09-06 later session: small Wormhole mini-faces + backlog 55 tap-pin removal SHIPPED (Matt approved mini for wormholes — the earlier veto was for boundary terminals); drilled-in boundary pin-stubs SUPERSEDED by the new **Replace-with** node-context gesture (swap type keeping wiring/state by name; headless --replace mode; backlog 57 = toggle-node A/B wishlist from same discussion); (3) backlog 56 id migration (Matt-decided, post-lanes, uniqueness check drops); (4) Matt's lane MDs still unwritten — the original plan this week. **09-04 session: junction mechanism ground-truthed → docs/research/feedback_sweeps/JUNCTION_OPERATING_POINT.md** — r5c should use directed axes (origin-asymmetry ratio + drive floor relative to measured oscillation threshold), not breakpoint jitter; ear-validated on patches/pending/oboes/oboe_dv_junc_tweaked.json (Matt's sandbox, awaiting his name — nothing supersedes oboe_default) | Matt's r5b axis-tour verdict, then pull neighbors from the full grid; apply winning shape/mult to other keepers. Backlog 53 (live mono mode) parked. Remaining 37a: in-loop breath (d). 34a; excite 46-48 | **feedback_curves5b axis tour 14 cells = the live queue** (full 144 in sweep/); amp_release/tuned/keytrack demos. **09-05 NEW: renders/dsp/audition/feedback_inloop1/ — 10 cells + control, oboe_default formants moved IN-loop (placement × res, normalized, loop-referenced criticals; README inside, tool: tools/gen_feedback_inloop1.py).** 09-03: **oboe_default promoted** (Matt's build-out of the r5b reed line: formant bandpass bell, Vibrato, Reverb — second feedback-loop patch in library/winds) |
| comp | Wolfie | **09-05: run 27 (interactive) — #22 done.** The three purge-casualty A/Bs (REVIEW 11/13/17) re-rendered at HEAD, same seeds, faithful to the originals → renders/comp/audition/{markov_phrases5,markov_phrases6,passage_end_grid}. Additive1.json rescued from patches/old/ → baselines/; 7 comp harnesses repointed | #21 zero-event check → #8 re-observe | **10 items, ALL verdictable** — the whole comp/REVIEW.md queue awaits ears; nothing blocked |

## dsp

Landed 2026-08-21 (Opus 4.8, MISC.md list): WhiteNoise density/boost/
continuity/zeroCrossTendency restored; Triangle `power` (symmetric, old
shark-fin behind `asymmetric` flag); UI piano dynamic octaves + trailing
top C. All byte-identical at defaults, null gate 196/196.
Before that: **PerformSource P1–P3 all landed** 08-18..20 (paramMap story
is now graph nodes engine+editor; gold pins, Note/Curve nodes, instrument
cache, RtMidi, wheel/pressure smoothers), plus Matt's hands-on day 08-20
and the AF archaeology thread (afp31 v1–v12 + decoded ground truth, afks,
sax recipe).

Review queue: dsp/REVIEW.md REBUILT 2026-09-14 — Awaiting is now (top
first) 53 stk_bowed_ext1, 54 stk_bowed_ext2 + cello-IR pick, 55
feedback_curves5b tour, 56 feedback_inloop1, 38 AF sax [discuss], 39
wheel [parked hardware], + one parked excitation stub. Everything else
folded to Resolved stubs there.

## comp

Run 26 (08-10) landed the phrase-aware cadence (composer/realizer cursor
disagreement fixed, arrivals 1/4→4/4), `cadentialArrival: approach`, and
revived six templates that had been rendering silence. Run 27 (09-05,
interactive) closed #22: the three purge-casualty A/Bs re-rendered.

Review queue (comp/REVIEW.md), now fully verdictable: 18 cadential arrival
[listen], 16 section key [listen], 9 Bruckner v2 [listen], 10 pedal_chords
[listen], 12 voicing_ab [listen], 14 passage-mode [read], 19 octave naming
[decide] — all under renders/comp/pending/ — plus the run-27 re-renders
under renders/comp/audition/: 11 literal repeats [listen], 13 phrase
endings [listen], 17 passage endings [listen].

## Standing notes

- The dsp and comp *scheduled* runs have collided twice in this shared
  working copy (runs 21, 25); staggering the two schedules is still open.
- **patches/library/ curated by Matt 2026-08-22 (bdc390d): 74 files, final
  patches only from here on.** KS piano baseline is now
  keys/acoustic_piano/piano_default.json (Piano_bright retired); clarinet_*,
  fm rhodes, v6 strings, percussion kick/snare are gone. rt_smoke cases
  repointed. Tools/docs that name the old files are historical.
- **patches/audition/ is the listening queue (renamed from pending/ 2026-08-22,
  gitignored); patches/pending/ is now Matt's sandbox — runs never write there.**
- **renders/{dsp,comp}/audition/ follows the same rename (Matt 2026-09-03): future
  round queues go to audition/, renders/*/pending/ = Matt's hand-work. The
  feedback_curves5a/5b queues predate the rename and stay in dsp/pending/.**
- patches/old/ is a PERMANENT archive — never purged (Matt 2026-09-05; the
  former 30-day nightly purge is rescinded, removed from both run prompts).
- Misc task drops: Matt can leave a dated list in a lane's MISC.md
  (pattern established 08-21); see WORKFLOW.md.
- Undecided feature ideas live in IDEAS.md (new 08-22), not the backlogs;
  runs read it, never act on it.

How this works: [WORKFLOW.md](WORKFLOW.md) · reports: dsp/reports/ ·
comp/reports/ · latest: dsp/reports/2026-08-30-dipsy-ui-round-ksbow-shape-editor-spec.md,
comp/reports/2026-08-10-wolfie-run26.md
