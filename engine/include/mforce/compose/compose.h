#pragma once
#include "mforce/compose/strategy_registry.h"
#include "mforce/music/structure.h"
#include "mforce/compose/templates.h"
#include "mforce/voicing/voicing_registry.h"

namespace mforce {

// What a host needs to compose: the strategies and the voicings it has
// registered. A host builds one (register_builtin and register_shelved
// fill it) and passes it to every compose call.
struct ComposeKit {
  StrategyRegistry strategies;
  VoicingRegistry  voicings;
};

// The result of composing a request.
struct Composition {
  Piece piece;
  PieceTemplate plan;   // the request as the strategies planned it: motifs
                        // realized and added, each passage as its
                        // strategy's plan step left it
};

// Composes a piece from a request. The request is copied into the plan
// first and never changed. The seed is the request's masterSeed.
Composition compose(const PieceTemplate& request, const ComposeKit& kit);

// Realizes the template's motif declarations into its realizedMotifs /
// realizedRhythms / realizedContours maps, drawing generation seeds from
// `rng` where a motif has none. compose calls it on the plan.
void realize_motifs(PieceTemplate& tmpl, Randomizer& rng);

} // namespace mforce
