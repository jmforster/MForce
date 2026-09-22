# Comp Crawl — Mary From a Fully-Specified Template: Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A fully-specified template (harmony first, melody from Matt's UI-saved passage string) renders through the existing model stack into a WAV that is unmistakably Mary Had a Little Lamb — melody on oboe1, block chords on piano_default — with zero generation anywhere in the pipeline.

**Architecture:** One new header derives `PhraseTemplate`s (Locked figures + leadStep connectors) from a passage string at compose time; `PassageTemplate` gains two fields to point at a `.psg` file; the CLI compose path applies the derivation and loads per-part instrument patches. Harmony needs zero new code — `SectionTemplate.chordProgression` + a `harmony`-role part + `chordConfig` already produce span-mode block chords (composer.h `realize_chord_parts_`).

**Tech Stack:** C++17 header-only music model (nlohmann/json), CMake multi-config build (`build/`, Release), engine_tests CHECK-macro harness, Python null-test script.

**Spec:** `docs/superpowers/specs/2026-09-21-comp-crawl-mary-design.md` — read it first; it carries Matt's five comp ground rules and the decisions this plan implements.

## Global Constraints

- Ground rules (spec §1): crawl only — NO generation, NO cadence machinery; harmony fully specified first; model stack extended, not rebuilt.
- Instrument rule (Matt 2026-09-21): melody = `patches/library/winds/oboe1.json`, accompaniment = `patches/library/keys/acoustic_piano/piano_default.json`.
- HOUSE octave convention everywhere: octave = midi/12, so "E5" = note number 64. Never scientific.
- Comp `.psg` convention: `|` = STRUCTURAL phrase boundary (spec §3).
- `docs/autonomy/GOALS.md` carries Matt's own uncommitted edit — NEVER stage, commit, or revert it. Stage files by explicit path only (never `git add <dir>/`).
- Two Claudes share this working copy: run `git branch --show-current` + `git status --short` immediately before EVERY commit; stage only this plan's files.
- Shell commands: no for/while loops; chain with `&&`; never `cd` + git (use `git -C`); literal paths, no shell variables.
- New renders go ONLY to `renders/comp/audition/crawl1/` — never a tree root.
- Build from repo root: `cmake --build build --config Release --target engine_tests` (and `--target mforce_cli`). Executables: `build/tools/engine_tests/Release/engine_tests.exe`, `build/tools/mforce_cli/Release/mforce_cli.exe`.
- Commit messages end with:
  `Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>`

## Verified codebase facts the tasks lean on (do not re-derive)

- `parse_passage(str, octave, bpm)` (engine/include/mforce/music/parse_util.h:246) → `std::vector<ParsedNote>`; `ParsedNote{float noteNumber; float durationSeconds; bool phraseStart}`; `kRestNote = -1.0f` marks rests; durations are `beats * 60 / bpm`, so **bpm=60 makes durationSeconds == beats**. `|` sets `phraseStart` on the next note; grouping only activates when the string contains `|`; **rests also end the current phrase** (v1 rule — why Task 2 refuses rests).
- `Scale` (basics.h:151): `length()`, `offset()` (tonic pitch-class), `ascending_step(i)` (semitones degree i→i+1). No midi→degree helper exists — Task 1 builds it.
- `FigureUnit{float duration; int step; bool rest; int accidental; ...}` (figures.h:501); `MelodicFigure : Figure` with `std::vector<FigureUnit> units`. Convention: `step` = scale-degree delta from previous note, **0 for a figure's first unit**; the bridge between figures lives in the connector.
- `FigureTemplate.source = FigureSource::Locked` + `lockedFigure` → composer returns the figure verbatim (composer.h:1160) and exempts it from tail reshaping (composer.h:1493).
- `PhraseTemplate.connectors` = `std::vector<std::optional<FigureConnector>>`; template-level convention = N−1 connectors for N figures, `connectors[i]` between figures i and i+1; `FigureConnector.leadStep` is the field the loader reads. **WARNING:** old `scores/baselines/template_mary.json` spells connectors `{"type":"Step","step":1}` — `from_json` ignores those keys (reads `elide`/`adjust`/`leadStep` only), so they parse as zeros. Do NOT copy that spelling.
- `Pitch::from_note_number(nn)` exists (basics.h:125). `PhraseTemplate.startingPitch` is `std::optional<Pitch>`.
- Harmony: `PieceTemplate::SectionTemplate.chordProgression` parses the flat form `[{"degree":5,"quality":"7","beats":4}]` (templates_json.h:27 `parse_chord_progression`; quality defaults `"Major"`). `realize_chord_parts_` (composer.h:543) realizes every `PartRole::Harmony` part: span mode (no `rhythmPattern`) = one event per authored chord at its authored duration; `chordConfig{octave, inversion, spread}` defaults octave 3, inversion 0 = root position; no `voicingSelector` = legacy path. Melodic realization skips Harmony parts (composer.h:189).
- `PartTemplate.instrumentPatch` exists (templates.h:575) and round-trips, but the compose CLI ignores it beyond parts[0] defaulting: `run_compose` (tools/mforce_cli/main.cpp:492) loads ONE patch and registers it for every part. `piece.parts[*].instrumentType` == template part NAME (composer.h:348); `conductor.instruments[instrumentType]` is the lookup (conductor.h:557). Multi-instrument mix pattern to imitate: main.cpp:395-483 (band mode + `render_and_write` at main.cpp:305).
- engine_tests pattern: `static void run_X_tests()` using `CHECK(cond)`, called from `main` alongside the other `run_*` calls (tools/engine_tests/main.cpp).
- Null-test script: `corpus/mtd_seg/null_test_templates.py`; a "template" = JSON with top-level `sections` + `parts`; scans `patches/` only today; invokes `mforce_cli --compose <patch> <prefix> 1 --template <t>`; CLI prints `Part '<name>': N events, ...` per part.
- Comp_Mary target content (spec §3; Matt may have re-saved `passages/Comp_Mary.psg` already — if its content differs from this ONLY in `|` placement/count or spacing, prefer the spec target; if the NOTES differ, STOP and ask Matt):
  `Eq Dq Cq Dq Eq Eq Eh Dq Dq Dh Eq Gq Gh | Eq Dq Cq Dq Eq Eq Eq Eq Dq Dq Eq Dq Cw`
