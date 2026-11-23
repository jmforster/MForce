#include "mforce/testing/check.h"
#include "test_support.h"
#include "mforce/core/component.h"
#include "mforce/core/pins.h"
#include "mforce/source/additive/formant_peaks.h"
#include <cmath>
#include <cstdio>
#include <vector>

using namespace mforce;

namespace formant_peaks_twin {
// The frequency grid for a row set: log-spaced, then each row's edges, the
// floats just inside them, and the centre.
static std::vector<float> grid(const std::vector<Row>& set) {
    std::vector<float> freqs = formant_log_grid();
    for (const auto& r : set) {
        const float a = r.f - r.w * 0.5f, b = r.f + r.w * 0.5f;
        freqs.push_back(a); freqs.push_back(b); freqs.push_back(r.f);
        freqs.push_back(std::nextafter(a, 1.0e9f)); freqs.push_back(std::nextafter(b, 0.0f));
    }
    return freqs;
}
}  // namespace formant_peaks_twin
void run_formant_peaks_twin_tests() {
    using namespace formant_peaks_twin;
    std::printf("formant peaks twin: the peak node and the spectrum, as rows\n");
    int rows = 0, bad = 0, inBand = 0;
    for (const auto& set : kSets) {
        FormantPeaks peaks;
        fill(peaks, set);
        peaks.prepare(RenderContext{48000}); peaks.start(4800); peaks.next();
        CHECK(peaks.peak_count() == int(set.size()));
        for (float freq : grid(set)) {
            ++rows;
            const bool oc = old_spectrum_contains(set, freq);
            const float og = old_spectrum_gain(set, freq);
            inBand += oc;
            if (!same_bits(peaks.gain(freq), og)) ++bad;
            if (!same_bits(peaks.gain(freq), oc ? og : 0.0f)) ++bad;          // 0 outside every row: the reference's gate is implied
        }
    }
    CHECK(bad == 0);
    CHECK(rows == 2000 * 5 + 5 * (1 + 3 + 5 + 2 + 4));
    CHECK(inBand > 0 && inBand < rows);
    // the edges follow an array change (the editor's path), not only prepare
    {
        FormantPeaks peaks;
        fill(peaks, kSets[0]);
        peaks.prepare(RenderContext{48000}); peaks.start(4800);
        CHECK(same_bits(peaks.gain(1000.0f), 1.0f) && peaks.gain(1100.0f) > 0.0f);
        peaks.set_array("width", {100.0f});
        CHECK(same_bits(peaks.gain(1100.0f), 0.0f));
        peaks.set_array("frequency", {2000.0f});
        CHECK(same_bits(peaks.gain(2000.0f), 1.0f));
    }
}
