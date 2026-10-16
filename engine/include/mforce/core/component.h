#pragma once
#include <memory>
#include <span>
#include <string_view>
#include <vector>
#include "mforce/core/pins.h"

namespace mforce {

// The root of every node. Owns the pin registry its members fill in while
// the node is constructed, and serves the self-description surface and the
// by-name access from it, so a node keeps only its declarations and its
// work. The by-name members are virtual only while the node families
// migrate off their hand-written overrides.
struct Component {
  Component() = default;
  virtual ~Component() = default;
  Component(const Component&) = delete;                      // a copy's pins would not register
  Component& operator=(const Component&) { return *this; }   // the registry is per object; a node assigns its own members

  virtual const char* type_name() const { return "Unknown"; }
  virtual SourceCategory category() const { return SourceCategory::Utility; }

  virtual std::span<const ParamDescriptor> param_descriptors() const { return paramDescs_; }
  virtual std::span<const InputDescriptor> input_descriptors() const { return inputDescs_; }
  virtual std::span<const SettingDescriptor> setting_descriptors() const { return settingDescs_; }
  virtual std::span<const ArrayDescriptor> array_descriptors() const { return arrayDescs_; }

  // Unknown names are ignored for now; a rejected wire fails the load from
  // checkpoint 2.5 on.
  virtual void set_param(std::string_view name, std::shared_ptr<ValueSource> src) {
    if (auto* p = find<Param>(name)) { p->set(std::move(src)); return; }
    if (auto* i = find<Input>(name)) i->set(std::move(src));
  }
  virtual std::shared_ptr<ValueSource> get_param(std::string_view name) const {
    if (auto* p = find<Param>(name)) return p->get();
    if (auto* i = find<Input>(name)) return i->get();
    return nullptr;
  }
  virtual void add_param(std::string_view name, std::shared_ptr<ValueSource> src) { set_param(name, std::move(src)); }
  virtual void clear_param(std::string_view name) { set_param(name, nullptr); }

  virtual void set_setting(std::string_view name, float v) {
    if (auto* s = find<SettingBase>(name)) { s->set_from_float(v); on_setting_changed(name); }
  }
  virtual float get_setting(std::string_view name) const {
    auto* s = find<SettingBase>(name);
    return s ? s->as_float() : 0.0f;
  }
  virtual void set_array(std::string_view name, std::vector<float> values) {
    if (auto* a = find<Array>(name)) { a->values = std::move(values); on_array_changed(name); }
  }
  virtual std::vector<float> get_array(std::string_view name) const {
    auto* a = find<Array>(name);
    return a ? a->values : std::vector<float>{};
  }
  // Hooks for a node that derives state from a setting or an array.
  virtual void on_setting_changed(std::string_view) {}
  virtual void on_array_changed(std::string_view) {}

  const std::vector<PinBase*>& pins() const { return pins_; }

  // Called by a pin's constructor; builds the descriptor tables as it goes.
  void register_pin(PinBase* pin) {
    pins_.push_back(pin);
    switch (pin->kind) {
      case PinKind::Param:   paramDescs_.push_back(static_cast<Param*>(pin)->descriptor()); break;
      case PinKind::Input:   inputDescs_.push_back(static_cast<Input*>(pin)->descriptor()); break;
      case PinKind::Slot:    inputDescs_.push_back(static_cast<SlotBase*>(pin)->descriptor()); break;
      case PinKind::Setting: settingDescs_.push_back(static_cast<SettingBase*>(pin)->descriptor()); break;
      case PinKind::Array:   arrayDescs_.push_back(static_cast<Array*>(pin)->descriptor()); break;
    }
  }

private:
  template <class P> P* find(std::string_view name) const {
    for (auto* pin : pins_)
      if (pin->kind == P::kKind && name == pin->name) return static_cast<P*>(pin);
    return nullptr;
  }
  std::vector<PinBase*> pins_;
  std::vector<ParamDescriptor> paramDescs_;
  std::vector<InputDescriptor> inputDescs_;
  std::vector<SettingDescriptor> settingDescs_;
  std::vector<ArrayDescriptor> arrayDescs_;
};

inline void PinBase::attach(Component* owner) { owner->register_pin(this); }

template <class T>
bool Slot<T>::set(std::shared_ptr<Component> part) {
  auto* view = dynamic_cast<T*>(part.get());
  if (!view) return false;
  if (!multi) clear();
  views.push_back(view);
  parts.push_back(std::move(part));
  return true;
}

} // namespace mforce
