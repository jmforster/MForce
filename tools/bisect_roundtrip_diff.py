"""Bisect WHICH node's re-serialization breaks a patch's roundtrip render
(backlog 3n). For each known-diff patch: roundtrip via mforce_ui, then for
each differing node (and differing root key), restore the ORIGINAL version
of just that piece into the roundtripped JSON and re-render. A restoration
that makes the render byte-identical names the culprit; patches whose
mismatch needs multiple simultaneous restorations are reported as such.

Usage: python tools/bisect_roundtrip_diff.py [patch ...]
       (no args = the KNOWN_DIFFS list from test_stable_roundtrip.py)
"""
import hashlib, json, re, subprocess, sys, tempfile
from pathlib import Path

UI  = str(Path("build/tools/mforce_ui/Release/mforce_ui.exe").resolve())
CLI = str(Path("build/tools/mforce_cli/Release/mforce_cli.exe").resolve())

def render_sha(patch, wav):
    r = subprocess.run([CLI, str(patch), str(wav)], capture_output=True, text=True)
    if r.returncode != 0:
        return None
    return hashlib.sha256(Path(wav).read_bytes()).hexdigest()

def main():
    if len(sys.argv) > 1:
        targets = sys.argv[1:]
    else:
        src = open("tools/test_stable_roundtrip.py", encoding="utf-8").read()
        targets = sorted(set(re.findall(r'"(patches/[^"]+)"', src)))

    for pt in targets:
        orig = json.load(open(pt, encoding="utf-8"))
        with tempfile.TemporaryDirectory() as td:
            td = Path(td)
            rt_path = td / "rt.json"
            r = subprocess.run([UI, "--roundtrip", pt, str(rt_path)],
                               capture_output=True, text=True)
            if r.returncode != 0:
                print(f"{pt}: ROUNDTRIP FAIL {r.stderr.strip()}")
                continue
            rt = json.load(open(rt_path, encoding="utf-8"))

            sha_orig = render_sha(pt, td / "o.wav")
            sha_rt   = render_sha(rt_path, td / "r.wav")
            if sha_orig is None:
                print(f"{pt}: original UNRENDERABLE"); continue
            if sha_orig == sha_rt:
                print(f"{pt}: CLEAN (already faithful)"); continue

            na = {n["id"]: n for n in orig["graph"]["nodes"]}
            nb = {n["id"]: n for n in rt["graph"]["nodes"]}
            # candidate restorations: (label, apply_fn)
            cands = []
            for k in sorted(set(na) | set(nb)):
                if na.get(k) != nb.get(k):
                    def make(nid):
                        def apply(doc):
                            nodes = [n for n in doc["graph"]["nodes"] if n["id"] != nid]
                            if nid in na:
                                nodes.append(na[nid])
                            doc["graph"]["nodes"] = nodes
                        return apply
                    cands.append((f"node:{k}", make(k)))
            for k in sorted(set(orig) | set(rt)):
                if k != "graph" and orig.get(k) != rt.get(k):
                    def make_root(key):
                        def apply(doc):
                            if key in orig: doc[key] = orig[key]
                            else: doc.pop(key, None)
                        return apply
                    cands.append((f"root:{k}", make_root(k)))

            fixes = []
            for label, apply in cands:
                doc = json.loads(json.dumps(rt))
                apply(doc)
                hp = td / "h.json"
                hp.write_text(json.dumps(doc), encoding="utf-8")
                if render_sha(hp, td / "h.wav") == sha_orig:
                    fixes.append(label)
            if fixes:
                print(f"{pt}: single-restoration fixes -> {', '.join(fixes)}")
            else:
                # try all-nodes-at-once to separate node-species from root keys
                doc = json.loads(json.dumps(rt))
                doc["graph"]["nodes"] = orig["graph"]["nodes"]
                hp = td / "h.json"
                hp.write_text(json.dumps(doc), encoding="utf-8")
                allnodes = render_sha(hp, td / "h.wav") == sha_orig
                print(f"{pt}: MULTI ({len(cands)} diffs; all-nodes-restored "
                      f"{'fixes' if allnodes else 'does NOT fix'})")

if __name__ == "__main__":
    sys.exit(main())
