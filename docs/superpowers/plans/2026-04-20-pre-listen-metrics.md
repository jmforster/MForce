# Pre-listen Metrics Pipeline — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Standalone Python CLI (`python tools/metrics/check.py <wav>`) that emits a sidecar `.features.json`, classifies render health as OK/WARN/FAIL with reasons, and computes a ~30-scalar timbral fingerprint (spectral + pitch + MFCC + envelope). Enables automated render QA and feeds future patch-search workflows.

**Architecture:** Pure Python, no C++ changes. Script loads WAV via `soundfile`, downmixes to mono, runs feature extractors (mostly `librosa` one-liners plus a few custom scalar calculations), applies thresholds, writes sidecar JSON next to the WAV, prints one-line summary, exits with status code matching health. Zero build-system integration; invoked directly.

**Tech Stack:** Python 3.11 (available at `/c/Users/Matt Forster/AppData/Local/Programs/Python/Python311/python`), `librosa`, `soundfile`, `numpy`, `scipy`. Installed globally or in a venv — Matt's choice.

**Scope reference:** `docs/superpowers/specs/2026-04-20-pre-listen-metrics-design.md` (resolutions applied: sidecar next to WAV, pass-through schema-version mismatch, monophonic pitch assumption, downmix stereo, silent-middle detection).

**Validation philosophy:** No pytest. Every task validates by running `check.py` on specific inputs and grepping the output. Known baselines: `renders/pluck_sanity.wav` (exists, working — OK expected) plus two synthesized fixtures (zero-filled for silent, saturated for clipped). Synthesized fixtures are throwaway — regenerated each time, not committed.

**Not in scope:** LUFS/sharpness/roughness, ML embeddings, inline C++ integration, regression-gate tool, batch comparison utility. All deferred per spec.

---

## File Structure

**New files (created by this plan):**
- `tools/metrics/check.py` — CLI entry point (~80 lines when done)
- `tools/metrics/features.py` — extraction functions grouped by category (~150 lines)
- `tools/metrics/health.py` — threshold constants + `classify()` (~60 lines)
- `tools/metrics/requirements.txt` — pinned dependencies
- `tools/metrics/README.md` — usage + output contract

**Modified files:** none in C++. No build-system changes.

---

## Task 1: Scaffolding and dependency install

**Files:**
- Create: `tools/metrics/requirements.txt`
- Create: `tools/metrics/README.md` (stub for now)
- Create: `tools/metrics/check.py` (import-only stub)
- Create: `tools/metrics/features.py` (import-only stub)
- Create: `tools/metrics/health.py` (import-only stub)

- [ ] **Step 1: Create the directory and stub files**

```bash
mkdir -p tools/metrics
```

`tools/metrics/requirements.txt`:
```
librosa==0.10.2
soundfile==0.12.1
numpy>=1.24,<2.0
scipy>=1.10,<2.0
```

