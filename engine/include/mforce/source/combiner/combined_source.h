#pragma once
#include "mforce/core/dsp_value_source.h"
#include "mforce/core/randomizer.h"
#include <memory>
#include <cmath>
#include <algorithm>

namespace mforce {

// Ported from C# CombinedSource — combines two sources via Add, Multiply,
// Fade, or Sum. Add averages (v1+v2)/2 to preserve headroom; Sum is a
// no-halving variant that's correct when callers want raw addition
// (e.g. modulator layering where amplitudes are chosen to stay bounded).
// Fade: linear crossfade from source1 to source2 over the started span.
enum class CombineOp { Add, Multiply, Fade, Sum };

// A combiner has two signal inputs and no `source`, so the inherited input of
// a Processor would be a phantom pin next to them: it derives from ValueSource
// and becomes a Processor when the multi-input shape is settled.
struct CombinedSource final : ValueSource {
  Input source1{this, "source1"};
  Input source2{this, "source2"};
  Setting<int>   operation{this, "operation", 0, 0, 3, kOpLabels};
  Setting<float> gainAdj  {this, "gainAdj", 0.0f, -1.0f, 10.0f};

  CombinedSource(std::shared_ptr<ValueSource> s1, std::shared_ptr<ValueSource> s2,
                 CombineOp op, float ga = 0.0f) {
    source1.set(std::move(s1));
    source2.set(std::move(s2));
    operation.value = static_cast<int>(op);
    gainAdj.value = ga;
  }

  const char* type_name() const override { return "CombinedSource"; }

  void on_start(int frames) override {
    totalFrames_ = frames;
    ptr_ = -1;
  }

  float compute() override {
    ++ptr_;
    const float v1 = source1.current();
    const float v2 = source2.current();
    const auto op = static_cast<CombineOp>(operation.value);

    if (op == CombineOp::Add) {
      return (v1 + v2 * (1.0f + gainAdj.value)) * 0.5f;
    } else if (op == CombineOp::Multiply) {
      return v1 * v2 * (1.0f + gainAdj.value);
    } else if (op == CombineOp::Sum) {
      return v1 + v2 * (1.0f + gainAdj.value);
    }
    // Fade
    float t = (totalFrames_ > 0) ? float(ptr_) / float(totalFrames_) : 0.0f;
    return v1 * (1.0f - t) + v2 * (1.0f + gainAdj.value) * t;
  }

private:
  // Display labels for the operation dropdown. Index must match CombineOp:
  // 0=Add (averaged mix), 1=Multiply, 2=Fade (crossfade), 3=Sum (raw add).
  // "Add" is shown as "Mix" since it's actually (v1+v2)/2 — averaging, not
  // summation. "Sum" is the raw-addition variant.
  static constexpr const char* kOpLabels[] = {
    "Mix", "Multiply", "Fade", "Sum", nullptr
  };

  int ptr_{-1};
  int totalFrames_{1};
};

} // namespace mforce
