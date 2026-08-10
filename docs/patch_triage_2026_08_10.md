# Patch triage manifest — 2026-08-10

Target structure (Matt, this session, rev 2 — patch/score schema split):

A **patch** is an instrument (DSP graph + instrument block). A **score** is
instrument-independent musical material (score events, templates, passage/piece
specs), optionally naming a suggested instrument, overridable at render time.
The old embedded `"score"` block in patch JSON stays supported as a smoke-test
convenience only; new comp artifacts are score files.

```
patches/  {sweep, pending}/<effort-family>/   # = dsp lane (gitignored working dirs)
          library/<instrument-family>/        # tracked keepers
          old/                                # grace window, 30-day scheduled purge
          fixtures/                           # tracked dev-test/regression patches
scores/   {sweep, pending}/<effort-family>/   # = comp lane (same shape)
          library/  fixtures/                 # (no old/ yet — add when needed)
renders/  {dsp, comp}/{sweep, pending}/<effort-family>/   # lane level SURVIVES here:
          library/                            #   renders come from both lanes; per-lane
                                              #   pending/ = Matt's two listening queues.
                                              #   library/ laneless, mirrors patches/library.
                                              #   No old/ — failed renders delete outright.
```

Rules: no loose files at any level above a family folder; pending families are
named for the effort; every audition verdict promotes to library/ or moves to
old/ same day; nothing cited by an open REVIEW.md item moves.

`fixtures/` rationale: ~80 tracked root `*_test.json` / `template_*` / `test_*`
patches are smoke tests and comp-render inputs referenced from specs, plans, and
reports. They are not audition material and must not age out of old/. DSP-side
fixtures go to patches/fixtures/; comp-side artifacts (templates, piece/passage
specs) seed scores/fixtures/.

MATT: Yep. Let's call it "baselines"

Follow-up work item (Wolfie retrench candidate): CLI `--score <score.json>
--patch <patch.json>` pairing; standard audition scores in scores/fixtures/
(e.g. the C3/C4/C5+chord phrase used for the curated batch render 2026-08-10).

## A. LIVE — does not move (open REVIEW items / modified in tree)

| Item | Why |
|---|---|
| pending/ks_piano_v5/ | dsp item 22b, awaiting verdict |
| pending/vowel_soprano_alt/ | dsp item 25, awaiting verdict |
| ks_piano/ | dsp item 22, A/B vs additive still open |
| fm_matrix/, fm_matrix2/ | Matt: "crazy results, re-listen later (parked)" |
| template_golden_phase1a.json, template_shaped_test.json | comp item 18 inputs |
| test_jazz_turnaround_*.json (11) | comp item 12 (voicing A/B) inputs |
| clarinet_c2/c2c_quiet.json, fable1_v6/v6_01_res_curve_lo.json | MODIFIED in working tree by an unidentified session — resolve before touching |

MATT: Already convinced KS is the future here, so verdict delivered on that A/B,
so move to /old (ks_piano_v5 stays pending). All others keep in pending, with the
2 comp ones moved to renders/dsp/pending.

(patches/pending/ keeps its path under rev 2 — live families do not move.
Comp templates listed above move to scores/fixtures/ with REVIEW.md item-18
paths updated in the same commit.)

## B. KEEP → library/ (proposed placements)

