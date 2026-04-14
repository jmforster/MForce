# Outline Strategies and FigureConnector Resurrection — Design

> Supplements `2026-04-10-composer-strategy-architecture-design.md` and
> `2026-04-10-phase-2-3-5-addendum.md`. Designed 2026-04-11 after the
> long brainstorming session that walked through the Billy Joel "My Life"
> chorus as a stress-test for outline behavior.
>
> Outline strategies and FigureConnector were both deferred out of Phase
> 1a and Phase 3. This spec consolidates the design for all of it and is
> intended to become a single implementation plan (or pair, if we decide
> to split between figure-level and passage-level work).

## Goals

1. **Resurrect `FigureConnector`** as an optional, generic, per-adjacency
   mechanism for eliding the tail of one figure and/or extending its last
   surviving note before the next figure plays. Used between any two
   figures in a phrase, not specific to outlines.
2. **Add `OutlineFigureStrategy`** — a figure-level strategy that takes a
   `StepSequence` outline and a `MelodicFigure` surface and emits one flat
   `MelodicFigure` that unfolds the surface across the outline anchors.
3. **Add `OutlinePassageStrategy`** — a passage-level strategy that takes
   a `StepSequence` outline and a `std::vector<PhraseTemplate>` (same
   length as the outline) and emits a `Passage` where each phrase's
   starting cursor is forced to the corresponding outline anchor.
4. **Wire figure-level and passage-level strategy dispatch** so that
   templates can select `OutlineFigureStrategy` / `OutlinePassageStrategy`
   by name (matching the phrase-level dispatch Phase 3 already added).
5. **Fix `PhraseTemplate::from_json`** so the `figures` field is optional.
   Surfaced in Phase 3's smoke test and still outstanding.

Stress test: faithfully reproduce the Billy Joel "My Life" chorus
construction. That example drove all of the brainstorming decisions.

## Non-goals

- **No `OutlinePhraseStrategy`.** Explicitly excluded during brainstorming.
  Phrases are just lists of figures joined by optional FigureConnectors;
  there is no need for a phrase-level outline primitive.
- **No chord/harmony awareness in outline strategies.** The insight that
  Billy's "3-1-5 / 2-7-5" arpeggio pattern is harmony-driven (I-chord over
  ur-line 3, V-chord over ur-line 2) means the StepSequence the user passes
  is *already* harmonically informed — by the user, in their head. A future
  tool that generates StepSequences from ChordProgression + ur-line lives
  *above* the outline strategies. Outline strategies stay harmony-agnostic.
- **No new FigureShape enums.** Cadence-target tags use the existing
  `FigureShape::CadentialApproach` via `ShapeCadentialApproachStrategy`
  from Phase 2.
- **No motif-level changes.** `Motif` (the Phase-5 rename of `Seed`) is
  untouched.
- **No changes to DURN parser.** The Phase 1b DURN parser absorbs `Xn`
  tokens into first-unit steps; this spec does not add connector
  emission to the parser. Complex elision/extension joints are
  template-authored, not DURN-authored.
- **No new `FigureStrategy` / `PassageStrategy` base classes.** The
  existing `Strategy` base with its `realize_figure` / `realize_phrase` /
  `realize_passage` virtual methods is reused; outline strategies just
  override the appropriate virtual.

## Part 1 — FigureConnector

### Type

```cpp
// engine/include/mforce/music/figures.h
//
// Resurrected from the pre-Phase-1b version, but simpler: only the
// elide+adjust mechanism, no ConnectorType enum, no default step(-1).
struct FigureConnector {
  int elideCount{0};     // notes removed from the END of the preceding figure
  float adjustCount{0};  // beats added to (positive) or removed from
                         // (negative) the last SURVIVING unit of the
                         // preceding figure after elision. Positive is the
                         // "My Life" case (extend to soak up tail). Negative
                         // is the "K467 pickups" case (shorten to make room
                         // for a following pickup figure). Must not reduce
                         // the unit's duration below zero.
};
```

Both fields default to zero, so a zero-initialized connector is semantically
equivalent to "no connector." The presence of a connector in the phrase's
connector vector is determined by `std::optional`, not by these values.

### Placement on PhraseTemplate

A parallel vector on `PhraseTemplate`, with element `i` describing the
connector that joins `figures[i-1]` → `figures[i]`. Element 0 is unused
(the first figure has no preceding figure), but the vector length must
equal `figures.size()` to make index math unambiguous:

```cpp
// engine/include/mforce/music/templates.h
struct PhraseTemplate {
  // ... existing fields ...
  std::vector<FigureTemplate> figures;
  std::vector<std::optional<FigureConnector>> connectors;  // size == figures.size()
                                                            // connectors[0] is unused
                                                            // connectors[i>0] joins figures[i-1]→figures[i]
};
```

