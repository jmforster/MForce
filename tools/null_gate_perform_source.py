"""Null gate for the PerformSource P1 conversion (plan_perform_source_p1.md).

--freeze : render every library/baselines patch with the CURRENT binary and
           record SHA256 of the WAV bytes (or RENDER_FAIL:<code>).
(default): re-render and compare against the frozen manifest.

Render failures are recorded and compared like hashes: a patch that failed
before must fail identically after (no silent regressions either way).
"""
import hashlib, json, subprocess, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CLI = ROOT / "build/tools/mforce_cli/Release/mforce_cli.exe"
SETS = ["patches/library", "patches/baselines"]
MANIFEST = ROOT / "tools/null_gate_manifest.json"
OUT = ROOT / "renders/scratch/null_gate.wav"

def render_hash(patch: Path) -> str:
    if OUT.exists():
        OUT.unlink()
    r = subprocess.run([str(CLI), str(patch), str(OUT)],
                       capture_output=True, text=True, cwd=ROOT)
    if r.returncode != 0 or not OUT.exists():
        return f"RENDER_FAIL:{r.returncode}"
    return hashlib.sha256(OUT.read_bytes()).hexdigest()

def main():
    patches = [p for s in SETS for p in sorted((ROOT / s).rglob("*.json"))]
    if "--freeze" in sys.argv:
        manifest = {}
        for p in patches:
            key = str(p.relative_to(ROOT)).replace("\\", "/")
            manifest[key] = render_hash(p)
            print("froze", key, flush=True)
        MANIFEST.write_text(json.dumps(manifest, indent=1))
        print(f"Manifest: {len(manifest)} entries")
        return
    ref = json.loads(MANIFEST.read_text())
    bad = 0
    for p in patches:
        key = str(p.relative_to(ROOT)).replace("\\", "/")
        h = render_hash(p)
        if ref.get(key) != h:
            bad += 1
            print(f"DIFF {key}: {ref.get(key)} -> {h}", flush=True)
    print(f"{len(patches) - bad}/{len(patches)} identical")
    sys.exit(1 if bad else 0)

main()
