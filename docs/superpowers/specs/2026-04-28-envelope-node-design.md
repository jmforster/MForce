# Multi-stage Envelope Node — Design Spec

**Date:** 2026-04-28
**Status:** Approved (Matt)
**Predecessor:** `392f623 fix(envelope): guard next() against pre-prepare invocation`

---

## Background

The bare `Envelope` value source (`engine/include/mforce/core/envelope.h`) supports an arbitrary list of `Stage` structs (Ramp + percent / minSec / maxSec). Today only the preset subclasses (`AREnvelope`, `ASEnvelope`, `ASREnvelope`, `ADSEnvelope`, `ADREnvelope`, `ADSREnvelope`) in `envelope_presets.h` are user-editable in the UI, via small `config_descriptors()` surfaces that expose a couple of scalars (e.g. `attack`, `sustainLevel`).

The bare `Envelope` type is registered (`engine/src/source_registrations.cpp:181`) and appears in the menu (`Envelopes > Envelope`), but the UI hardcodes `ADSREnvelope` for it (`tools/mforce_ui/main.cpp:227-230`) and writes `params.preset = "adsr"` to JSON — so creating "Envelope" from the menu is identical to creating "ADSR" today.

The comment at `envelope_presets.h:13` says: *"The full table-based Envelope editor will expose per-stage timing."* This spec implements that.

## Goal

Replace the bare `Envelope` UI node with a real multi-stage editor: an inspector table where the user can add/remove stages and edit per-stage values. Preset envelope nodes are untouched and keep working as today.

## Non-goals

- Curve preview in the inspector
- Drag-to-reorder stages
- Loop points / sustain-on-key / retrigger
- Migrating existing patches saved with `preset: "adsr"` / `"ar"` to multi-stage form (compat: legacy form keeps loading; new saves use multi-stage form)

---

## Engine changes

### `engine/include/mforce/core/envelope.h`

Add to the bare `Envelope` struct (factories `make_ar` / `make_adsr` and existing `add_stage(Stage)` stay unchanged):

```cpp
int stage_count() const { return int(stages_.size()); }
Stage&       stage(int i)       { return stages_[i]; }
const Stage& stage(int i) const { return stages_[i]; }

// Append a stage with sensible defaults. startVal continues the prior
// stage's endVal (or 0 if empty).
void add_stage_default() {
  Stage s;
  s.ramp.type     = RampType::Linear;
  s.ramp.power    = 0.0f;
  s.ramp.holdPct  = 0.0f;
  s.ramp.startVal = stages_.empty() ? 0.0f : stages_.back().ramp.endVal;
  s.ramp.endVal   = 0.0f;
  s.percent = 0.2f;
  s.minSec  = 0.0f;
  s.maxSec  = 99.0f;
  stages_.push_back(s);
}

void remove_stage(int i) {
  if (i >= 0 && i < int(stages_.size())) stages_.erase(stages_.begin() + i);
}
```

The presets in `envelope_presets.h` are unchanged.

---

## Patch loader changes

### `engine/src/patch_loader.cpp` (currently lines 258-274)

Add a `stages` branch ahead of the `preset` branch. Existing `preset: "ar"` / `"adsr"` branches stay for backward compat.

```cpp
else if (type == "Envelope") {
  if (!pp) throw std::runtime_error("Envelope requires params");
  const auto& p = *pp;

  if (p.contains("stages")) {
    auto env = std::make_shared<Envelope>(sampleRate);
    for (const auto& sj : p["stages"]) {
      Envelope::Stage s;
      s.ramp.startVal = sj.value("startVal", 0.0f);
      s.ramp.endVal   = sj.value("endVal",   0.0f);
      s.ramp.power    = sj.value("power",    0.0f);
      s.ramp.holdPct  = sj.value("holdPct",  0.0f);
      std::string t   = sj.value("type", std::string("Linear"));
      s.ramp.type = (t == "Expo")        ? RampType::Expo
                  : (t == "InverseExpo") ? RampType::InverseExpo
                  : (t == "Sine")        ? RampType::Sine
                                         : RampType::Linear;
      s.percent = sj.value("percent", 0.0f);
      s.minSec  = sj.value("minSec",  0.0f);
      s.maxSec  = sj.value("maxSec",  0.0f);
      env->add_stage(s);
    }
    valueNodes[id] = env;
  } else {
    // Existing preset path — unchanged
    std::string preset = p.value("preset", std::string("ar"));
    /* ...existing make_ar / make_adsr branches... */
  }
}
```

---

## UI changes

All in `tools/mforce_ui/main.cpp`.

### `init_dsp` — `NT_ENVELOPE` case (lines 227-230)

Construct a bare `Envelope` with a default 4-stage ADSR shape so a freshly-added node starts with something audible:

```cpp
if (typeName == NT_ENVELOPE) {
  dspSource = std::make_shared<Envelope>(Envelope::make_adsr(DSP_SAMPLE_RATE,
    0.05f, 0.1f, 0.7f, 0.2f));
  return;
}
```

(Editing then operates on this live object via the new accessors.)

### Inspector stage table (around line 3260, alongside FormantSpectrum's table)

For `node->typeName == NT_ENVELOPE`, render an ImGui table reading/writing live via `env->stage(i)`:

