# Section-level key — design (comp backlog #13)

Wolfie, 2026-08-06.

## The defect

`sections[].keyName` is silently ignored. `PieceTemplate::SectionTemplate`
(templates.h:582) has `name`, `beats`, `scaleOverride`, `progressionName`,
`chordProgression`, `keyContexts`, `styleName` — and no `keyName`. An author
who writes it gets a piece in the piece key with no warning. Found by
`--lint-template` in run 15; the run-13 probe
(`renders/probe_keyctx/cross_section_tmpl.json`) sets it on three sections
(C / G / D) and got nothing.

There is a second, quieter half of the same defect. `setup_piece_`
(composer.h:269-271) computes the section scale as

```cpp
Scale secScale = sd.scaleOverride.empty()
  ? piece.key.scale
  : Scale::get(tmpl.keyName, sd.scaleOverride);
```

— the section's `scaleOverride` is realized **at the piece tonic**. So a
section can change its scale TYPE but has no way to state its TONIC. That is
exactly the hole `keyName` was reaching for.

## The choice

The backlog states it as either/or: desugar to a beat-0 `keyContext`, or
reject loudly.

**Decision: desugar, and throw only on genuine authoring errors.**

Rejecting loudly removes the silent failure but offers nothing in its place —
the author still has to write a four-line `keyContexts` array to say "this
section is in G", which is the single most ordinary thing to want. There is
already precedent for shorthand-desugars-to-general one level up:
`PieceTemplate.keyName` + `scaleName` desugar into `piece.key` via
`Key::get(keyName + " " + scaleName)`.

Two things do throw, because they are unambiguous mistakes rather than
shorthand:

- an unparseable key name (`Key::get` already throws `Unknown Key: ...`);
- `keyName` together with an explicit `keyContexts` entry at beat <= 0 —
  two different statements about the same beat.

`keyName` plus a LATER `keyContext` is legal and useful: "this section is in
G, and modulates to D at beat 16."

## Semantics

Where the desugar lives matters. It happens at **compose time**
(`setup_piece_`), not at parse time. If `from_json` expanded `keyName` into a
`keyContexts` entry, `to_json` would write both, and re-reading the engine's
own output would then trip the beat-0 conflict throw. Parse stores the field;
serialize writes it back; the template round-trips byte-for-byte, which is
also what makes `--lint-template` stop reporting it.

Resolution order inside `setup_piece_`, per section:

| quantity | value |
|---|---|
| section tonic | `sd.keyName` if set, else `tmpl.keyName` |
| section scale type | `sd.scaleOverride` if set, else `tmpl.scaleName` |
| `section.scale` | `Scale::get(tonic, type)` |
| prepended `KeyContext` | beat 0, `Key::get(tonic + " " + Major\|Minor)`; carries `scaleOverride = section.scale` only when the type is neither Major nor Minor |

`sd.keyName` accepts both a bare tonic (`"G"`) and a full key name
(`"G Minor"`); a space means the type is stated and `scaleOverride` /
`scaleName` do not supply it.

The generalization of `Scale::get(tmpl.keyName, ...)` to
`Scale::get(sectionTonic, ...)` changes behaviour ONLY when `keyName` is
present, since without it `sectionTonic == tmpl.keyName` textually.

## What this does NOT do

Carried forward from run 13, and to be measured rather than asserted: key
awareness snaps the running cursor's PITCH into the new scale, it does not
move the cursor to the new TONIC. So a section `keyName` changes which
accidentals appear; it does not transpose the section. A template that wants
audible modulation must still offset the passage's `startingPitch` by the key
distance. The probe below reports what actually comes out.

## Verification

1. Build `mforce_cli` + `mforce_ui`; `mforce_ui --stamp` exits 0.
2. `--lint-template` over every template: `sections[].keyName` no longer
   appears as DROPPED, and no new findings appear.
3. Null test: every existing renderable template is byte-identical, because
   no template in the repo sets `sections[].keyName` today (verified by a
   repo-wide JSON scan) — so this must be a strict no-op at HEAD.
4. Probe: re-render `cross_section_tmpl.json` (C / G / D) and diff the
   realized pitches per section. Expect F# to appear in S1 and F#/C# in S2 if
   the desugar reaches realization; report whatever actually happens.
5. Negative probe: `keyName` + a beat-0 `keyContext` throws by name; a
   garbage key name throws by name.
