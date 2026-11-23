#include "mforce/shelved/register_shelved.h"
#include "mforce/compose/register_strategy.h"
#include "mforce/compose/templates_json.h"
#include "mforce/shelved/alternating_figure_strategy.h"
#include "mforce/shelved/connective_passage_strategy_json.h"
#include "mforce/shelved/elaborated_phrase_strategy_json.h"
#include "mforce/shelved/library_passage_strategy_json.h"
#include "mforce/shelved/pedal_buildup_strategy_json.h"
#include "mforce/shelved/period_passage_strategy_json.h"
#include "mforce/shelved/period_phrase_strategy_json.h"
#include "mforce/shelved/sentence_phrase_strategy_json.h"
#include "mforce/shelved/sequence_passage_strategy_json.h"
#include "mforce/shelved/two_figure_phrase_strategy_json.h"
#include "mforce/shelved/wandering_passage_strategy_json.h"

namespace mforce {

void register_shelved(ComposeKit& kit, SettingsCodecs& codecs) {
  register_strategy<PeriodPhraseStrategy>(kit, codecs);
  register_strategy<SentencePhraseStrategy>(kit, codecs);
  register_strategy<TwoFigurePhraseStrategy>(kit, codecs);
  register_strategy<ElaboratedPhraseStrategy>(kit, codecs);

  register_strategy<AlternatingFigureStrategy>(kit, codecs);
  register_strategy_holding_templates<PeriodPassageStrategy>(kit, codecs);
  register_strategy<LibraryPassageStrategy>(kit, codecs);
  register_strategy<PedalBuildupStrategy>(kit, codecs);
  register_strategy<SequencePassageStrategy>(kit, codecs);
  register_strategy<ConnectivePassageStrategy>(kit, codecs);
  register_strategy<WanderingPassageStrategy>(kit, codecs);
}

} // namespace mforce
