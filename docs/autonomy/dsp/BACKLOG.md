# DSP lane — backlog (Dipsy)

Priority order. Tags per WORKFLOW.md. (G1)-(G4) = GOALS.md Dipsy goals.

1. **[build] Commit the fable1 thread + autonomy scaffold** — motion layer,
   cutoff fix, UI array-restore fix, patches/generators/docs, docs/autonomy/.
   Scoped commits per concern. Unblocks clean iteration on everything else.
2. **[metric] (G1a) Derive motion-layer params from Iowa samples** — measure
   per-harmonic line width (frequency jitter spectrum) and amplitude-
   fluctuation rate/depth/cross-harmonic correlation from the Iowa viola set;
   map measurements onto motionDepth/motionHz/motionCoherence/shimmer*;
   produce an "iowa_grounded" patch vs the hand-tuned v4 winner. Queue A/B.

3. **[metric] (G1a) CMA-ES optimizer core** — the ears project, staged:
   (a) spec ✓ run2; (b) scorer CLI ✓ run3 (refmetrics.py + iowa_reference.py +
   score_candidate.py; stage-a check passes); (c) optimizer loop ✓ run3
   (cmaes.py self-tested + optimize.py, 27-dim warm-started encoder, 100-eval
   smoke 1.28→0.945, every term improved); (d) **viola validation run** — the
   600-eval run whose best-of-run WAV goes to REVIEW. NOTE (run 4): Matt's freq-curve verdict adds curve knots as search dims (+4-6) once default curves are picked from the v6 audition. **Preliminary result
   queued** run3: smoke best (104 evals) re-rendered on the full ladder →
   REVIEW A/B (renders/cmaes_smoke/opt_best_104ev vs v5_04). **Blocker EASED
   run14:** option (iii) happened — v6_cmaes_best renders 16.1s → 8.9s (1.81x)
   from the truncf + fast_exp2 commits, so 600 evals is now ≈50 min rather
   than 1.5-2.7h. Options (i) --resume and (ii) maxPartials 48 remain
   available if that is still too long. **NEW blocker surfaced run14:** the
   cutoff-gate/shared-rng fragility (REVIEW 0b) makes the objective
   DISCONTINUOUS — a last-bit parameter change can re-roll the bandwidth noise
   the candidate is scored on. Worth settling before spending 50 min of
   search. Still GATED on Matt's A/B read (metric pointing right? re-pin
   vibrato?) before committing the full run.
3b. **[build] UI support for paramMap curves** — ✓ DONE run5 (2026-07-29).
   Minimum landed: load stashes the paramMap verbatim, save carries forward
   curve-bearing entries remapping node ids through the rename map, so
   load→save preserves curves + config targets and keyboard playback (temp
   file → CLI loader) honors them. Verified via new headless `--roundtrip`
   mode on all four v6 patches. v6+ patches are now safe to re-save from the
   UI. **Still deferred (Later):** a visual curve editor on Parameter node
   links — until then, editing a curve-bearing Parameter node's wiring in the
   UI is not reflected (verbatim entry wins).

3b2. **[metric] Shimmer gain floor** — Matt-approved (2026-08-01): the
   shimmer walk at optimizer-chosen depth visits near-silence mid-note
   (measured 14.6dB dip-and-reswell on v6_cmaes_best final note). Add a
   floor config (gain never below ~0.3-0.5), render a small ladder on
   the cmaes-best patch, queue A/B. Statistics note: keep total variance
   near the Iowa 50% target if possible (floor redistributes, not
   removes).
3c. **[build] UI engine-stamp guard** — ✓ DONE run14 (2026-08-01), commit
   cc4185d. **FALSE-POSITIVE FIXED run18 (2026-08-03), commit 9c2946e:** it
   compared the exe against all 116 engine sources, but mforce_ui depends on
   59, so a comp-lane edit to composer.h/passage_strategies.h shouted STALE
   with no rebuild able to clear it (Wolfie, 12d405e). Dependency set now
   comes from MSBuild's own CL.read tlogs for mforce_ui + mforce_engine
   (73 files, and it now covers tools/mforce_ui/main.cpp, which the
   engine-only scan never checked); falls back to the coarse scan when no
   tlogs exist, and --stamp names the mode. Detection extracted to
   tools/mforce_ui/build_stamp.h so a locked (running) exe can still be
   checked; new tools/stamp_test includes it directly + ab_stale_guard.ps1
   A/Bs old vs new on the live tree, 3 directions, restoring every mtime it
   touches. **✓ FULLY CLOSED run19 (2026-08-04)** — run 18's "NOT YET RELINKED"
   was pessimistic. No UI process was running at run-19 start and the on-disk
   exe already carried the fix (`--stamp`: `dep set : tlog (74 files)`,
   `stale : no`, exit 0), so the rename-then-link trick had worked; the
   `mforce_ui_running_backup2.exe` in the build dir is its residue. Both
   targets relinked and `--stamp` re-verified at every cycle close this run.
   Title bar carries `[build MM-DD HH:MM @sha]` always, plus
   `*** STALE - REBUILD ***` and a dismissable red banner naming the offending
   file when the exe is older than the newest engine .h/.hpp/.cpp. Commit read
   from .git/HEAD, repo root found by walking up from the exe — no build-system
   change, no git binary, so the check cannot itself go stale. New headless
   `mforce_ui.exe --stamp` (exit 1 when stale) makes it verifiable without the
   GUI; both directions tested. Also fixed: printf from --stamp/--roundtrip was
   invisible from a console (WIN32 subsystem app), now attaches the parent
   console unless stdout is already redirected.
