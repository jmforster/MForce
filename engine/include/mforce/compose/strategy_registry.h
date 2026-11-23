#pragma once
#include "mforce/compose/realization_strategy.h"
#include "mforce/compose/strategy.h"
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>

namespace mforce {

// StrategyRegistry — the strategies a host has registered, by name, one
// table per level. A host fills one (register_builtin) and passes it to
// compose inside a ComposeKit.
//
// The lookups take the name a template gives. An empty name means the
// level's default strategy: default_figure, default_phrase,
// default_passage, block. A name that is not registered throws
// std::runtime_error naming the level and the name.
class StrategyRegistry {
public:
  void add(std::unique_ptr<FigureStrategy> s)      { put(figures_, std::move(s)); }
  void add(std::unique_ptr<PhraseStrategy> s)      { put(phrases_, std::move(s)); }
  void add(std::unique_ptr<PassageStrategy> s)     { put(passages_, std::move(s)); }
  void add(std::unique_ptr<RealizationStrategy> s) { put(realizations_, std::move(s)); }

  FigureStrategy& figure(std::string_view name) const {
    return require(figures_, "figure", name, "default_figure");
  }
  PhraseStrategy& phrase(std::string_view name) const {
    return require(phrases_, "phrase", name, "default_phrase");
  }
  PassageStrategy& passage(std::string_view name) const {
    return require(passages_, "passage", name, "default_passage");
  }
  RealizationStrategy& realization(std::string_view name) const {
    return require(realizations_, "realization", name, "block");
  }

  // The figure strategy of that name, or null when none is registered. For
  // the default figure strategy's shape dispatch, where a shape without a
  // registered strategy falls back to the generic generator.
  FigureStrategy* find_figure(std::string_view name) const {
    auto it = figures_.find(std::string(name));
    return it == figures_.end() ? nullptr : it->second.get();
  }

private:
  template <class T>
  using Table = std::unordered_map<std::string, std::unique_ptr<T>>;

  template <class T>
  static void put(Table<T>& table, std::unique_ptr<T> s) {
    const std::string name = s->name();
    table[name] = std::move(s);
  }

  template <class T>
  static T& require(const Table<T>& table, const char* level,
                    std::string_view name, const char* defaultName) {
    const std::string key = name.empty() ? std::string(defaultName)
                                         : std::string(name);
    auto it = table.find(key);
    if (it == table.end()) {
      throw std::runtime_error(std::string("no ") + level +
                               " strategy named '" + key + "' is registered");
    }
    return *it->second;
  }

  Table<FigureStrategy>      figures_;
  Table<PhraseStrategy>      phrases_;
  Table<PassageStrategy>     passages_;
  Table<RealizationStrategy> realizations_;
};

} // namespace mforce