(librosa 0.10.x is stable and matches 3.11; numpy pinned < 2 because librosa 0.10 isn't compatible with NumPy 2 yet.)

`tools/metrics/check.py`:
```python
#!/usr/bin/env python3
"""Pre-listen metrics — render health + timbral fingerprint.

Usage:
    python tools/metrics/check.py <wav-path>

Emits <wav-basename>.features.json next to the input wav.
Exit codes: 0=OK, 1=WARN, 2=FAIL, 64=tool error.
"""

import sys

def main():
    if len(sys.argv) != 2:
        print("Usage: check.py <wav-path>", file=sys.stderr)
        return 64
    wav_path = sys.argv[1]
    # Real implementation lands in Task 2.
    print(f"check.py: not implemented yet for {wav_path}")
    return 64

if __name__ == "__main__":
    sys.exit(main())
```

`tools/metrics/features.py`:
```python
"""Feature extraction functions. See check.py for orchestration."""
```

`tools/metrics/health.py`:
```python
"""Health classification: OK / WARN / FAIL + reasons."""
```

`tools/metrics/README.md`:
```markdown
# Pre-listen metrics

Standalone Python tool that analyzes a rendered WAV and emits health
classification + timbral features as a sidecar JSON.

## Setup

```
pip install -r tools/metrics/requirements.txt
```

(Or create a venv first if you want isolation. Nothing here assumes venv.)

## Usage

See `check.py --help`. (Filled in by Task 5.)
```

- [ ] **Step 2: Install dependencies**

Run:
```bash
python -m pip install -r tools/metrics/requirements.txt 2>&1 | tail -5
```

Expected: PASS with "Successfully installed librosa-0.10.2 soundfile-0.12.1 ...". If pip complains about numpy version conflict with librosa, relax numpy pin to whatever librosa wants.

- [ ] **Step 3: Verify imports work**

```bash
python -c "import librosa, soundfile, numpy, scipy; print('OK')"
```

Expected: `OK`. If any import fails, fix `requirements.txt` before proceeding.

- [ ] **Step 4: Verify the stub runs**

```bash
python tools/metrics/check.py renders/pluck_sanity.wav
```

Expected: `check.py: not implemented yet for renders/pluck_sanity.wav` on stderr, exit code 64.

- [ ] **Step 5: Commit**

```bash
git add tools/metrics/
git commit -m "feat(metrics): scaffold pre-listen metrics tool

Adds tools/metrics/{check.py, features.py, health.py, requirements.txt,
README.md}. Stubs only; real implementation lands in follow-up commits.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 2: Health metrics + sidecar output (Milestone M1)

**Files:**
- Modify: `tools/metrics/features.py`
- Modify: `tools/metrics/health.py`
- Modify: `tools/metrics/check.py`

- [ ] **Step 1: Implement `features.py:compute_health`**

Replace stub with:
```python
"""Feature extraction functions. See check.py for orchestration."""

import numpy as np

CLIP_THRESHOLD = 0.999
SILENT_THRESHOLD = 1e-4

def compute_health(y: np.ndarray) -> dict:
    """Health-related scalars for a mono float32 signal in [-1, 1]."""
    n = len(y)
    if n == 0:
        return dict(peak=0.0, rms=0.0, crest_factor=0.0, dc_offset=0.0,
                    clip_count=0, nonzero_count=0,
                    silent_start_frac=1.0, silent_end_frac=1.0,
                    silent_middle_frac=1.0, silent_total_frac=1.0)

    abs_y = np.abs(y)
    peak = float(abs_y.max())
    rms = float(np.sqrt(np.mean(y * y)))
    crest = (peak / rms) if rms > 0 else 0.0
    dc = float(np.mean(y))
    clip_count = int(np.sum(abs_y >= CLIP_THRESHOLD))
    silent_mask = abs_y < SILENT_THRESHOLD
    nonzero_count = int(n - np.sum(silent_mask))

    # Edge fractions (first 10%, last 10%) for context only — not used in FAIL.
    edge = max(1, n // 10)
    silent_start_frac = float(np.mean(silent_mask[:edge]))
    silent_end_frac = float(np.mean(silent_mask[-edge:]))

    # Middle region: skip first+last 10%. This is what drives the FAIL rule —
    # attack ramps and decay tails are legitimate quiet and shouldn't false-fail.
    mid_start = edge
    mid_end = n - edge
    if mid_end > mid_start:
        silent_middle_frac = float(np.mean(silent_mask[mid_start:mid_end]))
    else:
        silent_middle_frac = float(np.mean(silent_mask))

    silent_total_frac = float(np.mean(silent_mask))

    return dict(
        peak=peak, rms=rms, crest_factor=crest, dc_offset=dc,
        clip_count=clip_count, nonzero_count=nonzero_count,
        silent_start_frac=silent_start_frac,
        silent_end_frac=silent_end_frac,
        silent_middle_frac=silent_middle_frac,
        silent_total_frac=silent_total_frac,
    )
```

- [ ] **Step 2: Implement `health.py:classify`**

Replace stub with:
```python
"""Health classification: OK / WARN / FAIL + reasons."""

# Thresholds (tuned in Task 5 against real render library).
FAIL_SILENT_MIDDLE = 0.95
FAIL_CLIP_FRAC = 0.01        # > 1% of samples clipped → FAIL
WARN_PEAK_HEADROOM = 0.99    # peak ≥ this → WARN
WARN_DC_OFFSET = 0.02
WARN_SILENT_EDGE = 0.20      # leading/trailing silent region exceeds 20% → WARN
WARN_CHANNEL_IMBALANCE = 0.10  # |L_rms - R_rms| / max(L_rms, R_rms) > this → WARN

def classify(features: dict, stereo_imbalance: float = 0.0) -> tuple[str, list[str]]:
    """Return (status, reasons). status in {'OK', 'WARN', 'FAIL'}."""
    reasons = []
    fail = False
    warn = False

    peak = features.get('peak', 0.0)
    clip = features.get('clip_count', 0)
    nonzero = features.get('nonzero_count', 0)
    total = nonzero + int((features.get('silent_total_frac', 0.0) * (
        nonzero / max(1 - features.get('silent_total_frac', 0.0), 1e-9))))
    # Simpler: recompute total from silent_total_frac if we stored it. We'll
    # receive n separately in check.py; here use clip/nonzero proportions.

    # FAIL rules
    if peak == 0.0:
        reasons.append('zero_peak')
        fail = True
    if features.get('silent_middle_frac', 0.0) > FAIL_SILENT_MIDDLE:
        reasons.append('silent_middle_region')
        fail = True
    if nonzero > 0 and clip / nonzero > FAIL_CLIP_FRAC:
        reasons.append('excessive_clipping')
        fail = True

    # WARN rules (only if not already FAIL — still record reasons though)
    if peak >= WARN_PEAK_HEADROOM and peak > 0:
        reasons.append('peak_near_clip')
        warn = True
    if abs(features.get('dc_offset', 0.0)) > WARN_DC_OFFSET:
        reasons.append('dc_offset_high')
        warn = True
    if features.get('silent_start_frac', 0.0) > WARN_SILENT_EDGE:
        reasons.append('silent_start')
        warn = True
    if features.get('silent_end_frac', 0.0) > WARN_SILENT_EDGE:
        reasons.append('silent_end')
        warn = True
    if stereo_imbalance > WARN_CHANNEL_IMBALANCE:
        reasons.append('channel_imbalance')
        warn = True

    if fail:
        return 'FAIL', reasons
    if warn:
        return 'WARN', reasons
    return 'OK', reasons
```

(Note: the `total` estimation in my first draft is ugly. Simplify: pass `n` explicitly if needed. For MVP the FAIL rule uses `clip / nonzero` which is enough. I've left that in.)

- [ ] **Step 3: Implement `check.py` orchestration**

Replace stub with:
```python
#!/usr/bin/env python3
"""Pre-listen metrics — render health + timbral fingerprint."""

import json
import os
import sys
import numpy as np
import soundfile as sf

from features import compute_health
from health import classify

SCHEMA_VERSION = 1

def load_and_downmix(wav_path: str):
    """Return (mono float32 in [-1,1], sample_rate, channels, stereo_imbalance)."""
    y, sr = sf.read(wav_path, dtype='float32', always_2d=True)
    channels = y.shape[1]
    if channels == 1:
        mono = y[:, 0]
        imbalance = 0.0
    else:
        # Downmix L+R → mono (scaled by 0.5 to preserve headroom).
        mono = 0.5 * (y[:, 0] + y[:, 1])
        l_rms = float(np.sqrt(np.mean(y[:, 0] ** 2)))
        r_rms = float(np.sqrt(np.mean(y[:, 1] ** 2)))
        denom = max(l_rms, r_rms, 1e-9)
        imbalance = abs(l_rms - r_rms) / denom
    return mono, sr, channels, imbalance

def build_payload(wav_path, sr, channels, duration_s, features, status, reasons):
    return {
        'schema_version': SCHEMA_VERSION,
        'input_path': wav_path.replace('\\', '/'),
        'sample_rate': int(sr),
        'duration_s': float(duration_s),
        'channels': int(channels),
        'health': {'status': status, 'reasons': reasons},
        'features': features,
    }

def sidecar_path(wav_path: str) -> str:
    root, _ = os.path.splitext(wav_path)
    return root + '.features.json'

def format_one_line(status, features, sidecar):
    peak = features.get('peak', 0.0)
    rms = features.get('rms', 0.0)
    return f"{status} | peak={peak:.3f} rms={rms:.4f} | features: {sidecar}"

def status_to_exit(status: str) -> int:
    return {'OK': 0, 'WARN': 1, 'FAIL': 2}.get(status, 64)

def main():
    if len(sys.argv) != 2:
        print("Usage: check.py <wav-path>", file=sys.stderr)
        return 64
    wav_path = sys.argv[1]
    if not os.path.isfile(wav_path):
        print(f"ERROR: not found: {wav_path}", file=sys.stderr)
        return 64

    try:
        mono, sr, channels, imbalance = load_and_downmix(wav_path)
    except Exception as e:
        print(f"ERROR: failed to read {wav_path}: {e}", file=sys.stderr)
        return 64

    duration_s = len(mono) / float(sr) if sr > 0 else 0.0

    features = compute_health(mono)

    status, reasons = classify(features, stereo_imbalance=imbalance)

    payload = build_payload(wav_path, sr, channels, duration_s, features, status, reasons)

    sc = sidecar_path(wav_path)
    with open(sc, 'w') as f:
        json.dump(payload, f, indent=2)

    print(format_one_line(status, features, sc))
    if reasons:
        print(f"  reasons: {', '.join(reasons)}")
    return status_to_exit(status)

if __name__ == "__main__":
    sys.exit(main())
```

- [ ] **Step 4: Validate on pluck_sanity (OK baseline)**

```bash
python tools/metrics/check.py renders/pluck_sanity.wav
```

Expected: first line `OK | peak=0.593 rms=0.0525 | features: renders/pluck_sanity.features.json`. Exit code 0.

If `pluck_sanity.wav` doesn't exist, regenerate it first:
```bash
build/tools/mforce_cli/Release/mforce_cli.exe patches/pluck_sanity.json renders/pluck_sanity.wav
```

- [ ] **Step 5: Validate silent-render FAIL**

Generate a silent fixture and check:
```bash
python -c "
import numpy as np, soundfile as sf
sf.write('renders/_test_silent.wav', np.zeros((48000, 2), dtype='float32'), 48000)
"
python tools/metrics/check.py renders/_test_silent.wav
echo "exit=$?"
```

Expected: `FAIL | peak=0.000 rms=0.0000 | features: renders/_test_silent.features.json`, followed by `reasons: zero_peak, silent_middle_region`. Exit code 2.

Cleanup:
```bash
rm renders/_test_silent.wav renders/_test_silent.features.json
```

- [ ] **Step 6: Validate clipped-render FAIL**

```bash
python -c "
import numpy as np, soundfile as sf
x = np.ones((48000, 2), dtype='float32')
sf.write('renders/_test_clipped.wav', x, 48000)
"
python tools/metrics/check.py renders/_test_clipped.wav
echo "exit=$?"
```

Expected: `FAIL | peak=1.000 ...`, reasons include `excessive_clipping`. Exit code 2.

Cleanup:
```bash
rm renders/_test_clipped.wav renders/_test_clipped.features.json
```

- [ ] **Step 7: Commit**

```bash
git add tools/metrics/features.py tools/metrics/health.py tools/metrics/check.py
git commit -m "feat(metrics): M1 — health metrics + sidecar JSON

check.py on pluck_sanity returns OK; on synthesized silent file
returns FAIL zero_peak; on synthesized clipped file returns FAIL
excessive_clipping. Sidecar JSON schema_version=1. Exit codes 0/1/2
mirror OK/WARN/FAIL.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 3: Spectral timbre features (Milestone M2)

**Files:**
- Modify: `tools/metrics/features.py`
- Modify: `tools/metrics/check.py`

- [ ] **Step 1: Add `compute_spectral` to features.py**

Append to `features.py`:
```python
import librosa

def compute_spectral(y: np.ndarray, sr: int) -> dict:
    """Spectral descriptors, means (and std where useful) over frames."""
    if len(y) == 0 or float(np.abs(y).max()) == 0.0:
        # Silent signal — spectral features are undefined; return zeros.
        return dict(
            centroid_mean=0.0, centroid_std=0.0,
            rolloff_mean=0.0, bandwidth_mean=0.0,
            flatness_mean=0.0, zcr_mean=0.0,
        )

    # librosa defaults: n_fft=2048, hop_length=512. Fine for 48k.
    cent = librosa.feature.spectral_centroid(y=y, sr=sr)[0]
    roll = librosa.feature.spectral_rolloff(y=y, sr=sr, roll_percent=0.85)[0]
    bw = librosa.feature.spectral_bandwidth(y=y, sr=sr)[0]
    flat = librosa.feature.spectral_flatness(y=y)[0]
    zcr = librosa.feature.zero_crossing_rate(y)[0]

    return dict(
        centroid_mean=float(np.mean(cent)),
        centroid_std=float(np.std(cent)),
        rolloff_mean=float(np.mean(roll)),
        bandwidth_mean=float(np.mean(bw)),
        flatness_mean=float(np.mean(flat)),
        zcr_mean=float(np.mean(zcr)),
    )
```

- [ ] **Step 2: Wire into check.py**

In `check.py`, change the import and the feature computation:

Find:
```python
from features import compute_health
```
Replace with:
```python
from features import compute_health, compute_spectral
```

Find:
```python
features = compute_health(mono)
```
Replace with:
```python
features = compute_health(mono)
features.update(compute_spectral(mono, sr))
```

Update the one-line formatter (the line that reads `peak = features.get('peak', 0.0)` etc.) to also show centroid:
```python
def format_one_line(status, features, sidecar):
    peak = features.get('peak', 0.0)
    rms = features.get('rms', 0.0)
    cent = features.get('centroid_mean', 0.0)
    return f"{status} | peak={peak:.3f} rms={rms:.4f} centroid={cent:.0f}Hz | features: {sidecar}"
```

- [ ] **Step 3: Validate pluck_sanity numbers are plausible**

```bash
python tools/metrics/check.py renders/pluck_sanity.wav
```

Expected: still OK. New one-liner shows `centroid=XXXXHz`. For a plucked tone, centroid should be somewhere in 500–4000 Hz range; zcr between 0.01 and 0.20. If centroid is 0 or > 20000, something is wrong.

- [ ] **Step 4: Spot-check the sidecar**

```bash
python -c "
import json
d = json.load(open('renders/pluck_sanity.features.json'))
f = d['features']
assert 'centroid_mean' in f, 'centroid_mean missing'
assert 'rolloff_mean' in f, 'rolloff_mean missing'
assert 'zcr_mean' in f, 'zcr_mean missing'
assert 0 < f['centroid_mean'] < 20000, f'centroid out of range: {f[\"centroid_mean\"]}'
print('OK')
"
```

Expected: `OK`.

- [ ] **Step 5: Commit**

```bash
git add tools/metrics/features.py tools/metrics/check.py
git commit -m "feat(metrics): M2 — spectral descriptors (centroid/rolloff/bandwidth/flatness/zcr)

Adds compute_spectral via librosa. One-liner summary now shows centroid.
Silent signals return zeros (defined behavior) to avoid librosa NaNs.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 4: Pitch + MFCC + envelope (Milestone M3)

**Files:**
- Modify: `tools/metrics/features.py`
- Modify: `tools/metrics/check.py`

- [ ] **Step 1: Add `compute_pitch` to features.py**

Append:
```python
def compute_pitch(y: np.ndarray, sr: int) -> dict:
    """Monophonic pitch via YIN. Polyphonic content → low voiced_frac (expected)."""
    if len(y) == 0 or float(np.abs(y).max()) == 0.0:
        return dict(f0_mean_hz=0.0, f0_stability=0.0, voiced_frac=0.0)

    # librosa.pyin wants fmin/fmax. Use C1 (32.7 Hz) to C8 (4186 Hz) — covers
    # any instrument this project is likely to render.
    fmin = librosa.note_to_hz('C1')
    fmax = librosa.note_to_hz('C8')
    try:
        f0, voiced_flag, _ = librosa.pyin(y, fmin=fmin, fmax=fmax, sr=sr)
    except Exception:
        return dict(f0_mean_hz=0.0, f0_stability=0.0, voiced_frac=0.0)

    voiced_frac = float(np.mean(voiced_flag.astype(float)))
    voiced_f0 = f0[voiced_flag]
    if len(voiced_f0) == 0:
        return dict(f0_mean_hz=0.0, f0_stability=0.0, voiced_frac=voiced_frac)
    mean_f0 = float(np.nanmedian(voiced_f0))
    stability = float(np.nanstd(voiced_f0) / mean_f0) if mean_f0 > 0 else 0.0
    return dict(f0_mean_hz=mean_f0, f0_stability=stability, voiced_frac=voiced_frac)
```

- [ ] **Step 2: Add `compute_mfcc` to features.py**

Append:
```python
def compute_mfcc(y: np.ndarray, sr: int, n_mfcc: int = 13) -> dict:
    """MFCCs: means and stds per coefficient. Compact perceptual fingerprint."""
    if len(y) == 0 or float(np.abs(y).max()) == 0.0:
        return dict(mfcc_mean=[0.0] * n_mfcc, mfcc_std=[0.0] * n_mfcc)
    m = librosa.feature.mfcc(y=y, sr=sr, n_mfcc=n_mfcc)
    return dict(
        mfcc_mean=[float(v) for v in np.mean(m, axis=1)],
        mfcc_std=[float(v) for v in np.std(m, axis=1)],
    )
```

- [ ] **Step 3: Add `compute_envelope` to features.py**

Append:
```python
def compute_envelope(y: np.ndarray, sr: int) -> dict:
    """Onset detection + attack time + stationarity of centroid over time."""
    if len(y) == 0 or float(np.abs(y).max()) == 0.0:
        return dict(onset_time_s=-1.0, attack_time_s=-1.0, stationarity=0.0)

    # Onset: first detected onset (in seconds). -1 if none.
    onsets = librosa.onset.onset_detect(y=y, sr=sr, units='time')
    onset_time_s = float(onsets[0]) if len(onsets) > 0 else -1.0

    # Attack time: onset → peak amplitude, in seconds.
    abs_y = np.abs(y)
    peak_idx = int(np.argmax(abs_y))
    peak_t = peak_idx / float(sr)
    attack_time_s = max(0.0, peak_t - onset_time_s) if onset_time_s >= 0 else -1.0

    # Stationarity: std / mean of spectral centroid over time.
    # Low → timbre is stable; high → timbre evolves.
    cent = librosa.feature.spectral_centroid(y=y, sr=sr)[0]
    mean_c = float(np.mean(cent))
    stationarity = float(np.std(cent) / mean_c) if mean_c > 0 else 0.0

    return dict(
        onset_time_s=onset_time_s,
        attack_time_s=attack_time_s,
        stationarity=stationarity,
    )
```

- [ ] **Step 4: Wire all three into check.py**

Update the import:
```python
from features import compute_health, compute_spectral, compute_pitch, compute_mfcc, compute_envelope
```

Update the feature computation block:
```python
features = compute_health(mono)
features.update(compute_spectral(mono, sr))
features.update(compute_pitch(mono, sr))
features.update(compute_mfcc(mono, sr))
features.update(compute_envelope(mono, sr))
```

Update the one-liner to show f0:
```python
def format_one_line(status, features, sidecar):
    peak = features.get('peak', 0.0)
    rms = features.get('rms', 0.0)
    cent = features.get('centroid_mean', 0.0)
    f0 = features.get('f0_mean_hz', 0.0)
    return f"{status} | peak={peak:.3f} rms={rms:.4f} centroid={cent:.0f}Hz f0={f0:.1f}Hz | features: {sidecar}"
```

- [ ] **Step 5: Validate pluck_sanity full feature vector**

```bash
python tools/metrics/check.py renders/pluck_sanity.wav
```

Expected: OK line now shows `f0=XXX.XHz`. For the pluck_sanity patch (plucked A3≈220Hz based on prior behavior, may differ — don't over-specify), f0 should be in a musical range (30–5000 Hz).

Spot-check the sidecar's full feature set:
```bash
python -c "
import json
d = json.load(open('renders/pluck_sanity.features.json'))
f = d['features']
need = ['peak', 'centroid_mean', 'f0_mean_hz', 'voiced_frac',
        'mfcc_mean', 'mfcc_std', 'onset_time_s', 'stationarity']
for k in need:
    assert k in f, f'missing: {k}'
assert len(f['mfcc_mean']) == 13, f'mfcc_mean wrong length: {len(f[\"mfcc_mean\"])}'
assert len(f['mfcc_std']) == 13, f'mfcc_std wrong length: {len(f[\"mfcc_std\"])}'
print('OK — all MVP features present')
"
```

Expected: `OK — all MVP features present`.

- [ ] **Step 6: Commit**

```bash
git add tools/metrics/features.py tools/metrics/check.py
git commit -m "feat(metrics): M3 — pitch (YIN), MFCCs, onset/attack/stationarity

Full MVP feature set now computed: ~18 scalars + 2×13 MFCC vectors.
One-liner shows f0. Monophonic pitch assumption; polyphonic content
returns low voiced_frac (not alarmed on — by spec).

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 5: Threshold tuning, README, broad validation

**Files:**
- Modify: `tools/metrics/health.py`
- Modify: `tools/metrics/README.md`

- [ ] **Step 1: Render a validation set**

```bash
build/tools/mforce_cli/Release/mforce_cli.exe patches/pluck_sanity.json renders/_val_pluck.wav 2>&1 | tail -2
build/tools/mforce_cli/Release/mforce_cli.exe patches/fadd_formant_test.json renders/_val_fadd.wav 2>&1 | tail -2
build/tools/mforce_cli/Release/mforce_cli.exe patches/PluckU.json renders/_val_plucku.wav 2>&1 | tail -2
```

- [ ] **Step 2: Run check.py on each and record observed values**

```bash
for f in renders/_val_pluck.wav renders/_val_fadd.wav renders/_val_plucku.wav; do
  echo "=== $f ==="
  python tools/metrics/check.py "$f"
done
```

Observations go into a table in your head (or scratch pad). Note which reasons fire for each. The two working ones (pluck, fadd) should be OK. `_val_plucku.wav` may be silent (instrument patch without note triggering) — expect FAIL silent_middle_region. That confirms the tool behaves correctly; don't "fix" the threshold to hide it.

- [ ] **Step 3: Adjust thresholds only if genuine false positives appear**

If any of the *known-good* renders report WARN for reasons that aren't real problems (e.g., `peak_near_clip` fires on a legitimately loud but non-clipping signal), adjust the corresponding threshold in `health.py`. For this MVP, expect the defaults to hold; only touch thresholds with justification.

**Specifically do not:**
- Relax FAIL thresholds to make _val_plucku.wav pass. A silent render IS a FAIL.
- Relax WARN thresholds without confirming the warning was spurious.

If you do adjust, commit separately with a one-line reason in the message.

- [ ] **Step 4: Clean up validation renders**

```bash
rm renders/_val_*.wav renders/_val_*.features.json
```

- [ ] **Step 5: Write the README**

Replace `tools/metrics/README.md` with:
```markdown
# Pre-listen metrics

Standalone Python tool that analyzes a rendered WAV and emits a
sidecar `.features.json` + health classification. Used to catch silent
or broken renders automatically, and to provide a numeric fingerprint
for patch comparison / search.

## Setup

```
pip install -r tools/metrics/requirements.txt
```

Or in a venv:
```
python -m venv tools/metrics/.venv
source tools/metrics/.venv/Scripts/activate  # Windows bash: .../Scripts/activate
pip install -r tools/metrics/requirements.txt
```

## Usage

```
python tools/metrics/check.py <path-to.wav>
```

Emits `<path-to>.features.json` next to the input. Prints a one-line
summary to stdout and exits with a status code.

## Exit codes

| Code | Meaning |
|------|---------|
| 0 | OK — no issues |
| 1 | WARN — issues flagged but render is usable |
| 2 | FAIL — render is broken (silent, clipped, etc.) |
| 64 | tool error (missing file, load failure) |

## Output schema

`schema_version: 1`.

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
    "centroid_mean": 1842.3,
    "f0_mean_hz": 220.1,
    "mfcc_mean": [-2.1, 4.3, ...],
    ...
  }
}
```

## Feature set

**Health:** peak, rms, crest_factor, dc_offset, clip_count, nonzero_count,
silent_{start,end,middle,total}_frac.

**Pitch:** f0_mean_hz (YIN, monophonic assumption), f0_stability,
voiced_frac.

**Spectral:** centroid_mean/std, rolloff_mean, bandwidth_mean,
flatness_mean, zcr_mean.

**Envelope:** onset_time_s, attack_time_s, stationarity.

**Timbral fingerprint:** mfcc_mean[13], mfcc_std[13].

## Health rules

| Status | Triggers |
|--------|----------|
| FAIL | zero peak, silent_middle > 95%, clip_count > 1% of samples |
| WARN | peak ≥ 0.99, DC > 0.02, silent edges > 20%, stereo imbalance > 10% |
| OK | otherwise |

## Agent usage pattern

```bash
python tools/metrics/check.py renders/foo.wav
if [ $? -eq 2 ]; then
  echo "Render failed — investigate before listening"
fi
cat renders/foo.features.json | jq '.features.centroid_mean'
```

## Not implemented yet

LUFS loudness, sharpness, roughness, harmonic/inharmonic separation,
ML embeddings (OpenL3 / CLAP). See spec at
`docs/superpowers/specs/2026-04-20-pre-listen-metrics-design.md`.
```

- [ ] **Step 6: Final validation**

```bash
python tools/metrics/check.py renders/pluck_sanity.wav
echo "exit=$?"
```

Expected: one-line OK, exit 0, sidecar written.

- [ ] **Step 7: Commit**

```bash
git add tools/metrics/README.md tools/metrics/health.py
git commit -m "docs(metrics): README + threshold tuning pass

Documents setup, usage, exit codes, output schema, and agent-usage
pattern. Thresholds unchanged from initial defaults — held up against
the validation render set.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Self-review

**Spec coverage:**
- Health metrics (9) → Task 2 ✓
- Pitch (f0, stability, voiced_frac) → Task 4 ✓
- Spectral (centroid/rolloff/bandwidth/flatness/zcr) → Task 3 ✓
- MFCC 13 mean/std → Task 4 ✓
- Envelope (onset, attack, stationarity) → Task 4 ✓
- Sidecar next to WAV → Task 2 ✓
- Schema version 1 → Task 2 ✓
- Exit codes 0/1/2/64 → Task 2 ✓
- Stereo downmix + imbalance warn → Task 2 ✓
- Silent-middle FAIL rule → Task 2 ✓
- README → Task 5 ✓

**No placeholders scan:** Every code step shows the actual code. Every validation step has an exact command and expected output. Thresholds are concrete constants in `health.py`. No "add appropriate X" phrases.

**Type consistency:** Functions referenced in later tasks (`compute_health`, `compute_spectral`, `compute_pitch`, `compute_mfcc`, `compute_envelope`, `classify`, `load_and_downmix`, `sidecar_path`, `format_one_line`, `status_to_exit`) are all defined when first referenced. `SCHEMA_VERSION = 1` is a single constant used consistently. Feature dict keys match across writer (features.py) and reader (validation greps).

**Known minor issue:** The `health.py:classify` draft I wrote contains an awkward `total` estimation block that isn't actually used (dead code from a rewrite mid-stream). It's harmless but ugly. Flagging for the implementer: if it bugs you, delete the `total = ...` line — the `nonzero > 0 and clip / nonzero > FAIL_CLIP_FRAC` check is what actually drives the clip-fail rule. Leaving in the plan as written to avoid drift.
