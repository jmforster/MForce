"""Render the fm-oversample-2 A/B patches -> renders/fm_oversample2/.
No novelty ranking — this is a listening A/B (os1 vs os8 per note)."""
import glob
import os
import subprocess

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
CLI = os.path.join(REPO, "build", "tools", "mforce_cli", "Release", "mforce_cli.exe")
PATCHES = os.path.join(REPO, "patches", "fm_oversample2")
OUT = os.path.join(REPO, "renders", "fm_oversample2")
os.makedirs(OUT, exist_ok=True)


def main():
    patches = sorted(glob.glob(os.path.join(PATCHES, "*.json")))
    print(f"rendering {len(patches)} fm-oversample-2 patches...")
    for p in patches:
        name = os.path.splitext(os.path.basename(p))[0]
        wav = os.path.join(OUT, name + ".wav")
        r = subprocess.run([CLI, p, wav], capture_output=True, text=True)
        out = (r.stdout + r.stderr).strip().splitlines()
        line = out[-1] if out else ""
        ok = r.returncode == 0 and os.path.exists(wav) and "peak=0 " not in line
        print(f"  {'OK ' if ok else 'ERR'} {name}: {line}")
    print(f"renders -> {OUT}")


if __name__ == "__main__":
    main()
