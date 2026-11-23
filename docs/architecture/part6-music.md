# Part 6: music

Status: **signed by Matt, 2026-11-22.** He asked for section 4's change
to be explained before and after, said "I like that, but don't like
'decided' as a name", chose `plan` from the alternatives, and closed with
"Plan it is. Proceed." That is read as the signature on the text as
written with that one rename: the ten rulings of section 11 stand as
proposed, section 9's shelved list and section 10's deferrals among them
(he was shown both as mine to propose and his to change, and changed
neither). The blocks still read *proposed*; the signature decided each.
Parent: `target-architecture.md`, sections 3.5 to 3.8, 3.10, the
music entries of section 5, and row 6 of section 7. Written under the
2026-11-21 reset (`CLAUDE.md`, "Scope right now"), in Part 5's shape:
today from the code, the interfaces, the rulings a signature accepts, the
build order.

## 1. Today, from the code

- `music/` is one module of 58 headers and 15,093 lines. `composer.h` is
  2,078: the `Composer` class is 742 of them and the rest is the bodies of
  eight strategies declared in other headers. One of those bodies,
  `DefaultPassageStrategy::compose_passage`, is 320 lines.
- **`compose` promises not to touch its template and writes to it.** It
  takes `const PieceTemplate&` and casts the `const` away at three places.
  Through them go the realized motifs, the motifs strategies add as they
  work (four call sites), and each passage template as its strategy
  planned it.
  The command-line tool copies the template once per seed so one compose
  cannot leak into the next.
- **A section's harmony is stored twice**, as `chordProgression` and as
  `harmonyTimeline`. Set-up and the composer's chord walker write both.
  `PeriodPassageStrategy` writes only the timeline. Three readers read
  only `chordProgression` (the realize step's chord-figure path,
  `AlternatingFigureStrategy`, `PhraseAwareFigureStrategy`), so harmony a
  period passage generated does not exist for them. Four more casts write
  harmony into a `const` Section.
- **Strategy settings sit in the central template.** `PhraseTemplate` has
  four optional settings slots; `PassageTemplate` has five, plus
  `periods`, a rhythm pattern, and a raw JSON blob each voicing profile
  selector parses for itself. Nothing checks the strategy's name against
  the slot that is filled; five strategies handed an empty slot print to
  stderr and return an empty phrase or passage. A new strategy means
  editing `templates.h` and `templates_json.h`.
- **Four singletons** hold the strategies and selectors, and every
  `Composer` constructor fills them again. A thread-local pointer
  (`rng::detail::current`) carries the composer's random source to the
  strategies at 11 places.
- **The realization strategies are half built.** Two are registered
  (`block`, `rhythm_pattern`); the composer only ever asks for `block`,
  and writes the rhythm pattern's loop inline. The template's
  `realizationStrategy` key is read, saved, and never consulted. The
  registered `rhythm_pattern` has no caller, and does something other
  than the inline loop does (it plays the bar's pattern from each chord's
  start; the inline loop strikes by the bar and asks the harmony what
  sounds at each strike).
- **A hand-built piece is silent.** The only code that turns passages
  into events is private to `Composer`. `dun_to_piece` fills passages and
  nothing else, so `--dun` writes silence and exits 0.
- **`Chord::init_pitches` never returns** for a chord with fewer distinct
  pitch classes than tones: the power chord, and every guitar and piano
  dictionary voicing (`chord.cpp:204`).
- A truncated section's events are cut at the truncation, and the next
  section still starts at the full count (CLEANUP 125).
- No user anywhere: `IComposer`, `Genre`, `SectionStrategy`;
  `plan_section`, `plan_phrase`, `plan_figure` (no strategy overrides
  one); `Composer::compose_figure`, `compose_phrase`,
  `registry_get_for_phase2`. `ClassicalComposer` is a pass-through that
  three tools construct.
- Six music headers include JSON outside a codec file: `templates.h`,
  `voicing_profile_selector.h`, `melody_profile.h`, `style_table.h`,
  `pool_figure_builder.h`, `pattern_library.h`. They are the meter's six
  remaining include violations.

## 2. The modules after this part (proposed)

