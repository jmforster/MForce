#pragma once
#include "mforce/music/structure.h"
#include "mforce/music/templates.h"
#include <string>
#include <vector>
#include <memory>

namespace mforce {

// ---------------------------------------------------------------------------
// IComposer — interface for algorithmic composition.
// Piece is always passed for full context. PieceTemplate is the recipe.
// Narrower overloads compose only the specified scope.
// ---------------------------------------------------------------------------
struct IComposer {
  virtual ~IComposer() = default;
  virtual std::string name() const = 0;

  // Compose everything
  virtual void compose(Piece& piece, const PieceTemplate& tmpl) = 0;

  // Compose all sections for one part
  virtual void compose(Piece& piece, const PieceTemplate& tmpl,
                       const std::string& partName) = 0;

  // Compose one passage (one part + one section)
  virtual void compose(Piece& piece, const PieceTemplate& tmpl,
                       const std::string& partName,
                       const std::string& sectionName) = 0;
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
