# DSP lane — backlog

Housekept 2026-09-14 (fall cleaning, with Matt): dead/done/moot items moved
to the ledger, maybe-someday items moved to IDEAS.md (ledger notes the move
so cited ids stay resolvable), survivors compressed — full history for every
item is in run reports and this file's git history. Item ids are STABLE.
Tags per WORKFLOW.md.

## Active — engine/build

65. **[build] Loop behavior depends on graph rooting — minimal repro
    pair** (absorbs 66's residual — same suspected root, diagnose
    together) — found 2026-09-08 (valve1 probe). Two patches identical
    except `graph.output` (patches/baselines/feedback/
    loop_root_sensitivity_{fires,dead}.json): rooted at Junction (delay
    tap-only → §3.3 advance list) the loop ignites, RMS 0.064; rooted at
    Delay_line (plain pull, the shipped shape) it is dead to the
    quantization floor — same equations should hold both ways per
    feedback_loop_design.md §3.2/3.3. Eliminated: compensation, reader
    count, drive envelope, breath level. Firing mode sits at ~659 Hz ≈ 2×
    valve center, not f0. Practical rule until diagnosed: probe/measure
    loops ONLY at the shipped root. 66's residual, likely same mechanism:
    every valve2 cell runs exactly ONE uncompensated sample of loop
    latency when the valve chain adds a second consumer to Drive_inputs
    (−11c at C4 → −74c at C7, precisely 1/period) — the shared-consumer
    guard/advance ordering shifts the tap read by one sample; comp=1
    assumes the tap is the only z⁻¹. Diagnose = trace one sample of each
    rooting on the repro pair and find where state diverges.

71. **[build] Stateful chain double-advances behind a shared stateless
    node — repro pair in baselines** — found 2026-09-15 porting BandedWG
    bowed: adding ONE stateless CombinedSource (DvF, a second consumer
    of Dv) made the 4.5 s AdsrEnv run in exactly half the time (2.258 s
    = 2x advance rate of the env chain). Repro pair:
    patches/baselines/double_advance_{ok,bug}.json — identical except
    that node. NARROWED same day: the minimal env→mul→two-consumers
    shape is guarded correctly (verified), so the trigger needs the
    loop/advance-list context (tap-only delays re-pulling the shared
    cone during the advance pass). Same guard/advance cluster as 65/66
    — diagnose together. Until fixed: in loop patches, keep every
    stateful chain behind exactly ONE normal consumer (the ports now
    comply); symptom to recognize = gestures at exactly 2x speed.