| Module | Folder | Holds | May include |
|---|---|---|---|
| music (the model) | `music/` | pitch, scale, chord, meter, figures, the `Element`, the Piece tree, the harmony timeline, dynamics, the realize step; its codecs (`music_json`, DUN, the chord and note text parsers) | core |
| voicing | `voicing/` | the voicing selectors and profiles, their registry | music |
| compose | `compose/` | templates and their JSON, `compose`, the strategies and their settings, the figure builders, the walkers, the style and melody profiles | voicing, music |
| compose_shelved | `engine/shelved/` | shelved strategies and their codecs (section 9) | compose, voicing, music |
| perform | `perform/` | `Conductor` and the performers, `TimeMap`, the pitch gestures, the embedded score | voicing, music, contract |
| play | as today | | everything above |

- **`music/` keeps its name and becomes the model.** The model's include
  paths (`mforce/music/basics.h`, `figures.h`, `structure.h`) do not
  change, and they are the ones every other module and tool names. The
  alternative, a new `model/` folder, changes the model's paths in every
  user for a word.
- Everything stays in `namespace mforce`.
- Perform is a move and nothing else. The one performer's remaining work
  (composed pieces gaining onsets and holds, the note-at-a-time performer
  for live keys) is not in this part (section 12).
- A library target per module is Part 8. Until then the meter's map is
  the check, as it was for Part 5.

## 3. A strategy and its settings (proposed)

```cpp
// compose/strategy_choice.h
// A strategy a template names, with the settings written for it.
struct StrategyChoice {
  std::string name;      // empty = the default strategy of that level
  std::any    settings;  // empty = that strategy's defaults; otherwise
                         // a value of the strategy's own Settings type
};

// The settings as S. S's defaults when none were given. Throws, naming
// the strategy and both types, when the value held is not an S.
template <class S> S settings_of(const StrategyChoice& choice);
```

A strategy declares its settings in its own header and nowhere else:

```cpp
// compose/passage/pedal_buildup.h
struct PedalBuildupSettings {
  int levels{0};  int climbStep{2};  float cellBeats{3.0f};  /* ... */
};

class PedalBuildupStrategy : public PassageStrategy {
public:
  using Settings = PedalBuildupSettings;
  static constexpr const char* kName = "pedal_buildup";
  Passage compose_passage(Locus, const PassageTemplate&) override;
};
```

The template holds the choice, at four places:

```cpp
struct PhraseTemplate  { /* ... */ StrategyChoice strategy; };
struct PassageTemplate {
  /* ... */
  StrategyChoice strategy;                // how the passage is composed
  StrategyChoice realization;             // how a harmony part's chords
                                          // become events (section 7)
  StrategyChoice voicingProfileSelector;  // section 8
};
```

The nine settings slots, `periods`, `rhythmPattern` and the JSON blob
leave `templates.h`. Each becomes the `Settings` of the strategy that
reads it.

**Registration is explicit, and the registry is an object.**

```cpp
// compose/strategy_registry.h
class StrategyRegistry {     // name -> strategy, one table per level
public:
  void add(std::unique_ptr<FigureStrategy>);
  void add(std::unique_ptr<PhraseStrategy>);
  void add(std::unique_ptr<PassageStrategy>);
  void add(std::unique_ptr<RealizationStrategy>);
  PassageStrategy& passage(std::string_view name) const;  // throws when
                                                          // the name is
                                                          // not registered
  /* figure, phrase, realization likewise */
};

// compose/templates_json.h (a codec file)
class SettingsCodecs {       // name -> how that strategy's settings are
public:                      // read from and written to JSON
  template <class Strategy> void add();   // uses Strategy::kName and the
                                          // to_json / from_json of
                                          // Strategy::Settings
};

// What a host needs to load a template and compose from it.
struct ComposeKit {
  StrategyRegistry strategies;
  VoicingRegistry  voicings;
  SettingsCodecs   codecs;
};
ComposeKit builtin_compose_kit();     // compose's own strategies
void register_shelved(ComposeKit&);   // engine/shelved's; section 9
```

- **Why `std::any`.** Strategies are an open set (the target's
  direction), so the template cannot name their settings types. The three
  ways to hold "a value whose type only the two ends know":
  - a `std::variant` of every settings type. A closed list in a central
    header: today's problem with one slot instead of ten, and a shelved
    strategy's settings could never leave the engine.
  - a base class `StrategySettings` that each settings type derives. It
    would have a virtual destructor and nothing to call, and a type with
    a virtual function cannot be written as `PedalBuildupSettings{.levels
    = 3}`.
  - `std::any`: any copyable value, with a checked cast back. Settings
    stay plain structs. This is the one proposed.
- **The name and the settings cannot disagree from a file.** The codec
  reads the name, finds that strategy's codec, and reads the settings as
  that strategy's type. A template built in code can still pair them
  wrongly; `settings_of` throws on it with both names. Today's "empty
  slot, print, return an empty phrase" goes.
