#include "mforce/ui_control/pane.h"
#include "mforce/ui_control/session.h"

namespace mforce::ui {

void show_pane_tab(Session& s, Pane::Tab tab) { s.pane.tab = tab; }

void collapse_pane(Session& s, bool collapsed) { s.pane.collapsed = collapsed; }

}  // namespace mforce::ui