- Expected derivation of that string (anchor octave 5, C Major, 4 beats/bar) — the test oracle:
  - Phrase 1 (16 beats, startingPitch E5 = nn 64), 4 figures:
    - fig 1: durations [1,1,1,1], steps [0,−1,−1,+1] (E D C D)
    - conn +1 · fig 2: [1,1,2], steps [0,0,0] (E E Eh)
    - conn −1 · fig 3: [1,1,2], steps [0,0,0] (D D Dh)
    - conn +1 · fig 4: [1,1,2], steps [0,+2,0] (E G Gh)
  - Phrase 2 (16 beats, startingPitch E5), 4 figures:
    - fig 1: [1,1,1,1], steps [0,−1,−1,+1]
    - conn +1 · fig 2: [1,1,1,1], steps [0,0,0,0]
    - conn −1 · fig 3: [1,1,1,1], steps [0,0,+1,−1] (D D E D)
    - conn −1 · fig 4: [4], steps [0] (Cw)
  - 26 notes total; melody part should therefore report 26 events, harmony 8.

---

### Task 1: `scale_steps_between` — midi-to-diatonic-grid math

**Files:**
- Create: `engine/include/mforce/music/passage_melody.h`
- Test: `tools/engine_tests/main.cpp` (new `run_passage_melody_tests()`)

**Interfaces:**
- Produces: `int mforce::scale_grid_index(float nn, const Scale& scale)` — diatonic grid index (octave*length + degree, anchored so equal nn ⇒ equal index); throws `std::runtime_error` for a non-scale tone.
- Produces: `int mforce::scale_steps_between(float nnFrom, float nnTo, const Scale& scale)` = `scale_grid_index(nnTo) − scale_grid_index(nnFrom)`.

- [ ] **Step 1: Write the failing tests**

In `tools/engine_tests/main.cpp`, add (near the other `run_*` definitions), and add `run_passage_melody_tests();` to `main` alongside the existing calls. Include `mforce/music/passage_melody.h` with the other includes.

```cpp
static void run_passage_melody_tests() {
    using namespace mforce;
    Scale c = Scale::get("C", "Major");
    // House convention: E5 = 64, D5 = 62, C5 = 60, G5 = 67.
    CHECK(scale_steps_between(64.0f, 62.0f, c) == -1);   // E -> D
    CHECK(scale_steps_between(62.0f, 60.0f, c) == -1);   // D -> C
    CHECK(scale_steps_between(64.0f, 67.0f, c) ==  2);   // E -> G (E F G)
    CHECK(scale_steps_between(64.0f, 64.0f, c) ==  0);
    CHECK(scale_steps_between(60.0f, 72.0f, c) ==  7);   // C5 -> C6, full octave
    CHECK(scale_steps_between(72.0f, 59.0f, c) == -8);   // C6 -> B4, crosses octave
    // Non-scale tone throws (crawl material must be diatonic).
    bool threw = false;
    try { scale_steps_between(60.0f, 61.0f, c); } catch (const std::exception&) { threw = true; }
    CHECK(threw);
    // Non-C tonic: G Major, F#5 = 66 is a scale tone, F5 = 65 is not.
    Scale g = Scale::get("G", "Major");
    CHECK(scale_steps_between(67.0f, 66.0f, g) == -1);   // G -> F#
    threw = false;
    try { scale_grid_index(65.0f, g); } catch (const std::exception&) { threw = true; }
    CHECK(threw);
}
```

- [ ] **Step 2: Run to verify it fails to compile**

Run: `cmake --build build --config Release --target engine_tests`
Expected: FAIL — `passage_melody.h` not found / `scale_steps_between` undeclared.

- [ ] **Step 3: Write the implementation**

Create `engine/include/mforce/music/passage_melody.h`:

```cpp
#pragma once
// Passage-string -> template melody derivation (comp crawl, spec
// docs/superpowers/specs/2026-09-21-comp-crawl-mary-design.md §4).
// A comp-purpose passage string ('|' = structural phrase boundary) becomes
// PhraseTemplates of Locked figures: step[0]=0 inside a figure, the bridge
// between figures rides FigureConnector.leadStep.
#include "mforce/music/basics.h"
#include "mforce/music/parse_util.h"
#include "mforce/music/templates.h"
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace mforce {

// Diatonic grid index of a note number: octave * scale-length + degree,
// measured from the scale's tonic pitch class. Equal note numbers always
// map to equal indices, so deltas are scale-step distances. Throws on a
// non-scale tone — crawl material is diatonic by construction, and a
// silent snap would hide a transcription error.
inline int scale_grid_index(float nn, const Scale& scale) {
    const int len = scale.length();
    const int total = int(nn) - scale.offset();
    const int oct = (total >= 0) ? total / 12 : -((-total + 11) / 12);
    const int rem = total - 12 * oct;   // 0..11 semitones above a tonic
    float acc = 0.0f;
    for (int d = 0; d < len; ++d) {
        if (int(acc + 0.5f) == rem) return oct * len + d;
        acc += scale.ascending_step(d);
    }
    throw std::runtime_error(
        "passage melody: note number " + std::to_string(int(nn)) +
        " is not a tone of the scale");
}

inline int scale_steps_between(float nnFrom, float nnTo, const Scale& scale) {
    return scale_grid_index(nnTo, scale) - scale_grid_index(nnFrom, scale);
}

} // namespace mforce
```