Indexing convention: `connectors[i]` describes the joint *into* `figures[i]`,
not *out of* `figures[i]`. This means `connectors[0]` is unused (nothing
to join into from before the first figure) and `connectors[i]` for `i>0`
is the optional joint between `figures[i-1]` and `figures[i]`.

Alternative considered: parallel vector with size `figures.size() - 1` and
`connectors[i]` joins `figures[i]` → `figures[i+1]` (the old pre-Phase-1b
convention). Rejected because the size mismatch between `figures` and
`connectors` is an invitation for off-by-one bugs, and because the
"incoming connector to figure i" framing matches how the user thinks about
authoring a template.

### Placement on Phrase (runtime type)

**Phrase does NOT gain a connectors field.** Connectors are a
composition-time concept only. When `DefaultPhraseStrategy::realize_phrase`
walks the template's figures, it applies any present connector by
**mutating the previously-added figure in `phrase.figures`** (eliding its
trailing units and extending its new-last unit) before adding the next
figure. The resulting `Phrase` is self-contained — its figures already
reflect any elision/extension — and the Conductor walks it as today with
no changes.

This keeps the Conductor simple, keeps Phrase's shape unchanged from Phase
1b, and matches the existing "composer produces final data, conductor plays
it" pattern.

### Semantics (Reading B from brainstorming)

When a connector is present at `connectors[i]`:

1. **Elision**: the last `elideCount` units of `phrase.figures[i-1]` are
   removed. The figure's unit vector is truncated from the end.
2. **Adjustment**: if `adjustCount != 0` and the (now-shorter) figure still
   has at least one unit, its last unit's `duration` is modified by
   `adjustCount` beats. Positive values extend the duration (the "My Life"
   case — the surface figure absorbs the tail before the next tag). Negative
   values shorten it (the "K467 pickups" case — the cadence figure ends
   early so a following pickup figure can play in the freed time). If
   `abs(adjustCount)` would push the unit's duration to zero or below, the
   adjustment is clamped (or an error is raised — TBD at implementation).
3. **Cursor flow**: the next figure's first-unit step still applies to
   the cursor *as it stands after the elision*. Elision removes some
   units' step contributions, so the cursor is at a different position
   than it would have been without elision. The next figure's first
   unit plays at `(post-elision cursor) + next_fig.units[0].step`.

Importantly, adjustment does NOT affect the cursor — it only changes
duration. The cursor after adjustment equals the cursor after elision.

### The Billy Joel "My Life" example in FigureConnector terms

Given:
- Surface figure `s = [e(step=0), e(step=0), q(step=0)]` (eighth, eighth, quarter, all at same pitch)
- Outline figure `fig1 = OutlineFigureStrategy(ss1, s)` producing 9 units
- Three tag figures `tag1`, `tag2`, `tag3` with first-unit steps -1, +2, +2 respectively

Phrase 1: `[fig1, tag1]`, connectors `[_, std::nullopt]` — pure append.
The last unit of `fig1` plays normally, then `tag1` plays starting at
`cursor + tag1.units[0].step = cursor - 1`.

Phrase 2: `[fig1, tag2]`, connectors `[_, {elideCount: 1, adjustCount: 0}]`.
The last unit of `fig1` (a quarter note) is removed. The figure now ends
with the previous eighth note, cursor is one scale-degree-step back from
where it would have been. Then `tag2` plays starting at
`(post-elision cursor) + tag2.units[0].step = cursor + 2`.

Phrase 3: `[fig1, tag3]`, connectors `[_, {elideCount: 2, adjustCount: 1.0}]`.
The last two units of `fig1` are removed (an eighth + a quarter, total 1.5
beats of duration). The now-last unit (an eighth) has its duration extended
by 1 beat, becoming a dotted quarter (0.5 + 1.0 = 1.5 beats). Cursor is two
scale-degree-steps back. Then `tag3` plays starting at
`(post-elision cursor) + tag3.units[0].step = cursor + 2`.

### The K467 bar 8 → bar 9 example (negative adjust case)

Given (simplified):
- `cadence_fig` = the bar 7-8 cadential approach, ending with a half note on G
- `pickup_fig` = three eighths + sixteenth (`eu1 eu1 eui en+`) that lead back to bar 9's chord figure
- `chord_fig_9` = bar 9's arpeggio (matches bar 5)

Phrase: `[..., cadence_fig, pickup_fig, chord_fig_9, ...]`
Connectors: `[..., nullopt, {elideCount: 0, adjustCount: -1.5}, nullopt, ...]`

The connector before `pickup_fig` shortens `cadence_fig`'s last note (the
half on G) from 2 beats to 0.5 beats (an eighth), freeing 1.5 beats for
`pickup_fig`. Cursor stays on G (no elision means no step contributions
removed). `pickup_fig` then plays in that freed 1.5 beats and leads up to
`chord_fig_9`.

This matches the brainstorming walk-through of the Billy Joel chorus as
transcribed by Matt. `elideCount` counts units; the beat totals that result
depend on the specific durations of the units being elided and extended.

