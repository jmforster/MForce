// The music tests, in the order they run.
#include "mforce/testing/check.h"

void run_realize_events_tests();
void run_harmony_timeline_tests();
void run_init_pitches_tests();
void run_passage_parse_tests();
void run_drum_map_parse_tests();
void run_embedded_score_text_tests();

namespace {
constexpr mforce::testing::Test kTests[] = {
  {"run_realize_events_tests", run_realize_events_tests},
  {"run_harmony_timeline_tests", run_harmony_timeline_tests},
  {"run_init_pitches_tests", run_init_pitches_tests},
  {"run_passage_parse_tests", run_passage_parse_tests},
  {"run_drum_map_parse_tests", run_drum_map_parse_tests},
  {"run_embedded_score_text_tests", run_embedded_score_text_tests},
};
}  // namespace

int main() { return mforce::testing::run_tests(kTests); }