- [ ] **Step 4: Build and run the tests**

Run: `cmake --build build --config Release --target engine_tests && build/tools/engine_tests/Release/engine_tests.exe`
Expected: PASS, zero fails, check count grew by 9.

- [ ] **Step 5: Commit**

Verify branch first: `git -C C:/@dev/repos/mforce branch --show-current && git -C C:/@dev/repos/mforce status --short`

```bash
git -C C:/@dev/repos/mforce add engine/include/mforce/music/passage_melody.h tools/engine_tests/main.cpp && git -C C:/@dev/repos/mforce commit -m "feat(comp): scale_grid_index/scale_steps_between — diatonic grid math for passage-melody derivation

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

---

### Task 2: `phrases_from_passage` — the derivation

**Files:**
- Modify: `engine/include/mforce/music/passage_melody.h`
- Test: `tools/engine_tests/main.cpp` (extend `run_passage_melody_tests()`)

**Interfaces:**
- Consumes: `scale_grid_index` / `scale_steps_between` (Task 1); `parse_passage` (parse_util.h).
- Produces: `std::vector<PhraseTemplate> mforce::phrases_from_passage(const std::string& passageStr, int anchorOctave, const Scale& scale, float beatsPerBar)` — Locked figures split at barlines, `|` = phrase boundary, N−1 leadStep connectors per phrase, `startingPitch` set. Throws on rests, empty input, or non-scale tones.

- [ ] **Step 1: Write the failing tests**

Append to `run_passage_melody_tests()` (the Mary oracle from the plan header — check phrase 1 fully, spot-check phrase 2):

```cpp
    const std::string mary =
        "Eq Dq Cq Dq Eq Eq Eh Dq Dq Dh Eq Gq Gh | "
        "Eq Dq Cq Dq Eq Eq Eq Eq Dq Dq Eq Dq Cw";
    auto phrases = phrases_from_passage(mary, 5, c, 4.0f);
    CHECK(phrases.size() == 2);

    // Phrase 1: 4 figures, 3 connectors, starts E5.
    const auto& p1 = phrases[0];
    CHECK(p1.figures.size() == 4);
    CHECK(p1.connectors.size() == 3);
    CHECK(p1.startingPitch && int(p1.startingPitch->note_number()) == 64);
    CHECK(p1.cadenceType == 0);
    // Every figure Locked with a lockedFigure present.
    for (const auto& ft : p1.figures) {
        CHECK(ft.source == FigureSource::Locked);
        CHECK(ft.lockedFigure.has_value());
    }
    // fig1 = E D C D: durations 1,1,1,1; steps 0,-1,-1,+1.
    {
        const auto& u = p1.figures[0].lockedFigure->units;
        CHECK(u.size() == 4);
        CHECK(u[0].duration == 1.0f && u[0].step == 0);
        CHECK(u[1].step == -1 && u[2].step == -1 && u[3].step == 1);
    }
    // fig4 = E G Gh: durations 1,1,2; steps 0,+2,0.
    {
        const auto& u = p1.figures[3].lockedFigure->units;
        CHECK(u.size() == 3);
        CHECK(u[2].duration == 2.0f);
        CHECK(u[0].step == 0 && u[1].step == 2 && u[2].step == 0);
    }
    // Connectors: +1 (D->E), -1 (E->D), +1 (D->E).
    CHECK(p1.connectors[0] && p1.connectors[0]->leadStep == 1);
    CHECK(p1.connectors[1] && p1.connectors[1]->leadStep == -1);
    CHECK(p1.connectors[2] && p1.connectors[2]->leadStep == 1);

    // Phrase 2: last figure is the whole-note C alone in its bar.
    const auto& p2 = phrases[1];
    CHECK(p2.figures.size() == 4);
    CHECK(p2.startingPitch && int(p2.startingPitch->note_number()) == 64);
    {
        const auto& u = p2.figures[3].lockedFigure->units;
        CHECK(u.size() == 1 && u[0].duration == 4.0f && u[0].step == 0);
    }
    // fig3 = D D E D: steps 0,0,+1,-1; its lead connector is -1 (E->D).
    CHECK(p2.figures[2].lockedFigure->units[2].step == 1);
    CHECK(p2.connectors[2] && p2.connectors[2]->leadStep == -1);

    // A '|'-free string is one phrase (parse marks every note phraseStart
    // only when grouping is active — verify we don't fragment).
    auto one = phrases_from_passage("Cq Dq Eq Fq", 5, c, 4.0f);
    CHECK(one.size() == 1 && one[0].figures.size() == 1);

    // Rests refuse loudly (parse's rest-ends-phrase rule would corrupt
    // structural grouping; revisit when a crawl tune needs rests).
    threw = false;
    try { phrases_from_passage("Cq Rq Eq", 5, c, 4.0f); }
    catch (const std::exception&) { threw = true; }
    CHECK(threw);
