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
