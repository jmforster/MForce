#pragma once
#include "mforce/core/dsp_value_source.h"
#include <memory>
#include <vector>

namespace mforce {

// Ported from C# MForce.Sound.Source.MultiSource
// Weighted sum of ValueSources with optional per-source sample delay.
// Used for polyphony (overlapping notes) and composite instruments (WTString).
//
// The `source` input is a list, held in `entries` with its weights and
// delays, not a single pin: the inherited primary input stays unwired, and
// this node serves the list by name until list pins arrive. It advances the
// entries itself because each is read once per sample at its own delay.
struct MultiSource final : Processor {
  struct Entry {
    std::shared_ptr<ValueSource> source;
    float weight{1.0f};
    int delaySamples{0};
  };

  std::vector<Entry> entries;

  const char* type_name() const override { return "MultiSource"; }
  SourceCategory category() const override { return SourceCategory::Combiner; }

  std::span<const InputDescriptor> input_descriptors() const override {
    static constexpr InputDescriptor descs[] = {
      {"source", true},  // multi-input: wire multiple sources
    };
    return descs;
  }

  // Wiring `source` appends (add_param routes here through the base).
  void set_param(std::string_view name, std::shared_ptr<ValueSource> src) override {
    if (name == "source") add(std::move(src));
    else Processor::set_param(name, std::move(src));
  }

  void clear_param(std::string_view name) override {
    if (name == "source") entries.clear();
    else Processor::clear_param(name);
  }

  void add(std::shared_ptr<ValueSource> src, float weight = 1.0f, int delay = 0) {
    entries.push_back({std::move(src), weight, delay});
  }

  void on_prepare(const RenderContext& ctx, int frames) override {
    ptr_ = -1;
    for (auto& e : entries)
      e.source->prepare(ctx, frames + e.delaySamples);
  }

  float compute() override {
    ++ptr_;
    float sum = 0.0f;
    for (auto& e : entries) {
      float v = e.source->advance();
      if (ptr_ >= e.delaySamples) {
        sum += v * e.weight;
      }
    }
    return sum;
  }

private:
  int ptr_{-1};
};

} // namespace mforce