```

- [ ] **Step 2: Run to verify failure**

Run: `cmake --build build --config Release --target engine_tests`
Expected: FAIL — `phrases_from_passage` undeclared.

- [ ] **Step 3: Implement**

Append to `passage_melody.h` (inside `namespace mforce`):

```cpp
// Derive fully-specified PhraseTemplates from a comp-purpose passage string.
// anchorOctave: house octave applied to octave-less note names (E -> E5).
// Figure boundary = barline (a note STARTING on a bar multiple opens a new
// figure); '|' = structural phrase boundary. No cadence fields are set —
// fully-specified means there is nothing for cadence machinery to decide.
inline std::vector<PhraseTemplate> phrases_from_passage(
        const std::string& passageStr, int anchorOctave,
        const Scale& scale, float beatsPerBar) {
    // bpm 60 => ParsedNote.durationSeconds is BEATS.
    auto notes = parse_passage(passageStr.c_str(), anchorOctave, 60.0f);
    if (notes.empty())
        throw std::runtime_error("passage melody: empty passage string");

    std::vector<PhraseTemplate> phrases;
    PhraseTemplate cur;
    MelodicFigure fig;
    float passageBeat = 0.0f;   // absolute beat cursor (barlines are global)
    int prevGrid = 0;           // grid index of the previous sounded note
    bool inPhrase = false;

    auto flush_figure = [&]() {
        if (fig.units.empty()) return;
        FigureTemplate ft;
        ft.source = FigureSource::Locked;
        ft.lockedFigure = fig;
        cur.figures.push_back(std::move(ft));
        fig = MelodicFigure{};
    };
    auto flush_phrase = [&]() {
        flush_figure();
        if (!cur.figures.empty()) {
            cur.name = "phrase" + std::to_string(phrases.size() + 1);
            phrases.push_back(std::move(cur));
        }
        cur = PhraseTemplate{};
    };

    for (const auto& n : notes) {
        if (n.noteNumber == kRestNote)
            throw std::runtime_error(
                "passage melody: rests are not supported by the crawl "
                "derivation (parse's rest-ends-phrase rule would corrupt "
                "structural phrases)");
        const int grid = scale_grid_index(n.noteNumber, scale);
        const bool newPhrase = inPhrase && n.phraseStart
                               && notes.size() > 1 && &n != &notes.front()
                               && passage_has_bars_(passageStr);
        if (newPhrase) flush_phrase();

        const bool phraseStartNow = cur.figures.empty() && fig.units.empty();
        const float beatInBar = std::fmod(passageBeat, beatsPerBar);
        if (!phraseStartNow && beatInBar == 0.0f && !fig.units.empty()) {
            // Barline: close the figure, bridge with a connector.
            flush_figure();
            FigureConnector fc;
            fc.leadStep = grid - prevGrid;
            cur.connectors.push_back(fc);
        }

        FigureUnit u;
        u.duration = n.durationSeconds;    // == beats (bpm 60)
        u.step = fig.units.empty() ? 0 : (grid - prevGrid);
        fig.units.push_back(u);

        if (phraseStartNow)
            cur.startingPitch = Pitch::from_note_number(n.noteNumber);
        prevGrid = grid;
        passageBeat += n.durationSeconds;
        inPhrase = true;
    }
    flush_phrase();
    return phrases;
}
```

with this file-local helper ABOVE it:

```cpp
// parse_passage sets phraseStart=true on EVERY note of a '|'-free string
// (grouping only activates when a bar appears), so the derivation must
// know whether '|' grouping was active at all.
inline bool passage_has_bars_(const std::string& s) {
    return s.find('|') != std::string::npos;
}
```

Implementation notes for the executor, not optional:
- The `newPhrase` condition must fire only on a REAL `|` boundary: `phraseStart` is true on every note when the string has no `|`, and true on the first note always — hence the `passage_has_bars_` guard and the not-first-note guard. After the first flush, `inPhrase` stays true; `&n != &notes.front()` guards the first note.
- Barline test uses `std::fmod(passageBeat, beatsPerBar) == 0.0f` — Mary's durations are exact binary floats (1.0, 2.0, 4.0), so equality is exact. A tune with triplets would need epsilon handling; out of crawl scope, do not add it speculatively.
- A note starting mid-bar and ringing across the barline stays in its figure (boundary applies at note STARTS).

- [ ] **Step 4: Build and run**

Run: `cmake --build build --config Release --target engine_tests && build/tools/engine_tests/Release/engine_tests.exe`
Expected: PASS, zero fails.

- [ ] **Step 5: Commit**

Verify branch, then:

```bash
git -C C:/@dev/repos/mforce add engine/include/mforce/music/passage_melody.h tools/engine_tests/main.cpp && git -C C:/@dev/repos/mforce commit -m "feat(comp): phrases_from_passage — passage string to Locked-figure PhraseTemplates

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

---

### Task 3: Template schema — `melodyPassageFile` + `apply_passage_melodies`

**Files:**
- Modify: `engine/include/mforce/music/templates.h` (PassageTemplate — near `libraryConfig`/`chordProgression` members)
- Modify: `engine/include/mforce/music/templates_json.h` (`to_json`/`from_json(PassageTemplate)`)
- Modify: `engine/include/mforce/music/passage_melody.h` (`apply_passage_melodies`)
- Test: `tools/engine_tests/main.cpp`

**Interfaces:**
- Consumes: `phrases_from_passage` (Task 2).
- Produces: `PassageTemplate` members `std::string melodyPassageFile;` and `int melodyOctave{5};` with JSON keys `"melodyPassageFile"` / `"melodyOctave"` (emitted only when `melodyPassageFile` non-empty; round-trips).
- Produces: `void mforce::apply_passage_melodies(PieceTemplate& tmpl)` — for every part passage with a non-empty `melodyPassageFile`, reads the file (path as-written, resolved against the process CWD = repo root), REPLACES `passages.phrases` with the derivation. Throws with the file path in the message when the file is missing. Idempotent (phrases fully replaced each call).