### JSON round-trip

```json
{
  "name": "my_phrase",
  "figures": [ ... ],
  "connectors": [
    null,
    {"elide": 1, "adjust": 0},
    {"elide": 0, "adjust": -1.5}
  ]
}
```

`connectors[0]` is serialized as `null` (the unused slot). When the field
is absent from JSON entirely, the loader initializes `connectors` as a
vector of `std::nullopt` with the same length as `figures`. When both
`elide` and `adjust` are zero, they may be omitted from the JSON object.
A negative `adjust` value shortens the preceding figure's last unit (e.g.
to make room for a following pickup figure).

## Part 2 — OutlineFigureStrategy

### Type

```cpp
// engine/include/mforce/music/templates.h
struct OutlineFigureConfig {
  StepSequence outline;       // scale-degree deltas; cumulative, first entry
                              // typically 0 meaning "start at current cursor"
  MelodicFigure surface;      // recurring surface pattern
};

struct FigureTemplate {
  // ... existing fields ...
  std::optional<OutlineFigureConfig> outlineConfig;
};
```

```cpp
// engine/include/mforce/music/outline_strategies.h   (NEW header)
class OutlineFigureStrategy : public Strategy {
public:
  std::string name() const override { return "outline_figure"; }
  StrategyLevel level() const override { return StrategyLevel::Figure; }
  MelodicFigure realize_figure(const FigureTemplate& ft, StrategyContext& ctx) override;
};
```

### Algorithm

Given `outline = [d0, d1, d2, ...]` (cumulative deltas from the initial
cursor) and `surface` (a MelodicFigure), produce one flat MelodicFigure by
unfolding `surface.size()` copies of the surface, one per outline anchor.
The first copy's first-unit step is preserved from the surface (typically
0). Each non-first copy's first-unit step is **overwritten** to bridge the
outline delta:

```cpp
inline MelodicFigure OutlineFigureStrategy::realize_figure(
    const FigureTemplate& ft, StrategyContext& ctx) {
  if (!ft.outlineConfig) {
    std::cerr << "OutlineFigureStrategy: ft.outlineConfig is empty; returning empty figure\n";
    return MelodicFigure{};
  }
  const OutlineFigureConfig& cfg = *ft.outlineConfig;

  if (cfg.outline.size() == 0 || cfg.surface.units.empty()) {
    return MelodicFigure{};
  }

  MelodicFigure out;
  for (size_t i = 0; i < cfg.outline.size(); ++i) {
    // Copy surface units into out.
    size_t copyStart = out.units.size();
    for (const auto& u : cfg.surface.units) {
      out.units.push_back(u);
    }

    // For the first iteration, surface's own first-unit step is used as-is.
    // For subsequent iterations, the first unit's step is rewritten to
    // bridge from the cursor position at the end of the previous surface
    // copy to the new outline anchor.
    //
    // outline[i] is the delta from the previous anchor to the new anchor.
    // The previous surface copy ended at anchor[i-1] + surface.net_step().
    // The new anchor is at anchor[i-1] + outline[i].
    // So the bridging first-unit step is:
    //   outline[i] - surface.net_step()
    if (i > 0) {
      int bridge = cfg.outline[i] - cfg.surface.net_step();
      out.units[copyStart].step = bridge;
    }
  }

  return out;
}
```

The output is a single `MelodicFigure` with `outline.size() * surface.units.size()`
units. Subsequent composer code (apply_cadence, conductor playback,
FigureConnector elision) operates on it as a normal figure.

### Edge cases

- **Empty outline**: returns an empty figure. Caller warning logged.
- **Empty surface**: returns an empty figure. Caller warning logged.
- **Outline has only one entry**: the first-iteration branch alone fires;
  no bridging. The result is `surface.size()` units with no rewriting.
- **`outline[0] != 0`**: the first outline entry is ignored by the
  algorithm (the first surface copy uses its own first-unit step).
  Convention is `outline[0] == 0`, and this spec assumes the user follows
  it. If they don't, the first copy plays at `cursor + surface.units[0].step`
  as always, and the outline's first delta is silently discarded. A
  possible lint/warning could catch this but is out of scope.

### My Life example

```cpp
// Input:
//   outline = [0, -2, -3]       (cumulative: anchors at 0, -2, -5 relative to cursor)
//   surface = [e(s=0), e(s=0), q(s=0)]   (net_step = 0)
//
// Output: 9-unit figure
//   Copy 0: [e(s=0), e(s=0), q(s=0)]
//   Copy 1: first-unit step = outline[1] - surface.net_step = -2 - 0 = -2
//           [e(s=-2), e(s=0), q(s=0)]
//   Copy 2: first-unit step = outline[2] - surface.net_step = -3 - 0 = -3
//           [e(s=-3), e(s=0), q(s=0)]
//
// Concatenated: [e(s=0), e(s=0), q(s=0), e(s=-2), e(s=0), q(s=0), e(s=-3), e(s=0), q(s=0)]
```

