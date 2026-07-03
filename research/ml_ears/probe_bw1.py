"""Does bw1 (attack bandwidth) actually affect the output? Compare the 3 zero
variants: pairwise WAV difference + noise(flatness) and RMS in the attack window
vs sustain. Tells us if bw1 is inert (bug) or just too quiet (energy-preserving
AM makes high-bw quieter, masking the catch)."""
import os, numpy as np, soundfile as sf

REND = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..",
                                    "renders", "warmstart", "attack_sets"))
names = ["catch_zero_high", "catch_zero_higher", "catch_zero_highest"]  # bw1 0.55/0.8/1.0
ys = {}
for n in names:
    y, sr = sf.read(os.path.join(REND, n + ".wav"))
    if y.ndim > 1: y = y.mean(axis=1)
    ys[n] = (y, sr)

def flat(seg):
    sp = np.abs(np.fft.rfft(seg*np.hanning(len(seg)))) + 1e-9
    return np.exp(np.mean(np.log(sp)))/np.mean(sp)

print("per-variant: attack-window noise & level (vibrato in these, but compare relative):")
for n in names:
    y, sr = ys[n]
    atk = y[int(0.02*sr):int(0.12*sr)]      # the catch window
    sus = y[int(1.0*sr):int(2.0*sr)]
    print(f"  {n:22s} flat_attack={flat(atk):.4f} flat_sustain={flat(sus):.4f} "
          f"rms_attack={np.sqrt(np.mean(atk**2)):.4f} rms_sustain={np.sqrt(np.mean(sus**2)):.4f}")

# pairwise difference (are they even different signals?)
print("pairwise max|diff| over first 0.3s (0 = identical):")
a = ys["catch_zero_high"][0]; sr = ys["catch_zero_high"][1]
b = ys["catch_zero_highest"][0]
m = min(len(a), len(b), int(0.3*sr))
print(f"  high vs highest: {np.max(np.abs(a[:m]-b[:m])):.5f}  (sustain region "
      f"{np.max(np.abs(a[int(1*sr):int(1*sr)+m]-b[int(1*sr):int(1*sr)+m])):.5f})")
