#pragma once
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
};

struct InputDescriptor {
  const char* name;
  bool multi{false};  // true = accepts multiple connections (e.g. spectra, stages)
};

enum class ConfigType { Float, Int, Bool };

struct ConfigDescriptor {
  const char* name;
  ConfigType type;
  float default_value;
  float min_value;
  float max_value;   // for Float/Int; ignored for Bool
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
  virtual std::span<const ParamDescriptor> param_descriptors() const { return {}; }
  virtual void set_param(std::string_view /*name*/, std::shared_ptr<ValueSource> /*src*/) {}
  virtual std::shared_ptr<ValueSource> get_param(std::string_view /*name*/) const { return nullptr; }

  // Multi-input support: add/clear for pins that accept multiple connections
  virtual void add_param(std::string_view /*name*/, std::shared_ptr<ValueSource> /*src*/) {}
  virtual void clear_param(std::string_view /*name*/) {}

  // Input-only pins — connectable but no editable value (formant, partials, source on filters)
  virtual std::span<const InputDescriptor> input_descriptors() const { return {}; }

  // Config params — non-connectable scalars (int, float, bool)
  virtual std::span<const ConfigDescriptor> config_descriptors() const { return {}; }
  virtual void set_config(std::string_view /*name*/, float /*value*/) {}
  virtual float get_config(std::string_view /*name*/) const { return 0.0f; }

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
struct RefSource final : ValueSource {
  std::shared_ptr<ValueSource> source;

  explicit RefSource(std::shared_ptr<ValueSource> src) : source(std::move(src)) {}

  void prepare(const RenderContext& /*ctx*/, int /*frames*/) override {} // primary consumer prepares the real source
  float next() override { return source ? source->current() : 0.0f; }
  float current() const override { return source ? source->current() : 0.0f; }

  const char* type_name() const override { return "RefSource"; }
  SourceCategory category() const override { return SourceCategory::Utility; }
};

} // namespace mforce