3c2. **[build] FMSource phase param is dead** — ✓ DONE run19 (2026-08-04),
   commit fbbcb44. Inertness proven byte-exactly first: t1_06 and t1_07 each
   rendered IDENTICAL to a twin with `phase` deleted. Fixed by applying phase_
   as a carrier-side OFFSET (matches the base-class contract — currPos_
   telescopes to phase_current + sum(incr), so phase is an offset, not an
   integrated delta; never fed back into carrierPhase_, which would integrate
   the modulator twice). tools/null_test_fm.py PASS: 131 phase-unwired patches
   byte-identical (double(x)+0.0 is exact), all 4 wired ones differ. New
   research/fm_alias/measure_pm.py shows real sidebands, not just a different
   file: t1_07 centroid 1.40x / bw 1.19x; t1_06's slow sweep is a frequency
   deviation so it smears each partial into a 1.7 Hz cluster, 6 -> 109 peaks.
   Spec: specs/2026-08-04-fmsource-phase-pm-design.md.
   Also fixed t3_23_phase_velvet_jumps, which the null test caught as STILL
   DEAD — patch-side, not engine: RangeSource(-1,1,normalized=false) is the
   identity and VelvetNoiseSource emits {-1,0,+1}, so every offset was an
   INTEGER number of cycles. Now density=20 with +-0.5 cycle jumps.
   **REVIEW 7:** the run-12 audition verdict on t1_06/t1_07/t3_23 needs
   re-taking — those three were judged as something they were not.

14. **[build] Silently-ignored params** — ✓ **DONE run25** (2026-08-10),
   commit 2f39819. The count had GROWN to 66 in 30 files, and **59 of the 66
   were the linter crying wolf** for the third time. `SPECIAL_KEYS` was a
   hand-copied list of keys consumed by hand-written loader code and it
   drifted exactly as its own comment predicted:
   - run 22's Envelope `timeMode` (the absolute-seconds chuff fix) was never
     added → **15 false positives**, covering the piano template and every
     ks_piano patch including the v5 set queued for Matt. Proven live, not
     argued: deleting `timeMode` from `v5a_desc` moves the render peak
     **0.601 → 0.018**.
   - `AdditiveSource2`'s evolving/envelope branches read `startPartials` /
     `endAmplitudes` / `amplEnvelopes` through `.at()` and an indexed key
     loop, which the old extraction grep never covered → 4 more. **Group (b)
     was entirely bogus.**
   - `releaseMax` is read at `patch_loader.cpp:371` → **group (c) retired as
     a non-issue.**
   Allowlist is now SCRAPED from the two loader sources at lint time (value /
   contains / at / `[]` / `const char*` key lists), 119 keys, so it cannot
   drift again. `MANUAL_KEYS` down to a 2-key residue, and the selftest prints
   any manual entry the scraper already covers so it shrinks instead of rots.
   **Group (a) was the one real bug** and it was worse than written: the four
   `add_*_test` patches wire NO partials node at all, so
   `add_organ_test` / `add_saw_test` / `add_square_test` rendered
   BYTE-IDENTICAL to each other (`ec3650bd`) — an organ, a saw and a square
   producing one waveform. Migrated onto FullPartials
   (`tools/migrate_legacy_additive_params.py`); all three now distinct and
   correct against theory, not merely different — square's even harmonics sit
   98.5 dB below odd with odd at 0/−9.5/−14.0/−16.9 dB, saw has every harmonic
   at 0/−6.0/−9.5/−12.0 dB, both 20·log10(1/n) to the decimal. Both `_1` and
   `_2` endpoints are written because they interpolate across the partial
   range and both default to 1.0. The freqVar/amplVar → motion/shimmer half of
   the mapping is an INFERENCE from the legacy names, affecting only
   `add_string_test`. Also `_fx_limiter_test`'s PulseSource `width` → the real
   param `dutyCycle` (byte-identical, 0.5 is the default).
   **66 → 7 findings, all 7 in one file** → item 3l.

3l. **[build] Modulated even/odd partial weight has no equivalent** — the
   last 7 lint findings, all in `patches/Squeaker.json`, whose AdditiveSource
   `evenWeight`/`oddWeight` are `{"ref": ...}` value sources. FullPartials
   exposes them as `ConfigType::Float` configs, so a modulated even/odd weight
   cannot be expressed post-decomposition. Deliberately NOT flattened to a
   constant in run 25 — that would silently change one of Matt's own patches
   into something that is neither the old behaviour nor the intended one.
   Options: promote the two configs to ValueSource params, reach them via the
   paramMap/curve mechanism, or accept the loss and rewrite Squeaker.
   Engine edit; blocked in run 25 by the concurrent comp lane.
3m. **[build] UI: curve-superseded scalars should show `<curve>` like
   connected pins** (Matt, 2026-08-14). When a node's scalar config is the
   target of a paramMap curve, the slider is misleading — the curve stomps
   it at every note-on. Hide the slider and show `<curve>` in the same
   green font used for connected pins. Corollary from the same session:
   deleting a curve should make the scalar slider reappear/editable —
   Matt deleted the flat exc_body.cutoffFreq curve and then could not
   find the property to set the scalar, so he had to leave the curve in
   place. Ideally `<curve>` click-navigates to the curve editor entry.
   SUPERSEDED by the 2026-08-14 UI usability spec §2 (Mappings dialog +
   badges) — implement via that spec's chunk-2 plan, not standalone.
   ✓ DONE 2026-08-14 evening (chunk 2 landed, commit 3246ffe): badges +
   widget suppression + restore-on-delete shipped; REVIEW 30.
