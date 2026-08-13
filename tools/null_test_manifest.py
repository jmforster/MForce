#!/usr/bin/env python3
"""Render a patch set and emit a sha256 manifest — the before/after halves
of a corpus null test. Patches that fail to render record FAIL (a FAIL that
matches a FAIL is a pass for null purposes).

Hashes are computed over the sample data with trailing all-zero frames
stripped, so a render-length change that only adds/removes trailing
silence (e.g. the 2026-08-13 removal of the +0.5 s score tail) does not
read as a content change. Any nonzero sample difference still changes the
hash.

Usage: python tools/null_test_manifest.py <outfile> [dir ...]
Default dirs: patches/baselines patches/library
"""
import hashlib
import os
import struct
import subprocess
import sys
import glob
import tempfile
import wave


def stripped_hash(path):
    w = wave.open(path)
    n, ch, sw = w.getnframes(), w.getnchannels(), w.getsampwidth()
    raw = w.readframes(n)
    w.close()
    frame = ch * sw
    end = len(raw)
    zero = b"\x00" * frame
    while end >= frame and raw[end - frame:end] == zero:
        end -= frame
    return hashlib.sha256(raw[:end]).hexdigest()

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
CLI = os.path.join(ROOT, "build", "tools", "mforce_cli", "Release", "mforce_cli.exe")


def main():
    outfile = sys.argv[1]
    dirs = sys.argv[2:] or ["patches/baselines", "patches/library"]
    patches = []
    for d in dirs:
        patches += sorted(glob.glob(os.path.join(ROOT, d, "**", "*.json"),
                                    recursive=True))
    tmp = tempfile.mkdtemp(prefix="ncs_null_")
    lines = []
    for p in patches:
        rel = os.path.relpath(p, ROOT).replace("\\", "/")
        wav = os.path.join(tmp, "out.wav")
        if os.path.exists(wav):
            os.remove(wav)
        try:
            r = subprocess.run([CLI, p, wav], capture_output=True, timeout=600)
            ok = r.returncode == 0 and os.path.exists(wav)
        except subprocess.TimeoutExpired:
            ok = False
        if not ok:
            lines.append("FAIL  %s" % rel)
            continue
        lines.append("%s  %s" % (stripped_hash(wav), rel))
    with open(outfile, "w") as f:
        f.write("\n".join(lines) + "\n")
    n_fail = sum(1 for l in lines if l.startswith("FAIL"))
    print("%d patches, %d rendered, %d FAIL -> %s"
          % (len(lines), len(lines) - n_fail, n_fail, outfile))


if __name__ == "__main__":
    main()