- **An unknown strategy name is an error.** Today it prints and falls
  back to the default strategy, which composes different music without
  saying so anywhere a caller can read.
- The four singletons go. A host makes a kit and passes it; a test makes
  its own. That is also what makes shelving "not registering".
- The strategies reach the registry and the random source through
  `Locus`, which already carries the piece and the template. The
  thread-local `rng::current` and its `Scope` go. The same `Randomizer`
  is drawn from in the same order, so no draw changes.

**The file form.** One shape at each of the four places: a bare name, or
a name with settings.

```json
"strategy": "sequence_passage"
"strategy": { "name": "pedal_buildup", "settings": { "levels": 3 } }
"realization": { "name": "rhythm_pattern",
                 "settings": { "defaultPattern": [2, 2] } }
```

`tools/convert_template.py` rewrites the old keys (`periodConfig`,
`pedalBuildupConfig`, `periods`, `rhythmPattern`,
`voicingProfileSelectorConfig` and the rest). 10 files carry one of
those keys: 5 in `scores/baselines`, 3 in `scores/pending`, 2 in
`scores/old`. After it, an old key in a template is refused by name,
naming the converter; today an unread key is dropped in silence.

## 4. `compose`, and the plan it returns (proposed)

```cpp
// compose/compose.h
struct Composition {
  Piece piece;
  PieceTemplate plan;      // the request as the strategies planned it:
                           // motifs realized and added, each passage as
                           // its strategy's plan step left it
  std::vector<std::string> warnings;
};

// The seed is the request's masterSeed, as today.
Composition compose(const PieceTemplate& request, const ComposeKit& kit);
```

- **The record is separate, and it has the request's type.** `compose`
  copies the request, works on the copy without a cast, and returns it.
  What was asked for is never changed by asking. Because `plan` is a
  `PieceTemplate`, the generate, review, lock, regenerate loop is
  `compose(plan, kit)` with locks set, when that loop is built; no
  second type describes "a template with its blanks filled". The name is
  the code's own: every strategy has a plan step (`plan_passage`) whose
  output this is.
- The alternative is the template as in-and-out (`PieceTemplate&`). It
  removes the casts too. It keeps the property the tool already works
  round: a host that wants five pieces from one template must copy it
  five times itself, and one that forgets gets pieces that depend on the
  ones before.
- `Locus` carries `Piece* piece` (the piece being built) and
  `PieceTemplate* plan`, neither `const`. A strategy that generates
  harmony writes its own section's timeline through it. All seven casts
  go.
- `Composer` stops being a public type; `compose` is the entry.
  `IComposer`, `Genre`, `ClassicalComposer`, `compose.h` as it is today,
  `SectionStrategy`, the three `plan_` hooks nothing overrides and the
  three unused members are deleted. `plan_passage` stays: one strategy
  uses it.

## 5. Harmony stored once (proposed)

`Section::chordProgression` is deleted. `Section::harmonyTimeline` is the
one store. The writers write it; the three readers that read the
progression read the timeline's segments, unclipped, which is what the
progression was whenever the two agreed.

What may move: a chord figure, an alternating passage or a phrase-aware
passage in a section whose harmony a period passage generated. They read
nothing before and read the harmony now. No baseline is expected to have
the combination; the comp gate says.

## 6. The realize step is the model's (proposed)

```cpp
// music/realize.h
// Writes each part's events from its passages. A part that already has
// events is left as it is.
void realize_events(Piece& piece);

// music/structure.h: the beats of a section that are played.
float played_beats(const Section& s);   // beats - truncateTailBeats
```

- It reads only the Piece, so it belongs to the model, not to the
  composer. `compose` calls it; so does `dun_to_piece`, so a parsed score
  is playable as returned.
- `play` adds a warning for a part that has passages and no events,
  where today it plays nothing and says nothing.
- **One rule for a truncated tail:** the next section starts where the
  truncated one stops. `section_start_beat` sums `played_beats`, and the
  composer and the `TimeMap` both go through it, as now.

What moves: `--dun` renders its score (two tracked `.dun` files; silent
since April). Nothing else: no template sets a truncated tail.

## 7. The realization strategies, finished (proposed)

