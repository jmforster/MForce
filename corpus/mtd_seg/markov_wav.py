"""Render the model + random figure pools to 48kHz/16-bit/stereo WAV so they can
be auditioned in mforce_ui's audition panel (point it at the output folder).

A neutral additive tone (fundamental + 2 rolled-off harmonics) per note — the point
is to judge melody/rhythm, not timbre. Files are named k{NN}_{ii}_model.wav.

Reads:
  pools/generated.json
Writes:
  <repo>/renders/markov_figures/*.wav     (default; --out to override)

Usage:
  python markov_wav.py                 # 48kHz, 120 BPM, anchor C4
  python markov_wav.py --bpm 100 --anchor 67
"""
import argparse, json, math, pathlib, struct, wave

ROOT      = pathlib.Path(__file__).resolve().parent
POOL_DIR  = ROOT / "pools"
REPO      = ROOT.parent.parent
OUT_DIR   = REPO / "renders" / "markov_figures"

SR          = 48000
DEFAULT_BPM = 120
ANCHOR_MIDI = 60   # C4
MAJOR_SEMI  = [0, 2, 4, 5, 7, 9, 11]
HARMONICS   = [(1, 1.0), (2, 0.4), (3, 0.2)]
AMP         = 0.25


def step_to_freq(cum_step, anchor):
    octs, deg = divmod(cum_step, 7)
    midi = anchor + 12 * octs + MAJOR_SEMI[deg]
    return 440.0 * 2 ** ((midi - 69) / 12.0)


def synth_figure(step, pulse, bpm, anchor):
    spb = 60.0 / bpm
    buf = []
    cum = 0
    atk = int(0.003 * SR)
    for s, p in zip(step, pulse):
        cum += s
        freq = step_to_freq(cum, anchor)
        n = max(1, int(p * spb * SR))
        rel = int(n * 0.10)                       # short release => slight detache
        for i in range(n):
            t = i / SR
            v = sum(a * math.sin(2 * math.pi * h * freq * t) for h, a in HARMONICS)
            v *= AMP / sum(a for _, a in HARMONICS)
            if i < atk:                            # de-click attack
                v *= i / atk
            if i > n - rel:                        # release ramp
                v *= (n - i) / rel
            buf.append(v)
    return buf


def write_wav(path, mono):
    with wave.open(str(path), "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(SR)
        frames = bytearray()
        for v in mono:
            s = max(-1.0, min(1.0, v))
            iv = int(s * 32767)
            frames += struct.pack("<hh", iv, iv)   # L = R
        w.writeframes(bytes(frames))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--bpm", type=int, default=DEFAULT_BPM)
    ap.add_argument("--anchor", type=int, default=ANCHOR_MIDI)
    ap.add_argument("--tail", type=float, default=0.5,
                    help="seconds of silence appended to each WAV")
    ap.add_argument("--out", default=str(OUT_DIR))
    args = ap.parse_args()

    out = pathlib.Path(args.out)
    out.mkdir(parents=True, exist_ok=True)

    pool = json.loads((POOL_DIR / "generated.json").read_text(encoding="utf-8"))
    count = 0
    for e in pool:
        k = e["num_notes"]
        i = int(e["id"].rsplit("_", 1)[1])           # per-length index from gen_k4_3
        name = f"k{k:02d}_{i:02d}_model.wav"
        mono = synth_figure(e["step"], e["pulse"], args.bpm, args.anchor)
        mono += [0.0] * int(args.tail * SR)          # trailing silence
        write_wav(out / name, mono)
        count += 1

    print(f"wrote {count} WAVs to {out}")
    print(f"Point mforce_ui's audition panel at:\n  {out}")


if __name__ == "__main__":
    main()