- [ ] **Step 1: Write the failing test**

Append to `run_passage_melody_tests()`:

```cpp
    // Schema + apply: template with a melodyPassageFile gets its phrases
    // derived; the two new fields round-trip; a missing file throws.
    {
        // Write a scratch .psg next to the test's CWD-independent temp dir.
        const std::string psgPath = "renders/scratch/_pm_test.psg";
        std::filesystem::create_directories("renders/scratch");
        { std::ofstream f(psgPath); f << "Eq Dq Cq Dq | Cq Cq Ch"; }

        json tj = json::parse(R"({
          "keyName": "C", "scaleName": "Major", "bpm": 80,
          "sections": [{"name": "Main", "beats": 8}],
          "parts": [{
            "name": "melody", "role": "melody",
            "passages": { "Main": {
              "melodyPassageFile": "renders/scratch/_pm_test.psg",
              "melodyOctave": 5, "phrases": [] } }
          }]
        })");
        PieceTemplate tmpl;
        from_json(tj, tmpl);
        CHECK(tmpl.parts[0].passages.at("Main").melodyPassageFile
              == "renders/scratch/_pm_test.psg");
        CHECK(tmpl.parts[0].passages.at("Main").melodyOctave == 5);

        apply_passage_melodies(tmpl);
        const auto& pass = tmpl.parts[0].passages.at("Main");
        CHECK(pass.phrases.size() == 2);
        CHECK(pass.phrases[0].figures.size() == 1);   // one bar
        CHECK(pass.phrases[1].figures.size() == 1);

        // Round-trip keeps the fields.
        json out; to_json(out, tmpl.parts[0].passages.at("Main"));
        CHECK(out.value("melodyPassageFile", std::string())
              == "renders/scratch/_pm_test.psg");
        CHECK(out.value("melodyOctave", 0) == 5);

        // Missing file names the path.
        tmpl.parts[0].passages.at("Main").melodyPassageFile = "no/such.psg";
        threw = false;
        try { apply_passage_melodies(tmpl); }
        catch (const std::exception& e) {
            threw = std::string(e.what()).find("no/such.psg") != std::string::npos;
        }
        CHECK(threw);
        std::filesystem::remove(psgPath);
    }
```

(engine_tests already uses `<filesystem>` elsewhere; add the include if missing.)

- [ ] **Step 2: Run to verify failure**

Run: `cmake --build build --config Release --target engine_tests`
Expected: FAIL — no member `melodyPassageFile`.

- [ ] **Step 3: Implement**

`templates.h`, inside `PassageTemplate` next to the other optional authoring fields:

```cpp
    // Comp crawl (spec 2026-09-21): melody authored as a passage string in
    // a .psg file ('|' = structural phrase boundary). When set,
    // apply_passage_melodies() derives phrases from it, replacing phrases[].
    std::string melodyPassageFile;
    int melodyOctave{5};   // house octave anchor for octave-less note names
```

`templates_json.h` — in `to_json(json&, const PassageTemplate&)` (near the `libraryConfig` emission):

```cpp
    if (!pt.melodyPassageFile.empty()) {
        j["melodyPassageFile"] = pt.melodyPassageFile;
        j["melodyOctave"] = pt.melodyOctave;
    }
```

and in `from_json(const json&, PassageTemplate&)`:

```cpp
    pt.melodyPassageFile = j.value("melodyPassageFile", std::string(""));
    pt.melodyOctave = j.value("melodyOctave", 5);
```

`passage_melody.h` — append:

```cpp
// Resolve every melodyPassageFile in the template: read the .psg, derive
// phrases, replace passages.phrases wholesale (idempotent). Paths resolve
// against the CWD — the repo root by standing convention.
inline void apply_passage_melodies(PieceTemplate& tmpl) {
    const Scale scale = Scale::get(tmpl.keyName, tmpl.scaleName);
    const float beatsPerBar = float(tmpl.meter.beats_per_bar());
    for (auto& part : tmpl.parts) {
        for (auto& [secName, pass] : part.passages) {
            if (pass.melodyPassageFile.empty()) continue;
            std::ifstream f(pass.melodyPassageFile);
            if (!f)
                throw std::runtime_error(
                    "melodyPassageFile not found: " + pass.melodyPassageFile);
            std::stringstream ss; ss << f.rdbuf();
            pass.phrases = phrases_from_passage(
                ss.str(), pass.melodyOctave, scale, beatsPerBar);
        }
    }
}
```

(If `PieceTemplate.meter` lacks `beats_per_bar()` directly, use the same accessor `realize_chord_parts_` uses on sections — `Meter::beats_per_bar()` exists per composer.h:628.)

- [ ] **Step 4: Build and run**

Run: `cmake --build build --config Release --target engine_tests && build/tools/engine_tests/Release/engine_tests.exe`
Expected: PASS, zero fails.

- [ ] **Step 5: Commit**

Verify branch, then:

