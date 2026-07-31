"""Spectral characterisation of the FM matrix renders (dsp item 7 stage 2).

The novelty rank says "far from the library"; it does not say WHY, and it
cannot catch a patch whose modulator silently failed to wire (a wiring
fallback would still render a perfectly good FM tone). This measures, per
render:

  centroid    mean spectral centroid, Hz — where the energy sits
  cent sd     standard deviation of the centroid over time; near-zero means
              NOTHING IS MOVING, which for a "modulate everything" patch is a
              wiring failure, not a result
  bw          mean spectral bandwidth, Hz — sideband spread ~ modulation index
  flat        spectral flatness, 0 = pure tone .. 1 = noise
  hi          fraction of energy above 5 kHz (aliasing / brightness proxy)

Usage: python tools/fm_matrix_spectra.py [render_dir]
"""
import glob
import os
import sys
import wave

import numpy as np

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
DEFAULT = os.path.join(REPO, "renders", "fm_matrix")


def read_mono(path):
    with wave.open(path, "rb") as w:
        sr, n, ch = w.getframerate(), w.getnframes(), w.getnchannels()
        raw = w.readframes(n)
    x = np.frombuffer(raw, dtype="<i2").astype(np.float64) / 32768.0
    if ch == 2:
        x = x.reshape(-1, 2).mean(axis=1)
    return x, sr


def frames(x, sr, win=2048, hop=1024):
    w = np.hanning(win)
    for i in range(0, len(x) - win, hop):
        seg = x[i:i + win]
        if np.abs(seg).max() < 1e-5:
            continue
        yield np.abs(np.fft.rfft(seg * w))


def main():
    d = sys.argv[1] if len(sys.argv) > 1 else DEFAULT
    wavs = sorted(glob.glob(os.path.join(d, "*.wav")))
    print(f"{'case':34s} {'centroid':>9} {'cent sd':>8} {'bw':>8} "
          f"{'flat':>6} {'hi>5k':>7}")
    print("-" * 78)
    for p in wavs:
        x, sr = read_mono(p)
        freqs = np.fft.rfftfreq(2048, 1.0 / sr)
        cents, bws, flats, his = [], [], [], []
        for mag in frames(x, sr):
            e = mag + 1e-12
            tot = e.sum()
            c = float((freqs * e).sum() / tot)
            cents.append(c)
            bws.append(float(np.sqrt(((freqs - c) ** 2 * e).sum() / tot)))
            flats.append(float(np.exp(np.mean(np.log(e))) / np.mean(e)))
            his.append(float(e[freqs > 5000].sum() / tot))
        if not cents:
            print(f"{os.path.basename(p)[:-4]:34s}   (silent)")
            continue
        print(f"{os.path.basename(p)[:-4]:34s} {np.mean(cents):9.1f} "
              f"{np.std(cents):8.1f} {np.mean(bws):8.1f} "
              f"{np.mean(flats):6.4f} {np.mean(his):7.3f}")


if __name__ == "__main__":
    main()
