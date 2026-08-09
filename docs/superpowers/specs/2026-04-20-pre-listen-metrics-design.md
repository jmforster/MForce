# Pre-listen Metrics Pipeline — Design Spec

**Status:** Draft for review.
**Author:** Claude (DSP/UI).
**Date:** 2026-04-20.

---

## Problem

Today we have no automated feedback on what a patch actually produced. Today's session surfaced the pattern: `bowed_cello.json`, `brass_trumpet.json`, and `french_horn.json` all rendered 0 audio samples via raw CLI mode, and we only noticed because we manually inspected peak/rms lines. In normal use, silent or clipped renders slip through until Matt listens. That's unacceptable for any agent-driven patch search loop, and friction even for manual authoring.

Separately, there's no numerical way to compare patches — "is this patch brighter than that one?" requires ears. Any future patch-search, similarity retrieval, or batch-sweep analysis needs a shared feature vocabulary.

## Goals (in scope)

1. **Automated render health check.** Given a `.wav`, return `{ok, warn, fail}` + reasons. Catches: silent regions, clipping, DC offset, zero peak.
2. **Timbral fingerprint.** A compact, comparable feature vector per render — spectral descriptors + MFCCs. Enables distance metrics between patches.
3. **Sidecar persistence.** Features saved as `<render>.features.json` next to the `.wav`. Survives across sessions, greppable, diffable.
4. **Agent-friendly CLI.** Runnable as a standalone script; exit code reflects health; stdout is machine-readable; suitable for `Bash` tool invocation.
5. **Zero impact on the existing C++ build.** Pure Python add-on. No new C++ dependencies.

## Non-goals (out of scope — defer to follow-ups)

- Real-time feature display inside `mforce_ui` (separate ask; C++-side).
- Patch-search loop (Bayesian opt, CMA-ES) that *uses* these features. This spec provides the measurement; search is a later project.
- Perceptual models (LUFS, sharpness, roughness) — lower ROI for MVP; add if a user story needs them.
- ML embeddings (OpenL3, CLAP) — valuable for semantic similarity but wrong shape for MVP; phase 2.
- Inline C++ integration (mforce_cli calling the metrics automatically). Phase 2 after the Python tool is stable.

## User stories

**US1 — Manual render check.** Matt renders a new patch. One command produces a report:
```
$ python tools/metrics/check.py renders/foo.wav
OK | peak=0.59 rms=0.053 centroid=1842Hz zcr=0.12 f0=220.0Hz
Features: renders/foo.features.json
```

**US2 — Silent-render alarm.** Matt renders a broken patch; the tool exits non-zero and explains:
```
$ python tools/metrics/check.py renders/broken.wav
FAIL | silent_region=100% nonzero=0/240000
Reasons: zero peak; expected note window silent.
```

**US3 — Agent batch sweep.** Claude orchestrates 40 patch variants, runs the tool on each, reads the sidecar JSONs, ranks and reports. Tool must have a stable schema and predictable exit codes.

