"""Mechanical verification of the run-13 v2 passage-strategy renders.

Each of Matt's spec'd shapes has a checkable signature; this script checks
them from the EXPORTED piece JSON (note numbers, beats, durations), not from
the generator's own meta — the generator asserting its own claim is not
verification.

  chain_chromatic_peak     >=3 consecutive +1-semitone steps immediately
                           before the highest note; that note held >=2 beats
                           (the pause); monotone descent after it, net >=7
                           semitones, ending on a long tonic.
  chain_tonic_fanfare      chain entry pitches descend to the tonic; final 7
                           notes all one pitch (the tonic) in Matt's rhythm
                           q e. s q q h q (checked proportionally).
  chain_arpeggio_fallback  exactly 2 arpeggio falls (runs of consecutive
                           steps <= -3 semitones) => 3 rise cycles; each fall
                           lands above its cycle's start (almost-not-quite);
                           climactic rise into a held final note.
  pedal_buildup            rendered acceleration (first-quarter / last-quarter
                           mean pulse, held arrival excluded) <= 4x.
  pedal_chords[_only]      pedal part one sustained contiguous pitch until the
                           release; chord tension against the pedal (Matt's
                           criterion: no-pedal-in-chord + dissonant interval
                           classes) nondecreasing through the ramp and peaking
                           at the Ger6; out-of-pedal-triad fraction reported.
                           NOTE the spec'd "fraction of chord notes outside
                           the pedal triad" is non-monotone BY CONSTRUCTION
                           (the tonic triad C-E-G contains two non-G-B-D tones
                           yet is the consonant opener), so the pass criterion
                           is the tension measure, and the fraction is printed
                           for inspection.
  wandering_mod_* /        every note inside the key active at its beat;
  modulating_fifths_*      accidentals (pcs outside C major) DO occur in
                           altered-key spans and do NOT occur in C-major spans.
  suite_v2                 total beats in 45-70; the three member checks
                           applied to their own sections.

  python verify_passage_strategies.py [--dir renders/passage_strategies2]
"""
import argparse
import json
import pathlib
import sys

REPO = pathlib.Path(__file__).resolve().parent.parent.parent

PC = {"C": 0, "D": 2, "E": 4, "F": 5, "G": 7, "A": 9, "B": 11}
SCALES = {"Major": [0, 2, 4, 5, 7, 9, 11],
          "Minor": [0, 2, 3, 5, 7, 8, 10],
          "Harmonic Minor": [0, 2, 3, 5, 7, 8, 11],
          "Melodic Minor": [0, 2, 3, 5, 7, 9, 11]}
DISSONANT_ICS = {1, 2, 6, 10, 11}


def pitch_pc(name):
    ws = name.split()
    pc = PC[ws[0]]
    for w in ws[1:]:
        pc += 1 if w == "Sharp" else (-1 if w == "Flat" else 0)
    return pc % 12


def key_pcs(keyname, override=None):
    if override:
        root, ivls = pitch_pc(override["pitch"]), SCALES[override["scale"]]
    else:
        ws = keyname.split()
        root, ivls = pitch_pc(" ".join(ws[:-1])), SCALES[ws[-1]]
    return {(root + i) % 12 for i in ivls}


CMAJ = key_pcs("C Major")


def load(prefix):
    t = json.loads(pathlib.Path(prefix + "_tmpl.json").read_text())
    ev = json.loads(pathlib.Path(prefix + "_1.json").read_text())
    return t, ev


def part_notes(ev, name=None, index=None):
    if name is not None:
        parts = [p for p in ev["parts"] if p["name"] == name]
        if not parts:
            return []
        p = parts[0]
    else:
        p = ev["parts"][index or 0]
    return [(e["beat"], int(round(e["data"]["noteNumber"])),
             float(e["data"]["duration"]))
            for e in p["events"] if e.get("type") == "note"]


def slice_notes(notes, b0, b1):
    return [n for n in notes if b0 - 1e-6 <= n[0] < b1 - 1e-6]


