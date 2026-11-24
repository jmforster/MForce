#include "mforce/perform/text_performance.h"
#include "mforce/music/parse_util.h"
#include "mforce/music/structure.h"
#include "mforce/perform/conductor.h"
#include "mforce/perform/time_map.h"
#include <algorithm>
#include <stdexcept>
#include <string>

namespace mforce {

namespace {

// A passage note on its way to the performance: its onset as a name until
// the instrument's vocabulary turns it into an id.
struct Scheduled {
  float noteNumber;
  float duration;
  float start;
  bool hold;
  std::string onset;
};

// The notes in sequence at a cursor that rests advance too. A note holds
// unless it ends its phrase: the next element is a rest, begins a phrase, or
// there is none (parse_passage begins a phrase at every note of a passage
// with no '|', and after every rest).
std::vector<Scheduled> schedule(const std::vector<ParsedNote>& parsed) {
  std::vector<Scheduled> out;
  float cursor = 0.0f;
  for (size_t i = 0; i < parsed.size(); ++i) {
    const ParsedNote& pn = parsed[i];
    if (pn.noteNumber != kRestNote) {
      const bool lastOfPhrase = i + 1 >= parsed.size() || parsed[i + 1].noteNumber == kRestNote ||
                                parsed[i + 1].phraseStart;
      out.push_back({pn.noteNumber, pn.durationSeconds, cursor, !lastOfPhrase, {}});
    }
    cursor += pn.durationSeconds;
  }
  return out;
}

// The phrase marks made into onsets and holds. All breath, or an instrument
// that does not sustain: every note plain (the second is told). Otherwise a
// phrase's first note is a breath, and the others are tongued or slurred by
// the mode, or in Auto tongued on a repeated pitch and slurred on a new one.
void stamp(std::vector<Scheduled>& notes, bool sustaining, Onsets onsets, std::vector<std::string>& told) {
  if (onsets == Onsets::AllBreath || !sustaining) {
    for (auto& n : notes) { n.onset.clear(); n.hold = false; }
    if (onsets != Onsets::AllBreath) told.push_back("patch is not marked sustaining — phrase marks ignored");
    return;
  }
  for (size_t i = 0; i < notes.size(); ++i) {
    const bool firstOfPhrase = i == 0 || !notes[i - 1].hold;
    notes[i].onset = firstOfPhrase                 ? "breath"
                   : onsets == Onsets::AllTongue   ? "tongue"
                   : onsets == Onsets::AllSlur     ? "slur"
                   : notes[i - 1].noteNumber == notes[i].noteNumber ? "tongue"
                                                                    : "slur";
  }
}

// The latest end of any note of the performance.
double last_end(const Performance& p) {
  double end = 0.0;
  for (const auto& n : p.notes) end = std::max(end, double(n.start) + double(n.duration));
  return end;
}

}  // namespace

TextPerformance perform_note(float noteNumber, float velocity, float seconds) {
  TextPerformance t;
  t.performance.notes.push_back({.start = 0.0f, .duration = seconds, .noteNumber = noteNumber, .velocity = velocity});
  t.seconds = double(seconds) + double(kVoiceTailSeconds);
  return t;
}

TextPerformance perform_passage(std::string_view text, int octave, float bpm, float velocity,
                                Onsets onsets, const InstrumentClass& cls) {
  const std::string source(text);
  const std::vector<ParsedNote> parsed = parse_passage(source.c_str(), octave, bpm);
  if (parsed.empty()) throw std::runtime_error("No notes parsed from passage string");
  std::vector<Scheduled> notes = schedule(parsed);
  TextPerformance t;
  if (source.find('|') != std::string::npos) stamp(notes, cls.sustaining, onsets, t.notes);
  if (notes.empty()) throw std::runtime_error("Passage contains only rests");
  for (const auto& n : notes)
    t.performance.notes.push_back({.start = n.start, .duration = n.duration, .noteNumber = n.noteNumber,
                                   .velocity = velocity, .onset = cls.onset_id(n.onset),
                                   .hold = n.hold && cls.sustaining});
  t.seconds = last_end(t.performance) + double(kVoiceTailSeconds);
  return t;
}

TextPerformance perform_chords(std::string_view text, int octave, float bpm, std::string_view group,
                               std::string_view figure, float delayMs) {
  const std::string dictionary = group.empty() ? std::string("Default") : std::string(group);
  const auto chords = parse_chord_string(std::string(text), octave, dictionary, std::string(figure));
  if (chords.empty()) throw std::runtime_error("No chords parsed from string");
  Part part;
  part.name = "chords";
  for (const auto& pc : chords) part.add_chord(pc.chord);
  Conductor conductor;
  conductor.chordPerformer.defaultSpreadMs = delayMs;
  conductor.chordPerformer.register_josie_figures();
  TextPerformance t;
  t.performance = conductor.perform(part, TimeMap(bpm));
  t.seconds = double(part.totalBeats()) * 60.0 / double(bpm) + 1.0;
  return t;
}

TextPerformance perform_drums(std::string_view pattern, float bpm) {
  const ParsedDrumPattern pat = parse_drum_pattern(std::string(pattern).c_str());
  if (pat.figure.hits.empty()) throw std::runtime_error("No hits parsed from drum pattern");
  TextPerformance t;
  for (int rep = 0; rep < pat.repeats; ++rep) {
    const float repOffsetBeats = float(rep) * pat.figure.totalTime;
    for (const auto& hit : pat.figure.hits) {
      const float start = (repOffsetBeats + hit.time) * 60.0f / bpm;
      const float length = std::max(0.02f, (hit.duration > 0.0f ? hit.duration : 0.25f) * 60.0f / bpm);
      t.performance.hits.push_back({.start = start, .duration = length, .drumNumber = hit.drumNumber,
                                    .velocity = hit.velocity});
    }
  }
  t.seconds = double(pat.figure.totalTime) * 60.0 / double(bpm) * double(pat.repeats) + 1.0;
  return t;
}

}  // namespace mforce
