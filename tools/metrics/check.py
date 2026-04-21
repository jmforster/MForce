#!/usr/bin/env python3
"""Pre-listen metrics — render health + timbral fingerprint."""

import json
import os
import sys

# Allow invocation from any CWD by adding this script's dir to sys.path
# before the sibling-module imports below.
_THIS_DIR = os.path.dirname(os.path.abspath(__file__))
if _THIS_DIR not in sys.path:
    sys.path.insert(0, _THIS_DIR)

import numpy as np
import soundfile as sf

from features import compute_health, compute_spectral, compute_pitch, compute_mfcc, compute_envelope
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
    cent = features.get('centroid_mean', 0.0)
    f0 = features.get('f0_mean_hz', 0.0)
    return f"{status} | peak={peak:.3f} rms={rms:.4f} centroid={cent:.0f}Hz f0={f0:.1f}Hz | features: {sidecar}"


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
    features.update(compute_spectral(mono, sr))
    features.update(compute_pitch(mono, sr))
    features.update(compute_mfcc(mono, sr))
    features.update(compute_envelope(mono, sr))

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
