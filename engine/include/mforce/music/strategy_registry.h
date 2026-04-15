#pragma once
#include "mforce/music/strategy.h"
#include <memory>
#include <string>
#include <unordered_map>

namespace mforce {

class StrategyRegistry {
public:
  static StrategyRegistry& instance() {
    static StrategyRegistry inst;
    return inst;
  }

  void register_figure (std::unique_ptr<FigureStrategy> s)  { figures_[s->name()]  = std::move(s); }
  void register_phrase (std::unique_ptr<PhraseStrategy> s)  { phrases_[s->name()]  = std::move(s); }
  void register_passage(std::unique_ptr<PassageStrategy> s) { passages_[s->name()] = std::move(s); }

  FigureStrategy*  resolve_figure (const std::string& n) const { auto it = figures_.find(n);  return it == figures_.end()  ? nullptr : it->second.get(); }
  PhraseStrategy*  resolve_phrase (const std::string& n) const { auto it = phrases_.find(n);  return it == phrases_.end()  ? nullptr : it->second.get(); }
  PassageStrategy* resolve_passage(const std::string& n) const { auto it = passages_.find(n); return it == passages_.end() ? nullptr : it->second.get(); }

private:
  std::unordered_map<std::string, std::unique_ptr<FigureStrategy>>  figures_;
  std::unordered_map<std::string, std::unique_ptr<PhraseStrategy>>  phrases_;
  std::unordered_map<std::string, std::unique_ptr<PassageStrategy>> passages_;
};

} // namespace mforce
