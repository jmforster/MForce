"""Spot-check rendered WAV files for silence / sanity."""
import wave
import struct
import sys
from pathlib import Path

REPO = Path("C:/@dev/repos/mforce")
RENDERS = REPO / "renders/explore"


def wav_stats(path: Path):
    with wave.open(str(path), "rb") as w:
        ch = w.getnchannels()
        sw = w.getsampwidth()
        sr = w.getframerate()
        nf = w.getnframes()
        raw = w.readframes(nf)
    if sw == 2:
        fmt = f"<{nf*ch}h"
        samples = struct.unpack(fmt, raw)
        scale = 32768.0
    else:
        # assume 32-bit float? mforce writes 16-bit pcm — handle just in case
        return None
    peak = max(abs(s) for s in samples) / scale
    sq_sum = sum(s*s for s in samples)
    rms = (sq_sum / len(samples)) ** 0.5 / scale
    nonzero = sum(1 for s in samples if s != 0)
    return {
        "channels": ch, "sample_rate": sr, "frames": nf,
        "peak": peak, "rms": rms,
        "nonzero_pct": 100.0 * nonzero / len(samples),
    }


def main():
    # Spot-check one per progression: middle width, r=0
    samples = [
        "vowels-LIAR/LIAR_w140_r0.wav",
        "vowels-LIAR/LIAR_w60_r1.wav",
        "vowels-OOEE/OOEE_w100_r0.wav",
        "vowels-EEOO/EEOO_w280_r1.wav",
        "vowels-OOEEOO/OOEEOO_w140_r0.wav",
        "vowels-OEEAhat/OEEAhat_w200_r0.wav",
        "vowels-AHEEGOO/AHEEGOO_w100_r1.wav",
        "vowels-AcakeEEO/AcakeEEO_w140_r0.wav",
        "vowels-AcakeEEOO/AcakeEEOO_w60_r0.wav",
        "vowels-AcakeEEOO/AcakeEEOO_w280_r1.wav",
    ]
    all_ok = True
    for rel in samples:
        p = RENDERS / rel
        if not p.exists():
            print(f"MISSING: {rel}")
            all_ok = False
            continue
        s = wav_stats(p)
        ok = s and s["peak"] > 0.01
        flag = "" if ok else "  <-- LOW!"
        print(f"{rel:48s} peak={s['peak']:.4f} rms={s['rms']:.4f} "
              f"nz={s['nonzero_pct']:.1f}%  {flag}")
        if not ok:
            all_ok = False

    # Also count total wavs per folder
    print("\nFolder counts:")
    for d in sorted(RENDERS.glob("vowels-*")):
        n = len(list(d.glob("*.wav")))
        print(f"  {d.name}: {n}")

    print("\nOK" if all_ok else "\nSOME LOW")


if __name__ == "__main__":
    main()
