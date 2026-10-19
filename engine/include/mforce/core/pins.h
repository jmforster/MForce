#pragma once
// The pin kinds a node declares its members with. Complete only through
// dsp_value_source.h, which defines the pin methods that need ValueSource;
// include that header, not this one.
#include <memory>
#include <type_traits>
#include <vector>
#include "mforce/core/descriptors.h"

namespace mforce {

struct Component;
struct ValueSource;
struct RenderContext;

enum class PinKind { Param, Input, Slot, Setting, Array };

// Who prepares and advances a connectable pin's source: the node's base,
// every sample in declaration order (Auto, the rule), or the node itself
// (Manual). A pin is Manual when the base's sweep cannot express how the
// node uses it: pulled only in one mode or only between gaps, pulled once
// per table entry at note start, read at a fixed point of its own
// compute(), or a trigger chain evaluated at note setup and never per
// sample. The node then calls the pin's prepare() and next() itself.
enum class Pull { Auto, Manual };

// A pin is a member of a node that registers itself with the node while
// the node is constructed, so the member declarations are the node's pin
// table: declaration order is registration order, which is the order the
// pins are advanced and listed. Each concrete pin attaches itself in its
// own constructor, once its fields are initialised. C++ has no reflection;
// a member that files itself with its owner is the stand-in.
struct PinBase {
  const char* name;
  PinKind kind;
  Pull pull;
  virtual ~PinBase() = default;
  PinBase(const PinBase&) = delete;               // a copy would not be registered
  // A connectable pin forwards both host calls to its source; a setting or
  // an array has nothing to forward.
  virtual void prepare(const RenderContext&) {}
  virtual void start(int) {}
protected:
  PinBase(const char* n, PinKind k, Pull p = Pull::Auto) : name(n), kind(k), pull(p) {}
  // A node assigning itself assigns its pins' values.
  PinBase& operator=(const PinBase&) = default;
  void attach(Component* owner);                  // component.h
};

// A connectable parameter: a source, or a constant when nothing is wired.
struct Param final : PinBase {
  static constexpr PinKind kKind = PinKind::Param;
  float constant, min, max;
  const char* hint;
  std::shared_ptr<ValueSource> source;            // null = the constant
  Param(Component* owner, const char* n, float def, float mn, float mx,
        const char* h = nullptr, Pull p = Pull::Auto)
    : PinBase(n, kKind, p), constant(def), min(mn), max(mx), hint(h) { attach(owner); }
  float current() const;                          // the source's value, or the constant
  float next();                                   // Manual pins only: the source's value this tick, or the constant
  float step();                                   // Manual pins only: one sample of the source now, or the constant
  void set(std::shared_ptr<ValueSource> s) { source = std::move(s); }
  std::shared_ptr<ValueSource> get() const { return source; }
  ParamDescriptor descriptor() const { return {name, constant, min, max, hint}; }
  void prepare(const RenderContext& ctx) override;
  void start(int frames) override;
};

// A signal input: a source, required or optional; reads 0 when unwired.
struct Input final : PinBase {
  static constexpr PinKind kKind = PinKind::Input;
  enum Required { RequiredInput, Optional };
  Required required;
  const char* hint;
  bool multi;
  std::shared_ptr<ValueSource> source;
  Input(Component* owner, const char* n, Required r = RequiredInput,
        const char* h = nullptr, bool m = false, Pull p = Pull::Auto)
    : PinBase(n, kKind, p), required(r), hint(h), multi(m) { attach(owner); }
  float current() const;
  float next();                                   // Manual pins only: the source's value this tick, or 0
  float step();                                   // Manual pins only: one sample of the source now, or 0
  void set(std::shared_ptr<ValueSource> s) { source = std::move(s); }
  std::shared_ptr<ValueSource> get() const { return source; }
  InputDescriptor descriptor() const { return {name, multi, hint}; }
  void prepare(const RenderContext& ctx) override;
  void start(int frames) override;
};

// A scalar configuration value (float, int or bool), not connectable. The
// base addresses every Setting<T> through SettingBase.
struct SettingBase : PinBase {
  static constexpr PinKind kKind = PinKind::Setting;
  virtual void set_from_float(float v) = 0;
  virtual float as_float() const = 0;
  virtual SettingDescriptor descriptor() const = 0;
protected:
  explicit SettingBase(const char* n) : PinBase(n, kKind) {}
};

template <class T>
struct Setting final : SettingBase {
  static_assert(std::is_same_v<T, float> || std::is_same_v<T, int> || std::is_same_v<T, bool>,
                "a Setting is a float, an int or a bool");
  T value, min, max;
  const char* const* labels;                      // enum labels for an int setting
  Setting(Component* owner, const char* n, T def, T mn, T mx, const char* const* lb = nullptr)
    : SettingBase(n), value(def), min(mn), max(mx), labels(lb), default_(def) { attach(owner); }
  SettingDescriptor descriptor() const override {
    constexpr SettingType t = std::is_same_v<T, bool> ? SettingType::Bool
                            : std::is_same_v<T, int>  ? SettingType::Int : SettingType::Float;
    return {name, t, float(default_), float(min), float(max), labels};
  }
  void set_from_float(float v) override { value = from_float(v); }
  float as_float() const override { return float(value); }
  // The base does not clamp (min/max are advisory); a node that enforces
  // its declared range calls this from on_setting_changed.
  void clamp() { value = value < min ? min : (value > max ? max : value); }
private:
  // bool: nonzero; int: truncated toward zero; float: as is.
  static T from_float(float v) {
    if constexpr (std::is_same_v<T, bool>) return v != 0.0f;
    else return T(v);
  }
  T default_;                                     // what the descriptor reports after a change
};

// A user-editable float vector.
struct Array final : PinBase {
  static constexpr PinKind kKind = PinKind::Array;
  std::vector<float> values;
  const char* group;
  float def, min, max;
  Array(Component* owner, const char* n, const char* g, float d, float mn, float mx)
    : PinBase(n, kKind), group(g), def(d), min(mn), max(mx) { attach(owner); }
  ArrayDescriptor descriptor() const { return {name, group, def, min, max}; }
};

// A structural input: a part that implements interface T (partials, a
// formant, an evolution). Not a value; the node calls T's methods. The wire
// is type-checked when set, which is the one place a dynamic_cast is kept.
struct SlotBase : PinBase {
  static constexpr PinKind kKind = PinKind::Slot;
  virtual InputDescriptor descriptor() const = 0;
  virtual bool set(std::shared_ptr<Component> part) = 0;
protected:
  explicit SlotBase(const char* n) : PinBase(n, kKind) {}
};

template <class T>
struct Slot final : SlotBase {
  std::vector<std::shared_ptr<Component>> parts;  // the owning handles
  std::vector<T*> views;                          // the same parts as T*
  const char* hint;
  bool multi;
  Slot(Component* owner, const char* n, const char* h = nullptr, bool m = false)
    : SlotBase(n), hint(h), multi(m) { attach(owner); }
  bool set(std::shared_ptr<Component> part) override;   // component.h: false if part is not a T
  void clear() { parts.clear(); views.clear(); }
  InputDescriptor descriptor() const override { return {name, multi, hint}; }
};

} // namespace mforce
