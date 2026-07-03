# Phase 2 v1 — Combination Phrase from Markov Atoms — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A Python tool that samples 2 Markov figures, randomly combines them into a phrase (repeat + transform + placement), emits an engine template JSON, and renders it to WAV — so the combined phrase is hearable.

**Architecture:** The chooser lives in Python. The engine does the rest (figure stitching, instrument render) via the existing `mforce_cli --compose`. Python materializes every concrete figure (applying any transform itself) and injects each as a `userProvided` motif, so the engine receives plain figures + connectors. No C++ changes.

**Tech Stack:** Python 3.11 stdlib only. Existing `mforce_cli` binary. Reuses `markov_model.py` / `markov_generate.py` from Phase 1.

## Global Constraints

- **Python:** 3.11, **stdlib only** — no numpy, no pytest. Tests are plain `assert` scripts run via `python <file>`; a test "passes" when the script exits 0 and prints `OK`.
- **No C++ changes.** Use the existing binary: `build/tools/mforce_cli/Debug/mforce_cli.exe`.
- **Stock instrument patch:** `patches/Additive1.json` (verified to render audible audio, peak≈0.076).
- **Verified template format (current/working — `template_golden_phase1a.json`, NOT the stale `mary`):**
  - Top: `keyName`, `scaleName`, `bpm`, `masterSeed`.
  - `motifs[]`: explicit figure = `{"name": "...", "figure": {"units": [{"duration": f, "step": i}]}, "userProvided": true}`.
  - `parts[].passages[Section]`: `{"startingPitch": {...}, "phrases": [...]}`.
  - phrase: `{"name","startingPitch","figures":[...],"connectors":[...]}`.
  - figure ref: `{"source":"reference","motifName":"A"}`.
  - `connectors[]` is parallel to `figures[]`; each entry is `null` (default) | integer (`leadStep` shorthand) | `{"elide":N,"adjust":beats,"leadStep":S}`. `connectors[0]` is the lead-in to `figures[0]` → always `null`.
- **CLI invocation:** `mforce_cli --compose <patch.json> <out_prefix> <count> --template <template.json>` writes `<out_prefix>_<n>.wav` and `<out_prefix>_<n>.json` (event dump: `parts[0].events[].data.noteNumber`).
- **Engine octave convention:** `{"octave":4,"pitch":"C"}` realizes to MIDI 48. **Compare pitches RELATIVE** (subtract the first note) so the convention is irrelevant.
- **Figure representation (Python):** a figure is `{"units": [{"duration": float, "step": int}, ...]}` with `units[0].step == 0` (anchor). `net_step = sum of all steps`.
- **Reproducibility:** seed every RNG; store the seed in each emitted template's `masterSeed` and in a sidecar log (CLAUDE.md non-negotiable).
- **Commits:** this repo commits **only when Matt asks** (two Claudes share the tree). Treat each "Commit" step as a checkpoint to request approval, not an auto-run.
- **All new files** under `corpus/mtd_seg/`.

---

### Task 1: Figure helpers — net_step, invert, retrograde

**Files:**
- Create: `corpus/mtd_seg/markov_phrase.py`
- Test: `corpus/mtd_seg/test_markov_phrase.py`

**Interfaces:**
- Produces: `net_step(fig: dict) -> int`, `invert(fig: dict) -> dict`, `retrograde(fig: dict) -> dict`. `fig` is `{"units":[{"duration":float,"step":int}]}`. All preserve `units[0].step == 0`.

- [ ] **Step 1: Write the failing test**

```python
# test_markov_phrase.py
from markov_phrase import net_step, invert, retrograde

A = {"units": [{"duration":0.5,"step":0},{"duration":0.5,"step":1},
               {"duration":0.5,"step":1},{"duration":0.5,"step":-1}]}

def test_net_step():
    assert net_step(A) == 1

def test_invert():
    inv = invert(A)
    assert [u["step"] for u in inv["units"]] == [0,-1,-1,1]
    assert [u["duration"] for u in inv["units"]] == [0.5,0.5,0.5,0.5]
    assert net_step(inv) == -1

def test_retrograde():
    r = retrograde(A)
    # degrees [0,1,2,1] reversed+re-anchored -> [0,1,0,-1]
    assert [u["step"] for u in r["units"]] == [0,1,-1,-1]
    assert net_step(r) == -1

if __name__ == "__main__":
    test_net_step(); test_invert(); test_retrograde()
    print("OK")
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cd corpus/mtd_seg && python test_markov_phrase.py`
Expected: FAIL — `ImportError: cannot import name 'net_step' from 'markov_phrase'`

