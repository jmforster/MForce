#pragma once
#include "mforce/core/dsp_value_source.h"
#include <nlohmann/json.hpp>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace mforce {

// Resolve a JSON param value to a ValueSource (number → ConstantSource, ref → lookup).
// Provided by the caller (patch_loader) so the registry stays decoupled from loading logic.
using ResolveParamFn = std::function<std::shared_ptr<ValueSource>(
    const nlohmann::json& val)>;

// Factory: creates a default-initialized source. Caller wires params via set_param().
using SourceFactory = std::function<std::shared_ptr<ValueSource>(
    int sampleRate, std::optional<uint32_t> seed)>;

// Optional hook for types that need JSON-level config beyond connectable params
// (e.g. Envelope presets, SegmentSource values[], filter section counts).
using JsonConfigurator = std::function<void(
    ValueSource& src,
    const nlohmann::json& params,
    const ResolveParamFn& resolve)>;

class SourceRegistry {
public:
  static SourceRegistry& instance();

  void register_type(const std::string& type_name,
                     SourceCategory category,
                     SourceFactory factory,
                     JsonConfigurator configurator = nullptr);

  std::shared_ptr<ValueSource> create(const std::string& type_name,
                                      int sampleRate,
                                      std::optional<uint32_t> seed = {}) const;

  const JsonConfigurator* get_configurator(const std::string& type_name) const;

  // Returns all registered types, sorted by category then name.
  std::vector<std::pair<std::string, SourceCategory>> registered_types() const;

  bool has(const std::string& type_name) const;

  // Returns category for a registered type, or Utility if unknown.
  SourceCategory get_category(const std::string& type_name) const;

private:
  struct Entry {
    SourceCategory category;
    SourceFactory factory;
    JsonConfigurator configurator;  // may be null
  };

  std::unordered_map<std::string, Entry> entries_;
};

// Call once at startup to populate the registry with all source types.
void register_all_sources();

} // namespace mforce
