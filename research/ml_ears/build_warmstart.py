"""
build_warmstart.py — turn a real viola note into a grounded additive warm-start
patch, render it, and compare spectrum to the original.

For each target note:
  1. Load the Iowa sample, extract the sustain, measure harmonic amplitudes at
     k*f0 (these already contain the body formant for THIS note).
  2. Write an instrument-style additive patch (ExplicitPartials with the measured
     amplitudes, AdditiveSource at the note's pitch, ADSR amp envelope).
  3. Render it via mforce_cli.
  4. Trim a matching excerpt of the real note for A/B, and overlay both spectra.

This is the faithful static-spectrum warm-start: it reproduces the note's
steady-state spectrum but NOT its time behaviour (vibrato, bow noise,
fluctuation). The point is to hear exactly how far grounded-spectrum additive
gets, and to make the remaining gap concrete and measurable.
"""
import os, re, json, subprocess
import numpy as np
import soundfile as sf

HERE = os.path.dirname(os.path.abspath(__file__))
SAMPLE_DIR = os.path.join(HERE, "..", "inst_samples", "viola")
PATCH_DIR = os.path.join(HERE, "patches")
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
REND_DIR = os.path.join(REPO, "renders", "warmstart")
CLI = os.path.join(REPO, "build", "tools", "mforce_cli", "Release", "mforce_cli.exe")
os.makedirs(PATCH_DIR, exist_ok=True)
os.makedirs(REND_DIR, exist_ok=True)

SUS_START_S = 0.6
SUS_LEN_S   = 2.0
HARM_TOL    = 0.03
FREQ_MAX    = 15500.0      # just under engine CUTOFF (16000)
RENDER_SR   = 48000

TARGETS = [("D4", 62, "sulD"), ("A4", 69, "sulA")]

def midi_to_freq(m):
    return 440.0 * 2 ** ((m - 69) / 12.0)

def find_file(string, note):
    for f in os.listdir(SAMPLE_DIR):
        if f".{string}." in f and f".{note}." in f and f.endswith(".aif"):
            return os.path.join(SAMPLE_DIR, f)
    raise FileNotFoundError(f"{string} {note}")

def load_mono(path):
    x, sr = sf.read(path)
    if x.ndim > 1:
        x = x.mean(axis=1)
    return x.astype(np.float64), sr

def harmonic_amps(x, sr, f0):
    n0 = int(SUS_START_S * sr)
    n1 = min(len(x), n0 + int(SUS_LEN_S * sr))
    seg = x[n0:n1] * np.hanning(n1 - n0)
    nfft = 1 << int(np.ceil(np.log2(len(seg))))
    spec = np.abs(np.fft.rfft(seg, nfft))
    fbin = np.fft.rfftfreq(nfft, 1.0 / sr)
    mults, amps = [], []
    for k in range(1, int(FREQ_MAX / f0) + 1):
        lo, hi = k * f0 * (1 - HARM_TOL), k * f0 * (1 + HARM_TOL)
        sel = (fbin >= lo) & (fbin <= hi)
        if np.any(sel):
            mults.append(k)
            amps.append(float(np.max(spec[sel])))
    return np.array(mults, float), np.array(amps)

def attack_time(x, sr):
    """Rough attack: time for the amplitude envelope to reach 90% of its peak."""
    win = int(0.01 * sr)
    env = np.sqrt(np.convolve(x ** 2, np.ones(win) / win, "same"))
    pk = env.max()
    idx = np.argmax(env >= 0.9 * pk)
    return max(0.02, idx / sr)