- [ ] **Step 3: Write minimal implementation**

```python
# markov_phrase.py
"""Combine 2 Markov figures into a phrase template + render to WAV. Phase 2 v1."""

def net_step(fig):
    return sum(u["step"] for u in fig["units"])

def invert(fig):
    return {"units": [{"duration": u["duration"], "step": -u["step"]}
                      for u in fig["units"]]}

def retrograde(fig):
    units = fig["units"]; n = len(units)
    durs = [u["duration"] for u in units]
    deg, c = [], 0
    for u in units:
        c += u["step"]; deg.append(c)           # cumulative degrees, deg[0]=0
    rdurs = durs[::-1]
    rdeg = deg[::-1]
    base = rdeg[0]
    rdeg = [d - base for d in rdeg]              # re-anchor so rdeg[0]=0
    rsteps = [0] + [rdeg[i] - rdeg[i-1] for i in range(1, n)]
    return {"units": [{"duration": rdurs[i], "step": rsteps[i]} for i in range(n)]}
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cd corpus/mtd_seg && python test_markov_phrase.py`
Expected: prints `OK`, exit 0.

- [ ] **Step 5: Commit (checkpoint — ask Matt)**

```bash
git -C /c/@dev/repos/mforce add corpus/mtd_seg/markov_phrase.py corpus/mtd_seg/test_markov_phrase.py
git -C /c/@dev/repos/mforce commit -m "feat(phrase): figure helpers net_step/invert/retrograde"
```

---

### Task 2: Combination chooser — pattern, transform, placement → figures + connectors

**Files:**
- Modify: `corpus/mtd_seg/markov_phrase.py`
- Test: `corpus/mtd_seg/test_markov_phrase.py`

**Interfaces:**
- Consumes: `net_step`, `invert`, `retrograde` (Task 1).
- Produces:
  - `PATTERNS: list[str]` — tokens over `A`, `B`, `P` (P = transformed A, "prime").
  - `build_combination(figA, figB, pattern, transform, placement) -> (motifs, refs, connectors)` where
    `motifs: dict[str,dict]` (name→figure for the distinct concrete figures, names `"A"`,`"B"`,`"P"`),
    `refs: list[str]` (motif name per phrase position),
    `connectors: list` (parallel to refs; `connectors[0] is None`; later entries are int `leadStep`).
  - `transform ∈ {"invert","retrograde"}`, `placement ∈ {"same","climb","sequence"}`.

- [ ] **Step 1: Write the failing test**

```python
from markov_phrase import build_combination, PATTERNS

def test_pattern_set_nonempty():
    assert "AAAB" in PATTERNS and "AB" in PATTERNS

def test_aaab_same_note():
    motifs, refs, conns = build_combination(A, B, "AAAB", "invert", "same")
    assert refs == ["A","A","A","B"]
    # same-note repeats: leadStep = -net_step(A) = -1 before each repeated A; B seam = 0
    assert conns == [None, -1, -1, 0]
    assert set(motifs) == {"A","B"}            # no prime used in AAAB

def test_prime_pattern_uses_transform():
    motifs, refs, conns = build_combination(A, B, "AAA'B", "retrograde", "climb")
    assert refs == ["A","A","P","B"]
    assert "P" in motifs                        # P is retrograde(A)
    assert [u["step"] for u in motifs["P"]["units"]] == [0,1,-1,-1]
    # climb placement -> repeated-A seam leadStep 0; into P (different motif) -> 0; into B -> 0
    assert conns == [None, 0, 0, 0]

if __name__ == "__main__":
    test_net_step(); test_invert(); test_retrograde()
    test_pattern_set_nonempty(); test_aaab_same_note(); test_prime_pattern_uses_transform()
    print("OK")
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cd corpus/mtd_seg && python test_markov_phrase.py`
Expected: FAIL — `ImportError: cannot import name 'build_combination'`

- [ ] **Step 3: Write minimal implementation**