3n. ✓ DONE 2026-08-15 (commits 717bff2 b37bfa0 68c6557 c8ddc22 + patch
   repairs). Five root causes, all fixed: score injection on score-less
   files; formant child-id renaming (orphaned vowel curves — a UI save
   BRICKED saved vowel patches); RangeSource.normalized default vs loader
   contract; unmodeled params dropped (now carried verbatim via
   GraphNode::jsonExtras); stage-form adsr envelopes ignoring per-note
   sustainLevel (engine shape detection + declamp + UI sustainLevel
   emission retired). Gate exception list is EMPTY of fidelity debt —
   sole exemption FormantSequence1 (3p, not a save-path bug). NOTE: the
   sustainLevel-0.0-artifact repairs to the two UNTRACKED library files
   (cello_full_range.json, viola_res_lo.json) sit uncommitted in the
   working tree pending Matt's tracked/untracked call from REVIEW 26.
   Original text follows for the record:
   **[build] UI save round-trip changes the SOUND of 34 instrument
   patches** — found 2026-08-14 by the stable-identity regression
   (tools/test_stable_roundtrip.py; its KNOWN_DIFFS set is the exact
   worklist). Same family as the 08-13 "edit-any-setting -> volume cut"
   bug, which fixed only CombinedSource enum-strings + unwired source2
   scalars; still-lossy species measured in the roundtripped JSON:
   Envelope preset/timeMode/stage semantics, AdditiveSource noiseBed*,
   FullPartials motion/shimmer keys, ExplicitPartials decay keys,
   SegmentSource values, RangeSource.normalized, FMSource
   phase/oversample, MultiSource source, score/seconds injection.
   Affects LOCKED library patches (clarinet_locked, 8 voice winners,
   percussion, bells, both Rhodes families): opening one in the UI and
   saving would silently change the archived sound. Proven pre-existing
   (old-exe A/B + params-untouched-by-construction), NOT caused by
   stable ids — the new gate just made it visible. Fix = per-species
   loader/save parity like the 08-13 fix; each species fixed shrinks
   KNOWN_DIFFS until the gate is empty. Until then: don't re-save
   library patches from the UI without diffing the render.

