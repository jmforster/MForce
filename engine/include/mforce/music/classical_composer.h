#pragma once
#include "mforce/music/compose.h"
#include "mforce/music/composer.h"
#include <string>

namespace mforce {

// ---------------------------------------------------------------------------
// ClassicalComposer — IComposer façade around the new Composer framework.
//
// Phase 1a: preserves the three-overload IComposer API the CLI uses at
// tools/mforce_cli/main.cpp so no caller needs to change. All the
// composition logic now lives in Composer + the three Default strategies.
// Phase 3+ will add more strategies; Phase 5 will rename Seed to Motif.
// ---------------------------------------------------------------------------
struct ClassicalComposer : IComposer {
  Composer inner;

  explicit ClassicalComposer(uint32_t seed = 0xC1A5'0001u) : inner(seed) {}

  std::string name() const override { return "Classical"; }

  // Top-level: let Composer walk the whole piece.
  void compose(Piece& piece, const PieceTemplate& tmpl) override {
    inner.compose(piece, tmpl);
  }

  // Per-part: walk sections and delegate each to the 4-arg overload.
  // Matches the pre-refactor loop shape exactly.
  void compose(Piece& piece, const PieceTemplate& tmpl,
               const std::string& partName) override {
    for (auto& sec : piece.sections) {
      compose(piece, tmpl, partName, sec.name);
    }
  }

  // Per-(part,section): delegate to Composer::compose_one_passage, which
  // looks up the PartTemplate by name and realizes the one passage. Note:
  // this overload assumes setup_piece_ has already run (via an earlier call
  // to the 2-arg compose() overload). Calling the narrower overloads on a
  // fresh Piece without first calling the 2-arg overload is undefined —
  // that matches the pre-refactor behavior, where the narrower overloads
  // also required a set-up Piece.
  void compose(Piece& piece, const PieceTemplate& tmpl,
               const std::string& partName,
               const std::string& sectionName) override {
    inner.compose_one_passage(piece, tmpl, partName, sectionName);
  }
};

} // namespace mforce
