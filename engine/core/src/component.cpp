#include "mforce/core/dsp_value_source.h"

namespace mforce {

// The wiring door (component.h), defined where ValueSource is a complete
// type: a Param or Input takes a signal and refuses a part; an InputList
// takes a signal as its one feed and refuses a part, keeping what it held; a
// Slot takes its interface and refuses the rest (Slot<T>::set); a null part
// clears the pin. The one place a wire is type-checked.
bool Component::set_param(std::string_view name, std::shared_ptr<Component> part) {
  if (auto* p = find<Param>(name)) {
    if (!part) { p->set(nullptr); return true; }
    auto signal = std::dynamic_pointer_cast<ValueSource>(std::move(part));
    if (!signal) return false;                   // a part, not a signal
    p->set(std::move(signal));
    return true;
  }
  if (auto* i = find<Input>(name)) {
    if (!part) { i->set(nullptr); return true; }
    auto signal = std::dynamic_pointer_cast<ValueSource>(std::move(part));
    if (!signal) return false;
    i->set(std::move(signal));
    return true;
  }
  if (auto* l = find<InputList>(name)) {
    if (!part) { l->sources.clear(); on_input_list_changed(name); return true; }
    auto signal = std::dynamic_pointer_cast<ValueSource>(std::move(part));
    if (!signal) return false;                   // a part: the list keeps its feeds
    l->sources.clear();
    l->sources.push_back(std::move(signal));
    on_input_list_changed(name);
    return true;
  }
  if (auto* s = find<SlotBase>(name)) {
    if (!part) { s->clear(); on_slot_changed(name); return true; }
    if (!s->set(std::move(part))) return false;
    on_slot_changed(name);
    return true;
  }
  return false;
}

// Wiring one more (component.h): a list appends a signal and refuses a part;
// a multi slot appends; a null part empties either; every other pin replaces.
bool Component::add_param(std::string_view name, std::shared_ptr<Component> part) {
  if (auto* s = find<SlotBase>(name)) {
    if (!part) { s->clear(); on_slot_changed(name); return true; }
    if (!s->add(std::move(part))) return false;
    on_slot_changed(name);
    return true;
  }
  if (auto* l = find<InputList>(name)) {
    if (!part) { l->sources.clear(); on_input_list_changed(name); return true; }
    auto signal = std::dynamic_pointer_cast<ValueSource>(std::move(part));
    if (!signal) return false;                   // a part: the list keeps its feeds
    l->sources.push_back(std::move(signal));
    on_input_list_changed(name);
    return true;
  }
  return set_param(name, std::move(part));
}

} // namespace mforce