```bash
git -C C:/@dev/repos/mforce add engine/include/mforce/music/templates.h engine/include/mforce/music/templates_json.h engine/include/mforce/music/passage_melody.h tools/engine_tests/main.cpp && git -C C:/@dev/repos/mforce commit -m "feat(comp): melodyPassageFile on PassageTemplate + apply_passage_melodies

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

---

### Task 4: CLI compose — apply derivation + per-part instrument patches

**Files:**
- Modify: `tools/mforce_cli/main.cpp` (`run_compose`, lines ~519-583)

**Interfaces:**
- Consumes: `apply_passage_melodies` (Task 3); `PartTemplate.instrumentPatch`; `part.instrumentType == template part name`.
- Produces: compose renders honor per-part `instrumentPatch` (CLI patch argument = fallback), mixing all unique patches; **byte-identical output for every existing template** (single-patch case must not move).

- [ ] **Step 1: Capture the before state (this is the test)**

Run: `python corpus/mtd_seg/null_test_templates.py before_task4`
Expected: completes; note the rendered/failed counts (run 26 era: 38 rendered).

- [ ] **Step 2: Implement**

In `run_compose`, immediately after `from_json(tj, baseTmpl);` (line ~528), add:

```cpp
        apply_passage_melodies(baseTmpl);   // comp crawl: .psg melody -> phrases
```

(add `#include "mforce/music/passage_melody.h"` with the other music includes).

Replace the single-patch load + conductor registration + render block (lines ~533-577: from `auto ip = load_instrument_patch(patchPath);` through the stereo-dup loop, EXCLUDING the wav write / stats / piece-JSON / prints, which stay) with:

```cpp
        // Per-part instrument patches (comp rule 2026-09-21: melody oboe1,
        // accompaniment piano_default — carried in instrumentPatch; the CLI
        // patch argument is the fallback). One loaded instance per unique
        // path, so single-patch templates render exactly as before.
        std::map<std::string, InstrumentPatch> patchByPath;
        auto patch_for = [&](const std::string& p) -> InstrumentPatch& {
            const std::string& key = p.empty() ? patchPath : p;
            auto it = patchByPath.find(key);
            if (it == patchByPath.end()) {
                auto ip = load_instrument_patch(key);
                ip.instrument->volume = 0.5f;
                ip.instrument->hiBoost = 0.3f;
                it = patchByPath.emplace(key, std::move(ip)).first;
            }
            return it->second;
        };
```

Keep the existing template-default block (`tmpl.sections.empty()` / `tmpl.parts.empty()` / parts[0] fallback) as-is, then after `composer.compose(piece, tmpl);`:

```cpp
        // Conductor lookup is by part.instrumentType == template part name.
        Conductor conductor;
        std::vector<Instrument*> instruments;   // unique, in part order
        for (const auto& partTmpl : tmpl.parts) {
            auto& ip = patch_for(partTmpl.instrumentPatch);
            conductor.instruments[partTmpl.name] = ip.instrument.get();
            if (std::find(instruments.begin(), instruments.end(),
                          ip.instrument.get()) == instruments.end())
                instruments.push_back(ip.instrument.get());
        }
        conductor.perform(piece);

        // Render each unique instrument and sum (band-mode pattern,
        // main.cpp render_and_write) — one instrument sums to exactly the
        // old single-buffer render.
        float totalBeats = 0;
        for (auto& sec : piece.sections) totalBeats += sec.beats;
        float bpm = piece.sections[0].tempo;
        float totalSeconds = totalBeats * 60.0f / bpm + 2.0f;
        int sampleRate = patchByPath.begin()->second.sampleRate;
        int frames = int(totalSeconds * float(sampleRate));
        std::vector<float> mono(frames, 0.0f);
        std::vector<float> buf(frames);
        for (auto* inst : instruments) {
            std::fill(buf.begin(), buf.end(), 0.0f);
            { RenderContext _ctx{sampleRate}; inst->render(_ctx, buf.data(), frames); };
            for (int k = 0; k < frames; ++k) mono[k] += buf[k];
        }
```

The stereo-dup loop, wav write, stats, piece-JSON save, and the harmony/part prints that follow stay verbatim (they read `mono`/`frames` as before). `patchByPath` is declared inside the per-render `for (int i...)` loop, where the old `load_instrument_patch` call sat, so each render still gets fresh instruments.

- [ ] **Step 3: Build**

Run: `cmake --build build --config Release --target mforce_cli`
Expected: clean build. (If mforce_ui is running and locks nothing here, fine — only the CLI relinks.)

- [ ] **Step 4: Null gate — byte-identical for existing templates**

Run: `python corpus/mtd_seg/null_test_templates.py after_task4 && python corpus/mtd_seg/null_test_templates.py --cmp before_task4 after_task4`
Expected: **every template byte-identical**, same failure list as before. Any DIFF = a bug in this task; do not rationalize one.

- [ ] **Step 5: Commit**

Verify branch, then:

