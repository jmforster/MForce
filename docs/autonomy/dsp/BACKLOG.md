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

72. **[research] Loop-family ignition is duration-marginal and
    noise-realization-sensitive — fixed seeds mask it in every gate** —
    diagnosed 2026-09-16 from Matt's clarinet_att2 passage flutter (live
    keyboard fine, passage notes 2+ flutter). NOT an engine bug: per-note
    reset proven thorough (control strips sample-identical across notes;
    pre-tail engine fails identically, so not a backlog-63 regression).
    Mechanism: percent-mode envelopes compress attack ramps with note
    duration; att2's junction basin needs an absolute-time ramp, so
    ignition at 1.5 s prepared duration succeeds only ~1/12 for random
    noise state — and EVERY first-note/fresh-clone render uses the fixed
    virgin RNG seed, which happens to be a lucky draw (why note 1 always
    locked and sweeps/gates never see the marginality). Live keyboard is
    immune because live notes prepare at transport duration (2.0 s
    default) regardless of played speed. flute1 has a milder version
    (0.65→0.11 f0-band frac on clone reuse); c8_fast/oboe1 immune at
    their operating points. Mitigation validated in scratch: minSec
    floors on Drive_env/Ampl_env attack stages (0.35/0.12) take the
    0.556 s 12-note case from 0/12 to 9/12 locks and 1.5 s reuse to 2/2
    — residual is drive-floor-vs-threshold operating point (r5c axes,
    JUNCTION_OPERATING_POINT.md). Patch tweaks are Matt's; the campaign
    lesson is methodological: machine sweeps rate ignition on one lucky
    seed — a robustness gate should probe notes 2+ (continued RNG) or
    varied seeds. Diagnostic tool worth keeping: gencheck strip dump to
    .f32 (was a scratch-worktree hack, ~8 lines in gencheck's strip loop).

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
    design. MATT'S 09-16 VERDICTS FOLDED: Brass/Saxofony/BlowHole
    ABANDONED for now; BandedWG is the keeper — ROUND 5 RUN 2026-09-16:
    cutoff click root-caused (fixed 0.4 s tail cut ringing bars —
    adaptive ring-out shipped, ledger 63b) and percussion round 1
    staged (tools/gen_bwg_perc1.py, F&R mode sets at 48 kHz, 7/8 cells
    pass gates; tbar_bright machine-culled as runaway). Round-1 verdict
    same day ("all loooong ambient — TomDrum sounds like a gong; need
    characteristic envelopes or ultra-rapid decay") → ROUND 2 same day
    (gen_bwg_perc2.py, REVIEW 62): per-mode gains = frequency curves
    targeting constant T60 per cell (fixed gains made T60 ∝ period);
    measured calibration T60/4 documented in the script; 7/7 pass.
    Bowed-cell note for the next bowed round: the "cut off before fully
    bloomed" complaint = the gen scripts bake bow-stop at ~4.5 s in
    seconds-mode AdsrEnv — switch to gated/hold envelopes so held notes
    keep bowing. NONLINEAR BORE GREENLIT by Matt 09-16 ("if we need a
    non-linear bore, let's do it") — future campaign front, donors in
    donor_survey_2026_09_16.md. 1D PIERCE PROBE RUN same day
    (gen_pierce_probe1.py → REVIEW 63): sign-dependent damp cutoff on
    a KS pluck, dose-response 0/0/0.06/0.56/2.35/1.51 across swing
    0..12k — TRUE modal upwelling at 8k with a dead control; Mesh2D
    evidence gate MET (port round awaits the REVIEW 63 go). Probe
    lesson: Chafe's edge filter is an ALLPASS — dynamic-radius
    magnitude filters either kill the string or run away (v1);
    cutoff-by-sign is the mechanism in our pins. Next: perc axis round
    on 62 verdicts, bandedwg-as-excitation, Mesh2D port on 63 go;
    bowed thread continues toward cello, GATED on the whistle-top
    diagnosis (all ext3 cells lose string attack character above
    mid-register — measure what breaks: bow-friction operating point vs
    delay length, capture/friction not keytracked, Helmholtz collapse).
    ext3 btd_lar rejected; body_lar ≠ upgrade over body_m48, no default
    change. LATER ROUNDS: leaky-stick hysteresis round; canonicalize
    surviving families to 48k; Mohonk torsion/dispersion features;
    compensate-ON + drive exploration past STK's ranges. STOP CONDITIONS:
    a round with zero gate-passing cells ends its sub-thread (2-attempt
    rule per mechanism); the campaign parks when the filter/body work
    lands and Matt's bench has what it needs for hand-tuning.

73. **[research] New physical-model donors (Matt, REVIEW 59)** — DONE
    2026-09-16, verdicts in docs/research/donor_survey_2026_09_16.md +
    REVIEW 61: DAFx'04 FTM brass NO-GO (linear bore — freq-independent
    damping, zero radiation load; lip valve is our ceilinged class;
    small steal: lumped mouthpiece two-pole as a Biquad, zero code).
    Chafe 2019 2D-mesh GO: STK Mesh2D + published constants (edge
    allpass fc 1575/R; pie-pan r=0.75+0.2x; Pierce sign-dependent
    stiffness s=-0.5/+0.003), 5x realtime at the real plate's 25x6.
    NEXT STAGE (Matt-gated via REVIEW 61): zero-code 1D Pierce probe
    (loop-termination Biquad radius pin ← signal via Curve), then the
    Mesh2D port round on evidence.

74. **[campaign] Brass — the one-mass lip line, toward a usable trombone**
    — opened 2026-09-17 (REVIEW 66/67/68), first full assembly 2026-09-18
    (REVIEW 69, reports/2026-09-18-trombone1.md), second pass same day
    (REVIEW 70, reports/2026-09-18-trombone2.md, tools/gen_trombone2.py),
    third pass same day (REVIEW 71, reports/2026-09-18-trombone3.md,
    tools/gen_trombone3.py) — zero engine code in all three.
    MATT'S VERDICT AT ATTEMPT 2: "Yes, I'd play it. Decent trombone in the
    lows and trumpet in the highs... Line beats line_brighter. Nice mellow
    tone. Further refinement possible, of course, especially the blowing
    harder thing." The 1100 Hz "line" voicing corner is therefore the
    instrument's, fixed.
    WHERE IT STANDS: the published one-mass lip (Berjamin
    arXiv:1511.04247 §3.1) drives a trombone-length air column whose
    partial is chosen by the lip, terminated by Smyth & Scott's measured
    trombone bell (EURASIP 2011:151436, solved from its Table-1 geometry
    — the paper publishes no numeric filter), with the REVIEW-66
    steepener between them, and a voicing lowpass after it. Four measured
    per-note maps ride the played note: lip ratio (widest speaking
    window), air-column trim (equal temperament), breath support
    (ignition threshold), output trim (register level).
    Measured at attempt 2: **51/51 chromatic notes midi 29–79 (house
    F2–G6) speak within 1.9 cents**, register level spread 14.3 dB → 0.0,
    queue loudness matched to patches/library/winds/oboe1.json (worst cell
    −4.8 dB, was −18.9), no tuning or speak-time regression against
    attempt 1. Candidate at
    patches/audition/trombone1/trombone_attempt2.json awaiting Matt.
    RESOLVED at attempt 2: the "C2 does not sound" report was a NOTE-NAME
    mismatch, not a live-path bug — the UI names notes by midi/12 and the
    round-1 tooling used midi/12−1. mforce_cli and mforce_ui --gencheck
    agree to 1e-6 on the same patch; `gen_trombone2.py parity` is the
    standing check. Also resolved: the top two notes would not ignite from
    silence (only in a line, inheriting the previous note's energy) — the
    breath map fixes it.
    RESOLVED at attempt 3 — (a) **dynamics barely change timbre**, the
    long-standing 74a. It was not the physics; it was two gain stages of
    mine. (1) gen_trombone2.calibrate() peak-normalised INTO the steepener
    per cell and un-normalised after it, so soft and loud drove the
    nonlinearity to the same 0.800 peak by construction; ablated, the
    steepener contributes +6 Hz of centroid at S=0.2 and +65 Hz at S=3.0,
    i.e. it is a dynamics component the moment it sees dynamics. (2) the
    instrument was permanently at forte, because one global pressure had
    to keep G6 (72 kPa) alive while A#3 plays in tune over 2.7–53 kPa.
    Fixed: DRIVE_GAIN is a constant 1.0 (the steepener sees the raw
    travelling wave; verified the output trim does not move NL_in by 1e-6)
    and blowing pressure is per-note, velocity-driven, solved from each
    note's own measured floor and ceiling, with velocity 0.8 pinned to
    attempt 2's exact working point so the gates hold by construction.
    MEASURED, centroid pp→ff: F2 +0.6%→+1.3%, A#3 −0.3%→**+18.5%**, F5
    −0.4%→**+27.0%**; >1 kHz share ×0.98→×3.44 (A#3) and ×1.02→×8.42
    (F5); level pp→ff +4 dB→+23 dB. Attribution measured separately: the
    per-note span alone buys +5.3%, removing the normalisation triples it.
    Also resolved at attempt 3: the LIP was not the limit (it is shut ~28%
    of every cycle at pp and its pressure rise rate goes up 28× on A#3 and
    53× on F5 pp→ff, so no lip parameter was touched), and attempt 2's
    exact-0.0 dB register flattening is out per Matt's standing directive
    — the trim decomposes into a 15.1 dB smooth ramp and ±4.9 dB of
    note-to-note scatter with 5.0 dB jumps between ADJACENT semitones; the
    scatter (unambiguous artifact) is still corrected in full and 65% of
    the ramp, leaving 5.3 dB of register slope in the signal.
    VERDICT AT ATTEMPT 3 (09-19): "Line is still good, no regression...
    the loudness/brightness linkage is there, but it's subtle, and *only*
    there in the attack - once the notes settle, their sustain phases
    sound identical to me, just louder." Full verdict verbatim in REVIEW
    71's resolved stub. Attempt 4 dispatched same day.
    OPEN, IN PRIORITY ORDER: (a5) **the brightness linkage lives only in
    the attack — settled sustain is timbre-identical, just louder**
    (Matt's ears 09-19, "feel free to check this empirically"). Verify
    time-resolved first: centroid in an attack window vs a settled window,
    pp/mf/ff, and the travelling-wave amplitude AT the steepener input in
    the settled phase (probe via gen_trombone3.probe_signal). Then
    attribute: does the loop self-limit to the same sustain amplitude
    regardless of breath, does the a4 ±8 clamp rail flatten the top, or
    does the steepener see the difference and act too weakly? Real brass
    brightens with sustained level, so this is physics owed, not polish.
    Attempt 3's deferred work order 5 (steepener depth riding the breath)
    is sanctioned exactly if the measurement says the loop genuinely
    saturates. (a6) **the low-register rattle, REOPENED on new evidence**
    — attempt 2's closure measured lock-time and periodicity and stands
    as measured, but Matt's 09-19 octave map describes a different thing:
    an audible oscillation in attack AND release tail, frequency-linked
    (too slow low, right in octave 5, too fast at the top of octave 6),
    with a formant sweep "B-wAWWW"→"B-wAHHH" from G2 to C3 and a release
    rattle that compresses to a terminating buzz by G4 and a feature by
    C5. Measure the modulation rate of the attack/release envelope per
    note and match candidates against his map: bore round-trip rate
    (three-period bore ⇒ ~f0/3), the per-note lip resonance, the breath
    ramp constants, post-key-up bore ring-down. His map is the ground
    truth; fresh 2-attempt budget for the reopened claim. Also
    characterize what the patch does BELOW the solved range (maps start
    at F2/midi 29; he plays down into octave 1 and gets rattle-only).
    (a7) **intermittent squeak at max velocity** — new at attempt 3
    (velocity 1.0 drives each note's measured pressure ceiling); suspects
    are the a4 clamp railing 53–69% of the cycle at ff and overblow past
    the intended partial. A lower velocity→pressure ceiling is an
    acceptable fix — 0.8's "not-great trumpet player" squeak he likes.
    (a3) **the low register still does not open
    up** — F2 moves +1.3% because its usable breath span is 3.8:1 against
    A#3's 13.7:1: below ~26 kPa it goes sharp and then falls into a
    pressure hole where it stops speaking. Needs the lip/bore pairing
    looked at, not another map. (a4) **the engine's global ±8 value clamp
    (core/dsp_value_source.h) is inside the lip's operating range at ff** —
    the displacement rails for 53–69% of each cycle, and the POSITIVE rail
    matters because flow is proportional to the opening, so the top of the
    dynamic range is squared off by a safety clamp rather than by physics.
    Either an engine round or a rescaling of the lip's normalised units.
    Logged 09-18, no engine code that round. (b) **G6 needs
    72 kPa to ignite** where everything below B5 needs 8 — a real player
    does not need nine times the breath for a top note; that is a property
    of this lip/bore pairing. Attempt 3 measured the consequence: G6's pp
    is 92 kPa, louder than most of the instrument's ff, and its whole
    dynamic span is 2.3:1. (c) the slide is `round(f0/58.27)` rather
    than seven real positions, and the instrument can play notes a real
    tenor cannot. (d) no viscothermal loss on the ~2.7 m of cylindrical
    tube in front of the bell. (e) the loop sign convention is still
    non-inverting; a real bell reflection inverts at low frequency and
    adopting it changes which partials the tube supports. (f) nothing is
    fitted to a real trombone recording — the ML-ears/viola machinery is
    the obvious next validation. (g) **no shared note-naming helper in the
    repo** — the UI's house convention and the tools' scientific naming
    coexist and cost a round; a one-function fix whenever a tool touch is
    due anyway.
    CLOSED at attempt 2 (measured, not assumed): the low-register "rattle"
    has no separate onset defect that two hypotheses could find — the
    sustain is as periodic at the bottom as the top (residual 0.02–0.05
    everywhere) and the bottom reaches periodicity FASTER (10–50 ms vs
    90 ms at C6). Both first measurements were metric bugs of mine (fixed
    harmonic count; comb tolerance narrower than the analysis window's
    mainlobe) and the corrected metrics live in gen_trombone2.residual /
    lock_time. 2-attempt rule invoked; reopen only on new evidence from
    Matt's ears. REOPENED 09-19 as (a6) above — his octave map IS that
    new evidence, and it names a different observable (audible attack/
    tail oscillation rate) than the lock-time/periodicity metrics this
    closure measured; the closure's numbers themselves stand. Also measured and REJECTED: a keytracked voicing corner —
    it flattens brightness-relative-to-pitch (13.4×/1.9× → 4.1×/1.8×) but
    takes the register level spread from 13.1 dB to 36.5 dB by stripping
    the low notes of the only band they have energy in.
    MEASUREMENT NOTE worth reusing: a probe that renders an internal graph
    node is multiplied by the voice mix gain (velocity × volume) exactly
    like the real output — attempt 3's first pass read a soft note's lip
    as never closing because of it. gen_trombone3.probe_signal() divides
    both gains out and is the reusable form.
    STOP CONDITIONS: a round with zero gate-passing cells ends its
    sub-thread (2-attempt rule per mechanism); the campaign parks when
    Matt has a trombone he calls usable, or when two consecutive rounds
    fail to move his verdict.

75. **[bug] Passage renders cut the last note off abruptly — a trailing
    Rest does not help** — Matt 09-19 (REVIEW 71 verdict), auditioning
    Ode2Joy and shorter Passages in the UI. Suspects, in order: the
    transport/Generate window ends at the last EVENT rather than honoring
    a trailing Rest when computing score end; the voice tail allowance
    (kVoiceTailSec=0.4, backlog 63) not reaching the final voice on the
    passage path; Play-buffer length truncated to score end. Localize
    with the unified Generate: mforce_ui --gencheck vs mforce_cli on the
    same passage — if the CLI WAV rings out and the UI buffer doesn't,
    it's the window; if both cut, it's the loader/score-end math.
    Diagnosis is coordinator (Fable) work, queued behind the attempt-4
    dispatch so the build isn't rebuilt under a running render batch.

76. **[perf] UI chokes on trombone-scale patches (55 nodes)** — Matt
    09-19 (REVIEW 71 verdict), his laptop: wire drawing "super
    sluggish," five velocity-slider clicks queue up and count down over
    multiple seconds, noticeable key-press-to-sound delay. Profile with
    trombone_attempt3.json loaded; prime suspects: per-frame waveform
    strips across a 55-node graph, full-graph redraw per frame, live-path
    voice setup cost. The queued-clicks symptom smells like work done
    per-event on the UI thread (each click re-triggering something
    heavy), which would also explain key-to-sound latency. Related but
    separate: Matt added auto-grouping/auto-layout + a Patch node to
    GOALS himself (organization features); this item is the raw
    performance.

77. **[tool] Post-success patch audit — which knobs do anything, then
    strip the rest** — Matt 09-19, from hand-tweaking trombone_attempt3:
    "most tweaks I made *did nothing*. I set MaxValues to zero, other
    things to crazy numbers - no impact on sound... when we achieve
    success on a patch, there oughtta be one more step - *auditing* it to
    see which knobs actually do something, and stripping out the
    extraneous ones that got bolted on in the quest for the target
    sound." Mechanize the attempt-3 procedure that found the two dead
    gain stages: for every settable descriptor in a patch, render
    baseline vs perturbed, report audible effect size; for measured-dead
    candidates, strip and prove the render unchanged (null-gate
    discipline per knob). Two hard requirements: (1) sample the operating
    space — a few notes × velocities — before calling anything dead (the
    steepener is inert at any FIXED velocity; a single-point audit would
    strip the dynamics component); (2) classify, don't just cull:
    genuinely-extraneous (strip), masked (e.g. the per-note baked maps
    override upstream hand-tweaks by design — document, don't strip),
    and live-path bug (setting never reaches render — fix; backlog 31 is
    the known kin). Output = a per-knob table Matt can read before
    hand-tweaking, plus a minimal-patch candidate.

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
- 63. Voice tail allowance — SHIPPED 2026-09-15: every voice renders/
  lives kVoiceTailSec=0.4 s past duration (instrument.h); envelopes are
  0 past their last stage so the window is pure ring-out; musical prep
  still uses un-extended duration. Verified on piano_default: reverb
  tail now decays to -66 dB with zero step at voice end (was a
  mid-ring cut). Containment check moved to tail end. 15/79 gate
  patches ring past duration and re-froze; 64 byte-identical. The
  trailing hold-at-zero-stage workaround is obsolete. NOTE 2026-09-16:
  trailing hold-at-zero stages were NOT harmless in existing patches —
  percent stages steal sounding duration (Matt's staccato complaint);
  he purged them + shortened releases (winds + viola, committed
  72be940).
- 63b. Adaptive ring-out — SHIPPED 2026-09-16 (5dd07d7): a voice still
  above kRingFloor (-60 dBFS running peak) at the fixed tail's end
  keeps rendering/living until quiet or kMaxRingSec=8 s past duration;
  live callback extends in 100 ms chunks (Voice.ringEnv/ringBudget).
  Fixes REVIEW 59's BandedWG mid-ring cutoff click (bars rang seconds,
  window was 0.4 s). Quiet-enders byte-identical (71/79); 8 reverb-tail
  DIFFs refrozen deliberately. Same day: patch_loader honors an
  explicit `seconds` LARGER than score end (was silently overridden —
  ring headroom was inexpressible); 13 gate patches with dormant
  seconds fields lengthened, refrozen. UI passage Generate still clips
  the last note's ring at score end (frames = score end in
  generate_unified) — mild, pre-existing; fold into any future
  transport ring-headroom ask. SAME-DAY ADDENDUM (Matt's re-audition:
  "still hear the hard cutoff" on struck bowl): KEEP verdict per his
  conditional — the residual was the 8 s CAP chopping near-lossless
  resonators; cap now FADES 80 ms (offline + live), one deliberate
  DIFF (piano_seg) refrozen. Ambient ring-forever sounds are
  Stream-mode citizens per Matt; cap+fade is the notes-world backstop.
- 67. Loader unknown-key warning — SHIPPED 2026-09-15: wire_params_generic
  warns once per (type, key) on keys matching no descriptor and no
  branch-consumed allowlist entry (allowlist corpus-verified: 133
  patches load warning-free, all 16 flagged classes proved
  branch-consumed, none dead; positive control fires). Null gate 79/79
  byte-identical; both binaries rebuilt, --stamp 0.
- Partial motion layer + v1-v4 batches; 16 kHz cutoff fix; UI
  array-restore fix; 96-partial extrapolation — pre-run-1 era.
