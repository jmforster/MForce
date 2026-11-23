#include "mforce/ui_views/imgui_version.h"
#include <imgui.h>

namespace mforce::ui {

std::string_view imgui_version() { return IMGUI_VERSION; }

}  // namespace mforce::ui
