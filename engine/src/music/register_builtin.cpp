#include "mforce/music/register_builtin.h"
#include "mforce/music/composer.h"
#include "mforce/music/elaborated_phrase_strategy_json.h"
#include "mforce/music/library_passage_strategy_json.h"
#include "mforce/music/passage_strategies_json.h"
#include "mforce/music/period_passage_strategy_json.h"
#include "mforce/music/phrase_strategies_json.h"
#include "mforce/music/realization_strategy_json.h"
#include "mforce/music/templates_json.h"
#include "mforce/music/two_figure_phrase_strategy_json.h"
#include "mforce/music/voicing_json.h"
#include <memory>

namespace mforce {

namespace {

// A strategy a template can name: in the kit and in the codecs.
template <class Strategy>
void add_named(ComposeKit& kit, SettingsCodecs& codecs) {
  kit.strategies.add(std::make_unique<Strategy>());
  codecs.add<Strategy>();
}

// A voicing profile selector: its factory turns the settings a passage
// gives it into its typed constructor argument.
template <class Selector>
void add_profile_selector(ComposeKit& kit, SettingsCodecs& codecs) {
  kit.voicings.add_profile_selector(Selector::kName,
      [](const VoicingProfile& baseline, const std::any& settings)
          -> std::unique_ptr<VoicingProfileSelector> {
        using S = typename Selector::Settings;
        if constexpr (std::is_same_v<S, NoSettings>) {
          return std::make_unique<Selector>(baseline);
        } else {
          return std::make_unique<Selector>(
              settings_of<S>(StrategyChoice{Selector::kName, settings}));
        }
      });
  codecs.add<Selector>();
}

} // namespace

void register_builtin(ComposeKit& kit, SettingsCodecs& codecs) {
  StrategyRegistry& reg = kit.strategies;
  reg.add(std::make_unique<DefaultFigureStrategy>());
  add_named<DefaultPhraseStrategy>(kit, codecs);
  add_named<DefaultPassageStrategy>(kit, codecs);

  // Shape strategies: the default figure strategy dispatches to these by
  // name for the three shapes with logic of their own.
  reg.add(std::make_unique<ShapeCadentialApproachStrategy>());
  reg.add(std::make_unique<ShapeSkippingStrategy>());
  reg.add(std::make_unique<ShapeSteppingStrategy>());

  add_named<PeriodPhraseStrategy>(kit, codecs);
  add_named<SentencePhraseStrategy>(kit, codecs);
  add_named<WrapperPhraseStrategy>(kit, codecs);
  add_named<TwoFigurePhraseStrategy>(kit, codecs);
  add_named<ElaboratedPhraseStrategy>(kit, codecs);

  kit.voicings.add(std::make_unique<SmoothVoicingSelector>());

  add_profile_selector<StaticVoicingProfileSelector>(kit, codecs);
  add_profile_selector<RandomVoicingProfileSelector>(kit, codecs);
  add_profile_selector<DriftVoicingProfileSelector>(kit, codecs);
  add_profile_selector<ScriptedVoicingProfileSelector>(kit, codecs);

  add_named<AlternatingFigureStrategy>(kit, codecs);
  add_named<PhraseAwareFigureStrategy>(kit, codecs);
  add_named<PeriodPassageStrategy>(kit, codecs);
  add_named<LibraryPassageStrategy>(kit, codecs);
  add_named<PedalBuildupStrategy>(kit, codecs);
  add_named<SequencePassageStrategy>(kit, codecs);
  add_named<ConnectivePassageStrategy>(kit, codecs);
  add_named<WanderingPassageStrategy>(kit, codecs);

  add_named<BlockRealizationStrategy>(kit, codecs);
  add_named<RhythmPatternRealizationStrategy>(kit, codecs);
}

} // namespace mforce
