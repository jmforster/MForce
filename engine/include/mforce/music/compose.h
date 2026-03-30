#pragma once
#include "mforce/music/structure.h"
#include <string>
#include <vector>
#include <memory>

namespace mforce {

// ---------------------------------------------------------------------------
// IComposer — interface for algorithmic composition.
// ---------------------------------------------------------------------------
struct IComposer {
  virtual ~IComposer() = default;
  virtual std::string name() const = 0;

  // Compose at different scopes
  virtual void compose(Piece& piece) = 0;
  virtual void compose(Piece& piece, int partIndex) { (void)piece; (void)partIndex; }
  virtual void compose(Piece& piece, int partIndex, int sectionIndex) {
    (void)piece; (void)partIndex; (void)sectionIndex;
  }
};

// ---------------------------------------------------------------------------
// Genre — a named collection of Composers.
// ---------------------------------------------------------------------------
struct Genre {
  std::string genreName;
  std::vector<std::shared_ptr<IComposer>> composers;
  int defaultComposer{0};

  void add_composer(std::shared_ptr<IComposer> c) { composers.push_back(std::move(c)); }

  IComposer* get_default() {
    return composers.empty() ? nullptr : composers[defaultComposer].get();
  }

  IComposer* get_composer(const std::string& name) {
    for (auto& c : composers)
      if (c->name() == name) return c.get();
    return nullptr;
  }
};

} // namespace mforce