def build_patch(name, midi, mults, amps, atk):
    amps = amps / (amps.sum() + 1e-12) * 0.8     # keep render below clip
    f0 = midi_to_freq(midi)
    nodes = [
        {"id": "ampEnv", "type": "Envelope",
         "params": {"preset": "adsr", "attack": round(atk, 3),
                    "decay": 0.15, "sustainLevel": 1.0, "release": 0.2}},
        {"id": "vla_partials", "type": "ExplicitPartials",
         "params": {"mult1": mults.tolist(), "mult2": mults.tolist(),
                    "ampl1": [round(a, 8) for a in amps],
                    "ampl2": [round(a, 8) for a in amps],
                    "rolloff1": 0.0, "rolloff2": 0.0,
                    "detune1": 0.0, "detune2": 0.0}},
        {"id": "vla", "type": "AdditiveSource",
         "params": {"seed": 7, "frequency": round(f0, 3),
                    "amplitude": {"ref": "ampEnv"},
                    "partials": {"ref": "vla_partials"}}},
    ]
    patch = {
        "sampleRate": RENDER_SR,
        "graph": {"nodes": nodes, "output": "vla"},
        "instrument": {"paramMap": {"frequency": "vla.frequency"}, "polyphony": 1},
        "score": [{"note": midi, "velocity": 0.85, "time": 0.0, "duration": 3.0}],
    }
    p = os.path.join(PATCH_DIR, f"viola_{name}_warmstart.json")
    json.dump(patch, open(p, "w"), indent=2)
    return p

def spectrum_db(x, sr):
    n0 = int(SUS_START_S * sr)
    n1 = min(len(x), n0 + int(SUS_LEN_S * sr))
    seg = x[n0:n1] * np.hanning(n1 - n0)
    nfft = 1 << int(np.ceil(np.log2(len(seg))))
    spec = np.abs(np.fft.rfft(seg, nfft))
    f = np.fft.rfftfreq(nfft, 1.0 / sr)
    db = 20 * np.log10(spec / (spec.max() + 1e-12) + 1e-9)
    return f, db

def main():
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    fig, axes = plt.subplots(len(TARGETS), 1, figsize=(11, 4 * len(TARGETS)))
    if len(TARGETS) == 1:
        axes = [axes]

    for ax, (name, midi, string) in zip(axes, TARGETS):
        src = find_file(string, name)
        x, sr = load_mono(src)
        f0 = midi_to_freq(midi)
        mults, amps = harmonic_amps(x, sr, f0)
        # The ff samples swell (peak ~2s in), so attack_time over-reads; cap it.
        # Envelope/time-behaviour is the deferred part, not what this validates.
        atk = min(attack_time(x, sr), 0.08)
        patch = build_patch(name, midi, mults, amps, atk)

        # real reference excerpt (sustain) for A/B listening
        n0 = int(SUS_START_S * sr); n1 = min(len(x), n0 + int(3.0 * sr))
        real_wav = os.path.join(REND_DIR, f"viola_{name}_real.wav")
        sf.write(real_wav, x[n0:n1] / (np.max(np.abs(x[n0:n1])) + 1e-9) * 0.9, sr)

        # render the warm-start patch
        out_wav = os.path.join(REND_DIR, f"viola_{name}_warmstart.wav")
        r = subprocess.run([CLI, patch, out_wav], capture_output=True, text=True)
        ok = os.path.exists(out_wav)
        print(f"[{name}] f0={f0:.1f}  harmonics={len(mults)}  attack={atk*1000:.0f}ms  "
              f"render={'OK' if ok else 'FAIL'}")
        if not ok:
            print("   ", r.stderr.strip()[:300]); continue

        # spectra overlay
        fr, dr = spectrum_db(x, sr)
        y, syr = load_mono(out_wav)
        fy, dy = spectrum_db(y, syr)
        ax.plot(fr, dr, color="#4a90d9", lw=0.8, alpha=0.8, label=f"real viola {name}")
        ax.plot(fy, dy, color="#d9534f", lw=0.8, alpha=0.8, label=f"warm-start {name}")
        ax.set_xscale("log"); ax.set_xlim(150, 16000); ax.set_ylim(-80, 2)
        ax.set_title(f"{name} ({f0:.1f} Hz) — real vs grounded additive warm-start")
        ax.set_xlabel("Hz (log)"); ax.set_ylabel("dB"); ax.legend(fontsize=8, loc="upper right")

    fig.tight_layout()
    cmp_png = os.path.join(REND_DIR, "warmstart_spectra.png")
    fig.savefig(cmp_png, dpi=120)
    print("compare plot:", cmp_png)
    print("renders dir:", REND_DIR)

if __name__ == "__main__":
    main()