Note: `surface.net_step()` is always 0 for the "three repeated notes at the
same pitch" surface in this example, so the bridges simplify to just
`outline[i]`. For a non-zero-net surface, the bridges would subtract the
surface's net movement.

## Part 3 — OutlinePassageStrategy

### Type

```cpp
// engine/include/mforce/music/templates.h
struct OutlinePassageConfig {
  StepSequence outline;                  // scale-degree deltas for phrase start anchors
  std::vector<PhraseTemplate> phrases;   // one per outline entry; same length as outline
};

struct PassageTemplate {
  // ... existing fields ...
  std::optional<OutlinePassageConfig> outlineConfig;
};
```

```cpp
// engine/include/mforce/music/outline_strategies.h
class OutlinePassageStrategy : public Strategy {
public:
  std::string name() const override { return "outline_passage"; }
  StrategyLevel level() const override { return StrategyLevel::Passage; }
  Passage realize_passage(const PassageTemplate& passTmpl, StrategyContext& ctx) override;
};
```

### Algorithm

The passage's starting cursor is `passTmpl.startingPitch.value()` (required
since Phase 1b — the JSON loader enforces it). The outline is walked to
produce N anchor positions; at each anchor, the corresponding phrase is
realized with `ctx.cursor` overridden to the anchor pitch, and appended to
the passage. Cursor inheritance between phrases is explicitly overridden —
each phrase starts at its outline anchor, regardless of where the previous
phrase left the cursor.

```cpp
inline Passage OutlinePassageStrategy::realize_passage(
    const PassageTemplate& passTmpl, StrategyContext& ctx) {
  Passage passage;

  if (!passTmpl.outlineConfig) {
    std::cerr << "OutlinePassageStrategy: passTmpl.outlineConfig is empty; returning empty passage\n";
    return passage;
  }
  const OutlinePassageConfig& cfg = *passTmpl.outlineConfig;

  if (cfg.outline.size() != cfg.phrases.size()) {
    std::cerr << "OutlinePassageStrategy: outline.size (" << cfg.outline.size()
              << ") != phrases.size (" << cfg.phrases.size() << "); truncating to min\n";
    // Continue with the minimum length rather than hard-failing.
  }

  // Starting cursor: passage's required startingPitch.
  if (!passTmpl.startingPitch) {
    std::cerr << "OutlinePassageStrategy: passTmpl.startingPitch is empty; returning empty passage\n";
    return passage;
  }

  PitchReader reader(ctx.scale);
  reader.set_pitch(*passTmpl.startingPitch);

  const size_t n = std::min(cfg.outline.size(), cfg.phrases.size());
  for (size_t i = 0; i < n; ++i) {
    // Advance the outline cursor by this anchor's delta.
    reader.step(cfg.outline[i]);
    Pitch anchorPitch = reader.get_pitch();

    // Clone the context with cursor forced to the outline anchor.
    // This is the "outline wins" override: ignore whatever cursor state
    // the previous phrase left, force it to the anchor.
    StrategyContext phraseCtx = ctx;
    phraseCtx.cursor = anchorPitch;

    // Dispatch to the phrase level via the composer. DefaultPhraseStrategy
    // (or whatever strategy the phrase template selects) realizes the
    // phrase starting from the cursor.
    Phrase phrase = ctx.composer->realize_phrase(cfg.phrases[i], phraseCtx);
    passage.add_phrase(std::move(phrase));

    // DO NOT carry phrase's ending cursor forward. The next iteration
    // will reset via the outline anchor again.
  }

  return passage;
}
```

### "Outline wins" semantics

Each phrase's realization gets `phraseCtx.cursor = anchorPitch`. Inside
`DefaultPhraseStrategy::realize_phrase`, the phrase's `startingPitch` is
computed as:
```cpp
phrase.startingPitch = phraseTmpl.startingPitch ? *phraseTmpl.startingPitch
                                                  : ctx.cursor;
```

**Subtle point**: if the phrase template has its own `startingPitch` set,
that overrides the outline anchor. This means if a user authors a phrase
with `"startingPitch": {"octave": 4, "pitch": "G"}` and that phrase is
used inside an OutlinePassageStrategy, the explicit startingPitch wins
over the outline.

For My Life and similar use cases, phrases should NOT have their own
`startingPitch` set when used under OutlinePassageStrategy — let the
outline drive. This is a convention the user follows; the strategy does
not enforce it.

An alternative was considered: OutlinePassageStrategy could FORCIBLY clear
each phrase's `startingPitch` override before dispatch. Rejected because
(a) it mutates the template in unexpected ways and (b) a user who
authors both an outline and a phrase startingPitch probably has a reason.
Respect the phrase's explicit choice; document the convention.

### Length mismatch

If `cfg.outline.size() != cfg.phrases.size()`, the strategy truncates to
the minimum of the two and logs a warning. An alternative was to hard-fail
at JSON load time; rejected because a runtime warning lets the user
experiment more fluidly. Load-time validation can be added later as a
lint pass if mismatches become a source of bugs.

