#include "mforce/core/dsp_value_source.h"

namespace mforce {

// The wiring door (component.h), defined where ValueSource is a complete
// type: a Param or Input takes a signal and refuses a part; a Slot takes its
// interface and refuses the rest (Slot<T>::set); a null part clears the pin.
// The one place a wire is type-checked.
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
  if (auto* s = find<SlotBase>(name)) {
    if (!part) { s->clear(); on_slot_changed(name); return true; }
    if (!s->set(std::move(part))) return false;
    on_slot_changed(name);
    return true;
  }
  return false;
}

} // namespace mforce
