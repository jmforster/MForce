#pragma once
#include <cmath>
#include <string>
#include <string_view>
#include <vector>
#include "mforce/core/source_registry.h"
#include "mforce/patch/node.h"
#include "mforce/patch/type_declarations.h"
#include "float_text.h"
#include "sentence.h"

namespace mforce {

// The tests of a node's names that Patch::check() and the edits (edit.h)
// both make, and the words both refuse with, each written once.

// What a node's name means for its pins: a Param, or an Input of any sort.
struct Pin {
  bool declared{false};
  bool list{false};
  const TypeDeclarations::Input* slot{nullptr};          // the slot's declaration when the pin is one
};

inline Pin pin_of(const TypeDeclarations& d, std::string_view name) {
  if (d.param(name)) return {true, false, nullptr};
  if (const auto* in = d.input(name)) return {true, in->list, in->slot ? in : nullptr};
  return {};
}

// The scalar setting `name` declares (a float, an int, a bool or an enum);
// null when the type declares no setting of that name, or a string one.
inline const TypeDeclarations::Setting* scalar_setting(const TypeDeclarations& d, std::string_view name) {
  const auto* s = d.setting(name);
  return s && s->type != SettingType::String ? s : nullptr;
}

// The string setting `name` declares; null when it declares none.
inline const TypeDeclarations::Setting* string_setting(const TypeDeclarations& d, std::string_view name) {
  const auto* s = d.setting(name);
  return s && s->type == SettingType::String ? s : nullptr;
}

// The words a sentence about node `n` opens with: "node 'osc' (SineSource): ".
inline std::string node_opening(const Node& n) { return "node " + in_quotes(n.id) + " (" + n.type + "): "; }

// The sentence for a type the registry does not have, with the hint for a
// type that was renamed or retired.
inline std::string unknown_type_sentence(const std::string& type) {
  std::string sentence = "Unknown node type: " + type;
  if (const char* hint = retired_type_hint(type)) sentence += std::string("; ") + hint;
  return sentence;
}

// Every way `v` breaks the scalar setting `s`, each as the words that follow
// "'name' is <v>"; empty when `v` is within its declaration: a bool 0 or 1,
// an enum an index into its labels, a float or an int inside its range, an
// int whole.
inline std::vector<std::string> setting_faults(const TypeDeclarations::Setting& s, float v) {
  if (s.type == SettingType::Bool)
    return v != 0.0f && v != 1.0f ? std::vector<std::string>{": a bool setting holds 0 or 1"}
                                  : std::vector<std::string>{};
  if (!s.labels.empty())
    return is_index(v, s.labels.size())
               ? std::vector<std::string>{}
               : std::vector<std::string>{", which is not an index into its labels (" + comma_list(s.labels) + ")"};
  std::vector<std::string> faults;
  if (!(v >= s.min && v <= s.max))
    faults.push_back(", outside its range " + float_text(s.min) + " to " + float_text(s.max));
  if (s.type == SettingType::Int && std::isfinite(v) && v != std::trunc(v))
    faults.push_back(": an int setting holds a whole number");
  return faults;
}

} // namespace mforce
