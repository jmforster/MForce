#pragma once
#include <cmath>
#include <memory>
#include <span>
#include <string_view>
#include <vector>
#include "mforce/core/render_context.h"

namespace mforce {

// ---------------------------------------------------------------------------
// Self-description types for generic UI and serialization
// ---------------------------------------------------------------------------

enum class SourceCategory {
  Oscillator,   // SineSource, SawSource, PulseSource, FMSource, WavetableSource, HybridKS...
  Generator,    // RedNoiseSource, WanderNoise*, WhiteNoise, SegmentSource...
  Modulator,    // VarSource, RangeSource, RepeatingSource, Vibrato...
  Envelope,     // Envelope, PhasedValueSource
  Filter,       // BWLowpass, BWHighpass, BWBandpass, Delay
  Combiner,     // CombinedSource, CrossfadeSource, MultiSource
  Additive,     // AdditiveSource, FullAdditiveSource, Formant...
  Utility       // ConstantSource, StaticVarSource, StaticRangeSource
};

struct ParamDescriptor {
  const char* name;
  float default_value;
  float min_value;
  float max_value;
  // Optional UI advisory tag. Short, shown dim next to the pin name in the
  // inspector (e.g. "hz", "0-1", "semi", "cycles"). Advisory only — no
  // enforcement at connect-time. Leave null when the param's name already
  // tells you the expected range.
  const char* hint = nullptr;
};

struct InputDescriptor {
  const char* name;
  bool multi{false};  // true = accepts multiple connections (e.g. spectra, stages)
  // Optional UI advisory tag — same semantics as ParamDescriptor::hint.
  const char* hint = nullptr;
};

enum class SettingType { Float, Int, Bool };

struct SettingDescriptor {
  const char* name;
  SettingType type;
  float default_value;
  float min_value;
  float max_value;   // for Float/Int; ignored for Bool
  // Optional: null-terminated array of display labels for Int settings that
  // represent an enum. When set, the UI renders a dropdown instead of a
  // numeric input. The stored value remains the integer index.
  const char* const* enum_labels = nullptr;
};

// Array-of-floats params. The UI groups arrays sharing a non-null groupName
// into a single parallel-columns table (e.g. ExplicitPartials has four arrays
// — mult1/mult2/ampl1/ampl2 — all grouped as "partials" and kept equal length).
// Standalone arrays (groupName == nullptr) render as a single editable list.
struct ArrayDescriptor {
  const char* name;
  const char* groupName;      // nullptr = standalone; non-null = table group key
  float default_value;        // value for newly appended rows
  float min_value;
  float max_value;
};

// ---------------------------------------------------------------------------
// Base interface for all DSP value sources
// ---------------------------------------------------------------------------

struct ValueSource {
  virtual ~ValueSource() = default;
  virtual void prepare(const RenderContext& /*ctx*/, int /*frames*/) {}
  virtual float next() = 0;
  virtual float current() const = 0;

  // Self-description — defaults allow incremental adoption
  virtual const char* type_name() const { return "Unknown"; }
  virtual SourceCategory category() const { return SourceCategory::Utility; }

  // False when this node reads its `frequency` pin ONCE at note start and
  // never again (KSPianoString: init_note), so an articulated/bent frequency
  // is silently inert on it. The loader uses this to warn by name at load —
  // backlog 26a made loud (plan_perform_source_p3.md T2). Default true:
  // ordinary pulled-per-sample consumption tracks a moving frequency.
  virtual bool tracks_frequency_live() const { return true; }
  virtual std::span<const ParamDescriptor> param_descriptors() const { return {}; }
  virtual void set_param(std::string_view /*name*/, std::shared_ptr<ValueSource> /*src*/) {}
  virtual std::shared_ptr<ValueSource> get_param(std::string_view /*name*/) const { return nullptr; }

  // Multi-input support: add/clear for pins that accept multiple connections
  virtual void add_param(std::string_view /*name*/, std::shared_ptr<ValueSource> /*src*/) {}
  virtual void clear_param(std::string_view /*name*/) {}

  // Input-only pins — connectable but no editable value (formant, partials, source on filters)
  virtual std::span<const InputDescriptor> input_descriptors() const { return {}; }

  // Config params — non-connectable scalars (int, float, bool)
  virtual std::span<const SettingDescriptor> setting_descriptors() const { return {}; }
  virtual void set_setting(std::string_view /*name*/, float /*value*/) {}
  virtual float get_setting(std::string_view /*name*/) const { return 0.0f; }

  // Array params — user-editable float vectors (e.g. ExplicitPartials multipliers,
  // FixedSpectrum/BandSpectrum gains). Grouped arrays (shared groupName) must be
  // kept equal length by the node's set_array() implementation.
  virtual std::span<const ArrayDescriptor> array_descriptors() const { return {}; }
  virtual void set_array(std::string_view /*name*/, std::vector<float> /*values*/) {}
  virtual std::vector<float> get_array(std::string_view /*name*/) const { return {}; }
};

struct ConstantSource final : ValueSource {
  explicit ConstantSource(float v) : v_(v), cur_(v) {}
  void set(float v) { v_ = v; }
  float next() override { cur_ = v_; return cur_; }
  float current() const override { return cur_; }

  const char* type_name() const override { return "Constant"; }
  SourceCategory category() const override { return SourceCategory::Utility; }
private:
  float v_{0.0f};
  float cur_{0.0f};
};

// Transparent wrapper for shared sources with multiple consumers.
// The primary consumer calls next() on the real source; secondary consumers
// use a RefSource which just reads current() without advancing.
// Created automatically by the UI when multiple inputs wire to the same output.
// guard=true is the TAP form (feedback_loop_design.md): reads are armored —
// non-finite scrubbed to 0, clamped to +-8 — so a runaway loop saturates
// audibly instead of poisoning the graph. The z-1 a tap provides is
// positional (tap consumers evaluate before their source each tick), not
// implemented here.
struct RefSource final : ValueSource {
  std::shared_ptr<ValueSource> source;
  bool guard{false};

  explicit RefSource(std::shared_ptr<ValueSource> src, bool g = false)
    : source(std::move(src)), guard(g) {}

  void prepare(const RenderContext& /*ctx*/, int /*frames*/) override {} // primary consumer prepares the real source
  float next() override { return read(); }
  float current() const override { return read(); }

  const char* type_name() const override { return "RefSource"; }
  SourceCategory category() const override { return SourceCategory::Utility; }

private:
  float read() const {
    float v = source ? source->current() : 0.0f;
    if (!guard) return v;
    if (!std::isfinite(v)) return 0.0f;
    return v < -8.0f ? -8.0f : (v > 8.0f ? 8.0f : v);
  }
};

} // namespace mforce