### My Life example

```cpp
// Passage-level data:
//   passTmpl.startingPitch = Pitch(octave=4, pitch=E)    // ur-line starts on scale degree 3
//   outlineConfig.outline  = [0, -1, 1, -1]              // [3,2,3,2] relative positions
//   outlineConfig.phrases  = [P1, P2, P1, P3]            // 4 phrase templates
//
// P1, P2, P3 are PhraseTemplates, each containing:
//   figures = [fig1, tag_i]
//   connectors = [nullopt, conn_i]    // nullopt or { elideCount, adjustCount }
//
// Where:
//   fig1 is generated by OutlineFigureStrategy with ss1 and the 3-note surface
//   tag1, tag2, tag3 are the three distinct cadential-approach figures
//   conn1 = nullopt (pure append), conn2 = {elide:1, adjust:0}, conn3 = {elide:2, adjust:1.0}
//   P1 uses tag1+conn1, P2 uses tag2+conn2, P3 uses tag3+conn3
//
// At realize-passage time:
//   reader starts at E4 (scale degree 3)
//   outline[0] = 0 → anchor E4, phrase P1 plays from there
//   outline[1] = -1 → anchor D4, phrase P2 plays from there
//   outline[2] = +1 → anchor E4, phrase P1 plays again (recapitulation)
//   outline[3] = -1 → anchor D4, phrase P3 plays from there (with final cadence)
//
// 4 phrases in the output passage. P3's final CadentialApproach tag lands
// on scale degree 1 (the ur-line's theoretical fifth note) via normal
// cadence mechanisms, not a special fifth outline anchor.
```

## Part 4 — Figure-level and passage-level strategy dispatch

Phase 3 added `std::string strategy;` to `PhraseTemplate` and updated
`Composer::realize_phrase` to dispatch by name. This part does the same
for `FigureTemplate` and `PassageTemplate` so that `OutlineFigureStrategy`
and `OutlinePassageStrategy` are reachable via templates.

### FigureTemplate

```cpp
// engine/include/mforce/music/templates.h
struct FigureTemplate {
  // ... existing fields ...
  std::string strategy;   // empty = default_figure; otherwise a registered name
};
```

`Composer::realize_figure` is updated:

```cpp
MelodicFigure realize_figure(const FigureTemplate& figTmpl, StrategyContext& ctx) {
  const std::string n = figTmpl.strategy.empty() ? std::string("default_figure") : figTmpl.strategy;
  Strategy* s = registry_.get(n);
  if (!s) {
    std::cerr << "Unknown figure strategy '" << n << "', falling back to default_figure\n";
    s = registry_.get("default_figure");
  }
  return s->realize_figure(figTmpl, ctx);
}
```

When `strategy == ""`, the behavior is identical to the current code:
`registry_.get("default_figure")` returns `DefaultFigureStrategy`, which
runs its switch on `figTmpl.source`. Golden hash is preserved.

**Bit-identicality concern**: `Composer::realize_figure` is currently called
both from strategies (`DefaultPhraseStrategy::realize_phrase` → `ctx.composer->realize_figure`)
and internally from `DefaultFigureStrategy::realize_figure` (which calls
itself indirectly via the Generate-shape registry path). All call sites
continue to route through the new dispatcher. The new dispatcher must
produce the same strategy (`DefaultFigureStrategy`) for `strategy == ""`,
and the dispatcher must NOT introduce any extra RNG calls. Specifically,
the string construction `std::string("default_figure")` should not
consume any RNG or shared state.

**Implementation note**: keep the ternary simple. No extra allocations
beyond what Phase 3 already does for phrase dispatch (which did pass
verification).

### PassageTemplate

```cpp
// engine/include/mforce/music/templates.h
struct PassageTemplate {
  // ... existing fields ...
  std::string strategy;   // empty = default_passage
};
```

`Composer::realize_passage` gains the same dispatch pattern:

```cpp
Passage realize_passage(const PassageTemplate& passTmpl, StrategyContext& ctx) {
  const std::string n = passTmpl.strategy.empty() ? std::string("default_passage") : passTmpl.strategy;
  Strategy* s = registry_.get(n);
  if (!s) {
    std::cerr << "Unknown passage strategy '" << n << "', falling back to default_passage\n";
    s = registry_.get("default_passage");
  }
  return s->realize_passage(passTmpl, ctx);
}
```

Same bit-identicality expectation: for `strategy == ""`, behavior matches
the current code byte-for-byte.

### JSON round-trip for the new strategy fields

Just string emission/read, matching Phase 3's pattern:

```cpp
// FigureTemplate to_json:
if (!ft.strategy.empty()) j["strategy"] = ft.strategy;
// FigureTemplate from_json:
ft.strategy = j.value("strategy", std::string(""));

// PassageTemplate to_json:
if (!pt.strategy.empty()) j["strategy"] = pt.strategy;
// PassageTemplate from_json:
pt.strategy = j.value("strategy", std::string(""));
```