- Columns (in order): `pct`, `startVal`, `endVal`, `type`, `power`, `holdPct`, `minSec`, `maxSec`, `[x]`
- One row per stage
- `type` is a `Combo` over { Linear, Expo, InverseExpo, Sine }
- Other columns are `DragFloat`
- `[x]` button per row → `env->remove_stage(i)`
- Below the table: `[+ Add Stage]` → `env->add_stage_default()`
- Set `s_graphDirty = true` on any change

(Modeled on the existing FormantSpectrum row table at lines 3223-3261, but reads/writes the live DSP object directly rather than via a parallel UI cache — `Envelope::Stage` is plain data and existing UI paths already mutate `dspSource` from the UI thread.)

### Save (lines 1251-1255 and 1419-1422 — two parallel save paths)

Replace each `NT_ENVELOPE` block:

```cpp
if (node.typeName == NT_ENVELOPE) {
  if (auto* env = dynamic_cast<Envelope*>(node.dspSource.get())) {
    if (!jnode.contains("params")) jnode["params"] = json::object();
    json stages = json::array();
    for (int i = 0; i < env->stage_count(); ++i) {
      const auto& s = env->stage(i);
      const char* t = (s.ramp.type == RampType::Expo)        ? "Expo"
                    : (s.ramp.type == RampType::InverseExpo) ? "InverseExpo"
                    : (s.ramp.type == RampType::Sine)        ? "Sine"
                                                              : "Linear";
      stages.push_back({
        {"percent",  s.percent},
        {"startVal", s.ramp.startVal},
        {"endVal",   s.ramp.endVal},
        {"type",     t},
        {"power",    s.ramp.power},
        {"holdPct",  s.ramp.holdPct},
        {"minSec",   s.minSec},
        {"maxSec",   s.maxSec},
      });
    }
    jnode["params"]["stages"] = stages;
  }
}
```

### Load (around lines 822-853)

After the generic params pass, special-case `NT_ENVELOPE` with a `stages` array — rebuild the live `Envelope` from JSON.

```cpp
if (gn.typeName == NT_ENVELOPE && jnode.contains("params") &&
    jnode["params"].contains("stages")) {
  if (auto* env = dynamic_cast<Envelope*>(gn.dspSource.get())) {
    // Replace stages from JSON
    *env = Envelope(DSP_SAMPLE_RATE);
    for (const auto& sj : jnode["params"]["stages"]) {
      Envelope::Stage s;
      s.ramp.startVal = sj.value("startVal", 0.0f);
      s.ramp.endVal   = sj.value("endVal",   0.0f);
      s.ramp.power    = sj.value("power",    0.0f);
      s.ramp.holdPct  = sj.value("holdPct",  0.0f);
      std::string t   = sj.value("type", std::string("Linear"));
      s.ramp.type = (t == "Expo")        ? RampType::Expo
                  : (t == "InverseExpo") ? RampType::InverseExpo
                  : (t == "Sine")        ? RampType::Sine
                                         : RampType::Linear;
      s.percent = sj.value("percent", 0.0f);
      s.minSec  = sj.value("minSec",  0.0f);
      s.maxSec  = sj.value("maxSec",  0.0f);
      env->add_stage(s);
    }
  }
}
```

For backward compat with `preset: "adsr"` / `"ar"` patches loaded into the UI: leave the existing flow alone — the patch loader already builds the equivalent `Envelope` via `make_adsr` / `make_ar`, so the inspector will simply show the 4-stage (or 2-stage) shape.

---

## JSON examples

### New form (multi-stage)

```json
{ "id": "env1", "type": "Envelope",
  "params": {
    "stages": [
      {"percent":0.05, "startVal":0,   "endVal":1,   "type":"Linear", "power":0, "holdPct":0, "minSec":0.05,  "maxSec":1.0},
      {"percent":0.1,  "startVal":1,   "endVal":0.7, "type":"Linear", "power":0, "holdPct":0, "minSec":0.025, "maxSec":0.5},
      {"percent":0,    "startVal":0.7, "endVal":0.7, "type":"Linear", "power":0, "holdPct":0, "minSec":0,     "maxSec":0},
      {"percent":0.2,  "startVal":0.7, "endVal":0,   "type":"Sine",   "power":0, "holdPct":0, "minSec":0,     "maxSec":0}
    ]
  } }
```

### Legacy form (preset) — still loads

```json
{ "id": "env1", "type": "Envelope",
  "params": { "preset": "adsr", "attack":0.05, "decay":0.1, "sustainLevel":0.7, "release":0.2 } }
```

---

## Backward compatibility

- Existing patches with `params.preset` ("ar" / "adsr") and scalar fields → loader uses the existing `make_ar` / `make_adsr` branches. UI inspector will display the resulting 2- or 4-stage shape. Saving from the UI re-emits as the new `stages` form (no migration needed, no two-way migration).
- Existing patches with `type: "ADSREnvelope"` / `"AREnvelope"` / etc. → unchanged; preset subclasses keep their dedicated config UIs and JSON form.
- A `type: "Envelope"` patch with neither `stages` nor `preset` → loader treats `preset` as default `"ar"` (existing behavior).

---

## Testing

1. Build mforce_cli and mforce_ui in Release.
2. Render a patch with a bare `Envelope` (or use `patches/SaveTest.json`) — confirm waveform unchanged.
3. UI round-trip: open a patch, confirm bare Envelope shows the stage table; add a stage; edit values; save; reopen; confirm stages survive.
4. Render an Envelope-driven patch with the new multi-stage form and the equivalent legacy `preset: "adsr"` form — they should sound identical.
