"""Null gate for the PerformSource P1 conversion (plan_perform_source_p1.md).

--freeze : render every library/baselines patch with the CURRENT binary and
           record SHA256 of the WAV bytes (or RENDER_FAIL:<code>).
--jobs N : parallel renders (default: cpu_count - 2, floor 4). Renders are
           independent mforce_cli processes writing per-patch temp WAVs, so
           the gate parallelizes cleanly (2026-09-05, Matt: gate wall-clock
           was eating the 90% of the loop).
(default): re-render and compare against the frozen manifest.

Render failures are recorded and compared like hashes: a patch that failed
before must fail identically after (no silent regressions either way).
"""
import concurrent.futures as cf
import hashlib, json, os, subprocess, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CLI = ROOT / "build/tools/mforce_cli/Release/mforce_cli.exe"
SETS = ["patches/library", "patches/baselines"]
MANIFEST = ROOT / "tools/null_gate_manifest.json"
SCRATCH = ROOT / "renders/scratch"

def jobs_arg() -> int:
    if "--jobs" in sys.argv:
        return max(1, int(sys.argv[sys.argv.index("--jobs") + 1]))
    return max(4, (os.cpu_count() or 8) - 2)

def render_hash(patch: Path, out: Path) -> str:
    if out.exists():
        out.unlink()
    r = subprocess.run([str(CLI), str(patch), str(out)],
                       capture_output=True, text=True, cwd=ROOT)
    if r.returncode != 0 or not out.exists():
        return f"RENDER_FAIL:{r.returncode}"
    h = hashlib.sha256(out.read_bytes()).hexdigest()
    out.unlink()
    return h

def hash_all(patches):
    """(patch, hash) pairs in input order; per-patch temp WAVs, parallel."""
    SCRATCH.mkdir(parents=True, exist_ok=True)
    def work(ip):
        i, p = ip
        return p, render_hash(p, SCRATCH / f"null_gate_{i}.wav")
    with cf.ThreadPoolExecutor(max_workers=jobs_arg()) as ex:
        return list(ex.map(work, enumerate(patches)))

# library/voice vowels are identical mechanisms differing only in formant
# frequencies (Matt 2026-09-14): the gate keeps ONE sung and ONE spoken
# representative and skips the rest. words/ (sequences) still covered.
VOICE_DIR = "patches/library/voice"
VOICE_KEEP = {"sing_alto_A.json", "speech_c_AE.json"}

def gate_skipped(p: Path) -> bool:
    rel = p.relative_to(ROOT)
    if rel.parts[:3] != ("patches", "library", "voice") or len(rel.parts) != 4:
        return False
    name = rel.parts[3]
    return (name.startswith("sing_") or name.startswith("speech_")) \
        and name not in VOICE_KEEP

def main():
    patches = [p for s in SETS for p in sorted((ROOT / s).rglob("*.json"))
               if not gate_skipped(p)]
    if "--freeze" in sys.argv:
        manifest = {}
        for p, h in hash_all(patches):
            key = str(p.relative_to(ROOT)).replace("\\", "/")
            manifest[key] = h
            print("froze", key, flush=True)
        MANIFEST.write_text(json.dumps(manifest, indent=1))
        print(f"Manifest: {len(manifest)} entries")
        return
    ref = json.loads(MANIFEST.read_text())
    seen, bad, new = set(), 0, 0
    for p, h in hash_all(patches):
        key = str(p.relative_to(ROOT)).replace("\\", "/")
        seen.add(key)
        if key not in ref:
            # Authored since the freeze. NOT a regression — there is nothing to
            # compare it to. Reported so it is visible, but it must not fail the
            # run: a gate that cries wolf on a patch someone just saved is how a
            # real diff gets waved through later.
            new += 1
            print(f"NEW  {key} (not in manifest, nothing to compare)", flush=True)
            continue
        if ref[key] != h:
            bad += 1
            print(f"DIFF {key}: {ref[key]} -> {h}", flush=True)
    # A manifest entry with no file is its own failure — a patch that silently
    # vanished would otherwise shrink the denominator and read as a pass.
    for key in sorted(set(ref) - seen):
        bad += 1
        print(f"GONE {key} (in manifest, no file on disk)", flush=True)

    checked = len(ref)
    print(f"{checked - bad}/{checked} manifest entries identical"
          + (f"; {new} new patch(es) not in manifest" if new else ""))
    sys.exit(1 if bad else 0)

main()
