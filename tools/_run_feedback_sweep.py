"""Render all patches in a sweep dir to a renders dir (backlog 37 driver,
render phase). Usage: python tools/_run_feedback_sweep.py <patch_dir> <wav_dir>"""
import glob, os, subprocess, sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
CLI = os.path.join(ROOT, "build", "tools", "mforce_cli", "Release", "mforce_cli.exe")

def main():
    patch_dir, wav_dir = sys.argv[1], sys.argv[2]
    os.makedirs(wav_dir, exist_ok=True)
    ok = fail = 0
    for p in sorted(glob.glob(os.path.join(patch_dir, "fb1_*.json"))):
        name = os.path.splitext(os.path.basename(p))[0]
        wav = os.path.join(wav_dir, name + ".wav")
        r = subprocess.run([CLI, p, wav], capture_output=True, timeout=600)
        if r.returncode == 0 and os.path.exists(wav):
            ok += 1
        else:
            fail += 1
            print("FAIL", name, flush=True)
    print(f"{ok} rendered, {fail} failed -> {wav_dir}")

main()
