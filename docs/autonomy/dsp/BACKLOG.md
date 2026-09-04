# DSP lane — backlog (Dipsy)

Housekept 2026-08-22 (with Matt). Item ids are STABLE — reports and REVIEW
entries cite them — so ids are kept and items are ordered by priority within
each section instead of renumbered. Done/retired items live as one-liners in
the ledger at the bottom; their full text is in the run reports and this
file's git history. Tags per WORKFLOW.md. (G1)-(G4) = GOALS.md Dipsy goals.

## Active — engine/build

53. **[build] Live-keyboard mono mode — patch polyphony must be able to
    override the pool floor** — Matt 2026-09-02: a flute playing fast —
    in the world the previous note is cut off instantly when the next
    breath attack starts; live play should be able to behave that way.
    Today the UI floors the pool at LIVE_MIN_POLYPHONY=8
    (tools/mforce_ui/main.cpp:4307; loader takes max(patch, floor),
    engine/src/patch_loader.cpp:1625), so an authored polyphony 1 is
    ignored live and successive notes overlap in separate slots. Wanted:
    respect small patch polyphony (or a mono/voice-steal flag) in live
    play — note-on steals the sounding voice, last-note priority; watch
    for click on the cut (short fade-out on the stolen voice?). CLI
    renders already respect patch polyphony.

37a. **[build] Feedback-loop playability round** — Matt's 2026-08-31 evening
    hand-tuning verdicts (tweaked_57, patches/scratch, HIS sandbox — read
    only): in-loop SVF + hiss amp/density envelopes = "somewhere between a
    bow and a reed attack, slightly woody string sustain" — promising but
    "very fiddly". Work items distilled: (a) cutoff KEYTRACK (LogX curve
    from Note frequency, cutoff as multiple of f0) — fixed 6 kHz gives
    string lows / breathy mids / lung-condition-flutist highs (under-drive,
    same shape as KSString 34(c)) — MECHANISM LANDED 09-01: expressions-mode
    Curve (one Linear knot a=m) + compensate; demo loop_keytrack.json plays
    C3→A7 at constant |H(f0)|, ±5 cents (old fixed-cutoff ceiling was ~F7);
    ratio/family tuning by ear still Matt's; (b) ~~release on SVF cutoff~~ — Matt
    09-01: "doesn't really work"; SUPERSEDED by DelayLine `amplitude` pin
    (landed 09-01: read-side loop gain, the KS loss factor — release env
    on it ring-out-and-dies with pitch dead-steady, measured 260.2 Hz
    ±0.1 through the whole decay; demo patches/baselines/feedback/
    loop_amp_release.json, render in renders/dsp/pending/
    feedback_amp_release/); cutoff release stays available for treble-
    first *color*, no longer the decay mechanism; (c) drive sustain parameterized as
    offset-above-measured-critical per cell, making attack time a real
    axis — MECHANISM LANDED 09-01 in sweep r4 (gen_feedback_sweep_r4.py:
    bisection-measured critical per junction, 24/24 measurable, range
    0.05..1.93 — a 40x spread that explains r3's uncontrolled operating
    points; drive = crit*(1+offset) ramped from 0.95*crit over attack_s).
    Validated: bloom90 rises with ramp (0.25→0.40 s medians) and falls
    with offset (0.43→0.24 s). Run lessons: ramp must start just under
    critical (0.6*crit start spends the ramp inaudibly below threshold);
    measure attack note-on→90%, not 10→90 (swell delay is attack too);
    (d) audible in-loop breath (hiss 0.005-0.02 under its own decay
    envelope) — Matt's breath-integration principle, proven by ear on
    tweaked_57. Physics notes: cutoff modulation ⇒ pitch (filter phase lag
    is part of the loop period) — expressive scoop when wanted; the
    compensation debt is PAID as of 09-01 (phase_delay_at + DelayLine
    `compensate`, ~1 cent vs 40-78 flat; feedback_loop_design.md §5) —
    the fused-DampedDelayLine idea resolved composably instead.

