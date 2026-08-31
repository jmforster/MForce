# DSP lane — review queue

## Awaiting Matt

### (parked) THE BOW gets a family — 30 cells (2026-08-24)
PARKED 2026-08-24 evening — superseded by the KSString-modes decision
(item 52, now in Resolved): BowedStringEvolution is kept for old time's
sake / comparison / experimentation, not production. The 30 cells +
conditioning/brt ladders stay in bow_family for reference; **no verdict
owed**. Original entry follows for the record.
**renders/dsp/pending/bow_family/** — your parameter notes turned into axis
ladders off the exact discovery bytes (baseline included), notes 36/60/84,
3.5 s bows: RedNoise frequency 100→1400 + **rnfreq_track** (bow events
follow the note instead of sitting at 350 Hz — the stochastic-bow reading
says they should), density 0.15→1.0 (your "hesitant" axis), smoothness /
continuity / rampVariation, the TBD pair boost + zeroCrossTendency, bow
position 0.05→0.40, friction 2/8, tubeLoss ladder, and three combo cells
from your verbatim characterizations (hesitant / bright / darklow).
FOUND EN ROUTE: bowSpeed x frictionGain is ONE axis — the Friedlander
recursion has an exact scale symmetry (verified corr=1.0 on rendered
pairs), so shape depends only on the product ("drive"; baseline 1.2,
fric2=0.6, fric8=2.4) and the leftover is pure gain. The bowspeed cells
were exact duplicates and were dropped.

**YOUR SIZZLE REPORT (same day, headphones): measured and split in two.**
All 30 r1 cells carry it because both mechanisms are engine-side:
(1) DOMINANT — the KS-bend fractional read head (Approach A) is wrong for
a LIVE evolution: vibrato makes the reader drift past the writer, and the
output continuously sweeps the seam between this pass and last pass of
the string state. Vibrato depth 0 collapses the inter-harmonic floor
~25-30 dB at note 36. Hear it: **vib000** (no vibrato — but also no
vibrato, which is the open problem, backlog 33).
(2) SECONDARY — bow noise recirculating undamped: the loop had only flat
tubeLoss, no frequency-dependent loss. NEW `brightness` setting on
BowedStringEvolution (one-pole per line write, default 1.0 = original
node byte-identical — discovery bytes locked): **brt095/090/080/065/050**
ladder, plus **vib000_brt080** (both fixes together — the cleanest cell).
**THE FLUTTER (Matt's rename, and he's right — it's a third artifact):
~5-7 Hz amplitude modulation, and it IS the RedNoise.** Bow force =
bowSpeed x pressure, so the noise's slow content modulates amplitude
directly (constant pressure: mod-energy 487 -> 4 at n84). Highpassing the
pressure only partly helps — the friction nonlinearity demodulates the
noise, so its envelope variance becomes new LF wobble past any cutoff.
Depth is the knob: **conditioning ladder** pressure_const040 (character-
free anchor), cond_hp005/020/060/200 (const 0.4 + HP'd noise), and
**cond_hp200_half** (noise at half depth — flutter 487 -> 11, the best
character-keeping cell on paper). All patch-level: two nodes (BWHighpass +
CombinedSource) on the bow pin, no engine change — adoptable into the
discovery patch directly if it survives your ears.
VERDICT: which axes matter, does freq-tracking beat fixed 350, do the
combos read as characters, names for keepers; on the sizzle pair — does
vib000_brt080 kill the HF plateau; and on the conditioning ladder — where
does character survive minus flutter (cond_hp200_half is the candidate).

### 48. Triptych — bow without zither, 11 cells [listen] (2026-08-23)
**renders/dsp/pending/excite3_triptych/** — your attack/sustain(/release)
architecture: one-shot attack (creak / chaos scrape / jagged-in) crossfading
into a STATIONARY looping bed (buzz / fine / velvet — uniform statistics,
seam at zero, ~1.2 s passes wobbled by varPct, i.e. the anti-zither fix),
3x3 plus two cells with a release seg swelling as the bow settles
(creak__buzz__rel, jaggedin__fine__rel). NOTE: a silent-drop bug
(CombinedSource pin is source1, not source) meant the excite2 creak_into_*
cells you already heard had NO creak — both regenerated; re-listen counts.
VERDICT: does the triptych bow; which attack x bed pairs live; is the
release seg worth keeping.

### 47. Sustained excitation probe — the bow experiment, 9 cells [listen] (2026-08-23)
**renders/dsp/pending/excite2_sustain/** — textures LOOPING into the string
while the note lasts (SegmentSource oneShot=false, varPct 0.15 re-randomizes
each pass so the loop has no pitch of its own), bow-pressure Envelope on
seg.amplitude, same string harness as excite1. Seven beds (scrape buzz/fine,
cluster tailoff, shrinkgrow, jagged out, dots slow-wander, crunch) + two
composed string-attack cells: **creak_into_buzz_bed / creak_into_cluster_bed**
(one-shot fast reverse bounce + gated bed). Probe set built ahead of your
full r1 verdicts — those verdicts re-aim it. VERDICT: does anything bow?

### 46. Excitation round 1 — 13 candidates into a pitched string [listen] (2026-08-23)
**renders/dsp/pending/excite1/** — the excitation-candidates list from the
sweep verdicts, each played as SegmentSource → KSPianoString (afp31_gt's
damper + t60 keytrack) at notes 36/60/84. Includes the creak question
(reverse_full_faster/_fastest), the scrape coupling test (scrape_buzz), both
jagged legs, the cluster→thumps, dots cells, and combo_ref as the plain-
strike reference. Auto-leveled under the 0.7 limiter. VERDICT: whose
character survives/transforms interestingly through the string; which earn
the sustained-excitation round (gated, looping with varPct — the bow
experiment). Also round 3 combo/combopair (segment_sweep3/) still awaits
its raw verdict.

### 39. Wheel + pressure, live [try — BLOCKED on hardware] (2026-08-20)
**2026-08-29: Matt's current MIDI keyboard has no wheels; try pends
setting up a different controller.** Original entry:
The one piece of the 2026-08-20 session no human has exercised: P3 wired
CC1 mod wheel and channel pressure from the MIDI keyboard into
InstrumentState, smoothed per voice (~10 ms). Nothing hears them until a
patch does: add a Note node, wire its `wheel` (or `pressure`) pin through
a Curve into something audible — Overall_lpf.cutoffFreq on the WIP piano
is a natural first target — then play and ride the wheel. Everything else
from today (gold pins, Note faces, control strips, group editing,
drill camera, minimap) you verdicted live at the canvas; no listen items,
P3's null gate was 120/120 bit-identical.

### 38. AF saxophone — source exhausted, build awaiting go [discuss] (2026-08-18)
docs/research/af_sax/RECIPE.md: complete value table from the breakdown
video (2560x1440 — every knob read; frames archived). Architecture:
self-oscillating SVF as the tone source (gate rescales resonance to ~0),
ADSR 10/731/0.19/6.2 as "air pressure", multiplicative breath
y = x(1 + 0.1*noise*env), velocity-scaled legato glide (600->15 ms,
first note instant), 5.8 Hz TRI vibrato FM'd by 1.3 Hz RND with 6 s
fade-in, THE character = feedback delay loop at 2x period with
AUDIO-RATE delay modulation (x(1 + 0.36*signal)), in-loop tanh + 1P
damping (pitch+35 semis), loop gain crossfaded 0.5->1.16 by the
expression "Power" follower whose ATTACK = f(velocity) 600->16 ms (the
scoop). Body = stock reverb, size 0.06 decay 0.39 lp 8408 wet 1.0.
Only 3 TUNEs remain (vibrato depth, SVF tap, tanh shape). BLOCKED on
design go-ahead: needs a new ModDelayLoop node (audio-rate delay mod +
in-loop tanh + gain>1) and a body strategy (mini reverb node vs skip).
Reference audio af_sax_demo.wav (the played demo Matt prefers).
BEST SOURCE: the actual patch file is on the AF Discord — grab when
joining; parse_af_patch.py will decode it and settle the TUNEs.

## Resolved

Pared 2026-08-15 at Matt's request (and again 2026-08-22) — compact stubs
only; full detail lives in the run reports (docs/autonomy/dsp/reports/)
and git history.

- **52. KSString bow mode** (2026-08-24, verdicted same day): "character +
  promise, problems on the periphery"; all attacks too slow for a string.
  DECISION: KSString modes is the model; BowedStringEvolution parked.
  Full verdict text in the 08-24 report; direction carried by backlog 34.
- **41. UI piano dynamic octaves + top C** (2026-08-21, verdicted
  2026-08-29): "fine" — thresholds 25/45 and the trailing top C stand.
- **51. Listen-here** (2026-08-24, verdicted 2026-08-29): "works"; the
  one tweak — tap follows drill-OUT to the higher level (group face per
  Listen memory, cleared at main) — shipped same day.
- **40. Triangle power** (2026-08-21, verdicted 2026-08-29): "works
  fine" — symmetric concave/convex + signed convention stand.
- **35/36/37. AF piano thread (afp31 v1–v12, gt, afks)** (closed
  2026-08-29): Matt — "that's AF piano, we're beyond that." Superseded
  by the KS piano family (piano_default/piano_bright/piano_seg). The
  research corpus (RECIPE.md, parse_full.txt, decoded patches, renders)
  stays where it lives; afks_v1's bowed-equilibrium mechanism note may
  yet inform KSString modes. AF sax (38) unaffected, still [discuss].
- **49. Smoothing the bed** (2026-08-23, verdicted 2026-08-29): LEVEL is
  the dominant bed problem — too loud everywhere and must keytrack DOWN
  with note frequency; seams secondary (4 ms merge leaves subtle zither).
  wtsaw ok, jag40 > jag15, ladder extends upward; hot attacks
  indistinguishable (dropped); jins/jags "plucky not bowy" — attack
  textures carry bite, not bowing (feeds 34a). Full verdicts in the
  08-29 report; round-2 directions (keytracked bed, jag 60/80, wtsaw
  bed core) queued for the next sweep.

- **42. Curve editing + Envelope min/max** (2026-08-22, resolved
  2026-08-23): Matt tested minValue/maxValue explicitly — works as
  advertised; the knot-editor fix and Properties-pane curve editor have
  been in daily use through the sweep sessions without complaint.
- **45 + snare_corner2. Snare corners** (2026-08-23, resolved same day):
  both "terrible"/"nothing good" — snares PARKED with Matt's diagnosis
  (crack needs internal structure; density collapse alone doesn't redden).
  Full detail in oneshot_sweep/ROUND1_VERDICTS.md; revisit at instrument-
  roster time.
- **44. Segment sweep round 2** (2026-08-23, resolved same day, area by
  area): jagged paid off; reverse bounces re-based and parked for
  excitation; cluster->thump beats scrape->thump; dots2 kept+extended;
  scrape/kick/twohit2/texture closed. Full log:
  docs/research/oneshot_sweep/ROUND1_VERDICTS.md. seq() pause bug found by
  Matt's ears and fixed mid-review.
- **43. Segment sweep round 1** (2026-08-23, resolved same day): Matt
  auditioned ALL cells, area by area — "many are promising." Full verdicts +
  round-2 directions in docs/research/oneshot_sweep/ROUND1_VERDICTS.md
  (standout: scrape_then_thump; kick corner in wide atoms; scheduled
  clusters; dots = time-domain synth-kick recipe; rulebreak demoted;
  fixed_fine_zct dropped as "earsplitting"). Round 2 designed from the log.

- **34. AFP excitation ladder** (2026-08-15, resolved 2026-08-22 housekeeping):
  four revs of envelope-shape matching, shape verified matching at the
  string-input tap, then Matt's final verdict "just sounds terrible compared
  to his" — approach EXHAUSTED, front parked. Of the three logged
  change-of-approach directions, (2) get-the-real-patch HAPPENED (Wayback →
  item 36 ground truth); (1) substitution bisect and (3) literal chain clone
  remain available if the gt A/B still leaves a gap. Full rev history in this
  file's git history.
- **33. AFP excitation stage analysis** (2026-08-15, resolved 2026-08-22):
  the analysis stands at docs/research/afpiano_2021/ANALYSIS.md; its
  proposed moves A-E were mooted by the decoded ground-truth patch (item
  36) — moves now derive from real values, not frame inference.
- **32. Groups + Listen tap** (2026-08-15): "work great, and this is
  huge" — piano patch de-black-boxed. Follow-ups landed same day:
  ctrl-click deselect (pin-hover aware), hover-vs-selected cues with the
  blue selection kept, drill-out position loss fixed. Piano_bright
  accidental overwrite restored from git, hash-verified. Shared-source
  group refusal + duplicate-changes-sound trap → backlog 27 (absorbs 3q).
- **31. 3n closed** (2026-08-15): all 33 UI-save fidelity failures fixed
  (5 root causes, incl. engine adsr-shape sustain rewrite); gate 196
  patches, exception list empty but FormantSequence1 (backlog 3p).
  UI re-saves of library patches are SAFE now. The cello/viola
  untracked-repair decision closed by the 2026-08-15 reconciliation
  commit (5c5c139) — both entered library/strings WITH repairs.
- **30. Mappings dialog / Parameter retirement** (2026-08-14): "All
  good." QWERTY question answered: node graphs play via a synthesized
  Parameter frequency node (NodeGraph mode keeps the type).
- **29. Stable node identity** (2026-08-14): "Works"; his exc_body
  rename committed (3a316ac).
- **28. Note-contained sound + Piano_bright baseline** (2026-08-13/14):
  three audition-fix rounds (damper stages, UI preset parity, top-octave
  anchors), evening-run engine fixes (dispersion shedding, allpass
  fractional read), no-auto-promote rule established, then Matt's
  hand-tune became **Piano_bright = the KS piano library baseline**.
  Bass containment warns below ~E4 are expected/benign for this family.
- **27. KS v6 ladder + CMA runs 1-3** (2026-08-10..13): full history in
  reports; ended superseded by Piano_bright. Standing notes that
  survive: broadband inter-harmonic gap is a MECHANISM gap (same as
  viola); CMA endgame must retarget the UI-renamed node ids and the
  re-baselined (verb-free) objective; eval notes must cover searched
  curve regions. Analysis: docs/research/afpiano_2021/ANALYSIS.md.
- **26. Housekeeping 2026-08-10**: all sub-questions answered by Matt;
  the last thread (c2c_quiet / fable1_v6 "where did they go") resolved
  2026-08-15: his bulk move committed as renames into library/strings,
  c2c_quiet + v6_05_floor08_deep parked in patches/old/ (5c5c139).
- **25. Soprano alt-formant candidates**: renders+patches deleted; Matt
  recalls no winners but wants a re-do → backlog 3r (re-render with
  variation arms).
- **24. Node-graph stream fix**: "Good, done."
- **22b. KS v5**: superseded by v6.
- **11. Power renorm**: "Leave it." Documented degenerate case stands.

Older resolutions (2026-07-27 .. 2026-08-10) are preserved in git
history of this file and the run reports; verbatim verdict text with
reference value (expand round-2 ranking, vowel pass-2 notes) lives in
the 2026-08-10 revision of this file.
