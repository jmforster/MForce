#include "mforce/core/source_registry.h"
#include <algorithm>
#include <stdexcept>

namespace mforce {

SourceRegistry& SourceRegistry::instance() {
  static SourceRegistry reg;
  return reg;
}

void SourceRegistry::register_type(const std::string& type_name,
                                   SourceCategory category,
                                   SourceFactory factory,
                                   JsonConfigurator configurator) {
  entries_[type_name] = Entry{category, std::move(factory), std::move(configurator)};
}

std::shared_ptr<ValueSource> SourceRegistry::create(const std::string& type_name,
                                                    int sampleRate,
                                                    std::optional<uint32_t> seed) const {
  auto it = entries_.find(type_name);
  if (it == entries_.end())
    throw std::runtime_error("SourceRegistry: unknown type '" + type_name + "'");
  return it->second.factory(sampleRate, seed);
}

const JsonConfigurator* SourceRegistry::get_configurator(const std::string& type_name) const {
  auto it = entries_.find(type_name);
  if (it == entries_.end()) return nullptr;
  if (!it->second.configurator) return nullptr;
  return &it->second.configurator;
}

std::vector<std::pair<std::string, SourceCategory>> SourceRegistry::registered_types() const {
  std::vector<std::pair<std::string, SourceCategory>> result;
  result.reserve(entries_.size());
  for (const auto& [name, entry] : entries_)
    result.emplace_back(name, entry.category);
  std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) {
    if (a.second != b.second) return a.second < b.second;
    return a.first < b.first;
  });
  return result;
}

bool SourceRegistry::has(const std::string& type_name) const {
  return entries_.count(type_name) > 0;
}

SourceCategory SourceRegistry::get_category(const std::string& type_name) const {
  auto it = entries_.find(type_name);
  if (it == entries_.end()) return SourceCategory::Utility;
  return it->second.category;
}

} // namespace mforce