## Part 5 — Config JSON round-trip

### OutlineFigureConfig

```cpp
inline void to_json(json& j, const OutlineFigureConfig& c) {
  j["outline"] = c.outline;            // StepSequence already has to_json
  j["surface"] = c.surface;            // MelodicFigure already has to_json
}

inline void from_json(const json& j, OutlineFigureConfig& c) {
  from_json(j.at("outline"), c.outline);
  from_json(j.at("surface"), c.surface);
}
```

On `FigureTemplate`, the config is optional:

```cpp
// to_json:
if (ft.outlineConfig) j["outlineConfig"] = *ft.outlineConfig;
// from_json:
if (j.contains("outlineConfig")) {
  OutlineFigureConfig c;
  from_json(j.at("outlineConfig"), c);
  ft.outlineConfig = c;
}
```

### OutlinePassageConfig

```cpp
inline void to_json(json& j, const OutlinePassageConfig& c) {
  j["outline"] = c.outline;
  j["phrases"] = c.phrases;            // vector<PhraseTemplate> — each element round-trips normally
}

inline void from_json(const json& j, OutlinePassageConfig& c) {
  from_json(j.at("outline"), c.outline);
  const json& phrasesJson = j.at("phrases");
  c.phrases.clear();
  for (const auto& pj : phrasesJson) {
    PhraseTemplate p;
    from_json(pj, p);
    c.phrases.push_back(std::move(p));
  }
}
```

On `PassageTemplate`:

```cpp
if (pt.outlineConfig) j["outlineConfig"] = *pt.outlineConfig;
// ...
if (j.contains("outlineConfig")) {
  OutlinePassageConfig c;
  from_json(j.at("outlineConfig"), c);
  pt.outlineConfig = c;
}
```

### FigureConnector JSON

```cpp
inline void to_json(json& j, const FigureConnector& fc) {
  if (fc.elideCount != 0) j["elide"] = fc.elideCount;
  if (fc.adjustCount != 0) j["adjust"] = fc.adjustCount;
}

inline void from_json(const json& j, FigureConnector& fc) {
  fc.elideCount = j.value("elide", 0);
  fc.adjustCount = j.value("adjust", 0.0f);
}
```

Zero-valued fields omitted from output. Missing fields default to zero on
input.

### PhraseTemplate::connectors round-trip

```cpp
// to_json: emit as an array of objects or null per element.
{
  json arr = json::array();
  bool any_present = false;
  for (const auto& c : pt.connectors) {
    if (c) {
      arr.push_back(*c);
      any_present = true;
    } else {
      arr.push_back(nullptr);
    }
  }
  if (any_present) j["connectors"] = std::move(arr);
}

// from_json:
if (j.contains("connectors")) {
  pt.connectors.clear();
  for (const auto& cj : j.at("connectors")) {
    if (cj.is_null()) {
      pt.connectors.push_back(std::nullopt);
    } else {
      FigureConnector fc;
      from_json(cj, fc);
      pt.connectors.push_back(fc);
    }
  }
} else {
  // No connectors field in JSON → initialize as a vector of nullopts
  // matching figures.size().
  pt.connectors.assign(pt.figures.size(), std::nullopt);
}
```

**Length invariant**: after loading, `pt.connectors.size() == pt.figures.size()`.
If JSON provides a shorter connectors array, pad with nullopts. If longer,
truncate with a warning. (This matches the "flexible loading" pattern the
passage outline strategy uses for length mismatches.)

## Part 6 — Loader fix: `PhraseTemplate::figures` optional

Surfaced in Phase 3's smoke test: `PhraseTemplate::from_json` calls
`j.at("figures")` unconditionally, so any phrase JSON must have a
`"figures": []` field even when using a strategy that doesn't need the
figures list (Period, Sentence, and now Outline-consumed phrases where
the figures live inside the passage outline config).

Fix: change the from_json to read `figures` optionally:

```cpp
// Old:
from_json(j.at("figures"), pt.figures);

// New:
if (j.contains("figures")) {
  from_json(j.at("figures"), pt.figures);
} else {
  pt.figures.clear();
}
```

With this change, a phrase template that uses `strategy: "period_phrase"`
or similar doesn't need a `"figures": []` placeholder. The `connectors`
loading from Part 5 also defaults to an empty vector matching the (now
possibly empty) figures vector.

## Part 7 — DefaultPhraseStrategy::realize_phrase connector application

`DefaultPhraseStrategy::realize_phrase` currently walks `phraseTmpl.figures`
and realizes each figure via `ctx.composer->realize_figure(...)`, appending
each result to `phrase.figures`. This part adds connector application.

**Ordering** is important: the elision + extension must be applied to the
already-added previous figure BEFORE the current figure is realized. This
matches the cursor-model semantics — when figure `i` realizes, it sees the
cursor state after the elision has shortened the previous figure.

