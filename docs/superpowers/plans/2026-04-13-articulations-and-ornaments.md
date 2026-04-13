# Articulations and Ornaments Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make NotePerformer realize Articulations (parameter transforms) and expand Ornaments (one note into multiple play_note calls).

**Architecture:** NotePerformer's `perform_note` takes a `Note` instead of raw floats. It applies articulation transforms (staccato shortens, marcato boosts velocity), then expands ornaments into multiple `instrument.play_note` calls using the ornament's semitone intervals and sub-articulations. Both Conductor call sites (event-list path and compositional path) build a `Note` and pass it through.

**Tech Stack:** C++20, header-only music model in `engine/include/mforce/music/`

**Spec:** `docs/superpowers/specs/2026-04-13-articulations-and-ornaments-design.md`

---

### File Map

- **Modify:** `engine/include/mforce/music/conductor.h` — NotePerformer, Conductor::perform_events, Conductor::perform_phrase
- **Modify:** `engine/include/mforce/music/structure.h` — (no structural changes, but Note already has the fields we need)

No new files. All changes are in `conductor.h`.

---

### Task 1: Change NotePerformer::perform_note to accept a Note

Rewire the signature and both call sites. No behavior change yet — just plumbing.

**Files:**
- Modify: `engine/include/mforce/music/conductor.h:160-171` (NotePerformer)
- Modify: `engine/include/mforce/music/conductor.h:483-486` (perform_events call site)
- Modify: `engine/include/mforce/music/conductor.h:581-584` (perform_phrase call site)

- [ ] **Step 1: Update NotePerformer::perform_note signature**

Replace the current `perform_note` in `conductor.h:164-170`:

```cpp
struct NotePerformer {
  float sloppiness{0.0f};
  Randomizer rng{0xA07E'0000u};

  void perform_note(const Note& note, float startBeats, float bpm,
                    PitchedInstrument& instrument) {
    float startSeconds = startBeats * 60.0f / bpm;
    float durSeconds = note.durationBeats * 60.0f / bpm;
    startSeconds += sloppiness * rng.valuePN() * 0.003f;
    instrument.play_note(note.noteNumber, note.velocity, durSeconds, startSeconds);
  }
};
```

- [ ] **Step 2: Update perform_events call site**

In `Conductor::perform_events` (conductor.h:483-486), replace:

```cpp
      if (event.is_note() && pitched) {
        const auto& n = event.note();
        notePerformer.perform_note(n, absBeats, bpm, *pitched);
      }
```

(Remove the old line that unpacked n.noteNumber, n.velocity, n.durationBeats as separate args.)

- [ ] **Step 3: Update perform_phrase call site**

In `Conductor::perform_phrase` (conductor.h:581-584), the compositional path builds from FigureUnit fields. Build a Note and pass it:

```cpp
        if (!u.rest) {
          float vel = dynamics.velocity_at(currentBeat);
          Note n{soundNN, vel, u.duration, u.articulation, u.ornament};
          notePerformer.perform_note(n, currentBeat, bpm, instrument);
        }
```

- [ ] **Step 4: Build and verify**

```bash
"/c/Program Files/Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" --build build --target mforce_cli --config Debug
```

Expected: clean build (same pre-existing C4189 warning only).

- [ ] **Step 5: Run a render to verify no regression**

```bash
build/tools/mforce_cli/Debug/mforce_cli.exe patches/PluckU.json renders/art_orn_test_1.json
```

Expected: same output as before (peak/rms unchanged).

- [ ] **Step 6: Commit**

```bash
git add engine/include/mforce/music/conductor.h
git commit -m "refactor: NotePerformer::perform_note takes Note instead of raw floats"
```

---

### Task 2: Implement Articulation realization

Apply parameter transforms in perform_note based on the Note's articulation.

**Files:**
- Modify: `engine/include/mforce/music/conductor.h:160-171` (NotePerformer)

- [ ] **Step 1: Add articulation_transform helper**

Add a private helper inside NotePerformer that returns adjusted duration and velocity:

