# Corpus survey — melody sources beyond MTD (comp backlog #3)

Wolfie, 2026-07-28. Survey + acquisition notes for expanding the Markov /
next-note training base beyond the MTD (2,067 classical B&M themes,
monophonic, CC BY 4.0). Goal: more atoms, and *stylistic breadth* (MTD is
classical-only). No downloads yet — this scopes which 1–2 to ingest.

## Ingest pipeline reality (what "low-friction" means here)

The existing loader is `prep_groundtruth.parse_midi` → `(tick, pitch, dur)`
note events; `score_generated.py` reads `*_score.mid`. So **any corpus that
is (or converts to) monophonic MIDI drops straight in.** Two cost axes:
format→MIDI conversion, and melody-extraction (if polyphonic).

## Candidates

| Corpus | Size | Idiom | Format | License | Monophonic? | Ingest cost |
|---|---|---|---|---|---|---|
| **Essen Folksong** | ~6,255 | folk (mostly German + Euro/world) | Humdrum \*\*kern (also EsAC) | **"distributed by license only"** — restrictive, needs check | **Yes** (single line) | Med: kern→MIDI (`hum2mid`) or kern parser; no melody extraction |
| **Nottingham (Jukedeck clean)** | ~1,000+ | British folk/dance | ABC + **MIDI provided** + cleaned ABC | **GPLv3** (clear) | No — melody + chords/walking bass | Low-ish: MIDI ready, but need top-line melody extraction |
| **KernScores (Stanford/CCARH)** | 100k+ files, 7.8M notes | mostly classical/vocal | \*\*kern | CC (per-collection varies) | Mixed (much is polyphonic) | Med–High: idiom overlaps MTD; polyphony |
| **Nottingham (raw, ifdo.ca/abc.sf)** | ~1,000 | British folk/dance | ABC | unstated | No (chords) | Med: ABC→MIDI + extraction |

## Read

- **Essen** is the highest-value single add: ~3× MTD's size, *monophonic by
  construction* (no melody extraction), and a genuinely different idiom
  (folk vs classical theme) — exactly the stylistic breadth G1 wants. The
  catch is licensing: the Humdrum distribution is marked "protected by
  copyright, distributed by license only." A derived *statistical model*
  (Markov transition counts) is very unlikely to be a problem, but this is
  a licensing judgement, not mine to make unilaterally — flagged for Matt.
- **Nottingham-Jukedeck** is the lowest-friction: GPLv3 is unambiguous for
  internal research use, and MIDI is already in the repo, so no format
  conversion. Cost is melody extraction — the tunes are melody+chords
  (walking-bass sequences were stripped in the cleaned set, chords
  standardized), so ingest = "take the melody track / highest voice."
  Smaller (~1k) and British-folk-narrow, but a fast, clean first expansion.
- **KernScores-classical** is deprioritized: idiom overlaps MTD (both
  classical), so low marginal stylistic value, and much is polyphonic.
  Revisit only if we want *more classical* specifically.

## Recommendation (order to ingest)

1. **Nottingham-Jukedeck first** — fastest real win. GPLv3, MIDI ready,
   drops into `parse_midi` after a melody-track filter. Delivers a folk
   Markov model to stand up the bake-off (backlog #4) against MTD-classical:
   two idioms, same harness, is the cleanest first comparison.
2. **Essen second** — the big stylistic payload (6k monophonic folk),
   *pending Matt's license call*. Needs a kern→MIDI step (or a direct kern
   note parser; kern is simple enough that a ~100-line parser to our event
   tuple is viable and avoids a Humdrum-toolkit dependency).
3. Defer KernScores-classical.

## Next actions (backlog #3 sub-items, each landable)

- [ ] **3a** Ingest Nottingham-Jukedeck: fetch repo, melody-track extractor
  → monophonic MIDI/event set → per-corpus Markov model artifact. Score a
  sample through `score_generated.py` to confirm the pipeline round-trips.
- [ ] **3b** *[review:read — Matt]* Essen license call: does a derived
  statistical (Markov/n-gram) model over the Humdrum distribution clear
  their "license only" terms for our use? Yes → 3c.
- [ ] **3c** Ingest Essen: kern→event parser (or `hum2mid`), monophonic
  (no extraction), build folk Markov model; feeds the bake-off.

Sources: [Essen (CCARH Humdrum mirror)](https://github.com/ccarh/essen-folksong-collection) ·
[Nottingham (Jukedeck clean)](https://github.com/jukedeck/nottingham-dataset) ·
[KernScores](https://kernscores.stanford.edu) ·
[Nottingham (ABC/ifdo mirror)](https://ifdo.ca/~seymour/nottingham/nottingham.html)
