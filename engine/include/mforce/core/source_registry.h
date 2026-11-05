#pragma once
#include "mforce/core/dsp_value_source.h"
#include <nlohmann/json.hpp>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace mforce {

// Resolve a JSON param value to a ValueSource (number → ConstantSource, ref → lookup).
// Provided by the caller (patch_loader) so the registry stays decoupled from loading logic.
using ResolveParamFn = std::function<std::shared_ptr<ValueSource>(
    const nlohmann::json& val)>;

// Factory: creates a default-initialized node — a signal or a part. The
// caller wires its pins through set_param(); a signal learns the rate from
// the host's prepare(ctx), a part from its owner's.
using SourceFactory = std::function<std::shared_ptr<Component>(
    std::optional<uint32_t> seed)>;

// Optional hook for types that need JSON-level config beyond connectable params
// (e.g. Envelope presets, SegmentSource values[], filter section counts).
using JsonConfigurator = std::function<void(
    Component& src,
    const nlohmann::json& params,
    const ResolveParamFn& resolve)>;

// The one classification of a node type, declared where the type is
// registered: its family (the families table in docs/architecture/part3-nodes.md),
// the label the editor's menus show (derived from the type name unless given)
// and the shelf it sits on — Main for the create menu's family menus,
// Experimental for the parked and the unclassified. The editor's menus,
// colours and the folder a type's header lives in all follow from the family;
// nothing asks a node object.
enum class Family { Oscillator, Noise, Wavetable, Additive, Physical, Filter, Envelope, Modulator, Combiner, Utility };
constexpr int kFamilyCount = 10;
// Menu order: the sound-making families, then the shaping ones.
inline constexpr Family kFamilyOrder[kFamilyCount] = {
  Family::Oscillator, Family::Noise, Family::Wavetable, Family::Additive, Family::Physical,
  Family::Filter, Family::Envelope, Family::Modulator, Family::Combiner, Family::Utility,
};
const char* family_name(Family f);      // "Oscillators", "Noise", "Wavetable", "Additive", "Physical", "Filters", "Envelopes", "Modulators", "Combiners", "Utility"

enum class Shelf { Main, Experimental };

struct TypeTag {
  Family family;
  const char* label = nullptr;          // null: derived_label(type_name); copied at registration
  Shelf shelf = Shelf::Main;
};

// A registered type as the registry reports it. tag.label is the registry's
// own copy of the label (or null): valid until that name is registered again.
struct TypeEntry {
  std::string name;
  TypeTag tag;
};

// The label a type name reads as when its registration gives none: one
// trailing Source, Filter or Node dropped (never the whole name),
// then a space before each word — before a capital that follows a lower-case
// letter, before the last capital of a run that a lower-case letter follows,
// and before a digit that follows a letter: "HybridKSSource" -> "Hybrid KS",
// "BWLowpassFilter" -> "BW Lowpass", "Mesh2D" -> "Mesh 2D".
std::string derived_label(std::string_view type_name);

// A name this engine once registered: the tool that rewrites a patch still
// using it, so a load that fails on the name says what to run. nullptr for
// any other name.
const char* retired_type_hint(std::string_view type_name);

class SourceRegistry {
public:
  static SourceRegistry& instance();

  void register_type(const std::string& type_name,
                     TypeTag tag,
                     SourceFactory factory,
                     JsonConfigurator configurator = nullptr);

  std::shared_ptr<Component> create(const std::string& type_name,
                                    std::optional<uint32_t> seed = {}) const;

  const JsonConfigurator* get_configurator(const std::string& type_name) const;

  // Every registered type, in registration order (which is the menus' order).
  std::vector<TypeEntry> registered_types() const;
  // The types of one family on one shelf, in registration order.
  std::vector<TypeEntry> types_in(Family family, Shelf shelf) const;

  bool has(const std::string& type_name) const;
  // The tag of a registered type, or null for a name the registry does not
  // know; its label pointer is the registry's, valid until the name is registered again.
  const TypeTag* find(const std::string& type_name) const;

  // The tag's parts, by name; an unknown name throws (ask has() or find() first).
  Family      family(const std::string& type_name) const;
  std::string label(const std::string& type_name) const;   // the given label, else derived_label
  Shelf       shelf(const std::string& type_name) const;

private:
  struct Entry {
    TypeTag tag;                     // tag.label points into `label` below, or is null
    std::string label;               // the registration's label, copied; empty when none was given
    SourceFactory factory;
    JsonConfigurator configurator;   // may be null
  };
  const Entry& entry(const std::string& type_name) const;   // throws on an unknown name

  std::unordered_map<std::string, Entry> entries_;
  std::vector<std::string> order_;    // registration order
};

// Call once at startup to populate the registry with all source types.
void register_all_sources();

} // namespace mforce