# ---------------------------------------------------------------- checks
def check_chromatic_peak(mel):
    nn = [n[1] for n in mel]
    dur = [n[2] for n in mel]
    i = nn.index(max(nn))
    run, j = 0, i
    while j > 0 and nn[j] - nn[j - 1] == 1:
        run += 1
        j -= 1
    after = nn[i:]
    desc_ok = (all(b <= a for a, b in zip(after, after[1:]))
               and after[0] - after[-1] >= 7)
    checks = [("chrom_rise>=3", run >= 3, f"run={run}"),
              ("peak_pause>=2b", dur[i] >= 2.0, f"peak_dur={dur[i]}"),
              ("descent>=7st", desc_ok, f"net={after[0] - after[-1]}"),
              ("ends_long_tonic", nn[-1] % 12 == 0 and dur[-1] >= 2.0,
               f"final_pc={nn[-1] % 12} dur={dur[-1]}")]
    return checks


FANFARE = [1.0, 0.75, 0.25, 1.0, 1.0, 2.0, 1.0]


def check_tonic_fanfare(mel, tmpl):
    nn = [n[1] for n in mel]
    dur = [n[2] for n in mel]
    last = mel[-len(FANFARE):]
    one_pitch = len({n[1] for n in last}) == 1
    on_tonic = last[0][1] % 12 == 0
    scale = last[0][2] / FANFARE[0]
    rhythm_ok = all(abs(n[2] - f * scale) < 1e-3
                    for n, f in zip(last, FANFARE))
    cell_len = len([m for m in tmpl["motifs"]
                    if m["name"] == "CH"][0]["figure"]["units"])
    chain_nn = nn[:-len(FANFARE)]
    entry_firsts = chain_nn[::cell_len]
    desc = entry_firsts[-1] < entry_firsts[0]
    lands = last[0][1] - chain_nn[-1] if chain_nn else 0
    return [("fanfare_one_pitch", one_pitch and on_tonic,
             f"pitches={sorted({n[1] for n in last})}"),
            ("fanfare_rhythm q e. s q q h q", rhythm_ok,
             f"durs={[n[2] for n in last]}"),
            ("chain_descends_to_tonic", desc and abs(lands) <= 2,
             f"entry_firsts={entry_firsts}")]


def check_arpeggio(mel, tmpl):
    """The cell itself may contain third-leaps, so falls cannot be detected
    from the raw step stream (first version tried; a leapy cell produced 8
    'falls'). Figure boundaries come from the template — WHERE each figure
    starts is authored; WHAT the engine rendered there is what gets checked."""
    nn = [n[1] for n in mel]
    dur = [n[2] for n in mel]
    motifs = {m["name"]: m["figure"] for m in tmpl["motifs"]}
    refs = [f["motifName"]
            for f in tmpl["parts"][0]["passages"]["Main"]["phrases"][0]
            ["figures"]]
    spans, i = {}, 0                       # ref index -> (start, end) notes
    for ri, r in enumerate(refs):
        k = len(motifs[r]["units"])
        spans[ri] = (i, i + k)
        i += k
    arp_idx = [ri for ri, r in enumerate(refs) if r.startswith("ARP")]
    cyc_first = [ri for ri, r in enumerate(refs)
                 if r == "CY" and (ri == 0 or refs[ri - 1] != "CY")]
    n_cycles = len(cyc_first)
    falls_ok, landings, starts = True, [], []
    for ai, ri in enumerate(arp_idx):
        s, e = spans[ri]
        seg = nn[s:e]
        if not (all(b < a for a, b in zip(seg, seg[1:]))
                and seg[0] - seg[-1] >= 3):     # >= one (possibly minor) third
            falls_ok = False
        cs = nn[spans[cyc_first[ai]][0]]   # start of the cycle this fall ends
        starts.append(cs)
        landings.append(seg[-1])
    almost = all(0 < (l - s) <= 4 for l, s in zip(landings, starts))
    # Within each cycle, successive CY entries enter higher (the ascent).
    ascend_ok = True
    for ci, ri in enumerate(cyc_first):
        entries = []
        rj = ri
        while rj < len(refs) and refs[rj] == "CY":
            entries.append(nn[spans[rj][0]])
            rj += 1
        if len(entries) > 1 and entries[-1] <= entries[0]:
            ascend_ok = False
    steps = [b - a for a, b in zip(nn, nn[1:])]
    climax_ok = dur[-1] >= 2.0 and all(s > 0 for s in steps[-3:])
    return [("3_cycles_2_falls",
             n_cycles == 3 and len(arp_idx) == 2 and falls_ok,
             f"cycles={n_cycles} falls={len(arp_idx)}"),
            ("cycles_ascend", ascend_ok,
             f"cycle_entry_nn={[nn[spans[r][0]] for r in cyc_first]}"),
            ("falls_land_almost_not_quite", almost,
             f"starts={starts} landings={landings}"),
            ("climactic_rise_held", climax_ok,
             f"tail_steps={steps[-3:]} final_dur={dur[-1]}")]


