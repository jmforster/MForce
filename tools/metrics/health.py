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

    # WARN rules
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
