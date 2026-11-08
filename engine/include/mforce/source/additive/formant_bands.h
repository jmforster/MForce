#pragma once
// FormantBands: a formant of equal bands.
#include "mforce/core/dsp_value_source.h"
#include "mforce/source/additive/formant.h"

namespace mforce {

// ---------------------------------------------------------------------------
// FormantBands — a gain table over equal frequency bands from startFreq in
// steps of freqIncrement, interpolated between adjacent gains and 0 outside
// the span. Ported from C# BandSpectrum.
// ---------------------------------------------------------------------------
struct FormantBands final : Formant {
  // The owner drives both pins through the base; they are Manual.
  Param startFreq    {this, "startFreq",     0.0f, 0.0f,  20000.0f, nullptr, Pull::Manual};
  Param freqIncrement{this, "freqIncrement", 1.0f, 0.01f, 1000.0f,  nullptr, Pull::Manual};
  Array gains{this, "gains", nullptr, 0.0f, 0.0f, 1.0f};

  const char* type_name() const override { return "FormantBands"; }

  float gain(float freq) const override {
    float sf = startFreq.current();
    float fi = freqIncrement.current();
    // Outside the span there is nothing to read: below startFreq within one
    // bin the position would truncate to bin 0 and interpolate with a
    // negative fraction, so the span is checked before the table.
    if (!(freq >= sf && freq <= sf + fi * float(int(gains.values.size()) - 1))) return 0.0f;
    if (fi <= 0.0f) return 0.0f;

    float pos = (freq - sf) / fi;
    int idx = int(pos);
    if (idx < 0 || idx + 1 >= int(gains.values.size())) return 0.0f;

    float frac = pos - float(idx);
    return gains.values[idx] + (gains.values[idx + 1] - gains.values[idx]) * frac;
  }
};

} // namespace mforce
