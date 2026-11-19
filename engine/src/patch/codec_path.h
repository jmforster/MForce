#pragma once
#include <stdexcept>
#include <string>

namespace mforce {

// A reader's refusal: "<path>: <sentence>", the path relative to the level
// that throws it.
[[noreturn]] inline void refuse(const std::string& path, const std::string& sentence) {
  throw std::runtime_error(path + ": " + sentence);
}

// Runs `read`; a refusal it throws is rethrown with `step` in front of its
// path, so each level's step composes with the path below it
// ("graph" + "nodes[3]" + "params.cutoff"). A path below that opens with an
// index joins without a dot.
template <class Read>
void at_step(const std::string& step, Read&& read) {
  try {
    read();
  } catch (const std::runtime_error& e) {
    const std::string below = e.what();
    throw std::runtime_error(step + (below.rfind('[', 0) == 0 ? "" : ".") + below);
  }
}

} // namespace mforce
