# Status — session handoff (Claude reads this first; Matt's queue = */REVIEW.md)

Updated: 2026-09-18 (dsp, trombone round; Opus 5 dispatch).
**TROMBONE ATTEMPT 1 SHIPPED — REVIEW 69, backlog 74 opened as the brass
campaign item. Zero engine code.** Matt verdicted 68 ("attack indeed
resembles a brass attack… definitely not saxy") with four defects; this is
the full KNOWN-TARGET assembly answering all four plus the bell.
Artifacts: tools/gen_trombone1.py + tools/bell_smyth.py, candidate
patches/audition/trombone1/trombone_attempt1.json (instrument-style, 39
nodes, noteFaces + positions, QWERTY-playable), 8 ears cells in
renders/dsp/audition/trombone1/, report
reports/2026-09-18-trombone1.md, bell curves+coefficients in
docs/research/nonlinear_bore/smyth_bell_fit.json.
**BELL: Smyth & Scott 2011 (EURASIP 151436) FETCHED in full** — but it
publishes NO numeric filter (Figs 8/13/16 are raster plots, and ref [12]'s
RL/TL expressions are paywalled), so what ships is the paper's own model
solved on its own measured Bessel-horn geometry (Table 1, which
self-validates: Eq. 21 reproduces its own stated radii to 3 digits).
Three labelled substitutions, all measured not asserted: piecewise-
cylindrical transfer matrix at 400 slices (0.27 dB from a 2000-slice
reference) instead of Eqs. 22–25's conical scattering; flanged-piston
termination instead of [12]'s L&S (median 0.42 dB difference); and
radiated-POWER rather than on-axis pressure for the transmission, because
the on-axis reading is a mic inside the bell and rendered at centroid
2150 Hz vs the power reading's 760. Independent cross-check: solved
reflection is −3 dB at 662 Hz against ICMC'97's ~800 Hz bell-reflection
bandwidth, different group, different measurement.
**TWO DEFECTS WERE MISDIAGNOSED BY ROUND 68, both corrected by
measurement.** (1) The sharpness is NOT the lip — it grows with frequency,
the signature of the delay's linear-interpolated short read; the lip ratio
moves pitch only ~170 c/unit against a ~0.10-wide speaking window, so
chasing cents with it walks off the edge into silence. Fix: lip ratio
solved for the WIDEST SPEAKING WINDOW (margin against cracking), air
column solved for cents (BORE_MAP, 1.000 at C2 → 1.019 at C4). Both maps
measured at all 37 chromatic steps and pasted into the gen script.
(2) Low notes were slow because round 68's bore was THREE NOTE-PERIODS
LONG FOR EVERY NOTE — 15.7 m at C2. Replaced with the real architecture:
one trombone-length tube, delay ratio pin = partial number
round(f0/58.27 Hz). RESULTS: 37/37 notes speak within 4.2 c (was +15…+29 c
and dead below G2); speak median 114 ms, worst 216 ms (was C3 694, C4 348,
C5 426, C2 dead); playing window 9–51 kPa on all nine audition notes;
0.5/2/6 s renders scale the hold only (onset fixed at 0.100 s, tail 0.18 s
inside kVoiceTailSec). Fifth defect found and fixed: pm ∝ f_lip² left C2 a
near-pure sine 30 dB under C5 — PM_EXP 0.5 cuts the register spread to
4 dB. WIRING TRAP WORTH REMEMBERING: reading the tuned lip frequency back
through {"tap":"Flip"} cost −33.7 c and 15 dB (nothing else advances Flip
on that sample); each such pin now gets its own points map on __perf_f.
KNOWN WEAK SPOT, reported not hidden: blowing 4× harder moves the centroid
742→751 Hz — the dynamics-to-timbre link has nearly vanished vs round 68's
2.1×; that is backlog 74(a) and the next measurement. Breath-burst lever
measured at 12× and 40× and changed NOTHING — wired, off, reported.
dsp REVIEW Awaiting: **69**, 62, 65, 38, 39 (68 and 67 resolved).

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
