#include "mforce/music/register_builtin.h"
#include "mforce/music/composer.h"
#include <memory>

namespace mforce {

namespace {

template <class Selector>
VoicingRegistry::ProfileSelectorFactory profile_factory() {
  return []() -> std::unique_ptr<VoicingProfileSelector> {
    return std::make_unique<Selector>();
  };
}

} // namespace

void register_builtin(ComposeKit& kit) {
  StrategyRegistry& reg = kit.strategies;
  reg.add(std::make_unique<DefaultFigureStrategy>());
  reg.add(std::make_unique<DefaultPhraseStrategy>());
  reg.add(std::make_unique<DefaultPassageStrategy>());

  // Shape strategies: the default figure strategy dispatches to these by
  // name for the three shapes with logic of their own.
  reg.add(std::make_unique<ShapeCadentialApproachStrategy>());
  reg.add(std::make_unique<ShapeSkippingStrategy>());
  reg.add(std::make_unique<ShapeSteppingStrategy>());

  reg.add(std::make_unique<PeriodPhraseStrategy>());
  reg.add(std::make_unique<SentencePhraseStrategy>());
  reg.add(std::make_unique<WrapperPhraseStrategy>());
  reg.add(std::make_unique<TwoFigurePhraseStrategy>());
  reg.add(std::make_unique<ElaboratedPhraseStrategy>());

  kit.voicings.add(std::make_unique<SmoothVoicingSelector>());

  kit.voicings.add_profile_selector(
      "static", profile_factory<StaticVoicingProfileSelector>());
  kit.voicings.add_profile_selector(
      "random", profile_factory<RandomVoicingProfileSelector>());
  kit.voicings.add_profile_selector(
      "drift", profile_factory<DriftVoicingProfileSelector>());
  kit.voicings.add_profile_selector(
      "scripted", profile_factory<ScriptedVoicingProfileSelector>());

  reg.add(std::make_unique<AlternatingFigureStrategy>());
  reg.add(std::make_unique<PhraseAwareFigureStrategy>());
  reg.add(std::make_unique<PeriodPassageStrategy>());
  reg.add(std::make_unique<LibraryPassageStrategy>());
  reg.add(std::make_unique<PedalBuildupStrategy>());
  reg.add(std::make_unique<SequencePassageStrategy>());
  reg.add(std::make_unique<ConnectivePassageStrategy>());
  reg.add(std::make_unique<WanderingPassageStrategy>());

  reg.add(std::make_unique<BlockRealizationStrategy>());
  reg.add(std::make_unique<RhythmPatternRealizationStrategy>());
}

} // namespace mforce