37. **[research] Curve-space novelty sweep over feedback loops** — **r3 RUN
    2026-09-01 (the damped round)**: 100 cells on the playability-kit
    skeleton (keytracked SVF + compensate + amplitude release + in-loop DC
    blocker), curve families anchored on the four nature junctions (bow
    friction / reed table / jet sigmoid / lip valve, jittered) + r2's
    saturating + wild. **92 periodic / 1 chaotic / 7 dead** (r2: 66/17/17)
    — damping converts the buzz pile into pitched material. Top 15 by
    novelty-among-periodic → renders/dsp/pending/feedback_curves3/ (TOP.md
    describes each); un-jittered archetypes → patches/audition/
    feedback_archetypes/ + renders/dsp/pending/feedback_archetypes/.
    Run lessons: asymmetric junctions rectify DC into the loop (lip rode a
    +97%-of-peak rail, +185 cents sharp → HP1P 12 Hz in-loop blocker, the
    composable-compensation design absorbing its phase automatically);
    expansive (lip) curves need an explicit near-origin knot because
    smoothness easing zeroes endpoint slopes. **r4 VERDICTED 09-02: "nailed
    it, concept proven"** — full record docs/research/feedback_sweeps/
    R4_VERDICTS.md; flute002 → library/winds/flute_default.json; reed001a
    library-grade awaiting a name; winds > brass > strings (no bow attack);
    cross-cutting register problem = noise amp + drive ramp not keytracked
    (cutoff is). **r5a RUN 09-02** (gen_feedback_sweep_r5a.py): the
    keytrack round — 6 keepers × noise-exp {0,-.4,-.8} × ramp-exp
    {0,-.35,-.7}, transforms on Matt's saves (tweaks preserved; n00_r00 =
    control), new 5-note C3..C7 render format, 54/54 →
    renders/dsp/pending/feedback_curves5a/. Both axes measured live
    (hiss 56x C3→C7 flattened; attack 0.2s-flat → 0.38/0.04s tracked).
    Matt 09-03 interim: ramp curve maybe unnecessary; noise curve good,
    wants more aggressive → n90/n105/n120 extension rendered same day
    (18 cells, r00 only; caveat: power law boosts hiss BELOW home too).
    Matt's 5a verdict 09-03: ramp curve unnecessary; noise keytrack
    KEEPER, sweet spot exponent -0.80..-0.95 per patch, he fine-tunes by
    hand. **r5b RUN 09-03** (gen_feedback_sweep_r5b.py): reed001a
    deep-dive, noise kt baked at -0.85 — cutoff multiple {1.5,2,3,4.5,
    6,9} × drive shape {lin, blo=Expo2.5, ovs=+60% overshoot/240ms
    settle, tng=15ms spike} × junction jitter {j0 verbatim, j1-5 ±15%},
    criticals re-measured per (m,j) (36 bisections; spread tight,
    0.54-0.65 — and m3/j0 = 0.6252 vs the 0.631 implied by Matt's saved
    o30 drive). 144/144 → renders/dsp/sweep/feedback_curves5b/; 14-cell
    axis tour → renders/dsp/pending/feedback_curves5b/ (README inside;
    control cell = fb5b_m3p0_lin_j0, per-note peaks flat 0.083-0.087
    C3→C7). NEXT: Matt's tour verdict, then neighbors from the full
    grid. **09-03 eve steer (Matt): strings AND brass — both weak
    families — get the oboe treatment**: seed each round from a proven
    wind line (strings from oboe_again's junction/formants transposed to
    cello range — its bass sounds BOWED, Matt-pinned; brass likewise),
    plus junction-asymmetry-as-axis (r5c) and per-instrument reference
    analysis (research/ml_ears/oboe_ref_compare.py generalizes). Matt's
    09-03 library/winds saves (oboe_again, oboe_pumped) committed; he
    flagged straight-to-library as a protocol slip he'll sort himself.
    Matt 09-02 philosophy:
    per-range instruments (great oboe + great bassoon) over one averaged
    5-octave patch; keytrack still matters *within* the home range. Original brief follows —
    payoff run for the feedback-loop subsystem (docs/feedback_loop_design.md,
    shipped 2026-08-31): batch-generate seeded random Shaper curves inside
    the loop_selfosc / loop_bowed skeletons (vary breakpoint count, slope
    through zero, shoulder symmetry, bipolarity; also delay `ratio` pairs
    for inharmonic two-line loops), render via mforce_cli, filter with the
    novelty metric (research/novelty, run 5), surface survivors to
    renders/dsp/pending/ for Matt. The search space "all drawable
    nonlinearities inside resonant loops" is the literal "another KS" hunt —
    period doubling, subharmonics, multiphonics live at curve transitions.
    Start narrow: one loop shape, ~100 curves, listen before scaling.
    Reference behaviors to beat: selfosc blooms 0→limit cycle in ~70 ms;
    bowed sustains while drive>0.2/slope-5 and collapses below.