```cpp
// compose/realization.h
// How a harmony part turns a section's chords into events.
struct RealizationContext {
  const HarmonyTimeline& harmony;
  const Section& section;
  float sectionStart;                 // in piece beats
  const StrategyChoice& choice;
  // One chord symbol at a beat, voiced. The composer supplies it.
  std::function<Chord(const ScaleChord&, float beat, float beats)> voice;
};

class RealizationStrategy {
public:
  virtual ~RealizationStrategy() = default;
  virtual std::string name() const = 0;
  virtual void realize(const RealizationContext&, ElementSequence& out) = 0;
};
```

- The strategy owns the loop over the section, which is where the two
  differ. `block` is today's default: one event per authored chord, at
  the length the progression gives it. `rhythm_pattern` is today's inline
  loop, moved; its `Settings` is the `RhythmPattern`.
- Voicing stays the composer's and is handed in as `voice`, so each
  strategy is the emission rule and nothing else.
- `RealizationRequest` and the per-chord `rhythm_pattern` that nothing
  called are deleted.
- Today a `rhythmPattern` with no `realizationStrategy` selects the
  pattern. The converter writes that choice out as `"realization":
  {"name": "rhythm_pattern", ...}`. No tracked template has a
  `rhythmPattern`, so nothing in the gate moves.

## 8. Voicing (proposed)

The eight headers move to `voicing/`. `VoicingRegistry` holds the
selectors by name and a factory per profile selector (a profile selector
keeps state for one passage, so each passage gets a fresh one). The
profile selectors take their settings as typed values
(`RandomProfileSettings`, `DriftProfileSettings`,
`ScriptedProfileSettings`) through the same `StrategyChoice`;
`configure_from_json` and the `dynamic_cast` that special-cases the
static selector go. Nothing about how a chord is voiced changes.

## 9. What is shelved (proposed; the list is yours)

The target decided the mechanism and that the pattern library moves, and
left the other entries of `docs/autonomy/comp/GENERATORS.md` to you, one
at a time. Shelved code lives under `engine/shelved/`, registers through
`register_shelved` and no other way, and compose may not include it. The
build option that leaves it out is Part 8's; until then the tool and the
tests register both, so nothing changes in what runs.

My read of each entry:

| GENERATORS.md entry | Read | Why |
|---|---|---|
| PatternLibrary, `LibraryPassageStrategy` | moves | decided 2026-10-05 |
| `AlternatingFigureStrategy` | moves | a registered strategy; nothing in compose calls it |
| Wandering, pedal, sequence, connective passage strategies | move | the same |
| Period, Sentence, TwoFigure, Elaborated phrase strategies; `PeriodPassageStrategy` | move | the same |
| RandomFigureBuilder path | stays | a branch inside the default figure strategy, not a unit |
| Transform wiring in the phrase builder | stays | a branch inside the default phrase strategy |
| Cadence generation | stays | the same |
| Voicing selectors and profiles | stay | the target keeps voicing as a module perform may use |
| The scorer and screens | not engine code | `corpus/mtd_seg` |

`phrase_aware_figure` and `wrapper_phrase` are registered and not in the
catalogue; they stay unless you say otherwise. That is 11 strategies
moved and 6 left in compose (the three defaults, the three shapes), plus
those two.

## 10. Behaviour fixes: which are in (proposed)

The 2026-10-02 review's list for this area, and the target's section 6.

In, because each is silence, a hang or a false promise:

- hand-built pieces play (section 6);
- `compose` does not write through `const` (section 4);
- `Chord::init_pitches` returns: once every pitch class is voiced it
  doubles at the octave. A test voices every chord of every dictionary at
  every inversion and three spreads. Nothing that renders today moves:
  the inputs it changes never returned;
- the truncated-tail rule (section 6).

Left for the comp lane after the freeze, each with its review id in the
comp backlog:

- the figure transforms dropping rests, accidentals, articulations and
  ornaments (F084);
- `RandomFigureBuilder` returning figures that miss the count or length
  asked for (F109);
- the chord walker's label and override lookups (F123, F124).

Each of these three changes the notes a generator produces, so fixing one
re-rolls baselines, and what the right note is belongs with a listening
session. The target's section 6 says the review's confirmed high findings
are fixed during the refactor; F084 and F109 are high, and this leaves
them. Say so if you want them in.

## 11. The rulings the signature accepts

1. **`music/` is the model's folder**; `voicing/`, `compose/` and
   `perform/` sit beside it (section 2).
2. **`compose` returns a separate record of the request's type**, and
   never changes the request (section 4).
3. **`StrategyChoice` with `std::any`**; each strategy owns its settings
   and their codec; the registries are objects in a `ComposeKit`; the
   random source travels in `Locus` (section 3).
