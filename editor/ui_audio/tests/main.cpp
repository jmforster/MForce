// The ui_audio tests, in the order they run.
#include "mforce/testing/check.h"

void run_audio_api_tests();

namespace {
constexpr mforce::testing::Test kTests[] = {
  {"run_audio_api_tests", run_audio_api_tests},
};
}  // namespace

int main() { return mforce::testing::run_tests(kTests); }