```cpp
struct NotePerformer {
  float sloppiness{0.0f};
  Randomizer rng{0xA07E'0000u};

  // Articulation parameter transforms — returns {adjustedDuration, adjustedVelocity}
  static std::pair<float, float> apply_articulation(Articulation art,
                                                     float durSeconds, float velocity) {
    switch (art) {
      case Articulation::Staccato:
        return {durSeconds * 0.5f, velocity};
      case Articulation::Marcato:
        return {durSeconds, std::min(velocity * 1.3f, 1.0f)};
      case Articulation::Sforzando:
        return {durSeconds, std::min(velocity * 1.5f, 1.0f)};
      case Articulation::Mute:
        return {durSeconds * 0.7f, velocity * 0.6f};
      default:
        return {durSeconds, velocity};
    }
  }

  void perform_note(const Note& note, float startBeats, float bpm,
                    PitchedInstrument& instrument) {
    float startSeconds = startBeats * 60.0f / bpm;
    float durSeconds = note.durationBeats * 60.0f / bpm;
    startSeconds += sloppiness * rng.valuePN() * 0.003f;

    auto [dur, vel] = apply_articulation(note.articulation, durSeconds, note.velocity);
    instrument.play_note(note.noteNumber, vel, dur, startSeconds);
  }
};
```

- [ ] **Step 2: Build and verify**

```bash
"/c/Program Files/Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" --build build --target mforce_cli --config Debug
```

Expected: clean build.

- [ ] **Step 3: Commit**

```bash
git add engine/include/mforce/music/conductor.h
git commit -m "feat: NotePerformer applies articulation transforms (staccato, marcato, sforzando, mute)"
```

---

### Task 3: Implement Ornament expansion — Mordent

Expand a mordent into 3 play_note calls: main pitch, neighbor, main pitch.

**Files:**
- Modify: `engine/include/mforce/music/conductor.h` (NotePerformer)

- [ ] **Step 1: Add ornament sub-note duration helper**

Add a helper that calculates how long each ornament sub-note should be, based on tempo and parent duration. The heuristic: ornament sub-notes target ~60ms but are clamped to at most 1/4 of the parent note duration.

```cpp
  // How long (in seconds) should each ornament sub-note be?
  static float ornament_subnote_duration(float parentDurSeconds) {
    float target = 0.06f;  // 60ms target
    float maxFraction = parentDurSeconds * 0.25f;
    return std::min(target, maxFraction);
  }
```

- [ ] **Step 2: Add perform_mordent method**

```cpp
  void perform_mordent(const Note& note, const Mordent& m,
                       float startSeconds, float durSeconds,
                       PitchedInstrument& instrument) {
    float subDur = ornament_subnote_duration(durSeconds);
    float neighborNN = note.noteNumber + float(m.direction * m.semitones);

    // Sub-note articulations: m.articulations[0] for neighbor, m.articulations[1] for return
    // Falls back to Default if not specified.
    Articulation neighborArt = m.articulations.size() > 0 ? m.articulations[0] : Articulation::Default;
    Articulation returnArt   = m.articulations.size() > 1 ? m.articulations[1] : Articulation::Default;

    // 1. Main note (short) — uses the Note's own articulation
    auto [dur1, vel1] = apply_articulation(note.articulation, subDur, note.velocity);
    instrument.play_note(note.noteNumber, vel1, dur1, startSeconds);

    // 2. Neighbor note
    auto [dur2, vel2] = apply_articulation(neighborArt, subDur, note.velocity);
    instrument.play_note(neighborNN, vel2, dur2, startSeconds + subDur);

    // 3. Main note (remainder)
    float remainDur = durSeconds - 2.0f * subDur;
    auto [dur3, vel3] = apply_articulation(returnArt, remainDur, note.velocity);
    instrument.play_note(note.noteNumber, vel3, dur3, startSeconds + 2.0f * subDur);
  }
```

- [ ] **Step 3: Wire mordent into perform_note using std::visit**

Update `perform_note` to check for ornaments before the default path:

```cpp
  void perform_note(const Note& note, float startBeats, float bpm,
                    PitchedInstrument& instrument) {
    float startSeconds = startBeats * 60.0f / bpm;
    float durSeconds = note.durationBeats * 60.0f / bpm;
    startSeconds += sloppiness * rng.valuePN() * 0.003f;

    if (has_ornament(note.ornament)) {
      std::visit([&](auto&& orn) {
        using T = std::decay_t<decltype(orn)>;
        if constexpr (std::is_same_v<T, Mordent>) {
          perform_mordent(note, orn, startSeconds, durSeconds, instrument);
        } else if constexpr (std::is_same_v<T, std::monostate>) {
          // no ornament — fall through handled by has_ornament guard
        } else {
          // Trill, Turn — not yet implemented, play plain note
          auto [dur, vel] = apply_articulation(note.articulation, durSeconds, note.velocity);
          instrument.play_note(note.noteNumber, vel, dur, startSeconds);
        }
      }, note.ornament);
    } else {
      auto [dur, vel] = apply_articulation(note.articulation, durSeconds, note.velocity);
      instrument.play_note(note.noteNumber, vel, dur, startSeconds);
    }
  }
```