```python
# markov_phrase.py  (append)
PATTERNS = ["AB", "AAB", "AAAB", "ABAB", "AABB", "AAA'B", "ABA'B"]

def _tokens(pattern):
    # "AAA'B" -> ["A","A","P","B"]  (apostrophe turns the PRECEDING A into prime P)
    out = []
    for ch in pattern:
        if ch == "'":
            out[-1] = "P"
        else:
            out.append(ch)
    return out

def build_combination(figA, figB, pattern, transform, placement):
    refs = _tokens(pattern)
    motifs = {"A": figA, "B": figB}
    if "P" in refs:
        motifs["P"] = invert(figA) if transform == "invert" else retrograde(figA)
    connectors = [None]
    for i in range(1, len(refs)):
        prev = motifs[refs[i-1]]
        if refs[i] == refs[i-1] and refs[i] == "A":     # repeated-A seam: apply placement
            if placement == "same":
                connectors.append(-net_step(prev))
            elif placement == "sequence":
                connectors.append(-net_step(prev) + 2)
            else:                                        # climb
                connectors.append(0)
        else:
            connectors.append(0)                         # into a different motif: continue
    return motifs, refs, connectors
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cd corpus/mtd_seg && python test_markov_phrase.py`
Expected: prints `OK`.

- [ ] **Step 5: Commit (checkpoint — ask Matt)**

```bash
git -C /c/@dev/repos/mforce add corpus/mtd_seg/markov_phrase.py corpus/mtd_seg/test_markov_phrase.py
git -C /c/@dev/repos/mforce commit -m "feat(phrase): combination chooser -> figures + connectors"
```

---

### Task 3: Template emitter

**Files:**
- Modify: `corpus/mtd_seg/markov_phrase.py`
- Test: `corpus/mtd_seg/test_markov_phrase.py`

**Interfaces:**
- Consumes: `build_combination` output (Task 2).
- Produces: `make_template(motifs, refs, connectors, *, key="C", scale="Major", bpm=84.0, seed=1, start=("C",4)) -> dict` — a full template dict in the verified format. Section beats = sum of all figure durations across `refs`.

- [ ] **Step 1: Write the failing test**

```python
from markov_phrase import make_template

def test_template_structure():
    motifs, refs, conns = build_combination(A, B, "AAB", "invert", "same")
    t = make_template(motifs, refs, conns, key="C", scale="Major", bpm=84.0, seed=7)
    names = {m["name"] for m in t["motifs"]}
    assert names == {"A","B"}
    assert all(m.get("userProvided") for m in t["motifs"])
    ph = t["parts"][0]["passages"]["Main"]["phrases"][0]
    assert [f["motifName"] for f in ph["figures"]] == ["A","A","B"]
    assert len(ph["connectors"]) == len(ph["figures"])
    assert ph["connectors"][0] is None
    assert t["masterSeed"] == 7
    # section beats == total durations: A=2.0 + A=2.0 + B(A's len here is 2.0)... computed
    total = sum(u["duration"] for r in refs for u in motifs[r]["units"])
    assert t["sections"][0]["beats"] == total

if __name__ == "__main__":
    # ... (all prior tests) ...
    test_template_structure()
    print("OK")
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cd corpus/mtd_seg && python test_markov_phrase.py`
Expected: FAIL — `ImportError: cannot import name 'make_template'`

- [ ] **Step 3: Write minimal implementation**

```python
# markov_phrase.py  (append)
def make_template(motifs, refs, connectors, *, key="C", scale="Major",
                  bpm=84.0, seed=1, start=("C", 4)):
    total = sum(u["duration"] for r in refs for u in motifs[r]["units"])
    figures = [{"source": "reference", "motifName": r} for r in refs]
    conns = [None if c is None else int(c) for c in connectors]
    return {
        "keyName": key, "scaleName": scale, "bpm": bpm, "masterSeed": seed,
        "motifs": [{"name": name, "figure": fig, "userProvided": True}
                   for name, fig in motifs.items()],
        "sections": [{"name": "Main", "beats": total}],
        "parts": [{
            "name": "melody", "role": "melody",
            "passages": {"Main": {
                "startingPitch": {"octave": start[1], "pitch": start[0]},
                "phrases": [{
                    "name": "P1",
                    "startingPitch": {"octave": start[1], "pitch": start[0]},
                    "figures": figures,
                    "connectors": conns,
                }],
            }},
        }],
    }
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cd corpus/mtd_seg && python test_markov_phrase.py`
Expected: prints `OK`.

- [ ] **Step 5: Commit (checkpoint — ask Matt)**

```bash
git -C /c/@dev/repos/mforce add corpus/mtd_seg/markov_phrase.py corpus/mtd_seg/test_markov_phrase.py
git -C /c/@dev/repos/mforce commit -m "feat(phrase): template emitter (verified motif/connector format)"
```

---

### Task 4: Render + pitch-verification harness (closes the loop)