```cpp
for (int i = 0; i < numFigs; ++i) {
  // Apply incoming connector (if any) to the previously-added figure
  // BEFORE realizing figure i, so that figure i sees the correct cursor
  // state for any figure type that consults it (Literal, Outline).
  if (i > 0 && i < int(phraseTmpl.connectors.size()) && phraseTmpl.connectors[i]) {
    const FigureConnector& conn = *phraseTmpl.connectors[i];
    auto& prevFig = phrase.figures.back();
    if (conn.elideCount > 0 && !prevFig.units.empty()) {
      int elide = std::min(conn.elideCount, int(prevFig.units.size()));
      prevFig.units.resize(prevFig.units.size() - elide);
    }
    if (conn.adjustCount != 0 && !prevFig.units.empty()) {
      float newDur = prevFig.units.back().duration + conn.adjustCount;
      if (newDur < 0.0f) newDur = 0.0f;  // clamp rather than error for now
      prevFig.units.back().duration = newDur;
    }
  }

  // Existing shape-selection shim.
  FigureTemplate figTmpl = phraseTmpl.figures[i];
  if (phraseTmpl.function != MelodicFunction::Free
      && figTmpl.source == FigureSource::Generate
      && figTmpl.shape == FigureShape::Free) {
    figTmpl.shape = DefaultFigureStrategy::choose_shape(
        phraseTmpl.function, i, numFigs, ctx.rng->rng());
  }

  StrategyContext figCtx = ctx;
  MelodicFigure fig = ctx.composer->realize_figure(figTmpl, figCtx);
  phrase.add_figure(std::move(fig));
}
```

**Note on `ctx.cursor` staleness**: the existing Phase 1b path does not
maintain an intra-phrase running cursor. `ctx.cursor` as seen by figure
`i`'s realization is the PHRASE starting cursor, not where the previous
figure actually left the cursor. This is a pre-existing limitation and
is documented in the "Literal figure position caveat" below. The
connector-application block above correctly modifies the previous
figure's *stored* units (which the conductor will play through),
regardless of whether `ctx.cursor` reflects the running position.

### Literal figure position caveat

`DefaultFigureStrategy::realize_figure`'s Literal case (from Phase 1b
Task 1) uses `ctx.cursor` to compute scale-degree deltas for each literal
note. Under the cursor model, `ctx.cursor` as seen by a figure's
`realize_figure` should be the cursor state AT THAT FIGURE'S START — i.e.,
after all preceding figures' net_step and all elision/extension.

The current `DefaultPhraseStrategy::realize_phrase` does NOT propagate an
intra-phrase cursor into `ctx.cursor` when realizing each figure. It
passes the PHRASE starting cursor to every figure. That's a pre-existing
limitation that affects Literal figures inside phrases with more than one
figure: only the first figure sees a correct cursor; subsequent figures
see a stale cursor.

**Decision for this spec**: out of scope. Literal figures inside multi-
figure phrases is an edge case with no test cases yet. If we hit it in
practice, we'll fix it as a standalone Literal-figure correctness item.
This spec does not add intra-phrase cursor tracking.

For OutlineFigureStrategy, `ctx.cursor` is the phrase starting cursor,
which is the same as the outline's starting anchor because the outline
figure is always the first figure in the phrase in the My Life use case.
If future use cases put the outline figure as the second-or-later figure
in a phrase, cursor tracking will matter — but again, that's not this
spec's scope.

### Backward-compat check

When a template has no connectors (empty vector, or all nullopts), the
loop's connector-application block is a no-op. `phrase.add_figure(fig)`
is called once per figure with no mutation of previous figures. Behavior
matches Phase 3 exactly. Golden hash preserved.

## Part 8 — Registration in Composer

`Composer::Composer(...)` registers the two new strategies alongside the
existing Defaults and Phase 2 shapes:

```cpp
// Outline strategies (Phase 6)
registry_.register_strategy(std::make_unique<OutlineFigureStrategy>());
registry_.register_strategy(std::make_unique<OutlinePassageStrategy>());
```

`composer.h` includes `outline_strategies.h`.

## Part 9 — Verification

### Golden hash preservation

`template_golden_phase1a.json` does not use any outline strategy, does not
use any connector, and does not set the `strategy` field on any
FigureTemplate, PhraseTemplate, or PassageTemplate. After this spec is
implemented, the golden render must produce the same hash
(`0815d32e...`) it produces today. Any drift is a bug in the new code
paths that leaked into the default path.

### Smoke test template: My Life chorus

Create `patches/test_outline_my_life.json` exercising:

- `OutlinePassageStrategy` at the passage level with a 4-entry outline
- 4 PhraseTemplate instances inside the outline (two of them identical)
- Each PhraseTemplate containing a `figures` list with 2 entries: an
  outline figure (`OutlineFigureStrategy`) and a cadential-approach tag