| Patch | Proposed home | Why |
|---|---|---|
| viola_default.json | library/strings/ | adopted default (run 9) |
| clarinet_default.json, clarinet_locked.json | library/winds/ | locked keeper (2026-08-02) |
| woodwinds/Clarinet1.json | library/winds/ | lone survivor of woodwinds/ |
| fm_spacy_n177.json, inst_spacy.json | library/fm/ | the solved legacy-FM prize |
| inst_fm_rhodes_base/clack/dual.json | library/fm/ | rhodes family (complements Matt's curated/ picks) |
| library/voice/ (7) | stays | vowel winners, already home |
| hi_hat.json, kick_drum.json, snare_drum.json | library/percussion/ | only percussion patches we have |

MATT: Let's go library/effects/spacy for the fm, all other suggestions good.

## C. FIXTURES → patches/fixtures/ (tracked; no audition value, referenced by docs/tools)

- **dsp:** add_*_test, as2_*_test, fadd_*_test, ks_*_test (bowed/breathy/metallic/morph_*),
  wander*_test (15), bend_*, slide_*, algev_test_* (6), _env*, _fx_*, mux_noise_test,
  pluck_test/pluck_sanity/pluck_muted_test, reed_test, bowed_test, saw/sin/tri/pulse/tri_test,
  delay/distortion/filter_sweep/vibrato_test, rn_test, rn_sine_mod, lrn_test, CombineTest,
  TriTest, hybrid_*_test, clarinet_bed_test, prof_additive/ (partial-count profiling ladder)
- **comp → scores/fixtures/ (not patches/fixtures/):** template_binary, template_mary,
  template_ode_to_joy{,_free,_monkeys}, test_k467_* (6), test_afs_passage,
  test_elaborated, test_harmony_progression, test_passage_chords_* (2),
  test_period_sentence, period_markov_test. NOTE: these currently carry embedded
  instrument+score in one file — they move as-is now, schema split applied when
  the --score/--patch CLI pairing lands (follow-up item above).
- **comp-adjacent but instrument-shaped (stay patches/fixtures/):** inst_chord_test,
  inst_melody_test, inst_fm_bell_melody_test, inst_french_horn_test
- **untracked scaffolding joining fixtures:** MPXTest1-3, NATest1, mux_sine_test,
  mux_rednoise_test, test_lib_billy/blue/miley (PPL), test_aaab, phased_test,
  repeating_segment_test, FSTest, BaselineSIN, FormantSequence1

Good. Again, "baselines".

## D. → old/ (30-day grace window)

Superseded sweep generations and closed fronts:
- expand_sweep/ 1-4 (front retired; see unclear #2 first)
- fable1/ v1-v5 + fable1_v6 (default extracted to viola_default; v6 gated on the modified file)
- fm_oversample/, fm_oversample2/ (aliasing question resolved, os convention locked)
- formantseq_sweep/, formantseq_sweep2/, vowel_baseline/, vowelseq2/, vowels/,
  vowel_tweak/, vowel_tweak2/ (winners locked into library/voice)
- viola/ (v1-v7 recipes superseded by viola_default), clarinet_c1/,
  clarinet_c2/ minus the modified file
- piano1/ (superseded by ks_piano), fhn/ (342 files, April) + root fhn_* (4) + gs_* (3;
  GrayScottSource lives only on unmerged chord-walker branch)
- fm_spacy_* root ladder (13 untracked; keeper n177 already promoted in B)
- sort_bubble_cursor, sort_gnome_ramp, sort_insertion (algev_test_sort fixture retained in C)
- Early hand experiments: Additive1-6, SaveTest, Squeaker, NoisesOff, MurmurateThis,
  StringMPX, String2, breathy_vocal_pad, PluckU (tracked), String1 (tracked), bowed1, reed1
- patches/fable1_v4/output.wav → DELETE (render, not a patch)

## E. UNCLEAR — Matt's call

1. **Early hand-built instruments (tracked root):** guitar_pluck, french_horn,
   brass_trumpet, brass_bloom, bowed_cello, reed_clarinet, fm_bass. Pre-date the
   library concept; some sounded decent once. library/ (which family), fixtures/, or old/?
2. **expand_sweep cherry-picks:** the front retired but the run-9 verdict ranked
   several as keeper sound effects: fifth_r2 ("best, lusher"), leslie_swirl_combo,
   fifth_leslie_morph ("super cool"), pi_swept ("villain vibe"), narrow2wide,
   attack_wide2narrow. Promote these six to library/effects/ before the dirs hit old/?
3. **vowel_grid/ (58):** not a sweep — the systematic IPA formant catalog
   (speech M/W/C x 12 vowels + sung 4 voices x 5), with manifest. Superseded as
   audition material but it is reference data. library/voice/grid/, fixtures/, or old/?
4. **liar2 keeper:** liar2_110_x7 was called "~optimal" (kept x8/x9 for rigor).
   Promote liar2_110_x7 + liar2_220_x7 to library/effects/ (or voice/)? Rest to old/.
5. **Untracked headless strays:** gs_* included in D on the chord-walker rationale —
   veto if you want them held for that branch landing.

MATT: Let's make a /scratch under /renders and /scores for truly random stuff, and
move all of the above - I will manual deal w/

## Execution notes (after Matt's verdicts)

- Tracked moves via git mv (one restructure commit); untracked via filesystem move.
- Update: .gitignore (scores/ working dirs, renders lane level), CLAUDE.md structure
  section, REVIEW.md paths for moved comp templates, memory notes. patches/old/
  path is unchanged — scheduled-prompt purge lines stay as written.
- renders/ gains the {dsp, comp} lane level; existing renders/pending families
  move under renders/dsp/pending/ (they are all dsp-lane efforts); comp render
  dirs (cadential_arrival_ab, passage_*, markov_*, voicing_ab, section_key)
  move under renders/comp/pending/ with comp REVIEW.md paths updated.
- curated/ excluded per Matt (manual pass, then delete).
