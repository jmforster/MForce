# Status — open this file first

Updated: 2026-09-13 (render-unification day, Fable 5 with Matt; session
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

Review queue (dsp/REVIEW.md): **51 Listen-here fixed [try]**, **50 bow
family 30 cells [listen]**, 49 excite4 duck/smooth/Helmholtz [listen —
partially verdicted, + 7 follow-up cells unheard: jin ladder, hot attacks],
48 triptych [listen, partial], 47 sustained probe [listen, verdicted-ish],
46 excite1 [listen — interim only; piano_ab A/B + segment_sweep3 combo
verdicts still open], 41 piano [try], 40 Triangle [listen], 39 wheel [try],
35-38 AF items. **The day's full narrative + all verdicts:
docs/research/oneshot_sweep/ROUND1_VERDICTS.md** (bow discovery, parameter
notes, excite rounds). Bow patch: patches/baselines/
bow_evolution_discovery.json (byte-copy of Matt's pending/ save).

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
