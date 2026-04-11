#pragma once
#include "mforce/music/strategy.h"
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace mforce {

class StrategyRegistry {
public:
  void register_strategy(std::unique_ptr<Strategy> s) {
    const std::string n = s->name();
    strategies_[n] = std::move(s);
  }

  Strategy* get(const std::string& name) const {
    auto it = strategies_.find(name);
    return it == strategies_.end() ? nullptr : it->second.get();
  }

  std::vector<Strategy*> list_for_level(StrategyLevel lvl) const {
    std::vector<Strategy*> out;
    out.reserve(strategies_.size());
    for (auto& kv : strategies_) {
      if (kv.second->level() == lvl) out.push_back(kv.second.get());
    }
    return out;
  }

private:
  std::unordered_map<std::string, std::unique_ptr<Strategy>> strategies_;
};

} // namespace mforce
