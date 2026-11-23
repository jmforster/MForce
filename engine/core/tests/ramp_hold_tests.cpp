#include "mforce/testing/check.h"
#include "mforce/core/ramp.h"

using namespace mforce;


// RampType::Hold holds startVal, and every type starts at startVal.
void run_ramp_hold_tests() {
    Ramp h{0.7f, 0.2f, RampType::Hold, 0.0f};
    CHECK_NEAR(h.value(0.0f), 0.7f, 1e-9f);
    CHECK_NEAR(h.value(0.5f), 0.7f, 1e-9f);
    CHECK_NEAR(h.value(1.0f), 0.7f, 1e-9f);
    // Every type returns startVal at exactly pos==0 (Expo with power 0
    // would otherwise flip to endVal at pos 0).
    Ramp e{0.3f, 0.9f, RampType::Expo, 0.0f};
    CHECK_NEAR(e.value(0.0f), 0.3f, 1e-9f);
    Ramp s{0.3f, 0.9f, RampType::Sine, 0.0f};
    CHECK_NEAR(s.value(0.0f), 0.3f, 1e-6f);
}
