#include "mforce/music/compose.h"
#include "mforce/music/composer.h"

namespace mforce {

Composition compose(const PieceTemplate& request, const ComposeKit& kit) {
  Composition result;
  result.plan = request;
  Composer composer(request.masterSeed, kit);
  composer.compose(result.piece, result.plan);
  return result;
}

} // namespace mforce