- [ ] **Step 4: Build and verify**

```bash
"/c/Program Files/Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" --build build --target mforce_cli --config Debug
```

- [ ] **Step 5: Commit**

```bash
git add engine/include/mforce/music/conductor.h
git commit -m "feat: NotePerformer expands Mordent ornaments into 3 sub-notes"
```

---

### Task 4: Implement Ornament expansion — Trill

Expand a trill into alternating main/neighbor notes filling the full duration.

**Files:**
- Modify: `engine/include/mforce/music/conductor.h` (NotePerformer)

- [ ] **Step 1: Add perform_trill method**

```cpp
  void perform_trill(const Note& note, const Trill& t,
                     float startSeconds, float durSeconds,
                     PitchedInstrument& instrument) {
    float subDur = ornament_subnote_duration(durSeconds);
    float neighborNN = note.noteNumber + float(t.direction * t.semitones);
    int count = std::max(2, int(durSeconds / subDur));
    float actualSubDur = durSeconds / float(count);

    for (int i = 0; i < count; ++i) {
      bool isNeighbor = (i % 2 == 1);
      float nn = isNeighbor ? neighborNN : note.noteNumber;

      // Cycle through sub-articulations if provided
      Articulation art;
      if (isNeighbor && !t.articulations.empty()) {
        int artIdx = (i / 2) % int(t.articulations.size());
        art = t.articulations[artIdx];
      } else if (!isNeighbor) {
        art = note.articulation;
      } else {
        art = Articulation::Default;
      }

      auto [dur, vel] = apply_articulation(art, actualSubDur, note.velocity);
      instrument.play_note(nn, vel, dur, startSeconds + float(i) * actualSubDur);
    }
  }
```

- [ ] **Step 2: Wire trill into perform_note visitor**

In the `std::visit` lambda, replace the Trill fallback:

```cpp
        } else if constexpr (std::is_same_v<T, Trill>) {
          perform_trill(note, orn, startSeconds, durSeconds, instrument);
```

- [ ] **Step 3: Build and verify**

```bash
"/c/Program Files/Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" --build build --target mforce_cli --config Debug
```

- [ ] **Step 4: Commit**

```bash
git add engine/include/mforce/music/conductor.h
git commit -m "feat: NotePerformer expands Trill ornaments into alternating sub-notes"
```

---

### Task 5: Implement Ornament expansion — Turn

Expand a turn into 4 notes: above, main, below, main (or reversed).

**Files:**
- Modify: `engine/include/mforce/music/conductor.h` (NotePerformer)

- [ ] **Step 1: Add perform_turn method**

```cpp
  void perform_turn(const Note& note, const Turn& t,
                    float startSeconds, float durSeconds,
                    PitchedInstrument& instrument) {
    float subDur = ornament_subnote_duration(durSeconds);
    float remainDur = durSeconds - 3.0f * subDur;

    // 4 pitches: [above, main, below, main] or reversed
    float aboveNN = note.noteNumber + float(t.semitonesAbove);
    float belowNN = note.noteNumber - float(t.semitonesBelow);

    struct SubNote { float nn; float dur; };
    SubNote notes[4];
    if (t.direction >= 0) {
      // Above-first: above, main, below, main(remainder)
      notes[0] = {aboveNN, subDur};
      notes[1] = {note.noteNumber, subDur};
      notes[2] = {belowNN, subDur};
      notes[3] = {note.noteNumber, remainDur};
    } else {
      // Below-first: below, main, above, main(remainder)
      notes[0] = {belowNN, subDur};
      notes[1] = {note.noteNumber, subDur};
      notes[2] = {aboveNN, subDur};
      notes[3] = {note.noteNumber, remainDur};
    }

    float cursor = startSeconds;
    for (int i = 0; i < 4; ++i) {
      // First sub-note uses note's articulation, rest use sub-articulations if available
      Articulation art;
      if (i == 0) {
        art = note.articulation;
      } else if (i - 1 < int(t.articulations.size())) {
        art = t.articulations[i - 1];
      } else {
        art = Articulation::Default;
      }

      auto [dur, vel] = apply_articulation(art, notes[i].dur, note.velocity);
      instrument.play_note(notes[i].nn, vel, dur, cursor);
      cursor += notes[i].dur;
    }
  }
```

- [ ] **Step 2: Wire turn into perform_note visitor**

Replace the Turn fallback in the `std::visit` lambda:

```cpp
        } else if constexpr (std::is_same_v<T, Turn>) {
          perform_turn(note, orn, startSeconds, durSeconds, instrument);
```