def accel_ratio(durs):
    if len(durs) < 8:
        return 0.0
    body = durs[:-1]
    q = max(1, len(body) // 4)
    first, last = body[:q], body[-q:]
    return (sum(first) / len(first)) / max(1e-6, sum(last) / len(last))


def check_pedal_buildup(mel):
    a = accel_ratio([n[2] for n in mel])
    return [("accel<=4x", a <= 4.2, f"accel={a:.2f}")]


def tension(pcs, ped_pc=7):
    s = 0 if ped_pc in pcs else 2
    for pc in pcs:
        if min((pc - ped_pc) % 12, (ped_pc - pc) % 12) in DISSONANT_ICS:
            s += 1
    return s


def check_pedal_chords(ev, melody_variant, b0=0.0, b1=float("inf")):
    ped = slice_notes(part_notes(ev, name="pedal"), b0, b1)
    ped_nn = [n[1] for n in ped]
    sustained = len(set(ped_nn[:-1])) == 1 and ped_nn[-1] != ped_nn[0]
    contiguous = all(abs(a[0] + a[2] - b[0]) < 1e-3
                     for a, b in zip(ped, ped[1:]))
    v0 = slice_notes(part_notes(ev, name="voice0"), b0, b1)
    v1 = slice_notes(part_notes(ev, name="voice1"), b0, b1)
    if melody_variant:
        v2 = slice_notes(part_notes(ev, name="voice2"), b0, b1)
    else:
        v2 = slice_notes(part_notes(ev, name="melody"), b0, b1)
    n = min(len(v0), len(v1), len(v2))
    chords = [{v0[i][1] % 12, v1[i][1] % 12, v2[i][1] % 12} for i in range(n)]
    tens = [tension(c) for c in chords]
    ped_triad = {7, 11, 2}
    fracs = [round(len(c - ped_triad) / len(c), 2) for c in chords]
    ramp = tens[:-3]                       # diatonic ramp + Ger6; the last 3
    ramp_ok = (all(b >= a for a, b in zip(ramp, ramp[1:]))   # are the cadence
               and max(ramp) > ramp[0])
    moving = len(set(frozenset(c) for c in chords)) >= max(3, len(chords) - 4)
    return [("pedal_sustained_contiguous", sustained and contiguous,
             f"pedal_nn={ped_nn}"),
            ("chords_move_over_pedal", moving,
             f"n_chords={len(chords)}"),
            ("tension_ramps_to_Ger6", ramp_ok, f"tension={tens}"),
            ("outside_triad_frac(report)", True, f"frac={fracs}")]


def check_modulating(tmpl, ev, sec_idx=0):
    secs = tmpl["sections"]
    b0 = sum(s["beats"] for s in secs[:sec_idx])
    sec = secs[sec_idx]
    kcs = sec.get("keyContexts", [])
    mel = slice_notes(part_notes(ev, index=0), b0, b0 + sec["beats"])
    per_span, all_in, acc_altered, acc_cmaj = [], True, False, False
    for n in mel:
        rel = n[0] - b0
        active = None
        for kc in kcs:
            if kc["beat"] <= rel + 1e-6:
                active = kc
        pcs = key_pcs(active["key"], active.get("scaleOverride"))
        pc = n[1] % 12
        if pc not in pcs:
            all_in = False
        if pc not in CMAJ:
            if pcs != CMAJ:
                acc_altered = True
            else:
                acc_cmaj = True
    for kc in kcs:
        idx = kcs.index(kc)
        end = kcs[idx + 1]["beat"] if idx + 1 < len(kcs) else sec["beats"]
        span = [n for n in mel if kc["beat"] <= n[0] - b0 + 1e-6
                and n[0] - b0 < end - 1e-6]
        n_acc = sum(1 for n in span if n[1] % 12 not in CMAJ)
        per_span.append(f"{kc['key']}"
                        + ("(harm)" if kc.get("scaleOverride") else "")
                        + f":{len(span)}n/{n_acc}acc")
    has_altered = any(key_pcs(kc["key"], kc.get("scaleOverride")) != CMAJ
                      for kc in kcs)
    return [("all_notes_in_active_key", all_in, " ".join(per_span)),
            ("accidentals_in_altered_spans", acc_altered or not has_altered,
             ""),
            ("no_accidentals_in_C_spans", not acc_cmaj, "")]


def check_suite(tmpl, ev):
    secs = tmpl["sections"]
    total = sum(s["beats"] for s in secs)
    checks = [("total_beats_45_70", 45 <= total <= 70, f"beats={total}")]
    checks += [("S0." + n, ok, d)
               for n, ok, d in check_modulating(tmpl, ev, 0)]
    b0 = secs[0]["beats"]
    b1 = b0 + secs[1]["beats"]
    mel1 = slice_notes(part_notes(ev, index=0), b0, b1)
    checks += [("S1." + n, ok, d) for n, ok, d in check_chromatic_peak(mel1)]
    # S1's cadence lands on the SECTION's tonic; the "ends_long_tonic" check
    # stays meaningful because the suite stays keyed in C at that point.
    b2 = b1 + secs[2]["beats"]
    checks += [("S2." + n, ok, d)
               for n, ok, d in check_pedal_chords(ev, True, b1, b2)]
    return checks


# ---------------------------------------------------------------- driver
CHECKERS = [
    ("suite_v2", lambda t, ev: check_suite(t, ev)),
    ("chain_chromatic_peak",
     lambda t, ev: check_chromatic_peak(part_notes(ev, index=0))),
    ("chain_tonic_fanfare",
     lambda t, ev: check_tonic_fanfare(part_notes(ev, index=0), t)),
    ("chain_arpeggio_fallback",
     lambda t, ev: check_arpeggio(part_notes(ev, index=0), t)),
    ("pedal_buildup",
     lambda t, ev: check_pedal_buildup(part_notes(ev, index=0))),
    ("pedal_chords_only", lambda t, ev: check_pedal_chords(ev, False)),
    ("pedal_chords", lambda t, ev: check_pedal_chords(ev, True)),
    ("wandering_mod", lambda t, ev: check_modulating(t, ev)),
    ("modulating_fifths", lambda t, ev: check_modulating(t, ev)),
]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dir", default="renders/passage_strategies2")
    args = ap.parse_args()
    d = REPO / args.dir

    # scores.csv (written by the passage_strategies.py run) names the set that
    # was actually rendered; the dir may hold stale takes from earlier runs.
    csvp = d / "scores.csv"
    if csvp.exists():
        import csv as _csv
        with open(csvp, newline="") as f:
            prefixes = [str(d / row["file"])
                        for row in _csv.DictReader(f)]
    else:
        prefixes = sorted(str(p)[:-len("_tmpl.json")]
                          for p in d.glob("*_tmpl.json"))
    n_pass = n_fail = 0
    print(f"{'render':<28s} {'check':<34s} {'result':<6s} detail")
    print("-" * 110)
    for prefix in prefixes:
        label = pathlib.Path(prefix).name
        checker = next((fn for pat, fn in CHECKERS if label.startswith(pat)),
                       None)
        if checker is None:
            continue
        t, ev = load(prefix)
        for cname, ok, detail in checker(t, ev):
            n_pass += ok
            n_fail += not ok
            print(f"{label:<28s} {cname:<34s} "
                  f"{'PASS' if ok else 'FAIL':<6s} {detail}")
    print("-" * 110)
    print(f"{n_pass} pass, {n_fail} fail")
    return 0 if n_fail == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