**Files:**
- Modify: `corpus/mtd_seg/markov_phrase.py`
- Test: `corpus/mtd_seg/test_markov_phrase_render.py` (separate — it shells out to the engine)

**Interfaces:**
- Consumes: `make_template` (Task 3).
- Produces:
  - `degree_to_semitone(d: int) -> int` — major-scale degree → semitone offset.
  - `predict_relative_semitones(motifs, refs, connectors) -> list[int]` — Python's prediction of the realized pitch contour, relative to the first note, in semitones.
  - `render_template(template: dict, out_prefix: str) -> list[int]` — writes template to `<out_prefix>.json`, runs the CLI, returns realized note numbers from the event dump.

- [ ] **Step 1: Write the failing test**

```python
# test_markov_phrase_render.py
import os, pathlib
from markov_phrase import (build_combination, make_template,
                           predict_relative_semitones, render_template)

A = {"units": [{"duration":0.5,"step":0},{"duration":0.5,"step":1},
               {"duration":0.5,"step":1},{"duration":0.5,"step":-1}]}
B = {"units": [{"duration":1.0,"step":0},{"duration":1.0,"step":2}]}

def test_engine_matches_prediction():
    motifs, refs, conns = build_combination(A, B, "AAB", "invert", "same")
    t = make_template(motifs, refs, conns, seed=1)
    predicted = predict_relative_semitones(motifs, refs, conns)
    notes = render_template(t, "renders/_phrase_test")
    realized = [n - notes[0] for n in notes]
    assert realized == predicted, f"realized {realized} != predicted {predicted}"

if __name__ == "__main__":
    test_engine_matches_prediction()
    print("OK")
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cd corpus/mtd_seg && python test_markov_phrase_render.py`
Expected: FAIL — `ImportError: cannot import name 'predict_relative_semitones'`

- [ ] **Step 3: Write minimal implementation**

```python
# markov_phrase.py  (append)
import json, pathlib, subprocess

REPO = pathlib.Path(__file__).resolve().parent.parent.parent
CLI  = REPO / "build/tools/mforce_cli/Debug/mforce_cli.exe"
PATCH = REPO / "patches/Additive1.json"
MAJOR = [0, 2, 4, 5, 7, 9, 11]

def degree_to_semitone(d):
    octs, deg = divmod(d, 7)
    return 12 * octs + MAJOR[deg]

def predict_relative_semitones(motifs, refs, connectors):
    """Replay the engine's cursor math in scale-degrees, return semitones rel to first note."""
    degs, cursor = [], 0          # cursor = degree of previous note
    first = True
    for i, name in enumerate(refs):
        fig = motifs[name]
        lead = 0 if connectors[i] is None else connectors[i]
        anchor = 0 if first else cursor + lead   # first figure anchors at 0
        first = False
        d = 0
        for u in fig["units"]:
            d += u["step"]                       # degree within figure (unit0 step==0)
            degs.append(anchor + d)
        cursor = anchor + d                       # land on last note's degree
    semis = [degree_to_semitone(x) for x in degs]
    return [s - semis[0] for s in semis]

def render_template(template, out_prefix):
    tpath = REPO / (out_prefix + "_tmpl.json")    # the input template we author
    tpath.write_text(json.dumps(template), encoding="utf-8")
    subprocess.run([str(CLI), "--compose", str(PATCH), str(REPO / out_prefix), "1",
                    "--template", str(tpath)], check=True, capture_output=True)
    ev = json.loads((REPO / (out_prefix + "_1.json")).read_text(encoding="utf-8"))
    return [int(e["data"]["noteNumber"]) for e in ev["parts"][0]["events"]]
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cd corpus/mtd_seg && python test_markov_phrase_render.py`
Expected: prints `OK`. (Proves Python's connector/anchor math matches the engine's realized pitches.)

- [ ] **Step 5: Commit (checkpoint — ask Matt)**

```bash
git -C /c/@dev/repos/mforce add corpus/mtd_seg/markov_phrase.py corpus/mtd_seg/test_markov_phrase_render.py
git -C /c/@dev/repos/mforce commit -m "test(phrase): engine render matches predicted pitch contour"
```

---

### Task 5: CLI driver — sample 2 figures, roll a combination, render batch

**Files:**
- Modify: `corpus/mtd_seg/markov_phrase.py`

**Interfaces:**
- Consumes: `MarkovModel.load` + `gen_model` (from `markov_model` / `markov_generate`), all of Tasks 1–4.
- Produces: `main()` CLI — `python markov_phrase.py --n K --seed S [--bpm B]` writes K phrase WAVs to `renders/markov_phrases/` and a log line per phrase (pattern/transform/placement).

