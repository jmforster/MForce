#include "mforce/testing/check.h"
#include "mforce/render/perform_source.h"
#include <memory>

using namespace mforce;


void run_onset_field_tests() {
    auto ps = std::make_shared<PerformSource>();
    PerformOut out(ps, PerformOut::Field::Onset);
    CHECK(out.current() == 0.0f);                          // no note yet
    ps->set_note(440.0f, 0.8f, 48000, 1.0f, nullptr, 2.0f);
    CHECK(out.current() == 2.0f);
    CHECK(out.next() == 2.0f);
    ps->set_note(440.0f, 0.8f, 48000, 1.0f, nullptr);      // default arg = none
    CHECK(out.current() == 0.0f);
}