3s. **[build] Resurrect SlewLimiterSource Peak mode as an envelope
   follower** — Matt-approved logging 2026-08-16. Run 21 built
   SlewLimiterSource (Slew/Lag/Peak); run 22 reverted the family per
   Matt's "dead end" verdict on the CLICK use case — but Peak mode was
   a true envelope follower (|x| + asymmetric attack/release), which
   the AFP-31 study showed is a general-purpose block: AF uses it both
   as a cheap AD generator (trigger pulse -> percussive env) and as a
   step de-clicker (feedback crossfade smoothing). Future uses Matt
   named: AUTO-WAH (follower drives a filter cutoff from signal level —
   the SVFSource resonant LP is the natural partner) and SIDECHAIN
   DUCKING (follower on one signal modulates another's gain). The
   reverted code is in git history (run 21, commit era ~2026-08-05);
   resurrect Peak mode only, as its own small node (EnvFollowerSource?)
   rather than the three-mode family that got killed.

3r. **[listen-prep] Soprano alt-formant A/B re-render** — Matt (REVIEW 25
   response): he deleted the renders AND the pending patches, recalls "no
   good candidates" but wants a re-do to be sure, and invites setting
   variations based on that recollection. Regenerate the 4 A/B pairs from
   the run-25 recipe (alto formant table, unmoved, sung at A4; O pair as
   control) against the CURRENT library/voice soprano files (his merged
   picks), plus 1-2 variation arms since the originals didn't convince.
   Queue as a fresh [listen].

3q. **[build] Groups: shared-source selections hit the two-output refusal**
   — Matt (2026-08-15, testing REVIEW 32): a WhiteNoise feeding two
   consumers straddling the intended boundary refuses ("2 outputs"), and
   the natural workaround — duplicate the noise node — SILENTLY CHANGES
   THE SOUND (the duplicate is an independent RNG stream; the original
   fan-out was one correlated source via RefSource). He also overwrote
   Piano_bright doing it (restored from git, hash-verified). Options:
   (a) allow multiple output pins on a collapsed group (boundary already
   computes them; the refusal is policy, not mechanics); (b) treat a
   source feeding both sides as an INPUT-side pass-through rather than an
   output; (c) at minimum, warn in the Duplicate menu item that a
   duplicated generator gets a fresh random stream. (a) looks right —
   the single-output rule came from "a group IS a ValueSource", but a
   multi-out group is just a group with two taps.

3p. **[build] FormantSequence1 is CLI-unrenderable and migrates silent** —
   found closing 3n. The baselines patch uses the LEGACY inline formant
   form (structs, no refs), which the CLI loader never supported ("key
   'ref' not found"); the UI loads it fine. Migrating via UI roundtrip
   produces valid ref-form JSON that renders SILENT (peak=0), so the UI
   and CLI disagree about FormantSequence semantics beyond the format —
   same UI-vs-CLI family as old item 15. Diagnose the silence (likely
   FormantSequence's spectra wiring or blend path), then migrate the
   file; it is the roundtrip gate's only exemption until then.

15. **[build] 7 patches the CLI cannot render at all** — ✓ 6 of 7 DONE run19
   (2026-08-04), commit 6fc128b. Found while A/B-ing front 3. They were
   THREE distinct bugs, not one:
   (1) a bare mono source as `graph.output` was a hard error, though the
   instrument path already auto-wraps one — standard path now matches it
   (unity volume, centre pan, so it inherits the -3dB equal-power pan of
   item 3d). Fixes mux_{noise,rednoise,sine}_test;
   (2) `wire_params_generic` threw on any STRING in a pin/param slot, but a
   string there is always a legacy enum consumed by a hand-written branch
   further down — WavetableSource's `"evolution": "target"` is ALSO an input
   descriptor for the ref-wired form, so the generic loop killed the patch
   before its own special case ran. Strings skipped; other junk still throws.
   Fixes ks_morph_{flute,horn,saw}_test;
   (3) CombinedSource read `operation` as a string only, so legacy ordinals
   died on a raw json type_error. Both forms now accepted.
   **STILL OPEN — CombineTest.json**: it says `"operation": 3`, and no current
   CombineOp matches (0=add, 1=multiply, 2=fade). Deliberately NOT defaulted
   to Add — silent fallback is the exact failure mode this run kept finding.
   It now fails with a named, actionable error. **Needs one word from Matt**,
   who wrote the C#: what was ordinal 3? (REVIEW 10.)
   Regression-gated: 73/73 byte-identical vs the stored A/B arm, with exactly
   the 6 revived patches present only in the new arm.
3h. **[build] KS piano iteration** — gated on REVIEW 22: hammer voicing
   (treble partial richness without noise input), true register-flat
   aftersound mechanism, module re-wirability question. The additive
   piano lane (3e-NEXT3 below) and this lane now compete on Matt's ears.
3i. **[build] Envelope hold/loop mode** — one-flag engine change in
   envelope.h::next() so UI streams can run truly forever (UI currently
   preps 7200s with absolute_time overrides; run-24 UI report has the
   sketch).
3j. **[read] WanderNoiseSource deltaSpeed semantics** — applied per
   SAMPLE, so audio-rate wander self-cancels (slope flips every ~3
   samples); legacy init lastVal_=0.5 starts right-of-center. Candidate
   rate-normalization; behavior-changing for existing wander patches.
3k. ✓ DONE run 26 (2026-08-15, commit 2f7bbba). Full audit of what the
   callback can hold across graph mutations; three races closed:
   (1) g_bufferPlayback pointed INTO g_outputWaveform while four sites
   resized/cleared it unguarded (the header comment even claimed buffer
   playback "owns its data" — the bug in prose); (2) graph rewiring
   (update_node/all_dsp) mutated live shared_ptr edges the stream taps
   dereference — now under g_audioMutex; (3) Properties set_config can
   rebuild ExplicitPartials arrays mid-next() — locked. The load/new
   graph-swap path was already guarded (run 23). Voices were never at
   risk (own their patches). RESIDUAL noted: the Envelope stage-editor
   panel and Curves-tab breakpoint edits still mutate live objects
   unlocked — same fix pattern if a crash ever names them.
   Original: audio-thread AV in WaveSource::next, crash log 08-06 10:05.
3e-NEXT3. **[build] Piano next steps** — gated on REVIEW 19 verdict:
   (4) second decay stage (prompt/aftersound; C4 evidence now includes
   overtones decaying SLOWER than h1 — inverted vs the fixed n^0.6 law,
   so the feature likely needs per-partial break/rates, not one global
   break); per-register ATTACK curve (real 36/13/9 ms trend vs the
   uniform 8 ms lock; needs Envelope rebuild-on-config or a paramMap
   freq curve on the attack config); 600-eval run once residuals are
   funded; stretch-aware heterodyne (scorer debt, carried).
3e-NEXT2. **DONE run 23 — [build] Piano recovery bundle** (was: GATED on Matt's option pick
   (REVIEW 16)** — recommended: (1) absolute-seconds attack semantics for
   this path, (2) knock recalibration from measurement, (3) decay register
   curve rebuilt from per-note fits; then re-smoke. Follow-up feature
   candidate: (4) second decay stage (prompt/aftersound). Parallel
   exploration: (6) FM-for-bass via t1_04 pitch-mapping study. Full
   600-eval run explicitly NOT recommended until 1-3 land.
3g. **RETIRED-REVERTED run 22** — SlewLimiterSource + click family removed
   per Matt ("dead end"); CombinedSource op-parse fix survives.
3e-NEXT. **[metric] Piano: full run + scorer debt** — smoke landed run 20
   (1.521 -> 0.900). GATED on Matt's A/B (REVIEW 13): full 600-eval run.
   Scorer debt found by the smoke:
   (a) ✓ **DONE run21** (2026-08-05), commit 85a4015. broadband_ratios blew
   up when a band held no harmonic: `between_e / 1e-12`, a noise floor
   inflated by 12 decades. Live in the stored reference — piano C6 band0 was
   **4.63e+11** vs O(1) elsewhere, and C6 (1046.5 Hz) was the only eval note
   above BANDS[0]'s 700 Hz top, so the smoke's term3 was dominated by that
   one cell. Empty bands now return NaN, score_candidate nanmeans over them.
   Regenerating out/piano_reference.json changes C6 band0 to nan and leaves
   every other value identical to the digit.
   research/ml_ears/test_broadband_guard.py keeps the pre-fix formula inline
   so the blowup is shown, not asserted (1.325e13 at f0=880), and checks
   populated bands come back bit-identical.
   (b) ✓ **DONE run25** (2026-08-10), commit bc11389. Spec:
   specs/2026-08-06-stretch-aware-metrics-design.md.
   The 08-06 orphan run's stretch-aware `refmetrics.py` had been swept into
   run 23's commit 4ae968a and was **live in HEAD since 08-06, unverified and
   unreported**. Adopted its test (`test_stretch_metrics.py`): ALL PASS, B
   recovered to 0.02%, mask inflation 11.5-68x on synthetic ground truth,
   B=0 bit-identical.
   **But it was INERT** — spec steps 1-3 were done and 4-5 were not. Nothing
   called it: no `inharmonic` opt-in on any config, no mention in
   `iowa_reference.py` or `score_candidate.py`, and the stored reference
   predated the code. Wired: opt-in on piano_mf, B per note into
   harmonic_env/broadband_ratios/motion_stats, `b_map()` to the candidate side.
   **Naive wiring then REGRESSED the bass** (C2 band2 3.995 → 29.34, and C2 is
   an eval note). Diagnosed (`diag_stretch_bass.py`) rather than guessed: the
   fitted B is fine, the FIXED `tol_frac*f0` mask is not. Model error vs the
   real tracked peaks, in mask half-widths — C2 ≤0.15 at n 1-16 but up to 9.9
   at n 33-64; C4 up to 10.5; C5 up to 6.1. A single stiff-string B holds to
   n≈16 and no further, and a model-centred mask wide enough to hold every
   tracked partial would need 0.36-0.63*f0 — six to ten times current, which
   would swallow the gaps the metric measures.
   Fix is a better CENTRE, not a wider mask: `rm.measured_lines()` masks on the
   peaks the tracker actually found, model-filling only below the SNR gate;
   positions stored in the reference so the candidate is masked on them.
   Piano band2 before → after: C1 10.99→3.18, F1 13.7→0.767, C2 3.995→0.876,
   G2 2.677→0.159, C3 120.2→0.0254, C4 52.23→0.161, G4 2056→0.00202,
   C5 259.5→0.0160. **Every note improves, nothing regresses.**
   Same patch/code, only the reference differs: term3 1.1373→0.7934,
   term2 1.6085→1.3114, TOTAL 1.1879→1.0449 — the patch did not improve, the
   metric had been charging it for a defect in the reference's own
   measurement. Viola + clarinet references regenerate BYTE-IDENTICAL.
   (b2) **[metric] OPEN — `derive_motion` takes no B**, so the stored
   `motion_medians` are unchanged by the fix (verified: identical before and
   after). Same treatment as motion_stats, plus the measured-line idea.
   (b3) ✓ **DONE run25** (2026-08-10), commit bcf3413. Isolating one partial
   needs the neighbours (±f0) outside the passband, i.e. lp < f0/2; LP_HZ=40
   violates that below f0=80 (C1 f0/2=16.4, F1 21.8, C2 32.7 vs G2 49.0), and
   the break sits exactly where the numbers broke. So the residual there was
   adjacent-partial BEATING, not broadening. Confirmed by sensitivity sweep
   (`diag_lowf0_heterodyne.py`, the run-18 beat-rate control shape): C1 swings
   318.7 → 11.2 c across cutoffs 40/30/20, a **28.5x sensitivity**, vs 1.45-2.0x
   for G2 up.
   **The sweep killed the obvious fix**: a purely spacing-relative cutoff
   wrecks the treble (C6 would get a 419 Hz passband, resid 6.1 → 37.3 c). The
   rule is a CAP, `lp = min(LP_HZ, 0.4*f0)`, which does not bind above 100 Hz;
   tap count scales only when the cutoff narrows, so nothing above the cap
   moves by a bit.
   resid_cents_rms: C1 468.70 → **4.27**, F1 340.44 → **21.45**, C2 181.45 →
   **4.66**; G2/C3/C4/G4/C5/C6 unchanged to the digit. C1 amp_frac 42% → 12%.
   Scorer (C2 is an eval note): term2 1.3114 → **0.8123**, TOTAL 1.0449 →
   **0.9202**. Viola + clarinet references byte-identical.
   (b3-open) **F1 is still not trustworthy** — 6.4x sensitivity at 21.45 c.
   Better than 340 but not a measurement to rely on. Two attempts spent on the
   low-f0 family this run, so per the 2-attempt rule it is annotated and left
   rather than tried a third time. Likely needs a longer window (F1's 1.0 s
   sus gives 1 Hz resolution against a 43.7 Hz spacing) rather than a
   different filter.
   Velocity layers (mf-only today) stay Later.
3f. **[build] CombinedSource JSON op string "sum" silently falls back to
   Add** — ✓ DONE run21 (2026-08-05), commit b08d795. CombineOp gained Sum in
   run 20 but the loader never learned it: the string parser knew only
   add/multiply/fade and fell back to Add on anything else, and the ordinal
   switch threw on 3. Now add/mix/multiply/fade/sum parse, ordinal 3 maps to
   Sum, and anything unrecognised throws a NAMED error rather than quietly
   becoming Add (the rule run 19 set). Verified: "sum" and ordinal 3 render
   byte-identical (C45E9F8E) and differ from Add (BF396D58); "bogus" and
   ordinal 9 exit 1 with their error text. No existing patch affected — the
   only `operation` values in the repo are multiply (2), add (2), 0 (1).
   The separate linter check was dropped as redundant: the engine now fails
   loudly, which is strictly better than a static warning.
3g. **[build] SlewLimiterSource** — ✓ DONE run21 (2026-08-05), commits
   b08d795 (engine) + 5592354 (ladder). Spec:
   specs/2026-08-05-slew-limiter-design.md. Category Filter, modes
   Slew / Lag / **Peak**, `rate` + `fallRate` as ValueSources, UI menu entry
   under Filters.
   **The item's premise was wrong and measuring caught it**: the clicks are
   one-sample IMPULSES (0, ±0.5, 0), not steps, so a sign-keyed rate limiter
   just attenuates them to height rate*dt and they stay one sample wide.
   Proven in step_slew_impulse — Slew holds a POSITIVE impulse 10.00 ms and a
   negative one 0.02 ms (one sample). Peak mode is magnitude-keyed (attack
   while |in|>|held|, else decay toward zero at fallRate) and symmetric.
   tools/verify_slew.py vs patches/slew_test/ ALL PASS: Slew rise within
   0.5-1.9% of S/rate at 0.002% nonlinearity (a straight ramp); Lag tau
   within 0.05% at 33% nonlinearity (correctly not straight); fallRate
   asymmetry 10.05x vs 10x; Peak 10.00 ms glides both polarities.
   Ladder (patches/slew_clicks/, renders/slew_clicks/) → **REVIEW 15**:
   attack centroid 7116 → 3237 Hz (0.455x) as fallRate falls 20000 → 50,
   >8 kHz share 37.9% → 13.6%, monotonic, 6/6 distinct by sha256.
   Note the dial is NOT a lowpass: the glide is 0.5/fallRate s and a linear
   phase glide is a constant frequency offset, so fallRate trades chirp
   LENGTH against a fallRate/2 Hz deviation.
3e. **DONE run 20** (was: [build] Piano engine features (from measurement)) — (a)
   `inharmonicity` config on Partials: mults stretched by
   sqrt(1+B*n^2) at note-on; per-note B via the existing paramMap
   frequency-curve mechanism (B spans 225x across the keyboard,
   measured laws in out/piano_analysis_report.md). (b) per-partial
   decay: `decayRate`+`decayExp` configs (rate_i = rate*pmult^exp),
   prepare-cached per-sample gain. Then piano encoder (~14-18 dims,
   B+decay laws LOCKED from measurement per the clarinet-bed pattern;
   knock = graph-level noise burst, no engine feature) + first CMA-ES
   smoke. Prereq: iowa_reference onset-alignment (sample lead-ins vary
   0.04-0.54s).
3d. **[read] Pan-law question for Matt** — CLI WAVs are -3dB vs UI (equal-
   power center pan in StereoMixer vs unity mono). Option: mono patches
   write x1.0 to both channels so WAV loudness == UI loudness.

4. **[metric] (G1b) Novelty metric** — ✓ DONE run5 (2026-07-29).
   research/novelty/: embedding.py (36-dim MFCC + spectral-feature vector,
   numpy/scipy only) + novelty.py (build/score/manifest/selftest). Distance-
   to-nearest-library-neighbour in z-scored space. Wired into explore via the
   `manifest` subcommand (augments an --explore manifest.json with novelty +
   re-ranks; Python-side because the C++ filter carries no library ref).
   selftest PASS + real-render ordering validated. Gate for G2/G3 sweeps is
   now available.
5. **[metric] (G2) Recursive partial-expansion regime sweep** — ✓ FIRST BATCH
   done run5 (2026-07-29): 18 regimes (incl. 6 rule-breakers) generated
   (tools/gen_expand_sweep.py), rendered, novelty-ranked
   (tools/_run_expand_sweep.py); survivors queued for listen (REVIEW).
   Rounds 2-3 followed Matt's verdicts (gen_expand_sweep2/3.py).
   **✓ REOPEN DONE run18 (2026-08-03)** — depth 3-4 finally affordable
   (cost re-measured: LINEAR at ~17 ms/partial for a 4 s render, 6 cells
   216→5000 partials, which also confirms the base·(2c+1)^(r+1) formula).
   18 patches (tools/gen_expand_sweep4.py), 16 rendered in 285 s,
   novelty-ranked + a purpose-built depth-control comparison
   (tools/_run_expand_sweep4.py). **Answer: depth is second-order.**
   Holding partial count AND total spread fixed, halving depth moves the
   embedding 4.36, vs a batch median pair of 18.98 and loPct's 50.46 — i.e.
   about the size of a `power` tweak. Recurse 3-4 is available and cheap
   now, but it is not where the sound is; the levers are loPct/spacing/
   rule-breakers. AWAITING Matt's listen (REVIEW 4) to close the item —
   if he agrees, this front retires rather than getting a round 5.

12. **[build/review] `power` inert at count=1 in apply_expand_rule** — found
   run18: side-partial amplitude is `ampl*loPct + ampl*(1-loPct)*pow(t,power)`
   with `t = j/count` (left) / `(count-1-j)/count` (right), so at count=1
   t≡0, `pow(0,p)=0`, and power does nothing — every side partial lands on
   the loPct floor. Surfaced because peaked_deep rendered BYTE-IDENTICAL to
   micro_r3; proven live by the regression pair taper_flat_c2/taper_steep_c2
   (identical but for power, at count=2, and they differ: distinct sha256,
   distance 4.96). Not a coding bug — a defensible reading — but a trap that
   silently cost round 4 a cell. Options in REVIEW 5: leave it documented, or
   renormalise t (e.g. `(j+1)/(count+1)`) so count=1 is non-degenerate, which
   changes every existing expand patch. GATED on Matt; engine edit.

13. **[metric] Piano unison beat RATE — attempt 3, different method** —
   run18 attempts 1 and 2 both failed to resolve the rate (2-attempt rule
   → backlogged, not retried inline). Attempt 1: 3 s window, every partial
   "beat" at FFT bins 1-2 (decay curvature). Attempt 2: 8 s window +
   4-cycle floor + high-passed residual — depth control passes cleanly but
   a built-in floor-sensitivity check shows 9 of 11 rates CLIMB when the
   floor is raised 0.50→0.75→1.12 Hz, so the rate is a window artifact.
   Only C5/C6 hold (~1.9-2.0 Hz). Envelope-domain inference is the wrong
   tool. NEXT METHOD: resolve the unison strings as SEPARATE spectral lines
   — they are split by the beat frequency (0.6-2 Hz), which a ≥2 s coherent
   FFT resolves directly in the bass/mid — and read the split, don't infer
   it. What DID survive run18 and is usable now: single-strung B0/C1
   modulate at 0.016-0.029 vs 0.106-0.285 multi-strung (5-9x), so
   shimmerDepth ≈ 0.15 is a defensible SEED. shimmerHz/shimmerCoherence
   stay searchable in the piano encoder unless Matt redirects (REVIEW 6).
   research/ml_ears/piano_beating.py, out/piano_beating.json + .png.
6. DONE run 6 — see reports/2026-07-29-dipsy-run6.md. **(was: [metric] (G2) FormantSequence deep-dive)** — modulated/sequenced formant
   motion as a first-class timbre animator; sweep + novelty-filter.
7. **[build→metric] (G3) Oversampled FM render path** — ✓ CORE DONE run8
   (2026-07-30). FMSource `oversample` config (Int 1..16, default 1);
   carrier+mod sin() at M·SR + 8th-order Butterworth decimation (BWLPSection).
   M=1 byte-identical (spacy family safe). Measured suppression M=2/4/8 =
   24.5/34.7/39.6 dB (research/fm_alias/measure.py; M=8 kills 99% of in-band
   alias residual). Spec: specs/2026-07-30-oversampled-fm-design.md.
   **STAGE 2 ✓ DONE run12 (2026-07-31):** 24-patch "modulate everything"
   matrix (tools/gen_fm_matrix.py → patches/fm_matrix/), 9 rule-breakers;
   both ratios / phase / frequency / amplitude modulated at sub-audio AND
   audio rate, FM driving FM's depth, modRatio swept through zero. Wiring
   verified by measurement (spectral centroid sd 99..2719 Hz across the
   batch), novelty-ranked, survivors → REVIEW 4.
   Also research/fm_alias/measure_patch.py — alias measurement for ANY FM
   patch. It found the oversample-default question has a wrong premise: the
   ladder converges at high CARRIER (M=1/2/4/8 → 0/2.7/5.7/12.5 dB) but does
   NOT converge at high INDEX on a low carrier (0/-0.0/-2.4/-2.3 dB). REVIEW 3.
   **Open:** whichever patch family the audition verdict picks out.
8. **[metric] (G4) Additive performance** — ✓ STAGE 1 DONE run8 (2026-07-30):
   CLI render timer + tools/prof_additive.py sweep.
   **✓ STAGE 2a+2b DONE run12 (2026-07-31)** — 1.2-1.6x wall clock, commit
   aafbe9b, spec specs/2026-07-31-additive-hot-loop-design.md.
   2a (bit-exact, 14/14 byte-identical null test): IPartials::sum_partials
   batches the loop behind ONE virtual call per sample (accumulator threaded
   so nested sets keep the addition order); per-sample scalar cache kills 8
   virtual current() calls PER PARTIAL; per-partial pmult/rolloff/motion-scale
   cache keyed on (multE,roE); fmod→truncf.
   2b (fast sin): sinf measured at ~40% of the remaining loop, replaced by a
   folded degree-9 minimax polynomial in turns (core/fast_math.h). Worst
   deviation 1 LSB @16-bit, residual RMS -125..-140 dBFS; MORE accurate in
   float32 than the std::sin(x*TAU) it replaces. No toggle.
   2c (timer) ✓: mforce_cli prints load= and total=. viola_default is
   load=2288ms vs render=16.6ms — profile instrument patches by load=.
   Gates: tools/null_test_additive.py, residual_test_additive.py,
   ab_render_time.py, ablate_additive.py.
   Now: marginal 47.6 ns/sample/partial, ~437 partials/core RT (on a box with
   mforce_ui holding a core — better when quiet).
   **✓ STAGE 2d PHASE 1 DONE run14 (2026-08-01)** — isolated AVX2 prototype
   (research/additive_perf/simd_proto.cpp) beats scalar **18.4-19.1x** on
   synthetic arrays, vs a 2x abort criterion. Stage greenlit but RESTAGED,
   because tools/ablate_layers.py (new) showed a layer-free vector path
   reaches only ~51% of viola_default's loop: the flagship patches run
   motion+shimmer+onset+bandwidth together, and bandwidth's SHARED-rng walk
   re-rolls (not perturbs) the noise if vectorized. New sub-stages 2d-1..2d-4
   in the spec. **2d-3 (per-partial rng streams) is gated on REVIEW 0b** and
   is arguably a prerequisite for the rest.
   **✓ ALSO run14 — two libm calls in the hot body, both landed:**
   `truncf` was a CRT call under SSE2-baseline flags → int round-trip, 1.6x,
   BIT-EXACT 14/14 (commit e31d936, marginal 41.7→26.1 ns/sample/partial);
   `std::exp2` in the motion path was 65.6% of viola_default's entire loop →
   core/fast_math.h fast_exp2 (0.88 float32 eps), 1.7x on motion-bearing
   patches, v6_cmaes_best 16.1s→8.9s (commit 8396d3a). Residual 1 LSB on
   13/15; the exception is REVIEW 0a.
   **STAGE 2d (open, >1 session):** explicit SIMD of the partial loop. Spec:
   specs/2026-07-31-additive-simd-soa-design.md. Ablation says the
   memory-bound floor for the current layout is 3.3 ns/sample/partial vs
   26.1 now (was 57.3 before run 14) — and that the gap is NOT explainable by
   arithmetic count. FOUR ANTI-RESULTS, don't retry any (all on controlled
   A/B; (iii)-(iv) from run 14):
   (i) branchless degree-13 full-period sin polynomial, no quadrant folding —
   a WASH (MSVC already compiles the folded selects branchlessly) and less
   accurate (-123.4 vs -133.3 dB);
   (ii) 4x unroll with 4 independent accumulators — consistently SLOWER
   (0.88/0.94/0.93x on the 32/96/200p ladder). The compiler was already
   scheduling across iterations; the unroll only cost register pressure.
   (iii) `pfreq / rate_` → multiply by a cached reciprocal: 1.10x in the
   isolated prototype but 0.89x IN THE ENGINE (25.0→29.6 ns/sample/partial at
   200 partials), and not bit-exact. Rejected on both counts; noted inline.
   (iv) bit-exact `t*t` fast path for `Formant::get_gain`'s `std::pow` (all
   579 formant instances use power 2): 1.00-1.01x, i.e. nothing. The
   `contains()` gate means only in-band partials reach it. Noted at the call
   site in formant.h.
   Consequence: the cheap explanations are closed off. Anything that moves
   this number has to be real vector arithmetic or nothing — so phase 1 is an
   isolated SIMD prototype on synthetic arrays with a 2x abort criterion,
   BEFORE touching Partials. Weigh against 2e before committing.
   **STAGE 2e (open):** iFFT overlap-add additive (1000s of partials cheap;
   structurally different synthesis, NOT bit-identical → review:listen).
9. **[build] Convert 6 algev patches to instrument-style** — ✓ DONE run8
   (2026-07-30). instrument+score added + output rerouted to bare source;
   all 6 render with sound via the shared instrument path.
10. **[review:listen] Vowel/formant re-tune after formantWeight refactor** —
    ✓ **CLOSED run25** by Matt's 2026-08-09 verdict. Seven winners locked into
    `patches/library/voice/` (alto A/E/I/U, bass U, soprano A keep their pass-1
    winners; tenor U takes the pass-2 UW-F3 graft), all seven byte-identical to
    the auditioned WAVs. `tools/lock_vowel_winners.py` is the promotion script.
    Successor is item 16 below, which is a narrower question.

16. **[review:listen] Soprano E / I / U — sampling-limited or tunable?** —
    the only vowel families Matt rejected. Run 25 measured why instead of
    sweeping again (`research/vowel/`):
    (a) separability collapses monotonically with f0 — mean vowel-pair
    distance 26.94 (bass 110) / 23.87 (tenor 165) / 21.39 (alto 220) /
    **14.80 dB (soprano 440)**; E-vs-I 24.21 / 18.88 / 12.01 / 10.71. At
    440 Hz soprano E and I put F1 on the SAME harmonic (h1) and F2 on the SAME
    harmonic (h5), so they can differ only in gain. Harmonics below 3 kHz:
    27 / 18 / 13 / **6**. Soprano U sits 5.20 dB from O, the closest pair in
    the grid — "lacks vowel character" as a number.
    (b) a 2x2 crossing recipe with pitch says the recipes were still giving
    away ~7 dB: alto formants sung at 440 keep **16.90 dB** of E/I contrast
    vs the soprano recipe's 10.20.
    Candidate queued (REVIEW 25): alto formants unmoved, sung at A4. E-I
    +6.69 dB, U-O +8.66, I-O +4.72; E-U and I-U unchanged. Distinct from
    pass-2's `e3_altoXpose`, which scaled formants WITH the pitch (physically
    wrong — formants are a tract property).
    **GATED on Matt.** If none land, the honest read is that sung vowels at
    440 Hz are sampling-limited and the front retires rather than getting a
    pass 3. Escalation option if he wants one more: vibrato via the pitch-mod
    layer (coherent FM is the strongest partial-fusion cue there is).
17. **[design/build] Revisit the UI live-audio serialize-to-temp-file round
    trip** — Matt, 2026-08-13: "seems to work fine, and maybe it's the best
    alternative, but has always seemed wonky." NO ACTION NOW — future item.
    Current mechanism: every live keyboard note syncs the UI's in-memory
    graph to a temp JSON, then loads it through the engine patch loader so
    the CLI's voice-pool/paramMap/Multiplex machinery applies (the reason it
    exists: guarantees UI audio == patch-on-disk audio, the RD
    audition-path-mismatch lesson). Known costs: a full graph serialize +
    parse + voice-pool build per note-on (load= was ~0.6-1.3 s on v6
    patches — polyphony rebuilds the graph N times); and any UI-side
    (de)serialization defect propagates into the AUDIO, which is how the
    2026-08-13 damper-preset mangling became audible. When revisited,
    candidates: loader entry point that accepts an in-memory json object
    (kills the file, keeps the shared code path); caching the loaded
    instrument until the graph is dirtied (kills the per-note rebuild);
    or building the instrument directly from the UI node tree (fastest,
    but re-opens the two-loaders drift problem the 2026-08-13 shared
    dispatch just closed — needs a null-test harness comparing UI-built vs
    loader-built renders before it can be trusted).

11. **[build] Two standing dirty patch files** — ✓ **SHRUNK run21**: the
    Python half of this item is GONE. `score_candidate.py` and
    `iowa_reference.py` were committed by run 20 (72defb9), so HEAD can now
    run the clarinet CMA-ES pipeline on its own. What remains is only
    `patches/clarinet_c2/c2c_quiet.json` and
    `patches/fable1_v6/v6_01_res_curve_lo.json`, dirty at session start for
    seven runs now (a UI re-save of v6_01 and a clarinet variant). Left
    untouched again per the tree guard. One word from Matt: commit or revert.

## Done

- Partial motion layer + v1-v4 batches (docs/Fable1_results.md)
- 16 kHz cutoff break→continue fix; UI array-restore fix; 96-partial
  extrapolation
