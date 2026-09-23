"""Template null gate (walk3 plan Task 1).

render <stage>: renders every scores/baselines/*.json template through
mforce_cli --compose into renders/scratch/walk3_null_gate/<stage>/.
compare <a> <b>: per template, byte-compares the piece JSON and the WAV;
prints SAME / DIFF / MISSING per template and a summary line.
"""
import hashlib
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
CLI = REPO / "build/tools/mforce_cli/Release/mforce_cli.exe"
PATCH = REPO / "patches/library/keys/acoustic_piano/piano_default.json"
ROOT = REPO / "renders/scratch/walk3_null_gate"


def sha(p):
    return hashlib.sha256(p.read_bytes()).hexdigest() if p.exists() else None


def render(stage):
    out = ROOT / stage
    out.mkdir(parents=True, exist_ok=True)
    for t in sorted((REPO / "scores/baselines").glob("*.json")):
        prefix = out / t.stem
        p = subprocess.run(
            [str(CLI), "--compose", str(PATCH), str(prefix), "1",
             "--template", str(t)],
            capture_output=True, text=True, cwd=str(REPO))
        status = "ok" if p.returncode == 0 else f"rc={p.returncode}"
        print(f"{t.stem}: {status}")


def compare(a, b):
    same = diff = 0
    for t in sorted((REPO / "scores/baselines").glob("*.json")):
        ja, jb = (ROOT / a / f"{t.stem}_1.json"), (ROOT / b / f"{t.stem}_1.json")
        wa, wb = (ROOT / a / f"{t.stem}_1.wav"), (ROOT / b / f"{t.stem}_1.wav")
        if None in (sha(ja), sha(jb)):
            print(f"{t.stem}: MISSING"); diff += 1; continue
        if sha(ja) == sha(jb) and sha(wa) == sha(wb):
            print(f"{t.stem}: SAME"); same += 1
        else:
            print(f"{t.stem}: DIFF"); diff += 1
    print(f"{same} same, {diff} differ")


if __name__ == "__main__":
    {"render": lambda: render(sys.argv[2]),
     "compare": lambda: compare(sys.argv[2], sys.argv[3])}[sys.argv[1]]()