```bash
git -C C:/@dev/repos/mforce add tools/mforce_cli/main.cpp && git -C C:/@dev/repos/mforce commit -m "feat(comp): compose honors per-part instrumentPatch + applies .psg melodies (null gate byte-identical)

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

---

### Task 5: The crawl template, the committed .psg, the render, the REVIEW entry

**Files:**
- Create: `scores/baselines/passages/Comp_Mary.psg`
- Create: `scores/baselines/template_mary_crawl.json`
- Create: `renders/comp/audition/crawl1/` (README + WAV; renders stay gitignored)
- Modify: `docs/autonomy/comp/REVIEW.md`

**Interfaces:**
- Consumes: everything above.
- Produces: the crawl artifact pair Matt auditions; REVIEW item **20+next-free-number** (use the next unused id — REVIEW currently tops at 19).

- [ ] **Step 1: The committed passage file**

Read `passages/Comp_Mary.psg` (Matt's bench). If its NOTES match the spec target and it has exactly one `|` (space-separated) — copy it verbatim. If it still carries the known save slip (leftover `|` after `Dh`, or `|Eq` unspaced) — write the spec §3 target instead. If the notes themselves differ from the oracle in the plan header: STOP, report to Matt, do not guess.

Create `scores/baselines/passages/Comp_Mary.psg`:

```
Eq Dq Cq Dq Eq Eq Eh Dq Dq Dh Eq Gq Gh | Eq Dq Cq Dq Eq Eq Eq Eq Dq Dq Eq Dq Cw
```

- [ ] **Step 2: The template**

Create `scores/baselines/template_mary_crawl.json` — harmony FIRST in the file (ground rule 2 made visible):

```json
{
  "keyName": "C",
  "scaleName": "Major",
  "bpm": 80.0,
  "masterSeed": 100,
  "sections": [
    {
      "name": "Main",
      "beats": 32,
      "chordProgression": [
        {"degree": 1, "beats": 4},
        {"degree": 1, "beats": 4},
        {"degree": 5, "quality": "7", "beats": 4},
        {"degree": 1, "beats": 4},
        {"degree": 1, "beats": 4},
        {"degree": 1, "beats": 4},
        {"degree": 5, "quality": "7", "beats": 4},
        {"degree": 1, "beats": 4}
      ]
    }
  ],
  "parts": [
    {
      "name": "melody",
      "role": "melody",
      "instrumentPatch": "patches/library/winds/oboe1.json",
      "passages": {
        "Main": {
          "melodyPassageFile": "scores/baselines/passages/Comp_Mary.psg",
          "melodyOctave": 5,
          "phrases": []
        }
      }
    },
    {
      "name": "chords",
      "role": "harmony",
      "instrumentPatch": "patches/library/keys/acoustic_piano/piano_default.json",
      "passages": {
        "Main": {
          "phrases": [],
          "chordConfig": {"octave": 3}
        }
      }
    }
  ]
}
```

(`quality` defaults to `"Major"` when omitted; `"7"` is the dominant-seventh ChordDef name per templates_json.h's own example. If `ChordDef::get("7")` throws at render, check `ChordDef::all()` for the house spelling — e.g. `"Dom7"` — and use that; note whichever spelling ships in the REVIEW entry.)

- [ ] **Step 3: Render**

```bash
build/tools/mforce_cli/Release/mforce_cli.exe --compose patches/library/keys/acoustic_piano/piano_default.json renders/comp/audition/crawl1/mary_crawl 1 --template scores/baselines/template_mary_crawl.json
```

Expected stdout checks (all four, mechanically):
- `Part 'melody': 26 events` — the 26 notes of Mary, no more, no fewer.
- `Part 'chords': 8 events` — one block chord per bar.
- `Composed #1: ... (32 beats @ 80 bpm)`.
- Harmony line reports 1 segment, 8 chords for 'Main'.

Then verify the melody against the oracle event-by-event: the piece JSON is saved next to the WAV (`mary_crawl_1.json`); check the melody part's first four events are nn 64, 62, 60, 62 at beats 0,1,2,3 and the final event is nn 60, duration 4, at beat 28. Wrong pitches = derivation or connector bug; go back, do not ship.

- [ ] **Step 4: Queue README + REVIEW entry**

`renders/comp/audition/crawl1/README.md` — plain language, three sentences: what this is (the comp-restart crawl: Mary from a fully-specified template, melody oboe1 / chords piano root-position blocks), the one question (is this Mary, competently harmonized?), what a yes triggers (freeze as the crawl baseline; walk stage brainstorm opens).

Prepend to `docs/autonomy/comp/REVIEW.md` under "Awaiting Matt" (next free item number; [listen] tag; date). Plain language per queue-hygiene rule; cite `renders/comp/audition/crawl1/mary_crawl_1.wav`, the template path, and the ground rules it inaugurates. State the verdict semantics: YES freezes template+psg+render as the crawl baseline; NO gets a diagnosis round, not a rebuild.

- [ ] **Step 5: Commit**

Verify branch, then (renders stay untracked — do NOT add them):

```bash
git -C C:/@dev/repos/mforce add scores/baselines/passages/Comp_Mary.psg scores/baselines/template_mary_crawl.json docs/autonomy/comp/REVIEW.md && git -C C:/@dev/repos/mforce commit -m "feat(comp): Mary crawl template + committed passage — first fully-specified harmony-first render queued

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

---

### Task 6: Null-test script — scores/baselines scan + zero-event failure (backlog 21)

**Files:**
- Modify: `corpus/mtd_seg/null_test_templates.py`

**Interfaces:**
- Consumes: CLI stdout format `Part '<name>': N events`.
- Produces: script scans `scores/baselines/` alongside `patches/`; any render whose parts sum to 0 events is a FAILURE, not a silent pass.

- [ ] **Step 1: Implement**

In `templates()`, scan both roots:

```python
def templates():
    """A template is a JSON with both `sections` and `parts` at top level."""
    out = []
    for root in (REPO / "patches", REPO / "scores" / "baselines"):
        for p in sorted(root.rglob("*.json")):
            try:
                j = json.loads(p.read_text(encoding="utf-8"))
            except Exception:
                continue
            if isinstance(j, dict) and "sections" in j and "parts" in j:
                out.append(p)
    return out
```

In `render_all`, after the returncode/wav check, add (import `re` at top):

```python
        # Backlog 21: a zero-event render is a FAILURE, not a hash. Six
        # committed templates once rendered pure silence for months because
        # nothing here objected.
        events = [int(x) for x in
                  re.findall(r"Part '[^']+': (\d+) events", p.stdout)]
        if events and sum(events) == 0:
            failed[name] = "zero events (silent render)"
            continue
