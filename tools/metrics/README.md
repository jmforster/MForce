# Pre-listen metrics

Standalone Python tool that analyzes a rendered WAV and emits a sidecar
`.features.json` + health classification. Used to catch silent or broken
renders automatically, and to provide a numeric fingerprint for patch
comparison / search.

## Setup

```
pip install -r tools/metrics/requirements.txt
```

Or in a venv:
```
python -m venv tools/metrics/.venv
source tools/metrics/.venv/Scripts/activate   # Windows bash
pip install -r tools/metrics/requirements.txt
```

## Usage

```
python tools/metrics/check.py <path-to.wav>
```

Emits `<path-to>.features.json` next to the input. Prints a one-line
summary to stdout and exits with a status code.

Example:
```
$ python tools/metrics/check.py renders/pluck_sanity.wav
OK | peak=0.593 rms=0.0525 centroid=1310Hz f0=264.7Hz | features: renders/pluck_sanity.features.json
```

## Exit codes

| Code | Meaning |
|------|---------|
| 0 | OK — no issues |
| 1 | WARN — issues flagged but render is usable |
| 2 | FAIL — render is broken (silent, clipped, etc.) |
| 64 | tool error (missing file, load failure) |

## Output schema

`schema_version: 1`. Consumers should pass through unknown future schema
versions with a warning rather than blocking.

```json
{
  "schema_version": 1,
  "input_path": "renders/foo.wav",
  "sample_rate": 48000,
  "duration_s": 4.0,
  "channels": 2,
  "health": {
    "status": "OK",
    "reasons": []
  },
  "features": {
    "peak": 0.593,
    "rms": 0.0525,
    "crest_factor": 11.29,
    "dc_offset": 0.0,
    "clip_count": 0,
    "nonzero_count": 95999,
    "silent_start_frac": 0.0,
    "silent_end_frac": 1.0,
    "silent_middle_frac": 0.13,
    "silent_total_frac": 0.21,
    "centroid_mean": 1309.78,
    "centroid_std": 742.1,
    "rolloff_mean": 1636.44,
    "bandwidth_mean": 1284.3,
    "flatness_mean": 0.012,
    "zcr_mean": 0.02,
    "f0_mean_hz": 264.7,
    "f0_stability": 0.04,
    "voiced_frac": 0.80,
    "mfcc_mean": [-482.1, 95.3, ...],
    "mfcc_std": [48.2, 18.7, ...],
    "onset_time_s": 0.032,
    "attack_time_s": 0.0,
    "stationarity": 0.845
  }
}
```

## Feature set

**Health (9 scalars)** — `peak`, `rms`, `crest_factor`, `dc_offset`,
`clip_count`, `nonzero_count`, `silent_{start,end,middle,total}_frac`.

**Pitch (3)** — `f0_mean_hz` (YIN, monophonic assumption),
`f0_stability`, `voiced_frac`. Polyphonic renders will get low
`voiced_frac` — that's expected, not a warning.

**Spectral (6)** — `centroid_{mean,std}`, `rolloff_mean`,
`bandwidth_mean`, `flatness_mean`, `zcr_mean`.

**Envelope (3)** — `onset_time_s`, `attack_time_s`, `stationarity`.

**Timbral fingerprint (2 × 13)** — `mfcc_mean[13]`, `mfcc_std[13]`.
Use for distance-based comparison between patches.

## Health rules

| Status | Triggers |
|--------|----------|
| **FAIL** | zero peak; silent_middle > 95%; clip_count > 1% of samples |
| **WARN** | peak ≥ 0.99 (headroom); DC > 0.02; silent_start > 50% (very slow attack); stereo imbalance > 10% |
| **OK** | otherwise |

Trailing silence (silent_end) is **not warned** — instrument patches
naturally decay to zero after the note window.

## Stereo handling

Stereo WAVs are downmixed to mono (`0.5 * (L + R)`) before analysis.
Per-channel RMS imbalance > 10% triggers a `channel_imbalance` WARN.

## Agent usage pattern

```bash
python tools/metrics/check.py renders/foo.wav
status=$?
if [ $status -eq 2 ]; then
  echo "Render failed — investigate before listening"
  # sidecar is still written for diagnostic use
fi

# Read specific features for downstream reasoning
python -c "
import json
f = json.load(open('renders/foo.features.json'))['features']
print('centroid:', f['centroid_mean'])
"
```

For batch sweeps (patch search, regression gates), render a set of WAVs
and run this tool on each. Sidecars are stable JSON — safe to grep, diff,
and load into downstream tools.

## Not implemented yet (spec phase 2)

LUFS loudness (pyloudnorm), sharpness/roughness (custom), harmonic/
inharmonic separation, onset strength envelope time-series, ML audio
embeddings (OpenL3, CLAP), inline C++ integration, regression-gate
comparison utility.

See `docs/superpowers/specs/2026-04-20-pre-listen-metrics-design.md`.
