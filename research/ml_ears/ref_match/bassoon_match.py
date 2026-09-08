"""Bassoon reference match: recon the ref (low register — fmin 70),
render bassoon_attempt playing OTJ in D3, run the full comparison."""
import json, copy, os, subprocess, sys
import numpy as np

SCRATCH = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, SCRATCH)
sys.path.insert(0, r"C:\@dev\repos\mforce\research\ml_ears")
import oboe_ref_compare as orc

CLI = r"C:\@dev\repos\mforce\build\tools\mforce_cli\Release\mforce_cli.exe"
PATCH = r"C:\@dev\repos\mforce\patches\pending\bassoons\bassoon_attempt.json"
REF = os.path.join(SCRATCH, "bassoon_ref.wav")

NOTE = "C C# D D# E F F# G G# A A# B".split()

def recon(path, fmin=70.0):
    sr, x = orc.load(path)
    print(f"{path.split(os.sep)[-1]}: {len(x)/sr:.1f}s @ {sr}")
    ts, f0 = orc.pitch_track(sr, x, fmin=fmin, fmax=1200.0)
    v = f0[~np.isnan(f0)]
    print(f"  pitch {np.percentile(v,5):.0f}-{np.percentile(v,95):.0f} Hz median {np.median(v):.0f}")
    segs = orc.sustained_segments(ts, f0, min_len=0.15)
    for t0, t1, f in segs[:24]:
        m = 69 + 12 * np.log2(f / 440); n = int(round(m))
        print(f"  {t0:6.2f}-{t1:6.2f} ({t1-t0:4.2f}s) {f:6.1f} Hz {NOTE[n%12]}{n//12-1} {int((m-n)*100):+3d}c")
    return sr, x, ts, f0, segs

def build_render(patch_path, out_wav, eighth=0.31, base=50):
    D, E, FS, G, A = base, base + 2, base + 4, base + 5, base + 7
    Q = 2 * eighth
    l1 = [(FS,Q),(FS,Q),(G,Q),(A,Q),(A,Q),(G,Q),(FS,Q),(E,Q),
          (D,Q),(D,Q),(E,Q),(FS,Q),(FS,Q*1.5),(E,Q*0.5),(E,Q*2)]
    l2 = [(FS,Q),(FS,Q),(G,Q),(A,Q),(A,Q),(G,Q),(FS,Q),(E,Q),
          (D,Q),(D,Q),(E,Q),(FS,Q),(E,Q*1.5),(D,Q*0.5),(D,Q*2)]
    p = copy.deepcopy(json.load(open(patch_path)))
    t = 0.0; score = []
    for note, dur in l1 + l2:
        score.append({"time": round(t,3), "duration": round(dur,3),
                      "note": note, "velocity": 0.85})
        t += dur
    p["score"] = score; p["seconds"] = round(t + 1.5, 2)
    pj = out_wav.replace(".wav", ".json")
    json.dump(p, open(pj, "w"))
    r = subprocess.run([CLI, pj, out_wav], capture_output=True, timeout=300)
    if r.returncode != 0:
        print("RENDER FAIL:", r.stderr.decode(errors="replace")[:200]); sys.exit(1)

def compare(refwav, renwav, fmin=70.0):
    srR, ref, tsR, f0R, segR = recon(refwav, fmin)
    srN, ren, tsN, f0N, segN = recon(renwav, fmin)
    print("\nLTAS envelope peaks (200-5000 Hz):")
    for nm, sr, x in (("ref", srR, ref), ("render", srN, ren)):
        f, db = orc.ltas(sr, x)
        _, _, peaks = orc.spectral_envelope_peaks(f, db, lo=200, hi=5000)
        tops = sorted(sorted(peaks, key=lambda p: -p[2])[:5])
        print(f"  {nm}: " + "; ".join(f"{p[0]:.0f}Hz({p[1]:.0f}dB,p{p[2]:.0f})" for p in tops))
    if segR and segN:
        best = None
        for a in segR:
            for b in segN:
                dc = abs(1200 * np.log2(a[2] / b[2]))
                if dc > 80: continue
                dur = min(a[1]-a[0], b[1]-b[0])
                sc = dc - 40 * dur
                if best is None or sc < best[0]: best = (sc, a, b)
        if best:
            _, a, b = best
            print(f"\nmatched: ref {a[2]:.1f} Hz ({a[1]-a[0]:.2f}s) vs render {b[2]:.1f} Hz ({b[1]-b[0]:.2f}s)")
            hR = orc.harmonic_env(srR, ref, a[0], a[1], a[2]); hR -= hR[0]
            hN = orc.harmonic_env(srN, ren, b[0], b[1], b[2]); hN -= hN[0]
            n = min(len(hR), len(hN), 16)
            print("  ref:    " + " ".join(f"{v:6.1f}" for v in hR[:n]))
            print("  render: " + " ".join(f"{v:6.1f}" for v in hN[:n]))
            print("  delta:  " + " ".join(f"{v:6.1f}" for v in (hN[:n]-hR[:n])))
            hlR, mR = orc.interharmonic_noise(srR, ref, a[0], a[1], a[2])
            hlN, mN = orc.interharmonic_noise(srN, ren, b[0], b[1], b[2])
            print(f"  noise gap: ref {hlR-mR:.1f} dB, render {hlN-mN:.1f} dB")
            vR = orc.vibrato(tsR, f0R, a[0], a[1]); vN = orc.vibrato(tsN, f0N, b[0], b[1])
            print(f"  vibrato: ref {vR}, render {vN}")
            aR = orc.attack_time(srR, ref, a[0], a[2]); aN = orc.attack_time(srN, ren, b[0], b[2])
            print(f"  attack: ref {(aR or -1)*1000:.0f} ms, render {(aN or -1)*1000:.0f} ms")

if __name__ == "__main__":
    wav = os.path.join(SCRATCH, "otj_bassoon_base.wav")
    build_render(PATCH, wav)
    compare(REF, wav)
