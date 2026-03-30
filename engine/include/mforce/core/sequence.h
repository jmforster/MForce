#pragma once
#include <vector>
#include <cmath>

namespace mforce {

// ---------------------------------------------------------------------------
// Ported from C# ISequence / SimpleSequence / CompositeSequence
// ---------------------------------------------------------------------------

struct Sequence {
  virtual ~Sequence() = default;
  virtual bool has_more() const = 0;
  virtual float get_next() = 0;
  virtual void reset() = 0;
};

struct SimpleSequence final : Sequence {
  std::vector<float> values;
  bool loop{false};
  int maxLoops{-1}; // -1 = infinite

  SimpleSequence() = default;
  explicit SimpleSequence(std::vector<float> v, bool lp = false, int ml = -1)
  : values(std::move(v)), loop(lp), maxLoops(ml) {}

  bool has_more() const override {
    if (loop && (maxLoops < 0 || loopCount_ < maxLoops)) return true;
    return ptr_ < int(values.size());
  }

  float get_next() override {
    if (ptr_ >= int(values.size())) {
      if (loop) { ptr_ = 0; loopCount_++; }
      else return 0.0f;
    }
    return values[ptr_++];
  }

  void reset() override { ptr_ = 0; loopCount_ = 0; }

private:
  int ptr_{0};
  int loopCount_{0};
};

struct CompositeSequence final : Sequence {
  std::vector<std::unique_ptr<Sequence>> sequences;
  bool loop{false};

  bool has_more() const override {
    if (loop) return true;
    return idx_ < int(sequences.size()) && sequences[idx_]->has_more();
  }

  float get_next() override {
    while (idx_ < int(sequences.size())) {
      if (sequences[idx_]->has_more())
        return sequences[idx_]->get_next();
      idx_++;
      if (idx_ >= int(sequences.size()) && loop) {
        for (auto& s : sequences) s->reset();
        idx_ = 0;
      }
    }
    return 0.0f;
  }

  void reset() override {
    idx_ = 0;
    for (auto& s : sequences) s->reset();
  }

private:
  int idx_{0};
};

} // namespace mforce