67. **[build, small] Loader: warn on unknown params/settings keys** —
    2026-09-08. The generic settings/params loops skip unrecognized keys
    silently, so an old binary loading a newer patch plays a silently
    downgraded patch. Wanted: one stderr line per unknown key ("[load]
    Shaper: unknown param 'hysteresis' — engine older than patch?").
    Cheap; converts every future old-binary/new-patch mismatch from a
    mystery into a log line.

69. **[audit] 48k rate-bake sort (physical vs normalized constants)** —
    filed 2026-09-13 from Matt's question after the STK 22050 discovery.
    MForce is structurally rate-parametric, but a handful of PER-SAMPLE
    constants are 48k-baked and would shift character at any other rate,
    exactly the STK disease: ks_string.h dnAmp_ *= 0.9995 ("at 48 kHz"),
    envFollow_ 0.99995/sample, lipTension normalized one-pole, the noise
    family's continuity/step semantics (per-sample blending/odds), UI live
    audio + saves pin 48000. No action while everything renders at 48k;
    MUST be sorted before any non-48k work (44.1k export, variable-rate
    live, the oversampling ambition). Sort rule from the STK port: per
    constant ask "physical or normalized-frequency quantity"; normalized
    ones re-derive from sr, null gate at 48k proving byte-identity.

53. **[build] Live-keyboard mono mode — patch polyphony must override the
    pool floor** — Matt 2026-09-02: fast flute lines should cut the
    previous note at the next attack. UI floors the pool at
    LIVE_MIN_POLYPHONY=8 (tools/mforce_ui/main.cpp; loader takes
    max(patch, floor)), so authored polyphony 1 is ignored live. Wanted:
    respect small patch polyphony (or a mono/voice-steal flag), note-on
    steals, last-note priority, watch for click on the cut. CLI already
    respects patch polyphony. Related: the STK legato finding (STATUS
    09-13 Round 3, STK_PORT_NOTES §LEGATO) — voice reuse without reset is
    the same delivery mechanism, brainstorm input to 68.

35. **[fix] Editor↔loader asymmetries on input pins — three ways a
    CLI-legal patch mangles in the editor** — 2026-08-29 (grail
    post-mortem): (a) editor reconstructs wires by EXACT pin name, so
    loader-side param aliases ("cutoff" → cutoffFreq) drop the wire
    silently; (b) numeric constants on input-descriptor pins have no
    editor representation and evaporate on load; (c) update_node_dsp
    wires every unconnected input pin to ConstantSource(0.0), clobbering
    engine defaults (WavetableSource's WhiteNoise default never applies
    in the editor). Fix directions: shared alias table for (a); reject or
    synthesize a Var node for (b); leave engine defaults on unconnected
    input-only pins for (c). Related trap: Edit>Convert node→patch
    synthesizes a legacy paramMap frequency row that collides with a
    Note-face wire — should synthesize a Note face instead, and the
    Parameter-mapping dialog should stop accepting new bindings.

36. **[cleanup] Envelope dialect + preset-file consolidation (node types
    STAY)** — Matt 2026-08-29: (a) convert legacy generic-Envelope
    `"preset":"ar"`-style params to explicit stages at LOAD (kills one of
    three envelope authoring dialects, gate-provable); (b) deduplicate
    envelope_presets.h boilerplate (~200 lines across six preset classes);
    (c) formalize the lazy first-sample init idiom — a `first_sample()`
    hook replacing three hand-rolled spellings (KSString, WavetableSource,
    SegmentSource). The rule, stated once: prepare() runs before the pin
    chain computes, so pin reads via current() in prepare are STALE (the
    08-29 SegmentSource nondeterminism trap).

19. **[build] hiBoost → explicit curve — the P4 cleanup sweep** — Matt
    2026-08-18: `PitchedInstrument.hiBoost` is a hidden loudness
    compensation (keytrack curve wearing a scalar's clothing). Convert to
    visible transfer-curve wiring (CurveNode from NoteState frequency into
    the voice-gain path); null-test like the paramMap flip. P4 also
    carries the RangeSource migration (perform_source_design.md §7 +
    pin_model_design.md).

28. **[build] Patch→Node→Patch conversion drops instrument extras** —
    2026-08-20: the Edit-menu conversion lane (`--convert-roundtrip`)
    loses `instrument.volume` — the return trip synthesizes the block
    instead of restoring stashed extras. Fix: stash
    s_loadedInstrumentExtras across convert_patch_to_node_graph and merge
    back; gate by comparing instrument blocks through the lane.

3p. **[build] FormantSequence1 is CLI-unrenderable and migrates silent** —
    legacy inline formant form renders in the UI only; UI-roundtripped
    ref-form JSON renders SILENT, so UI and CLI disagree about
    FormantSequence semantics beyond the format. Diagnose the silence,
    then migrate; the roundtrip gate's only exemption until then.

31. **[build] Lint gap: descriptors the loader never wires** — 2026-08-22
    (Envelope minValue/maxValue went ignored four days): every
    hand-written patch_loader.cpp branch that skips wire_params_generic is
    a candidate; lint_patches.py can't see it (allowlist scraped FROM
    descriptors). Add a check: per registered type, per descriptor, prove
    a JSON key actually reaches set_param through the loader (one-node
    patch, marker-ref, assert get_param round-trips).

37. **[research] Feedback-loop search — live threads** — the curve-space
    sweep campaign, full history in R4_VERDICTS.md + run reports (r3
    damped round, r4 "nailed it, concept proven" → flute_default; r5a
    noise-keytrack keeper at exponent −0.80..−0.95; r5b axis tour
    verdicted 09-14 "all promising, tho very similar" — heap, no
    neighbor round). LIVE: (a) r5c = junction-asymmetry round using
    DIRECTED axes per docs/research/feedback_sweeps/
    JUNCTION_OPERATING_POINT.md (origin-asymmetry ratio + drive floor
    relative to measured threshold, not breakpoint jitter); (b) strings
    AND brass get the oboe treatment (Matt 09-03: seed from a proven
    wind line — oboe_again's bass sounds BOWED and is the strings-round
    lead); (c) audible in-loop breath (hiss 0.005-0.02 under its own
    decay envelope) — breath must couple to the tonal path, the
    parallel-sum dead end stands. NOTE 09-14: in-loop FORMANTS rejected
    (REVIEW 56, "none successful") — formants stay outside as the
    oboe_default bell; breath-in-loop is unaffected. Attack-texture
    insight (Matt 09-14, from the excite archive): ZITHERING fast
    enough approaches continuous bow excitation; creak_fine
    (excite3_triptych) demonstrates best — lo-f = "a 20-foot tall cello
    on Mars"; candidate excitation for this family's `source` pin.
    Philosophy notes: per-range instruments over one averaged 5-octave
    patch; keytrack matters within the home range.

70. **[campaign] STK-lineage bowed chassis — extensions on the validated
    port** — the resumed STK campaign (port phase closed, REVIEW stk_port1
    stub). State after Matt's 09-14 verdicts: ext1 saxophony cells +
    ext_perf (vibrato fixed, "most stringy") kept for hand-play; ext1
    hysteresis cells rejected as harsh — next hysteresis-in-chassis round
    must build in the ext2 diagnosis first (stick LEAKS ~2%, plateau
    continuous); ext2 cello-register cells all promising, "could be good
    if tamed by filters we're planning to add anyway." ROUND 3 DONE
    2026-09-14 (IR-download idea DEAD — links rotten; derived from his
    measurements instead): tools/fit_larson_body.py recomputes the LTAS
    ratio fine-grained from FOUR pairs and fits a 14-biquad stack (err
    median 0.68 dB at the pairs' 2.8 dB spread; coefficients in
    docs/research/stk_port/larson_body_fit.json);
    tools/gen_stk_bowed_ext3.py stages nobody/Maestre/Larson/btd+Larson
    to audition (REVIEW 58), in-graph LTAS validated +0.83 dB median.
    REMAINING FAMILIES PORTED 2026-09-15 (round 4): Saxofony, BlowHole,
    BandedWG — tools/gen_stk_{saxofony,blowhole,bandedwg}.py +
    tools/stk_ref/{saxofony,blowhole,bandedwg}_ref.cpp, native-22050
    validation per the rate discipline, ears queue = REVIEW 59
    (stk_port2). Findings en route: 1-sample envelope stages never fire
    (2+ do); engine BowTable outputs dv*rc (multiply folded in);
    backlog 71 double-advance repro; BlowHole is register-chaotic at
    22050 (ref flips against itself); BandedWG bowed blooms ~4 s by
    design. LATER ROUNDS: leaky-stick hysteresis round; canonicalize
    surviving families to 48k; Mohonk torsion/dispersion features;
    compensate-ON + drive exploration past STK's ranges. STOP CONDITIONS:
    a round with zero gate-passing cells ends its sub-thread (2-attempt
    rule per mechanism); the campaign parks when the filter/body work
    lands and Matt's bench has what it needs for hand-tuning.

## Design questions

68. **[design, brainstorm] Articulation-keyed multi-graph Instruments** —
    Matt 2026-09-10, verbatim shape, to be brainstormed before any spec:
    (1) an Instrument has MULTIPLE node graphs keyed by Articulation.name;
    (2) articulations specified by Composer/score or "Performer";
    (3) Performer selects the Instrument (as now); (4) PlayNote selects
    the articulation-specific root, default when absent. Context: this is
    the concrete mechanism for the Perform-layer Tier 2 of 64
    (duration-aware articulation — Tier 1 landed 09-10, duration is a
    Note-face perform field; Tier 2 = performer identity, patch-picking,
    "Fred prefers double tonguing", Composition→Performance→Realization
    per perform_source_design.md). Plumbing reality: articulation does
    NOT reach PlayNote today — Conductor flattens it via
    apply_articulation into {adjustedDuration, adjustedVelocity}; any
    design re-plumbs the name through Conductor → play_note/prepare_voice
    (the flatten can stay for its duration/velocity effects). Encoding:
    articulation is a NAME; discrete selection wants a switch inside
    PlayNote, not a curve on a variant ordinal. Brainstorm must cover:
    voice-pool shape (pool per root vs per-note root selection — voices
    are pre-built copies of ONE graph today); patch JSON form (named
    graphs vs table of patch refs); cache/memory cost per articulation;
    coherence with the Ornament → PitchCurve layering
    (pitch_modulation_design.md — the existing "score symbol becomes a
    per-note performance object" pattern). Ornament pacing is settled
    (Matt 09-10): fractional dur[] stretching is a non-problem; the rare
    slow-ornament case is a COMPOSE-tier decision via existing tie
    machinery. Also related: STK legato finding (voice reuse without
    reset = Slide/HammerOn/PullOff family; 53 is the mono-mode twin).

56. **[design] Node ids: migrate labels-as-ids → assigned invisible ids** —
    Matt-decided 2026-09-06, post-lanes; needs its own spec (both loaders,
    both savers, groups/dynamicPins/ui keys, roundtrip contract). Shape:
    ids use a reserved char (e.g. '@'); DUAL RESOLUTION — refs resolve by
    id first, then unique name (generators may stay name-based); legacy
    load synthesizes ids from labels, upgrade on save; null gate
    unaffected; one-time churn of tracked patches. EXPLICIT REQUIREMENT:
    DROP the label-uniqueness check — labels become pure display;
    name-refs require exactly one match (ambiguous = loud load error).
    Kills the whole rename-rewrite registry class (six subsystems).

54. **[design] Function/Operation node** — Matt 2026-09-06: explicit
    small node for scalar math — op enum (add/mul/sub/div, maybe
    min/max/pow) over one or two inputs, or one-input f(x) — replacing
    the CombinedSource-with-constant idiom ("node types are cheap").
    Needs the usual sweep: which ops, one node or two, migration stance
    (probably none — idiom keeps loading). Absorbs question 23: the
    legacy CombinedSource connectable Amplitude stays unrestored; an
    explicit node is the honest spelling.

27. **[design] Groups multi-output — control wires AND shared sources
    collide with the one-output rule** — two hits: (a) a Curve feeding a
    target outside the group can't move in (one-output check refuses);
    (b) a WhiteNoise feeding consumers across the boundary refuses, and
    the duplicate-the-node workaround SILENTLY CHANGES THE SOUND
    (independent RNG stream vs one correlated source via RefSource). Real
    fix: multi-output group faces — one synthetic output pin per
    boundary-crossing source, projection maps per pin. Design question
    first: does a control tap belong in the audio interface, or do faces
    distinguish audio-out from control-outs (gold, per the pin model)?
    Minimum interim: warn in Duplicate that a generator gets a fresh
    stream.

26. **[design/build] Bend is inert on KSPianoString — and the missing
    block-level cadence** — (a) KSPianoString reads frequency once at
    init (ks_piano_string.h); comb lengths fixed for the note's life — a
    bend curve moves a number nobody reads. The 26a loud warning landed
    with P3; the deeper "can KS retune mid-ring" question remains.
    (b) The engine has NO block cadence (fixed / per-note / per-sample
    only); is a per-block tier worth introducing, and what's the hook?
    Cost is wildly per-node (t60 tracking = 3 pows/block; retuning combs
    mid-ring is a much harder problem).

63. **[build, small] Voice tail allowance — kill the cutoff click for
    good** — Matt 2026-09-07: voices die at envelope-zero while Reverb/
    ringing filters inside still hold sound → click. Workaround today: a
    trailing hold-at-zero stage. Right fix: engine grants every voice a
    fixed tail allowance (0.25-0.5 s, or derived from reverb/delay sizes)
    past envelope-end. Voice lifetime bookkeeping only.

62. **[build, small] noiseBed* settings must affect ONLY the noise bed** —
    Matt 2026-09-07, hard requirement: noiseBedDelay/Fade/FadePow
    actually gate the TONE (full_additive_source.h). Move the tone gate
    to toneDelay/toneFade/toneFadePow, keep noiseBed* bed-scoped. No
    compat shims (standing policy); migrate the patches that set them
    (clarinet1 at minimum — sweep for others) in the same commit.

61. **[housekeeping] Partials + noiseBed settings: document and prove
    effect** — Matt 2026-09-07: ~23 partial-motion settings + 6 noiseBed*
    settings, none documented, none proven audible. Per setting: one-line
    mechanism from code; rendered A/B proof-of-effect (flag inert knobs);
    flag naming traps; deliverable = settings reference doc in
    docs/research/.

60. **[build, small] Save backups for hand-work** — Matt 2026-09-07,
    after the oboe_grouped scare: on save, rotate a couple of prior
    versions beside the file (name~1.json / .bak). pending/ is gitignored;
    cheap insurance.

59. **[bug, unreproduced — gremlin watch] Live-session state degradation**
    — two sightings, capture drills ready: (a) live-path clicking in
    lower octaves after a long edit session, cleared by app restart, file
    proven clean via CLI (2026-09-07) — if it recurs capture BEFORE
    restarting: File>New + reload, stderr, fresh patch, QWERTY vs MIDI,
    held vs short; (b) auto-listen ghost on the FIXED cf04155 build
    (2026-09-07 late) — on sighting read the breadcrumb "Listen:
    Patch | Group" row (post-cf04155 it shows the ACTUAL tap; Group =
    real missed path, Patch = different bug wearing the costume), note
    the exact preceding click. Also on the watch list: the two knot-save
    losses + density=2000 reverts (pre-09-07, unreproduced).

25. **[gated on REVIEW 50 listen] Wave-evolution types — nothing retires
    until tried WITH A REAL EXCITATION INPUT** — Matt's 2026-08-23
    reversal stands ("a pretty fucking nice bowed string... mind blown";
    bow = BowedStringEvolution + RedNoise on the bow pin). Discovery
    patch: patches/baselines/bow_evolution_discovery.json. Old
    "amounted to nothing" verdicts were made without an excitation input;
    ReedEvolution/BrassEvolution get the same retry before any retire
    call. Bow-family sweep cells sit in REVIEW 50.

## Listen-prep

3r. **[listen-prep] Vowel review pass — soprano re-render + tenor O** —
    Matt 2026-08-22: review ALL sung vowels ("the 3 soprano vowels" +
    "tenor O is not quite right either"). Voice envelopes swept to
    release 0.1 same day, so locked WAVs in renders/library no longer
    match — re-render before any A/B. Regenerate the 4 soprano alt-formant
    A/B pairs from the run-25 recipe + 1-2 variation arms against CURRENT
    library/voice files. Honest-exit note: run-25 measurement says
    separability collapses with f0 (soprano E and I share both formant
    harmonics at 440 Hz) — if no arm lands, the vowel front retires;
    escalation option is vibrato via the pitch-mod layer (coherent FM =
    strongest partial-fusion cue).

## Done / retired ledger

One line each; full text in run reports + this file's git history.
Fall-cleaning moves (2026-09-14) are marked → IDEAS or → ledger.

- 1. fable1 thread + autonomy scaffold committed — early runs.
- 2. Iowa motion-param derivation — superseded; folded into item 3.
- 3. CMA-ES endgame — → IDEAS 2026-09-14 (parked pending re-scope).
- 3b. UI paramMap curve preservation — run 5.
- 3b2. Shimmer gain floor — RETIRED 2026-09-14 (stale since 08-22;
  additive lane no longer the piano front-runner).
- 3c. UI engine-stamp guard — runs 14-19.
- 3c2. FMSource dead `phase` param — run 19; real PM sidebands.
- 3e. Piano engine features (inharmonicity, per-partial decay) — run 20.
- 3e-NEXT a/b/b3. Scorer debt closures — runs 21/25; net: 22.5% of the
  objective was measurement artifact.
- 3e-b2. derive_motion/F1 scorer debt — → IDEAS 2026-09-14 (input to the
  steal-first "backtest scorers before trust" decision).
- 3e-NEXT2. Piano recovery bundle — run 23.
- 3e-NEXT3 + 3h. Additive-piano / KS-v5 items — RETIRED, superseded by
  Piano_bright.
- 3f. CombinedSource "sum" + ordinal 3 — run 21; closes item 15 residue.
- 3g. SlewLimiterSource — built run 21, reverted run 22; Peak-mode
  resurrection → IDEAS 2026-09-14 (was 3s).
- 3i. Envelope hold/loop mode — → IDEAS 2026-09-14.
- 3j. WanderNoise deltaSpeed semantics — → IDEAS 2026-09-14.
- 3k. Audio-thread races — run 26. RESIDUAL: stage-editor/Curves-tab
  edits still mutate live objects unlocked; same fix if a crash names them.
- 3l. Squeaker modulated even/odd weight — → IDEAS 2026-09-14.
- 3m. `<curve>` badges — 08-14 (3246ffe).
- 3n. UI save round-trip fidelity — 08-15; five root causes; sole
  exemption = 3p.
- 3s. Peak-mode env follower — → IDEAS 2026-09-14.
- 4. Novelty metric — run 5.
- 5. Recursive partial expansion — rounds 1-4; front retired.
- 6. FormantSequence deep-dive — run 6.
- 7. Oversampled FM + modulation matrix — runs 8/12.
- 8-open. Additive SIMD 2d/2e — → IDEAS 2026-09-14 (anti-results
  preserved in specs/2026-07-31-additive-simd-soa-design.md).
- 9. Six algev patches converted to instrument-style — run 8.
- 10. Vowel/formant re-tune — run 25; 7 winners locked.
- 11. Standing dirty patch files — closed 08-20.
- 12. `power` inert at count=1 — "leave it"; degenerate case documented.
- 13. Piano unison beat rate — RETIRED 2026-09-14 (additive piano lane
  retired; two failed attempts + method note recorded in
  research/ml_ears/piano_beating.py outputs).
- 14. Silently-ignored params — run 25; survivors were 3l.
- 15. 7 CLI-unrenderable patches — run 19 + 3f.
- 16. Soprano E/I/U tunability — superseded by 3r.
- 17. UI live-audio round trip — 08-18.
- 18. MIDI off the UI-frame cadence — → IDEAS 2026-09-14 ("when live
  feel matters").
- 20. PerformSource P1-P3 — landed 08-18..20.
- 21. WhiteNoise density/boost/continuity + zeroCrossTendency — 08-21.
- 22. Vibrato speed/depth envelopes — CLOSED 2026-09-14 (Matt: audio-rate
  never wanted; per-note is the ask) — ALREADY WORKS via dynamicPins
  (speed/depth are setting descriptors); render-proven 19c@C3 vs
  226c@C6 from one depth curve. Nothing built.
- 23. CombinedSource connectable Amplitude — CLOSED 2026-09-14, absorbed
  by 54 (explicit op node is the honest spelling).
- 3d. Pan law — CLOSED 2026-09-14 (Matt: mono writes x1): StereoMixer
  equal-power law normalized to unity center; verified exactly sqrt2 x
  old within 1.4 LSB; manifest refrozen same commit. Edge pans now
  +3 dB relative to old law, soft-clip guarded.
- 24. Attribute proof-of-effect ladders — → IDEAS 2026-09-14 ("when
  bored"; census data stands in docs/config_pin_census.md).
- 29. Triangle `power` — 08-21; REVIEW 40.
- 30. UI piano dynamic octaves + top C — 08-21; REVIEW 41.
- 32. Listen-here silent on non-string excite nodes — 08-24; REVIEW 51.
- 33. Vibrato/bend on live-evolution wavetables — → IDEAS 2026-09-14
  (production path parked with BowedStringEvolution).
- 34. KSString physical-mode family — → IDEAS 2026-09-14 (superseded in
  spirit by the harness campaign + junction roadmap).
- 38. Live 2nd-note instant-attack — CLOSED 09-01, not a bug (perceptual
  masking).
- 55. Wormhole mini-face, no tap pin — landed 2026-09-06, roundtrip
  byte-identical.
- 57. Toggle/selector A/B node — → IDEAS 2026-09-14 (wishlist, no go).
- 58. Crackle revisit — → IDEAS 2026-09-14.
- 64. Duration-aware articulation TIER 1 — landed 2026-09-10 (4081879 +
  9152dc5): duration is a Note-face perform field; keyboard Live
  checkbox. Tier 2 folded into 68.
- 66. Delay compensation walk defects (a)+(b) — FIXED 2026-09-08 same
  day (inputs-only walk, kMaxMembers 16; r090 +389c → −22c); the
  ±1-sample residual lives in 65.
- Partial motion layer + v1-v4 batches; 16 kHz cutoff fix; UI
  array-restore fix; 96-partial extrapolation — pre-run-1 era.
