#include "mforce/play/play.h"
#include "mforce/music/embedded_score.h"
#include "mforce/music/time_map.h"
#include "mforce/render/build_instrument.h"
#include "mforce/render/build_sound.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <memory>
#include <stdexcept>

namespace mforce {

namespace {

using Clock = std::chrono::steady_clock;

double ms_between(Clock::time_point a, Clock::time_point b) {
  return std::chrono::duration<double, std::milli>(b - a).count();
}

// The peak and rms of the result's stereo frames.
void measure(PlayResult& r) {
  float peak = 0.0f;
  double rms = 0.0;
  for (float s : r.stereo) {
    const float a = std::fabs(s);
    if (a > peak) peak = a;
    rms += double(s) * double(s);
  }
  r.peak = peak;
  r.rms = std::sqrt(rms / r.stereo.size());
}

// Each part with events, performed once and played on its instrument; the
// distinct instruments in the order they first appear over the parts.
std::vector<Instrument*> perform_parts(const Piece& piece, const Ensemble& ensemble,
                                       Conductor& conductor, const TimeMap& time) {
  std::vector<Instrument*> instruments;
  for (const auto& part : piece.parts) {
    const auto it = ensemble.byPart.find(part.instrumentType);
    if (it == ensemble.byPart.end() || !it->second) continue;
    Instrument* inst = it->second;
    if (std::find(instruments.begin(), instruments.end(), inst) == instruments.end())
      instruments.push_back(inst);
    if (!part.elementSequence.empty()) inst->play(conductor.perform(part, time));
  }
  return instruments;
}

// Each instrument rendered into its own buffer, its faults collected, the
// buffers summed; a sum of more than one instrument past 0.98 scaled to it.
std::vector<float> mix(const std::vector<Instrument*>& instruments, PlayResult& r) {
  const int frames = r.frames;
  std::vector<float> mono(frames, 0.0f);
  std::vector<float> buf(frames);
  for (auto* inst : instruments) {
    std::fill(buf.begin(), buf.end(), 0.0f);
    inst->render(buf.data(), frames);
    inst->collect_faults(r.faults);
    inst->clear();
    for (int k = 0; k < frames; ++k) mono[k] += buf[k];
  }
  for (int k = 0; k < frames; ++k) r.mixPeak = std::max(r.mixPeak, std::fabs(mono[k]));
  if (instruments.size() > 1 && r.mixPeak > 0.98f) {
    r.scale = 0.98f / r.mixPeak;
    for (int k = 0; k < frames; ++k) mono[k] *= r.scale;
  }
  return mono;
}

} // namespace

PlayResult play(const Piece& piece, const Ensemble& ensemble, Conductor& conductor, float tailSeconds) {
  const auto t0 = Clock::now();
  const TimeMap time(piece.sections);
  const std::vector<Instrument*> instruments = perform_parts(piece, ensemble, conductor, time);
  if (instruments.empty()) throw std::invalid_argument("play: no part of the piece has an instrument");

  PlayResult r;
  r.sampleRate = instruments.front()->render_context().sampleRate;
  float totalBeats = 0.0f;
  for (const auto& s : piece.sections) totalBeats += s.beats;
  float totalSeconds = time.seconds_at(totalBeats) + tailSeconds;
  r.frames = int(totalSeconds * float(r.sampleRate));

  const auto t1 = Clock::now();
  const std::vector<float> mono = mix(instruments, r);
  r.stereo.resize(size_t(r.frames) * 2);
  for (int j = 0; j < r.frames; ++j) {
    r.stereo[j * 2]     = mono[j];
    r.stereo[j * 2 + 1] = mono[j];
  }
  const auto t2 = Clock::now();
  measure(r);
  r.buildMs = ms_between(t0, t1);
  r.renderMs = ms_between(t1, t2);
  return r;
}

PlayResult play(const Patch& patch) {
  const auto t0 = Clock::now();
  PlayResult r;
  r.sampleRate = patch.sampleRate;
  std::unique_ptr<Mixer> mixer;
  if (!patch.instrument) {
    BuiltSound sound = build_sound(patch);
    r.warnings = std::move(sound.warnings);
    r.frames = sound.frames;
    mixer = std::move(sound.mixer);
  } else {
    auto inst = build_instrument(patch, 0, &r.warnings);
    r.frames = sound_frames(patch);
    if (!patch.score.empty()) {
      EmbeddedScore score = perform_embedded_score(patch.score, inst->instrument_class());
      r.warnings.insert(r.warnings.end(), score.warnings.begin(), score.warnings.end());
      inst->play(score.performance);
      const int scoreFrames = int(std::lround(score.endSeconds * patch.sampleRate));
      r.frames = patch.seconds ? std::max(r.frames, scoreFrames) : scoreFrames;
    }
    mixer = instrument_mixer(std::move(inst), patch.sampleRate);
  }

  const auto t1 = Clock::now();
  r.stereo.resize(size_t(r.frames) * 2);
  mixer->render(r.stereo.data(), r.frames);
  const auto t2 = Clock::now();
  mixer->collect_faults(r.faults);
  measure(r);
  r.buildMs = ms_between(t0, t1);
  r.renderMs = ms_between(t1, t2);
  return r;
}

} // namespace mforce
