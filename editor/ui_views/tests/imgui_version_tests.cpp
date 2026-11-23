// The views are compiled against the Dear ImGui the submodule pins.
#include "mforce/testing/check.h"
#include "mforce/ui_views/imgui_version.h"

void run_imgui_version_tests() {
  CHECK(mforce::ui::imgui_version() == "1.92.7");
}