4. **The template file form changes**: one shape at four places, a
   converter over the 10 files that carry an old key (5 baselines, 3 in
   `scores/pending`, 2 in `scores/old`), old keys refused by name
   (section 3).
5. **A realization strategy owns the section's loop**, and the rhythm
   pattern becomes one's settings (section 7).
6. **The harmony timeline is the one store** (section 5).
7. **The realize step is the model's, `dun_to_piece` calls it, and a
   truncated section pulls the next one earlier** (section 6).
8. **Which fixes are in and which are left** (section 10).
9. **The shelved list** (section 9).
10. **The generators' bodies move as they are.** The 320-line function
    and its like go to their own files unchanged. Shortening them is
    rewriting how music is generated, which is comp work with ears, not a
    move.

## 12. Build order and proof

Each step is one commit or a few. Each ends with the build,
`engine_tests`, `test_figures`, the null gate at 77 of 77, and the comp
gate (`tools/comp_template_null_gate.py`: the 21 baseline templates'
piece JSON and WAV) the same as the step before.

1. **The honest composer.** `compose` and `Composition`; the working
   copy; `Locus` with the piece, the record, the kit and the random
   source; the registries as objects; the dead types deleted; the tools
   on `compose`.
2. **Harmony once, realize public.** Sections 5 and 6, and
   `init_pitches`. `--dun` sounds: its two scores are rendered for the
   report.
3. **Settings.** `StrategyChoice`, the settings beside their strategies,
   the codecs, the realization strategies, voicing's typed settings, the
   converter and the 10 files.
4. **The split.** `composer.h`'s strategy bodies to their own files; the
   folders of section 2; the shelved folder; the four file readers
   (`melody_profile`, `style_table`, `pool_figure_builder`,
   `pattern_library`) into `_json.h` files beside them; the meter's map.
   Moves only.

Expected on the meter at the close: include edges outside the map 6 to
0; `composer.h` and `passage_strategies.h` off the list of files over the
limit; `const_cast` down by at least seven; file-level mutable variables
down by the four singletons and the thread-local. Functions over the limit stay
where they are (ruling 10).

One independent review and one short report at the end of the part.

## 13. Not in this part

- Composed pieces gaining onsets and holds; the note-at-a-time performer
  for live keys: with the editor's design, or after the freeze.
- F084, F109, F123, F124 (section 10).
- `Chord` leaving the `Element` variant.
- Library code that prints to stderr (42 places in music), reads two
  environment variables (`MFORCE_ANCHOR_LOG`, `MFORCE_FIGURE_RANDOM`) and
  opens `lib/figures/markov_pool.json` by a relative path. `Composition`
  has a `warnings` list from this part on; moving the prints into it is a
  CLEANUP row.
- `templates_json.h` under the file limit; bodies out of headers into
  `.cpp` files.
- `test_figures` into the engine tests, a target per module, the build
  option for the shelved folder: Part 8.

## 14. For the C# reader

**`std::any` is `object` with a checked cast.** `std::any settings` can
hold a value of any copyable type; `std::any_cast<PedalBuildupSettings>`
gives it back or throws `std::bad_any_cast`, as `(PedalBuildupSettings)obj`
throws `InvalidCastException`. The value is stored inside the `any`
(boxed, in C# terms), and copying the `any` copies the value, so a
template that holds one still copies as a value. The C# design you would
reach for first is a generic base, `PassageStrategy<TSettings>`. C++ can
write that (a class template), and it moves the cast into the base
instead of removing it: the template still has to hold settings whose
type it cannot name. `std::variant` is the closed alternative: a
discriminated union, which C# has no built-in form of.

**`const` is a promise the compiler checks, and `const_cast` breaks it.**
`const PieceTemplate&` says "I will not change this", and the compiler
refuses a write through it. `const_cast` removes the word without
changing what the object is; if the caller's object really was declared
`const`, writing to it is undefined behaviour, not an exception. C#'s
`in` parameter is the nearest thing, and only for the top level of a
struct; Java has nothing (a `final` reference still lets you change the
object). Returning a new value, as `compose` now does, is the C# habit
of returning a record and leaving the argument alone.

**A registry you pass, against a registry you reach for.**
`static StrategyRegistry inst;` inside `instance()` is C#'s
`static readonly Lazy<T>`: one per process, built on first use, reachable
from anywhere. It is why every `Composer` re-registered everything and
why a test could not compose with two strategies and nothing else. A
`ComposeKit` passed by reference is constructor injection: the caller
decides what is registered, which is all "shelved" means.
