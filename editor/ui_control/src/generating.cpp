// Generate, Play, Stop: the open patch rendered as the transport's tab says,
// and the take played on the output's playback line.
#include "mforce/ui_control/generating.h"
#include <algorithm>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include "mforce/music/parse_util.h"
#include "mforce/patch/patch_json.h"
#include "mforce/patch/type_declarations.h"
#include "mforce/play/play.h"
#include "mforce/ui_control/session.h"
#include "mforce/ui_control/sounds.h"
#include "fault_line.h"

namespace mforce::ui {

namespace {

using Line = AudioAdapter::Line;
using Mode = Transport::Mode;

// Every signal node of the patch, by id, in the graph's order.
std::vector<std::string> signal_ids(const Patch& p) {
  std::vector<std::string> ids;
  for (const auto& n : p.graph.nodes)
    if (const TypeDeclarations* d = type_declarations(n.type); d && d->signal) ids.push_back(n.id);
  return ids;
}

// The render as the take, with the ids its strips are of.
void keep(Session& s, PlayResult r, std::vector<std::string> ids) {
  s.take.stereo = std::move(r.stereo);
  s.take.sampleRate = r.sampleRate;
  s.take.frames = r.frames;
  s.take.stripIds = std::move(ids);
  s.take.strips = std::move(r.strips);
  s.take.mode = s.transport.mode;
  if (std::string f = fault_line(r.faults); !f.empty()) s.status = std::move(f);
}

void generate_note(Session& s) {
  const Patch& p = s.open.patch;
  const float note = parse_note_input(s.transport.note.c_str());
  const TextPerformance t = perform_note(note, s.keyboard.velocity, s.keyboard.seconds);
  std::vector<std::string> ids = signal_ids(p);
  PlayResult r = play(p, t.performance, t.seconds, Level::Guarded, ids);
  s.status = "Generated note";
  s.lastNote = {int(note + 0.5f), s.keyboard.velocity};
  keep(s, std::move(r), std::move(ids));
  s.take.note = s.transport.note;
  s.take.velocity = s.keyboard.velocity;
  s.take.seconds = s.keyboard.seconds;
}

void generate_passage(Session& s) {
  const Patch& p = s.open.patch;
  const InstrumentClass cls{p.instrument->sustaining, p.instrument->onsets};
  const TextPerformance t = perform_passage(s.transport.passage, s.keyboard.octave, s.transport.bpm,
                                            s.keyboard.velocity, s.transport.onsets, cls);
  std::vector<std::string> ids = signal_ids(p);
  PlayResult r = play(p, t.performance, t.seconds, Level::Guarded, ids);
  s.status = t.notes.empty() ? "Generated " + std::to_string(t.performance.notes.size()) + " notes" : t.notes.front();
  keep(s, std::move(r), std::move(ids));
}

void generate_chords(Session& s) {
  const Transport& tr = s.transport;
  const TextPerformance t = perform_chords(tr.chords, s.keyboard.octave, tr.bpm, tr.chordGroup, tr.figure, tr.delayMs);
  const size_t chords =
      parse_chord_string(tr.chords, s.keyboard.octave, tr.chordGroup.empty() ? "Default" : tr.chordGroup, tr.figure).size();
  PlayResult r = play(s.open.patch, t.performance, t.seconds, Level::Normalised);
  s.status = "Generated " + std::to_string(chords) + " chords";
  keep(s, std::move(r), {});
}

// Each drum the map names and the pattern reaches, its patch read from disk.
std::vector<std::pair<int, Patch>> drum_patches(const std::string& map, const Performance& hits) {
  const auto paths = parse_drum_map(map);
  if (paths.empty())
    throw std::runtime_error("No drum patches specified — set Drums field (e.g. KK=patches/kick.json;SN=patches/snare.json)");
  int highest = 0;
  for (const auto& h : hits.hits) highest = std::max(highest, h.drumNumber);
  std::vector<std::pair<int, Patch>> drums;
  for (const auto& [number, path] : paths) {
    if (number > highest) continue;
    if (!std::filesystem::exists(path)) throw std::runtime_error("Drum patch not found: " + path);
    drums.emplace_back(number, load_patch(path));
  }
  if (drums.empty()) throw std::runtime_error("No drum patches could be loaded");
  return drums;
}

void generate_drums(Session& s) {
  const TextPerformance t = perform_drums(s.transport.drumPattern, s.transport.bpm);
  const auto drums = drum_patches(s.transport.drumMap, t.performance);
  PlayResult r = play_drums(drums, t.performance, t.seconds, s.open.patch.sampleRate);
  s.status = "Generated " + std::to_string(t.performance.hits.size()) + " hits";
  keep(s, std::move(r), {});
}

void generate_plain(Session& s) {
  PlayResult r = play(s.open.patch);
  char text[96];
  std::snprintf(text, sizeof text, "Generated the plain sound: %.1f s", double(r.frames) / double(r.sampleRate));
  s.status = text;
  keep(s, std::move(r), {});
}

// Why the open patch cannot be rendered; empty when it can.
std::string cannot_render(const Session& s) {
  const Patch& p = s.open.patch;
  if (p.graph.nodes.empty() && s.open.path.empty()) return "No patch loaded — open a patch first";
  if (p.graph.output.empty()) return "Output node has no source connected — wire a node into Output";
  return {};
}

bool note_take_stale(const Session& s) {
  const Take& t = s.take;
  return t.stereo.empty() || t.mode != Mode::Note || t.note != s.transport.note ||
         t.velocity != s.keyboard.velocity || t.seconds != s.keyboard.seconds;
}

void play_plain(Session& s) {
  try {
    s.audio.play(Line::Playback, std::make_unique<PlainSound>(s.open.patch, s.audio.sample_rate()));
    s.status = "Streaming the plain sound (Space or Stop to end)";
  } catch (const std::exception& e) {
    s.status = e.what();
  }
}

}  // namespace

void set_transport(Session& s, const Transport& t) { s.transport = t; }

int note_tab_number(const Session& s) {
  try {
    return int(parse_note_input(s.transport.note.c_str()));
  } catch (const std::exception&) {               // a number std::stof cannot hold
    return 60;
  }
}

void set_note_tab(Session& s, int pitchClass, int octave) {
  static constexpr const char* kPitch[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
  const int oct = std::clamp(octave, 0, 8);
  s.transport.note = kPitch[((pitchClass % 12) + 12) % 12] + std::to_string(oct);
  s.keyboard.octave = oct;
}

void request_generate(Session& s) { s.generating = 2; }

bool generate_requested(const Session& s) { return s.generating > 0; }

void run_requested(Session& s) {
  if (s.generating > 0 && --s.generating == 0) generate(s);
}

void generate(Session& s) {
  s.audio.play(Line::Playback, nullptr);
  s.take = Take{};
  const bool drums = s.transport.mode == Mode::Drums && s.open.patch.instrument;
  if (const std::string why = cannot_render(s); !why.empty() && !drums) {
    s.status = why;
    return;
  }
  try {
    if (!s.open.patch.instrument) generate_plain(s);
    else if (s.transport.mode == Mode::Note) generate_note(s);
    else if (s.transport.mode == Mode::Passage) generate_passage(s);
    else if (s.transport.mode == Mode::Chords) generate_chords(s);
    else generate_drums(s);
  } catch (const std::exception& e) {
    s.take = Take{};
    s.status = e.what();
  }
}

void play(Session& s) {
  stop(s);
  if (!s.open.patch.instrument && !s.open.patch.graph.nodes.empty()) {
    play_plain(s);
    return;
  }
  const Mode mode = s.transport.mode;
  if ((mode == Mode::Note && note_take_stale(s)) || (mode == Mode::Passage && s.take.stereo.empty()) ||
      mode == Mode::Chords || mode == Mode::Drums)
    generate(s);
  if (s.take.stereo.empty()) return;
  const int rate = s.audio.sample_rate();
  s.audio.play(Line::Playback, std::make_unique<TakeSound>(s.take, rate));
  if (s.take.sampleRate != rate)
    s.status += (s.status.empty() ? "" : "; ") + std::string("the render is at ") + std::to_string(s.take.sampleRate) +
                " Hz and plays here at " + std::to_string(rate) + " Hz, the output's rate";
}

void stop(Session& s) {
  s.audio.play(Line::Playback, nullptr);
  s.live.stop();
}

bool playing(Session& s) {
  Sound* playback = s.audio.playing(Line::Playback);
  return (playback && !playback->over()) || s.live.sounding() > 0;
}

}  // namespace mforce::ui
