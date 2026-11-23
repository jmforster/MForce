// The module links and answers with its name.
#include "mforce/testing/check.h"
#include "mforce/ui_control/module_name.h"

void run_module_name_tests() {
  CHECK(mforce::ui::control_module_name() == "ui_control");
}