36. **[cleanup] Envelope dialect + preset-file consolidation (node types
    STAY)** — Matt 2026-08-29, after deciding against retiring the preset
    envelope node types (named settings are mapping/promotion targets —
    capability, not just convenience). Also carries (c, Matt same day
    "cleanliness"): formalize the lazy first-sample init idiom — a
    `first_sample()` hook called once after prepare(), replacing the three
    hand-rolled spellings (KSString `initialized_`/init_note, WavetableSource
    `ptr_==0`/fill_table, SegmentSource `pendingInit_`). The RULE behind
    them, stated once: prepare() runs before the pin chain computes for the
    note, so pin reads via current() in prepare are STALE — that exact trap
    caused the 08-29 SegmentSource nondeterminism (clicks/thumps per
    keypress on pin-driven width). Two cleanups that don't touch the
    type roster: (a) convert the legacy generic-Envelope
    `"preset":"ar"`-style params (algev patches et al.) to explicit stages
    at LOAD, paramMap→wiring precedent — kills one of three envelope
    authoring dialects, gate-provable byte-identical; (b) deduplicate
    envelope_presets.h internals — the six preset classes repeat the same
    curve/power member/descriptor/set/get/rebuild boilerplate (~200 lines
    after the 08-29 Curve/Power addition); a shared helper or small base
    for the per-stage ramp overrides shrinks it without changing any
    external name or behavior.

35. **[fix] Editor↔loader asymmetries on input pins — three ways a
    CLI-legal patch mangles in the editor** — found 2026-08-29 (Dipsy +
    Matt, grail post-mortem session). A patch that renders correctly via
    mforce_cli can load into the editor missing wires, constants, or
    engine defaults: (a) the editor reconstructs wires by EXACT pin name,
    so loader-side param aliases ("cutoff" → BWLowpassFilter.cutoffFreq,
    registered in source_registrations.cpp) drop the wire silently —
    Matt saw Combined→lpf.cutoff vanish; (b) numeric constants on
    input-descriptor pins (e.g. "source1": 8000.0) have no editor
    representation — inputOnly pins carry no editable value — so the
    constant evaporates on load and playback re-serializes without it;
    (c) update_node_dsp wires EVERY unconnected input pin to
    ConstantSource(0.0) (main.cpp first pass), clobbering engine defaults
    — WavetableSource's WhiteNoise inputSource default never applies in
    the editor, which silences value-conserving evolutions (Sort/HE,
    plain Pluck) that sound fine in CLI. Fix directions: alias table
    shared with the loader for (a); either reject or synthesize a Var
    node for (b); leave engine defaults in place for unconnected
    input-only pins for (c). Related trap, same session: Edit>Convert
    node→patch synthesizes a legacy paramMap frequency row that collides
    with a later Note-face wire ("not a ConstantSource" at play) — the
    heuristic should synthesize a Note face instead, and the Parameter-
    mapping dialog should stop accepting new bindings (residue-only).

19. **[build] hiBoost → explicit curve — the P4 cleanup sweep** — Matt
    2026-08-18 (PerformSource brainstorm): `PitchedInstrument.hiBoost` is a
    hidden loudness compensation — `gain *= 1 + (log10(max(f,100))-2)*hiBoost`
    at voice summing — a keytrack curve wearing a scalar's clothing. Convert
    to a visible transfer-curve wiring (CurveNode from NoteState frequency
    into the voice-gain path) so the last invisible per-note compensation
    becomes patch data. Null-test the conversion like the paramMap flip.
    P4 also carries the RangeSource migration (see
    docs/perform_source_design.md §7 + docs/pin_model_design.md).

28. **[build] Patch→Node→Patch conversion drops instrument extras** — found
    2026-08-20 chasing a phantom volume diff in Matt's WIP piano: the
    Edit-menu conversion lane (`--convert-roundtrip` drives it) loses
    `instrument.volume` because the node-graph form has no instrument block
    and the return trip SYNTHESIZES one from the heuristic instead of
    restoring the stashed extras. Same class as 3n save-fidelity. Plain
    load/save (`--roundtrip`, rt_smoke, interactive save) carry extras
    correctly. Fix: stash `s_loadedInstrumentExtras` across
    convert_patch_to_node_graph and merge into the synthesized block on the
    way back; gate by comparing instrument blocks through the lane.

3l. **[build] Modulated even/odd partial weight has no equivalent** — the
    last 7 lint findings, all in `patches/Squeaker.json`, whose
    AdditiveSource `evenWeight`/`oddWeight` are `{"ref": ...}` value
    sources. FullPartials exposes them as Float configs, so a modulated
    even/odd weight cannot be expressed post-decomposition. Deliberately
    NOT flattened to a constant (run 25) — that would silently change one
    of Matt's own patches. Options: promote the two configs to ValueSource
    params, reach them via the paramMap/curve mechanism, or accept the
    loss and rewrite Squeaker. Engine edit.

