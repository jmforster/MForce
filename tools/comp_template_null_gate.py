"""The comp gate: every baseline template composes to the bytes the manifest holds.

  python tools/comp_template_null_gate.py check  [--build DIR] [--jobs N]
  python tools/comp_template_null_gate.py freeze [--build DIR] [--jobs N]

Each scores/baselines/*.json is composed with DIR/tools/mforce_cli/Release/
mforce_cli.exe --compose on piano_default, seed 1, into renders/scratch/
comp_gate/; the manifest (tools/comp_gate_manifest.json) holds the sha256 of
the piece JSON and of the WAV per template, or "refused" for a template the
tool refuses (one that is not JSON). DIR defaults to build.

check: compares with the manifest and prints one line per template that
differs, is missing from the manifest, or is in the manifest and gone, then a
summary line; exit 1 on any. A template recorded as refused passes while it is
still refused. Seven of the templates name shelved strategies, so the gate
cannot pass on a build without them: check reads MFORCE_SHELVED from DIR's
CMakeCache.txt and, when it is not ON, says so and exits 1.

freeze: rewrites the manifest from what this build composes.
"""
import concurrent.futures as cf
import hashlib
import json
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PATCH = ROOT / "patches/library/keys/acoustic_piano/piano_default.json"
MANIFEST = ROOT / "tools/comp_gate_manifest.json"
OUT = ROOT / "renders/scratch/comp_gate"
REFUSED = "refused"


def arg(name, default):
    return sys.argv[sys.argv.index(name) + 1] if name in sys.argv else default


def sha(p):
    return hashlib.sha256(p.read_bytes()).hexdigest()


def shelved_on(build):
    cache = build / "CMakeCache.txt"
    if not cache.exists():
        return None
    for line in cache.read_text(encoding="utf-8", errors="replace").splitlines():
        if line.startswith("MFORCE_SHELVED:"):
            return line.split("=", 1)[1].strip().upper() in ("ON", "TRUE", "1", "YES")
    return False


def compose(cli, template):
    """{"json": sha, "wav": sha} for one template, or "refused"."""
    prefix = OUT / template.stem
    outputs = [OUT / f"{template.stem}_1.json", OUT / f"{template.stem}_1.wav"]
    for p in outputs:
        if p.exists():
            p.unlink()
    r = subprocess.run([str(cli), "--compose", str(PATCH), str(prefix), "1", "--template", str(template)],
                       capture_output=True, text=True, cwd=str(ROOT))
    if r.returncode != 0 or not all(p.exists() for p in outputs):
        return REFUSED
    return {"json": sha(outputs[0]), "wav": sha(outputs[1])}


def compose_all(cli, jobs):
    OUT.mkdir(parents=True, exist_ok=True)
    templates = sorted((ROOT / "scores/baselines").glob("*.json"))
    with cf.ThreadPoolExecutor(max_workers=jobs) as ex:
        results = list(ex.map(lambda t: compose(cli, t), templates))
    return {str(t.relative_to(ROOT)).replace("\\", "/"): h for t, h in zip(templates, results)}


def main():
    command = sys.argv[1] if len(sys.argv) > 1 else ""
    if command not in ("check", "freeze"):
        print(__doc__)
        return 2
    build = ROOT / arg("--build", "build")
    cli = build / "tools/mforce_cli/Release/mforce_cli.exe"
    if not cli.exists():
        print(f"no mforce_cli at {cli}: build first (this gate does not build)")
        return 1
    if not shelved_on(build):
        print(f"{build.name} is configured without the shelved strategies, which seven baseline "
              f"templates name: configure it with -DMFORCE_SHELVED=ON")
        return 1
    got = compose_all(cli, int(arg("--jobs", "3")))
    if command == "freeze":
        MANIFEST.write_text(json.dumps(got, indent=1, sort_keys=True) + "\n", encoding="utf-8", newline="\n")
        print(f"manifest: {len(got)} templates, {sum(v == REFUSED for v in got.values())} refused")
        return 0
    ref = json.loads(MANIFEST.read_text(encoding="utf-8"))
    bad = 0
    for key in sorted(set(ref) | set(got)):
        if key not in ref:
            print(f"NEW  {key}: not in the manifest")
        elif key not in got:
            print(f"GONE {key}: in the manifest, no file")
        elif ref[key] != got[key]:
            print(f"DIFF {key}: {ref[key]} -> {got[key]}")
        else:
            continue
        bad += 1
    same = len(set(ref) & set(got)) - sum(1 for k in set(ref) & set(got) if ref[k] != got[k])
    refused = sum(1 for k in set(ref) & set(got) if ref[k] == REFUSED == got[k])
    print(f"{same}/{len(ref)} templates as frozen ({refused} refused, as frozen); {bad} failing")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
