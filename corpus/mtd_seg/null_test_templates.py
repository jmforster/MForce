"""Template-side null test — render every renderable template in patches/ and
hash the WAVs, so a composer change can state its blast radius by measurement.

Run 18 did this by hand (38/38 byte-identical) and so did run 15; it is the
standard evidence for "this composer edit is a no-op for existing templates",
so it belongs in a script instead of being re-derived every run.

Usage:
    python corpus/mtd_seg/null_test_templates.py <out_dir>      # render + hash
    python corpus/mtd_seg/null_test_templates.py --cmp A B      # compare two

Typical sequence for an engine change:
    python ... null_test_templates.py after
    git stash / checkout the edited headers ; rebuild
    python ... null_test_templates.py before
    restore ; rebuild
    python ... null_test_templates.py --cmp before after
"""
import hashlib
import json
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
CLI = REPO / "build/tools/mforce_cli/Release/mforce_cli.exe"
PATCH = REPO / "patches/Additive1.json"
SCRATCH = REPO / "renders/null_test_templates"


def templates():
    """A template is a JSON with both `sections` and `parts` at top level."""
    out = []
    for p in sorted((REPO / "patches").rglob("*.json")):
        try:
            j = json.loads(p.read_text(encoding="utf-8"))
        except Exception:
            continue
        if isinstance(j, dict) and "sections" in j and "parts" in j:
            out.append(p)
    return out


def render_all(tag):
    outdir = SCRATCH / tag
    outdir.mkdir(parents=True, exist_ok=True)
    hashes, failed = {}, {}
    for t in templates():
        name = t.stem
        prefix = outdir / name
        p = subprocess.run([str(CLI), "--compose", str(PATCH), str(prefix),
                            "1", "--template", str(t)],
                           capture_output=True, text=True)
        wav = Path(str(prefix) + "_1.wav")
        if p.returncode != 0 or not wav.exists():
            err = (p.stderr or p.stdout).strip().splitlines()
            failed[name] = err[-1] if err else f"rc={p.returncode}"
            continue
        hashes[name] = hashlib.sha256(wav.read_bytes()).hexdigest()
    (outdir / "_hashes.json").write_text(
        json.dumps({"hashes": hashes, "failed": failed}, indent=1),
        encoding="utf-8")
    print(f"[{tag}] rendered {len(hashes)}, unrenderable {len(failed)}")
    for k, v in sorted(failed.items()):
        print(f"    SKIP {k}: {v}")
    return hashes, failed


def compare(a, b):
    ja = json.loads((SCRATCH / a / "_hashes.json").read_text(encoding="utf-8"))
    jb = json.loads((SCRATCH / b / "_hashes.json").read_text(encoding="utf-8"))
    ha, hb = ja["hashes"], jb["hashes"]
    common = sorted(set(ha) & set(hb))
    same = [k for k in common if ha[k] == hb[k]]
    diff = [k for k in common if ha[k] != hb[k]]
    print(f"common {len(common)}: {len(same)} identical, {len(diff)} DIFFER")
    for k in diff:
        print(f"    DIFF {k}")
    only_a = sorted(set(ha) - set(hb))
    only_b = sorted(set(hb) - set(ha))
    if only_a:
        print(f"  renderable only in {a}: {only_a}")
    if only_b:
        print(f"  renderable only in {b}: {only_b}")
    return len(diff) == 0 and not only_a and not only_b


if __name__ == "__main__":
    if len(sys.argv) >= 4 and sys.argv[1] == "--cmp":
        sys.exit(0 if compare(sys.argv[2], sys.argv[3]) else 1)
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(2)
    render_all(sys.argv[1])
