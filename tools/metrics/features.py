"""Feature extraction functions. See check.py for orchestration."""

import numpy as np
import librosa

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

    # Edge fractions (first 10%, last 10%) — context only, drive WARN not FAIL.
    edge = max(1, n // 10)
    silent_start_frac = float(np.mean(silent_mask[:edge]))
    silent_end_frac = float(np.mean(silent_mask[-edge:]))

    # Middle region: skip first+last 10%. Drives the silent-region FAIL rule —
    # attack ramps and decay tails are legitimate quiet.
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


def compute_pitch(y: np.ndarray, sr: int) -> dict:
    """Monophonic pitch via YIN. Polyphonic content → low voiced_frac (expected)."""
    if len(y) == 0 or float(np.abs(y).max()) == 0.0:
        return dict(f0_mean_hz=0.0, f0_stability=0.0, voiced_frac=0.0)

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


def compute_mfcc(y: np.ndarray, sr: int, n_mfcc: int = 13) -> dict:
    """MFCCs: means and stds per coefficient. Compact perceptual fingerprint."""
    if len(y) == 0 or float(np.abs(y).max()) == 0.0:
        return dict(mfcc_mean=[0.0] * n_mfcc, mfcc_std=[0.0] * n_mfcc)
    m = librosa.feature.mfcc(y=y, sr=sr, n_mfcc=n_mfcc)
    return dict(
        mfcc_mean=[float(v) for v in np.mean(m, axis=1)],
        mfcc_std=[float(v) for v in np.std(m, axis=1)],
    )


def compute_envelope(y: np.ndarray, sr: int) -> dict:
    """Onset detection + attack time + stationarity of centroid over time."""
    if len(y) == 0 or float(np.abs(y).max()) == 0.0:
        return dict(onset_time_s=-1.0, attack_time_s=-1.0, stationarity=0.0)

    onsets = librosa.onset.onset_detect(y=y, sr=sr, units='time')
    onset_time_s = float(onsets[0]) if len(onsets) > 0 else -1.0

    abs_y = np.abs(y)
    peak_idx = int(np.argmax(abs_y))
    peak_t = peak_idx / float(sr)
    attack_time_s = max(0.0, peak_t - onset_time_s) if onset_time_s >= 0 else -1.0

    cent = librosa.feature.spectral_centroid(y=y, sr=sr)[0]
    mean_c = float(np.mean(cent))
    stationarity = float(np.std(cent) / mean_c) if mean_c > 0 else 0.0

    return dict(
        onset_time_s=onset_time_s,
        attack_time_s=attack_time_s,
        stationarity=stationarity,
    )
