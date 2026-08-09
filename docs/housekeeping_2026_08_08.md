# Repo housekeeping — 2026-08-08

Survey + two cleanup passes. "Second pass" is the latest state; older sections kept for the record.

## Second pass (Matt's round-2 annotations, executed same day)

| Annotation | Status |
|---|---|
| renders/library: keep ignoring | ✅ decision recorded — blanket renders/ ignore stays |
| CLAUDE.md: prune | ✅ legacy-repo section replaced with a two-line pointer to siblings ../mforce-legacy and ../mforce-unity; porting-era guidance dropped; garbled "## Commands" header fixed; sweep/pending/library structure documented. Committed with the docs trail. |
| Docs backlog: agree | ✅ all 21 superpowers specs/plans committed (`87747f2`, 10,262 lines). Working-notes triage still open — see leftovers. |
| Big disk items: delete | ✅ corpus/MTD_1.0.0.zip (497 MB), build/ (785 MB), research __MACOSX junk, and the **16 render dirs listed in the table below** (~3.6 GB incl. old/) all deleted. |

**Disk free: 8 GB → 21 GB.** renders/ is down from 4.7 GB to 1.1 GB.

### Deliberately NOT deleted from renders/
The "Delete" annotation sat under the table of the 16 biggest dirs, so I deleted exactly those.
~60 smaller dirs (1.1 GB total) remain, including **ks_piano/ (your pending A/B audition)**,
fable1*/, cmaes_*/, clarinet_c2/, plus loose test WAVs and a `desktop.ini`. If you meant
scorched earth, say so and I'll clear everything except sweep/pending/library + ks_piano.

### Remaining leftovers (unchanged from pass 1)
1. **Patch triage into sweep/pending/library** — fhn/ (342), curated/ (10), vowels/ (32), viola/ (7), ~60 loose test patches, and the 564 tracked sweep-shaped dirs (needs a proposal). Library taxonomy also undecided.
2. **Docs working notes** (~22 session .txt/.md at docs/ root + research files) — needs your keep/kill pass.
3. **build/ is gone** — next build is a full rebuild.

### Headline numbers after pass 2
Tracked 1,067 · untracked 496 (patches 454, docs 30, engine 8, research 2, lib 2) · disk free 21 GB.

---

Below: pass-1 record and original survey.

## Actions taken (Matt's annotations, executed 2026-08-08)

