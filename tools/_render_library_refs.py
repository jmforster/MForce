"""Render every patches/library JSON to WAV for the novelty library
(backlog 37 sweep prerequisite). Output dir is the only argument.
Failures are reported and skipped — the library embedding just goes without
that sound."""
import os, subprocess, sys, glob

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
CLI = os.path.join(ROOT, "build", "tools", "mforce_cli", "Release", "mforce_cli.exe")

def main():
    out_dir = sys.argv[1]
    os.makedirs(out_dir, exist_ok=True)
    patches = sorted(glob.glob(os.path.join(ROOT, "patches", "library", "**", "*.json"),
                               recursive=True))
    ok = fail = 0
    for p in patches:
        name = os.path.splitext(os.path.basename(p))[0]
        wav = os.path.join(out_dir, name + ".wav")
        r = subprocess.run([CLI, p, wav], capture_output=True, timeout=600)
        if r.returncode == 0 and os.path.exists(wav):
            ok += 1
        else:
            fail += 1
            print("FAIL", os.path.relpath(p, ROOT), flush=True)
    print(f"{ok} rendered, {fail} failed -> {out_dir}")

main()
