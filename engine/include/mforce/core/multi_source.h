#pragma once
#include "mforce/core/dsp_value_source.h"
#include <memory>
#include <vector>

namespace mforce {

// Ported from C# MForce.Sound.Source.MultiSource
// Weighted sum of ValueSources with optional per-source sample delay.
// Used for polyphony (overlapping notes) and composite instruments (WTString).
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

  void set_param(std::string_view name, std::shared_ptr<ValueSource> src) override {
    if (name == "source") add(std::move(src));
  }

  void add_param(std::string_view name, std::shared_ptr<ValueSource> src) override {
    if (name == "source") add(std::move(src));
  }

  void clear_param(std::string_view name) override {
    if (name == "source") entries.clear();
  }

  void add(std::shared_ptr<ValueSource> src, float weight = 1.0f, int delay = 0) {
    entries.push_back({std::move(src), weight, delay});
  }

  void prepare(const RenderContext& ctx, int frames) override {
    ptr_ = -1;
    for (auto& e : entries)
      e.source->prepare(ctx, frames + e.delaySamples);
  }

  float next() override {
    ++ptr_;
    float sum = 0.0f;
    for (auto& e : entries) {
      float v = e.source->next();
      if (ptr_ >= e.delaySamples) {
        sum += v * e.weight;
      }
    }
    cur_ = sum;
    return cur_;
  }

  float current() const override { return cur_; }

  void set_weight(int index, float w) { entries[index].weight = w; }
  float get_weight(int index) const { return entries[index].weight; }

private:
  int ptr_{-1};
  float cur_{0.0f};
};

} // namespace mforce
