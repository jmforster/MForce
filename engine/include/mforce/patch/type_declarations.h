#pragma once
#include <string>
#include <string_view>
#include <vector>
#include "mforce/core/descriptors.h"

namespace mforce {

struct Component;

// What a registered type declares, read once from one instance of it into
// plain values.
struct TypeDeclarations {
  struct Param   { std::string name; float defaultValue{0}, min{0}, max{0}; std::string hint; };
  struct Input   { std::string name;
                   bool list{false};                     // several feeds in order: a list of signals, or a multi slot
                   bool slot{false};                     // takes a part, not a signal
                   std::string hint;                     // on a slot, what it takes, in words
                   std::vector<std::string> accepts; };  // on a slot, the registered types its door takes
  struct Setting { std::string name; SettingType type{SettingType::Float};
                   float defaultValue{0}, min{0}, max{0};
                   std::string defaultText;              // a string setting's default
                   std::vector<std::string> labels;      // an enum's labels, by index
                   bool structural{false}; };            // sizes or reshapes the node: takes no dynamic pin
  struct Array   { std::string name;
                   std::string group;                    // the table it is a column of; empty when it stands alone
                   float rowDefault{0}, min{0}, max{0};
                   std::vector<std::string> labels; };   // a labelled column's labels, by index

  std::string type;
  bool signal{false};                                    // a signal; false: a part
  std::vector<Param> params;                             // each list in declaration order
  std::vector<Input> inputs;
  std::vector<Setting> settings;
  std::vector<Array> arrays;

  const Param*   param(std::string_view name) const;     // null when the type declares no such name
  const Input*   input(std::string_view name) const;
  const Setting* setting(std::string_view name) const;
  const Array*   array(std::string_view name) const;
};

// The declarations of a registered type; null for a name the registry does not have.
const TypeDeclarations* type_declarations(std::string_view type);
// Every registered type, in registration order, as the registry stands now.
std::vector<std::string> declared_types();
// The one instance of `type` the table was read from: ask it, never wire it. Null for an unknown name.
const Component* prototype(std::string_view type);

} // namespace mforce