```

Name-collision guard: two roots can hold same-stem templates; key `hashes`/`failed` by `p.relative_to(REPO).as_posix()` instead of `t.stem` if a collision exists (check with a set; template_mary lives in scores/baselines only, but worktree-era duplicates under patches/ may collide — prefer the relative-path key unconditionally, it only changes the report labels).

- [ ] **Step 2: Verify**

Run: `python corpus/mtd_seg/null_test_templates.py task6_check`
Expected: template count grew (scores/baselines now included — template_mary, template_mary_crawl, and the other score baselines); `template_mary_crawl` renders and hashes (26+8 events); no new failures beyond the historically-failing ones. Then delete the scratch dir: `renders/null_test_templates/task6_check` (derived data).

Sanity-check the zero-event branch fires: run once against a temp copy of template_mary_crawl with `"melodyPassageFile"` pointing at an empty scratch .psg — expect the run to record `zero events` — then delete the temp copy. (Positive control; the check must be seen to fail once before it is trusted.)

- [ ] **Step 3: Commit**

Verify branch, then:

```bash
git -C C:/@dev/repos/mforce add corpus/mtd_seg/null_test_templates.py && git -C C:/@dev/repos/mforce commit -m "test(comp): null-test scans scores/baselines and fails zero-event renders (backlog 21)

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

---

### Task 7: Shelve catalog + lane bookkeeping

**Files:**
- Create: `docs/autonomy/comp/GENERATORS.md`
- Modify: `docs/autonomy/comp/BACKLOG.md`
- Modify: `docs/autonomy/STATUS.md`

**Interfaces:** none — documentation of service status (ground rule 5). Code untouched.

- [ ] **Step 1: Write the catalog**

`docs/autonomy/comp/GENERATORS.md`: header stating the rule (Matt 2026-09-21: all generator code except the Markov Figure generator is cataloged but SHELVED until recalled or retired; cataloging declares service status, deletes nothing; recall or retirement is Matt's call per entry). Then a table with columns Generator / Where / Status / One-line what-it-was, covering at least:
- Markov Figure generator (tools + corpus pipeline, renders/markov_figures lineage) — **LIVE** (the atom source).
- RandomFigureBuilder path (`FigureSource::Generate` in composer.h) — SHELVED.
- AlternatingFigureStrategy — SHELVED.
- Wandering / Bruckner-pedal / sequence / connective / suite passage strategies (passage_strategies.h + configs) — SHELVED.
- Period/Sentence/TwoFigure/Elaborated phrase strategies (phrase_strategies.h et al.) — SHELVED.
- Figure transform wiring in the phrase builder (invert/retrograde/rotate/ornament repeats) — SHELVED.
- Cadence generation (`apply_cadence`, cadentialArrival shaping) — SHELVED.
- Passage-mode scorer + phrase composite screens (corpus/mtd_seg harnesses) — SHELVED.
- Voicing selector machinery beyond legacy root-position path (voicingSelector/profiles/dictionary) — SHELVED (the crawl's harmony uses the legacy path only).
- PatternLibrary / LibraryPassageStrategy (.ppl) — SHELVED.
For each SHELVED entry name the file(s) so recall is one lookup. Statuses are Matt's to change; do not mark anything RETIRED in this pass.

- [ ] **Step 2: Backlog bookkeeping**

`docs/autonomy/comp/BACKLOG.md`: under item 23 add a dated progress note (crawl spec + plan shipped, template + render queued as REVIEW item N, GENERATORS.md created; walk stage = next brainstorm on Matt's crawl verdict). Move item 21 to the Done ledger with a one-liner (run: this plan's Task 6, scans scores/baselines too).

- [ ] **Step 3: STATUS update**

Prepend a dated update to `docs/autonomy/STATUS.md`: comp lane revived; ground rules (one line each); what shipped (passage_melody derivation + schema + per-part CLI patches, null gate byte-identical; crawl template + render queued; backlog 21 closed; GENERATORS.md); what's open on Matt (crawl verdict REVIEW N, Comp_Mary.psg bench re-save optional now that the committed copy is canon); what's open on dev (walk-stage fit-rule brainstorm, gated on the verdict). State results and live threads; no session-arc language.

- [ ] **Step 4: Commit**

Verify branch, then:

```bash
git -C C:/@dev/repos/mforce add docs/autonomy/comp/GENERATORS.md docs/autonomy/comp/BACKLOG.md docs/autonomy/STATUS.md && git -C C:/@dev/repos/mforce commit -m "docs(comp): generator shelve catalog + backlog/status for the crawl campaign

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

---

## Plan self-review (done at authoring)

- Spec coverage: §3 conventions → Tasks 2/5; §4 schema/derivation → Tasks 1-3; §5 realization + instrument rule → Tasks 4/5; §6 provenance → Task 5; §7 old form untouched → no task touches template_mary.json (Task 6 only adds it to the scan); §8 catalog → Task 7; §9 gates → Tasks 4 (byte-identical null), 5 (render checks + REVIEW), 6 (zero-event); §10 walk stage → recorded in Task 7 bookkeeping only.
- Known uncertainty, flagged in-task: the dominant-seventh ChordDef spelling (`"7"` vs house alternative) — Task 5 Step 2 says how to resolve it.
- Type consistency: `phrases_from_passage(const std::string&, int, const Scale&, float)` used identically in Tasks 2/3; `scale_grid_index`/`scale_steps_between` signatures match Tasks 1/2; JSON keys `melodyPassageFile`/`melodyOctave` identical in Tasks 3/5.
