#pragma once
#include <cstddef>
#include <cstdlib>

namespace mforce {

// True when the process environment holds a variable of this name, whatever
// its value. The one place the engine reads the environment.
//
// MSVC's C library marks std::getenv unsafe (its pointer can be invalidated
// by a later change to the environment) and offers getenv_s, which asked for
// no buffer reports the size the value would need: 0 when the variable is
// not set. A library without the checked form uses std::getenv.
inline bool environment_variable_is_set(const char* name) {
#if defined(_MSC_VER)
  std::size_t required = 0;
  getenv_s(&required, nullptr, 0, name);
  return required != 0;
#else
  return std::getenv(name) != nullptr;
#endif
}

} // namespace mforce
