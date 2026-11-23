#pragma once
#include "mforce/core/dsp_value_source.h"
#include <memory>
#include <vector>

namespace mforce {

// Follows the C# MForce.Sound.Source.MultiSource
// Weighted sum of ValueSources with optional per-source sample delay.
// Used for polyphony (overlapping notes) and composite instruments (WTString).
//
// The `source` pin is a list of feeds, and the two arrays beside it give
// each feed, by position, its gain and its start delay. The node prepares,
// starts and advances its feeds itself, because each is read once per
// sample at its own delay. It is a ValueSource, not a Processor: it has no
// single `source` input for a Processor to own.
struct MultiSource final : ValueSource {
  InputList source{this, "source"};
  // Each feed's gain and start delay in samples, by position in `source`;
  // a feed past an array's end takes the default (1 and 0).
  Array weights{this, "weights", nullptr, 1.0f, -100.0f, 100.0f};
  Array delays {this, "delays",  nullptr, 0.0f, 0.0f, 960000.0f};

  const char* type_name() const override { return "MultiSource"; }

  void on_input_list_changed(std::string_view) override { rebuild_entries_(); }
  void on_array_changed(std::string_view) override { rebuild_entries_(); }

  void on_prepare(const RenderContext& ctx) override {
    for (auto& e : entries_)
      e.source->prepare(ctx);
  }

  void on_start(int frames) override {
    ptr_ = -1;
    for (auto& e : entries_)
      e.source->start(frames + e.delaySamples);
  }

  float compute() override {
    ++ptr_;
    float sum = 0.0f;
    for (auto& e : entries_) {
      float v = e.source->next();
      if (ptr_ >= e.delaySamples) {
        sum += v * e.weight;
      }
    }
    return sum;
  }

private:
  struct Entry {
    std::shared_ptr<ValueSource> source;
    float weight{1.0f};
    int delaySamples{0};
  };

  // The layout compute() walks: entry i is feed i with its weight and
  // delay, or the arrays' defaults past their ends; array values past the
  // last feed are not read. Derived from the list and the two arrays
  // whenever one changes, and written nowhere else.
  void rebuild_entries_() {
    entries_.clear();
    for (size_t i = 0; i < source.sources.size(); ++i)
      entries_.push_back({source.sources[i],
                          i < weights.values.size() ? weights.values[i] : weights.def,
                          int(i < delays.values.size() ? delays.values[i] : delays.def)});
  }

  std::vector<Entry> entries_;
  int ptr_{-1};
};

} // namespace mforce
