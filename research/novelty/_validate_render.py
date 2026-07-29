"""One-off: render a small library + candidate set and run the novelty metric on
real patch renders (dsp BACKLOG item 4 validation). Renders into
renders/novelty/. Not part of the tool — a reproducible validation driver."""
import os
import subprocess
import sys

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
CLI = os.path.join(REPO, "build", "tools", "mforce_cli", "Release", "mforce_cli.exe")
OUT = os.path.join(REPO, "renders", "novelty")
os.makedirs(os.path.join(OUT, "library"), exist_ok=True)
os.makedirs(os.path.join(OUT, "candidates"), exist_ok=True)

# Familiar library: bowed-string + vowel additive timbres (all CLI-renderable;
# many legacy patches only sound via the UI keyboard, so they are avoided here).
LIBRARY = [
    "patches/viola/viola_saw_16p_full.json",
    "patches/viola/viola_saw_24p_full.json",
    "patches/viola/viola_saw_32p_full.json",
    "patches/viola/viola_saw_24p_dark.json",
    "patches/vowels/OOEE_w400.json",
    "patches/vowels/OOEE_w600.json",
    "patches/vowels/AHEEGOO_w400.json",
]
# Candidates: two exact library members (-> ~0), a related vowel (low-moderate),
# a timbre variant (low), and an FM patch far from the additive-string library
# (should rank most novel).
CANDIDATES = [
    "patches/viola/viola_saw_24p_full.json",   # exact library member -> ~0
    "patches/vowels/OOEE_w400.json",           # exact library member -> ~0
    "patches/viola/viola_saw_24p_bright.json", # timbre variant, near
    "patches/vowels/EEOO_w400.json",           # related vowel family
    "patches/vowels/LIAR_w900.json",           # wide-formant vowel, farther
    "patches/fm_spacy_test.json",              # FM, far from additive strings
]


def render(patch, dest_dir):
    name = os.path.splitext(os.path.basename(patch))[0]
    wav = os.path.join(dest_dir, name + ".wav")
    r = subprocess.run([CLI, os.path.join(REPO, patch), wav],
                       capture_output=True, text=True)
    ok = r.returncode == 0 and os.path.exists(wav)
    tail = (r.stdout + r.stderr).strip().splitlines()
    print(f"  {'OK ' if ok else 'ERR'} {name}: {tail[-1] if tail else ''}")
    return wav if ok else None


print("rendering library...")
for p in LIBRARY:
    render(p, os.path.join(OUT, "library"))
print("rendering candidates...")
for p in CANDIDATES:
    render(p, os.path.join(OUT, "candidates"))
print(f"done -> {OUT}")
