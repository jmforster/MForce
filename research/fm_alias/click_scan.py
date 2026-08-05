"""Click scanner for fm_phase / fm_clicks renders.

A 'click' here is a one-sample phase-jump spike: the waveform leaves the
smooth trajectory for ~1 sample and returns. Detected via second difference
(spike detector) with a threshold relative to the local smooth signal.

Usage: python click_scan.py file.wav [file2.wav ...]
Prints per-file: click count, rate, spacing stats, amplitude stats, times.
"""
import sys
import wave
import numpy as np


def load_wav(path):
    with wave.open(path, "rb") as w:
        sr = w.getframerate()
        n = w.getnframes()
        ch = w.getnchannels()
        sw = w.getsampwidth()
        raw = w.readframes(n)
    if sw == 2:
        x = np.frombuffer(raw, dtype=np.int16).astype(np.float64) / 32768.0
    elif sw == 4:
        x = np.frombuffer(raw, dtype=np.int32).astype(np.float64) / 2147483648.0
    elif sw == 3:
        b = np.frombuffer(raw, dtype=np.uint8).reshape(-1, 3)
        x = ((b[:, 0].astype(np.int32)) | (b[:, 1].astype(np.int32) << 8)
             | (b[:, 2].astype(np.int32) << 16))
        x = np.where(x >= 1 << 23, x - (1 << 24), x).astype(np.float64) / (1 << 23)
    else:
        raise ValueError(f"unsupported sampwidth {sw}")
    if ch > 1:
        x = x.reshape(-1, ch).mean(axis=1)
    return x, sr


def find_clicks(x, sr, thresh_ratio=4.0, min_abs=0.02, merge_gap=8):
    """Detect spikes via second difference outliers.

    Returns list of (sample_index, click_amplitude) where click_amplitude is
    the peak |d2| in the merged cluster (proportional to the discontinuity).
    """
    d2 = np.abs(np.diff(x, 2))
    # robust scale of the smooth signal's own second difference
    med = np.median(d2)
    mad = np.median(np.abs(d2 - med)) + 1e-12
    thresh = max(med + thresh_ratio * 6.0 * mad, min_abs)
    idx = np.where(d2 > thresh)[0]
    clicks = []
    if len(idx) == 0:
        return clicks, thresh
    # merge neighbouring detections into single click events
    start = idx[0]
    prev = idx[0]
    for i in idx[1:]:
        if i - prev > merge_gap:
            seg = d2[start:prev + 1]
            clicks.append((start + int(np.argmax(seg)) + 1, float(seg.max())))
            start = i
        prev = i
    seg = d2[start:prev + 1]
    clicks.append((start + int(np.argmax(seg)) + 1, float(seg.max())))
    return clicks, thresh


def report(path, show_times=False):
    x, sr = load_wav(path)
    dur = len(x) / sr
    clicks, thresh = find_clicks(x, sr)
    print(f"\n=== {path}")
    print(f"  sr={sr} dur={dur:.3f}s peak={np.max(np.abs(x)):.3f} thresh={thresh:.4f}")
    print(f"  clicks: {len(clicks)}  rate={len(clicks)/dur:.2f}/s")
    if clicks:
        t = np.array([c[0] for c in clicks]) / sr
        a = np.array([c[1] for c in clicks])
        gaps = np.diff(t)
        print(f"  amp(|d2|): min={a.min():.3f} med={np.median(a):.3f} max={a.max():.3f}")
        if len(gaps):
            print(f"  spacing: min={gaps.min()*1000:.1f}ms med={np.median(gaps)*1000:.1f}ms "
                  f"max={gaps.max()*1000:.1f}ms mean={gaps.mean()*1000:.1f}ms")
        # rate over time (1s bins)
        bins = np.arange(0, dur + 1, 1.0)
        hist, _ = np.histogram(t, bins=bins)
        print(f"  per-second counts: {list(hist)}")
        if show_times:
            for ti, ai in clicks[:80]:
                print(f"    t={ti/sr:.4f}s  amp={ai:.3f}")
    return clicks, sr, dur


if __name__ == "__main__":
    show = "--times" in sys.argv
    for p in [a for a in sys.argv[1:] if not a.startswith("--")]:
        report(p, show_times=show)