| Annotation | Status |
|---|---|
| tools: COMMIT all except .pyc | ✅ done — commit `0bd0cd9`, 13 files (ppl_to_json source, generators, migration one-offs) |
| mforce-legacy: DELETED | ✅ confirmed gone (Matt deleted) |
| mforce-unity: DELETED | ✅ confirmed gone (Matt deleted; it lives at github.com/jmforster/MForce-unity) |
| snaps: IGNORE .PNG and .JPG | ✅ .gitignore now has snaps/*.png/jpg (both cases), verified with check-ignore |
| specs: DELETE | ✅ deleted all 7 explore_*.json, including `git rm` of the one tracked file (explore_smoke.json); specs/ removed |
| Root strays: DELETE ALL | ✅ deleted out.wav_1/2/3, prompt1.txt, Pre_crash.txt, mforce_ui_crash.log, all root PNGs, and `git rm` combo.txt. Also deleted the three UI state files (imgui.ini, mforce_recents.json, mforce_ui_settings.json) — **note:** these regenerate on next UI run, but window layout + recents list are reset. All three are now gitignored so the regenerated ones stay out of status. |
| New sweep/pending/library structure | ✅ skeleton created for both patches/ and renders/; patches/sweep/ + patches/pending/ gitignored; patches/library/ tracked (has .gitkeep). renders/ was already ignored wholesale. |

### New workflow structure (from Matt's note)

For both `patches/` and `renders/`, in parallel:
- `sweep/` — ML-ears fodder, never listened to. Disposable, **not** checked in.
- `pending/` — candidates for Matt to audition. Disposable, **not** checked in.
- `library/` — curated keepers, organized by instrument family/type. **Checked in** (patches).

## Leftover / ambiguous / open questions

1. **renders/library — checked in or not?** Your note says library = "curated good patches, checked in", but for renders that would mean WAVs in git. Currently the blanket `renders/` ignore still covers it. Assumed *not* checked in until you say otherwise (renders are regenerable from patches).
MATT: yes, continue to ignore, we don't need WAVs in git

2. **Populating the structure is untouched** — nothing was moved into sweep/pending/library. Needs your triage:
   - patches/fhn/ (342 untracked) — sweep material? Looks like ML-ears fodder → probably `git`-invisible move to patches/sweep/ or delete.
   - patches/curated/ (10) — obvious library candidate.
   - patches/vowels/ (32), viola/ (7) — library or pending?
   - ~60 loose test patches at patches/ root (test_k467_*, sort_*, mux_*…) — pending/delete?
   - Existing *tracked* patch dirs (fm_matrix2, vowel_grid, vowel_tweak*, liar2, expand_sweep*…, 564 files) are sweep-shaped but tracked — moving/deleting them is a git operation; want a proposal first?
   - What's the instrument-family tree for library/? (e.g. keys/, strings/, winds/, bells/, synth/?)
3. **CLAUDE.md still references the deleted repos** — "Legacy C# core implementation: ./mforce-legacy" and "./mforce-unity" (plus porting notes about gen-1/gen-2 wrappers). Left alone since it's your instructions file — say the word and I'll prune that section.
Prune, that is old news. Both -unity and -legacy are still there as *siblings* under repos if they need to be consulted

4. **Docs backlog (52 untracked)** — 21 superpowers specs/plans + ~22 working notes + research files. Recommendation stands: commit the specs/plans trail, then triage the session-scratch .txt files.
MATT: Yes, agree with recommendation.

5. **Big disk items untouched** (no annotation from you): corpus/MTD_1.0.0.zip (497 MB, extracted already), renders/ pruning (4.7 GB incl. old/ 1.6 GB), build/ (785 MB, regenerable), research/inst_samples __MACOSX junk (~180 files).
MATT: delete.

## Headline numbers (after cleanup pass)

| Metric | Before | After |
|---|---|---|
| Tracked files | 1,035 | 1,046 |
| Untracked files (not ignored) | 854 | **518** |
| Ignored files | 12,531 | 12,428 |
| Repo size on disk | ~7 GB | ~7 GB (deletes were small; big wins are item 5 above) |
| Free space on C: | ~8 GB | ~8 GB |

Remaining untracked: patches 454 (fhn 342 + vowels/curated/viola/loose), docs 52, engine 8 (third_party nested clones), research 2, lib 2.

## Size by top-level directory (survey, sizes unchanged)

| Directory | Size | Git status | Notes |
|---|---|---|---|
| renders/ | **4.7 GB** | all ignored (4,229 files) | breakdown below |
| build/ | 785 MB | ignored | regenerable |
| research/ | 719 MB | mostly ignored | inst_samples/ 672 MB, ml_ears/ 46 MB |
| corpus/ | 580 MB | ignored | **497 MB is MTD_1.0.0.zip** (extracted to mtd_full/ 60 MB); essen 28 MB, nottingham 17 MB, mtd_seg 8 MB |
| docs/ | 52 MB | 155 tracked, 52 untracked | |
| engine/ | 20 MB (141 MB with third_party .git) | 117 tracked | nested clones: imgui, imnodes, glfw, rtaudio |
| lib/ | 7.7 MB | 1 tracked, 2 untracked | json + ppl |
| patches/ | 5.2 MB | 564 tracked, 454 untracked (sweep/ now ignored) | |
| tools/ | 1.3 MB | 86 tracked | ✅ fully tracked now |
| snaps/ | 260 KB | ignored | screenshots |
| styles/ | 5 KB | 2 tracked | chord-walker harmony style tables |

## renders/ breakdown (4.7 GB, all git-ignored) — **all 16 dirs below DELETED in pass 2**

| Subdir | Size | Subdir | Size |
|---|---|---|---|
| old/ | **1.6 GB** | markov_phrases5/ | 104 MB |
| passage_end_grid/ | 412 MB | markov_phrases6/ | 102 MB |
| nulltest_fm/ | 260 MB | markov_phrases3/ | 95 MB |
| fhn/ | 189 MB | corpus_flavors/ | 71 MB |
| markov_phrases4/ | 149 MB | markov_phrases/ | 68 MB |
| passage_strategies2/ | 144 MB | fm_matrix2/ | 67 MB |
| engine_passage_strategies/ | 138 MB | passage_chords_ab/ | 65 MB |
| explore/ | 124 MB | nulltest/ | 62 MB |

All regenerable from patches + specs (seeds stored in JSON). Archive-or-delete
territory; the markov_phrases* six total ~570 MB.

MATT: Delete