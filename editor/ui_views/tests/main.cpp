// The ui_views tests, in the order they run.
#include "mforce/testing/check.h"

void run_imgui_version_tests();

namespace {
constexpr mforce::testing::Test kTests[] = {
  {"run_imgui_version_tests", run_imgui_version_tests},
};
}  // namespace

int main() { return mforce::testing::run_tests(kTests); }
