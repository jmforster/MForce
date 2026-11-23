#include "mforce/compose/register_builtin.h"
#include "mforce/compose/default_figure_strategy.h"
#include "mforce/compose/default_passage_strategy.h"
#include "mforce/compose/default_phrase_strategy.h"
#include "mforce/compose/phrase_aware_figure_strategy.h"
#include "mforce/compose/realization_strategy_json.h"
#include "mforce/compose/register_strategy.h"
#include "mforce/compose/shape_strategies.h"
#include "mforce/compose/templates_json.h"
#include "mforce/compose/wrapper_phrase_strategy.h"
#include "mforce/voicing/drift_voicing_profile_selector.h"
#include "mforce/voicing/random_voicing_profile_selector.h"
#include "mforce/voicing/scripted_voicing_profile_selector.h"
#include "mforce/voicing/smooth_voicing_selector.h"
#include "mforce/voicing/static_voicing_profile_selector.h"
#include "mforce/voicing/voicing_json.h"
#include <memory>

namespace mforce {

namespace {

// A voicing profile selector: its factory turns the settings a passage
// gives it into its typed constructor argument.
template <class Selector>
void add_profile_selector(ComposeKit& kit, SettingsCodecs& codecs) {
  kit.voicings.add_profile_selector(Selector::kName,
      [](const VoicingProfile& baseline, const std::any& settings)
          -> std::unique_ptr<VoicingProfileSelector> {
        using S = typename Selector::Settings;
        if constexpr (std::is_same_v<S, NoSettings>) {
          // Settings of another type given to it in code throw here.
          (void)settings_of<S>(StrategyChoice{Selector::kName, settings});
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
  register_strategy<DefaultPhraseStrategy>(kit, codecs);
  register_strategy<DefaultPassageStrategy>(kit, codecs);

  // Shape strategies: the default figure strategy dispatches to these by
  // name for the three shapes with logic of their own.
  reg.add(std::make_unique<ShapeCadentialApproachStrategy>());
  reg.add(std::make_unique<ShapeSkippingStrategy>());
  reg.add(std::make_unique<ShapeSteppingStrategy>());

  register_strategy<WrapperPhraseStrategy>(kit, codecs);

  kit.voicings.add(std::make_unique<SmoothVoicingSelector>());

  add_profile_selector<StaticVoicingProfileSelector>(kit, codecs);
  add_profile_selector<RandomVoicingProfileSelector>(kit, codecs);
  add_profile_selector<DriftVoicingProfileSelector>(kit, codecs);
  add_profile_selector<ScriptedVoicingProfileSelector>(kit, codecs);

  register_strategy<PhraseAwareFigureStrategy>(kit, codecs);

  register_strategy<BlockRealizationStrategy>(kit, codecs);
  register_strategy<RhythmPatternRealizationStrategy>(kit, codecs);
}

} // namespace mforce
