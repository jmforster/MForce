"""
build_instrument.py — ONE viola instrument: a single shared body formant
(separated, fixed in Hz) + a generic de-formanted source spectrum. Plays any
pitch from the same patch (unlike the per-note warm-starts).

Source-filter model:
  measured_note(k) ~= Source(k) x BodyTransfer(k*f0)
We estimate BodyTransfer from derive_formant.py's pooled curve, build it as a
BandSpectrum (fixed Hz), de-formant each note's harmonics by it, and AVERAGE the
residual source across notes/strings -> one generic source spectrum. The engine
re-applies the shared formant per note via the additive-boost formantWeight, so
the fixed body modes correctly colour whatever pitch is played.

Renders a short phrase across all four strings and compares the rendered D4/A4
segments back to the real samples (validation that one instrument reproduces
both via the shared formant).
"""
import os, json, subprocess
import numpy as np
import soundfile as sf

HERE = os.path.dirname(os.path.abspath(__file__))
SAMPLE_DIR = os.path.join(HERE, "..", "inst_samples", "viola")
OUT = os.path.join(HERE, "out")
PATCH_DIR = os.path.join(HERE, "patches")
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
REND = os.path.join(REPO, "renders", "warmstart")
CLI = os.path.join(REPO, "build", "tools", "mforce_cli", "Release", "mforce_cli.exe")
for d in (PATCH_DIR, REND):
    os.makedirs(d, exist_ok=True)

SUS_START_S, SUS_LEN_S, HARM_TOL = 0.6, 2.0, 0.03
FREQ_MAX, RENDER_SR, NMAX = 15500.0, 48000, 48

# Notes used to estimate the generic source (spread across the 4 strings).
SOURCE_SET = [("sulC", "C3", 48), ("sulC", "G3", 55), ("sulG", "G3", 55),
              ("sulG", "D4", 62), ("sulD", "D4", 62), ("sulD", "A4", 69),
              ("sulA", "A4", 69), ("sulA", "E5", 76)]
# Body-formant BandSpectrum coverage (linear Hz grid).
BAND_LO, BAND_HI, BAND_INC = 160.0, 6000.0, 25.0

def midi_to_freq(m): return 440.0 * 2 ** ((m - 69) / 12.0)

def find_file(string, note):
    for f in os.listdir(SAMPLE_DIR):
        if f".{string}." in f and f".{note}." in f and f.endswith(".aif"):
            return os.path.join(SAMPLE_DIR, f)
    raise FileNotFoundError(f"{string} {note}")

def load_mono(p):
    x, sr = sf.read(p)
    return (x.mean(axis=1) if x.ndim > 1 else x).astype(np.float64), sr

def harmonic_amps(x, sr, f0):
    n0 = int(SUS_START_S * sr); n1 = min(len(x), n0 + int(SUS_LEN_S * sr))
    seg = x[n0:n1] * np.hanning(n1 - n0)
    nfft = 1 << int(np.ceil(np.log2(len(seg))))
    spec = np.abs(np.fft.rfft(seg, nfft)); fb = np.fft.rfftfreq(nfft, 1.0 / sr)
    ks, amps = [], []
    for k in range(1, min(NMAX, int(FREQ_MAX / f0)) + 1):
        sel = (fb >= k * f0 * (1 - HARM_TOL)) & (fb <= k * f0 * (1 + HARM_TOL))
        if np.any(sel):
            ks.append(k); amps.append(float(np.max(spec[sel])))
    return np.array(ks), np.array(amps)

