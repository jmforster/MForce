#pragma once
#include "mforce/core/randomizer.h"
#include <cstdint>

namespace mforce::rng {

namespace detail {
  inline thread_local Randomizer* current = nullptr;
}

inline uint32_t next() { return detail::current->rng(); }
inline float next_float() { return detail::current->value(); }

// RAII guard. Installs an existing Randomizer as the current thread-local
// RNG for the lifetime of the Scope. Takes the Randomizer by reference
// (non-owning) so Composer's rng_ member remains the authoritative state
// holder — this preserves draw-sequence determinism across the refactor.
class Scope {
public:
  explicit Scope(Randomizer& r) : previous_(detail::current) {
    detail::current = &r;
  }
  ~Scope() { detail::current = previous_; }

  Scope(const Scope&) = delete;
  Scope& operator=(const Scope&) = delete;

private:
  Randomizer* previous_;
};

} // namespace mforce::rng