- [ ] **Step 1: Write the failing test** (smoke, asserts files appear)

```python
# append to test_markov_phrase_render.py
def test_batch_smoke():
    import subprocess, glob, pathlib
    REPO = pathlib.Path(__file__).resolve().parent.parent.parent
    subprocess.run(["python", "markov_phrase.py", "--n", "2", "--seed", "5"], check=True)
    wavs = glob.glob(str(REPO / "renders/markov_phrases/*.wav"))
    assert len(wavs) >= 2
```
(Add `test_batch_smoke()` to the `__main__` block.)

- [ ] **Step 2: Run test to verify it fails**

Run: `cd corpus/mtd_seg && python test_markov_phrase_render.py`
Expected: FAIL — `--n` unrecognized / no `main` (SystemExit or AssertionError).

- [ ] **Step 3: Write minimal implementation**

```python
# markov_phrase.py  (append)
import argparse, random
from markov_model import MarkovModel
from markov_generate import gen_model

def _sample_figure(model, rng, kmin=3, kmax=5):
    k = rng.randint(kmin, kmax)
    step, pulse = gen_model(model, k, rng)
    return {"units": [{"duration": float(pulse[i]), "step": int(step[i])}
                      for i in range(len(step))]}

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--n", type=int, default=8)
    ap.add_argument("--seed", type=int, default=1234)
    ap.add_argument("--bpm", type=float, default=84.0)
    args = ap.parse_args()

    rng = random.Random(args.seed)
    model = MarkovModel.load()
    outdir = REPO / "renders/markov_phrases"
    outdir.mkdir(parents=True, exist_ok=True)

    for i in range(args.n):
        figA = _sample_figure(model, rng)
        figB = _sample_figure(model, rng)            # independent (contrast = TODO)
        pattern   = rng.choice(PATTERNS)
        transform = rng.choice(["invert", "retrograde"])
        placement = rng.choice(["same", "climb", "sequence"])
        motifs, refs, conns = build_combination(figA, figB, pattern, transform, placement)
        seed_i = args.seed * 1000 + i
        t = make_template(motifs, refs, conns, bpm=args.bpm, seed=seed_i)
        prefix = f"renders/markov_phrases/p{i:02d}_{pattern.replace(chr(39),'x')}"
        notes = render_template(t, prefix)
        print(f"p{i:02d}: {pattern:6} t={transform:9} place={placement:8} "
              f"{len(refs)} figs, {len(notes)} notes -> {prefix}_1.wav")

if __name__ == "__main__":
    main()
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cd corpus/mtd_seg && python test_markov_phrase_render.py`
Expected: prints `OK`; `renders/markov_phrases/` contains ≥2 WAVs.

- [ ] **Step 5: Generate the audition batch + Commit (checkpoint — ask Matt)**

```bash
cd corpus/mtd_seg && python markov_phrase.py --n 12 --seed 1234
```
Then (on Matt's go):
```bash
git -C /c/@dev/repos/mforce add corpus/mtd_seg/markov_phrase.py corpus/mtd_seg/test_markov_phrase_render.py
git -C /c/@dev/repos/mforce commit -m "feat(phrase): batch combination-phrase renderer"
```

**Deliverable:** ~12 WAV phrases in `renders/markov_phrases/`, each a 2-figure combination with pattern/transform/placement logged. Audition in `mforce_ui` (point the audition folder there). Then the decision gate from the spec: do the combinations sound musical?

---

## Notes for the implementer

- **WAV format for UI audition:** `mforce_cli` writes 16-bit stereo via `write_wav_16le_stereo`. Confirm its sample rate matches the UI device (48 kHz). If the audition panel won't load them, that's a format mismatch to resolve (resample or adjust), not a logic bug.
- **If `predict_relative_semitones` disagrees with the engine (Task 4):** do NOT patch the test to match. The mismatch means the engine's connector/anchor semantics differ from the spec's model — stop and re-derive the bridging rule against `composer.h` realization, because that rule is the whole point of the placement axis. (memory: `arch_fc_and_step_zero`.)
- **Composition tier is mid-refactor** in this working copy (uncommitted edits to `composer.h`, `templates.h`, `templates_json.h`, `default_strategies.h` by the other Claude). The verified format above is current as of 2026-06-22; if a render suddenly returns 0 events, re-check the template format against a freshly-working shipped template before assuming a Python bug.
```