def main():
    import matplotlib; matplotlib.use("Agg"); import matplotlib.pyplot as plt

    # --- 1. body transfer (BandSpectrum) from derived formant ---
    fe = json.load(open(os.path.join(OUT, "formant_envelope.json")))
    cf = np.array(fe["freq_hz"]); fdb = np.array(fe["formant_db"])
    band_f = np.arange(BAND_LO, BAND_HI + BAND_INC, BAND_INC)
    band_db = np.interp(band_f, cf, fdb, left=float(fdb.min()), right=float(fdb.min()))
    # Body modes = boost above the median baseline, capped at a physical +18 dB.
    band_db = np.clip(band_db - np.median(band_db), 0.0, 18.0)
    band_G = 10 ** (band_db / 20.0) - 1.0                  # additive-boost gain

    def body_gain(fhz):  # replicate engine BandSpectrum.get_gain (linear interp)
        return np.interp(fhz, band_f, band_G, left=0.0, right=0.0)

    # --- 2. generic source = avg over notes of (measured / (1 + G)) ---
    acc = np.zeros(NMAX); cnt = np.zeros(NMAX)
    for string, note, midi in SOURCE_SET:
        try:
            x, sr = load_mono(find_file(string, note))
        except FileNotFoundError:
            continue
        f0 = midi_to_freq(midi)
        ks, amps = harmonic_amps(x, sr, f0)
        amps = amps / (amps.max() + 1e-12)                 # per-note relative
        src = amps / (1.0 + body_gain(ks * f0))            # de-formant
        for k, s in zip(ks, src):
            acc[k - 1] += s; cnt[k - 1] += 1
    nz = cnt > 0
    src = np.zeros(NMAX); src[nz] = acc[nz] / cnt[nz]
    nh = int(np.max(np.where(nz))) + 1
    mults = list(range(1, nh + 1))
    sa = np.array(src[:nh]); sa = sa / (sa.sum() + 1e-12) * 0.8   # render level

    # --- 3. assemble ONE instrument patch ---
    nodes = [
        {"id": "ampEnv", "type": "Envelope",
         "params": {"preset": "adsr", "attack": 0.06, "decay": 0.15,
                    "sustainLevel": 1.0, "release": 0.2}},
        {"id": "viola_body", "type": "BandSpectrum",
         "params": {"startFreq": BAND_LO, "freqIncrement": BAND_INC,
                    "gains": [round(float(g), 5) for g in band_G]}},
        {"id": "vla_partials", "type": "ExplicitPartials",
         "params": {"mult1": mults, "mult2": mults,
                    "ampl1": [round(float(a), 8) for a in sa],
                    "ampl2": [round(float(a), 8) for a in sa],
                    "rolloff1": 0.0, "rolloff2": 0.0, "detune1": 0.0, "detune2": 0.0}},
        {"id": "vib", "type": "Vibrato",
         "params": {"frequency": 293.66, "speed": 5.5, "depth": 0.025,
                    "attack": 0.4, "speedVar": 0.12, "depthVar": 0.15,
                    "zeroCrossTendency": 1.0}},
        {"id": "vla", "type": "AdditiveSource",
         "params": {"seed": 7, "frequency": {"ref": "vib"},
                    "amplitude": {"ref": "ampEnv"},
                    "formant": {"ref": "viola_body"}, "formantWeight": 1.0,
                    "partials": {"ref": "vla_partials"}}},
    ]
    score = [{"note": 48, "velocity": 0.85, "time": 0.0, "duration": 1.5},
             {"note": 55, "velocity": 0.85, "time": 2.0, "duration": 1.5},
             {"note": 62, "velocity": 0.85, "time": 4.0, "duration": 1.5},
             {"note": 69, "velocity": 0.85, "time": 6.0, "duration": 1.5}]
    patch = {"sampleRate": RENDER_SR,
             "graph": {"nodes": nodes, "output": "vla"},
             "instrument": {"paramMap": {"frequency": "vib.frequency"}, "polyphony": 1},
             "score": score}
    ppath = os.path.join(PATCH_DIR, "viola_instrument.json")
    json.dump(patch, open(ppath, "w"), indent=2)

    out_wav = os.path.join(REND, "viola_instrument.wav")
    r = subprocess.run([CLI, ppath, out_wav], capture_output=True, text=True)
    print("render:", "OK" if os.path.exists(out_wav) else "FAIL", r.stderr.strip()[-200:])
    if os.path.exists(out_wav):           # post-normalize for fair A/B level
        yy, syy = sf.read(out_wav)
        sf.write(out_wav, yy / (np.max(np.abs(yy)) + 1e-12) * 0.7, syy)
    print(f"generic source: {nh} harmonics, from {int(cnt.max())} notes")
    print("body BandSpectrum points:", len(band_G), "peak gain factor:", round(float(band_G.max()+1),2))

    # --- 4. validate: rendered D4/A4 segments vs real ---
    if not os.path.exists(out_wav):
        return
    y, sy = load_mono(out_wav)
    def seg_spec(x, sr, t0):
        s = x[int(t0 * sr):int((t0 + 1.0) * sr)] * np.hanning(int(1.0 * sr))
        nfft = 1 << int(np.ceil(np.log2(len(s))))
        sp = np.abs(np.fft.rfft(s, nfft)); f = np.fft.rfftfreq(nfft, 1.0 / sr)
        return f, 20 * np.log10(sp / (sp.max() + 1e-12) + 1e-9)
    fig, axes = plt.subplots(2, 1, figsize=(11, 8))
    for ax, (note, midi, t0) in zip(axes, [("D4", 62, 4.3), ("A4", 69, 6.3)]):
        string = "sulD" if note == "D4" else "sulA"
        rx, rsr = load_mono(find_file(string, note))
        fr, dr = seg_spec(rx, rsr, SUS_START_S)
        fy, dy = seg_spec(y, sy, t0)
        ax.plot(fr, dr, color="#4a90d9", lw=0.8, alpha=0.8, label=f"real {note}")
        ax.plot(fy, dy, color="#5cb85c", lw=0.8, alpha=0.8, label=f"1-instrument {note}")
        ax.set_xscale("log"); ax.set_xlim(150, 16000); ax.set_ylim(-80, 2)
        ax.set_title(f"{note} — real vs single shared-formant instrument")
        ax.legend(fontsize=8, loc="upper right"); ax.set_xlabel("Hz"); ax.set_ylabel("dB")
    fig.tight_layout()
    fig.savefig(os.path.join(REND, "instrument_spectra.png"), dpi=120)
    print("compare plot:", os.path.join(REND, "instrument_spectra.png"))

if __name__ == "__main__":
    main()
