"""Comp #9 A/B — the PAC held-note workaround, measured.

A Cadential phrase's ARRIVAL figure is forced to FigureShape::HeldNote (a
single long note). That was a 2026-04 workaround for arrivals that were only
brushed in passing. apply_cadence has since grown a real tail rebuild
(build_approach_steps + settle_tail), and HeldNote defeats it: with one unit
to work with, build_approach_steps takes its FAR branch and puts the whole
distance in one step. The arrival is sustained but LEAPT ONTO.

`PhraseTemplate.cadentialArrival: "approach"` hands the rebuild a real figure
instead. This measures both arms on the two templates in patches/ that use a
cadential phrase:

  * does the arrival land on the cadence target degree (both should);
  * the INTERVAL into the final note — the leap the held arm is accused of;
  * the final note's duration — the sustain the held arm exists to protect.

Which one resolves better is a taste question and goes to REVIEW, not here.

Usage:  python corpus/mtd_seg/cadential_arrival_ab.py
"""
import json
import subprocess
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
CLI = REPO / "build/tools/mforce_cli/Release/mforce_cli.exe"
PATCH = REPO / "patches/baselines/Additive1.json"
OUT = REPO / "renders/cadential_arrival_ab"

TEMPLATES = ["template_golden_phase1a", "template_shaped_test"]


def variant(tmpl, arrival):
    """Set cadentialArrival on every phrase whose function is cadential."""
    j = json.loads((REPO / "patches" / f"{tmpl}.json").read_text(encoding="utf-8"))
    touched = 0
    for part in j.get("parts", []):
        for passage in part.get("passages", {}).values():
            for ph in passage.get("phrases", []):
                if ph.get("function") == "cadential":
                    if arrival:
                        ph["cadentialArrival"] = arrival
                    touched += 1
    return j, touched


def render(name, template):
    OUT.mkdir(parents=True, exist_ok=True)
    tpath = OUT / f"{name}_tmpl.json"
    tpath.write_text(json.dumps(template, indent=1), encoding="utf-8")
    p = subprocess.run([str(CLI), "--compose", str(PATCH), str(OUT / name),
                        "1", "--template", str(tpath)],
                       capture_output=True, text=True)
    if p.returncode != 0:
        raise RuntimeError(f"{name}: CLI failed\n{p.stdout}\n{p.stderr}")


def events(name):
    j = json.loads((OUT / f"{name}_1.json").read_text(encoding="utf-8"))
    for part in j.get("parts", []):
        if part.get("events"):
            return part["events"]
    return []


DEG_OF_PC = {0: 0, 2: 1, 4: 2, 5: 3, 7: 4, 9: 5, 11: 6}  # major, relative to key


def measure(name, key_pc):
    evs = events(name)
    if len(evs) < 2:
        return None
    last, prev = evs[-1], evs[-2]
    nn = float(last["data"]["noteNumber"])
    pn = float(prev["data"]["noteNumber"])
    deg = DEG_OF_PC.get(int(round(nn - key_pc)) % 12)
    return {
        "notes": len(evs),
        "finalNN": nn,
        "finalDeg": deg,
        "approachInterval": nn - pn,
        "finalDur": float(last["data"]["duration"]),
        "prevDur": float(prev["data"]["duration"]),
    }


KEY_PC = {"template_golden_phase1a": 7, "template_shaped_test": 7}  # both in G


def main():
    rows = []
    for t in TEMPLATES:
        for arm, arrival in (("held", None), ("approach", "approach")):
            tmpl, touched = variant(t, arrival)
            name = f"{t}_{arm}"
            render(name, tmpl)
            m = measure(name, KEY_PC[t])
            rows.append((t, arm, touched, m))

    print(f"{'template':<28}{'arm':<10}{'cadPhr':>7}{'notes':>7}"
          f"{'finalNN':>9}{'deg':>5}{'intoFinal':>11}{'finalDur':>10}")
    for t, arm, touched, m in rows:
        if m is None:
            print(f"{t:<28}{arm:<10}{touched:>7}   NO EVENTS")
            continue
        print(f"{t:<28}{arm:<10}{touched:>7}{m['notes']:>7}"
              f"{m['finalNN']:>9.1f}{str(m['finalDeg']):>5}"
              f"{m['approachInterval']:>+11.1f}{m['finalDur']:>10.2f}")


if __name__ == "__main__":
    main()