- FigureConnectors on phrases 2 and 3 to encode elision + extension
- `PassageTemplate::startingPitch` set to E4 in C major

Render. Verify:
- No parse errors
- Non-silent + non-crashing audio (rms > 0.01, reasonable peak)
- Composed JSON shows 4 phrases in the passage
- Phrase 0 and phrase 2 have identical figure structure (same outline,
  same tag)
- Phrase 1 and phrase 3 have different tags
- Phrase 1's outline figure has 9 units; phrase 2's has 8 (1 elided);
  phrase 3's has 7 (2 elided), with unit 6's duration = dotted quarter

This template is committed as a smoke test but NOT pinned as a golden.
Matt auditions it on return and decides whether to pin or adjust.

### Per-task verification during implementation

Each task in the derived implementation plan re-renders the existing
golden (`template_golden_phase1a.json`) and checks the hash. A
bit-identical result is the signal that the new code paths (dispatch,
new config fields, parallel vectors) don't leak into the default path.

## Part 10 — File structure

| File | Change |
|---|---|
| `engine/include/mforce/music/figures.h` | Re-add `struct FigureConnector` (simpler than pre-Phase-1b: only `elideCount` + `adjustCount`). |
| `engine/include/mforce/music/templates.h` | Add `OutlineFigureConfig`, `OutlinePassageConfig`. Add `FigureTemplate::strategy`, `FigureTemplate::outlineConfig`, `PassageTemplate::strategy`, `PassageTemplate::outlineConfig`, `PhraseTemplate::connectors`. |
| `engine/include/mforce/music/templates_json.h` | JSON round-trip for all the new fields/types above. Fix `PhraseTemplate::from_json` to make `figures` optional. |
| `engine/include/mforce/music/music_json.h` | Re-add `to_json` / `from_json` for `FigureConnector`. |
| `engine/include/mforce/music/outline_strategies.h` | NEW. `OutlineFigureStrategy` and `OutlinePassageStrategy` class declarations + inline definitions. |
| `engine/include/mforce/music/composer.h` | Include `outline_strategies.h`. Register both new strategies. Update `realize_figure` and `realize_passage` dispatchers to consult the new `strategy` fields. |
| `engine/include/mforce/music/default_strategies.h` | Update `DefaultPhraseStrategy::realize_phrase` to apply connectors (elide + adjust on the previous figure). |
| `patches/test_outline_my_life.json` | NEW. Smoke test template. Not pinned as golden. |

No changes to `conductor.h`, `dun_parser.h`, `strategy.h`, `strategy_registry.h`,
`shape_strategies.h`, `phrase_strategies.h`, `classical_composer.h`,
`structure.h`, `basics.h`, `pitch_reader.h`, `tools/mforce_cli/main.cpp`.

## Part 11 — Out of scope / explicit non-goals

- No `OutlinePhraseStrategy`.
- No intra-phrase cursor tracking for Literal figures in multi-figure phrases.
- No load-time validation of `connectors.size() == figures.size()` — loader auto-pads/truncates with a warning.
- No hard-fail on outline/phrases length mismatch in `OutlinePassageConfig` — strategy logs a warning and truncates to the minimum.
- No harmony awareness in outline strategies. The StepSequence is
  provided by the author (or a future helper tool); the strategy doesn't
  know about chords.
- No StepSequence authoring helpers ("generate this ur-line from a chord
  progression"). That's a separate layer that would live above outline
  strategies if built.
- No DURN parser changes. DURN continues to absorb `Xn` tokens into
  first-unit steps; complex joint patterns are template-authored.
- No audition step — mechanical verification only (non-silent + non-crash
  + hash-preservation of the default golden + smoke test runs cleanly).
  Matt audits the smoke test WAV on return.
- No deletion of the pre-existing `FigureBuilder` shape methods (still
  duplicated between `figures.h` and `shape_strategies.h` after Phase 2).
- No Outline* nested depth (recursive outlines — outline whose surface is
  itself an outline). If needed later, the type system already allows it
  because OutlineFigureStrategy returns a MelodicFigure that could be
  used as a `surface` in another OutlineFigureStrategy invocation.

## Summary — what ships

At the end of the plan derived from this spec, `main` will have:

1. A resurrected, simpler `FigureConnector` type (elideCount + extendCount, optional per-position).
2. `OutlineFigureStrategy` and `OutlinePassageStrategy` registered in the composer.
3. Figure-level and passage-level strategy dispatch by name (`strategy` field on FigureTemplate and PassageTemplate).
4. `PhraseTemplate::from_json` accepting templates without a `figures` field.
5. A smoke test template exercising the My Life chorus construction: 4-phrase outline passage with an outline figure in each phrase, 3 distinct tag figures, connectors for elision + extension on phrases 2 and 3.
6. Golden hash `0815d32e...` bit-identically preserved (the golden doesn't use any new feature).

No audition step during execution; Matt auditions the smoke test WAV on return.