- [ ] **Step 3: Build and verify**

```bash
"/c/Program Files/Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" --build build --target mforce_cli --config Debug
```

- [ ] **Step 4: Commit**

```bash
git add engine/include/mforce/music/conductor.h
git commit -m "feat: NotePerformer expands Turn ornaments into 4 sub-notes"
```

---

### Task 6: Smoke test with an ornament-bearing composition

Build a small inline test in mforce_cli that creates a Part with ornaments and renders it. Verify the output has more rendered notes than input notes (proving expansion happened) and non-zero audio.

**Files:**
- Modify: `tools/mforce_cli/main.cpp` (add a `--test-ornaments` mode)

- [ ] **Step 1: Add test_ornaments function**

Add before the `main()` function:

```cpp
static int test_ornaments(int /*argc*/, char** /*argv*/) {
    auto ip = load_instrument("patches/PluckU.json");
    if (!ip.instrument) {
        std::cerr << "ERROR: could not load PluckU.json\n";
        return 1;
    }

    Part part;
    part.instrumentType = "pluck";
    float bpm = 120.0f;

    // Plain note — C4, quarter note
    part.add_note(0.0f, 60.0f, 1.0f, 1.0f);

    // Staccato note — D4
    part.add_note(1.0f, 62.0f, 1.0f, 1.0f, Articulation::Staccato);

    // Marcato note — E4
    part.add_note(2.0f, 64.0f, 1.0f, 1.0f, Articulation::Marcato);

    // Mordent up on F4 with HammerOn + PullOff
    {
        Note n{65.0f, 1.0f, 1.0f, Articulation::Default,
               Mordent{1, 2, {Articulation::HammerOn, Articulation::PullOff}}};
        part.events.push_back({3.0f, n});
        part.totalBeats = 4.0f;
    }

    // Trill up on G4
    {
        Note n{67.0f, 1.0f, 2.0f, Articulation::Default, Trill{1, 2, {}}};
        part.events.push_back({4.0f, n});
        part.totalBeats = 6.0f;
    }

    // Turn on A4
    {
        Note n{69.0f, 1.0f, 2.0f, Articulation::Default, Turn{1, 2, 1, {}}};
        part.events.push_back({6.0f, n});
        part.totalBeats = 8.0f;
    }

    Conductor conductor;
    conductor.instruments["pluck"] = ip.instrument.get();
    conductor.perform(part, bpm, *ip.instrument);

    int noteCount = int(ip.instrument->renderedNotes.size());
    std::cout << "Rendered " << noteCount << " notes from 6 input events\n";

    // 6 input events should produce more rendered notes:
    // plain=1, staccato=1, marcato=1, mordent=3, trill>=4, turn=4 = at least 14
    if (noteCount < 14) {
        std::cerr << "FAIL: expected at least 14 rendered notes, got " << noteCount << "\n";
        return 1;
    }

    float totalSeconds = part.totalBeats * 60.0f / bpm + 2.0f;
    int frames = int(totalSeconds * float(ip.sampleRate));
    std::vector<float> mono(frames, 0.0f);
    ip.instrument->render(mono.data(), frames);

    float peak = 0.0f;
    for (auto s : mono) {
        float a = std::fabs(s);
        if (a > peak) peak = a;
    }

    std::cout << "Peak amplitude: " << peak << "\n";
    if (peak < 0.01f) {
        std::cerr << "FAIL: expected non-zero audio output\n";
        return 1;
    }

    std::cout << "PASS\n";
    return 0;
}
```

- [ ] **Step 2: Add --test-ornaments to main()**

In the argument dispatch in `main()`, add a branch:

```cpp
    if (argc > 1 && std::string(argv[1]) == "--test-ornaments")
        return test_ornaments(argc, argv);
```

- [ ] **Step 3: Build**

```bash
"/c/Program Files/Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" --build build --target mforce_cli --config Debug
```

- [ ] **Step 4: Run the test**

```bash
build/tools/mforce_cli/Debug/mforce_cli.exe --test-ornaments
```

Expected output:
```
Rendered 14 notes from 6 input events
Peak amplitude: <non-zero>
PASS
```

- [ ] **Step 5: Commit**

```bash
git add tools/mforce_cli/main.cpp engine/include/mforce/music/conductor.h
git commit -m "feat: articulation realization and ornament expansion in NotePerformer with smoke test"
```

---

### Task 7: Build mforce_ui to verify no breakage

**Files:** None modified — verification only.

- [ ] **Step 1: Build UI target**

```bash
"/c/Program Files/Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" --build build --target mforce_ui --config Debug
```

Expected: clean build.