3i. **[build] Envelope hold/loop mode** — one-flag engine change in
    envelope.h::next() so UI streams can run truly forever (UI currently
    preps 7200s with absolute_time overrides; run-24 UI report has the
    sketch).

3p. **[build] FormantSequence1 is CLI-unrenderable and migrates silent** —
    found closing 3n. The baselines patch uses the LEGACY inline formant
    form (structs, no refs), which the CLI loader never supported ("key
    'ref' not found"); the UI loads it fine. Migrating via UI roundtrip
    produces valid ref-form JSON that renders SILENT (peak=0), so UI and
    CLI disagree about FormantSequence semantics beyond the format.
    Diagnose the silence (likely spectra wiring or blend path), then
    migrate the file; it is the roundtrip gate's only exemption until then.

3s. **[build] Resurrect SlewLimiterSource Peak mode as an envelope
    follower** — Matt-approved logging 2026-08-16. Run 21 built the
    Slew/Lag/Peak family; run 22 reverted it per Matt's "dead end" verdict
    on the CLICK use case — but Peak mode was a true envelope follower
    (|x| + asymmetric attack/release), which the AFP-31 study showed is a
    general-purpose block (AF uses it as cheap AD generator and step
    de-clicker). Future uses Matt named: AUTO-WAH (follower drives filter
    cutoff; SVFSource resonant LP is the natural partner) and SIDECHAIN
    DUCKING. Code is in git history (run 21, ~2026-08-05); resurrect Peak
    only, as its own small node (EnvFollowerSource?).

18. **[build] MIDI input off the UI-frame cadence** — Matt 2026-08-18: not
    critical (additive/KS CPU glitches dominate; polling latency "least of
    our worries"), queued for when live feel matters. Today pump_midi
    drains RtMidi once per UI frame → 0-16.7 ms jitter on top of the
    ~11 ms buffer floor. The move: trigger notes on RtMidi's WinMM
    callback thread via setCallback. Prereqs in place (instrument cache —
    note-on does no disk I/O on the fast path). Remaining work is thread
    discipline: prepare_voice + voice_schedule_unlocked under g_audioMutex
    from the MIDI thread is fine, but (a) last-note display globals become
    atomics, (b) no transport_set_status from the MIDI thread, (c)
    g_keyboard.duration read becomes atomic, (d) cache invalid at note-on
    falls back to queueing for the UI thread — worst case one
    frame-latency note after an edit. Do NOT drain MIDI in the audio
    callback (Unity pattern): prepare inside the render deadline risks
    underruns for zero latency win.

31. **[build] Lint gap: descriptors the loader never wires** — found
    2026-08-22 via Envelope minValue/maxValue: the params existed in
    `param_descriptors()` since P1 (08-18) but the loader's hand-written
    `Envelope` branch never called `wire_params_generic`, so the keys were
    silently ignored for four days and `lint_patches.py` could not see it
    (its allowlist is scraped FROM descriptors — a descriptor the loader
    ignores looks "consumed"). Every hand-written branch in patch_loader.cpp
    that constructs a type without calling `wire_params_generic` is a
    candidate. Add a check: for each registered type, instantiate, and for
    each descriptor confirm a JSON key with that name actually reaches
    `set_param` through the loader (build a one-node patch, set the key to a
    ref of a marker source, assert `get_param` returns it). Same failure
    class as backlog 14 and 26a, one level lower.


34. **[build] KSString physical-mode family — strings / flutes / horns on
    the piano's KS architecture** — Matt's direction 2026-08-24 after the
    ksbow verdict (REVIEW 52): "lots of character, lots of promise";
    KSString modes (and possibly a KSPipe sibling) over debugging the
    wave-evolution trio. BowedStringEvolution parked for comparison;
    Reed/BrassEvolution not being debugged. Known first problems from the
    verdict: (a) **attacks too slow for a string** — the bow-env 60 ms sine
    rise + friction startup read as horn; the `source` pin still takes
    excite4 attack textures (creak et al.), which was the design intent —
    fast bite + bowed sustain; (b) over-resonance in the a-cell config —
    resonance/drive balance knob work; (c) top octave: screech (a) or
    under-drive (b) — bow-side keytracking (bowSpeed/frictionGain/pressure
    curves per note) parallel to the existing t60 curve; (d) the
    middle-register trumpet-adjacency in b/c is a HINT for the horn mode,
    not only a defect — the junction at those settings is already lip-ish.
    KSPipe = jet/air excitation + open-pipe loop for flutes/recorders.

33. **[build] Vibrato/bend on live-evolution wavetables needs a real
    mechanism** — PRIORITY DROPPED 2026-08-24 evening: BowedStringEvolution
    parked (REVIEW 52), so live-evolution tables are no longer a production
    path; item stands for whenever they return. Found chasing Matt's
    headphone sizzle on the bow family. The fractional-read-head resample (KS bend Approach A,
    project_ks_bend) treats the table as frozen content; with a live
    evolution (bowed string) the table is the string's ring buffer, and any
    rateScale != 1 makes the reader lap the writer — the output sweeps the
    old-pass/new-pass seam continuously, measured as ~25-30 dB of
    inter-harmonic broadband at note 36 (vibrato depth 0.01!). Options:
    revive Approach B (loop-length modulation) for evolution tables;
    in-loop allpass tuning driven per-sample from the frequency pin;
    or evolution-aware bend routing (resample stays for frozen tables,
    waveguide modulation for live ones). Until fixed, vibrato on
    BowedStringEvolution = sizzle; the bow family r2 should decide
    direction WITH Matt (Approach B was parked deliberately).

## Design questions

27. **[design] Groups multi-output — control wires AND shared sources
    collide with the one-output rule** (absorbs 3q) — two hits on the same
    policy. (a) Matt 2026-08-20, piano control-strip cleanup: a Curve node
    can't move into a group when it feeds a target OUTSIDE the group —
    collapsed face has exactly one output pin, the two-output check
    refuses. Workaround: disconnect, move, reconnect. (b) Matt 2026-08-15,
    testing Groups: a WhiteNoise feeding two consumers straddling the
    boundary refuses ("2 outputs"), and the natural workaround — duplicate
    the noise node — SILENTLY CHANGES THE SOUND (independent RNG stream vs
    one correlated source via RefSource); he also overwrote Piano_bright
    doing it (restored from git, hash-verified). Real fix is multi-output
    group faces: one synthetic output pin per boundary-crossing source,
    projection maps per pin like inputs already do. Design question first:
    does a control tap (curve → setting elsewhere) belong in the audio
    group's interface, or should the face distinguish audio-out from
    control-outs (gold, per the pin model)? Minimum interim: warn in the
    Duplicate menu item that a duplicated generator gets a fresh stream.

26. **[design/build] Bend is inert on KSPianoString — and the missing
    block-level cadence** — found 2026-08-19. Two halves, do not conflate:
    **(a) Concrete bug, fixable today.** `KSPianoString::init_note()` reads
    frequency exactly once on the first sample (`ks_piano_string.h:384`);
    comb lengths are fixed for the note's life. `frequency_->next()` is
    still pulled every sample but nothing reads the result — a bend curve
    on a piano patch moves a number nobody looks at, silently inert (same
    failure class as silently-ignored params). WavetableSource by contrast
    tracks bend per sample via a fractional read head. Minimum fix: refuse
    or warn when a bend/pitch curve targets a node that cannot retune.
    26a warning landed with P3 (loud in both loaders); the deeper "can KS
    retune mid-ring" question remains.
    **(b) Design question, blocks nothing.** The engine has NO block
    cadence: only prepare() per note and next() per sample; the 512-frame
    block exists solely in the UI's RtAudio callback. Tier table: fixed
    (load) / per-note (dynamic pins) / **per-block (does not exist)** /
    per-sample (pins). Spec §6.6 parks the narrow version (`t60` tracking
    a bend — ~3 pows/block, per-source opt-in). The wide question: is a
    block tier worth introducing, and what is the hook — block_tick() on
    ValueSource, an opt-in flag, something else? Cost is wildly per-node:
    t60 is 3 pows; retuning comb lengths mid-ring is a different, much
    harder problem (delay-line length change artifacts). Musical note: a
    KS string has no decay *stage* — t60 is a rate, not a phase.

## Listen-prep

3r. **[listen-prep] Vowel review pass — soprano re-render + tenor O** —
    **Matt 2026-08-22 (curating library/voice): review ALL the sung vowels,
    not just soprano — "some of the vowels need additional work; I knew
    this about the 3 soprano vowels but tenor O is not quite right either."**
    Same day: every voice/ envelope had release 0.0 (= the release ramp
    filled the sustain region, and live key-up cut hard → click); swept to
    release 0.1, so the locked WAVs in renders/library no longer match
    these files — re-render before any A/B. Original soprano item follows.
    Soprano alt-formant A/B re-render — Matt (REVIEW 25
    response): he deleted the renders AND the pending patches, recalls "no
    good candidates" but wants a re-do to be sure, and invites variation
    arms based on that recollection. Regenerate the 4 A/B pairs from the
    run-25 recipe (alto formant table, unmoved, sung at A4; O pair as
    control) against the CURRENT library/voice soprano files, plus 1-2
    variation arms. Queue as a fresh [listen]. Context: run-25 measurement
    says separability collapses with f0 (bass 26.94 → soprano 14.80 dB
    mean pair distance; at 440 Hz soprano E and I share BOTH formant
    harmonics), so if no arm lands, the honest read is sampling-limited
    and the vowel front retires. Escalation option: vibrato via the
    pitch-mod layer (coherent FM is the strongest partial-fusion cue).

24. **[listen-prep] Does each attribute actually change the sound?** — Matt
    2026-08-19, on the attribute census: "being used by a patch ≠
    contributes materially to the sound." Prep A/B ladders isolating each
    suspect, queue for Matt's ears "when bored" — low priority, not a
    blocker. Suspects, strongest first (docs/config_pin_census.md):
    - HammerBank `harm1`-`harm4` + `numBands` — in all 147 patches,
      non-default in ZERO. Strongest candidate in the census.
    - KSPianoString `inharmHp` — present 83, non-default 0, never mapped.
    - FullPartials' 12 never-set shared attributes (motionScale,
      shimmerCoherence/Evolve/Floor, tradeDepth/Hz, onsetSpread/Tilt/Fade,
      decayRate, decayExp, inharmonicity).
    - Vibrato `threshold` + `zeroCrossTendency` — non-default in 0 of 48.
    Method note: "always default" does NOT mean unused when the attribute
    is MAPPED (ExplicitPartials.inharmonicity is default in all 4 patches
    that set it and curve-driven in all 4; KSPianoString.t60 mapped in all
    85). Check the mapped column before calling anything dead.

## Additive / scorer lane (metric debt + parked search)

3e-b2. **[metric] `derive_motion` takes no B** — the stretch-aware fix
    (run 25) never reached the stored `motion_medians` (verified identical
    before/after). Same treatment as motion_stats, plus the measured-line
    idea. Sub-note (b3-open): **F1 is still not trustworthy** — 6.4x
    cutoff-sensitivity at 21.45 c after the low-f0 cap fix; two attempts
    spent, per the 2-attempt rule annotated not retried. Likely needs a
    longer window (F1's 1.0 s sus = 1 Hz resolution vs 43.7 Hz spacing),
    not a different filter.

13. **[metric] Piano unison beat RATE — attempt 3, different method** —
    attempts 1-2 (run 18) both failed their own controls (decay curvature;
    floor-sensitive rates). Envelope-domain inference is the wrong tool.
    NEXT METHOD: resolve the unison strings as SEPARATE spectral lines —
    split by the beat frequency (0.6-2 Hz), directly resolvable by a ≥2 s
    coherent FFT in bass/mid — read the split, don't infer it. Usable now:
    single-strung B0/C1 modulate 5-9x below multi-strung, so shimmerDepth
    ≈ 0.15 is a defensible seed; shimmerHz/Coherence stay searchable.
    research/ml_ears/piano_beating.py, out/piano_beating.json + .png.

3b2. **[metric] Shimmer gain floor** — Matt-approved 2026-08-01: the
    shimmer walk at optimizer-chosen depth visits near-silence mid-note
    (measured 14.6 dB dip-and-reswell on v6_cmaes_best final note). Add a
    floor config (gain never below ~0.3-0.5), render a small ladder, queue
    A/B; keep total variance near the Iowa 50% target (floor
    redistributes, not removes). **Staleness flag (2026-08-22):** approved
    pre-Piano_bright; the additive lane is no longer the piano
    front-runner. Confirm still wanted before building.

3. **[metric] (G1a) CMA-ES endgame — PARKED pending re-scope** (absorbs
    item 2, Iowa motion-param derivation, superseded by the measured
    refmetrics/motion_stats path). The staged core is DONE (spec run 2,
    scorer CLI run 3, optimizer loop run 3, smoke 1.28→0.945; render cost
    solved run 14, 600 evals ≈ 50 min). What blocks a full run is no
    longer speed but scope: the objective was re-baselined across runs
    21/25 (22.5% of it was measurement artifact), Piano_bright made KS the
    piano baseline, and the UI renamed node ids the encoder targets.
    Standing prerequisites before any 600-eval spend: retarget renamed
    node ids + verb-free objective; eval notes must cover searched curve
    regions; settle the cutoff-gate/shared-rng discontinuity (REVIEW 0b —
    a last-bit param change can re-roll the bandwidth noise a candidate is
    scored on). Viola vs piano vs clarinet target is Matt's pick.

8-open. **[metric] (G4) Additive performance — stages 2d/2e** — landed
    stages took the hot loop 90.6 → 26.1 ns/sample/partial (runs 12/14).
    2d explicit SIMD: isolated AVX2 prototype passed its abort criterion
    18-19x, but a layer-free vector path reaches only ~51% of flagship
    patches (bandwidth's shared-rng walk re-rolls if vectorized) —
    restaged 2d-1..2d-4 in specs/2026-07-31-additive-simd-soa-design.md;
    2d-3 (per-partial rng streams) gated on REVIEW 0b. FOUR anti-results
    recorded in the spec/backlog history — don't retry: branchless
    full-period sin, 4x unroll, reciprocal-for-divide, pow fast path.
    2e iFFT overlap-add: structurally different synthesis, NOT
    bit-identical → review:listen when attempted.

## [read] Questions parked on Matt

22. **Vibrato lost its user-supplied speed/depth envelopes** — legacy
    `SetSpeedEnvelope`/`SetDepthEnvelope` became a hardcoded internal ramp
    (vibrato.h:121). Only instance of the pattern in the legacy tree;
    47 patches use Vibrato. Restore as pins, leave hardcoded, or fold into
    the config-chain design? Nothing blocked on it.

23. **CombinedSource lost its connectable Amplitude** — legacy had
    `Amplitude` as a connectable final multiply alongside scalar GainAdj.
    Expressible today as an extra multiply node (cost, not wall);
    CombinedSource is everywhere (138 patches). Restore the pin, or is
    the extra node the honest spelling?
    > Sweep provenance for 21-23: 31 legacy classes expose 82
    > `Set*(ISingleValueSource)` setters; zero were demoted to configs;
    > these were the whole delta.

25. **[REVERSED 2026-08-23: THE BOW LIVES HERE] Wave-evolution revisit** —
    BowedStringEvolution + RedNoise on the bow pin (accidental UI defaults)
    = Matt: "a pretty fucking nice bowed string... mind blown." NOTHING
    retires until each type has been tried WITH A REAL EXCITATION INPUT —
    the old "amounted to nothing" verdicts were made without one.
    Discovery patch: patches/baselines/bow_evolution_discovery.json
    (+ Matt's live copy in pending/). Next: (a) bow family sweep — density
    (hesitant character), RedNoise frequency + bow position (brightness),
    boost/zct (TBD), per Matt's notes in the oneshot_sweep log; (b) revisit
    ReedEvolution and BrassEvolution with proper inputs (Reed "does
    nothing" at defaults — so did the bow until RedNoise landed on the
    right pin). Original text follows for the record.
    (was) **Retire or keep the wave-evolution experiments** — the old
    breath/bow grail hunt. Usage: BrassEvolution 0 patches, Averaging 0,
    BezierPull/EKS/ReactionDiffusion/CellularAutomaton/HistogramEqualize
    1 each, ReedEvolution 3, BowedString 5. Retiring the 0/1-patch types
    is cheap and shrinks the config-pin surface. The RD audition-path
    mismatch means some were never heard properly — argues for a listen
    before retiring BowedString (5) and Reed (3).

3j. **WanderNoiseSource deltaSpeed semantics** — applied per SAMPLE, so
    audio-rate wander self-cancels (slope flips every ~3 samples); legacy
    init lastVal_=0.5 starts right-of-center. Candidate
    rate-normalization; behavior-changing for existing wander patches.

3d. **Pan-law question** — CLI WAVs are -3 dB vs UI (equal-power center
    pan in StereoMixer vs unity mono). Option: mono patches write x1.0 to
    both channels so WAV loudness == UI loudness.

## Done / retired ledger

One line each; full text in run reports + this file's git history.

- 1. fable1 thread + autonomy scaffold committed — early runs.
- 2. Iowa motion-param derivation — superseded; folded into item 3 re-scope.
- 3b. UI paramMap curve preservation — run 5.
- 3c. UI engine-stamp guard — run 14 (cc4185d); FP fixed run 18 (9c2946e);
  closed run 19. Title-bar stamp + `--stamp` headless check.
- 3c2. FMSource dead `phase` param — run 19 (fbbcb44); real PM sidebands.
- 3e. Piano engine features (inharmonicity, per-partial decay) — run 20.
- 3e-NEXT a/b/b3. Scorer debt: empty-band blowup run 21 (85a4015);
  stretch-aware masks run 25 (bc11389); low-f0 heterodyne cap run 25
  (bcf3413). Net: unchanged patch 1.1879 → 0.9202 — 22.5% of the objective
  was measurement artifact.
- 3e-NEXT2. Piano recovery bundle — run 23.
- 3e-NEXT3 + 3h. Additive-piano / KS-v5 iteration items — RETIRED,
  superseded by Piano_bright (Matt's hand-tuned KS baseline, 08-14).
  Reopen only if the additive piano lane revives.
- 3f. CombinedSource "sum" + ordinal 3 — run 21 (b08d795). Also closes
  item 15's residue: ordinal 3 = Sum; the REVIEW 10 ask is dead.
- 3g. SlewLimiterSource — built run 21, REVERTED run 22 per Matt ("dead
  end"); Peak-mode resurrection lives as 3s.
- 3k. Audio-thread races — run 26 (2f7bbba). RESIDUAL: envelope
  stage-editor + Curves-tab breakpoint edits still mutate live objects
  unlocked; same fix pattern if a crash ever names them.
- 3m. `<curve>` badges for curve-superseded scalars — 08-14 (3246ffe).
- 3n. UI save round-trip fidelity (34 patches) — 08-15; five root causes;
  gate 196 patches, sole exemption = 3p.
- 4. Novelty metric — run 5 (research/novelty/).
- 5. Recursive partial expansion — rounds 1-4, runs 5-18; depth measured
  second-order (levers are loPct/spacing/rule-breakers); front retired.
- 6. FormantSequence deep-dive — run 6.
- 7. Oversampled FM + modulate-everything matrix — runs 8/12; alias
  suppression 24.5/34.7/39.6 dB at M=2/4/8; low-carrier non-convergence
  premise correction recorded.
- 9. Six algev patches converted to instrument-style — run 8.
- 10. Vowel/formant re-tune — closed run 25; 7 winners locked into
  patches/library/voice/ (lock_vowel_winners.py), byte-identical to
  auditioned WAVs.
- 11. Standing dirty patch files — closed 08-20 (Matt's 5c5c139 reconcile;
  nothing left to commit).
- 12. `power` inert at count=1 — Matt's verdict "leave it"; documented
  degenerate case stands (regression pair proves count=2 works).
- 14. Silently-ignored params — run 25 (2f39819); 66 → 7 findings (59 were
  the linter), allowlist now scraped from loader sources; survivors = 3l.
- 15. 7 CLI-unrenderable patches — 6/7 run 19 (6fc128b), three distinct
  loader bugs; CombineTest residue closed by 3f.
- 16. Soprano E/I/U tunability — superseded by 3r (REVIEW 25 folded).
- 17. UI live-audio round trip — instrument cache + voicePool 08-18;
  per-note load 466 ms → cached.
- 20. PerformSource P1-P3 — landed 08-18..20 (~26 commits + same-evening
  Opus-5 audit keeping all, 4 edge fixes); Matt hands-on 08-20; residual
  = REVIEW 39 (wheel/pressure live).
- 21. WhiteNoise density/boost/continuity + zeroCrossTendency — 08-21.
- 29. Triangle `power` (symmetric; `asymmetric` flag) — 08-21; REVIEW 40.
- 30. UI piano dynamic octaves + top C — 08-21; REVIEW 41.
- 32. Listen-here silent on non-string excite nodes — 08-24, two causes:
  starved RefSource outside the tap cone (loader promotion fix, null gate
  180/180) + instrument volume crushing raw-excitation taps (UI monitors at
  unity now, Matt's call); REVIEW 51. En route: null-gate manifest was
  stale since the 08-22 voice release-bump (8657bae) + curation — re-frozen
  at 180 entries.
- 38. Live 2nd-note instant-attack — CLOSED 09-01, NOT A BUG: Matt couldn't
  repro on other patches or on the original loop patch next morning;
  perceptual — attack transients partially masked by the noise bed of the
  already-sounding note. Engine had already been exonerated by CLI probe.
- Partial motion layer + v1-v4 batches (docs/Fable1_results.md); 16 kHz
  cutoff break→continue fix; UI array-restore fix; 96-partial
  extrapolation — pre-run-1 era.
