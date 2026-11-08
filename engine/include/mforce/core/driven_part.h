#pragma once
// DrivenPart: the abstract base of an owner-driven part.
#include "mforce/core/dsp_value_source.h"
#include <array>
#include <cassert>

namespace mforce {

// ---------------------------------------------------------------------------
// DrivenPart — a Component that plugs into a slot and is driven by its owner:
// prepare once per build, start once per note, next once per sample. The
// base owns the pin lifecycle its members share: prepare and start forward
// to every pin, next advances every Param pin, each in declaration order,
// then the hook runs — so a part keeps only its declarations and its work,
// and a pin it adds is never left unprepared. A part's connectable pins are
// Manual: no sweep runs on a part. What the part answers between the calls
// (a sum of partials, a gain at a frequency) is its derived type's.
// ---------------------------------------------------------------------------
struct DrivenPart : Component {
  // prepare also lists the part's Param pins, so next() walks that list and
  // not every setting: the same calls Partials once wrote by hand, through
  // pointers kept inside the node (no heap, as Component's own sweep list).
  void prepare(const RenderContext& ctx) {
    paramCount_ = 0;
    for (auto* pin : pins()) {
      pin->prepare(ctx);
      if (pin->kind == PinKind::Param) {
        assert(paramCount_ < kMaxParams && "a part declares more Param pins than DrivenPart::kMaxParams");
        params_[size_t(paramCount_++)] = static_cast<Param*>(pin);
      }
    }
    on_prepare(ctx);
  }
  void start(int frames) {
    for (auto* pin : pins()) pin->start(frames);
    on_start(frames);
  }
  void next() {
    for (int i = 0; i < paramCount_; ++i) params_[size_t(i)]->next();
    on_next();
  }

protected:
  // The hooks: after the pins, in the same order the part would have called them.
  virtual void on_prepare(const RenderContext&) {}
  virtual void on_start(int) {}
  virtual void on_next() {}

private:
  // The Param pins, listed at prepare in declaration order. No part declares
  // more than eight today; the assert says when one does.
  static constexpr int kMaxParams = 16;
  std::array<Param*, kMaxParams> params_{};
  int paramCount_{0};
};

} // namespace mforce
