#include "mforce/ui_model/new_patch.h"
#include "mforce/patch/edit.h"
#include "mforce/patch/instrument_block.h"

namespace mforce::ui {

namespace {

// A row from (50, 80), 220 apart: the Sine, the Envelope, the Output node.
constexpr Vec2 kSinePos{50.0f, 80.0f};
constexpr Vec2 kEnvelopePos{270.0f, 80.0f};
constexpr Vec2 kOutputPos{490.0f, 80.0f};

}  // namespace

Patch new_patch(std::uint32_t sineSeed, std::uint32_t envelopeSeed) {
  Patch p;
  (void)set_instrument(p, InstrumentBlock{});
  (void)add_node(p, "SineSource", fresh_id(p, "SineSource"), sineSeed, kSinePos);
  (void)add_node(p, "Envelope", fresh_id(p, "Envelope"), envelopeSeed, kEnvelopePos);
  (void)move_output(p, kOutputPos);
  return p;
}

}  // namespace mforce::ui
