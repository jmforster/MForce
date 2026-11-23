#include "mforce/patch/type_declarations.h"
#include "mforce/core/source_registry.h"
#include "mforce/source/register_all_sources.h"
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace mforce {

namespace {

// One type's entry: its declarations and the instance they were read from.
struct Entry {
  TypeDeclarations declarations;
  std::shared_ptr<Component> prototype;
};

// The cache: entries keyed by type name. A std::map's nodes do not move, so a
// pointer into an entry stays valid as later types are added.
struct Table {
  std::mutex mutex;
  std::map<std::string, Entry, std::less<>> entries;
};

Table& table() {
  static Table t;
  return t;
}

// Registers every engine type the first time any caller asks the table.
void register_once() {
  static const bool registered = (register_all_sources(), true);
  (void)registered;
}

std::string text(const char* s) { return s ? std::string(s) : std::string(); }

std::vector<std::string> label_list(const char* const* labels) {
  std::vector<std::string> out;
  for (int i = 0; labels && labels[i]; ++i) out.emplace_back(labels[i]);
  return out;
}

// The registered types whose fresh instance the by-name door of a scratch
// `owner` takes on `slot`, in registration order.
std::vector<std::string> accepted_types(const std::string& owner, std::string_view slot) {
  const auto& registry = SourceRegistry::instance();
  const auto scratch = registry.create(owner);
  std::vector<std::string> accepts;
  for (const auto& candidate : registry.registered_types())
    if (scratch->set_param(slot, registry.create(candidate.name))) accepts.push_back(candidate.name);
  return accepts;
}

TypeDeclarations read_declarations(const std::string& type, const Component& proto) {
  TypeDeclarations d;
  d.type = type;
  d.signal = dynamic_cast<const ValueSource*>(&proto) != nullptr;
  for (const auto& p : proto.param_descriptors())
    d.params.push_back({p.name, p.default_value, p.min_value, p.max_value, text(p.hint)});
  for (const auto& i : proto.input_descriptors())
    d.inputs.push_back({i.name, i.multi, i.slot, text(i.hint),
                        i.slot ? accepted_types(type, i.name) : std::vector<std::string>{}});
  for (const auto& s : proto.setting_descriptors())
    d.settings.push_back({s.name, s.type, s.default_value, s.min_value, s.max_value,
                          s.type == SettingType::String ? proto.get_string(s.name) : std::string(),
                          label_list(s.enum_labels), s.structural});
  for (const auto& a : proto.array_descriptors())
    d.arrays.push_back({a.name, text(a.groupName), a.default_value, a.min_value, a.max_value,
                        label_list(a.enum_labels)});
  return d;
}

// The entry of `type`, read on its first ask; null for a name the registry
// does not have, which caches nothing. The read runs outside the lock: it
// creates instances, and nothing it calls may wait on the table.
const Entry* entry(std::string_view type) {
  register_once();
  Table& t = table();
  {
    std::lock_guard<std::mutex> lock(t.mutex);
    if (auto it = t.entries.find(type); it != t.entries.end()) return &it->second;
  }
  const std::string name(type);
  const auto& registry = SourceRegistry::instance();
  if (!registry.has(name)) return nullptr;
  Entry fresh;
  fresh.prototype = registry.create(name);
  fresh.declarations = read_declarations(name, *fresh.prototype);
  std::lock_guard<std::mutex> lock(t.mutex);
  return &t.entries.try_emplace(name, std::move(fresh)).first->second;   // a racing first read keeps its entry
}

template <class T>
const T* named(const std::vector<T>& list, std::string_view name) {
  for (const auto& item : list)
    if (item.name == name) return &item;
  return nullptr;
}

} // namespace

const TypeDeclarations::Param* TypeDeclarations::param(std::string_view name) const { return named(params, name); }
const TypeDeclarations::Input* TypeDeclarations::input(std::string_view name) const { return named(inputs, name); }
const TypeDeclarations::Setting* TypeDeclarations::setting(std::string_view name) const { return named(settings, name); }
const TypeDeclarations::Array* TypeDeclarations::array(std::string_view name) const { return named(arrays, name); }

const TypeDeclarations* type_declarations(std::string_view type) {
  const Entry* e = entry(type);
  return e ? &e->declarations : nullptr;
}

std::vector<std::string> declared_types() {
  register_once();
  std::vector<std::string> names;
  for (const auto& t : SourceRegistry::instance().registered_types()) names.push_back(t.name);
  return names;
}

const Component* prototype(std::string_view type) {
  const Entry* e = entry(type);
  return e ? e->prototype.get() : nullptr;
}

} // namespace mforce
