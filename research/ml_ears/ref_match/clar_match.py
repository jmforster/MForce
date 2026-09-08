"""Clarinet reference match: render clarinet_attempt playing OTJ in D
(the reference's measured key, first note F#4), then run the ml_ears
comparison against the YouTube reference."""
import json, copy, os, subprocess, sys

SCRATCH = os.path.dirname(os.path.abspath(__file__))
CLI = r"C:\@dev\repos\mforce\build\tools\mforce_cli\Release\mforce_cli.exe"
PATCH = r"C:\@dev\repos\mforce\patches\pending\clarinets\clarinet_attempt.json"

# OTJ, D major, eighth = 0.31 s (measured from the reference).
E = 0.31
Q = 2 * E
D4, E4, FS4, G4, A4 = 62, 64, 66, 67, 69
line1 = [(FS4, Q), (FS4, Q), (G4, Q), (A4, Q), (A4, Q), (G4, Q), (FS4, Q), (E4, Q),
         (D4, Q), (D4, Q), (E4, Q), (FS4, Q), (FS4, Q * 1.5), (E4, Q * 0.5), (E4, Q * 2)]
line2 = [(FS4, Q), (FS4, Q), (G4, Q), (A4, Q), (A4, Q), (G4, Q), (FS4, Q), (E4, Q),
         (D4, Q), (D4, Q), (E4, Q), (FS4, Q), (E4, Q * 1.5), (D4, Q * 0.5), (D4, Q * 2)]

def build_render(patch_path, out_wav, tag):
    p = copy.deepcopy(json.load(open(patch_path)))
    t = 0.0
    score = []
    for note, dur in line1 + line2:
        score.append({"time": round(t, 3), "duration": round(dur, 3),
                      "note": note, "velocity": 0.85})
        t += dur
    p["score"] = score
    p["seconds"] = round(t + 1.5, 2)
    pj = os.path.join(SCRATCH, f"otj_{tag}.json")
    json.dump(p, open(pj, "w"))
    r = subprocess.run([CLI, pj, out_wav], capture_output=True, timeout=300)
    if r.returncode != 0:
        print("RENDER FAIL:", r.stderr.decode(errors="replace")[:200])
        sys.exit(1)
    return out_wav

if __name__ == "__main__":
    src = sys.argv[1] if len(sys.argv) > 1 else PATCH
    tag = sys.argv[2] if len(sys.argv) > 2 else "clar_attempt"
    wav = build_render(src, os.path.join(SCRATCH, f"otj_{tag}.wav"), tag)

    sys.path.insert(0, r"C:\@dev\repos\mforce\research\ml_ears")
    import oboe_ref_compare as orc
    orc.REF = os.path.join(SCRATCH, "clarinet_ref.wav")
    orc.REN = wav
    orc.main()
