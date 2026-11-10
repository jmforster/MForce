"""Conversion null gate (plan_perform_source_p2a.md Task 6).

Every patch in the P1 manifest is copied to a scratch file, rewritten there
into wiring format by tools/convert_patch.py (whose paramMap rule does the
conversion), rendered, and compared against the hash the SAME
patch produced before PerformSource existed. A converted patch that renders one
byte differently means the wiring format does not say what the paramMap said.

Patches with no paramMap are converted trivially (no-op) and still rendered — a
no-op conversion that changed the audio would mean the tool is corrupting files
it claims not to touch.

Classification mirrors tools/null_gate_perform_source.py:
  DIFF          converted render != frozen hash. Fails.
  CONVERT_FAIL  the converter refused the patch. Fails.
  GONE          manifest entry with no file on disk. Fails.
Nothing is silently absorbed into a shrinking denominator.
"""
import hashlib, json, shutil, subprocess, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CLI = ROOT / "build/tools/mforce_cli/Release/mforce_cli.exe"
CONVERTER = ROOT / "tools/convert_patch.py"
MANIFEST = ROOT / "tools/null_gate_manifest.json"
TMP_JSON = ROOT / "renders/scratch/wiring_gate.json"
TMP_WAV = ROOT / "renders/scratch/wiring_gate.wav"


def convert_file(src: Path):
    shutil.copyfile(src, TMP_JSON)
    r = subprocess.run([sys.executable, str(CONVERTER), str(TMP_JSON)],
                       capture_output=True, text=True, cwd=str(ROOT))
    out = (r.stdout or "") + (r.stderr or "")
    refused = [line for line in out.splitlines() if "NOT rewritten" in line]
    return r.returncode, (refused[0] if refused else out).strip()


def render_hash(patch: Path) -> str:
    if TMP_WAV.exists():
        TMP_WAV.unlink()
    r = subprocess.run([str(CLI), str(patch), str(TMP_WAV)],
                       capture_output=True, text=True, cwd=str(ROOT))
    if r.returncode != 0 or not TMP_WAV.exists():
        return f"RENDER_FAIL:{r.returncode}"
    return hashlib.sha256(TMP_WAV.read_bytes()).hexdigest()


def main():
    ref = json.loads(MANIFEST.read_text())
    bad = converted = 0
    for key, want in sorted(ref.items()):
        src = ROOT / key
        if not src.exists():
            bad += 1
            print(f"GONE {key} (in manifest, no file on disk)", flush=True)
            continue
        had_pm = "paramMap" in json.loads(
            src.read_text(encoding="utf-8")).get("instrument", {})
        rc, err = convert_file(src)
        if rc != 0:
            bad += 1
            print(f"CONVERT_FAIL {key}: {err.splitlines()[-1][:120] if err else rc}",
                  flush=True)
            continue
        if had_pm:
            converted += 1
        got = render_hash(TMP_JSON)
        if got != want:
            bad += 1
            print(f"DIFF {key} (paramMap={had_pm}): {want[:16]} -> {got[:16]}",
                  flush=True)
    total = len(ref)
    print(f"{total - bad}/{total} identical after conversion "
          f"({converted} carried a paramMap)")
    sys.exit(1 if bad else 0)


main()