**US4 — Regression gate.** After a DSP refactor (like today's `RenderContext` sweep), Claude renders a canonical patch set, compares new features against stored baselines, flags any render whose feature vector moved by more than ε. Cheaper and stronger than peak/rms diff alone.

## Architecture

**Shape:** single Python script + small library, self-contained in `tools/metrics/`.

```
tools/metrics/
  check.py          # CLI entry point: wav in → features.json out + health report
  features.py       # feature extraction (librosa wrappers + a few custom metrics)
  health.py         # threshold logic (ok/warn/fail classification)
  requirements.txt  # librosa, soundfile, numpy, scipy
  README.md         # how to run, how to interpret output
```

No build-system integration. `python check.py <wav>` is the contract. Output conventions:
- **Stdout:** one-line summary (`OK | key=val key=val …`), human-readable.
- **Sidecar:** `<wav-basename>.features.json` next to the input wav.
- **Exit code:** `0` = ok, `1` = warn, `2` = fail, `64` = tool error (missing dep, file not found).

**Why Python:** librosa gets us 80% of the features in 10 lines. scipy covers the rest. No equivalent C++ library comes close in developer-minutes. Subprocess-from-C++ cost is negligible for offline use.

**Why not a C++ extension to mforce_cli (yet):** (a) Python's library ecosystem is where MIR lives; rebuilding in C++ is months. (b) The tool needs to evolve (add features, tune thresholds) faster than the C++ build cycle. (c) Agent workflows already shell out to Python; no added complexity.

## MVP feature set

Four categories, kept minimal. Every feature here has a reason; anything not needed now is in the "follow-ups" pile.

### Health (must compute; drives ok/warn/fail)
| Feature | Definition | Source |
|---|---|---|
| `peak` | `max(abs(x))` | direct |
| `rms` | `sqrt(mean(x^2))` | direct |
| `crest_factor` | `peak / rms` | derived |
| `dc_offset` | `mean(x)` | direct |
| `clip_count` | # samples with `abs(x) >= 0.999` | direct |
| `silent_start_frac` | fraction of initial samples where `abs(x) < 1e-4` | direct |
| `silent_end_frac` | same, trailing | direct |
| `silent_total_frac` | overall silent fraction | direct |
| `nonzero_count` | total nonzero samples | direct |

### Pitch
| Feature | Definition | Source |
|---|---|---|
| `f0_mean_hz` | YIN-estimated fundamental, median over voiced frames | `librosa.pyin` |
| `f0_stability` | std / mean of f0 across voiced frames | derived |
| `voiced_frac` | fraction of frames classified voiced | `librosa.pyin` |

### Timbre (spectral)
| Feature | Definition | Source |
|---|---|---|
| `centroid_mean`, `centroid_std` | spectral centroid, mean + std over frames | `librosa.feature.spectral_centroid` |
| `rolloff_mean` | spectral roll-off frequency (85%) | `librosa.feature.spectral_rolloff` |
| `bandwidth_mean` | spectral bandwidth | `librosa.feature.spectral_bandwidth` |
| `flatness_mean` | spectral flatness (tonal↔noise indicator) | `librosa.feature.spectral_flatness` |
| `zcr_mean` | zero-crossing rate | `librosa.feature.zero_crossing_rate` |
| `mfcc_mean[13]`, `mfcc_std[13]` | MFCCs, mean + std per coefficient | `librosa.feature.mfcc` |

### Envelope (temporal)
| Feature | Definition | Source |
|---|---|---|
| `onset_time_s` | first detected onset | `librosa.onset.onset_detect` |
| `attack_time_s` | time from onset to peak | derived |
| `stationarity` | std(centroid_over_frames) / mean | derived |

Total: ~30 scalars + 2 × 13 MFCC vectors. One sidecar JSON, ~1–2 KB.

**Deferred features** (not MVP; candidates for phase 2):
- LUFS loudness (pyloudnorm)
- Sharpness, roughness (not in librosa; needs custom or external lib)
- Onset strength envelope as a time series (not just scalar)
- Harmonic/inharmonic separation ratio
- OpenL3 / CLAP embeddings

## Health thresholds

Thresholds are advisory and tunable. MVP values:

| Status | Condition |
|---|---|
| **FAIL** | `peak == 0` OR `silent_total_frac > 0.95` OR `clip_count / n > 0.01` |
| **WARN** | `peak > 0.99` OR `abs(dc_offset) > 0.02` OR `silent_start_frac > 0.20` OR `silent_end_frac > 0.20` OR `voiced_frac < 0.30` |
| **OK** | otherwise |

Reasons are enumerated in the output so the user knows *why* a warn/fail fired.

Thresholds live in `health.py` as named constants; easy to tune after Matt's first week of use.

## Output schema

**Sidecar JSON** (`foo.features.json`):
```json
{
  "schema_version": 1,
  "input_path": "renders/foo.wav",
  "sample_rate": 48000,
  "duration_s": 4.0,
  "channels": 1,
  "health": {
    "status": "warn",
    "reasons": ["dc_offset_above_0.02"]
  },
  "features": {
    "peak": 0.593,
    "rms": 0.053,
    ...,
    "mfcc_mean": [-2.1, 4.3, …],
    "mfcc_std":  [0.8, 1.2, …]
  }
}
```

`schema_version` is bumped when the feature set changes incompatibly. Consumers (agent scripts, future regression tools) can branch on it.

**Stdout** (single line for grep-ability):
```
OK | peak=0.593 rms=0.053 centroid=1842Hz f0=220Hz | features: renders/foo.features.json
```

**Exit codes:**
- `0` — OK
- `1` — WARN (features still computed and written)
- `2` — FAIL (features still computed and written — a FAIL patch's features are useful diagnostics)
- `64` — tool error (no features written)

## Integration points

**Phase 1 (MVP, this spec):**
- Standalone Python CLI. No C++ changes.
- Agent workflow: after `mforce_cli` renders, run `check.py` on the output; read sidecar for downstream reasoning.

**Phase 2 (deferred):**
- Optional `mforce_cli --check` flag that spawns the Python script via subprocess.
- UI renders-to-audition pipeline calls it in background; shows health badge in transport panel.
- Regression-gate: `tools/metrics/gate.py baseline.json current.json` reports significant drift.

## Tech stack

- **Python 3.10+** (librosa baseline).
- **librosa** (MIT) — spectral features, MFCC, pitch, onset.
- **soundfile** (BSD) — WAV read with correct sample-rate detection.
- **numpy** (BSD) — vector math.
- **scipy** (BSD) — transitive dep of librosa; we may use `scipy.signal` directly for DC offset + clip detection (faster than librosa wrappers for trivial ops).

No GPU, no ML libraries in MVP. Rough runtime on a 4-second 48 kHz WAV: ~200–400 ms, dominated by librosa's MFCC computation.

## Open questions (resolve before plan)

1. **Where do rendered WAVs live?** Today `renders/` at the repo root. The sidecar JSON goes next to the WAV. Confirm this is the expected layout — happy to put sidecars in `renders/features/` instead if that's cleaner.
2. **Do we want the schema-version field to block downstream consumers on mismatch, or just log?** I'd say "pass through with warning" for MVP; hard-block once a real downstream exists.
3. **Should YIN pitch tracking assume a monophonic render?** Polyphonic content breaks YIN; it'll return noisy f0 values. Acceptable for MVP (most test patches are monophonic) but worth noting — polyphonic patches get low `voiced_frac` and we shouldn't alarm on that alone.
4. **Stereo vs mono.** Today mforce_cli writes stereo WAVs. MVP can analyze the L+R sum (downmix to mono) and flag any channel imbalance > threshold. OK?
5. **Expected note window.** `silent_start_frac` is easy to compute over the whole WAV. Matt's patches often have an attack ramp + decay tail, so the start+end of a render can be legitimately quiet. Two options: (a) trim silence from both ends before computing `silent_total_frac`, (b) take a silence threshold and only fail if the *middle* region is silent. I'd propose (b) — safer for instrument patches.

## Milestones

1. **M1: Skeleton + health.** `check.py` reads a WAV, computes the 9 health metrics, emits sidecar, exits with status code. No timbre features yet. ~2 hrs.
2. **M2: Spectral features.** Add centroid/rolloff/bandwidth/flatness/ZCR means. ~1 hr.
3. **M3: Pitch + MFCC + envelope.** YIN, MFCCs, onset/attack/stationarity. ~1.5 hrs.
4. **M4: Threshold tuning + README.** Run against the existing render library, adjust thresholds, document usage. ~1 hr.

Total: ~half day, matches the checkpoint estimate.

## Validation

MVP is done when:
1. `check.py` on `renders/pluck_sanity.wav` reports `OK` with sensible numbers.
2. `check.py` on a known-silent render reports `FAIL` with `zero_peak`.
3. `check.py` on a clipped render reports `FAIL` with `excessive_clipping`.
4. Running it on all `renders/*.wav` in the repo takes < 30 s total.
5. Sidecar JSONs are machine-readable (valid JSON, schema_version = 1).
6. README shows an agent how to invoke it and interpret the output in under 5 lines of prose.

No user listening required to confirm success.

docs/superpowers/specs/2026-04-22-composer-owns-event-sequence-design.md
 docs/superpowers/specs/2026-04-22-composer-owns-event-sequence-design.md