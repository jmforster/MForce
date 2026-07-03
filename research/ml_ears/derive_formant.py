"""
derive_formant.py — extract the pitch-invariant viola body formant from a set of
isolated notes spanning multiple pitches/strings (Iowa MIS viola, arco ff).

Idea: a body resonance sits at a FIXED frequency (Hz). As pitch changes, the
harmonics slide through it; harmonics that land on a resonance get boosted. So if
we pool the harmonic amplitudes of MANY notes onto an absolute-Hz axis (after
normalizing each note's level), the consistent peaks that emerge are the body
formant — independent of any single note's fundamental. That pooled envelope is
the instrument's approximate spectral transfer function.

Outputs (research/ml_ears/out/):
  - per_note.json        : f0, string, harmonic amplitudes per file
  - formant_envelope.json: pooled resonance envelope (log-freq) + detected peaks
  - formant.png          : scatter of all harmonics + pooled envelope + peaks +
                           literature viola-mode priors
"""
import glob, os, re, json
import numpy as np
import soundfile as sf

HERE = os.path.dirname(os.path.abspath(__file__))
SAMPLE_DIR = os.path.join(HERE, "..", "inst_samples", "viola")
OUT_DIR = os.path.join(HERE, "out")
os.makedirs(OUT_DIR, exist_ok=True)

# --- analysis params ---
SUS_START_S = 0.6     # skip attack
SUS_LEN_S   = 2.0     # sustain window length
HARM_TOL    = 0.03    # +/- fraction of f0 to search around each k*f0 (vibrato slack)
FREQ_MAX    = 12000.0 # ignore harmonics above this
N_BINS      = 64      # log-freq bins for the pooled envelope
FREQ_LO     = 160.0   # envelope range
FREQ_HI     = 11000.0

# Literature viola signature-mode priors (Hz) for sanity overlay only.
LIT_MODES = {"A0": 220, "B1-": 440, "B1+": 510, "C-bout": 660,
             "mid": 1200, "bridge-hill": 2300, "super-bridge": 3500}

NOTE_SEMI = {"C": 0, "D": 2, "E": 4, "F": 5, "G": 7, "A": 9, "B": 11}

def note_to_freq(name):
    m = re.match(r"^([A-G])(b|#)?(\d)$", name)
    if not m:
        return None
    letter, acc, octv = m.group(1), m.group(2), int(m.group(3))
    semi = NOTE_SEMI[letter] + (1 if acc == "#" else -1 if acc == "b" else 0)
    midi = 12 * (octv + 1) + semi      # MIDI: C-1 = 0, so C4 = 60, A4 = 69
    return 440.0 * 2 ** ((midi - 69) / 12.0)

def parse_name(fn):
    m = re.search(r"sul([ADGC])\.([A-G][b#]?\d)\.", fn)
    if not m:
        return None, None
    return m.group(1), m.group(2)

def harmonic_amps(x, sr, f0):
    """Return (freqs_hz, amps_db) for harmonics of f0 found in the sustain."""
    n0 = int(SUS_START_S * sr)
    n1 = min(len(x), n0 + int(SUS_LEN_S * sr))
    seg = x[n0:n1]
    if len(seg) < sr // 2:
        return None
    seg = seg * np.hanning(len(seg))
    nfft = 1 << int(np.ceil(np.log2(len(seg))))
    spec = np.abs(np.fft.rfft(seg, nfft))
    fbin = np.fft.rfftfreq(nfft, 1.0 / sr)
    freqs, amps = [], []
    kmax = int(FREQ_MAX / f0)
    for k in range(1, kmax + 1):
        fc = k * f0
        lo, hi = fc * (1 - HARM_TOL), fc * (1 + HARM_TOL)
        sel = (fbin >= lo) & (fbin <= hi)
        if not np.any(sel):
            continue
        amps.append(float(np.max(spec[sel])))
        freqs.append(fc)
    if not amps:
        return None
    amps = np.array(amps)
    db = 20 * np.log10(amps / np.max(amps) + 1e-9)   # per-note normalize: peak = 0 dB
    return np.array(freqs), db

