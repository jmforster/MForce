"""Timbre-feature embedding for the novelty metric (dsp BACKLOG item 4).

Turns a rendered WAV into a fixed-length vector that captures its timbral
character, so two sounds can be compared by embedding distance. Deliberately
numpy/scipy-only (matches research/ml_ears — no librosa dependency) so the
metric is reproducible across environments.

The vector concatenates frame-wise features summarised by mean + std:
  - MFCC (mel filterbank -> log -> DCT-II), N_MFCC coefficients
  - spectral centroid, flatness, rolloff(0.85), flux, zero-crossing rate
Total = N_MFCC*2 + 5*2 dims. Distances are taken in a library-z-scored space
(see novelty.py) so no single high-variance dim dominates.
"""
import numpy as np

SR_REF = 48000          # embeddings assume this SR; resampling is caller's job
N_FFT = 2048
HOP = 512
N_MELS = 40
N_MFCC = 13
FMIN = 40.0
FMAX = 16000.0
ROLLOFF_PCT = 0.85

FEATURE_NAMES = (
    [f"mfcc{i}_mean" for i in range(N_MFCC)]
    + [f"mfcc{i}_std" for i in range(N_MFCC)]
    + ["centroid_mean", "centroid_std",
       "flatness_mean", "flatness_std",
       "rolloff_mean", "rolloff_std",
       "flux_mean", "flux_std",
       "zcr_mean", "zcr_std"]
)
EMBED_DIM = len(FEATURE_NAMES)


def _hz_to_mel(f):
    return 2595.0 * np.log10(1.0 + f / 700.0)


def _mel_to_hz(m):
    return 700.0 * (10.0 ** (m / 2595.0) - 1.0)


def _mel_filterbank(sr, n_fft, n_mels, fmin, fmax):
    """Triangular mel filterbank, shape (n_mels, n_fft//2+1)."""
    n_bins = n_fft // 2 + 1
    fft_freqs = np.linspace(0, sr / 2, n_bins)
    mel_pts = np.linspace(_hz_to_mel(fmin), _hz_to_mel(min(fmax, sr / 2)), n_mels + 2)
    hz_pts = _mel_to_hz(mel_pts)
    fb = np.zeros((n_mels, n_bins))
    for m in range(1, n_mels + 1):
        lo, ctr, hi = hz_pts[m - 1], hz_pts[m], hz_pts[m + 1]
        up = (fft_freqs - lo) / max(ctr - lo, 1e-9)
        dn = (hi - fft_freqs) / max(hi - ctr, 1e-9)
        fb[m - 1] = np.clip(np.minimum(up, dn), 0.0, None)
    return fb


def _dct2(x, n_out):
    """DCT-II along last axis, keep first n_out coefficients (orthonormal)."""
    n = x.shape[-1]
    k = np.arange(n_out)[:, None]
    j = np.arange(n)[None, :]
    basis = np.cos(np.pi * (2 * j + 1) * k / (2 * n))
    # orthonormal scaling
    scale = np.sqrt(2.0 / n) * np.ones(n_out)
    scale[0] = np.sqrt(1.0 / n)
    return (x @ basis.T) * scale


_MEL_FB = _mel_filterbank(SR_REF, N_FFT, N_MELS, FMIN, FMAX)


def _frames(x, n_fft, hop):
    if len(x) < n_fft:
        x = np.pad(x, (0, n_fft - len(x)))
    n = 1 + (len(x) - n_fft) // hop
    idx = np.arange(n_fft)[None, :] + hop * np.arange(n)[:, None]
    return x[idx] * np.hanning(n_fft)[None, :]


def embed(x, sr=SR_REF):
    """WAV samples -> EMBED_DIM feature vector. x may be mono or stereo."""
    x = np.asarray(x, dtype=np.float64)
    if x.ndim > 1:
        x = x.mean(axis=1)
    if sr != SR_REF:
        # linear resample to the reference rate the filterbank was built for
        n_new = int(round(len(x) * SR_REF / sr))
        x = np.interp(np.linspace(0, len(x) - 1, n_new),
                      np.arange(len(x)), x)
    # normalise level so novelty is about timbre, not loudness
    peak = np.max(np.abs(x)) + 1e-12
    x = x / peak

    fr = _frames(x, N_FFT, HOP)
    mag = np.abs(np.fft.rfft(fr, N_FFT, axis=1))          # (frames, bins)
    power = mag ** 2
    freqs = np.fft.rfftfreq(N_FFT, 1.0 / SR_REF)

    # MFCC
    mel = power @ _MEL_FB.T                                # (frames, n_mels)
    logmel = np.log(mel + 1e-9)
    mfcc = _dct2(logmel, N_MFCC)                           # (frames, n_mfcc)

    # spectral shape features (per frame)
    e = mag.sum(axis=1) + 1e-12
    centroid = (mag * freqs[None, :]).sum(axis=1) / e
    gmean = np.exp(np.mean(np.log(mag + 1e-9), axis=1))
    amean = mag.mean(axis=1) + 1e-12
    flatness = gmean / amean
    csum = np.cumsum(mag, axis=1)
    thresh = ROLLOFF_PCT * csum[:, -1:]
    rolloff = freqs[np.argmax(csum >= thresh, axis=1)]
    dmag = np.diff(mag, axis=0, prepend=mag[:1])
    flux = np.sqrt((np.maximum(dmag, 0.0) ** 2).sum(axis=1))
    zc = np.mean(np.abs(np.diff(np.sign(fr), axis=1)) > 0, axis=1)

    def ms(v):
        return [float(np.mean(v)), float(np.std(v))]

    vec = np.concatenate([
        mfcc.mean(axis=0), mfcc.std(axis=0),
        ms(centroid) + ms(flatness) + ms(rolloff) + ms(flux) + ms(zc),
    ])
    return vec.astype(np.float64)


def embed_file(path):
    import soundfile as sf
    x, sr = sf.read(path)
    return embed(x, sr)
