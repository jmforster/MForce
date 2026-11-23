#pragma once
#include "mforce/music/no_settings.h"
#include <any>
#include <stdexcept>
#include <string>
#include <typeinfo>

namespace mforce {

// A strategy a template names, with the settings written for it.
struct StrategyChoice {
  std::string name;      // empty = the default strategy of that level
  std::any    settings;  // empty = that strategy's defaults; otherwise a
                         // value of the strategy's own Settings type
};

// The settings as S: S's defaults when none were given. Throws
// std::runtime_error naming the strategy and both types when the value held
// is not an S.
template <class S>
S settings_of(const StrategyChoice& choice) {
  if (!choice.settings.has_value()) return S{};
  if (const S* s = std::any_cast<S>(&choice.settings)) return *s;
  throw std::runtime_error("strategy '" + choice.name + "' was given settings of type " +
                           choice.settings.type().name() + " where it reads " +
                           typeid(S).name());
}

} // namespace mforce
