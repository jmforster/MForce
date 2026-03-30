#pragma once
#include <memory>

namespace mforce {

struct ValueSource {
  virtual ~ValueSource() = default;
  virtual void prepare(int /*frames*/) {}
  virtual float next() = 0;
  virtual float current() const = 0;
};

struct ConstantSource final : ValueSource {
  explicit ConstantSource(float v) : v_(v), cur_(v) {}
  void set(float v) { v_ = v; }
  float next() override { cur_ = v_; return cur_; }
  float current() const override { return cur_; }
private:
  float v_{0.0f};
  float cur_{0.0f};
};

} // namespace mforce
