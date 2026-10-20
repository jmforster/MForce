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
// delays, not a pin: this node serves the list by name and advances the
// entries itself, because each is read once per sample at its own delay.
// It becomes a Processor when the list pin kind arrives (checkpoint 3.1).
struct MultiSource final : ValueSource {
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
  bool set_param(std::string_view name, std::shared_ptr<ValueSource> src) override {
    if (name != "source") return ValueSource::set_param(name, std::move(src));
    add(std::move(src));
    return true;
  }

  bool clear_param(std::string_view name) override {
    if (name != "source") return ValueSource::clear_param(name);
    entries.clear();
    return true;
  }

  void add(std::shared_ptr<ValueSource> src, float weight = 1.0f, int delay = 0) {
    entries.push_back({std::move(src), weight, delay});
  }

  void on_prepare(const RenderContext& ctx) override {
    for (auto& e : entries)
      e.source->prepare(ctx);
  }

  void on_start(int frames) override {
    ptr_ = -1;
    for (auto& e : entries)
      e.source->start(frames + e.delaySamples);
  }

  float compute() override {
    ++ptr_;
    float sum = 0.0f;
    for (auto& e : entries) {
      float v = e.source->next();
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
