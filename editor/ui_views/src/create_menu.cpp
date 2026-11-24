#include "mforce/ui_views/create_menu.h"
#include <algorithm>
#include "mforce/core/source_registry.h"
#include "mforce/patch/type_declarations.h"

namespace mforce::ui {

namespace {

// The quick items, in the menu's order, after the Note.
struct Favourite {
  const char* label;
  const char* type;
};
constexpr Favourite kFavourites[] = {
  {"Curve", "CurveNode"}, {"Envelope", "Envelope"}, {"Var", "VarSource"}, {"Range", "RangeSource"},
};

// The first of the shaping families: a separator goes before it.
constexpr Family kFirstShapingFamily = Family::Physical;

bool offered(const std::string& type) {
  if (type == "PerformNode") return false;
  return std::none_of(std::begin(kFavourites), std::end(kFavourites),
                      [&](const Favourite& f) { return type == f.type; });
}

std::vector<CreateFamily> shelf(Shelf which) {
  const SourceRegistry& reg = SourceRegistry::instance();
  std::vector<CreateFamily> families;
  bool shaping = false, separated = false;
  for (Family f : kFamilyOrder) {
    shaping = shaping || f == kFirstShapingFamily;
    if (f == Family::Output) continue;
    CreateFamily family{family_name(f), {}, false};
    for (const auto& e : reg.types_in(f, which))
      if (offered(e.name)) family.items.push_back(CreateItem{reg.label(e.name), e.name});
    if (family.items.empty()) continue;
    family.separatorBefore = which == Shelf::Main && shaping && !separated;
    separated = separated || family.separatorBefore;
    families.push_back(std::move(family));
  }
  return families;
}

}  // namespace

bool can_create(const CreateItem& item, const Patch& patch) { return !item.type.empty() || patch.instrument.has_value(); }

CreateMenu create_menu() {
  (void)declared_types();                         // the declarations table registers every source on first use
  CreateMenu menu;
  menu.favourites.push_back(CreateItem{"Note", {}});
  for (const auto& f : kFavourites) menu.favourites.push_back(CreateItem{f.label, f.type});
  menu.main = shelf(Shelf::Main);
  menu.experimental = shelf(Shelf::Experimental);
  return menu;
}

}  // namespace mforce::ui