def main():
    files = sorted(glob.glob(os.path.join(SAMPLE_DIR, "*.aif")))
    per_note = []
    all_f, all_db = [], []
    skipped = []
    for f in files:
        string, note = parse_name(os.path.basename(f))
        f0 = note_to_freq(note) if note else None
        if f0 is None:
            skipped.append((os.path.basename(f), "name parse"))
            continue
        x, sr = sf.read(f)
        if x.ndim > 1:
            x = x.mean(axis=1)
        ha = harmonic_amps(x, sr, f0)
        if ha is None:
            skipped.append((os.path.basename(f), "no harmonics"))
            continue
        freqs, db = ha
        keep = freqs <= FREQ_HI
        all_f.append(freqs[keep]); all_db.append(db[keep])
        per_note.append({"file": os.path.basename(f), "string": string,
                         "note": note, "f0": round(f0, 2), "n_harm": int(keep.sum())})

    all_f = np.concatenate(all_f); all_db = np.concatenate(all_db)

    # Pool harmonics into log-freq bins -> resonance envelope (mean dB per bin).
    edges = np.logspace(np.log10(FREQ_LO), np.log10(FREQ_HI), N_BINS + 1)
    centers = np.sqrt(edges[:-1] * edges[1:])
    env = np.full(N_BINS, np.nan)
    for i in range(N_BINS):
        sel = (all_f >= edges[i]) & (all_f < edges[i + 1])
        if np.any(sel):
            env[i] = np.mean(all_db[sel])
    valid = ~np.isnan(env)
    cen_v, env_v = centers[valid], env[valid]

    # Detrend: fit the average source rolloff as a straight line in dB vs
    # log-freq, subtract it -> residual isolates the body resonance (formant).
    logf = np.log10(cen_v)
    slope, intercept = np.polyfit(logf, env_v, 1)
    trend = slope * logf + intercept
    formant = env_v - trend                 # resonance relative to source rolloff

    # Peak-pick on the DETRENDED formant (these are the body modes).
    peaks = []
    for i in range(1, len(formant) - 1):
        if formant[i] > formant[i - 1] and formant[i] > formant[i + 1] and formant[i] > 0:
            peaks.append({"freq": round(float(cen_v[i]), 1),
                          "gain_db": round(float(formant[i]), 2)})
    peaks.sort(key=lambda p: -p["gain_db"])
    print(f"source rolloff fit: {slope:.1f} dB/decade ({slope*np.log10(2):.1f} dB/oct)")

    json.dump(per_note, open(os.path.join(OUT_DIR, "per_note.json"), "w"), indent=2)
    json.dump({"freq_hz": [round(float(c), 1) for c in cen_v],
               "pooled_db": [round(float(e), 2) for e in env_v],
               "formant_db": [round(float(e), 2) for e in formant],
               "source_slope_db_per_oct": round(float(slope * np.log10(2)), 2),
               "peaks": peaks},
              open(os.path.join(OUT_DIR, "formant_envelope.json"), "w"), indent=2)

    # --- plot: top = pooled (source x formant), bottom = detrended formant ---
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    fig, (ax, ax2) = plt.subplots(2, 1, figsize=(11, 8), sharex=True)

    ax.scatter(all_f, all_db, s=2, alpha=0.10, color="#4a90d9", label="harmonics (all notes)")
    ax.plot(cen_v, env_v, color="#d9534f", lw=2.0, label="pooled envelope (source x formant)")
    ax.plot(cen_v, trend, color="#888", lw=1.4, ls="--", label=f"source rolloff fit ({slope*np.log10(2):.1f} dB/oct)")
    ax.set_ylabel("normalized amplitude (dB)")
    ax.set_title(f"Viola body formant from {len(per_note)} notes (4 strings)")
    ax.legend(loc="lower left", fontsize=8)

    ax2.axhline(0, color="#ccc", lw=0.8)
    ax2.plot(cen_v, formant, color="#5cb85c", lw=2.2, label="detrended formant (body resonance)")
    for p in peaks[:8]:
        ax2.axvline(p["freq"], color="#5cb85c", ls=":", lw=0.8, alpha=0.7)
        ax2.annotate(f"{p['freq']:.0f}", (p["freq"], formant.max()),
                     fontsize=7, color="#3a7d3a", rotation=90, va="top")
    for name, fhz in LIT_MODES.items():
        ax2.axvline(fhz, color="#888", ls="--", lw=0.7, alpha=0.5)
        ax2.annotate(name, (fhz, formant.min()), fontsize=6, color="#555",
                     rotation=90, va="bottom")
    ax2.set_xscale("log")
    ax2.set_xlim(FREQ_LO, FREQ_HI)
    ax2.set_xlabel("frequency (Hz, log)"); ax2.set_ylabel("resonance (dB)")
    ax2.set_title("Detrended formant — green = body modes, gray dashed = literature priors")
    ax2.legend(loc="upper right", fontsize=8)
    fig.tight_layout()
    fig.savefig(os.path.join(OUT_DIR, "formant.png"), dpi=120)

    print(f"notes used: {len(per_note)}   skipped: {len(skipped)}")
    if skipped:
        for s in skipped[:10]:
            print("  skip:", s)
    print(f"harmonic points pooled: {len(all_f)}")
    print("top formant peaks (freq Hz, gain dB above median):")
    for p in peaks[:8]:
        print(f"  {p['freq']:8.1f}   {p['gain_db']:+.2f}")
    print("out:", OUT_DIR)

if __name__ == "__main__":
    main()
