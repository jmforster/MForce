#include "mforce/core/source_registry.h"
#include <algorithm>
#include <cctype>
#include <stdexcept>

namespace mforce {

SourceRegistry& SourceRegistry::instance() {
  static SourceRegistry reg;
  return reg;
}

const char* family_name(Family f) {
  switch (f) {
    case Family::Oscillator: return "Oscillators";
    case Family::Noise:      return "Noise";
    case Family::Wavetable:  return "Wavetable";
    case Family::Additive:   return "Additive";
    case Family::Physical:   return "Physical";
    case Family::Filter:     return "Filters";
    case Family::Envelope:   return "Envelopes";
    case Family::Modulator:  return "Modulators";
    case Family::Combiner:   return "Combiners";
    case Family::Utility:    return "Utility";
  }
  return "Utility";
}

std::string derived_label(std::string_view type_name) {
  std::string s(type_name);
  for (std::string_view suffix : {"Source", "Filter", "Node", "Envelope"}) {
    if (s.size() > suffix.size() && std::string_view(s).ends_with(suffix)) {
      s.resize(s.size() - suffix.size());
      break;
    }
  }
  std::string out;
  for (size_t i = 0; i < s.size(); ++i) {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    if (i > 0) {
      const unsigned char p = static_cast<unsigned char>(s[i - 1]);
      const unsigned char n = i + 1 < s.size() ? static_cast<unsigned char>(s[i + 1]) : 0;
      const bool wordStart = (std::isupper(c) && (std::islower(p) || (std::isupper(p) && std::islower(n))))
                          || (std::isdigit(c) && std::isalpha(p));
      if (wordStart) out += ' ';
    }
    out += static_cast<char>(c);
  }
  return out;
}

void SourceRegistry::register_type(const std::string& type_name,
                                   TypeTag tag,
                                   SourceFactory factory,
                                   JsonConfigurator configurator) {
  if (!entries_.count(type_name)) order_.push_back(type_name);
  entries_[type_name] = Entry{tag, std::move(factory), std::move(configurator)};
}

std::shared_ptr<Component> SourceRegistry::create(const std::string& type_name,
                                                  std::optional<uint32_t> seed) const {
  auto it = entries_.find(type_name);
  if (it == entries_.end())
    throw std::runtime_error("SourceRegistry: unknown type '" + type_name + "'");
  return it->second.factory(seed);
}

const JsonConfigurator* SourceRegistry::get_configurator(const std::string& type_name) const {
  auto it = entries_.find(type_name);
  if (it == entries_.end()) return nullptr;
  if (!it->second.configurator) return nullptr;
  return &it->second.configurator;
}

std::vector<TypeEntry> SourceRegistry::registered_types() const {
  std::vector<TypeEntry> result;
  result.reserve(order_.size());
  for (const auto& name : order_)
    result.push_back({name, entries_.at(name).tag});
  return result;
}

std::vector<TypeEntry> SourceRegistry::types_in(Family family, Shelf shelf) const {
  std::vector<TypeEntry> result;
  for (const auto& name : order_) {
    const TypeTag& tag = entries_.at(name).tag;
    if (tag.family == family && tag.shelf == shelf) result.push_back({name, tag});
  }
  return result;
}

bool SourceRegistry::has(const std::string& type_name) const {
  return entries_.count(type_name) > 0;
}

Family SourceRegistry::family(const std::string& type_name) const {
  auto it = entries_.find(type_name);
  return it == entries_.end() ? Family::Utility : it->second.tag.family;
}

std::string SourceRegistry::label(const std::string& type_name) const {
  auto it = entries_.find(type_name);
  if (it == entries_.end()) return type_name;
  return it->second.tag.label ? std::string(it->second.tag.label) : derived_label(type_name);
}

Shelf SourceRegistry::shelf(const std::string& type_name) const {
  auto it = entries_.find(type_name);
  return it == entries_.end() ? Shelf::Main : it->second.tag.shelf;
}

} // namespace mforce
